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
		
	local importItem = menuMan.createActionItem "WhiteoutDex_Importer" "WhiteoutDex Toolkit"
	local exportItem = menuMan.createActionItem "WhiteoutDex_Exporter" "WhiteoutDex Toolkit"
	local managerItem = menuMan.createActionItem "WhiteoutDex_Manager" "WhiteoutDex Toolkit"
	local settingsItem = menuMan.createActionItem "WhiteoutDex_Settings" "WhiteoutDex Toolkit"
	local aboutItem = menuMan.createActionItem "WhiteoutDex_About" "WhiteoutDex Toolkit"

	local sep1 = menuMan.createSeparatorItem()
	local sep2 = menuMan.createSeparatorItem()
	
	try
	(
		whiteoutDexMain.addItem importItem -1
		whiteoutDexMain.addItem exportItem -1
		whiteoutDexMain.addItem sep1 -1
		whiteoutDexMain.addItem managerItem -1
		whiteoutDexMain.addItem settingsItem -1
		whiteoutDexMain.addItem sep2 -1
		whiteoutDexMain.addItem aboutItem -1
		
		menuMan.updateMenuBar()
	)
	catch print (getCurrentException())
		
	print "WhiteoutDex Menu installed"
)
InstallWhiteoutDexMenu()
