-------------------------------------------------------------------------------
-- Wc3 Bitmap — FINAL
-- textureMap plugin for 3ds Max (2016 - 2027)
-- Extends Bitmaptexture with replaceUI:false
--
-- Native Bitmaptexture rollouts remain fully functional:
--   Bitmap Parameters  → File picker, preview (viewport updates in real-time)
--   Coordinates        → UV Offset/Tiling/Angle (AutoKey keyframes work natively)
--   Output             → RGB/Alpha curves
--
-- Our custom WC3 rollouts appear additionally:
--   Texture Parameters → Replaceable Texture, Wrap U/V, Sphere EnvMap
--   Prefix Path        → Export path for MDX exporter
--   Info               → Build version
-------------------------------------------------------------------------------

plugin textureMap Wc3Bitmap
name:"Wc3Bitmap"
classID:#(0x3a7c10f1, 0x5e2d4b08)
extends:Bitmaptexture replaceUI:true version:10
(
	local iniPath = (getDir #userScripts) + "\\WhiteoutDexConfig\\MatPrefixPath.ini"
	
	-- Ensures the prefix path INI file exists with defaults
	fn ensurePrefixINI =
	(
		if not (doesFileExist iniPath) do
		(
			makeDir (getFilenamePath iniPath) all:true
			setINISetting iniPath "PathMenu" "Count" "4"
			setINISetting iniPath "PathMenu" "Path1" "Textures\\\\"
			setINISetting iniPath "PathMenu" "Path2" "war3mapImported\\\\"
			setINISetting iniPath "PathMenu" "Path3" "ReplaceableTextures\\\\Shadows\\\\"
			setINISetting iniPath "PathMenu" "Path4" "ReplaceableTextures\\\\Cliff\\\\"
		)
	)
	
	fn openPrefixPathEditor =
	(
		ensurePrefixINI()
		local startInfo = dotNetObject "System.Diagnostics.ProcessStartInfo"
		startInfo.FileName = "notepad.exe"
		startInfo.Arguments = iniPath
		startInfo.UseShellExecute = true
		(dotNetClass "System.Diagnostics.Process").Start(startInfo)
	)
	
	---------------------------------------------------------------------------
	-- PARAMETERS: Texture Properties (TEXS chunk)
	---------------------------------------------------------------------------
	parameters texParams rollout:texParamsRollout
	(
		replaceableId	type:#integer	animatable:false	default:1	ui:ddReplaceableId
		wrapU			type:#boolean	animatable:false	default:false	ui:chkWrapU
		wrapV			type:#boolean	animatable:false	default:false	ui:chkWrapV
		sphereEnvMap	type:#boolean	animatable:false	default:false	ui:chkSphereEnv
		
		on wrapU set val do ( try ( delegate.coords.U_Tile = val ) catch() )
		on wrapV set val do ( try ( delegate.coords.V_Tile = val ) catch() )
		on sphereEnvMap set val do
		(
			try ( delegate.coords.mappingType = if val then 1 else 0 ) catch()
		)
	)
	
	---------------------------------------------------------------------------
	-- PARAMETERS: Prefix Path (export metadata)
	---------------------------------------------------------------------------
	parameters pathParams rollout:prefixPathRollout
	(
		prefixPath	type:#string	animatable:false	default:""	ui:edtPrefixPath
	)
	
	---------------------------------------------------------------------------
	-- ROLLOUT: Texture Parameters
	---------------------------------------------------------------------------
	rollout texParamsRollout "WC3 Texture Parameters"
	(
		label			lblBitmapFile	"Bitmap:" align:#left
		edittext		edtBitmapFile	"" width:300 height:20 align:#center readOnly:true
		button			btnPickBitmap	"Pick Bitmap..." width:300 height:22 align:#center \
			tooltip:"Select a bitmap file"
		button			btnBrowseMPQ	"Open Texture Browser" width:300 height:22 align:#center \
			tooltip:"Open MPQ/CASC Texture Browser"
		label			lblSep0		"" height:2
		dropdownlist	ddReplaceableId	"Replaceable Texture" \
			items:#("Not Used", "Team Color", "Team Glow", "Cliff", "Lordaeron Tree", "Ashenvale Tree", "Barrens Tree", "Northrend Tree", "Mushroom Tree") \
			width:300 align:#center
		label			lblSep1		"" height:2
		checkbox		chkWrapU	"U Tile (Wrap Width)" align:#left across:2
		checkbox		chkWrapV	"V Tile (Wrap Height)" align:#left
		checkbox		chkSphereEnv	"Sphere EnvMap" align:#left
		
		fn updateBitmapDisplay =
		(
			local f = try ( delegate.filename ) catch( "" )
			if f == undefined do f = ""
			edtBitmapFile.text = if f != "" then (filenameFromPath f) else "(none)"
		)
		
		on texParamsRollout open do
		(
			updateBitmapDisplay()
			try ( chkWrapU.checked = delegate.coords.U_Tile ) catch()
			try ( chkWrapV.checked = delegate.coords.V_Tile ) catch()
			try ( chkSphereEnv.checked = (delegate.coords.mappingType == 1) ) catch()
			-- Enable/disable MPQ/CASC browse button
			local browserOK = false
			try (
				if ::NeoDexMPQ != undefined and ::NeoDexMPQ.getDirectory() != "" do browserOK = true
				if not browserOK do (
					local cascIni = getDir #plugcfg + "\\NeoDex_Settings.ini"
					local cascDir = getINISetting cascIni "CASC" "W3Path"
					if cascDir != "" and doesFileExist (cascDir + "\\.build.info") do browserOK = true
				)
			) catch()
			btnBrowseMPQ.enabled = browserOK
		)
		
		on btnBrowseMPQ pressed do
		(
			try (::NeoDexTexBrowserTarget = #(this, "bitmap"); ::NeoDexTexBrowser.show())
			catch (messageBox "Texture Browser not available.\nCheck MPQ/CASC settings." title:"Wc3Bitmap")
		)
		
		on btnPickBitmap pressed do
		(
			local f = getOpenFileName caption:"Select Bitmap" \
				types:"Image Files (*.tga,*.blp,*.dds,*.png,*.jpg,*.bmp,*.tif)|*.tga;*.blp;*.dds;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff|All Files (*.*)|*.*|"
			if f != undefined do
			(
				delegate.filename = f
				updateBitmapDisplay()
			)
		)
	)
	
	---------------------------------------------------------------------------
	-- ROLLOUT: Prefix Path
	---------------------------------------------------------------------------
	rollout prefixPathRollout "Prefix Path"
	(
		global WdxBitmapForMenu
		
		edittext	edtPrefixPath	"" width:300 height:20 align:#center
		button		btnSelPath		"\x25BC  Select Path" width:146 height:18 align:#left across:2 \
			tooltip:"Select prefix path from presets"
		button		btnEditPath		"+  Edit Paths" width:146 height:18 align:#right \
			tooltip:"Edit prefix path presets in notepad"
		
		on edtPrefixPath entered txt do
		( edtPrefixPath.text = substituteString (substituteString txt "\r" "") "\n" "" )
		
		on btnSelPath pressed do
		(
			WdxBitmapForMenu = this
			ensurePrefixINI()
			local q = bit.intAsChar 34
			local PathCount = (getINISetting iniPath "PathMenu" "Count") as integer
			local code = "rcMenu wdxBmpPathMenu\n(\n"
			local lastMenu = (PathCount + 1) as string
			for i = 1 to PathCount do
			(
				local pval = getINISetting iniPath "PathMenu" ("Path" + i as string)
				code += "menuItem mi" + (i as string) + " " + q + pval + q + "\n"
			)
			code += "menuItem mi" + lastMenu + " " + q + "(Clear)" + q + "\n"
			for i = 1 to PathCount do
			(
				local tempstr = getINISetting iniPath "PathMenu" ("Path" + i as string)
				code += "on mi" + (i as string) + " picked do (\n"
				code += "WdxBitmapForMenu.prefixPathRollout.edtPrefixPath.text = " + q + tempstr + q + "\n"
				code += "WdxBitmapForMenu.prefixPath = " + q + tempstr + q + "\n)\n"
			)
			code += "on mi" + lastMenu + " picked do (\n"
			code += "WdxBitmapForMenu.prefixPathRollout.edtPrefixPath.text = " + q + q + "\n"
			code += "WdxBitmapForMenu.prefixPath = " + q + q + "\n)\n"
			code += ")\npopUpMenu wdxBmpPathMenu pos:mouse.screenpos"
			execute code
		)
		
		on btnEditPath pressed do ( openPrefixPathEditor() )
	)
	
	---------------------------------------------------------------------------
	-- ROLLOUT: Info
	---------------------------------------------------------------------------
	rollout infoRollout "Info"
	(
		label	lblName		"Wc3 Bitmap" align:#center
		label	lblBuild	"Build 2.1.0" align:#center
		label	lblInfo		"textureMap Plugin for 3ds Max" align:#center
	)
	
	---------------------------------------------------------------------------
	-- EVENTS
	---------------------------------------------------------------------------
	on create do
	(
		delegate.coords.U_Tile = false
		delegate.coords.V_Tile = false
	)
)
