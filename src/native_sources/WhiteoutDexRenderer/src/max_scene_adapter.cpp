// ============================================================================
// WhiteoutDex Max Scene Adapter — Implements IModelSource for 3ds Max scenes
// Refactored from extract.cpp. All Max SDK coupling lives here.
// ============================================================================

#include "max_scene_adapter.h"

#include <maxscript/maxscript.h>
#include <maxscript/foundation/numbers.h>
#include <Windows.h>
#include <chrono>
#include <algorithm>
#include <cwchar>

using namespace WhiteoutDex;

static Point3 GetVNormal(Mesh& mesh, int faceIdx, int vertIdx);

// ============================================================================
// Ctor / Dtor
// ============================================================================

MaxSceneAdapter::MaxSceneAdapter() {}
MaxSceneAdapter::~MaxSceneAdapter() {}

// ============================================================================
// PackMatrix: Matrix3 → 16 floats (row-major 4x4)
// ============================================================================

void MaxSceneAdapter::PackMatrix(const Matrix3& tm, float* dst) {
    for (int r = 0; r < 3; r++) {
        Point3 row = tm.GetRow(r);
        dst[r*4+0] = row.x; dst[r*4+1] = row.y; dst[r*4+2] = row.z; dst[r*4+3] = 0.0f;
    }
    Point3 trans = tm.GetRow(3);
    dst[12] = trans.x; dst[13] = trans.y; dst[14] = trans.z; dst[15] = 1.0f;
}

// ============================================================================
// FilterMode Mapping
// ============================================================================

int MaxSceneAdapter::MapMaterialFilterMode(int wc3fm) {
    // Wc3Material.ms dropdown (1-based): 1=None, 2=Transparent, 3=Blend,
    //   4=Additive, 5=AddAlpha, 6=Modulate, 7=Modulate2x
    // Renderer FilterMode (0-based): 0=None .. 6=Modulate2x
    int fm = wc3fm - 1;
    if (fm < 0) fm = 0;
    if (fm > 6) fm = 6;
    return fm;
}

int MaxSceneAdapter::MapParticleFilterMode(int bpfm) {
    // Wc3Particles2 PB_BLEND: 0=Blend,1=Add,2=Modulate,3=Mod2X,4=AlphaKey
    // Renderer FilterMode:    0=None,1=Transparent,2=Blend,3=Additive,
    //                         4=AddAlpha,5=Modulate,6=Modulate2x
    switch (bpfm) {
        case 0: return 2;  // Blend
        case 1: return 3;  // Add → Additive
        case 2: return 5;  // Modulate
        case 3: return 6;  // Mod2X → Modulate2x
        case 4: return 1;  // AlphaKey → Transparent
        default: return 2; // fallback Blend
    }
}

int MaxSceneAdapter::MapRibbonFilterMode(int rbfm) {
    // Matches Wc3Material-style 1-based dropdown → 0-based renderer enum
    switch (rbfm) {
        case 1: return 0; case 2: return 1; case 3: return 2;
        case 4: return 3; case 5: return 4; case 6: return 5;
        case 7: return 6; default: return 2;
    }
}

// ============================================================================
// IParamBlock2 helpers (unchanged from extract.cpp)
// ============================================================================

bool MaxSceneAdapter::PB2Float(Animatable* anim, const wchar_t* name, TimeValue t, float& out) {
    if (!anim) return false;
    for (int pb = 0; pb < anim->NumParamBlocks(); pb++) {
        IParamBlock2* pblock = anim->GetParamBlock(pb);
        if (!pblock) continue;
        for (int p = 0; p < pblock->NumParams(); p++) {
            ParamID pid = pblock->IndextoID(p);
            ParamDef& def = pblock->GetParamDef(pid);
            if (def.int_name && _wcsicmp(def.int_name, name) == 0) {
                Interval iv = FOREVER;
                if (def.type == TYPE_INT) { int v=0; pblock->GetValue(pid,t,v,iv); out=(float)v; }
                else { pblock->GetValue(pid, t, out, iv); }
                return true;
            }
        }
    }
    return false;
}

bool MaxSceneAdapter::PB2Int(Animatable* anim, const wchar_t* name, TimeValue t, int& out) {
    if (!anim) return false;
    for (int pb = 0; pb < anim->NumParamBlocks(); pb++) {
        IParamBlock2* pblock = anim->GetParamBlock(pb);
        if (!pblock) continue;
        for (int p = 0; p < pblock->NumParams(); p++) {
            ParamID pid = pblock->IndextoID(p);
            ParamDef& def = pblock->GetParamDef(pid);
            if (def.int_name && _wcsicmp(def.int_name, name) == 0) {
                Interval iv = FOREVER;
                if (def.type == TYPE_FLOAT) { float v=0; pblock->GetValue(pid,t,v,iv); out=(int)v; }
                else { pblock->GetValue(pid, t, out, iv); }
                return true;
            }
        }
    }
    return false;
}

bool MaxSceneAdapter::PB2Bool(Animatable* anim, const wchar_t* name, TimeValue t, BOOL& out) {
    if (!anim) return false;
    for (int pb = 0; pb < anim->NumParamBlocks(); pb++) {
        IParamBlock2* pblock = anim->GetParamBlock(pb);
        if (!pblock) continue;
        for (int p = 0; p < pblock->NumParams(); p++) {
            ParamID pid = pblock->IndextoID(p);
            ParamDef& def = pblock->GetParamDef(pid);
            if (def.int_name && _wcsicmp(def.int_name, name) == 0) {
                Interval iv = FOREVER;
                int val = 0; pblock->GetValue(pid, t, val, iv);
                out = val ? TRUE : FALSE;
                return true;
            }
        }
    }
    return false;
}

bool MaxSceneAdapter::PB2Color(Animatable* anim, const wchar_t* name, TimeValue t, Color& out) {
    if (!anim) return false;
    for (int pb = 0; pb < anim->NumParamBlocks(); pb++) {
        IParamBlock2* pblock = anim->GetParamBlock(pb);
        if (!pblock) continue;
        for (int p = 0; p < pblock->NumParams(); p++) {
            ParamID pid = pblock->IndextoID(p);
            ParamDef& def = pblock->GetParamDef(pid);
            if (def.int_name && _wcsicmp(def.int_name, name) == 0) {
                Color cv = pblock->GetColor(pid, t);
                if (cv.r > 1.0f || cv.g > 1.0f || cv.b > 1.0f)
                    out = Color(cv.r / 255.0f, cv.g / 255.0f, cv.b / 255.0f);
                else
                    out = cv;
                return true;
            }
        }
    }
    return false;
}

bool MaxSceneAdapter::PB2Texmap(Animatable* anim, const wchar_t* name, Texmap*& out) {
    if (!anim) return false;
    for (int pb = 0; pb < anim->NumParamBlocks(); pb++) {
        IParamBlock2* pblock = anim->GetParamBlock(pb);
        if (!pblock) continue;
        for (int p = 0; p < pblock->NumParams(); p++) {
            ParamID pid = pblock->IndextoID(p);
            ParamDef& def = pblock->GetParamDef(pid);
            if (def.int_name && _wcsicmp(def.int_name, name) == 0) {
                Interval iv = FOREVER;
                pblock->GetValue(pid, 0, out, iv);
                return true;
            }
        }
    }
    return false;
}

Object* MaxSceneAdapter::GetBaseObject(INode* node) {
    if (!node) return nullptr;
    Object* obj = node->GetObjectRef();
    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID)
        obj = static_cast<IDerivedObject*>(obj)->GetObjRef();
    return obj;
}

Modifier* MaxSceneAdapter::FindSkinModifier(INode* node) { return FindModifierByClassID(node, SKIN_CLASSID); }

Modifier* MaxSceneAdapter::FindModifierByClassID(INode* node, Class_ID cid) {
    if (!node) return nullptr;
    Object* objRef = node->GetObjectRef();
    while (objRef && objRef->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* dobj = static_cast<IDerivedObject*>(objRef);
        for (int i = 0; i < dobj->NumModifiers(); i++) {
            Modifier* mod = dobj->GetModifier(i);
            if (mod && mod->ClassID() == cid) return mod;
        }
        objRef = dobj->GetObjRef();
    }
    return nullptr;
}

std::wstring MaxSceneAdapter::GetMaxFilePath() {
    Interface* ip = GetCOREInterface();
    if (!ip) return L"";
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
    const MCHAR* fp = ip->GetCurFilePath().data();
#else
    const MCHAR* fp = ip->GetCurFilePath();
#endif
    if (!fp || !fp[0]) return L"";
    std::wstring path(fp);
    size_t pos = path.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? path.substr(0, pos + 1) : L"";
}

// ============================================================================
// Texture loading (stores pixel data for GetTextures() instead of renderer calls)
// ============================================================================

