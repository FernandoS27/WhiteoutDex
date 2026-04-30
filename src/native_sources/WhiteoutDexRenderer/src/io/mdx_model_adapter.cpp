// ============================================================================
// MDX Model Adapter — Translates WhiteoutLib MDX types to IModelSource.
// ============================================================================

#include "mdx_model_adapter.h"
#include "content_provider.h"
#include "team_glow_data.h"
#include "texture_image_usage.h"
#include "renderer/model_source_utils.h"
#include <cmath>
#include <cstdio>

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
    auto transformAll = [](auto& arr) {
        for (auto& x : arr) transformNodeTracks(x.node);
    };
    transformAll(m.bones);
    transformAll(m.helpers);
    transformAll(m.attachments);
    transformAll(m.lights);
    transformAll(m.particleEmitters);
    transformAll(m.particleEmitters2);
    transformAll(m.ribbonEmitters);
    transformAll(m.eventObjects);
    transformAll(m.cornEmitters);
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

// ============================================================================
// File-local helpers — MDX-specific subtexture lookup. The generic bit-test /
// solid-fill / parser-dispatch helpers now live in renderer/model_source_utils.h
// since they're shared with MaxSceneAdapter.
// ============================================================================

// Pick the DiffuseMap subtexture (falling back to slot [0] for layers that
// didn't tag one). Returns nullptr only when the subTextures list is empty.
inline const Layer::SubTexture* FindDiffuseSubTexture(
    const std::vector<Layer::SubTexture>& subs) {
    if (subs.empty()) return nullptr;
    for (const auto& s : subs)
        if (s.slot == Layer::SlotType::DiffuseMap) return &s;
    return &subs[0];
}

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

    // Build the per-hierarchy-node bone-visibility gate table once.
    // Previewd's CreateBone @0x1404573d0 caches
    //   CAnimBoneObj::geosetId = (bone.geosetId == -1) ? -1 : bone.geosetAnimId
    // and CAnimBoneObj::IsVisible queries anim->geosetStatus[geosetAnimId]
    // (indexed by GeosetAnimation). We translate that to an index into
    // fs.geosetAlphas (indexed by target Geoset) so the per-frame sweep can
    // AND against the geoset's animated alpha directly.
    const auto& nodes = hierarchy_.Nodes();
    boneGateGeoset_.assign(nodes.size(), -1);
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].source != HierarchyNode::Source::Bone) continue;
        const auto& bone = model_.bones[nodes[i].sourceIndex];
        if (bone.geosetId == whiteout::mdx::Bone::MULTIPLE_GEOSETS) continue;
        const whiteout::u32 gaId = bone.geosetAnimationId;
        if (gaId == whiteout::mdx::Bone::MULTIPLE_GEOSETS) continue;
        if (gaId >= model_.geosetAnimations.size()) continue;
        const whiteout::u32 targetGeoset = model_.geosetAnimations[gaId].geosetId;
        if (targetGeoset >= model_.geosets.size()) continue;
        boneGateGeoset_[i] = (int)targetGeoset;
    }
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

        // Second UVAS channel: only present when the MDX geoset declared
        // it (UVAS numChannels >= 2). Layers with CoordID == 1 sample
        // from this stream; the renderer bakes a sibling VB at template
        // upload only when some material layer actually targets it.
        const bool hasUv1 = gs.textureCoordinateSets.size() >= 2
                         && (int)gs.textureCoordinateSets[1].size() == vc;
        if (hasUv1) mesh.uvs1.resize(vc);

        // Positions, normals, and tangents are already in our default coord
        // space — TransformMdxModelToMaxCoords ran at load time, so a plain
        // copy preserves W (handedness) and world-space XYZ.
        for (int v = 0; v < vc; v++) {
            mesh.positions[v] = gs.vertexPositions[v];
            if (v < (int)gs.vertexNormals.size())
                mesh.normals[v] = gs.vertexNormals[v];
            if (!gs.textureCoordinateSets.empty() &&
                v < (int)gs.textureCoordinateSets[0].size()) {
                mesh.uvs[v] = {gs.textureCoordinateSets[0][v].x,
                               gs.textureCoordinateSets[0][v].y};
            }
            if (hasUv1) {
                mesh.uvs1[v] = {gs.textureCoordinateSets[1][v].x,
                                gs.textureCoordinateSets[1][v].y};
            }
            if (hasTangents) mesh.tangents[v] = gs.tangents[v];
        }

        mesh.indices.assign(gs.faces.begin(), gs.faces.end());

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
    // Cross-model dedup key. The MDX path string is the canonical resource
    // identifier — two models referencing `Textures/Dirt.blp` produce one
    // GPU upload via TextureAssetManager's shared cache.
    td.sharedKey     = NormalizeTextureKey(path);

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
        // Engine policy: filename-suffix-driven sRGB / linear (mirrors
        // CImageFile::DetermineImageUsage @ Preview 0x7ff609bad260).
        // Ignores the file's stored DXGI flag — same behaviour as
        // `CreateImageTexture`'s `(imageUsage - 1) > 1` override.
        gfxFmt = ApplyTextureSrgbPolicy(gfxFmt, path);
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
        // path::string() uses the ANSI code page on Windows, which corrupts
        // non-ASCII characters (e.g. CJK). path::u8string() gives correct
        // UTF-8 bytes that WhiteoutLib parsers accept.
        auto u8 = p.u8string();
        std::string pathStr(reinterpret_cast<const char*>(u8.data()), u8.size());
        auto result = DispatchTextureParser(ExtensionLower(p),
            [&](auto& parser) { return parser.parse(pathStr); });
        if (result) { applyResult(*result); return true; }
        return false;
    };

    // Try parsing from a memory buffer (CASC/MPQ source).
    auto tryParseBuffer = [&](std::span<const uint8_t> buf, const std::string& ext) -> bool {
        auto result = DispatchTextureParser(ext,
            [&](auto& parser) { return parser.parse(buf); });
        if (result) { applyResult(*result); return true; }
        return false;
    };

    // 1. Try local disk via FileResolver.
    fs::path resolved = resolver_.ResolveTexture(path);
    if (!resolved.empty() && tryParsePath(resolved)) {
        return td;
    }

    // 2. Try CASC/MPQ via FileContentProvider.
    if (contentProvider_) {
        std::string foundExt;
        auto data = contentProvider_->ReadFile(path, &foundExt);
        if (data) {
            if (foundExt.empty()) foundExt = ExtensionLower(fs::path(path));
            if (tryParseBuffer(*data, foundExt)) {
                return td;
            }
        }
    }

    auto u8base = resolver_.BasePath().u8string();
    std::fprintf(stderr, "  [tex %d] NOT FOUND: '%s' (base: %s)\n",
                 textureId, path.c_str(), reinterpret_cast<const char*>(u8base.data()));

    // Leave the TextureData invalid (width = height = 0, no pixels).
    // UploadStagedTextures sees the zero-size and skips the upload, so
    // the per-actor ModelScope::Get(id) returns gfx::TextureHandle::
    // Invalid at draw time. `bindMaterialTex` then falls through to the
    // slot-specific default (White for t0 albedo, FlatNormal for t1,
    // NeutralOrm for t2, Black for t3/t4) — which means a missing ORM
    // file gives the engine-faithful (1,1,0,0) instead of magenta
    // (which the multi-layer-blend math would otherwise read as
    // metal=1, teamBlend=1 and turn the surface into a chrome-finish
    // team-coloured mirror).
    return td;
}

