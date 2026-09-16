// MaxCore — Vertex optimizer implementation
#include "vertex_optimizer.h"

#include <unordered_map>
#include <cmath>
#include <functional>

namespace core {

namespace {

// Reforged HD geosets carry more than one UVAS channel: channel 0 is the
// texture-atlas unwrap, channel 1 a second, non-overlapping unwrap (lightmap /
// AO bake). Signing only UV set 0 welded vertices that differ purely in the
// second unwrap — 66 of 5429 on Reforged's townhall.mdx — and whichever vertex
// won silently took its UV1 with it, tearing map channel 2 and re-exporting
// that damage back into the file. Every populated set takes part in the
// signature.
struct VertexSignature {
    int32_t px, py, pz;   // Quantized position
    int32_t nx, ny, nz;   // Quantized normal
    int32_t uvCount;      // Number of populated UV sets
    std::array<int32_t, ir::kMaxUVSets * 2> uv;  // Quantized UV sets, u/v interleaved

    bool operator==(const VertexSignature& o) const {
        return px == o.px && py == o.py && pz == o.pz &&
               nx == o.nx && ny == o.ny && nz == o.nz &&
               uvCount == o.uvCount && uv == o.uv;
    }
};

struct VertexSigHash {
    size_t operator()(const VertexSignature& s) const {
        size_t h = 0;
        auto combine = [&h](int32_t v) {
            h ^= std::hash<int32_t>{}(v) + 0x9e3779b9 + (h << 6) + (h >> 2);
        };
        combine(s.px); combine(s.py); combine(s.pz);
        combine(s.nx); combine(s.ny); combine(s.nz);
        combine(s.uvCount);
        for (int32_t q : s.uv) combine(q);
        return h;
    }
};

int32_t quantize(float v, float invThreshold) {
    return static_cast<int32_t>(std::floor(v * invThreshold + 0.5f));
}

} // anonymous namespace

void VertexOptimizer::optimize(ir::Mesh& mesh, float threshold) {
    if (mesh.vertices.empty()) return;

    float invThreshold = 1.0f / threshold;
    std::unordered_map<VertexSignature, uint32_t, VertexSigHash> sigMap;
    std::vector<ir::Vertex> newVertices;
    std::vector<uint32_t> remap(mesh.vertices.size());

    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto& v = mesh.vertices[i];
        VertexSignature sig;
        sig.px = quantize(v.position.x, invThreshold);
        sig.py = quantize(v.position.y, invThreshold);
        sig.pz = quantize(v.position.z, invThreshold);
        sig.nx = quantize(v.normal.x, invThreshold * 10.0f);
        sig.ny = quantize(v.normal.y, invThreshold * 10.0f);
        sig.nz = quantize(v.normal.z, invThreshold * 10.0f);
        sig.uvCount = v.uvSetCount;
        sig.uv.fill(0);
        for (int32_t s = 0; s < v.uvSetCount && s < ir::kMaxUVSets; ++s) {
            sig.uv[s * 2]     = quantize(v.uvSets[s].x, invThreshold * 100.0f);
            sig.uv[s * 2 + 1] = quantize(v.uvSets[s].y, invThreshold * 100.0f);
        }

        auto it = sigMap.find(sig);
        if (it != sigMap.end()) {
            // Verify detailed equality (skin influences must also match)
            const auto& existing = newVertices[it->second];
            bool skinMatch = (v.skinInfluences.size() == existing.skinInfluences.size());
            if (skinMatch) {
                for (size_t s = 0; s < v.skinInfluences.size() && skinMatch; ++s) {
                    if (v.skinInfluences[s].boneIndex != existing.skinInfluences[s].boneIndex ||
                        std::fabs(v.skinInfluences[s].weight - existing.skinInfluences[s].weight) > 0.001f) {
                        skinMatch = false;
                    }
                }
            }
            if (skinMatch) {
                remap[i] = it->second;
                continue;
            }
        }

        uint32_t newIdx = static_cast<uint32_t>(newVertices.size());
        sigMap[sig] = newIdx;
        remap[i] = newIdx;
        newVertices.push_back(v);
    }

    // Remap indices
    for (auto& idx : mesh.indices) {
        idx = remap[idx];
    }

    mesh.vertices = std::move(newVertices);
}

} // namespace core