int MaxSceneAdapter::LoadTexture(const std::wstring& filePath, int replaceableId) {
    if (replaceableId == 1 || replaceableId == 2) {
        std::wstring key = (replaceableId == 1) ? L"__TEAMCOLOR__" : L"__TEAMGLOW__";
        auto it = texPathToId_.find(key);
        if (it != texPathToId_.end()) return it->second;
        int id = nextTexId_++;
        texPathToId_[key] = id;
        std::vector<uint8_t> rgba(4*4*4, 0);
        for (int i = 0; i < 16; i++) { rgba[i*4]=255; rgba[i*4+1]=0; rgba[i*4+2]=0; rgba[i*4+3]=255; }
        loadedTextures_.push_back({id, replaceableId, rgba, 4, 4});
        TextureEntry te; te.textureId=id; te.replaceableId=replaceableId;
        texEntries_.push_back(te);
        return id;
    }

    if (filePath.empty()) return -1;
    auto it = texPathToId_.find(filePath);
    if (it != texPathToId_.end()) return it->second;

    BitmapInfo bi; bi.SetName(filePath.c_str());
    BMMRES status;
    Bitmap* bmp = TheManager->Load(&bi, &status);

    if (!bmp || status != BMMRES_SUCCESS) {
        int id = nextTexId_++;
        texPathToId_[filePath] = id;
        std::vector<uint8_t> rgba(4*4*4, 0);
        for (int i = 0; i < 16; i++) { rgba[i*4]=255; rgba[i*4+1]=0; rgba[i*4+2]=255; rgba[i*4+3]=255; }
        loadedTextures_.push_back({id, 0, rgba, 4, 4});
        TextureEntry te; te.textureId=id; te.filePath=filePath;
        texEntries_.push_back(te);
        mprintf(_M("  Texture %d: [missing] %s\n"), id, filePath.c_str());
        return id;
    }

    int w = bmp->Width(), h = bmp->Height();
    int id = nextTexId_++;
    texPathToId_[filePath] = id;

    std::vector<uint8_t> rgba(w*h*4);
    BMM_Color_64* line = new BMM_Color_64[w];
    long long sumR=0, sumG=0, sumB=0, sumA=0;
    for (int y = 0; y < h; y++) {
        bmp->GetPixels(0, y, w, line);
        for (int x = 0; x < w; x++) {
            int idx = (y*w+x)*4;
            rgba[idx]=(uint8_t)(line[x].r>>8); rgba[idx+1]=(uint8_t)(line[x].g>>8);
            rgba[idx+2]=(uint8_t)(line[x].b>>8); rgba[idx+3]=(uint8_t)(line[x].a>>8);
            sumR+=rgba[idx]; sumG+=rgba[idx+1]; sumB+=rgba[idx+2]; sumA+=rgba[idx+3];
        }
    }
    delete[] line;
    bmp->DeleteThis();

    int total = w*h;
    mprintf(_M("  Texture %d: %dx%d avgRGBA=[%d,%d,%d,%d] '%s'\n"), id, w, h,
            (int)(sumR/total), (int)(sumG/total), (int)(sumB/total), (int)(sumA/total),
            filePath.c_str());

    loadedTextures_.push_back({id, 0, std::move(rgba), w, h});
    TextureEntry te; te.textureId=id; te.filePath=filePath;
    texEntries_.push_back(te);
    return id;
}

int MaxSceneAdapter::LoadTextureWithTeamColor(const std::wstring& filePath, int tcR, int tcG, int tcB) {
    if (filePath.empty()) return -1;
    std::wstring key = L"__TC__" + filePath;
    auto it = texPathToId_.find(key);
    if (it != texPathToId_.end()) return it->second;

    mprintf(_M("  [TC] Loading BLP for compositing: '%s'\n"), filePath.c_str());

    BitmapInfo bi; bi.SetName(filePath.c_str());
    BMMRES status;
    Bitmap* bmp = TheManager->Load(&bi, &status);

    if (!bmp || status != BMMRES_SUCCESS) {
        int id = nextTexId_++;
        texPathToId_[key] = id;
        std::vector<uint8_t> rgba(4*4*4);
        for (int i = 0; i < 16; i++) {
            rgba[i*4]=(uint8_t)tcR; rgba[i*4+1]=(uint8_t)tcG;
            rgba[i*4+2]=(uint8_t)tcB; rgba[i*4+3]=255;
        }
        loadedTextures_.push_back({id, 0, rgba, 4, 4});
        TextureEntry te; te.textureId=id; te.filePath=filePath;
        texEntries_.push_back(te);
        mprintf(_M("  [TC] FAILED to load bitmap! Using solid red fallback. Status=%d\n"), (int)status);
        return id;
    }

    int w = bmp->Width(), h = bmp->Height();
    int id = nextTexId_++;
    texPathToId_[key] = id;

    std::vector<uint8_t> rgba(w*h*4);
    BMM_Color_64* line = new BMM_Color_64[w];
    int alphaZero = 0, alphaFull = 0, alphaMid = 0;
    long long alphaSum = 0;

    for (int y = 0; y < h; y++) {
        bmp->GetPixels(0, y, w, line);
        for (int x = 0; x < w; x++) {
            int idx = (y*w+x)*4;
            uint8_t baseR = (uint8_t)(line[x].r >> 8);
            uint8_t baseG = (uint8_t)(line[x].g >> 8);
            uint8_t baseB = (uint8_t)(line[x].b >> 8);
            uint8_t baseA = (uint8_t)(line[x].a >> 8);
            alphaSum += baseA;
            if (baseA == 0) alphaZero++;
            else if (baseA >= 254) alphaFull++;
            else alphaMid++;
            float t = baseA / 255.0f;
            rgba[idx]   = (uint8_t)(tcR * (1.0f - t) + baseR * t);
            rgba[idx+1] = (uint8_t)(tcG * (1.0f - t) + baseG * t);
            rgba[idx+2] = (uint8_t)(tcB * (1.0f - t) + baseB * t);
            rgba[idx+3] = 255;
        }
    }
    delete[] line;
    bmp->DeleteThis();

    int totalPixels = w * h;
    int avgAlpha = totalPixels > 0 ? (int)(alphaSum / totalPixels) : 0;
    mprintf(_M("  [TC] %dx%d — alpha stats: zero=%d, full=%d, mid=%d, avg=%d (of %d pixels)\n"),
            w, h, alphaZero, alphaFull, alphaMid, avgAlpha, totalPixels);
    if (alphaFull == totalPixels)
        mprintf(_M("  [TC] *** WARNING: ALL pixels have alpha=255! TeamColor will be invisible! ***\n"));
    if (alphaZero == totalPixels)
        mprintf(_M("  [TC] *** WARNING: ALL pixels have alpha=0! Entire texture will be TeamColor! ***\n"));

    loadedTextures_.push_back({id, 0, std::move(rgba), w, h});
    TextureEntry te; te.textureId=id; te.filePath=filePath;
    texEntries_.push_back(te);
    mprintf(_M("  Texture %d: %dx%d [TeamColor] '%s'\n"), id, w, h, filePath.c_str());
    return id;
}

int MaxSceneAdapter::GenerateTeamGlowTexture(int tcR, int tcG, int tcB) {
    std::wstring key = L"__TEAMGLOW_GEN__";
    auto it = texPathToId_.find(key);
    if (it != texPathToId_.end()) return it->second;

    int id = nextTexId_++;
    texPathToId_[key] = id;

    const int size = 64;
    std::vector<uint8_t> rgba(size * size * 4, 0);
    float center = (size - 1) / 2.0f;
    float maxDist = center;

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float dx = x - center;
            float dy = y - center;
            float dist = sqrtf(dx*dx + dy*dy) / maxDist;
            if (dist > 1.0f) dist = 1.0f;
            float intensity = (1.0f - dist) * (1.0f - dist);
            int idx = (y * size + x) * 4;
            rgba[idx]   = (uint8_t)(tcR * intensity);
            rgba[idx+1] = (uint8_t)(tcG * intensity);
            rgba[idx+2] = (uint8_t)(tcB * intensity);
            rgba[idx+3] = (uint8_t)(255 * intensity);
        }
    }

    loadedTextures_.push_back({id, 0, std::move(rgba), size, size});
    TextureEntry te; te.textureId = id; te.replaceableId = 2;
    texEntries_.push_back(te);
    mprintf(_M("  Texture %d: %dx%d [TeamGlow generated]\n"), id, size, size);
    return id;
}

// ============================================================================
// CollectScene — called once on main thread before Get*()
// ============================================================================

