/*
	WhiteoutDex_Toolkit-Importer.mcr
	=================================
	Opens a file dialog pre-filtered to MDX/MDL files,
	then imports using the native MDLXImporter.dle plugin.
	
	Compatibility: 3ds Max 2016 - 2027
*/

macroScript WhiteoutDex_Importer
	category:"WhiteoutDex Toolkit"
	toolTip:"Import Warcraft III Model"
	buttonText:"Import MDX/MDL"
(
	on execute do
	(
		-- Find the MDLX importer class dynamically
		local importerClass = undefined
		for c in importerPlugin.classes do
		(
			local cName = c as string
			if (findString cName "MDLX" != undefined) or \
			   (findString cName "Mdlx" != undefined) or \
			   (findString cName "Warcraft" != undefined) then
			(
				importerClass = c
				exit
			)
		)
		
		-- Open file dialog with MDX/MDL filter
		-- Only the filter group names are translated; the extension masks
		-- after each '|' are parsed by Max and stay verbatim.
		local f = getOpenFileName \
			caption:(::WdxL.t "imp_open_model_cap") \
			types:((::WdxL.t "imp_mdx_filter") + " (*.mdx)|*.mdx|" + (::WdxL.t "imp_mdl_filter") + " (*.mdl)|*.mdl|" + (::WdxL.t "bmp_all_files_filter") + " (*.*)|*.*|") \
			historyCategory:"WhiteoutDexImport"
		
		if f != undefined do
		(
			if importerClass != undefined then
				importFile f using:importerClass
			else
				importFile f
		)
	)
)

-- The macroScript name stays WhiteoutDex_ImportCASC: it is the identifier a
-- user's customised toolbars and quad menus refer to, and renaming it would
-- silently drop the action out of every one of them. Only the label changes —
-- and it says "game storages" because this browses a classic MPQ install as
-- readily as a Reforged CASC one (see wdxMBAssetRoots in
-- WhiteoutDexModelBrowser.ms).
macroScript WhiteoutDex_ImportCASC
	category:"WhiteoutDex Toolkit"
	toolTip:"Import a Warcraft III model straight out of the game storages"
	buttonText:"Import from Game Storages"
(
	/* Same import as WhiteoutDex_Importer, but the file comes out of the
	   installed game rather than off disk: the CASC browser picks a model,
	   it is extracted to a temp copy, and that copy goes through the normal
	   importer so its options dialog appears exactly as it always does.

	   The extraction is only the .mdx itself. Textures are not pulled out
	   alongside it - MDLXImporter resolves those through CASC on its own,
	   off the same W3Path the browser used. */
	on execute do
	(
		if ::WhiteoutDexModelBrowser == undefined then
		(
			messageBox ("The WhiteoutDex asset browser is not loaded.\n\n" + "Reinstall WhiteoutDex or check the Listener for script errors.") title:"WhiteoutDex" beep:false
		)
		else
		(
			local picked = ::WhiteoutDexModelBrowser.pickEx mode:#model
			if picked != undefined do
			(
				local f = ::WhiteoutDexModelBrowser.extractToTemp picked[1] picked[2]
				if f != undefined do
				(
					-- Find the MDLX importer class dynamically
					local importerClass = undefined
					for c in importerPlugin.classes do
					(
						local cName = c as string
						if (findString cName "MDLX" != undefined) or (findString cName "Mdlx" != undefined) or (findString cName "Warcraft" != undefined) then
						(
							importerClass = c
							exit
						)
					)

					if importerClass != undefined then
						importFile f using:importerClass
					else
						importFile f
				)
			)
		)
	)
)
