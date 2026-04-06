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
#include <algorithm>

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
                               : 0xFFFFFFFF;
    out.keyCount = irTrack.keys.size();

    bool hasTangents = (irTrack.interpolation == ir::InterpolationType::Hermite ||
                        irTrack.interpolation == ir::InterpolationType::Bezier);

    if (hasTangents) {
        using TK = typename Track<MdxT>::TangentKey;
        out.keys_data.resize(irTrack.keys.size() * sizeof(TK));
        auto* tangentKeys = reinterpret_cast<TK*>(out.keys_data.data());
        for (size_t k = 0; k < irTrack.keys.size(); k++) {
            tangentKeys[k].frame = mdx_transform::ticksToMs(irTrack.keys[k].time);
            tangentKeys[k].value = valueFn(irTrack.keys[k].value);
            tangentKeys[k].inTan = valueFn(irTrack.keys[k].inTangent);
            tangentKeys[k].outTan = valueFn(irTrack.keys[k].outTangent);
        }
    } else {
        using K = typename Track<MdxT>::Key;
        out.keys_data.resize(irTrack.keys.size() * sizeof(K));
        auto* keys = reinterpret_cast<K*>(out.keys_data.data());
        for (size_t k = 0; k < irTrack.keys.size(); k++) {
            keys[k].frame = mdx_transform::ticksToMs(irTrack.keys[k].time);
            keys[k].value = valueFn(irTrack.keys[k].value);
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
               Node::NodeType type, Node::NodeFlag extraFlags)
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

    // Animation tracks
    for (auto& na : ir.nodeAnimations) {
        if (na.nodeIndex != irNodeIndex) continue;
        node.translationTracks = convertTrack<Vector3f, Point3>(na.translation, positionTransform);
        node.rotationTracks = convertTrack<Quaternion, Quat>(na.rotation, rotationTransform);
        node.scalingTracks = convertTrack<Vector3f, Point3>(na.scale, scaleTransform);
        break;
    }

    return node;
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

} // anonymous namespace

