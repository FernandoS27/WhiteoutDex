// MDLXImporter — MdxModelDisassembler implementation
#include "mdx_model_disassembler.h"
#include "mdx_coord_transform.h"
#include "mdx_track_mapper.h"

namespace wdx = whiteout::mdx;

// ── Centralized MDX → Max coordinate transform ─────────────
// Applied once after all data has been mapped to IR types.
// MDX(x,y,z)        → Max(y, -x, z)       positions / normals
// MDX quat(x,y,z,w) → Max Quat(y, -x, z, w) rotations
// MDX(x,y,z)        → Max(y, x, z)        scale (no negation)
// Bind pose matrix:  full basis change M_max = C⁻¹ · M_mdx · C

namespace {

void swizzlePos(Point3& p)    { p = Point3(p.y, -p.x, p.z); }
void swizzleScale(Point3& s)  { s = Point3(s.y, s.x, s.z); }
void swizzleQuat(Quat& q)     { q = Quat(q.y, -q.x, q.z, q.w); }
void swizzleTangent(Point4& t) { t = Point4(t.y, -t.x, t.z, t.w); }

void transformPosTrack(ir::Vec3Track& track) {
    for (auto& k : track.keys) {
        swizzlePos(k.value);
        if (k.hasTangents) { swizzlePos(k.inTangent); swizzlePos(k.outTangent); }
    }
}
void transformScaleTrack(ir::Vec3Track& track) {
    for (auto& k : track.keys) {
        swizzleScale(k.value);
        if (k.hasTangents) { swizzleScale(k.inTangent); swizzleScale(k.outTangent); }
    }
}
void transformQuatTrack(ir::QuatTrack& track) {
    for (auto& k : track.keys) {
        swizzleQuat(k.value);
        if (k.hasTangents) { swizzleQuat(k.inTangent); swizzleQuat(k.outTangent); }
    }
}

void transformToMaxCoordinates(ir::IRModel& ir) {
    // ── Nodes ──
    for (auto& node : ir.nodes)
        swizzlePos(node.pivotPoint);

    // ── Bones ──
    for (auto& bone : ir.bones) {
        swizzlePos(bone.pivotPoint);
        // Full basis change for bind pose matrix
        Matrix3& m = bone.bindPose;
        Point3 r0 = m.GetRow(0), r1 = m.GetRow(1), r2 = m.GetRow(2), t = m.GetRow(3);
        m.SetRow(0, Point3( r1.y, -r1.x,  r1.z));
        m.SetRow(1, Point3(-r0.y,  r0.x, -r0.z));
        m.SetRow(2, Point3( r2.y, -r2.x,  r2.z));
        swizzlePos(t);
        m.SetRow(3, t);
    }

    // ── Sequences ──
    for (auto& seq : ir.sequences) {
        swizzlePos(seq.extentMin);
        swizzlePos(seq.extentMax);
    }

    // ── Meshes ──
    for (auto& mesh : ir.meshes) {
        for (auto& vert : mesh.vertices) {
            swizzlePos(vert.position);
            swizzlePos(vert.normal);
            if (vert.hasTangent)
                swizzleTangent(vert.tangent);
        }
        for (auto& ext : mesh.sequenceExtents) {
            swizzlePos(ext.minBound);
            swizzlePos(ext.maxBound);
        }
        swizzlePos(mesh.globalExtent.minBound);
        swizzlePos(mesh.globalExtent.maxBound);
    }

    // ── Node Animations (inline tracks) ──
    for (auto& na : ir.nodeAnimations) {
        transformPosTrack(na.translation);
        transformQuatTrack(na.rotation);
        transformScaleTrack(na.scale);
    }

    // ── Cameras ──
    for (auto& cam : ir.cameras) {
        swizzlePos(cam.position);
        swizzlePos(cam.targetPosition);
        if (cam.positionTrackIndex >= 0)
            transformPosTrack(ir.vec3Tracks[cam.positionTrackIndex]);
        if (cam.targetPositionTrackIndex >= 0)
            transformPosTrack(ir.vec3Tracks[cam.targetPositionTrackIndex]);
    }

    // ── Texture Animations (shared pool tracks) ──
    for (auto& ta : ir.textureAnimations) {
        if (ta.translationTrackIndex >= 0)
            transformPosTrack(ir.vec3Tracks[ta.translationTrackIndex]);
        if (ta.rotationTrackIndex >= 0)
            transformQuatTrack(ir.quatTracks[ta.rotationTrackIndex]);
        if (ta.scaleTrackIndex >= 0)
            transformScaleTrack(ir.vec3Tracks[ta.scaleTrackIndex]);
    }

    // ── Collision Shapes ──
    for (auto& col : ir.collisionShapes)
        for (auto& v : col.vertices)
            swizzlePos(v);
}

} // anonymous namespace

