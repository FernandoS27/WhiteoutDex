// MDLXImporter — Import options dialog implementation
#include "import_dialog.h"
#include "resource.h"

#include <max.h>
#include <MaxDirectories.h>
#include <commctrl.h>
#include <string>

// After max.h: that header has opinions about windows.h, which this one
// includes.
#include "wdx_localization.h" // wdx::l10n::LocalizeDialog
#include "wdx_window_icon.h"  // wdx::ApplyWindowIcon

// ============================================================================
// Dialog state passed via LPARAM → GWLP_USERDATA
// ============================================================================
struct DialogState {
    MdlxImportOptions* opts;
    bool isReforged;
    bool hasCornEmitters; // CORN chunks present in the source MDX
    bool confirmed;       // true if user clicked Import
    bool onControlSet;    // re-entrancy guard for cascading checkbox updates
};

// ============================================================================
// Helpers
// ============================================================================

namespace {

void setCheck(HWND hDlg, int id, bool val) {
    CheckDlgButton(hDlg, id, val ? BST_CHECKED : BST_UNCHECKED);
}

bool getCheck(HWND hDlg, int id) {
    return IsDlgButtonChecked(hDlg, id) == BST_CHECKED;
}

void enableCtrl(HWND hDlg, int id, bool on) {
    EnableWindow(GetDlgItem(hDlg, id), on ? TRUE : FALSE);
}

void showCtrl(HWND hDlg, int id, bool show) {
    ShowWindow(GetDlgItem(hDlg, id), show ? SW_SHOW : SW_HIDE);
}

// ── INI persistence ────────────────────────────────────────

std::wstring getINIPath(Interface* gi) {
    MSTR dir;
    if (gi)
        dir = gi->GetDir(APP_PLUGCFG_DIR);
    else {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::wstring s(buf);
        auto pos = s.find_last_of(L'\\');
        dir = MSTR(s.substr(0, pos).c_str());
    }
    return std::wstring(dir.data()) + L"\\WhiteoutDexImporter.ini";
}

// Disambiguate Win32 API from MaxSDK::Util wrappers (same pattern as mdlx_import_options.cpp)
static auto Win32_WritePrivateProfileStringW =
    static_cast<BOOL(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR)>(
        &::WritePrivateProfileStringW);

// Write settings from the dialog to the shared INI file.
// Uses the same keys as the macroscript so both UIs stay in sync.
void saveDialogSettingsToINI(HWND hDlg, bool isReforged) {
    auto gi = GetCOREInterface();
    std::wstring iniPath = getINIPath(gi);
    const wchar_t* sec = L"Settings";

    auto writeB = [&](const wchar_t* key, bool val) {
        Win32_WritePrivateProfileStringW(sec, key, val ? L"true" : L"false", iniPath.c_str());
    };
    auto writeI = [&](const wchar_t* key, int val) {
        wchar_t buf[16];
        _itow_s(val, buf, 10);
        Win32_WritePrivateProfileStringW(sec, key, buf, iniPath.c_str());
    };

    // Geometry
    writeB(L"ImportSkinning", getCheck(hDlg, IDC_CHK_SKINNED));

    // Materials
    writeB(L"ImportMaterials", getCheck(hDlg, IDC_CHK_IMPORT_MATERIALS));
    writeB(L"ImportTextures", getCheck(hDlg, IDC_CHK_IMPORT_TEXTURES));

    // Objects
    writeB(L"ImportObjects", getCheck(hDlg, IDC_CHK_IMPORT_OBJECTS));
    writeB(L"ImportBones", getCheck(hDlg, IDC_CHK_BONES));
    writeB(L"ImportHelpers", getCheck(hDlg, IDC_CHK_HELPERS));
    writeB(L"ImportLights", getCheck(hDlg, IDC_CHK_LIGHTS));
    writeB(L"ImportAttachments", getCheck(hDlg, IDC_CHK_ATTACHMENTS));
    writeB(L"ImportParticleEmitters1", getCheck(hDlg, IDC_CHK_PE1));
    writeB(L"ImportParticleEmitters2", getCheck(hDlg, IDC_CHK_PE2));
    writeB(L"ImportRibbonEmitters", getCheck(hDlg, IDC_CHK_RIBBON_EMITTERS));
    writeB(L"ImportEventObjects", getCheck(hDlg, IDC_CHK_EVENT_OBJECTS));
    writeB(L"ImportCollisionShapes", getCheck(hDlg, IDC_CHK_COLLISION_SHAPES));
    writeB(L"ImportCameras", getCheck(hDlg, IDC_CHK_CAMERAS));

    // Animations
    writeB(L"ImportAnimations", getCheck(hDlg, IDC_CHK_IMPORT_ANIMATIONS));
    writeB(L"ImportTranslation", getCheck(hDlg, IDC_CHK_TRANSLATION));
    writeB(L"ImportRotation", getCheck(hDlg, IDC_CHK_ROTATIONS));
    writeB(L"ImportScale", getCheck(hDlg, IDC_CHK_SCALE));
    writeB(L"ImportParameterAnimations", getCheck(hDlg, IDC_CHK_PARAMETER));
    writeB(L"ImportUVAnimations", getCheck(hDlg, IDC_CHK_UNWRAP_ANIMS));
    writeB(L"ImportTextureAnimations", getCheck(hDlg, IDC_CHK_TEXTURE_ANIMS));
    writeB(L"ImportVisibility", getCheck(hDlg, IDC_CHK_VISIBILITY));
    writeB(L"ImportColorAnimations", getCheck(hDlg, IDC_CHK_COLOR));

    // Helper Options
    writeB(L"ImportHelpersAsPointHelpers", getCheck(hDlg, IDC_CHK_POINT_HELPERS));

    // Optimizer
    writeB(L"OptimizeGeometry", getCheck(hDlg, IDC_CHK_OPT_GEOMETRY));
    writeB(L"OptimizeBonesAndHelpers", getCheck(hDlg, IDC_CHK_OPT_BONES));

    // Texture search flags are no longer persisted — the resolver always
    // tries whichever archive directory is configured (matches WhiteoutFlakes).

    // Mode
    writeB(L"ImportMode", getCheck(hDlg, IDC_RDO_MERGE)); // true => "Merge"

    // Fast Settings preset index (1-based to match macroscript)
    int preset = 6; // default All
    if (getCheck(hDlg, IDC_RDO_CUSTOM))          preset = 1;
    else if (getCheck(hDlg, IDC_RDO_STATIC_NO_MAT)) preset = 2;
    else if (getCheck(hDlg, IDC_RDO_STATIC_MAT))    preset = 3;
    else if (getCheck(hDlg, IDC_RDO_ANIM_NO_SKIN))  preset = 4;
    else if (getCheck(hDlg, IDC_RDO_ANIM_NO_OBJ))   preset = 5;
    else if (getCheck(hDlg, IDC_RDO_ALL))            preset = 6;
    writeI(L"FastSettings", preset);

    // Reforged objects. Same visibility gating as dialogToOptions — only
    // persist the value the user actually saw.
    if (IsWindowVisible(GetDlgItem(hDlg, IDC_CHK_CORN_EMITTERS)))
        writeB(L"ImportCornEmitters", getCheck(hDlg, IDC_CHK_CORN_EMITTERS));
    if (isReforged)
        writeB(L"ImportFaceFX", getCheck(hDlg, IDC_CHK_FACEFX));
}

// ── Populate options struct from dialog state ──────────────

void dialogToOptions(HWND hDlg, MdlxImportOptions& opts, bool isReforged) {
    auto& c = opts.core;

    c.importSkinning = getCheck(hDlg, IDC_CHK_SKINNED);

    c.importMaterials = getCheck(hDlg, IDC_CHK_IMPORT_MATERIALS);
    c.importTextures = getCheck(hDlg, IDC_CHK_IMPORT_TEXTURES);

    c.importObjects = getCheck(hDlg, IDC_CHK_IMPORT_OBJECTS);
    c.importBones = getCheck(hDlg, IDC_CHK_BONES);
    c.importHelpers = getCheck(hDlg, IDC_CHK_HELPERS);
    c.importLights = getCheck(hDlg, IDC_CHK_LIGHTS);
    c.importAttachments = getCheck(hDlg, IDC_CHK_ATTACHMENTS);
    c.importParticleEmitters1 = getCheck(hDlg, IDC_CHK_PE1);
    c.importParticleEmitters2 = getCheck(hDlg, IDC_CHK_PE2);
    c.importRibbonEmitters = getCheck(hDlg, IDC_CHK_RIBBON_EMITTERS);
    c.importEventObjects = getCheck(hDlg, IDC_CHK_EVENT_OBJECTS);
    c.importCollisionShapes = getCheck(hDlg, IDC_CHK_COLLISION_SHAPES);
    c.importCameras = getCheck(hDlg, IDC_CHK_CAMERAS);

    c.importAnimations = getCheck(hDlg, IDC_CHK_IMPORT_ANIMATIONS);
    c.importTranslation = getCheck(hDlg, IDC_CHK_TRANSLATION);
    c.importRotation = getCheck(hDlg, IDC_CHK_ROTATIONS);
    c.importScale = getCheck(hDlg, IDC_CHK_SCALE);
    c.importParameterAnimations = getCheck(hDlg, IDC_CHK_PARAMETER);
    c.importUVAnimations = getCheck(hDlg, IDC_CHK_UNWRAP_ANIMS);
    c.importTextureAnimations = getCheck(hDlg, IDC_CHK_TEXTURE_ANIMS);
    c.importVisibility = getCheck(hDlg, IDC_CHK_VISIBILITY);
    c.importColorAnimations = getCheck(hDlg, IDC_CHK_COLOR);

    c.importHelpersAsPointHelpers = getCheck(hDlg, IDC_CHK_POINT_HELPERS);

    c.optimizeGeometry = getCheck(hDlg, IDC_CHK_OPT_GEOMETRY);
    c.optimizeBonesAndHelpers = getCheck(hDlg, IDC_CHK_OPT_BONES);

    c.mode = getCheck(hDlg, IDC_RDO_MERGE)
        ? ir::CoreImportOptions::ImportMode::Merge
        : ir::CoreImportOptions::ImportMode::NewScene;

    // Preset
    if (getCheck(hDlg, IDC_RDO_CUSTOM))             c.preset = ir::CoreImportOptions::Preset::Custom;
    else if (getCheck(hDlg, IDC_RDO_STATIC_NO_MAT)) c.preset = ir::CoreImportOptions::Preset::StaticNoMaterials;
    else if (getCheck(hDlg, IDC_RDO_STATIC_MAT))    c.preset = ir::CoreImportOptions::Preset::StaticMaterials;
    else if (getCheck(hDlg, IDC_RDO_ANIM_NO_SKIN))  c.preset = ir::CoreImportOptions::Preset::AnimatedNoSkinning;
    else if (getCheck(hDlg, IDC_RDO_ANIM_NO_OBJ))   c.preset = ir::CoreImportOptions::Preset::AnimatedNoObjects;
    else                                              c.preset = ir::CoreImportOptions::Preset::All;

    // Texture search flags are no longer carried in opts — the resolver
    // always tries whichever archive directory is configured. The dialog's
    // "Search Textures" checkbox is kept for future use but currently a no-op.
    (void)IDC_CHK_SEARCH_TEXTURES;

    // Reforged objects. The Corn Emitters checkbox is honored whenever it
    // was actually visible (i.e. the file has corn data OR header is v1200);
    // otherwise leave the default-true alone so legacy paths still import
    // any stray CORN chunks. FaceFX stays Reforged-only.
    if (IsWindowVisible(GetDlgItem(hDlg, IDC_CHK_CORN_EMITTERS)))
        opts.importCornEmitters = getCheck(hDlg, IDC_CHK_CORN_EMITTERS);
    if (isReforged)
        opts.importFaceFX = getCheck(hDlg, IDC_CHK_FACEFX);
}

// ── Populate dialog controls from options struct ───────────

void optionsToDialog(HWND hDlg, const MdlxImportOptions& opts, bool isReforged) {
    const auto& c = opts.core;

    setCheck(hDlg, IDC_CHK_SKINNED, c.importSkinning);

    setCheck(hDlg, IDC_CHK_IMPORT_MATERIALS, c.importMaterials);
    setCheck(hDlg, IDC_CHK_IMPORT_TEXTURES, c.importTextures);

    setCheck(hDlg, IDC_CHK_IMPORT_OBJECTS, c.importObjects);
    setCheck(hDlg, IDC_CHK_BONES, c.importBones);
    setCheck(hDlg, IDC_CHK_HELPERS, c.importHelpers);
    setCheck(hDlg, IDC_CHK_LIGHTS, c.importLights);
    setCheck(hDlg, IDC_CHK_ATTACHMENTS, c.importAttachments);
    setCheck(hDlg, IDC_CHK_PE1, c.importParticleEmitters1);
    setCheck(hDlg, IDC_CHK_PE2, c.importParticleEmitters2);
    setCheck(hDlg, IDC_CHK_RIBBON_EMITTERS, c.importRibbonEmitters);
    setCheck(hDlg, IDC_CHK_EVENT_OBJECTS, c.importEventObjects);
    setCheck(hDlg, IDC_CHK_COLLISION_SHAPES, c.importCollisionShapes);
    setCheck(hDlg, IDC_CHK_CAMERAS, c.importCameras);

    setCheck(hDlg, IDC_CHK_IMPORT_ANIMATIONS, c.importAnimations);
    setCheck(hDlg, IDC_CHK_TRANSLATION, c.importTranslation);
    setCheck(hDlg, IDC_CHK_ROTATIONS, c.importRotation);
    setCheck(hDlg, IDC_CHK_SCALE, c.importScale);
    setCheck(hDlg, IDC_CHK_PARAMETER, c.importParameterAnimations);
    setCheck(hDlg, IDC_CHK_UNWRAP_ANIMS, c.importUVAnimations);
    setCheck(hDlg, IDC_CHK_TEXTURE_ANIMS, c.importTextureAnimations);
    setCheck(hDlg, IDC_CHK_VISIBILITY, c.importVisibility);
    setCheck(hDlg, IDC_CHK_COLOR, c.importColorAnimations);

    setCheck(hDlg, IDC_CHK_POINT_HELPERS, c.importHelpersAsPointHelpers);

    setCheck(hDlg, IDC_CHK_OPT_GEOMETRY, c.optimizeGeometry);
    setCheck(hDlg, IDC_CHK_OPT_BONES, c.optimizeBonesAndHelpers);

    // Mode
    setCheck(hDlg, IDC_RDO_NEW_SCENE, c.mode == ir::CoreImportOptions::ImportMode::NewScene);
    setCheck(hDlg, IDC_RDO_MERGE, c.mode == ir::CoreImportOptions::ImportMode::Merge);

    // Preset radio
    setCheck(hDlg, IDC_RDO_CUSTOM,        c.preset == ir::CoreImportOptions::Preset::Custom);
    setCheck(hDlg, IDC_RDO_STATIC_NO_MAT, c.preset == ir::CoreImportOptions::Preset::StaticNoMaterials);
    setCheck(hDlg, IDC_RDO_STATIC_MAT,    c.preset == ir::CoreImportOptions::Preset::StaticMaterials);
    setCheck(hDlg, IDC_RDO_ANIM_NO_SKIN,  c.preset == ir::CoreImportOptions::Preset::AnimatedNoSkinning);
    setCheck(hDlg, IDC_RDO_ANIM_NO_OBJ,   c.preset == ir::CoreImportOptions::Preset::AnimatedNoObjects);
    setCheck(hDlg, IDC_RDO_ALL,           c.preset == ir::CoreImportOptions::Preset::All);

    // Texture search checkbox is currently a no-op (kept for future use);
    // the resolver always tries whichever archive directory is configured.
    setCheck(hDlg, IDC_CHK_SEARCH_TEXTURES, !opts.cascDirectory.empty() ||
                                             !opts.mpqDirectory.empty());

    // Reforged objects
    setCheck(hDlg, IDC_CHK_CORN_EMITTERS, opts.importCornEmitters);
    setCheck(hDlg, IDC_CHK_FACEFX, opts.importFaceFX);
}

// ── Cascading enable/disable (mirrors macroscript logic) ───

// Object sub-checkboxes controlled by "Import Objects"
static const int kObjectControls[] = {
    IDC_CHK_IMPORT_OBJECTS, IDC_CHK_BONES, IDC_CHK_HELPERS,
    IDC_CHK_LIGHTS, IDC_CHK_ATTACHMENTS, IDC_CHK_PE1, IDC_CHK_PE2,
    IDC_CHK_RIBBON_EMITTERS, IDC_CHK_EVENT_OBJECTS,
    IDC_CHK_COLLISION_SHAPES, IDC_CHK_CAMERAS,
    IDC_CHK_CORN_EMITTERS, IDC_CHK_FACEFX
};

// Animation sub-checkboxes controlled by "Import Animations"
static const int kAnimControls[] = {
    IDC_CHK_IMPORT_ANIMATIONS, IDC_CHK_TRANSLATION, IDC_CHK_ROTATIONS,
    IDC_CHK_SCALE, IDC_CHK_PARAMETER, IDC_CHK_UNWRAP_ANIMS,
    IDC_CHK_TEXTURE_ANIMS, IDC_CHK_VISIBILITY, IDC_CHK_COLOR
};

// All checkboxes controlled by Fast Settings presets
static const int kFastSettingsControls[] = {
    IDC_CHK_SKINNED, IDC_CHK_IMPORT_MATERIALS, IDC_CHK_IMPORT_TEXTURES,
    IDC_CHK_IMPORT_OBJECTS, IDC_CHK_BONES, IDC_CHK_HELPERS,
    IDC_CHK_LIGHTS, IDC_CHK_ATTACHMENTS, IDC_CHK_PE1, IDC_CHK_PE2,
    IDC_CHK_EVENT_OBJECTS, IDC_CHK_RIBBON_EMITTERS,
    IDC_CHK_COLLISION_SHAPES, IDC_CHK_CAMERAS,
    IDC_CHK_CORN_EMITTERS, IDC_CHK_FACEFX,
    IDC_CHK_IMPORT_ANIMATIONS, IDC_CHK_TRANSLATION, IDC_CHK_ROTATIONS,
    IDC_CHK_SCALE, IDC_CHK_PARAMETER, IDC_CHK_UNWRAP_ANIMS,
    IDC_CHK_TEXTURE_ANIMS, IDC_CHK_VISIBILITY, IDC_CHK_COLOR
};

void enableObjects(HWND hDlg, bool on) {
    for (int id : kObjectControls)
        enableCtrl(hDlg, id, on);
}

void enableAnimations(HWND hDlg, bool on) {
    for (int id : kAnimControls)
        enableCtrl(hDlg, id, on);
}

void checkObjects(HWND hDlg, bool state) {
    setCheck(hDlg, IDC_CHK_IMPORT_OBJECTS, state);
    setCheck(hDlg, IDC_CHK_BONES, state);
    setCheck(hDlg, IDC_CHK_HELPERS, state);
    setCheck(hDlg, IDC_CHK_LIGHTS, state);
    setCheck(hDlg, IDC_CHK_ATTACHMENTS, state);
    setCheck(hDlg, IDC_CHK_PE1, state);
    setCheck(hDlg, IDC_CHK_PE2, state);
    setCheck(hDlg, IDC_CHK_RIBBON_EMITTERS, state);
    setCheck(hDlg, IDC_CHK_EVENT_OBJECTS, state);
    setCheck(hDlg, IDC_CHK_COLLISION_SHAPES, state);
    setCheck(hDlg, IDC_CHK_CAMERAS, state);
    setCheck(hDlg, IDC_CHK_CORN_EMITTERS, state);
    setCheck(hDlg, IDC_CHK_FACEFX, state);
}

void checkBH(HWND hDlg, bool state) {
    setCheck(hDlg, IDC_CHK_IMPORT_OBJECTS, state);
    setCheck(hDlg, IDC_CHK_BONES, state);
    setCheck(hDlg, IDC_CHK_HELPERS, state);
}

void checkAnimations(HWND hDlg, bool state) {
    setCheck(hDlg, IDC_CHK_IMPORT_ANIMATIONS, state);
    setCheck(hDlg, IDC_CHK_TRANSLATION, state);
    setCheck(hDlg, IDC_CHK_ROTATIONS, state);
    setCheck(hDlg, IDC_CHK_SCALE, state);
    setCheck(hDlg, IDC_CHK_PARAMETER, state);
    setCheck(hDlg, IDC_CHK_UNWRAP_ANIMS, state);
    setCheck(hDlg, IDC_CHK_TEXTURE_ANIMS, state);
    setCheck(hDlg, IDC_CHK_VISIBILITY, state);
    setCheck(hDlg, IDC_CHK_COLOR, state);
}

void checkMaterials(HWND hDlg, bool state) {
    setCheck(hDlg, IDC_CHK_IMPORT_MATERIALS, state);
    setCheck(hDlg, IDC_CHK_IMPORT_TEXTURES, state);
}

// Returns true if the current preset is Custom
bool isCustomPreset(HWND hDlg) {
    return getCheck(hDlg, IDC_RDO_CUSTOM);
}

// Apply the cascading enabled state after preset / checkbox changes
void applyEnabledState(HWND hDlg) {
    bool custom = isCustomPreset(hDlg);

    // When not Custom, all fast-settings-controlled checkboxes are disabled
    for (int id : kFastSettingsControls)
        enableCtrl(hDlg, id, custom);

    // If not custom but animations are off, disable anim sub-checkboxes
    if (!getCheck(hDlg, IDC_CHK_IMPORT_ANIMATIONS)) {
        enableAnimations(hDlg, false);
        if (custom) enableCtrl(hDlg, IDC_CHK_IMPORT_ANIMATIONS, true);
    }

    // If not custom but objects are off, disable object sub-checkboxes
    if (!getCheck(hDlg, IDC_CHK_IMPORT_OBJECTS)) {
        enableObjects(hDlg, false);
        if (custom) enableCtrl(hDlg, IDC_CHK_IMPORT_OBJECTS, true);
    }

    // These three are ALWAYS enabled regardless of preset
    enableCtrl(hDlg, IDC_CHK_POINT_HELPERS, true);
    enableCtrl(hDlg, IDC_CHK_OPT_GEOMETRY, true);
    enableCtrl(hDlg, IDC_CHK_OPT_BONES, true);
}

// Handle a fast settings radio button change
void onFastSettingsChanged(HWND hDlg, DialogState* ds) {
    ds->onControlSet = false;

    if (getCheck(hDlg, IDC_RDO_CUSTOM)) {
        // Custom — enable all
    }
    else if (getCheck(hDlg, IDC_RDO_STATIC_NO_MAT)) {
        checkMaterials(hDlg, false);
        setCheck(hDlg, IDC_CHK_SKINNED, false);
        checkAnimations(hDlg, false);
        checkObjects(hDlg, false);
    }
    else if (getCheck(hDlg, IDC_RDO_STATIC_MAT)) {
        checkMaterials(hDlg, true);
        setCheck(hDlg, IDC_CHK_SKINNED, false);
        checkAnimations(hDlg, false);
        checkObjects(hDlg, false);
    }
    else if (getCheck(hDlg, IDC_RDO_ANIM_NO_SKIN)) {
        checkMaterials(hDlg, true);
        setCheck(hDlg, IDC_CHK_SKINNED, false);
        checkAnimations(hDlg, true);
        checkObjects(hDlg, false);
        checkBH(hDlg, true);
    }
    else if (getCheck(hDlg, IDC_RDO_ANIM_NO_OBJ)) {
        checkMaterials(hDlg, true);
        setCheck(hDlg, IDC_CHK_SKINNED, true);
        checkAnimations(hDlg, true);
        checkObjects(hDlg, false);
        checkBH(hDlg, true);
    }
    else if (getCheck(hDlg, IDC_RDO_ALL)) {
        checkMaterials(hDlg, true);
        setCheck(hDlg, IDC_CHK_SKINNED, true);
        checkAnimations(hDlg, true);
        checkObjects(hDlg, true);
    }

    applyEnabledState(hDlg);
    ds->onControlSet = true;
}

// ── Localization ───────────────────────────────────────────
//
// The .rc gives every control an English caption; this replaces them from the
// shared catalog (src/pre_startup_scripts/WhiteoutDexLocalization.ms) in one
// MaxScript round trip. Entries the catalog is missing keep their English, so
// the table can list controls whose keys have not been translated yet.
//
// The texture-search group and the window caption are deliberately absent:
// they say something different for Classic and Reforged, so
// setupReforgedVisibility sets them afterwards from its own pair of keys.

constexpr wdx::l10n::DialogString kImportStrings[] = {
    {IDC_GRP_GEOMETRY, "imp_geometry_grp"},
    {IDC_CHK_SKINNED, "imp_skinned_chk"},

    {IDC_GRP_MATERIALS, "imp_materials_grp"},
    {IDC_CHK_IMPORT_MATERIALS, "imp_import_materials_chk"},
    {IDC_CHK_IMPORT_TEXTURES, "imp_import_textures_chk"},

    {IDC_GRP_OBJECTS, "imp_objects_grp"},
    {IDC_CHK_IMPORT_OBJECTS, "imp_import_objects_chk"},
    {IDC_CHK_BONES, "imp_bones_chk"},
    {IDC_CHK_HELPERS, "imp_helpers_chk"},
    {IDC_CHK_LIGHTS, "imp_lights_chk"},
    {IDC_CHK_ATTACHMENTS, "imp_attachments_chk"},
    {IDC_CHK_PE1, "imp_particle_emitters_1_chk"},
    {IDC_CHK_PE2, "imp_particle_emitters_2_chk"},
    {IDC_CHK_EVENT_OBJECTS, "imp_event_objects_chk"},
    {IDC_CHK_RIBBON_EMITTERS, "imp_ribbon_emitters_chk"},
    {IDC_CHK_COLLISION_SHAPES, "imp_collision_shapes_chk"},
    {IDC_CHK_CAMERAS, "imp_cameras_chk"},
    {IDC_CHK_CORN_EMITTERS, "imp_corn_emitters_chk"},
    {IDC_CHK_FACEFX, "imp_facefx_chk"},

    {IDC_GRP_ANIMATIONS, "imp_animations_grp"},
    {IDC_CHK_IMPORT_ANIMATIONS, "imp_import_animations_chk"},
    {IDC_CHK_TRANSLATION, "imp_translation_chk"},
    {IDC_CHK_ROTATIONS, "imp_rotations_chk"},
    {IDC_CHK_SCALE, "imp_scale_chk"},
    {IDC_CHK_PARAMETER, "imp_parameter_chk"},
    {IDC_CHK_UNWRAP_ANIMS, "imp_unwrap_animations_chk"},
    {IDC_CHK_TEXTURE_ANIMS, "imp_texture_animations_chk"},
    {IDC_CHK_VISIBILITY, "imp_visibility_chk"},
    {IDC_CHK_COLOR, "imp_color_chk"},

    {IDC_GRP_SETTINGS, "imp_settings_grp"},
    {IDC_LBL_FAST_SETTINGS, "imp_fast_settings_lbl"},
    {IDC_LBL_MODE, "imp_mode_lbl"},

    {IDC_GRP_HELPER_OPTIONS, "imp_helper_options_grp"},
    {IDC_CHK_POINT_HELPERS, "imp_import_helpers_as_point_helpers_chk"},

    {IDC_GRP_OPTIMIZER, "imp_optimizer_grp"},
    {IDC_CHK_OPT_GEOMETRY, "imp_optimize_geometry_chk"},
    {IDC_CHK_OPT_BONES, "imp_optimize_bones_and_helpers_chk"},

    {IDC_GRP_PROGRESS, "imp_progress_grp"},
    {IDC_LBL_STATUS, "imp_idle_lbl"},

    {IDOK, "imp_import_btn"},
    {IDCANCEL, "common_cancel_btn"},
};

// The two radio groups share their catalog entries with the MaxScript UI, which
// reads the same pipe-joined strings through WdxL.tList.
constexpr int kFastSettingsIds[] = {
    IDC_RDO_CUSTOM, IDC_RDO_STATIC_NO_MAT, IDC_RDO_STATIC_MAT,
    IDC_RDO_ANIM_NO_SKIN, IDC_RDO_ANIM_NO_OBJ, IDC_RDO_ALL,
};
constexpr int kModeIds[] = {IDC_RDO_NEW_SCENE, IDC_RDO_MERGE};

void localizeDialog(HWND hDlg) {
    wdx::l10n::LocalizeDialog(hDlg, kImportStrings);
    wdx::l10n::LocalizeRadioGroup(hDlg, "imp_fast_settings_labels", kFastSettingsIds);
    wdx::l10n::LocalizeRadioGroup(hDlg, "imp_mode_labels", kModeIds);
}

// ── v1200 control visibility ───────────────────────────────

void setupReforgedVisibility(HWND hDlg, bool isReforged, bool hasCornEmitters) {
    // Corn Emitters checkbox is shown when EITHER the header is Reforged OR
    // the file actually has CORN chunks (legacy header + Reforged chunks does
    // happen in the wild). FaceFX stays Reforged-only.
    const bool showCorn = isReforged || hasCornEmitters;
    showCtrl(hDlg, IDC_CHK_CORN_EMITTERS, showCorn);
    showCtrl(hDlg, IDC_CHK_FACEFX, isReforged);

    // Update group title for texture search. Localized here rather than in
    // kImportStrings because which string is right depends on the header we
    // just read; LocalizeDialog has already run, so these overwrite it.
    if (isReforged) {
        const wdx::l10n::DialogString reforged[] = {
            {IDC_GRP_TEXTURES, "imp_casc_textures_grp"},
            {IDC_CHK_SEARCH_TEXTURES, "imp_search_casc_chk"},
            {0, "imp_whiteoutdex_importer_reforged_title"},
        };
        SetDlgItemTextW(hDlg, IDC_GRP_TEXTURES, L"CASC Textures");
        SetDlgItemTextW(hDlg, IDC_CHK_SEARCH_TEXTURES, L"Search CASC Archives for Textures");
        SetWindowTextW(hDlg, L"WhiteoutDex MDX Importer (Reforged)");
        wdx::l10n::LocalizeDialog(hDlg, reforged);
    } else {
        const wdx::l10n::DialogString classic[] = {
            {IDC_GRP_TEXTURES, "imp_mpq_textures_grp"},
            {IDC_CHK_SEARCH_TEXTURES, "imp_search_mpq_chk"},
        };
        SetDlgItemTextW(hDlg, IDC_GRP_TEXTURES, L"MPQ Textures");
        SetDlgItemTextW(hDlg, IDC_CHK_SEARCH_TEXTURES, L"Search MPQ Archives for Textures");
        wdx::l10n::LocalizeDialog(hDlg, classic);
    }

    // If neither the corn nor FaceFX row is shown, shrink the Objects group
    // slightly so the empty row doesn't leave a gap.
    if (!showCorn && !isReforged) {
        HWND hGrp = GetDlgItem(hDlg, IDC_GRP_OBJECTS);
        RECT rc;
        GetWindowRect(hGrp, &rc);
        MapWindowPoints(HWND_DESKTOP, hDlg, reinterpret_cast<LPPOINT>(&rc), 2);
        MoveWindow(hGrp, rc.left, rc.top, rc.right - rc.left, (rc.bottom - rc.top) - 20, TRUE);
    }
}

} // anonymous namespace

