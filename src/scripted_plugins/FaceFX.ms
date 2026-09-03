
plugin simpleManipulator Wdx_FaceFX
name:"Wc3 FaceFX"
classID:#(0x7A1B2C08, 0x3D4E5F08)
category:"Warcraft III Plugins"
(
	------------------------------------- Variable Registration -------------------------------------
	local Icon_Path		  = (GetDir #userIcons + "\\WhiteoutDexIcon\\")
	local MessageboxText  = "FaceFX Emitter\nUsed in Warcraft 3 Reforged (MDX v1200)\nReferences a .facefx_ingame file for\nfacial animation and lip-sync data"
	------------------------------------- Parameter Registration -------------------------------------

	parameters main rollout:General
	(
		adsorption  type:#string	ui:Temp_adsorption
	)

	parameters main rollout:params1
	(
		facefxName		type:#string	ui:facefxName		animatable:false	default:""
		facefxPath		type:#string	ui:facefxPath		animatable:false	default:""
	)

	------------------------------------- Utility Functions -------------------------------------

	-----------------------------------Import FaceFX configuration parameters-----------------------------------
	fn ImportFaceFX =
	(
		local input_file = getOpenFileName caption:(::WdxL.t "facefx_select_facefx_data_file_cap") types:"Config File (*.ini)|*.ini|" historyCategory:"FaceFXImportHistory"
		if (input_file == undefined) then (return undefined)
		local obj = selection[1]
		----------------------------------------------------------------------------------------------------------------------
		local d01 = getINISetting input_file "FaceFX" "Name"
		local d02 = getINISetting input_file "FaceFX" "Path"
		----------------------------------------------------------------------------------------------------------------------
		if (d01 != undefined and d01 != "") do (obj.facefxName = d01)
		if (d02 != undefined and d02 != "") do (obj.facefxPath = d02)
		----------------------------------------------------------------------------------------------------------------------
		messagebox (::WdxL.t "common_import_complete_msg")
	)

	-----------------------------------Export FaceFX configuration parameters-----------------------------------
	fn ExportFaceFX =
	(
		local output_file = getSaveFileName caption:(::WdxL.t "facefx_save_facefx_data_file_cap") types:"Config File (*.ini)|*.ini|" historyCategory:"FaceFXExportHistory"
		if (output_file == undefined) then (return undefined)
		local obj = selection[1]
		----------------------------------------------------------------------------------------------------------------------
		setINISetting output_file "FaceFX" "Name"	obj.facefxName
		setINISetting output_file "FaceFX" "Path"	obj.facefxPath
		----------------------------------------------------------------------------------------------------------------------
		messagebox (::WdxL.t "common_export_complete_msg")
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
				btn_adsorption.text = (::WdxL.t "common_parent_prefix") + Temp_adsorption.text
			)catch()
		)
		on General open do
		(
			if ::WdxL != undefined then
			(
				General.title = ::WdxL.t "facefx_general_functions_title"
				-- Only reset the caption while nothing is adsorbed; once a
				-- parent is picked the button shows that name instead.
				if Temp_adsorption.text == "" do
					btn_adsorption.text = ::WdxL.t "common_adsorb_btn"
			)
		)
	)

	rollout paramsE "Import / Export Configuration" rolledUp:true
	(
		button btnImport "Import" height:24 width:64 pos:[12,7]
		button btnExport "Export" height:24 width:64 pos:[86,7]
		On btnImport pressed do ImportFaceFX()
		On btnExport pressed do ExportFaceFX()
		on paramsE open do
		(
			if ::WdxL != undefined then
			(
				paramsE.title = ::WdxL.t "facefx_import_export_configuration_title"
				btnImport.text = ::WdxL.t "facefx_import_btn"
				btnExport.text = ::WdxL.t "facefx_export_btn"
			)
		)
	)

	rollout params1 "FaceFX Settings"
	(
		label l1 "- FaceFX Target Name -" pos:[25,7] align:#center
		edittext facefxName pos:[8,25] height:20 width:142
		label l2 "- FaceFX File Path -" pos:[29,52] align:#center
		edittext facefxPath pos:[8,70] height:20 width:142
		button btnBrowse "Browse FaceFX File" pos:[12,98] height:24 width:138
		button btnInfo "FaceFX Explanation" pos:[12,127] height:24 width:138

		on btnBrowse pressed do
		(
			local f = getOpenFileName caption:(::WdxL.t "facefx_select_facefx_file_cap") types:"FaceFX Files (*.facefx_ingame)|*.facefx_ingame|All Files|*.*|" historyCategory:"FaceFXFile"
			if f != undefined then facefxPath.text = f
		)
		on btnInfo pressed do
		(
			messagebox (MessageboxText + "    ")
		)
		on params1 open do
		(
			if ::WdxL != undefined then
			(
				params1.title = ::WdxL.t "facefx_facefx_settings_title"
				l1.text = ::WdxL.t "facefx__facefx_target_name_lbl"
				l2.text = ::WdxL.t "facefx__facefx_file_path_lbl"
				btnBrowse.text = ::WdxL.t "facefx_browse_facefx_file_btn"
				btnInfo.text = ::WdxL.t "facefx_facefx_explanation_btn"
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
				1: (nodeTM.translation = gridPoint; #stop)
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
		local dc = [255,200,80]
		dc = [dc.x/255, dc.y/255, dc.z/255]

		-- Kopf (Kreis)
		BuildGizmoCircle 19.0 [0,0,0] dc

		-- Linkes Auge (Bogen)
		BuildGizmo #( [-12,3,0], [-10,6,0], [-8,7,0], [-6,6,0], [-4,3,0] ) dc

		-- Rechtes Auge (Bogen)
		BuildGizmo #( [4,3,0], [6,6,0], [8,7,0], [10,6,0], [12,3,0] ) dc

		-- Linke Pupille
		BuildGizmoCircle 1.8 [-8,3.5,0] dc

		-- Rechte Pupille
		BuildGizmoCircle 1.8 [8,3.5,0] dc

		-- Nase (minimale Linie)
		BuildGizmo #( [0,1,0], [0,-4,0], [-2,-5,0] ) dc

		-- Mund (einfache Kurve)
		BuildGizmo #( [-8,-9,0], [-4,-12,0], [0,-13,0], [4,-12,0], [8,-9,0] ) dc
	)
)
