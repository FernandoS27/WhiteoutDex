/*
	WhiteoutDexGlobals.ms
	================
	Combined global configuration, utilities, material management, and renderer.
	
	Contains:
	  - Global config variables
	  - WhiteoutDexUtils (general utilities: Base64, CLI, HSB color)
	  - Material Manager (team colors, materials, callbacks)
	  - Wc3 Renderer (ribbon rendering, lighting presets)
	  - Custom Attribute definitions & Note Track migration
	
	Load Order: This must be loaded FIRST among all pre-start-up scripts.
*/

/* ============================================================================
   SECTION 0: ERROR REPORTER STUB
   Lightweight fallback if ErrorReporter.ms is not loaded.
   Provides ::ErrorReporter.logError/logWarning/logInfo used by
   SequenceManager, MaterialManager, and other tools.
   ============================================================================ */

if ::ErrorReporter == undefined then
(
    struct ErrorReporterStub
    (
        fn logError category source msg = (format "WhiteoutDex ERROR [%/%]: %\n" category source msg),
        fn logWarning category source msg = (format "WhiteoutDex WARNING [%/%]: %\n" category source msg),
        fn logInfo category source msg = (format "WhiteoutDex INFO [%/%]: %\n" category source msg)
    )
    global ErrorReporter = ErrorReporterStub()
)


/* ============================================================================
   SECTION 1: GLOBAL CONFIG VARIABLES
   ============================================================================ */

-- Skin tools application mode: 1=Replace, 2=Add (default), 3=Subtract
global whiteoutdexSkinToolsApply = 2

-- Native BLP bitmap support (blp.bmi plugin)
-- Detected at startup: if blp.bmi exists in the package, it was loaded 
-- before scripts (plugins parts load before pre-start-up scripts parts).
-- When true, Max can open/save .blp files directly without WhiteoutTexCLI conversion.
global WhiteoutDexHasNativeBLP = false
(
    local bundleRoot = (systemTools.getEnvVariable "APPDATA") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex"
    -- (maxVersion())[8] is only populated on Max 2017+; derive year from
    -- the build number for cross-version compatibility (Max 2016+).
    local maxYear = ((maxVersion())[1] / 1000) + 1998
    local bmiPath = bundleRoot + "\\native plugins\\Max" + (maxYear as string) + "\\blp.bmi"
    local bmiPath2 = (systemTools.getEnvVariable "ALLUSERSPROFILE") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex\\native plugins\\Max" + (maxYear as string) + "\\blp.bmi"
    WhiteoutDexHasNativeBLP = (doesFileExist bmiPath) or (doesFileExist bmiPath2)
    if WhiteoutDexHasNativeBLP then format "WhiteoutDex: Native BLP support detected (blp.bmi)\n"
)


/* ============================================================================
   SECTION 2: WHITEOUTDEX UTILITIES
   General-purpose utility functions: Base64, rounding, file I/O,
   HSB color adjustment, message boxes, CLI execution.
   ============================================================================ */

-- ============================================================================
-- DATA STRUCTS (used by SequenceManager, TeamColorManager etc.)
-- ============================================================================
struct Wc3Extent (
	minExtent,
	maxExtent,
	bound
)

struct Wc3Sequence (
	name, 
	startTime, 
	endTime, 
	loop, 
	rarity, 
	movespeed,
	extents,
	ex,
	sharedGroup
)

struct Wc3Material (
    maxObj,
    layers,
    id,
    tvertexId,
    ex,
    isConstantColor = false,
    isSortPrimsNearZ = false,
    isSortPrimsFarZ = false,
    isFullResolution = false,
    priorityPlane = 0
)