namespace mdx_disasm {

// ── Track storage helpers ───────────────────────────────────

int32_t MdxModelDisassembler::storeFloatTrack(ir::IRModel& ir, ir::FloatTrack&& track) {
    if (track.empty()) return -1;
    int32_t idx = static_cast<int32_t>(ir.floatTracks.size());
    ir.floatTracks.push_back(std::move(track));
    return idx;
}

int32_t MdxModelDisassembler::storeColorTrack(ir::IRModel& ir, ir::ColorTrack&& track) {
    if (track.empty()) return -1;
    int32_t idx = static_cast<int32_t>(ir.colorTracks.size());
    ir.colorTracks.push_back(std::move(track));
    return idx;
}

int32_t MdxModelDisassembler::storeVec3Track(ir::IRModel& ir, ir::Vec3Track&& track) {
    if (track.empty()) return -1;
    int32_t idx = static_cast<int32_t>(ir.vec3Tracks.size());
    ir.vec3Tracks.push_back(std::move(track));
    return idx;
}

int32_t MdxModelDisassembler::storeVec4Track(ir::IRModel& ir, ir::Vec4Track&& track) {
    if (track.empty()) return -1;
    int32_t idx = static_cast<int32_t>(ir.vec4Tracks.size());
    ir.vec4Tracks.push_back(std::move(track));
    return idx;
}

int32_t MdxModelDisassembler::storeIntTrack(ir::IRModel& ir, ir::IntTrack&& track) {
    if (track.empty()) return -1;
    int32_t idx = static_cast<int32_t>(ir.intTracks.size());
    ir.intTracks.push_back(std::move(track));
    return idx;
}

// ── Main disassemble ────────────────────────────────────────

ir::IRModel MdxModelDisassembler::disassemble(
    const wdx::Model& mdxModel, const MdlxImportOptions& options)
{
    ir::IRModel ir;
    version_ = mdxModel.version;

    hierarchy_.build(mdxModel);

    mapNodes(mdxModel, ir);
    mapBones(mdxModel, ir);
    mapSequences(mdxModel, ir);
    mapGlobalSequences(mdxModel, ir);
    mapTextures(mdxModel, ir);
    mapMaterials(mdxModel, ir);
    mapTextureAnimations(mdxModel, ir);
    mapGeosets(mdxModel, ir);
    mapGeosetAnimations(mdxModel, ir);
    mapLights(mdxModel, ir);
    mapAttachments(mdxModel, ir);
    mapParticleEmitters(mdxModel, ir);
    mapParticleEmitters2(mdxModel, ir);
    mapRibbonEmitters(mdxModel, ir);
    mapEventObjects(mdxModel, ir);
    mapCameras(mdxModel, ir);
    mapCollisionShapes(mdxModel, ir);

    if (version_ >= 1200) {
        mapCornEmitters(mdxModel, ir);
        mapFaceEffects(mdxModel, ir);
    }

    mapNodeAnimations(mdxModel, ir);

    // Single pass: convert all positions, rotations, scales from MDX to Max coordinates
    transformToMaxCoordinates(ir);

    return ir;
}

// ── Nodes ───────────────────────────────────────────────────

void MdxModelDisassembler::mapNodes(const wdx::Model& mdx, ir::IRModel& ir) {
    ir.nodes.resize(hierarchy_.nodeCount());
    for (const auto& mn : hierarchy_.nodes()) {
        auto& irNode = ir.nodes[mn.irIndex];
        irNode.parentIndex = mn.irParent;

        // Pivot points (indexed by MDX objectId) — raw MDX coords, transformed later
        if (mn.mdxObjectId < mdx.pivotPoints.size()) {
            const auto& pp = mdx.pivotPoints[mn.mdxObjectId];
            irNode.pivotPoint = Point3(pp.x, pp.y, pp.z);
        }
    }

    // Fill names and node flags from bone/helper/etc source data
    auto setNodeInfo = [&](uint32_t objectId, const std::string& name, uint32_t flags) {
        int32_t idx = hierarchy_.irIndexForObjectId(objectId);
        if (idx >= 0 && idx < static_cast<int32_t>(ir.nodes.size())) {
            ir.nodes[idx].name = name;
            ir.nodes[idx].nodeFlags = flags;
        }
    };

    for (const auto& bone : mdx.bones)
        setNodeInfo(bone.node.objectId, bone.node.name, static_cast<uint32_t>(bone.node.flags));
    for (const auto& helper : mdx.helpers)
        setNodeInfo(helper.node.objectId, helper.node.name, static_cast<uint32_t>(helper.node.flags));
    for (const auto& light : mdx.lights)
        setNodeInfo(light.node.objectId, light.node.name, static_cast<uint32_t>(light.node.flags));
    for (const auto& att : mdx.attachments)
        setNodeInfo(att.node.objectId, att.node.name, static_cast<uint32_t>(att.node.flags));
    for (const auto& pe : mdx.particleEmitters)
        setNodeInfo(pe.node.objectId, pe.node.name, static_cast<uint32_t>(pe.node.flags));
    for (const auto& pe2 : mdx.particleEmitters2)
        setNodeInfo(pe2.node.objectId, pe2.node.name, static_cast<uint32_t>(pe2.node.flags));
    for (const auto& rib : mdx.ribbonEmitters)
        setNodeInfo(rib.node.objectId, rib.node.name, static_cast<uint32_t>(rib.node.flags));
    for (const auto& evt : mdx.eventObjects)
        setNodeInfo(evt.node.objectId, evt.node.name, static_cast<uint32_t>(evt.node.flags));
    for (const auto& col : mdx.collisionShapes)
        setNodeInfo(col.node.objectId, col.node.name, static_cast<uint32_t>(col.node.flags));
    for (const auto& corn : mdx.cornEmitters)
        setNodeInfo(corn.node.objectId, corn.node.name, static_cast<uint32_t>(corn.node.flags));
}

// ── Bones & Helpers ─────────────────────────────────────────

void MdxModelDisassembler::mapBones(const wdx::Model& mdx, ir::IRModel& ir) {
    for (const auto& bone : mdx.bones) {
        int32_t irIdx = hierarchy_.irIndexForObjectId(bone.node.objectId);
        if (irIdx < 0) continue;

        ir::Bone irBone;
        irBone.name = bone.node.name;
        irBone.nodeIndex = irIdx;
        irBone.parentIndex = hierarchy_.resolveParent(bone.node.parentId);
        irBone.isHelper = hierarchy_.nodes()[irIdx].isHelper;
        irBone.type = irBone.isHelper ? ir::BoneType::Helper : ir::BoneType::Standard;
        irBone.nodeFlags = static_cast<uint32_t>(bone.node.flags);

        if (bone.node.objectId < mdx.pivotPoints.size()) {
            const auto& pp = mdx.pivotPoints[bone.node.objectId];
            irBone.pivotPoint = Point3(pp.x, pp.y, pp.z);
        }

        // Bind pose — stored in raw MDX coords, transformed later
        if (version_ >= 1200 && bone.node.objectId < mdx.bindPoses.size()) {
            const auto& bp = mdx.bindPoses[bone.node.objectId];
            Matrix3 m;
            m.SetRow(0, Point3(bp[0], bp[1], bp[2]));
            m.SetRow(1, Point3(bp[3], bp[4], bp[5]));
            m.SetRow(2, Point3(bp[6], bp[7], bp[8]));
            m.SetRow(3, Point3(bp[9], bp[10], bp[11]));
            irBone.bindPose = m;
        } else {
            irBone.bindPose.IdentityMatrix();
            irBone.bindPose.SetTrans(irBone.pivotPoint);
        }

        ir.bones.push_back(std::move(irBone));
    }

    // Helpers (v800 only — separate array)
    for (const auto& helper : mdx.helpers) {
        int32_t irIdx = hierarchy_.irIndexForObjectId(helper.node.objectId);
        if (irIdx < 0) continue;

        ir::Bone irBone;
        irBone.name = helper.node.name;
        irBone.nodeIndex = irIdx;
        irBone.parentIndex = hierarchy_.resolveParent(helper.node.parentId);
        irBone.isHelper = true;
        irBone.type = ir::BoneType::Helper;
        irBone.nodeFlags = static_cast<uint32_t>(helper.node.flags);

        if (helper.node.objectId < mdx.pivotPoints.size()) {
            const auto& pp = mdx.pivotPoints[helper.node.objectId];
            irBone.pivotPoint = Point3(pp.x, pp.y, pp.z);
        }

        irBone.bindPose.IdentityMatrix();
        irBone.bindPose.SetTrans(irBone.pivotPoint);

        ir.bones.push_back(std::move(irBone));
    }
}

// ── Sequences ───────────────────────────────────────────────

void MdxModelDisassembler::mapSequences(const wdx::Model& mdx, ir::IRModel& ir) {
    ir.sequences.reserve(mdx.sequences.size());
    for (const auto& seq : mdx.sequences) {
        ir::Sequence irSeq;
        irSeq.name = seq.name;
        irSeq.startTime = mdx_coord::msToTicks(seq.intervalStart);
        irSeq.endTime = mdx_coord::msToTicks(seq.intervalEnd);
        irSeq.isLooping = (seq.flags & 1) == 0; // MDX flag bit 0 = NonLooping
        irSeq.rarity = seq.rarity;
        irSeq.moveSpeed = seq.moveSpeed;
        irSeq.flags = seq.flags;

        irSeq.extentMin = Point3(seq.extent.minimum.x, seq.extent.minimum.y, seq.extent.minimum.z);
        irSeq.extentMax = Point3(seq.extent.maximum.x, seq.extent.maximum.y, seq.extent.maximum.z);
        irSeq.extentRadius = seq.extent.boundsRadius;

        ir.sequences.push_back(std::move(irSeq));
    }
}

void MdxModelDisassembler::mapGlobalSequences(const wdx::Model& mdx, ir::IRModel& ir) {
    ir.globalSequenceDurations = mdx.globalSequences;
}

// ── Textures ────────────────────────────────────────────────

void MdxModelDisassembler::mapTextures(const wdx::Model& mdx, ir::IRModel& ir) {
    ir.textures.reserve(mdx.textures.size());
    for (const auto& tex : mdx.textures) {
        ir::Texture irTex;
        irTex.filePath = tex.fileName;
        irTex.replaceableId = static_cast<int32_t>(tex.replaceableId);
        irTex.wrapU = (tex.flags & 1) != 0;
        irTex.wrapV = (tex.flags & 2) != 0;
        ir.textures.push_back(std::move(irTex));
    }
}

// ── Materials ───────────────────────────────────────────────

void MdxModelDisassembler::mapMaterials(const wdx::Model& mdx, ir::IRModel& ir) {
    ir.materials.reserve(mdx.materials.size());
    for (const auto& mat : mdx.materials) {
        ir::Material irMat;
        irMat.priorityPlane = static_cast<int32_t>(mat.priorityPlane);
        irMat.flags = mat.flags;
        irMat.shaderName = mat.shader;

        for (const auto& layer : mat.layers) {
            ir::MaterialLayer irLayer;
            irLayer.blendMode = static_cast<ir::BlendMode>(layer.filterMode);
            irLayer.alpha = layer.alpha;
            irLayer.uvSetIndex = static_cast<int32_t>(layer.coordId);

            // Shading flags
            irLayer.unshaded = wdx::hasFlag(layer.shadingFlags,
                wdx::Layer::ShadingFlag::Unshaded);
            irLayer.twoSided = wdx::hasFlag(layer.shadingFlags,
                wdx::Layer::ShadingFlag::TwoSided);
            irLayer.noDepthTest = wdx::hasFlag(layer.shadingFlags,
                wdx::Layer::ShadingFlag::NoDepthTest);
            irLayer.noDepthWrite = wdx::hasFlag(layer.shadingFlags,
                wdx::Layer::ShadingFlag::NoDepthSet);
            irLayer.sphereEnvMap = wdx::hasFlag(layer.shadingFlags,
                wdx::Layer::ShadingFlag::SphereEnvMap);
            irLayer.unfogged = wdx::hasFlag(layer.shadingFlags,
                wdx::Layer::ShadingFlag::Unfogged);

            // Texture references.
            // v1100+ (and upgraded v800): textureId is always 0; actual textures are in subTextures.
            // Raw v800 (no upgrade): subTextures is empty, textureId holds the diffuse index.
            if (!layer.subTextures.empty()) {
                // Detect upgraded v800 HD materials: WhiteoutLib UpgradeOldVersions sets
                // ALL sub-textures to DiffuseMap slot. For these, assign PBR slots by
                // the WC3 Reforged layer convention (diffuse=0, normal=1, ORM=2, emissive=3, env=4).
                bool allDiffuseSlots = layer.is_hd && layer.subTextures.size() > 1;
                if (allDiffuseSlots) {
                    for (const auto& sub : layer.subTextures) {
                        if (sub.slot != wdx::Layer::SlotType::DiffuseMap) {
                            allDiffuseSlots = false;
                            break;
                        }
                    }
                }

                static const ir::TextureSlot kHdSlotOrder[] = {
                    ir::TextureSlot::Diffuse,
                    ir::TextureSlot::Normal,
                    ir::TextureSlot::ORM,
                    ir::TextureSlot::Emissive,
                    ir::TextureSlot::Environment   // TeamColor was not a layer slot in v800/v1100
                };

                for (size_t si = 0; si < layer.subTextures.size(); si++) {
                    const auto& sub = layer.subTextures[si];
                    ir::TextureRef ref;
                    ref.textureIndex = static_cast<int32_t>(sub.textureId);

                    if (allDiffuseSlots) {
                        ref.slot = (si < 5) ? kHdSlotOrder[si] : ir::TextureSlot::Diffuse;
                    } else {
                        switch (sub.slot) {
                        case wdx::Layer::SlotType::NormalMap:      ref.slot = ir::TextureSlot::Normal; break;
                        case wdx::Layer::SlotType::ORMMap:         ref.slot = ir::TextureSlot::ORM; break;
                        case wdx::Layer::SlotType::EmissiveMap:    ref.slot = ir::TextureSlot::Emissive; break;
                        case wdx::Layer::SlotType::TeamColor:      ref.slot = ir::TextureSlot::TeamColor; break;
                        case wdx::Layer::SlotType::EnvironmentMap: ref.slot = ir::TextureSlot::Environment; break;
                        default: ref.slot = ir::TextureSlot::Diffuse; break;
                        }
                    }
                    irLayer.textureRefs.push_back(ref);
                }
            } else if (layer.textureId != 0 && layer.textureId < mdx.textures.size()) {
                // Raw v800 without sub-textures (textureId is the actual diffuse texture index).
                // textureId == 0 is the reset value for v1100+ layers — not a real texture ref.
                ir::TextureRef ref;
                ref.textureIndex = static_cast<int32_t>(layer.textureId);
                ref.slot = ir::TextureSlot::Diffuse;
                irLayer.textureRefs.push_back(ref);
            }

            // HD (Reforged) PBR properties — applies to is_hd layers regardless of version
            if (layer.is_hd) {
                irLayer.emissiveGain = layer.emissiveGain;
                irLayer.fresnelColor = Point3(layer.fresnelColor.x,
                                               layer.fresnelColor.y,
                                               layer.fresnelColor.z);
                irLayer.fresnelOpacity = layer.fresnelOpacity;
                irLayer.fresnelTeamColor = layer.fresnelTeamColor;
            }

            if (layer.alphaTracks.isUsed)
                irLayer.alphaTrackIndex = storeFloatTrack(ir,
                    mapFloatTrack(layer.alphaTracks));

            if (layer.textureIdTracks.isUsed) {
                irLayer.textureIdTrackIndex = static_cast<int32_t>(ir.intTracks.size());
                ir.intTracks.push_back(mapIntTrack(layer.textureIdTracks));
            }

            // v1200 HD animated tracks
            if (version_ >= 1200 && layer.is_hd) {
                if (layer.emissiveGainTracks.isUsed)
                    irLayer.emissiveGainTrackIndex = storeFloatTrack(ir,
                        mapFloatTrack(layer.emissiveGainTracks));
                if (layer.fresnelColorTracks.isUsed)
                    irLayer.fresnelColorTrackIndex = storeColorTrack(ir,
                        mapColorTrack(layer.fresnelColorTracks));
                if (layer.fresnelAlphaTracks.isUsed)
                    irLayer.fresnelAlphaTrackIndex = storeFloatTrack(ir,
                        mapFloatTrack(layer.fresnelAlphaTracks));
                if (layer.fresnelTeamColorTracks.isUsed)
                    irLayer.fresnelTeamColorTrackIndex = storeFloatTrack(ir,
                        mapFloatTrack(layer.fresnelTeamColorTracks));
            }

            if (layer.textureAnimationId < mdx.textureAnimations.size())
                irLayer.textureAnimationIndex = static_cast<int32_t>(layer.textureAnimationId);

            irMat.layers.push_back(std::move(irLayer));
        }

        ir.materials.push_back(std::move(irMat));
    }
}

// ── Texture Animations ──────────────────────────────────────

void MdxModelDisassembler::mapTextureAnimations(const wdx::Model& mdx, ir::IRModel& ir) {
    ir.textureAnimations.reserve(mdx.textureAnimations.size());
    for (const auto& ta : mdx.textureAnimations) {
        ir::TextureAnimation irTA;
        if (ta.translationTracks.isUsed)
            irTA.translationTrackIndex = storeVec3Track(ir,
                mapPositionTrack(ta.translationTracks));

        if (ta.rotationTracks.isUsed) {
            auto qtrack = mapRotationTrack(ta.rotationTracks);
            irTA.rotationTrackIndex = static_cast<int32_t>(ir.quatTracks.size());
            ir.quatTracks.push_back(std::move(qtrack));
        }

        if (ta.scalingTracks.isUsed)
            irTA.scaleTrackIndex = storeVec3Track(ir,
                mapScaleTrack(ta.scalingTracks));

        ir.textureAnimations.push_back(std::move(irTA));
    }
}

// ── Geosets (Meshes) ────────────────────────────────────────

void MdxModelDisassembler::mapGeosets(const wdx::Model& mdx, ir::IRModel& ir) {
    ir.meshes.reserve(mdx.geosets.size());

    for (size_t gi = 0; gi < mdx.geosets.size(); ++gi) {
        const auto& geo = mdx.geosets[gi];
        ir::Mesh irMesh;
        irMesh.name = "Geoset" + std::to_string(gi);
        irMesh.materialIndex = static_cast<int32_t>(geo.materialId);

        size_t vertCount = geo.vertexPositions.size();
        irMesh.vertices.resize(vertCount);

        // Positions & normals — raw MDX coords, transformed later
        for (size_t v = 0; v < vertCount; ++v) {
            const auto& vp = geo.vertexPositions[v];
            irMesh.vertices[v].position = Point3(vp.x, vp.y, vp.z);
            if (v < geo.vertexNormals.size()) {
                const auto& vn = geo.vertexNormals[v];
                irMesh.vertices[v].normal = Point3(vn.x, vn.y, vn.z);
            }
        }

        // UV sets
        int32_t uvCount = static_cast<int32_t>(geo.textureCoordinateSets.size());
        for (int32_t uv = 0; uv < uvCount && uv < 4; ++uv) {
            const auto& uvSet = geo.textureCoordinateSets[uv];
            for (size_t v = 0; v < vertCount && v < uvSet.size(); ++v) {
                irMesh.vertices[v].uvSets[uv] = Point2(uvSet[v].x, uvSet[v].y);
                irMesh.vertices[v].uvSetCount = std::max(irMesh.vertices[v].uvSetCount, uv + 1);
            }
        }

        // Tangents (v1200) — raw MDX coords, transformed later
        if (!geo.tangents.empty()) {
            for (size_t v = 0; v < vertCount && v < geo.tangents.size(); ++v) {
                const auto& t = geo.tangents[v];
                irMesh.vertices[v].tangent = Point4(t.x, t.y, t.z, t.w);
                irMesh.vertices[v].hasTangent = true;
            }
        }

        // Face indices — MDX and Max both use CCW winding in right-handed Z-up,
        // so copy in file order (no reversal).
        irMesh.indices.reserve(geo.faces.size());
        for (auto idx : geo.faces)
            irMesh.indices.push_back(static_cast<uint32_t>(idx));

        // Skinning: v1200 SKIN data vs v800 matrix groups
        if (version_ >= 1200 && !geo.skinData.empty()) {
            // SKIN: 4 bone indices + 4 weights per vertex (8 bytes per vertex).
            //
            // Each SKIN byte is an index into the flat matrix table (the raw
            // concatenation of MATS values in file order). That table entry is
            // a bone objectId which we then resolve through the hierarchy.
            // This matches the reference MaxScript importer (applySkinning in
            // WhiteoutDexSceneRebuilder.ms — flatBoneIds + flatToSkin).
            //
            // Fallback: if MATS is empty for any reason, treat the SKIN byte
            // directly as an objectId (matches our exporter's v1200 output).
            const auto& flatMats = geo.matrixIndices;

            for (size_t v = 0; v < vertCount; ++v) {
                size_t base = v * 8;
                if (base + 7 >= geo.skinData.size()) break;

                auto& vert = irMesh.vertices[v];
                for (int j = 0; j < 4; ++j) {
                    uint8_t boneIdx = geo.skinData[base + j];
                    uint8_t weight = geo.skinData[base + 4 + j];
                    if (weight == 0) continue;

                    uint32_t objectId;
                    if (!flatMats.empty() && boneIdx < flatMats.size())
                        objectId = flatMats[boneIdx];
                    else
                        objectId = static_cast<uint32_t>(boneIdx);

                    ir::SkinInfluence inf;
                    inf.boneIndex = hierarchy_.irIndexForObjectId(objectId);
                    inf.weight = static_cast<float>(weight) / 255.0f;
                    if (inf.boneIndex >= 0)
                        vert.skinInfluences.push_back(inf);
                }
            }
        } else if (!geo.matrixGroups.empty()) {
            // v800 matrix groups: vertexGroups[v] → index into matrixGroups
            // matrixGroups tells how many bones per group; matrixIndices has the bone ObjectIDs
            std::vector<size_t> groupStarts;
            groupStarts.reserve(geo.matrixGroups.size());
            size_t offset = 0;
            for (auto groupSize : geo.matrixGroups) {
                groupStarts.push_back(offset);
                offset += groupSize;
            }

            for (size_t v = 0; v < vertCount; ++v) {
                if (v >= geo.vertexGroups.size()) break;
                uint8_t groupIdx = geo.vertexGroups[v];
                if (groupIdx >= geo.matrixGroups.size()) continue;

                uint32_t groupSize = geo.matrixGroups[groupIdx];
                size_t start = groupStarts[groupIdx];
                float equalWeight = (groupSize > 0) ? (1.0f / static_cast<float>(groupSize)) : 0.0f;

                auto& vert = irMesh.vertices[v];
                for (uint32_t j = 0; j < groupSize; ++j) {
                    if (start + j >= geo.matrixIndices.size()) break;
                    uint32_t boneObjectId = geo.matrixIndices[start + j];
                    int32_t irBoneIdx = hierarchy_.irIndexForObjectId(boneObjectId);
                    if (irBoneIdx < 0) continue;

                    ir::SkinInfluence inf;
                    inf.boneIndex = irBoneIdx;
                    inf.weight = equalWeight;
                    vert.skinInfluences.push_back(inf);
                }
            }
        }

        // Sequence extents
        for (const auto& ext : geo.sequenceExtents) {
            ir::Mesh::SequenceExtent irExt;
            irExt.minBound = Point3(ext.minimum.x, ext.minimum.y, ext.minimum.z);
            irExt.maxBound = Point3(ext.maximum.x, ext.maximum.y, ext.maximum.z);
            irExt.boundRadius = ext.boundsRadius;
            irMesh.sequenceExtents.push_back(irExt);
        }

        irMesh.globalExtent.minBound = Point3(geo.extent.minimum.x, geo.extent.minimum.y, geo.extent.minimum.z);
        irMesh.globalExtent.maxBound = Point3(geo.extent.maximum.x, geo.extent.maximum.y, geo.extent.maximum.z);
        irMesh.globalExtent.boundRadius = geo.extent.boundsRadius;

        ir.meshes.push_back(std::move(irMesh));
    }
}

// ── Geoset Animations ───────────────────────────────────────

void MdxModelDisassembler::mapGeosetAnimations(const wdx::Model& mdx, ir::IRModel& ir) {
    for (const auto& ga : mdx.geosetAnimations) {
        ir::IRModel::GeosetAnim irGA;
        irGA.meshIndex = static_cast<int32_t>(ga.geosetId);
        irGA.alpha = ga.alpha;
        irGA.color = Color(ga.color.z, ga.color.y, ga.color.x);  // MDX BGR → RGB
        irGA.dropShadow = (ga.flags & 0x1) != 0;  // bit 0: drop shadow
        irGA.usesColor  = (ga.flags & 0x2) != 0;  // bit 1: uses color

        if (ga.alphaTracks.isUsed)
            irGA.alphaTrackIndex = storeFloatTrack(ir, mapFloatTrack(ga.alphaTracks));
        if (ga.colorTracks.isUsed) {
            // GeosetAnimation color is BGR in MDX — swap to RGB
            auto bgrTrack = mapTrack<whiteout::Vector3f, Color>(
                ga.colorTracks,
                [](const whiteout::Vector3f& v) { return Color(v.z, v.y, v.x); });
            irGA.colorTrackIndex = storeColorTrack(ir, std::move(bgrTrack));
        }

        ir.geosetAnims.push_back(std::move(irGA));
    }
}

// ── Lights ──────────────────────────────────────────────────

void MdxModelDisassembler::mapLights(const wdx::Model& mdx, ir::IRModel& ir) {
    for (const auto& light : mdx.lights) {
        ir::Light irLight;
        irLight.nodeIndex = hierarchy_.irIndexForObjectId(light.node.objectId);

        switch (light.type) {
        case wdx::Light::LightType::Omni:        irLight.type = ir::Light::Type::Omni; break;
        case wdx::Light::LightType::Directional:  irLight.type = ir::Light::Type::Directional; break;
        case wdx::Light::LightType::Ambient:       irLight.type = ir::Light::Type::Ambient; break;
        }

        irLight.attenuationStart = light.attenuationStart;
        irLight.attenuationEnd = light.attenuationEnd;
        irLight.color = Color(light.color.x, light.color.y, light.color.z);
        irLight.intensity = light.intensity;
        irLight.ambientColor = Color(light.ambientColor.x, light.ambientColor.y, light.ambientColor.z);
        irLight.ambientIntensity = light.ambientIntensity;

        if (light.attenuationStartTracks.isUsed)
            irLight.attStartTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(light.attenuationStartTracks));
        if (light.attenuationEndTracks.isUsed)
            irLight.attEndTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(light.attenuationEndTracks));
        if (light.colorTracks.isUsed)
            irLight.colorTrackIndex = storeColorTrack(ir,
                mapColorTrack(light.colorTracks));
        if (light.intensityTracks.isUsed)
            irLight.intensityTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(light.intensityTracks));
        if (light.ambientColorTracks.isUsed)
            irLight.ambColorTrackIndex = storeColorTrack(ir,
                mapColorTrack(light.ambientColorTracks));
        if (light.ambientIntensityTracks.isUsed)
            irLight.ambIntensityTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(light.ambientIntensityTracks));
        if (light.visibilityTracks.isUsed)
            irLight.visibilityTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(light.visibilityTracks));
        if (light.shadowIntensityTracks.isUsed)
            irLight.shadowIntensityTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(light.shadowIntensityTracks));

        ir.lights.push_back(std::move(irLight));
    }
}

