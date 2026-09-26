// MDLXExporter — Resource IDs for the export options dialog
#pragma once

// Dialog
#define IDD_EXPORT_OPTIONS          101

// ── Model name / format (always visible) ──
#define IDC_LBL_MODEL_NAME          1000
#define IDC_EDT_MODEL_NAME          1001
#define IDC_LBL_FORMAT              1002
#define IDC_BTN_FORMAT_CLASSIC      1011  // owner-drawn format card
#define IDC_BTN_FORMAT_REFORGED     1012  // owner-drawn format card

// ── Tabs (owner-drawn buttons, not a SysTabControl32) ──
#define IDC_BTN_TAB_GEOMETRY        1015
#define IDC_BTN_TAB_TEXTURES        1016

// ── Geometry tab ──
#define IDC_CHK_MERGE_SIMILAR       1021
#define IDC_CHK_FIX_SHARED_NORMALS  1027
#define IDC_CHK_KEEP_UNUSED_BH      1028
#define IDC_CHK_QUANTIZE_SKIN       1032  // inverted "Disable Skin Quantize"
#define IDC_LBL_EXTENTS_PREC        1043
#define IDC_EDT_EXTENTS_PREC        1044
#define IDC_SPIN_EXTENTS_PREC       1045

// ── Footer (owner-drawn check buttons) ──
#define IDC_CHK_AUTO_INCREMENT      1030
#define IDC_CHK_OPEN_FOLDER         1031

// ── Textures tab ──
// Shared (visible regardless of format version)
#define IDC_CHK_TEX_CONVERT         1060
#define IDC_CHK_TEX_MIPMAPS         1061
#define IDC_CHK_TEX_OVERWRITE       1062
// BLP-specific (visible only for Classic v800)
#define IDC_LBL_BLP_COMPRESSION     1070
#define IDC_CMB_BLP_COMPRESSION     1071
#define IDC_LBL_BLP_JPEG_QUALITY    1072
#define IDC_EDT_BLP_JPEG_QUALITY    1073
#define IDC_SPIN_BLP_JPEG_QUALITY   1074
#define IDC_CHK_BLP_DITHERING       1075
// DDS-specific (visible only for Reforged v1800)
#define IDC_LBL_DDS_FORMAT          1080
#define IDC_CMB_DDS_FORMAT          1081

// ── Scene check banner (the banner itself is painted by the dialog) ──
#define IDC_BTN_SCENE_FIX_ALL        1092
#define IDC_BTN_SCENE_DETAILS        1093

// Problem Details Dialog
#define IDD_PROBLEM_DETAILS          102
#define IDC_LV_PROBLEMS              2000
#define IDC_LBL_PROBLEM_STATUS       2001
#define IDC_BTN_FIX_SELECTED         2002
#define IDC_BTN_FIX_ALL              2003
#define IDC_BTN_RESCAN               2004
// The prompt above the list. It was -1 (unaddressable) until the dialog
// started relabelling itself from the translation catalog, which needs an id.
#define IDC_LBL_PROBLEM_PROMPT       2005

// ── Window icon ──
#define IDI_WHITEOUTDEX_ICON        200
