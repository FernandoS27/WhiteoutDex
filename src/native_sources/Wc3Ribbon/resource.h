//{{NO_DEPENDENCIES}}
// Used by Wc3Ribbon.rc
//

#pragma once

#ifndef SS_ETCHEDHORZ
#define SS_ETCHEDHORZ 0x10
#endif

/// @name Rollout map indices
/// @{
#define MAP_MATERIAL                0
#define MAP_PROPERTIES              1
#define MAP_COUNT                   2
/// @}

/// @name Dialog IDs
/// @{
#define IDD_ROLLOUT_PROPERTIES      119
#define IDD_ROLLOUT_MATERIAL        120
/// @}

/// @name String Table IDs
/// @{
#define IDS_GENERAL                 2
#define IDS_CLASSNAME               4
#define IDS_PARAMCHANGE             6
#define IDS_LENGTH                  12
#define IDS_WIDTH                   13
#define IDS_LIBDESCRIPTION          16
#define IDS_PARAMS                  18
#define IDS_HEIGHTABOVE             19
#define IDS_HEIGHTBELOW             27
#define IDS_EDGESPERSEC             28
#define IDS_EDGELIFETIME            29
#define IDS_TEXROWS                 30
#define IDS_TEXCOLS                 31
#define IDS_TEXSLOT                 32
#define IDS_MATERIAL                33
#define IDS_COLOR                   34
#define IDS_ALPHA                   35
#define IDS_GRAVITY                 36
#define IDS_CATEGORY                38
#define IDS_ROLLOUT_PROPERTIES      39
#define IDS_ROLLOUT_MATERIAL        46
/// @}

/// @name Spinner / Edit control IDs (Ribbon Properties)
/// @{
#define IDC_EDIT_HEIGHTABOVE        1059
#define IDC_SPIN_HEIGHTABOVE        1060
#define IDC_COLORSWATCH             1066
#define IDC_EDIT_HEIGHTBELOW        1067
#define IDC_SPIN_HEIGHTBELOW        1068
#define IDC_EDIT_EDGESPERSEC        1069
#define IDC_SPIN_EDGESPERSEC        1070
#define IDC_EDIT_EDGELIFETIME       1071
#define IDC_SPIN_EDGELIFETIME       1072
#define IDC_EDIT_ALPHA              1080
#define IDC_SPIN_ALPHA              1081
#define IDC_EDIT_GRAVITY            1082
#define IDC_SPIN_GRAVITY            1083
/// @}

/// @name Controller-type combobox IDs
/// @{
#define IDC_CTRL_COLOR              1090
#define IDC_CTRL_ABOVE              1091
#define IDC_CTRL_BELOW              1092
#define IDC_CTRL_ALPHA              1093
#define IDC_CTRL_EMISSION           1094
#define IDC_CTRL_LIFESPAN           1095
#define IDC_CTRL_GRAVITY            1096
#define IDC_CTRL_TEXSLOT            1097
/// @}

/// @name Material rollout controls
/// @{
#define IDC_MTLBUTTON               1079
#define IDC_EDIT_TEXROWS            1073
#define IDC_SPIN_TEXROWS            1074
#define IDC_EDIT_TEXCOLS            1075
#define IDC_SPIN_TEXCOLS            1076
#define IDC_EDIT_TEXSLOT            1077
#define IDC_SPIN_TEXSLOT            1078
/// @}


/// @name Static-label IDs for translation
/// These captions were declared with id -1, which SetDlgItemText cannot
/// reach. They are numbered from 6000 — above every other control ID in
/// this plug-in — so the rollout can be relabelled from the shared
/// catalog at WM_INITDIALOG. See common/wdx_localization.h.
/// @{
#define WDXRB_LBL_RIBBON_COLOR                   6000
#define WDXRB_LBL_CONTROLLER                     6001
#define WDXRB_LBL_ABOVE_LENGTH                   6002
#define WDXRB_LBL_CONTROLLER_2                   6003
#define WDXRB_LBL_BELOW_LENGTH                   6004
#define WDXRB_LBL_CONTROLLER_3                   6005
#define WDXRB_LBL_ALPHA                          6006
#define WDXRB_LBL_CONTROLLER_4                   6007
#define WDXRB_LBL_EMISSION_RATE                  6008
#define WDXRB_LBL_CONTROLLER_5                   6009
#define WDXRB_LBL_LIFE_SPAN                      6010
#define WDXRB_LBL_CONTROLLER_6                   6011
#define WDXRB_LBL_GRAVITY                        6012
#define WDXRB_LBL_CONTROLLER_7                   6013
#define WDXRB_LBL_MATERIAL                       6014
#define WDXRB_LBL_SEQUENCE_MODE                  6015
#define WDXRB_LBL_ROWS                           6016
#define WDXRB_LBL_COLS                           6017
#define WDXRB_LBL_SEQUENCE_POSITION              6018
#define WDXRB_LBL_CONTROLLER_8                   6019
/// @}

#ifdef APSTUDIO_INVOKED
#ifndef APSTUDIO_READONLY_SYMBOLS
#define _APS_NEXT_RESOURCE_VALUE    121
#define _APS_NEXT_COMMAND_VALUE     40001
#define _APS_NEXT_CONTROL_VALUE     1098
#define _APS_NEXT_SYMED_VALUE       101
#endif
#endif
