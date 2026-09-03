/**
 * @file resource.h
 * @brief Resource ID definitions for the Wc3Particles1 3ds Max plugin.
 *
 * Contains dialog, control, and string-table resource IDs shared between
 * the C++ source and the Wc3Particles1.rc resource script.
 */

#pragma once

/// @name Dialog IDs
/// @{
#define IDD_P1_ROLLOUT_CONFIG       180  ///< Import/Export config rollout.
#define IDD_P1_ROLLOUT_EMITTER      181  ///< Emitter Options rollout.
#define IDD_P1_ROLLOUT_TIMING       182  ///< Timing Options rollout.
#define IDD_P1_ROLLOUT_MODEL        183  ///< Model Options rollout.
/// @}

/// @name Button and checkbox control IDs
/// @{
#define IDC_P1_BUTTON_IMPORT        2020  ///< Import particle config button.
#define IDC_P1_BUTTON_EXPORT        2021  ///< Export particle config button.
#define IDC_P1_CHECK_LOAD_DYNAMIC   2022  ///< Load animated keys checkbox.
#define IDC_P1_BUTTON_BROWSE        2023  ///< File-browse button for the model path.
#define IDC_P1_BUTTON_BROWSE_CASC   2024  ///< Archive-browse button (CASC/MPQ) for the model path.
/// @}

/// @name Edit/Spinner control IDs
/// @{
#define IDC_P1_EDIT_COUNT           5000
#define IDC_P1_SPIN_COUNT           5001
#define IDC_P1_EDIT_SPEED           5002
#define IDC_P1_SPIN_SPEED           5003
#define IDC_P1_EDIT_LATITUDE        5004
#define IDC_P1_SPIN_LATITUDE        5005
#define IDC_P1_EDIT_LONGITUDE       5006
#define IDC_P1_SPIN_LONGITUDE       5007
#define IDC_P1_EDIT_ACCEL           5008
#define IDC_P1_SPIN_ACCEL           5009
#define IDC_P1_EDIT_LIFE            5010
#define IDC_P1_SPIN_LIFE            5011
#define IDC_P1_EDIT_EMISSION        5012
#define IDC_P1_SPIN_EMISSION        5013
#define IDC_P1_EDIT_SCALE           5014
#define IDC_P1_SPIN_SCALE           5015
#define IDC_P1_CUSTOMEDIT_PATH      5016
#define IDC_P1_EDIT_PATH_PREFIX     5017
/// @}

/// @name Controller-type combobox IDs
/// @{
#define IDC_P1_CTRL_SPEED           2030  ///< Speed controller type combo.
#define IDC_P1_CTRL_LATITUDE        2031  ///< Latitude controller type combo.
#define IDC_P1_CTRL_LONGITUDE       2032  ///< Longitude controller type combo.
#define IDC_P1_CTRL_ACCEL           2033  ///< Acceleration controller type combo.
#define IDC_P1_CTRL_EMISSION        2034  ///< Emission Rate controller type combo.
/// @}

/// @name String resource IDs
/// @{
#define IDS_P1_CLASS_NAME           32100  ///< Plugin class name.
#define IDS_P1_OBJECT_NAME          32101  ///< Object name in viewport label.
#define IDS_P1_CATEGORY             32102  ///< Category in Create panel.
#define IDS_P1_LIB_DESCRIPTION      32103  ///< String returned by LibDescription().
#define IDS_P1_PARAM_COUNT          32110  ///< Count parameter name.
#define IDS_P1_PARAM_SPEED          32111  ///< Speed parameter name.
#define IDS_P1_PARAM_EMISSION       32112  ///< Emission rate parameter name.
#define IDS_P1_PARAM_LIFE           32113  ///< Lifespan parameter name.
#define IDS_P1_PARAM_ACCEL          32114  ///< Acceleration parameter name.
#define IDS_P1_PARAM_LATITUDE       32115  ///< Latitude parameter name.
#define IDS_P1_PARAM_LONGITUDE      32116  ///< Longitude parameter name.
#define IDS_P1_PARAM_SCALE          32117  ///< Scale parameter name.
#define IDS_P1_ROLLOUT_CONFIG       32120  ///< Config rollout title.
#define IDS_P1_ROLLOUT_EMITTER      32121  ///< Emitter rollout title.
#define IDS_P1_ROLLOUT_TIMING       32122  ///< Timing rollout title.
#define IDS_P1_ROLLOUT_MODEL        32123  ///< Model rollout title.
/// @}

/// @name Static-label IDs for translation
/// These captions were declared with id -1, which SetDlgItemText cannot
/// reach. They are numbered from 6000 — above every other control ID in
/// this plug-in — so the rollout can be relabelled from the shared
/// catalog at WM_INITDIALOG. See common/wdx_localization.h.
/// @{
#define WDXP1_LBL_COUNT                          6000
#define WDXP1_LBL_SPEED                          6001
#define WDXP1_LBL_CONTROL                        6002
#define WDXP1_LBL_LATITUDE                       6003
#define WDXP1_LBL_CONTROL_2                      6004
#define WDXP1_LBL_LONGITUDE                      6005
#define WDXP1_LBL_CONTROL_3                      6006
#define WDXP1_LBL_GRAVITY                        6007
#define WDXP1_LBL_CONTROL_4                      6008
#define WDXP1_LBL_LIFESPAN                       6009
#define WDXP1_LBL_EMISSION_RATE                  6010
#define WDXP1_LBL_CONTROL_5                      6011
#define WDXP1_LBL_MODEL_PATH_PREFIX              6012
#define WDXP1_LBL_PARTICLE_MODEL_FILE            6013
#define WDXP1_LBL_PARTICLE_SCALE                 6014
/// @}

