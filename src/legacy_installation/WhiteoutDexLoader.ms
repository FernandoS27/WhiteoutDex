-------------------------------------------------------------------------------
-- WhiteoutDexLoader.ms
-- Startup loader for 3ds Max 2016 & 2017
--
-- Max 2016/2017 load the WhiteoutDex native plugins (.dle, .dlo, .bmi, .dlx)
-- from the ApplicationPlugins bundle like every later version, but they do NOT
-- load scripts from PackageContents.xml. This file loads those instead.
--
-- INSTALLATION:
--   The installer places it in <Max install dir>\scripts\startup\ for every
--   Max 2016/2017 it detects. By hand, copy it to:
--   C:\Program Files\Autodesk\3ds Max 2016\scripts\startup\
--   or
--   C:\Program Files\Autodesk\3ds Max 2017\scripts\startup\
--   (and delete any older WhiteoutDexLoader2016.ms there, or everything loads
--   twice).
--
-- Scripts load in the order PackageContents.xml lists them for Max 2018+:
-- alphabetical within each folder, the menu installer last. New scripts are
-- picked up without touching this file.
--
-- SKIPPED on Max 2016/2017 (require .NET / newer Max features):
--   WhiteoutDexTextureBrowserHelper.ms  (loads the managed texture browser DLL)
--   WhiteoutDexAutoUpdater.ms           (HTTP/.NET features, Max 2018+ only)
--   WhiteoutDexSidebar.ms               (.NET UI controls, Max 2018+ only)
--   WhiteoutDexMenuCallback.ms          (menu API of Max 2025+)
-------------------------------------------------------------------------------

if (maxVersion())[1] >= 20000 then
(
	-- 20000 = Max 2018, which reads the scripts from PackageContents.xml itself.
	format "** WDX Loader: 3ds Max 2018+ loads WhiteoutDex from PackageContents.xml - skipping.\n"
)
else
(
	local skipped = #(
		"whiteoutdextexturebrowserhelper.ms",
		"whiteoutdexautoupdater.ms",
		"whiteoutdexsidebar.ms",
		"whiteoutdexmenucallback.ms"
	)

	fn wdxLoad filePath =
	(
		if doesFileExist filePath then
		(
			try ( fileIn filePath ) catch ( format "** WDX Loader: ERROR loading %\n" filePath )
		)
		else
			format "** WDX Loader: File not found: %\n" filePath
	)

	-- Every file matching pattern in dir, alphabetically, except the names
	-- (lowercase) listed in skip.
	fn wdxLoadFolder dir pattern skip =
	(
		local files = getFiles (dir + pattern)
		sort files
		for f in files where findItem skip (toLower (filenameFromPath f)) == 0 do
			wdxLoad f
	)

	-- The installer writes the machine-wide bundle; a developer deploy
	-- (deploy_appdata) writes the per-user one. A folder only counts once it
	-- holds scripts: upgrading from a per-user install leaves that folder
	-- behind with nothing but maps\ and the settings file in it.
	local sysEnv = dotNetClass "System.Environment"
	local wdxRoot = undefined
	for envVar in #("PROGRAMDATA", "APPDATA") while wdxRoot == undefined do
	(
		local base = try ( sysEnv.GetEnvironmentVariable envVar ) catch ( undefined )
		if base != undefined do
		(
			local candidate = base + @"\Autodesk\ApplicationPlugins\WhiteoutDex"
			if doesFileExist (candidate + @"\scripts\pre_startup_scripts\WhiteoutDexGlobals.ms") do
				wdxRoot = candidate
		)
	)

	if wdxRoot == undefined then
	(
		format "** WDX Loader: WhiteoutDex scripts not found in ApplicationPlugins. Aborting.\n"
	)
	else
	(
		format "** WDX Loader: Loading from %\n" wdxRoot

		local scripts = wdxRoot + @"\scripts\"

		-----------------------------------------------------------------------
		-- 1) Pre-Startup Scripts, then 2) Scripted Plugins - the manifest's
		--    "pre-start-up scripts parts" block, in its order
		-----------------------------------------------------------------------
		wdxLoadFolder (scripts + @"pre_startup_scripts\") "*.ms" skipped
		wdxLoadFolder (scripts + @"scripted_plugins\") "*.ms" skipped

		-----------------------------------------------------------------------
		-- 3) MacroScripts
		-----------------------------------------------------------------------
		wdxLoadFolder (scripts + @"macroscripts\") "*.mcr" skipped

		-----------------------------------------------------------------------
		-- 4) Post-Startup Scripts - the menu installer last, as the manifest
		--    gives it a block of its own after the rest
		-----------------------------------------------------------------------
		wdxLoadFolder (scripts + @"post_startup_scripts\") "*.ms" (skipped + #("whiteoutdexmenuinstaller.ms"))
		wdxLoad (scripts + @"post_startup_scripts\WhiteoutDexMenuInstaller.ms")

		format "** WDX Loader: Done.\n"
	)
)