std::vector<TextureData> MdxModelAdapter::GetTextures() {
    std::vector<TextureData> result;
    result.reserve(model_.textures.size());

    for (int i = 0; i < (int)model_.textures.size(); i++) {
        const auto& tex = model_.textures[i];
        TextureData td;
        if (tex.replaceableId != 0) {
            // Replaceable slot: adapter only declares the id; the
            // ReplaceableTextureManager::RegisterModelSlot path resolves
            // the canonical CASC asset (or bakes a TeamColor / TeamGlow
            // swatch for ids 1/2) and stamps the pixels into stagedTextures.
            // Leaving width/height = 0 here keeps UploadStagedTextures
            // off this slot until the manager fills it.
            td.textureId     = i;
            td.replaceableId = (int)tex.replaceableId;
            td.width = td.height = 0;
        } else if (!tex.fileName.empty()) {
            // Skip the BLP/CASC decode entirely when the renderer's
            // shared cache already holds this path — UploadStagedTextures
            // will detect (sharedKey != "" && pixels empty) and borrow
            // from the cache via TextureAssetManager::BindShared.
            std::string sharedKey = NormalizeTextureKey(tex.fileName);
            if (IsTextureCached(sharedKey)) {
                td.textureId     = i;
                td.replaceableId = (int)tex.replaceableId;
                td.width = td.height = 0;
                td.sharedKey = std::move(sharedKey);
            } else {
                td = LoadTextureFile(tex.fileName, i, (int)tex.replaceableId);
            }
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
    using SF = Layer::ShadingFlag;
    const u32 s = (u32)sf;
    int flags = 0;
    if (hasFlag(s, SF::TwoSided))    flags |= MAT_TWO_SIDED;
    if (hasFlag(s, SF::Unshaded))    flags |= MAT_UNSHADED;
    if (hasFlag(s, SF::Unfogged))    flags |= MAT_UNFOGGED;
    if (hasFlag(s, SF::NoDepthTest)) flags |= MAT_NO_DEPTH_TEST;
    if (hasFlag(s, SF::NoDepthSet))  flags |= MAT_NO_DEPTH_SET;
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

        for (int li = 0; li < (int)mat.layers.size(); ++li) {
            const auto& layer = mat.layers[li];
            MaterialLayerData ld;
            ld.filterMode = MapFilterMode((int)layer.filterMode);
            ld.alpha      = layer.alpha;
            ld.flags      = MapShadingFlags(layer.shadingFlags);
            ld.textureAnimationId = ((int32_t)layer.textureAnimationId < 0
                                     || (int32_t)layer.textureAnimationId >= (int)model_.textureAnimations.size())
                                    ? -1 : (int)layer.textureAnimationId;
            // CoordID maps the layer to one of the geoset's UVAS channels.
            // SphereEnvMap (Layer::ShadingFlag::SphereEnvMap = 0x2) tells the
            // engine to compute UVs procedurally from normal/eye instead of
            // sampling — Previewd's ProcessTexLayers (@0x7ff609b69e40) writes
            // -1 into the runtime layer in that case. We mirror that
            // sentinel; the draw path treats coordId == -1 as "synthesise"
            // (currently falls back to channel 0 since no env-map shader
            // permute is wired yet).
            if (hasFlag((u32)layer.shadingFlags, Layer::ShadingFlag::SphereEnvMap)) {
                ld.coordId = -1;
            } else {
                ld.coordId = static_cast<int>(layer.coordId);
            }
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
                // Reforged multi-texture layout: subtextures bind to t0..t5
                // by ARRAY POSITION, not by their `slot` (texSemantic) tag.
                // Previewd's ProcessTexLayers (@0x7ff609b69e40) calls
                // `CWar3Mat::SetTexture(mat, subIdx, tex, semantic)` where
                // `subIdx` is the loop counter — the semantic is metadata,
                // never the actual GPU register. Authored content can (and
                // does) tag sibling subtextures with the same SlotType
                // (e.g. berserkelemental.mdx mat 3 has sub[0] and sub[1]
                // both tagged DiffuseMap, with sub[1] supplying the normal
                // map by position). Switching on slot would route both into
                // the diffuse field; binding by index matches the engine.
                auto subAt = [&](size_t pos) -> int {
                    return pos < layer.subTextures.size()
                               ? (int)layer.subTextures[pos].textureId : -1;
                };
                ld.textureId      = subAt(0);   // t0 albedo
                ld.normalMapId    = subAt(1);   // t1 normal
                ld.ormMapId       = subAt(2);   // t2 ORM
                ld.emissiveMapId  = subAt(3);   // t3 emissive
                // t4 team-colour: live-swatch sentinel logic still applies,
                // but the resolution key is "what's at position 4", not
                // "the SlotType::TeamColor subtexture".
                if (layer.subTextures.size() > 4) {
                    const int tex = (int)layer.subTextures[4].textureId;
                    if (tex >= 0 && tex < (int)model_.textures.size()
                        && model_.textures[tex].replaceableId == 1) {
                        ld.teamColorMapId = kHdTeamColorActive;
                    } else {
                        ld.teamColorMapId = tex;
                    }
                }
                // Position 5 (typically EnvironmentMap) currently has no
                // bind site in our HD draw path — drop it here.
            } else {
                ld.textureId = (int)layer.textureId;
            }

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
    using NF = whiteout::mdx::Node::NodeFlag;
    for (int i = 0; i < (int)nodes.size(); i++) {
        const uint32_t nf = nodes[i].flags;
        sk.billboardFlags[i] = PackBillboardFlags(
            hasFlag(nf, NF::Billboarded),
            hasFlag(nf, NF::BillboardedLockX),
            hasFlag(nf, NF::BillboardedLockY),
            hasFlag(nf, NF::BillboardedLockZ),
            hasFlag(nf, NF::CameraAnchored));
        sk.nodePivots[i]  = nodes[i].pivot;
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
    // Fallback to ObjectIdToNodeIndex when splitIndex is out of range so
    // third-party exporters that stuff objectIds into MATS still work.
    //
    // Each geoset emits a COMPACT PALETTE SUBSET of just the bones it uses.
    // VertexInfluence::boneIdx is written as a LOCAL slot index into this
    // subset so it fits in uint8 (the BLS shaders read ATTR6 as R8G8B8A8
    // — only 256 distinct indices addressable). This mirrors Previewd's
    // `usedBonesPerPrimitive` mechanism. The model-wide bone palette would
    // fail for models like nightelf_exp (650 bones).
    //
    // v800 matrix-groups with count > 4 get a pseudo slot appended AFTER the
    // subset bones (mirrors BuildPrimBone's `sum / N` for N > 3). Count <= 4
    // groups keep the uniform 1/N per-vertex weights path (mathematically
    // equivalent to matrix averaging).
    auto resolveBoneIdx = [&](int matsValue) -> int {
        int nodeIdx = hierarchy_.BoneIndexToNodeIndex(matsValue);
        if (nodeIdx < 0) nodeIdx = hierarchy_.ObjectIdToNodeIndex(matsValue);
        return (nodeIdx >= 0) ? nodeIdx : 0;
    };

    constexpr int kMaxPaletteSlots = 256;  // matches bls::kMaxBones / uint8 index cap

    for (int gi = 0; gi < (int)model_.geosets.size(); gi++) {
        const auto& gs = model_.geosets[gi];
        int vc = (int)gs.vertexPositions.size();
        SkinWeightData sw;
        sw.geosetId = gi;
        sw.influences.resize(vc);

        // Build a stable, deduplicated subset of global node indices referenced
        // by this geoset's skin. Insertion order = slot order.
        std::vector<int> subset;
        std::unordered_map<int, int> globalToLocal;
        auto addToSubset = [&](int globalIdx) -> int {
            auto it = globalToLocal.find(globalIdx);
            if (it != globalToLocal.end()) return it->second;
            int local = (int)subset.size();
            subset.push_back(globalIdx);
            globalToLocal.emplace(globalIdx, local);
            return local;
        };

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
                        int globalNode = resolveBoneIdx(matsValue);
                        int localSlot  = addToSubset(globalNode);
                        sw.influences[v].boneIdx[k] = localSlot;
                        sw.influences[v].weight[k]  = gs.skinData[base + 4 + k] / 255.0f;
                    }
                }
            }
        } else if (!gs.vertexGroups.empty() && !gs.matrixGroups.empty()) {
            // v800: vertexGroups + matrixGroups + matrixIndices indirection.
            std::vector<u32> groupStart(gs.matrixGroups.size() + 1, 0);
            for (int g = 0; g < (int)gs.matrixGroups.size(); g++)
                groupStart[g + 1] = groupStart[g] + gs.matrixGroups[g];

            // First pre-populate subset with every bone referenced by any
            // N<=4 group, so local slots for direct references stay low and
            // pseudo slots for N>4 groups cleanly follow them at the end.
            std::vector<int> groupPseudoSlotGlobal(gs.matrixGroups.size(), -1);
            for (int g = 0; g < (int)gs.matrixGroups.size(); g++) {
                u32 count = gs.matrixGroups[g];
                if (count > 4) continue;  // deferred to the pseudo-slot pass
                u32 start = groupStart[g];
                u32 clamp = count > 4 ? 4 : count;
                for (u32 k = 0; k < clamp && (start + k) < gs.matrixIndices.size(); k++) {
                    int globalNode = resolveBoneIdx((int)gs.matrixIndices[start + k]);
                    addToSubset(globalNode);
                }
            }

            // Now assign pseudo slots (AFTER the subset bones). nodeIndices
            // stay in GLOBAL positions — they're the source for per-frame
            // averaging against SkinningSystem::OffsetMatrices().
            for (int g = 0; g < (int)gs.matrixGroups.size(); g++) {
                u32 count = gs.matrixGroups[g];
                if (count <= 4) continue;
                int pseudoLocal = (int)subset.size() + (int)sw.groupAverages.size();
                if (pseudoLocal >= kMaxPaletteSlots) {
                    std::fprintf(stderr,
                        "[GetSkinWeights] geoset %d group %d (N=%u) exceeds per-geoset"
                        " palette cap (%d); falling back to 4-bone clamp\n",
                        gi, g, count, kMaxPaletteSlots);
                    continue;
                }
                GroupAverageRecord rec;
                rec.pseudoSlot = pseudoLocal;
                rec.nodeIndices.reserve(count);
                u32 start = groupStart[g];
                for (u32 k = 0; k < count && (start + k) < gs.matrixIndices.size(); k++) {
                    int nodeIdx = resolveBoneIdx((int)gs.matrixIndices[start + k]);
                    rec.nodeIndices.push_back(nodeIdx);
                }
                groupPseudoSlotGlobal[g] = pseudoLocal;
                sw.groupAverages.push_back(std::move(rec));
            }

            // Per-vertex: write local slot indices.
            for (int v = 0; v < vc && v < (int)gs.vertexGroups.size(); v++) {
                int groupId = gs.vertexGroups[v];
                if (groupId >= (int)gs.matrixGroups.size()) continue;

                int pseudoLocal = groupPseudoSlotGlobal[groupId];
                if (pseudoLocal >= 0) {
                    sw.influences[v].boneIdx[0] = pseudoLocal;
                    sw.influences[v].weight[0]  = 1.0f;
                } else {
                    u32 start = groupStart[groupId];
                    u32 count = gs.matrixGroups[groupId];
                    if (count > 4) count = 4;
                    float w = (count > 0) ? 1.0f / (float)count : 0.0f;
                    for (u32 k = 0; k < count && (start + k) < gs.matrixIndices.size(); k++) {
                        int globalNode = resolveBoneIdx((int)gs.matrixIndices[start + k]);
                        auto it = globalToLocal.find(globalNode);
                        int localSlot = (it != globalToLocal.end()) ? it->second
                                                                    : addToSubset(globalNode);
                        sw.influences[v].boneIdx[k] = localSlot;
                        sw.influences[v].weight[k]  = w;
                    }
                }
            }
        }

        // Check final cap including pseudo slots.
        const int paletteSlotsUsed = (int)subset.size() + (int)sw.groupAverages.size();
        if (paletteSlotsUsed > kMaxPaletteSlots) {
            std::fprintf(stderr,
                "[GetSkinWeights] geoset %d needs %d palette slots (>%d)\n",
                gi, paletteSlotsUsed, kMaxPaletteSlots);
        }

        sw.subsetNodeIndices = std::move(subset);
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
        const u32 nf = (u32)pe.node.flags;
        using NF = Node::NodeFlag;
        cfg.modelSpace  = hasFlag(nf, NF::ModelSpace);
        cfg.xyQuad      = hasFlag(nf, NF::XYQuad);
        cfg.sortZ       = hasFlag(nf, NF::SortPrimitives);
        cfg.unshaded    = hasFlag(nf, NF::Unshaded);
        cfg.lineEmitter = hasFlag(nf, NF::LineEmitter);
        cfg.unfogged    = hasFlag(nf, NF::Unfogged);

        cfg.priorityPlane  = (int)pe.priorityPlane;
        cfg.replaceableId  = (int)pe.replaceableId;
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
        using NF = Node::NodeFlag;
        init.modelSpace = hasFlag(nf, NF::ModelSpace);
        init.xyQuads    = hasFlag(nf, NF::XYQuad);
        init.sortZ      = hasFlag(nf, NF::SortPrimitives);
        init.longitude  = hasFlag(nf, NF::LineEmitter) ? 0.0f : 6.2831853071795864769f;

        // Material descriptor. Single texture slot 0; MDX `textureId` is the
        // index into the model's texture table (resolved by the render service).
        init.material.textureId     = static_cast<int>(pe.textureId);
        init.material.filterMode    = MapToServiceFilterMode(pe.filterMode);
        init.material.unshaded      = hasFlag(nf, NF::Unshaded);
        init.material.unfogged      = hasFlag(nf, NF::Unfogged);
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
            const auto& mat   = model_.materials[rb.materialId];
            const auto& layer = mat.layers[0];

            if (const auto* diffuse = FindDiffuseSubTexture(layer.subTextures))
                cfg.textureId = (int)diffuse->textureId;
            else
                cfg.textureId = (int)layer.textureId;

            cfg.filterMode = MapFilterMode((int)layer.filterMode);

            const u32 sf = (u32)layer.shadingFlags;
            cfg.unshaded = hasFlag(sf, Layer::ShadingFlag::Unshaded);
            cfg.twoSided = hasFlag(sf, Layer::ShadingFlag::TwoSided);

            // Ribbons inherit priorityPlane from their referenced material
            // (MDX MATS chunk carries it per material, mirroring how
            // geosets get theirs). PE2 emitters carry their own priority
            // because they own an inline material rather than referencing
            // one — see MDLPARTICLEEMITTER2::priorityPlane vs ribbons'
            // materialId indirection.
            cfg.priorityPlane = (int)mat.priorityPlane;
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
        if (cs.vertices.size() >= 1) cd.vertices[0] = cs.vertices[0];
        if (cs.vertices.size() >= 2) cd.vertices[1] = cs.vertices[1];
        // Previewd builds geoset vertices at `pivot + extent` in bind-pose world;
        // the bone matrix we apply is a skinning delta (identity at bind). Store the
        // pivot so the renderer can reconstruct the bind-pose world-space corners.
        if (cs.node.objectId < model_.pivotPoints.size())
            cd.pivot = model_.pivotPoints[cs.node.objectId];
        result.push_back(cd);
    }
    return result;
}

