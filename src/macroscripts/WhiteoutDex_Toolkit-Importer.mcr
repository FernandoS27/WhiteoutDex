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
		local f = getOpenFileName \
			caption:"Import Warcraft III Model" \
			types:"Warcraft III Model (*.mdx)|*.mdx|Warcraft III Text Model (*.mdl)|*.mdl|All Files (*.*)|*.*|" \
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

macroScript WhiteoutDex_ImportCASC
	category:"WhiteoutDex Toolkit"
	toolTip:"Import a Warcraft III model straight out of the game archives"
	buttonText:"Import from CASC"
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
