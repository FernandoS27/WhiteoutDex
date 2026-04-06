/**
 * @file resource.h
 * @brief Resource ID definitions for the Wc3Particles2 3ds Max plugin.
 *
 * Contains dialog, control, and string-table resource IDs shared between
 * the C++ source and the Wc3Particles2.rc resource script.
 */

#pragma once

/// @name Dialog IDs
/// @{
#define IDD_WC3PARTICLES2_PARTICLE       172  ///< (Legacy) Full particle emitter rollup panel.
#define IDD_ROLLOUT_EMITTER              173  ///< Emitter Options rollout.
#define IDD_ROLLOUT_TIMING               174  ///< Timing Options rollout.
#define IDD_ROLLOUT_SIZE                 175  ///< Size Options rollout.
#define IDD_ROLLOUT_TEXTURE              176  ///< Texture Options rollout.
#define IDD_ROLLOUT_PARTICLE             177  ///< Particle Options rollout.
#define IDD_ROLLOUT_OTHER                178  ///< Other Options rollout.
#define IDD_ROLLOUT_CONFIG               179  ///< Import/Export config rollout.
/// @}

/// @name Button and checkbox control IDs
/// @{
#define IDC_CHECKBOX_SQUIRT         1020  ///< Squirt mode checkbox.
#define IDC_BUTTON_BROWSE           1021  ///< File-browse button for the texture path.
#define IDC_BUTTON_IMPORT           1030  ///< Import particle config button.
#define IDC_BUTTON_EXPORT           1031  ///< Export particle config button.
#define IDC_CHECK_LOAD_DYNAMIC      1032  ///< Load animated keys checkbox.
#define IDC_BUTTON_IMPORT_TEX       1033  ///< Import particle texture button.
#define IDC_BUTTON_BROWSE_CASC      1034  ///< Browse MPQ/CASC button.
#define IDC_COMBO_BLEND             1035  ///< Blend mode combobox.
#define IDC_RADIO_TYPE_HEAD         1023  ///< Particle type: Head only.
#define IDC_RADIO_TYPE_TAIL         1024  ///< Particle type: Tail only.
#define IDC_RADIO_TYPE_BOTH         1025  ///< Particle type: Head and Tail.
#define IDC_RADIO_BLEND_0           1026  ///< Blend mode: Blend.
#define IDC_CHECKBOX_LINE_EMITTER   1028  ///< Line emitter checkbox.
/// @}

/// @name Edit/Spinner control IDs
/// Each parameter is exposed as a paired CustEdit and SpinnerControl.
/// @{
#define IDC_EDIT_COUNT              3036
#define IDC_SPIN_COUNT              3037
#define IDC_EDIT_LIFE               3042
#define IDC_SPIN_LIFE               3043
#define IDC_EDIT_WIDTH              3044
#define IDC_SPIN_WIDTH              3045
#define IDC_EDIT_HEIGHT             3046
#define IDC_SPIN_HEIGHT             3047
#define IDC_EDIT_SPEED              3048
#define IDC_SPIN_SPEED              3049
#define IDC_EDIT_VARIATION          3050
#define IDC_SPIN_VARIATION          3051
#define IDC_EDIT_ANGLE_Y            3052
#define IDC_EDIT_INITVEL            3053
#define IDC_SPIN_INITVEL            3054
#define IDC_STATIC_MAXRATE          3055
#define IDC_SPIN_ANGLE_Y            3056
#define IDC_EDIT_GRAVITY            3057
#define IDC_SPIN_GRAVITY            3058
#define IDC_EDIT_PRIORITY           3059
#define IDC_SPIN_PRIORITY           3060
#define IDC_EDIT_MIDTIME            3063
#define IDC_SPIN_MIDTIME            3064
#define IDC_EDIT_STARTSIZE          3067
#define IDC_SPIN_STARTSIZE          3068
#define IDC_EDIT_MIDSIZE            3069
#define IDC_SPIN_MIDSIZE            3070
#define IDC_EDIT_ENDSIZE            3071
#define IDC_SPIN_ENDSIZE            3072
#define IDC_EDIT_TAILLEN            3073
#define IDC_SPIN_TAILLEN            3074
#define IDC_CUSTOMEDIT_PATH         3075
#define IDC_EDIT_START_ALPHA        3076
#define IDC_SPIN_START_ALPHA        3077
#define IDC_EDIT_MID_ALPHA          3078
#define IDC_SPIN_MID_ALPHA          3079
#define IDC_EDIT_START_SCALE_X      3080
#define IDC_SPIN_START_SCALE_X      3081
#define IDC_EDIT_START_SCALE_Y      3082
#define IDC_SPIN_START_SCALE_Y      3083
#define IDC_EDIT_END_ALPHA          3084
#define IDC_SPIN_END_ALPHA          3085
#define IDC_EDIT_END_SCALE_X        3086
#define IDC_SPIN_END_SCALE_X        3087
#define IDC_EDIT_START_COLOR_R      3088
#define IDC_SPIN_START_COLOR_R      3089
#define IDC_EDIT_START_COLOR_G      3090
#define IDC_SPIN_START_COLOR_G      3091
#define IDC_EDIT_START_COLOR_B      3092
#define IDC_SPIN_START_COLOR_B      3093
#define IDC_EDIT_ROWS               3094
#define IDC_SPIN_ROWS               3095
#define IDC_EDIT_COLS               3096
#define IDC_SPIN_COLS               3097
#define IDC_EDIT_END_COLOR_R        3098
#define IDC_SPIN_END_COLOR_R        3099
#define IDC_EDIT_END_COLOR_G        3100
#define IDC_SPIN_END_COLOR_G        3101
#define IDC_EDIT_MID_COLOR_R        3102
#define IDC_SPIN_MID_COLOR_R        3103
#define IDC_EDIT_MID_COLOR_G        3104
#define IDC_SPIN_MID_COLOR_G        3105
#define IDC_EDIT_MID_COLOR_B        3106
#define IDC_SPIN_MID_COLOR_B        3107
#define IDC_EDIT_END_COLOR_B        3108
#define IDC_SPIN_END_COLOR_B        3109
/// @}

