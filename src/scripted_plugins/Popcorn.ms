
plugin simpleManipulator Wdx_Wc3Popcorn
name:"Wc3 Popcorn"
classID:#(0x7A1B2C09, 0x3D4E5F09)
category:"Warcraft III Plugins"
(
	------------------------------------- Variable Registration -------------------------------------
	local Icon_Path		  = (GetDir #userIcons + "\\WhiteoutDexIcon\\")
	local MessageboxText  = "Popcorn FX Emitter\nUsed in Warcraft 3 Reforged (MDX v1200)\nReferences a .pkfx particle effect file\nFlags control when the effect plays"
	local MessageboxTitle = "Popcorn FX Info"
	------------------------------------- Parameter Registration -------------------------------------

	parameters main rollout:General
	(
		adsorption  type:#string	ui:Temp_adsorption
	)

	parameters main rollout:params1
	(
		popcornPath		type:#string	ui:popcornPath		animatable:false	default:""
	)

parameters main rollout:params2
	(
		flagAlways		type:#boolean	ui:flagAlways		animatable:false	default:false
		flagBirth		type:#boolean	ui:flagBirth		animatable:false	default:false
		flagDeath		type:#boolean	ui:flagDeath		animatable:false	default:false
		flagDissipate	type:#boolean	ui:flagDissipate	animatable:false	default:false
		flagPortrait	type:#boolean	ui:flagPortrait		animatable:false	default:false
		rawFlags		type:#string						animatable:false	default:""
	)

	parameters main rollout:params3
	(
		alpha			type:#float		ui:alpha			default:1.0
		emissionRate	type:#float		ui:emissionRate		default:0.0
		lifeSpan		type:#float		ui:lifeSpan			default:0.0			animatable:false
		speed			type:#float		ui:speed			default:0.0
		replaceableId	type:#integer	ui:replaceableId	default:0			animatable:false
	)

	parameters main rollout:params4
	(
		baseColor		type:#color		ui:baseColor		default:[255,255,255]	animatable:false
		YS_baseColor	type:#color		ui:YS_baseColor		default:[255,255,255]	animatable:false
		colorMultiplier	type:#float		ui:colorMultiplier	default:1.0				animatable:false
		teamColor		type:#color		ui:teamColor		default:[255,0,0]		animatable:false
		teamColorAlpha	type:#float		ui:teamColorAlpha	default:0.0				animatable:false
	)

	------------------------------------- Utility Functions -------------------------------------

	fn adjustColor_PS baseColor hueShift satShift brightShift =
	(
		::LxXUtils.AdjustColor_PS baseColor hueShift satShift brightShift
	)

	-----------------------------------Import Popcorn configuration parameters-----------------------------------
	fn ImportPopcorn =
	(
		local input_file = getOpenFileName caption:(::WdxL.t "popcorn_select_popcorn_fx_data_file_cap") types:"Config File (*.ini)|*.ini|" historyCategory:"PopcornImportHistory"
		if (input_file == undefined) then (return undefined)
		local obj = selection[1]
		----------------------------------------------------------------------------------------------------------------------
		local d01 = getINISetting input_file "Popcorn" "Path"
		local d02 = getINISetting input_file "Popcorn" "Always"
		local d03 = getINISetting input_file "Popcorn" "Birth"
		local d04 = getINISetting input_file "Popcorn" "Death"
		local d05 = getINISetting input_file "Popcorn" "Dissipate"
		local d06 = getINISetting input_file "Popcorn" "Portrait"
		local d07 = getINISetting input_file "Popcorn" "Alpha"
		local d08 = getINISetting input_file "Popcorn" "EmissionRate"
		local d09 = getINISetting input_file "Popcorn" "LifeSpan"
		local d10 = getINISetting input_file "Popcorn" "Speed"
		local d11 = getINISetting input_file "Popcorn" "ReplaceableId"
		local d12 = getINISetting input_file "Popcorn" "BaseColor"
		local d13 = getINISetting input_file "Popcorn" "ColorMultiplier"
		local d14 = getINISetting input_file "Popcorn" "TeamColor"
		local d15 = getINISetting input_file "Popcorn" "TeamColorAlpha"
		----------------------------------------------------------------------------------------------------------------------
		if (d01 != undefined and d01 != "") do (obj.popcornPath = d01)
		if (d02 != undefined) do (obj.flagAlways    = (d02 == "True"))
		if (d03 != undefined) do (obj.flagBirth     = (d03 == "True"))
		if (d04 != undefined) do (obj.flagDeath     = (d04 == "True"))
		if (d05 != undefined) do (obj.flagDissipate = (d05 == "True"))
		if (d06 != undefined) do (obj.flagPortrait  = (d06 == "True"))
		if (d07 != undefined and d07 != "") do (obj.alpha         = d07 as float)
		if (d08 != undefined and d08 != "") do (obj.emissionRate  = d08 as float)
		if (d09 != undefined and d09 != "") do (obj.lifeSpan      = d09 as float)
		if (d10 != undefined and d10 != "") do (obj.speed         = d10 as float)
		if (d11 != undefined and d11 != "") do (obj.replaceableId = d11 as integer)
		if d12 != undefined and d12 != "" do
		(
			local ci = findString d12 ")"
			local cs = substring d12 1 (ci-1)
			local cz = filterString cs " "
			obj.params4.baseColor.color    = color (cz[2] as float) (cz[3] as float) (cz[4] as float)
			obj.params4.YS_baseColor.color = obj.params4.baseColor.color
		)
		if (d13 != undefined and d13 != "") do (obj.colorMultiplier = d13 as float)
		if d14 != undefined and d14 != "" do
		(
			local ci = findString d14 ")"
			local cs = substring d14 1 (ci-1)
			local cz = filterString cs " "
			obj.params4.teamColor.color = color (cz[2] as float) (cz[3] as float) (cz[4] as float)
		)
		if (d15 != undefined and d15 != "") do (obj.teamColorAlpha = d15 as float)
		----------------------------------------------------------------------------------------------------------------------
		messagebox "Import Complete!"
	)

	-----------------------------------Export Popcorn configuration parameters-----------------------------------
	fn ExportPopcorn =
	(
		local output_file = getSaveFileName caption:(::WdxL.t "popcorn_save_popcorn_fx_data_file_cap") types:"Config File (*.ini)|*.ini|" historyCategory:"PopcornExportHistory"
		if (output_file == undefined) then (return undefined)
		local obj = selection[1]
		----------------------------------------------------------------------------------------------------------------------
		setINISetting output_file "Popcorn" "Path"				obj.popcornPath
		setINISetting output_file "Popcorn" "Always"			(obj.flagAlways    as string)
		setINISetting output_file "Popcorn" "Birth"				(obj.flagBirth     as string)
		setINISetting output_file "Popcorn" "Death"				(obj.flagDeath     as string)
		setINISetting output_file "Popcorn" "Dissipate"			(obj.flagDissipate as string)
		setINISetting output_file "Popcorn" "Portrait"			(obj.flagPortrait  as string)
		setINISetting output_file "Popcorn" "Alpha"				(obj.alpha         as string)
		setINISetting output_file "Popcorn" "EmissionRate"		(obj.emissionRate  as string)
		setINISetting output_file "Popcorn" "LifeSpan"			(obj.lifeSpan      as string)
		setINISetting output_file "Popcorn" "Speed"				(obj.speed         as string)
		setINISetting output_file "Popcorn" "ReplaceableId"		(obj.replaceableId as string)
		setINISetting output_file "Popcorn" "BaseColor"			(obj.baseColor     as string)
		setINISetting output_file "Popcorn" "ColorMultiplier"	(obj.colorMultiplier as string)
		setINISetting output_file "Popcorn" "TeamColor"			(obj.teamColor     as string)
		setINISetting output_file "Popcorn" "TeamColorAlpha"	(obj.teamColorAlpha as string)
		----------------------------------------------------------------------------------------------------------------------
		messagebox "Export Complete!"
	)

	------------------------------------- Rollout Definitions -------------------------------------

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
				btn_adsorption.text = "Parent: " + Temp_adsorption.text
			)catch()
		)
		on General open do
		(
			if ::WdxL != undefined then
			(
				General.title = ::WdxL.t "popcorn_general_functions_title"
			)
		)
	)

	rollout paramsE "Import / Export Configuration" rolledUp:true
	(
		button btnImport "Import" height:24 width:64 pos:[12,7]
		button btnExport "Export" height:24 width:64 pos:[86,7]
		On btnImport pressed do ImportPopcorn()
		On btnExport pressed do ExportPopcorn()
		on paramsE open do
		(
			if ::WdxL != undefined then
			(
				paramsE.title = ::WdxL.t "popcorn_import_export_configuration_title"
				btnImport.text = ::WdxL.t "popcorn_import_btn"
				btnExport.text = ::WdxL.t "popcorn_export_btn"
			)
		)
	)

	rollout params1 "Popcorn FX File"
	(
		label l1 "- Popcorn FX Path -" pos:[29,7] align:#center
		edittext popcornPath pos:[8,25] height:20 width:142
		button btnBrowse "Browse Popcorn FX File" pos:[12,53] height:24 width:138
		button btnInfo "Popcorn FX Explanation" pos:[12,82] height:24 width:138

		on btnBrowse pressed do
		(
			local f = getOpenFileName caption:(::WdxL.t "popcorn_select_popcorn_fx_file_cap") types:"Popcorn FX (*.pkfx)|*.pkfx|PKB File (*.pkb)|*.pkb|All Files|*.*|" historyCategory:"PopcornFXFile"
			if f != undefined then popcornPath.text = f
		)
		on btnInfo pressed do
		(
			messagebox (MessageboxText + "    ")
		)
		on params1 open do
		(
			if ::WdxL != undefined then
			(
				params1.title = ::WdxL.t "popcorn_popcorn_fx_file_title"
				l1.text = ::WdxL.t "popcorn__popcorn_fx_path_lbl"
				btnBrowse.text = ::WdxL.t "popcorn_browse_popcorn_fx_file_btn"
				btnInfo.text = ::WdxL.t "popcorn_popcorn_fx_explanation_btn"
			)
		)
	)

	rollout params2 "Event Flags"
	(
		label l2 "Always On ......................." height:16 pos:[12,8]  align:#left width:124
		checkbox flagAlways    height:16 pos:[137,8]  tooltip:"Effect is always active"
		label l3 "Birth .............................." height:16 pos:[12,29] align:#left width:124
		checkbox flagBirth     height:16 pos:[137,29] tooltip:"Effect plays on unit birth"
		label l4 "Death ............................" height:16 pos:[12,50] align:#left width:124
		checkbox flagDeath     height:16 pos:[137,50] tooltip:"Effect plays on unit death"
		label l5 "Dissipate ......................." height:16 pos:[12,71] align:#left width:124
		checkbox flagDissipate height:16 pos:[137,71] tooltip:"Effect plays on unit dissipate"
		label l6 "Portrait .........................." height:16 pos:[12,92] align:#left width:124
		checkbox flagPortrait  height:16 pos:[137,92] tooltip:"Effect plays in portrait view"
		-- Checkbox-Aenderung loescht rawFlags damit Export aus Booleans neu baut
		on flagAlways    changed state do rawFlags = ""
		on flagBirth     changed state do rawFlags = ""
		on flagDeath     changed state do rawFlags = ""
		on flagDissipate changed state do rawFlags = ""
		on flagPortrait  changed state do rawFlags = ""
		on params2 open do
		(
			if ::WdxL != undefined then
			(
				params2.title = ::WdxL.t "popcorn_event_flags_title"
				l2.text = ::WdxL.t "popcorn_always_on_lbl"
				flagAlways.tooltip = ::WdxL.t "popcorn_effect_is_always_active_tip"
				l3.text = ::WdxL.t "popcorn_birth_lbl"
				flagBirth.tooltip = ::WdxL.t "popcorn_effect_plays_on_unit_birth_tip"
				l4.text = ::WdxL.t "popcorn_death_lbl"
				flagDeath.tooltip = ::WdxL.t "popcorn_effect_plays_on_unit_death_tip"
				l5.text = ::WdxL.t "popcorn_dissipate_lbl"
				flagDissipate.tooltip = ::WdxL.t "popcorn_effect_plays_on_unit_dissipate_tip"
				l6.text = ::WdxL.t "popcorn_portrait_lbl"
				flagPortrait.tooltip = ::WdxL.t "popcorn_effect_plays_in_portrait_view_tip"
			)
		)
	)

	rollout params3 "Emission Settings"
	(
		label l7  "Alpha .................." height:16 pos:[12,8]  align:#left width:85
		spinner alpha        height:16 pos:[94,7]  range:[0,1,1.0]          scale:0.01  fieldwidth:44 tooltip:"Particle alpha opacity"
		label l8  "Emission Rate ........" height:16 pos:[12,31] align:#left width:85
		spinner emissionRate  height:16 pos:[94,30] range:[0,99999,0]       scale:1     fieldwidth:44 tooltip:"Particles emitted per second"
		label l9  "Life Span ............." height:16 pos:[12,54] align:#left width:85
		spinner lifeSpan      height:16 pos:[94,53] range:[0,99999,0]       scale:0.1   fieldwidth:44 tooltip:"Particle lifetime in seconds"
		label l10 "Speed .................." height:16 pos:[12,77] align:#left width:85
		spinner speed         height:16 pos:[94,76] range:[-99999,99999,0]  scale:0.1   fieldwidth:44 tooltip:"Particle emission speed"
		label l11 "- Replaceable Texture -" pos:[29,101] align:#center
		dropdownList replaceableId "" items:#("Not Used", "Team Color", "Team Glow") selection:1 width:138 pos:[12,119]
		on params3 open do
		(
			if ::WdxL != undefined then
			(
				params3.title = ::WdxL.t "popcorn_emission_settings_title"
				l7.text = ::WdxL.t "popcorn_alpha_lbl"
				alpha.tooltip = ::WdxL.t "popcorn_particle_alpha_opacity_tip"
				l8.text = ::WdxL.t "popcorn_emission_rate_lbl"
				emissionRate.tooltip = ::WdxL.t "popcorn_particles_emitted_per_second_tip"
				l9.text = ::WdxL.t "popcorn_life_span_lbl"
				lifeSpan.tooltip = ::WdxL.t "popcorn_particle_lifetime_in_seconds_tip"
				l10.text = ::WdxL.t "popcorn_speed_lbl"
				speed.tooltip = ::WdxL.t "popcorn_particle_emission_speed_tip"
				l11.text = ::WdxL.t "popcorn__replaceable_texture_lbl"
			)
		)
	)

	rollout params4 "Color Settings"
	(
		label       l12             "—— - Base Color - ——"      align:#center
		colorPicker baseColor                                   height:32 width:89   pos:[8,25]   enabled:false color:(color 255 255 255)
		colorPicker YS_baseColor                                height:1  width:1    pos:[8,25]   visible:false color:(color 255 255 255)
		button      btn_baseColor   "..."                       height:32 width:32   pos:[118,25] tooltip:"Change Color"
		label       l13             "Color Multiplier ........"  width:85             pos:[12,64]  align:#left
		spinner     colorMultiplier                              height:16 scale:0.1  pos:[94,64]  range:[0,100,1.0]    fieldwidth:44
		label       l14             "—— - Team Color - ——"      align:#center offset:[0,6]
		colorPicker teamColor                                   height:32 width:89   pos:[8,103]  enabled:false color:(color 255 0 0)
		button      btn_teamColor   "..."                       height:32 width:32   pos:[118,103] tooltip:"Change Team Color"
		label       l15             "Team Color Alpha ...."      width:85             pos:[12,142]  align:#left
		spinner     teamColorAlpha                               height:16 scale:0.01 pos:[94,142]  range:[0,1,0.0]     fieldwidth:44

		on btn_baseColor pressed do
		(
			local c = (dotNetClass "System.Windows.Forms.ColorDialog")
			local dlg = dotNetObject "System.Windows.Forms.ColorDialog"
			dlg.color = (dotNetObject "System.Drawing.Color").FromArgb (baseColor.color.r as integer) (baseColor.color.g as integer) (baseColor.color.b as integer)
			dlg.FullOpen = true
			if (dlg.ShowDialog() == (dotNetClass "System.Windows.Forms.DialogResult").OK) then
			(
				baseColor.color    = color dlg.Color.R dlg.Color.G dlg.Color.B
				YS_baseColor.color = baseColor.color
			)
		)
		on btn_teamColor pressed do
		(
			local c = (dotNetClass "System.Windows.Forms.ColorDialog")
			local dlg = dotNetObject "System.Windows.Forms.ColorDialog"
			dlg.color = (dotNetObject "System.Drawing.Color").FromArgb (teamColor.color.r as integer) (teamColor.color.g as integer) (teamColor.color.b as integer)
			dlg.FullOpen = true
			if (dlg.ShowDialog() == (dotNetClass "System.Windows.Forms.DialogResult").OK) then
			(
				teamColor.color = color dlg.Color.R dlg.Color.G dlg.Color.B
			)
		)
		on params4 open do
		(
			if ::WdxL != undefined then
			(
				params4.title = ::WdxL.t "popcorn_color_settings_title"
				l12.text = ::WdxL.t "popcorn__base_color_lbl"
				btn_baseColor.tooltip = ::WdxL.t "popcorn_change_color_tip"
				l13.text = ::WdxL.t "popcorn_color_multiplier_lbl"
				l14.text = ::WdxL.t "popcorn__team_color_lbl"
				btn_teamColor.tooltip = ::WdxL.t "popcorn_change_team_color_tip"
				l15.text = ::WdxL.t "popcorn_team_color_alpha_lbl"
			)
		)
	)

	------------------------------------- Create Tool -------------------------------------
	tool create
	(
		on mousePoint click do
		(
			case click of
			(
				1: nodeTM.translation = gridPoint
				2: #stop
				3: #stop
			)
		)
	)

	------------------------------------- Gizmo Drawing -------------------------------------
	fn BuildGizmo points Drawcolor =
	(
		local unselectedColor = Drawcolor
		local selectedColor   = Drawcolor
		local giz             = manip.makeGizmoShape()
		giz.startNewLine()
		for pnt in points do giz.addpoint pnt
		this.addGizmoShape giz 0 unselectedColor selectedColor
	)

	fn BuildGizmoCircle radius center Drawcolor =
	(
		local unselectedColor = Drawcolor
		local selectedColor   = Drawcolor
		local giz = manip.makeCircle center radius 32
		this.addGizmoShape giz 0 unselectedColor selectedColor
	)

	on updateGizmos do
	(
		this.clearGizmos()
		local Drawcolor = [102,255,178]
		Drawcolor = [Drawcolor.x/255, Drawcolor.y/255, Drawcolor.z/255]

		-- Starburst / Explosion (Popcorn-Zacken)
		BuildGizmo #( [-20.2227,-7.44002,0], [-17.1315,-6.32913,0], [-13.6539,-4.92844,0], [-11.6736,-3.38285,0], [-11.3355,-2.17536,0], [-11.8668,-0.871269,0], [-13.6539,0.915818,0], [-18.339,4.10359,0], [-13.0743,4.00699,0], [-11.6253,4.20019,0], [-9.98315,4.92469,0], [-8.53416,6.22878,0], [-8.00286,7.53287,0], [-7.90627,11.0104,0], [-8.29263,13.3771,0], [-9.83822,19.1731,0], [-7.47154,14.7778,0], [-6.26405,12.7975,0], [-4.52526,10.9139,0], [-3.41437,10.286,0], [-1.96538,9.94787,0], [-1.09599,10.286,0], [0.497899,12.3628,0], [2.23669,15.5506,0], [6.48712,27.6256,0], [4.36194,17.6275,0], [3.87894,14.3915,0], [3.78234,11.8316,0], [3.87894,10.4309,0], [4.16874,9.85129,0], [4.79663,9.46489,0], [5.85923,9.80299,0], [13.3457,15.4057,0], [19.6729,20.6704,0], [29.6227,29.7508,0], [20.6873,19.2697,0], [14.6981,11.8316,0], [11.8968,7.96763,0], [11.124,6.22884,0], [11.3172,5.35945,0], [12.1866,4.10366,0], [14.0702,3.52406,0], [22.4744,3.08936,0], [16.6785,1.49554,0], [12.6696,-0.291545,0], [8.66074,-3.14122,0], [7.59815,-4.59021,0], [6.58385,-6.7637,0], [6.24576,-8.64738,0], [6.63215,-11.4488,0], [7.50155,-13.0426,0], [8.99884,-14.6365,0], [6.87365,-13.6705,0], [4.94167,-13.0909,0], [3.15458,-12.9943,0], [2.52669,-13.429,0], [2.09199,-14.9263,0], [2.04375,-17.0031,0], [2.67165,-22.5576,0], [1.17436,-19.1766,0], [0.0634651,-17.4378,0], [-1.04743,-16.3269,0], [-2.25492,-15.6024,0], [-4.3318,-16.0371,0], [-7.03658,-19.2732,0], [-9.78966,-23.91,0], [-12.3978,-29.0297,0], [-10.0312,-22.3161,0], [-9.06517,-18.5487,0], [-8.82367,-16.4235,0], [-8.82367,-14.6848,0], [-9.30666,-14.0569,0], [-10.2244,-14.0569,0], [-11.625,-14.7331,0], [-16.938,-18.5004,0], [-20.7537,-21.7365,0], [-25.3421,-25.9386,0], [-29.0129,-29.3678,0], [-25.2455,-24.876,0], [-20.5605,-19.563,0], [-16.938,-15.5058,0], [-13.074,-10.9174,0], [-12.108,-9.61329,0], [-12.108,-9.082,0], [-12.9774,-8.3092,0], [-14.6196,-7.92281,0], [-16.7448,-7.68131,0], [-20.2227,-7.44002,0]) Drawcolor

		-- "P" Buchstabe (Aussenlinie)
		BuildGizmo #( [-5.95865,7.14973,0], [1.82823,7.14973,0], [3.36512,6.63743,0], [4.86785,5.647,0], [5.92659,4.48579,0], [6.88288,2.64153,0], [7.22441,1.17295,0], [7.25856,-0.159014,0], [6.74627,-1.59344,0], [5.85829,-2.89125,0], [4.73124,-4.05246,0], [3.50173,-4.94043,0], [1.82823,-5.45273,0], [-1.75783,-5.45273,0], [-1.75783,-13.8202,0], [-5.9928,-13.8202,0], [-5.95865,7.14973,0] ) Drawcolor

		-- "P" Buchstabe (Innenloch)
		BuildGizmo #( [-1.78839,2.987,0], [1.37882,2.987,0], [2.17535,2.5253,0], [2.65518,1.77951,0], [2.77593,0.861821,0], [2.67933,0.064877,0], [2.17529,-0.707061,0], [1.42711,-1.21506,0], [-1.71594,-1.21506,0], [-1.78839,2.987,0] ) Drawcolor

		-- Drei Popcorn-Koerner (Kreise)
		BuildGizmoCircle 3.4 [12.6726,-7.75778,0] Drawcolor
		BuildGizmoCircle 2.2 [25.2409,-5.09385,0] Drawcolor
		BuildGizmoCircle 1.2 [23.5332,12.5291,0]  Drawcolor
	)
)
