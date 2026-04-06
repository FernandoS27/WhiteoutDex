plugin simpleManipulator Wc3_Light
name:"Wc3 Light"
classID:#(0x456E2573, 0x2A456757)
category:"Warcraft 3 Systems"
( 
	local Icon_Path = ((systemTools.getEnvVariable "APPDATA") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex\\whiteoutdex_icons\\")
	local MessageboxText2 = "Conversion Complete!"
	local MessageboxText3 = "No animation data for current attribute!"
	local parentBtnLabel = "Parent: "
	local TempSelction = "None"
	parameters main rollout:General
    (
        adsorption  type:#string	ui:Temp_adsorption
    )
	parameters main rollout:params1
	(
		LightType    ui:LightType   type:#integer  default:1              animatable:false
		DecayStart   ui:DS_Spn       type:#float    default:80             animatable:true
		DecayEnd     ui:DE_Spn       type:#float    default:200            animatable:true
		AmbColor     ui:Dif_Color    type:#color    default:[255,230,142]  animatable:true
		AmbValue     ui:Dif_Value    type:#float    default:1.3            animatable:true
		ShadowColor  ui:Amb_Color    type:#color    default:[122,172,255]  animatable:true
		ShadowValue  ui:Amb_Value    type:#float    default:0.5            animatable:true
	)
	rollout General "General Functions"
	(
		PickButton btn_adsorption "Select Object (Adsorb)" pos:[12,7] width:138 height:24
		edittext Temp_adsorption "" pos:[9999,0] width:0 height:0
		
		on btn_adsorption picked obj do
		(
			try
			(
				selection[1].pos = obj.pos
				selection[1].parent = obj
				Temp_adsorption.text = obj.name
				btn_adsorption.text = parentBtnLabel + obj.name
			)catch()
		)
		on General open do
		(
			if ::L != undefined then
				General.title = ::L.t "light_general_functions_title"
		)
	)
	rollout params1 "Basic Parameters"
	(
		label        l1          "- Light Type -"    align:#center
		dropdownlist LightType items:#("Omnidirectional", "Directional", "Ambient") width:138 pos:[12,25]
		-----------------------------------------------------------------------------------------------------
		label        l4          "Decay Start .."        pos:[12,52] align:#left     width:70
		spinner      DS_Spn                              pos:[79,51]                height:16  fieldwidth:59  range:[0,9999,80]
		label        l5          "Control:"           pos:[12,79] align:#left
		checkbutton  DS_None_BTN      images:#(Icon_Path + "ndx_none.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Instant Controller" pos:[58,76] width:20 height:20
		checkbutton  DS_Linear_BTN    images:#(Icon_Path + "ndx_linear.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Linear Controller"  pos:[82,76] width:20 height:20
		checkbutton  DS_Bezier_BTN    images:#(Icon_Path + "ndx_bezier.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Bezier Controller"  pos:[106,76] width:20 height:20
		checkbutton  DS_Hermite_BTN   images:#(Icon_Path + "ndx_hermite.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Hermite Controller" pos:[130,76] width:20 height:20
		-----------------------------------------------------------------------------------------------------
		label        l6          "Decay End ..."         pos:[12,106] align:#left     width:70
		spinner      DE_Spn                              pos:[79,105]                height:16  fieldwidth:59  range:[0,9999,200]
		label        l7          "Control:"           pos:[12,133] align:#left
		checkbutton  DE_None_BTN      images:#(Icon_Path + "ndx_none.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Instant Controller" pos:[58,130] width:20 height:20
		checkbutton  DE_Linear_BTN    images:#(Icon_Path + "ndx_linear.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Linear Controller"  pos:[82,130] width:20 height:20
		checkbutton  DE_Bezier_BTN    images:#(Icon_Path + "ndx_bezier.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Bezier Controller"  pos:[106,130] width:20 height:20
		checkbutton  DE_Hermite_BTN   images:#(Icon_Path + "ndx_hermite.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Hermite Controller" pos:[130,130] width:20 height:20
		-----------------------------------------------------------------------------------------------------
		group "Ambient Light"
		(
			colorPicker Dif_Color    height:32 width:142   pos:[8,173]  color:(color 255 230 142)
			label        l8          "Control:"          pos:[12,217] align:#left
			checkbutton  Dif_Color_None_BTN   	 images:#(Icon_Path + "ndx_none.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Instant Controller"  pos:[58,214] width:20 height:20
			checkbutton  Dif_Color_Linear_BTN    images:#(Icon_Path + "ndx_linear.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Linear Controller"   pos:[82,214] width:20 height:20
			checkbutton  Dif_Color_Bezier_BTN    images:#(Icon_Path + "ndx_bezier.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Bezier Controller"   pos:[106,214] width:20 height:20
			checkbutton  Dif_Color_Hermite_BTN   images:#(Icon_Path + "ndx_hermite.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Hermite Controller"  pos:[130,214] width:20 height:20
			label        l9          "Strength ....."       pos:[12,244] align:#left     width:70
			spinner      Dif_Value                          pos:[79,243] height:16      fieldwidth:59     range:[0,10000,0]
			label        l10         "Control:"          pos:[12,271] align:#left
			checkbutton  DIF_Strength_None_BTN     images:#(Icon_Path + "ndx_none.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Instant Controller" pos:[58,268] width:20 height:20
			checkbutton  DIF_Strenght_Linear_BTN   images:#(Icon_Path + "ndx_linear.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Linear Controller"  pos:[82,268] width:20 height:20
			checkbutton  DIF_Strength_Bezier_BTN   images:#(Icon_Path + "ndx_bezier.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Bezier Controller"  pos:[106,268] width:20 height:20
			checkbutton  DIF_Strength_Hermite_BTN  images:#(Icon_Path + "ndx_hermite.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Hermite Controller" pos:[130,268] width:20 height:20
		)
		-----------------------------------------------------------------------------------------------------
		group "Shadow Light"
		(
			colorPicker Amb_Color    height:32 width:142   pos:[8,317]  color:(color 122 172 255)
			label        l11         "Control:"          pos:[12,361] align:#left
			checkbutton  DIF_Amb_Color_None_BTN   	 images:#(Icon_Path + "ndx_none.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Instant Controller"  pos:[58,358] width:20 height:20
			checkbutton  DIF_Amb_Color_Linear_BTN    images:#(Icon_Path + "ndx_linear.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Linear Controller"   pos:[82,358] width:20 height:20
			checkbutton  DIF_Amb_Color_Bezier_BTN    images:#(Icon_Path + "ndx_bezier.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Bezier Controller"   pos:[106,358] width:20 height:20
			checkbutton  DIF_Amb_Color_Hermite_BTN   images:#(Icon_Path + "ndx_hermite.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Hermite Controller"  pos:[130,358] width:20 height:20
			label        l12         "Strength ....."       pos:[12,388] align:#left     width:70
			spinner      Amb_Value                          pos:[79,387] height:16      fieldwidth:59     range:[0,10000,0]
			label        l13         "Control:"          pos:[12,415] align:#left
			checkbutton  Amb_Strength_None_BTN     images:#(Icon_Path + "ndx_none.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Instant Controller" pos:[58,412] width:20 height:20
			checkbutton  Amb_Strength_Linear_BTN   images:#(Icon_Path + "ndx_linear.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Linear Controller"  pos:[82,412] width:20 height:20
			checkbutton  Amb_Strength_Bezier_BTN   images:#(Icon_Path + "ndx_bezier.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Bezier Controller"  pos:[106,412] width:20 height:20
			checkbutton  Amb_Strength_Hermite_BTN  images:#(Icon_Path + "ndx_hermite.bmp", undefined, 2, 1,2,1,2, True) tooltip:"Hermite Controller" pos:[130,412] width:20 height:20
		)
		
		-- Button group arrays (initialized in on open)
		local dsBtns, deBtns, difColorBtns, difValBtns, ambColorBtns, ambValBtns

		-- ==================== Generic Helper Functions ====================

		-- Syncs checkbuttons for a float property controller
		fn syncFloatBtnGroup btnArr ctrl =
		(
			for b in btnArr do b.checked = false
			if ctrl != undefined do
			(
				if classof ctrl == Bezier_Float then
				(
					if ctrl.keys.count > 0 and ctrl.keys[1].inTangentType == #step then
						btnArr[1].checked = true
					else
						btnArr[3].checked = true
				)
				else if classof ctrl == Linear_Float then
					btnArr[2].checked = true
				else if classof ctrl == TCB_Float then
					btnArr[4].checked = true
			)
		)

		-- Syncs checkbuttons for a color property controller.
		-- Color uses bezier_color for step/linear/bezier (distinguished by tangent), TCB_Point3 for hermite.
		fn syncColorBtnGroup btnArr ctrl =
		(
			for b in btnArr do b.checked = false
			if ctrl != undefined do
			(
				if classof ctrl == bezier_color then
				(
					if ctrl.keys.count > 0 then
					(
						local tt = ctrl.keys[1].inTangentType
						if tt == #step then btnArr[1].checked = true
						else if tt == #linear then btnArr[2].checked = true
						else if tt == #auto or tt == #smooth or tt == #flat then btnArr[3].checked = true
					)
					else
						btnArr[3].checked = true  -- No keys yet: default Bezier
				)
				else if classof ctrl == TCB_Point3 then
					btnArr[4].checked = true
			)
		)

		-- Applies tangent types to all keys of a controller
		fn applyTangents ctrl tangentType =
		(
			for i = 1 to ctrl.keys.count do
			(
				ctrl.keys[i].inTangentType = tangentType
				ctrl.keys[i].outTangentType = tangentType
			)
		)

		-- ==================== Refresh Functions ====================
		fn refreshDS       = (try(syncFloatBtnGroup dsBtns       (if selection.count > 0 and selection[1].DecayStart.isAnimated  then selection[1].DecayStart.controller  else undefined))catch())
		fn refreshDE       = (try(syncFloatBtnGroup deBtns       (if selection.count > 0 and selection[1].DecayEnd.isAnimated    then selection[1].DecayEnd.controller    else undefined))catch())
		fn refreshDifColor = (try(syncColorBtnGroup difColorBtns (if selection.count > 0 and selection[1].AmbColor.isAnimated    then selection[1].AmbColor.controller    else undefined))catch())
		fn refreshDifVal   = (try(syncFloatBtnGroup difValBtns   (if selection.count > 0 and selection[1].AmbValue.isAnimated    then selection[1].AmbValue.controller    else undefined))catch())
		fn refreshAmbColor = (try(syncColorBtnGroup ambColorBtns (if selection.count > 0 and selection[1].ShadowColor.isAnimated then selection[1].ShadowColor.controller else undefined))catch())
		fn refreshAmbVal   = (try(syncFloatBtnGroup ambValBtns   (if selection.count > 0 and selection[1].ShadowValue.isAnimated then selection[1].ShadowValue.controller else undefined))catch())

		-- ==================== Decay Start Controller Events ====================
		on DS_None_BTN changed state do
		(	try (if selection[1].DecayStart.isAnimated then
			(	if classof selection[1].DecayStart.controller != Bezier_float do selection[1].DecayStart.controller = Bezier_float()
				applyTangents selection[1].DecayStart.controller #step; refreshDS(); messagebox MessageboxText2
			) else (refreshDS(); messagebox MessageboxText3)) catch()
		)
		on DS_Linear_BTN changed state do
		(	try (if selection[1].DecayStart.isAnimated then
			(	selection[1].DecayStart.controller = Linear_float(); refreshDS(); messagebox MessageboxText2
			) else (refreshDS(); messagebox MessageboxText3)) catch()
		)
		on DS_Bezier_BTN changed state do
		(	try (if selection[1].DecayStart.isAnimated then
			(	if classof selection[1].DecayStart.controller != Bezier_float do selection[1].DecayStart.controller = Bezier_float()
				applyTangents selection[1].DecayStart.controller #smooth; refreshDS(); messagebox MessageboxText2
			) else (refreshDS(); messagebox MessageboxText3)) catch()
		)
		on DS_Hermite_BTN changed state do
		(	try (if selection[1].DecayStart.isAnimated then
			(	selection[1].DecayStart.controller = TCB_float(); refreshDS(); messagebox MessageboxText2
			) else (refreshDS(); messagebox MessageboxText3)) catch()
		)
		-- ==================== Decay End Controller Events ====================
		on DE_None_BTN changed state do
		(	try (if selection[1].DecayEnd.isAnimated then
			(	if classof selection[1].DecayEnd.controller != Bezier_float do selection[1].DecayEnd.controller = Bezier_float()
				applyTangents selection[1].DecayEnd.controller #step; refreshDE(); messagebox MessageboxText2
			) else (refreshDE(); messagebox MessageboxText3)) catch()
		)
		on DE_Linear_BTN changed state do
		(	try (if selection[1].DecayEnd.isAnimated then
			(	selection[1].DecayEnd.controller = Linear_float(); refreshDE(); messagebox MessageboxText2
			) else (refreshDE(); messagebox MessageboxText3)) catch()
		)
		on DE_Bezier_BTN changed state do
		(	try (if selection[1].DecayEnd.isAnimated then
			(	if classof selection[1].DecayEnd.controller != Bezier_float do selection[1].DecayEnd.controller = Bezier_float()
				applyTangents selection[1].DecayEnd.controller #smooth; refreshDE(); messagebox MessageboxText2
			) else (refreshDE(); messagebox MessageboxText3)) catch()
		)
		on DE_Hermite_BTN changed state do
		(	try (if selection[1].DecayEnd.isAnimated then
			(	selection[1].DecayEnd.controller = TCB_float(); refreshDE(); messagebox MessageboxText2
			) else (refreshDE(); messagebox MessageboxText3)) catch()
		)
		-- ==================== Ambient Color Controller Events ====================
		on Dif_Color_None_BTN changed state do
		(	try (if selection[1].AmbColor.isAnimated then
			(	if classof selection[1].AmbColor.controller != bezier_color do selection[1].AmbColor.controller = bezier_color()
				applyTangents selection[1].AmbColor.controller #step; refreshDifColor(); messagebox MessageboxText2
			) else (refreshDifColor(); messagebox MessageboxText3)) catch()
		)
		on Dif_Color_Linear_BTN changed state do
		(	try (if selection[1].AmbColor.isAnimated then
			(	if classof selection[1].AmbColor.controller != bezier_color do selection[1].AmbColor.controller = bezier_color()
				applyTangents selection[1].AmbColor.controller #linear; refreshDifColor(); messagebox MessageboxText2
			) else (refreshDifColor(); messagebox MessageboxText3)) catch()
		)
		on Dif_Color_Bezier_BTN changed state do
		(	try (if selection[1].AmbColor.isAnimated then
			(	if classof selection[1].AmbColor.controller != bezier_color do selection[1].AmbColor.controller = bezier_color()
				applyTangents selection[1].AmbColor.controller #smooth; refreshDifColor(); messagebox MessageboxText2
			) else (refreshDifColor(); messagebox MessageboxText3)) catch()
		)
		on Dif_Color_Hermite_BTN changed state do
		(	try (if selection[1].AmbColor.isAnimated then
			(	selection[1].AmbColor.controller = TCB_point3(); refreshDifColor(); messagebox MessageboxText2
			) else (refreshDifColor(); messagebox MessageboxText3)) catch()
		)
		-- ==================== Ambient Strength Controller Events ====================
		on DIF_Strength_None_BTN changed state do
		(	try (if selection[1].AmbValue.isAnimated then
			(	if classof selection[1].AmbValue.controller != Bezier_float do selection[1].AmbValue.controller = Bezier_float()
				applyTangents selection[1].AmbValue.controller #step; refreshDifVal(); messagebox MessageboxText2
			) else (refreshDifVal(); messagebox MessageboxText3)) catch()
		)
		on DIF_Strenght_Linear_BTN changed state do
		(	try (if selection[1].AmbValue.isAnimated then
			(	selection[1].AmbValue.controller = Linear_float(); refreshDifVal(); messagebox MessageboxText2
			) else (refreshDifVal(); messagebox MessageboxText3)) catch()
		)
		on DIF_Strength_Bezier_BTN changed state do
		(	try (if selection[1].AmbValue.isAnimated then
			(	if classof selection[1].AmbValue.controller != Bezier_float do selection[1].AmbValue.controller = Bezier_float()
				applyTangents selection[1].AmbValue.controller #smooth; refreshDifVal(); messagebox MessageboxText2
			) else (refreshDifVal(); messagebox MessageboxText3)) catch()
		)
		on DIF_Strength_Hermite_BTN changed state do
		(	try (if selection[1].AmbValue.isAnimated then
			(	selection[1].AmbValue.controller = TCB_float(); refreshDifVal(); messagebox MessageboxText2
			) else (refreshDifVal(); messagebox MessageboxText3)) catch()
		)
		-- ==================== Shadow Color Controller Events ====================
		on DIF_Amb_Color_None_BTN changed state do
		(	try (if selection[1].ShadowColor.isAnimated then
			(	if classof selection[1].ShadowColor.controller != bezier_color do selection[1].ShadowColor.controller = bezier_color()
				applyTangents selection[1].ShadowColor.controller #step; refreshAmbColor(); messagebox MessageboxText2
			) else (refreshAmbColor(); messagebox MessageboxText3)) catch()
		)
		on DIF_Amb_Color_Linear_BTN changed state do
		(	try (if selection[1].ShadowColor.isAnimated then
			(	if classof selection[1].ShadowColor.controller != bezier_color do selection[1].ShadowColor.controller = bezier_color()
				applyTangents selection[1].ShadowColor.controller #linear; refreshAmbColor(); messagebox MessageboxText2
			) else (refreshAmbColor(); messagebox MessageboxText3)) catch()
		)
		on DIF_Amb_Color_Bezier_BTN changed state do
		(	try (if selection[1].ShadowColor.isAnimated then
			(	if classof selection[1].ShadowColor.controller != bezier_color do selection[1].ShadowColor.controller = bezier_color()
				applyTangents selection[1].ShadowColor.controller #smooth; refreshAmbColor(); messagebox MessageboxText2
			) else (refreshAmbColor(); messagebox MessageboxText3)) catch()
		)
		on DIF_Amb_Color_Hermite_BTN changed state do
		(	try (if selection[1].ShadowColor.isAnimated then
			(	selection[1].ShadowColor.controller = TCB_point3(); refreshAmbColor(); messagebox MessageboxText2
			) else (refreshAmbColor(); messagebox MessageboxText3)) catch()
		)
		-- ==================== Shadow Strength Controller Events ====================
		on Amb_Strength_None_BTN changed state do
		(	try (if selection[1].ShadowValue.isAnimated then
			(	if classof selection[1].ShadowValue.controller != Bezier_float do selection[1].ShadowValue.controller = Bezier_float()
				applyTangents selection[1].ShadowValue.controller #step; refreshAmbVal(); messagebox MessageboxText2
			) else (refreshAmbVal(); messagebox MessageboxText3)) catch()
		)
		on Amb_Strength_Linear_BTN changed state do
		(	try (if selection[1].ShadowValue.isAnimated then
			(	selection[1].ShadowValue.controller = Linear_float(); refreshAmbVal(); messagebox MessageboxText2
			) else (refreshAmbVal(); messagebox MessageboxText3)) catch()
		)
		on Amb_Strength_Bezier_BTN changed state do
		(	try (if selection[1].ShadowValue.isAnimated then
			(	if classof selection[1].ShadowValue.controller != Bezier_float do selection[1].ShadowValue.controller = Bezier_float()
				applyTangents selection[1].ShadowValue.controller #smooth; refreshAmbVal(); messagebox MessageboxText2
			) else (refreshAmbVal(); messagebox MessageboxText3)) catch()
		)
		on Amb_Strength_Hermite_BTN changed state do
		(	try (if selection[1].ShadowValue.isAnimated then
			(	selection[1].ShadowValue.controller = TCB_float(); refreshAmbVal(); messagebox MessageboxText2
			) else (refreshAmbVal(); messagebox MessageboxText3)) catch()
		)

		-- ==================== Spinner/ColorPicker Events ====================
		on DS_Spn changed val   do refreshDS()
		on DE_Spn changed val   do refreshDE()
		on Dif_Value changed val do refreshDifVal()
		on Amb_Value changed val do refreshAmbVal()

		-- ColorPicker events use a timer because the event fires BEFORE the keyframe is set
		on Dif_Color changed val do
		(
			local updateTimer = dotNetObject "System.Windows.Forms.Timer"
			updateTimer.Interval = 100
			dotNet.addEventHandler updateTimer "tick" (fn timerTick sender args = (refreshDifColor(); sender.Stop(); sender.Dispose()))
			updateTimer.Start()
		)
		on Amb_Color changed val do
		(
			local updateTimer = dotNetObject "System.Windows.Forms.Timer"
			updateTimer.Interval = 100
			dotNet.addEventHandler updateTimer "tick" (fn timerTick sender args = (refreshAmbColor(); sender.Stop(); sender.Dispose()))
			updateTimer.Start()
		)

		-- ==================== Rollout Open ====================
		on params1 open do
		(
			dsBtns       = #(DS_None_BTN, DS_Linear_BTN, DS_Bezier_BTN, DS_Hermite_BTN)
			deBtns       = #(DE_None_BTN, DE_Linear_BTN, DE_Bezier_BTN, DE_Hermite_BTN)
			difColorBtns = #(Dif_Color_None_BTN, Dif_Color_Linear_BTN, Dif_Color_Bezier_BTN, Dif_Color_Hermite_BTN)
			difValBtns   = #(DIF_Strength_None_BTN, DIF_Strenght_Linear_BTN, DIF_Strength_Bezier_BTN, DIF_Strength_Hermite_BTN)
			ambColorBtns = #(DIF_Amb_Color_None_BTN, DIF_Amb_Color_Linear_BTN, DIF_Amb_Color_Bezier_BTN, DIF_Amb_Color_Hermite_BTN)
			ambValBtns   = #(Amb_Strength_None_BTN, Amb_Strength_Linear_BTN, Amb_Strength_Bezier_BTN, Amb_Strength_Hermite_BTN)

			refreshDS(); refreshDE(); refreshDifColor(); refreshDifVal(); refreshAmbColor(); refreshAmbVal()
			
			if ::L != undefined then
			(
				params1.title = ::L.t "light_basic_parameters_title"
				l1.text = ::L.t "light__light_type_lbl"
				l4.text = ::L.t "light_decay_start_lbl"
				l5.text = ::L.t "light_control_lbl"
				DS_None_BTN.tooltip = ::L.t "light_instant_controller_tip"
				DS_Linear_BTN.tooltip = ::L.t "light_linear_controller_tip"
				DS_Bezier_BTN.tooltip = ::L.t "light_bezier_controller_tip"
				DS_Hermite_BTN.tooltip = ::L.t "light_hermite_controller_tip"
				l6.text = ::L.t "light_decay_end_lbl"
				l7.text = ::L.t "light_control_lbl_2"
				DE_None_BTN.tooltip = ::L.t "light_instant_controller_tip_2"
				DE_Linear_BTN.tooltip = ::L.t "light_linear_controller_tip_2"
				DE_Bezier_BTN.tooltip = ::L.t "light_bezier_controller_tip_2"
				DE_Hermite_BTN.tooltip = ::L.t "light_hermite_controller_tip_2"
				l8.text = ::L.t "light_control_lbl_3"
				Dif_Color_None_BTN.tooltip = ::L.t "light_instant_controller_tip_3"
				Dif_Color_Linear_BTN.tooltip = ::L.t "light_linear_controller_tip_3"
				Dif_Color_Bezier_BTN.tooltip = ::L.t "light_bezier_controller_tip_3"
				Dif_Color_Hermite_BTN.tooltip = ::L.t "light_hermite_controller_tip_3"
				l9.text = ::L.t "light_strength_lbl"
				l10.text = ::L.t "light_control_lbl_4"
				DIF_Strength_None_BTN.tooltip = ::L.t "light_instant_controller_tip_4"
				DIF_Strenght_Linear_BTN.tooltip = ::L.t "light_linear_controller_tip_4"
				DIF_Strength_Bezier_BTN.tooltip = ::L.t "light_bezier_controller_tip_4"
				DIF_Strength_Hermite_BTN.tooltip = ::L.t "light_hermite_controller_tip_4"
				l11.text = ::L.t "light_control_lbl_5"
				DIF_Amb_Color_None_BTN.tooltip = ::L.t "light_instant_controller_tip_5"
				DIF_Amb_Color_Linear_BTN.tooltip = ::L.t "light_linear_controller_tip_5"
				DIF_Amb_Color_Bezier_BTN.tooltip = ::L.t "light_bezier_controller_tip_5"
				DIF_Amb_Color_Hermite_BTN.tooltip = ::L.t "light_hermite_controller_tip_5"
				l12.text = ::L.t "light_strength_lbl_2"
				l13.text = ::L.t "light_control_lbl_6"
				Amb_Strength_None_BTN.tooltip = ::L.t "light_instant_controller_tip_6"
				Amb_Strength_Linear_BTN.tooltip = ::L.t "light_linear_controller_tip_6"
				Amb_Strength_Bezier_BTN.tooltip = ::L.t "light_bezier_controller_tip_6"
				Amb_Strength_Hermite_BTN.tooltip = ::L.t "light_hermite_controller_tip_6"
			)
		)
		------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	)

	tool create
	(
		on mousePoint click do
		(
			case click of
			(
				1: nodeTM.translation = gridPoint
				2: #stop
			)
		)
	)
	
	fn BuildGizmo points Drawcolor =
	(
		local unselectedColor = Drawcolor
		local selectedColor   = Drawcolor
		local giz			  = manip.makeGizmoShape()
		giz.startNewLine() 
		for pnt in points do giz.addpoint pnt
		this.addGizmoShape giz 0 unselectedColor selectedColor
	)

    on updateGizmos do
    (
        this.clearGizmos()
		local Drawcolor = [255,143,68]
		Drawcolor = [Drawcolor.x/255,Drawcolor.y/255,Drawcolor.z/255]
		BuildGizmo #([-0.25,0,13.218],[0.25,0,13.218],[0.25,0,9.218],[-0.25,0,9.218],[-0.25,0,13.218]) Drawcolor
		BuildGizmo #([-9.135,0,4.214],[-5.135,0,4.214],[-5.135,0,3.714],[-9.135,0,3.714],[-9.135,0,4.214]) Drawcolor
		BuildGizmo #([9.135,0,4.214],[5.135,0,4.214],[5.135,0,3.714],[9.135,0,3.714],[9.135,0,4.214]) Drawcolor
		BuildGizmo #([-5.043,0,6.232],[-8.669,0,7.922],[-8.457,0,8.357],[-4.832,0,6.685],[-5.043,0,6.232]) Drawcolor
		BuildGizmo #([5.043,0,6.232],[8.669,0,7.922],[8.457,0,8.357],[4.832,0,6.685],[5.043,0,6.232]) Drawcolor
		BuildGizmo #([-3.248,0,8.317],[-5.248,0,11.781],[-4.815,0,12.031],[-2.815,0,8.567],[-3.248,0,8.317]) Drawcolor
		BuildGizmo #([3.248,0,8.317],[5.248,0,11.781],[4.815,0,12.031],[2.815,0,8.567],[3.248,0,8.317]) Drawcolor
		BuildGizmo #([0,-5,2.5],[0,0,7.5],[-5,0,2.5]) Drawcolor
		BuildGizmo #([5,0,2.5],[0,0,7.5],[0,5,2.5]) Drawcolor
		BuildGizmo #([0,-5,2.5],[-5,0,2.5],[0,5,2.5],[5,0,2.5],[0,-5,2.5]) Drawcolor
		BuildGizmo #([0,-5,2.5],[0,-2.5,0],[-2.5,0,0]) Drawcolor
		BuildGizmo #([-5,0,2.5],[-2.5,0,0],[0,2.5,0]) Drawcolor
		BuildGizmo #([0,5,2.5],[0,2.5,0],[2.5,0,0]) Drawcolor
		BuildGizmo #([5,0,2.5],[2.5,0,0],[0,-2.5,0]) Drawcolor
		BuildGizmo #([0,-2.5,0],[0,-2.5,-5],[-2.5,0,-5]) Drawcolor
		BuildGizmo #([-2.5,0,0],[-2.5,0,-5],[0,2.5,-5]) Drawcolor
		BuildGizmo #([0,2.5,0],[0,2.5,-5],[2.5,0,-5]) Drawcolor
		BuildGizmo #([2.5,0,0],[2.5,0,-5],[0,-2.5,-5]) Drawcolor
	)
)
