// MDLXExporter — MdxExporterPlugin implementation
// DEBUG: Logs to %TEMP%\mdlx_export_debug.log
#include "mdx_exporter_plugin.h"
#include <wdx_text.h>
#include "mdx_export_options.h"
#include "mdx_class_ids.h"
#include "mdx_node_registration.h"
#include "export_dialog.h"
#include "export_paths.h"
#include "mdx_export_debug.h"

#include <scene/node_classifier.h>
#include <scene/scene_traversal.h>
#include <extraction/bone_extractor.h>
#include <extraction/mesh_extractor.h>
#include <extraction/modifier_reader.h>
#include <animation/anim_dispatcher.h>
#include <optimization/vertex_optimizer.h>
#include <optimization/keyframe_optimizer.h>
#include <optimization/bone_optimizer.h>
#include <util/max_helpers.h>

#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <decomp.h>
#include <util/error_reporter.h>

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
#include "extraction/geoset_anim_extractor.h"
#include "extraction/camera_extractor_wrapper.h"

#include "assembly/mdx_sequence_manager.h"
#include "assembly/mdx_model_builder.h"

#include "texture/texture_export.h"

#include <whiteout/models/mdx/writer.h>
#include <MaxDirectories.h>
#include <shellapi.h>
#include <cmath>
#include <cctype>

extern HINSTANCE GetDllInstance();

int MdxExporterPlugin::ExtCount() { return 2; }
const TCHAR* MdxExporterPlugin::Ext(int n) { switch(n){case 0:return _T("mdx");case 1:return _T("mdl");default:return _T("");} }
const TCHAR* MdxExporterPlugin::LongDesc() { return _T("Warcraft III MDX/MDL Model"); }
const TCHAR* MdxExporterPlugin::ShortDesc() { return _T("MDX/MDL Export"); }
const TCHAR* MdxExporterPlugin::AuthorName() { return _T("WhiteoutDex"); }
const TCHAR* MdxExporterPlugin::CopyrightMessage() { return _T(""); }
const TCHAR* MdxExporterPlugin::OtherMessage1() { return _T(""); }
const TCHAR* MdxExporterPlugin::OtherMessage2() { return _T(""); }
unsigned int MdxExporterPlugin::Version() { return 100; }
void MdxExporterPlugin::ShowAbout(HWND) {}
BOOL MdxExporterPlugin::SupportsOptions(int,DWORD) { return TRUE; }

