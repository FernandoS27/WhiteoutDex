// MDLXExporter — MDX model builder implementation
#include "mdx_model_builder.h"
#include "mdx_hierarchy_resolver.h"
#include "mdx_material_mapper.h"
#include "mdx_geoset_merger.h"
#include "mdx_extent_calculator.h"
#include "mdx_skin_quantizer.h"
#include "mdx_coord_transform.h"
#include "../extraction/wc3_facefx_extractor.h"

#include <cstring>
#include <map>
#include <set>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <windows.h>

namespace wdx = whiteout::mdx;
using whiteout::f32;
using whiteout::u32;
using whiteout::Vector3f;
using whiteout::Vector2f;
using whiteout::Vector4f;
using whiteout::Quaternion;
using wdx::Model;
using wdx::Node;
using wdx::Track;
using wdx::InterpolationType;
using wdx::Geoset;
using wdx::Bone;
using wdx::Helper;
using wdx::Attachment;
using wdx::ParticleEmitter;
using wdx::ParticleEmitter2;
using wdx::CornEmitter;
using wdx::RibbonEmitter;
using wdx::EventObject;
using wdx::CollisionShape;
using wdx::GeosetAnimation;
using wdx::TextureAnimation;
using wdx::Texture;
using wdx::Sequence;
using wdx::Extent;
using wdx::FaceEffect;
// Note: Light and Camera clash with Max SDK globals; use wdx:: prefix

namespace {

// ── Track conversion helpers ─────────────────────────────────

template <typename MdxT, typename IrT>
Track<MdxT> convertTrack(const ir::Track<IrT>& irTrack,
                          MdxT (*valueFn)(const IrT&))
{
    Track<MdxT> out;
    if (irTrack.empty()) return out;

    out.isUsed = true;
    out.interpolationType = static_cast<InterpolationType>(
        static_cast<int>(irTrack.interpolation));
    out.globalSequenceId = (irTrack.globalSequenceIndex >= 0)
                               ? static_cast<uint32_t>(irTrack.globalSequenceIndex)
                               : Track<MdxT>::kNoGlobalSequence;
    out.keyCount = irTrack.keys.size();

    bool hasTangents = (irTrack.interpolation == ir::InterpolationType::Hermite ||
                        irTrack.interpolation == ir::InterpolationType::Bezier);

    out.timestamps.resize(irTrack.keys.size());
    if (hasTangents) {
        using TK = typename Track<MdxT>::TangentKey;
        out.keys_data.resize(irTrack.keys.size() * sizeof(TK) / sizeof(MdxT));
        auto tangentKeys = out.tangentKeys();
        for (size_t k = 0; k < irTrack.keys.size(); k++) {
            out.timestamps[k] = mdx_transform::ticksToMs(irTrack.keys[k].time);
            tangentKeys[k].value = valueFn(irTrack.keys[k].value);
            tangentKeys[k].inTan = valueFn(irTrack.keys[k].inTangent);
            tangentKeys[k].outTan = valueFn(irTrack.keys[k].outTangent);
        }
    } else {
        out.keys_data.resize(irTrack.keys.size());
        for (size_t k = 0; k < irTrack.keys.size(); k++) {
            out.timestamps[k] = mdx_transform::ticksToMs(irTrack.keys[k].time);
            out.keys_data[k] = valueFn(irTrack.keys[k].value);
        }
    }

    return out;
}

Vector3f positionTransform(const Point3& p) { return mdx_transform::position(p); }
Quaternion rotationTransform(const Quat& q) { return mdx_transform::rotation(q); }
Vector3f scaleTransform(const Point3& s) { return mdx_transform::scale(s); }
f32 floatIdentity(const float& f) { return f; }
Vector3f colorTransform(const Color& c) { return {c.r, c.g, c.b}; }
u32 intIdentity(const int32_t& i) { return static_cast<u32>(i); }

// UV-space transforms (no axis remap — texture animations operate in 2D UV space)
Vector3f uvTranslationIdentity(const Point3& p) { return {p.x, p.y, p.z}; }
Quaternion uvRotationIdentity(const Quat& q) { return {q.x, q.y, q.z, q.w}; }
Vector3f uvScaleIdentity(const Point3& s) { return {s.x, s.y, s.z}; }

// ── Node builder ─────────────────────────────────────────────

Node buildNode(const ir::IRModel& ir, int32_t irNodeIndex,
               const MdxHierarchyResolver& hierarchy,
               Node::NodeType type, Node::NodeFlag extraFlags,
               float invScale = 1.0f)
{
    Node node;
    if (irNodeIndex >= 0 && irNodeIndex < static_cast<int32_t>(ir.nodes.size())) {
        auto& irNode = ir.nodes[irNodeIndex];
        node.name = irNode.name;
    }

    node.objectId = hierarchy.getObjectId(irNodeIndex);
    node.parentId = hierarchy.getParentId(irNodeIndex);
    node.type = type;
    node.flags = extraFlags;

    // Merge in IR node flags (billboard, don't-inherit, camera-anchored, etc.)
    if (irNodeIndex >= 0 && irNodeIndex < static_cast<int32_t>(ir.nodes.size())) {
        node.flags = node.flags | static_cast<Node::NodeFlag>(ir.nodes[irNodeIndex].nodeFlags);
    }

    // Set type flag
    switch (type) {
    case Node::NodeType::Bone:       node.flags = node.flags | Node::NodeFlag::Bone; break;
    case Node::NodeType::Light:      node.flags = node.flags | Node::NodeFlag::Light; break;
    case Node::NodeType::Attachment:  node.flags = node.flags | Node::NodeFlag::Attachment; break;
    case Node::NodeType::ParticleEmitter:
        node.flags = node.flags | Node::NodeFlag::ParticleEmitter; break;
    case Node::NodeType::ParticleEmitter2:
        node.flags = node.flags | Node::NodeFlag::ParticleEmitter; break;
    case Node::NodeType::RibbonEmitter:
        node.flags = node.flags | Node::NodeFlag::RibbonEmitter; break;
    case Node::NodeType::EventObject:
        node.flags = node.flags | Node::NodeFlag::EventObject; break;
    case Node::NodeType::CollisionShape:
        node.flags = node.flags | Node::NodeFlag::CollisionShape; break;
    default: break;
    }

    // Animation tracks — merge ALL sequence sub-tracks into one combined track per channel.
    // AnimDispatcher creates one NodeAnimation per (node × sequence). MDX expects a single
    // KGTR/KGRT/KGSC per node containing keys from ALL sequences.
    // Delta correction (absolute → relative to bind pose) is already done in AnimDispatcher.
    {
        ir::Track<Point3> mergedTrans;
        ir::Track<Quat>   mergedRot;
        ir::Track<Point3> mergedScale;

        for (auto& na : ir.nodeAnimations) {
            if (na.nodeIndex != irNodeIndex) continue;

            if (!na.translation.empty()) {
                if (mergedTrans.empty())
                    mergedTrans.interpolation = na.translation.interpolation;
                // Propagate globalSequenceIndex if this sub-track carries one
                // and the merged track hasn't been tagged yet. (Patch C)
                if (na.translation.globalSequenceIndex >= 0 &&
                    mergedTrans.globalSequenceIndex < 0)
                    mergedTrans.globalSequenceIndex = na.translation.globalSequenceIndex;
                for (auto& key : na.translation.keys)
                    mergedTrans.keys.push_back(key);
            }
            if (!na.rotation.empty()) {
                if (mergedRot.empty())
                    mergedRot.interpolation = na.rotation.interpolation;
                if (na.rotation.globalSequenceIndex >= 0 &&
                    mergedRot.globalSequenceIndex < 0)
                    mergedRot.globalSequenceIndex = na.rotation.globalSequenceIndex;
                for (auto& key : na.rotation.keys)
                    mergedRot.keys.push_back(key);
            }
            if (!na.scale.empty()) {
                if (mergedScale.empty())
                    mergedScale.interpolation = na.scale.interpolation;
                if (na.scale.globalSequenceIndex >= 0 &&
                    mergedScale.globalSequenceIndex < 0)
                    mergedScale.globalSequenceIndex = na.scale.globalSequenceIndex;
                for (auto& key : na.scale.keys)
                    mergedScale.keys.push_back(key);
            }
        }

        // Sort merged keys by time
        auto sortByTime = [](auto& track) {
            std::sort(track.keys.begin(), track.keys.end(),
                [](const auto& a, const auto& b) { return a.time < b.time; });
        };
        if (!mergedTrans.empty()) sortByTime(mergedTrans);
        if (!mergedRot.empty())   sortByTime(mergedRot);
        if (!mergedScale.empty()) sortByTime(mergedScale);

        // Apply scene scale correction to translation keys
        if (invScale != 1.0f) {
            for (auto& key : mergedTrans.keys) {
                key.value *= invScale;
                key.inTangent *= invScale;
                key.outTangent *= invScale;
            }
        }

        node.translationTracks = convertTrack<Vector3f, Point3>(mergedTrans, positionTransform);
        node.rotationTracks    = convertTrack<Quaternion, Quat>(mergedRot, rotationTransform);
        node.scalingTracks     = convertTrack<Vector3f, Point3>(mergedScale, scaleTransform);
    }

    return node;
}

// Content hash of one IR GeosetAnim — two meshes may share a merged geoset
// only when their geoset animation is byte-for-byte interchangeable, so the
// single surviving GeosetAnimation entry is valid for all merged vertices.
// Returns a non-zero value; 0 is reserved for "mesh has no GeosetAnim".
uint64_t geosetAnimSignature(const ir::IRModel& ir,
                             const ir::IRModel::GeosetAnim& ga)
{
    uint64_t h = 1469598103934665603ull; // FNV-1a
    auto mix = [&h](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    auto mixf = [&](float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        mix(bits);
    };

    mixf(ga.alpha);
    mixf(ga.color.r); mixf(ga.color.g); mixf(ga.color.b);
    mix(ga.usesColor ? 1u : 0u);
    mix(ga.dropShadow ? 2u : 0u);

    if (ga.alphaTrackIndex >= 0 &&
        ga.alphaTrackIndex < static_cast<int32_t>(ir.floatTracks.size())) {
        const auto& t = ir.floatTracks[ga.alphaTrackIndex];
        mix(0xA1FAu);
        mix(static_cast<uint64_t>(t.interpolation));
        mix(static_cast<uint64_t>(t.globalSequenceIndex + 1));
        for (const auto& k : t.keys) {
            mix(static_cast<uint32_t>(k.time));
            mixf(k.value);
        }
    }
    if (ga.colorTrackIndex >= 0 &&
        ga.colorTrackIndex < static_cast<int32_t>(ir.colorTracks.size())) {
        const auto& t = ir.colorTracks[ga.colorTrackIndex];
        mix(0xC0102u);
        mix(static_cast<uint64_t>(t.interpolation));
        mix(static_cast<uint64_t>(t.globalSequenceIndex + 1));
        for (const auto& k : t.keys) {
            mix(static_cast<uint32_t>(k.time));
            mixf(k.value.r); mixf(k.value.g); mixf(k.value.b);
        }
    }

    return h ? h : 1u;
}

// Get float track from IR
Track<f32> getFloatTrack(const ir::IRModel& ir, int32_t trackIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int32_t>(ir.floatTracks.size()))
        return {};
    return convertTrack<f32, float>(ir.floatTracks[trackIndex], floatIdentity);
}

Track<Vector3f> getVec3Track(const ir::IRModel& ir, int32_t trackIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int32_t>(ir.vec3Tracks.size()))
        return {};
    return convertTrack<Vector3f, Point3>(ir.vec3Tracks[trackIndex], positionTransform);
}

