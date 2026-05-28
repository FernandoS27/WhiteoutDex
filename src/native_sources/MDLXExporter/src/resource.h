// MDLXExporter — Resource IDs for the export options dialog
#pragma once

// Dialog
#define IDD_EXPORT_OPTIONS          101

// ── Model Name ──
#define IDC_GRP_MODEL_NAME          1000
#define IDC_EDT_MODEL_NAME          1001

// ── Format Version ──
#define IDC_GRP_FORMAT_VERSION      1010
#define IDC_RDO_CLASSIC             1011
#define IDC_RDO_REFORGED            1012

// ── Tab Control ──
#define IDC_TAB_CONTROL             1015

// ── Options (Tab 0) ──
#define IDC_GRP_OPTIONS             1020
#define IDC_CHK_MERGE_SIMILAR       1021
#define IDC_CHK_FIX_SHARED_NORMALS  1027
#define IDC_CHK_KEEP_UNUSED_BH      1028
#define IDC_CHK_DISABLE_SKINQUANT   1029
#define IDC_CHK_AUTO_INCREMENT      1030
#define IDC_CHK_OPEN_FOLDER         1031

// ── Texture Conversion (Tab 1) ──
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
// DDS-specific (visible only for Reforged v1200)
#define IDC_LBL_DDS_FORMAT          1080
#define IDC_CMB_DDS_FORMAT          1081

// ── Extents Calculation ──
#define IDC_GRP_EXTENTS             1040
#define IDC_LBL_EXTENTS_PREC       1043
#define IDC_EDT_EXTENTS_PREC       1044
#define IDC_SPIN_EXTENTS_PREC      1045

// ── Progress ──
#define IDC_GRP_PROGRESS            1050
#define IDC_LBL_STATUS              1051
#define IDC_PROGRESS_BAR            1052

// ── Scene Monitor (Status Panel + Problem Details Dialog) ─────────────
// Status panel inside main export dialog
#define IDC_GRP_SCENE_STATUS         1090
#define IDC_LBL_SCENE_STATUS         1091
#define IDC_BTN_SCENE_FIX_ALL        1092
#define IDC_BTN_SCENE_DETAILS        1093
#define IDC_PNL_SCENE_STATUS_BAR     1094  // Colored status bar (green/red)

// ── Material Fix Settings (Tab 1, between Options and Texture Conversion) ──
// Basic Parameters group
#define IDC_GRP_MAT_BASIC            1100
#define IDC_CHK_MAT_UNSHADED         1101
#define IDC_CHK_MAT_UNFOGGED         1102
#define IDC_CHK_MAT_TWOSIDED         1103
// Filter Mode group (radio group)
#define IDC_GRP_MAT_FILTER           1110
#define IDC_RDO_MAT_FILTER_NONE      1111
#define IDC_RDO_MAT_FILTER_TRANSP    1112
#define IDC_RDO_MAT_FILTER_BLEND     1113
#define IDC_RDO_MAT_FILTER_ADD       1114
#define IDC_RDO_MAT_FILTER_ADD2X     1115
#define IDC_RDO_MAT_FILTER_MOD       1116
#define IDC_RDO_MAT_FILTER_MOD2X     1117
// Texture Parameters group
#define IDC_GRP_MAT_TEXPARAMS        1120
#define IDC_LBL_MAT_PREFIX           1121
#define IDC_EDT_MAT_PREFIX           1122
#define IDC_BTN_MAT_PREFIX_DROP      1123
#define IDC_BTN_MAT_PREFIX_EDIT      1124
#define IDC_CHK_MAT_UTILE            1125
#define IDC_CHK_MAT_VTILE            1126

// Problem Details Dialog
#define IDD_PROBLEM_DETAILS          102
#define IDC_LV_PROBLEMS              2000
#define IDC_LBL_PROBLEM_STATUS       2001
#define IDC_BTN_FIX_SELECTED         2002
#define IDC_BTN_FIX_ALL              2003
#define IDC_BTN_RESCAN               2004
