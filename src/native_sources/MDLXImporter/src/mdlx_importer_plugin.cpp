// MDLXImporter — MdlxImporterPlugin implementation
#include "mdlx_importer_plugin.h"
#include "mdlx_import_options.h"
#include "mdlx_class_ids.h"
#include "import_dialog.h"

// Disassembly (MDX → IR)
#include "disassembly/mdx_model_disassembler.h"

// Scene builders (IR → Wc3 scripted plugin nodes)
#include "scene/wc3_material_builder.h"
#include "scene/wc3_scene_builders.h"
#include "scene/texture_resolver.h"

// Core (MaxCore)
#include <core/intermediate_types.h>
#include <util/error_reporter.h>
#include <util/class_ids.h>
#include <optimization/vertex_optimizer.h>
#include <optimization/bone_optimizer.h>
#include <optimization/keyframe_optimizer.h>

// WhiteoutLib
#include <whiteout/models/mdx/parser.h>

// Max SDK
#include <MaxDirectories.h>
#include <triobj.h>
#include <modstack.h>
#include <iskin.h>
#include <istdplug.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <fstream>

// Narrow-string helper for Max's wchar_t names
static std::string narrow(const MCHAR* ws) {
    if (!ws) return "";
    std::string s;
    while (*ws) { s += static_cast<char>(*ws > 127 ? '?' : *ws); ++ws; }
    return s;
}

static std::ofstream& importLog() {
    static std::ofstream s_log;
    if (!s_log.is_open()) {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring p(tmp);
        p += L"mdlx_import_debug.log";
        s_log.open(p, std::ios::app);
    }
    return s_log;
}
#define ILOG importLog()

// Forward declarations for INI loading
void loadImportOptionsFromINI(Interface* gi, MdlxImportOptions& opts);
void applyFastPreset(MdlxImportOptions& opts);

// Module handle from dllmain.cpp
extern HINSTANCE hInstance;

int MdlxImporterPlugin::ExtCount() { return 2; }

const TCHAR* MdlxImporterPlugin::Ext(int n) {
    switch (n) {
    case 0: return _T("mdx");
    case 1: return _T("mdl");
    default: return _T("");
    }
}

const TCHAR* MdlxImporterPlugin::LongDesc() { return _T("Warcraft III MDX/MDL Model"); }
const TCHAR* MdlxImporterPlugin::ShortDesc() { return _T("MDX/MDL Import"); }
const TCHAR* MdlxImporterPlugin::AuthorName() { return _T("WhiteoutDex"); }
const TCHAR* MdlxImporterPlugin::CopyrightMessage() { return _T(""); }
const TCHAR* MdlxImporterPlugin::OtherMessage1() { return _T(""); }
const TCHAR* MdlxImporterPlugin::OtherMessage2() { return _T(""); }
unsigned int MdlxImporterPlugin::Version() { return 100; }
void MdlxImporterPlugin::ShowAbout(HWND /*hWnd*/) {}