// ============================================================================
// Dialog Procedure
// ============================================================================

static INT_PTR CALLBACK ImportDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    DialogState* ds = reinterpret_cast<DialogState*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        ds = reinterpret_cast<DialogState*>(lParam);
        SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ds));

        wdx::ApplyWindowIcon(hDlg, IDI_WHITEOUTDEX_ICON);

        // Relabel from the catalog before anything reads a caption back.
        localizeDialog(hDlg);

        // Populate controls from current options
        optionsToDialog(hDlg, *ds->opts, ds->isReforged);

        // Show/hide reforged-specific controls.
        setupReforgedVisibility(hDlg, ds->isReforged, ds->hasCornEmitters);

        // Apply enable/disable cascading
        applyEnabledState(hDlg);

        // Center dialog on parent
        CenterWindow(hDlg, GetParent(hDlg));
        return TRUE;
    }

    case WM_COMMAND: {
        if (!ds) break;
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        switch (id) {
        case IDOK:
            // Import pressed
            dialogToOptions(hDlg, *ds->opts, ds->isReforged);
            saveDialogSettingsToINI(hDlg, ds->isReforged);
            ds->confirmed = true;
            EndDialog(hDlg, IDOK);
            return TRUE;

        case IDCANCEL:
            ds->confirmed = false;
            EndDialog(hDlg, IDCANCEL);
            return TRUE;

        // ── Fast Settings radio buttons ──
        case IDC_RDO_CUSTOM:
        case IDC_RDO_STATIC_NO_MAT:
        case IDC_RDO_STATIC_MAT:
        case IDC_RDO_ANIM_NO_SKIN:
        case IDC_RDO_ANIM_NO_OBJ:
        case IDC_RDO_ALL:
            if (code == BN_CLICKED)
                onFastSettingsChanged(hDlg, ds);
            return TRUE;

        // ── Import Objects master toggle ──
        case IDC_CHK_IMPORT_OBJECTS:
            if (code == BN_CLICKED && ds->onControlSet) {
                ds->onControlSet = false;
                bool state = getCheck(hDlg, IDC_CHK_IMPORT_OBJECTS);
                checkObjects(hDlg, state);
                enableObjects(hDlg, state);
                enableCtrl(hDlg, IDC_CHK_IMPORT_OBJECTS, true);
                ds->onControlSet = true;
            }
            return TRUE;

        // ── Import Animations master toggle ──
        case IDC_CHK_IMPORT_ANIMATIONS:
            if (code == BN_CLICKED && ds->onControlSet) {
                ds->onControlSet = false;
                bool state = getCheck(hDlg, IDC_CHK_IMPORT_ANIMATIONS);
                checkAnimations(hDlg, state);
                enableAnimations(hDlg, state);
                enableCtrl(hDlg, IDC_CHK_IMPORT_ANIMATIONS, true);
                ds->onControlSet = true;
            }
            return TRUE;

        // ── Bones unchecked → also uncheck Skinned ──
        case IDC_CHK_BONES:
            if (code == BN_CLICKED && ds->onControlSet) {
                ds->onControlSet = false;
                if (!getCheck(hDlg, IDC_CHK_BONES))
                    setCheck(hDlg, IDC_CHK_SKINNED, false);
                ds->onControlSet = true;
            }
            return TRUE;

        default:
            break;
        }
        break;
    }

    case WM_CLOSE:
        if (ds) ds->confirmed = false;
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }

    return FALSE;
}

// ============================================================================
// Public API
// ============================================================================

bool showImportDialog(HINSTANCE hInstance, HWND hWndParent,
                      MdlxImportOptions& opts, bool isReforged,
                      bool hasCornEmitters)
{
    DialogState ds{};
    ds.opts = &opts;
    ds.isReforged = isReforged;
    ds.hasCornEmitters = hasCornEmitters;
    ds.confirmed = false;
    ds.onControlSet = true;

    INT_PTR result = DialogBoxParamW(
        hInstance, MAKEINTRESOURCEW(IDD_IMPORT_OPTIONS),
        hWndParent, ImportDialogProc,
        reinterpret_cast<LPARAM>(&ds));

    return (result == IDOK) && ds.confirmed;
}
