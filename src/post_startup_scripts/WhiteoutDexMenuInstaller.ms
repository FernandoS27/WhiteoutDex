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

	-- Helper: safely add an action item
	fn addAction menu name =
	(
		local item = menuMan.createActionItem name "WhiteoutDex Toolkit"
		if item != undefined then menu.addItem item -1
	)
	fn addSep menu =
	(
		local s = menuMan.createSeparatorItem()
		if s != undefined then menu.addItem s -1
	)
		
	try
	(
		addAction whiteoutDexMain "WhiteoutDex_Importer"
		addAction whiteoutDexMain "WhiteoutDex_Exporter"
		addSep whiteoutDexMain
		addAction whiteoutDexMain "WhiteoutDex_SequenceManager"
		addAction whiteoutDexMain "WhiteoutDex_VisibilityKeyer"
		addAction whiteoutDexMain "WhiteoutDex_NodeManager"
		addSep whiteoutDexMain
		addAction whiteoutDexMain "WhiteoutDex_AnimGeneral"
		addAction whiteoutDexMain "WhiteoutDex_AnimControllers"
		addAction whiteoutDexMain "WhiteoutDex_AnimSkin"
		addAction whiteoutDexMain "WhiteoutDex_AnimScaler"
		addSep whiteoutDexMain
		addAction whiteoutDexMain "WhiteoutDex_SkinChanger"
		addAction whiteoutDexMain "WhiteoutDex_GridDummy"
		addAction whiteoutDexMain "WhiteoutDex_ObjectTools"
		addAction whiteoutDexMain "WhiteoutDex_ObjectSettings"
		addAction whiteoutDexMain "WhiteoutDex_KeyframeOptimizer"
		addAction whiteoutDexMain "WhiteoutDex_CellShadeCreator"
		addSep whiteoutDexMain
		addAction whiteoutDexMain "WhiteoutDex_Renderer"
		addAction whiteoutDexMain "WhiteoutDex_Settings"
		addSep whiteoutDexMain
		addAction whiteoutDexMain "WhiteoutDex_About"
		
		menuMan.updateMenuBar()
	)
	catch print (getCurrentException())
		
	print "WhiteoutDex Menu installed"
)
InstallWhiteoutDexMenu()