void MaxSceneAdapter::CollectScene() {
    nextTexId_ = 0; nextMatId_ = 0; texPathToId_.clear(); mtlToId_.clear();
    loadedTextures_.clear(); texEntries_.clear();
    bones_.clear(); boneNameToIdx_.clear(); geosets_.clear();
    materials_.clear(); particles_.clear(); ribbons_.clear(); collisions_.clear();

    CollectGeometry();
    CollectMaterials();
    CollectBones();
    CollectParticleEmitters();
    CollectRibbonEmitters();
    CollectCollisionShapes();

    // Build initial material snapshots for change detection
    matSnapshots_.clear();
    auto snapMtl = [&](Mtl* mtl, int key) {
        MaterialSnapshot snap;
        int wc3fm = 1; PB2Int(mtl, L"filterMode", 0, wc3fm);
        snap.filterMode = MapMaterialFilterMode(wc3fm);
        BOOL flag = FALSE; int flags = 0;
        if (PB2Bool(mtl, L"twoSided", 0, flag) && flag)      flags |= 1;
        if (PB2Bool(mtl, L"unshaded", 0, flag) && flag)      flags |= 2;
        if (PB2Bool(mtl, L"unfogged", 0, flag) && flag)      flags |= 4;
        if (PB2Bool(mtl, L"noDepthTest", 0, flag) && flag)   flags |= 8;
        if (PB2Bool(mtl, L"noDepthSet", 0, flag) && flag)    flags |= 16;
        if (PB2Bool(mtl, L"constantColor", 0, flag) && flag) flags |= 32;
        snap.flags = flags;
        Texmap* texmap = nullptr;
        PB2Texmap(mtl, L"diffuseMap", texmap);
        int retex = 0;
        if (texmap && texmap->ClassID() == WC3_BITMAP_CLASS_ID) {
            int replId = 1; PB2Int(texmap, L"replaceableId", 0, replId);
            retex = std::max(0, replId - 1);
        }
        snap.replaceableTexture = retex;
        // Get texture path
        BitmapTex* bmt = nullptr;
        if (texmap) {
            if (texmap->ClassID() == WC3_BITMAP_CLASS_ID) {
                for (int r = 0; r < texmap->NumRefs(); r++) {
                    ReferenceTarget* ref = texmap->GetReference(r);
                    if (ref && ref->ClassID() == Class_ID(BMTEX_CLASS_ID, 0))
                        { bmt = static_cast<BitmapTex*>(ref); break; }
                }
            } else if (texmap->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                bmt = static_cast<BitmapTex*>(texmap);
            }
        }
        if (bmt) { const MCHAR* fn = bmt->GetMapName(); if (fn && fn[0]) snap.texturePath = fn; }
        int sortOrd = 1; PB2Int(mtl, L"sortOrder", 0, sortOrd);
        snap.sortOrder = std::max(0, sortOrd - 1);
        int priPlane = 0; PB2Int(mtl, L"priorityPlane", 0, priPlane);
        snap.priorityPlane = priPlane;
        matSnapshots_[key] = snap;
    };
    for (auto& mi : materials_) {
        if (!mi.mtl) continue;
        if (mi.mtl->ClassID() == WARCRAFT3_MAT_CLASS_ID) {
            snapMtl(mi.mtl, mi.materialId);
        } else if (mi.mtl->NumSubMtls() > 0) {
            for (int si = 0; si < mi.mtl->NumSubMtls(); si++) {
                Mtl* subMtl = mi.mtl->GetSubMtl(si);
                if (subMtl && subMtl->ClassID() == WARCRAFT3_MAT_CLASS_ID)
                    snapMtl(subMtl, mi.materialId * 1000 + si);
            }
        }
    }
}

// ============================================================================
// Collect geometry (identical logic to old extract.cpp)
// ============================================================================

void MaxSceneAdapter::CollectGeometry() {
    geosets_.clear();
    Interface* ip = GetCOREInterface();
    if (!ip) return;
    int geosetId = 0;

    std::function<void(INode*)> processNode = [&](INode* node) {
        if (!node || node->IsNodeHidden()) return;
        Object* baseObj = GetBaseObject(node);
        if (!baseObj) goto recurse;
        if (baseObj->SuperClassID() != GEOMOBJECT_CLASS_ID) goto recurse;
        if (baseObj->ClassID() == WC3PARTICLES2_CLASS_ID || baseObj->ClassID() == WC3RIBBON_CLASS_ID) goto recurse;
        {
            if (!baseObj->CanConvertToType(triObjectClassID)) goto recurse;
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
            const MCHAR* className = baseObj->GetObjectName(false);
#else
            MSTR classNameStr;
            baseObj->GetClassName(classNameStr);
            const MCHAR* className = classNameStr.data();
#endif
            if (_wcsicmp(className, L"Editable Mesh") != 0 &&
                _wcsicmp(className, L"Editable Poly") != 0)
                goto recurse;
            Mtl* mtl = node->GetMtl();
            if (mtl && mtl->ClassID() == WARCRAFT3_MAT_CLASS_ID) {
                Texmap* diffTex = nullptr; PB2Texmap(mtl, L"diffuseMap", diffTex);
                int retex = 0;
                if (diffTex && diffTex->ClassID() == WC3_BITMAP_CLASS_ID) {
                    int replId = 1; PB2Int(diffTex, L"replaceableId", 0, replId);
                    retex = std::max(0, replId - 1);
                }
                if (retex >= 4) {
                    if (!diffTex) { mprintf(_M("  [Skipped replaceable %d geoset '%s']\n"), retex, node->GetName()); goto recurse; }
                }
            }
            GeosetInfo gi; gi.geosetId = geosetId++; gi.node = node;
            geosets_.push_back(gi);
        }
    recurse:
        for (int c = 0; c < node->NumberOfChildren(); c++) processNode(node->GetChildNode(c));
    };

    INode* root = ip->GetRootNode();
    for (int i = 0; i < root->NumberOfChildren(); i++) processNode(root->GetChildNode(i));
}

// ============================================================================
// Extract a single Wc3Material into a MaterialLayerInfo
// ============================================================================

MaterialLayerInfo MaxSceneAdapter::ExtractWc3MaterialLayer(Mtl* mtl) {
    MaterialLayerInfo layer;

    int wc3fm = 1; PB2Int(mtl, L"filterMode", 0, wc3fm);
    layer.filterMode = MapMaterialFilterMode(wc3fm);
    float opacity = 100; PB2Float(mtl, L"opacity", 0, opacity);
    layer.alpha = std::min(opacity / 100.0f, 1.0f);

    Texmap* texmap = nullptr;
    PB2Texmap(mtl, L"diffuseMap", texmap);
    int retex = 0;
    if (texmap && texmap->ClassID() == WC3_BITMAP_CLASS_ID) {
        int replId = 1; PB2Int(texmap, L"replaceableId", 0, replId);
        retex = std::max(0, replId - 1);
    }
    layer.replaceableTexture = retex;

    BOOL flag = FALSE; int flags = 0;
    if (PB2Bool(mtl,L"twoSided",0,flag) && flag)    flags|=1;
    if (PB2Bool(mtl,L"unshaded",0,flag) && flag)    flags|=2;
    if (PB2Bool(mtl,L"unfogged",0,flag) && flag)    flags|=4;
    if (PB2Bool(mtl,L"noDepthTest",0,flag) && flag)  flags|=8;
    if (PB2Bool(mtl,L"noDepthSet",0,flag) && flag)   flags|=16;
    if (PB2Bool(mtl,L"constantColor",0,flag) && flag) flags|=32;
    layer.flags = flags;

    int baseTexId = -1;
    std::wstring baseTexPath;
    if (texmap) {
        mprintf(_M("    \x2192 texmap found, ClassID=0x%x,0x%x\n"),
                texmap->ClassID().PartA(), texmap->ClassID().PartB());
        BitmapTex* bmtex = nullptr;
        if (texmap->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
            bmtex = static_cast<BitmapTex*>(texmap);
        } else if (texmap->ClassID() == WC3_BITMAP_CLASS_ID) {
            for (int ri = 0; ri < texmap->NumRefs(); ri++) {
                ReferenceTarget* ref = texmap->GetReference(ri);
                if (ref && ref->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                    bmtex = static_cast<BitmapTex*>(ref);
                    break;
                }
            }
        }
        if (bmtex) {
            const MCHAR* fname = bmtex->GetMapName();
            if (fname && fname[0]) {
                baseTexPath = std::wstring(fname);
                mprintf(_M("    \x2192 texture path: '%s'\n"), baseTexPath.c_str());
                baseTexId = LoadTexture(baseTexPath, 0);
            } else {
                mprintf(_M("    \x2192 texture has no filename!\n"));
            }
        } else {
            mprintf(_M("    \x2192 texmap is NOT a BitmapTexture (e.g. Mix/Composite)\n"));
        }
    } else {
        mprintf(_M("    \x2192 NO texmap found for 'diffuseMap' property\n"));
    }

    if (layer.replaceableTexture == 1 && !baseTexPath.empty()) {
        mprintf(_M("    \x2192 TEAMCOLOR branch: compositing '%s'\n"), baseTexPath.c_str());
        layer.textureId = LoadTextureWithTeamColor(baseTexPath, 255, 0, 0);
        layer.filterMode = 0; layer.alpha = 1.0f;
    } else if (layer.replaceableTexture == 2) {
        mprintf(_M("    \x2192 TEAMGLOW branch: generating glow texture\n"));
        layer.textureId = GenerateTeamGlowTexture(255, 0, 0);
    } else if (layer.replaceableTexture >= 1 && baseTexId >= 0) {
        mprintf(_M("    \x2192 REPLACEABLE branch: replTex=%d, baseTexId=%d\n"), layer.replaceableTexture, baseTexId);
        layer.textureId = baseTexId; layer.filterMode = 0; layer.alpha = 1.0f;
    } else if (layer.replaceableTexture >= 1 && baseTexId < 0) {
        mprintf(_M("    \x2192 SOLID REPLACEABLE branch: replTex=%d\n"), layer.replaceableTexture);
        layer.textureId = LoadTexture(L"", layer.replaceableTexture);
    } else {
        layer.textureId = baseTexId;
    }

    return layer;
}

// ============================================================================
// Collect materials — supports single Wc3Material and Composite materials
// ============================================================================