// ── Attachments ─────────────────────────────────────────────

void MdxModelDisassembler::mapAttachments(const wdx::Model& mdx, ir::IRModel& ir) {
    for (const auto& att : mdx.attachments) {
        ir::Attachment irAtt;
        irAtt.nodeIndex = hierarchy_.irIndexForObjectId(att.node.objectId);
        irAtt.name = att.node.name;
        irAtt.path = att.path;
        irAtt.attachmentId = static_cast<int32_t>(att.attachmentId);

        if (att.visibilityTracks.isUsed)
            irAtt.visibilityTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(att.visibilityTracks));

        ir.attachments.push_back(std::move(irAtt));
    }
}

// ── Particle Emitters 1 ─────────────────────────────────────

void MdxModelDisassembler::mapParticleEmitters(const wdx::Model& mdx, ir::IRModel& ir) {
    for (const auto& pe : mdx.particleEmitters) {
        ir::ParticleEmitter irPE;
        irPE.nodeIndex = hierarchy_.irIndexForObjectId(pe.node.objectId);
        irPE.variant = 1;
        irPE.emissionRate = pe.emissionRate;
        irPE.speed = pe.initialVelocity;
        irPE.gravity = pe.gravity;
        irPE.latitude = pe.latitude;
        irPE.longitude = pe.longitude;
        irPE.lifespan = pe.lifespan;
        irPE.modelPath = pe.spawnModelFileName;

        if (pe.emissionRateTracks.isUsed)
            irPE.emissionRateTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(pe.emissionRateTracks));
        if (pe.speedTracks.isUsed)
            irPE.speedTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(pe.speedTracks));
        if (pe.gravityTracks.isUsed)
            irPE.gravityTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(pe.gravityTracks));
        if (pe.latitudeTracks.isUsed)
            irPE.latitudeTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(pe.latitudeTracks));
        if (pe.longitudeTracks.isUsed)
            irPE.longitudeTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(pe.longitudeTracks));
        if (pe.lifespanTracks.isUsed)
            irPE.lifespanTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(pe.lifespanTracks));
        if (pe.visibilityTracks.isUsed)
            irPE.visibilityTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(pe.visibilityTracks));

        ir.particleEmitters.push_back(std::move(irPE));
    }
}