-- ============================================================================
struct WhiteoutDexUtilsStruct
(
	-- ========================================
	-- BASE64 IMAGE FUNCTIONS
	-- ========================================
	
	fn SaveBase64ToImage base64String savePath =
	(
		try
		(
			local bytes = (dotNetClass "System.Convert").FromBase64String(base64String)
			local dir = dotNetClass "System.IO.Directory"
			if not dir.Exists (getFilenamePath savePath) then
				(dir.CreateDirectory (getFilenamePath savePath))
			(dotNetClass "System.IO.File").WriteAllBytes savePath bytes
			return true
		)
		catch
		(
			format "⚠ WhiteoutDex [SaveBase64ToImage]: %\n" (getCurrentException())
			return false
		)
	),
	
	fn LoadBase64ToImage str =
	(
		try
		(
			local ConvertClass = dotNetClass "System.Convert"
			local ImageClass = dotNetClass "System.Drawing.Image"
			local byteArr = ConvertClass.FromBase64String str
			local memstream = dotnetobject "System.IO.MemoryStream" byteArr
			local img = ImageClass.fromstream memstream
			memstream.close()
			return img
		)
		catch
		(
			format "⚠ WhiteoutDex [LoadBase64ToImage]: %\n" (getCurrentException())
			return null
		)
	),
	
	-- ========================================
	-- MATH / ROUNDING
	-- ========================================
	
	fn RoundDecimalDotNet num digits =
	(
		local mathClass = dotNetClass "System.Math"
		local midpoint = dotNetClass "System.MidpointRounding"
		local result = mathClass.Round num digits midpoint.AwayFromZero
		local resultFloat = result * 1.00
		if (mod resultFloat 1.0 == 0.0) then
			(resultFloat as Integer)
		else
			(resultFloat as Float)
	),
	
	-- ========================================
	-- FILE I/O
	-- ========================================
	
	fn format_txt filepath filetext =
	(
		if doesFileExist filepath == true then
		(
			local fin = openfile filepath mode:"r+"
			seek fin #eof
			local txt = filetext + "\n"
			format txt to:fin
			close fin
		)
		else
		(
			local newfile = createFile filepath
			close newfile
			this.format_txt filepath filetext
		)
	),
	
	-- ========================================
	-- HSB COLOR OPERATIONS
	-- ========================================
	
	-- Converts RGB (0-255 per channel) to HSB.
	-- Returns #(hue 0-360, saturation 0-1, brightness 0-1)
	fn rgbToHSB r g b =
	(
		local rn = r / 255.0
		local gn = g / 255.0
		local bn = b / 255.0
		local cmax = amax #(rn, gn, bn)
		local cmin = amin #(rn, gn, bn)
		local delta = cmax - cmin
		local h = 0.0
		if delta > 0.0 do
		(
			case cmax of
			(
				rn: h = (mod ((gn - bn) / delta * 60.0 + 360.0) 360.0)
				gn: h = (mod ((bn - rn) / delta * 60.0 + 120.0) 360.0)
				bn: h = (mod ((rn - gn) / delta * 60.0 + 240.0) 360.0)
			)
		)
		local s = if cmax > 0.0 then (delta / cmax) else 0.0
		#(h, s, cmax)
	),
	
	-- Converts HSB back to a Max color value.
	-- h: 0-360, s: 0-1, v: 0-1
	fn hsbToRGB h s v =
	(
		h = mod h 360.0
		s = amax 0.0 (amin 1.0 s)
		v = amax 0.0 (amin 1.0 v)
		
		local c = v * s
		local x = c * (1.0 - abs (mod (h / 60.0) 2.0 - 1.0))
		local m = v - c
		
		local r = 0.0
		local g = 0.0
		local b = 0.0
		case of
		(
			(h < 60.0):  (r = c; g = x)
			(h < 120.0): (r = x; g = c)
			(h < 180.0): (g = c; b = x)
			(h < 240.0): (g = x; b = c)
			(h < 300.0): (b = c; r = x)
			default:     (b = x; r = c)
		)
		
		color ((r + m) * 255.0 + 0.5) ((g + m) * 255.0 + 0.5) ((b + m) * 255.0 + 0.5)
	),
	
	-- Shifts a color by hue (-180..180), saturation (-100..100) and brightness (-100..100).
	-- Mimics Photoshop-style Hue/Saturation adjustment behavior.
	fn AdjustColor_PS clr hueDelta satDelta valDelta =
	(
		local hsb = this.rgbToHSB clr.r clr.g clr.b
		
		-- Hue: wrap negative deltas into positive range
		local newH = mod (hsb[1] + (if hueDelta < 0 then hueDelta + 360 else hueDelta)) 360.0
		
		-- Saturation: direct offset in 0-1 range
		local newS = amax 0.0 (amin 1.0 (hsb[2] + (satDelta / 100.0)))
		
		-- Saturation also affects brightness slightly (PS behavior)
		local bri = hsb[3]
		if satDelta > 0 do (bri = bri + (1.0 - bri) * (0.5 * (satDelta / 100.0)))
		if satDelta < 0 do (bri = bri - bri * (0.4 * (satDelta / -100.0)))
		
		-- Brightness
		local newV = bri
		if valDelta >= 0 do
		(
			newV = bri + (valDelta / 100.0) * (1.0 - bri)
			newS = newS - newS * (valDelta / 100.0)
		)
		if valDelta < 0 do
		(
			newV = bri + (valDelta / 100.0) * bri
		)
		
		this.hsbToRGB newH newS newV
	),
	
	-- ========================================
	-- UI / DIALOG FUNCTIONS
	-- ========================================
	
	fn ShowMessageBox message title =
	(
		local msgbox = dotnetclass "System.Windows.Forms.MessageBox"
		local msgboxbuttons = dotnetclass "System.Windows.Forms.MessageBoxButtons"
		local msgboxicons = dotnetclass "System.Windows.Forms.MessageBoxIcon"
		msgbox.show message title msgboxbuttons.OK msgboxicons.none
	),
	
	fn RightClickMenu_ChangelName ChangeName tootipName =
	(
		local menuManager = menuMan
		for i = 1 to menuManager.numMenus() do
		(
			local currentMenu = menuManager.getMenu(i)
			for j = 1 to currentMenu.numItems() do
			(
				local menuItem = currentMenu.getItem(j)
				if menuItem.getTitle() == ChangeName then
				(
					if menuItem.getUseCustomTitle() == false do
						(menuItem.setUseCustomTitle true)
					menuItem.setTitle tootipName
				)
			)
		)
	),
	
	-- ========================================
	-- COMMAND LINE / PROCESS EXECUTION
	-- ========================================
	
	fn RunCLI_AutoClose Order =
	(
		local startInfo = dotNetObject "System.Diagnostics.ProcessStartInfo"
		startInfo.FileName = "cmd.exe"
		startInfo.Arguments = "/C " + Order
		startInfo.WindowStyle = (dotNetClass "System.Diagnostics.ProcessWindowStyle").Hidden
		startInfo.CreateNoWindow = true
		startInfo.UseShellExecute = false
		startInfo.RedirectStandardOutput = true
		startInfo.RedirectStandardError = true
		local process = dotNetObject "System.Diagnostics.Process"
		process.StartInfo = startInfo
		process.Start()
		local output = process.StandardOutput.ReadToEnd()
		local error = process.StandardError.ReadToEnd()
		process.WaitForExit()
		#(output, error, process.ExitCode)
	),
	
	fn RunCLI exePath Order =
	(
		local startInfo = dotNetObject "System.Diagnostics.ProcessStartInfo"
		startInfo.FileName = exePath
		startInfo.Arguments = Order
		startInfo.WindowStyle = (dotNetClass "System.Diagnostics.ProcessWindowStyle").Hidden
		startInfo.CreateNoWindow = true
		startInfo.UseShellExecute = false
		startInfo.RedirectStandardOutput = true
		startInfo.RedirectStandardError = true
		local process = dotNetObject "System.Diagnostics.Process"
		process.StartInfo = startInfo
		process.Start()
		local output = process.StandardOutput.ReadToEnd()
		local error = process.StandardError.ReadToEnd()
		process.WaitForExit()
		#(output, error, process.ExitCode)
	),
	
	-- ========================================
	-- LOCALE DETECTION
	-- ========================================
	
	fn GetMaxLanguageIsChina =
	(
		local Str_tmp = getdir #userscripts
		(findString (toUpper Str_tmp) "\\CHS") != undefined
	)
)