namespace {

std::string wcharToUtf8(const wchar_t* wstr) {
    if (!wstr || !wstr[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr, nullptr);
    return result;
}

// ── Bone/Helper creation ────────────────────────────────────

INode* createBoneNode(const ir::Bone& bone, Interface* gi, bool /*asPointHelper*/) {
    // Always use Point helpers instead of BoneGeometry.
    // Max bones auto-orient along their length axis and require special
    // mode (BoneSys) to position freely — Point helpers behave like plain
    // transform nodes and match MaxScript's `point pos:pivot` path.
    Object* obj = static_cast<Object*>(
        gi->CreateInstance(HELPER_CLASS_ID, core_ids::POINT_HELPER_ID));

    if (!obj) return nullptr;

    INode* node = gi->CreateObjectNode(obj);
    MSTR name;
    name.printf(_T("%hs"), bone.name.c_str());
    node->SetName(name);

    return node;
}

// ── Mesh creation ───────────────────────────────────────────

INode* createMeshNode(const ir::Mesh& irMesh, Interface* gi) {
    TriObject* triObj = CreateNewTriObject();
    if (!triObj) return nullptr;

    Mesh& mesh = triObj->GetMesh();

    int vertCount = static_cast<int>(irMesh.vertices.size());
    if (vertCount == 0) {
        triObj->DeleteMe();
        return nullptr;
    }

    // Truncate to whole triangles
    int faceCount = static_cast<int>(irMesh.indices.size() / 3);

    mesh.setNumVerts(vertCount);
    mesh.setNumFaces(faceCount);

    // Vertices
    for (int i = 0; i < vertCount; ++i) {
        mesh.setVert(i, irMesh.vertices[i].position);
    }

    // Faces — validate each index is in range
    int validFaces = 0;
    for (int f = 0; f < faceCount; ++f) {
        int base = f * 3;
        uint32_t i0 = irMesh.indices[base];
        uint32_t i1 = irMesh.indices[base + 1];
        uint32_t i2 = irMesh.indices[base + 2];
        if (i0 >= static_cast<uint32_t>(vertCount) ||
            i1 >= static_cast<uint32_t>(vertCount) ||
            i2 >= static_cast<uint32_t>(vertCount))
            continue;  // skip degenerate face

        mesh.faces[validFaces].setVerts(
            static_cast<int>(i0), static_cast<int>(i1), static_cast<int>(i2));
        mesh.faces[validFaces].setEdgeVisFlags(1, 1, 1);
        mesh.faces[validFaces].setSmGroup(1);
        if (irMesh.materialIndex >= 0)
            mesh.faces[validFaces].setMatID(0);
        ++validFaces;
    }
    if (validFaces < faceCount)
        mesh.setNumFaces(validFaces);
    faceCount = validFaces;

    if (faceCount == 0) {
        triObj->DeleteMe();
        return nullptr;
    }

    // UV mapping (channel 1)
    if (irMesh.vertices[0].uvSetCount > 0) {
        mesh.setMapSupport(1, TRUE);
        mesh.setNumMapVerts(1, vertCount);
        mesh.setNumMapFaces(1, faceCount);

        for (int i = 0; i < vertCount; ++i) {
            const auto& uv = irMesh.vertices[i].uvSets[0];
            mesh.setMapVert(1, i, Point3(uv.x, 1.0f - uv.y, 0.0f));
        }

        TVFace* mapFaces1 = mesh.mapFaces(1);
        if (mapFaces1) {
            // Rebuild face list with only the validated faces
            int mf = 0;
            for (int f = 0; f < static_cast<int>(irMesh.indices.size() / 3); ++f) {
                int base = f * 3;
                uint32_t i0 = irMesh.indices[base];
                uint32_t i1 = irMesh.indices[base + 1];
                uint32_t i2 = irMesh.indices[base + 2];
                if (i0 >= static_cast<uint32_t>(vertCount) ||
                    i1 >= static_cast<uint32_t>(vertCount) ||
                    i2 >= static_cast<uint32_t>(vertCount))
                    continue;
                mapFaces1[mf].setTVerts(
                    static_cast<int>(i0), static_cast<int>(i1), static_cast<int>(i2));
                ++mf;
            }
        }
    }

    // Additional UV sets (channels 2-4)
    for (int uvSet = 1; uvSet < irMesh.vertices[0].uvSetCount && uvSet < 4; ++uvSet) {
        int channel = uvSet + 1;
        mesh.setMapSupport(channel, TRUE);
        mesh.setNumMapVerts(channel, vertCount);
        mesh.setNumMapFaces(channel, faceCount);

        for (int i = 0; i < vertCount; ++i) {
            const auto& uv = irMesh.vertices[i].uvSets[uvSet];
            mesh.setMapVert(channel, i, Point3(uv.x, 1.0f - uv.y, 0.0f));
        }

        TVFace* mapFacesCh = mesh.mapFaces(channel);
        if (mapFacesCh) {
            int mf = 0;
            for (int f = 0; f < static_cast<int>(irMesh.indices.size() / 3); ++f) {
                int base = f * 3;
                uint32_t i0 = irMesh.indices[base];
                uint32_t i1 = irMesh.indices[base + 1];
                uint32_t i2 = irMesh.indices[base + 2];
                if (i0 >= static_cast<uint32_t>(vertCount) ||
                    i1 >= static_cast<uint32_t>(vertCount) ||
                    i2 >= static_cast<uint32_t>(vertCount))
                    continue;
                mapFacesCh[mf].setTVerts(
                    static_cast<int>(i0), static_cast<int>(i1), static_cast<int>(i2));
                ++mf;
            }
        }
    }

    // Normals via smoothing groups (explicit normals handled later if needed)
    mesh.buildNormals();
    mesh.InvalidateGeomCache();
    mesh.InvalidateTopologyCache();

    INode* node = gi->CreateObjectNode(triObj);
    MSTR name;
    name.printf(_T("%hs"), irMesh.name.c_str());
    node->SetName(name);

    return node;
}

// ── Skin modifier application ───────────────────────────────

bool applySkinModifier(INode* meshNode, const ir::Mesh& irMesh,
                       const std::vector<INode*>& boneNodes, Interface* gi)
{
    // Check if any vertex has skin influences
    bool hasSkin = false;
    for (const auto& v : irMesh.vertices) {
        if (!v.skinInfluences.empty()) { hasSkin = true; break; }
    }
    if (!hasSkin) return false;

    // Create Skin modifier
    Modifier* skinMod = static_cast<Modifier*>(
        gi->CreateInstance(OSM_CLASS_ID, core_ids::SKIN_CLASS_ID));
    if (!skinMod) return false;

    // Get IDerivedObject
    IDerivedObject* derivObj = nullptr;
    Object* obj = meshNode->GetObjectRef();
    if (obj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        derivObj = static_cast<IDerivedObject*>(obj);
    } else {
        derivObj = CreateDerivedObject(obj);
        meshNode->SetObjectRef(derivObj);
    }
    derivObj->AddModifier(skinMod);

    // Need to evaluate the pipeline to initialize ISkin
    gi->ForceCompleteRedraw(FALSE);
    meshNode->EvalWorldState(0);

    // Get ISkinImportData
    ISkinImportData* skinImport = static_cast<ISkinImportData*>(
        skinMod->GetInterface(I_SKINIMPORTDATA));
    if (!skinImport) return false;

    // Collect all unique bones used
    std::set<int32_t> usedBones;
    for (const auto& v : irMesh.vertices)
        for (const auto& inf : v.skinInfluences)
            if (inf.boneIndex >= 0 && inf.boneIndex < static_cast<int32_t>(boneNodes.size()))
                usedBones.insert(inf.boneIndex);

    // Add bones to skin
    for (int32_t bIdx : usedBones) {
        if (boneNodes[bIdx])
            skinImport->AddBoneEx(boneNodes[bIdx], TRUE);
    }

    // Assign weights per vertex
    int vertCount = static_cast<int>(irMesh.vertices.size());
    for (int v = 0; v < vertCount; ++v) {
        const auto& influences = irMesh.vertices[v].skinInfluences;
        if (influences.empty()) continue;

        Tab<INode*> bones;
        Tab<float> weights;
        bones.SetCount(static_cast<int>(influences.size()));
        weights.SetCount(static_cast<int>(influences.size()));

        for (int j = 0; j < static_cast<int>(influences.size()); ++j) {
            int32_t bIdx = influences[j].boneIndex;
            bones[j] = (bIdx >= 0 && bIdx < static_cast<int32_t>(boneNodes.size()))
                ? boneNodes[bIdx] : nullptr;
            weights[j] = influences[j].weight;
        }

        skinImport->AddWeights(meshNode, v, bones, weights);
    }

    return true;
}

// ── Animation key insertion ─────────────────────────────────
// Matches WhiteoutDexSceneRebuilder.ms animation import logic.

// Bezier constant: (3 * frameRate) / ticksPerFrame
// At 30fps: 3*30/160 = 0.5625.  Converts raw MDX tangent offsets
// into the slope form that Max's bezier controllers expect.
float getBezierConstant() {
    int tpf = GetTicksPerFrame();
    float fps = 4800.0f / static_cast<float>(tpf);
    return (3.0f * fps) / static_cast<float>(tpf);
}

void insertTranslationKeys(INode* node, const ir::Vec3Track& track,
                           const Point3& stubVal)
{
    if (track.empty()) return;

    Control* tmCtrl = node->GetTMController();
    if (!tmCtrl) return;

    // DON'T replace the position controller!
    // The existing one (from AttachChild) is "Position XYZ" with 3 float sub-controllers.
    // Replacing it with bezier_position causes the X channel to be lost (IBezPoint3Key bug).
    // Instead, use SetValue() on the existing controller — the SDK-recommended approach.
    Control* posCtrl = tmCtrl->GetPositionController();
    if (!posCtrl) return;

    ILOG << "    TRANS '" << narrow(node->GetName()) << "'"
         << " stub=(" << stubVal.x << "," << stubVal.y << "," << stubVal.z
         << ") keys=" << track.keys.size() << "\n";
    if (track.keys.size() > 0) {
        auto& k0 = track.keys[0];
        ILOG << "      key[0] time=" << k0.time
             << " val=(" << k0.value.x << "," << k0.value.y << "," << k0.value.z << ")\n";
    }
    ILOG.flush();

    int numKeys = static_cast<int>(track.keys.size());

    // Set bind-pose position at frame 0
    bool needStub = (numKeys == 0 || track.keys[0].time > 0);
    if (needStub) {
        Point3 sv = stubVal;
        posCtrl->SetValue(0, &sv, 1, CTRL_ABSOLUTE);
    }

    // Set animation keys via SetValue — Max creates the right key type internally
    for (int i = 0; i < numKeys; ++i) {
        Point3 val = track.keys[i].value;
        posCtrl->SetValue(track.keys[i].time, &val, 1, CTRL_ABSOLUTE);
    }

    // ORT cycle for global sequences
    if (track.globalSequenceIndex >= 0) {
        posCtrl->SetORT(ORT_CYCLE, ORT_AFTER);
        posCtrl->EnableORTs(TRUE);
    }
}

void insertRotationKeys(INode* node, const ir::QuatTrack& track,
                        const Quat& stubVal) {
    if (track.empty()) return;

    Control* tmCtrl = node->GetTMController();
    if (!tmCtrl) return;

    // Replace rotation controller with linear_rotation for quaternion SLERP.
    // Unlike position, we MUST replace because Euler XYZ can't do quaternion interpolation.
    Control* rotCtrl = static_cast<Control*>(
        CreateInstance(CTRL_ROTATION_CLASS_ID, Class_ID(LININTERP_ROTATION_CLASS_ID, 0)));
    if (!rotCtrl) return;
    tmCtrl->SetRotationController(rotCtrl);

    int numRotKeys = static_cast<int>(track.keys.size());

    // MaxScript fixQuaternions: ensure consecutive keys are in the same
    // hemisphere so SLERP takes the shortest arc.
    Quat prevQ = stubVal;
    std::vector<Quat> fixedKeys(numRotKeys);
    for (int i = 0; i < numRotKeys; ++i) {
        Quat q = track.keys[i].value;
        if ((q.x * prevQ.x + q.y * prevQ.y + q.z * prevQ.z + q.w * prevQ.w) < 0.0f)
            q = -q;
        fixedKeys[i] = q;
        prevQ = q;
    }

    // Use SetValue for key creation — SDK-recommended approach
    // SetValue for rotation expects CONJUGATE quaternion compared to ILinRotKey.val
    // Conjugate: negate x,y,z; keep w
    bool needRotStub = (numRotKeys == 0 || track.keys[0].time > 0);
    if (needRotStub) {
        Quat sv(-stubVal.x, -stubVal.y, -stubVal.z, stubVal.w);
        rotCtrl->SetValue(0, &sv, 1, CTRL_ABSOLUTE);
    }

    for (int i = 0; i < numRotKeys; ++i) {
        Quat q(-fixedKeys[i].x, -fixedKeys[i].y, -fixedKeys[i].z, fixedKeys[i].w);
        rotCtrl->SetValue(track.keys[i].time, &q, 1, CTRL_ABSOLUTE);
    }

    if (track.globalSequenceIndex >= 0) {
        rotCtrl->SetORT(ORT_CYCLE, ORT_AFTER);
        rotCtrl->EnableORTs(TRUE);
    }
}

void insertScaleKeys(INode* node, const ir::Vec3Track& track,
                     const Point3& stubVal)
{
    if (track.empty()) return;

    Control* tmCtrl = node->GetTMController();
    if (!tmCtrl) return;

    // Use existing scale controller + SetValue (same approach as position)
    Control* scaleCtrl = tmCtrl->GetScaleController();
    if (!scaleCtrl) return;

    int numKeys = static_cast<int>(track.keys.size());

    // Set bind-pose scale at frame 0
    bool needStub = (numKeys == 0 || track.keys[0].time > 0);
    if (needStub) {
        ScaleValue sv(stubVal);
        scaleCtrl->SetValue(0, &sv, 1, CTRL_ABSOLUTE);
    }

    // Set animation keys
    for (int i = 0; i < numKeys; ++i) {
        ScaleValue sv(track.keys[i].value);
        scaleCtrl->SetValue(track.keys[i].time, &sv, 1, CTRL_ABSOLUTE);
    }

    if (track.globalSequenceIndex >= 0) {
        scaleCtrl->SetORT(ORT_CYCLE, ORT_AFTER);
        scaleCtrl->EnableORTs(TRUE);
    }
}

// ── Parameter animation helpers ─────────────────────────────
// Matches MaxScript applyFloatParamAnimations / applyColorParamAnimations.
// Creates a controller of the appropriate type, writes keys via IKeyControl,
// then assigns the controller to a paramblock param via SetController.

// Paramblock param locator (same as scene builders)
struct PBParam {
    IParamBlock2* pb = nullptr;
    ParamID id = -1;
    explicit operator bool() const { return pb != nullptr; }
};

PBParam findPBParam(ReferenceTarget* target, const wchar_t* name) {
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

// Write float keys onto a controller and return it.  Caller assigns to PB.
Control* createFloatController(const ir::FloatTrack& track) {
    if (track.empty()) return nullptr;

    Class_ID cid;
    switch (track.interpolation) {
    case ir::InterpolationType::Linear:  cid = Class_ID(LININTERP_FLOAT_CLASS_ID, 0); break;
    case ir::InterpolationType::Hermite: cid = Class_ID(TCBINTERP_FLOAT_CLASS_ID, 0); break;
    default:                             cid = Class_ID(HYBRIDINTERP_FLOAT_CLASS_ID, 0); break;
    }

    Control* ctrl = static_cast<Control*>(CreateInstance(CTRL_FLOAT_CLASS_ID, cid));
    if (!ctrl) return nullptr;

    IKeyControl* ikc = GetKeyControlInterface(ctrl);
    if (!ikc) { ctrl->DeleteThis(); return nullptr; }

    float bezConst = getBezierConstant();
    int numKeys = static_cast<int>(track.keys.size());

    for (int i = 0; i < numKeys; ++i) {
        const auto& kf = track.keys[i];
        switch (track.interpolation) {
        case ir::InterpolationType::Linear: {
            ILinFloatKey key;
            memset(&key, 0, sizeof(key));
            key.time = kf.time;
            key.val = kf.value;
            ikc->AppendKey(&key);
            break;
        }
        case ir::InterpolationType::Hermite: {
            ITCBFloatKey key;
            memset(&key, 0, sizeof(key));
            key.time = kf.time;
            key.val = kf.value;
            ikc->AppendKey(&key);
            break;
        }
        default: { // Bezier (also used for None/DontInterp on float params)
            IBezFloatKey key;
            memset(&key, 0, sizeof(key));
            key.time = kf.time;
            key.val = kf.value;

            if (track.interpolation == ir::InterpolationType::None) {
                SetInTanType(key.flags, BEZKEY_STEP);
                SetOutTanType(key.flags, BEZKEY_STEP);
            } else if (kf.hasTangents) {
                SetInTanType(key.flags, BEZKEY_USER);
                SetOutTanType(key.flags, BEZKEY_USER);
                if (i > 0) {
                    float dt = static_cast<float>(kf.time - track.keys[i-1].time);
                    if (dt > 0.0f) key.intan = bezConst * (kf.inTangent - kf.value) / dt;
                }
                if (i < numKeys - 1) {
                    float dt = static_cast<float>(track.keys[i+1].time - kf.time);
                    if (dt > 0.0f) key.outtan = bezConst * (kf.outTangent - kf.value) / dt;
                }
            }
            ikc->AppendKey(&key);
            break;
        }
        }
    }
    ikc->SortKeys();

    if (track.globalSequenceIndex >= 0) {
        ctrl->SetORT(ORT_CYCLE, ORT_AFTER);
        ctrl->EnableORTs(TRUE);
    }
    return ctrl;
}

// Write Point3 (color) keys onto a controller and return it.
Control* createColorController(const ir::ColorTrack& track) {
    if (track.empty()) return nullptr;

    // MaxScript maps: #linear → bezier_point3, #hermite → tcb_point3,
    //                 #bezier → bezier_color (same as bezier_point3)
    Class_ID cid;
    switch (track.interpolation) {
    case ir::InterpolationType::Hermite: cid = Class_ID(TCBINTERP_POINT3_CLASS_ID, 0); break;
    default:                             cid = Class_ID(HYBRIDINTERP_POINT3_CLASS_ID, 0); break;
    }

    Control* ctrl = static_cast<Control*>(CreateInstance(CTRL_POINT3_CLASS_ID, cid));
    if (!ctrl) return nullptr;

    IKeyControl* ikc = GetKeyControlInterface(ctrl);
    if (!ikc) { ctrl->DeleteThis(); return nullptr; }

    float bezConst = getBezierConstant();
    int numKeys = static_cast<int>(track.keys.size());

    for (int i = 0; i < numKeys; ++i) {
        const auto& kf = track.keys[i];
        Point3 val(kf.value.r, kf.value.g, kf.value.b);

        if (track.interpolation == ir::InterpolationType::Hermite) {
            ITCBPoint3Key key;
            memset(&key, 0, sizeof(key));
            key.time = kf.time;
            key.val = val;
            ikc->AppendKey(&key);
        } else {
            // Bezier, Linear, None all use IBezPoint3Key
            IBezPoint3Key key;
            memset(&key, 0, sizeof(key));
            key.time = kf.time;
            key.val = val;

            if (track.interpolation == ir::InterpolationType::None) {
                SetInTanType(key.flags, BEZKEY_STEP);
                SetOutTanType(key.flags, BEZKEY_STEP);
            } else if (kf.hasTangents && track.interpolation == ir::InterpolationType::Bezier) {
                SetInTanType(key.flags, BEZKEY_USER);
                SetOutTanType(key.flags, BEZKEY_USER);
                Point3 inT(kf.inTangent.r, kf.inTangent.g, kf.inTangent.b);
                Point3 outT(kf.outTangent.r, kf.outTangent.g, kf.outTangent.b);
                if (i > 0) {
                    float dt = static_cast<float>(kf.time - track.keys[i-1].time);
                    if (dt > 0.0f) key.intan = bezConst * (inT - val) / dt;
                }
                if (i < numKeys - 1) {
                    float dt = static_cast<float>(track.keys[i+1].time - kf.time);
                    if (dt > 0.0f) key.outtan = bezConst * (outT - val) / dt;
                }
            }
            ikc->AppendKey(&key);
        }
    }
    ikc->SortKeys();

    if (track.globalSequenceIndex >= 0) {
        ctrl->SetORT(ORT_CYCLE, ORT_AFTER);
        ctrl->EnableORTs(TRUE);
    }
    return ctrl;
}

// Animate a float paramblock param from an IR float track index.
void animateFloatPB(IParamBlock2* pb, ParamID pid,
                    int32_t trackIndex, const ir::IRModel& irModel)
{
    if (!pb || trackIndex < 0 ||
        trackIndex >= static_cast<int32_t>(irModel.floatTracks.size()))
        return;
    const auto& track = irModel.floatTracks[trackIndex];
    Control* ctrl = createFloatController(track);
    if (ctrl)
        pb->SetControllerByID(pid, 0, ctrl, FALSE);
}

// Animate a float param by name on a scripted plugin.
void animateFloatNamed(ReferenceTarget* ref, const wchar_t* name,
                       int32_t trackIndex, const ir::IRModel& irModel)
{
    if (!ref || trackIndex < 0 ||
        trackIndex >= static_cast<int32_t>(irModel.floatTracks.size()))
        return;
    auto p = findPBParam(ref, name);
    if (!p) return;
    const auto& track = irModel.floatTracks[trackIndex];
    Control* ctrl = createFloatController(track);
    if (ctrl)
        p.pb->SetControllerByID(p.id, 0, ctrl, FALSE);
}

// Animate a color param by name on a scripted plugin.
void animateColorNamed(ReferenceTarget* ref, const wchar_t* name,
                       int32_t trackIndex, const ir::IRModel& irModel)
{
    if (!ref || trackIndex < 0 ||
        trackIndex >= static_cast<int32_t>(irModel.colorTracks.size()))
        return;
    auto p = findPBParam(ref, name);
    if (!p) return;
    const auto& track = irModel.colorTracks[trackIndex];
    Control* ctrl = createColorController(track);
    if (ctrl)
        p.pb->SetControllerByID(p.id, 0, ctrl, FALSE);
}

// Animate a color paramblock param from an IR color track index.
void animateColorPB(IParamBlock2* pb, ParamID pid,
                    int32_t trackIndex, const ir::IRModel& irModel)
{
    if (!pb || trackIndex < 0 ||
        trackIndex >= static_cast<int32_t>(irModel.colorTracks.size()))
        return;
    const auto& track = irModel.colorTracks[trackIndex];
    Control* ctrl = createColorController(track);
    if (ctrl)
        pb->SetControllerByID(pid, 0, ctrl, FALSE);
}

// ParamIDs for native plugins (mirrored from scene builders)
enum P1Params : ParamID {
    P1_PB_COUNT = 0, P1_PB_SPEED = 1, P1_PB_EMISSION_RATE = 2,
    P1_PB_LIFE = 3, P1_PB_ACCELERATION = 4,
    P1_PB_LATITUDE = 5, P1_PB_LONGITUDE = 6,
};
enum P2Params : ParamID {
    P2_PB_COUNT = 0, P2_PB_SPEED = 1, P2_PB_VARIATION = 2,
    P2_PB_WIDTH = 4, P2_PB_HEIGHT = 5,
    P2_PB_GRAVITY = 36, P2_PB_LATITUDE = 40,
};
enum RibbonParams : ParamID {
    RB_PB_HEIGHT_ABOVE = 0, RB_PB_HEIGHT_BELOW = 1,
    RB_PB_TEX_SLOT = 6, RB_PB_COLOR = 8, RB_PB_ALPHA = 9,
};

// ── Visibility key insertion ────────────────────────────────
// Matches MaxScript applyVisibilityAnimations:
//   None (DontInterp) → On_Off (boolean toggle) controller
//   Linear            → linear_float
//   Hermite           → tcb_float
//   Bezier            → bezier_float with custom tangents

void insertVisibilityKeys(INode* node, const ir::FloatTrack& track,
                          const std::vector<ir::Sequence>& sequences)
{
    if (track.empty()) return;

    int numKeys = static_cast<int>(track.keys.size());

    // Ensure frame 0 exists
    bool hasFrameZero = false;
    for (const auto& k : track.keys)
        if (k.time == 0) { hasFrameZero = true; break; }

    Control* visCtrl = nullptr;

    switch (track.interpolation) {
    case ir::InterpolationType::None: {
        // DontInterp → On/Off toggle controller (BOOL_CONTROL_CLASS_ID).
        // On/Off default state is ON (visible=1.0). Each key toggles state.
        // Output is 1.0 (visible) or -1.0 (hidden).
        //
        // Critical: MDX sequences are independent — visibility resets to ON
        // at the start of every sequence UNLESS there's an explicit 0 key
        // at that time. Since On/Off accumulates toggles across the whole
        // timeline, we must insert correction toggles at sequence boundaries.
        static const Class_ID ON_OFF_CLASS_ID(0x984b8d27, 0x938f3e43);
        visCtrl = static_cast<Control*>(
            CreateInstance(CTRL_FLOAT_CLASS_ID, ON_OFF_CLASS_ID));
        if (!visCtrl) return;
        node->SetVisController(visCtrl);

        // Build ordered lookup: time → MDX value
        std::map<TimeValue, float> keyMap;
        for (const auto& k : track.keys)
            keyMap[k.time] = k.value;

        // On/Off starts ON. Track accumulated state.
        bool state = true;

        // AddNewKey inserts a toggle point at time t.
        auto emitToggle = [&](TimeValue t) {
            visCtrl->AddNewKey(t, 0);
            state = !state;
        };

        if (!sequences.empty()) {
            // Process sequence by sequence.
            for (const auto& seq : sequences) {
                // Expected state at sequence start: ON unless explicit 0 key
                bool startVis = true;
                auto it = keyMap.find(seq.startTime);
                if (it != keyMap.end() && it->second < 0.5f)
                    startVis = false;

                // Correct accumulated state if it doesn't match
                if (state != startVis)
                    emitToggle(seq.startTime);

                // Process keys within (startTime, endTime)
                auto kit = keyMap.upper_bound(seq.startTime);
                for (; kit != keyMap.end() && kit->first < seq.endTime; ++kit) {
                    bool desired = (kit->second >= 0.5f);
                    if (state != desired)
                        emitToggle(kit->first);
                }
            }
        } else {
            // No sequences — process all keys linearly
            for (const auto& [t, v] : keyMap) {
                bool desired = (v >= 0.5f);
                if (state != desired)
                    emitToggle(t);
            }
        }
        break;
    }
    case ir::InterpolationType::Linear: {
        visCtrl = static_cast<Control*>(
            CreateInstance(CTRL_FLOAT_CLASS_ID, Class_ID(LININTERP_FLOAT_CLASS_ID, 0)));
        if (!visCtrl) return;
        node->SetVisController(visCtrl);

        IKeyControl* ikc = GetKeyControlInterface(visCtrl);
        if (!ikc) return;

        if (!hasFrameZero) {
            ILinFloatKey stubKey;
            memset(&stubKey, 0, sizeof(stubKey));
            stubKey.time = 0;
            stubKey.val = track.keys[0].value;
            ikc->AppendKey(&stubKey);
        }

        for (int i = 0; i < numKeys; ++i) {
            ILinFloatKey key;
            memset(&key, 0, sizeof(key));
            key.time = track.keys[i].time;
            key.val = track.keys[i].value;
            ikc->AppendKey(&key);
        }
        ikc->SortKeys();
        break;
    }
    case ir::InterpolationType::Hermite: {
        visCtrl = static_cast<Control*>(
            CreateInstance(CTRL_FLOAT_CLASS_ID, Class_ID(TCBINTERP_FLOAT_CLASS_ID, 0)));
        if (!visCtrl) return;
        node->SetVisController(visCtrl);

        IKeyControl* ikc = GetKeyControlInterface(visCtrl);
        if (!ikc) return;

        if (!hasFrameZero) {
            ITCBFloatKey stubKey;
            memset(&stubKey, 0, sizeof(stubKey));
            stubKey.time = 0;
            stubKey.val = track.keys[0].value;
            stubKey.tens = 0.0f;
            stubKey.cont = 0.0f;
            stubKey.bias = 0.0f;
            stubKey.easeIn = 0.0f;
            stubKey.easeOut = 0.0f;
            ikc->AppendKey(&stubKey);
        }

        for (int i = 0; i < numKeys; ++i) {
            ITCBFloatKey key;
            memset(&key, 0, sizeof(key));
            key.time = track.keys[i].time;
            key.val = track.keys[i].value;
            key.tens = 0.0f;
            key.cont = 0.0f;
            key.bias = 0.0f;
            key.easeIn = 0.0f;
            key.easeOut = 0.0f;
            ikc->AppendKey(&key);
        }
        ikc->SortKeys();
        break;
    }
    case ir::InterpolationType::Bezier: {
        visCtrl = static_cast<Control*>(
            CreateInstance(CTRL_FLOAT_CLASS_ID, Class_ID(HYBRIDINTERP_FLOAT_CLASS_ID, 0)));
        if (!visCtrl) return;
        node->SetVisController(visCtrl);

        IKeyControl* ikc = GetKeyControlInterface(visCtrl);
        if (!ikc) return;

        if (!hasFrameZero) {
            IBezFloatKey stubKey;
            memset(&stubKey, 0, sizeof(stubKey));
            stubKey.time = 0;
            stubKey.val = track.keys[0].value;
            stubKey.intan = 0.0f;
            stubKey.outtan = 0.0f;
            ikc->AppendKey(&stubKey);
        }

        float bezConst = getBezierConstant();

        for (int i = 0; i < numKeys; ++i) {
            IBezFloatKey key;
            memset(&key, 0, sizeof(key));
            key.time = track.keys[i].time;
            key.val = track.keys[i].value;

            // Custom tangents from MDX data
            SetInTanType(key.flags, BEZKEY_USER);
            SetOutTanType(key.flags, BEZKEY_USER);

            if (track.keys[i].hasTangents) {
                // In tangent: relative to previous key time
                if (i > 0) {
                    float dt = static_cast<float>(track.keys[i].time - track.keys[i-1].time);
                    if (dt > 0.0f)
                        key.intan = bezConst * (track.keys[i].inTangent - key.val) / dt;
                }
                // Out tangent: relative to next key time
                if (i < numKeys - 1) {
                    float dt = static_cast<float>(track.keys[i+1].time - track.keys[i].time);
                    if (dt > 0.0f)
                        key.outtan = bezConst * (track.keys[i].outTangent - key.val) / dt;
                }
            }

            ikc->AppendKey(&key);
        }
        ikc->SortKeys();
        break;
    }
    }

    // ORT cycle for global sequences
    if (visCtrl && track.globalSequenceIndex >= 0) {
        visCtrl->SetORT(ORT_CYCLE, ORT_AFTER);
        visCtrl->EnableORTs(TRUE);
    }
}

// ── Timeline remapping ──────────────────────────────────────
// Matches MaxScript: convertSequences + preProcessKeys + apply*Animations.
// Compacts sequences to start at frame 10 with 20-frame gaps,
// remaps all animation key times, and inserts boundary keys.

struct SeqRange {
    TimeValue oldStart, oldEnd;
    TimeValue newStart, newEnd;
    bool isLooping;
    size_t origIdx;
};

template <typename T>
void remapTrackKeys(ir::Track<T>& track,
                    const std::vector<SeqRange>& ranges,
                    const T& defaultVal,
                    bool useDefaultForStartBoundary)
{
    if (track.keys.empty() || track.globalSequenceIndex >= 0) return;

    std::vector<ir::Keyframe<T>> newKeys;

    for (size_t si = 0; si < ranges.size(); ++si) {
        const auto& r = ranges[si];

        // Collect keys in [oldStart, oldEnd].
        // Back-to-back: key at boundary goes to next sequence.
        std::vector<size_t> seqKeyIdx;
        for (size_t ki = 0; ki < track.keys.size(); ++ki) {
            TimeValue t = track.keys[ki].time;
            if (t < r.oldStart || t > r.oldEnd) continue;
            if (t == r.oldEnd && si + 1 < ranges.size()
                && ranges[si + 1].oldStart == r.oldEnd)
                continue;
            seqKeyIdx.push_back(ki);
        }

        TimeValue offset = r.newStart - r.oldStart;

        if (seqKeyIdx.empty()) {
            // No keys in this sequence — stub at start and end
            ir::Keyframe<T> sk{}; sk.time = r.newStart; sk.value = defaultVal;
            ir::Keyframe<T> ek{}; ek.time = r.newEnd;   ek.value = defaultVal;
            newKeys.push_back(sk);
            newKeys.push_back(ek);
        } else {
            // Start boundary
            if (track.keys[seqKeyIdx[0]].time + offset != r.newStart) {
                ir::Keyframe<T> sk{};
                sk.time = r.newStart;
                sk.value = useDefaultForStartBoundary
                    ? defaultVal : track.keys[seqKeyIdx[0]].value;
                newKeys.push_back(sk);
            }

            // Remap sequence keys
            for (size_t ki : seqKeyIdx) {
                ir::Keyframe<T> k = track.keys[ki];
                k.time += offset;
                newKeys.push_back(k);
            }

            // End boundary
            size_t lastKi = seqKeyIdx.back();
            if (track.keys[lastKi].time + offset != r.newEnd) {
                ir::Keyframe<T> ek{};
                ek.time = r.newEnd;
                ek.value = r.isLooping
                    ? track.keys[seqKeyIdx[0]].value   // loop closure
                    : track.keys[lastKi].value;         // hold last
                newKeys.push_back(ek);
            }
        }

        // Gap keys: proportional remapping into (shorter) inter-sequence gap
        if (si + 1 < ranges.size()) {
            TimeValue ogS = r.oldEnd, ogE = ranges[si + 1].oldStart;
            TimeValue ngS = r.newEnd, ngE = ranges[si + 1].newStart;
            float oLen = static_cast<float>(ogE - ogS);
            float nLen = static_cast<float>(ngE - ngS);
            if (oLen > 0.0f && nLen > 0.0f) {
                for (const auto& k : track.keys) {
                    if (k.time > ogS && k.time < ogE) {
                        float ratio = static_cast<float>(k.time - ogS) / oLen;
                        ir::Keyframe<T> gk = k;
                        gk.time = ngS + static_cast<TimeValue>(ratio * nLen);
                        newKeys.push_back(gk);
                    }
                }
            }
        }
    }

    std::sort(newKeys.begin(), newKeys.end(),
        [](const ir::Keyframe<T>& a, const ir::Keyframe<T>& b) {
            return a.time < b.time;
        });

    track.keys = std::move(newKeys);
}

void remapTimeline(ir::IRModel& irModel) {
    if (irModel.sequences.empty()) return;

    const TimeValue tpf = GetTicksPerFrame(); // 160 at 30 fps

    // Build ranges sorted by old start time
    std::vector<SeqRange> ranges;
    ranges.reserve(irModel.sequences.size());
    for (size_t i = 0; i < irModel.sequences.size(); ++i) {
        SeqRange r{};
        r.oldStart  = irModel.sequences[i].startTime;
        r.oldEnd    = irModel.sequences[i].endTime;
        r.isLooping = irModel.sequences[i].isLooping;
        r.origIdx   = i;
        ranges.push_back(r);
    }
    std::sort(ranges.begin(), ranges.end(),
        [](const SeqRange& a, const SeqRange& b) { return a.oldStart < b.oldStart; });

    // Compact: frame 10 start, gap = ((endFrame/10)*10 + 20) frames
    TimeValue nextStart = 10 * tpf;
    for (auto& r : ranges) {
        TimeValue dur = r.oldEnd - r.oldStart;
        r.newStart = nextStart;
        r.newEnd   = nextStart + dur;
        TimeValue endFrames = r.newEnd / tpf;
        nextStart = ((endFrames / 10) * 10 + 20) * tpf;
    }

    // Update ir::Sequence times
    for (const auto& r : ranges) {
        irModel.sequences[r.origIdx].startTime = r.newStart;
        irModel.sequences[r.origIdx].endTime   = r.newEnd;
    }

    // Remap node animation tracks
    for (auto& na : irModel.nodeAnimations) {
        remapTrackKeys(na.translation, ranges, Point3(0.0f, 0.0f, 0.0f), false);
        remapTrackKeys(na.rotation,    ranges, Quat(0.0f, 0.0f, 0.0f, 1.0f), false);
        remapTrackKeys(na.scale,       ranges, Point3(1.0f, 1.0f, 1.0f), false);
    }

    // Remap indexed tracks (global-sequence tracks skipped inside remapTrackKeys)
    for (auto& t : irModel.floatTracks)
        remapTrackKeys(t, ranges, 1.0f, true);
    for (auto& t : irModel.vec3Tracks)
        remapTrackKeys(t, ranges, Point3(0.0f, 0.0f, 0.0f), false);
    for (auto& t : irModel.quatTracks)
        remapTrackKeys(t, ranges, Quat(0.0f, 0.0f, 0.0f, 1.0f), false);
    for (auto& t : irModel.colorTracks)
        remapTrackKeys(t, ranges, Color(1.0f, 1.0f, 1.0f), false);
    for (auto& t : irModel.intTracks)
        remapTrackKeys(t, ranges, int32_t(0), true);
    for (auto& t : irModel.vec4Tracks)
        remapTrackKeys(t, ranges, Point4(0.0f, 0.0f, 0.0f, 0.0f), false);

    // Remap event object key times
    for (auto& evt : irModel.eventObjects) {
        std::vector<TimeValue> newTimes;
        for (TimeValue kt : evt.keyTimes) {
            for (size_t si = 0; si < ranges.size(); ++si) {
                const auto& r = ranges[si];
                if (kt >= r.oldStart && kt <= r.oldEnd) {
                    newTimes.push_back(kt + (r.newStart - r.oldStart));
                    break;
                }
                if (si + 1 < ranges.size() && kt > r.oldEnd
                    && kt < ranges[si + 1].oldStart) {
                    float oLen = static_cast<float>(ranges[si + 1].oldStart - r.oldEnd);
                    float nLen = static_cast<float>(ranges[si + 1].newStart - r.newEnd);
                    if (oLen > 0.0f && nLen > 0.0f) {
                        float ratio = static_cast<float>(kt - r.oldEnd) / oLen;
                        newTimes.push_back(
                            r.newEnd + static_cast<TimeValue>(ratio * nLen));
                    }
                    break;
                }
            }
        }
        evt.keyTimes = std::move(newTimes);
    }
}

} // namespace

// ═══════════════════════════════════════════════════════════════
// DoImport — main import pipeline
// ═══════════════════════════════════════════════════════════════

int MdlxImporterPlugin::DoImport(const TCHAR* name, ImpInterface* ii,
                                  Interface* gi, BOOL suppressPrompts)
{
    core::ExportErrorReporter reporter;
    MdlxImportOptions opts;

    // 1. Load settings from INI
    loadImportOptionsFromINI(gi, opts);

    // 2. Parse MDX/MDL file via WhiteoutLib (fast — needed to detect version)
    std::string filePath = wcharToUtf8(name);
    whiteout::mdx::Parser parser;
    whiteout::mdx::Model mdxModel;

    try {
        mdxModel = parser.parse(filePath);
    } catch (const std::exception& e) {
        MSTR msg;
        msg.printf(_T("Failed to parse model file:\n%hs"), e.what());
        if (gi->GetMAXHWnd())
            MessageBoxW(gi->GetMAXHWnd(), msg.data(), _T("MDLXImporter"), MB_OK | MB_ICONERROR);
        return IMPEXP_FAIL;
    }

    opts.detectedVersion = mdxModel.version;

    // 3. Show import options dialog (unless suppressed)
    if (!suppressPrompts) {
        bool isReforged = (mdxModel.version >= 1200);
        if (!showImportDialog(hInstance, gi->GetMAXHWnd(), opts, isReforged))
            return IMPEXP_CANCEL;
    }

    applyFastPreset(opts);

    // 4. Disassemble: mdx::Model → ir::IRModel
    mdx_disasm::MdxModelDisassembler disassembler;
    ir::IRModel irModel = disassembler.disassemble(mdxModel, opts);

    // 4b. Compact timeline (MaxScript convertSequences + preProcessKeys)
    remapTimeline(irModel);

    // 4c. For v1200 (Reforged), pivot points from PIVT may be zero.
    // The authoritative world-space positions come from the BPOS bind pose
    // matrix translations.  Patch pivots so all downstream code (bone
    // positioning, animation offsets, scene builders) uses correct values.
    if (opts.detectedVersion >= 1200) {
        for (auto& bone : irModel.bones) {
            Point3 bposPos = bone.bindPose.GetTrans();
            bone.pivotPoint = bposPos;
            if (bone.nodeIndex >= 0
                && bone.nodeIndex < static_cast<int32_t>(irModel.nodes.size())) {
                irModel.nodes[bone.nodeIndex].pivotPoint = bposPos;
            }
        }
    }

    // 5. Handle import mode
    if (opts.core.mode == ir::CoreImportOptions::ImportMode::NewScene) {
        ii->NewScene();
    }

    // 6. Optimize (optional)
    if (opts.core.optimizeGeometry) {
        core::VertexOptimizer vopt;
        for (auto& mesh : irModel.meshes)
            vopt.optimize(mesh, 0.001f);
    }
    if (opts.core.optimizeBonesAndHelpers) {
        core::BoneOptimizer bopt;
        bopt.optimize(irModel);
    }

    // 7. Build skeleton (bones and helpers)
    // SuspendAnimate prevents auto-keying during SetNodeTM + AttachChild
    SuspendAnimate();
    AnimateOff();

    std::vector<INode*> nodeMap(irModel.nodes.size(), nullptr);
    std::vector<INode*> boneNodes;

    if (opts.core.importBones || opts.core.importHelpers) {
        boneNodes.reserve(irModel.bones.size());

        // Determine which bones are directly wanted by import settings
        // and which must be force-imported because they are ancestors of wanted bones.
        std::vector<bool> mustImport(irModel.nodes.size(), false);
        for (const auto& bone : irModel.bones) {
            if (bone.isHelper && !opts.core.importHelpers) continue;
            if (!bone.isHelper && !opts.core.importBones) continue;
            if (bone.nodeIndex >= 0 && bone.nodeIndex < static_cast<int32_t>(mustImport.size()))
                mustImport[bone.nodeIndex] = true;
        }
        // Walk ancestors: any node with an imported descendant must also be imported
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& bone : irModel.bones) {
                if (bone.nodeIndex < 0 || bone.nodeIndex >= static_cast<int32_t>(mustImport.size()))
                    continue;
                if (!mustImport[bone.nodeIndex]) continue;
                if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int32_t>(mustImport.size())
                    && !mustImport[bone.parentIndex]) {
                    mustImport[bone.parentIndex] = true;
                    changed = true;
                }
            }
        }

