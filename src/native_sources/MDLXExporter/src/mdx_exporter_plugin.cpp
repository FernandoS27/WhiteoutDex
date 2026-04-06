// MDLXExporter — MdxExporterPlugin implementation
#include "mdx_exporter_plugin.h"
#include "mdx_export_options.h"
#include "mdx_class_ids.h"
#include "mdx_node_registration.h"
#include "export_dialog.h"

// Core (MaxCore)
#include <scene/node_classifier.h>
#include <scene/scene_traversal.h>
#include <extraction/bone_extractor.h>
#include <extraction/mesh_extractor.h>
#include <extraction/modifier_reader.h>
#include <animation/anim_dispatcher.h>
#include <optimization/vertex_optimizer.h>
#include <optimization/keyframe_optimizer.h>
#include <optimization/bone_optimizer.h>
#include <util/error_reporter.h>

// Format extractors
#include "extraction/wc3_material_extractor.h"
#include "extraction/wc3_light_extractor.h"
#include "extraction/wc3_attachment_extractor.h"
#include "extraction/wc3_particle1_extractor.h"
#include "extraction/wc3_particle2_extractor.h"
#include "extraction/wc3_ribbon_extractor.h"
#include "extraction/wc3_event_extractor.h"
#include "extraction/wc3_collision_extractor.h"
#include "extraction/wc3_popcorn_extractor.h"
#include "extraction/wc3_facefx_extractor.h"
#include "extraction/wc3_vertex_color_extractor.h"

// Assembly
#include "assembly/mdx_sequence_manager.h"
#include "assembly/mdx_model_builder.h"

// WhiteoutLib
#include <whiteout/models/mdx/writer.h>

#include <MaxDirectories.h>
#include <locale>
#include <codecvt>

extern HINSTANCE GetDllInstance();

int MdxExporterPlugin::ExtCount() { return 2; }

const TCHAR* MdxExporterPlugin::Ext(int n) {
    switch (n) {
    case 0: return _T("mdx");
    case 1: return _T("mdl");
    default: return _T("");
    }
}

const TCHAR* MdxExporterPlugin::LongDesc() { return _T("Warcraft III MDX/MDL Model"); }
const TCHAR* MdxExporterPlugin::ShortDesc() { return _T("MDX/MDL Export"); }
const TCHAR* MdxExporterPlugin::AuthorName() { return _T("WhiteoutDex"); }
const TCHAR* MdxExporterPlugin::CopyrightMessage() { return _T(""); }
const TCHAR* MdxExporterPlugin::OtherMessage1() { return _T(""); }
const TCHAR* MdxExporterPlugin::OtherMessage2() { return _T(""); }
unsigned int MdxExporterPlugin::Version() { return 100; }
void MdxExporterPlugin::ShowAbout(HWND /*hWnd*/) {}

BOOL MdxExporterPlugin::SupportsOptions(int /*ext*/, DWORD /*options*/) {
    return TRUE;
}