global WhiteoutDexUtils = WhiteoutDexUtilsStruct()

-- REQUIRED wrappers: called by Wc3Material.ms as plain function names
fn WdxSaveBase64ToImage base64String savePath = ::WhiteoutDexUtils.SaveBase64ToImage base64String savePath
fn WdxLoadBase64ToImage str = ::WhiteoutDexUtils.LoadBase64ToImage str
fn wdx_format_txt filepath filetext = ::WhiteoutDexUtils.format_txt filepath filetext
fn WdxAdjustColor_PS clr hueDelta satDelta valDelta = ::WhiteoutDexUtils.AdjustColor_PS clr hueDelta satDelta valDelta
fn WdxGetMaxLanguageIsChina = ::WhiteoutDexUtils.GetMaxLanguageIsChina()


/* ============================================================================
   SECTION 3: MATERIAL MANAGER
   Team colors, team glow textures, material lifecycle callbacks.
   ============================================================================ */

struct WhiteoutDexMaterialManagerStruct
(
	public bundleRoot = (systemTools.getEnvVariable "APPDATA") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex",
	public teamGlowTextures = #(),
	public teamColors = #(
		color 255 3 3,      -- 1: Red
		color 0 66 255,     -- 2: Blue
		color 28 230 185,   -- 3: Teal
		color 84 0 129,     -- 4: Purple
		color 255 252 0,    -- 5: Yellow
		color 254 138 14,   -- 6: Orange
		color 32 192 0,     -- 7: Green
		color 229 91 176,   -- 8: Pink
		color 149 150 151,  -- 9: Grey
		color 126 191 241,  -- 10: Light Blue
		color 16 98 70,     -- 11: Dark Green
		color 78 42 4,      -- 12: Brown
		color 40 40 40,     -- 13: Maroon
		color 40 40 40,     -- 14: Navy
		color 40 40 40,     -- 15: Turquoise
		color 40 40 40      -- 16: Violet
	),
	public activeTeamColor = 1,
	public materials = #(),
	
	fn getTeamGlowFiles =
	(
		-- Priority: 1) MPQ archives (Classic v800)  2) CASC storage (Reforged v1200)  3) Local bundled TGAs
		
		-- Try MPQ (Classic v800) - only if a directory is configured in Settings
		if ::WhiteoutDexMPQ != undefined and ::WhiteoutDexMPQ.getDirectory() != "" then
		(
			try
			(
				if not ::WhiteoutDexMPQ.isInitialized then ::WhiteoutDexMPQ.scanAndOpenMPQs()
				if ::WhiteoutDexMPQ.isInitialized then
				(
					local mpqFiles = #()
					for i = 0 to 15 do
					(
						local numStr = if i < 10 then ("0" + i as string) else (i as string)
						local texPath = ::WhiteoutDexMPQ.findTexture ("TeamGlow\\TeamGlow" + numStr)
						if texPath != undefined then append mpqFiles texPath
					)
					if mpqFiles.count == 16 then
					(
						format "WhiteoutDex: TeamGlow loaded from MPQ archives\n"
						return mpqFiles
					)
				)
			) catch ()
		)
		
		-- Try CASC (Reforged v1200) - only if a W3 path is configured in Settings
		if ::WhiteoutDexCASC != undefined and ::WhiteoutDexCASC.w3path != "" then
		(
			try
			(
				if not ::WhiteoutDexCASC.isInitialized then ::WhiteoutDexCASC.init()
				if ::WhiteoutDexCASC.isInitialized then
				(
					local cascFiles = #()
					for i = 0 to 15 do
					(
						local numStr = if i < 10 then ("0" + i as string) else (i as string)
						local texPath = ::WhiteoutDexCASC.findTexture ("TeamGlow\\TeamGlow" + numStr)
						if texPath != undefined then append cascFiles texPath
					)
					if cascFiles.count == 16 then
					(
						format "WhiteoutDex: TeamGlow loaded from CASC storage\n"
						return cascFiles
					)
				)
			) catch ()
		)
		
		-- Fallback: local bundled TGA files
		local dir = this.bundleRoot + "\\maps\\WhiteoutDex\\TeamGlow\\"
		local files = getFiles (dir + "*.tga")
		sort files
		if files.count == 16 then
			format "WhiteoutDex: TeamGlow loaded from local bundle\n"
		files
	),
	
	fn generateTeamGlows =
	(
		local files = this.getTeamGlowFiles()
		this.teamGlowTextures = #()
		if files.count == 16 then
		(
			for f in files do
			(
				local tmp = BitmapTexture()
				tmp.fileName = f
				tmp.coords.V_Tile = false
				tmp.coords.U_Tile = false
				tmp.monoOutput = 1
				append this.teamGlowTextures tmp
			)
		)
		else
			::ErrorReporter.logError "Material" "TeamGlow" "TeamGlow files were not found"
		
		::WhiteoutDexWc3MaterialTeamGlowTextures = this.teamGlowTextures
	),
	
	fn updateTeamColor teamColor =
	(
		if (teamColor > 0 and teamColor < 17) then
		(
			this.activeTeamColor = teamColor
			::WhiteoutDexTeamColor = teamColor
			for m in this.materials do
				m.retexture = m.retexture
		)
	),
	
	fn resetMaterials =
	(
		this.materials = #()
		::WhiteoutDexMaterials = this.materials
	),
	
	fn registerCallbacks =
	(
		callbacks.removeScripts #postSystemStartup id:#WhiteoutDexMaterials
		callbacks.addScript #postSystemStartup "::WdxMaterialManager.generateTeamGlows()" id:#WhiteoutDexMaterials
		
		callbacks.removeScripts #systemPreNew id:#WhiteoutDexMaterials
		callbacks.removeScripts #systemPreReset id:#WhiteoutDexMaterials
		callbacks.removeScripts #filePreOpen id:#WhiteoutDexMaterials
		callbacks.addScript #systemPreNew "::WdxMaterialManager.resetMaterials()" id:#WhiteoutDexMaterials
		callbacks.addScript #systemPreReset "::WdxMaterialManager.resetMaterials()" id:#WhiteoutDexMaterials
		callbacks.addScript #filePreOpen "::WdxMaterialManager.resetMaterials()" id:#WhiteoutDexMaterials
	)
)