        for (const auto& bone : irModel.bones) {
            if (bone.nodeIndex < 0 || bone.nodeIndex >= static_cast<int32_t>(mustImport.size()))
                continue;
            if (!mustImport[bone.nodeIndex]) continue;

            INode* boneNode = createBoneNode(bone, gi,
                bone.isHelper && opts.core.importHelpersAsPointHelpers);
            if (!boneNode) continue;

            // Replace rotation controller immediately with linear_rotation
            // before SetNodeTM.  The default Euler XYZ controller decomposes
            // rotations imprecisely (gimbal lock); linear_rotation stores the
            // quaternion directly and matches MaxScript's convention.
            {
                Control* tmCtrl = boneNode->GetTMController();
                if (tmCtrl) {
                    Control* linRot = static_cast<Control*>(
                        CreateInstance(CTRL_ROTATION_CLASS_ID,
                                       Class_ID(LININTERP_ROTATION_CLASS_ID, 0)));
                    if (linRot) tmCtrl->SetRotationController(linRot);
                }
            }

            // Position bone at pivot with identity rotation — matching MaxScript.
            // The full BPOS bind pose matrix (v1200) is metadata for round-trip
            // export, NOT for skeleton transforms.  Using it here would give bones
            // non-identity rest rotations, breaking the animation key math which
            // assumes local position = pivotDiff and rest rotation = identity.
            Matrix3 tm;
            tm.IdentityMatrix();
            tm.SetTrans(bone.pivotPoint);
            boneNode->SetNodeTM(0, tm);

            // MaxScript setupNode: apply DontInherit flags
            // MDX flags: 0x1 = DontInheritTranslation, 0x2 = DontInheritRotation, 0x4 = DontInheritScaling
            if (bone.nodeFlags & 0x7) {
                DWORD inheritFlags = INHERIT_ALL;
                if (bone.nodeFlags & 0x1)
                    inheritFlags &= ~(INHERIT_POS_X | INHERIT_POS_Y | INHERIT_POS_Z);
                if (bone.nodeFlags & 0x2)
                    inheritFlags &= ~(INHERIT_ROT_X | INHERIT_ROT_Y | INHERIT_ROT_Z);
                if (bone.nodeFlags & 0x4)
                    inheritFlags &= ~(INHERIT_SCL_X | INHERIT_SCL_Y | INHERIT_SCL_Z);
                Control* tmCtrl = boneNode->GetTMController();
                if (tmCtrl)
                    tmCtrl->SetInheritanceFlags(inheritFlags, TRUE);
            }

            // MaxScript: p.showLinks = true
            boneNode->ShowBone(2);

            if (bone.nodeIndex >= 0 && bone.nodeIndex < static_cast<int32_t>(nodeMap.size()))
                nodeMap[bone.nodeIndex] = boneNode;

            boneNodes.push_back(boneNode);
        }