// ── Particle Emitters 2 ─────────────────────────────────────

void MdxModelDisassembler::mapParticleEmitters2(const wdx::Model& mdx, ir::IRModel& ir) {
    for (const auto& pe2 : mdx.particleEmitters2) {
        ir::ParticleEmitter irPE;
        irPE.nodeIndex = hierarchy_.irIndexForObjectId(pe2.node.objectId);
        irPE.variant = 2;
        irPE.emissionRate = pe2.emissionRate;
        irPE.speed = pe2.speed;
        irPE.variation = pe2.variation;
        irPE.lifespan = pe2.lifespan;
        irPE.gravity = pe2.gravity;
        irPE.latitude = pe2.latitude;
        irPE.width = pe2.width;
        irPE.length = pe2.length;
        irPE.tailLength = pe2.tailLength;
        irPE.filterMode = static_cast<int32_t>(pe2.filterMode);
        irPE.rows = static_cast<int32_t>(pe2.rows);
        irPE.columns = static_cast<int32_t>(pe2.columns);
        irPE.headOrTail = static_cast<int32_t>(pe2.headOrTail);
        irPE.priorityPlane = static_cast<int32_t>(pe2.priorityPlane);
        irPE.midTime = pe2.time;
        irPE.textureIndex = static_cast<int32_t>(pe2.textureId);
        irPE.replaceableId = static_cast<int32_t>(pe2.replaceableId);
        // IR flags layout: bit 0 = squirt, bits 15-20 = PE2-specific node flags
        // (DontInherit/billboard flags at bits 1-7 go to irNode.nodeFlags, not here)
        irPE.flags = static_cast<uint32_t>(pe2.node.flags) & 0x1F8000u;
        if (pe2.squirt)
            irPE.flags |= 0x1;

        for (int i = 0; i < 3; ++i) {
            irPE.segmentColors[i] = Color(pe2.segmentColor[i].x,
                                           pe2.segmentColor[i].y,
                                           pe2.segmentColor[i].z);
            irPE.segmentAlpha[i] = static_cast<float>(pe2.segmentAlpha[i]) / 255.0f;
            irPE.segmentScale[i] = pe2.segmentScaling[i];
            irPE.headInterval[i] = static_cast<int32_t>(pe2.headInterval[i]);
            irPE.headDecayInterval[i] = static_cast<int32_t>(pe2.headDecayInterval[i]);
            irPE.tailInterval[i] = static_cast<int32_t>(pe2.tailInterval[i]);
            irPE.tailDecayInterval[i] = static_cast<int32_t>(pe2.tailDecayInterval[i]);
        }

        if (pe2.emissionRateTracks.isUsed)
            irPE.emissionRateTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(pe2.emissionRateTracks));
        if (pe2.speedTracks.isUsed)
            irPE.speedTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(pe2.speedTracks));
        if (pe2.variationTracks.isUsed)
            irPE.variationTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(pe2.variationTracks));
        if (pe2.gravityTracks.isUsed)
            irPE.gravityTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(pe2.gravityTracks));
        if (pe2.latitudeTracks.isUsed)
            irPE.latitudeTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(pe2.latitudeTracks));
        if (pe2.lengthTracks.isUsed)
            irPE.lengthTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(pe2.lengthTracks));
        if (pe2.widthTracks.isUsed)
            irPE.widthTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(pe2.widthTracks));
        if (pe2.visibilityTracks.isUsed)
            irPE.visibilityTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(pe2.visibilityTracks));

        ir.particleEmitters.push_back(std::move(irPE));
    }
}

