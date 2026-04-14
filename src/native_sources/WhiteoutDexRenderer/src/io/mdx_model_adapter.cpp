// ============================================================================
// MDX Model Adapter — Translates WhiteoutLib MDX types to IModelSource.
// ============================================================================

#include "mdx_model_adapter.h"
#include "content_provider.h"
#include "team_glow_data.h"
#include <cmath>
#include <cstdio>
#include <whiteout/textures/blp/blp.h>
#include <whiteout/textures/dds/parser.h>
#include <whiteout/textures/tga/parser.h>
#include <whiteout/textures/png/parser.h>
#include <whiteout/textures/texture.h>

namespace WhiteoutDex {

using namespace whiteout;
using namespace whiteout::mdx;
namespace fs = std::filesystem;

// ============================================================================
// MDX → Max (renderer-native) coordinate transform
// Mirrors MDLXImporter's transformToMaxCoordinates, but operates directly on
// whiteout::mdx::Model so the entire model is in renderer-native space after
// load. Applied once in the adapter constructor; all downstream getters then
// just copy values without per-call swizzling.
//   MDX(x,y,z)        → Max(y, -x, z)       positions / normals
//   MDX quat(x,y,z,w) → Max quat(y, -x, z, w)
//   MDX(x,y,z)        → Max(y,  x, z)       scale (no negation)
//   MDX(x,y,z,w)      → Max(y, -x, z, w)    tangent (w = handedness, kept)
//   Bind pose 3x4:    full basis change M_max = C^-1 · M_mdx · C
// ============================================================================
namespace {

inline void swizPos(Vector3f& v)   { v = {v.y, -v.x, v.z}; }
inline void swizScale(Vector3f& v) { v = {v.y,  v.x, v.z}; }
inline void swizQuat(Quaternion& q){ q = {q.y, -q.x, q.z, q.w}; }
inline void swizTangent(Vector4f& t){ t = {t.y, -t.x, t.z, t.w}; }

template <typename T, typename Fn>
void transformTrack(Track<T>& track, Fn fn) {
    if (!track.isUsed || track.keys_data.empty()) return;
    if (isSmoothInterpolation(track.interpolationType)) {
        for (auto& k : track.tangentKeys()) {
            fn(k.value);
            fn(k.inTan);
            fn(k.outTan);
        }
    } else {
        for (auto& k : track.keys()) {
            fn(k.value);
        }
    }
}

void transformNodeTracks(Node& n) {
    transformTrack(n.translationTracks, [](Vector3f& v){ swizPos(v); });
    transformTrack(n.rotationTracks,    [](Quaternion& q){ swizQuat(q); });
    transformTrack(n.scalingTracks,     [](Vector3f& v){ swizScale(v); });
}

// 3x4 bind pose layout (per BindPose3x4ToMatrix44f):
//   row0 basis X = bp[0..2]
//   row1 basis Y = bp[3..5]
//   row2 basis Z = bp[6..8]
//   row3 trans   = bp[9..11]
// Apply full basis change M_max = C^-1 · M_mdx · C with C = [0 -1 0; 1 0 0; 0 0 1]
void transformBindPose(std::array<f32, 12>& bp) {
    Vector3f r0{bp[0], bp[1], bp[2]};
    Vector3f r1{bp[3], bp[4], bp[5]};
    Vector3f r2{bp[6], bp[7], bp[8]};
    Vector3f t {bp[9], bp[10], bp[11]};

    bp[0] =  r1.y; bp[1] = -r1.x; bp[2] =  r1.z;
    bp[3] = -r0.y; bp[4] =  r0.x; bp[5] = -r0.z;
    bp[6] =  r2.y; bp[7] = -r2.x; bp[8] =  r2.z;
    swizPos(t);
    bp[9] = t.x; bp[10] = t.y; bp[11] = t.z;
}

void TransformMdxModelToMaxCoords(whiteout::mdx::Model& m) {
    // Geosets: positions, normals, tangents, extents
    for (auto& gs : m.geosets) {
        for (auto& v : gs.vertexPositions) swizPos(v);
        for (auto& v : gs.vertexNormals)   swizPos(v);
        for (auto& t : gs.tangents)        swizTangent(t);
        swizPos(gs.extent.minimum);
        swizPos(gs.extent.maximum);
        for (auto& ext : gs.sequenceExtents) {
            swizPos(ext.minimum);
            swizPos(ext.maximum);
        }
    }

    // Pivot points (indexed by objectId)
    for (auto& p : m.pivotPoints) swizPos(p);

    // Sequence and model extents
    for (auto& s : m.sequences) {
        swizPos(s.extent.minimum);
        swizPos(s.extent.maximum);
    }
    swizPos(m.modelExtent.minimum);
    swizPos(m.modelExtent.maximum);

    // Node-bearing structures: transform their TRS tracks
    for (auto& b  : m.bones)             transformNodeTracks(b.node);
    for (auto& h  : m.helpers)           transformNodeTracks(h.node);
    for (auto& a  : m.attachments)       transformNodeTracks(a.node);
    for (auto& l  : m.lights)            transformNodeTracks(l.node);
    for (auto& pe : m.particleEmitters)  transformNodeTracks(pe.node);
    for (auto& pe : m.particleEmitters2) transformNodeTracks(pe.node);
    for (auto& re : m.ribbonEmitters)    transformNodeTracks(re.node);
    for (auto& eo : m.eventObjects)      transformNodeTracks(eo.node);
    for (auto& ce : m.cornEmitters)      transformNodeTracks(ce.node);
    for (auto& cs : m.collisionShapes) {
        transformNodeTracks(cs.node);
        for (auto& v : cs.vertices) swizPos(v);
    }

    // Cameras: static + animated positions
    for (auto& c : m.cameras) {
        swizPos(c.position);
        swizPos(c.targetPosition);
        transformTrack(c.positionTracks,       [](Vector3f& v){ swizPos(v); });
        transformTrack(c.targetPositionTracks, [](Vector3f& v){ swizPos(v); });
    }

    // Reforged bind poses
    for (auto& bp : m.bindPoses) transformBindPose(bp);

    // Note: TextureAnimation tracks are 2D UV-space — no 3D swizzle.
}

inline Vector3f toXM(const Vector3f& v) { return v; }

} // namespace

// ============================================================================
// Constructor
// ============================================================================

MdxModelAdapter::MdxModelAdapter(whiteout::mdx::Model model, fs::path basePath,
                                 CoordSpace /*space*/,
                                 IContentProvider* contentProvider)
    : model_(std::move(model))
    , basePath_(std::move(basePath))
    , resolver_(basePath_)
    , contentProvider_(contentProvider) {
    // Apply the full MDX → renderer-native coordinate transform once, on the
    // whole model. Both standalone and Max-plugin PE1 paths load raw MDX, so
    // both need this. The CoordSpace parameter is now ignored (kept for ABI
    // compatibility with existing call sites).
    TransformMdxModelToMaxCoords(model_);

    hierarchy_.Build(model_);
}

// ============================================================================
// GetMeshes — MDX geosets are already indexed
// ============================================================================

std::vector<MeshData> MdxModelAdapter::GetMeshes() {
    std::vector<MeshData> result;
    result.reserve(model_.geosets.size());

    for (int i = 0; i < (int)model_.geosets.size(); i++) {
        const auto& gs = model_.geosets[i];
        MeshData mesh;
        mesh.geosetId   = i;
        mesh.materialId = (int)gs.materialId;

        int vc = (int)gs.vertexPositions.size();
        mesh.positions.resize(vc);
        mesh.normals.resize(vc);
        mesh.uvs.resize(vc);

        for (int v = 0; v < vc; v++) {
            mesh.positions[v] = toXM(gs.vertexPositions[v]);
            if (v < (int)gs.vertexNormals.size())
                mesh.normals[v] = toXM(gs.vertexNormals[v]);
            if (!gs.textureCoordinateSets.empty() &&
                v < (int)gs.textureCoordinateSets[0].size()) {
                mesh.uvs[v] = {gs.textureCoordinateSets[0][v].x,
                               gs.textureCoordinateSets[0][v].y};
            }
        }

        mesh.indices.resize(gs.faces.size());
        for (int f = 0; f < (int)gs.faces.size(); f++)
            mesh.indices[f] = gs.faces[f];

        result.push_back(std::move(mesh));
    }
    return result;
}

// ============================================================================
// GetTextures — Load BLP/DDS/TGA/PNG files; generate TeamColor/TeamGlow
// ============================================================================

TextureData MdxModelAdapter::LoadTextureFile(const std::string& path,
                                              int textureId,
                                              int replaceableId) const {
    TextureData td;
    td.textureId     = textureId;
    td.replaceableId = replaceableId;
    td.width = td.height = 0;

    // Parse a texture from a decoded result into td.
    auto applyResult = [&](whiteout::textures::Texture& tex) {
        if (tex.format() != whiteout::textures::PixelFormat::RGBA8)
            tex.format(whiteout::textures::PixelFormat::RGBA8);
        auto pixels = tex.mipData(0);
        td.width  = (int)tex.width();
        td.height = (int)tex.height();
        td.rgba.assign(pixels.begin(), pixels.end());
    };

    // Try parsing from a file path on disk.
    auto tryParsePath = [&](const fs::path& p) -> bool {
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        std::optional<whiteout::textures::Texture> result;

        if (ext == ".blp") {
            whiteout::textures::blp::Parser parser;
            result = parser.parse(p.string());
        } else if (ext == ".dds") {
            whiteout::textures::dds::Parser parser;
            result = parser.parse(p.string());
        } else if (ext == ".tga") {
            whiteout::textures::tga::Parser parser;
            result = parser.parse(p.string());
        } else if (ext == ".png") {
            whiteout::textures::png::Parser parser;
            result = parser.parse(p.string());
        }

        if (result) { applyResult(*result); return true; }
        return false;
    };

    // Try parsing from a memory buffer (CASC/MPQ source).
    auto tryParseBuffer = [&](std::span<const uint8_t> buf, const std::string& ext) -> bool {
        std::optional<whiteout::textures::Texture> result;

        if (ext == ".blp") {
            whiteout::textures::blp::Parser parser;
            result = parser.parse(buf);
        } else if (ext == ".dds") {
            whiteout::textures::dds::Parser parser;
            result = parser.parse(buf);
        } else if (ext == ".tga") {
            whiteout::textures::tga::Parser parser;
            result = parser.parse(buf);
        } else if (ext == ".png") {
            whiteout::textures::png::Parser parser;
            result = parser.parse(buf);
        }

        if (result) { applyResult(*result); return true; }
        return false;
    };

    // 1. Try local disk via FileResolver.
    fs::path resolved = resolver_.ResolveTexture(path);
    if (!resolved.empty() && tryParsePath(resolved)) {
        std::fprintf(stdout, "  [tex %d] loaded %s\n", textureId, resolved.string().c_str());
        return td;
    }

    // 2. Try CASC/MPQ via FileContentProvider.
    if (contentProvider_) {
        std::string foundExt;
        auto data = contentProvider_->ReadFile(path, &foundExt);
        if (data) {
            if (foundExt.empty()) {
                foundExt = fs::path(path).extension().string();
                std::transform(foundExt.begin(), foundExt.end(), foundExt.begin(), ::tolower);
            }
            if (tryParseBuffer(*data, foundExt)) {
                std::fprintf(stdout, "  [tex %d] loaded from archive: %s\n",
                             textureId, path.c_str());
                return td;
            }
        }
    }

    std::fprintf(stderr, "  [tex %d] NOT FOUND: '%s' (base: %s)\n",
                 textureId, path.c_str(), resolver_.BasePath().string().c_str());

    // Fallback: 4x4 magenta checkerboard
    td.width  = 4;
    td.height = 4;
    td.rgba.resize(4 * 4 * 4);
    for (int j = 0; j < 16; j++) {
        td.rgba[j * 4 + 0] = 255;
        td.rgba[j * 4 + 1] = 0;
        td.rgba[j * 4 + 2] = 255;
        td.rgba[j * 4 + 3] = 255;
    }
    return td;
}

TextureData MdxModelAdapter::GenerateTeamColorTexture(int textureId,
                                                       int replaceableId) const {
    TextureData td;
    td.textureId     = textureId;
    td.replaceableId = replaceableId;
    if (replaceableId == 2) {
        // TeamGlow: decode embedded TGA tinted with default red
        td.rgba = DecodeTeamGlow(255, 0, 0, td.width, td.height);
    } else {
        // TeamColor: solid 4x4 red
        td.width = 4; td.height = 4;
        td.rgba.resize(64);
        for (int j = 0; j < 16; j++) {
            td.rgba[j * 4 + 0] = 255;
            td.rgba[j * 4 + 1] = 0;
            td.rgba[j * 4 + 2] = 0;
            td.rgba[j * 4 + 3] = 255;
        }
    }
    return td;
}

std::vector<TextureData> MdxModelAdapter::GetTextures() {
    std::vector<TextureData> result;
    result.reserve(model_.textures.size());

    for (int i = 0; i < (int)model_.textures.size(); i++) {
        const auto& tex = model_.textures[i];
        TextureData td;
        if (tex.replaceableId == 1 || tex.replaceableId == 2) {
            td = GenerateTeamColorTexture(i, (int)tex.replaceableId);
        } else if (!tex.fileName.empty()) {
            td = LoadTextureFile(tex.fileName, i, (int)tex.replaceableId);
        } else {
            // Empty texture → 4x4 white
            td.textureId     = i;
            td.replaceableId = (int)tex.replaceableId;
            td.width = td.height = 4;
            td.rgba.assign(4 * 4 * 4, 255);
        }
        // Propagate MDX texture wrap flags (0x1 = WrapWidth/U, 0x2 = WrapHeight/V)
        td.wrapFlags = static_cast<uint32_t>(tex.flags) & 0x3;
        result.push_back(std::move(td));
    }
    return result;
}

// ============================================================================
// GetMaterials — Map Layer FilterMode + ShadingFlags to renderer types
// ============================================================================

int MdxModelAdapter::MapShadingFlags(Layer::ShadingFlag sf) const {
    int flags = 0;
    u32 s = (u32)sf;
    if (s & (u32)Layer::ShadingFlag::TwoSided)    flags |= MAT_TWO_SIDED;
    if (s & (u32)Layer::ShadingFlag::Unshaded)     flags |= MAT_UNSHADED;
    if (s & (u32)Layer::ShadingFlag::Unfogged)     flags |= MAT_UNFOGGED;
    if (s & (u32)Layer::ShadingFlag::NoDepthTest)  flags |= MAT_NO_DEPTH_TEST;
    if (s & (u32)Layer::ShadingFlag::NoDepthSet)   flags |= MAT_NO_DEPTH_SET;
    return flags;
}

std::vector<MaterialData> MdxModelAdapter::GetMaterials() {
    std::vector<MaterialData> result;
    result.reserve(model_.materials.size());

    for (int i = 0; i < (int)model_.materials.size(); i++) {
        const auto& mat = model_.materials[i];
        MaterialData md;
        md.materialId    = i;
        md.priorityPlane = (int)mat.priorityPlane;
        md.sortOrder     = 0;

        std::fprintf(stdout, "  [mat %d] %d layer(s)\n", i, (int)mat.layers.size());

        for (int li = 0; li < (int)mat.layers.size(); ++li) {
            const auto& layer = mat.layers[li];
            MaterialLayerData ld;
            ld.filterMode = MapFilterMode((int)layer.filterMode);
            ld.alpha      = layer.alpha;
            ld.flags      = MapShadingFlags(layer.shadingFlags);

            // Texture resolution:
            //   Classic (v800-v1100): layer.textureId indexes the texture array.
            //   Reforged (v1200+): layer.textureId is unused (0) and textures
            //     live in layer.subTextures[], one per slot. This applies to
            //     *all* v1200 layers — the is_hd flag only controls PBR shading
            //     (fresnel, emissive gain), not texture storage layout. We
            //     only sample the diffuse map, so grab the DiffuseMap slot;
            //     if no DiffuseMap is present fall back to subTextures[0].
            if (!layer.subTextures.empty()) {
                int diffuseTex = (int)layer.subTextures[0].textureId;
                for (const auto& sub : layer.subTextures) {
                    if (sub.slot == Layer::SlotType::DiffuseMap) {
                        diffuseTex = (int)sub.textureId;
                        break;
                    }
                }
                ld.textureId = diffuseTex;
            } else {
                ld.textureId = (int)layer.textureId;
            }

            std::fprintf(stdout,
                "    layer %d: fm=%d tex=%d alpha=%.2f flags=0x%x is_hd=%d subTex=%d\n",
                li, (int)layer.filterMode, ld.textureId, ld.alpha, ld.flags,
                (int)layer.is_hd, (int)layer.subTextures.size());

            md.layers.push_back(ld);
        }
        result.push_back(std::move(md));
    }
    return result;
}

// ============================================================================
// GetSkeleton — Build inverse bind matrices
// ============================================================================

SkeletonData MdxModelAdapter::GetSkeleton() {
    SkeletonData sk;
    // Use the full hierarchy node count (bones + helpers + emitters + etc.)
    // as the palette size. MDX vertices can skin to ANY node type via objectId,
    // not just bones. The palette is indexed by node position in the
    // topologically-sorted hierarchy (same as allNodeMatrices from Evaluate).
    sk.nodeCount = hierarchy_.NodeCount();

    // MDX vertices — both v800 and v1200 — are authored in the default pose
    // (every bone T=0, R=identity, S=1). Our local matrix formula
    // M = T(-pivot)·S·R·T(pivot+t) evaluates to identity at default, so the
    // hierarchy produces identity bone world matrices in the bind pose and
    // inverseBind must also be identity. The v1200 BPOS chunk stores absolute
    // bind-pose world matrices as metadata, but they are NOT the inverse-bind
    // for this skinning convention — using them shifts every HD vertex into
    // bone-local space and the model explodes. mdx-m3-viewer ignores BPOS for
    // skinning for the same reason.
    sk.inverseBindMatrices.assign(sk.nodeCount, Matrix44f::identity());

    // Extract billboard flags + rest pivots from ALL hierarchy nodes (any node
    // type can be a skinning target and may have billboard flags). Indexed by
    // node position in the hierarchy — matches allNodeMatrices indexing.
    sk.billboardFlags.assign(sk.nodeCount, 0);
    sk.nodePivots.assign(sk.nodeCount, Vector3f{0, 0, 0});
    const auto& nodes = hierarchy_.Nodes();
    for (int i = 0; i < (int)nodes.size(); i++) {
        uint32_t nf = nodes[i].flags;
        uint32_t bbf = 0;
        using NF = whiteout::mdx::Node::NodeFlag;
        if (nf & (uint32_t)NF::Billboarded)      bbf |= BONE_BILLBOARD_FULL;
        if (nf & (uint32_t)NF::BillboardedLockX) bbf |= BONE_BILLBOARD_LOCK_X;
        if (nf & (uint32_t)NF::BillboardedLockY) bbf |= BONE_BILLBOARD_LOCK_Y;
        if (nf & (uint32_t)NF::BillboardedLockZ) bbf |= BONE_BILLBOARD_LOCK_Z;
        sk.billboardFlags[i] = bbf;

        const auto& p = nodes[i].pivot;
        sk.nodePivots[i] = {p.x, p.y, p.z};
    }

    return sk;
}

// ============================================================================
// GetSkinWeights — v1200 skinData or v800 vertexGroups indirection
// ============================================================================

std::vector<SkinWeightData> MdxModelAdapter::GetSkinWeights() {
    std::vector<SkinWeightData> result;
    result.reserve(model_.geosets.size());

    // MDX matrixIndices reference nodes by objectId. Map to the node's position
    // in the topologically-sorted hierarchy (same index space as allNodeMatrices
    // and the GPU bone palette). Any node type (bone, helper, etc.) is valid.

    for (int gi = 0; gi < (int)model_.geosets.size(); gi++) {
        const auto& gs = model_.geosets[gi];
        int vc = (int)gs.vertexPositions.size();
        SkinWeightData sw;
        sw.geosetId = gi;
        sw.influences.resize(vc);

        if (!gs.skinData.empty()) {
            // v1200: packed u8: 4 bone indices + 4 weights per vertex (8 bytes each)
            // SKIN bytes are indices into the geoset's local matrixIndices (MATS) table,
            // which stores objectIds. If MATS is empty, treat as direct objectId.
            for (int v = 0; v < vc; v++) {
                int base = v * 8;
                if (base + 7 < (int)gs.skinData.size()) {
                    for (int k = 0; k < 4; k++) {
                        uint8_t raw = gs.skinData[base + k];
                        // Resolve to objectId via matrixIndices (if available)
                        int objectId;
                        if (!gs.matrixIndices.empty() && raw < gs.matrixIndices.size())
                            objectId = (int)gs.matrixIndices[raw];
                        else
                            objectId = (int)raw;
                        // Map objectId → node position in hierarchy
                        int nodeIdx = hierarchy_.ObjectIdToNodeIndex(objectId);
                        sw.influences[v].boneIdx[k] = (nodeIdx >= 0) ? nodeIdx : 0;
                        sw.influences[v].weight[k]  = gs.skinData[base + 4 + k] / 255.0f;
                    }
                }
            }
        } else if (!gs.vertexGroups.empty() && !gs.matrixGroups.empty()) {
            // v800: vertexGroups + matrixGroups + matrixIndices indirection
            // Build prefix sums for matrixGroups
            std::vector<u32> groupStart(gs.matrixGroups.size() + 1, 0);
            for (int g = 0; g < (int)gs.matrixGroups.size(); g++)
                groupStart[g + 1] = groupStart[g] + gs.matrixGroups[g];

            for (int v = 0; v < vc && v < (int)gs.vertexGroups.size(); v++) {
                int groupId = gs.vertexGroups[v];
                if (groupId < (int)gs.matrixGroups.size()) {
                    u32 start = groupStart[groupId];
                    u32 count = gs.matrixGroups[groupId];
                    if (count > 4) count = 4;  // clamp to 4 influences
                    float w = (count > 0) ? 1.0f / (float)count : 0.0f;
                    for (u32 k = 0; k < count && (start + k) < gs.matrixIndices.size(); k++) {
                        int objectId = (int)gs.matrixIndices[start + k];
                        int nodeIdx = hierarchy_.ObjectIdToNodeIndex(objectId);
                        sw.influences[v].boneIdx[k] = (nodeIdx >= 0) ? nodeIdx : 0;
                        sw.influences[v].weight[k]  = w;
                    }
                }
            }
        }
        result.push_back(std::move(sw));
    }
    return result;
}

// ============================================================================
// GetParticleConfigs
// ============================================================================

int MdxModelAdapter::MapPE2FilterMode(u32 mdxMode) const {
    // MDX PE2 filterMode: 0=Blend, 1=Additive, 2=Modulate, 3=Modulate2x, 4=AlphaKey
    // Renderer FilterMode: 0=None, 1=Transparent, 2=Blend, 3=Additive,
    //                      4=AddAlpha, 5=Modulate, 6=Modulate2x
    switch (mdxMode) {
        case 0: return FILTER_BLEND;       // Blend
        case 1: return FILTER_ADDITIVE;    // Additive
        case 2: return FILTER_MODULATE;    // Modulate
        case 3: return FILTER_MODULATE_2X; // Modulate2x
        case 4: return FILTER_TRANSPARENT; // AlphaKey → Transparent
        default: return FILTER_BLEND;
    }
}

std::vector<ParticleEmitterConfig> MdxModelAdapter::GetParticleConfigs() {
    std::vector<ParticleEmitterConfig> result;
    result.reserve(model_.particleEmitters2.size());

    for (int i = 0; i < (int)model_.particleEmitters2.size(); i++) {
        const auto& pe = model_.particleEmitters2[i];
        ParticleEmitterConfig cfg;
        cfg.textureId  = (int)pe.textureId;
        cfg.filterMode = MapPE2FilterMode(pe.filterMode);
        cfg.rows       = (int)pe.rows;
        cfg.cols       = (int)pe.columns;
        cfg.lifeSpan   = pe.lifespan;
        cfg.squirt     = (pe.squirt != 0);

        // 3-segment color — MDX PE2 segmentColor is stored RGB (per spec),
        // unlike KGAC geoset anim colors which are BGR.
        cfg.startColor = {pe.segmentColor[0].x, pe.segmentColor[0].y, pe.segmentColor[0].z};
        cfg.midColor   = {pe.segmentColor[1].x, pe.segmentColor[1].y, pe.segmentColor[1].z};
        cfg.endColor   = {pe.segmentColor[2].x, pe.segmentColor[2].y, pe.segmentColor[2].z};

        // Alpha (MDX stores as u8, config expects float 0-255)
        cfg.startAlpha = (float)pe.segmentAlpha[0];
        cfg.midAlpha   = (float)pe.segmentAlpha[1];
        cfg.endAlpha   = (float)pe.segmentAlpha[2];

        // Scale
        cfg.startScale = pe.segmentScaling[0];
        cfg.midScale   = pe.segmentScaling[1];
        cfg.endScale   = pe.segmentScaling[2];
        cfg.midTime    = pe.time;

        // Head/Tail
        // MDX: 0=Head, 1=Tail, 2=Both → Renderer: 1=Head, 2=Tail, 3=Both
        cfg.particleType = (int)pe.headOrTail + 1;
        cfg.tailLength   = pe.tailLength;

        // UV animation frames (headInterval/headDecayInterval/tailInterval/tailDecayInterval)
        cfg.headLifeStart   = (int)pe.headInterval[0];
        cfg.headLifeEnd     = (int)pe.headInterval[1];
        cfg.headLifeRepeat  = (int)pe.headInterval[2];
        cfg.headDecayStart  = (int)pe.headDecayInterval[0];
        cfg.headDecayEnd    = (int)pe.headDecayInterval[1];
        cfg.headDecayRepeat = (int)pe.headDecayInterval[2];
        cfg.tailLifeStart   = (int)pe.tailInterval[0];
        cfg.tailLifeEnd     = (int)pe.tailInterval[1];
        cfg.tailLifeRepeat  = (int)pe.tailInterval[2];
        cfg.tailDecayStart  = (int)pe.tailDecayInterval[0];
        cfg.tailDecayEnd    = (int)pe.tailDecayInterval[1];
        cfg.tailDecayRepeat = (int)pe.tailDecayInterval[2];

        // Flags from node
        u32 nf = (u32)pe.node.flags;
        cfg.modelSpace  = (nf & (u32)Node::NodeFlag::ModelSpace) != 0;
        cfg.xyQuad      = (nf & (u32)Node::NodeFlag::XYQuad)     != 0;
        cfg.sortZ       = (nf & (u32)Node::NodeFlag::SortPrimitives) != 0;
        cfg.unshaded    = (nf & (u32)Node::NodeFlag::Unshaded)   != 0;
        cfg.lineEmitter = (nf & (u32)Node::NodeFlag::LineEmitter) != 0;
        cfg.unfogged    = (nf & (u32)Node::NodeFlag::Unfogged)   != 0;

        cfg.priorityPlane = (int)pe.priorityPlane;
        // MDX has no per-emitter Count cap; leave at default (0 = unlimited)

        result.push_back(cfg);
    }
    return result;
}

// ============================================================================
// GetRibbonConfigs — Ribbon references materialId, must resolve texture
// ============================================================================

std::vector<RibbonEmitterConfig> MdxModelAdapter::GetRibbonConfigs() {
    std::vector<RibbonEmitterConfig> result;
    result.reserve(model_.ribbonEmitters.size());

    for (int i = 0; i < (int)model_.ribbonEmitters.size(); i++) {
        const auto& rb = model_.ribbonEmitters[i];
        RibbonEmitterConfig cfg;

        // Resolve texture + layer flags through material. For ribbons the
        // engine binds CWar3Mat per-layer, so unshaded/twoSided come from the
        // LAYER shading flags, not the node — most Wc3 ribbons set Unshaded
        // on the layer (the node flag is rarely used for ribbons).
        if (rb.materialId < (u32)model_.materials.size() &&
            !model_.materials[rb.materialId].layers.empty()) {
            const auto& layer = model_.materials[rb.materialId].layers[0];

            // Texture resolution — same logic as GetMaterials():
            //   Classic (v800-v1100): layer.textureId indexes the texture array.
            //   Reforged (v1200+): layer.textureId is 0 (unused); textures
            //     live in layer.subTextures[]. Pick the DiffuseMap slot, or
            //     fall back to subTextures[0].
            if (!layer.subTextures.empty()) {
                int diffuseTex = (int)layer.subTextures[0].textureId;
                for (const auto& sub : layer.subTextures) {
                    if (sub.slot == Layer::SlotType::DiffuseMap) {
                        diffuseTex = (int)sub.textureId;
                        break;
                    }
                }
                cfg.textureId = diffuseTex;
            } else {
                cfg.textureId = (int)layer.textureId;
            }

            cfg.filterMode = MapFilterMode((int)layer.filterMode);

            u32 sf = (u32)layer.shadingFlags;
            cfg.unshaded = (sf & (u32)Layer::ShadingFlag::Unshaded) != 0;
            cfg.twoSided = (sf & (u32)Layer::ShadingFlag::TwoSided) != 0;
        }
        // Ribbons are double-sided in the engine regardless of layer flag
        // (CRibbonEmitter::Render does not bind a cull state).
        cfg.twoSided = true;

        cfg.rows     = (int)rb.rows;
        cfg.cols     = (int)rb.columns;
        cfg.emission = (float)rb.emissionRate;
        cfg.life     = rb.lifespan;
        cfg.gravity  = rb.gravity;

        result.push_back(cfg);
    }
    return result;
}

// ============================================================================
// GetCollisionShapes
// ============================================================================

std::vector<CollisionShapeData> MdxModelAdapter::GetCollisionShapes() {
    std::vector<CollisionShapeData> result;
    result.reserve(model_.collisionShapes.size());

    for (const auto& cs : model_.collisionShapes) {
        CollisionShapeData cd;
        cd.type   = (int)cs.type;
        cd.radius = cs.radius;
        if (cs.vertices.size() >= 1) {
            cd.vertices[0] = {cs.vertices[0].x, cs.vertices[0].y, cs.vertices[0].z};
        }
        if (cs.vertices.size() >= 2) {
            cd.vertices[1] = {cs.vertices[1].x, cs.vertices[1].y, cs.vertices[1].z};
        }
        result.push_back(cd);
    }
    return result;
}

// ============================================================================
// SetActiveSequence
// ============================================================================

void MdxModelAdapter::SetActiveSequence(int sequenceIndex) {
    activeSeqIdx_ = sequenceIndex;
    if (sequenceIndex >= 0 && sequenceIndex < (int)model_.sequences.size()) {
        seqStart_ = (int)model_.sequences[sequenceIndex].intervalStart;
        seqEnd_   = (int)model_.sequences[sequenceIndex].intervalEnd;
    } else {
        seqStart_ = seqEnd_ = 0;
    }
}

// ============================================================================
// SetCameraPosition — store camera position for billboard evaluation
// ============================================================================

void MdxModelAdapter::SetCameraPosition(float x, float y, float z) {
    cameraPos_ = { x, y, z };
}

// ============================================================================
// Evaluate — Per-frame animation evaluation
// ============================================================================

FrameState MdxModelAdapter::Evaluate(int timeMs, int globalTimeMs) {
    FrameState fs;

    // Evaluate bone hierarchy (pass camera position for billboard nodes)
    std::vector<Matrix44f> boneWorld, allNodes;
    hierarchy_.Evaluate(timeMs, seqStart_, seqEnd_,
                        model_.globalSequences, boneWorld, allNodes,
                        &cameraPos_, globalTimeMs);
    // Use ALL node matrices as the skinning palette (indexed by node position
    // in the hierarchy). Vertices can reference any node type via objectId.
    fs.boneWorldMatrices = std::move(allNodes);

    // Helper: compute effective time for a track, handling global sequences.
    // If the track uses a globalSequenceId, wraps by the global sequence duration
    // using wall-clock time (globalTimeMs) so that global sequences run
    // independently of the active animation sequence.
    auto effectiveTime = [&](u32 gsId) -> std::tuple<int, int, int> {
        if (gsId != whiteout::mdx::Track<whiteout::f32>::kNoGlobalSequence && gsId < (u32)model_.globalSequences.size()) {
            u32 duration = model_.globalSequences[gsId];
            if (duration > 0) {
                int gsTime = (globalTimeMs >= 0) ? globalTimeMs : timeMs;
                int t = (int)std::fmod((float)gsTime, (float)duration);
                return {t, 0, (int)duration};
            }
        }
        return {timeMs, seqStart_, seqEnd_};
    };

    // GeosetAnimation evaluation
    int geosetCount = (int)model_.geosets.size();
    fs.geosetAlphas.assign(geosetCount, 1.0f);
    fs.geosetColors.assign(geosetCount, Vector3f(1, 1, 1));

    for (const auto& ga : model_.geosetAnimations) {
        int gid = (int)ga.geosetId;
        if (gid < 0 || gid >= geosetCount) continue;

        {
            auto [t, s, e] = effectiveTime(ga.alphaTracks.globalSequenceId);
            fs.geosetAlphas[gid] = EvaluateTrackF32(ga.alphaTracks, t, s, e, ga.alpha);
        }

        if (ga.colorTracks.isUsed) {
            auto [t, s, e] = effectiveTime(ga.colorTracks.globalSequenceId);
            Vector3f color = EvaluateTrackVec3(ga.colorTracks, t, s, e, ga.color);
            fs.geosetColors[gid] = {color.x, color.y, color.z};
        } else {
            fs.geosetColors[gid] = {ga.color.x, ga.color.y, ga.color.z};
        }
    }

    // Layer alpha (KMTA) evaluation — per material layer
    for (int mi = 0; mi < (int)model_.materials.size(); mi++) {
        const auto& mat = model_.materials[mi];
        for (int li = 0; li < (int)mat.layers.size(); li++) {
            const auto& layer = mat.layers[li];
            if (!layer.alphaTracks.isUsed) continue;

            auto [t, s, e] = effectiveTime(layer.alphaTracks.globalSequenceId);
            float alpha = EvaluateTrackF32(layer.alphaTracks, t, s, e, layer.alpha);
            FrameState::LayerAlphaState las;
            las.materialId = mi;
            las.layerIndex = li;
            las.alpha = alpha;
            fs.layerAlphas.push_back(las);
        }
    }

    // Layer texture ID (KMTF) evaluation — animated texture swap per layer
    // Classic (v800-v1100): KMTF track lives on layer.textureIdTracks.
    // Reforged (v1200+): parser moves KMTF into subTexture.tracks; we evaluate
    //   the DiffuseMap subtexture's track (falling back to subTextures[0]).
    for (int mi = 0; mi < (int)model_.materials.size(); mi++) {
        const auto& mat = model_.materials[mi];
        for (int li = 0; li < (int)mat.layers.size(); li++) {
            const auto& layer = mat.layers[li];

            const Track<u32>* kmtf = nullptr;
            u32 defaultTexId = layer.textureId;

            if (layer.textureIdTracks.isUsed) {
                // Classic path
                kmtf = &layer.textureIdTracks;
            } else if (!layer.subTextures.empty()) {
                // Reforged path: find DiffuseMap subtexture's track
                const Layer::SubTexture* diffuse = &layer.subTextures[0];
                for (const auto& sub : layer.subTextures) {
                    if (sub.slot == Layer::SlotType::DiffuseMap) {
                        diffuse = &sub;
                        break;
                    }
                }
                if (diffuse->tracks.isUsed) {
                    kmtf = &diffuse->tracks;
                    defaultTexId = diffuse->textureId;
                }
            }

            if (!kmtf) continue;

            auto [t, s, e] = effectiveTime(kmtf->globalSequenceId);
            u32 texId = EvaluateTrackU32(*kmtf, t, s, e, defaultTexId);
            FrameState::LayerTextureIdState lts;
            lts.materialId = mi;
            lts.layerIndex = li;
            lts.textureId  = (int)texId;
            fs.layerTextureIds.push_back(lts);
        }
    }

    // Particle emitter per-frame state
    fs.particleStates.resize(model_.particleEmitters2.size());
    const auto& nodes = hierarchy_.Nodes();

    // Non-bone nodes (emitters, ribbons, attachments) live at their pivot in
    // world space. The hierarchy's world matrix W is the "delta from bind"
    // used for skinning (identity at default), so origin·W = origin, not the
    // pivot. Pre-multiply by T(pivot) to turn it into an absolute transform:
    //   origin · T(pivot) · W = pivot · W = animated pivot position
    // This matches mdx-m3-viewer's particle spawn (location = pivot + random,
    // then location · worldMatrix).
    auto worldOf = [&](int nodeIdx) -> Matrix44f {
        if (nodeIdx < 0 || nodeIdx >= (int)fs.boneWorldMatrices.size()) return Matrix44f::identity();
        const auto& piv = nodes[nodeIdx].pivot;
        Matrix44f pivotT = Matrix44f::translation({piv.x, piv.y, piv.z});
        return pivotT * fs.boneWorldMatrices[nodeIdx];
    };

    for (int i = 0; i < (int)model_.particleEmitters2.size(); i++) {
        const auto& pe = model_.particleEmitters2[i];
        auto& ps = fs.particleStates[i];
        ps.emitterId = i;

        int nodeIdx = hierarchy_.ObjectIdToNodeIndex((int)pe.node.objectId);

        ps.transform = worldOf(nodeIdx);

        { auto [t,s,e] = effectiveTime(pe.emissionRateTracks.globalSequenceId);
          ps.emissionRate = EvaluateTrackF32(pe.emissionRateTracks, t, s, e, pe.emissionRate); }
        { auto [t,s,e] = effectiveTime(pe.speedTracks.globalSequenceId);
          ps.speed        = EvaluateTrackF32(pe.speedTracks, t, s, e, pe.speed); }
        { auto [t,s,e] = effectiveTime(pe.variationTracks.globalSequenceId);
          ps.variation    = EvaluateTrackF32(pe.variationTracks, t, s, e, pe.variation); }
        { auto [t,s,e] = effectiveTime(pe.latitudeTracks.globalSequenceId);
          ps.coneAngle    = EvaluateTrackF32(pe.latitudeTracks, t, s, e, pe.latitude); }
        { auto [t,s,e] = effectiveTime(pe.gravityTracks.globalSequenceId);
          ps.gravity      = EvaluateTrackF32(pe.gravityTracks, t, s, e, pe.gravity); }
        { auto [t,s,e] = effectiveTime(pe.widthTracks.globalSequenceId);
          ps.width        = EvaluateTrackF32(pe.widthTracks, t, s, e, pe.width); }
        { auto [t,s,e] = effectiveTime(pe.lengthTracks.globalSequenceId);
          ps.length       = EvaluateTrackF32(pe.lengthTracks, t, s, e, pe.length); }
        { auto [t,s,e] = effectiveTime(pe.visibilityTracks.globalSequenceId);
          ps.visibility   = EvaluateTrackF32(pe.visibilityTracks, t, s, e, 1.0f); }

        (void)pe; // all PE2 per-frame fields set above
    }

    // Attachment transforms
    for (int i = 0; i < (int)model_.attachments.size(); i++) {
        const auto& att = model_.attachments[i];
        int nodeIdx = hierarchy_.ObjectIdToNodeIndex((int)att.node.objectId);
        Matrix44f tm = worldOf(nodeIdx);
        float vis = 1.0f;
        if (att.visibilityTracks.isUsed) {
            auto [t,s,e] = effectiveTime(att.visibilityTracks.globalSequenceId);
            vis = EvaluateTrackF32(att.visibilityTracks, t, s, e, 1.0f);
        }
        fs.attachmentStates.push_back({i, tm, vis});
    }

    // PE1 (model particle emitter) per-frame state
    for (int i = 0; i < (int)model_.particleEmitters.size(); i++) {
        const auto& pe = model_.particleEmitters[i];
        FrameState::PE1FrameState ps;
        ps.emitterId = i;

        int nodeIdx = hierarchy_.ObjectIdToNodeIndex((int)pe.node.objectId);
        ps.transform = worldOf(nodeIdx);

        // Evaluate animated tracks (lat/lon already in radians in MDX)
        { auto [t,s,e] = effectiveTime(pe.emissionRateTracks.globalSequenceId);
          ps.emissionRate = EvaluateTrackF32(pe.emissionRateTracks, t, s, e, pe.emissionRate); }
        { auto [t,s,e] = effectiveTime(pe.speedTracks.globalSequenceId);
          ps.speed = EvaluateTrackF32(pe.speedTracks, t, s, e, pe.initialVelocity); }
        { auto [t,s,e] = effectiveTime(pe.latitudeTracks.globalSequenceId);
          ps.latitude = EvaluateTrackF32(pe.latitudeTracks, t, s, e, pe.latitude); }
        { auto [t,s,e] = effectiveTime(pe.longitudeTracks.globalSequenceId);
          ps.longitude = EvaluateTrackF32(pe.longitudeTracks, t, s, e, pe.longitude); }
        { auto [t,s,e] = effectiveTime(pe.gravityTracks.globalSequenceId);
          ps.gravity = EvaluateTrackF32(pe.gravityTracks, t, s, e, pe.gravity); }
        { auto [t,s,e] = effectiveTime(pe.visibilityTracks.globalSequenceId);
          ps.visibility = EvaluateTrackF32(pe.visibilityTracks, t, s, e, 1.0f); }

        fs.pe1States.push_back(ps);
    }

    // Ribbon emitter per-frame state
    fs.ribbonStates.resize(model_.ribbonEmitters.size());

    for (int i = 0; i < (int)model_.ribbonEmitters.size(); i++) {
        const auto& rb = model_.ribbonEmitters[i];
        auto& rs = fs.ribbonStates[i];
        rs.emitterId = i;

        int nodeIdx = hierarchy_.ObjectIdToNodeIndex((int)rb.node.objectId);
        rs.transform = worldOf(nodeIdx);

        { auto [t,s,e] = effectiveTime(rb.heightAboveTracks.globalSequenceId);
          rs.above      = EvaluateTrackF32(rb.heightAboveTracks, t, s, e, rb.heightAbove); }
        { auto [t,s,e] = effectiveTime(rb.heightBelowTracks.globalSequenceId);
          rs.below      = EvaluateTrackF32(rb.heightBelowTracks, t, s, e, rb.heightBelow); }
        { auto [t,s,e] = effectiveTime(rb.alphaTracks.globalSequenceId);
          rs.alpha      = EvaluateTrackF32(rb.alphaTracks, t, s, e, rb.alpha); }
        { auto [t,s,e] = effectiveTime(rb.visibilityTracks.globalSequenceId);
          rs.visibility = EvaluateTrackF32(rb.visibilityTracks, t, s, e, 1.0f); }
        { auto [t,s,e] = effectiveTime(rb.textureSlotTracks.globalSequenceId);
          rs.slot       = (int)EvaluateTrackU32(rb.textureSlotTracks, t, s, e, rb.textureSlot); }

        // MDX ribbon color is stored BGR (matches CImVector layout the engine
        // uses internally); the renderer wants RGB, so swap channels — same
        // convention applied to GeosetAnim colors above.
        if (rb.colorTracks.isUsed) {
            auto [t,s,e] = effectiveTime(rb.colorTracks.globalSequenceId);
            Vector3f c = EvaluateTrackVec3(rb.colorTracks, t, s, e, rb.color);
            rs.color = {c.z, c.y, c.x};
        } else {
            rs.color = {rb.color.z, rb.color.y, rb.color.x};
        }
    }

    // Collision shape transforms
    fs.collisionTransforms.resize(model_.collisionShapes.size());
    for (int i = 0; i < (int)model_.collisionShapes.size(); i++) {
        const auto& cs = model_.collisionShapes[i];
        int nodeIdx = hierarchy_.ObjectIdToNodeIndex((int)cs.node.objectId);
        fs.collisionTransforms[i] = (nodeIdx >= 0 && nodeIdx < (int)fs.boneWorldMatrices.size())
                                     ? fs.boneWorldMatrices[nodeIdx] : Matrix44f::identity();
    }

    // Texture animation evaluation
    for (int i = 0; i < (int)model_.textureAnimations.size(); i++) {
        const auto& ta = model_.textureAnimations[i];
        // Find which materials reference this textureAnimation
        for (int mi = 0; mi < (int)model_.materials.size(); mi++) {
            for (int li = 0; li < (int)model_.materials[mi].layers.size(); li++) {
                const auto& layer = model_.materials[mi].layers[li];
                if ((int)layer.textureAnimationId == i) {
                    FrameState::TexAnimState tas;
                    tas.materialId = mi;
                    tas.layerIndex = li;

                    auto [tt, ts, te] = effectiveTime(ta.translationTracks.globalSequenceId);
                    Vector3f trans = EvaluateTrackVec3(ta.translationTracks, tt, ts, te, {0, 0, 0});
                    auto [st, ss, se] = effectiveTime(ta.scalingTracks.globalSequenceId);
                    Vector3f scale = EvaluateTrackVec3(ta.scalingTracks, st, ss, se, {1, 1, 1});
                    tas.uOff  = trans.x;
                    tas.vOff  = trans.y;
                    tas.uTile = scale.x;
                    tas.vTile = scale.y;

                    // Evaluate rotation track (Z-axis rotation in UV space)
                    auto [rt, rs, re] = effectiveTime(ta.rotationTracks.globalSequenceId);
                    Quaternion rot = EvaluateTrackQuat(ta.rotationTracks, rt, rs, re,
                                                      Quaternion(0, 0, 0, 1));
                    // Extract Z-axis rotation angle from quaternion
                    tas.rotation = 2.0f * std::atan2(rot.z, rot.w);
                    fs.texAnims.push_back(tas);
                    break; // first matching layer per material
                }
            }
        }
    }

    return fs;
}

// ============================================================================
// GetAttachmentConfigs
// ============================================================================

std::vector<AttachmentConfig> MdxModelAdapter::GetAttachmentConfigs() {
    std::vector<AttachmentConfig> result;
    for (const auto& att : model_.attachments) {
        AttachmentConfig cfg;
        cfg.attachmentId = (int)att.attachmentId;
        cfg.modelPath = att.path;
        result.push_back(cfg);
    }
    return result;
}

// ============================================================================
// GetPE1Configs
// ============================================================================

std::vector<PE1EmitterConfig> MdxModelAdapter::GetPE1Configs() {
    std::vector<PE1EmitterConfig> result;
    for (const auto& pe : model_.particleEmitters) {
        if (pe.spawnModelFileName.empty()) continue;  // skip emitters without model
        PE1EmitterConfig cfg;
        cfg.modelPath = pe.spawnModelFileName;
        cfg.lifespan  = pe.lifespan;
        cfg.scale     = 1.0f;  // MDX PE1 has no scale field
        result.push_back(cfg);
    }
    return result;
}

// ============================================================================
// GetSequences
// ============================================================================

std::vector<IModelSource::SequenceInfo> MdxModelAdapter::GetSequences() {
    std::vector<SequenceInfo> result;
    result.reserve(model_.sequences.size());
    for (const auto& seq : model_.sequences) {
        result.push_back({seq.name, (int)seq.intervalStart, (int)seq.intervalEnd});
    }
    return result;
}

// ============================================================================
// GetCameraPresets — extract Camera objects from MDX model
// ============================================================================

std::vector<CameraPreset> MdxModelAdapter::GetCameraPresets() const {
    std::vector<CameraPreset> presets;
    for (const auto& cam : model_.cameras) {
        auto& pos = cam.position;
        auto& tgt = cam.targetPosition;
        float dx = pos.x - tgt.x, dy = pos.y - tgt.y, dz = pos.z - tgt.z;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (dist < 0.01f) dist = 100.0f;
        float invD = 1.0f / dist;
        float pitch = std::asin(std::clamp(dz * invD, -1.0f, 1.0f));
        float yaw = std::atan2(dy * invD, dx * invD);

        CameraPreset cp;
        // Convert std::string name to wstring
        cp.name = std::wstring(cam.name.begin(), cam.name.end());
        cp.pitch = pitch;
        cp.yaw = yaw;
        cp.distance = dist;
        cp.target = {tgt.x, tgt.y, tgt.z};
        cp.isLive = false;
        presets.push_back(cp);
    }
    return presets;
}

} // namespace WhiteoutDex
