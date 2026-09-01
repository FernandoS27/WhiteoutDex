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
--   UV Animation       → TXAN controller type + U/V/W offset, rotation, tiling
--   Prefix Path        → Export path for MDX exporter
--   Info               → Build version
--
-- UV animation (TXAN) lives HERE, on the texture — not on Wc3Material.
-- The anim_* params share their controllers with the delegate Bitmaptexture's
-- own coords (U_Offset/V_Offset/W_Angle/U_Tiling/V_Tiling), so keying either
-- side animates both the viewport and the exportable params. anim_WOffset has
-- no StdUVGen counterpart and exists for MDX round-trip only.
-------------------------------------------------------------------------------

plugin textureMap Wc3Bitmap
name:"Wc3Bitmap"
classID:#(0x3a7c10f1, 0x5e2d4b08)
extends:Bitmaptexture replaceUI:true version:11
(
	local iniPath = (getDir #userScripts) + "\\WhiteoutDexConfig\\MatPrefixPath.ini"

	---------------------------------------------------------------------------
	-- LOCAL FUNCTIONS: UV animation controller sharing
	---------------------------------------------------------------------------

	-- Push the anim_* param controllers down onto the delegate's coords so
	-- the viewport plays the same animation the exporter will read.
	fn pushUVControllersToCoords =
	(
		try ( delegate.coords.U_Offset.controller = this.anim_UOffset.controller ) catch()
		try ( delegate.coords.V_Offset.controller = this.anim_VOffset.controller ) catch()
		try ( delegate.coords.W_Angle.controller  = this.anim_WAngle.controller  ) catch()
		try ( delegate.coords.U_Tiling.controller = this.anim_UTiling.controller ) catch()
		try ( delegate.coords.V_Tiling.controller = this.anim_VTiling.controller ) catch()
	)

	-- Adopt controllers that only exist on the delegate's coords (legacy
	-- scenes where the importer animated the StdUVGen directly). Only pulls
	-- a channel when the param side has no animation of its own.
	fn pullUVControllersFromCoords =
	(
		try (
			if delegate.coords.U_Offset.isAnimated and not this.anim_UOffset.isAnimated do
				this.anim_UOffset.controller = delegate.coords.U_Offset.controller
		) catch()
		try (
			if delegate.coords.V_Offset.isAnimated and not this.anim_VOffset.isAnimated do
				this.anim_VOffset.controller = delegate.coords.V_Offset.controller
		) catch()
		try (
			if delegate.coords.W_Angle.isAnimated and not this.anim_WAngle.isAnimated do
				this.anim_WAngle.controller = delegate.coords.W_Angle.controller
		) catch()
		try (
			if delegate.coords.U_Tiling.isAnimated and not this.anim_UTiling.isAnimated do
				this.anim_UTiling.controller = delegate.coords.U_Tiling.controller
		) catch()
		try (
			if delegate.coords.V_Tiling.isAnimated and not this.anim_VTiling.isAnimated do
				this.anim_VTiling.controller = delegate.coords.V_Tiling.controller
		) catch()
	)

	fn setBezierTangents ctrl tangentType =
	(
		if classof ctrl == Bezier_Float do
			for i = 1 to ctrl.keys.count do
			( ctrl.keys[i].inTangentType = tangentType; ctrl.keys[i].outTangentType = tangentType )
	)

	-- CONTROLLER SWITCHING: actually changes the controller class on the track
	-- ctrlType: 1=None, 2=Linear, 3=Bezier, 4=Hermite
	fn switchUVAnimController ctrlType =
	(
		local hasU = (try(this.anim_UOffset.isAnimated != false)catch(false))
		local hasV = (try(this.anim_VOffset.isAnimated != false)catch(false))
		if not hasU and not hasV do return()
		case ctrlType of
		(
			1: () -- None: leave as is
			2: ( -- Linear
				if hasU do try ( this.anim_UOffset.controller = Linear_Float() ) catch()
				if hasV do try ( this.anim_VOffset.controller = Linear_Float() ) catch()
			)
			3: ( -- Bezier smooth
				if hasU do try (
					if classof this.anim_UOffset.controller != Bezier_Float do
						this.anim_UOffset.controller = Bezier_Float()
					setBezierTangents this.anim_UOffset.controller #smooth
				) catch()
				if hasV do try (
					if classof this.anim_VOffset.controller != Bezier_Float do
						this.anim_VOffset.controller = Bezier_Float()
					setBezierTangents this.anim_VOffset.controller #smooth
				) catch()
			)
			4: ( -- Hermite (TCB)
				if hasU do try ( this.anim_UOffset.controller = TCB_Float() ) catch()
				if hasV do try ( this.anim_VOffset.controller = TCB_Float() ) catch()
			)
		)
		pushUVControllersToCoords()
	)

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
	-- PARAMETERS: UV Animation (TXAN)
	-- Controllers on these params are shared with delegate.coords, so the
	-- viewport and the MDX exporter always see the same animation.
	---------------------------------------------------------------------------
	parameters uvAnim rollout:uvAnimRollout
	(
		uvCtrlType		type:#integer	animatable:false	default:1	ui:ddUVCtrl
		anim_UOffset	type:#float		animatable:true		default:0.0	ui:spnUOffset
		anim_VOffset	type:#float		animatable:true		default:0.0	ui:spnVOffset
		anim_WOffset	type:#float		animatable:true		default:0.0	ui:spnWOffset
		anim_WAngle		type:#float		animatable:true		default:0.0	ui:spnWAngle
		anim_UTiling	type:#float		animatable:true		default:1.0	ui:spnUTiling
		anim_VTiling	type:#float		animatable:true		default:1.0	ui:spnVTiling

		on uvCtrlType set val do ( switchUVAnimController val )
		on anim_UOffset set val do ( try ( delegate.coords.U_Offset = val ) catch(); pushUVControllersToCoords() )
		on anim_VOffset set val do ( try ( delegate.coords.V_Offset = val ) catch(); pushUVControllersToCoords() )
		on anim_WOffset set val do ()   -- StdUVGen has no W_Offset; param is MDX round-trip only
		on anim_WAngle  set val do ( try ( delegate.coords.W_Angle  = val ) catch(); pushUVControllersToCoords() )
		on anim_UTiling set val do ( try ( delegate.coords.U_Tiling = val ) catch(); pushUVControllersToCoords() )
		on anim_VTiling set val do ( try ( delegate.coords.V_Tiling = val ) catch(); pushUVControllersToCoords() )
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
			-- The browse button opens the native texture picker, so what has
			-- to be there is WhiteoutDexRenderer.dlx plus a configured install.
			local browserOK = false
			try (
				browserOK = (::WhiteoutDexTexBrowser != undefined and
				             ::WhiteoutDexTexBrowser.isAvailable())
			) catch()
			btnBrowseMPQ.enabled = browserOK
		)
		
		on btnBrowseMPQ pressed do
		(
			-- pick, not show: show() routes through the slot-target global,
			-- which exists for the material buttons. Here the answer comes
			-- straight back, so the file name label can be refreshed with it.
			try
			(
				local got = ::WhiteoutDexTexBrowser.pick slot:"bitmap"
				if got != undefined do
				(
					delegate.filename = got[1]
					updateBitmapDisplay()
				)
			)
			catch (messageBox ("Texture Browser not available.\n" + (getCurrentException()) ) title:"Wc3Bitmap")
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
	-- ROLLOUT: UV Animation (TXAN)
	---------------------------------------------------------------------------
	rollout uvAnimRollout "UV Animation (TXAN)"
	(
		dropdownlist	ddUVCtrl	"Controller Type ............" items:#("None", "Linear", "Bezier", "Hermite") width:280 align:#center
		label		lblSepUV	"" height:2
		spinner		spnUOffset	"U Offset .................." range:[-9999,9999,0.0] type:#float scale:0.01 fieldwidth:55 align:#right
		spinner		spnVOffset	"V Offset .................." range:[-9999,9999,0.0] type:#float scale:0.01 fieldwidth:55 align:#right
		spinner		spnWOffset	"W Offset ................" range:[-9999,9999,0.0] type:#float scale:0.01 fieldwidth:55 align:#right
		spinner		spnWAngle	"W Angle ................." range:[-9999,9999,0.0] type:#float scale:0.1 fieldwidth:55 align:#right
		spinner		spnUTiling	"U Tiling ..................." range:[-9999,9999,1.0] type:#float scale:0.01 fieldwidth:55 align:#right
		spinner		spnVTiling	"V Tiling ..................." range:[-9999,9999,1.0] type:#float scale:0.01 fieldwidth:55 align:#right

		on uvAnimRollout open do
		(
			-- Adopt animation authored directly on the delegate's coords
			-- (legacy scenes / importer StdUVGen writes) so the spinners and
			-- the exporter see it.
			pullUVControllersFromCoords()
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
		label	lblBuild	"Build 2.2.0" align:#center
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
	-- Legacy scenes may carry UV animation only on the delegate's coords
	-- (pre-2.2.0 imports animated the StdUVGen directly). Adopt those
	-- controllers into the anim_* params on load.
	on load do ( pullUVControllersFromCoords() )
)