// ── Ribbon Emitters ─────────────────────────────────────────

void MdxModelDisassembler::mapRibbonEmitters(const wdx::Model& mdx, ir::IRModel& ir) {
    for (const auto& rib : mdx.ribbonEmitters) {
        ir::RibbonEmitter irRib;
        irRib.nodeIndex = hierarchy_.irIndexForObjectId(rib.node.objectId);
        irRib.heightAbove = rib.heightAbove;
        irRib.heightBelow = rib.heightBelow;
        irRib.alpha = rib.alpha;
        irRib.color = Color(rib.color.x, rib.color.y, rib.color.z);
        irRib.lifespan = rib.lifespan;
        irRib.materialIndex = static_cast<int32_t>(rib.materialId);
        irRib.textureSlot = static_cast<int32_t>(rib.textureSlot);
        irRib.emissionRate = static_cast<int32_t>(rib.emissionRate);
        irRib.rows = static_cast<int32_t>(rib.rows);
        irRib.columns = static_cast<int32_t>(rib.columns);
        irRib.gravity = rib.gravity;

        if (rib.heightAboveTracks.isUsed)
            irRib.heightAboveTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(rib.heightAboveTracks));
        if (rib.heightBelowTracks.isUsed)
            irRib.heightBelowTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(rib.heightBelowTracks));
        if (rib.alphaTracks.isUsed)
            irRib.alphaTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(rib.alphaTracks));
        if (rib.colorTracks.isUsed)
            irRib.colorTrackIndex = storeColorTrack(ir,
                mapColorTrack(rib.colorTracks));
        if (rib.visibilityTracks.isUsed)
            irRib.visibilityTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(rib.visibilityTracks));
        if (rib.textureSlotTracks.isUsed)
            irRib.textureSlotTrackIndex = storeIntTrack(ir,
                mapIntTrack(rib.textureSlotTracks));

        ir.ribbonEmitters.push_back(std::move(irRib));
    }
}

