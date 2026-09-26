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

	-- The UV channels the TXAN controller type applies to.
	fn uvAnimProps = #(#anim_UOffset, #anim_VOffset, #anim_WOffset, #anim_WAngle, #anim_UTiling, #anim_VTiling)

	-- The keyed controller of a UV channel, or undefined.
	fn keyedUVController prop =
	(
		local c = try ( getPropertyController this prop ) catch ( undefined )
		if c != undefined and (try ( numKeys c ) catch ( 0 )) > 0 then c else undefined
	)

	-- A bezier_float whose keys all hold (STEP out-tangents) is how
	-- DontInterp is stored; the importer writes it and the exporter reads it.
	fn isStepBezier ctrl =
	(
		if classof ctrl != Bezier_Float or ctrl.keys.count == 0 do return false
		for i = 1 to ctrl.keys.count do
			if ctrl.keys[i].outTangentType != #step do return false
		true
	)

	fn setBezierTangents ctrl tangentType =
	(
		for i = 1 to ctrl.keys.count do
		( ctrl.keys[i].inTangentType = tangentType; ctrl.keys[i].outTangentType = tangentType )
	)

	-- Dropdown index (1=None, 2=Linear, 3=Bezier, 4=Hermite) of the first
	-- keyed UV channel, or 0 when no channel is keyed.
	fn detectUVInterp =
	(
		for p in uvAnimProps() do
		(
			local c = keyedUVController p
			if c != undefined do
				return (if isStepBezier c then 1
				        else if classof c == Linear_Float then 2
				        else if classof c == TCB_Float then 4
				        else 3)
		)
		0
	)

	-- Converts every keyed UV channel to the picked interpolation. Called only
	-- from the dropdown's selected event: uvCtrlType's set handler also runs
	-- on create and load (MAXScript Help, "Scripted Plug-in Clauses"), where
	-- the keys must never change. Assigning a new controller class to a
	-- keyed param keeps the key times and values; a bezier that already has
	-- its own tangents keeps them.
	fn switchUVAnimController ctrlType =
	(
		for p in uvAnimProps() do
		(
			local c = keyedUVController p
			if c != undefined do try (
				case ctrlType of
				(
					1: ( -- None (DontInterp): hold every key
						if classof c != Bezier_Float do
						( setPropertyController this p (Bezier_Float()); c = getPropertyController this p )
						setBezierTangents c #step
					)
					2: ( if classof c != Linear_Float do setPropertyController this p (Linear_Float()) )
					3: ( -- Bezier: smooth tangents only for keys that had none
						local fresh = (classof c != Bezier_Float) or (isStepBezier c)
						if classof c != Bezier_Float do
						( setPropertyController this p (Bezier_Float()); c = getPropertyController this p )
						if fresh do setBezierTangents c #smooth
					)
					4: ( if classof c != TCB_Float do setPropertyController this p (TCB_Float()) )
				)
			) catch()
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

		-- uvCtrlType has no set handler: it would also run on create and load.
		-- The dropdown's selected event converts the keys (uvAnimRollout).
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
			local none = "(none)"
			if ::WdxL != undefined do try ( none = ::WdxL.t "bmp_no_bitmap_lbl" ) catch()
			edtBitmapFile.text = if f != "" then (filenameFromPath f) else none
		)

		on texParamsRollout open do
		(
			-- Localize before updateBitmapDisplay so the empty-slot placeholder
			-- is already the translated one on first paint.
			if ::WdxL != undefined then
			(
				-- ddReplaceableId is `ui:`-bound to replaceableId; assigning
				-- `.items` snaps the control to entry 1, so restore from the
				-- stored parameter rather than from the control.
				local savRepl = this.replaceableId
				texParamsRollout.title	= ::WdxL.t "bmp_tex_params_rollout_title"
				lblBitmapFile.text		= ::WdxL.t "bmp_bitmap_lbl"
				btnPickBitmap.text		= ::WdxL.t "bmp_pick_bitmap_btn"
				btnPickBitmap.tooltip	= ::WdxL.t "bmp_pick_bitmap_tip"
				btnBrowseMPQ.text		= ::WdxL.t "bmp_open_browser_btn"
				btnBrowseMPQ.tooltip	= ::WdxL.t "bmp_open_browser_tip"
				ddReplaceableId.caption	= ::WdxL.t "bmp_replaceable_ddl"
				chkWrapU.text			= ::WdxL.t "bmp_wrap_u_chk"
				chkWrapV.text			= ::WdxL.t "bmp_wrap_v_chk"
				chkSphereEnv.text		= ::WdxL.t "bmp_sphere_env_chk"
				ddReplaceableId.items	= ::WdxL.tList "bmp_replaceable_items"
				ddReplaceableId.selection = savRepl
				this.replaceableId		= savRepl
			)

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
			catch (messageBox ((::WdxL.t "bmp_browser_unavailable_msg") + "\n" + (getCurrentException()) ) title:"Wc3Bitmap")
		)

		on btnPickBitmap pressed do
		(
			-- The type filter's group names are translated; the extension
			-- masks after each '|' are parsed by Max and stay verbatim.
			local f = getOpenFileName caption:(::WdxL.t "bmp_select_bitmap_cap") \
				types:((::WdxL.t "bmp_image_files_filter") + " (*.tga,*.blp,*.dds,*.png,*.jpg,*.bmp,*.tif)|*.tga;*.blp;*.dds;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff|" + (::WdxL.t "bmp_all_files_filter") + " (*.*)|*.*|")
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

			if ::WdxL != undefined then
			(
				local savCtrl = this.uvCtrlType
				uvAnimRollout.title	= ::WdxL.t "bmp_uv_anim_rollout_title"
				ddUVCtrl.caption	= ::WdxL.t "bmp_controller_type_ddl"
				spnUOffset.caption	= ::WdxL.t "bmp_u_offset_spn"
				spnVOffset.caption	= ::WdxL.t "bmp_v_offset_spn"
				spnWOffset.caption	= ::WdxL.t "bmp_w_offset_spn"
				spnWAngle.caption	= ::WdxL.t "bmp_w_angle_spn"
				spnUTiling.caption	= ::WdxL.t "bmp_u_tiling_spn"
				spnVTiling.caption	= ::WdxL.t "bmp_v_tiling_spn"
				ddUVCtrl.items		= ::WdxL.tList "mat_controller_items"
				ddUVCtrl.selection	= savCtrl
				this.uvCtrlType		= savCtrl
			)

			-- Show what the keys are, not what was last picked: tangents can
			-- change in the curve editor without the dropdown knowing.
			local detected = detectUVInterp()
			if detected > 0 and detected != this.uvCtrlType do this.uvCtrlType = detected
		)

		on ddUVCtrl selected i do ( switchUVAnimController i )
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
		
		on prefixPathRollout open do
		(
			if ::WdxL != undefined then
			(
				-- The glyph prefixes ("▼ ", "+ ") are part of the button art,
				-- so they are re-applied here rather than carried in the
				-- catalog, where a translator could lose them.
				prefixPathRollout.title	= ::WdxL.t "bmp_prefix_path_rollout_title"
				btnSelPath.text			= "\x25BC  " + (::WdxL.t "bmp_select_path_btn")
				btnSelPath.tooltip		= ::WdxL.t "bmp_select_path_tip"
				btnEditPath.text		= "+  " + (::WdxL.t "bmp_edit_paths_btn")
				btnEditPath.tooltip		= ::WdxL.t "bmp_edit_paths_tip"
			)
		)

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
			code += "menuItem mi" + lastMenu + " " + q + (::WdxL.t "bmp_clear_path_menu") + q + "\n"
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

		on infoRollout open do
		(
			if ::WdxL != undefined then
			(
				infoRollout.title	= ::WdxL.t "mat_info_rollout_title"
				lblInfo.text		= ::WdxL.t "bmp_info_desc_lbl"
			)
		)
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
