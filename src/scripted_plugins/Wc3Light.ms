plugin simpleManipulator Wdx_Wc3Light
name:"Wc3 Light"
classID:#(0x7A1B2C04, 0x3D4E5F04)
category:"Warcraft III Plugins"
( 
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
			if ::WdxL != undefined then
				General.title = ::WdxL.t "light_general_functions_title"
		)
	)
	rollout params1 "Basic Parameters"
	(
		label        l1          "- Light Type -"    align:#center
		dropdownlist LightType items:#("Omnidirectional", "Directional", "Ambient") width:138 pos:[12,25]
		-----------------------------------------------------------------------------------------------------
		label        l4          "Decay Start .."        pos:[12,52] align:#left     width:70
		spinner      DS_Spn                              pos:[79,51]                height:16  fieldwidth:59  range:[0,9999,80]
		label        l5          "Control:"              pos:[12,76] align:#left
		dropdownlist DS_DDL      items:#("None", "Linear", "Bezier", "Hermite") pos:[58,73] width:92 tooltip:"Interpolation type for Decay Start"
		-----------------------------------------------------------------------------------------------------
		label        l6          "Decay End ..."         pos:[12,100] align:#left     width:70
		spinner      DE_Spn                              pos:[79,99]                height:16  fieldwidth:59  range:[0,9999,200]
		label        l7          "Control:"              pos:[12,124] align:#left
		dropdownlist DE_DDL      items:#("None", "Linear", "Bezier", "Hermite") pos:[58,121] width:92 tooltip:"Interpolation type for Decay End"
		-----------------------------------------------------------------------------------------------------
		group "Shadow Light"
		(
			colorPicker Amb_Color    height:32 width:142   pos:[8,167]  color:(color 122 172 255)
			label        l11         "Control:"          pos:[12,207] align:#left
			dropdownlist AmbColor_DDL items:#("None", "Linear", "Bezier", "Hermite") pos:[58,204] width:92 tooltip:"Interpolation type for Shadow Color"
			label        l12         "Strength ....."       pos:[12,232] align:#left     width:70
			spinner      Amb_Value                          pos:[79,231] height:16      fieldwidth:59     range:[0,10000,0]
			label        l13         "Control:"          pos:[12,256] align:#left
			dropdownlist AmbVal_DDL  items:#("None", "Linear", "Bezier", "Hermite") pos:[58,253] width:92 tooltip:"Interpolation type for Shadow Strength"
		)
		-----------------------------------------------------------------------------------------------------
		group "Ambient Light"
		(
			colorPicker Dif_Color    height:32 width:142   pos:[8,299]  color:(color 255 230 142)
			label        l8          "Control:"          pos:[12,339] align:#left
			dropdownlist DifColor_DDL items:#("None", "Linear", "Bezier", "Hermite") pos:[58,336] width:92 tooltip:"Interpolation type for Ambient Color"
			label        l9          "Strength ....."       pos:[12,364] align:#left     width:70
			spinner      Dif_Value                          pos:[79,363] height:16      fieldwidth:59     range:[0,10000,0]
			label        l10         "Control:"          pos:[12,388] align:#left
			dropdownlist DifVal_DDL  items:#("None", "Linear", "Bezier", "Hermite") pos:[58,385] width:92 tooltip:"Interpolation type for Ambient Strength"
		)

		-- ==================== Generic Helper Functions ====================

		-- Returns dropdown index (1-4) for a float property controller
		fn getFloatInterpIndex ctrl =
		(
			if ctrl == undefined then return 1
			if classof ctrl == Bezier_Float then
			(
				if ctrl.keys.count > 0 and ctrl.keys[1].inTangentType == #step then 1
				else 3
			)
			else if classof ctrl == Linear_Float then 2
			else if classof ctrl == TCB_Float then 4
			else 1
		)

		-- Returns dropdown index (1-4) for a color property controller
		fn getColorInterpIndex ctrl =
		(
			if ctrl == undefined then return 1
			if classof ctrl == bezier_color then
			(
				if ctrl.keys.count > 0 then
				(
					local tt = ctrl.keys[1].inTangentType
					if tt == #step then 1
					else if tt == #linear then 2
					else 3
				)
				else 3
			)
			else if classof ctrl == TCB_Point3 then 4
			else 1
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

		-- Applies a float interpolation choice (1=None, 2=Linear, 3=Bezier, 4=Hermite)
		fn applyFloatInterp prop idx =
		(
			try (
				if not prop.isAnimated then (messagebox MessageboxText3; return())
				case idx of
				(
					1: (if classof prop.controller != Bezier_float do prop.controller = Bezier_float()
						applyTangents prop.controller #step)
					2: (prop.controller = Linear_float())
					3: (if classof prop.controller != Bezier_float do prop.controller = Bezier_float()
						applyTangents prop.controller #smooth)
					4: (prop.controller = TCB_float())
				)
				messagebox MessageboxText2
			) catch()
		)

		-- Applies a color interpolation choice
		fn applyColorInterp prop idx =
		(
			try (
				if not prop.isAnimated then (messagebox MessageboxText3; return())
				case idx of
				(
					1: (if classof prop.controller != bezier_color do prop.controller = bezier_color()
						applyTangents prop.controller #step)
					2: (if classof prop.controller != bezier_color do prop.controller = bezier_color()
						applyTangents prop.controller #linear)
					3: (if classof prop.controller != bezier_color do prop.controller = bezier_color()
						applyTangents prop.controller #smooth)
					4: (prop.controller = TCB_point3())
				)
				messagebox MessageboxText2
			) catch()
		)

		-- ==================== Refresh Functions ====================
		fn refreshDS       = (try(DS_DDL.selection       = getFloatInterpIndex (if selection.count > 0 and selection[1].DecayStart.isAnimated  then selection[1].DecayStart.controller  else undefined))catch())
		fn refreshDE       = (try(DE_DDL.selection       = getFloatInterpIndex (if selection.count > 0 and selection[1].DecayEnd.isAnimated    then selection[1].DecayEnd.controller    else undefined))catch())
		fn refreshDifColor = (try(DifColor_DDL.selection = getColorInterpIndex (if selection.count > 0 and selection[1].AmbColor.isAnimated    then selection[1].AmbColor.controller    else undefined))catch())
		fn refreshDifVal   = (try(DifVal_DDL.selection   = getFloatInterpIndex (if selection.count > 0 and selection[1].AmbValue.isAnimated    then selection[1].AmbValue.controller    else undefined))catch())
		fn refreshAmbColor = (try(AmbColor_DDL.selection = getColorInterpIndex (if selection.count > 0 and selection[1].ShadowColor.isAnimated then selection[1].ShadowColor.controller else undefined))catch())
		fn refreshAmbVal   = (try(AmbVal_DDL.selection   = getFloatInterpIndex (if selection.count > 0 and selection[1].ShadowValue.isAnimated then selection[1].ShadowValue.controller else undefined))catch())

		-- ==================== Dropdown Events ====================
		on DS_DDL selected idx do
			(try (applyFloatInterp selection[1].DecayStart idx; refreshDS()) catch())
		on DE_DDL selected idx do
			(try (applyFloatInterp selection[1].DecayEnd idx; refreshDE()) catch())
		on DifColor_DDL selected idx do
			(try (applyColorInterp selection[1].AmbColor idx; refreshDifColor()) catch())
		on DifVal_DDL selected idx do
			(try (applyFloatInterp selection[1].AmbValue idx; refreshDifVal()) catch())
		on AmbColor_DDL selected idx do
			(try (applyColorInterp selection[1].ShadowColor idx; refreshAmbColor()) catch())
		on AmbVal_DDL selected idx do
			(try (applyFloatInterp selection[1].ShadowValue idx; refreshAmbVal()) catch())

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
			refreshDS(); refreshDE(); refreshDifColor(); refreshDifVal(); refreshAmbColor(); refreshAmbVal()
			
			if ::WdxL != undefined then
			(
				params1.title = ::WdxL.t "light_basic_parameters_title"
				l1.text = ::WdxL.t "light__light_type_lbl"
				l4.text = ::WdxL.t "light_decay_start_lbl"
				l5.text = ::WdxL.t "light_control_lbl"
				l6.text = ::WdxL.t "light_decay_end_lbl"
				l7.text = ::WdxL.t "light_control_lbl_2"
				l8.text = ::WdxL.t "light_control_lbl_3"
				l9.text = ::WdxL.t "light_strength_lbl"
				l10.text = ::WdxL.t "light_control_lbl_4"
				l11.text = ::WdxL.t "light_control_lbl_5"
				l12.text = ::WdxL.t "light_strength_lbl_2"
				l13.text = ::WdxL.t "light_control_lbl_6"
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
