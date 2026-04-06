// MDLXImporter — Import options INI persistence
#include "mdlx_import_options.h"

#include <max.h>
#include <MaxDirectories.h>
#include <string>

namespace {

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

} // namespace

void loadImportOptionsFromINI(Interface* gi, MdlxImportOptions& opts) {
    MSTR dir = gi->GetDir(APP_PLUGCFG_DIR);
    std::wstring iniPath = std::wstring(dir.data()) + L"\\WhiteoutDexImporter.ini";

    if (GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;

    const wchar_t* sec = L"Settings";
    auto& c = opts.core;

    // ── Mode ──
    auto mode = iniGetString(iniPath.c_str(), sec, L"ImportMode");
    if (mode == L"Merge") c.mode = ir::CoreImportOptions::ImportMode::Merge;
    else                  c.mode = ir::CoreImportOptions::ImportMode::NewScene;

    // ── Geometry ──
    auto skin = iniGetString(iniPath.c_str(), sec, L"ImportSkinning");
    if (!skin.empty()) c.importSkinning = iniBool(skin);

    // ── Materials ──
    auto mat = iniGetString(iniPath.c_str(), sec, L"ImportMaterials");
    if (!mat.empty()) c.importMaterials = iniBool(mat);

    auto tex = iniGetString(iniPath.c_str(), sec, L"ImportTextures");
    if (!tex.empty()) c.importTextures = iniBool(tex);

    // ── Objects ──
    auto obj = iniGetString(iniPath.c_str(), sec, L"ImportObjects");
    if (!obj.empty()) c.importObjects = iniBool(obj);

    auto bones = iniGetString(iniPath.c_str(), sec, L"ImportBones");
    if (!bones.empty()) c.importBones = iniBool(bones);

    auto helpers = iniGetString(iniPath.c_str(), sec, L"ImportHelpers");
    if (!helpers.empty()) c.importHelpers = iniBool(helpers);

    auto lights = iniGetString(iniPath.c_str(), sec, L"ImportLights");
    if (!lights.empty()) c.importLights = iniBool(lights);

    auto att = iniGetString(iniPath.c_str(), sec, L"ImportAttachments");
    if (!att.empty()) c.importAttachments = iniBool(att);

    auto pe1 = iniGetString(iniPath.c_str(), sec, L"ImportParticleEmitters1");
    if (!pe1.empty()) c.importParticleEmitters1 = iniBool(pe1);

    auto pe2 = iniGetString(iniPath.c_str(), sec, L"ImportParticleEmitters2");
    if (!pe2.empty()) c.importParticleEmitters2 = iniBool(pe2);

    auto rib = iniGetString(iniPath.c_str(), sec, L"ImportRibbonEmitters");
    if (!rib.empty()) c.importRibbonEmitters = iniBool(rib);

    auto evt = iniGetString(iniPath.c_str(), sec, L"ImportEventObjects");
    if (!evt.empty()) c.importEventObjects = iniBool(evt);

    auto coll = iniGetString(iniPath.c_str(), sec, L"ImportCollisionShapes");
    if (!coll.empty()) c.importCollisionShapes = iniBool(coll);

    auto cam = iniGetString(iniPath.c_str(), sec, L"ImportCameras");
    if (!cam.empty()) c.importCameras = iniBool(cam);

    // ── Animations ──
    auto anim = iniGetString(iniPath.c_str(), sec, L"ImportAnimations");
    if (!anim.empty()) c.importAnimations = iniBool(anim);

    auto trans = iniGetString(iniPath.c_str(), sec, L"ImportTranslation");
    if (!trans.empty()) c.importTranslation = iniBool(trans);

    auto rot = iniGetString(iniPath.c_str(), sec, L"ImportRotation");
    if (!rot.empty()) c.importRotation = iniBool(rot);

    auto scl = iniGetString(iniPath.c_str(), sec, L"ImportScale");
    if (!scl.empty()) c.importScale = iniBool(scl);

    auto param = iniGetString(iniPath.c_str(), sec, L"ImportParameterAnimations");
    if (!param.empty()) c.importParameterAnimations = iniBool(param);

    auto uv = iniGetString(iniPath.c_str(), sec, L"ImportUVAnimations");
    if (!uv.empty()) c.importUVAnimations = iniBool(uv);

    auto texAnim = iniGetString(iniPath.c_str(), sec, L"ImportTextureAnimations");
    if (!texAnim.empty()) c.importTextureAnimations = iniBool(texAnim);

    auto vis = iniGetString(iniPath.c_str(), sec, L"ImportVisibility");
    if (!vis.empty()) c.importVisibility = iniBool(vis);

    auto col = iniGetString(iniPath.c_str(), sec, L"ImportColorAnimations");
    if (!col.empty()) c.importColorAnimations = iniBool(col);

    // ── Helper Options ──
    auto helpPt = iniGetString(iniPath.c_str(), sec, L"ImportHelpersAsPointHelpers");
    if (!helpPt.empty()) c.importHelpersAsPointHelpers = iniBool(helpPt);

    // ── Optimization ──
    auto optGeo = iniGetString(iniPath.c_str(), sec, L"OptimizeGeometry");
    if (!optGeo.empty()) c.optimizeGeometry = iniBool(optGeo);

    auto optBone = iniGetString(iniPath.c_str(), sec, L"OptimizeBonesAndHelpers");
    if (!optBone.empty()) c.optimizeBonesAndHelpers = iniBool(optBone);

    // ── MDX-specific ──
    auto mpq = iniGetString(iniPath.c_str(), sec, L"SearchMPQ");
    if (!mpq.empty()) opts.searchMPQ = iniBool(mpq);

    auto casc = iniGetString(iniPath.c_str(), sec, L"SearchCASC");
    if (!casc.empty()) opts.searchCASC = iniBool(casc);

    auto mpqDir = iniGetString(iniPath.c_str(), sec, L"MPQDirectory");
    if (!mpqDir.empty()) opts.mpqDirectory = mpqDir;

    auto cascDir = iniGetString(iniPath.c_str(), sec, L"CASCDirectory");
    if (!cascDir.empty()) opts.cascDirectory = cascDir;

    // ── Reforged v1200 ──
    auto corn = iniGetString(iniPath.c_str(), sec, L"ImportCornEmitters");
    if (!corn.empty()) opts.importCornEmitters = iniBool(corn);

    auto ffx = iniGetString(iniPath.c_str(), sec, L"ImportFaceFX");
    if (!ffx.empty()) opts.importFaceFX = iniBool(ffx);
}

void applyFastPreset(MdlxImportOptions& opts) {
    auto& c = opts.core;
    switch (c.preset) {
    case ir::CoreImportOptions::Preset::StaticNoMaterials:
        c.importSkinning = false;
        c.importMaterials = false;
        c.importTextures = false;
        c.importObjects = false;
        c.importAnimations = false;
        break;
    case ir::CoreImportOptions::Preset::StaticMaterials:
        c.importSkinning = false;
        c.importMaterials = true;
        c.importTextures = true;
        c.importObjects = false;
        c.importAnimations = false;
        break;
    case ir::CoreImportOptions::Preset::AnimatedNoSkinning:
        c.importSkinning = false;
        c.importMaterials = true;
        c.importTextures = true;
        c.importObjects = true;
        c.importBones = true;
        c.importHelpers = true;
        c.importLights = false;
        c.importAttachments = false;
        c.importParticleEmitters1 = false;
        c.importParticleEmitters2 = false;
        c.importRibbonEmitters = false;
        c.importEventObjects = false;
        c.importCollisionShapes = false;
        c.importCameras = false;
        c.importAnimations = true;
        break;
    case ir::CoreImportOptions::Preset::AnimatedNoObjects:
        c.importSkinning = true;
        c.importMaterials = true;
        c.importTextures = true;
        c.importObjects = true;
        c.importBones = true;
        c.importHelpers = true;
        c.importLights = false;
        c.importAttachments = false;
        c.importParticleEmitters1 = false;
        c.importParticleEmitters2 = false;
        c.importRibbonEmitters = false;
        c.importEventObjects = false;
        c.importCollisionShapes = false;
        c.importCameras = false;
        c.importAnimations = true;
        break;
    case ir::CoreImportOptions::Preset::All:
        c.importSkinning = true;
        c.importMaterials = true;
        c.importTextures = true;
        c.importObjects = true;
        c.importBones = true;
        c.importHelpers = true;
        c.importLights = true;
        c.importAttachments = true;
        c.importParticleEmitters1 = true;
        c.importParticleEmitters2 = true;
        c.importRibbonEmitters = true;
        c.importEventObjects = true;
        c.importCollisionShapes = true;
        c.importCameras = true;
        c.importAnimations = true;
        c.importTranslation = true;
        c.importRotation = true;
        c.importScale = true;
        c.importParameterAnimations = true;
        c.importUVAnimations = true;
        c.importTextureAnimations = true;
        c.importVisibility = true;
        c.importColorAnimations = true;
        break;
    case ir::CoreImportOptions::Preset::Custom:
    default:
        break;
    }
}