Track<Vector3f> getColorTrack(const ir::IRModel& ir, int32_t trackIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int32_t>(ir.colorTracks.size()))
        return {};
    return convertTrack<Vector3f, Color>(ir.colorTracks[trackIndex], colorTransform);
}

Track<u32> getIntTrack(const ir::IRModel& ir, int32_t trackIndex) {
    if (trackIndex < 0 || trackIndex >= static_cast<int32_t>(ir.intTracks.size()))
        return {};
    return convertTrack<u32, int32_t>(ir.intTracks[trackIndex], intIdentity);
}

// PREM names what it spawns with one of two flags. Blizzard's files pair
// EmitterUsesMDL with a model path and EmitterUsesTGA with a texture, and the
// Wc3Particles1 plugin only keeps the path, so the extension decides.
Node::NodeFlag particleEmitter1SpawnFlag(const std::string& path) {
    size_t dot = path.find_last_of('.');
    std::string ext = (dot == std::string::npos) ? std::string() : path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool isImage = (ext == "tga" || ext == "blp" || ext == "dds" || ext == "tif" ||
                          ext == "png" || ext == "jpg");
    return static_cast<Node::NodeFlag>(isImage ? 0x10000u : 0x8000u);
}

} // anonymous namespace

Model MdxModelBuilder::build(const ir::IRModel& ir, const MdxExportOptions& opts) {
    Model model;
    model.version = opts.version;
    model.blendTime = opts.blendTime;

    // No automatic scale correction — export at native scene scale.
    // Pivots, vertices, and translation deltas are all in the same world space.
    float invScale = 1.0f;

    // 1. Hierarchy
    MdxHierarchyResolver hierarchy;
    hierarchy.resolve(ir, opts.version);

    // Clear + banner the LOD debug log so each export starts fresh
    {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        std::string lodLogPath = std::string(tempPath) + "mdlx_lod_debug.log";
        std::ofstream lodLog(lodLogPath, std::ios::trunc);
        if (lodLog.is_open()) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            lodLog << "=== MDLX LOD Debug Log ===\n"
                   << "Export started " << st.wYear << "-" << st.wMonth << "-" << st.wDay
                   << " " << st.wHour << ":" << st.wMinute << ":" << st.wSecond << "\n"
                   << "MDX version: " << opts.version << "\n"
                   << "IR meshes:   " << ir.meshes.size() << "\n"
                   << "----------------------------------------\n";
        }
    }

    // 2. Textures
    for (auto& tex : ir.textures) {
        Texture mdxTex;
        mdxTex.fileName = tex.filePath;
        mdxTex.replaceableId = tex.replaceableId;
        mdxTex.flags = (tex.wrapU ? Texture::Flag::WrapWidth : Texture::Flag::None)
                     | (tex.wrapV ? Texture::Flag::WrapHeight : Texture::Flag::None);
        model.textures.push_back(std::move(mdxTex));
    }

    // 3. Materials
    MdxMaterialMapper matMapper;
    for (auto& mat : ir.materials)
        model.materials.push_back(matMapper.map(mat, ir, opts.version));

    // Fallback: if no textures/materials were extracted, create a default
    // white.blp material so editors and viewers don't choke on dangling MaterialID refs.
    if (model.textures.empty() && model.materials.empty()) {
        Texture fallbackTex;
        fallbackTex.fileName = "Textures\\white.blp";
        fallbackTex.replaceableId = 0;
        fallbackTex.flags = Texture::Flag::None;
        model.textures.push_back(std::move(fallbackTex));

        wdx::Material fallbackMat;
        wdx::Layer layer;
        layer.filterMode = wdx::Layer::FilterMode::None;
        layer.shadingFlags = wdx::Layer::ShadingFlag::None;
        layer.textureId = 0;
        layer.alpha = 1.0f;
        layer.coordId = 0;
        layer.textureAnimationId = 0xFFFFFFFF;
        fallbackMat.layers.push_back(std::move(layer));
        model.materials.push_back(std::move(fallbackMat));
    }

    // 4. Global sequences
    for (auto dur : ir.globalSequenceDurations)
        model.globalSequences.push_back(mdx_transform::ticksToMs(dur));

    // 5. Sequences
    for (auto& seq : ir.sequences) {
        Sequence mdxSeq;
        mdxSeq.name = seq.name;
        mdxSeq.intervalStart = mdx_transform::ticksToMs(seq.startTime);
        mdxSeq.intervalEnd = mdx_transform::ticksToMs(seq.endTime);
        mdxSeq.moveSpeed = seq.moveSpeed;
        mdxSeq.flags = static_cast<Sequence::Flag>(seq.flags)
                     | (seq.isLooping ? Sequence::Flag::None : Sequence::Flag::NonLooping);
        mdxSeq.rarity = seq.rarity;
        model.sequences.push_back(std::move(mdxSeq));
    }

    // 6. Bones + helpers
    std::vector<int32_t> irBoneToMdxBone(ir.bones.size(), -1);
    for (auto& bone : ir.bones) {
        auto boneFlags = static_cast<Node::NodeFlag>(bone.nodeFlags);
        irBoneToMdxBone[&bone - ir.bones.data()] =
            (bone.isHelper && opts.version < 1200) ? -1 : static_cast<int32_t>(model.bones.size());
        if (bone.isHelper && opts.version < 1200) {
            // v800: separate helper
            Helper h;
            h.node = buildNode(ir, bone.nodeIndex, hierarchy,
                               Node::NodeType::Helper, boneFlags, invScale);
            model.helpers.push_back(std::move(h));
        } else {
            // Bone (or v1200 helper-as-bone)
            Bone b;
            b.node = buildNode(ir, bone.nodeIndex, hierarchy,
                               Node::NodeType::Bone, Node::NodeFlag::Bone | boneFlags, invScale);
            b.geosetId = Bone::MULTIPLE_GEOSETS;
            b.geosetAnimationId = Bone::MULTIPLE_GEOSETS;
            model.bones.push_back(std::move(b));
        }
    }

    // 7. Pivot points — one per hierarchy node.
    //    hierarchy.totalNodes() is the authoritative count (computed at step 1
    //    from the IR model, before model vectors are populated at steps 9-14).
    model.pivotPoints.resize(hierarchy.totalNodes(), Vector3f{0, 0, 0});
    for (auto& mapping : hierarchy.mappings()) {
        if (mapping.irNodeIndex >= 0 &&
            mapping.irNodeIndex < static_cast<int32_t>(ir.nodes.size()))
        {
            model.pivotPoints[mapping.objectId] =
                mdx_transform::position(ir.nodes[mapping.irNodeIndex].pivotPoint * invScale);
        }
    }

    // 8. Geosets
    for (auto& irMesh : ir.meshes) {
        Geoset geo;
        geo.materialId = (irMesh.materialIndex >= 0)
                             ? static_cast<uint32_t>(irMesh.materialIndex) : 0;

        // Clamp materialId to valid range — prevents viewer crashes when
        // Multi/Sub-Object materials aren't fully extracted
        if (!model.materials.empty() && geo.materialId >= model.materials.size()) {
            geo.materialId = 0;
        }

        // Vertex data
        for (auto& v : irMesh.vertices) {
            geo.vertexPositions.push_back(mdx_transform::position(v.position * invScale));
            geo.vertexNormals.push_back(mdx_transform::normal(v.normal));

            // UV sets
            for (int uv = 0; uv < v.uvSetCount; uv++) {
                if (uv >= static_cast<int>(geo.textureCoordinateSets.size()))
                    geo.textureCoordinateSets.resize(static_cast<size_t>(uv + 1));
                geo.textureCoordinateSets[uv].push_back(
                    mdx_transform::texcoord(v.uvSets[uv]));
            }

            // Tangent
            if (v.hasTangent) {
                geo.tangents.push_back(
                    Vector4f{-v.tangent.y, v.tangent.x, v.tangent.z, v.tangent.w});
            }

            // Skin weights (v1200 SKIN chunk)
            if (opts.version >= 1200 && !v.skinInfluences.empty()) {
                // SKIN: 4 bone indices + 4 weights per vertex. v1400+ writes
                // them as u16 (bone ids past 255), older versions as bytes.
                // boneIndex is an IR node index; convert to hierarchy objectId.
                //
                // mesh_extractor hands us every influence, heaviest first, so
                // the first 4 are the dominant bones. Renormalize over just
                // those 4 and hand out the byte budget by largest remainder so
                // the row sums to exactly 255 — a vertex with 5+ influences,
                // or plain per-bone rounding, otherwise ends up under-weighted
                // and drifts toward the origin.
                uint16_t boneIds[4] = {0, 0, 0, 0};
                uint8_t weights[4] = {0, 0, 0, 0};
                size_t count = std::min(v.skinInfluences.size(), size_t(4));

                float kept = 0.0f;
                for (size_t si = 0; si < count; si++)
                    kept += v.skinInfluences[si].weight;

                int   assigned = 0;
                float remainder[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                for (size_t si = 0; si < count; si++) {
                    uint32_t objId = hierarchy.getObjectId(v.skinInfluences[si].boneIndex);
                    boneIds[si] = (objId != Node::NO_PARENT)
                                      ? static_cast<uint16_t>(objId) : 0;
                    float exact = (kept > 1e-7f)
                        ? v.skinInfluences[si].weight / kept * 255.0f
                        : 0.0f;
                    int whole = static_cast<int>(exact);
                    weights[si] = static_cast<uint8_t>(whole);
                    remainder[si] = exact - static_cast<float>(whole);
                    assigned += whole;
                }
                for (int leftover = 255 - assigned; leftover > 0; --leftover) {
                    int best = -1;
                    for (size_t si = 0; si < count; si++) {
                        if (weights[si] == 255) continue;
                        if (best < 0 || remainder[si] > remainder[best])
                            best = static_cast<int>(si);
                    }
                    if (best < 0) break;
                    weights[best]++;
                    remainder[best] -= 1.0f;
                }
                for (int bi = 0; bi < 4; bi++) geo.skinData.push_back(boneIds[bi]);
                for (int wi = 0; wi < 4; wi++) geo.skinData.push_back(weights[wi]);
            }

            // v800 vertex groups (v1200 uses SKIN chunk instead, GNDX must be empty)
            if (opts.version < 1200) {
                if (!v.skinInfluences.empty()) {
                    // Vertex group = index into matrixGroups (resolved later)
                    geo.vertexGroups.push_back(0); // placeholder, resolved below
                } else {
                    geo.vertexGroups.push_back(0);
                }
            }
        }

        // Ensure UV sets have the right size
        for (auto& uvSet : geo.textureCoordinateSets) {
            while (uvSet.size() < geo.vertexPositions.size())
                uvSet.push_back(Vector2f{0, 0});
        }
        if (geo.textureCoordinateSets.empty()) {
            geo.textureCoordinateSets.resize(1);
            geo.textureCoordinateSets[0].resize(
                geo.vertexPositions.size(), Vector2f{0, 0});
        }

        // Indices
        for (auto idx : irMesh.indices)
            geo.faces.push_back(static_cast<uint16_t>(idx));

        // Face groups
        geo.faceTypeGroups.push_back(4); // triangles
        geo.faceGroups.push_back(static_cast<uint32_t>(geo.faces.size()));

        // v800 matrix groups: iterative relaxation + largest-remainder quantization
        if (opts.version < 1200) {
            if (opts.disableSkinQuantize) {
                // Bypass quantizer: build matrix groups directly from raw weights.
                // Each unique combination of bone objectIds becomes one group.
                //
                // Logic is intentionally 1:1 with NeoDex (NeoDexSceneParser.ms
                // processSkinning): take EVERY bone reported by the Skin
                // modifier, sort ascending by objectId, use the resulting list
                // as the matrix-group key. Do NOT filter by weight threshold
                // — v800 ignores weights and the Max SDK only reports bones
                // that actually have an assignment, so there are no
                // "bleed weights" to worry about at this stage.
                //
                // The vertex loop above already pushed one placeholder `0` per
                // vertex into geo.vertexGroups. Clear it before we fill in the
                // real group indices, or GNDX would end up with 2× the vertex
                // count. (The quantizer branch below uses std::move which
                // overwrites the placeholders in one shot, so it doesn't need
                // an explicit clear().)
                geo.vertexGroups.clear();

                std::map<std::vector<uint32_t>, uint32_t> groupMap;
                std::vector<std::vector<uint32_t>> groupBones;

                for (auto& v : irMesh.vertices) {
                    std::vector<uint32_t> boneIds;
                    for (auto& inf : v.skinInfluences) {
                        if (inf.boneIndex < 0) continue;
                        uint32_t objId = hierarchy.getObjectId(inf.boneIndex);
                        if (objId != Node::NO_PARENT)
                            boneIds.push_back(objId);
                    }
                    // Sort + dedupe (NeoDex uses `sort bonegroup`; duplicates
                    // shouldn't normally occur but dedupe guards against
                    // edge cases where the same bone is referenced twice).
                    std::sort(boneIds.begin(), boneIds.end());
                    boneIds.erase(std::unique(boneIds.begin(), boneIds.end()),
                                  boneIds.end());
                    if (boneIds.empty()) boneIds.push_back(0);

                    auto it = groupMap.find(boneIds);
                    if (it == groupMap.end()) {
                        uint32_t gid = static_cast<uint32_t>(groupBones.size());
                        groupMap[boneIds] = gid;
                        groupBones.push_back(boneIds);
                        geo.vertexGroups.push_back(static_cast<uint8_t>(gid));
                    } else {
                        geo.vertexGroups.push_back(static_cast<uint8_t>(it->second));
                    }
                }

                for (auto& bones : groupBones) {
                    geo.matrixGroups.push_back(static_cast<uint32_t>(bones.size()));
                    for (auto id : bones)
                        geo.matrixIndices.push_back(id);
                }
            } else {
                MdxSkinQuantizer quantizer;
                auto skinResult = quantizer.quantize(irMesh, hierarchy);
                geo.vertexGroups  = std::move(skinResult.vertexGroups);
                geo.matrixGroups  = std::move(skinResult.matrixGroups);
                geo.matrixIndices = std::move(skinResult.matrixIndices);
            }
        } else {
            // v1200: SKIN chunk handles per-vertex weights; GNDX is empty.
            // MTGC/MATS list one group per referenced bone (each size 1,
            // sequential objectIds) matching the MaxScript exporter.
            uint32_t maxBoneId = 0;
            for (auto& v : irMesh.vertices) {
                for (auto& inf : v.skinInfluences) {
                    if (inf.boneIndex < 0 || inf.weight <= 0.0f) continue;
                    uint32_t objId = hierarchy.getObjectId(inf.boneIndex);
                    if (objId != Node::NO_PARENT && objId > maxBoneId)
                        maxBoneId = objId;
                }
            }
            uint32_t numBones = maxBoneId + 1;
            // vertexGroups stays empty (GNDX count = 0)
            geo.matrixGroups.resize(numBones, 1);
            geo.matrixIndices.resize(numBones);
            for (uint32_t i = 0; i < numBones; ++i)
                geo.matrixIndices[i] = i;
        }

        geo.selectionGroup = irMesh.selectionGroup;
        // Selection flag 4: unselectable in game (Object Settings checkbox).
        geo.selectionFlags = irMesh.unselectable ? 4u : 0u;

        // LOD fields — propagate from IR (populated by mesh_extractor from
        // UserProps Wc3GeosetLod / Wc3LodName). For v800 these fields don't
        // exist in the MDX GEOS layout, so we force (0, empty) to keep the
        // legacy behavior byte-identical. For v1000+ we honor the user's
        // imported values, which round-trips HD LOD layers correctly.
        if (opts.version > 800) {
            geo.lod = static_cast<uint32_t>(irMesh.lod);
            geo.lodName = irMesh.lodName;
        } else {
            geo.lod = 0;
            // geo.lodName stays default-constructed (empty) for v800
        }

        // DEDICATED LOD DEBUG LOG — writes what we're about to commit to the
        // MDX Geoset. Compare against the [MESH EXTRACT] lines in the same
        // log to verify IR→MDX propagation.
        {
            char tempPath[MAX_PATH];
            GetTempPathA(MAX_PATH, tempPath);
            std::string lodLogPath = std::string(tempPath) + "mdlx_lod_debug.log";
            std::ofstream lodLog(lodLogPath, std::ios::app);
            if (lodLog.is_open()) {
                lodLog << "[GEOSET BUILD] geoset#" << (model.geosets.size())
                       << " mdxVersion=" << opts.version
                       << " mesh='" << irMesh.name << "'"
                       << " irLod=" << irMesh.lod
                       << " irLodName='" << irMesh.lodName << "'"
                       << " -> geo.lod=" << geo.lod
                       << " geo.lodName='" << geo.lodName << "'"
                       << "\n";
            }
        }

        model.geosets.push_back(std::move(geo));
    }

    // Merge geosets if enabled. Meshes only merge when their GeosetAnim
    // content is identical (signature match) — otherwise per-geoset
    // visibility/color animation would collapse onto one merged geoset
    // (e.g. four sword geosets sharing a material but carrying different
    // KGAO tracks). The remap rebinds GeosetAnimation.geosetId below.
    std::vector<uint32_t> geosetRemap;
    if (opts.mergeGeosets) {
        std::vector<uint64_t> animSigs(model.geosets.size(), 0);
        for (auto& irGa : ir.geosetAnims) {
            if (irGa.meshIndex >= 0 &&
                irGa.meshIndex < static_cast<int32_t>(animSigs.size()))
                animSigs[irGa.meshIndex] = geosetAnimSignature(ir, irGa);
        }
        MdxGeosetMerger merger;
        merger.merge(model.geosets, animSigs, &geosetRemap);
    }

    // ── Bone ↔ Geoset back-reference fix ────────────────────────────────
    // The MDX bone trailer carries two fields beyond the Node:
    //   uint32 geosetId       — index of the geoset that uses this bone
    //                           for skinning. -1 (MULTIPLE_GEOSETS) if the
    //                           bone is unbound, or referenced by multiple
    //                           geosets.
    //   uint32 geosetAnimId   — same idea, but for the geoset animation
    //                           (KGAO chunk). -1 if none.
    //
    // The previous code unconditionally wrote MULTIPLE_GEOSETS for every
    // bone (mdx_model_builder.cpp:326). NeoDex computes the actual
    // back-reference (NeoDexSceneParser.ms LoadBone / Wc3Bone construction):
    // a bone referenced by exactly ONE geoset's MATS list gets that
    // geoset's index; a bone referenced by zero or multiple geosets stays
    // at -1.
    //
    // Symptom of the missing back-reference: bones bound to a single
    // geoset (typical for accessory chains like a hat bone) drift slightly
    // during animation — strict renderers use this field to pick the
    // correct skinning matrix evaluation path.
    {
        // Map: boneObjectId -> geoset index, or -2 if multiple geosets reference it
        std::map<uint32_t, int32_t> boneToGeoset;
        for (size_t gi = 0; gi < model.geosets.size(); ++gi) {
            int32_t gIdx = static_cast<int32_t>(gi);
            for (uint32_t boneObjId : model.geosets[gi].matrixIndices) {
                auto it = boneToGeoset.find(boneObjId);
                if (it == boneToGeoset.end()) {
                    boneToGeoset[boneObjId] = gIdx;
                } else if (it->second >= 0 && it->second != gIdx) {
                    it->second = -2;  // multi-geoset
                }
            }
        }
        for (auto& b : model.bones) {
            auto it = boneToGeoset.find(b.node.objectId);
            if (it != boneToGeoset.end() && it->second >= 0) {
                b.geosetId = static_cast<uint32_t>(it->second);
            }
            // else: keep MULTIPLE_GEOSETS (already set at construction)
        }
    }
    // ────────────────────────────────────────────────────────────────────

    // 9. Lights
    for (auto& irLight : ir.lights) {
        wdx::Light light;
        light.node = buildNode(ir, irLight.nodeIndex, hierarchy,
                               Node::NodeType::Light, Node::NodeFlag::Light, invScale);

        switch (irLight.type) {
        case ir::Light::Type::Omni:        light.type = wdx::Light::LightType::Omni; break;
        case ir::Light::Type::Directional: light.type = wdx::Light::LightType::Directional; break;
        case ir::Light::Type::Ambient:     light.type = wdx::Light::LightType::Ambient; break;
        }

        light.attenuationStart = irLight.attenuationStart;
        light.attenuationEnd = irLight.attenuationEnd;
        light.color = {irLight.color.r, irLight.color.g, irLight.color.b};
        light.intensity = irLight.intensity;
        light.ambientColor = {irLight.ambientColor.r, irLight.ambientColor.g,
                              irLight.ambientColor.b};
        light.ambientIntensity = irLight.ambientIntensity;
        // Version-gated by the writer: shadowIntensity from v1200, the shadow
        // casting range from v1300, the falloff from v1600.
        light.shadowIntensity = irLight.shadowIntensity;
        light.shadowCasting = irLight.shadowCasting;
        light.shadowCastingStart = irLight.shadowCastingStart;
        light.shadowCastingEnd = irLight.shadowCastingEnd;
        light.quadraticFalloff = irLight.quadraticFalloff;
        light.linearFalloff = irLight.linearFalloff;
        light.damping = irLight.damping;

        light.attenuationStartTracks = getFloatTrack(ir, irLight.attStartTrackIndex);
        light.attenuationEndTracks = getFloatTrack(ir, irLight.attEndTrackIndex);
        light.intensityTracks = getFloatTrack(ir, irLight.intensityTrackIndex);
        light.ambientIntensityTracks = getFloatTrack(ir, irLight.ambIntensityTrackIndex);
        light.visibilityTracks = getFloatTrack(ir, irLight.visibilityTrackIndex);
        light.colorTracks = getColorTrack(ir, irLight.colorTrackIndex);
        light.ambientColorTracks = getColorTrack(ir, irLight.ambColorTrackIndex);
        light.shadowCastingStartTracks = getFloatTrack(ir, irLight.shadowCastStartTrackIndex);
        light.shadowCastingEndTracks = getFloatTrack(ir, irLight.shadowCastEndTrackIndex);
        light.quadraticFalloffTracks = getFloatTrack(ir, irLight.quadFalloffTrackIndex);
        light.linearFalloffTracks = getFloatTrack(ir, irLight.linearFalloffTrackIndex);
        light.dampingTracks = getFloatTrack(ir, irLight.dampingTrackIndex);

        model.lights.push_back(std::move(light));
    }

    // 10. Attachments
    for (auto& irAt : ir.attachments) {
        Attachment attach;
        attach.node = buildNode(ir, irAt.nodeIndex, hierarchy,
                                Node::NodeType::Attachment, Node::NodeFlag::Attachment, invScale);
        attach.path = irAt.path;
        attach.attachmentId = static_cast<uint32_t>(irAt.attachmentId);
        attach.visibilityTracks = getFloatTrack(ir, irAt.visibilityTrackIndex);
        model.attachments.push_back(std::move(attach));
    }

    // 11. Particle emitters
    for (auto& irPe : ir.particleEmitters) {
        if (irPe.variant == 1) {
            ParticleEmitter pe;
            pe.node = buildNode(ir, irPe.nodeIndex, hierarchy,
                                Node::NodeType::ParticleEmitter,
                                Node::NodeFlag::ParticleEmitter, invScale);
            pe.node.flags = pe.node.flags | particleEmitter1SpawnFlag(irPe.modelPath);

            pe.emissionRate = irPe.emissionRate;
            pe.gravity = irPe.gravity;
            pe.longitude = irPe.longitude;
            pe.latitude = irPe.latitude;
            pe.spawnModelFileName = irPe.modelPath;
            pe.lifespan = irPe.lifespan;
            pe.initialVelocity = irPe.speed;

            pe.emissionRateTracks = getFloatTrack(ir, irPe.emissionRateTrackIndex);
            pe.gravityTracks = getFloatTrack(ir, irPe.gravityTrackIndex);
            pe.longitudeTracks = getFloatTrack(ir, irPe.longitudeTrackIndex);
            pe.latitudeTracks = getFloatTrack(ir, irPe.latitudeTrackIndex);
            pe.lifespanTracks = getFloatTrack(ir, irPe.lifespanTrackIndex);
            pe.speedTracks = getFloatTrack(ir, irPe.speedTrackIndex);
            pe.visibilityTracks = getFloatTrack(ir, irPe.visibilityTrackIndex);

            model.particleEmitters.push_back(std::move(pe));
        }
        else if (irPe.variant == 2) {
            ParticleEmitter2 pe;
            pe.node = buildNode(ir, irPe.nodeIndex, hierarchy,
                                Node::NodeType::ParticleEmitter2,
                                Node::NodeFlag::ParticleEmitter, invScale);

            // Apply PE2 node flags
            pe.node.flags = pe.node.flags |
                static_cast<Node::NodeFlag>(irPe.flags & 0x1F8000);

            pe.speed = irPe.speed;
            pe.variation = irPe.variation;
            pe.latitude = irPe.latitude;
            pe.gravity = irPe.gravity;
            pe.lifespan = irPe.lifespan;
            pe.emissionRate = irPe.emissionRate;
            pe.length = irPe.length;
            pe.width = irPe.width;
            pe.filterMode = irPe.filterMode;
            pe.rows = irPe.rows;
            pe.columns = irPe.columns;
            pe.headOrTail = irPe.headOrTail;
            pe.tailLength = irPe.tailLength;
            pe.time = irPe.midTime;
            pe.squirt = (irPe.flags & 1) ? 1u : 0u;
            pe.priorityPlane = irPe.priorityPlane;
            pe.textureId = (irPe.textureIndex >= 0)
                               ? static_cast<uint32_t>(irPe.textureIndex) : 0;
            pe.replaceableId = irPe.replaceableId;

            // Segment data
            for (int s = 0; s < 3; s++) {
                pe.segmentColor[s] = {irPe.segmentColors[s].r,
                                      irPe.segmentColors[s].g,
                                      irPe.segmentColors[s].b};
                pe.segmentAlpha[s] = static_cast<uint8_t>(irPe.segmentAlpha[s] * 255.0f + 0.5f);
                pe.segmentScaling[s] = irPe.segmentScale[s];
                pe.headInterval[s] = static_cast<uint32_t>(irPe.headInterval[s]);
                pe.headDecayInterval[s] = static_cast<uint32_t>(irPe.headDecayInterval[s]);
                pe.tailInterval[s] = static_cast<uint32_t>(irPe.tailInterval[s]);
                pe.tailDecayInterval[s] = static_cast<uint32_t>(irPe.tailDecayInterval[s]);
            }

            pe.speedTracks = getFloatTrack(ir, irPe.speedTrackIndex);
            pe.variationTracks = getFloatTrack(ir, irPe.variationTrackIndex);
            pe.latitudeTracks = getFloatTrack(ir, irPe.latitudeTrackIndex);
            pe.gravityTracks = getFloatTrack(ir, irPe.gravityTrackIndex);
            pe.emissionRateTracks = getFloatTrack(ir, irPe.emissionRateTrackIndex);
            pe.lengthTracks = getFloatTrack(ir, irPe.lengthTrackIndex);
            pe.widthTracks = getFloatTrack(ir, irPe.widthTrackIndex);
            pe.visibilityTracks = getFloatTrack(ir, irPe.visibilityTrackIndex);

            model.particleEmitters2.push_back(std::move(pe));
        }
        else if (irPe.variant == 3 && opts.version >= 1200) {
            // CornEmitter (PopcornFX)
            CornEmitter corn;
            corn.node = buildNode(ir, irPe.nodeIndex, hierarchy,
                                  Node::NodeType::CornEmitter, Node::NodeFlag::None, invScale);
            corn.lifeSpan = irPe.lifespan;
            corn.emissionRate = irPe.emissionRate;
            corn.speed = irPe.speed;
            corn.replaceableId = irPe.replaceableId;
            corn.path = irPe.modelPath;
            corn.animVisibilityGuide = irPe.animVisibilityGuide;
            // OR-merge popcorn render flags (Unshaded / PopcornUnfogged /
            // PopcornScaling) onto the node bits. buildNode left those
            // bits at 0 — DontInherit / billboard etc. already live on
            // irNode.nodeFlags. Mask to just the three popcorn bits so we
            // can't accidentally smuggle PE2-only flags through.
            corn.node.flags = static_cast<whiteout::mdx::Node::NodeFlag>(
                static_cast<uint32_t>(corn.node.flags) | (irPe.flags & 0x68000u));
            corn.color = Vector3f(irPe.segmentColors[0].r, irPe.segmentColors[0].g,
                                  irPe.segmentColors[0].b);
            corn.alpha = irPe.segmentAlpha[0];

            // KPPL / KPPE / KPPS / KPPC / KPPA / KPPV. Field names on
            // CornEmitter match their chunks — `lifeSpanTracks` really is
            // KPPL and `alphaTracks` really is KPPA (an earlier WhiteoutLib
            // revision had those two crossed, so map by chunk, not by name).
            corn.lifeSpanTracks     = getFloatTrack(ir, irPe.lifespanTrackIndex);
            corn.emissionRateTracks = getFloatTrack(ir, irPe.emissionRateTrackIndex);
            corn.speedTracks        = getFloatTrack(ir, irPe.speedTrackIndex);
            corn.colorTracks        = getColorTrack(ir, irPe.colorTrackIndex);
            corn.alphaTracks        = getFloatTrack(ir, irPe.alphaTrackIndex);
            corn.visibilityTracks   = getFloatTrack(ir, irPe.visibilityTrackIndex);

            model.cornEmitters.push_back(std::move(corn));
        }
    }

    // 12. Ribbon emitters
    for (auto& irRib : ir.ribbonEmitters) {
        RibbonEmitter rib;
        rib.node = buildNode(ir, irRib.nodeIndex, hierarchy,
                             Node::NodeType::RibbonEmitter,
                             Node::NodeFlag::RibbonEmitter, invScale);
        rib.heightAbove = irRib.heightAbove;
        rib.heightBelow = irRib.heightBelow;
        rib.alpha = irRib.alpha;
        rib.color = {irRib.color.r, irRib.color.g, irRib.color.b};
        rib.lifespan = irRib.lifespan;
        rib.gravity = irRib.gravity;
        rib.emissionRate = static_cast<uint32_t>(irRib.emissionRate);
        rib.rows = static_cast<uint32_t>(irRib.rows);
        rib.columns = static_cast<uint32_t>(irRib.columns);
        rib.materialId = (irRib.materialIndex >= 0)
                             ? static_cast<uint32_t>(irRib.materialIndex) : 0;
        rib.textureSlot = static_cast<uint32_t>(irRib.textureSlot);

        rib.heightAboveTracks = getFloatTrack(ir, irRib.heightAboveTrackIndex);
        rib.heightBelowTracks = getFloatTrack(ir, irRib.heightBelowTrackIndex);
        rib.alphaTracks = getFloatTrack(ir, irRib.alphaTrackIndex);
        rib.colorTracks = getColorTrack(ir, irRib.colorTrackIndex);
        rib.textureSlotTracks = getIntTrack(ir, irRib.textureSlotTrackIndex);
        rib.visibilityTracks = getFloatTrack(ir, irRib.visibilityTrackIndex);

        model.ribbonEmitters.push_back(std::move(rib));
    }

    // 13. Event objects
    for (auto& irEvt : ir.eventObjects) {
        EventObject evt;
        evt.node = buildNode(ir, irEvt.nodeIndex, hierarchy,
                             Node::NodeType::EventObject,
                             Node::NodeFlag::EventObject, invScale);
        // Set node name to the event code (e.g., "SND_FAL0")
        evt.node.name = irEvt.eventCode;

        for (auto keyTime : irEvt.keyTimes)
            evt.eventTrackTimes.push_back(mdx_transform::ticksToMs(keyTime));
        if (irEvt.globalSequenceIndex >= 0)
            evt.globalSequenceId = static_cast<u32>(irEvt.globalSequenceIndex);

        // The game cannot load an event object without keys: a KEVT with
        // count 0 fails MDL::ReadBinEventObjects (and with it the whole model
        // — invisible in game), and dropping the chunk desyncs that reader
        // unless the object is the section's last. Give it one key that never
        // fires: past every sequence (the game buckets keys by inclusive
        // interval) and not global (a global track is rebased to its first key,
        // which would then fire every loop). A whole frame past, frame-aligned:
        // the importer rounds times to the nearest frame, and 1 ms past the end
        // would come back as the end itself.
        if (evt.eventTrackTimes.empty()) {
            const TimeValue tpf = GetTicksPerFrame();
            TimeValue lastEnd = 0;
            for (const auto& seq : ir.sequences)
                lastEnd = std::max(lastEnd, seq.endTime);
            const TimeValue unplayed = ((lastEnd + tpf - 1) / tpf + 1) * tpf;
            evt.eventTrackTimes.push_back(mdx_transform::ticksToMs(unplayed));
            evt.globalSequenceId = 0xFFFFFFFF;
        }

        model.eventObjects.push_back(std::move(evt));
    }

    // 14. Collision shapes
    //
    // Two wire formats for CLID:
    //   * v800 / v1000+ static: vertices[] are absolute world positions,
    //                           PIVT for this node is (0, 0, 0) or pos,
    //                           no animation tracks.
    //   * v1000+ animated:      vertices[] are LOCAL extents centered
    //                           around the pivot; PIVT is the world-space
    //                           anchor the hitbox follows; parentId forced
    //                           to root (0xFFFFFFFF); KGTR/KGRT tracks
    //                           animate the offset from that anchor.
    //
    // Detection is per-node: a single model may mix static decorative
    // hitboxes with animated bone-following hitboxes. We pick the
    // animated branch when the node has at least one translation or
    // rotation key AND the export version supports it (v1000+).
    //
    // See exporter_handoff_collision_shapes.md §3-4 for the contract.
    for (auto& irCs : ir.collisionShapes) {
        CollisionShape cs;
        cs.node = buildNode(ir, irCs.nodeIndex, hierarchy,
                            Node::NodeType::CollisionShape,
                            Node::NodeFlag::CollisionShape, invScale);

        switch (irCs.shape) {
        case ir::CollisionShape::Shape::Box:
            cs.type = CollisionShape::ShapeType::Box; break;
        case ir::CollisionShape::Shape::Sphere:
            cs.type = CollisionShape::ShapeType::Sphere; break;
        case ir::CollisionShape::Shape::Cylinder:
            cs.type = CollisionShape::ShapeType::Cylinder; break;
        case ir::CollisionShape::Shape::Plane:
            cs.type = CollisionShape::ShapeType::Plane; break;
        }

        for (auto& v : irCs.vertices)
            cs.vertices.push_back(mdx_transform::position(v * invScale));

        // For BOX shapes: after swizzle, take component-wise min/max to
        // ensure v1 ≤ v2 in MDX space. The Max→MDX swizzle (-y, x, z) flips
        // the Y axis sign, so Max-min can become MDX-max on Y. Normalizing
        // guarantees correct axis-aligned box representation downstream.
        if (irCs.shape == ir::CollisionShape::Shape::Box && cs.vertices.size() == 2) {
            auto& a = cs.vertices[0];
            auto& b = cs.vertices[1];
            whiteout::Vector3f vmin{ std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) };
            whiteout::Vector3f vmax{ std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) };
            cs.vertices[0] = vmin;
            cs.vertices[1] = vmax;
        }

        cs.radius = irCs.radius * invScale;

        // ── v1000+ animated collision: convert absolute → local ──
        //
        // If the builder is emitting v1000 or higher AND this collision's
        // node has animated transform tracks, rewrite the vertices to be
        // pivot-relative (local extents) and set parentId to root.
        //
        // The absolute vertices we have are `pivot + local_extent`
        // (produced by the extractor from node.pos + box half-sizes).
        // Subtracting the pivot (in MDX space) gives the local extent
        // directly. Tracks already animate around that pivot, so they
        // need no modification.
        //
        // Reference model (KulTirasMarine.mdx, v1000):
        //   B_KGS_Torso: pivot = ( 0.99,  0.00, 54.26)   ← world anchor
        //                v1    = (-15.88, -24.03, -40.25) ← local min corner
        //                v2    = ( 15.88,  24.03,   0.00) ← local max corner
        //   + KGTR 554 keys, KGRT 480 keys, parentId = 0xFFFFFFFF
        const bool isAnimated =
            cs.node.translationTracks.isUsed ||
            cs.node.rotationTracks.isUsed;
        if (opts.version >= 1000 && isAnimated) {
            // Fetch pivot in MDX space for this node
            uint32_t objId = cs.node.objectId;
            if (objId < model.pivotPoints.size()) {
                const auto& pv = model.pivotPoints[objId];
                for (auto& vert : cs.vertices) {
                    vert.x -= pv.x;
                    vert.y -= pv.y;
                    vert.z -= pv.z;
                }
            }
            // Box-bottom anchor for v1000+ animated shapes.
            //
            // The extractor produces a Max-space AABB where the pivot is at
            // the box's BOTTOM (z ∈ [pos.z, pos.z+height]). That's correct
            // for the legacy v800 convention and must stay unchanged so
            // existing v800 models (e.g. Madara hitboxes) keep roundtripping.
            //
            // The v1000+ MDX convention, however, places the pivot at the
            // box's TOP face — local z ∈ [-height, 0]. After the pivot
            // subtract above we currently have z ∈ [0, +height], so we
            // shift both Z values down by 'height' to match the ORIG file.
            //
            // Reference (KulTirasMarine.mdx, v1000):
            //   Torso: ORIG v1.z=-40.25, v2.z=0.00 (height 40.25 below pivot)
            if (irCs.shape == ir::CollisionShape::Shape::Box && cs.vertices.size() == 2) {
                float height = cs.vertices[1].z - cs.vertices[0].z;
                cs.vertices[0].z -= height;   // was 0, becomes -height
                cs.vertices[1].z -= height;   // was +height, becomes 0
            }
            // Animated hitboxes live at root level; they follow their
            // bone through the animation tracks, not through parenting.
            cs.node.parentId = 0xFFFFFFFFu;
        }

        model.collisionShapes.push_back(std::move(cs));
    }

    // 15. Cameras
    //
    // MDX-Spec camera convention: KCTR/KTTR tracks store OFFSETS relative to
    // cam.position / cam.targetPosition, NOT absolute world positions.
    // getVec3Track returns the Max-extracted absolute positions transformed
    // into MDX-space. We must subtract the camera's base position/target to
    // convert them into deltas, matching how Blizzard's arthas.mdx stores
    // camera animation (KCTR[0] = (0,0,0) when static at t=0).
    //
    // Note: Track<T>::keys_data is a raw byte buffer; we reinterpret it as
    // Key or TangentKey (same layout as used in convertTrack above).
    auto applyCameraDelta = [](Track<Vector3f>& track, const Vector3f& origin) {
        if (!track.isUsed) return;
        bool hasTangents =
            (track.interpolationType == InterpolationType::Hermite ||
             track.interpolationType == InterpolationType::Bezier);
        if (hasTangents) {
            auto keys = track.tangentKeys();
            for (size_t k = 0; k < keys.size(); ++k) {
                keys[k].value.x -= origin.x;
                keys[k].value.y -= origin.y;
                keys[k].value.z -= origin.z;
                // Tangents are velocity-like deltas — do NOT shift them
            }
        } else {
            for (size_t k = 0; k < track.keys_data.size(); ++k) {
                track.keys_data[k].x -= origin.x;
                track.keys_data[k].y -= origin.y;
                track.keys_data[k].z -= origin.z;
            }
        }
    };

    for (auto& irCam : ir.cameras) {
        wdx::Camera cam;
        cam.name = irCam.name;
        cam.position = mdx_transform::position(irCam.position * invScale);
        cam.targetPosition = mdx_transform::position(irCam.targetPosition * invScale);
        cam.fieldOfView = irCam.fov;
        cam.nearClippingPlane = irCam.nearClip;
        cam.farClippingPlane = irCam.farClip;

        cam.positionTracks = getVec3Track(ir, irCam.positionTrackIndex);
        cam.targetPositionTracks = getVec3Track(ir, irCam.targetPositionTrackIndex);
        cam.targetRotationTracks = getFloatTrack(ir, irCam.rotationTrackIndex);
        // KCVS / IDUF / ELAF / PTSF are written at every version (see the
        // writer); only a 3.0 source scene carries them.
        cam.visibilityTracks = getFloatTrack(ir, irCam.visibilityTrackIndex);
        cam.focusDistanceTracks = getFloatTrack(ir, irCam.focusDistanceTrackIndex);
        cam.focalLengthTracks = getFloatTrack(ir, irCam.focalLengthTrackIndex);
        cam.fStopTracks = getFloatTrack(ir, irCam.fStopTrackIndex);

        // Convert absolute world positions -> deltas relative to cam.position/targetPosition
        applyCameraDelta(cam.positionTracks,       cam.position);
        applyCameraDelta(cam.targetPositionTracks, cam.targetPosition);

        model.cameras.push_back(std::move(cam));
    }

    // 16. Geoset animations (visibility per geoset)
    if (!ir.geosetAnims.empty()) {
        // Mesh index → final geoset index (identity when merging is off).
        // Merged meshes carry identical anim content (the merger only fuses
        // equal signatures), so keep exactly one GeosetAnimation per geoset.
        std::set<uint32_t> geosetAnimEmitted;
        for (auto& irGa : ir.geosetAnims) {
            uint32_t gid = (irGa.meshIndex >= 0) ? static_cast<uint32_t>(irGa.meshIndex) : 0;
            if (!geosetRemap.empty() && gid < geosetRemap.size())
                gid = geosetRemap[gid];
            if (geosetAnimEmitted.count(gid)) continue;
            geosetAnimEmitted.insert(gid);

            GeosetAnimation ga;
            ga.alpha = irGa.alpha;
            ga.geosetId = gid;
            ga.color = {irGa.color.r, irGa.color.g, irGa.color.b};
            ga.alphaTracks = getFloatTrack(ir, irGa.alphaTrackIndex);
            ga.colorTracks = getColorTrack(ir, irGa.colorTrackIndex);
            // flags bit 0 = DropShadow, bit 1 = use color (static or animated)
            bool hasNonWhiteColor = (irGa.color.r < 0.999f || irGa.color.g < 0.999f || irGa.color.b < 0.999f);
            if (irGa.usesColor || ga.colorTracks.isUsed || hasNonWhiteColor)
                ga.flags = ga.flags | GeosetAnimation::Flag::Color;
            if (irGa.dropShadow)
                ga.flags = ga.flags | GeosetAnimation::Flag::DropShadow;
            model.geosetAnimations.push_back(std::move(ga));
        }
    } else {
        // Fallback: create default visible geoset animations
        for (size_t i = 0; i < model.geosets.size(); i++) {
            GeosetAnimation ga;
            ga.alpha = 1.0f;
            ga.geosetId = static_cast<uint32_t>(i);
            ga.color = Vector3f{1, 1, 1};
            model.geosetAnimations.push_back(std::move(ga));
        }
    }

    // 16b. Bone visibility gates. The importer keeps a gated bone's geoset as
    // the `Wc3VisibilityGeoset` UserProp (the mesh node's handle). The bone
    // then names that geoset and its geoset animation, which is the pairing
    // every shipped model uses. Bones without the prop keep the MATS-derived
    // geosetId above and no gate.
    if (!ir.geosetAnims.empty()) {
        for (size_t bi = 0; bi < ir.bones.size(); ++bi) {
            const int32_t mdxBone = irBoneToMdxBone[bi];
            const int32_t nodeIdx = ir.bones[bi].nodeIndex;
            if (mdxBone < 0 || nodeIdx < 0 || nodeIdx >= static_cast<int32_t>(ir.nodes.size()))
                continue;
            INode* boneNode = ir.nodes[nodeIdx].maxNode;
            int handle = 0;
            if (!boneNode || !boneNode->GetUserPropInt(_T("Wc3VisibilityGeoset"), handle) || handle <= 0)
                continue;

            for (size_t mi = 0; mi < ir.meshes.size(); ++mi) {
                const int32_t meshNode = ir.meshes[mi].nodeIndex;
                if (meshNode < 0 || meshNode >= static_cast<int32_t>(ir.nodes.size()) ||
                    !ir.nodes[meshNode].maxNode ||
                    ir.nodes[meshNode].maxNode->GetHandle() != static_cast<ULONG>(handle))
                    continue;
                uint32_t gid = static_cast<uint32_t>(mi);
                if (!geosetRemap.empty() && gid < geosetRemap.size())
                    gid = geosetRemap[gid];
                for (size_t gai = 0; gai < model.geosetAnimations.size(); ++gai) {
                    if (model.geosetAnimations[gai].geosetId != gid) continue;
                    model.bones[mdxBone].geosetId = gid;
                    model.bones[mdxBone].geosetAnimationId = static_cast<uint32_t>(gai);
                    break;
                }
                if (model.bones[mdxBone].geosetAnimationId != Bone::MULTIPLE_GEOSETS)
                    break;
            }
        }
    }

    // 17. Texture animations
    for (auto& irTa : ir.textureAnimations) {
        TextureAnimation ta;
        if (irTa.translationTrackIndex >= 0 &&
            irTa.translationTrackIndex < static_cast<int32_t>(ir.vec3Tracks.size()))
        {
            ta.translationTracks = convertTrack<Vector3f, Point3>(
                ir.vec3Tracks[irTa.translationTrackIndex], uvTranslationIdentity);
        }
        if (irTa.rotationTrackIndex >= 0 &&
            irTa.rotationTrackIndex < static_cast<int32_t>(ir.quatTracks.size()))
        {
            ta.rotationTracks = convertTrack<Quaternion, Quat>(
                ir.quatTracks[irTa.rotationTrackIndex], uvRotationIdentity);
        }
        if (irTa.scaleTrackIndex >= 0 &&
            irTa.scaleTrackIndex < static_cast<int32_t>(ir.vec3Tracks.size()))
        {
            ta.scalingTracks = convertTrack<Vector3f, Point3>(
                ir.vec3Tracks[irTa.scaleTrackIndex], uvScaleIdentity);
        }
        model.textureAnimations.push_back(std::move(ta));
    }

    // 18. Extents
    MdxExtentCalculator extCalc;
    extCalc.compute(ir, model);

    // 19. Face effects (Reforged FaceFX, v1200+)
    if (opts.version >= 1200) {
        for (auto& node : ir.nodes) {
            if (!node.ext) continue;
            auto* fx = dynamic_cast<mdx_extract::FaceFXExtensionData*>(node.ext.get());
            if (!fx) continue;
            FaceEffect fe;
            fe.name = fx->facefxName;
            fe.path = fx->facefxPath;
            model.faceEffects.push_back(std::move(fe));
        }
    }

    // 20. Bind poses (v > 800)
    //     Every node in the hierarchy gets a 3×4 row-major matrix derived from
    //     its world transform at frame 0, matching the MaxScript exporter which
    //     uses node.maxObj.transform for ALL node types (not just bones).
    //
    //     IMPORTANT: The BPOS chunk must contain PIVT_count + 1 matrices — one
    //     per node PLUS a trailing identity matrix. Retera Model Studio expects
    //     this and throws "Index N out of bounds for length N" in
    //     updateIdObjectReferences if the trailer is missing. Verified against
    //     Blizzard's arthas.mdx (v1000, BPOS=182, PIVT=181) and
    //     KulTirasMarine.mdx (v1000, BPOS=157, PIVT=156).
    if (opts.version > 800) {
        const uint32_t numNodes = hierarchy.totalNodes();
        model.bindPoses.resize(numNodes + 1);   // +1 for trailing identity

        auto transformBP = [](const Matrix3& m) -> std::array<f32, 12> {
            Point3 r0 = m.GetRow(0), r1 = m.GetRow(1),
                   r2 = m.GetRow(2), r3 = m.GetRow(3);
            return {{
                -r0.y, r0.x, r0.z,
                -r1.y, r1.x, r1.z,
                -r2.y, r2.x, r2.z,
                -r3.y, r3.x, r3.z
            }};
        };

        for (auto& mapping : hierarchy.mappings()) {
            std::array<f32, 12> bp = {};
            if (mapping.irNodeIndex >= 0 &&
                mapping.irNodeIndex < static_cast<int32_t>(ir.nodes.size()))
            {
                bp = transformBP(ir.nodes[mapping.irNodeIndex].worldTM);
            }
            model.bindPoses[mapping.objectId] = bp;
        }

        // Trailing identity matrix at index numNodes — required by Retera.
        // Row-major 3×4: [Xaxis(1,0,0), Yaxis(0,1,0), Zaxis(0,0,1), Trans(0,0,0)].
        model.bindPoses[numNodes] = {{
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 0.0f
        }};
    }

    // ── DEBUG: Dump final MDX bone pivots and KGTR keys ──
    {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring path(tmp);
        path += L"mdlx_translation_debug.log";
        std::ofstream dbg(path, std::ios::trunc);
        dbg << "=== MDX Translation Debug ===\n";
        dbg << "invScale=" << invScale << "\n\n";

        // Helper lambda to dump a node's translation track
        auto dumpNode = [&](const char* type, int idx, const Node& node, uint32_t objectId) {
            dbg << type << "[" << idx << "] '" << node.name << "'"
                << " objectId=" << objectId
                << " parentId=" << node.parentId << "\n";

            // Pivot
            if (objectId < model.pivotPoints.size()) {
                auto& p = model.pivotPoints[objectId];
                dbg << "  pivot=(" << p.x << ", " << p.y << ", " << p.z << ")\n";
            }

            // Parent pivot
            if (node.parentId < model.pivotPoints.size() && node.parentId != 0xFFFFFFFF) {
                auto& pp = model.pivotPoints[node.parentId];
                dbg << "  parentPivot=(" << pp.x << ", " << pp.y << ", " << pp.z << ")\n";
                if (objectId < model.pivotPoints.size()) {
                    auto& p = model.pivotPoints[objectId];
                    dbg << "  pivotDelta=(" << (p.x - pp.x) << ", " << (p.y - pp.y) << ", " << (p.z - pp.z) << ")\n";
                }
            }

            // KGTR keys
            auto& tr = node.translationTracks;
            if (tr.isUsed && tr.keyCount > 0) {
                bool hasTangents = (tr.interpolationType == InterpolationType::Hermite ||
                                    tr.interpolationType == InterpolationType::Bezier);
                dbg << "  KGTR: " << tr.keyCount << " keys, interp="
                    << static_cast<int>(tr.interpolationType) << "\n";

                int showKeys = tr.keyCount > 10 ? 10 : static_cast<int>(tr.keyCount);
                if (!hasTangents) {
                    auto keys = tr.keys();
                    for (int k = 0; k < showKeys; ++k) {
                        dbg << "    [" << k << "] t=" << tr.timestamps[k] << "ms"
                            << " v=(" << keys[k].x << ", " << keys[k].y
                            << ", " << keys[k].z << ")\n";
                    }
                    if (tr.keyCount > 10u) dbg << "    ... (" << tr.keyCount << " total)\n";
                } else {
                    auto keys = tr.tangentKeys();
                    for (int k = 0; k < showKeys; ++k) {
                        dbg << "    [" << k << "] t=" << tr.timestamps[k] << "ms"
                            << " v=(" << keys[k].value.x << ", " << keys[k].value.y
                            << ", " << keys[k].value.z << ")\n";
                    }
                    if (tr.keyCount > 10u) dbg << "    ... (" << tr.keyCount << " total)\n";
                }
            } else {
                dbg << "  KGTR: none\n";
            }

            // KGRT summary
            auto& rt = node.rotationTracks;
            if (rt.isUsed && rt.keyCount > 0) {
                dbg << "  KGRT: " << rt.keyCount << " keys\n";
            }

            // KGSC (scale) tracks
            auto& sc = node.scalingTracks;
            if (sc.isUsed && sc.keyCount > 0) {
                bool scTangents = (sc.interpolationType == InterpolationType::Hermite ||
                                   sc.interpolationType == InterpolationType::Bezier);
                dbg << "  KGSC: " << sc.keyCount << " keys, interp="
                    << static_cast<int>(sc.interpolationType) << "\n";
                int showSc = sc.keyCount > 10 ? 10 : static_cast<int>(sc.keyCount);
                if (!scTangents) {
                    auto keys = sc.keys();
                    for (int k = 0; k < showSc; ++k) {
                        dbg << "    [" << k << "] t=" << sc.timestamps[k] << "ms"
                            << " s=(" << keys[k].x << ", " << keys[k].y
                            << ", " << keys[k].z << ")\n";
                    }
                    if (sc.keyCount > 10u) dbg << "    ... (" << sc.keyCount << " total)\n";
                } else {
                    auto keys = sc.tangentKeys();
                    for (int k = 0; k < showSc; ++k) {
                        dbg << "    [" << k << "] t=" << sc.timestamps[k] << "ms"
                            << " s=(" << keys[k].value.x << ", " << keys[k].value.y
                            << ", " << keys[k].value.z << ")\n";
                    }
                    if (sc.keyCount > 10u) dbg << "    ... (" << sc.keyCount << " total)\n";
                }
            }

            dbg << "\n";
        };

        for (size_t i = 0; i < model.bones.size(); ++i)
            dumpNode("BONE", (int)i, model.bones[i].node, model.bones[i].node.objectId);
        for (size_t i = 0; i < model.helpers.size(); ++i)
            dumpNode("HELPER", (int)i, model.helpers[i].node, model.helpers[i].node.objectId);

        dbg << "=== END ===\n";
        dbg.flush();
    }

    return model;
}