// ============================================================================
// Evaluate — Per-frame animation evaluation
//
// Phase 3: stateless — sequence + camera arrive as parameters from
// AnimationDriver instead of being set via SetActiveSequence /
// SetCameraPosition. The const qualifier reflects that.
// ============================================================================

FrameState MdxModelAdapter::Evaluate(int sequenceIdx, int timeMs, int globalTimeMs,
                                     const Matrix44f& worldTransform,
                                     const Vector3f& cameraPos) const {
    // Resolve sequence range. Out-of-range indices collapse to (0, 0) — same
    // behaviour the old SetActiveSequence(invalid) path produced.
    int seqStart = 0, seqEnd = 0;
    if (sequenceIdx >= 0 && sequenceIdx < (int)model_.sequences.size()) {
        seqStart = (int)model_.sequences[sequenceIdx].intervalStart;
        seqEnd   = (int)model_.sequences[sequenceIdx].intervalEnd;
    }

    FrameState fs;

    // Evaluate bone hierarchy (pass camera position for billboard nodes)
    std::vector<Matrix44f> boneWorld, allNodes;
    hierarchy_.Evaluate(timeMs, seqStart, seqEnd,
                        model_.globalSequences, boneWorld, allNodes,
                        &cameraPos, globalTimeMs);
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
            } else {
                // duration==0: static global sequence — hold the single key permanently.
                // The key may be at any frame (e.g. 65667), so use a wide range
                // to ensure FindBracket finds it.
                return {0, 0, 0x3FFFFFFF};
            }
        }
        return {timeMs, seqStart, seqEnd};
    };

    // Per-track eval helpers: fold effectiveTime + EvaluateTrack* into one call.
    // `def` is the fallback used both when the track is not authored (Evaluate*
    // returns it verbatim) and when every key lies outside the active sequence.
    auto evalF32 = [&](const Track<f32>& tr, float def, bool forceNoInterp = false) {
        auto [t, s, e] = effectiveTime(tr.globalSequenceId);
        return EvaluateTrackF32(tr, t, s, e, def, forceNoInterp);
    };
    auto evalVec3 = [&](const Track<Vector3f>& tr, const Vector3f& def) {
        auto [t, s, e] = effectiveTime(tr.globalSequenceId);
        return EvaluateTrackVec3(tr, t, s, e, def);
    };
    auto evalQuat = [&](const Track<Quaternion>& tr, const Quaternion& def) {
        auto [t, s, e] = effectiveTime(tr.globalSequenceId);
        return EvaluateTrackQuat(tr, t, s, e, def);
    };
    auto evalU32 = [&](const Track<u32>& tr, u32 def) {
        auto [t, s, e] = effectiveTime(tr.globalSequenceId);
        return EvaluateTrackU32(tr, t, s, e, def);
    };

    // Resolve a MDX Node's objectId → hierarchy palette position.
    auto nodeOf = [&](const Node& n) {
        return hierarchy_.ObjectIdToNodeIndex((int)n.objectId);
    };

    // GeosetAnimation evaluation
    int geosetCount = (int)model_.geosets.size();
    fs.geosetAlphas.assign(geosetCount, 1.0f);
    fs.geosetColors.assign(geosetCount, Vector3f(1, 1, 1));

    for (const auto& ga : model_.geosetAnimations) {
        int gid = (int)ga.geosetId;
        if (gid < 0 || gid >= geosetCount) continue;

        // Match Previewd's SetGeosetAlpha @0x14019c4d0: when the KGAO track is
        // authored, the fresh-sequence identity is 1.0 (not the static
        // ga.alpha). The static only applies when no KGAO track is authored.
        const float identity = ga.alphaTracks.isUsed ? 1.0f : ga.alpha;
        fs.geosetAlphas[gid] = evalF32(ga.alphaTracks, identity);

        // KGAC colors are stored BGR on the wire; static ga.color is RGB.
        if (ga.colorTracks.isUsed) {
            Vector3f c = evalVec3(ga.colorTracks, ga.color);
            fs.geosetColors[gid] = {c.z, c.y, c.x};
        } else {
            fs.geosetColors[gid] = ga.color;
        }
    }

    // Per-frame bone-ancestor visibility sweep. Matches Previewd's DFS
    // skip in PrepareObjectHierarchyViews @0x140535390: any node whose
    // ancestor chain includes a bone with a zero-byte KGAO alpha is
    // considered invisible for the frame. `nodes` is topologically sorted
    // (parent before child) so one forward pass resolves the transitive
    // closure. Non-bone nodes have boneGateGeoset_[i] == -1 and simply
    // propagate the parent's visibility.
    const auto& hierNodes = hierarchy_.Nodes();
    std::vector<uint8_t> nodeVisible(hierNodes.size(), 1);
    for (size_t ni = 0; ni < hierNodes.size(); ++ni) {
        uint8_t vis = (hierNodes[ni].parentIdx < 0)
                        ? uint8_t{1}
                        : nodeVisible[hierNodes[ni].parentIdx];
        const int gate = boneGateGeoset_[ni];
        if (vis && gate >= 0 && fs.geosetAlphas[gate] <= 0.0f) vis = 0;
        nodeVisible[ni] = vis;
    }
    auto gateByBoneAncestors = [&](int nodeIdx) -> float {
        return (nodeIdx >= 0 && nodeIdx < (int)nodeVisible.size() &&
                !nodeVisible[nodeIdx])
                   ? 0.0f
                   : 1.0f;
    };

    // Layer alpha (KMTA) evaluation — per material layer
    for (int mi = 0; mi < (int)model_.materials.size(); mi++) {
        const auto& mat = model_.materials[mi];
        for (int li = 0; li < (int)mat.layers.size(); li++) {
            const auto& layer = mat.layers[li];
            if (!layer.alphaTracks.isUsed) continue;
            FrameState::LayerAlphaState las;
            las.materialId = mi;
            las.layerIndex = li;
            las.alpha      = evalF32(layer.alphaTracks, layer.alpha);
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
            lfs.materialId       = mi;
            lfs.layerIndex       = li;
            lfs.fresnelColor     = evalVec3(layer.fresnelColorTracks,     layer.fresnelColor);
            lfs.fresnelOpacity   = evalF32 (layer.fresnelAlphaTracks,     layer.fresnelOpacity);
            lfs.fresnelTeamColor = evalF32 (layer.fresnelTeamColorTracks, layer.fresnelTeamColor);
            lfs.emissiveGain     = evalF32 (layer.emissiveGainTracks,     layer.emissiveGain);
            fs.layerFresnels.push_back(lfs);
        }
    }

    // Layer texture ID (KMTF) evaluation — animated texture swap per layer.
    //
    // Classic (v800-v1100): KMTF lives on layer.textureIdTracks and only
    // animates the (single) diffuse texture. One state emitted, slot=Diffuse.
    //
    // Reforged (v1200+): every SubTexture carries its own KMTF track. Each
    // animated slot — Normal/ORM/Emissive/TeamColor as well as Diffuse —
    // emits an independent state so the per-slot bind in the HD draw path
    // can pick up animated normal/ORM/emissive/team swaps, not just diffuse.
    // ApplyLayerStates routes the state into the matching *MapId field by
    // slot; states for slots we don't render (EnvironmentMap and beyond)
    // are dropped there.
    for (int mi = 0; mi < (int)model_.materials.size(); mi++) {
        const auto& mat = model_.materials[mi];
        for (int li = 0; li < (int)mat.layers.size(); li++) {
            const auto& layer = mat.layers[li];

            // Classic path: single track on the layer itself.
            if (layer.textureIdTracks.isUsed) {
                FrameState::LayerTextureIdState lts;
                lts.materialId = mi;
                lts.layerIndex = li;
                lts.slot       = FrameState::LayerTexSlot::Diffuse;
                lts.textureId  = (int)evalU32(layer.textureIdTracks, layer.textureId);
                fs.layerTextureIds.push_back(lts);
                continue;
            }

            // Reforged path: walk every subtexture for an animated track.
            // The runtime slot is the subtexture's ARRAY INDEX, not its
            // `slot` (texSemantic) tag — Previewd's ProcessTexLayers binds
            // by position, and shipping content sometimes mistags siblings
            // (e.g. berserkelemental.mdx mat 3 has both sub[0] and sub[1]
            // tagged DiffuseMap, with sub[1] actually feeding t1/normal).
            // Position 5 (typically EnvironmentMap) has no bind site so
            // we skip it here rather than emit a state the apply step
            // would silently drop.
            for (size_t k = 0; k < layer.subTextures.size(); ++k) {
                const auto& sub = layer.subTextures[k];
                if (!sub.tracks.isUsed) continue;
                FrameState::LayerTexSlot slot;
                switch (k) {
                    case 0: slot = FrameState::LayerTexSlot::Diffuse;   break;
                    case 1: slot = FrameState::LayerTexSlot::Normal;    break;
                    case 2: slot = FrameState::LayerTexSlot::ORM;       break;
                    case 3: slot = FrameState::LayerTexSlot::Emissive;  break;
                    case 4: slot = FrameState::LayerTexSlot::TeamColor; break;
                    default: continue;                  // position 5+ (EnvMap) — no bind
                }
                FrameState::LayerTextureIdState lts;
                lts.materialId = mi;
                lts.layerIndex = li;
                lts.slot       = slot;
                lts.textureId  = (int)evalU32(sub.tracks, sub.textureId);
                fs.layerTextureIds.push_back(lts);
            }
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
        const int nodeIdx = nodeOf(pe.node);

        ps.emitterId = i;
        // Lift into scene world space for nested actors (PE1 children, etc.)
        // — for top-level actors worldTransform is identity so this is a no-op.
        // Particle/ribbon/attachment draws use frame.world=identity, so the
        // emitter transform must already be in scene world space here.
        ps.transform = kPE2SpawnFrameRotation * worldOf(nodeIdx) * worldTransform;

        const bool squirting = (pe.squirt != 0);
        ps.squirting    = squirting;
        ps.emissionRate = evalF32(pe.emissionRateTracks, pe.emissionRate, squirting);
        ps.speed        = evalF32(pe.speedTracks,     pe.speed);
        ps.variation    = evalF32(pe.variationTracks, pe.variation);
        // MDX stores latitude in degrees; Previewd's SetEmitterLatitude2
        // (0x140530917) and ILoadParticleEmitters2 (0x14049e4f3) both convert
        // via deg * pi / 180 before calling CPlaneParticleEmitter::SetLatitude.
        // CreateParticle consumes it as radians (sin/cos).
        ps.coneAngle    = evalF32(pe.latitudeTracks, pe.latitude) * (3.14159265358979323846f / 180.0f);
        ps.gravity      = evalF32(pe.gravityTracks, pe.gravity);
        ps.width        = evalF32(pe.widthTracks,   pe.width);
        ps.length       = evalF32(pe.lengthTracks,  pe.length);
        ps.visibility   = evalF32(pe.visibilityTracks, 1.0f) * gateByBoneAncestors(nodeIdx);
    }

    // Attachment transforms
    for (int i = 0; i < (int)model_.attachments.size(); i++) {
        const auto& att = model_.attachments[i];
        const int nodeIdx = nodeOf(att.node);
        const float vis = evalF32(att.visibilityTracks, 1.0f) * gateByBoneAncestors(nodeIdx);
        fs.attachmentStates.push_back({i, worldOf(nodeIdx) * worldTransform, vis});
    }

    // PE1 (model particle emitter) per-frame state. Lat/lon already in radians
    // in MDX so no unit conversion needed.
    for (int i = 0; i < (int)model_.particleEmitters.size(); i++) {
        const auto& pe = model_.particleEmitters[i];
        const int nodeIdx = nodeOf(pe.node);

        FrameState::PE1FrameState ps;
        ps.emitterId    = i;
        ps.transform    = worldOf(nodeIdx) * worldTransform;
        ps.emissionRate = evalF32(pe.emissionRateTracks, pe.emissionRate);
        ps.speed        = evalF32(pe.speedTracks,        pe.initialVelocity);
        ps.latitude     = evalF32(pe.latitudeTracks,     pe.latitude);
        ps.longitude    = evalF32(pe.longitudeTracks,    pe.longitude);
        ps.gravity      = evalF32(pe.gravityTracks,      pe.gravity);
        ps.visibility   = evalF32(pe.visibilityTracks,   1.0f) * gateByBoneAncestors(nodeIdx);
        fs.pe1States.push_back(ps);
    }

    // Ribbon emitter per-frame state
    fs.ribbonStates.resize(model_.ribbonEmitters.size());

    for (int i = 0; i < (int)model_.ribbonEmitters.size(); i++) {
        const auto& rb = model_.ribbonEmitters[i];
        auto& rs = fs.ribbonStates[i];
        const int nodeIdx = nodeOf(rb.node);

        rs.emitterId  = i;
        rs.transform  = worldOf(nodeIdx) * worldTransform;
        rs.above      = evalF32(rb.heightAboveTracks, rb.heightAbove);
        rs.below      = evalF32(rb.heightBelowTracks, rb.heightBelow);
        rs.alpha      = evalF32(rb.alphaTracks,       rb.alpha);
        rs.visibility = evalF32(rb.visibilityTracks,  1.0f) * gateByBoneAncestors(nodeIdx);
        rs.slot       = (int)evalU32(rb.textureSlotTracks, rb.textureSlot);

        // MDX ribbon color is stored BGR when animated (CImVector wire layout);
        // the static rb.color is already RGB. Swap on the animated path so the
        // renderer always sees RGB. Same convention applies to GeosetAnim and
        // Light color tracks.
        if (rb.colorTracks.isUsed) {
            Vector3f c = evalVec3(rb.colorTracks, rb.color);
            rs.color = {c.z, c.y, c.x};
        } else {
            rs.color = rb.color;
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

        // Visibility track (KLAV) gates the whole light. 0 = off. The
        // ancestor-bone gate additionally hides the light if its parent
        // bone chain is invisible, matching Previewd's DFS subtree skip.
        const int lightNodeIdx = nodeOf(L.node);
        const float visibility = evalF32(L.visibilityTracks, 1.0f) *
                                 gateByBoneAncestors(lightNodeIdx);
        // Strict > 0 matches Previewd's SetLightValues @0x14052f5e0 which
        // gates EnvApply/EnvUnApply on `isVisible > 0.0`.
        ls.enabled = visibility > 0.0f;
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
        Vector3f color = L.color;
        if (L.colorTracks.isUsed) {
            Vector3f animated = evalVec3(L.colorTracks, L.color);
            color = {animated.z, animated.y, animated.x}; // BGR -> RGB
        }
        float inten = std::max(0.0f, evalF32(L.intensityTracks,        L.intensity));
        float ambI  = std::max(0.0f, evalF32(L.ambientIntensityTracks, L.ambientIntensity));
        ls.diffuse = { color.x * inten, color.y * inten, color.z * inten };
        ls.ambient = { ambI, ambI, ambI };

        // World transform of the light's node. The hierarchy's local matrix
        // encodes TRS around the pivot as Translate(-pivot)*S*R*Translate(pivot+t),
        // so transforming {0,0,0} through it collapses to zero for identity
        // TRS -- you have to feed in the PIVOT to recover the node's animated
        // world position. `worldOf` handles that by premultiplying Translate(pivot).
        // Lift into scene world space — light shader CBs expect world-space
        // positions/directions, and bone matrices stay model-space (paired
        // with frame.world=mi->worldTransform), so for nested actors we have
        // to fold worldTransform in here. No-op when worldTransform=identity.
        Matrix44f world = worldOf(lightNodeIdx) * worldTransform;
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
        const int nodeIdx = nodeOf(model_.collisionShapes[i].node);
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

        Vector3f   trans = evalVec3(ta.translationTracks, {0, 0, 0});
        Vector3f   scale = evalVec3(ta.scalingTracks,     {1, 1, 1});
        Quaternion rot   = evalQuat(ta.rotationTracks,    Quaternion(0, 0, 0, 1));

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
// GetEventObjects
// ============================================================================
// MDX EventObject names follow `XXX-IIII` where the 4-letter prefix
// (chars 0..2) drives dispatch and chars 4..7 are the SLK row id.
// We decode here so the renderer doesn't have to re-parse the name on
// every fire. Unknown prefixes are still surfaced so the renderer can
// log them once per model rather than swallowing them silently.

static EventObjectConfig::Kind DecodeEventKind(std::string_view name) {
    if (name.size() < 3) return EventObjectConfig::Kind::Unknown;
    auto eq = [&](const char* p) {
        return name[0] == p[0] && name[1] == p[1] && name[2] == p[2];
    };
    if (eq("SPN")) return EventObjectConfig::Kind::SPN;
    if (eq("SPL")) return EventObjectConfig::Kind::SPL;
    if (eq("UBR")) return EventObjectConfig::Kind::UBR;
    if (eq("FPT")) return EventObjectConfig::Kind::FPT;
    if (eq("SND")) return EventObjectConfig::Kind::SND;
    return EventObjectConfig::Kind::Unknown;
}

std::vector<EventObjectConfig> MdxModelAdapter::GetEventObjects() {
    std::vector<EventObjectConfig> result;
    result.reserve(model_.eventObjects.size());
    for (const auto& ev : model_.eventObjects) {
        EventObjectConfig cfg;
        cfg.name = ev.node.name;
        cfg.kind = DecodeEventKind(cfg.name);
        // Skip leading prefix + dash; the SLK id is the remainder. MDX
        // name fields are fixed 80 bytes but typically null-trimmed by
        // the parser, so just slice from char 4 onward when long enough.
        if (cfg.name.size() >= 4) {
            std::string_view tail{cfg.name.data() + 4, cfg.name.size() - 4};
            // Trim trailing nulls / spaces that some authoring tools leave.
            while (!tail.empty() && (tail.back() == '\0' || tail.back() == ' ')) tail.remove_suffix(1);
            cfg.id.assign(tail);
        }
        cfg.nodeIndex        = hierarchy_.ObjectIdToNodeIndex((int)ev.node.objectId);
        if (ev.node.objectId < model_.pivotPoints.size())
            cfg.pivot = model_.pivotPoints[ev.node.objectId];
        cfg.globalSequenceId = ev.globalSequenceId;
        cfg.eventTrackTimes  = ev.eventTrackTimes;
        result.push_back(std::move(cfg));
    }
    return result;
}

// ============================================================================
// GetGlobalSequences
// ============================================================================

std::vector<uint32_t> MdxModelAdapter::GetGlobalSequences() {
    return std::vector<uint32_t>(model_.globalSequences.begin(),
                                 model_.globalSequences.end());
}

// ============================================================================
// GetSequences
// ============================================================================

std::vector<SequenceInfo> MdxModelAdapter::GetSequences() const {
    std::vector<SequenceInfo> result;
    result.reserve(model_.sequences.size());
    for (const auto& seq : model_.sequences) {
        result.push_back({seq.name, (int)seq.intervalStart, (int)seq.intervalEnd,
                          seq.moveSpeed});
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