        // Set parent relationships (second pass — all nodes must exist first)
        // MaxScript relinkObjects walks up the parent chain when a parent wasn't
        // created (e.g. helpers disabled).  We replicate that here.
        for (const auto& bone : irModel.bones) {
            if (bone.nodeIndex < 0 || bone.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                continue;
            INode* boneNode = nodeMap[bone.nodeIndex];
            if (!boneNode) continue;

            // Walk up the parent chain until we find one that exists in nodeMap
            int32_t parentIdx = bone.parentIndex;
            while (parentIdx >= 0 && parentIdx < static_cast<int32_t>(nodeMap.size())
                   && !nodeMap[parentIdx]) {
                parentIdx = irModel.nodes[parentIdx].parentIndex;
            }

            if (parentIdx >= 0 && parentIdx < static_cast<int32_t>(nodeMap.size())) {
                INode* parent = nodeMap[parentIdx];
                if (parent)
                    parent->AttachChild(boneNode);
            }
        }
    }

    ResumeAnimate();  // ← skeleton setup done, restore animate state

    // 8. Build meshes
    std::vector<INode*> meshNodes;
    meshNodes.reserve(irModel.meshes.size());

    for (const auto& irMesh : irModel.meshes) {
        INode* meshNode = createMeshNode(irMesh, gi);
        meshNodes.push_back(meshNode);  // may be nullptr for empty meshes
    }

