// MDLXImporter — Wc3 scene builders implementation
//
// Phase 4: Full paramblock setup for all Wc3/Blizz plugin types.
// Each builder creates plugin instances and configures their IParamBlock2
// parameters from IR data. Animation key insertion on paramblock params
// is deferred to Phase 5 (parameter animation pipeline).

#include "wc3_scene_builders.h"
#include "texture_resolver.h"
#include "../mdlx_class_ids.h"

#include <scene/paramblock_reader.h>
#include <notetrck.h>
#include <bitmap.h>
#include <gencam.h>
#include <ilayer.h>
#include <ilayermanager.h>
#include <modstack.h>
#include <maxscript/maxscript.h>

// ── Helpers ────────────────────────────────────────────────

namespace {

using PBR = core::ParamBlockReader;

// Paramblock write helpers for scripted plugins (name-based lookup)

struct PBParam {
    IParamBlock2* pb = nullptr;
    ParamID id = -1;
    explicit operator bool() const { return pb != nullptr; }
};

PBParam findParam(ReferenceTarget* target, const wchar_t* name) {
    if (!target) return {};
    for (int i = 0; i < target->NumRefs(); i++) {
        auto* ref = target->GetReference(i);
        auto* pb = dynamic_cast<IParamBlock2*>(ref);
        if (!pb) continue;
        auto* desc = pb->GetDesc();
        if (!desc) continue;
        for (int j = 0; j < desc->Count(); j++) {
            ParamID pid = desc->IndextoID(j);
            const ParamDef& pd = desc->GetParamDef(pid);
            if (pd.int_name && _wcsicmp(pd.int_name, name) == 0)
                return { pb, pid };
        }
    }
    return {};
}

void pbSetInt(ReferenceTarget* t, const wchar_t* n, int v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}
void pbSetFloat(ReferenceTarget* t, const wchar_t* n, float v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}
void pbSetBool(ReferenceTarget* t, const wchar_t* n, BOOL v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}
void pbSetColor(ReferenceTarget* t, const wchar_t* n, Color v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}
void pbSetString(ReferenceTarget* t, const wchar_t* n, const MCHAR* v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}

std::wstring toWstr(const std::string& u8) {
    if (u8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring r(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, r.data(), len);
    return r;
}

// Position a node at its pivot point before parenting.
// MaxScript: pos:obj.pivot — every scene object must be placed
// at its MDX pivot point so AttachChild(keepPos=1) computes correct local offset.
void positionAtPivot(INode* node, int32_t nodeIndex,
                     const ir::IRModel& irModel) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(irModel.nodes.size()))
        return;
    Matrix3 tm;
    tm.IdentityMatrix();
    tm.SetTrans(irModel.nodes[nodeIndex].pivotPoint);
    node->SetNodeTM(0, tm);
}

// Walk up the parent chain until we find one that exists in nodeMap,
// matching MaxScript relinkObjects behavior.
void attachToParent(INode* node, int32_t nodeIndex,
                    const ir::IRModel& irModel,
                    const std::vector<INode*>& nodeMap) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(irModel.nodes.size()))
        return;
    int32_t parentIdx = irModel.nodes[nodeIndex].parentIndex;
    while (parentIdx >= 0 && parentIdx < static_cast<int32_t>(nodeMap.size())
           && !nodeMap[parentIdx]) {
        parentIdx = irModel.nodes[parentIdx].parentIndex;
    }
    if (parentIdx >= 0 && parentIdx < static_cast<int32_t>(nodeMap.size()) && nodeMap[parentIdx])
        nodeMap[parentIdx]->AttachChild(node);
}

// Apply MDX node properties: billboard flags, CameraAnchored (user properties),
// and DontInherit flags (Max inheritance flags).
// Matches MaxScript setupNode() behavior.
void setupNodeProperties(INode* node, int32_t nodeIndex,
                         const ir::IRModel& irModel) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(irModel.nodes.size()))
        return;
    uint32_t flags = irModel.nodes[nodeIndex].nodeFlags;

    // Billboard flags → user properties (matching MaxScript setBillboardFlags)
    node->SetUserPropInt(_T("Billboarded"),      (flags & 0x8)  ? 1 : 0);
    node->SetUserPropInt(_T("BillboardedLockX"), (flags & 0x10) ? 1 : 0);
    node->SetUserPropInt(_T("BillboardedLockY"), (flags & 0x20) ? 1 : 0);
    node->SetUserPropInt(_T("BillboardedLockZ"), (flags & 0x40) ? 1 : 0);

    // CameraAnchored → user property
    node->SetUserPropInt(_T("CameraAnchored"),   (flags & 0x80) ? 1 : 0);

    // DontInherit flags → Max inheritance flags
    if (flags & 0x7) {
        DWORD inheritFlags = INHERIT_ALL;
        if (flags & 0x1)
            inheritFlags &= ~(INHERIT_POS_X | INHERIT_POS_Y | INHERIT_POS_Z);
        if (flags & 0x2)
            inheritFlags &= ~(INHERIT_ROT_X | INHERIT_ROT_Y | INHERIT_ROT_Z);
        if (flags & 0x4)
            inheritFlags &= ~(INHERIT_SCL_X | INHERIT_SCL_Y | INHERIT_SCL_Z);
        Control* tmCtrl = node->GetTMController();
        if (tmCtrl)
            tmCtrl->SetInheritanceFlags(inheritFlags, TRUE);
    }
}

