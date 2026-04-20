-------------------------------------------------------------------------------
-- WhiteoutDexLoader2016.ms
-- Legacy loader for 3ds Max 2016 & 2017
--
-- Max 2016/2017 does NOT load scripts from PackageContents.xml.
-- Native plugins (.dle, .dlo, .bmi, .dlx) load fine via ApplicationPlugins,
-- but all .ms and .mcr files need to be loaded manually.
--
-- INSTALLATION:
--   Copy this file to:
--   C:\Program Files\Autodesk\3ds Max 2016\scripts\startup\
--   or
--   C:\Program Files\Autodesk\3ds Max 2017\scripts\startup\
--
-- SKIPPED on Max 2016/2017 (require .NET / Max 2022+ features):
--   WhiteoutDexTextureBrowserHelper.ms  (loads .NET DLL for Max 2022+)
--   WhiteoutDexAutoUpdater.ms   (HTTP/.NET features)
--   WhiteoutDexSidebar.ms       (.NET UI controls)
-------------------------------------------------------------------------------

(
	fn wdxLoad filePath =
	(
		if doesFileExist filePath then
		(
			try ( fileIn filePath ) catch ( format "** WDX Loader: ERROR loading %\n" filePath )
		)
		else
			format "** WDX Loader: File not found: %\n" filePath
	)
	
	local sysEnv = dotNetClass "System.Environment"
	local wdxRoot = undefined
	
	-- Try %APPDATA%\Autodesk\ApplicationPlugins\WhiteoutDex
	try
	(
		local roaming = sysEnv.GetEnvironmentVariable "APPDATA"
		if roaming != undefined do
		(
			local testPath = roaming + @"\Autodesk\ApplicationPlugins\WhiteoutDex"
			if doesFileExist testPath do wdxRoot = testPath
		)
	) catch()
	
	-- Try %PROGRAMDATA%\Autodesk\ApplicationPlugins\WhiteoutDex
	if wdxRoot == undefined do
	(
		try
		(
			local pd = sysEnv.GetEnvironmentVariable "PROGRAMDATA"
			if pd != undefined do
			(
				local testPath = pd + @"\Autodesk\ApplicationPlugins\WhiteoutDex"
				if doesFileExist testPath do wdxRoot = testPath
			)
		) catch()
	)
	
	if wdxRoot == undefined then
	(
		format "** WDX Loader: WhiteoutDex folder not found in ApplicationPlugins. Aborting.\n"
	)
	else
	(
		format "** WDX Loader: Loading from %\n" wdxRoot
		
		local pre  = wdxRoot + @"\pre_startup_scripts\"
		local sp   = wdxRoot + @"\scripted_plugins\"
		local mcr  = wdxRoot + @"\macroscripts\"
		local post = wdxRoot + @"\post_startup_scripts\"
		
		-----------------------------------------------------------------------
		-- 1) Pre-Startup Scripts (order matters!)
		-----------------------------------------------------------------------
		wdxLoad (pre + "WhiteoutDexGlobals.ms")
		wdxLoad (pre + "WhiteoutDexLocalization.ms")
		-- SKIP: WhiteoutDexTextureBrowserHelper.ms (needs .NET DLL, Max 2022+ only)
		wdxLoad (pre + "WhiteoutDexUtility.ms")
		
		-----------------------------------------------------------------------
		-- 2) Scripted Plugins
		-----------------------------------------------------------------------
		wdxLoad (sp + "Wc3Material.ms")
		wdxLoad (sp + "Wc3Bitmap.ms")
		wdxLoad (sp + "Wc3AttachPoint.ms")
		wdxLoad (sp + "Wc3Light.ms")
		wdxLoad (sp + "Wc3RefEvent.ms")
		wdxLoad (sp + "Wc3CollisionShapes.ms")
		wdxLoad (sp + "Wc3VertexColor.ms")
		wdxLoad (sp + "Popcorn.ms")
		wdxLoad (sp + "FaceFX.ms")
		
		-----------------------------------------------------------------------
		-- 3) MacroScripts
		-----------------------------------------------------------------------
		wdxLoad (mcr + "WhiteoutDex_Toolkit-Importer.mcr")
		wdxLoad (mcr + "WhiteoutDex_Toolkit-Exporter.mcr")
		wdxLoad (mcr + "WhiteoutDex_Toolkit-Settings.mcr")
		wdxLoad (mcr + "WhiteoutDex_Toolkit-About.mcr")
		wdxLoad (mcr + "WhiteoutDex_Toolkit-Tools.mcr")
		
		-----------------------------------------------------------------------
		-- 4) Post-Startup Scripts
		-----------------------------------------------------------------------
		wdxLoad (post + "AnimTools.ms")
		wdxLoad (post + "GlobalSequenceManager.ms")
		wdxLoad (post + "GridAndDummyCreator.ms")
		wdxLoad (post + "NodeManager.ms")
		wdxLoad (post + "ObjectManipulationTools.ms")
		wdxLoad (post + "ObjectSettings.ms")
		wdxLoad (post + "SequenceManager.ms")
		wdxLoad (post + "SkinChanger.ms")
		wdxLoad (post + "TeamColorManager.ms")
		wdxLoad (post + "VisibilityKeyer.ms")
		wdxLoad (post + "Wc3AttachPointCallback.ms")
		wdxLoad (post + "Wc3RefEventCallback.ms")
		-- SKIP: WhiteoutDexAutoUpdater.ms  (HTTP/.NET, Max 2018+ only)
		-- SKIP: WhiteoutDexSidebar.ms      (.NET UI controls, Max 2018+ only)
		wdxLoad (post + "WhiteoutDexMenuInstaller.ms")
		
		format "** WDX Loader: Done.\n"
	)
)