    // NOTE: Skin modifier application is deferred to AFTER animation key
    // writing (step 12b) so that AddBoneEx captures the post-controller
    // bone TMs.  In MaxScript this happens automatically because the Skin
    // modifier evaluates lazily on the first viewport refresh, which occurs
    // after all controllers have been replaced.

    // Extract model directory for texture path resolution
    std::wstring modelDir;
    {
        std::wstring fullPath(name);
        auto lastSep = fullPath.find_last_of(L"\\/");
        if (lastSep != std::wstring::npos)
            modelDir = fullPath.substr(0, lastSep + 1);
    }

    // Open CASC storage once (shared between material builder and PE2 builder)
    void* cascPtr = nullptr;
    if (opts.core.importTextures && opts.searchCASC)
        cascPtr = mdx_scene::openCascStorage(opts.cascDirectory);

    // 10. Build materials and assign to meshes
    std::vector<Mtl*> materials;
    if (opts.core.importMaterials) {
        mdx_scene::Wc3MaterialBuilder matBuilder;
        materials = matBuilder.buildMaterials(
            irModel, opts.core.importTextures, modelDir, cascPtr, gi, reporter);

        for (size_t mi = 0; mi < irModel.meshes.size(); ++mi) {
            int32_t matIdx = irModel.meshes[mi].materialIndex;
            if (meshNodes[mi] && matIdx >= 0 && matIdx < static_cast<int32_t>(materials.size()))
                meshNodes[mi]->SetMtl(materials[matIdx]);
        }
    }