/// @name Colour swatch control IDs
/// @{
#define IDC_COLOR_START             4105  ///< Colour swatch: start colour.
#define IDC_COLOR_MID               4106  ///< Colour swatch: mid colour.
#define IDC_COLOR_END               4107  ///< Colour swatch: end colour.
/// @}

/// @name Blend mode radio button IDs
/// @{
#define IDC_RADIO_BLEND_1           4108  ///< Blend mode: Add.
#define IDC_RADIO_BLEND_2           4109  ///< Blend mode: Modulate.
#define IDC_RADIO_BLEND_3           4110  ///< Blend mode: Mod2X.
#define IDC_RADIO_BLEND_4           4111  ///< Blend mode: AlphaKey.
/// @}

/// @name Additional checkbox and combo control IDs
/// @{
#define IDC_CHECKBOX_SORT           4112  ///< Sort particles by depth checkbox.
#define IDC_CHECKBOX_UNSHADED       4113  ///< Unshaded rendering checkbox.
#define IDC_COMBO_TEXTURE_CAT       4114  ///< Replaceable texture category combo box.
#define IDC_CHECKBOX_UNFOGGED       4115  ///< Unfogged rendering checkbox.
#define IDC_CHECKBOX_MODELSPACE     4116  ///< Model-space simulation checkbox.
#define IDC_CHECKBOX_XYQUAD         4117  ///< XY-plane aligned quads checkbox.
/// @}

