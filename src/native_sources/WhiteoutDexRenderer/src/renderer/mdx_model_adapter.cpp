// ============================================================================
// MDX Model Adapter — Translates WhiteoutLib MDX types to IModelSource.
// ============================================================================

#include "mdx_model_adapter.h"
#include "team_glow_data.h"
#include <cmath>
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
// Constructor
// ============================================================================

MdxModelAdapter::MdxModelAdapter(whiteout::mdx::Model model, fs::path basePath)
    : model_(std::move(model))
    , basePath_(std::move(basePath)) {
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
            mesh.positions[v] = {gs.vertexPositions[v].x,
                                 gs.vertexPositions[v].y,
                                 gs.vertexPositions[v].z};
            if (v < (int)gs.vertexNormals.size())
                mesh.normals[v] = {gs.vertexNormals[v].x,
                                   gs.vertexNormals[v].y,
                                   gs.vertexNormals[v].z};
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

    fs::path texPath = basePath_ / path;

    // Try BLP first, then DDS, TGA, PNG with different extensions
    auto tryParse = [&](const fs::path& p) -> bool {
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

        if (result) {
            if (result->format() != whiteout::textures::PixelFormat::RGBA8)
                result->format(whiteout::textures::PixelFormat::RGBA8);
            auto pixels = result->mipData(0);
            td.width  = (int)result->width();
            td.height = (int)result->height();
            td.rgba.assign(pixels.begin(), pixels.end());
            return true;
        }
        return false;
    };

    // Try original path
    if (fs::exists(texPath) && tryParse(texPath))
        return td;

    // Try swapping extension: .blp <-> .dds <-> .tga <-> .png
    static const char* exts[] = {".blp", ".dds", ".tga", ".png"};
    for (auto* e : exts) {
        fs::path alt = texPath;
        alt.replace_extension(e);
        if (fs::exists(alt) && tryParse(alt))
            return td;
    }

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
        if (tex.replaceableId == 1 || tex.replaceableId == 2) {
            result.push_back(GenerateTeamColorTexture(i, (int)tex.replaceableId));
        } else if (!tex.fileName.empty()) {
            result.push_back(LoadTextureFile(tex.fileName, i, (int)tex.replaceableId));
        } else {
            // Empty texture → 4x4 white
            TextureData td;
            td.textureId     = i;
            td.replaceableId = (int)tex.replaceableId;
            td.width = td.height = 4;
            td.rgba.assign(4 * 4 * 4, 255);
            result.push_back(std::move(td));
        }
    }
    return result;
}

// ============================================================================
// GetMaterials — Map Layer FilterMode + ShadingFlags to renderer types
// ============================================================================

int MdxModelAdapter::MapLayerFilterMode(Layer::FilterMode fm) const {
    // Layer::FilterMode maps 1:1 to renderer FilterMode for values 0-6
    int v = (int)fm;
    if (v >= 0 && v <= 6) return v;
    return FILTER_NONE;
}

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

        for (const auto& layer : mat.layers) {
            MaterialLayerData ld;
            ld.filterMode = MapLayerFilterMode(layer.filterMode);
            ld.textureId  = (int)layer.textureId;
            ld.alpha      = layer.alpha;
            ld.flags      = MapShadingFlags(layer.shadingFlags);
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
    sk.boneCount = hierarchy_.BoneCount();
    sk.nodeCount = hierarchy_.NodeCount();

    if (model_.version >= 1200 && !model_.bindPoses.empty()) {
        // Reforged: use bindPoses directly (3x4 → 4x4 → invert)
        sk.inverseBindMatrices.resize(sk.boneCount);
        for (int i = 0; i < sk.boneCount && i < (int)model_.bindPoses.size(); i++) {
            XMMATRIX bp = BindPose3x4ToXMMatrix(model_.bindPoses[i]);
            XMVECTOR det;
            sk.inverseBindMatrices[i] = XMMatrixInverse(&det, bp);
        }
    } else {
        // Classic v800: evaluate hierarchy at rest pose (frame 0 of first seq)
        int restStart = 0, restEnd = 0;
        if (!model_.sequences.empty()) {
            restStart = (int)model_.sequences[0].intervalStart;
            restEnd   = (int)model_.sequences[0].intervalEnd;
        }
        std::vector<XMMATRIX> boneWorld, allNodes;
        hierarchy_.Evaluate(restStart, restStart, restEnd,
                            model_.globalSequences, boneWorld, allNodes);

        sk.inverseBindMatrices.resize(sk.boneCount);
        for (int i = 0; i < sk.boneCount; i++) {
            XMVECTOR det;
            sk.inverseBindMatrices[i] = XMMatrixInverse(&det, boneWorld[i]);
        }
    }
    return sk;
}

// ============================================================================
// GetSkinWeights — v1200 skinData or v800 vertexGroups indirection
// ============================================================================