    // 11. Build format-specific scene objects (scripted plugins)
    if (opts.core.importObjects) {
        if (opts.core.importLights) {
            mdx_scene::Wc3LightBuilder lightBuilder;
            lightBuilder.buildLights(irModel, nodeMap, gi, reporter);
        }
        if (opts.core.importAttachments) {
            mdx_scene::Wc3AttachmentBuilder attBuilder;
            attBuilder.buildAttachments(irModel, nodeMap, gi, reporter);
        }
        if (opts.core.importParticleEmitters1) {
            mdx_scene::Wc3Particle1Builder pe1Builder;
            pe1Builder.buildParticles(irModel, nodeMap, gi, reporter);
        }
        if (opts.core.importParticleEmitters2) {
            mdx_scene::Wc3Particle2Builder pe2Builder;
            pe2Builder.buildParticles(irModel, nodeMap, modelDir, cascPtr, gi, reporter);
        }
        if (opts.core.importRibbonEmitters) {
            mdx_scene::Wc3RibbonBuilder ribBuilder;
            ribBuilder.buildRibbons(irModel, nodeMap, materials, gi, reporter);
        }
        if (opts.core.importEventObjects) {
            mdx_scene::Wc3EventBuilder evtBuilder;
            evtBuilder.buildEvents(irModel, nodeMap, gi, reporter);
        }
        if (opts.core.importCollisionShapes) {
            mdx_scene::Wc3CollisionBuilder colBuilder;
            colBuilder.buildCollisions(irModel, nodeMap, gi, reporter);
        }

        // v1200-specific
        if (opts.detectedVersion >= 1200) {
            if (opts.importCornEmitters) {
                mdx_scene::Wc3PopcornBuilder cornBuilder;
                cornBuilder.buildPopcorn(irModel, nodeMap, gi, reporter);
            }
            if (opts.importFaceFX) {
                mdx_scene::Wc3FaceFxBuilder ffxBuilder;
                ffxBuilder.buildFaceFX(irModel, nodeMap, gi, reporter);
            }
        }

        // Vertex colors
        mdx_scene::Wc3VertexColorBuilder vcBuilder;
        vcBuilder.applyVertexColors(irModel, meshNodes, gi, reporter);
    }