void MaxSceneAdapter::CollectMaterials() {
    materials_.clear(); mtlToId_.clear(); nextMatId_ = 0;
    for (auto& gs : geosets_) {
        Mtl* mtl = gs.node->GetMtl();
        if (!mtl) continue;
        auto it = mtlToId_.find(mtl);
        if (it != mtlToId_.end()) { gs.materialId = it->second; continue; }

        MaterialInfo mi; mi.materialId = nextMatId_++; mi.mtl = mtl;
        gs.materialId = mi.materialId; mtlToId_[mtl] = mi.materialId;

        if (mtl->ClassID() == WARCRAFT3_MAT_CLASS_ID) {
            // Single Wc3Material — extract as one layer
            mprintf(_M("  Mat %d '%s': Wc3Material\n"), mi.materialId, gs.node->GetName());
            MaterialLayerInfo layer = ExtractWc3MaterialLayer(mtl);

            int sortOrd = 1; PB2Int(mtl, L"sortOrder", 0, sortOrd);
            mi.sortOrder = std::max(0, sortOrd - 1);
            int priPlane = 0; PB2Int(mtl, L"priorityPlane", 0, priPlane);
            mi.priorityPlane = priPlane;

            mi.layers.push_back(layer);
        } else if (mtl->NumSubMtls() > 0) {
            // Composite / multi-material — check if sub-materials are Wc3Materials.
            // First sub-material sets the scene blend; subsequent layers blend
            // to the previous layer using their own filterMode.
            int numSubs = mtl->NumSubMtls();
            mprintf(_M("  Mat %d '%s': Composite (%d sub-materials)\n"),
                    mi.materialId, gs.node->GetName(), numSubs);

            for (int si = 0; si < numSubs; si++) {
                Mtl* subMtl = mtl->GetSubMtl(si);
                if (!subMtl) continue;

                if (subMtl->ClassID() == WARCRAFT3_MAT_CLASS_ID) {
                    mprintf(_M("    Layer %d: Wc3Material '%s'\n"), si, subMtl->GetName().data());
                    MaterialLayerInfo layer = ExtractWc3MaterialLayer(subMtl);

                    // Take sortOrder/priorityPlane from the first layer
                    if (mi.layers.empty()) {
                        int sortOrd = 1; PB2Int(subMtl, L"sortOrder", 0, sortOrd);
                        mi.sortOrder = std::max(0, sortOrd - 1);
                        int priPlane = 0; PB2Int(subMtl, L"priorityPlane", 0, priPlane);
                        mi.priorityPlane = priPlane;
                    }

                    mi.layers.push_back(layer);
                } else {
                    mprintf(_M("    Layer %d: non-Wc3 material '%s' (skipped)\n"),
                            si, subMtl->GetName().data());
                }
            }

            // Fallback: if no Wc3Material sub-materials found, treat as generic
            if (mi.layers.empty()) {
                MaterialLayerInfo layer;
                layer.flags = 1;
                if (mtl->NumSubTexmaps() > 0) {
                    Texmap* diffuse = mtl->GetSubTexmap(0);
                    if (diffuse && diffuse->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                        const MCHAR* fname = static_cast<BitmapTex*>(diffuse)->GetMapName();
                        if (fname && fname[0]) layer.textureId = LoadTexture(std::wstring(fname), 0);
                    }
                }
                mi.layers.push_back(layer);
            }
        } else {
            // Generic non-Wc3 material — single layer fallback
            MaterialLayerInfo layer;
            layer.flags = 1;
            if (mtl->NumSubTexmaps() > 0) {
                Texmap* diffuse = mtl->GetSubTexmap(0);
                if (diffuse && diffuse->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                    const MCHAR* fname = static_cast<BitmapTex*>(diffuse)->GetMapName();
                    if (fname && fname[0]) layer.textureId = LoadTexture(std::wstring(fname), 0);
                }
            }
            mi.layers.push_back(layer);
        }
        materials_.push_back(mi);
    }
}

// ============================================================================
// Collect bones
// ============================================================================

void MaxSceneAdapter::CollectBones() {
    bones_.clear(); boneNameToIdx_.clear();
    Interface* ip = GetCOREInterface();
    if (!ip) return;
    std::vector<INode*> allBones;
    std::unordered_map<INode*, bool> boneSet;

    std::function<void(INode*)> scan = [&](INode* node) {
        if (!node) return;
        Modifier* skinMod = FindSkinModifier(node);
        if (skinMod) {
            ISkin* skin = (ISkin*)skinMod->GetInterface(I_SKIN);
            if (skin) for (int b = 0; b < skin->GetNumBones(); b++) {
                INode* bn = skin->GetBone(b);
                if (bn && !boneSet[bn]) { boneSet[bn]=true; allBones.push_back(bn); }
            }
        }
        for (int c = 0; c < node->NumberOfChildren(); c++) scan(node->GetChildNode(c));
    };
    INode* root = ip->GetRootNode();
    for (int i = 0; i < root->NumberOfChildren(); i++) scan(root->GetChildNode(i));

    for (int i = 0; i < (int)allBones.size(); i++) {
        BoneInfo bi; bi.node = allBones[i]; bi.index = i;
        bones_.push_back(bi);
        boneNameToIdx_[std::wstring(allBones[i]->GetName())] = i;
    }
}

// ============================================================================
// Collect particle emitters
// ============================================================================