// ParamIDs for native plugins (mirrored from plugin headers)

enum P1Params : ParamID {
    P1_PB_COUNT = 0, P1_PB_SPEED = 1, P1_PB_EMISSION_RATE = 2,
    P1_PB_LIFE = 3, P1_PB_ACCELERATION = 4,
    P1_PB_LATITUDE = 5, P1_PB_LONGITUDE = 6, P1_PB_SCALE = 7,
};

enum P2Params : ParamID {
    PB_COUNT = 0, PB_SPEED = 1, PB_VARIATION = 2, PB_LIFE = 3,
    PB_WIDTH = 4, PB_HEIGHT = 5, PB_INITVEL = 6, PB_ANGLE_Y = 7,
    PB_MIDTIME = 8,
    PB_COLOR_START = 9, PB_COLOR_MID = 10, PB_COLOR_END = 11,
    PB_ALPHA_START = 12, PB_ALPHA_MID = 13, PB_ALPHA_END = 14,
    PB_SCALE_START = 15, PB_SCALE_MID = 16, PB_SCALE_END = 17,
    PB_HEAD_LIFE_START = 18, PB_HEAD_LIFE_REPEAT = 19, PB_HEAD_LIFE_END = 20,
    PB_HEAD_DECAY_START = 21, PB_HEAD_DECAY_REPEAT = 22, PB_HEAD_DECAY_END = 23,
    PB_TAIL_LEN = 24, PB_TYPE = 25, PB_ROWS = 26, PB_COLS = 27,
    PB_TAIL_LIFE_START = 28, PB_TAIL_LIFE_REPEAT = 29, PB_TAIL_LIFE_END = 30,
    PB_TAIL_DECAY_START = 31, PB_TAIL_DECAY_REPEAT = 32, PB_TAIL_DECAY_END = 33,
    PB_SQUIRT = 34, PB_BLEND = 35, PB_GRAVITY = 36,
    PB_SORT = 37, PB_LINE_EMIT = 38, PB_UNSHADED = 39,
    PB_LATITUDE = 40, PB_PRIORITY = 41, PB_UNFOGGED = 42,
    PB_MODELSPACE = 43, PB_XYQUAD = 44, PB_REPLACEABLE_ID = 45,
    PB_LONGITUDE = 46,
};

enum RibbonParams : ParamID {
    pb_height_above = 0, pb_height_below = 1,
    pb_edges_per_second = 2, pb_edge_lifetime = 3,
    pb_tex_rows = 4, pb_tex_cols = 5, pb_tex_slot = 6,
    pb_material = 7, pb_color = 8, pb_alpha = 9, pb_gravity = 10,
};

constexpr ULONG WC3P1_MODEL_PATH_IID = 0x7B3C8D01;
constexpr ULONG WC3P2_TEXTURE_PATH_IID   = 0x7B3C8D10;
constexpr ULONG WC3P2_TEXTURE_PREFIX_IID = 0x7B3C8D11;

} // anonymous namespace