/// @name String resource IDs
/// @{
#define IDS_GENERAL             30028   ///< General rollup title.
#define IDS_HEIGHT              30038   ///< Label for emitter length/height parameter.
#define IDS_WIDTH               30039   ///< Label for emitter width parameter.
#define IDS_CLASS_NAME          30131   ///< Plugin class name shown in scene management.
#define IDS_OBJECT_NAME         30492   ///< Object name shown in the viewport label.
#define IDS_CATEGORY            30493   ///< Category string shown in the Create panel.
#define IDS_PARAM_COUNT         30494   ///< Viewport particle count parameter name.
#define IDS_PARAM_SPEED         30496   ///< Emission speed parameter name.
#define IDS_PARAM_VARIATION     30497   ///< Speed variation parameter name.
#define IDS_PARAM_LIFE          30499   ///< Particle lifetime parameter name.
#define IDS_PARAM_INITVEL       30500   ///< Emission rate (parts/sec) parameter name.
#define IDS_LIB_DESCRIPTION     31341   ///< String returned by LibDescription().
#define IDS_PARAM_ANGLE_Y       31342   ///< Cone angle parameter name.
#define IDS_PARAM_MIDTIME       31345   ///< Mid-life time parameter name.
#define IDS_PARAM_COLOR_START           31347   ///< Start colour parameter name.
#define IDS_PARAM_COLOR_MID             31348   ///< Mid colour parameter name.
#define IDS_PARAM_COLOR_END             31349   ///< End colour parameter name.
#define IDS_PARAM_ALPHA_START           31350   ///< Start alpha parameter name.
#define IDS_PARAM_ALPHA_MID             31351   ///< Mid alpha parameter name.
#define IDS_PARAM_ALPHA_END             31352   ///< End alpha parameter name.
#define IDS_PARAM_SCALE_START           31353   ///< Start scale parameter name.
#define IDS_PARAM_SCALE_MID             31354   ///< Mid scale parameter name.
#define IDS_PARAM_SCALE_END             31355   ///< End scale parameter name.
#define IDS_PARAM_HEAD_LIFE_START       31356   ///< Head lifespan UV start-frame parameter name.
#define IDS_PARAM_HEAD_LIFE_REPEAT      31357   ///< Head lifespan UV repeat-count parameter name.
#define IDS_PARAM_HEAD_LIFE_END         31358   ///< Head lifespan UV end-frame parameter name.
#define IDS_PARAM_HEAD_DECAY_START      31359   ///< Head decay UV start-frame parameter name.
#define IDS_PARAM_HEAD_DECAY_REPEAT     31360   ///< Head decay UV repeat-count parameter name.
#define IDS_PARAM_HEAD_DECAY_END        31361   ///< Head decay UV end-frame parameter name.
#define IDS_PARAM_TAIL_LEN              31362   ///< Tail length multiplier parameter name.
#define IDS_PARAM_TYPE                  31363   ///< Particle type (Head/Tail/Both) parameter name.
#define IDS_PARAM_TAIL_DECAY_START      31364   ///< Tail decay UV start-frame parameter name.
#define IDS_PARAM_TAIL_DECAY_END        31365   ///< Tail decay UV end-frame parameter name.
#define IDS_PARAM_TAIL_DECAY_REPEAT     31366   ///< Tail decay UV repeat-count parameter name.
#define IDS_PARAM_TAIL_LIFE_START       31367   ///< Tail lifespan UV start-frame parameter name.
#define IDS_PARAM_TAIL_LIFE_END         31368   ///< Tail lifespan UV end-frame parameter name.
#define IDS_PARAM_TAIL_LIFE_REPEAT      31369   ///< Tail lifespan UV repeat-count parameter name.
#define IDS_PARAM_GRAVITY               31370   ///< Gravity parameter name.
#define IDS_PARAM_SQUIRT                31371   ///< Squirt mode parameter name.
/// @}

/// @name Rollout title string IDs
/// @{
#define IDS_ROLLOUT_EMITTER         31380   ///< Emitter Options rollout title.
#define IDS_ROLLOUT_TIMING          31381   ///< Timing Options rollout title.
#define IDS_ROLLOUT_SIZE            31382   ///< Size Options rollout title.
#define IDS_ROLLOUT_TEXTURE         31383   ///< Texture Options rollout title.
#define IDS_ROLLOUT_PARTICLE        31384   ///< Particle Options rollout title.
#define IDS_ROLLOUT_OTHER           31385   ///< Other Options rollout title.
#define IDS_ROLLOUT_CONFIG          31386   ///< Import/Export rollout title.
/// @}

/// @name Controller-type combobox IDs
/// @{
#define IDC_CTRL_SPEED              1040  ///< Speed controller type combo.
#define IDC_CTRL_VARIATION          1041  ///< Variation controller type combo.
#define IDC_CTRL_ANGLE_Y            1042  ///< Cone Angle controller type combo.
#define IDC_CTRL_GRAVITY            1043  ///< Gravity controller type combo.
#define IDC_CTRL_INITVEL            1044  ///< Emission Rate controller type combo.
#define IDC_CTRL_WIDTH              1045  ///< Width controller type combo.
#define IDC_CTRL_HEIGHT             1046  ///< Height controller type combo.
/// @}
#define IDC_EDIT_PATH_PREFIX         1047  ///< Texture path prefix edit.
