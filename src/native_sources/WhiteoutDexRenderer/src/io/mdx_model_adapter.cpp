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

// Translate a WhiteoutLib PixelFormat + sRGB flag into our gfx::Format.
// Returns Format::Unknown when the source format has no matching GFX
// variant -- callers fall back to a CPU decode + RGBA8 upload in that
// case. Normal maps MUST stay in BC3/BC5/BC7 because hd_ps.slang's
// decodeNormalMap hardcodes Blizzard's packed-channel convention (R for
// the X low-precision byte, A for the X high-precision byte, G for Y);
// any CPU decode to RGBA8 would shuffle those channels and produce
// wrong world normals on mirrored UV islands.
inline gfx::Format WhiteoutFormatToGfx(whiteout::textures::PixelFormat pf, bool srgb) {
    using PF = whiteout::textures::PixelFormat;
    switch (pf) {
        case PF::R8:      return gfx::Format::R8_UNORM;
        case PF::R16:     return gfx::Format::R16_UNORM;
        case PF::R32F:    return gfx::Format::R32_FLOAT;
        case PF::RG8:     return gfx::Format::R8G8_UNORM;
        case PF::RG16:    return gfx::Format::R16G16_UNORM;
        case PF::RG32F:   return gfx::Format::R32G32_FLOAT;
        case PF::RGBA8:   return srgb ? gfx::Format::R8G8B8A8_UNORM_SRGB
                                      : gfx::Format::R8G8B8A8_UNORM;
        case PF::RGBA16:  return gfx::Format::R16G16B16A16_UNORM;
        case PF::RGBA32F: return gfx::Format::R32G32B32A32_FLOAT;
        case PF::BC1:     return srgb ? gfx::Format::BC1_UNORM_SRGB : gfx::Format::BC1_UNORM;
        case PF::BC2:     return srgb ? gfx::Format::BC2_UNORM_SRGB : gfx::Format::BC2_UNORM;
        case PF::BC3:     return srgb ? gfx::Format::BC3_UNORM_SRGB : gfx::Format::BC3_UNORM;
        case PF::BC4:     return gfx::Format::BC4_UNORM;
        case PF::BC5:     return gfx::Format::BC5_UNORM;
        case PF::BC6H:    return gfx::Format::BC6H_UF16;
        case PF::BC7:     return srgb ? gfx::Format::BC7_UNORM_SRGB : gfx::Format::BC7_UNORM;
    }
    return gfx::Format::Unknown;
}

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
                                 IContentProvider* contentProvider)
    : model_(std::move(model))
    , basePath_(std::move(basePath))
    , resolver_(basePath_)
    , contentProvider_(contentProvider) {
    // MDX data is authored in Blizzard space. If the renderer is configured to
    // run in Max space (WDX_DEFAULT_COORD_SPACE=Max), swizzle the whole model
    // once at load. For the default Blizzard build this is a no-op.
    if constexpr (kDefaultCoordSpace == CoordSpace::Max) {
        TransformMdxModelToMaxCoords(model_);
    }

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
        mesh.lod        = gs.lod;

        int vc = (int)gs.vertexPositions.size();
        mesh.positions.resize(vc);
        mesh.normals.resize(vc);
        mesh.uvs.resize(vc);
        // Tangents: MDX v900+ geosets store per-vertex tangent frames
        // (gs.tangents). Length matches gs.vertexPositions when the
        // model was exported with tangent data; older / stripped MDX
        // files leave it empty, and we leave mesh.tangents empty too so
        // the HD path can branch on presence.
        const bool hasTangents = (int)gs.tangents.size() == vc;
        if (hasTangents) mesh.tangents.resize(vc);

        for (int v = 0; v < vc; v++) {
            mesh.positions[v] = toXM(gs.vertexPositions[v]);
            if (v < (int)gs.vertexNormals.size())
                mesh.normals[v] = toXM(gs.vertexNormals[v]);
            if (!gs.textureCoordinateSets.empty() &&
                v < (int)gs.textureCoordinateSets[0].size()) {
                mesh.uvs[v] = {gs.textureCoordinateSets[0][v].x,
                               gs.textureCoordinateSets[0][v].y};
            }
            if (hasTangents) {
                // gs.tangents is already in our default coord space --
                // TransformMdxModelToMaxCoords ran swizTangent on every
                // entry at load time, so the XYZ component is correct
                // world-space and W carries the handedness sign.
                mesh.tangents[v] = gs.tangents[v];
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
    //
    // Preserve the native pixel format end-to-end: BC3/BC5/BC7 normal
    // maps read Blizzard's packed-precision convention (R = X low byte,
    // A = X high byte, G = Y) that hd_ps.slang::decodeNormalMap
    // reconstructs via `nx = 2 * sample.x * sample.w - 1`. Any CPU
    // decode to RGBA8 rewrites those channels through the generic BC
    // decoder and destroys the packing. We only fall back to an RGBA8
    // re-encode when the source format has no matching gfx::Format.
    auto applyResult = [&](whiteout::textures::Texture& tex) {
        gfx::Format gfxFmt = WhiteoutFormatToGfx(tex.format(), tex.isSrgb());
        if (gfxFmt == gfx::Format::Unknown) {
            tex.format(whiteout::textures::PixelFormat::RGBA8);
            gfxFmt = tex.isSrgb() ? gfx::Format::R8G8B8A8_UNORM_SRGB
                                  : gfx::Format::R8G8B8A8_UNORM;
        }
        td.width     = (int)tex.width();
        td.height    = (int)tex.height();
        td.format    = gfxFmt;
        td.mipLevels = (int)tex.mipCount();

        // Concatenate all mip levels into one tight buffer, mip0
        // first. D3D12Device::CreateTexture walks subresources in
        // this order via GetCopyableFootprints, advancing by
        // `rowSize * rows` per mip.
        size_t total = 0;
        for (uint32_t m = 0; m < tex.mipCount(); ++m)
            total += tex.mipData(m).size();
        td.pixels.resize(total);
        uint8_t* cursor = td.pixels.data();
        for (uint32_t m = 0; m < tex.mipCount(); ++m) {
            auto src = tex.mipData(m);
            std::memcpy(cursor, src.data(), src.size());
            cursor += src.size();
        }
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
    td.pixels.resize(4 * 4 * 4);
    for (int j = 0; j < 16; j++) {
        td.pixels[j * 4 + 0] = 255;
        td.pixels[j * 4 + 1] = 0;
        td.pixels[j * 4 + 2] = 255;
        td.pixels[j * 4 + 3] = 255;
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
        td.pixels = DecodeTeamGlow(255, 0, 0, td.width, td.height);
    } else {
        // TeamColor: solid 4x4 red
        td.width = 4; td.height = 4;
        td.pixels.resize(64);
        for (int j = 0; j < 16; j++) {
            td.pixels[j * 4 + 0] = 255;
            td.pixels[j * 4 + 1] = 0;
            td.pixels[j * 4 + 2] = 0;
            td.pixels[j * 4 + 3] = 255;
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
            td.pixels.assign(4 * 4 * 4, 255);
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
            ld.textureAnimationId = ((int32_t)layer.textureAnimationId < 0
                                     || (int32_t)layer.textureAnimationId >= (int)model_.textureAnimations.size())
                                    ? -1 : (int)layer.textureAnimationId;
            // MDX layer shader id (Layer::ShaderType). Cast through uint32_t
            // so non-mesh / future values round-trip unchanged; the draw path
            // treats unknown ids as SDLegacy (0), matching Previewd's MDL
            // IReadShader fallback.
            ld.shaderId   = static_cast<int>(static_cast<whiteout::u32>(layer.shader));

            // Reforged HD material knobs. These feed the HD PS CB
            // pixelParams/fresnelColor slots (see CGxMatParams::PixelParams
            // RE). Classic MDX layers zero all of these; v1200+ HD layers
            // may author non-zero fresnel rim + team-colour tint.
            ld.emissiveGain    = layer.emissiveGain;
            ld.fresnelOpacity  = layer.fresnelOpacity;
            ld.fresnelTeamColor = layer.fresnelTeamColor;
            ld.fresnelColor    = { layer.fresnelColor.x,
                                   layer.fresnelColor.y,
                                   layer.fresnelColor.z };

            // Texture resolution:
            //   Classic (v800-v1100): layer.textureId indexes the texture array.
            //   Reforged (v1200+): layer.textureId is unused (0) and textures
            //     live in layer.subTextures[], one per slot. This applies to
            //     *all* v1200 layers — the is_hd flag only controls PBR shading
            //     (fresnel, emissive gain), not texture storage layout. We
            //     only sample the diffuse map, so grab the DiffuseMap slot;
            //     if no DiffuseMap is present fall back to subTextures[0].
            if (!layer.subTextures.empty()) {
                // Reforged multi-texture layout: each subtexture is tagged
                // by its SlotType. Pluck out every slot the HD PS reads
                // (DiffuseMap / NormalMap / ORMMap / EmissiveMap / TeamColor)
                // so the renderer can bind them to t0..t4 respectively.
                int diffuseTex = (int)layer.subTextures[0].textureId;
                for (const auto& sub : layer.subTextures) {
                    const int tex = static_cast<int>(sub.textureId);
                    switch (sub.slot) {
                        case Layer::SlotType::DiffuseMap:  diffuseTex        = tex; break;
                        case Layer::SlotType::NormalMap:   ld.normalMapId    = tex; break;
                        case Layer::SlotType::ORMMap:      ld.ormMapId       = tex; break;
                        case Layer::SlotType::EmissiveMap: ld.emissiveMapId  = tex; break;
                        case Layer::SlotType::TeamColor:   ld.teamColorMapId = tex; break;
                        default: break;
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
    sk.nodeParents.assign(sk.nodeCount, -1);
    const auto& nodes = hierarchy_.Nodes();
    for (int i = 0; i < (int)nodes.size(); i++) {
        uint32_t nf = nodes[i].flags;
        uint32_t bbf = 0;
        using NF = whiteout::mdx::Node::NodeFlag;
        // One-hot priority to match Previewd's GetObjectFlags @0x140456dd0:
        // Billboarded > LockX > LockY > LockZ. Multiple file bits collapse
        // to a single engine flag. CameraAnchored is independent.
        if      (nf & (uint32_t)NF::Billboarded)      bbf |= BONE_BILLBOARD_FULL;
        else if (nf & (uint32_t)NF::BillboardedLockX) bbf |= BONE_BILLBOARD_LOCK_X;
        else if (nf & (uint32_t)NF::BillboardedLockY) bbf |= BONE_BILLBOARD_LOCK_Y;
        else if (nf & (uint32_t)NF::BillboardedLockZ) bbf |= BONE_BILLBOARD_LOCK_Z;
        if (nf & (uint32_t)NF::CameraAnchored)        bbf |= BONE_BILLBOARD_CAMERA_ANCHORED;
        sk.billboardFlags[i] = bbf;

        const auto& p = nodes[i].pivot;
        sk.nodePivots[i] = {p.x, p.y, p.z};
        sk.nodeParents[i] = nodes[i].parentIdx;
    }

    return sk;
}

// ============================================================================
// GetSkinWeights — v1200 skinData or v800 vertexGroups indirection
// ============================================================================

std::vector<SkinWeightData> MdxModelAdapter::GetSkinWeights() {
    std::vector<SkinWeightData> result;
    result.reserve(model_.geosets.size());

    // MATS (matrixIndices) stores dense BONE indices (0..numBones-1), NOT
    // objectIds. Matches Previewd's BuildPrimBone @0x1402cf5e0:
    //     qmemcpy(bone, &boneMatrices[*matrix], sizeof(C34Matrix));
    // where `boneMatrices` is the dense `data->boneMtx[0..numBones-1]` array.
    // For Blizzard models (bones densely numbered 0..numBones-1 at the start
    // of objectId space) this coincides with objectId-based lookup; for
    // models where bones aren't densely numbered first, splitIndex resolution
    // picks the correct bone whereas objectId resolution would miss.
    // Fallback to ObjectIdToNodeIndex when splitIndex is out of range so
    // third-party exporters that stuff objectIds into MATS still work.
    //
    // v800 matrix-groups with count > 4 get a pseudo palette slot holding the
    // per-frame average of the group's bone matrices (mirrors BuildPrimBone's
    // fallback `sum / N` for N > 3). Pseudo slots live at [nodeCount, 256),
    // numbered globally across all geosets. Count <= 4 keeps the uniform 1/N
    // per-vertex weights path (mathematically equivalent to matrix averaging).
    auto resolveBoneIdx = [&](int matsValue) -> int {
        int nodeIdx = hierarchy_.BoneIndexToNodeIndex(matsValue);
        if (nodeIdx < 0) nodeIdx = hierarchy_.ObjectIdToNodeIndex(matsValue);
        return (nodeIdx >= 0) ? nodeIdx : 0;
    };

    const int nodeCount = hierarchy_.NodeCount();
    constexpr int kMaxPaletteSlots = 256;  // matches bls::kMaxBones
    int nextPseudoSlot = nodeCount;

    for (int gi = 0; gi < (int)model_.geosets.size(); gi++) {
        const auto& gs = model_.geosets[gi];
        int vc = (int)gs.vertexPositions.size();
        SkinWeightData sw;
        sw.geosetId = gi;
        sw.influences.resize(vc);

        if (!gs.skinData.empty()) {
            // v1200: packed u8 per vertex, 4 bone-palette indices + 4 weights.
            // SKIN bytes index the geoset's MATS table (which stores dense
            // bone indices). Empty MATS ⇒ direct bone index.
            for (int v = 0; v < vc; v++) {
                int base = v * 8;
                if (base + 7 < (int)gs.skinData.size()) {
                    for (int k = 0; k < 4; k++) {
                        uint8_t raw = gs.skinData[base + k];
                        int matsValue;
                        if (!gs.matrixIndices.empty() && raw < gs.matrixIndices.size())
                            matsValue = (int)gs.matrixIndices[raw];
                        else
                            matsValue = (int)raw;
                        sw.influences[v].boneIdx[k] = resolveBoneIdx(matsValue);
                        sw.influences[v].weight[k]  = gs.skinData[base + 4 + k] / 255.0f;
                    }
                }
            }
        } else if (!gs.vertexGroups.empty() && !gs.matrixGroups.empty()) {
            // v800: vertexGroups + matrixGroups + matrixIndices indirection.
            std::vector<u32> groupStart(gs.matrixGroups.size() + 1, 0);
            for (int g = 0; g < (int)gs.matrixGroups.size(); g++)
                groupStart[g + 1] = groupStart[g] + gs.matrixGroups[g];

            // Pre-pass: reserve pseudo slots for groups with count > 4.
            std::vector<int> groupPseudoSlot(gs.matrixGroups.size(), -1);
            for (int g = 0; g < (int)gs.matrixGroups.size(); g++) {
                u32 count = gs.matrixGroups[g];
                if (count <= 4) continue;
                if (nextPseudoSlot >= kMaxPaletteSlots) {
                    std::fprintf(stderr,
                        "[GetSkinWeights] palette exhausted at slot %d; geoset %d group %d (N=%u) "
                        "falls back to 4-bone clamp (will lose influences)\n",
                        nextPseudoSlot, gi, g, count);
                    continue;
                }
                GroupAverageRecord rec;
                rec.pseudoSlot = nextPseudoSlot++;
                rec.nodeIndices.reserve(count);
                u32 start = groupStart[g];
                for (u32 k = 0; k < count && (start + k) < gs.matrixIndices.size(); k++) {
                    int nodeIdx = resolveBoneIdx((int)gs.matrixIndices[start + k]);
                    rec.nodeIndices.push_back(nodeIdx);
                }
                groupPseudoSlot[g] = rec.pseudoSlot;
                sw.groupAverages.push_back(std::move(rec));
            }

            for (int v = 0; v < vc && v < (int)gs.vertexGroups.size(); v++) {
                int groupId = gs.vertexGroups[v];
                if (groupId >= (int)gs.matrixGroups.size()) continue;

                int pseudoSlot = groupPseudoSlot[groupId];
                if (pseudoSlot >= 0) {
                    sw.influences[v].boneIdx[0] = pseudoSlot;
                    sw.influences[v].weight[0]  = 1.0f;
                } else {
                    u32 start = groupStart[groupId];
                    u32 count = gs.matrixGroups[groupId];
                    if (count > 4) count = 4;
                    float w = (count > 0) ? 1.0f / (float)count : 0.0f;
                    for (u32 k = 0; k < count && (start + k) < gs.matrixIndices.size(); k++) {
                        sw.influences[v].boneIdx[k] = resolveBoneIdx((int)gs.matrixIndices[start + k]);
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
// GetPlaneEmitterInits — PE2 service path (docs/PARTICLEEMITTERS2.md Phase 4).
//
// Produces a PlaneEmitterInit per MDX ParticleEmitter2, folding the MDX
// start/mid/end color+alpha+scale + mid-time + head/tail life/decay intervals
// onto the two-key CParticleKey layout that the new service expects.
// ============================================================================

namespace {

particle::FilterMode MapToServiceFilterMode(whiteout::u32 mdxMode) {
    // MDX PE2 filterMode values match the service enum 1:1.
    switch (mdxMode) {
        case 0: return particle::FilterMode::Blend;
        case 1: return particle::FilterMode::Additive;
        case 2: return particle::FilterMode::Modulate;
        case 3: return particle::FilterMode::Modulate2X;
        case 4: return particle::FilterMode::AlphaKey;
        default: return particle::FilterMode::Blend;
    }
}

particle::ImVector MdxColorToImVector(const whiteout::Vector3f& rgb, whiteout::u8 alpha) {
    // MDX colors are float [0,1]; ImVector is BGRA8. Conversion matches the
    // RE loader: (int)(255.0 * v) with truncation toward zero. Alpha is already
    // a byte in the MDX binary.
    auto clamp8 = [](float v) -> uint8_t {
        if (v <= 0.0f) return 0;
        if (v >= 1.0f) return 255;
        return static_cast<uint8_t>(v * 255.0f);
    };
    return { alpha, clamp8(rgb.x), clamp8(rgb.y), clamp8(rgb.z) };
}

} // namespace

std::vector<particle::PlaneEmitterInit> MdxModelAdapter::GetPlaneEmitterInits() const {
    std::vector<particle::PlaneEmitterInit> result;
    result.reserve(model_.particleEmitters2.size());

    using namespace whiteout::mdx;

    for (const auto& pe : model_.particleEmitters2) {
        particle::PlaneEmitterInit init;

        init.textureRows    = pe.rows;
        init.textureCols    = pe.columns;
        init.lifeSpan       = pe.lifespan;
        init.tailLength     = pe.tailLength;
        init.angularVelocity = 0.0f;                  // not in MDX
        init.priorityPlane  = static_cast<int>(pe.priorityPlane);
        init.replaceableId  = static_cast<int>(pe.replaceableId);

        // Particle type: MDX 0=Head, 1=Tail, 2=Both (per the spec on line 848
        // of MDX_FILE_FORMAT_SPECIFICATION.md).
        init.hasHead = (pe.headOrTail != 1);   // anything but "tail-only"
        init.hasTail = (pe.headOrTail != 0);   // anything but "head-only"

        // Flag bits from the node.
        const whiteout::u32 nf = static_cast<whiteout::u32>(pe.node.flags);
        init.modelSpace    = (nf & (whiteout::u32)Node::NodeFlag::ModelSpace)     != 0;
        init.xyQuads       = (nf & (whiteout::u32)Node::NodeFlag::XYQuad)         != 0;
        init.sortZ         = (nf & (whiteout::u32)Node::NodeFlag::SortPrimitives) != 0;
        const bool lineEmitter = (nf & (whiteout::u32)Node::NodeFlag::LineEmitter) != 0;
        init.longitude     = lineEmitter ? 0.0f : 6.2831853071795864769f;

        // Material descriptor. Single texture slot 0; MDX `textureId` is the
        // index into the model's texture table (resolved by the render service).
        init.material.textureId     = static_cast<int>(pe.textureId);
        init.material.filterMode    = MapToServiceFilterMode(pe.filterMode);
        init.material.unshaded      = (nf & (whiteout::u32)Node::NodeFlag::Unshaded) != 0;
        init.material.unfogged      = (nf & (whiteout::u32)Node::NodeFlag::Unfogged) != 0;
        init.material.replaceableId = static_cast<int>(pe.replaceableId);

        // MDX one-shot "squirt" flag: arm the NeedSquirt latch at activation.
        init.squirtAtStart = (pe.squirt != 0);

        // --- 2-key folding (docs/PARTICLEEMITTERS2.md §3.4 mapping table) ---
        const float midTime = pe.time * pe.lifespan;

        particle::ImVector startColor = MdxColorToImVector(pe.segmentColor[0], pe.segmentAlpha[0]);
        particle::ImVector midColor   = MdxColorToImVector(pe.segmentColor[1], pe.segmentAlpha[1]);
        particle::ImVector endColor   = MdxColorToImVector(pe.segmentColor[2], pe.segmentAlpha[2]);

        // Key 0 — life phase
        auto& k0 = init.keys[0];
        k0.endTime        = midTime;
        k0.startColor     = startColor;
        k0.endColor       = midColor;
        k0.startScale     = pe.segmentScaling[0];
        k0.endScale       = pe.segmentScaling[1];
        k0.headCellStart  = static_cast<int>(pe.headInterval[0]);
        k0.headCellEnd    = static_cast<int>(pe.headInterval[1]);
        k0.headCellRepeat = static_cast<int>(pe.headInterval[2]);
        k0.tailCellStart  = static_cast<int>(pe.tailInterval[0]);
        k0.tailCellEnd    = static_cast<int>(pe.tailInterval[1]);
        k0.tailCellRepeat = static_cast<int>(pe.tailInterval[2]);

        // Key 1 — decay phase
        auto& k1 = init.keys[1];
        k1.endTime        = pe.lifespan;
        k1.startColor     = midColor;
        k1.endColor       = endColor;
        k1.startScale     = pe.segmentScaling[1];
        k1.endScale       = pe.segmentScaling[2];
        k1.headCellStart  = static_cast<int>(pe.headDecayInterval[0]);
        k1.headCellEnd    = static_cast<int>(pe.headDecayInterval[1]);
        k1.headCellRepeat = static_cast<int>(pe.headDecayInterval[2]);
        k1.tailCellStart  = static_cast<int>(pe.tailDecayInterval[0]);
        k1.tailCellEnd    = static_cast<int>(pe.tailDecayInterval[1]);
        k1.tailCellRepeat = static_cast<int>(pe.tailDecayInterval[2]);

        // Simulate in the renderer-native default space. The MDX adapter has
        // already placed the per-frame TRS into that space (or left it in
        // Blizzard when default == Blizzard), so the emitter and the transform
        // it will receive agree by construction.
        init.coordSpace = kDefaultCoordSpace;

        result.push_back(std::move(init));
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

        if (rb.materialId < (u32)model_.materials.size() &&
            !model_.materials[rb.materialId].layers.empty()) {
            const auto& layer = model_.materials[rb.materialId].layers[0];

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
        // Previewd builds geoset vertices at `pivot + extent` in bind-pose world;
        // the bone matrix we apply is a skinning delta (identity at bind). Store the
        // pivot so the renderer can reconstruct the bind-pose world-space corners.
        if (cs.node.objectId < model_.pivotPoints.size()) {
            const auto& p = model_.pivotPoints[cs.node.objectId];
            cd.pivot = {p.x, p.y, p.z};
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
            fs.geosetColors[gid] = {color.z, color.y, color.x};
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

    for (int mi = 0; mi < (int)model_.materials.size(); mi++) {
        const auto& mat = model_.materials[mi];
        for (int li = 0; li < (int)mat.layers.size(); li++) {
            const auto& layer = mat.layers[li];
            const bool anyAnim =
                layer.fresnelColorTracks.isUsed    ||
                layer.fresnelAlphaTracks.isUsed    ||
                layer.fresnelTeamColorTracks.isUsed||
                layer.emissiveGainTracks.isUsed;
            if (!anyAnim) continue;

            FrameState::LayerFresnelState lfs;
            lfs.materialId = mi;
            lfs.layerIndex = li;

            if (layer.fresnelColorTracks.isUsed) {
                auto [t, s, e] = effectiveTime(layer.fresnelColorTracks.globalSequenceId);
                Vector3f c = EvaluateTrackVec3(layer.fresnelColorTracks, t, s, e, layer.fresnelColor);
                lfs.fresnelColor = { c.x, c.y, c.z };
            } else {
                lfs.fresnelColor = { layer.fresnelColor.x, layer.fresnelColor.y, layer.fresnelColor.z };
            }

            if (layer.fresnelAlphaTracks.isUsed) {
                auto [t, s, e] = effectiveTime(layer.fresnelAlphaTracks.globalSequenceId);
                lfs.fresnelOpacity = EvaluateTrackF32(layer.fresnelAlphaTracks, t, s, e, layer.fresnelOpacity);
            } else {
                lfs.fresnelOpacity = layer.fresnelOpacity;
            }

            if (layer.fresnelTeamColorTracks.isUsed) {
                auto [t, s, e] = effectiveTime(layer.fresnelTeamColorTracks.globalSequenceId);
                lfs.fresnelTeamColor = EvaluateTrackF32(layer.fresnelTeamColorTracks, t, s, e, layer.fresnelTeamColor);
            } else {
                lfs.fresnelTeamColor = layer.fresnelTeamColor;
            }

            if (layer.emissiveGainTracks.isUsed) {
                auto [t, s, e] = effectiveTime(layer.emissiveGainTracks.globalSequenceId);
                lfs.emissiveGain = EvaluateTrackF32(layer.emissiveGainTracks, t, s, e, layer.emissiveGain);
            } else {
                lfs.emissiveGain = layer.emissiveGain;
            }

            fs.layerFresnels.push_back(lfs);
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

    // Previewd's PlaceObject case 5 (PE2, 0x14053392e) pre-rotates the node's
    // world matrix by +π/2 around Z before handing it to
    // CParticleEmitter2::Update as modelToWorld. That's the compensation that
    // aligns the emitter's (width→+X, length→+Y) spawn plane with the MDX
    // authoring convention where "length" extends along the node's forward
    // direction. Without it the spawn rectangle is 90° off.
    const Matrix44f kPE2SpawnFrameRotation = Matrix44f::rotation_z(
        1.5707963267948966f);

    for (int i = 0; i < (int)model_.particleEmitters2.size(); i++) {
        const auto& pe = model_.particleEmitters2[i];
        auto& ps = fs.particleStates[i];
        ps.emitterId = i;

        int nodeIdx = hierarchy_.ObjectIdToNodeIndex((int)pe.node.objectId);

        ps.transform = kPE2SpawnFrameRotation * worldOf(nodeIdx);

        { auto [t,s,e] = effectiveTime(pe.emissionRateTracks.globalSequenceId);
          ps.emissionRate = EvaluateTrackF32(pe.emissionRateTracks, t, s, e, pe.emissionRate); }
        { auto [t,s,e] = effectiveTime(pe.speedTracks.globalSequenceId);
          ps.speed        = EvaluateTrackF32(pe.speedTracks, t, s, e, pe.speed); }
        { auto [t,s,e] = effectiveTime(pe.variationTracks.globalSequenceId);
          ps.variation    = EvaluateTrackF32(pe.variationTracks, t, s, e, pe.variation); }
        { auto [t,s,e] = effectiveTime(pe.latitudeTracks.globalSequenceId);
          // MDX stores latitude in degrees; Previewd's
          // SetEmitterLatitude2 (0x140530917) and ILoadParticleEmitters2
          // (0x14049e4f3) both convert via deg * pi / 180 before calling
          // CPlaneParticleEmitter::SetLatitude. The emitter's CreateParticle
          // consumes it as radians (sin/cos). Match the conversion here.
          const float degs = EvaluateTrackF32(pe.latitudeTracks, t, s, e, pe.latitude);
          ps.coneAngle     = degs * (3.14159265358979323846f / 180.0f); }
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
            rs.color = {rb.color.x, rb.color.y, rb.color.z};
        }
    }

    // MDX scene lights. For each light, evaluate the animated color/
    // intensity/visibility tracks at current time and resolve the light's
    // world-space position (omni) or direction (directional) from its node
    // transform. Mirrors Previewd's CAnimLightObj evaluation (see
    // AnimateAllLights + CGxLightToShaderLight at 0x1403fbc00). Ambient
    // MDX lights are rare; we carry them through with kind=Ambient and
    // let the renderer fold them into the ShaderLight.ambient channel.
    fs.lights.reserve(model_.lights.size());
    for (int i = 0; i < (int)model_.lights.size(); ++i) {
        const auto& L = model_.lights[i];
        FrameState::LightState ls;
        ls.kind = (L.type == Light::LightType::Omni)        ? FrameState::LightKind::Omni :
                  (L.type == Light::LightType::Directional) ? FrameState::LightKind::Directional :
                                                              FrameState::LightKind::Ambient;

        // Visibility track (KLAV) gates the whole light. 0 = off.
        float visibility = 1.0f;
        if (L.visibilityTracks.isUsed) {
            auto [t,s,e] = effectiveTime(L.visibilityTracks.globalSequenceId);
            visibility = EvaluateTrackF32(L.visibilityTracks, t, s, e, 1.0f);
        }
        ls.enabled = visibility > 0.001f;
        if (!ls.enabled) { fs.lights.push_back(ls); continue; }

        // Diffuse (KLAC/KLAI) and ambient (KLBC/KLBI) color + intensity.
        //
        // Channel order: the MDX *binary* format stores light colors as RGB
        // on disk. Verified against Previewd's ReadBinLight (0x140769930):
        //     pLight->staticColor.r = GetFloat(buf);
        //     pLight->staticColor.g = GetFloat(buf);
        //     pLight->staticColor.b = GetFloat(buf);
        // Third float -> .b. WhiteoutLib reads the three floats straight into
        // Vector3f{x,y,z} in file order, so x=R, y=G, z=B with no swap needed.
        // (The MDL *text* loader at 0x140760b40 writes &staticColor.b first,
        // but that's a separate format; we only see the binary path.)
        //
        // Packing (matches CGxLightToShaderLight at 0x1403fbc00, path A):
        //   diffuse = C4Vector(m_dirColor) * m_dirIntensity          (RGB)
        //   ambient = C4Vector(m_ambColor) * ambLightModifier
        //             + m_ambIntensity (broadcast scalar)
        // In SD mode (our path) ambLightModifier is 0, so ambient collapses
        // to {ambIntensity, ambIntensity, ambIntensity} -- ambColor is
        // IGNORED. Matching that avoids double-contribution oversaturation.
        // Visibility (KLAV) is an enable-gate only; the engine never scales
        // color or intensity by it.
        Vector3f color  = L.color;
        float    inten  = L.intensity;
        float    ambI   = L.ambientIntensity;
        if (L.colorTracks.isUsed) {
            auto [t,s,e] = effectiveTime(L.colorTracks.globalSequenceId);
            Vector3f animatedColor = EvaluateTrackVec3(L.colorTracks, t, s, e, L.color);
            color = {animatedColor.z, animatedColor.y, animatedColor.x}; // BGR -> RGB
        }
        if (L.intensityTracks.isUsed) {
            auto [t,s,e] = effectiveTime(L.intensityTracks.globalSequenceId);
            inten = EvaluateTrackF32(L.intensityTracks, t, s, e, L.intensity);
        }
        if (L.ambientIntensityTracks.isUsed) {
            auto [t,s,e] = effectiveTime(L.ambientIntensityTracks.globalSequenceId);
            ambI = EvaluateTrackF32(L.ambientIntensityTracks, t, s, e, L.ambientIntensity);
        }
        if (inten < 0.0f) inten = 0.0f;     // engine clamps intensity at 0
        if (ambI  < 0.0f) ambI  = 0.0f;
        ls.diffuse = { color.x * inten, color.y * inten, color.z * inten };
        ls.ambient = { ambI, ambI, ambI };

        // World transform of the light's node. The hierarchy's local matrix
        // encodes TRS around the pivot as Translate(-pivot)*S*R*Translate(pivot+t),
        // so transforming {0,0,0} through it collapses to zero for identity
        // TRS -- you have to feed in the PIVOT to recover the node's animated
        // world position. Match the `worldOf` helper used for particle/attachment
        // nodes: premultiply a Translate(pivot) and transform {0,0,0} through.
        const auto& nodes = hierarchy_.Nodes();
        const int   nodeIdx = hierarchy_.ObjectIdToNodeIndex((int)L.node.objectId);
        Matrix44f   world = Matrix44f::identity();
        if (nodeIdx >= 0 && nodeIdx < (int)fs.boneWorldMatrices.size()) {
            const auto& piv = nodes[nodeIdx].pivot;
            Matrix44f pivotT = Matrix44f::translation({piv.x, piv.y, piv.z});
            world = pivotT * fs.boneWorldMatrices[nodeIdx];
        }
        // Omni: node's animated world pivot. Directional: -Z axis of the node
        // in world space. Engine's SetLightDirection (0x14052f540) sets the
        // local direction to (0, 0, -1) and transforms it by the world matrix
        // (translation removed) -- that's the emission direction the shader
        // expects as CGxLight.m_dir.
        if (ls.kind == FrameState::LightKind::Directional) {
            ls.worldDir = whiteout::transform_normal(Vector3f{0, 0, -1}, world);
        } else {
            ls.worldPos = whiteout::transform_point(Vector3f{0, 0, 0}, world);
        }
        ls.attenStart = L.attenuationStart;
        ls.attenEnd   = L.attenuationEnd;
        fs.lights.push_back(ls);
    }

    // Collision shape transforms
    fs.collisionTransforms.resize(model_.collisionShapes.size());
    for (int i = 0; i < (int)model_.collisionShapes.size(); i++) {
        const auto& cs = model_.collisionShapes[i];
        int nodeIdx = hierarchy_.ObjectIdToNodeIndex((int)cs.node.objectId);
        fs.collisionTransforms[i] = (nodeIdx >= 0 && nodeIdx < (int)fs.boneWorldMatrices.size())
                                     ? fs.boneWorldMatrices[nodeIdx] : Matrix44f::identity();
    }

    // Texture animation evaluation. We emit two parallel results:
    //   * texAnimMatrices[] — one composed 2x3 UV matrix per TXAN entry.
    //     This is what the BLS SD VS consumes; indexed by
    //     textureAnimationId so multiple referring layers share one matrix.
    //   * texAnims[]        — legacy per-(material, layer) TRS, kept for
    //     the Slang mesh path until it's retired.
    for (int i = 0; i < (int)model_.textureAnimations.size(); i++) {
        const auto& ta = model_.textureAnimations[i];

        auto [tt, ts, te] = effectiveTime(ta.translationTracks.globalSequenceId);
        Vector3f trans = EvaluateTrackVec3(ta.translationTracks, tt, ts, te, {0, 0, 0});
        auto [st, ss, se] = effectiveTime(ta.scalingTracks.globalSequenceId);
        Vector3f scale = EvaluateTrackVec3(ta.scalingTracks, st, ss, se, {1, 1, 1});
        auto [rt, rs, re] = effectiveTime(ta.rotationTracks.globalSequenceId);
        Quaternion rot = EvaluateTrackQuat(ta.rotationTracks, rt, rs, re,
                                           Quaternion(0, 0, 0, 1));

        // BLS palette entry — Previewd composition order:
        //   final = Translate(t) * Scale-around-(0.5,0.5)(s) * Rotate-around-(0.5,0.5)(r)
        const float ang = 2.0f * std::atan2(rot.z, rot.w);
        const float c = std::cos(ang), si = std::sin(ang);
        const float a =  scale.x * c;
        const float b = -scale.x * si;
        const float d =  scale.y * si;
        const float e =  scale.y * c;
        const float cc = 0.5f - (a * 0.5f + b * 0.5f) + trans.x;
        const float ff = 0.5f - (d * 0.5f + e * 0.5f) + trans.y;

        FrameState::TexAnimMatrix tam{};
        tam.textureAnimId = i;
        tam.row0[0] = a; tam.row0[1] = b; tam.row0[2] = 0.0f; tam.row0[3] = cc;
        tam.row1[0] = d; tam.row1[1] = e; tam.row1[2] = 0.0f; tam.row1[3] = ff;
        fs.texAnimMatrices.push_back(tam);

        // Legacy emission — one entry per referring layer (first only).
        for (int mi = 0; mi < (int)model_.materials.size(); mi++) {
            for (int li = 0; li < (int)model_.materials[mi].layers.size(); li++) {
                const auto& layer = model_.materials[mi].layers[li];
                if ((int)layer.textureAnimationId == i) {
                    FrameState::TexAnimState tas;
                    tas.materialId = mi;
                    tas.layerIndex = li;
                    tas.uOff  = trans.x;
                    tas.vOff  = trans.y;
                    tas.uTile = scale.x;
                    tas.vTile = scale.y;
                    tas.rotation = ang;
                    fs.texAnims.push_back(tas);
                    break;
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
        const auto& pos = cam.position;
        const auto& tgt = cam.targetPosition;

        // Spherical decomposition for legacy UI readouts only.
        float dx = pos.x - tgt.x, dy = pos.y - tgt.y, dz = pos.z - tgt.z;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (dist < 0.01f) dist = 100.0f;
        float invD = 1.0f / dist;
        float pitch = std::asin(std::clamp(dz * invD, -1.0f, 1.0f));
        float yaw   = std::atan2(dy * invD, dx * invD);

        CameraPreset cp;
        cp.name        = std::wstring(cam.name.begin(), cam.name.end());
        cp.position    = pos;
        cp.target      = tgt;
        cp.fovDiagonal = cam.fieldOfView;
        cp.zNear       = cam.nearClippingPlane;
        cp.zFar        = cam.farClippingPlane;
        cp.staticRoll  = 0.0f;
        cp.pitch       = pitch;
        cp.yaw         = yaw;
        cp.distance    = dist;
        cp.isLive      = false;

        const bool animated = cam.positionTracks.isUsed
                           || cam.targetPositionTracks.isUsed
                           || cam.targetRotationTracks.isUsed;
        if (animated) {
            auto posTracks  = cam.positionTracks;
            auto tgtTracks  = cam.targetPositionTracks;
            auto rollTracks = cam.targetRotationTracks;
            Vector3f pivot       = pos;
            Vector3f targetPivot = tgt;
            cp.animator = [posTracks  = std::move(posTracks),
                           tgtTracks  = std::move(tgtTracks),
                           rollTracks = std::move(rollTracks),
                           pivot, targetPivot]
                (Vector3f& outPos, Vector3f& outTgt,
                 float& outRoll, int timeMs,
                 int seqStart, int seqEnd) {
                const Vector3f zero{0.0f, 0.0f, 0.0f};
                Vector3f posDelta = EvaluateTrackVec3(posTracks, timeMs, seqStart, seqEnd, zero);
                Vector3f tgtDelta = EvaluateTrackVec3(tgtTracks, timeMs, seqStart, seqEnd, zero);
                outPos  = { pivot.x       + posDelta.x, pivot.y       + posDelta.y, pivot.z       + posDelta.z };
                outTgt  = { targetPivot.x + tgtDelta.x, targetPivot.y + tgtDelta.y, targetPivot.z + tgtDelta.z };
                outRoll = EvaluateTrackF32(rollTracks, timeMs, seqStart, seqEnd, 0.0f);
            };
        }

        presets.push_back(std::move(cp));
    }
    return presets;
}

} // namespace WhiteoutDex
