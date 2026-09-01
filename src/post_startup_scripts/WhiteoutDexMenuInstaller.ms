/*
	WhiteoutDexMenuInstaller.ms
	===========================
	Legacy menu installer for Max 2016-2024.
	Loaded via PackageContents.xml only for Max 2016-2024.
	Max 2025+ uses WhiteoutDexMenuCallback.ms instead.
	
	Note: Import/Export are handled natively by MDLXImporter.dle / MDLXExporter.dle
	      and appear automatically under File > Import / File > Export.
*/

fn InstallWhiteoutDexMenu =
(
	fn isMenuInMainBar main ndMenu =
	(
		result = false
		local n = main.numItems()
		for i = 1 to n do
		(
			local it = main.getItem i
			if (it.getSubMenu ()) == ndMenu then result = true
		)
		result
	)
	
	fn cleanMenu m =
	(
		local n = m.numItems()
		for i = n to 1 by -1 do m.removeItemByPosition i
	)
    
	local main = menuMan.getMainMenuBar()
	local whiteoutDexMain = menuMan.findMenu "WhiteoutDex"

	if whiteoutDexMain == undefined then
	(
		whiteoutDexMain = menuMan.createMenu "WhiteoutDex"
		subWhiteoutDex = menuMan.createSubMenuItem "WhiteoutDex" whiteoutDexMain
		main.addItem subWhiteoutDex -1
	)
	else
	(
		if not (isMenuInMainBar main whiteoutDexMain) then
		(
			subWhiteoutDex = menuMan.createSubMenuItem "WhiteoutDex" whiteoutDexMain
			main.addItem subWhiteoutDex -1
		)
	)
	
	local num = whiteoutDexMain.numItems()
	if num != 0 then cleanMenu whiteoutDexMain

	-- Helper: safely add an action item.
	-- The label a menu action shows is the macroscript's buttonText, which is
	-- fixed at registration time and therefore always English. setUseCustomTitle
	-- overrides it per menu item, which is the only hook the legacy menuMan API
	-- gives us for a translated menu; the action keeps its English name in
	-- Customize User Interface, matching how Max handles its own actions.
	fn addAction menu name locKey:unsupplied =
	(
		local item = menuMan.createActionItem name "WhiteoutDex Toolkit"
		if item != undefined then
		(
			if locKey != unsupplied and ::WdxL != undefined do
			(
				try
				(
					item.setTitle (::WdxL.t locKey)
					item.setUseCustomTitle true
				)
				catch ()
			)
			menu.addItem item -1
		)
	)
	fn addSep menu =
	(
		local s = menuMan.createSeparatorItem()
		if s != undefined then menu.addItem s -1
	)
		
	try
	(
		addAction whiteoutDexMain "WhiteoutDex_Importer"          locKey:"menu_importer"
		addAction whiteoutDexMain "WhiteoutDex_ImportCASC"        locKey:"menu_import_casc"
		addAction whiteoutDexMain "WhiteoutDex_Exporter"          locKey:"menu_exporter"
		addSep whiteoutDexMain
		addAction whiteoutDexMain "WhiteoutDex_SequenceManager"   locKey:"menu_sequence_manager"
		addAction whiteoutDexMain "WhiteoutDex_VisibilityKeyer"   locKey:"menu_visibility_keyer"
		addAction whiteoutDexMain "WhiteoutDex_NodeManager"       locKey:"menu_node_manager"
		addSep whiteoutDexMain
		addAction whiteoutDexMain "WhiteoutDex_AnimGeneral"       locKey:"menu_anim_general"
		addAction whiteoutDexMain "WhiteoutDex_AnimControllers"   locKey:"menu_anim_controllers"
		addAction whiteoutDexMain "WhiteoutDex_AnimSkin"          locKey:"menu_anim_skin"
		addAction whiteoutDexMain "WhiteoutDex_AnimScaler"        locKey:"menu_anim_scaler"
		addSep whiteoutDexMain
		addAction whiteoutDexMain "WhiteoutDex_SkinChanger"       locKey:"menu_skin_changer"
		addAction whiteoutDexMain "WhiteoutDex_GridDummy"         locKey:"menu_grid_dummy"
		addAction whiteoutDexMain "WhiteoutDex_ObjectTools"       locKey:"menu_object_tools"
		addAction whiteoutDexMain "WhiteoutDex_ObjectSettings"    locKey:"menu_object_settings"
		addAction whiteoutDexMain "WhiteoutDex_KeyframeOptimizer" locKey:"menu_keyframe_optimizer"
		addAction whiteoutDexMain "WhiteoutDex_CellShadeCreator"  locKey:"menu_cell_shade"
		addSep whiteoutDexMain
		addAction whiteoutDexMain "WhiteoutDex_Renderer"          locKey:"menu_renderer"
		addAction whiteoutDexMain "WhiteoutDex_Settings"          locKey:"menu_settings"
		addSep whiteoutDexMain
		addAction whiteoutDexMain "WhiteoutDex_About"             locKey:"menu_about"
		
		menuMan.updateMenuBar()
	)
	catch print (getCurrentException())
		
	print "WhiteoutDex Menu installed"
)
InstallWhiteoutDexMenu()