Model MdxModelBuilder::build(const ir::IRModel& ir, const MdxExportOptions& opts) {
    Model model;
    model.version = opts.version;
    model.blendTime = opts.blendTime;

    // 1. Hierarchy
    MdxHierarchyResolver hierarchy;
    hierarchy.resolve(ir, opts.version);

    // 2. Textures
    for (auto& tex : ir.textures) {
        Texture mdxTex;
        mdxTex.fileName = tex.filePath;
        mdxTex.replaceableId = tex.replaceableId;
        mdxTex.flags = (tex.wrapU ? 1u : 0u) | (tex.wrapV ? 2u : 0u);
        model.textures.push_back(std::move(mdxTex));
    }

    // 3. Materials
    MdxMaterialMapper matMapper;
    for (auto& mat : ir.materials)
        model.materials.push_back(matMapper.map(mat, ir, opts.version));

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
        mdxSeq.flags = seq.flags | (seq.isLooping ? 0u : 1u);
        mdxSeq.rarity = seq.rarity;
        model.sequences.push_back(std::move(mdxSeq));
    }

    // 6. Bones + helpers
    for (auto& bone : ir.bones) {
        if (bone.isHelper && opts.version < 1200) {
            // v800: separate helper
            Helper h;
            h.node = buildNode(ir, bone.nodeIndex, hierarchy,
                               Node::NodeType::Helper, Node::NodeFlag::None);
            model.helpers.push_back(std::move(h));
        } else {
            // Bone (or v1200 helper-as-bone)
            Bone b;
            b.node = buildNode(ir, bone.nodeIndex, hierarchy,
                               Node::NodeType::Bone, Node::NodeFlag::Bone);
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
                mdx_transform::position(ir.nodes[mapping.irNodeIndex].pivotPoint);
        }
    }

    // 8. Geosets
    for (auto& irMesh : ir.meshes) {
        Geoset geo;
        geo.materialId = (irMesh.materialIndex >= 0)
                             ? static_cast<uint32_t>(irMesh.materialIndex) : 0;

        // Vertex data
        for (auto& v : irMesh.vertices) {
            geo.vertexPositions.push_back(mdx_transform::position(v.position));
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
                // SKIN: 4 bone indices (u8) + 4 weights (u8) = 8 bytes per vertex
                // boneIndex is an IR node index; convert to hierarchy objectId
                uint8_t boneIds[4] = {0, 0, 0, 0};
                uint8_t weights[4] = {0, 0, 0, 0};
                size_t count = std::min(v.skinInfluences.size(), size_t(4));
                for (size_t si = 0; si < count; si++) {
                    uint32_t objId = hierarchy.getObjectId(v.skinInfluences[si].boneIndex);
                    boneIds[si] = (objId != Node::NO_PARENT)
                                      ? static_cast<uint8_t>(objId) : 0;
                    weights[si] = static_cast<uint8_t>(
                        v.skinInfluences[si].weight * 255.0f + 0.5f);
                }
                for (int bi = 0; bi < 4; bi++) geo.skinData.push_back(boneIds[bi]);
                for (int wi = 0; wi < 4; wi++) geo.skinData.push_back(weights[wi]);
            }

            // v800 vertex groups
            if (!v.skinInfluences.empty()) {
                // Vertex group = index into matrixGroups (resolved later)
                geo.vertexGroups.push_back(0); // placeholder, resolved below
            } else {
                geo.vertexGroups.push_back(0);
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
                std::map<std::vector<uint32_t>, uint32_t> groupMap;
                std::vector<std::vector<uint32_t>> groupBones;

                for (auto& v : irMesh.vertices) {
                    std::vector<uint32_t> boneIds;
                    for (auto& inf : v.skinInfluences) {
                        if (inf.boneIndex < 0 || inf.weight <= 0.0f) continue;
                        uint32_t objId = hierarchy.getObjectId(inf.boneIndex);
                        if (objId != Node::NO_PARENT)
                            boneIds.push_back(objId);
                    }
                    std::sort(boneIds.begin(), boneIds.end());
                    boneIds.erase(std::unique(boneIds.begin(), boneIds.end()), boneIds.end());
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

        geo.selectionGroup = 0;
        geo.selectionFlags = 0;
        geo.lod = 0;

        model.geosets.push_back(std::move(geo));
    }

    // Merge geosets if enabled
    if (opts.mergeGeosets) {
        MdxGeosetMerger merger;
        merger.merge(model.geosets);
    }

    // 9. Lights
    for (auto& irLight : ir.lights) {
        wdx::Light light;
        light.node = buildNode(ir, irLight.nodeIndex, hierarchy,
                               Node::NodeType::Light, Node::NodeFlag::Light);

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

        light.attenuationStartTracks = getFloatTrack(ir, irLight.attStartTrackIndex);
        light.attenuationEndTracks = getFloatTrack(ir, irLight.attEndTrackIndex);
        light.intensityTracks = getFloatTrack(ir, irLight.intensityTrackIndex);
        light.ambientIntensityTracks = getFloatTrack(ir, irLight.ambIntensityTrackIndex);
        light.visibilityTracks = getFloatTrack(ir, irLight.visibilityTrackIndex);
        light.colorTracks = getColorTrack(ir, irLight.colorTrackIndex);
        light.ambientColorTracks = getColorTrack(ir, irLight.ambColorTrackIndex);

        model.lights.push_back(std::move(light));
    }

    // 10. Attachments
    for (auto& irAt : ir.attachments) {
        Attachment attach;
        attach.node = buildNode(ir, irAt.nodeIndex, hierarchy,
                                Node::NodeType::Attachment, Node::NodeFlag::Attachment);
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
                                Node::NodeFlag::ParticleEmitter);
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
                                Node::NodeFlag::ParticleEmitter);

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
                                  Node::NodeType::CornEmitter, Node::NodeFlag::None);
            corn.lifeSpan = irPe.lifespan;
            corn.emissionRate = irPe.emissionRate;
            corn.speed = irPe.speed;
            corn.replaceableId = irPe.replaceableId;
            corn.path = irPe.modelPath;
            corn.color = {irPe.segmentColors[0].r, irPe.segmentColors[0].g,
                          irPe.segmentColors[0].b, irPe.segmentAlpha[0]};
            model.cornEmitters.push_back(std::move(corn));
        }
    }

    // 12. Ribbon emitters
    for (auto& irRib : ir.ribbonEmitters) {
        RibbonEmitter rib;
        rib.node = buildNode(ir, irRib.nodeIndex, hierarchy,
                             Node::NodeType::RibbonEmitter,
                             Node::NodeFlag::RibbonEmitter);
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
        rib.visibilityTracks = getFloatTrack(ir, irRib.visibilityTrackIndex);

        model.ribbonEmitters.push_back(std::move(rib));
    }

    // 13. Event objects
    for (auto& irEvt : ir.eventObjects) {
        EventObject evt;
        evt.node = buildNode(ir, irEvt.nodeIndex, hierarchy,
                             Node::NodeType::EventObject,
                             Node::NodeFlag::EventObject);
        // Set node name to the event code (e.g., "SND_FAL0")
        evt.node.name = irEvt.eventCode;

        for (auto keyTime : irEvt.keyTimes)
            evt.eventTrackTimes.push_back(mdx_transform::ticksToMs(keyTime));

        model.eventObjects.push_back(std::move(evt));
    }

    // 14. Collision shapes
    for (auto& irCs : ir.collisionShapes) {
        CollisionShape cs;
        cs.node = buildNode(ir, irCs.nodeIndex, hierarchy,
                            Node::NodeType::CollisionShape,
                            Node::NodeFlag::CollisionShape);

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
            cs.vertices.push_back(mdx_transform::position(v));

        cs.radius = irCs.radius;
        model.collisionShapes.push_back(std::move(cs));
    }

    // 15. Cameras
    for (auto& irCam : ir.cameras) {
        wdx::Camera cam;
        cam.name = irCam.name;
        cam.position = mdx_transform::position(irCam.position);
        cam.targetPosition = mdx_transform::position(irCam.targetPosition);
        cam.fieldOfView = irCam.fov;
        cam.nearClippingPlane = irCam.nearClip;
        cam.farClippingPlane = irCam.farClip;

        cam.positionTracks = getVec3Track(ir, irCam.positionTrackIndex);
        cam.targetPositionTracks = getVec3Track(ir, irCam.targetPositionTrackIndex);
        cam.targetRotationTracks = getFloatTrack(ir, irCam.rotationTrackIndex);

        model.cameras.push_back(std::move(cam));
    }

    // 16. Geoset animations (visibility per geoset)
    if (!ir.geosetAnims.empty()) {
        for (auto& irGa : ir.geosetAnims) {
            GeosetAnimation ga;
            ga.alpha = irGa.alpha;
            ga.geosetId = (irGa.meshIndex >= 0) ? static_cast<uint32_t>(irGa.meshIndex) : 0;
            ga.color = {irGa.color.r, irGa.color.g, irGa.color.b};
            ga.alphaTracks = getFloatTrack(ir, irGa.alphaTrackIndex);
            ga.colorTracks = getColorTrack(ir, irGa.colorTrackIndex);
            // flags bit 0 = use color (static or animated)
            bool hasNonWhiteColor = (irGa.color.r < 0.999f || irGa.color.g < 0.999f || irGa.color.b < 0.999f);
            if (ga.colorTracks.isUsed || hasNonWhiteColor)
                ga.flags = 1u;
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
            fe.target = fx->facefxName;
            fe.path = fx->facefxPath;
            model.faceEffects.push_back(std::move(fe));
        }
    }

    // 20. Bind poses (v > 800)
    //     Every node in the hierarchy gets a 3×4 row-major matrix derived from
    //     its world transform at frame 0, matching the MaxScript exporter which
    //     uses node.maxObj.transform for ALL node types (not just bones).
    if (opts.version > 800) {
        model.bindPoses.resize(hierarchy.totalNodes());

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
    }

    return model;
}
