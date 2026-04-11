// MDLXExporter — MdxExporterPlugin implementation
// DEBUG: Logs to %TEMP%\mdlx_export_debug.log
#include "mdx_exporter_plugin.h"
#include "mdx_export_options.h"
#include "mdx_class_ids.h"
#include "mdx_node_registration.h"
#include "export_dialog.h"
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

#include "assembly/mdx_sequence_manager.h"
#include "assembly/mdx_model_builder.h"

#include <whiteout/models/mdx/writer.h>
#include <MaxDirectories.h>
#include <cmath>

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
    auto mn=gs(L"ModelName"); if(!mn.empty()) opts.modelName=wcharToUtf8(mn.c_str());
    auto ver=gs(L"ExportVersion"); if(!ver.empty()){int v=_wtoi(ver.c_str()); opts.version=(v==2||v==1200)?1200:800;}
    auto m=gs(L"MergeSimilarMeshes"); if(!m.empty()) opts.mergeGeosets=iniBool(m);
    auto fn=gs(L"FixNormals"); if(!fn.empty()) opts.fixNormals=iniBool(fn);
    auto fsn=gs(L"FixSharedNormals"); if(!fsn.empty()) opts.fixSharedNormals=iniBool(fsn);
    auto th=gs(L"Threshold"); if(!th.empty()) opts.fixNormalsThreshold=(float)_wtof(th.c_str());
    auto sm=gs(L"ExportSmoothgroups"); if(!sm.empty()) opts.exportSmoothgroups=iniBool(sm);
    auto kb=gs(L"KeepUnusedBonesHelpers"); if(!kb.empty()) opts.keepUnusedBonesHelpers=iniBool(kb);
    auto nq=gs(L"DisableSkinQuantize"); if(!nq.empty()) opts.disableSkinQuantize=iniBool(nq);
    auto et=gs(L"ExtentsType"); if(!et.empty()) opts.extentsType=_wtoi(et.c_str());
    auto ep=gs(L"ExtentsPrecision"); if(!ep.empty()) opts.extentsPrecision=_wtoi(ep.c_str());
}

