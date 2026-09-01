/*
	WhiteoutDex_Toolkit-Exporter.mcr
	=================================
	Opens a save dialog pre-filtered to MDX/MDL files,
	then exports using the native MDLXExporter.dle plugin.
	
	Compatibility: 3ds Max 2016 - 2027
*/

macroScript WhiteoutDex_Exporter
	category:"WhiteoutDex Toolkit"
	toolTip:"Export Warcraft III Model"
	buttonText:"Export MDX/MDL"
(
	on execute do
	(
		-- Find the MDLX exporter class dynamically
		local exporterClass = undefined
		for c in exporterPlugin.classes do
		(
			local cName = c as string
			if (findString cName "MDLX" != undefined) or \
			   (findString cName "Mdlx" != undefined) or \
			   (findString cName "Warcraft" != undefined) then
			(
				exporterClass = c
				exit
			)
		)
		
		-- Suggest filename based on current scene name
		local seedName = ""
		if maxFileName != "" then
			seedName = (getFilenamePath maxFilePath) + (getFilenameFile maxFileName) + ".mdx"
		
		-- Open save dialog with MDX/MDL filter
		-- Only the filter group names are translated; the extension masks
		-- after each '|' are parsed by Max and stay verbatim.
		local f = getSaveFileName \
			caption:(::WdxL.t "exp_save_model_cap") \
			filename:seedName \
			types:((::WdxL.t "imp_mdx_filter") + " (*.mdx)|*.mdx|" + (::WdxL.t "imp_mdl_filter") + " (*.mdl)|*.mdl|") \
			historyCategory:"WhiteoutDexExport"
		
		if f != undefined do
		(
			if exporterClass != undefined then
				exportFile f using:exporterClass
			else
				exportFile f
		)
	)
)