// ── Event Objects ───────────────────────────────────────────

void MdxModelDisassembler::mapEventObjects(const wdx::Model& mdx, ir::IRModel& ir) {
    for (const auto& evt : mdx.eventObjects) {
        ir::EventObject irEvt;
        irEvt.nodeIndex = hierarchy_.irIndexForObjectId(evt.node.objectId);

        // Parse event code from name (e.g., "SNDxAbcd" → code="SNDx", data="Abcd")
        const auto& name = evt.node.name;
        if (name.size() >= 4) {
            irEvt.eventCode = name.substr(0, 4);
            if (name.size() > 4)
                irEvt.eventData = name.substr(4);
        } else {
            irEvt.eventCode = name;
        }

        for (auto time : evt.eventTrackTimes)
            irEvt.keyTimes.push_back(mdx_coord::msToTicks(time));

        ir.eventObjects.push_back(std::move(irEvt));
    }
}

// ── Cameras ─────────────────────────────────────────────────

void MdxModelDisassembler::mapCameras(const wdx::Model& mdx, ir::IRModel& ir) {
    for (const auto& cam : mdx.cameras) {
        ir::Camera irCam;
        irCam.name = cam.name;
        irCam.position = Point3(cam.position.x, cam.position.y, cam.position.z);
        irCam.targetPosition = Point3(cam.targetPosition.x, cam.targetPosition.y, cam.targetPosition.z);
        irCam.fov = cam.fieldOfView;
        irCam.nearClip = cam.nearClippingPlane;
        irCam.farClip = cam.farClippingPlane;

        if (cam.positionTracks.isUsed)
            irCam.positionTrackIndex = storeVec3Track(ir,
                mapPositionTrack(cam.positionTracks));
        if (cam.targetPositionTracks.isUsed)
            irCam.targetPositionTrackIndex = storeVec3Track(ir,
                mapPositionTrack(cam.targetPositionTracks));
        if (cam.targetRotationTracks.isUsed)
            irCam.rotationTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(cam.targetRotationTracks));

        ir.cameras.push_back(std::move(irCam));
    }
}