namespace {

std::string wcharToUtf8(const wchar_t* wstr) {
    if (!wstr || !wstr[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr, nullptr);
    return result;
}

// ── INI settings loader ─────────────────────────────────────

// Win32 GetPrivateProfileStringW — use function pointer to avoid
// ambiguity with MaxSDK::Util::GetPrivateProfileString wrapper.
static auto Win32_GetPrivateProfileStringW =
    static_cast<DWORD(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD, LPCWSTR)>(
        &::GetPrivateProfileStringW);

std::wstring iniGetString(const wchar_t* path, const wchar_t* section,
                          const wchar_t* key, const wchar_t* def = L"")
{
    wchar_t buf[512];
    Win32_GetPrivateProfileStringW(section, key, def, buf, 512, path);
    return buf;
}

bool iniBool(const std::wstring& val) {
    return val == L"true" || val == L"True" || val == L"1";
}

void loadOptionsFromINI(Interface* gi, MdxExportOptions& opts) {
    MSTR dir = gi->GetDir(APP_PLUGCFG_DIR);
    std::wstring iniPath = std::wstring(dir.data()) + L"\\MDLXExporter.ini";

    if (GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;

    // [Settings] section
    auto modelName = iniGetString(iniPath.c_str(), L"Settings", L"ModelName");
    if (!modelName.empty())
        opts.modelName = wcharToUtf8(modelName.c_str());

    auto ver = iniGetString(iniPath.c_str(), L"Settings", L"ExportVersion");
    if (!ver.empty()) {
        int v = _wtoi(ver.c_str());
        opts.version = (v == 2 || v == 1200) ? 1200 : 800;
    }

    auto merge = iniGetString(iniPath.c_str(), L"Settings", L"MergeSimilarMeshes");
    if (!merge.empty()) opts.mergeGeosets = iniBool(merge);

    auto fixN = iniGetString(iniPath.c_str(), L"Settings", L"FixNormals");
    if (!fixN.empty()) opts.fixNormals = iniBool(fixN);

    auto fixSN = iniGetString(iniPath.c_str(), L"Settings", L"FixSharedNormals");
    if (!fixSN.empty()) opts.fixSharedNormals = iniBool(fixSN);

    auto thresh = iniGetString(iniPath.c_str(), L"Settings", L"Threshold");
    if (!thresh.empty()) opts.fixNormalsThreshold = static_cast<float>(_wtof(thresh.c_str()));

    auto smooth = iniGetString(iniPath.c_str(), L"Settings", L"ExportSmoothgroups");
    if (!smooth.empty()) opts.exportSmoothgroups = iniBool(smooth);

    auto keepBH = iniGetString(iniPath.c_str(), L"Settings", L"KeepUnusedBonesHelpers");
    if (!keepBH.empty()) opts.keepUnusedBonesHelpers = iniBool(keepBH);

    auto noQuant = iniGetString(iniPath.c_str(), L"Settings", L"DisableSkinQuantize");
    if (!noQuant.empty()) opts.disableSkinQuantize = iniBool(noQuant);

    auto extType = iniGetString(iniPath.c_str(), L"Settings", L"ExtentsType");
    if (!extType.empty()) opts.extentsType = _wtoi(extType.c_str());

    auto extPrec = iniGetString(iniPath.c_str(), L"Settings", L"ExtentsPrecision");
    if (!extPrec.empty()) opts.extentsPrecision = _wtoi(extPrec.c_str());
}

void collectSkinBoneNodes(const std::vector<core::SceneNode>& sceneNodes,
                          std::vector<INode*>& out)
{
    for (auto& sn : sceneNodes) {
        if (sn.category == core::NodeCategory::Mesh && sn.maxNode) {
            auto* skin = core::ModifierReader::findSkin(sn.maxNode);
            if (!skin) continue;
            auto* ctx = skin->GetContextInterface(sn.maxNode);
            if (!ctx) continue;
            for (int i = 0; i < skin->GetNumBones(); i++) {
                INode* bone = skin->GetBone(i);
                if (bone) out.push_back(bone);
            }
        }
    }
}

void resolveSkinIndices(INode* meshNode, ir::Mesh& mesh,
                        const std::unordered_map<INode*, int32_t>& nodeToIndex,
                        const std::vector<ir::Bone>& bones)
{
    ISkin* skin = core::ModifierReader::findSkin(meshNode);
    if (!skin) return;

    int numBones = skin->GetNumBones();

    // Build lookup: ISkin bone index → ir::Bone::nodeIndex (= IR node index)
    std::vector<int32_t> skinToNodeIndex(numBones, -1);
    for (int i = 0; i < numBones; ++i) {
        INode* boneNode = skin->GetBone(i);
        if (!boneNode) continue;
        auto it = nodeToIndex.find(boneNode);
        if (it != nodeToIndex.end()) {
            int32_t boneIdx = it->second;
            if (boneIdx >= 0 && boneIdx < static_cast<int32_t>(bones.size()))
                skinToNodeIndex[i] = bones[boneIdx].nodeIndex;
        }
    }

    // Remap every skin influence from ISkin index → IR node index
    for (auto& v : mesh.vertices) {
        for (auto& inf : v.skinInfluences) {
            int rawIdx = inf.boneIndex;
            if (rawIdx >= 0 && rawIdx < numBones)
                inf.boneIndex = skinToNodeIndex[rawIdx];
            else
                inf.boneIndex = -1;
        }
    }
}

} // namespace

int MdxExporterPlugin::DoExport(const TCHAR* name, ExpInterface* /*ei*/,
                                 Interface* gi, BOOL suppressPrompts, DWORD /*options*/)
{
    core::ExportErrorReporter reporter;
    MdxExportOptions opts;

    // 1. Load settings from INI (written by the macroscript UI)
    loadOptionsFromINI(gi, opts);

    // 2. Show dialog if prompts are not suppressed
    if (!suppressPrompts) {
        if (!showExportDialog(GetDllInstance(), gi->GetMAXHWnd(), opts))
            return IMPEXP_CANCEL;
    }

    // 2. Register MDX node types
    core::NodeClassifier classifier;
    registerMdxNodeTypes(classifier);

    // 3. Traverse scene
    core::SceneTraversal traversal;
    auto sceneResult = traversal.traverse(gi, classifier);

    // 4. Build IR model skeleton
    ir::IRModel irModel;
    irModel.nodes = std::move(sceneResult.irNodes);

    // 5. Extract bones
    std::vector<INode*> skinBoneNodes;
    collectSkinBoneNodes(sceneResult.nodes, skinBoneNodes);
    core::BoneExtractor boneEx;
    auto boneResult = boneEx.extract(sceneResult.nodes, skinBoneNodes, reporter);
    irModel.bones = std::move(boneResult.bones);

    // 5b. Create IR nodes for implicit/ancestor bones missing from scene traversal
    //     (Skin-referenced bones and hierarchy ancestors may not have been visited
    //     by SceneTraversal, so they lack an IR node entry. Without one the
    //     hierarchy resolver maps them all to the same objectId and their pivot
    //     points / names are lost.)
    for (size_t bi = 0; bi < irModel.bones.size(); ++bi) {
        auto& bone = irModel.bones[bi];
        if (bone.nodeIndex >= 0) continue; // already has an IR node

        int32_t newIdx = static_cast<int32_t>(irModel.nodes.size());
        ir::IRModel::Node irn;
        irn.name = bone.name;
        irn.pivotPoint = bone.pivotPoint;
        irn.worldTM = bone.bindPose;
        // Recover the INode* so animations and other queries can reach it
        for (auto& [node, boneIdx] : boneResult.nodeToIndex) {
            if (boneIdx == static_cast<int32_t>(bi)) {
                irn.maxNode = node;
                break;
            }
        }
        irModel.nodes.push_back(std::move(irn));
        bone.nodeIndex = newIdx;
    }
    // Resolve parentIndex for the newly created IR nodes
    for (auto& bone : irModel.bones) {
        if (bone.nodeIndex < 0) continue;
        auto& irn = irModel.nodes[bone.nodeIndex];
        if (irn.parentIndex >= 0) continue; // already resolved by traversal
        if (bone.parentIndex >= 0 &&
            bone.parentIndex < static_cast<int32_t>(irModel.bones.size()))
        {
            irn.parentIndex = irModel.bones[bone.parentIndex].nodeIndex;
        }
    }

    // 6. Extract meshes
    core::MeshExtractor meshEx;
    for (auto& sn : sceneResult.nodes) {
        if (sn.category == core::NodeCategory::Mesh) {
            auto mesh = meshEx.extract(sn.maxNode, sn.nodeIndex, 0, reporter);

            ISkin* skin = core::ModifierReader::findSkin(sn.maxNode);
            if (skin) {
                resolveSkinIndices(sn.maxNode, mesh, boneResult.nodeToIndex, irModel.bones);
            } else {
                // Unskinned mesh: create a synthetic bone named after the geoset,
                // parented to whatever the mesh was linked to, and skin all verts to it.
                int32_t newNodeIdx = static_cast<int32_t>(irModel.nodes.size());

                ir::IRModel::Node irn;
                irn.name = "Mesh " + mesh.name;
                irn.maxNode = sn.maxNode;
                irn.worldTM = sn.maxNode->GetNodeTM(0);
                irn.pivotPoint = irn.worldTM.GetTrans();

                // Parent the synthetic bone to the mesh node's parent
                INode* parentMax = sn.maxNode->GetParentNode();
                if (parentMax && !parentMax->IsRootNode()) {
                    for (int32_t ni = 0; ni < static_cast<int32_t>(irModel.nodes.size()); ++ni) {
                        if (irModel.nodes[ni].maxNode == parentMax) {
                            irn.parentIndex = ni;
                            break;
                        }
                    }
                }

                irModel.nodes.push_back(std::move(irn));

                ir::Bone synBone;
                synBone.name = "Mesh " + mesh.name;
                synBone.nodeIndex = newNodeIdx;
                synBone.pivotPoint = irModel.nodes[newNodeIdx].pivotPoint;
                synBone.bindPose = irModel.nodes[newNodeIdx].worldTM;
                synBone.isHelper = false;
                irModel.bones.push_back(std::move(synBone));

                // Skin every vertex entirely to the synthetic bone
                for (auto& v : mesh.vertices) {
                    v.skinInfluences.clear();
                    v.skinInfluences.push_back({newNodeIdx, 1.0f});
                }
            }

            irModel.meshes.push_back(std::move(mesh));
        }
    }

    // 7. Extract MDX-specific: materials, lights, attachments, particles, etc.
    auto mtlMap = mdx_extract::extractMaterials(sceneResult.nodes, irModel, reporter);
    mdx_extract::extractLights(sceneResult.nodes, irModel, reporter);
    mdx_extract::extractAttachments(sceneResult.nodes, irModel, reporter);
    mdx_extract::extractParticles1(sceneResult.nodes, irModel, reporter);
    mdx_extract::extractParticles2(sceneResult.nodes, irModel, reporter);
    mdx_extract::extractRibbons(sceneResult.nodes, irModel, mtlMap, reporter);
    mdx_extract::extractEvents(sceneResult.nodes, irModel, reporter);
    mdx_extract::extractCollisions(sceneResult.nodes, irModel, reporter);
    mdx_extract::extractVertexColors(sceneResult.nodes, irModel, reporter);

    if (opts.version >= 1200) {
        mdx_extract::extractPopcorn(sceneResult.nodes, irModel, reporter);
        mdx_extract::extractFaceFX(sceneResult.nodes, irModel, reporter);
    }

    // 8. Extract sequences
    MdxSequenceManager seqMgr;
    irModel.sequences = seqMgr.extractSequences(gi);

    // 9. Bake animations
    if (opts.exportAnimations && !irModel.sequences.empty()) {
        core::AnimDispatcher dispatcher;
        core::AnimDispatcher::Config animCfg;
        animCfg.tickInterval = opts.animSampleInterval;
        animCfg.angleThreshold = opts.ikRefinementThreshold;
        dispatcher.bakeAll(irModel, irModel.sequences, animCfg, reporter);
    }

    // 10. Optimize
    if (opts.optimizeVertices) {
        core::VertexOptimizer vopt;
        for (auto& mesh : irModel.meshes)
            vopt.optimize(mesh, opts.vertexMergeThreshold);
    }
    if (opts.optimizeKeyframes) {
        core::KeyframeOptimizer kopt;
        for (auto& na : irModel.nodeAnimations) {
            kopt.optimize(na.translation);
            kopt.optimize(na.rotation);
            kopt.optimize(na.scale);
        }
        for (auto& ft : irModel.floatTracks) kopt.optimize(ft);
        for (auto& vt : irModel.vec3Tracks) kopt.optimize(vt);
    }

    // 11. Assemble MDX model
    MdxModelBuilder builder;
    whiteout::mdx::Model mdxModel = builder.build(irModel, opts);

    // 12. Set model name (from INI, or fall back to filename stem)
    std::string filePath = wcharToUtf8(name);
    if (!opts.modelName.empty()) {
        mdxModel.modelName = opts.modelName;
    } else {
        auto slash = filePath.find_last_of("/\\");
        auto stem = (slash != std::string::npos) ? filePath.substr(slash + 1) : filePath;
        auto dot = stem.rfind('.');
        if (dot != std::string::npos) stem = stem.substr(0, dot);
        mdxModel.modelName = stem;
    }

    // 13. Write
    whiteout::mdx::Writer writer;
    writer.write(filePath, mdxModel);

    // 14. Report
    if (reporter.hasWarnings() || reporter.hasErrors()) {
        reporter.showSummaryDialog(gi->GetMAXHWnd());
    }

    return IMPEXP_SUCCESS;
}