namespace mdx_scene {

// ── Wc3LightBuilder ────────────────────────────────────────

void Wc3LightBuilder::buildLights(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& irLight : irModel.lights) {
        Object* obj = static_cast<Object*>(
            gi->CreateInstance(HELPER_CLASS_ID, mdx_ids::WC3_LIGHT));
        if (!obj) {
            reporter.warning(L"Wc3_Light scripted plugin not found — skipping light");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        MSTR name;
        if (irLight.nodeIndex >= 0 && irLight.nodeIndex < static_cast<int32_t>(irModel.nodes.size()))
            name.printf(_T("%hs"), irModel.nodes[irLight.nodeIndex].name.c_str());
        else
            name = _T("Wc3Light");
        node->SetName(name);

        // Set parent (walk chain like MaxScript relinkObjects)
        positionAtPivot(node, irLight.nodeIndex, irModel);
        setupNodeProperties(node, irLight.nodeIndex, irModel);
        attachToParent(node, irLight.nodeIndex, irModel, nodeMap);

        // Paramblock setup
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (ref) {
            int lightType = 1; // Default Omni
            switch (irLight.type) {
            case ir::Light::Type::Omni:        lightType = 1; break;
            case ir::Light::Type::Directional: lightType = 2; break;
            case ir::Light::Type::Ambient:     lightType = 3; break;
            }
            pbSetInt(ref, L"LightType", lightType);
            pbSetFloat(ref, L"DecayStart", irLight.attenuationStart);
            pbSetFloat(ref, L"DecayEnd", irLight.attenuationEnd);
            // Wc3_Light uses ShadowColor/ShadowValue for main light color/intensity
            pbSetColor(ref, L"ShadowColor", irLight.color);
            pbSetFloat(ref, L"ShadowValue", irLight.intensity);
            pbSetColor(ref, L"AmbColor", irLight.ambientColor);
            pbSetFloat(ref, L"AmbValue", irLight.ambientIntensity);
        }

        // Register for animation key insertion
        if (irLight.nodeIndex >= 0 && irLight.nodeIndex < static_cast<int32_t>(nodeMap.size()))
            nodeMap[irLight.nodeIndex] = node;
    }
}

// ── Wc3AttachmentBuilder ────────────────────────────────────

void Wc3AttachmentBuilder::buildAttachments(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& irAtt : irModel.attachments) {
        if (irAtt.attachmentId == -1) continue; // Skip FaceFX placeholders

        Object* obj = static_cast<Object*>(
            gi->CreateInstance(HELPER_CLASS_ID, mdx_ids::WC3_ATTACH_POINT));
        if (!obj) {
            reporter.warning(L"Wc3_AttachPoint scripted plugin not found — skipping attachment");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        MSTR name;
        name.printf(_T("%hs"), irAtt.name.c_str());
        node->SetName(name);

        positionAtPivot(node, irAtt.nodeIndex, irModel);
        setupNodeProperties(node, irAtt.nodeIndex, irModel);
        attachToParent(node, irAtt.nodeIndex, irModel, nodeMap);

        // Paramblock setup
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (ref) {
            pbSetInt(ref, L"attachmentId", irAtt.attachmentId);
            if (!irAtt.path.empty()) {
                auto wpath = toWstr(irAtt.path);
                pbSetBool(ref, L"usesExternalModel", TRUE);
                pbSetString(ref, L"externalModelPath", wpath.c_str());
            }
            if (irAtt.attachmentId >= 0) {
                pbSetBool(ref, L"usesAttachmentId", TRUE);
            }
        }

        // Register for animation key insertion
        if (irAtt.nodeIndex >= 0 && irAtt.nodeIndex < static_cast<int32_t>(nodeMap.size()))
            nodeMap[irAtt.nodeIndex] = node;
    }
}

// ── Wc3Particle1Builder ─────────────────────────────────────

void Wc3Particle1Builder::buildParticles(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& irPE : irModel.particleEmitters) {
        if (irPE.variant != 1) continue;

        Object* obj = static_cast<Object*>(
            gi->CreateInstance(GEOMOBJECT_CLASS_ID, mdx_ids::WC3_PARTICLES1));
        if (!obj) {
            reporter.warning(L"Wc3Particles1 plugin not found — skipping PE1");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        MSTR name;
        if (irPE.nodeIndex >= 0 && irPE.nodeIndex < static_cast<int32_t>(irModel.nodes.size()))
            name.printf(_T("%hs"), irModel.nodes[irPE.nodeIndex].name.c_str());
        else
            name = _T("Wc3PE1");
        node->SetName(name);

        positionAtPivot(node, irPE.nodeIndex, irModel);
        setupNodeProperties(node, irPE.nodeIndex, irModel);
        attachToParent(node, irPE.nodeIndex, irModel, nodeMap);

        // Paramblock setup
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        IParamBlock2* pb = ref ? PBR::findParamBlock(ref, 0) : nullptr;
        if (pb) {
            pb->SetValue(P1_PB_SPEED, 0, irPE.speed);
            pb->SetValue(P1_PB_EMISSION_RATE, 0, irPE.emissionRate);
            pb->SetValue(P1_PB_LIFE, 0, irPE.lifespan);
            pb->SetValue(P1_PB_ACCELERATION, 0, irPE.gravity);
            pb->SetValue(P1_PB_LATITUDE, 0, irPE.latitude);
            pb->SetValue(P1_PB_LONGITUDE, 0, irPE.longitude);
        }

        // Model path via custom interface
        if (!irPE.modelPath.empty()) {
            auto* pathPtr = static_cast<MSTR*>(obj->GetInterface(WC3P1_MODEL_PATH_IID));
            if (pathPtr) {
                auto wpath = toWstr(irPE.modelPath);
                *pathPtr = wpath.c_str();
            }
        }

        // Register for animation key insertion
        if (irPE.nodeIndex >= 0 && irPE.nodeIndex < static_cast<int32_t>(nodeMap.size()))
            nodeMap[irPE.nodeIndex] = node;
    }
}

// ── Wc3Particle2Builder ─────────────────────────────

void Wc3Particle2Builder::buildParticles(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    const std::wstring& modelDir, void* cascStorage,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& irPE : irModel.particleEmitters) {
        if (irPE.variant != 2) continue;

        Object* obj = static_cast<Object*>(
            gi->CreateInstance(GEOMOBJECT_CLASS_ID, mdx_ids::WC3_PARTICLES2));
        if (!obj) {
            reporter.warning(L"Wc3Particles2 plugin not found — skipping PE2");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        MSTR name;
        if (irPE.nodeIndex >= 0 && irPE.nodeIndex < static_cast<int32_t>(irModel.nodes.size()))
            name.printf(_T("%hs"), irModel.nodes[irPE.nodeIndex].name.c_str());
        else
            name = _T("Wc3PE2");
        node->SetName(name);

        positionAtPivot(node, irPE.nodeIndex, irModel);
        setupNodeProperties(node, irPE.nodeIndex, irModel);
        attachToParent(node, irPE.nodeIndex, irModel, nodeMap);

        // Paramblock setup — all 47 PE2 parameters
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        IParamBlock2* pb = ref ? PBR::findParamBlock(ref, 0) : nullptr;
        if (pb) {
            pb->SetValue(PB_SPEED, 0, irPE.speed);
            pb->SetValue(PB_VARIATION, 0, irPE.variation);
            pb->SetValue(PB_INITVEL, 0, irPE.emissionRate);
            pb->SetValue(PB_LIFE, 0, irPE.lifespan);
            pb->SetValue(PB_GRAVITY, 0, irPE.gravity);
            pb->SetValue(PB_LATITUDE, 0, static_cast<int>(irPE.latitude));
            pb->SetValue(PB_LONGITUDE, 0, irPE.longitude);
            pb->SetValue(PB_WIDTH, 0, irPE.width);
            pb->SetValue(PB_HEIGHT, 0, irPE.length);
            pb->SetValue(PB_TAIL_LEN, 0, irPE.tailLength);
            pb->SetValue(PB_MIDTIME, 0, irPE.midTime);

            pb->SetValue(PB_BLEND, 0, irPE.filterMode);
            pb->SetValue(PB_ROWS, 0, irPE.rows);
            pb->SetValue(PB_COLS, 0, irPE.columns);
            pb->SetValue(PB_TYPE, 0, irPE.headOrTail);
            pb->SetValue(PB_REPLACEABLE_ID, 0, irPE.replaceableId);
            pb->SetValue(PB_PRIORITY, 0, irPE.priorityPlane);

            // Segment colors (stored as Point3 in PE2)
            Point3 cStart(irPE.segmentColors[0].r, irPE.segmentColors[0].g, irPE.segmentColors[0].b);
            Point3 cMid(irPE.segmentColors[1].r, irPE.segmentColors[1].g, irPE.segmentColors[1].b);
            Point3 cEnd(irPE.segmentColors[2].r, irPE.segmentColors[2].g, irPE.segmentColors[2].b);
            pb->SetValue(PB_COLOR_START, 0, cStart);
            pb->SetValue(PB_COLOR_MID, 0, cMid);
            pb->SetValue(PB_COLOR_END, 0, cEnd);

            // Segment alpha (0-1 float → 0-255 int)
            pb->SetValue(PB_ALPHA_START, 0, static_cast<int>(irPE.segmentAlpha[0] * 255.0f));
            pb->SetValue(PB_ALPHA_MID, 0, static_cast<int>(irPE.segmentAlpha[1] * 255.0f));
            pb->SetValue(PB_ALPHA_END, 0, static_cast<int>(irPE.segmentAlpha[2] * 255.0f));

            // Segment scale
            pb->SetValue(PB_SCALE_START, 0, irPE.segmentScale[0]);
            pb->SetValue(PB_SCALE_MID, 0, irPE.segmentScale[1]);
            pb->SetValue(PB_SCALE_END, 0, irPE.segmentScale[2]);

            // Head/tail UV animation intervals
            pb->SetValue(PB_HEAD_LIFE_START, 0, irPE.headInterval[0]);
            pb->SetValue(PB_HEAD_LIFE_REPEAT, 0, irPE.headInterval[1]);
            pb->SetValue(PB_HEAD_LIFE_END, 0, irPE.headInterval[2]);
            pb->SetValue(PB_HEAD_DECAY_START, 0, irPE.headDecayInterval[0]);
            pb->SetValue(PB_HEAD_DECAY_REPEAT, 0, irPE.headDecayInterval[1]);
            pb->SetValue(PB_HEAD_DECAY_END, 0, irPE.headDecayInterval[2]);
            pb->SetValue(PB_TAIL_LIFE_START, 0, irPE.tailInterval[0]);
            pb->SetValue(PB_TAIL_LIFE_REPEAT, 0, irPE.tailInterval[1]);
            pb->SetValue(PB_TAIL_LIFE_END, 0, irPE.tailInterval[2]);
            pb->SetValue(PB_TAIL_DECAY_START, 0, irPE.tailDecayInterval[0]);
            pb->SetValue(PB_TAIL_DECAY_REPEAT, 0, irPE.tailDecayInterval[1]);
            pb->SetValue(PB_TAIL_DECAY_END, 0, irPE.tailDecayInterval[2]);

            // Flags → individual bools
            uint32_t f = irPE.flags;
            pb->SetValue(PB_SORT, 0, (f & 0x10000) ? 1 : 0);
            pb->SetValue(PB_UNSHADED, 0, (f & 0x8000) ? 1 : 0);
            pb->SetValue(PB_LINE_EMIT, 0, (f & 0x20000) ? 1 : 0);
            pb->SetValue(PB_UNFOGGED, 0, (f & 0x40000) ? 1 : 0);
            pb->SetValue(PB_MODELSPACE, 0, (f & 0x80000) ? 1 : 0);
            pb->SetValue(PB_XYQUAD, 0, (f & 0x100000) ? 1 : 0);
            pb->SetValue(PB_SQUIRT, 0, (f & 1) ? 1 : 0);
        }

        // Texture path via custom interface.
        //
        // The Wc3Particles2 plugin stores two fields: m_texturePrefix and
        // m_particlePath. The renderer concatenates them with the .max file
        // directory as: basePath + texPrefix + texFile. For a freshly imported
        // file (unsaved .max), basePath is empty, so the natural fallback is
        // to put the absolute resolved path in m_particlePath and leave the
        // prefix empty — the renderer's Try3 fallback then uses texFile as-is.
        // CASC extraction (inside resolveTexturePathFull) will extract the
        // texture to <modelDir>\<relPath> and return that absolute path.
        if (irPE.textureIndex >= 0 &&
            irPE.textureIndex < static_cast<int32_t>(irModel.textures.size()))
        {
            const auto& irTex = irModel.textures[irPE.textureIndex];
            if (!irTex.filePath.empty()) {
                auto wpath = mdx_scene::resolveTexturePathFull(
                    modelDir, toWstr(irTex.filePath), cascStorage);
                auto* pathPtr = static_cast<MSTR*>(obj->GetInterface(WC3P2_TEXTURE_PATH_IID));
                if (pathPtr) *pathPtr = wpath.c_str();
                auto* prefixPtr = static_cast<MSTR*>(obj->GetInterface(WC3P2_TEXTURE_PREFIX_IID));
                if (prefixPtr) *prefixPtr = _T("");
            }
        }

        // Register for animation key insertion
        if (irPE.nodeIndex >= 0 && irPE.nodeIndex < static_cast<int32_t>(nodeMap.size()))
            nodeMap[irPE.nodeIndex] = node;
    }
}

// ── Wc3RibbonBuilder ───────────────────────────────

void Wc3RibbonBuilder::buildRibbons(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    const std::vector<Mtl*>& materials, Interface* gi,
    core::ExportErrorReporter& reporter)
{
    for (const auto& irRib : irModel.ribbonEmitters) {
        Object* obj = static_cast<Object*>(
            gi->CreateInstance(GEOMOBJECT_CLASS_ID, mdx_ids::WC3_RIBBON));
        if (!obj) {
            reporter.warning(L"Wc3Ribbon plugin not found — skipping ribbon");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        MSTR name;
        if (irRib.nodeIndex >= 0 && irRib.nodeIndex < static_cast<int32_t>(irModel.nodes.size()))
            name.printf(_T("%hs"), irModel.nodes[irRib.nodeIndex].name.c_str());
        else
            name = _T("Wc3Ribbon");
        node->SetName(name);

        // Assign material
        if (irRib.materialIndex >= 0 && irRib.materialIndex < static_cast<int32_t>(materials.size()))
            node->SetMtl(materials[irRib.materialIndex]);

        positionAtPivot(node, irRib.nodeIndex, irModel);
        setupNodeProperties(node, irRib.nodeIndex, irModel);
        attachToParent(node, irRib.nodeIndex, irModel, nodeMap);

        // Paramblock setup
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        IParamBlock2* pb = ref ? PBR::findParamBlock(ref, 0) : nullptr;
        if (pb) {
            pb->SetValue(pb_height_above, 0, irRib.heightAbove);
            pb->SetValue(pb_height_below, 0, irRib.heightBelow);
            pb->SetValue(pb_edge_lifetime, 0, irRib.lifespan);
            pb->SetValue(pb_gravity, 0, irRib.gravity);
            pb->SetValue(pb_edges_per_second, 0, irRib.emissionRate);
            pb->SetValue(pb_tex_rows, 0, irRib.rows);
            pb->SetValue(pb_tex_cols, 0, irRib.columns);
            pb->SetValue(pb_tex_slot, 0, irRib.textureSlot);
            pb->SetValue(pb_alpha, 0, irRib.alpha);

            Point3 col(irRib.color.r, irRib.color.g, irRib.color.b);
            pb->SetValue(pb_color, 0, col);

            // Material reference
            if (irRib.materialIndex >= 0 && irRib.materialIndex < static_cast<int32_t>(materials.size()))
                pb->SetValue(pb_material, 0, materials[irRib.materialIndex]);
        }

        // Register for animation key insertion
        if (irRib.nodeIndex >= 0 && irRib.nodeIndex < static_cast<int32_t>(nodeMap.size()))
            nodeMap[irRib.nodeIndex] = node;
    }
}

// ── Wc3EventBuilder ────────────────────────────────────────

void Wc3EventBuilder::buildEvents(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& irEvt : irModel.eventObjects) {
        Object* obj = static_cast<Object*>(
            gi->CreateInstance(HELPER_CLASS_ID, mdx_ids::WC3_EVENT_V2021));
        if (!obj) {
            // Try older version
            obj = static_cast<Object*>(
                gi->CreateInstance(HELPER_CLASS_ID, mdx_ids::WC3_EVENT_V2020));
        }
        if (!obj) {
            reporter.warning(L"Wc3_Event scripted plugin not found — skipping event");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        MSTR name;
        if (irEvt.nodeIndex >= 0 && irEvt.nodeIndex < static_cast<int32_t>(irModel.nodes.size()))
            name.printf(_T("%hs"), irModel.nodes[irEvt.nodeIndex].name.c_str());
        else
            name.printf(_T("%hs%hs"), irEvt.eventCode.c_str(), irEvt.eventData.c_str());
        node->SetName(name);

        positionAtPivot(node, irEvt.nodeIndex, irModel);
        setupNodeProperties(node, irEvt.nodeIndex, irModel);
        attachToParent(node, irEvt.nodeIndex, irModel, nodeMap);

        // Set event key times via IntTab paramblock
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        IParamBlock2* pb = ref ? PBR::findParamBlock(ref, 0) : nullptr;
        if (pb && !irEvt.keyTimes.empty()) {
            pb->SetCount(0, 0); // Clear existing entries
            for (auto t : irEvt.keyTimes) {
                int keyTime = static_cast<int>(t);
                pb->Append(0, 1, &keyTime);
            }
        }

        // Register for animation key insertion
        if (irEvt.nodeIndex >= 0 && irEvt.nodeIndex < static_cast<int32_t>(nodeMap.size()))
            nodeMap[irEvt.nodeIndex] = node;
    }
}

// ── Wc3CollisionBuilder ─────────────────────────────────────

void Wc3CollisionBuilder::buildCollisions(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& irCol : irModel.collisionShapes) {
        Class_ID clsId = (irCol.shape == ir::CollisionShape::Shape::Sphere)
            ? mdx_ids::WC3_COLLISION_SPH : mdx_ids::WC3_COLLISION_BOX;

        Object* obj = static_cast<Object*>(
            gi->CreateInstance(HELPER_CLASS_ID, clsId));
        if (!obj) {
            reporter.warning(L"Wc3Collision scripted plugin not found — skipping collision shape");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        MSTR name;
        if (irCol.nodeIndex >= 0 && irCol.nodeIndex < static_cast<int32_t>(irModel.nodes.size()))
            name.printf(_T("%hs"), irModel.nodes[irCol.nodeIndex].name.c_str());
        else
            name = _T("Wc3Collision");
        node->SetName(name);

        positionAtPivot(node, irCol.nodeIndex, irModel);
        setupNodeProperties(node, irCol.nodeIndex, irModel);
        attachToParent(node, irCol.nodeIndex, irModel, nodeMap);

        // Paramblock setup: dimensions from collision vertices
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (ref) {
            if (irCol.shape == ir::CollisionShape::Shape::Sphere) {
                pbSetFloat(ref, L"radius", irCol.radius);
            } else if (irCol.shape == ir::CollisionShape::Shape::Box && irCol.vertices.size() >= 2) {
                Point3 ext = irCol.vertices[1] - irCol.vertices[0];
                pbSetFloat(ref, L"width", fabsf(ext.x));
                pbSetFloat(ref, L"length", fabsf(ext.y));
                pbSetFloat(ref, L"height", fabsf(ext.z));
            }
        }

        // Register for animation key insertion
        if (irCol.nodeIndex >= 0 && irCol.nodeIndex < static_cast<int32_t>(nodeMap.size()))
            nodeMap[irCol.nodeIndex] = node;
    }
}

// ── Wc3VertexColorBuilder ───────────────────────────────────

void Wc3VertexColorBuilder::applyVertexColors(
    const ir::IRModel& irModel, const std::vector<INode*>& meshNodes,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& ga : irModel.geosetAnims) {
        if (!ga.usesColor && !ga.dropShadow) continue;
        if (ga.meshIndex < 0 || ga.meshIndex >= static_cast<int32_t>(meshNodes.size()))
            continue;
        INode* meshNode = meshNodes[ga.meshIndex];
        if (!meshNode) continue;

        // Create Wc3VertexMod modifier — scripted plugin extends VertexPaint.
        // CreateInstance returns a MSPlugin wrapper; cast via Animatable hierarchy.
        void* rawObj = gi->CreateInstance(OSM_CLASS_ID, mdx_ids::WC3_VERTEX_MOD);
        if (!rawObj) {
            reporter.warning(L"Wc3VertexMod plugin not found");
            break;
        }
        Modifier* mod = static_cast<Modifier*>(rawObj);
        auto* ref = static_cast<ReferenceTarget*>(rawObj);

        pbSetBool(ref, L"UsesDropShadow", ga.dropShadow ? TRUE : FALSE);
        pbSetBool(ref, L"UsesColor", ga.usesColor ? TRUE : FALSE);
        if (ga.usesColor)
            pbSetColor(ref, L"VertexColor", ga.color);

        // Add modifier to mesh node
        Object* objRef = meshNode->GetObjectRef();
        IDerivedObject* dobj = nullptr;
        if (objRef && objRef->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
            dobj = static_cast<IDerivedObject*>(objRef);
        } else {
            dobj = CreateDerivedObject(objRef);
            meshNode->SetObjectRef(dobj);
        }
        dobj->AddModifier(mod);
    }
}

// ── Wc3PopcornBuilder (v1200) ───────────────────────────────

void Wc3PopcornBuilder::buildPopcorn(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& irPE : irModel.particleEmitters) {
        if (irPE.variant != 3) continue; // Corn emitter variant

        Object* obj = static_cast<Object*>(
            gi->CreateInstance(HELPER_CLASS_ID, mdx_ids::BLIZZ_POPCORN));
        if (!obj) {
            reporter.warning(L"BlizzPopcorn scripted plugin not found — skipping corn emitter");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        MSTR name;
        if (irPE.nodeIndex >= 0 && irPE.nodeIndex < static_cast<int32_t>(irModel.nodes.size()))
            name.printf(_T("%hs"), irModel.nodes[irPE.nodeIndex].name.c_str());
        else
            name = _T("BlizzPopcorn");
        node->SetName(name);

        positionAtPivot(node, irPE.nodeIndex, irModel);
        setupNodeProperties(node, irPE.nodeIndex, irModel);
        attachToParent(node, irPE.nodeIndex, irModel, nodeMap);

        // Paramblock setup
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (ref) {
            if (!irPE.modelPath.empty()) {
                auto wpath = toWstr(irPE.modelPath);
                pbSetString(ref, L"popcornPath", wpath.c_str());
            }
            pbSetFloat(ref, L"LifeSpan", irPE.lifespan);
            pbSetFloat(ref, L"EmissionRate", irPE.emissionRate);
            pbSetFloat(ref, L"Speed", irPE.speed);
            pbSetFloat(ref, L"alpha", 1.0f); // Default alpha; animated via colorTrack
            pbSetInt(ref, L"ReplaceableId", irPE.replaceableId);
        }

        // Register for animation key insertion
        if (irPE.nodeIndex >= 0 && irPE.nodeIndex < static_cast<int32_t>(nodeMap.size()))
            nodeMap[irPE.nodeIndex] = node;
    }
}

// ── Wc3FaceFxBuilder (v1200) ────────────────────────────────

void Wc3FaceFxBuilder::buildFaceFX(
    const ir::IRModel& irModel, const std::vector<INode*>& nodeMap,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& irAtt : irModel.attachments) {
        if (irAtt.attachmentId != -1) continue; // Only FaceFX placeholders (marker = -1)

        Object* obj = static_cast<Object*>(
            gi->CreateInstance(HELPER_CLASS_ID, mdx_ids::BLIZZ_FACEFX));
        if (!obj) {
            reporter.warning(L"BlizzFaceFX scripted plugin not found — skipping face effect");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        MSTR name;
        name.printf(_T("%hs"), irAtt.name.c_str());
        node->SetName(name);

        // Paramblock setup: ParamID 0 = facefxName, ParamID 1 = facefxPath
        IParamBlock2* pb = PBR::findParamBlock(
            dynamic_cast<ReferenceTarget*>(obj), 0);
        if (pb) {
            auto wname = toWstr(irAtt.name);
            auto wpath = toWstr(irAtt.path);
            pb->SetValue(0, 0, wname.c_str());
            pb->SetValue(1, 0, wpath.c_str());
        }
    }
}

// ── Wc3CameraBuilder ──────────────────────────────────────

std::vector<Wc3CameraBuilder::CameraNodePair> Wc3CameraBuilder::buildCameras(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    std::vector<CameraNodePair> result;
    if (irModel.cameras.empty()) return result;

    // Collect all created nodes for layer assignment
    std::vector<INode*> allCamNodes;

    for (const auto& irCam : irModel.cameras) {
        // Create camera + target via MaxScript — SDK's LOOKAT_CAM_CLASS_ID + SetTarget
        // doesn't wire the LookAt controller properly; MaxScript Targetcamera does.
        std::wstring wname = toWstr(irCam.name);

        wchar_t script[2048];
        swprintf_s(script, 2048,
            L"(\n"
            L"local tgt = Targetobject transform:(matrix3 [1,0,0] [0,1,0] [0,0,1] [%g,%g,%g])\n"
            L"tgt.name = \"%s_Target\"\n"
            L"local cam = Targetcamera fov:(radToDeg %g) nearclip:%g farclip:%g pos:[%g,%g,%g] target:tgt\n"
            L"cam.name = \"%s\"\n"
            L"true\n"
            L")",
            irCam.targetPosition.x, irCam.targetPosition.y, irCam.targetPosition.z,
            wname.c_str(),
            irCam.fov, irCam.nearClip, irCam.farClip,
            irCam.position.x, irCam.position.y, irCam.position.z,
            wname.c_str());

        ExecuteMAXScriptScript(script,
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
            MAXScript::ScriptSource::NonEmbedded,
#endif
            TRUE, nullptr);

        // Look up created nodes by name
        std::wstring tgtName = wname + L"_Target";
        INode* camNode = gi->GetINodeByName(wname.c_str());
        INode* targetNode = gi->GetINodeByName(tgtName.c_str());

        if (!camNode) {
            reporter.warning(L"Failed to create camera '" + wname + L"'");
            result.push_back({});
            continue;
        }

        result.push_back({ camNode, targetNode });
        allCamNodes.push_back(camNode);
        if (targetNode) allCamNodes.push_back(targetNode);
    }

    // Place all camera nodes in the "Cameras" layer (matching MaxScript behavior)
    if (!allCamNodes.empty()) {
        ILayerManager* lm = GetCOREInterface13()->GetLayerManager();
        if (lm) {
            MSTR layerName(_T("Cameras"));
            ILayer* layer = lm->GetLayer(layerName);
            if (!layer)
                layer = lm->CreateLayer(layerName);
            if (layer) {
                for (INode* n : allCamNodes)
                    layer->AddToLayer(n);
            }
        }
    }

    return result;
}

// ── Wc3SequenceBuilder ─────────────────────────────────────

void Wc3SequenceBuilder::buildSequences(
    const ir::IRModel& irModel, Interface* gi,
    core::ExportErrorReporter& reporter)
{
    if (irModel.sequences.empty()) return;

    // Create a DefNoteTrack on the scene root node for sequence markers
    INode* rootNode = gi->GetRootNode();
    if (!rootNode) return;

    DefNoteTrack* noteTrack = static_cast<DefNoteTrack*>(
        NewDefaultNoteTrack());
    rootNode->AddNoteTrack(noteTrack);

    TimeValue minTime = irModel.sequences[0].startTime;
    TimeValue maxTime = irModel.sequences[0].endTime;

    for (const auto& seq : irModel.sequences) {
        // Add note key at sequence start
        MSTR note;
        note.printf(_T("%hs"), seq.name.c_str());
        NoteKey* startKey = new NoteKey(seq.startTime, note, 0);
        noteTrack->keys.Append(1, &startKey);

        if (seq.startTime < minTime) minTime = seq.startTime;
        if (seq.endTime > maxTime) maxTime = seq.endTime;
    }

    // Set animation range
    Interval range(minTime, maxTime);
    gi->SetAnimRange(range);
}

} // namespace mdx_scene
