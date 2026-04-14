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
