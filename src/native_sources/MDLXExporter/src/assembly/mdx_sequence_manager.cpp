// MDLXExporter — Sequence manager implementation
// DEBUG: Logs to %TEMP%\mdlx_export_debug.log
#include "mdx_sequence_manager.h"
#include <wdx_text.h>
#include "../mdx_export_debug.h"

#include <icustattribcontainer.h>
#include <custattrib.h>
#include <iparamb2.h>
#include <inode.h>
#include <notetrck.h>
#include <cstdio>
#include <algorithm>
#include <unordered_map>
#include <cwctype>
#include <locale.h>

namespace {
// "Rarity=0.5" / "MoveSpeed=270.5" in note keys always use '.': _wtof follows
// the host's LC_NUMERIC, and 3ds Max 2027 on a German Windows runs with ','
// (270.5 read as 270). The "C" locale variant ignores that.
float noteFloat(const wchar_t* s) {
    static _locale_t c = _create_locale(LC_NUMERIC, "C");
    return static_cast<float>(c ? _wtof_l(s, c) : _wtof(s));
}
enum SeqParamID : ParamID { PID_SeqNames=0,PID_StartFrames=1,PID_EndFrames=2,PID_NonLooping=3,PID_Rarity=4,PID_MoveSpeed=5,PID_SeqExtents=6,PID_SharedGroup=7 };

IParamBlock2* findSequenceCA(INode* rootNode) {
    ICustAttribContainer* cac = rootNode->GetCustAttribContainer();
    if (!cac) { ELOG << "  No CustAttrib container on root node\n"; return nullptr; }
    ELOG << "  Root has " << cac->GetNumCustAttribs() << " custom attributes\n";
    for (int i=0;i<cac->GetNumCustAttribs();i++) {
        CustAttrib* ca=cac->GetCustAttrib(i); if(!ca) continue;
        IParamBlock2* pb=ca->GetParamBlock(0); if(!pb) continue;
        ParamBlockDesc2* desc=pb->GetDesc(); if(!desc) continue;
        ELOG << "    CA[" << i << "] paramCount=" << desc->Count();
        if (desc->Count()>=7) {
            const ParamDef& pd=desc->GetParamDef(PID_SeqNames);
            ELOG << " type[0]=" << pd.type;
            if (pd.type==TYPE_STRING_TAB) {
                ELOG << " -> MATCH (WhiteoutDexSequenceData)\n";
                return pb;
            }
        }
        ELOG << " -> no match\n";
    }
    ELOG << "  WhiteoutDexSequenceData CA not found\n";
    return nullptr;
}

std::string wstrToUtf8(const wchar_t* wstr) {
    if(!wstr||!wstr[0]) return {};
    int len=WideCharToMultiByte(CP_UTF8,0,wstr,-1,nullptr,0,nullptr,nullptr);
    if(len<=0) return {};
    std::string result(static_cast<size_t>(len-1),'\0');
    WideCharToMultiByte(CP_UTF8,0,wstr,-1,result.data(),len,nullptr,nullptr);
    return result;
}
void parseExtentString(const wchar_t* str,float& bound,Point3& mn,Point3& mx) {
    if(!str||!str[0]) return;
    // space separated, '.' decimals; a scene imported where LC_NUMERIC used
    // ',' (older builds under Max 2027 on a German Windows) wrote commas
    std::wstring v(str); for(auto& ch:v) if(ch==L',') ch=L'.';
    static _locale_t c=_create_locale(LC_NUMERIC,"C");
    _swscanf_s_l(v.c_str(),L"%f %f %f %f %f %f %f",c,&bound,&mn.x,&mn.y,&mn.z,&mx.x,&mx.y,&mx.z);
}

// Note track fallback
std::vector<std::wstring> tokenizeNoteValue(const wchar_t* str) {
    std::vector<std::wstring> tokens; if(!str) return tokens;
    std::wstring cur;
    for(const wchar_t* p=str;*p;++p){if(*p==L'"'||*p==L'\r'||*p==L'\n'||*p==L'\t'){if(!cur.empty()){tokens.push_back(cur);cur.clear();}}else cur+=*p;}
    if(!cur.empty()) tokens.push_back(cur); return tokens;
}
bool wcsieq(const std::wstring& a,const wchar_t* b){size_t bl=wcslen(b);if(a.size()!=bl)return false;for(size_t i=0;i<bl;i++)if(std::towlower(a[i])!=std::towlower(b[i]))return false;return true;}
bool wcsistartswith(const std::wstring& s,const wchar_t* prefix,size_t prefixLen){if(s.size()<prefixLen)return false;for(size_t i=0;i<prefixLen;i++)if(std::towlower(s[i])!=std::towlower(prefix[i]))return false;return true;}

struct NoteKeyEntry { std::wstring value; TimeValue time; };

std::vector<ir::Sequence> extractFromNoteTracks(INode* rootNode) {
    std::vector<ir::Sequence> sequences;
    ELOG << "  Trying Note Track fallback...\n";
    if(!rootNode->HasNoteTracks()||rootNode->NumNoteTracks()==0){ELOG << "  No note tracks\n"; return sequences;}
    auto* nt=dynamic_cast<DefNoteTrack*>(rootNode->GetNoteTrack(0)); if(!nt) return sequences;
    int numKeys=nt->keys.Count(); ELOG << "  Note track: " << numKeys << " keys\n";
    if(numKeys==0) return sequences;
    std::unordered_map<std::wstring,std::vector<NoteKeyEntry>> groups; std::vector<std::wstring> groupOrder;
    for(int i=0;i<numKeys;i++){NoteKey* nk=nt->keys[i];if(!nk)continue;const wchar_t* val=nk->note.data();if(!val||!val[0])continue;std::wstring key(val);NoteKeyEntry entry;entry.value=key;entry.time=nk->time;if(groups.find(key)==groups.end())groupOrder.push_back(key);groups[key].push_back(entry);}
    struct SeqParsed{std::string name;TimeValue startTime,endTime;bool nonLooping;float rarity,moveSpeed;};
    std::vector<SeqParsed> parsed;
    for(auto& keyStr:groupOrder){auto& grp=groups[keyStr];if(grp.size()<2)continue;std::sort(grp.begin(),grp.end(),[](const NoteKeyEntry& a,const NoteKeyEntry& b){return a.time<b.time;});size_t pairCount=grp.size()/2;for(size_t p=0;p<pairCount;p++){auto& k1=grp[p*2];auto& k2=grp[p*2+1];auto tokens=tokenizeNoteValue(k1.value.c_str());std::string seqName=tokens.empty()?"Unknown":wdx::text::wideToMdx(tokens[0].c_str());bool nonLoop=false;float rare=0,speed=0;for(size_t t=1;t<tokens.size();t++){if(wcsieq(tokens[t],L"NonLooping"))nonLoop=true;else if(wcsistartswith(tokens[t],L"Rarity",6))rare=noteFloat(tokens[t].c_str()+6);else if(wcsistartswith(tokens[t],L"MoveSpeed",9))speed=noteFloat(tokens[t].c_str()+9);}SeqParsed sp;sp.name=seqName;sp.startTime=std::min(k1.time,k2.time);sp.endTime=std::max(k1.time,k2.time);sp.nonLooping=nonLoop;sp.rarity=rare;sp.moveSpeed=speed;parsed.push_back(std::move(sp));}}
    std::sort(parsed.begin(),parsed.end(),[](const SeqParsed& a,const SeqParsed& b){return a.startTime<b.startTime;});
    for(auto& sp:parsed){ir::Sequence seq;seq.name=sp.name;seq.startTime=sp.startTime;seq.endTime=sp.endTime;seq.isLooping=!sp.nonLooping;seq.rarity=sp.rarity;seq.moveSpeed=sp.moveSpeed;sequences.push_back(std::move(seq));}
    return sequences;
}
} // namespace