    // Close CASC storage (no longer needed after materials + PE2)
    if (cascPtr) {
        mdx_scene::closeCascStorage(cascPtr);
        cascPtr = nullptr;
    }

    // 12. Apply skin modifiers BEFORE animation (MaxScript order)
    ILOG << "\n==== Skinning (before animation) ====\n";
    if (opts.core.importSkinning) {
        for (size_t mi = 0; mi < irModel.meshes.size(); ++mi) {
            if (meshNodes[mi])
                applySkinModifier(meshNodes[mi], irModel.meshes[mi], nodeMap, gi);
        }
    }
    ILOG << "==== end skinning ====\n";

    // 13. Insert animation keyframes (AFTER skinning)
    // 
    // SuspendAnimate()/AnimateOn() is required for SetValue() to create keys.
    // SDK docs: "the animate button should be turned on"
    //
    ILOG << "\n==== Animation Keys ====\n";
    if (opts.core.importAnimations) {
        SuspendAnimate();
        AnimateOn();  // ← CRITICAL: SetValue needs this to create keys

        const Quat stubRot(0.0f, 0.0f, 0.0f, 1.0f);

        // Read local positions from actual Max node transforms after hierarchy
        // setup.  This matches MaxScript's "in coordsys parent subPos = obj.pos"
        // and correctly handles v1200 where PIVT may differ from BPOS.
        std::vector<Point3> nodeLocalPos(irModel.nodes.size(), Point3(0,0,0));
        for (size_t i = 0; i < irModel.nodes.size(); ++i) {
            INode* nd = (i < nodeMap.size()) ? nodeMap[i] : nullptr;
            if (!nd) continue;

            Matrix3 worldTM = nd->GetNodeTM(0);
            INode* par = nd->GetParentNode();
            if (par && !par->IsRootNode()) {
                Matrix3 parentTM = par->GetNodeTM(0);
                Matrix3 localTM = worldTM * Inverse(parentTM);
                nodeLocalPos[i] = localTM.GetTrans();
            } else {
                nodeLocalPos[i] = worldTM.GetTrans();
            }
        }

        for (const auto& na : irModel.nodeAnimations) {
            if (na.nodeIndex < 0 || na.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                continue;
            INode* node = nodeMap[na.nodeIndex];
            if (!node) continue;

            Point3 localPos = (na.nodeIndex < static_cast<int32_t>(nodeLocalPos.size()))
                ? nodeLocalPos[na.nodeIndex] : Point3(0,0,0);

            ILOG << "  ANIM node[" << na.nodeIndex << "]"
                 << " localPos=(" << localPos.x << "," << localPos.y << "," << localPos.z
                 << ") KGTR=" << na.translation.keys.size()
                 << " KGRT=" << na.rotation.keys.size()
                 << " KGSC=" << na.scale.keys.size() << "\n";

            // Rotation: write raw keys (no offset needed)
            if (opts.core.importRotation && !na.rotation.empty())
                insertRotationKeys(node, na.rotation, stubRot);

            // Translation: add localPos to every key value + stub
            if (opts.core.importTranslation && !na.translation.empty()) {
                ir::Vec3Track adjustedTrack = na.translation;
                for (auto& k : adjustedTrack.keys) {
                    k.value += localPos;
                    if (k.hasTangents) {
                        k.inTangent += localPos;
                        k.outTangent += localPos;
                    }
                }
                insertTranslationKeys(node, adjustedTrack, localPos);
            }

            // Scale: write raw keys
            if (opts.core.importScale && !na.scale.empty())
                insertScaleKeys(node, na.scale, Point3(1.0f, 1.0f, 1.0f));
        }

        ResumeAnimate();  // ← restore animate state

        // Visibility animations (uses IKeyControl directly, not SetValue)
        if (opts.core.importVisibility) {
            ILOG << "\n==== Visibility Animations ====\n";

            // Geoset animations: alpha track → mesh node visibility
            for (const auto& ga : irModel.geosetAnims) {
                if (ga.alphaTrackIndex < 0 ||
                    ga.alphaTrackIndex >= static_cast<int32_t>(irModel.floatTracks.size()))
                    continue;
                const auto& track = irModel.floatTracks[ga.alphaTrackIndex];
                if (track.empty()) continue;

                // Find the mesh node for this geoset
                if (ga.meshIndex < 0 ||
                    ga.meshIndex >= static_cast<int32_t>(meshNodes.size()))
                    continue;
                INode* meshNode = meshNodes[ga.meshIndex];
                if (!meshNode) continue;

                ILOG << "  geosetAnim mesh[" << ga.meshIndex << "] '"
                     << narrow(meshNode->GetName()) << "' keys=" << track.keys.size()
                     << " interp=" << static_cast<int>(track.interpolation) << "\n";
                insertVisibilityKeys(meshNode, track, irModel.sequences);
            }

            // Object visibility: lights, attachments, PE1, PE2, ribbons
            auto applyObjVis = [&](int32_t nodeIndex, int32_t trackIndex, const char* type) {
                if (trackIndex < 0 ||
                    trackIndex >= static_cast<int32_t>(irModel.floatTracks.size()))
                    return;
                const auto& track = irModel.floatTracks[trackIndex];
                if (track.empty()) return;
                if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                    return;
                INode* node = nodeMap[nodeIndex];
                if (!node) return;

                ILOG << "  " << type << " node[" << nodeIndex << "] '"
                     << narrow(node->GetName()) << "' keys=" << track.keys.size()
                     << " interp=" << static_cast<int>(track.interpolation) << "\n";
                insertVisibilityKeys(node, track, irModel.sequences);
            };

            for (const auto& light : irModel.lights)
                applyObjVis(light.nodeIndex, light.visibilityTrackIndex, "light");
            for (const auto& att : irModel.attachments)
                applyObjVis(att.nodeIndex, att.visibilityTrackIndex, "attachment");
            for (const auto& pe : irModel.particleEmitters)
                applyObjVis(pe.nodeIndex, pe.visibilityTrackIndex,
                            pe.variant == 2 ? "pe2" : "pe1");
            for (const auto& rib : irModel.ribbonEmitters)
                applyObjVis(rib.nodeIndex, rib.visibilityTrackIndex, "ribbon");

            ILOG << "==== end visibility ====\n";
            ILOG.flush();
        }

        // Parameter animations on plugin objects
        if (opts.core.importParameterAnimations) {
            ILOG << "\n==== Parameter Animations ====\n";

            // Helper: get the first IParamBlock2 on a node's base object
            auto getNodePB = [](INode* node) -> IParamBlock2* {
                if (!node) return nullptr;
                Object* obj = node->GetObjectRef();
                if (!obj) return nullptr;
                auto* ref = dynamic_cast<ReferenceTarget*>(obj);
                if (!ref) return nullptr;
                for (int i = 0; i < ref->NumRefs(); i++) {
                    auto* pb = dynamic_cast<IParamBlock2*>(ref->GetReference(i));
                    if (pb) return pb;
                }
                return nullptr;
            };

            auto getNodeRef = [](INode* node) -> ReferenceTarget* {
                if (!node) return nullptr;
                Object* obj = node->GetObjectRef();
                return obj ? dynamic_cast<ReferenceTarget*>(obj) : nullptr;
            };

            // PE1: speed, emissionRate, lifespan, gravity, latitude, longitude
            for (const auto& pe : irModel.particleEmitters) {
                if (pe.variant != 1) continue;
                if (pe.nodeIndex < 0 || pe.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                    continue;
                IParamBlock2* pb = getNodePB(nodeMap[pe.nodeIndex]);
                if (!pb) continue;
                ILOG << "  PE1 node[" << pe.nodeIndex << "]\n";
                animateFloatPB(pb, P1_PB_SPEED,          pe.speedTrackIndex, irModel);
                animateFloatPB(pb, P1_PB_EMISSION_RATE,  pe.emissionRateTrackIndex, irModel);
                animateFloatPB(pb, P1_PB_LIFE,           pe.lifespanTrackIndex, irModel);
                animateFloatPB(pb, P1_PB_ACCELERATION,   pe.gravityTrackIndex, irModel);
                animateFloatPB(pb, P1_PB_LATITUDE,       pe.latitudeTrackIndex, irModel);
                animateFloatPB(pb, P1_PB_LONGITUDE,      pe.longitudeTrackIndex, irModel);
            }

            // PE2: speed, variation, latitude, gravity, emissionRate, width, length
            for (const auto& pe : irModel.particleEmitters) {
                if (pe.variant != 2) continue;
                if (pe.nodeIndex < 0 || pe.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                    continue;
                IParamBlock2* pb = getNodePB(nodeMap[pe.nodeIndex]);
                if (!pb) continue;
                ILOG << "  PE2 node[" << pe.nodeIndex << "]\n";
                animateFloatPB(pb, P2_PB_SPEED,      pe.speedTrackIndex, irModel);
                animateFloatPB(pb, P2_PB_VARIATION,  pe.variationTrackIndex, irModel);
                animateFloatPB(pb, P2_PB_LATITUDE,   pe.latitudeTrackIndex, irModel);
                animateFloatPB(pb, P2_PB_GRAVITY,    pe.gravityTrackIndex, irModel);
                animateFloatPB(pb, P2_PB_COUNT,      pe.emissionRateTrackIndex, irModel);
                animateFloatPB(pb, P2_PB_WIDTH,      pe.widthTrackIndex, irModel);
                animateFloatPB(pb, P2_PB_HEIGHT,     pe.lengthTrackIndex, irModel);
            }

            // Ribbons: heightAbove, heightBelow, alpha, color, textureSlot
            for (const auto& rib : irModel.ribbonEmitters) {
                if (rib.nodeIndex < 0 || rib.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                    continue;
                IParamBlock2* pb = getNodePB(nodeMap[rib.nodeIndex]);
                if (!pb) continue;
                ILOG << "  Ribbon node[" << rib.nodeIndex << "]\n";
                animateFloatPB(pb, RB_PB_HEIGHT_ABOVE, rib.heightAboveTrackIndex, irModel);
                animateFloatPB(pb, RB_PB_HEIGHT_BELOW, rib.heightBelowTrackIndex, irModel);
                animateFloatPB(pb, RB_PB_ALPHA,        rib.alphaTrackIndex, irModel);
                animateColorPB(pb, RB_PB_COLOR,        rib.colorTrackIndex, irModel);
                animateFloatPB(pb, RB_PB_TEX_SLOT,     rib.textureSlotTrackIndex, irModel);
            }

            // Lights: attStart, attEnd, color, intensity, ambColor, ambIntensity
            for (const auto& light : irModel.lights) {
                if (light.nodeIndex < 0 || light.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                    continue;
                auto* ref = getNodeRef(nodeMap[light.nodeIndex]);
                if (!ref) continue;
                ILOG << "  Light node[" << light.nodeIndex << "]\n";
                animateFloatNamed(ref, L"DecayStart",   light.attStartTrackIndex, irModel);
                animateFloatNamed(ref, L"DecayEnd",     light.attEndTrackIndex, irModel);
                animateColorNamed(ref, L"ShadowColor",  light.colorTrackIndex, irModel);
                animateFloatNamed(ref, L"ShadowValue",  light.intensityTrackIndex, irModel);
                animateColorNamed(ref, L"AmbColor",     light.ambColorTrackIndex, irModel);
                animateFloatNamed(ref, L"AmbValue",     light.ambIntensityTrackIndex, irModel);
            }

            ILOG << "==== end parameter animations ====\n";
            ILOG.flush();
        }

        // POST-ANIMATION VERIFICATION: Check actual bone positions AND rotations at frame 0
        ILOG << "\n==== Post-Animation Verify (frame 0) ====\n";
        for (const auto& bone : irModel.bones) {
            if (bone.nodeIndex < 0 || bone.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                continue;
            INode* boneNode = nodeMap[bone.nodeIndex];
            if (!boneNode) continue;

            Matrix3 wTM = boneNode->GetNodeTM(0);
            Point3 wp = wTM.GetTrans();
            Point3 expectedWP = bone.pivotPoint;

            // Extract rotation from world TM
            Matrix3 rotOnly = wTM;
            rotOnly.NoTrans();  // remove translation, keep rotation+scale
            Point3 r0 = rotOnly.GetRow(0);
            Point3 r1 = rotOnly.GetRow(1);
            Point3 r2 = rotOnly.GetRow(2);

            // Also get local TM rotation
            Matrix3 localTM;
            localTM.IdentityMatrix();
            INode* par = boneNode->GetParentNode();
            if (par && !par->IsRootNode()) {
                Matrix3 pTM = par->GetNodeTM(0);
                localTM = wTM * Inverse(pTM);
            } else {
                localTM = wTM;
            }
            Matrix3 localRot = localTM;
            localRot.NoTrans();
            Point3 lr0 = localRot.GetRow(0);
            Point3 lr1 = localRot.GetRow(1);
            Point3 lr2 = localRot.GetRow(2);

            // Check if rotation is identity
            bool rotIsIdentity = (fabs(lr0.x-1)<0.01f && fabs(lr0.y)<0.01f && fabs(lr0.z)<0.01f
                               && fabs(lr1.x)<0.01f && fabs(lr1.y-1)<0.01f && fabs(lr1.z)<0.01f
                               && fabs(lr2.x)<0.01f && fabs(lr2.y)<0.01f && fabs(lr2.z-1)<0.01f);

            Point3 diff = wp - expectedWP;
            float err = Length(diff);

            ILOG << "  bone[" << bone.nodeIndex << "] '" << bone.name
                 << "' pos=(" << wp.x << "," << wp.y << "," << wp.z
                 << ") exp=(" << expectedWP.x << "," << expectedWP.y << "," << expectedWP.z
                 << ") err=" << err;

            if (!rotIsIdentity) {
                ILOG << " ROT=[(" << lr0.x << "," << lr0.y << "," << lr0.z
                     << ")(" << lr1.x << "," << lr1.y << "," << lr1.z
                     << ")(" << lr2.x << "," << lr2.y << "," << lr2.z << ")]";
            }
            ILOG << (err > 0.1f ? " *** BAD ***" : " OK") << "\n";
        }
        ILOG.flush();
    }
    ILOG << "==== end animation ====\n";
    ILOG.flush();

    // 14. Build sequences (Note Track entries)
    {
        mdx_scene::Wc3SequenceBuilder seqBuilder;
        seqBuilder.buildSequences(irModel, gi, reporter);
    }

    // 15. Report
    if (reporter.hasWarnings() || reporter.hasErrors()) {
        reporter.showSummaryDialog(gi->GetMAXHWnd());
    }

    gi->ForceCompleteRedraw();

    ILOG << "\n==== Import Complete ====\n";
    ILOG.flush();

    return IMPEXP_SUCCESS;
}
