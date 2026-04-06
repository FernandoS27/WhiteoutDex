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
#include <string>
#include <vector>
#include <set>
#include <unordered_map>

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

    // MaxScript: obj.pos.controller = typeToController smoothType #position
    // Linear → linear_position; Bezier → bezier_position; Hermite → tcb_position
    Control* posCtrl = nullptr;
    switch (track.interpolation) {
    case ir::InterpolationType::Linear:
        posCtrl = static_cast<Control*>(
            CreateInstance(CTRL_POSITION_CLASS_ID, Class_ID(LININTERP_POSITION_CLASS_ID, 0)));
        break;
    case ir::InterpolationType::Hermite:
        posCtrl = static_cast<Control*>(
            CreateInstance(CTRL_POSITION_CLASS_ID, Class_ID(TCBINTERP_POSITION_CLASS_ID, 0)));
        break;
    default: // Bezier
        posCtrl = static_cast<Control*>(
            CreateInstance(CTRL_POSITION_CLASS_ID, Class_ID(HYBRIDINTERP_POSITION_CLASS_ID, 0)));
        break;
    }
    if (!posCtrl) return;
    tmCtrl->SetPositionController(posCtrl);

    IKeyControl* kc = GetKeyControlInterface(posCtrl);
    if (!kc) return;

    float bezConst = getBezierConstant();
    int numKeys = static_cast<int>(track.keys.size());

    // Insert bind-pose local position at frame 0 so the skeleton displays
    // correctly before the first animation key (frame 10+).
    bool needStub = (numKeys == 0 || track.keys[0].time > 0);
    if (needStub) {
        if (track.interpolation == ir::InterpolationType::Linear) {
            ILinPoint3Key k0; k0.time = 0; k0.val = stubVal;
            kc->AppendKey(&k0);
        } else if (track.interpolation == ir::InterpolationType::Bezier) {
            IBezPoint3Key k0; memset(&k0, 0, sizeof(k0));
            k0.time = 0; k0.val = stubVal;
            k0.flags = (BEZKEY_USER << BEZKEY_INTYPESHIFT) | (BEZKEY_USER << BEZKEY_OUTTYPESHIFT);
            k0.inLength = Point3(0.333333f, 0.333333f, 0.333333f);
            k0.outLength = Point3(0.333333f, 0.333333f, 0.333333f);
            kc->AppendKey(&k0);
        } else {
            ITCBPoint3Key k0; memset(&k0, 0, sizeof(k0));
            k0.time = 0; k0.val = stubVal;
            kc->AppendKey(&k0);
        }
    }

    for (int i = 0; i < numKeys; ++i) {
        const auto& key = track.keys[i];
        Point3 val = key.value;

        if (track.interpolation == ir::InterpolationType::Linear) {
            ILinPoint3Key linKey;
            linKey.time = key.time;
            linKey.val = val;
            kc->AppendKey(&linKey);
        } else if (track.interpolation == ir::InterpolationType::Bezier) {
            IBezPoint3Key bezKey;
            memset(&bezKey, 0, sizeof(bezKey));
            bezKey.time = key.time;
            bezKey.val = val;

            // MaxScript bezier tangent conversion:
            // key.inTangent = bezierConstant * (k.intan - k.value) / delta
            // key.outTangent = bezierConstant * (k.outtan - k.value) / delta
            // key.inTangentLength = key.outTangentLength = [0.333,0.333,0.333]
            if (key.hasTangents) {
                Point3 inOff = key.inTangent;
                Point3 outOff = key.outTangent;

                if (i > 0) {
                    float delta = static_cast<float>(key.time - track.keys[i - 1].time);
                    if (delta != 0.0f)
                        bezKey.intan = bezConst * (inOff - val) / delta;
                }
                if (i < numKeys - 1) {
                    float delta = static_cast<float>(track.keys[i + 1].time - key.time);
                    if (delta != 0.0f)
                        bezKey.outtan = bezConst * (outOff - val) / delta;
                }
            }
            bezKey.flags = (BEZKEY_USER << BEZKEY_INTYPESHIFT) | (BEZKEY_USER << BEZKEY_OUTTYPESHIFT);
            bezKey.inLength = Point3(0.333333f, 0.333333f, 0.333333f);
            bezKey.outLength = Point3(0.333333f, 0.333333f, 0.333333f);
            kc->AppendKey(&bezKey);
        } else {
            // Hermite / TCB — write value only, tangents handled by TCB controller
            ITCBPoint3Key tcbKey;
            memset(&tcbKey, 0, sizeof(tcbKey));
            tcbKey.time = key.time;
            tcbKey.val = val;
            tcbKey.tens = 0.0f;
            tcbKey.cont = 0.0f;
            tcbKey.bias = 0.0f;
            tcbKey.easeIn = 0.0f;
            tcbKey.easeOut = 0.0f;
            kc->AppendKey(&tcbKey);
        }
    }
    kc->SortKeys();

    // MaxScript: global sequences get ORT cycle
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

    // MaxScript: obj.rotation.controller = linear_rotation()
    // Always use linear rotation regardless of interpolation type.
    // Hermite/Bezier rotation objects get post-processed to TCB later.
    Control* rotCtrl = static_cast<Control*>(
        CreateInstance(CTRL_ROTATION_CLASS_ID, Class_ID(LININTERP_ROTATION_CLASS_ID, 0)));
    if (!rotCtrl) return;
    tmCtrl->SetRotationController(rotCtrl);

    IKeyControl* kc = GetKeyControlInterface(rotCtrl);
    if (!kc) return;

    int numRotKeys = static_cast<int>(track.keys.size());

    // Insert bind-pose rotation at frame 0.
    bool needRotStub = (numRotKeys == 0 || track.keys[0].time > 0);

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

    if (needRotStub) {
        ILinRotKey k0;
        k0.time = 0;
        k0.val = stubVal;
        kc->AppendKey(&k0);
    }

    // MaxScript: writeKeys rotData.keys controller #Linear
    // Rotation keys are written as linear keys (value only, no tangents)
    for (int i = 0; i < numRotKeys; ++i) {
        const auto& key = track.keys[i];
        ILinRotKey linKey;
        linKey.time = key.time;
        linKey.val = fixedKeys[i];
        kc->AppendKey(&linKey);
    }
    kc->SortKeys();

    // MaxScript: global sequences get ORT cycle
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

    // MaxScript: obj.scale.controller = typeToController smoothType #scale
    Control* scaleCtrl = nullptr;
    switch (track.interpolation) {
    case ir::InterpolationType::Linear:
        scaleCtrl = static_cast<Control*>(
            CreateInstance(CTRL_SCALE_CLASS_ID, Class_ID(LININTERP_SCALE_CLASS_ID, 0)));
        break;
    case ir::InterpolationType::Hermite:
        scaleCtrl = static_cast<Control*>(
            CreateInstance(CTRL_SCALE_CLASS_ID, Class_ID(TCBINTERP_SCALE_CLASS_ID, 0)));
        break;
    default: // Bezier
        scaleCtrl = static_cast<Control*>(
            CreateInstance(CTRL_SCALE_CLASS_ID, Class_ID(HYBRIDINTERP_SCALE_CLASS_ID, 0)));
        break;
    }
    if (!scaleCtrl) return;
    tmCtrl->SetScaleController(scaleCtrl);

    IKeyControl* kc = GetKeyControlInterface(scaleCtrl);
    if (!kc) return;

    // Insert bind-pose scale at frame 0.
    float bezConst = getBezierConstant();
    int numKeys = static_cast<int>(track.keys.size());

    bool needStub = (numKeys == 0 || track.keys[0].time > 0);
    if (needStub) {
        if (track.interpolation == ir::InterpolationType::Linear) {
            ILinScaleKey k0; k0.time = 0; k0.val = ScaleValue(stubVal);
            kc->AppendKey(&k0);
        } else if (track.interpolation == ir::InterpolationType::Bezier) {
            IBezScaleKey k0; memset(&k0, 0, sizeof(k0));
            k0.time = 0; k0.val = ScaleValue(stubVal);
            k0.flags = (BEZKEY_USER << BEZKEY_INTYPESHIFT) | (BEZKEY_USER << BEZKEY_OUTTYPESHIFT);
            k0.inLength = Point3(0.333333f, 0.333333f, 0.333333f);
            k0.outLength = Point3(0.333333f, 0.333333f, 0.333333f);
            kc->AppendKey(&k0);
        } else {
            ITCBScaleKey k0; memset(&k0, 0, sizeof(k0));
            k0.time = 0; k0.val = ScaleValue(stubVal);
            kc->AppendKey(&k0);
        }
    }

    for (int i = 0; i < numKeys; ++i) {
        const auto& key = track.keys[i];
        Point3 val = key.value;

        if (track.interpolation == ir::InterpolationType::Linear) {
            ILinScaleKey linKey;
            linKey.time = key.time;
            linKey.val = ScaleValue(val);
            kc->AppendKey(&linKey);
        } else if (track.interpolation == ir::InterpolationType::Bezier) {
            IBezScaleKey bezKey;
            memset(&bezKey, 0, sizeof(bezKey));
            bezKey.time = key.time;
            bezKey.val = ScaleValue(val);

            if (key.hasTangents) {
                Point3 inOff = key.inTangent;
                Point3 outOff = key.outTangent;

                if (i > 0) {
                    float delta = static_cast<float>(key.time - track.keys[i - 1].time);
                    if (delta != 0.0f)
                        bezKey.intan = bezConst * (inOff - val) / delta;
                }
                if (i < numKeys - 1) {
                    float delta = static_cast<float>(track.keys[i + 1].time - key.time);
                    if (delta != 0.0f)
                        bezKey.outtan = bezConst * (outOff - val) / delta;
                }
            }
            bezKey.flags = (BEZKEY_USER << BEZKEY_INTYPESHIFT) | (BEZKEY_USER << BEZKEY_OUTTYPESHIFT);
            bezKey.inLength = Point3(0.333333f, 0.333333f, 0.333333f);
            bezKey.outLength = Point3(0.333333f, 0.333333f, 0.333333f);
            kc->AppendKey(&bezKey);
        } else {
            // Hermite / TCB
            ITCBScaleKey tcbKey;
            memset(&tcbKey, 0, sizeof(tcbKey));
            tcbKey.time = key.time;
            tcbKey.val = ScaleValue(val);
            tcbKey.tens = 0.0f;
            tcbKey.cont = 0.0f;
            tcbKey.bias = 0.0f;
            tcbKey.easeIn = 0.0f;
            tcbKey.easeOut = 0.0f;
            kc->AppendKey(&tcbKey);
        }
    }
    kc->SortKeys();

    // MaxScript: global sequences get ORT cycle
    if (track.globalSequenceIndex >= 0) {
        scaleCtrl->SetORT(ORT_CYCLE, ORT_AFTER);
        scaleCtrl->EnableORTs(TRUE);
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

            Matrix3 tm = bone.bindPose;
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

    // 10. Build materials and assign to meshes
    std::vector<Mtl*> materials;
    if (opts.core.importMaterials) {
        // Extract model directory for texture path resolution
        std::wstring modelDir;
        {
            std::wstring fullPath(name);
            auto lastSep = fullPath.find_last_of(L"\\/");
            if (lastSep != std::wstring::npos)
                modelDir = fullPath.substr(0, lastSep + 1);
        }

        mdx_scene::Wc3MaterialBuilder matBuilder;
        materials = matBuilder.buildMaterials(irModel, opts.core.importTextures, modelDir, gi, reporter);

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
            pe2Builder.buildParticles(irModel, nodeMap, gi, reporter);
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

    // 12. Insert animation keyframes
    // MaxScript order: Rotation → Translation → Scale (per node)
    //
    // Frame-0 stubs use fixed identity values matching MaxScript's
    // preProcessKeys defaults: [0,0,0] position, identity rotation,
    // [1,1,1] scale.  MDX animation keys are parent-relative absolute
    // values, so [0,0,0] means "at parent origin".
    if (opts.core.importAnimations) {
        const Point3 stubPos(0.0f, 0.0f, 0.0f);
        const Quat   stubRot(0.0f, 0.0f, 0.0f, 1.0f);
        const Point3 stubScale(1.0f, 1.0f, 1.0f);

        for (const auto& na : irModel.nodeAnimations) {
            if (na.nodeIndex < 0 || na.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                continue;
            INode* node = nodeMap[na.nodeIndex];
            if (!node) continue;

            if (opts.core.importRotation && !na.rotation.empty())
                insertRotationKeys(node, na.rotation, stubRot);
            if (opts.core.importTranslation && !na.translation.empty())
                insertTranslationKeys(node, na.translation, stubPos);
            if (opts.core.importScale && !na.scale.empty())
                insertScaleKeys(node, na.scale, stubScale);
        }
    }

    // 12b. Apply skin modifiers (AFTER animation so AddBoneEx captures
    //      the controller-defined bone TMs, not the pre-animation TMs).
    if (opts.core.importSkinning) {
        for (size_t mi = 0; mi < irModel.meshes.size(); ++mi) {
            if (meshNodes[mi])
                applySkinModifier(meshNodes[mi], irModel.meshes[mi], nodeMap, gi);
        }
    }

    // 13. Build sequences (Note Track entries)
    {
        mdx_scene::Wc3SequenceBuilder seqBuilder;
        seqBuilder.buildSequences(irModel, gi, reporter);
    }

    // 14. Report
    if (reporter.hasWarnings() || reporter.hasErrors()) {
        reporter.showSummaryDialog(gi->GetMAXHWnd());
    }

    gi->ForceCompleteRedraw();

    return IMPEXP_SUCCESS;
}