global WdxMaterialManager = WhiteoutDexMaterialManagerStruct()
::WdxMaterialManager.registerCallbacks()

-- REQUIRED globals: referenced directly by Wc3Material.ms and TeamColorManager.ms
global WhiteoutDexWc3MaterialTeamGlowTextures = ::WdxMaterialManager.teamGlowTextures
global WhiteoutDexWc3MaterialTeamColors = ::WdxMaterialManager.teamColors
global WhiteoutDexTeamColor = ::WdxMaterialManager.activeTeamColor
global WhiteoutDexMaterials = ::WdxMaterialManager.materials

-- REQUIRED wrapper: called by TeamColorManager.ms
fn WhiteoutDexUpdateTeamColor teamColor = ::WdxMaterialManager.updateTeamColor teamColor


/* ============================================================================
   SECTION 4: WC3 RENDERER
   Ribbon emitter mesh generation, particle picking, lighting presets.
   ============================================================================ */

struct WhiteoutDexRendererStruct
(
	public tmpObjects = #(),
	public lightState = 2,
	public renderUseSkylight = true,
	public ambientLightPresets = #(color 0 0 0, color 255 255 255, color 211 204 127, color 75 92 117),
	public globalLightPresets = #(color 255 255 255, color 0 0 0, color 128 51 44, color 138 163 180),
	public beforeAmbient = color 0 0 0,
	public beforeGlobalTint = color 255 255 255,
	public renderAuto = false,
	
	fn RotatePoint p q =
	(
		local q2 = quat p.x p.y p.z 0
		q2 = q * q2 * (inverse q)
		[q2.x, q2.y, q2.z]
	),
	
	fn round a = (ceil ((floor (a*2))/2) as integer),
	
	fn RibbonGenerateMesh r =
	(
		fn GenerateVertices t r tf rotatePointFn =
		(
			at time t
			(
				local tmp = (tf.frame / framerate)
				local pos = r.pos - [0, 0, r.gravity * tmp * tmp]
				local q = (r.rotation as quat)
				local p2 = [r.above, 0, 0]
				local p1 = [-r.below, 0, 0]
				#(((rotatePointFn p1 q) + pos), ((rotatePointFn p2 q) + pos))
			)
		)
		
		local numPlanes = this.round (r.life * r.emission)
		local step = (framerate / (r.emission as float)) as time
		local start, end
		local vertices = #()
		local tvertices = #()
		local faces = #()
		local face1 = [2, 1, 4]
		local face2 = [3, 4, 1]
		local pos = undefined
		local i = currentTime
		local fnRotate = this.RotatePoint
		
		while (numPlanes != 0) do
		(
			numPlanes -= 1
			at time i if r.visibility then
				if start == undefined then start = i else end = i
			i -= step
		)
		
		if start != undefined then
		(
			while (start >= end) do
			(
				at time start local pos2 = r.pos
				if (pos != pos2) then
				(
					if vertices.count != 0 then
					(
						append faces face1
						append faces face2
						face1 += 2
						face2 += 2
					)
					join vertices (GenerateVertices start r (currentTime - start) fnRotate)
				)
				pos = pos2    -- ← FIX: Position aktualisieren damit der Duplikat-Check greift
				start -= step
			)
			
			local geo = mesh vertices:vertices faces:faces
			setNumTVerts geo vertices.count
			setTVert geo 1 [0, 0, 0]
			setTVert geo 2 [0, 1, 0]
			local j = 3
			local totalPlanes = faces.count / 2
			for i = 1 to totalPlanes do
			(
				local tmp = (i / (totalPlanes as float))
				setTVert geo j [tmp, 0, 0]
				setTVert geo (j+1) [tmp, 1, 0]
				j += 2
			)
			buildTVFaces geo
			
			for i = 1 to faces.count do setTVFace geo i faces[i]
			if r.rmaterial != undefined then
			(
				local rm = r.rmaterial
				local m = Standard()
				m.name = "Tmp: " + r.name
				m.opacity = rm.alpha
				m.twoSided = rm.twosides
				m.useSelfIllumColor = true
				m.selfIllumColor = r.VertexColor
				m.diffuseMap = rm.texture
				if rm.filtermode > 1 then m.opacityMap = rm.texture
				geo.material = m
			)
			geo.visibility = linear_float()
			geo.visibility.controller.value = r.alpha
			geo
		)
		else undefined
	),
	
	fn PickRibbonEmitters =
	(
		local result = #()
		for h in helpers do
			if (not h.isHidden) and (classof h) as string == "BlizRibbon" then
				append result h
		result
	),
	
	fn PickParticleEmitters2 =
	(
		local result = #()
		for h in helpers do
			if (not h.isHidden) and (classof h) == BlizzParticle2 then
				append result h
		result
	),
	
	fn setLightState newState =
	(
		if newState >= 1 and newState <= this.ambientLightPresets.count then
			this.lightState = newState
	),
	
	fn preRenderWc3 =
	(
		this.tmpObjects = #()
		local ribbons = this.PickRibbonEmitters()
		local emitters = this.PickParticleEmitters2()
		for r in ribbons do append this.tmpObjects (this.RibbonGenerateMesh r)
		
		this.beforeAmbient = ambientColor
		this.beforeGlobalTint = lightTintColor
		ambientColor = this.ambientLightPresets[this.lightState]
		lightTintColor = this.globalLightPresets[this.lightState]
	),
	
	fn postRenderWc3 =
	(
		for w in this.tmpObjects where w != undefined do delete w
		this.tmpObjects = #()
		ambientColor = this.beforeAmbient
		lightTintColor = this.beforeGlobalTint
	)
)

