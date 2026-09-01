-------------------------------------------------------------------------------
-- Wc3 Material — FINAL
-- material plugin for 3ds Max (2016 - 2027)
-- Extends Standard material with WC3/Reforged material parameters
-- Works with: Wc3Bitmap (preferred — carries the UV animation) and plain
-- Bitmaptexture (no TXAN authoring UI, UVGen animation still exports)
--
-- Layer animation metadata lives here:
--   Opacity (KMTA) + Controller Type
--   Texture Slot (KMTF) + Coord ID
--   replaceableId (was on Wc3Bitmap, now on this material)
--
-- UV Animation (TXAN) does NOT live here anymore — it moved to the Wc3Bitmap
-- texture plugin (anim_* params shared with the bitmap's own coords). Edit UV
-- animations on the diffuse texture, not on the material.
-------------------------------------------------------------------------------

plugin material Wdx_Wc3Material
name:"Wc3Material"
classID:#(0x4b8e20a3, 0x1f6c3d57)
extends:Standard replaceUI:true version:13
(
	local isLoading = false
	
	---------------------------------------------------------------------------
	-- LOCAL FUNCTIONS (before parameters)
	---------------------------------------------------------------------------
	
	-- Gets the native texture for viewport delegate assignment.
	-- Both plain Bitmaptexture and the Wc3Bitmap wrapper render fine as a
	-- delegate sub-texmap, so pass the texture through unchanged.
	fn getNativeTex tex =
	(
		if tex == undefined do return undefined
		tex
	)

	-- Resolve a texmap to the native Bitmaptexture behind it: a plain
	-- Bitmaptexture passes through, a Wc3Bitmap yields its delegate.
	-- (Scripted plugins do NOT forward unknown property accesses, so
	-- .coords / .fileName / .alphaSource must be reached via .delegate.)
	fn getNativeBitmap tex =
	(
		if tex == undefined do return undefined
		if classof tex == Bitmaptexture do return tex
		local d = try ( tex.delegate ) catch ( undefined )
		if d != undefined and classof d == Bitmaptexture then d else undefined
	)
	
	fn adjustFilterMode val =
	(
		fn cfgOpacity bmpTex mono alpha =
		(
			if classof bmpTex == Bitmaptexture do
			( bmpTex.monoOutput = mono; bmpTex.alphaSource = alpha )
		)
		fn texHasAlpha bmpTex =
		(
			if classof bmpTex != Bitmaptexture do return false
			try (
				local bmp = bmpTex.bitmap
				if bmp == undefined do return false
				local w = bmp.width; local h = bmp.height
				for pt in #([0,0],[w-1,0],[0,h-1],[w-1,h-1],[w/2,h/2]) do
				( local px = (getPixels bmp pt 1)[1]; if px.alpha < 250 do return true )
				return false
			) catch ( return false )
		)
		local gD = color 92 92 92; local gA = color 150 150 150
		if val == 1 then ( delegate.opacityMapEnable = off; delegate.Diffuse = gD; delegate.Ambient = gA; return() )
		local opMap = delegate.opacityMap
		if opMap == undefined then ( delegate.opacityMapEnable = off; return() )
		if val == 2 or val == 3 then
		( delegate.opacityMapEnable = on; delegate.opacityType = 0; delegate.Diffuse = gD; delegate.Ambient = gA; cfgOpacity opMap 1 0 )
		else if val == 4 or val == 5 then
		(
			delegate.opacityMapEnable = on; delegate.opacityType = 0
			delegate.Diffuse = color 0 0 0; delegate.Ambient = color 0 0 0
			if classof opMap == Bitmaptexture do
			( opMap.coords.U_Tile = true; opMap.coords.V_Tile = true
			  if texHasAlpha opMap then cfgOpacity opMap 1 0 else cfgOpacity opMap 0 2 )
		)
		else if val == 6 or val == 7 then
		( delegate.opacityMapEnable = off; delegate.opacityType = 1; delegate.Diffuse = gD; delegate.Ambient = gA )
	)
	
	fn generateOpacity tex =
	(
		local nb = getNativeBitmap tex
		if nb == undefined do return undefined
		local ok = false
		try ( ok = (nb.fileName != undefined and nb.fileName != "") or (nb.bitmap != undefined) ) catch()
		if not ok do return undefined
		nb.alphaSource = 0; nb.monoOutput = 1
		delegate.opacityMap = nb; nb
	)
	
	fn applyFilterMode = ( adjustFilterMode this.filterMode )
	
	fn updateShaderState =
	(
		local isSD = (this.shaderType == 1)
		try (
			this.texMapRollout.mbNormal.enabled		= not isSD
			this.texMapRollout.mbORM.enabled			= not isSD
			this.texMapRollout.mbEmissive.enabled	= not isSD
			this.texMapRollout.mbTeamColor.enabled	= not isSD
			this.texMapRollout.mbEnv.enabled			= not isSD
		) catch()
		try (
			this.reforgedRollout.spnEmGain.enabled		= not isSD
			this.reforgedRollout.spnFrR.enabled			= not isSD
			this.reforgedRollout.spnFrG.enabled			= not isSD
			this.reforgedRollout.spnFrB.enabled			= not isSD
			this.reforgedRollout.spnFrOpacity.enabled	= not isSD
			this.reforgedRollout.spnFrTeamCol.enabled	= not isSD
			this.reforgedRollout.edtShaderPath.enabled	= not isSD
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
	fn switchOpacityController ctrlType =
	(
		try (
			if this.opacity.isAnimated == false do return()
			case ctrlType of
			(
				1: () -- None: leave as is
				2: ( this.opacity.controller = Linear_Float() )
				3: (
					if classof this.opacity.controller != Bezier_Float do
						this.opacity.controller = Bezier_Float()
					setBezierTangents this.opacity.controller #smooth
				)
				4: ( this.opacity.controller = TCB_Float() )
			)
		) catch()
	)
	
	fn getMapName texMap =
	(
		if texMap == undefined do return "(None)"
		local fname = ""
		try ( fname = filenameFromPath texMap.fileName ) catch()
		-- Wc3Bitmap: the filename lives on the delegate
		if fname == "" do ( try ( fname = filenameFromPath texMap.delegate.fileName ) catch() )
		if fname != "" then fname else (classof texMap) as string
	)

	-- Extract the directory prefix from an MDX texture path.
	-- Examples:
	--   "Textures\Foo.blp"          → "Textures\"
	--   "war3mapImported\bar.blp"   → "war3mapImported\"
	--   "foo.blp"                   → ""
	--   "ReplaceableTextures\Team\x.blp" → "ReplaceableTextures\Team\"
	-- Handles both backslash and forward slash; always returns with the
	-- trailing separator included (empty string if no separator present).
	fn extractTexturePrefix mdxPath =
	(
		if mdxPath == undefined or mdxPath == "" then return ""
		local s = mdxPath as string
		-- Find the last path separator (either \ or /)
		local lastSep = 0
		for i = s.count to 1 by -1 do
		(
			local c = s[i]
			if c == "\\" or c == "/" do ( lastSep = i; exit )
		)
		if lastSep == 0 then "" else (substring s 1 lastSep)
	)

	-- Build a full MDX path from (prefix + filename) for export.
	-- Passes through anything already containing a separator unchanged,
	-- so the exporter can call this without worrying about double-prefixing.
	fn buildTexturePath prefix fileName =
	(
		if fileName == undefined or fileName == "" then return ""
		-- If filename already contains a separator, assume it's a full path
		local hasSep = (findString fileName "\\" != undefined) or \
		               (findString fileName "/" != undefined)
		if hasSep then fileName
		else if prefix == undefined or prefix == "" then fileName
		else prefix + fileName
	)
	
	---------------------------------------------------------------------------
	-- PARAMETERS: Material Basic
	---------------------------------------------------------------------------
	parameters matBasic rollout:basicRollout
	(
		shaderType		type:#integer	animatable:false	default:1	ui:ddShader
		filterMode		type:#integer	animatable:false	default:1	ui:ddFilterMode
		twoSided		type:#boolean	animatable:false	default:false	ui:chkTwoSided
		unshaded		type:#boolean	animatable:false	default:false	ui:chkUnshaded
		unfogged		type:#boolean	animatable:false	default:false	ui:chkUnfogged
		constantColor	type:#boolean	animatable:false	default:false	ui:chkConstColor
		fullResolution	type:#boolean	animatable:false	default:false	ui:chkFullRes
		noDepthTest		type:#boolean	animatable:false	default:false	ui:chkNoDTest
		noDepthSet		type:#boolean	animatable:false	default:false	ui:chkNoDSet
		unselectable	type:#boolean	animatable:false	default:false	ui:chkUnsel
		sortOrder		type:#integer	animatable:false	default:1	ui:ddSortOrder
		priorityPlane	type:#integer	animatable:false	default:0	ui:spnPriority
		-- replaceableId: moved here from Wc3Bitmap (1 = Not Used, 2+ = replaceable type)
		replaceableId	type:#integer	animatable:false	default:1	ui:ddReplaceable
		
		on twoSided set val do ( try ( delegate.twoSided = val ) catch() )
		on noDepthTest set val do
		(
			if val then try ( this.basicRollout.ddSortOrder.enabled = true ) catch()
			else ( this.sortOrder = 1; try ( this.basicRollout.ddSortOrder.enabled = false ) catch() )
		)
		on filterMode set val do
		(
			if val == 1 or val == 2 then
			( this.noDepthSet = false; try ( this.basicRollout.chkNoDSet.enabled = false ) catch() )
			else try ( this.basicRollout.chkNoDSet.enabled = true ) catch()
			applyFilterMode()
		)
		on shaderType set val do ( updateShaderState() )
		on unselectable set val do
		(
			try (
				local selMat = medit.GetCurMtl()
				local matObjs = for o in objects where o.material == selMat collect o
				if matObjs.count >= 1 then
					for i in matObjs do ( if val then (setUserProp i "Unselectable" 1) else (setUserProp i "Unselectable" 0) )
				else val = false
			) catch()
		)
	)
	
	---------------------------------------------------------------------------
	-- PARAMETERS: Layer Animation (KMTA + KMTF)
	-- UV Animation (TXAN) moved to the Wc3Bitmap texture plugin.
	---------------------------------------------------------------------------
	parameters layerAnim rollout:layerAnimRollout
	(
		opacity			type:#float		animatable:true		default:100.0	ui:spnOpacity
		opacityCtrl		type:#integer	animatable:false	default:1		ui:ddOpacCtrl
		flipbook		type:#boolean	animatable:false	default:false	ui:chkFlipbook
		coordId			type:#integer	animatable:false	default:-1		ui:spnCoordId

		on opacity set val do ( try ( delegate.opacity = val ) catch() )
		on opacityCtrl set val do ( switchOpacityController val )
	)
	
	---------------------------------------------------------------------------
	-- PARAMETERS: Texture Maps
	---------------------------------------------------------------------------
	parameters texMaps rollout:texMapRollout
	(
		diffuseMap		type:#texturemap	animatable:false	ui:mbDiffuse
		normalMap		type:#texturemap	animatable:false	ui:mbNormal
		ormMap			type:#texturemap	animatable:false	ui:mbORM
		emissiveMap		type:#texturemap	animatable:false	ui:mbEmissive
		teamColorMap	type:#texturemap	animatable:false	ui:mbTeamColor
		environmentMap	type:#texturemap	animatable:false	ui:mbEnv

		-- Texture path prefixes (MDX folder path, stripped of filename).
		-- Populated by the importer from the original MDX texture path, e.g.
		-- "Textures\Foo.blp" → prefix="Textures\", filename stored in the
		-- bitmap. On export the prefix is re-prepended to produce the full
		-- MDX path. User-editable so they can relocate textures between
		-- folders (e.g. "war3mapImported\") without touching the filename.
		diffusePrefix		type:#string	animatable:false	default:""	ui:edtDiffPrefix
		normalPrefix		type:#string	animatable:false	default:""	ui:edtNormPrefix
		ormPrefix			type:#string	animatable:false	default:""	ui:edtORMPrefix
		emissivePrefix		type:#string	animatable:false	default:""	ui:edtEmisPrefix
		teamColorPrefix		type:#string	animatable:false	default:""	ui:edtTeamColPrefix
		environmentPrefix	type:#string	animatable:false	default:""	ui:edtEnvPrefix

		on diffuseMap set val do
		(
			try (
				delegate.diffuseMap = getNativeTex val
				generateOpacity val
				applyFilterMode()
				try ( this.texMapRollout.mbDiffuse.text = getMapName val ) catch()
			) catch()
		)
		on normalMap set val do ( try ( this.texMapRollout.mbNormal.text = getMapName val ) catch() )
		on ormMap set val do ( try ( this.texMapRollout.mbORM.text = getMapName val ) catch() )
		on emissiveMap set val do ( try ( this.texMapRollout.mbEmissive.text = getMapName val ) catch() )
		on teamColorMap set val do ( try ( this.texMapRollout.mbTeamColor.text = getMapName val ) catch() )
		on environmentMap set val do ( try ( this.texMapRollout.mbEnv.text = getMapName val ) catch() )
	)
	
	---------------------------------------------------------------------------
	-- PARAMETERS: Reforged Settings
	---------------------------------------------------------------------------
	parameters reforged rollout:reforgedRollout
	(
		emissiveGain	type:#float		animatable:false	default:1.0		ui:spnEmGain
		fresnelR		type:#float		animatable:false	default:1.0		ui:spnFrR
		fresnelG		type:#float		animatable:false	default:1.0		ui:spnFrG
		fresnelB		type:#float		animatable:false	default:1.0		ui:spnFrB
		fresnelOpacity	type:#float		animatable:false	default:0.0		ui:spnFrOpacity
		fresnelTeamCol	type:#float		animatable:false	default:0.0		ui:spnFrTeamCol
		shaderPath		type:#string	animatable:false	default:""		ui:edtShaderPath
	)
	
	---------------------------------------------------------------------------
	-- ROLLOUT: Material Basic Parameters
	---------------------------------------------------------------------------
	rollout basicRollout "Material Basic Parameters"
	(
		dropdownlist	ddShader		"Shader ........................................................" items:#("SD", "HD", "SD on HD", "Crystal") width:300 align:#center
		dropdownlist	ddFilterMode	"Filter Mode ................................................." items:#("None", "Transparent", "Blend", "Additive", "AddAlpha", "Modulate", "Modulate 2x") width:300 align:#center 
		label			lblSep1		"" height:2
		checkbox	chkTwoSided		"Two Sided"		align:#left across:3
		checkbox	chkUnshaded		"Unshaded"		align:#left
		checkbox	chkUnfogged		"Unfogged"		align:#left
		checkbox	chkConstColor	"Constant Color"	align:#left across:3
		checkbox	chkFullRes		"Full Resolution"	align:#left
		checkbox	chkNoDTest		"No Depth Test"	align:#left
		checkbox	chkNoDSet		"No Depth Set"	align:#left across:3 enabled:false
		checkbox	chkUnsel		"Unselectable"	align:#left
		label		lblPad			"" align:#left
		label		lblSep2		"" height:2
		dropdownlist	ddSortOrder	"Sort Order ..................................................." items:#("Not Used", "Near to far", "Far to near") width:300 align:#center enabled:false
		spinner		spnPriority		"Priority Plane ................." range:[-20,20,0] type:#integer fieldwidth:55 align:#right
		label		lblSep2b	"" height:2
		dropdownlist	ddReplaceable	"Replaceable Texture ................................" items:#("Not Used", "Team Color", "Team Glow", "Cliff", "Lord Cliffington") width:300 align:#center
		
		on basicRollout open do
		(
			try ( delegate.twoSided = this.twoSided ) catch()
			ddSortOrder.enabled = this.noDepthTest
			chkNoDSet.enabled = not (this.filterMode == 1 or this.filterMode == 2)
			updateShaderState()
		)
	)
	
	---------------------------------------------------------------------------
	-- ROLLOUT: Layer Animation (KMTA + KMTF)
	-- UV Animation (TXAN) is edited on the Wc3Bitmap diffuse texture now.
	---------------------------------------------------------------------------
	rollout layerAnimRollout "Layer Animation"
	(
		group "Opacity (KMTA)"
		(
			spinner			spnOpacity	"Opacity .........................." range:[0,100,100] type:#float fieldwidth:55 align:#right
			dropdownlist	ddOpacCtrl	"Controller ......................." items:#("None", "Linear", "Bezier", "Hermite") width:280 align:#center
		)
		group "Texture Slot (KMTF)"
		(
			checkbox	chkFlipbook	"Texture Flipbook (Step)" align:#left
			spinner		spnCoordId	"Coord ID ......................." range:[-1,8,-1] type:#integer fieldwidth:55 align:#right
		)
		label	lblUVHint	"UV Animation (TXAN) is on the Wc3Bitmap texture." align:#left
	)
	
	---------------------------------------------------------------------------
	-- ROLLOUT: Material Texture Maps
	---------------------------------------------------------------------------
	rollout texMapRollout "Material Texture Maps"
	(
		-- Helper: open the WhiteoutDex Texture Browser targeted at a specific slot
		-- Defined first so the `on btnBrw* pressed` handlers can reference it.
		-- `mat` is passed explicitly because `this` inside a local fn is not
		-- guaranteed to resolve to the plugin owner across all MaxScript versions.
		fn openTexBrowser mat slotName =
		(
			-- The browser is the native asset picker now, so what has to be
			-- installed is WhiteoutDexRenderer.dlx, not the old WinForms DLL.
			if ::WhiteoutDexTexBrowser == undefined or not (::WhiteoutDexTexBrowser.isAvailable()) then
			(
				messageBox "WhiteoutDex Texture Browser is not available.\n\nMake sure WhiteoutDexRenderer.dlx is installed and that you have set the Warcraft III path in WhiteoutDex Settings." \
					title:"WhiteoutDex" beep:false
				return undefined
			)
			::WhiteoutDexTexBrowserTarget = #(mat, slotName)
			try (::WhiteoutDexTexBrowser.show()) catch
				(messageBox ("Texture Browser could not be opened:\n" + (getCurrentException())) title:"WhiteoutDex" beep:false)
		)

		label		lblDiff			"Diffuse ............................................................." align:#left height:16
		mapbutton	mbDiffuse		"(None)" width:270 height:20 align:#left across:2
		button		btnBrwDiff		"⚙" width:24 height:20 align:#right tooltip:"Browse via Texture Browser"
		edittext	edtDiffPrefix	"Path Prefix: " width:300 align:#center
		label		lblSep3			"" height:2
		label		lblNorm			"Normal ............................................................" align:#left height:16
		mapbutton	mbNormal		"(None)" width:270 height:20 align:#left across:2
		button		btnBrwNorm		"⚙" width:24 height:20 align:#right tooltip:"Browse via Texture Browser"
		edittext	edtNormPrefix	"Path Prefix: " width:300 align:#center
		label		lblSep4			"" height:2
		label		lblORM			"ORM ................................................................." align:#left height:16
		mapbutton	mbORM			"(None)" width:270 height:20 align:#left across:2
		button		btnBrwORM		"⚙" width:24 height:20 align:#right tooltip:"Browse via Texture Browser"
		edittext	edtORMPrefix	"Path Prefix: " width:300 align:#center
		label		lblSep5			"" height:2
		label		lblEmis			"Emissive ........................................................." align:#left height:16
		mapbutton	mbEmissive		"(None)" width:270 height:20 align:#left across:2
		button		btnBrwEmis		"⚙" width:24 height:20 align:#right tooltip:"Browse via Texture Browser"
		edittext	edtEmisPrefix	"Path Prefix: " width:300 align:#center
		label		lblSepTC		"" height:2
		label		lblTeamCol		"Team Color (HD, optional) ............................" align:#left height:16
		mapbutton	mbTeamColor		"(None)" width:270 height:20 align:#left across:2
		button		btnBrwTeamCol	"⚙" width:24 height:20 align:#right tooltip:"Browse via Texture Browser"
		edittext	edtTeamColPrefix "Path Prefix: " width:300 align:#center
		label		lblSep6			"" height:2
		label		lblEnv			"Environment ................................................." align:#left height:16
		mapbutton	mbEnv			"(None)" width:270 height:20 align:#left across:2
		button		btnBrwEnv		"⚙" width:24 height:20 align:#right tooltip:"Browse via Texture Browser"
		edittext	edtEnvPrefix	"Path Prefix: " width:300 align:#center

		-- Browser-button handlers: open Texture Browser pre-targeted to this material slot
		on btnBrwDiff    pressed do openTexBrowser this "diffuse"
		on btnBrwNorm    pressed do openTexBrowser this "normal"
		on btnBrwORM     pressed do openTexBrowser this "orm"
		on btnBrwEmis    pressed do openTexBrowser this "emissive"
		on btnBrwTeamCol pressed do openTexBrowser this "team_color"
		on btnBrwEnv     pressed do openTexBrowser this "environment"

		on texMapRollout open do
		(
			mbDiffuse.text		= getMapName this.diffuseMap
			mbNormal.text		= getMapName this.normalMap
			mbORM.text			= getMapName this.ormMap
			mbEmissive.text		= getMapName this.emissiveMap
			mbTeamColor.text	= getMapName this.teamColorMap
			mbEnv.text			= getMapName this.environmentMap
			updateShaderState()
		)
	)
	
	---------------------------------------------------------------------------
	-- ROLLOUT: Reforged Settings
	---------------------------------------------------------------------------
	rollout reforgedRollout "Reforged Settings"
	(
		spinner		spnEmGain		"Emissive Gain ............." range:[0,100,1.0] type:#float scale:0.1 fieldwidth:55 align:#right
		label		lblSep7		"" height:2
		spinner		spnFrR			"Fresnel Color R ............" range:[0,1,1.0] type:#float scale:0.01 fieldwidth:55 align:#right
		spinner		spnFrG			"Fresnel Color G ............" range:[0,1,1.0] type:#float scale:0.01 fieldwidth:55 align:#right
		spinner		spnFrB			"Fresnel Color B ............" range:[0,1,1.0] type:#float scale:0.01 fieldwidth:55 align:#right
		spinner		spnFrOpacity	"Fresnel Opacity ............" range:[0,1,0.0] type:#float scale:0.01 fieldwidth:55 align:#right
		spinner		spnFrTeamCol	"Fresnel Team Color ...." range:[0,1,0.0] type:#float scale:0.01 fieldwidth:55 align:#right
		label		lblSep8		"" height:2
		edittext	edtShaderPath	"Shader Path:" width:300 height:20 align:#center
		
		on reforgedRollout open do ( updateShaderState() )
	)
	
	---------------------------------------------------------------------------
	-- ROLLOUT: Info
	---------------------------------------------------------------------------
	rollout infoRollout "Info"
	(
		label	lblName		"Wc3 Material" align:#center
		label	lblBuild	"Build 2.3.0" align:#center
		label	lblInfo		"Material Plugin for 3ds Max" align:#center
	)
	
	---------------------------------------------------------------------------
	-- EVENTS
	---------------------------------------------------------------------------
	on create do ( delegate.shaderType = 1 )
	on load do ( isLoading = true; isLoading = false )
)