std::vector<ir::Sequence> MdxSequenceManager::extractSequences(Interface* gi) {
    std::vector<ir::Sequence> sequences;
    ELOG << "\n==== SequenceManager::extractSequences ====\n";
    INode* rootNode=gi->GetRootNode();
    if(!rootNode){ELOG << "  No root node!\n"; return sequences;}

    IParamBlock2* pb=findSequenceCA(rootNode);
    if(!pb){
        auto result=extractFromNoteTracks(rootNode);
        ELOG << "  Note tracks extracted: " << result.size() << " sequences\n";
        for(auto& s:result) ELOG << "    '" << s.name << "' " << s.startTime << "-" << s.endTime << "\n";
        ELOG << "==== end SequenceManager ====\n\n"; EFLUSH;
        return result;
    }

    int count=pb->Count(PID_SeqNames);
    int tpf=GetTicksPerFrame();
    ELOG << "  CA found: " << count << " sequences, ticksPerFrame=" << tpf << "\n";
    sequences.reserve(count);
    for(int i=0;i<count;i++){
        ir::Sequence seq; Interval valid=FOREVER;
        const MCHAR* name=nullptr; pb->GetValue(PID_SeqNames,0,name,valid,i); seq.name=wdx::text::wideToMdx(name);
        int sf=0,ef=0; pb->GetValue(PID_StartFrames,0,sf,valid,i); pb->GetValue(PID_EndFrames,0,ef,valid,i);
        seq.startTime=sf*tpf; seq.endTime=ef*tpf;
        int nl=0; pb->GetValue(PID_NonLooping,0,nl,valid,i); seq.isLooping=(nl==0);
        pb->GetValue(PID_Rarity,0,seq.rarity,valid,i); pb->GetValue(PID_MoveSpeed,0,seq.moveSpeed,valid,i);
        const MCHAR* extStr=nullptr; pb->GetValue(PID_SeqExtents,0,extStr,valid,i);
        parseExtentString(extStr,seq.extentRadius,seq.extentMin,seq.extentMax);
        ELOG << "  [" << i << "] '" << seq.name << "' frames=" << sf << "-" << ef
             << " ticks=" << seq.startTime << "-" << seq.endTime
             << " " << (seq.isLooping?"loop":"nonloop") << "\n";
        sequences.push_back(std::move(seq));
    }
    ELOG << "==== end SequenceManager ====\n\n"; EFLUSH;
    return sequences;
}