global WdxRenderer = WhiteoutDexRendererStruct()
-- Note: No backward-compat wrappers needed - all callers use ::WdxRenderer directly


/* ============================================================================
   SECTION 5: CUSTOM ATTRIBUTE DEFINITIONS & MIGRATION
   Sequence data is stored as Custom Attributes on rootNode instead of Note Tracks.
   This section defines the CA block and provides migration from legacy Note Tracks.
   ============================================================================ */

-- Define the Custom Attribute block for sequence data
global NeoDexSequenceCA = attributes "NeoDexSequenceData"
(
	parameters main
    (
        seqNames     type:#stringTab  tabSizeVariable:true
        startFrames  type:#intTab     tabSizeVariable:true
        endFrames    type:#intTab     tabSizeVariable:true
        nonLooping   type:#boolTab    tabSizeVariable:true
        rarity       type:#floatTab   tabSizeVariable:true
        moveSpeed    type:#floatTab   tabSizeVariable:true
        seqExtents   type:#stringTab  tabSizeVariable:true
        sharedGroup  type:#stringTab  tabSizeVariable:true   -- Reforged v1200
    )
)

struct WhiteoutDexSequenceStorageStruct
(
    -- Check if Custom Attributes are present on rootNode
    fn hasCA =
    (
        for i = 1 to custAttributes.count rootNode do
            if (custAttributes.get rootNode i).name == "NeoDexSequenceData" then return true
        false
    ),
    
    -- Ensure CA block exists on rootNode, create if missing
    fn ensureCA =
    (
        if not (this.hasCA()) then
            custAttributes.add rootNode ::NeoDexSequenceCA
    ),
    
    -- Remove existing CA block (for clean rebuild)
    fn removeCA =
    (
        for i = custAttributes.count rootNode to 1 by -1 do
            if (custAttributes.get rootNode i).name == "NeoDexSequenceData" then
                custAttributes.delete rootNode i
    ),
    
    -- Clear all sequence data from CA without removing the block
    fn clearCA =
    (
        this.ensureCA()
		rootNode.seqNames    = #()
        rootNode.startFrames = #()
        rootNode.endFrames   = #()
        rootNode.nonLooping  = #()
        rootNode.rarity      = #()
        rootNode.moveSpeed   = #()
        rootNode.seqExtents  = #()
        rootNode.sharedGroup = #()   -- Reforged v1200
    ),
    
    -- Check if legacy Note Tracks exist on rootNode
    fn hasLegacyNoteTracks =
    (
        HasNoteTracks rootNode
    ),
    
    -- Migrate legacy Note Track data to Custom Attributes
    -- Returns number of migrated sequences, 0 if nothing to migrate
    -- Handles all edge cases from the old Note Track system:
    --   - Empty value keys (skipped)
    --   - Orphan keys without matching pair (discarded)
    --   - Duplicate sequence names / 4+ keys with same value (split into pairs, renamed)
    --   - Sorting by start time
    fn migrateFromNoteTracks removeTracks:true =
    (
        if not (this.hasLegacyNoteTracks()) then return 0
        
        local nt = GetNoteTrack rootNode 1
        local keys = nt.keys
        
        -- Step 1: Group keys by value (2 keys with same value = 1 sequence)
        local grouped = #()
        local lookup = Dictionary #string
        for k in keys do
        (
            local kVal = k.value
            if kVal == "" then continue
            if hasDictValue lookup kVal then
            (
                local idx = lookup[kVal]
                append grouped[idx] k
            )
            else
            (
                append grouped #(k)
                lookup[kVal] = grouped.count
            )
        )
        
        -- Step 2: Split groups into valid pairs
        -- Groups with 2 keys = 1 sequence (normal case)
        -- Groups with 4 keys = 2 sequences with same name (split + rename)
        -- Groups with 6 keys = 3 sequences, etc.
        -- Groups with odd count: take as many pairs as possible, discard remainder
        local validPairs = #()
        for grp in grouped do
        (
            local pairCount = grp.count / 2
            if pairCount == 0 then continue
            
            -- Sort keys in this group by time so pairing is correct
            fn sortByTime a b = if a.time < b.time then -1 else if a.time > b.time then 1 else 0
            qsort grp sortByTime
            
            -- Extract pairs (key1=start, key2=end)
            for p = 1 to pairCount do
            (
                local k1 = grp[(p - 1) * 2 + 1]
                local k2 = grp[(p - 1) * 2 + 2]
                append validPairs #(k1, k2)
            )
        )
        
        if validPairs.count == 0 then return 0
        
        -- Step 3: Parse all pairs into sequence data
        local migrated = #()  -- Array of #(name, startF, endF, noLoop, rarity, speed)
        for pair in validPairs do
        (
            local key1 = pair[1]
            local key2 = pair[2]
            
            -- Ensure key1 is the earlier one
            if key1.time > key2.time then swap key1 key2
            
            -- Parse note key value format: "Name"\r\nNonLooping\r\nRarity X\r\nMoveSpeed Y
            local data = filterString key1.value "\"\n\t\x0D"
            
            local seqName = if data.count >= 1 then data[1] else "Unknown"
            local noLoop = false
            local rare = 0.0
            local speed = 0.0
            
            for i = 2 to data.count do
            (
                local c = data[i]
                if stricmp c "NonLooping" == 0 then noLoop = true
                if c.count >= 6 and stricmp (substring c 1 6) "Rarity" == 0 then
                    try ( rare = (trimLeft (substring c 7 -1)) as float ) catch()
                if c.count >= 9 and stricmp (substring c 1 9) "MoveSpeed" == 0 then
                    try ( speed = (trimLeft (substring c 10 -1)) as float ) catch()
            )
            
            local startF = key1.time.frame as integer
            local endF   = key2.time.frame as integer
            
            append migrated #(seqName, startF, endF, noLoop, rare, speed)
        )
        
        -- Step 4: Handle duplicate sequence names (rename with _001, _002 suffix)
        local usedNames = #()
        for i = 1 to migrated.count do
        (
            local baseName = migrated[i][1]
            if (findItem usedNames (toLower baseName)) != 0 then
            (
                -- Name already used, find unique suffix
                local counter = 0
                local newName
                do (
                    counter += 1
                    newName = baseName + "_" + formattedPrint counter format:"03d"
                ) while (findItem usedNames (toLower newName)) != 0
                migrated[i][1] = newName
                append usedNames (toLower newName)
            )
            else
            (
                append usedNames (toLower baseName)
            )
        )
        
        -- Step 5: Sort by start frame
        fn sortByStart a b = if a[2] < b[2] then -1 else if a[2] > b[2] then 1 else 0
        qsort migrated sortByStart
        
        -- Step 6: Write to Custom Attributes
        this.removeCA()
        this.ensureCA()
        
        for seq in migrated do
        (
            append rootNode.seqNames     seq[1]
            append rootNode.startFrames  seq[2]
            append rootNode.endFrames    seq[3]
            append rootNode.nonLooping   seq[4]
            append rootNode.rarity       seq[5]
            append rootNode.moveSpeed    seq[6]
			append rootNode.seqExtents  ""
        )
        
        -- Step 7: Remove old note tracks
        if removeTracks then
            while (numNoteTracks rootNode) != 0 do
                deleteNoteTrack rootNode (getNoteTrack rootNode 1)
        
        format "WhiteoutDex: Migrated % sequences from Note Tracks to Custom Attributes\n" migrated.count
        migrated.count
    ),
    
    -- Auto-migration: checks for legacy data and migrates if needed
    -- Called on SequenceManager open and scene load
    fn autoMigrate =
    (
        -- Already has WhiteoutDex CA with data
        if (this.hasCA()) then
        (
            try (if rootNode.seqNames.count > 0 then return false) catch()
        )
        
        -- Try migrating from legacy NoteTracks
        if this.hasLegacyNoteTracks() then
        (
            this.migrateFromNoteTracks removeTracks:true
            return true
        )
        
        -- No data - ensure CA exists
        this.ensureCA()
        false
    )
)

global SequenceStorage = WhiteoutDexSequenceStorageStruct()