std::vector<SkinWeightData> MdxModelAdapter::GetSkinWeights() {
    std::vector<SkinWeightData> result;
    result.reserve(model_.geosets.size());

    for (int gi = 0; gi < (int)model_.geosets.size(); gi++) {
        const auto& gs = model_.geosets[gi];
        int vc = (int)gs.vertexPositions.size();
        SkinWeightData sw;
        sw.geosetId = gi;
        sw.influences.resize(vc);

        if (!gs.skinData.empty()) {
            // v1200: packed u8: 4 bone indices + 4 weights per vertex (8 bytes each)
            for (int v = 0; v < vc; v++) {
                int base = v * 8;
                if (base + 7 < (int)gs.skinData.size()) {
                    for (int k = 0; k < 4; k++) {
                        sw.influences[v].boneIdx[k] = (int)gs.skinData[base + k];
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
                        sw.influences[v].boneIdx[k] = (int)gs.matrixIndices[start + k];
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

        // 3-segment color (MDX stores as Vector3f, need to assign RGB)
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

        // Resolve texture through material
        if (rb.materialId < (u32)model_.materials.size() &&
            !model_.materials[rb.materialId].layers.empty()) {
            const auto& layer = model_.materials[rb.materialId].layers[0];
            cfg.textureId  = (int)layer.textureId;
            cfg.filterMode = MapLayerFilterMode(layer.filterMode);
        }

        cfg.rows     = (int)rb.rows;
        cfg.cols     = (int)rb.columns;
        cfg.emission = (float)rb.emissionRate;
        cfg.life     = rb.lifespan;
        cfg.gravity  = rb.gravity;

        // Flags from node
        u32 nf = (u32)rb.node.flags;
        cfg.unshaded = (nf & (u32)Node::NodeFlag::Unshaded) != 0;
        cfg.twoSided = true; // ribbons are typically two-sided

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

FrameState MdxModelAdapter::Evaluate(int timeMs) {
    FrameState fs;

    // Evaluate bone hierarchy (pass camera position for billboard nodes)
    std::vector<XMMATRIX> boneWorld, allNodes;
    hierarchy_.Evaluate(timeMs, seqStart_, seqEnd_,
                        model_.globalSequences, boneWorld, allNodes,
                        &cameraPos_);
    fs.boneWorldMatrices = std::move(boneWorld);

    // Helper: compute effective time for a track, handling global sequences.
    // If the track uses a globalSequenceId, wraps timeMs by the global sequence duration.
    auto effectiveTime = [&](u32 gsId) -> std::tuple<int, int, int> {
        if (gsId != 0xFFFFFFFF && gsId < (u32)model_.globalSequences.size()) {
            u32 duration = model_.globalSequences[gsId];
            if (duration > 0) {
                int t = (int)std::fmod((float)timeMs, (float)duration);
                return {t, 0, (int)duration};
            }
        }
        return {timeMs, seqStart_, seqEnd_};
    };

    // GeosetAnimation evaluation
    int geosetCount = (int)model_.geosets.size();
    fs.geosetAlphas.assign(geosetCount, 1.0f);
    fs.geosetColors.assign(geosetCount, XMFLOAT3(1, 1, 1));

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
            fs.geosetColors[gid] = {ga.color.z, ga.color.y, ga.color.x};
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

    // Particle emitter per-frame state
    fs.particleStates.resize(model_.particleEmitters2.size());
    const auto& nodes = hierarchy_.Nodes();

    for (int i = 0; i < (int)model_.particleEmitters2.size(); i++) {
        const auto& pe = model_.particleEmitters2[i];
        auto& ps = fs.particleStates[i];
        ps.emitterId = i;

        // Find node world matrix
        int nodeIdx = hierarchy_.ObjectIdToNodeIndex((int)pe.node.objectId);
        ps.transform = (nodeIdx >= 0 && nodeIdx < (int)allNodes.size())
                        ? allNodes[nodeIdx] : XMMatrixIdentity();

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

    // Ribbon emitter per-frame state
    fs.ribbonStates.resize(model_.ribbonEmitters.size());

    for (int i = 0; i < (int)model_.ribbonEmitters.size(); i++) {
        const auto& rb = model_.ribbonEmitters[i];
        auto& rs = fs.ribbonStates[i];
        rs.emitterId = i;

        int nodeIdx = hierarchy_.ObjectIdToNodeIndex((int)rb.node.objectId);
        rs.transform = (nodeIdx >= 0 && nodeIdx < (int)allNodes.size())
                        ? allNodes[nodeIdx] : XMMatrixIdentity();

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

        if (rb.colorTracks.isUsed) {
            auto [t,s,e] = effectiveTime(rb.colorTracks.globalSequenceId);
            Vector3f c = EvaluateTrackVec3(rb.colorTracks, t, s, e, rb.color);
            rs.color = {c.x, c.y, c.z};
        } else {
            rs.color = {rb.color.x, rb.color.y, rb.color.z};
        }
    }

    // Collision shape transforms
    fs.collisionTransforms.resize(model_.collisionShapes.size());
    for (int i = 0; i < (int)model_.collisionShapes.size(); i++) {
        const auto& cs = model_.collisionShapes[i];
        int nodeIdx = hierarchy_.ObjectIdToNodeIndex((int)cs.node.objectId);
        fs.collisionTransforms[i] = (nodeIdx >= 0 && nodeIdx < (int)allNodes.size())
                                     ? allNodes[nodeIdx] : XMMatrixIdentity();
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