namespace {

std::string wcharToUtf8(const wchar_t* wstr) {
    if (!wstr || !wstr[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8,0,wstr,-1,nullptr,0,nullptr,nullptr);
    if (len<=0) return {};
    std::string result(static_cast<size_t>(len-1),'\0');
    WideCharToMultiByte(CP_UTF8,0,wstr,-1,result.data(),len,nullptr,nullptr);
    return result;
}

static auto Win32_GetPrivateProfileStringW =
    static_cast<DWORD(WINAPI*)(LPCWSTR,LPCWSTR,LPCWSTR,LPWSTR,DWORD,LPCWSTR)>(&::GetPrivateProfileStringW);

std::wstring iniGetString(const wchar_t* path,const wchar_t* section,const wchar_t* key,const wchar_t* def=L"") {
    wchar_t buf[512]; Win32_GetPrivateProfileStringW(section,key,def,buf,512,path); return buf;
}
bool iniBool(const std::wstring& val) { return val==L"true"||val==L"True"||val==L"1"; }

void loadOptionsFromINI(Interface* gi, MdxExportOptions& opts) {
    MSTR dir = gi->GetDir(APP_PLUGCFG_DIR);
    std::wstring iniPath = std::wstring(dir.data()) + L"\\MDLXExporter.ini";
    if (GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    auto gs = [&](const wchar_t* k){ return iniGetString(iniPath.c_str(),L"Settings",k); };
    // ModelName is deliberately NOT restored here. It is a per-scene value but
    // the INI is per-user, so seeding it stamped the last exported model's name
    // onto every later export — including headless ones, which never see the
    // dialog. The dialog still prefills its field from the INI for an unsaved
    // scene; a saved scene's name wins over both (see DoExport).
    auto ver=gs(L"ExportVersion"); if(!ver.empty()){int v=_wtoi(ver.c_str()); opts.version=(v==2||v>=1200)?1800:800;}
    auto m=gs(L"MergeSimilarMeshes"); if(!m.empty()) opts.mergeGeosets=iniBool(m);
    auto fsn=gs(L"FixSharedNormals"); if(!fsn.empty()) opts.fixSharedNormals=iniBool(fsn);
    auto kb=gs(L"KeepUnusedBonesHelpers"); if(!kb.empty()) opts.keepUnusedBonesHelpers=iniBool(kb);
    auto nq=gs(L"DisableSkinQuantize"); if(!nq.empty()) opts.disableSkinQuantize=iniBool(nq);
    auto ai=gs(L"AutoIncrementFilename"); if(!ai.empty()) opts.autoIncrementFilename=iniBool(ai);
    auto of=gs(L"OpenFolderAfterExport"); if(!of.empty()) opts.openFolderAfterExport=iniBool(of);
    // Texture conversion (shared)
    auto tc=gs(L"TexConvertEnabled"); if(!tc.empty()) opts.texConvertEnabled=iniBool(tc);
    auto tm=gs(L"TexGenerateMipmaps"); if(!tm.empty()) opts.texGenerateMipmaps=iniBool(tm);
    auto to=gs(L"TexOverwriteExisting"); if(!to.empty()) opts.texOverwriteExisting=iniBool(to);
    // BLP-specific
    auto bc=gs(L"BlpCompression"); if(!bc.empty()) opts.blpCompression=_wtoi(bc.c_str());
    auto bj=gs(L"BlpJpegQuality"); if(!bj.empty()) opts.blpJpegQuality=_wtoi(bj.c_str());
    auto bd=gs(L"BlpDithering"); if(!bd.empty()) opts.blpDithering=iniBool(bd);
    // DDS-specific
    auto df=gs(L"DdsFormat"); if(!df.empty()) opts.ddsFormat=_wtoi(df.c_str());
    auto ep=gs(L"ExtentsPrecision"); if(!ep.empty()) opts.extentsPrecision=_wtoi(ep.c_str());
    // exportSmoothgroups: forced true via struct default
    // extentsType: forced 1 (animation-dependent) via struct default
}

void collectSkinBoneNodes(const std::vector<core::SceneNode>& sceneNodes, std::vector<INode*>& out) {
    // Collect every INode referenced by any Skin modifier in the scene.
    // The collected list is what BoneExtractor uses to decide which scene
    // nodes are "real bones" (BONE chunk) vs "helpers" (HELP chunk).
    //
    // Important: do NOT gate on GetContextInterface(). For FBX-imported
    // meshes that interface can return null (binding context is established
    // lazily) even though the skin's GetNumBones/GetBone enumeration is
    // perfectly valid — and skin weights still extract correctly via the
    // GetVertexWeight* APIs. The earlier `if (!ctx) continue;` bailout
    // caused FBX skeletons (where bones are typically Dummy objects, not
    // BoneGeometry) to be entirely missed from the skin reference list,
    // which then routed every node into the HELP chunk.
    //
    // Multiple meshes can reference the same bone — dedupe via a set so
    // the output list stays clean.
    std::unordered_set<INode*> seen;
    for (auto& sn : sceneNodes) {
        if (sn.category != core::NodeCategory::Mesh || !sn.maxNode) continue;
        auto* skin = core::ModifierReader::findSkin(sn.maxNode);
        if (!skin) continue;
        int n = skin->GetNumBones();
        for (int i = 0; i < n; ++i) {
            INode* b = skin->GetBone(i);
            if (b && seen.insert(b).second) {
                out.push_back(b);
            }
        }
    }
}

void resolveSkinIndices(INode* meshNode, ir::Mesh& mesh,
                        const std::unordered_map<INode*,int32_t>& nodeToIndex,
                        const std::vector<ir::Bone>& bones) {
    ISkin* skin=core::ModifierReader::findSkin(meshNode);
    if (!skin) return;
    int numBones=skin->GetNumBones();
    std::vector<int32_t> skinToNodeIndex(numBones,-1);

    char meshBuf[256] = {};
    const MCHAR* meshName = meshNode->GetName();
    if (meshName) WideCharToMultiByte(CP_UTF8, 0, meshName, -1, meshBuf, 255, nullptr, nullptr);
    ELOG << "  resolveSkinIndices '" << meshBuf << "' numSkinBones=" << numBones << "\n";

    for (int i=0;i<numBones;++i) {
        INode* bn=skin->GetBone(i); if(!bn) continue;
        auto it=nodeToIndex.find(bn);
        if(it!=nodeToIndex.end()) {
            int32_t bi=it->second;
            if(bi>=0&&bi<(int)bones.size()) {
                skinToNodeIndex[i]=bones[bi].nodeIndex;
                if (i < 10 || bones[bi].nodeIndex < 0) {
                    char bnBuf[256] = {};
                    const MCHAR* bname = bn->GetName();
                    if (bname) WideCharToMultiByte(CP_UTF8, 0, bname, -1, bnBuf, 255, nullptr, nullptr);
                    ELOG << "    skin[" << i << "] '" << bnBuf << "' boneListIdx=" << bi
                         << " nodeIdx=" << bones[bi].nodeIndex << "\n";
                }
            }
        } else {
            char bnBuf[256] = {};
            const MCHAR* bname = bn->GetName();
            if (bname) WideCharToMultiByte(CP_UTF8, 0, bname, -1, bnBuf, 255, nullptr, nullptr);
            ELOG << "    skin[" << i << "] '" << bnBuf << "' NOT FOUND in nodeToIndex!\n";
        }
    }
    EFLUSH;

    for (auto& v : mesh.vertices)
        for (auto& inf : v.skinInfluences) { int r=inf.boneIndex; if(r>=0&&r<numBones) inf.boneIndex=skinToNodeIndex[r]; else inf.boneIndex=-1; }
}

// Turns a mesh whose triangles wind against their own normals. Some ripped
// and converted models come that way (Beerus: every face of body and heads
// wound inward, the explicit normals outward). Max draws back faces and
// shades by the normals, so the scene looks right; Warcraft III culls by
// the winding and showed the inside. Only a mesh where at least 90% of the
// triangles disagree is turned: a normal mesh has ~3% (creases, thin parts),
// and an intentionally inverted shell (cell-shade outline) flips winding and
// normals together, so it agrees with itself and is left as it is.
bool fixInsideOutWinding(ir::Mesh& mesh) {
    const size_t tris = mesh.indices.size() / 3;
    if (tris == 0) return false;
    size_t against = 0;
    for (size_t f = 0; f < tris; ++f) {
        const auto& a = mesh.vertices[mesh.indices[3 * f]];
        const auto& b = mesh.vertices[mesh.indices[3 * f + 1]];
        const auto& c = mesh.vertices[mesh.indices[3 * f + 2]];
        const Point3 faceN = CrossProd(b.position - a.position, c.position - a.position);
        if (DotProd(faceN, a.normal + b.normal + c.normal) < 0.0f) ++against;
    }
    if (against * 10 < tris * 9) return false;
    for (size_t f = 0; f < tris; ++f) std::swap(mesh.indices[3 * f + 1], mesh.indices[3 * f + 2]);
    ELOG << "  mesh '" << mesh.name << "' wound against its normals (" << against << "/" << tris
         << " triangles) -> winding reversed\n";
    return true;
}

} // namespace


int MdxExporterPlugin::DoExport(const TCHAR* name, ExpInterface*, Interface* gi, BOOL suppressPrompts, DWORD)
{
    core::ExportErrorReporter reporter;
    MdxExportOptions opts;

    // Names and paths go out in the code page the importer found in the
    // model (root user property Wc3CodePage, wdx_text.h); a scene made in
    // Max has none and gets the Windows code page, GBK or UTF-8.
    int sceneCodePage = 0;
    if (gi && gi->GetRootNode())
        gi->GetRootNode()->GetUserPropInt(_T("Wc3CodePage"), sceneCodePage);
    wdx::text::CodePageScope codePageScope(static_cast<UINT>(sceneCodePage));

    ELOG << "==== MDLXExporter::DoExport ====\n";
    ELOG << "File: " << wcharToUtf8(name) << "\n"; EFLUSH;

    loadOptionsFromINI(gi, opts);
    ELOG << "Options: version=" << opts.version << " merge=" << opts.mergeGeosets
         << " anim=" << opts.exportAnimations << " skinQuant=" << !opts.disableSkinQuantize << "\n";

    // The options dialog stays open after Export and shows the progress of the
    // steps below. mdxExport() brings its own (setProgressDialog); without a
    // dialog (#noPrompt) the progress calls do nothing.
    ExportDialog ownDialog(GetDllInstance(), gi->GetMAXHWnd());
    ExportDialog& dialog = progressDialog_ ? *progressDialog_ : ownDialog;
    if (!suppressPrompts) {
        if (!dialog.run(opts, name)) return IMPEXP_CANCEL;
        ELOG << "Dialog: version=" << opts.version << "\n";
    }

    dialog.step(ExportStep::Scene);
    core::NodeClassifier classifier;
    registerMdxNodeTypes(classifier);
    core::SceneTraversal traversal;
    auto sceneResult = traversal.traverse(gi, classifier);

    ELOG << "\n==== Scene Traversal ====\n";
    ELOG << "Scene nodes: " << sceneResult.nodes.size() << "\n";
    for (size_t i=0;i<sceneResult.nodes.size();i++) {
        auto& sn=sceneResult.nodes[i];
        ELOG << "  [" << i << "] cat=" << (int)sn.category << " nodeIdx=" << sn.nodeIndex
             << " tag=" << (sn.customTag.empty()? "-" : sn.customTag);
        if (sn.maxNode) {
            ELOG << " '" << wcharToUtf8(sn.maxNode->GetName()) << "'";
            Point3 p=sn.maxNode->GetNodeTM(0).GetTrans();
            ELOG << " pos=(" << p.x << "," << p.y << "," << p.z << ")";
        }
        ELOG << "\n";
    } EFLUSH;

    ir::IRModel irModel;
    irModel.nodes = std::move(sceneResult.irNodes);

    ELOG << "\n==== Bone Extraction ====\n";
    dialog.step(ExportStep::Bones);
    std::vector<INode*> skinBoneNodes;
    collectSkinBoneNodes(sceneResult.nodes, skinBoneNodes);
    core::BoneExtractor boneEx;
    auto boneResult = boneEx.extract(sceneResult.nodes, skinBoneNodes, reporter);
    irModel.bones = std::move(boneResult.bones);
    ELOG << "Bones: " << irModel.bones.size() << " (skin-referenced: " << skinBoneNodes.size() << ")\n";
    for (size_t i=0;i<irModel.bones.size();i++) {
        auto& b=irModel.bones[i];
        ELOG << "  bone[" << i << "] '" << b.name << "' nodeIdx=" << b.nodeIndex
             << " parent=" << b.parentIndex << " helper=" << b.isHelper
             << " pivot=(" << b.pivotPoint.x << "," << b.pivotPoint.y << "," << b.pivotPoint.z << ")\n";
    } EFLUSH;

    // Implicit bones
    for (size_t bi=0;bi<irModel.bones.size();++bi) {
        auto& bone=irModel.bones[bi]; if(bone.nodeIndex>=0) continue;
        int32_t newIdx=(int32_t)irModel.nodes.size();
        ir::IRModel::Node irn; irn.name=bone.name; irn.pivotPoint=bone.pivotPoint; irn.worldTM=bone.bindPose;
        for (auto& [node,boneIdx]:boneResult.nodeToIndex) { if(boneIdx==(int32_t)bi){irn.maxNode=node;break;} }
        irModel.nodes.push_back(std::move(irn)); bone.nodeIndex=newIdx;
        ELOG << "  implicit bone[" << bi << "] -> irNode[" << newIdx << "]\n";
    }
    for (auto& bone:irModel.bones) {
        if(bone.nodeIndex<0) continue; auto& irn=irModel.nodes[bone.nodeIndex];
        if(irn.parentIndex>=0) continue;
        if(bone.parentIndex>=0 && bone.parentIndex<(int)irModel.bones.size())
            irn.parentIndex=irModel.bones[bone.parentIndex].nodeIndex;
    } EFLUSH;

    // Meshes
    ELOG << "\n==== Mesh Extraction ====\n";
    dialog.step(ExportStep::Meshes);
    core::MeshExtractor meshEx;
    std::vector<int32_t> meshBoneIdxs; // bones minted for unskinned meshes
    size_t meshTotal = 0, meshDone = 0;
    for (auto& sn:sceneResult.nodes) if(sn.category==core::NodeCategory::Mesh) ++meshTotal;
    for (auto& sn:sceneResult.nodes) {
        if(sn.category!=core::NodeCategory::Mesh) continue;
        dialog.items(meshDone++, meshTotal);
        auto mesh=meshEx.extract(sn.maxNode,sn.nodeIndex,0,reporter);
        if (mesh.indices.empty()) {
            // No faces at frame 0 (a tyFlow/particle object before its first
            // birth, an emptied Editable Poly). A geoset with no vertices
            // crashes Warcraft III on load, so the node exports as a plain
            // helper instead: it keeps its place in the hierarchy, its
            // animation still bakes, and the bone optimizer drops it when
            // nothing needs it.
            reporter.warning(L"Node has no faces at frame 0; exported without geometry.",
                             sn.maxNode->GetName());
            if (boneResult.nodeToIndex.find(sn.maxNode) == boneResult.nodeToIndex.end()) {
                const int32_t ni = sn.nodeIndex;
                ir::Bone hb;
                hb.name = irModel.nodes[ni].name;
                hb.nodeIndex = ni;
                hb.pivotPoint = irModel.nodes[ni].pivotPoint;
                hb.bindPose = irModel.nodes[ni].worldTM;
                hb.nodeFlags = irModel.nodes[ni].nodeFlags;
                hb.isHelper = true;
                boneResult.nodeToIndex[sn.maxNode] = (int32_t)irModel.bones.size();
                meshBoneIdxs.push_back((int32_t)irModel.bones.size());
                irModel.bones.push_back(std::move(hb));
            }
            ELOG << "  mesh '" << wcharToUtf8(sn.maxNode->GetName())
                 << "' has no faces -> helper, no geoset\n";
            continue;
        }
        if (fixInsideOutWinding(mesh))
            reporter.warning(L"Triangles wound against their normals (inside out in the game); "
                             L"the winding was reversed in the export, the scene is unchanged.",
                             sn.maxNode->GetName());
        ISkin* skin=core::ModifierReader::findSkin(sn.maxNode);
        if(skin) {
            resolveSkinIndices(sn.maxNode,mesh,boneResult.nodeToIndex,irModel.bones);
            ELOG << "  mesh '" << mesh.name << "' verts=" << mesh.vertices.size() << " skinned(" << skin->GetNumBones() << " bones)\n";
            for(size_t v=0;v<std::min(mesh.vertices.size(),size_t(3));v++){
                ELOG << "    v[" << v << "]:"; float tw=0;
                for(auto& inf:mesh.vertices[v].skinInfluences){ELOG<<" b"<<inf.boneIndex<<"="<<inf.weight; tw+=inf.weight;}
                ELOG << " sum=" << tw << "\n";
            }
        } else {
            // Unskinned mesh: the mesh node itself always becomes a bone and
            // every vertex rigid-binds to it. The mesh's IR node already
            // exists from scene traversal (sn.nodeIndex) with the correct
            // parent chain, pivot (= the custom pivot's world position at
            // frame 0), bind TM and node flags — the bone just points at it,
            // and AnimDispatcher bakes the node's own animation. This covers
            // a standalone animated mesh, a mesh linked to a bone/helper/
            // dummy, and a mesh linked to another (animated) mesh alike.
            // The old behavior skinned the mesh to its PARENT when the
            // parent was in the bone list, silently dropping the mesh's own
            // animation and pivot.
            int32_t ni;
            auto selfIt = boneResult.nodeToIndex.find(sn.maxNode);
            if (selfIt != boneResult.nodeToIndex.end()) {
                // Node is already a bone: another mesh's Skin modifier
                // references it, or a bone/helper is linked under it.
                // BoneExtractor put that bone on this mesh's traversal node
                // (sn.nodeIndex), so its children stay attached.
                auto& b = irModel.bones[selfIt->second];
                b.isHelper = false; // carries geometry → BONE chunk
                ni = b.nodeIndex;
                ELOG << "  mesh '" << mesh.name << "' unskinned -> existing bone '"
                     << b.name << "' (nodeIdx=" << ni << ")\n";
            } else {
                ni = sn.nodeIndex;
                ir::Bone sb;
                sb.name = irModel.nodes[ni].name;
                sb.nodeIndex = ni;
                sb.pivotPoint = irModel.nodes[ni].pivotPoint;
                sb.bindPose = irModel.nodes[ni].worldTM;
                sb.nodeFlags = irModel.nodes[ni].nodeFlags;
                sb.isHelper = false; // carries geometry → BONE chunk
                boneResult.nodeToIndex[sn.maxNode] = (int32_t)irModel.bones.size();
                meshBoneIdxs.push_back((int32_t)irModel.bones.size());
                irModel.bones.push_back(std::move(sb));
                ELOG << "  mesh '" << mesh.name << "' unskinned -> own bone irNode[" << ni << "]\n";
            }
            for (auto& v : mesh.vertices) {
                v.skinInfluences.clear();
                v.skinInfluences.push_back({ni, 1.0f});
            }
        }
        irModel.meshes.push_back(std::move(mesh));
    } EFLUSH;

    // Fixup pass: resolve bone-array parent links for the bones just created
    // for unskinned meshes. Runs after the mesh loop so a mesh linked to a
    // mesh that appears LATER in traversal order still resolves. The bone
    // parentIndex is what BoneOptimizer uses to keep ancestor chains alive;
    // the node-level parentIndex (already set by traversal) is what the
    // hierarchy resolver writes into the MDX.
    if (!meshBoneIdxs.empty()) {
        // nodeIndex → bone-array index for every bone that has an IR node.
        std::unordered_map<int32_t, int32_t> nodeIdxToBone;
        for (size_t b = 0; b < irModel.bones.size(); ++b)
            if (irModel.bones[b].nodeIndex >= 0)
                nodeIdxToBone[irModel.bones[b].nodeIndex] = (int32_t)b;

        // Mesh-category IR nodes (these have no MDX objectId of their own
        // unless a bone points at them).
        std::unordered_set<int32_t> meshCatNodes;
        for (auto& sn : sceneResult.nodes)
            if (sn.category == core::NodeCategory::Mesh)
                meshCatNodes.insert(sn.nodeIndex);

        // Worklist: resolving one bone can mint a helper for a skinned-mesh
        // ancestor, which then needs its own parent resolved too.
        std::vector<int32_t> work(meshBoneIdxs.begin(), meshBoneIdxs.end());
        while (!work.empty()) {
            const int32_t bi = work.back();
            work.pop_back();
            const int32_t ni = irModel.bones[bi].nodeIndex;
            if (ni < 0 || irModel.bones[bi].parentIndex >= 0) continue;
            int32_t p = irModel.nodes[ni].parentIndex;
            while (p >= 0) {
                auto pit = nodeIdxToBone.find(p);
                if (pit != nodeIdxToBone.end()) {
                    irModel.bones[bi].parentIndex = pit->second;
                    break;
                }
                if (meshCatNodes.count(p)) {
                    // Ancestor is a SKINNED mesh (unskinned ones are all in
                    // nodeIdxToBone by now): give it a helper bone so the
                    // chain stays connected and its node animation exports.
                    ir::Bone hb;
                    hb.name = irModel.nodes[p].name;
                    hb.nodeIndex = p;
                    hb.pivotPoint = irModel.nodes[p].pivotPoint;
                    hb.bindPose = irModel.nodes[p].worldTM;
                    hb.nodeFlags = irModel.nodes[p].nodeFlags;
                    hb.isHelper = true;
                    const int32_t hbIdx = (int32_t)irModel.bones.size();
                    nodeIdxToBone[p] = hbIdx;
                    irModel.bones.push_back(std::move(hb));
                    irModel.bones[bi].parentIndex = hbIdx;
                    ELOG << "  helper minted for skinned-mesh ancestor '"
                         << irModel.nodes[p].name << "' (nodeIdx=" << p << ")\n";
                    work.push_back(hbIdx);
                    break;
                }
                // Non-bone content node (light, emitter, …): it has its own
                // objectId, so the MDX hierarchy resolves through it; for the
                // bone-array linkage keep walking to the nearest bone above.
                p = irModel.nodes[p].parentIndex;
            }
        }
    } EFLUSH;

    // MDX extractors
    ELOG << "\n==== MDX Extractors ====\n";
    dialog.step(ExportStep::Objects);
    auto mtlMap=mdx_extract::extractMaterials(sceneResult.nodes,irModel,reporter,opts.version);
    mdx_extract::extractLights(sceneResult.nodes,irModel,reporter);
    mdx_extract::extractAttachments(sceneResult.nodes,irModel,reporter);
    mdx_extract::extractParticles1(sceneResult.nodes,irModel,reporter);
    mdx_extract::extractParticles2(sceneResult.nodes,irModel,reporter);
    mdx_extract::extractRibbons(sceneResult.nodes,irModel,mtlMap,reporter);
    mdx_extract::extractEvents(sceneResult.nodes,irModel,reporter);
    mdx_extract::extractCollisions(sceneResult.nodes,irModel,reporter);
    mdx_extract::extractVertexColors(sceneResult.nodes,irModel,reporter);
    mdx_extract::extractGeosetAnims(sceneResult.nodes,irModel,reporter);
    if(opts.version>=1200){mdx_extract::extractPopcorn(sceneResult.nodes,irModel,reporter);mdx_extract::extractFaceFX(sceneResult.nodes,irModel,reporter);}
    ELOG << "Materials=" << irModel.materials.size() << " Textures=" << irModel.textures.size()
         << " Lights=" << irModel.lights.size() << " Attach=" << irModel.attachments.size()
         << " PE=" << irModel.particleEmitters.size() << " Ribbons=" << irModel.ribbonEmitters.size()
         << " Events=" << irModel.eventObjects.size() << " Collisions=" << irModel.collisionShapes.size()
         << " GeosetAnims=" << irModel.geosetAnims.size() << "\n";
    EFLUSH;

    // Sequences
    ELOG << "\n==== Sequences ====\n";
    MdxSequenceManager seqMgr;
    irModel.sequences = seqMgr.extractSequences(gi);
    ELOG << "Found: " << irModel.sequences.size() << " sequences\n";
    for(size_t i=0;i<irModel.sequences.size();i++){
        auto& s=irModel.sequences[i];
        ELOG << "  [" << i << "] '" << s.name << "' ticks=" << s.startTime << "-" << s.endTime
             << " " << (s.isLooping?"loop":"nonloop") << " rarity=" << s.rarity << "\n";
    }
    if(irModel.sequences.empty()) {
        // Default sequence: a 'Stand' from frame 10 to 60. Without at least
        // one sequence, MDX has no animation interval and most viewers
        // (Magos, the game itself) treat the model as broken. Frame 10-60
        // matches the convention used by NeoDex and Blizzard's stock models
        // (frame 0 is reserved as the bind-pose / baseline frame).
        int tpf = GetTicksPerFrame();
        ir::Sequence stand;
        stand.name      = "Stand";
        stand.startTime = 10 * tpf;
        stand.endTime   = 60 * tpf;
        stand.isLooping = true;
        stand.rarity    = 0.0f;
        stand.moveSpeed = 0.0f;
        irModel.sequences.push_back(std::move(stand));
        ELOG << "  *** No sequences in scene — auto-inserted default 'Stand' "
             << "(frames 10-60, ticks " << irModel.sequences[0].startTime
             << "-" << irModel.sequences[0].endTime << ", looping) ***\n";
    }
    EFLUSH;
    seqMgr.sampleExtents(irModel.sequences, irModel);

    // Cameras sample their tracks per sequence
    mdx_extract::extractCameras(sceneResult.nodes,irModel,reporter);
    ELOG << "Cameras=" << irModel.cameras.size() << "\n"; EFLUSH;

    // Bake
    ELOG << "\n==== Animation Bake ====\n";
    dialog.step(ExportStep::Animations);
    if(opts.exportAnimations && !irModel.sequences.empty()) {
        core::AnimDispatcher dispatcher;
        core::AnimDispatcher::Config cfg; cfg.tickInterval=opts.animSampleInterval > 0 ? opts.animSampleInterval : GetTicksPerFrame(); cfg.angleThreshold=opts.ikRefinementThreshold;
        cfg.onNode = [&dialog](size_t done, size_t total) { dialog.items(done, total); };
        dispatcher.bakeAll(irModel,irModel.sequences,cfg,reporter);
        ELOG << "Baked: " << irModel.nodeAnimations.size() << " node animations\n";
    } else {
        ELOG << "SKIPPED (anim=" << opts.exportAnimations << " seq=" << irModel.sequences.size() << ")\n";
    }

    // Track analysis
    ELOG << "\n==== Track Analysis (ABSOLUTE vs DELTA) ====\n";
    int aR=0,aT=0,dR=0,dT=0;
    for(auto& na:irModel.nodeAnimations){
        std::string nm=(na.nodeIndex>=0&&na.nodeIndex<(int)irModel.nodes.size())?irModel.nodes[na.nodeIndex].name:"?";
        ELOG << "  node[" << na.nodeIndex << "] '" << nm << "' TR=" << na.translation.keys.size()
             << " RT=" << na.rotation.keys.size() << " SC=" << na.scale.keys.size() << "\n";
        if(!na.translation.empty()){
            auto& k=na.translation.keys[0]; bool z=fabsf(k.value.x)<0.01f&&fabsf(k.value.y)<0.01f&&fabsf(k.value.z)<0.01f;
            ELOG << "    TR key0 t=" << k.time << " v=(" << k.value.x << "," << k.value.y << "," << k.value.z << ") " << (z?"DELTA":"ABSOLUTE") << "\n";
            if(z) dT++; else aT++;
            // Dump all subsequent translation keys to see the actual motion curve
            for (size_t ki = 1; ki < na.translation.keys.size(); ++ki) {
                auto& kk = na.translation.keys[ki];
                ELOG << "    TR key" << ki << " t=" << kk.time
                     << " v=(" << kk.value.x << "," << kk.value.y << "," << kk.value.z << ")\n";
            }
        }
        if(!na.rotation.empty()){
            auto& k=na.rotation.keys[0]; bool id=fabsf(k.value.x)<0.01f&&fabsf(k.value.y)<0.01f&&fabsf(k.value.z)<0.01f&&fabsf(fabsf(k.value.w)-1.0f)<0.01f;
            ELOG << "    RT key0 t=" << k.time << " q=(" << k.value.x << "," << k.value.y << "," << k.value.z << "," << k.value.w << ") " << (id?"DELTA":"ABSOLUTE") << "\n";
            if(id) dR++; else aR++;
            // Also dump all subsequent keys so we can see the actual animation curve
            for (size_t ki = 1; ki < na.rotation.keys.size(); ++ki) {
                auto& kk = na.rotation.keys[ki];
                ELOG << "    RT key" << ki << " t=" << kk.time
                     << " q=(" << kk.value.x << "," << kk.value.y << ","
                     << kk.value.z << "," << kk.value.w << ")\n";
            }
        }
        if(!na.scale.empty()){
            auto& k=na.scale.keys[0];
            ELOG << "    SC key0 t=" << k.time << " s=(" << k.value.x << "," << k.value.y << "," << k.value.z << ")\n";
        }
    }
    ELOG << "Summary: trans " << dT << " delta/" << aT << " abs, rot " << dR << " delta/" << aR << " abs\n";
    if(aR>0||aT>0) ELOG << "*** ABSOLUTE TRACKS → model builder will apply delta correction ***\n";
    EFLUSH;

    // Optimize
    dialog.step(ExportStep::Optimizing);
    if(opts.optimizeVertices){core::VertexOptimizer vo; for(auto& m:irModel.meshes) vo.optimize(m,opts.vertexMergeThreshold);}
    if(opts.optimizeKeyframes){
        // Collect floatTrack indices that are visibility/alpha tracks.
        // These have DontInterp/step semantics and must NOT be reduced —
        // the boundary-stub keys (same value at every sequence start) are
        // intentional and required for round-trip with Blizzard tooling.
        // Without this exempt, the Saurus export went 43 → 6 keys.
        std::unordered_set<int32_t> visibilityTrackIndices;
        auto addIfValid = [&](int32_t idx){ if (idx >= 0) visibilityTrackIndices.insert(idx); };

        for (const auto& ga   : irModel.geosetAnims)      addIfValid(ga.alphaTrackIndex);
        for (const auto& lite : irModel.lights)           addIfValid(lite.visibilityTrackIndex);
        for (const auto& at   : irModel.attachments)      addIfValid(at.visibilityTrackIndex);
        for (const auto& pe   : irModel.particleEmitters) addIfValid(pe.visibilityTrackIndex); // PE1+PE2+Corn
        for (const auto& rib  : irModel.ribbonEmitters)   addIfValid(rib.visibilityTrackIndex);
        for (const auto& cam  : irModel.cameras)          addIfValid(cam.visibilityTrackIndex);
        // KMTA layer alpha behaves like visibility: DontInterp holds with a
        // boundary stub at every sequence start (form-switch composites flip
        // 1.0/0.0 per sequence). Reduction dropped those stubs (27 → 16 keys
        // on Mr.War3's 2-layer body material), breaking per-sequence holds.
        for (const auto& mat  : irModel.materials)
            for (const auto& lay : mat.layers)            addIfValid(lay.alphaTrackIndex);

        ELOG << "Keyframe optimizer: exempting " << visibilityTrackIndices.size()
             << " visibility floatTrack(s) from reduction\n";

        // Texture-anim (TXAN) vec3 tracks need their keys AT sequence
        // interval boundaries: WC3/Flakes track evaluation loop-wraps
        // between the last and first in-interval key, so a UV-scroll
        // track whose seq-start/end stub keys get reduced away spends
        // the rest of the sequence lerping back to the first key —
        // Mr.War3's spell rays all re-swept at once. Keep every key.
        std::unordered_set<int32_t> keepVec3TrackIndices;
        for (const auto& ta : irModel.textureAnimations) {
            if (ta.translationTrackIndex >= 0) keepVec3TrackIndices.insert(ta.translationTrackIndex);
            if (ta.scaleTrackIndex >= 0)       keepVec3TrackIndices.insert(ta.scaleTrackIndex);
        }
        // Camera KCTR/KTTR, for the same reason: one track spans every
        // sequence, so a camera holding still at a sequence end lost its end
        // key (equal to the next sequence's start) and swung back towards its
        // first key - arthasillidanfight's "Arthas Run 2" camera, last ~4 s.
        // The extractor has already reduced them per sequence.
        for (const auto& cam : irModel.cameras) {
            if (cam.positionTrackIndex >= 0)       keepVec3TrackIndices.insert(cam.positionTrackIndex);
            if (cam.targetPositionTrackIndex >= 0) keepVec3TrackIndices.insert(cam.targetPositionTrackIndex);
        }

        // ── PE2 Rotation Fix (NeoDex-compatible) ─────────────────
        // The AnimDispatcher bakes rotations as parent-relative deltas,
        // so a statically-rotated PE2 produces all-identity KGRT keys.
        // But MDX needs the WORLD rotation in the KGRT track with
        // identity at frame 0 (bind pose) — otherwise the emitter
        // direction is wrong during playback.
        //
        // Solution: read the world rotation directly from the INode
        // (same as NeoDex: `at time 0f obj.rotation`), then:
        //   - If world rotation is identity → nothing to do
        //   - If world rotation is non-identity → replace baked keys
        //     with: identity at t=0, world rotation at seq starts/ends
        //
        // Matches NeoDex: createParticle2RotationFix / fixAnimatedParticle2Rotation
        // (Wc3Animation.ms lines 3048-3160)
        {
            int pe2Fixed = 0;
            const Quat identityQ(0.0f, 0.0f, 0.0f, 1.0f);

            auto isQuatIdentity = [](const Quat& q) -> bool {
                return fabsf(q.x) < 0.01f && fabsf(q.y) < 0.01f &&
                       fabsf(q.z) < 0.01f &&
                       (fabsf(q.w - 1.0f) < 0.01f || fabsf(q.w + 1.0f) < 0.01f);
            };

            for (auto& pe : irModel.particleEmitters) {
                if (pe.variant != 2) continue;

                // Get the INode for this PE2
                if (pe.nodeIndex < 0 ||
                    pe.nodeIndex >= static_cast<int32_t>(irModel.nodes.size()))
                    continue;
                INode* maxNode = irModel.nodes[pe.nodeIndex].maxNode;
                if (!maxNode) continue;

                // Read world rotation at frame 0 from INode
                // (equivalent to NeoDex: `at time 0f obj.rotation`)
                Matrix3 nodeTM = maxNode->GetNodeTM(0);
                AffineParts ap;
                decomp_affine(nodeTM, &ap);
                Quat worldRot = ap.q;

                ELOG << "  PE2 node[" << pe.nodeIndex << "] '"
                     << irModel.nodes[pe.nodeIndex].name
                     << "' worldRot=(" << worldRot.x << "," << worldRot.y
                     << "," << worldRot.z << "," << worldRot.w << ")";

                // The rewrite below keys the sequence boundaries; a rotation
                // on a global sequence runs on its own clock, and those keys
                // would be spliced into its loop.
                bool rotOnGlobalSeq = false;
                for (const auto& na : irModel.nodeAnimations)
                    if (na.nodeIndex == pe.nodeIndex &&
                        na.rotation.globalSequenceIndex >= 0)
                        rotOnGlobalSeq = true;
                if (rotOnGlobalSeq) {
                    ELOG << " → global-sequence rotation, skip\n";
                    continue;
                }

                if (isQuatIdentity(worldRot)) {
                    ELOG << " → identity, skip\n";
                    continue;
                }
                ELOG << " → non-identity, fixing\n";

                // Find the NodeAnimation for this PE2
                ir::NodeAnimation* anim = nullptr;
                for (auto& na : irModel.nodeAnimations) {
                    if (na.nodeIndex == pe.nodeIndex) { anim = &na; break; }
                }
                if (!anim) continue;

                // Check if the rotation is truly animated (non-constant)
                // by comparing world rotation at multiple times.
                // If constant → Case B (synthetic track).
                // If varying → Case A (resample with zeroed frame 0).
                bool isRotAnimated = false;
                {
                    Control* tmCtrl = maxNode->GetTMController();
                    Control* rotCtrl = tmCtrl ? tmCtrl->GetRotationController() : nullptr;
                    if (rotCtrl && rotCtrl->NumKeys() > 0) {
                        // Check if any key has a different rotation
                        for (int ki = 0; ki < rotCtrl->NumKeys(); ki++) {
                            TimeValue t = rotCtrl->GetKeyTime(ki);
                            if (t == 0) continue;
                            Matrix3 tm2 = maxNode->GetNodeTM(t);
                            AffineParts ap2;
                            decomp_affine(tm2, &ap2);
                            float dot = fabsf(ap.q.x*ap2.q.x + ap.q.y*ap2.q.y +
                                              ap.q.z*ap2.q.z + ap.q.w*ap2.q.w);
                            if (dot < 0.9999f) { isRotAnimated = true; break; }
                        }
                    }
                }

                auto& rotKeys = anim->rotation.keys;

                if (isRotAnimated) {
                    // Case A: rotation IS animated → resample from INode
                    // with identity at frame 0
                    ELOG << "    Case A: animated rotation, resampling\n";

                    // Collect unique times from existing baked keys
                    std::vector<TimeValue> times;
                    for (auto& k : rotKeys)
                        times.push_back(k.time);
                    // Ensure sequence boundaries are included
                    for (auto& seq : irModel.sequences) {
                        bool hasStart = false, hasEnd = false;
                        for (auto t : times) {
                            if (t == seq.startTime) hasStart = true;
                            if (t == seq.endTime) hasEnd = true;
                        }
                        if (!hasStart) times.push_back(seq.startTime);
                        if (!hasEnd) times.push_back(seq.endTime);
                    }
                    std::sort(times.begin(), times.end());
                    times.erase(std::unique(times.begin(), times.end()), times.end());

                    rotKeys.clear();
                    for (TimeValue t : times) {
                        ir::Keyframe<Quat> k;
                        k.time = t;
                        if (t == 0) {
                            k.value = identityQ;
                        } else {
                            Matrix3 tm = maxNode->GetNodeTM(t);
                            AffineParts ap2;
                            decomp_affine(tm, &ap2);
                            k.value = ap2.q;
                        }
                        rotKeys.push_back(k);
                    }
                    anim->rotation.interpolation = ir::InterpolationType::Linear;
                    ELOG << "    Resampled " << rotKeys.size() << " keys\n";
                } else {
                    // Case B: NOT animated → create synthetic track
                    // Identity at t=0, worldRot at all sequence boundaries
                    ELOG << "    Case B: static rotation, creating synthetic KGRT\n";

                    rotKeys.clear();

                    // t=0: identity
                    ir::Keyframe<Quat> k0;
                    k0.time = 0;
                    k0.value = identityQ;
                    rotKeys.push_back(k0);

                    // Sequence starts/ends: world rotation
                    for (auto& seq : irModel.sequences) {
                        bool hasStart = false, hasEnd = false;
                        for (auto& k : rotKeys) {
                            if (k.time == seq.startTime) hasStart = true;
                            if (k.time == seq.endTime) hasEnd = true;
                        }
                        if (!hasStart) {
                            ir::Keyframe<Quat> ks;
                            ks.time = seq.startTime;
                            ks.value = worldRot;
                            rotKeys.push_back(ks);
                        }
                        if (!hasEnd) {
                            ir::Keyframe<Quat> ke;
                            ke.time = seq.endTime;
                            ke.value = worldRot;
                            rotKeys.push_back(ke);
                        }
                    }

                    std::sort(rotKeys.begin(), rotKeys.end(),
                        [](const ir::Keyframe<Quat>& a, const ir::Keyframe<Quat>& b) {
                            return a.time < b.time;
                        });

                    anim->rotation.interpolation = ir::InterpolationType::Linear;
                    ELOG << "    Created " << rotKeys.size() << " keys\n";
                }

                pe2Fixed++;
            }
            if (pe2Fixed > 0)
                ELOG << "  PE2 rotation fix: " << pe2Fixed << " node(s) corrected\n";
        }

        core::KeyframeOptimizer ko;

        // Node animations: full optimization
        for (auto& na : irModel.nodeAnimations) {
            ko.optimize(na.translation);
            ko.optimize(na.rotation);
            ko.optimize(na.scale);
        }

        // Float tracks: skip visibility tracks
        for (size_t i = 0; i < irModel.floatTracks.size(); ++i) {
            if (visibilityTrackIndices.count(static_cast<int32_t>(i))) {
                ELOG << "  skip floatTrack[" << i << "] (visibility, "
                     << irModel.floatTracks[i].keys.size() << " keys preserved)\n";
                continue;
            }
            ko.optimize(irModel.floatTracks[i]);
        }

        // Vec3 tracks: skip texture-anim and camera tracks (boundary keys required)
        for (size_t i = 0; i < irModel.vec3Tracks.size(); ++i) {
            if (keepVec3TrackIndices.count(static_cast<int32_t>(i))) {
                ELOG << "  skip vec3Track[" << i << "] (texture-anim/camera, "
                     << irModel.vec3Tracks[i].keys.size() << " keys preserved)\n";
                continue;
            }
            ko.optimize(irModel.vec3Tracks[i]);
        }

        EFLUSH;
    }

    // Remove unused bones/helpers (matches NeoDex optimizeBonesAndHelpers).
    // Bones are kept if they are: skin-referenced, have animation, or are
    // ancestors of a kept bone. Controlled by the "Keep Unused" checkbox.
    if (!opts.keepUnusedBonesHelpers) {
        ELOG << "\n==== Bone Optimizer ====\n";
        size_t before = irModel.bones.size();
        core::BoneOptimizer bo;
        bo.optimize(irModel);
        ELOG << "Bones: " << before << " -> " << irModel.bones.size()
             << " (" << (before - irModel.bones.size()) << " removed)\n";
        EFLUSH;
    }

    // Fix texture extensions in MDX paths (matches NeoDex GetImageFile).
    // BitmapTex::GetMapName() returns the disk path (.tga, .png, etc.)
    // but MDX must reference the game format: .blp for v800, .dds for v1200.
    // NeoDex always forces .blp via: (getFilenameFile s) + ".blp"
    // This runs BEFORE texture conversion so the converter sees the
    // correct target extension; it also works when conversion is off.
    //
    // Classic (v800) has no DDS loader at all, so .dds must be rewritten to
    // .blp there. Reforged reads BLP as well as DDS, so .blp is left alone
    // when targeting v1200.
    {
        const bool isReforged = (opts.version >= 1200);
        const char* targetExt = isReforged ? ".dds" : ".blp";
        int fixed = 0;
        for (auto& tex : irModel.textures) {
            if (tex.filePath.empty()) continue;
            auto dot = tex.filePath.rfind('.');
            if (dot == std::string::npos) continue;
            const std::string ext = tex.filePath.substr(dot);
            std::string extLower = ext;
            std::transform(extLower.begin(), extLower.end(), extLower.begin(),
                           [](unsigned char c) { return static_cast<char>(::tolower(c)); });
            // Skip if already correct
            if (extLower == targetExt) continue;
            // Reforged keeps .blp references as-is (the engine loads both);
            // classic must convert .dds since it cannot load DDS at all.
            if (isReforged && extLower == ".blp") continue;
            // Swap .dds/.tga/.png/.bmp/.jpg/.jpeg to target format
            const std::string oldPath = tex.filePath;
            tex.filePath = tex.filePath.substr(0, dot) + targetExt;
            fixed++;
            ELOG << "  texPath fix: '" << oldPath
                 << "' -> '" << tex.filePath << "'\n";
        }
        if (fixed > 0) {
            ELOG << "Fixed " << fixed << " texture extension(s) to " << targetExt << "\n";
        }
    }

    // Texture conversion (BLP for v800, DDS for v1200) — runs on the IR
    // before the model build so converted file paths land in the MDX. Uses
    // WhiteoutLib parsers/writers; skips textures whose source bitmap was
    // not captured (e.g. IFL frames, replaceable-only entries).
    if (opts.texConvertEnabled) {
        ELOG << "\n==== Texture Conversion ====\n"; EFLUSH;
        dialog.step(ExportStep::Textures);
        mdx_export::convertExportTextures(irModel, wcharToUtf8(name), opts, reporter);
    }

    // Build
    ELOG << "\n==== Model Build ====\n"; EFLUSH;
    dialog.step(ExportStep::Building);
    MdxModelBuilder builder;
    whiteout::mdx::Model mdxModel = builder.build(irModel, opts);
    ELOG << "Result: " << mdxModel.bones.size() << " bones, " << mdxModel.helpers.size() << " helpers, "
         << mdxModel.geosets.size() << " geosets, " << mdxModel.sequences.size() << " sequences\n";
    ELOG << "Pivots: " << mdxModel.pivotPoints.size() << "\n";
    for(size_t i=0;i<mdxModel.pivotPoints.size();i++){auto& p=mdxModel.pivotPoints[i]; ELOG<<"  pivot["<<i<<"]=("<<p.x<<","<<p.y<<","<<p.z<<")\n";}
    EFLUSH;

    // ── Name + Write ────────────────────────────────────────────────────
    //
    // Path comes from Max's File→Export save dialog. Optional post-processing:
    //   - Auto-increment: if file exists, walk _1, _2, ... to find a free name
    //   - Open folder: ShellExecute the export directory after a successful write
    //
    std::string filePath = wcharToUtf8(name);

    auto fileExistsW = [](const std::wstring& wp) {
        return GetFileAttributesW(wp.c_str()) != INVALID_FILE_ATTRIBUTES;
    };

    auto utf8ToWide = [](const std::string& s) -> std::wstring {
        if (s.empty()) return {};
        int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        if (wlen <= 0) return {};
        std::wstring w(static_cast<size_t>(wlen - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), wlen);
        return w;
    };

    if (opts.autoIncrementFilename && fileExistsW(utf8ToWide(filePath))) {
        // Shared with the export dialog's header preview (export_paths.h), so
        // the name shown there is the name written here.
        const std::string candidate =
            wcharToUtf8(mdx_export::AutoIncrementPath(utf8ToWide(filePath)).c_str());
        ELOG << "AutoIncrement: " << filePath << " -> " << candidate << "\n";
        filePath = candidate;
    }

    // MODL name: an explicit name from the dialog wins; otherwise the saved
    // scene's own name; and only for a never-saved scene does the export
    // filename stand in. The dialog seeds its field from the scene too, so the
    // scene name is what a normal interactive export ends up writing.
    if (!opts.modelName.empty()) {
        mdxModel.modelName = opts.modelName;
    } else if (std::wstring sceneName = currentSceneModelName(); !sceneName.empty()) {
        mdxModel.modelName = wcharToUtf8(sceneName.c_str());
    } else {
        auto sl = filePath.find_last_of("/\\");
        auto st = (sl != std::string::npos) ? filePath.substr(sl + 1) : filePath;
        auto dt = st.rfind('.');
        if (dt != std::string::npos) st = st.substr(0, dt);
        mdxModel.modelName = st;
    }
    ELOG << "MODL name: '" << mdxModel.modelName << "'\n";

    dialog.step(ExportStep::Writing);
    whiteout::mdx::Writer writer;
    writer.write(filePath, mdxModel);
    ELOG << "\n==== Written: " << filePath << " ====\n";

    // Close the progress window before the report, which would otherwise
    // open behind it, and before the folder opens.
    dialog.close();

    if (reporter.hasWarnings() || reporter.hasErrors())
        reporter.showSummaryDialog(gi->GetMAXHWnd());

    // Open folder after export
    if (opts.openFolderAfterExport) {
        auto sl = filePath.find_last_of("/\\");
        if (sl != std::string::npos) {
            std::string dir = filePath.substr(0, sl);
            std::wstring wdir = utf8ToWide(dir);
            ShellExecuteW(nullptr, L"open", wdir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            ELOG << "Opened folder: " << dir << "\n";
        }
    }

    ELOG << "\n==== Export Complete ====\n"; EFLUSH;
    return IMPEXP_SUCCESS;
}
