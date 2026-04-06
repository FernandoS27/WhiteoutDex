// MaxCore — Vertex optimizer implementation
#include "vertex_optimizer.h"

#include <unordered_map>
#include <cmath>
#include <functional>

namespace core {

namespace {

struct VertexSignature {
    int32_t px, py, pz;   // Quantized position
    int32_t nx, ny, nz;   // Quantized normal
    int32_t u0, v0;       // Quantized UV set 0

    bool operator==(const VertexSignature& o) const {
        return px == o.px && py == o.py && pz == o.pz &&
               nx == o.nx && ny == o.ny && nz == o.nz &&
               u0 == o.u0 && v0 == o.v0;
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
        combine(s.u0); combine(s.v0);
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
        sig.u0 = (v.uvSetCount > 0) ? quantize(v.uvSets[0].x, invThreshold * 100.0f) : 0;
        sig.v0 = (v.uvSetCount > 0) ? quantize(v.uvSets[0].y, invThreshold * 100.0f) : 0;

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