// ── Collision Shapes ────────────────────────────────────────

void MdxModelDisassembler::mapCollisionShapes(const wdx::Model& mdx, ir::IRModel& ir) {
    for (const auto& col : mdx.collisionShapes) {
        ir::CollisionShape irCol;
        irCol.nodeIndex = hierarchy_.irIndexForObjectId(col.node.objectId);

        switch (col.type) {
        case wdx::CollisionShape::ShapeType::Box:      irCol.shape = ir::CollisionShape::Shape::Box; break;
        case wdx::CollisionShape::ShapeType::Sphere:   irCol.shape = ir::CollisionShape::Shape::Sphere; break;
        case wdx::CollisionShape::ShapeType::Cylinder:  irCol.shape = ir::CollisionShape::Shape::Cylinder; break;
        case wdx::CollisionShape::ShapeType::Plane:     irCol.shape = ir::CollisionShape::Shape::Plane; break;
        }

        for (const auto& v : col.vertices)
            irCol.vertices.push_back(Point3(v.x, v.y, v.z));

        irCol.radius = col.radius;
        ir.collisionShapes.push_back(std::move(irCol));
    }
}

// ── Corn Emitters (v1200) ───────────────────────────────────

void MdxModelDisassembler::mapCornEmitters(const wdx::Model& mdx, ir::IRModel& ir) {
    // Corn emitters are stored as ParticleEmitters with a distinct variant
    // For now, store metadata via ExtensionData on the IR node
    for (const auto& corn : mdx.cornEmitters) {
        ir::ParticleEmitter irPE;
        irPE.nodeIndex = hierarchy_.irIndexForObjectId(corn.node.objectId);
        irPE.variant = 3; // Corn emitter variant
        irPE.lifespan = corn.lifeSpan;
        irPE.emissionRate = corn.emissionRate;
        irPE.speed = corn.speed;
        irPE.replaceableId = static_cast<int32_t>(corn.replaceableId);
        irPE.modelPath = corn.path;

        if (corn.emissionRateTracks.isUsed)
            irPE.emissionRateTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(corn.emissionRateTracks));
        if (corn.speedTracks.isUsed)
            irPE.speedTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(corn.speedTracks));
        if (corn.lifeSpanTracks.isUsed)
            irPE.lifespanTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(corn.lifeSpanTracks));
        if (corn.visibilityTracks.isUsed)
            irPE.visibilityTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(corn.visibilityTracks));
        if (corn.colorTracks.isUsed)
            irPE.colorTrackIndex = storeVec4Track(ir,
                mapVec4Track(corn.colorTracks));
        if (corn.lifeSpanVariationTracks.isUsed)
            irPE.lifespanVariationTrackIndex = storeFloatTrack(ir,
                mapFloatTrack(corn.lifeSpanVariationTracks));

        ir.particleEmitters.push_back(std::move(irPE));
    }
}