// Sequence extents (SEQS) from the exported meshes as Max plays each
// sequence. The scene keeps the extents an imported model came with (Sequence
// Manager "seqExtents") and the exporter wrote them back unchecked: fara14.max
// was moved ~300 units after its import, so six sequences carried boxes that
// missed the model completely (one with min Y above max Y), and two sequences
// added later had none and got the bind pose, which Spell Throw leaves by 110
// units. The game culls a unit by that box. A stored box stays while it holds
// the meshes at the sequence start (an unedited import keeps its own values);
// otherwise the meshes are evaluated on every frame of the sequence (at most
// 61 times) and their world bounds are used; 17 samples still missed 11
// units of Spell Throw's swing. GetDeformBBox with the node's object TM is
// the precise box (maxsdk/include/object.h, Object::GetDeformBBox).
void MdxSequenceManager::sampleExtents(std::vector<ir::Sequence>& sequences,
                                       const std::vector<INode*>& meshNodes) {
    if (meshNodes.empty()) return;
    auto boundsAt = [&](TimeValue t) {
        Box3 all;
        for (INode* n : meshNodes) {
            ObjectState os = n->EvalWorldState(t);
            if (!os.obj) continue;
            Matrix3 tm = n->GetObjTMAfterWSM(t);
            Box3 b;
            os.obj->GetDeformBBox(t, b, &tm);
            if (!b.IsEmpty()) all += b;
        }
        return all;
    };
    const TimeValue tpf = std::max(GetTicksPerFrame(), 1);
    for (auto& seq : sequences) {
        if (seq.endTime < seq.startTime) continue;
        Box3 atStart = boundsAt(seq.startTime);
        if (atStart.IsEmpty()) continue;
        if (seq.extentRadius > 0.0f) {
            const Point3 mn(std::min(seq.extentMin.x, seq.extentMax.x), std::min(seq.extentMin.y, seq.extentMax.y),
                            std::min(seq.extentMin.z, seq.extentMax.z));
            const Point3 mx(std::max(seq.extentMin.x, seq.extentMax.x), std::max(seq.extentMin.y, seq.extentMax.y),
                            std::max(seq.extentMin.z, seq.extentMax.z));
            const float tol = 0.5f + 0.01f * Length(atStart.pmax - atStart.pmin);
            bool holds = true;
            for (int k = 0; k < 3; ++k)
                holds = holds && mn[k] <= atStart.pmin[k] + tol && mx[k] >= atStart.pmax[k] - tol;
            if (holds) continue;
            ELOG << "  seq '" << seq.name << "' stored extent does not hold the meshes -> sampled\n";
        }
        const TimeValue span = seq.endTime - seq.startTime;
        const int n = static_cast<int>(std::min<TimeValue>(60, span / tpf));
        Box3 box = atStart;
        for (int k = 1; k <= n; ++k)
            box += boundsAt(seq.startTime + static_cast<TimeValue>(static_cast<int64_t>(span) * k / n));
        seq.extentMin = box.pmin;
        seq.extentMax = box.pmax;
        seq.extentRadius = 0.5f * Length(box.pmax - box.pmin);
        ELOG << "  seq '" << seq.name << "' extent sampled at " << (n + 1) << " times: ("
             << box.pmin.x << "," << box.pmin.y << "," << box.pmin.z << ")..("
             << box.pmax.x << "," << box.pmax.y << "," << box.pmax.z << ")\n";
    }
    EFLUSH;
}