void collectSkinBoneNodes(const std::vector<core::SceneNode>& sceneNodes, std::vector<INode*>& out) {
    for (auto& sn : sceneNodes) {
        if (sn.category==core::NodeCategory::Mesh && sn.maxNode) {
            auto* skin=core::ModifierReader::findSkin(sn.maxNode);
            if (!skin) continue;
            auto* ctx=skin->GetContextInterface(sn.maxNode);
            if (!ctx) continue;
            for (int i=0;i<skin->GetNumBones();i++) { INode* b=skin->GetBone(i); if(b) out.push_back(b); }
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

} // namespace

int MdxExporterPlugin::DoExport(const TCHAR* name, ExpInterface*, Interface* gi, BOOL suppressPrompts, DWORD)
{
    core::ExportErrorReporter reporter;
    MdxExportOptions opts;

    ELOG << "==== MDLXExporter::DoExport ====\n";
    ELOG << "File: " << wcharToUtf8(name) << "\n"; EFLUSH;

    loadOptionsFromINI(gi, opts);
    ELOG << "Options: version=" << opts.version << " merge=" << opts.mergeGeosets
         << " anim=" << opts.exportAnimations << " skinQuant=" << !opts.disableSkinQuantize << "\n";

    if (!suppressPrompts) {
        if (!showExportDialog(GetDllInstance(), gi->GetMAXHWnd(), opts)) return IMPEXP_CANCEL;
        ELOG << "Dialog: version=" << opts.version << "\n";
    }

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
    core::MeshExtractor meshEx;
    for (auto& sn:sceneResult.nodes) {
        if(sn.category!=core::NodeCategory::Mesh) continue;
        auto mesh=meshEx.extract(sn.maxNode,sn.nodeIndex,0,reporter);
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
            int32_t ni=(int32_t)irModel.nodes.size();
            ir::IRModel::Node irn; irn.name="Mesh "+mesh.name; irn.maxNode=sn.maxNode;
            irn.worldTM=sn.maxNode->GetNodeTM(0); irn.pivotPoint=irn.worldTM.GetTrans();
            INode* par=sn.maxNode->GetParentNode();
            if(par&&!par->IsRootNode()) for(int32_t j=0;j<(int32_t)irModel.nodes.size();++j) if(irModel.nodes[j].maxNode==par){irn.parentIndex=j;break;}
            irModel.nodes.push_back(std::move(irn));
            ir::Bone sb; sb.name="Mesh "+mesh.name; sb.nodeIndex=ni; sb.pivotPoint=irModel.nodes[ni].pivotPoint; sb.bindPose=irModel.nodes[ni].worldTM;
            irModel.bones.push_back(std::move(sb));
            for(auto& v:mesh.vertices){v.skinInfluences.clear();v.skinInfluences.push_back({ni,1.0f});}
            ELOG << "  mesh '" << mesh.name << "' unskinned -> synth bone irNode[" << ni << "]\n";
        }
        irModel.meshes.push_back(std::move(mesh));
    } EFLUSH;

    // MDX extractors
    ELOG << "\n==== MDX Extractors ====\n";
    auto mtlMap=mdx_extract::extractMaterials(sceneResult.nodes,irModel,reporter);
    mdx_extract::extractLights(sceneResult.nodes,irModel,reporter);
    mdx_extract::extractAttachments(sceneResult.nodes,irModel,reporter);
    mdx_extract::extractParticles1(sceneResult.nodes,irModel,reporter);
    mdx_extract::extractParticles2(sceneResult.nodes,irModel,reporter);
    mdx_extract::extractRibbons(sceneResult.nodes,irModel,mtlMap,reporter);
    mdx_extract::extractEvents(sceneResult.nodes,irModel,reporter);
    mdx_extract::extractCollisions(sceneResult.nodes,irModel,reporter);
    mdx_extract::extractVertexColors(sceneResult.nodes,irModel,reporter);
    if(opts.version>=1200){mdx_extract::extractPopcorn(sceneResult.nodes,irModel,reporter);mdx_extract::extractFaceFX(sceneResult.nodes,irModel,reporter);}
    ELOG << "Materials=" << irModel.materials.size() << " Textures=" << irModel.textures.size()
         << " Lights=" << irModel.lights.size() << " Attach=" << irModel.attachments.size()
         << " PE=" << irModel.particleEmitters.size() << " Ribbons=" << irModel.ribbonEmitters.size()
         << " Events=" << irModel.eventObjects.size() << " Collisions=" << irModel.collisionShapes.size() << "\n";
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
    if(irModel.sequences.empty()) ELOG << "  *** NO SEQUENCES! Check CA or NoteTracks on root ***\n";
    EFLUSH;

    // Bake
    ELOG << "\n==== Animation Bake ====\n";
    if(opts.exportAnimations && !irModel.sequences.empty()) {
        core::AnimDispatcher dispatcher;
        core::AnimDispatcher::Config cfg; cfg.tickInterval=opts.animSampleInterval; cfg.angleThreshold=opts.ikRefinementThreshold;
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
        }
        if(!na.rotation.empty()){
            auto& k=na.rotation.keys[0]; bool id=fabsf(k.value.x)<0.01f&&fabsf(k.value.y)<0.01f&&fabsf(k.value.z)<0.01f&&fabsf(fabsf(k.value.w)-1.0f)<0.01f;
            ELOG << "    RT key0 t=" << k.time << " q=(" << k.value.x << "," << k.value.y << "," << k.value.z << "," << k.value.w << ") " << (id?"DELTA":"ABSOLUTE") << "\n";
            if(id) dR++; else aR++;
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
    if(opts.optimizeVertices){core::VertexOptimizer vo; for(auto& m:irModel.meshes) vo.optimize(m,opts.vertexMergeThreshold);}
    if(opts.optimizeKeyframes){core::KeyframeOptimizer ko; for(auto& na:irModel.nodeAnimations){ko.optimize(na.translation);ko.optimize(na.rotation);ko.optimize(na.scale);} for(auto& ft:irModel.floatTracks)ko.optimize(ft); for(auto& vt:irModel.vec3Tracks)ko.optimize(vt);}

    // Build
    ELOG << "\n==== Model Build ====\n"; EFLUSH;
    MdxModelBuilder builder;
    whiteout::mdx::Model mdxModel = builder.build(irModel, opts);
    ELOG << "Result: " << mdxModel.bones.size() << " bones, " << mdxModel.helpers.size() << " helpers, "
         << mdxModel.geosets.size() << " geosets, " << mdxModel.sequences.size() << " sequences\n";
    ELOG << "Pivots: " << mdxModel.pivotPoints.size() << "\n";
    for(size_t i=0;i<mdxModel.pivotPoints.size();i++){auto& p=mdxModel.pivotPoints[i]; ELOG<<"  pivot["<<i<<"]=("<<p.x<<","<<p.y<<","<<p.z<<")\n";}
    EFLUSH;

    // Name + Write
    std::string filePath=wcharToUtf8(name);
    if(!opts.modelName.empty()) mdxModel.modelName=opts.modelName;
    else { auto sl=filePath.find_last_of("/\\"); auto st=(sl!=std::string::npos)?filePath.substr(sl+1):filePath; auto dt=st.rfind('.'); if(dt!=std::string::npos) st=st.substr(0,dt); mdxModel.modelName=st; }

    whiteout::mdx::Writer writer;
    writer.write(filePath, mdxModel);
    ELOG << "\n==== Written: " << filePath << " ====\n";

    if(reporter.hasWarnings()||reporter.hasErrors()) reporter.showSummaryDialog(gi->GetMAXHWnd());

    ELOG << "\n==== Export Complete ====\n"; EFLUSH;
    return IMPEXP_SUCCESS;
}