// ── Face Effects (v1200) ────────────────────────────────────

void MdxModelDisassembler::mapFaceEffects(const wdx::Model& mdx, ir::IRModel& ir) {
    // FaceEffects don't have a direct IR counterpart — store as extension data
    // on a placeholder node. For now, create AttachmentS with a special marker.
    for (const auto& ffx : mdx.faceEffects) {
        ir::Attachment irAtt;
        irAtt.nodeIndex = -1; // FaceEffects don't have a node in the hierarchy
        irAtt.name = ffx.target;
        irAtt.path = ffx.path;
        irAtt.attachmentId = -1; // Marker for FaceFX
        ir.attachments.push_back(std::move(irAtt));
    }
}

// ── Node Animations (transform tracks) ──────────────────────

void MdxModelDisassembler::mapNodeAnimations(const wdx::Model& mdx, ir::IRModel& ir) {
    // Collect transform tracks from all node types that have them
    auto processNode = [&](const wdx::Node& node) {
        int32_t irIdx = hierarchy_.irIndexForObjectId(node.objectId);
        if (irIdx < 0) return;

        bool hasTranslation = node.translationTracks.isUsed;
        bool hasRotation = node.rotationTracks.isUsed;
        bool hasScale = node.scalingTracks.isUsed;

        if (!hasTranslation && !hasRotation && !hasScale) return;

        ir::NodeAnimation na;
        na.nodeIndex = irIdx;

        if (hasTranslation)
            na.translation = mapPositionTrack(node.translationTracks);
        if (hasRotation)
            na.rotation = mapRotationTrack(node.rotationTracks);
        if (hasScale)
            na.scale = mapScaleTrack(node.scalingTracks);

        ir.nodeAnimations.push_back(std::move(na));
    };

    for (const auto& bone : mdx.bones) processNode(bone.node);
    for (const auto& helper : mdx.helpers) processNode(helper.node);
    for (const auto& light : mdx.lights) processNode(light.node);
    for (const auto& att : mdx.attachments) processNode(att.node);
    for (const auto& pe : mdx.particleEmitters) processNode(pe.node);
    for (const auto& pe2 : mdx.particleEmitters2) processNode(pe2.node);
    for (const auto& rib : mdx.ribbonEmitters) processNode(rib.node);
    for (const auto& evt : mdx.eventObjects) processNode(evt.node);
    for (const auto& col : mdx.collisionShapes) processNode(col.node);
    for (const auto& corn : mdx.cornEmitters) processNode(corn.node);
}

} // namespace mdx_disasm
