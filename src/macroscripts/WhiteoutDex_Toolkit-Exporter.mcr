macroScript WhiteoutDex_Exporter
buttonText:"WhiteoutDex Exporter"
category:"WhiteoutDex Toolkit"
internalCategory:"WhiteoutDex Toolkit" 
(
	/*
		WhiteoutDex Exporter - UI + Tabbed Layout
		=====================================
		All export logic is in WhiteoutDexExportFunctions.ms
		This file only contains the UI rollouts and event handlers.
		
		Provided by WhiteoutDexExportFunctions.ms:
		  - WhiteoutDexSceneMonitorStruct / ::whiteoutDexSceneMonitorInstance
		  - WhiteoutDexExportSettings struct
		  - exportScene function
		  - getIncrementedFilename function
		  - Global variables: theDuplicates, thePolyProblems, theBoneProblems, theMaterialProblems
	*/
	
    local whiteoutDexVersion = ".66"

global mainRoll, ProblemDetailsRollout, settingsRoll
global whiteoutDexMatSettingsRef
-------------------------------------------------------------------------------------------------------
-- Material Settings Dialog
-------------------------------------------------------------------------------------------------------
rollout materialSettingsDialog "Material Settings for Fix" width:340
(
    local iniPath = (getDir #plugcfg) + "\\MDLXExporter.ini"
    local matIniPath = GetDir #userScripts + "\\WhiteoutDexConfig\\MatPrefixPath.ini"
    
    group "Basic Parameters"
    (
        checkbox chk_unshaded "Unshaded" across:3
        checkbox chk_unfogged "UnFogged"
        checkbox chk_twosided "Two Sided"
    )
    
    group "Filter Mode"
    (
        radiobuttons rad_filterMode "" labels:#("None", "Transparent", "Blend", "Additive", "Additive 2x", "Modulate", "Modulate 2x") \
            columns:3 default:1
    )
    
    group "Texture Parameters"
    (
        label lbl_prefix "Prefix Path:"
        edittext edt_prefixPath "" width:220 align:#left
        button btn_selectPrefix "▼" width:24 height:20 offset:[110,-24]
        button btn_editPrefix   "+"  width:24 height:20 offset:[85,-25] tooltip:"Edit shortcut prefix path"
        
        checkbox chk_uTile "U_Tile (Wrap Width)" across:2 align:#left
        checkbox chk_vTile "V_Tile (Wrap Height)" align:#left
    )
    
    button btn_save "Save Settings" width:148 height:30 across:2 align:#left
    button btn_cancel "Cancel" width:148 height:30 align:#right
    
    fn loadSettings =
    (
        local unshaded = getINISetting iniPath "MaterialFix" "Unshaded"
        local unfogged = getINISetting iniPath "MaterialFix" "Unfogged"
        local twosided = getINISetting iniPath "MaterialFix" "Twosided"
        local filterMode = getINISetting iniPath "MaterialFix" "FilterMode"
        local prefixPath = getINISetting iniPath "MaterialFix" "PrefixPath"
        local uTile = getINISetting iniPath "MaterialFix" "UTile"
        local vTile = getINISetting iniPath "MaterialFix" "VTile"
        
        if unshaded != "" then chk_unshaded.checked = (unshaded == "true")
        if unfogged != "" then chk_unfogged.checked = (unfogged == "true")
        if twosided != "" then chk_twosided.checked = (twosided == "true")
        if filterMode != "" then rad_filterMode.state = (filterMode as integer)
        if prefixPath != "" then edt_prefixPath.text = prefixPath
        if uTile != "" then chk_uTile.checked = (uTile == "true")
        if vTile != "" then chk_vTile.checked = (vTile == "true")
    )
    
    fn saveSettings =
    (
        setINISetting iniPath "MaterialFix" "Unshaded" (chk_unshaded.checked as string)
        setINISetting iniPath "MaterialFix" "Unfogged" (chk_unfogged.checked as string)
        setINISetting iniPath "MaterialFix" "Twosided" (chk_twosided.checked as string)
        setINISetting iniPath "MaterialFix" "FilterMode" (rad_filterMode.state as string)
        setINISetting iniPath "MaterialFix" "PrefixPath" edt_prefixPath.text
        setINISetting iniPath "MaterialFix" "UTile" (chk_uTile.checked as string)
        setINISetting iniPath "MaterialFix" "VTile" (chk_vTile.checked as string)
    )
    
    on btn_selectPrefix pressed do
    (
        if not (doesFileExist matIniPath) then
        (
            makeDir (getFilenamePath matIniPath) all:true
            setINISetting matIniPath "PathMenu" "Count" "4"
            setINISetting matIniPath "PathMenu" "Path1" "Textures\\\\"
            setINISetting matIniPath "PathMenu" "Path2" "war3mapImported\\\\"
            setINISetting matIniPath "PathMenu" "Path3" "ReplaceableTextures\\\\Shadows\\\\"
            setINISetting matIniPath "PathMenu" "Path4" "ReplaceableTextures\\\\Cliff\\\\"
        )
        local PathCount = (getINISetting matIniPath "PathMenu" "Count") as integer
        local code = "rcMenu prefixMenu\n(\n"
        local lastMenu = "1"
        for i = 1 to PathCount do
        (
            code += "menuItem mi" + (i as string) + " \""+ (getINISetting matIniPath "PathMenu" ("Path" + i as string)) + "\"\n"
            lastMenu = (i + 1) as string
        )
        code += "menuItem mi" + lastMenu + " \"" + "(Clear)" + "\"\n"
        for i = 1 to PathCount do
        (
            local tempstr = getINISetting matIniPath "PathMenu" ("Path" + i as string)
             code += "on mi" + (i as string) + " picked do (whiteoutDexMatSettingsRef.edt_prefixPath.text = \"" + tempstr + "\")\n"
        )
        code += "on mi" + lastMenu + " picked do (whiteoutDexMatSettingsRef.edt_prefixPath.text = \"\")\n"
        code += ")\npopUpMenu prefixMenu pos:mouse.screenpos"
        execute code
    )
    
    on btn_editPrefix pressed do
    (
        if not (doesFileExist matIniPath) then
        (
            makeDir (getFilenamePath matIniPath) all:true
            setINISetting matIniPath "PathMenu" "Count" "4"
            setINISetting matIniPath "PathMenu" "Path1" "Textures\\\\"
            setINISetting matIniPath "PathMenu" "Path2" "war3mapImported\\\\"
            setINISetting matIniPath "PathMenu" "Path3" "ReplaceableTextures\\\\Shadows\\\\"
            setINISetting matIniPath "PathMenu" "Path4" "ReplaceableTextures\\\\Cliff\\\\"
        )
        local processClass = dotNetClass "System.Diagnostics.Process"
        local startInfo = dotNetObject "System.Diagnostics.ProcessStartInfo"
        startInfo.FileName = "notepad.exe"
        startInfo.Arguments = matIniPath
        startInfo.UseShellExecute = true
        process = processClass.Start(startInfo)
    )
    
    on btn_save pressed do
    (
        saveSettings()
        destroyDialog materialSettingsDialog
    )
    
    on btn_cancel pressed do
    (
        destroyDialog materialSettingsDialog
    )
    
    on materialSettingsDialog open do
    (
        loadSettings()
        whiteoutDexMatSettingsRef = materialSettingsDialog
        if ::L != undefined then
        (
            materialSettingsDialog.title = ::L.t "exp_material_settings_for_fix_title"
            chk_unshaded.text = ::L.t "exp_unshaded_chk"
            chk_unfogged.text = ::L.t "exp_unfogged_chk"
            chk_twosided.text = ::L.t "exp_two_sided_chk"
            lbl_prefix.text = ::L.t "exp_prefix_path_lbl"
            btn_selectPrefix.tooltip = try(::L.t "mat_select_prefix_path_tip") catch("Select prefix path")
            btn_editPrefix.tooltip = try(::L.t "mat_edit_shortcut_prefix_path_tip") catch("Edit shortcut prefix path")
            chk_uTile.text = ::L.t "exp_utile_wrap_width_chk"
            chk_vTile.text = ::L.t "exp_vtile_wrap_height_chk"
            btn_save.text = ::L.t "exp_save_settings_btn"
            btn_cancel.text = ::L.t "exp_cancel_btn"
        )
    )
)

-------------------------------------------------------------------------------------------------------
-- Problem Details Rollout
-------------------------------------------------------------------------------------------------------
rollout ProblemDetailsRollout "Scene Problems" width:350 height:400
(
	dotNetControl lb_problems "System.Windows.Forms.ListView" pos:[10,35] width:330 height:320
	button btn_fix "Fix Selected Problem Types" width:220 height:22 across:2 align:#left
	button cancelBtn "Cancel" width:80 height:22 align:#right

	fn setupListView =
	(
		lb_problems.View = (dotNetClass "System.Windows.Forms.View").Details
		lb_problems.FullRowSelect = true
		lb_problems.GridLines = true
		lb_problems.HideSelection = false
		lb_problems.MultiSelect = true
		lb_problems.Columns.Clear()
		lb_problems.Columns.Add "Problem Type" 100
		lb_problems.Columns.Add "Object/Name" 230
	)

	fn updateListView =
	(
		lb_problems.Items.Clear()
		for d in ::theDuplicates do
		(
			local li = lb_problems.Items.Add "Duplicate Name"
			li.SubItems.Add d
			li.Tag = "duplicate"
		)
		for m in ::thePolyProblems do
		(
			local problemType = if (matchPattern m pattern:"*[EMPTY]*") then "Empty Mesh" else "Editable Poly"
			local objName = substituteString m "[POLY] " ""
			objName = substituteString objName "[EMPTY] " ""
			local li = lb_problems.Items.Add problemType
			li.SubItems.Add objName
			li.Tag = "mesh"
		)
		for b in ::theBoneProblems do
		(
			local li = lb_problems.Items.Add "Invalid Controller"
			li.SubItems.Add b.name
			li.Tag = "bone"
		)
		for m in ::theMaterialProblems do
		(
			local li = lb_problems.Items.Add "Physical Material"
			li.SubItems.Add m.name
			li.Tag = "material"
		)
	)

	fn setFixBtnEnabled =
	(
		btn_fix.enabled = lb_problems.SelectedItems.Count > 0
	)
	
	on lb_problems ItemSelectionChanged args do setFixBtnEnabled()
	
	on ProblemDetailsRollout open do
	(
		setupListView()
		updateListView()
		setFixBtnEnabled()
		if ::L != undefined then
		(
			ProblemDetailsRollout.title = ::L.t "exp_scene_problems_title"
			btn_fix.text = ::L.t "exp_fix_selected_problem_types_btn"
			cancelBtn.text = ::L.t "exp_cancel_btn_2"
		)
	)

	on btn_fix pressed do
	(
		local fixDuplicates = false
		local fixMeshes = false
		local fixBones = false
		local fixMaterials = false
		for i = 0 to lb_problems.SelectedItems.Count - 1 do
		(
			local itemTag = lb_problems.SelectedItems.Item[i].Tag
			case itemTag of
			(
				"duplicate": fixDuplicates = true
				"mesh": fixMeshes = true
				"bone": fixBones = true
				"material": fixMaterials = true
			)
		)
		if fixDuplicates then ::whiteoutDexSceneMonitorInstance.fixDuplicateNames()	
		if fixMeshes then ::whiteoutDexSceneMonitorInstance.fixMeshProblems()	
		if fixBones then ::whiteoutDexSceneMonitorInstance.fixBoneControllers()	
		if fixMaterials then ::whiteoutDexSceneMonitorInstance.fixMaterials()
		::whiteoutDexSceneMonitorInstance.refreshAllProblems()
		::theDuplicates = ::whiteoutDexSceneMonitorInstance.duplicates
		::thePolyProblems = ::whiteoutDexSceneMonitorInstance.polyProblems
		::theBoneProblems = ::whiteoutDexSceneMonitorInstance.boneProblems
		::theMaterialProblems = ::whiteoutDexSceneMonitorInstance.materialProblems
		updateListView()
		setFixBtnEnabled()
		if lb_problems.Items.count == 0 then
			(try DestroyDialog ProblemDetailsRollout catch())
	)
	
	on cancelBtn pressed do
		(try DestroyDialog ProblemDetailsRollout catch())
)

-------------------------------------------------------------------------------------------------------
-- Main Export Rollout (Tabbed UI)
-------------------------------------------------------------------------------------------------------
rollout mainroll "Export Settings" width:245
(
	local contentWidth = 208
	local iniPath = getDir #plugcfg + "\\MDLXExporter.ini"
	local lastExportPath = ""
	local dnColorGreen = (dotNetClass "System.Drawing.Color").Green
	local dnColorRed = (dotNetClass "System.Drawing.Color").Red
	local currentTab = 1

	-- ===========================================
	-- HEADER (flow layout)
	-- ===========================================

	group "Model Name"
	(
		edittext modelNameTxt multiLine:false width:contentWidth height:18 text:"Just Another Model" tooltip:"Set the name for your exported model"
	)

	group "Format Version"
	(
		radiobuttons exportVersionRdo width:contentWidth labels:#("Classic (v800)", "Reforged (v1200)") default:1 columns:2 tooltip:"v800 for Classic WC3, v1200 for Reforged models"
	)

	group "File Name"
	(
		edittext filenameTxt multiLine:false width:180 height:18 align:#left across:2 tooltip:"Last export path - will be used for quick export"
		button filenameBrowseBtn "..." width:25 height:18 align:#right tooltip:"Clear the quick export path"
		checkbox filenameOverwriteChk "Overwrite without Warning" width:contentWidth
		checkbox filenameAutoIncrementChk "Auto-increment filename" width:contentWidth tooltip:"Automatically adds _1, _2, etc. to prevent overwriting"
	)

	-- ===========================================
	-- TAB BUTTONS
	-- ===========================================

	button btn_tabOptions "[ Options ]" width:110 height:22 across:2 align:#left
	button btn_tabTexture "Texture Conversion" width:110 height:22 align:#right

	-- ===========================================
	-- TAB CONTENT (pos-based, overlapping)
	-- ===========================================

	groupBox grp_tabFrame "" pos:[6,290] width:232 height:180

	-- --- Tab 1: Options ---
	checkbox optionsMergeSimilarChk        "Merge similar meshes"       pos:[16,300] width:contentWidth visible:true tooltip:"Combines similar meshes to optimize the exported model"
	checkbox optionsFixNormalsChk           "AT Skinning Fix normals"    pos:[16,320] width:contentWidth visible:true tooltip:"Recalculates normals for connected vertices"
	label    lbl_threshold                  "Threshold:"                 pos:[26,342] visible:true
	spinner  optionsFixNormalsTresholdSpn   "" type:#float range:[0,100,0.1] scale:0.025 fieldWidth:55 pos:[160,340] visible:true tooltip:"Distance threshold for vertex connections"
	checkbox optionsExportSmoothgroupsChk   "Export Smoothgroups"        pos:[16,360] width:contentWidth visible:true tooltip:"Exports smoothing groups"
	checkbox optionsFixSharedNormalsChk     "Fix shared normals"         pos:[16,380] width:contentWidth visible:true checked:true tooltip:"Averages normals of shared vertices across geosets"
	checkbox optionsKeepUnusedBonesHelpersChk "Keep unused Bones/Helpers" pos:[16,400] width:contentWidth visible:true tooltip:"Exports all bones and helpers in the scene"
	checkbox optionsOpenFolderChk           "Open folder after export"   pos:[16,420] width:contentWidth visible:true tooltip:"Automatically opens the export folder after successful export"
	button   optionsMaterialFixBtn          "Material Fix Settings..."   width:208 height:22 pos:[16,440] visible:true tooltip:"Configure default settings for Fix Materials function"

	-- --- Tab 2: Texture Conversion (hidden, same area) ---
	checkbox     texConvertBLPChk   "Convert textures to BLP on export"  pos:[16,300] width:contentWidth checked:true visible:false tooltip:"Automatically converts textures to BLP format during export"
	label        lbl_blpSection     "— BLP Settings —"                   pos:[70,322] visible:false
	label        lbl_compress       "Compression:"                       pos:[16,342] visible:false
	dropdownlist texCompressionDdl  "" items:#("Paletted (256 colors)", "JPEG") pos:[16,358] width:contentWidth visible:false tooltip:"BLP compression method"
	label        lbl_jpegQual       "JPEG Quality:"                      pos:[26,388] visible:false
	spinner      texJpegQualitySpn  "" type:#integer range:[1,100,75] fieldWidth:55 pos:[160,386] visible:false tooltip:"JPEG compression quality (1-100)"
	checkbox     texDitheringChk    "Dithering (reduces color banding)"  pos:[16,406] width:contentWidth visible:false tooltip:"Apply dithering to reduce color banding in paletted mode"
	checkbox     texMipmapsChk      "Generate Mipmaps"                   pos:[16,426] width:contentWidth checked:true visible:false tooltip:"Generate mipmap levels for the BLP file"
	label        lbl_outSection     "— Output —"                         pos:[85,448] visible:false
	checkbox     texOverwriteBLPChk "Overwrite existing BLP"             pos:[16,466] width:contentWidth visible:false tooltip:"Overwrite existing BLP files. If unchecked, existing BLP files will be skipped."

	-- ===========================================
	-- FOOTER (pos-based)
	-- ===========================================

	groupBox     grp_extents          "Extents Calculation" pos:[6,508] width:232 height:58
	radiobuttons extentsTypeRdo       labels:#("Animation Dependent", "Global") default:1 columns:2 pos:[16,524] tooltip:"Animation Dependent adjusts based on animation length, Global uses the same precision for all animations"
	label        lbl_extPrec          "Extents Precision:" pos:[26,548]
	spinner      extentsPrecisionSpn  "" type:#integer range:[0,10,6] fieldWidth:55 pos:[160,546]

	groupBox      grp_status          "Scene Status" pos:[6,572] width:232 height:88
	dotNetControl statusPnl           "System.Windows.Forms.Panel" width:208 height:25 pos:[18,590] tooltip:"Current scene status overview"
	label         statusLbl           "All Clear!" pos:[95,618] tooltip:"Current scene status message"
	button        statusFixAllBtn     "Fix All" width:104 height:26 pos:[16,636] tooltip:"Automatically fix all detected problems in the scene"
	button        statusShowProblemsBtn "Show Details" width:104 height:26 pos:[124,636] tooltip:"Show a detailed list of detected problems"

	groupBox     grp_exportMode "Export Mode" pos:[6,666] width:232 height:36
	radiobuttons exportModeRdo  labels:#("Standard", "Debug") default:1 columns:2 pos:[16,682] tooltip:"Standard Mode for normal export, Debug Mode for troubleshooting"

	groupBox    grp_progress "Progress" pos:[6,708] width:232 height:58
	progressBar progressPBar color:(color 30 10 190) pos:[16,726] width:208 tooltip:"Shows the export progress"
	label       progressLbl  "Idle" pos:[100,748] tooltip:"Current export status"

	button exportBtn "Export" width:230 height:32 pos:[7,772] tooltip:"Start the export process with current settings"


	-- ===========================================
	-- REPOSITION ALL FROM TAB BUTTONS
	-- ===========================================

	fn repositionAll tabY =
	(
		local x = 16
		local tabH = 210
		grp_tabFrame.pos    = [6, tabY - 6]
		grp_tabFrame.width  = 232
		grp_tabFrame.height = tabH + 18

		local cY = tabY + 8

		-- Tab 1: Options
		optionsMergeSimilarChk.pos        = [x, cY]
		optionsFixNormalsChk.pos          = [x, cY + 20]
		lbl_threshold.pos                 = [x + 10, cY + 42]
		optionsFixNormalsTresholdSpn.pos   = [160, cY + 40]
		optionsExportSmoothgroupsChk.pos  = [x, cY + 60]
		optionsFixSharedNormalsChk.pos    = [x, cY + 80]
		optionsKeepUnusedBonesHelpersChk.pos = [x, cY + 100]
		optionsOpenFolderChk.pos          = [x, cY + 120]
		optionsMaterialFixBtn.pos         = [x, cY + 144]

		-- Tab 2: Texture Conversion
		texConvertBLPChk.pos   = [x, cY]
		lbl_blpSection.pos     = [70, cY + 22]
		lbl_compress.pos       = [x, cY + 40]
		texCompressionDdl.pos  = [x, cY + 56]
		lbl_jpegQual.pos       = [x + 10, cY + 86]
		texJpegQualitySpn.pos  = [160, cY + 84]
		texDitheringChk.pos    = [x, cY + 104]
		texMipmapsChk.pos      = [x, cY + 124]
		lbl_outSection.pos     = [85, cY + 146]
		texOverwriteBLPChk.pos = [x, cY + 164]

		-- Footer
		local fY = tabY + tabH + 18
		grp_extents.pos          = [6,  fY]
		extentsTypeRdo.pos       = [x,  fY + 16]
		lbl_extPrec.pos          = [x + 10, fY + 38]
		extentsPrecisionSpn.pos  = [160, fY + 36]
		grp_status.pos           = [6,  fY + 64]
		statusPnl.pos            = [18, fY + 82]
		statusLbl.pos            = [95, fY + 110]
		statusFixAllBtn.pos      = [x,  fY + 128]
		statusShowProblemsBtn.pos = [124,fY + 128]
		grp_exportMode.pos       = [6,  fY + 160]
		exportModeRdo.pos        = [x,  fY + 176]
		grp_progress.pos         = [6,  fY + 202]
		grp_progress.height      = 58
		progressPBar.pos         = [x,  fY + 220]
		progressLbl.pos          = [100,fY + 242]
		exportBtn.pos            = [7,  fY + 268]
	)


	-- ===========================================
	-- TAB SWITCHING
	-- ===========================================

	fn switchTab tab =
	(
		currentTab = tab
		local s1 = (tab == 1)
		optionsMergeSimilarChk.visible        = s1
		optionsFixNormalsChk.visible          = s1
		lbl_threshold.visible                 = s1
		optionsFixNormalsTresholdSpn.visible   = s1
		optionsExportSmoothgroupsChk.visible  = s1
		optionsFixSharedNormalsChk.visible    = s1
		optionsKeepUnusedBonesHelpersChk.visible = s1
		optionsOpenFolderChk.visible          = s1
		optionsMaterialFixBtn.visible         = s1

		local s2 = (tab == 2)
		texConvertBLPChk.visible   = s2
		lbl_blpSection.visible     = s2
		lbl_compress.visible       = s2
		texCompressionDdl.visible  = s2
		lbl_jpegQual.visible       = s2
		texJpegQualitySpn.visible  = s2
		texDitheringChk.visible    = s2
		texMipmapsChk.visible      = s2
		lbl_outSection.visible     = s2
		texOverwriteBLPChk.visible = s2

		btn_tabOptions.text = if s1 then "[ Options ]" else "Options"
		btn_tabTexture.text = if s2 then "[ Texture Conversion ]" else "Texture Conversion"
	)


	-- ===========================================
	-- SAVE / LOAD SETTINGS
	-- ===========================================

	fn saveSettings =
	(
		setINISetting iniPath "Settings" "Filename" (filenameTxt.text as string)
		setINISetting iniPath "Settings" "FilenameOverwrite" (filenameOverwriteChk.checked as string)
		setINISetting iniPath "Settings" "FilenameAutoIncrement" (filenameAutoIncrementChk.checked as string)
		setINISetting iniPath "Settings" "MergeSimilarMeshes" (optionsMergeSimilarChk.checked as string)
		setINISetting iniPath "Settings" "FixNormals" (optionsFixNormalsChk.checked as string)
		setINISetting iniPath "Settings" "FixSharedNormals" (optionsFixSharedNormalsChk.checked as string)
		setINISetting iniPath "Settings" "Threshold" (optionsFixNormalsTresholdSpn.value as string)
		setINISetting iniPath "Settings" "ExportSmoothgroups" (optionsExportSmoothgroupsChk.checked as string)
		setINISetting iniPath "Settings" "KeepUnusedBonesHelpers" (optionsKeepUnusedBonesHelpersChk.checked as string)
		setINISetting iniPath "Settings" "OpenFolder" (optionsOpenFolderChk.checked as string)
		setINISetting iniPath "Settings" "ExtentsType" (extentsTypeRdo.state as string)
		setINISetting iniPath "Settings" "ExtentsPrecision" (extentsPrecisionSpn.value as string)
		setINISetting iniPath "Settings" "ExportVersion" (exportVersionRdo.state as string)
		-- Texture Conversion settings
		setINISetting iniPath "TextureConversion" "ConvertBLP" (texConvertBLPChk.checked as string)
		setINISetting iniPath "TextureConversion" "Compression" (texCompressionDdl.selection as string)
		setINISetting iniPath "TextureConversion" "JpegQuality" (texJpegQualitySpn.value as string)
		setINISetting iniPath "TextureConversion" "Dithering" (texDitheringChk.checked as string)
		setINISetting iniPath "TextureConversion" "Mipmaps" (texMipmapsChk.checked as string)
		setINISetting iniPath "TextureConversion" "OverwriteBLP" (texOverwriteBLPChk.checked as string)
	)
    
	fn loadSettings =
	(
		local filename = getINISetting iniPath "Settings" "Filename"
		local filenameOverwrite = getINISetting iniPath "Settings" "FilenameOverwrite"
		local filenameAutoIncrement = getINISetting iniPath "Settings" "FilenameAutoIncrement"
		local mergeMeshes = getINISetting iniPath "Settings" "MergeSimilarMeshes"
		local fixNormals = getINISetting iniPath "Settings" "FixNormals"
		local fixSharedNormals = getINISetting iniPath "Settings" "FixSharedNormals"
		local exportSmooth = getINISetting iniPath "Settings" "ExportSmoothgroups"
		local keepUnusedBH = getINISetting iniPath "Settings" "KeepUnusedBonesHelpers"
		local threshold = getINISetting iniPath "Settings" "Threshold"
		local openFolder = getINISetting iniPath "Settings" "OpenFolder"
		local extentsType = getINISetting iniPath "Settings" "ExtentsType"
		local extentsPrecision = getINISetting iniPath "Settings" "ExtentsPrecision"
		local exportVer = getINISetting iniPath "Settings" "ExportVersion"
		
		if filename != "" then filenameTxt.text = filename
		if filenameOverwrite != "" then filenameOverwriteChk.checked = (filenameOverwrite == "true")
		if filenameAutoIncrement != "" then filenameAutoIncrementChk.checked = (filenameAutoIncrement == "true")
		if mergeMeshes != "" then optionsMergeSimilarChk.checked = (mergeMeshes == "true")
		if fixNormals != "" then optionsFixNormalsChk.checked = (fixNormals == "true")
		if fixSharedNormals != "" then optionsFixSharedNormalsChk.checked = (fixSharedNormals == "true")
		if exportSmooth != "" then optionsExportSmoothgroupsChk.checked = (exportSmooth == "true")
		if keepUnusedBH != "" then optionsKeepUnusedBonesHelpersChk.checked = (keepUnusedBH == "true")
		if threshold != "" then optionsFixNormalsTresholdSpn.value = (threshold as float)
		if openFolder != "" then optionsOpenFolderChk.checked = (openFolder == "true")
		if extentsType != "" then extentsTypeRdo.state = (extentsType as integer)
		if extentsPrecision != "" then extentsPrecisionSpn.value = (extentsPrecision as integer)
		if exportVer != "" then exportVersionRdo.state = (exportVer as integer)

		-- Texture Conversion
		local tcConvert = getINISetting iniPath "TextureConversion" "ConvertBLP"
		local tcCompress = getINISetting iniPath "TextureConversion" "Compression"
		local tcJpeg = getINISetting iniPath "TextureConversion" "JpegQuality"
		local tcDither = getINISetting iniPath "TextureConversion" "Dithering"
		local tcMipmap = getINISetting iniPath "TextureConversion" "Mipmaps"
		local tcOverwrite = getINISetting iniPath "TextureConversion" "OverwriteBLP"

		if tcConvert != "" then texConvertBLPChk.checked = (tcConvert == "true")
		if tcCompress != "" then texCompressionDdl.selection = (tcCompress as integer)
		if tcJpeg != "" then texJpegQualitySpn.value = (tcJpeg as integer)
		if tcDither != "" then texDitheringChk.checked = (tcDither == "true")
		if tcMipmap != "" then texMipmapsChk.checked = (tcMipmap == "true")
		if tcOverwrite != "" then texOverwriteBLPChk.checked = (tcOverwrite == "true")
	)


	-- ===========================================
	-- SCENE SCAN
	-- ===========================================

	fn scanScene =
	(
		local problemCount = ::whiteoutDexSceneMonitorInstance.refreshAllProblems()
		::theDuplicates = ::whiteoutDexSceneMonitorInstance.duplicates
		::thePolyProblems = ::whiteoutDexSceneMonitorInstance.polyProblems
		::theBoneProblems = ::whiteoutDexSceneMonitorInstance.boneProblems
		::theMaterialProblems = ::whiteoutDexSceneMonitorInstance.materialProblems
		
		local problemsExist = problemCount > 0
		statusFixAllBtn.enabled = \
		statusShowProblemsBtn.enabled = problemsExist
		statusPnl.BackColor = if problemsExist then dnColorRed else dnColorGreen
		statusLbl.text = if problemsExist then problemCount as string + " problems found!" else (if ::L != undefined then (::L.t "exp_all_clear_lbl") else "All Clear!")
	)


	-- ===========================================
	-- EVENT HANDLERS
	-- ===========================================

	on btn_tabOptions pressed do switchTab 1
	on btn_tabTexture pressed do switchTab 2

	on filenameBrowseBtn pressed do
	(
		local userFilename = getSaveFileName types:"MDX File (*.mdx)|*.mdx|MDL File (*.mdl)|*.mdl"
		if userFilename == undefined then return()
		filenameTxt.text = userFilename
	)

	on exportBtn pressed do
	(
		filenameTxt.text = trimLeft (trimRight filenameTxt.text)
		
		if filenameTxt.text == "" then
		(
			local userFilename = getSaveFileName types:"MDX File (*.mdx)|*.mdx|MDL File (*.mdl)|*.mdl"
			if userFilename == undefined then return()
			filenameTxt.text = userFilename
		)
		else
		(
			if not isDirectoryWriteable (getFilenamePath filenameTxt.text) then
			(
				local msgTitle = if ::L != undefined then (::L.t "exp_invalid_filename_ptitle") else "Invalid Filename"
				local userSelectFilename = queryBox "The selected filename is not valid.\nDo you want to select a new Filename?" title:msgTitle icon:#question
				if not userSelectFilename then return()
				local userFilename = getSaveFileName types:"MDX File (*.mdx)|*.mdx|MDL File (*.mdl)|*.mdl"
				if userFilename == undefined then return()
				filenameTxt.text = userFilename
			)
		)
		
		local finalFilename = filenameTxt.text
		
		if filenameAutoIncrementChk.checked then
		(
			finalFilename = ::getIncrementedFilename filenameTxt.text
		)
		else
		(
			if doesFileExist finalFilename and not filenameOverwriteChk.checked then
			(
				local msgTitle = if ::L != undefined then (::L.t "exp_file_already_exists_ptitle") else "File Exists"
				local userOverwrite = queryBox "The selected filename already exists.\nDo you want to overwrite it?" title:msgTitle icon:#question
				if not userOverwrite then return()
			)
		)
		
		local filenameType = getFilenameType finalFilename
		if filenameType != ".mdl" and filenameType != ".mdx" then finalFilename += ".mdx"

		-- Flush current settings to INI so the native plugin picks them up
		saveSettings()
		setINISetting iniPath "Settings" "ModelName" modelNameTxt.text

		local time1 = timestamp()
		local exportSuccess = false
		progressLbl.text = if ::L != undefined then (try (::L.t "exp_exporting_lbl") catch ("Exporting...")) else "Exporting..."
		progressPBar.value = 0
		
		DisableSceneRedraw()
		try
		(
			exportSuccess = exportFile finalFilename #noPrompt
		)
		catch
		(
			messageBox "Export Failed: Error during export\nPlease open the listener and report the bug"
			format "*** % ***\n" (getCurrentException()) to:listener
		)
		EnableSceneRedraw()
		
		if exportSuccess then
		(
			progressPBar.value = 80
			
			-- BLP texture conversion (post-export, Classic v800 only)
			if exportVersionRdo.state == 1 and texConvertBLPChk.checked then
			(
				progressLbl.text = "Converting Textures..."
				try
				(
					local blpSettings = WhiteoutDexExportSettings()
					blpSettings.texOverwrite = texOverwriteBLPChk.checked
					blpSettings.texCompression = if texCompressionDdl.selection == 2 then "jpeg" else "paletted"
					blpSettings.texJpegQuality = texJpegQualitySpn.value
					blpSettings.texDither = texDitheringChk.checked
					blpSettings.texMipmaps = texMipmapsChk.checked
					convertTexturesToBLP finalFilename blpSettings
				)
				catch (format "WhiteoutDex: BLP conversion warning: %\n" (getCurrentException()))
			)
			
			progressPBar.value = 100
			progressLbl.text = if ::L != undefined then (try (::L.t "exp_done_lbl") catch ("Done")) else "Done"
			
			local elapsed = ((timestamp() - time1) / 1000.0) as string
			messageBox ("Total Time: " + elapsed + "s.") title:"Export Successful"
			
			if optionsOpenFolderChk.checked then
			(
				local exportDir = getFilenamePath finalFilename
				try (shellLaunch "explorer.exe" exportDir) catch()
			)
		)
		else
		(
			progressPBar.value = 0
			progressLbl.text = if ::L != undefined then (try (::L.t "exp_failed_lbl") catch ("Failed")) else "Failed"
			messageBox "Export Failed, check the listener for details."
		)
		
		if filenameAutoIncrementChk.checked then
			format "Exported to: %\n" finalFilename
	)
	
	on statusFixAllBtn pressed do
	(
		::whiteoutDexSceneMonitorInstance.fixAllProblems()
		::theDuplicates = ::whiteoutDexSceneMonitorInstance.duplicates
		::thePolyProblems = ::whiteoutDexSceneMonitorInstance.polyProblems
		::theBoneProblems = ::whiteoutDexSceneMonitorInstance.boneProblems
		::theMaterialProblems = ::whiteoutDexSceneMonitorInstance.materialProblems
		scanScene()
	)
	
	on statusShowProblemsBtn pressed do
	(
		try destroyDialog ProblemDetailsRollout catch()
		mainRoll.visible = false
		createDialog ProblemDetailsRollout \
			style:#(#style_titlebar, #style_border, #style_sysmenu, #style_resizing) \
			modal:true \
			parent:mainroll.hwnd
		mainRoll.visible = true
		scanScene()
	)
	
	on optionsMaterialFixBtn pressed do
		(createDialog materialSettingsDialog modal:true)

	-- Save on change handlers
	on optionsMergeSimilarChk changed state do saveSettings()
	on optionsFixNormalsChk changed state do
	(
		optionsFixNormalsTresholdSpn.enabled = state
		saveSettings()
	)
	on optionsExportSmoothgroupsChk changed state do saveSettings()
	on optionsFixSharedNormalsChk changed state do saveSettings()
	on optionsKeepUnusedBonesHelpersChk changed state do saveSettings()
	on optionsFixNormalsTresholdSpn changed val do saveSettings()
	on optionsOpenFolderChk changed state do saveSettings()

	on filenameAutoIncrementChk changed state do
	(
		filenameOverwriteChk.enabled = not state
		saveSettings()
	)
	on filenameOverwriteChk changed state do
	(
		if state and filenameAutoIncrementChk.checked then
			filenameAutoIncrementChk.checked = false
		saveSettings()
	)

	-- Texture Conversion save on change
	on texConvertBLPChk changed state do saveSettings()
	on texCompressionDdl selected idx do saveSettings()
	on texJpegQualitySpn changed val do saveSettings()
	on texDitheringChk changed state do saveSettings()
	on texMipmapsChk changed state do saveSettings()
	on texOverwriteBLPChk changed state do saveSettings()


	-- ===========================================
	-- ROLLOUT OPEN
	-- ===========================================

	on mainroll open do
	(
		loadSettings()

		-- SceneMonitor initialisieren
		::whiteoutDexSceneMonitorInstance.initialize()
		::theDuplicates = ::whiteoutDexSceneMonitorInstance.duplicates
		::thePolyProblems = ::whiteoutDexSceneMonitorInstance.polyProblems
		::theBoneProblems = ::whiteoutDexSceneMonitorInstance.boneProblems
		::theMaterialProblems = ::whiteoutDexSceneMonitorInstance.materialProblems
		
		scanScene()
		
		optionsFixNormalsTresholdSpn.enabled = optionsFixNormalsChk.checked
		if filenameAutoIncrementChk.checked then
			filenameOverwriteChk.enabled = false
		else
			filenameOverwriteChk.enabled = true

		-- Position berechnen
		local tabY = btn_tabOptions.pos.y + btn_tabOptions.height + 8
		repositionAll tabY
		switchTab 1

		-- ==========================================
		-- LOCALIZATION
		-- ==========================================
		if ::L != undefined then
		(
			-- Title
			mainroll.title = ::L.t "exp_export_settings_title"

			-- Header
			modelNameTxt.tooltip = ::L.t "exp_set_the_name_for_your_exported_tip"
			exportVersionRdo.tooltip = ::L.t "exp_v800_for_classic_wc3_v1200_fo_tip"
			filenameTxt.tooltip = ::L.t "exp_last_export_path_will_be_use_tip"
			filenameBrowseBtn.text = ::L.t "exp__btn"
			filenameBrowseBtn.tooltip = ::L.t "exp_clear_the_quick_export_path_tip"
			filenameOverwriteChk.text = ::L.t "exp_overwrite_without_warning_chk"
			filenameAutoIncrementChk.text = ::L.t "exp_autoincrement_filename_chk"
			filenameAutoIncrementChk.tooltip = ::L.t "exp_automatically_adds_1_2_etc_tip"

			-- Tab 1: Options
			optionsMergeSimilarChk.text = ::L.t "exp_merge_similar_meshes_chk"
			optionsMergeSimilarChk.tooltip = ::L.t "exp_combines_similar_meshes_to_opt_tip"
			optionsFixNormalsChk.text = ::L.t "exp_at_skinning_fix_normals_chk"
			optionsFixNormalsChk.tooltip = ::L.t "exp_recalculates_normals_for_conne_tip"
			lbl_threshold.text = ::L.t "exp_threshold_lbl"
			optionsFixNormalsTresholdSpn.tooltip = ::L.t "exp_distance_threshold_for_vertex_tip"
			optionsExportSmoothgroupsChk.text = ::L.t "exp_export_smoothgroups_chk"
			optionsExportSmoothgroupsChk.tooltip = ::L.t "exp_exports_smoothing_groups_prod_tip"
			optionsFixSharedNormalsChk.text = ::L.t "exp_fix_shared_normals_chk"
			optionsFixSharedNormalsChk.tooltip = ::L.t "exp_averages_normals_of_shared_ver_tip"
			optionsKeepUnusedBonesHelpersChk.text = ::L.t "exp_keep_unused_boneshelpers_chk"
			optionsKeepUnusedBonesHelpersChk.tooltip = ::L.t "exp_exports_all_bones_and_helpers_tip"
			optionsOpenFolderChk.text = ::L.t "exp_open_folder_after_export_chk"
			optionsOpenFolderChk.tooltip = ::L.t "exp_automatically_opens_the_export_tip"
			optionsMaterialFixBtn.text = ::L.t "exp_material_fix_settings_btn"
			optionsMaterialFixBtn.tooltip = ::L.t "exp_configure_default_settings_for_tip"

			-- Tab 2: Texture Conversion
			btn_tabTexture.text = if currentTab == 2 then ("[ " + (::L.t "exp_texture_conversion_tab") + " ]") else (::L.t "exp_texture_conversion_tab")
			btn_tabOptions.text = if currentTab == 1 then ("[ " + (::L.t "exp_options_tab") + " ]") else (::L.t "exp_options_tab")
			texConvertBLPChk.text = ::L.t "exp_convert_textures_to_blp_chk"
			texConvertBLPChk.tooltip = ::L.t "exp_convert_textures_to_blp_tip"
			lbl_blpSection.text = "— " + (::L.t "exp_blp_settings_section") + " —"
			lbl_compress.text = ::L.t "exp_compression_lbl"
			texCompressionDdl.tooltip = ::L.t "exp_compression_tip"
			lbl_jpegQual.text = ::L.t "exp_jpeg_quality_lbl"
			texJpegQualitySpn.tooltip = ::L.t "exp_jpeg_quality_tip"
			texDitheringChk.text = ::L.t "exp_dithering_chk"
			texDitheringChk.tooltip = ::L.t "exp_dithering_tip"
			texMipmapsChk.text = ::L.t "exp_generate_mipmaps_chk"
			texMipmapsChk.tooltip = ::L.t "exp_generate_mipmaps_tip"
			lbl_outSection.text = "— " + (::L.t "exp_output_section") + " —"
			texOverwriteBLPChk.text = ::L.t "exp_overwrite_existing_blp_chk"
			texOverwriteBLPChk.tooltip = ::L.t "exp_overwrite_existing_blp_tip"

			-- Footer
			extentsTypeRdo.tooltip = ::L.t "exp_animation_dependent_adjusts_ba_tip"
			lbl_extPrec.text = ::L.t "exp_extents_precision_lbl"
			statusLbl.tooltip = ::L.t "exp_current_scene_status_message_tip"
			statusFixAllBtn.text = ::L.t "exp_fix_all_btn"
			statusFixAllBtn.tooltip = ::L.t "exp_automatically_fix_all_detected_tip"
			statusShowProblemsBtn.text = ::L.t "exp_show_details_btn"
			statusShowProblemsBtn.tooltip = ::L.t "exp_show_a_detailed_list_of_detect_tip"
			exportModeRdo.tooltip = ::L.t "exp_standard_mode_for_normal_expor_tip"
			progressPBar.tooltip = ::L.t "exp_shows_the_export_progress_tip"
			progressLbl.text = ::L.t "exp_idle_lbl"
			progressLbl.tooltip = ::L.t "exp_current_export_status_tip"
			exportBtn.text = ::L.t "exp_export_btn"
			exportBtn.tooltip = ::L.t "exp_start_the_export_process_with_tip"
		)
	)
	
	on mainroll close do
	(
		saveSettings()
	)
)

	on execute do
	(
		try destroyDialog mainRoll catch()
		CreateDialog mainRoll height:760 modal:true
	)

) -- Ende des macroScript