void MaxSceneAdapter::CollectParticleEmitters() {
    particles_.clear();
    Interface* ip = GetCOREInterface(); if (!ip) return;
    int emitterId = 0;
    std::wstring basePath = GetMaxFilePath();

    std::function<void(INode*)> scan = [&](INode* node) {
        if (!node || node->IsNodeHidden()) return;
        Object* baseObj = GetBaseObject(node);
        if (baseObj && baseObj->ClassID() == WC3PARTICLES2_CLASS_ID) {
            ParticleEmitterInfo pi; pi.emitterId = emitterId++; pi.node = node;

            // Read texture path/prefix via GetInterface (cross-DLL)
            std::wstring texPath, texFile;
            auto* pathPtr  = static_cast<const MSTR*>(baseObj->GetInterface(WC3P2_TEXTURE_PATH_IID));
            auto* prefPtr  = static_cast<const MSTR*>(baseObj->GetInterface(WC3P2_TEXTURE_PREFIX_IID));
            if (pathPtr  && pathPtr->Length()  > 0) texFile = pathPtr->data();
            if (prefPtr  && prefPtr->Length()  > 0) texPath = prefPtr->data();

            if (!texFile.empty()) {
                std::wstring fp = basePath + texPath + texFile;
                mprintf(_M("  [Particle '%s'] prefix='%s' file='%s'\n"), node->GetName(), texPath.c_str(), texFile.c_str());
                mprintf(_M("    Try1: '%s' %s\n"), fp.c_str(),
                        GetFileAttributesW(fp.c_str()) != INVALID_FILE_ATTRIBUTES ? _M("FOUND") : _M("not found"));
                if (GetFileAttributesW(fp.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    fp = basePath + L"Textures\\" + texFile;
                    mprintf(_M("    Try2: '%s' %s\n"), fp.c_str(),
                            GetFileAttributesW(fp.c_str()) != INVALID_FILE_ATTRIBUTES ? _M("FOUND") : _M("not found"));
                }
                if (GetFileAttributesW(fp.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    fp = texFile;
                    mprintf(_M("    Try3 (filename only): '%s'\n"), fp.c_str());
                }
                int replId = 0; PB2Int(baseObj, L"ReplaceableId", 0, replId);
                pi.textureId = LoadTexture(fp, replId);
                mprintf(_M("    \x2192 texId=%d\n"), pi.textureId);
            } else {
                // Check for replaceable texture (TeamColor/TeamGlow)
                int replId = 0; PB2Int(baseObj, L"ReplaceableId", 0, replId);
                if (replId > 0) {
                    pi.textureId = LoadTexture(L"", replId);
                } else {
                    mprintf(_M("  [Particle '%s'] NO texture file set!\n"), node->GetName());
                }
            }
            particles_.push_back(pi);
        }
        for (int c = 0; c < node->NumberOfChildren(); c++) scan(node->GetChildNode(c));
    };
    INode* root = ip->GetRootNode();
    for (int i = 0; i < root->NumberOfChildren(); i++) scan(root->GetChildNode(i));
}

// ============================================================================
// Collect ribbon emitters
// ============================================================================

void MaxSceneAdapter::CollectRibbonEmitters() {
    ribbons_.clear(); Interface* ip = GetCOREInterface(); if(!ip) return;
    int emitterId = 0;
    std::function<void(INode*)> scan = [&](INode* node) {
        if (!node || node->IsNodeHidden()) return;
        Object* baseObj = GetBaseObject(node);
        if (baseObj && baseObj->ClassID() == WC3RIBBON_CLASS_ID) {
            RibbonEmitterInfo ri; ri.emitterId = emitterId++; ri.node = node;

            // Wc3Ribbon stores its material in PB2 param "Material" (pb_material=7, TYPE_MTL)
            // Resolve texture from the assigned material
            Mtl* mtl = nullptr;
            for (int pb = 0; pb < baseObj->NumParamBlocks(); pb++) {
                IParamBlock2* pblock = static_cast<Animatable*>(baseObj)->GetParamBlock(pb);
                if (!pblock) continue;
                // pb_material = ParamID 7
                mtl = pblock->GetMtl(7, 0);
                if (mtl) break;
            }
            // Fallback: check node material
            if (!mtl) mtl = node->GetMtl();
            if (mtl) {
                // Extract diffuse texture from the Wc3Material or standard material
                Texmap* diffTex = nullptr;
                if (mtl->ClassID() == WARCRAFT3_MAT_CLASS_ID) {
                    PB2Texmap(mtl, L"diffuseMap", diffTex);
                } else {
                    diffTex = mtl->GetSubTexmap(ID_DI);
                }
                if (diffTex) {
                    BitmapTex* bmtex = nullptr;
                    if (diffTex->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                        bmtex = static_cast<BitmapTex*>(diffTex);
                    } else if (diffTex->ClassID() == WC3_BITMAP_CLASS_ID) {
                        for (int r = 0; r < diffTex->NumRefs(); r++) {
                            ReferenceTarget* ref = diffTex->GetReference(r);
                            if (ref && ref->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                                bmtex = static_cast<BitmapTex*>(ref); break;
                            }
                        }
                    }
                    if (bmtex) {
                        const MCHAR* fname = bmtex->GetMapName();
                        if (fname && fname[0]) ri.textureId = LoadTexture(std::wstring(fname), 0);
                    }
                }
            }
            ribbons_.push_back(ri);
        }
        for (int c = 0; c < node->NumberOfChildren(); c++) scan(node->GetChildNode(c));
    };
    INode* root = ip->GetRootNode();
    for (int i = 0; i < root->NumberOfChildren(); i++) scan(root->GetChildNode(i));
}

// ============================================================================
// Collect collision shapes
// ============================================================================

void MaxSceneAdapter::CollectCollisionShapes() {
    collisions_.clear(); Interface* ip = GetCOREInterface(); if(!ip) return;
    std::function<void(INode*)> scan = [&](INode* node) {
        if (!node || node->IsNodeHidden()) return;
        Object* baseObj = GetBaseObject(node);
        if (baseObj && baseObj->SuperClassID() == HELPER_CLASS_ID) {
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
            const MCHAR* cn = baseObj->GetObjectName(false);
#else
            MSTR cnStr; baseObj->GetClassName(cnStr);
            const MCHAR* cn = cnStr.data();
#endif
            if (wcsstr(cn,L"CollisionSphere")||wcsstr(cn,L"Wc3CollisionSphere"))
                collisions_.push_back({1, node});
            else if (wcsstr(cn,L"CollisionBox")||wcsstr(cn,L"Wc3CollisionBox"))
                collisions_.push_back({0, node});
        }
        for (int c = 0; c < node->NumberOfChildren(); c++) scan(node->GetChildNode(c));
    };
    INode* root = ip->GetRootNode();
    for (int i = 0; i < root->NumberOfChildren(); i++) scan(root->GetChildNode(i));
}

// ============================================================================
// GetVNormal helper
// ============================================================================

static Point3 GetVNormal(Mesh& mesh, int faceIdx, int vertIdx) {
    DWORD smGroup = mesh.faces[faceIdx].getSmGroup();
    if (smGroup == 0) return mesh.getFaceNormal(faceIdx);
    Point3 n(0,0,0);
    for (int f = 0; f < mesh.getNumFaces(); f++) {
        if (mesh.faces[f].getSmGroup() & smGroup)
            for (int v = 0; v < 3; v++)
                if (mesh.faces[f].v[v] == (DWORD)vertIdx) { n += mesh.getFaceNormal(f); break; }
    }
    return Normalize(n);
}

// ============================================================================
// IModelSource::GetMeshes()
// ============================================================================

std::vector<MeshData> MaxSceneAdapter::GetMeshes() {
    std::vector<MeshData> result;
    for (auto& gs : geosets_) {
        mprintf(_M("    mesh[%d] '%s' ...\n"), gs.geosetId, gs.node->GetName());

        Modifier* skinMod = FindSkinModifier(gs.node);
        if (skinMod) skinMod->DisableMod();
        ObjectState os = gs.node->EvalWorldState(0);

        if (!os.obj || !os.obj->CanConvertToType(triObjectClassID)) {
            if (skinMod) skinMod->EnableMod();
            continue;
        }
        TriObject* triObj = static_cast<TriObject*>(os.obj->ConvertToType(0, triObjectClassID));
        if (!triObj) { if (skinMod) skinMod->EnableMod(); continue; }

        Mesh& mesh = triObj->GetMesh();
        mesh.buildNormals();
        int numFaces = mesh.getNumFaces();
        int numVerts = numFaces * 3;
        gs.expandedVertCount = numVerts;
        gs.faceVertMap.resize(numVerts);

        MeshData md;
        md.geosetId   = gs.geosetId;
        md.materialId = gs.materialId;
        md.positions.resize(numVerts);
        md.normals.resize(numVerts);
        md.uvs.resize(numVerts);
        md.indices.resize(numFaces * 3);

        bool hasUVs = mesh.getNumMapVerts(1) > 0;
        MeshNormalSpec* specN = mesh.GetSpecifiedNormals();
        bool hasSpecN = specN && specN->GetNumNormals() > 0;

        for (int f = 0; f < numFaces; f++) {
            Face& face = mesh.faces[f];
            for (int v = 0; v < 3; v++) {
                int outIdx = f*3+v;
                int origV = face.v[v];
                gs.faceVertMap[outIdx] = origV;

                Point3 pos = mesh.verts[origV];
                md.positions[outIdx] = {pos.x, pos.y, pos.z};

                Point3 n = hasSpecN ? specN->GetNormal(f,v) : GetVNormal(mesh,f,origV);
                n = Normalize(n);
                md.normals[outIdx] = {n.x, n.y, n.z};

                if (hasUVs) {
                    TVFace& tvf = mesh.mapFaces(1)[f];
                    UVVert uv = mesh.mapVerts(1)[tvf.t[v]];
                    md.uvs[outIdx] = {uv.x, 1.0f - uv.y};
                } else {
                    md.uvs[outIdx] = {0, 0};
                }
                md.indices[f*3+v] = (uint32_t)outIdx;
            }
        }

        if (triObj != os.obj) triObj->DeleteThis();
        if (skinMod) skinMod->EnableMod();

        // Validate skin vertex count
        if (skinMod) {
            ISkin* skin = (ISkin*)skinMod->GetInterface(I_SKIN);
            ISkinContextData* ctx = skin ? skin->GetContextInterface(gs.node) : nullptr;
            int skinVerts = ctx ? ctx->GetNumPoints() : -1;
            int meshOrigVerts = mesh.getNumVerts();
            if (skinVerts >= 0 && skinVerts != meshOrigVerts)
                mprintf(_M("  *** WARNING Geoset %d: mesh=%d, ISkin=%d MISMATCH!\n"),
                        gs.geosetId, meshOrigVerts, skinVerts);
        }

        mprintf(_M("  Geoset %d: %d expanded, %d faces '%s'%s\n"),
                gs.geosetId, numVerts, numFaces, gs.node->GetName(),
                skinMod ? _M(" [skinned]") : _M(""));

        result.push_back(std::move(md));
    }
    return result;
}

// ============================================================================
// IModelSource::GetTextures()
// ============================================================================

std::vector<TextureData> MaxSceneAdapter::GetTextures() {
    std::vector<TextureData> result;
    result.reserve(loadedTextures_.size());
    for (auto& lt : loadedTextures_) {
        TextureData td;
        td.textureId    = lt.textureId;
        td.replaceableId = lt.replaceableId;
        td.rgba          = std::move(lt.rgba);
        td.width         = lt.width;
        td.height        = lt.height;
        result.push_back(std::move(td));
    }
    loadedTextures_.clear();
    return result;
}

// ============================================================================
// IModelSource::GetMaterials()
// ============================================================================

std::vector<MaterialData> MaxSceneAdapter::GetMaterials() {
    std::vector<MaterialData> result;
    for (auto& mi : materials_) {
        MaterialData md;
        md.materialId    = mi.materialId;
        md.priorityPlane = mi.priorityPlane;
        md.sortOrder     = mi.sortOrder;
        for (auto& li : mi.layers) {
            MaterialLayerData ld;
            ld.filterMode = li.filterMode;
            ld.textureId  = li.textureId;
            ld.alpha      = li.alpha;
            ld.flags      = li.flags;
            md.layers.push_back(ld);
        }
        result.push_back(std::move(md));
    }
    return result;
}

// ============================================================================
// IModelSource::GetSkeleton()
// ============================================================================

SkeletonData MaxSceneAdapter::GetSkeleton() {
    SkeletonData sd;
    sd.boneCount = (int)bones_.size();
    sd.nodeCount = sd.boneCount;  // Max adapter only tracks skinning bones
    sd.inverseBindMatrices.resize(sd.boneCount);
    for (int i = 0; i < sd.boneCount; i++) {
        Matrix3 inv = Inverse(bones_[i].node->GetNodeTM(0));
        bones_[i].inverseBind = inv;
        float m16[16];
        PackMatrix(inv, m16);
        sd.inverseBindMatrices[i] = XMMATRIX(
            m16[0], m16[1], m16[2],  m16[3],
            m16[4], m16[5], m16[6],  m16[7],
            m16[8], m16[9], m16[10], m16[11],
            m16[12],m16[13],m16[14], m16[15]
        );
    }
    return sd;
}

// ============================================================================
// IModelSource::GetSkinWeights()
// ============================================================================

std::vector<SkinWeightData> MaxSceneAdapter::GetSkinWeights() {
    std::vector<SkinWeightData> result;

    for (auto& gs : geosets_) {
        if (gs.expandedVertCount == 0 || gs.faceVertMap.empty()) continue;
        Modifier* skinMod = FindSkinModifier(gs.node);
        if (!skinMod) { mprintf(_M("  Geoset %d: no Skin modifier\n"), gs.geosetId); continue; }
        ISkin* skin = (ISkin*)skinMod->GetInterface(I_SKIN);
        ISkinContextData* ctx = skin ? skin->GetContextInterface(gs.node) : nullptr;
        if (!ctx) { mprintf(_M("  Geoset %d: ISkin context failed\n"), gs.geosetId); continue; }

        int origVC = ctx->GetNumPoints();
        struct OW { int bi[4]={0,0,0,0}; float wt[4]={0,0,0,0}; };
        std::vector<OW> ow(origVC);
        int zeroWeightVerts = 0;
        for (int v = 0; v < origVC; v++) {
            int nw = ctx->GetNumAssignedBones(v);
            float totalW = 0;
            for (int b = 0; b < std::min(nw, 4); b++) {
                INode* bn = skin->GetBone(ctx->GetAssignedBone(v,b));
                int idx = 0;
                if (bn) { auto it = boneNameToIdx_.find(std::wstring(bn->GetName())); if (it != boneNameToIdx_.end()) idx = it->second; }
                ow[v].bi[b] = idx;
                ow[v].wt[b] = ctx->GetBoneWeight(v, b);
                totalW += ow[v].wt[b];
            }
            if (totalW < 0.001f) zeroWeightVerts++;
        }

        int ec = gs.expandedVertCount;
        SkinWeightData sw;
        sw.geosetId = gs.geosetId;
        sw.influences.resize(ec);
        int outOfRange = 0, mapped = 0, maxFVM = 0;
        for (int vi = 0; vi < ec; vi++) {
            int origV = gs.faceVertMap[vi];
            if (origV > maxFVM) maxFVM = origV;
            if (origV >= 0 && origV < origVC) {
                for (int j = 0; j < 4; j++) {
                    sw.influences[vi].boneIdx[j] = ow[origV].bi[j];
                    sw.influences[vi].weight[j]  = ow[origV].wt[j];
                }
                mapped++;
            } else {
                outOfRange++;
            }
        }
        mprintf(_M("  Geoset %d: skin %d expanded, %d ISkin, maxIdx=%d, mapped=%d, OOB=%d, zeroW=%d\n"),
                gs.geosetId, ec, origVC, maxFVM, mapped, outOfRange, zeroWeightVerts);
        result.push_back(std::move(sw));
    }
    return result;
}

// ============================================================================
// IModelSource::GetParticleConfigs()
// ============================================================================

std::vector<ParticleEmitterConfig> MaxSceneAdapter::GetParticleConfigs() {
    std::vector<ParticleEmitterConfig> result;
    for (auto& pi : particles_) {
        Object* obj = GetBaseObject(pi.node); if (!obj) continue;
        ParticleEmitterConfig cfg;
        int iv = 0; float fv = 0; BOOL bv = FALSE;

        cfg.textureId = pi.textureId >= 0 ? pi.textureId : 0;

        // Wc3Particles2 PB2 param names (from Particles.h enum)
        int blendMode = 0; PB2Int(obj, L"BlendMode", 0, blendMode);
        cfg.filterMode = MapParticleFilterMode(blendMode);

        if(PB2Int(obj, L"TextureRows", 0, iv)) cfg.rows = iv;
        if(PB2Int(obj, L"TextureCols", 0, iv)) cfg.cols = iv;
        if(PB2Bool(obj, L"Unshaded", 0, bv)) cfg.unshaded = bv != 0;
        if(PB2Float(obj, L"Life", 0, fv)) cfg.lifeSpan = fv;
        if(PB2Bool(obj, L"Squirt", 0, bv)) cfg.squirt = bv != 0;

        // Segment colors (stored as Point3 in Wc3Particles2)
        Color cv;
        if(PB2Color(obj, L"ColorStart", 0, cv)) { cfg.startColor = {cv.r, cv.g, cv.b}; }
        if(PB2Color(obj, L"ColorMid", 0, cv))   { cfg.midColor   = {cv.r, cv.g, cv.b}; }
        if(PB2Color(obj, L"ColorEnd", 0, cv))    { cfg.endColor   = {cv.r, cv.g, cv.b}; }

        // Segment alpha (0-255 int)
        if(PB2Int(obj, L"AlphaStart", 0, iv)) cfg.startAlpha = static_cast<float>(iv);
        if(PB2Int(obj, L"AlphaMid", 0, iv))   cfg.midAlpha   = static_cast<float>(iv);
        if(PB2Int(obj, L"AlphaEnd", 0, iv))   cfg.endAlpha   = static_cast<float>(iv);

        // Segment scale
        if(PB2Float(obj, L"ScaleStart", 0, fv)) cfg.startScale = fv;
        if(PB2Float(obj, L"ScaleMid", 0, fv))   cfg.midScale   = fv;
        if(PB2Float(obj, L"ScaleEnd", 0, fv))   cfg.endScale   = fv;

        if(PB2Float(obj, L"MidTime", 0, fv)) cfg.midTime = fv;

        // ParticleType: Wc3Particles2 stores 0=Head,1=Tail,2=Both; renderer uses 1=Head,2=Tail,3=Both
        if(PB2Int(obj, L"ParticleType", 0, iv)) cfg.particleType = iv + 1;
        if(PB2Float(obj, L"TailLength", 0, fv)) cfg.tailLength = fv;

        if(PB2Bool(obj, L"ModelSpace", 0, bv)) cfg.modelSpace = bv != 0;
        if(PB2Bool(obj, L"XYQuad", 0, bv))     cfg.xyQuad     = bv != 0;

        // Head/tail UV animation frames
        if(PB2Int(obj, L"HeadLifeStart", 0, iv))  cfg.headLifeStart  = iv;
        if(PB2Int(obj, L"HeadLifeEnd", 0, iv))    cfg.headLifeEnd    = iv;
        if(PB2Int(obj, L"HeadLifeRepeat", 0, iv)) cfg.headLifeRepeat = iv;
        if(PB2Int(obj, L"HeadDecayStart", 0, iv))  cfg.headDecayStart  = iv;
        if(PB2Int(obj, L"HeadDecayEnd", 0, iv))    cfg.headDecayEnd    = iv;
        if(PB2Int(obj, L"HeadDecayRepeat", 0, iv)) cfg.headDecayRepeat = iv;
        if(PB2Int(obj, L"TailLifeStart", 0, iv))  cfg.tailLifeStart  = iv;
        if(PB2Int(obj, L"TailLifeEnd", 0, iv))    cfg.tailLifeEnd    = iv;
        if(PB2Int(obj, L"TailLifeRepeat", 0, iv)) cfg.tailLifeRepeat = iv;
        if(PB2Int(obj, L"TailDecayStart", 0, iv))  cfg.tailDecayStart  = iv;
        if(PB2Int(obj, L"TailDecayEnd", 0, iv))    cfg.tailDecayEnd    = iv;
        if(PB2Int(obj, L"TailDecayRepeat", 0, iv)) cfg.tailDecayRepeat = iv;

        if(PB2Bool(obj, L"SortPrimitives", 0, bv)) cfg.sortZ = bv != 0;

        mprintf(_M("  Particle %d: '%s' tex=%d fm=%d unshaded=%d\n"),
                pi.emitterId, pi.node->GetName(), pi.textureId, cfg.filterMode, (int)cfg.unshaded);
        result.push_back(cfg);
    }
    return result;
}

// ============================================================================
// IModelSource::GetRibbonConfigs()
// ============================================================================

std::vector<RibbonEmitterConfig> MaxSceneAdapter::GetRibbonConfigs() {
    std::vector<RibbonEmitterConfig> result;
    for (auto& ri : ribbons_) {
        Object* obj = GetBaseObject(ri.node); if(!obj) continue;
        RibbonEmitterConfig cfg;
        int iv=0; float fv=0;

        cfg.textureId = ri.textureId >= 0 ? ri.textureId : 0;

        // Wc3Ribbon has no filtermode param — derive from material on the node
        // Default to Blend (2) which is most common for ribbons
        Mtl* mtl = ri.node->GetMtl();
        if (mtl && mtl->ClassID() == WARCRAFT3_MAT_CLASS_ID) {
            int wc3fm = 0; PB2Int(mtl, L"filterMode", 0, wc3fm);
            cfg.filterMode = MapMaterialFilterMode(wc3fm);
        } else {
            cfg.filterMode = MapRibbonFilterMode(4); // fallback: old default
        }

        // Wc3Ribbon PB2 param names
        if(PB2Int(obj, L"Texture Rows", 0, iv))    cfg.rows = iv;
        if(PB2Int(obj, L"Texture Columns", 0, iv)) cfg.cols = iv;

        // Wc3Ribbon has no unshaded/twosided params — use sensible defaults
        cfg.unshaded = true;
        cfg.twoSided = true;

        if(PB2Int(obj, L"Edges Per Second", 0, iv)) cfg.emission = static_cast<float>(iv);
        if(PB2Float(obj, L"Edge Lifetime", 0, fv))  cfg.life = fv;
        if(PB2Float(obj, L"Gravity", 0, fv))        cfg.gravity = fv;

        mprintf(_M("  Ribbon %d: '%s' tex=%d fm=%d\n"), ri.emitterId, ri.node->GetName(), ri.textureId, cfg.filterMode);
        result.push_back(cfg);
    }
    return result;
}

// ============================================================================
// IModelSource::GetCollisionShapes()
// ============================================================================

std::vector<CollisionShapeData> MaxSceneAdapter::GetCollisionShapes() {
    std::vector<CollisionShapeData> result;
    for (auto& ci : collisions_) {
        Object* obj = GetBaseObject(ci.node); if(!obj) continue;
        CollisionShapeData cs;
        cs.type = ci.type;
        cs.radius = 0;
        cs.vertices[0] = {0,0,0};
        cs.vertices[1] = {0,0,0};
        if (ci.type == 1) {
            float r=10; PB2Float(obj,L"radius",0,r);
            cs.radius = r;
        } else {
            float w=5,l=5,h=5;
            PB2Float(obj,L"width",0,w); w*=.5f;
            PB2Float(obj,L"length",0,l); l*=.5f;
            PB2Float(obj,L"height",0,h); h*=.5f;
            cs.vertices[0] = {-w,-l,0};
            cs.vertices[1] = {w,l,h*2};
        }
        result.push_back(cs);
    }
    return result;
}

// ============================================================================
// IModelSource::SetActiveSequence() — no-op for Max (Max controls the timeline)
// ============================================================================

void MaxSceneAdapter::SetActiveSequence(int) {}

// ============================================================================
// IModelSource::Evaluate() — compute per-frame state from Max scene
// ============================================================================

FrameState MaxSceneAdapter::Evaluate(int timeMs) {
    // Convert ms to Max ticks
    int tpf = GetTicksPerFrame(), fps = GetFrameRate();
    TimeValue t = (tpf > 0 && fps > 0)
        ? (TimeValue)((float)timeMs * (float)fps / 1000.0f * (float)tpf)
        : 0;

    FrameState state;

    // Bone world matrices
    if (!bones_.empty()) {
        int bc = (int)bones_.size();
        state.boneWorldMatrices.resize(bc);
        for (int i = 0; i < bc; i++) {
            float m16[16];
            PackMatrix(bones_[i].node->GetNodeTM(t), m16);
            state.boneWorldMatrices[i] = XMMATRIX(
                m16[0], m16[1], m16[2],  m16[3],
                m16[4], m16[5], m16[6],  m16[7],
                m16[8], m16[9], m16[10], m16[11],
                m16[12],m16[13],m16[14], m16[15]
            );
        }
    }

    // Geoset transforms + visibility
    if (!geosets_.empty()) {
        int c = (int)geosets_.size();
        state.geosetTransforms.resize(c);
        for (int i = 0; i < c; i++) {
            INode* node = geosets_[i].node;
            if (!node) { state.geosetTransforms[i] = XMMatrixIdentity(); continue; }
            float m16[16];
            PackMatrix(node->GetNodeTM(t), m16);
            state.geosetTransforms[i] = XMMATRIX(
                m16[0], m16[1], m16[2],  m16[3],
                m16[4], m16[5], m16[6],  m16[7],
                m16[8], m16[9], m16[10], m16[11],
                m16[12],m16[13],m16[14], m16[15]
            );
        }

        state.geosetAlphas.resize(c, 1.0f);
        state.geosetColors.resize(c, {1,1,1});
        for (int i = 0; i < c; i++) {
            INode* node = geosets_[i].node; if(!node) continue;
            float vis = node->GetVisibility(t);
            // Geoset alpha is visibility only — per-layer opacity is sent
            // separately via layerAlphas so each layer can fade independently
            state.geosetAlphas[i] = std::max(0.f, std::min(1.f, vis));

            // Geoset colors
            Modifier* mod = FindModifierByClassID(geosets_[i].node, WC3VERTEXMOD_CLASS_ID);
            if (mod) {
                BOOL uc=FALSE; PB2Bool(mod,L"UsesColor",t,uc);
                if (uc) {
                    Color col(1,1,1); PB2Color(mod,L"VertexColor",t,col);
                    state.geosetColors[i] = {col.r, col.g, col.b};
                }
            }
        }
    }

    // Per-layer alpha (opacity from each Wc3Material sub-material at current time)
    {
        std::vector<int> sentMats;
        for (auto& gs : geosets_) {
            Mtl* mtl = gs.node ? gs.node->GetMtl() : nullptr;
            if (!mtl) continue;
            auto it = mtlToId_.find(mtl);
            if (it == mtlToId_.end()) continue;
            int matId = it->second;
            if (std::find(sentMats.begin(), sentMats.end(), matId) != sentMats.end()) continue;
            sentMats.push_back(matId);

            if (mtl->ClassID() == WARCRAFT3_MAT_CLASS_ID) {
                float a = 100; PB2Float(mtl, L"opacity", t, a);
                state.layerAlphas.push_back({matId, 0, std::min(a / 100.0f, 1.0f)});
            } else if (mtl->NumSubMtls() > 0) {
                int layerIdx = 0;
                for (int si = 0; si < mtl->NumSubMtls(); si++) {
                    Mtl* subMtl = mtl->GetSubMtl(si);
                    if (!subMtl || subMtl->ClassID() != WARCRAFT3_MAT_CLASS_ID) continue;
                    float a = 100; PB2Float(subMtl, L"opacity", t, a);
                    state.layerAlphas.push_back({matId, layerIdx, std::min(a / 100.0f, 1.0f)});
                    layerIdx++;
                }
            }
        }
    }

    // Particle emitter states
    for (auto& pi : particles_) {
        Object* obj = GetBaseObject(pi.node); if(!obj) continue;
        FrameState::ParticleFrameState ps;
        ps.emitterId = pi.emitterId;

        float m16[16];
        PackMatrix(pi.node->GetNodeTM(t), m16);
        ps.transform = XMMATRIX(
            m16[0],m16[1],m16[2],m16[3], m16[4],m16[5],m16[6],m16[7],
            m16[8],m16[9],m16[10],m16[11], m16[12],m16[13],m16[14],m16[15]
        );
        float fv=0;
        PB2Float(obj,L"EmissionRate",t,fv); ps.emissionRate=fv;
        PB2Float(obj,L"Speed",t,fv);        ps.speed=fv;
        PB2Float(obj,L"Variation",t,fv);    ps.variation=fv;
        PB2Float(obj,L"ConeAngle",t,fv);    ps.coneAngle=fv;
        PB2Float(obj,L"Gravity",t,fv);      ps.gravity=fv;
        PB2Float(obj,L"Width",t,fv);        ps.width=fv;
        PB2Float(obj,L"Height",t,fv);       ps.length=fv;
        ps.visibility = pi.node->GetVisibility(t);
        state.particleStates.push_back(ps);
    }

    // Ribbon emitter states
    for (auto& ri : ribbons_) {
        Object* obj = GetBaseObject(ri.node); if(!obj) continue;
        FrameState::RibbonFrameState rs;
        rs.emitterId = ri.emitterId;

        float m16[16];
        PackMatrix(ri.node->GetNodeTM(t), m16);
        rs.transform = XMMATRIX(
            m16[0],m16[1],m16[2],m16[3], m16[4],m16[5],m16[6],m16[7],
            m16[8],m16[9],m16[10],m16[11], m16[12],m16[13],m16[14],m16[15]
        );
        float fv=0; Color cv;
        if(PB2Float(obj,L"Height Above",t,fv)) rs.above=fv; else rs.above=20;
        if(PB2Float(obj,L"Height Below",t,fv)) rs.below=fv; else rs.below=20;
        if(PB2Float(obj,L"Alpha",t,fv)) rs.alpha=fv; else rs.alpha=1;
        if(PB2Color(obj,L"Color",t,cv)){rs.color={cv.r,cv.g,cv.b};} else{rs.color={1,1,1};}
        rs.visibility = ri.node->GetVisibility(t);
        int iv=0;
        if(PB2Int(obj,L"Texture Slot",t,iv)) rs.slot=iv; else rs.slot=0;
        state.ribbonStates.push_back(rs);
    }

    // Collision transforms
    for (auto& ci : collisions_) {
        float m16[16];
        PackMatrix(ci.node->GetNodeTM(t), m16);
        state.collisionTransforms.push_back(XMMATRIX(
            m16[0],m16[1],m16[2],m16[3], m16[4],m16[5],m16[6],m16[7],
            m16[8],m16[9],m16[10],m16[11], m16[12],m16[13],m16[14],m16[15]
        ));
    }

    // Texture animations (per-layer, supports composite materials)
    {
        auto readUVAnim = [&](Mtl* mtl, int matId, int layerIdx) {
            float uOff=0, vOff=0, uTile=1, vTile=1, wAng=0;

            // Read directly from Wc3Material PB2 (most reliable — doesn't
            // depend on controller sharing between material and BitmapTex)
            bool hasPB = PB2Float(mtl, L"anim_UOffset", t, uOff);
            if (hasPB) {
                PB2Float(mtl, L"anim_VOffset", t, vOff);
                PB2Float(mtl, L"anim_UTiling", t, uTile);
                PB2Float(mtl, L"anim_VTiling", t, vTile);
                PB2Float(mtl, L"anim_WAngle",  t, wAng);
            } else {
                // Fallback: read from BitmapTex UVGen (non-Wc3 materials)
                Texmap* texmap = nullptr;
                if (PB2Texmap(mtl, L"diffuseMap", texmap) && texmap) {
                    BitmapTex* bmt = nullptr;
                    if (texmap->ClassID() == WC3_BITMAP_CLASS_ID) {
                        for (int r = 0; r < texmap->NumRefs(); r++) {
                            ReferenceTarget* ref = texmap->GetReference(r);
                            if (ref && ref->ClassID() == Class_ID(BMTEX_CLASS_ID,0))
                                { bmt = static_cast<BitmapTex*>(ref); break; }
                        }
                    } else if (texmap->ClassID() == Class_ID(BMTEX_CLASS_ID,0)) {
                        bmt = static_cast<BitmapTex*>(texmap);
                    }
                    if (bmt) {
                        StdUVGen* uvg = bmt->GetUVGen();
                        if (uvg) {
                            uOff = uvg->GetUOffs(t); vOff = uvg->GetVOffs(t);
                            uTile = uvg->GetUScl(t); vTile = uvg->GetVScl(t);
                            wAng = uvg->GetWAng(t);
                        }
                    }
                }
            }
            if (uOff!=0||vOff!=0||uTile!=1||vTile!=1||wAng!=0) {
                FrameState::TexAnimState tas;
                tas.materialId = matId;
                tas.layerIndex = layerIdx;
                tas.uOff = uOff; tas.vOff = vOff;
                tas.uTile = uTile; tas.vTile = vTile;
                tas.rotation = wAng;
                state.texAnims.push_back(tas);
            }
        };

        std::vector<int> sentTA;
        for (auto& gs : geosets_) {
            Mtl* mtl = gs.node ? gs.node->GetMtl() : nullptr;
            if (!mtl) continue;
            auto it = mtlToId_.find(mtl); if (it == mtlToId_.end()) continue;
            int matId = it->second;
            if (std::find(sentTA.begin(), sentTA.end(), matId) != sentTA.end()) continue;
            sentTA.push_back(matId);

            if (mtl->ClassID() == WARCRAFT3_MAT_CLASS_ID) {
                readUVAnim(mtl, matId, 0);
            } else if (mtl->NumSubMtls() > 0) {
                int layerIdx = 0;
                for (int si = 0; si < mtl->NumSubMtls(); si++) {
                    Mtl* subMtl = mtl->GetSubMtl(si);
                    if (!subMtl || subMtl->ClassID() != WARCRAFT3_MAT_CLASS_ID) continue;
                    readUVAnim(subMtl, matId, layerIdx);
                    layerIdx++;
                }
            }
        }
    }

    return state;
}

// ============================================================================
// RefreshMaterials — re-read material properties, detect changes
// ============================================================================

MaxSceneAdapter::MaterialRefreshResult MaxSceneAdapter::RefreshMaterials() {
    MaterialRefreshResult result;
    result.changed = false;

    // Helper: get current texture file path from a Wc3Material
    auto getTexturePath = [&](Mtl* mtl) -> std::wstring {
        Texmap* texmap = nullptr;
        if (!PB2Texmap(mtl, L"diffuseMap", texmap) || !texmap) return {};
        BitmapTex* bmt = nullptr;
        if (texmap->ClassID() == WC3_BITMAP_CLASS_ID) {
            for (int r = 0; r < texmap->NumRefs(); r++) {
                ReferenceTarget* ref = texmap->GetReference(r);
                if (ref && ref->ClassID() == Class_ID(BMTEX_CLASS_ID, 0))
                    { bmt = static_cast<BitmapTex*>(ref); break; }
            }
        } else if (texmap->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
            bmt = static_cast<BitmapTex*>(texmap);
        }
        if (!bmt) return {};
        const MCHAR* fname = bmt->GetMapName();
        return (fname && fname[0]) ? std::wstring(fname) : std::wstring{};
    };

    // Helper: snapshot current properties from a Wc3Material
    auto snapshotMtl = [&](Mtl* mtl) -> MaterialSnapshot {
        MaterialSnapshot snap;
        int wc3fm = 1; PB2Int(mtl, L"filterMode", 0, wc3fm);
        snap.filterMode = MapMaterialFilterMode(wc3fm);

        BOOL flag = FALSE; int flags = 0;
        if (PB2Bool(mtl, L"twoSided", 0, flag) && flag)      flags |= 1;
        if (PB2Bool(mtl, L"unshaded", 0, flag) && flag)      flags |= 2;
        if (PB2Bool(mtl, L"unfogged", 0, flag) && flag)      flags |= 4;
        if (PB2Bool(mtl, L"noDepthTest", 0, flag) && flag)   flags |= 8;
        if (PB2Bool(mtl, L"noDepthSet", 0, flag) && flag)    flags |= 16;
        if (PB2Bool(mtl, L"constantColor", 0, flag) && flag) flags |= 32;
        snap.flags = flags;

        Texmap* texmap = nullptr;
        PB2Texmap(mtl, L"diffuseMap", texmap);
        int retex = 0;
        if (texmap && texmap->ClassID() == WC3_BITMAP_CLASS_ID) {
            int replId = 1; PB2Int(texmap, L"replaceableId", 0, replId);
            retex = std::max(0, replId - 1);
        }
        snap.replaceableTexture = retex;
        snap.texturePath = getTexturePath(mtl);

        int sortOrd = 1; PB2Int(mtl, L"sortOrder", 0, sortOrd);
        snap.sortOrder = std::max(0, sortOrd - 1);
        int priPlane = 0; PB2Int(mtl, L"priorityPlane", 0, priPlane);
        snap.priorityPlane = priPlane;

        return snap;
    };

    // Compare current properties with cached snapshots
    bool anyChanged = false;
    for (auto& mi : materials_) {
        if (!mi.mtl) continue;

        if (mi.mtl->ClassID() == WARCRAFT3_MAT_CLASS_ID) {
            MaterialSnapshot cur = snapshotMtl(mi.mtl);
            auto it = matSnapshots_.find(mi.materialId);
            if (it == matSnapshots_.end() ||
                it->second.filterMode != cur.filterMode ||
                it->second.flags != cur.flags ||
                it->second.priorityPlane != cur.priorityPlane ||
                it->second.sortOrder != cur.sortOrder ||
                it->second.replaceableTexture != cur.replaceableTexture ||
                it->second.texturePath != cur.texturePath)
            {
                anyChanged = true;
                break;
            }
        } else if (mi.mtl->NumSubMtls() > 0) {
            for (int si = 0; si < mi.mtl->NumSubMtls(); si++) {
                Mtl* subMtl = mi.mtl->GetSubMtl(si);
                if (!subMtl || subMtl->ClassID() != WARCRAFT3_MAT_CLASS_ID) continue;
                // Use a combined key for sub-material snapshots
                int key = mi.materialId * 1000 + si;
                MaterialSnapshot cur = snapshotMtl(subMtl);
                auto it = matSnapshots_.find(key);
                if (it == matSnapshots_.end() ||
                    it->second.filterMode != cur.filterMode ||
                    it->second.flags != cur.flags ||
                    it->second.replaceableTexture != cur.replaceableTexture ||
                    it->second.texturePath != cur.texturePath)
                {
                    anyChanged = true;
                    break;
                }
            }
            if (anyChanged) break;
        }
    }

    if (!anyChanged) return result;

    // Something changed — re-collect materials and textures from scratch
    // Clear texture caches so they get reloaded
    loadedTextures_.clear();
    texPathToId_.clear();
    texEntries_.clear();
    nextTexId_ = 0;

    // Re-collect materials (uses existing geoset/node data)
    CollectMaterials();

    // Update snapshots
    matSnapshots_.clear();
    for (auto& mi : materials_) {
        if (!mi.mtl) continue;
        if (mi.mtl->ClassID() == WARCRAFT3_MAT_CLASS_ID) {
            matSnapshots_[mi.materialId] = snapshotMtl(mi.mtl);
        } else if (mi.mtl->NumSubMtls() > 0) {
            for (int si = 0; si < mi.mtl->NumSubMtls(); si++) {
                Mtl* subMtl = mi.mtl->GetSubMtl(si);
                if (subMtl && subMtl->ClassID() == WARCRAFT3_MAT_CLASS_ID)
                    matSnapshots_[mi.materialId * 1000 + si] = snapshotMtl(subMtl);
            }
        }
    }

    // Build results
    result.materials = GetMaterials();
    result.textures  = GetTextures();
    result.changed   = true;
    return result;
}

// ============================================================================
// IModelSource::GetSequences() — Max doesn't have MDX sequences
// ============================================================================

std::vector<IModelSource::SequenceInfo> MaxSceneAdapter::GetSequences() {
    return {};
}

// ============================================================================
// GetCameraPresets — collect camera objects from the Max scene
// ============================================================================

std::vector<CameraPreset> MaxSceneAdapter::GetCameraPresets() {
    std::vector<CameraPreset> presets;
    Interface* ip = GetCOREInterface();
    if (!ip) return presets;

    std::function<void(INode*)> findCameras = [&](INode* node) {
        if (!node) return;
        Object* obj = GetBaseObject(node);
        if (obj && obj->SuperClassID() == CAMERA_CLASS_ID) {
            // Get camera world transform
            Matrix3 tm = node->GetNodeTM(0);
            Point3 pos = tm.GetRow(3);
            // Get target: if it's a target camera, use the target node
            Point3 tgt = pos + tm.GetRow(2) * -100.0f; // default: look along -Z
            INode* targNode = node->GetTarget();
            if (targNode) tgt = targNode->GetNodeTM(0).GetRow(3);

            // Convert to orbital camera params (pitch/yaw/distance around target)
            Point3 dir = pos - tgt;
            float dist = Length(dir);
            if (dist < 0.01f) dist = 100.0f;
            dir = Normalize(dir);
            float pitch = asinf(std::clamp(dir.z, -1.0f, 1.0f));
            float yaw = atan2f(dir.y, dir.x);

            CameraPreset cp;
            cp.name = std::wstring(node->GetName());
            cp.pitch = pitch;
            cp.yaw = yaw;
            cp.distance = dist;
            cp.target = {tgt.x, tgt.y, tgt.z};
            cp.isLive = false;
            presets.push_back(cp);
        }
        for (int i = 0; i < node->NumberOfChildren(); i++)
            findCameras(node->GetChildNode(i));
    };
    findCameras(ip->GetRootNode());
    return presets;
}
