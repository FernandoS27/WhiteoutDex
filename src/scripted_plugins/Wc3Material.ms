-------------------------------------------------------------------------------
-- Wc3 Material — FINAL
-- material plugin for 3ds Max (2016 - 2027)
-- Extends Standard material with WC3/Reforged material parameters
-- Parameters prefixed with  to avoid conflicts with NeoDex
-- Works with: Wc3Bitmap (replaceUI:false) or standard Bitmaptexture
--
-- All animation metadata lives here:
--   Opacity (KMTA) + Controller Type
--   Texture Slot (KMTF) + Coord ID
--   UV Animation (TXAN) + Controller Type + U/V/W spinners
-------------------------------------------------------------------------------

plugin material Wc3Material
name:"Wc3Material"
classID:#(0x4b8e20a3, 0x1f6c3d57)
extends:Standard replaceUI:true version:11
(
	local isLoading = false
	
	---------------------------------------------------------------------------
	-- LOCAL FUNCTIONS (before parameters)
	---------------------------------------------------------------------------
	
	-- Gets UVGen from any texture (Wc3Bitmap or standard Bitmaptexture)
	fn getUVGen tex =
	(
		if tex == undefined do return undefined
		try ( if tex.coords != undefined do return tex.coords ) catch()
		try ( return (getSubAnim tex 1) ) catch()
		undefined
	)
	
	fn adjustFilterMode val =
	(
		fn cfgOpacity bmpTex mono alpha =
		(
			local cl = classof bmpTex
			if cl == Bitmaptexture or cl == Wc3Bitmap do
			( bmpTex.monoOutput = mono; bmpTex.alphaSource = alpha )
		)
		fn texHasAlpha bmpTex =
		(
			local cl = classof bmpTex
			if cl != Bitmaptexture and cl != Wc3Bitmap do return false
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
			local cl = classof opMap
			if cl == Bitmaptexture or cl == Wc3Bitmap do
			( opMap.coords.U_Tile = true; opMap.coords.V_Tile = true
			  if texHasAlpha opMap then cfgOpacity opMap 1 0 else cfgOpacity opMap 0 2 )
		)
		else if val == 6 or val == 7 then
		( delegate.opacityMapEnable = off; delegate.opacityType = 1; delegate.Diffuse = gD; delegate.Ambient = gA )
	)
	
	fn generateOpacity tex =
	(
		local cl = classof tex
		if cl != Bitmaptexture and cl != Wc3Bitmap do return undefined
		local ok = false
		try ( ok = (tex.fileName != undefined and tex.fileName != "") or (tex.bitmap != undefined) ) catch()
		if not ok do return undefined
		tex.alphaSource = 0; tex.monoOutput = 1
		delegate.opacityMap = tex; tex
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
	
	-- Controller sharing: sync UV controllers from diffuse texture to material
	fn materialControllersUpdate =
	(
		if this.diffuseMap == undefined do return false
		try (
			this.anim_UOffset.controller = this.diffuseMap.coords.U_Offset.controller
			this.anim_VOffset.controller = this.diffuseMap.coords.V_Offset.controller
		) catch()
		try ( this.anim_WAngle.controller = this.diffuseMap.coords.W_Angle.controller ) catch()
		try (
			this.anim_UTiling.controller = this.diffuseMap.coords.U_Tiling.controller
			this.anim_VTiling.controller = this.diffuseMap.coords.V_Tiling.controller
		) catch()
	)
	
	fn textureControllersUpdate =
	(
		if this.diffuseMap == undefined do return false
		try (
			this.diffuseMap.coords.U_Offset.controller = this.anim_UOffset.controller
			this.diffuseMap.coords.V_Offset.controller = this.anim_VOffset.controller
		) catch()
		try ( this.diffuseMap.coords.W_Angle.controller = this.anim_WAngle.controller ) catch()
		try (
			this.diffuseMap.coords.U_Tiling.controller = this.anim_UTiling.controller
			this.diffuseMap.coords.V_Tiling.controller = this.anim_VTiling.controller
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
	
	fn switchTexAnimController ctrlType =
	(
		if this.diffuseMap == undefined do return()
		try (
			local hasU = (try(this.diffuseMap.coords.U_Offset.isAnimated != false)catch(false))
			local hasV = (try(this.diffuseMap.coords.V_Offset.isAnimated != false)catch(false))
			if not hasU and not hasV do return()
			case ctrlType of
			(
				1: () -- None
				2: ( -- Linear
					if hasU do ( this.diffuseMap.coords.U_Offset.controller = Linear_Float(); this.anim_UOffset.controller = this.diffuseMap.coords.U_Offset.controller )
					if hasV do ( this.diffuseMap.coords.V_Offset.controller = Linear_Float(); this.anim_VOffset.controller = this.diffuseMap.coords.V_Offset.controller )
				)
				3: ( -- Bezier smooth
					if hasU do (
						if classof this.diffuseMap.coords.U_Offset.controller != Bezier_Float do this.diffuseMap.coords.U_Offset.controller = Bezier_Float()
						setBezierTangents this.diffuseMap.coords.U_Offset.controller #smooth
						this.anim_UOffset.controller = this.diffuseMap.coords.U_Offset.controller
					)
					if hasV do (
						if classof this.diffuseMap.coords.V_Offset.controller != Bezier_Float do this.diffuseMap.coords.V_Offset.controller = Bezier_Float()
						setBezierTangents this.diffuseMap.coords.V_Offset.controller #smooth
						this.anim_VOffset.controller = this.diffuseMap.coords.V_Offset.controller
					)
				)
				4: ( -- Hermite (TCB)
					if hasU do ( this.diffuseMap.coords.U_Offset.controller = TCB_Float(); this.anim_UOffset.controller = this.diffuseMap.coords.U_Offset.controller )
					if hasV do ( this.diffuseMap.coords.V_Offset.controller = TCB_Float(); this.anim_VOffset.controller = this.diffuseMap.coords.V_Offset.controller )
				)
			)
		) catch()
	)
	
	fn getMapName texMap =
	(
		if texMap == undefined do return "(None)"
		local fname = ""
		try ( fname = filenameFromPath texMap.fileName ) catch()
		if fname != "" then fname else (classof texMap) as string
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
	-- PARAMETERS: Layer Animation (KMTA + KMTF + TXAN)
	---------------------------------------------------------------------------
	parameters layerAnim rollout:layerAnimRollout
	(
		opacity			type:#float		animatable:true		default:100.0	ui:spnOpacity
		opacityCtrl		type:#integer	animatable:false	default:1		ui:ddOpacCtrl
		flipbook		type:#boolean	animatable:false	default:false	ui:chkFlipbook
		coordId			type:#integer	animatable:false	default:-1		ui:spnCoordId
		uvCtrlType		type:#integer	animatable:false	default:1		ui:ddUVCtrl
		-- UV tracking params (share controllers with diffuse texture coords)
		anim_UOffset	type:#float		animatable:true		default:0.0		ui:spnUOffset
		anim_VOffset	type:#float		animatable:true		default:0.0		ui:spnVOffset
		anim_WAngle		type:#float		animatable:true		default:0.0		ui:spnWAngle
		anim_UTiling	type:#float		animatable:true		default:1.0		ui:spnUTiling
		anim_VTiling	type:#float		animatable:true		default:1.0		ui:spnVTiling
		
		on opacity set val do ( try ( delegate.opacity = val ) catch() )
		on opacityCtrl set val do ( switchOpacityController val )
		on uvCtrlType set val do ( switchTexAnimController val )
		-- Sync UV values directly to diffuse texture coords
		on anim_UOffset set val do ( textureControllersUpdate() )
		on anim_VOffset set val do ( textureControllersUpdate() )
		on anim_WAngle  set val do ( textureControllersUpdate() )
		on anim_UTiling set val do ()
		on anim_VTiling set val do ()
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
		
		on diffuseMap set val do
		(
			try (
				delegate.diffuseMap = val
				generateOpacity val
				applyFilterMode()
				try ( this.texMapRollout.mbDiffuse.text = getMapName val ) catch()
				-- Sync current UV values from texture to material spinners
				if val != undefined do
				(
					try (
						this.anim_UOffset = val.coords.U_Offset
						this.anim_VOffset = val.coords.V_Offset
						this.anim_WAngle  = val.coords.W_Angle
						this.anim_UTiling = val.coords.U_Tiling
						this.anim_VTiling = val.coords.V_Tiling
					) catch()
				)
				materialControllersUpdate()
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
		
		on basicRollout open do
		(
			try ( delegate.twoSided = this.twoSided ) catch()
			ddSortOrder.enabled = this.noDepthTest
			chkNoDSet.enabled = not (this.filterMode == 1 or this.filterMode == 2)
			updateShaderState()
		)
	)
	
	---------------------------------------------------------------------------
	-- ROLLOUT: Layer Animation (KMTA + KMTF + TXAN)
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
		group "UV Animation (TXAN)"
		(
			dropdownlist	ddUVCtrl	"Controller Type ............" items:#("None", "Linear", "Bezier", "Hermite") width:280 align:#center
			label		lblSepUV	"" height:2
			spinner		spnUOffset	"U Offset .................." range:[-9999,9999,0.0] type:#float scale:0.01 fieldwidth:55 align:#right
			spinner		spnVOffset	"V Offset .................." range:[-9999,9999,0.0] type:#float scale:0.01 fieldwidth:55 align:#right
			spinner		spnWAngle	"W Angle ................." range:[-9999,9999,0.0] type:#float scale:0.1 fieldwidth:55 align:#right
			spinner		spnUTiling	"U Tiling ..................." range:[-9999,9999,1.0] type:#float scale:0.01 fieldwidth:55 align:#right
			spinner		spnVTiling	"V Tiling ..................." range:[-9999,9999,1.0] type:#float scale:0.01 fieldwidth:55 align:#right
		)
	)
	
	---------------------------------------------------------------------------
	-- ROLLOUT: Material Texture Maps
	---------------------------------------------------------------------------
	rollout texMapRollout "Material Texture Maps"
	(
		label		lblDiff		"Diffuse ............................................................." align:#left height:16
		mapbutton	mbDiffuse	"(None)" width:300 height:20 align:#center
		label		lblSep3		"" height:2
		label		lblNorm		"Normal ............................................................" align:#left height:16
		mapbutton	mbNormal	"(None)" width:300 height:20 align:#center
		label		lblSep4		"" height:2
		label		lblORM		"ORM ................................................................." align:#left height:16
		mapbutton	mbORM		"(None)" width:300 height:20 align:#center
		label		lblSep5		"" height:2
		label		lblEmis		"Emissive ........................................................." align:#left height:16
		mapbutton	mbEmissive	"(None)" width:300 height:20 align:#center
		label		lblSepTC	"" height:2
		label		lblTeamCol	"Team Color (HD, optional) ..........................." align:#left height:16
		mapbutton	mbTeamColor	"(None)" width:300 height:20 align:#center
		label		lblSep6		"" height:2
		label		lblEnv		"Environment ................................................." align:#left height:16
		mapbutton	mbEnv		"(None)" width:300 height:20 align:#center

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
		label	lblBuild	"Build 2.1.0" align:#center
		label	lblInfo		"Material Plugin for 3ds Max" align:#center
	)
	
	---------------------------------------------------------------------------
	-- EVENTS
	---------------------------------------------------------------------------
	on create do ( delegate.shaderType = 1 )
	on load do ( isLoading = true; isLoading = false )
)
