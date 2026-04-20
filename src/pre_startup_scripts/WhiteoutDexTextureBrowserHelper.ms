/*
    WhiteoutDexTextureBrowserHelper.ms
    =====================
    Unified MPQ + CASC helper using WhiteoutDexTextureBrowser.dll
    Replaces: WhiteoutDexMPQHelper.ms, WhiteoutDexCASCHelper.ms
    
    Provides the SAME public API as the old helpers:
        ::WhiteoutDexMPQ   (global, used by SceneRebuilder, Settings, etc.)
        ::WhiteoutDexCASC  (global, used by SceneRebuilder, Settings, etc.)
    
    No more runtime C# compilation — everything runs through the native DLL.
    BLP/DDS decoding happens in native C++ (no WhiteoutTexCLI.exe needed).
    
    Load Order: After WhiteoutDexGlobals.ms (needs ::WhiteoutDexHasNativeBLP)
*/

/* ============================================================================
   SECTION 1: DLL LOADER
   Finds and loads the correct WhiteoutDexTextureBrowser.dll for the running Max version.
   ============================================================================ */

global WhiteoutDexTextureBrowserDLL = undefined
(
    local bundleRoot = (systemTools.getEnvVariable "APPDATA") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex"
    local maxYear = (maxVersion())[8]
    
    -- Max 2026+ uses .NET 8 DLL, Max 2022-2025 uses .NET Framework 4.8.1
    local dllFolder = if maxYear >= 2026 then "Max2026+" else "Max2022-2025"
    local dllPath = bundleRoot + "\\native plugins\\" + dllFolder + "\\WhiteoutDexTextureBrowser.dll"
    
    -- Also check ProgramData as alternate location
    local dllPath2 = (systemTools.getEnvVariable "ALLUSERSPROFILE") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex\\native plugins\\" + dllFolder + "\\WhiteoutDexTextureBrowser.dll"
    
    local finalPath = if doesFileExist dllPath then dllPath
                      else if doesFileExist dllPath2 then dllPath2
                      else undefined
    
    if finalPath != undefined then
    (
        try
        (
            dotNet.loadAssembly finalPath
            WhiteoutDexTextureBrowserDLL = dotNetClass "WhiteoutDex.WhiteoutDexTextureBrowser"
            format "WhiteoutDex: Native DLL loaded from '%'\n" (filenameFromPath finalPath)
        )
        catch
        (
            format "WhiteoutDex WARNING: Failed to load WhiteoutDexTextureBrowser.dll: %\n" (getCurrentException())
            WhiteoutDexTextureBrowserDLL = undefined
        )
    )
    else
    (
        format "WhiteoutDex WARNING: WhiteoutDexTextureBrowser.dll not found.\n"
        format "  Expected at: %\n" dllPath
        format "  MPQ/CASC support will be unavailable.\n"
    )
)


/* ============================================================================
   SECTION 2: MPQ HELPER
   Same API as the old WhiteoutDexMPQHelperStruct — drop-in replacement.
   ============================================================================ */

struct WhiteoutDexMPQHelperStruct
(
    -- Public properties (accessed by SceneRebuilder, Settings, etc.)
    public isInitialized = false,
    public mpqDirectory = "",
    
    -- Private
    private mpqHandles = #(),       -- array of int handles from DLL
    private mdxFileList = #(),
    private textureCache = undefined,
    private tempDir = ((getDir #temp) + "\\WhiteoutDex_MPQ\\"),
    private iniFile = (getDir #plugcfg + "\\WhiteoutDexMPQ.ini"),
    private ndx = ::WhiteoutDexTextureBrowserDLL,
    
    -- ========================================================================
    -- SETTINGS
    -- ========================================================================
    
    public fn loadSettings =
    (
        local val = getINISetting this.iniFile "MPQ" "Directory"
        if val != "" then this.mpqDirectory = val
        format "WhiteoutDex MPQ: Directory = '%'\n" this.mpqDirectory
    ),
    
    public fn saveSettings dir =
    (
        this.mpqDirectory = dir
        setINISetting this.iniFile "MPQ" "Directory" dir
    ),
    
    public fn getDirectory =
    (
        this.mpqDirectory
    ),
    
    -- ========================================================================
    -- MPQ LIFECYCLE
    -- ========================================================================
    
    public fn scanAndOpenMPQs =
    (
        this.closeMPQs()
        
        if this.ndx == undefined then
        (
            format "WhiteoutDex MPQ: Native DLL not loaded.\n"
            return false
        )
        
        if this.mpqDirectory == "" or not (doesFileExist this.mpqDirectory) then
        (
            format "WhiteoutDex MPQ: No valid directory set.\n"
            return false
        )
        
        this.textureCache = Dictionary #string
        makeDir this.tempDir all:true
        
        -- Scan for MPQ files recursively
        local mpqExtensions = #("*.mpq", "*.w3mod", "*.w3x", "*.w3m", "*.mix")
        local mpqFiles = #()
        
        for ext in mpqExtensions do
            join mpqFiles (getFiles (this.mpqDirectory + "\\" + ext))
        
        -- Recursive subdirectory scan
        local dirsToScan = #(this.mpqDirectory)
        while dirsToScan.count > 0 do
        (
            local currentDir = dirsToScan[1]
            deleteItem dirsToScan 1
            local subDirs = getDirectories (currentDir + "\\*")
            for d in subDirs do
            (
                append dirsToScan d
                for ext in mpqExtensions do
                    join mpqFiles (getFiles (d + ext))
            )
        )
        
        if mpqFiles.count == 0 then
        (
            format "WhiteoutDex MPQ: No MPQ files found in '%'\n" this.mpqDirectory
            return false
        )
        
        format "WhiteoutDex MPQ: Found % MPQ files, opening...\n" mpqFiles.count
        
        this.mpqHandles = #()
        for f in mpqFiles do
        (
            try
            (
                local handle = this.ndx.MPQ_Open f
                if handle >= 0 then
                (
                    append this.mpqHandles handle
                    format "  Opened: '%' -> handle %\n" (filenameFromPath f) handle
                )
                else
                    format "  SKIP: '%'\n" (filenameFromPath f)
            )
            catch
            (
                format "  ERROR: '%' - %\n" (filenameFromPath f) (getCurrentException())
            )
        )
        
        this.isInitialized = (this.mpqHandles.count > 0)
        format "WhiteoutDex MPQ: % archives open.\n" this.mpqHandles.count
        this.isInitialized
    ),
    
    public fn closeMPQs =
    (
        if this.ndx != undefined then
            try ( this.ndx.MPQ_CloseAll() ) catch()
        this.mpqHandles = #()
        this.mdxFileList = #()
        this.isInitialized = false
    ),
    
    -- ========================================================================
    -- TEXTURE SEARCH
    -- Returns file path to extracted texture, or undefined.
    -- Uses native BLP/DDS decoding — no WhiteoutTexCLI needed.
    -- ========================================================================
    
    public fn findTexture fname =
    (
        if not this.isInitialized then return undefined
        if fname == "" or fname == undefined then return undefined
        if this.ndx == undefined then return undefined
        
        -- Check cache
        if this.textureCache != undefined and hasDictValue this.textureCache fname then
        (
            local cached = this.textureCache[fname]
            if doesFileExist cached then return cached
        )
        
        -- Check previously extracted files
        local blpCachePath = this.tempDir + fname + ".blp"
        local pngPath = this.tempDir + fname + ".png"
        local tgaPath = this.tempDir + fname + ".tga"
        
        if ::WhiteoutDexHasNativeBLP and doesFileExist blpCachePath then
        (
            if this.textureCache != undefined then this.textureCache[fname] = blpCachePath
            return blpCachePath
        )
        if doesFileExist pngPath then
        (
            if this.textureCache != undefined then this.textureCache[fname] = pngPath
            return pngPath
        )
        if doesFileExist tgaPath then
        (
            if this.textureCache != undefined then this.textureCache[fname] = tgaPath
            return tgaPath
        )
        
        -- Search in all MPQ archives
        local searchPaths = #(
            "Textures\\" + fname + ".blp",
            fname + ".blp",
            "ReplaceableTextures\\" + fname + ".blp",
            "SharedModels\\Textures\\" + fname + ".blp"
        )
        
        local foundHandle = -1
        local foundPath = undefined
        
        for sp in searchPaths while foundHandle < 0 do
        (
            for h in this.mpqHandles while foundHandle < 0 do
            (
                if this.ndx.MPQ_HasFile h sp then
                (
                    foundHandle = h
                    foundPath = sp
                )
            )
        )
        
        if foundHandle < 0 then return undefined
        
        -- Extract BLP to temp
        makeDir this.tempDir all:true
        local success = this.ndx.MPQ_ExtractToDisk foundHandle foundPath blpCachePath
        if not success then
        (
            format "WhiteoutDex MPQ: Failed to extract '%'\n" foundPath
            return undefined
        )
        
        -- Native BLP support: return BLP directly
        if ::WhiteoutDexHasNativeBLP then
        (
            if this.textureCache != undefined then this.textureCache[fname] = blpCachePath
            format "WhiteoutDex MPQ: '%' -> '%' (native BLP)\n" foundPath (filenameFromPath blpCachePath)
            return blpCachePath
        )
        
        -- Decode BLP to PNG via native DLL
        try
        (
            local bmp = this.ndx.DecodeBLP blpCachePath
            if bmp != undefined then
            (
                bmp.Save pngPath ((dotNetClass "System.Drawing.Imaging.ImageFormat").Png)
                bmp.Dispose()
                try ( deleteFile blpCachePath ) catch()
                if this.textureCache != undefined then this.textureCache[fname] = pngPath
                format "WhiteoutDex MPQ: '%' -> '%'\n" foundPath (filenameFromPath pngPath)
                return pngPath
            )
        )
        catch
        (
            format "WhiteoutDex MPQ: BLP decode failed for '%': %\n" foundPath (getCurrentException())
        )
        
        -- Fallback: return raw BLP file
        if doesFileExist blpCachePath then blpCachePath else undefined
    ),
    
    -- ========================================================================
    -- MDX BROWSER
    -- ========================================================================
    
    public fn listMDXFiles =
    (
        if not this.isInitialized then return #()
        if this.ndx == undefined then return #()
        
        this.mdxFileList = #()
        local mdxSet = Dictionary #string
        
        try
        (
            local allFiles = this.ndx.MPQ_ListAllFiles ".mdx"
            if allFiles != undefined then
            (
                for i = 1 to allFiles.count do
                (
                    local f = allFiles[i]
                    local key = toLower f
                    if not (hasDictValue mdxSet key) then
                    (
                        mdxSet[key] = true
                        append this.mdxFileList f
                    )
                )
            )
        )
        catch
        (
            format "WhiteoutDex MPQ: Error listing MDX files: %\n" (getCurrentException())
        )
        
        sort this.mdxFileList
        format "WhiteoutDex MPQ: Found % MDX files.\n" this.mdxFileList.count
        this.mdxFileList
    ),
    
    public fn filterMDXFiles searchTerm =
    (
        if searchTerm == "" or searchTerm == undefined then return this.mdxFileList
        local searchLower = toLower searchTerm
        for f in this.mdxFileList where findString (toLower f) searchLower != undefined collect f
    ),
    
    public fn extractMDX mpqPath =
    (
        if not this.isInitialized then return undefined
        if mpqPath == "" or mpqPath == undefined then return undefined
        if this.ndx == undefined then return undefined
        
        local fname = filenameFromPath mpqPath
        local outputPath = this.tempDir + fname
        
        if doesFileExist outputPath then return outputPath
        makeDir this.tempDir all:true
        
        -- Try all open archives
        local success = false
        for h in this.mpqHandles while not success do
        (
            if this.ndx.MPQ_HasFile h mpqPath then
                success = this.ndx.MPQ_ExtractToDisk h mpqPath outputPath
        )
        
        if success then
        (
            format "WhiteoutDex MPQ: Extracted MDX '%'\n" fname
            outputPath
        )
        else
        (
            format "WhiteoutDex MPQ: MDX '%' not found.\n" mpqPath
            undefined
        )
    ),
    
    public fn validateDirectory =
    (
        if this.mpqDirectory == "" or not (doesFileExist this.mpqDirectory) then return false
        local mpqExtensions = #("*.mpq", "*.w3mod", "*.mix")
        for ext in mpqExtensions do
            if (getFiles (this.mpqDirectory + "\\" + ext)).count > 0 then return true
        local subDirs = getDirectories (this.mpqDirectory + "\\*")
        for d in subDirs do
            for ext in mpqExtensions do
                if (getFiles (d + ext)).count > 0 then return true
        false
    ),
    
    -- ========================================================================
    -- CLEANUP
    -- ========================================================================
    
    public fn cleanup cleanTemp:false =
    (
        this.closeMPQs()
        this.textureCache = undefined
        if cleanTemp then
        (
            try (
                local tempFiles = getFiles (this.tempDir + "*.*")
                for f in tempFiles do try ( deleteFile f ) catch()
                format "WhiteoutDex MPQ: Temp cleaned (% files)\n" tempFiles.count
            ) catch()
        )
    )
)


/* ============================================================================
   SECTION 3: CASC HELPER
   Same API as the old WhiteoutDexCASCHelperStruct — drop-in replacement.
   ============================================================================ */

struct WhiteoutDexCASCHelperStruct
(
    -- Public properties (accessed by SceneRebuilder, Settings, etc.)
    public isInitialized = false,
    public w3path = "",
    
    -- Private
    private fileList = #(),
    private textureCache = undefined,
    private mdxPathMap = undefined,    -- maps display name → real CASC path
    private tempDir = ((getDir #temp) + "\\WhiteoutDex_CASC\\"),
    private iniFile = (getDir #plugcfg + "\\WhiteoutDex_Settings.ini"),
    private ndx = ::WhiteoutDexTextureBrowserDLL,
    
    -- ========================================================================
    -- SETTINGS
    -- ========================================================================
    
    public fn loadSettings =
    (
        local val = getINISetting this.iniFile "CASC" "W3Path"
        if val != "" then this.w3path = val
        format "WhiteoutDex CASC: W3Path = '%'\n" this.w3path
    ),
    
    public fn saveSettings =
    (
        setINISetting this.iniFile "CASC" "W3Path" this.w3path
    ),
    
    -- ========================================================================
    -- CASC LIFECYCLE
    -- ========================================================================
    
    public fn init w3dir: =
    (
        if w3dir != unsupplied and w3dir != undefined then
            this.w3path = w3dir
        
        if this.w3path == "" or not (doesFileExist this.w3path) then
        (
            format "WhiteoutDex CASC: Warcraft III path not set.\n"
            return false
        )
        
        if not (doesFileExist (this.w3path + "\\.build.info")) then
        (
            format "WhiteoutDex CASC: No .build.info found at '%'\n" this.w3path
            return false
        )
        
        if this.ndx == undefined then
        (
            format "WhiteoutDex CASC: Native DLL not loaded.\n"
            return false
        )
        
        format "WhiteoutDex CASC: Initializing from '%'...\n" this.w3path
        
        local log = this.ndx.CASC_Open this.w3path
        format "%\n" log
        
        local fileCount = this.ndx.CASC_FileCount()
        if fileCount > 0 then
        (
            this.isInitialized = true
            this.textureCache = Dictionary #string
            this.saveSettings()
            format "WhiteoutDex CASC: Ready! % files available.\n" fileCount
        )
        else
        (
            -- Partially initialized (encoding-based extraction still works)
            this.isInitialized = true
            this.saveSettings()
            format "WhiteoutDex CASC: Initialized (TVFS may be incomplete).\n"
        )
        
        makeDir this.tempDir all:true
        true
    ),
    
    -- ========================================================================
    -- FILE OPERATIONS
    -- ========================================================================
    
    public fn listFiles =
    (
        if not this.isInitialized then return #()
        if this.ndx == undefined then return #()
        
        local dotNetArr = this.ndx.CASC_ListFiles()
        this.fileList = #()
        if dotNetArr != undefined then
            for i = 1 to dotNetArr.count do
                append this.fileList dotNetArr[i]
        
        format "WhiteoutDex CASC: % files listed.\n" this.fileList.count
        this.fileList
    ),
    
    public fn filterFiles searchTerm =
    (
        if not this.isInitialized then return #()
        if this.ndx == undefined then return #()
        if searchTerm == "" or searchTerm == undefined then return this.fileList
        
        local dotNetArr = this.ndx.CASC_SearchFiles searchTerm
        if dotNetArr == undefined then return #()
        
        local filtered = #()
        for i = 1 to dotNetArr.count do
            append filtered dotNetArr[i]
        filtered
    ),
    
    public fn extractFile cascPath outputPath: =
    (
        if not this.isInitialized then return undefined
        if cascPath == "" or cascPath == undefined then return undefined
        if this.ndx == undefined then return undefined
        
        if outputPath == unsupplied or outputPath == undefined then
            outputPath = this.tempDir + (filenameFromPath cascPath)
        
        if doesFileExist outputPath then return outputPath
        makeDir (getFilenamePath outputPath) all:true
        
        local result = this.ndx.CASC_ExtractToDisk cascPath outputPath
        if result > 0 then
        (
            format "WhiteoutDex CASC: Extracted '%' (% bytes)\n" cascPath result
            outputPath
        )
        else
        (
            format "WhiteoutDex CASC: Failed to extract '%'\n" cascPath
            undefined
        )
    ),
    
    -- ========================================================================
    -- TEXTURE SEARCH
    -- Returns file path to extracted texture, or undefined.
    -- Uses native BLP/DDS decoding — no WhiteoutTexCLI needed.
    -- ========================================================================
    
    public fn findTexture fname =
    (
        if not this.isInitialized then return undefined
        if fname == "" or fname == undefined then return undefined
        if this.ndx == undefined then return undefined
        
        -- Check cache
        if this.textureCache != undefined and hasDictValue this.textureCache fname then
        (
            local cached = this.textureCache[fname]
            if doesFileExist cached then return cached
        )
        
        -- Check previously extracted files
        local blpCachePath = this.tempDir + fname + ".blp"
        local ddsCachePath = this.tempDir + fname + ".dds"
        local pngPath = this.tempDir + fname + ".png"
        local tgaPath = this.tempDir + fname + ".tga"
        
        if ::WhiteoutDexHasNativeBLP and doesFileExist blpCachePath then
        (
            if this.textureCache != undefined then this.textureCache[fname] = blpCachePath
            return blpCachePath
        )
        if doesFileExist ddsCachePath then
        (
            if this.textureCache != undefined then this.textureCache[fname] = ddsCachePath
            return ddsCachePath
        )
        if doesFileExist pngPath then
        (
            if this.textureCache != undefined then this.textureCache[fname] = pngPath
            return pngPath
        )
        if doesFileExist tgaPath then
        (
            if this.textureCache != undefined then this.textureCache[fname] = tgaPath
            return tgaPath
        )
        
        -- Search in CASC: DDS first (Reforged), then BLP (Classic)
        local searchPaths = #(
            "Textures\\" + fname + ".dds",
            fname + ".dds",
            "Textures\\" + fname + ".blp",
            fname + ".blp",
            "ReplaceableTextures\\" + fname + ".blp",
            "SharedModels\\Textures\\" + fname + ".blp"
        )
        
        local cascFilePath = undefined
        for sp in searchPaths while cascFilePath == undefined do
        (
            try (
                local results = this.ndx.CASC_SearchFiles sp
                if results != undefined and results.count > 0 then
                    cascFilePath = results[1]
            ) catch()
        )
        
        if cascFilePath == undefined then return undefined
        
        -- Extract texture to temp
        local texExt = getFilenameType cascFilePath
        local texTempPath = this.tempDir + fname + texExt
        makeDir this.tempDir all:true
        
        local extractResult = this.ndx.CASC_ExtractToDisk cascFilePath texTempPath
        if extractResult <= 0 then return undefined
        
        -- DDS: return directly (Max handles DDS)
        if toLower texExt == ".dds" then
        (
            if this.textureCache != undefined then this.textureCache[fname] = texTempPath
            format "WhiteoutDex CASC: '%' -> '%'\n" (filenameFromPath cascFilePath) (filenameFromPath texTempPath)
            return texTempPath
        )
        
        -- BLP with native support: return directly
        if toLower texExt == ".blp" and ::WhiteoutDexHasNativeBLP then
        (
            if this.textureCache != undefined then this.textureCache[fname] = texTempPath
            format "WhiteoutDex CASC: '%' -> '%' (native BLP)\n" (filenameFromPath cascFilePath) (filenameFromPath texTempPath)
            return texTempPath
        )
        
        -- Decode BLP to PNG via native DLL
        try
        (
            local bmp = this.ndx.DecodeBLP texTempPath
            if bmp != undefined then
            (
                bmp.Save pngPath ((dotNetClass "System.Drawing.Imaging.ImageFormat").Png)
                bmp.Dispose()
                try ( deleteFile texTempPath ) catch()
                if this.textureCache != undefined then this.textureCache[fname] = pngPath
                format "WhiteoutDex CASC: '%' -> '%'\n" (filenameFromPath cascFilePath) (filenameFromPath pngPath)
                return pngPath
            )
        )
        catch
        (
            format "WhiteoutDex CASC: BLP decode failed: %\n" (getCurrentException())
        )
        
        -- Fallback: return raw file
        if doesFileExist texTempPath then texTempPath else undefined
    ),
    
    -- ========================================================================
    -- MDX BROWSER
    -- ========================================================================
    
    public fn listMDXFiles =
    (
        if not this.isInitialized then return #()
        if this.ndx == undefined then return #()
        
        this.mdxPathMap = Dictionary #string
        
        local rawFiles = this.filterFiles ".mdx"
        local displayNames = #()
        local mdxSet = Dictionary #string
        
        for f in rawFiles do
        (
            local lower = toLower f
            local displayName = f
            local isHD = false
            
            -- Detect HD: path contains _hd.w3mod
            if findString lower "_hd.w3mod" != undefined then
                isHD = true
            
            -- Strip container prefix (everything up to and including the last ':')
            -- e.g. "war3.w3mod:_hd.w3mod:units\human\footman.mdx" → "units\human\footman.mdx"
            local lastColon = 0
            for i = 1 to f.count do
                if f[i] == ":" then lastColon = i
            
            if lastColon > 0 then
                displayName = substring f (lastColon + 1) -1
            
            -- Add [HD] prefix for HD models
            if isHD then
                displayName = "[HD] " + displayName
            
            -- Normalize separators for display
            displayName = substituteString displayName "/" "\\"
            
            local key = toLower displayName
            if not (hasDictValue mdxSet key) then
            (
                mdxSet[key] = true
                append displayNames displayName
                this.mdxPathMap[displayName] = f
            )
        )
        
        sort displayNames
        format "WhiteoutDex CASC: Found % MDX files.\n" displayNames.count
        displayNames
    ),
    
    public fn extractMDX cascPath =
    (
        if not this.isInitialized then return undefined
        if cascPath == "" or cascPath == undefined then return undefined
        if this.ndx == undefined then return undefined
        
        -- Resolve display name → real CASC path
        local realPath = cascPath
        if this.mdxPathMap != undefined and hasDictValue this.mdxPathMap cascPath then
            realPath = this.mdxPathMap[cascPath]
        
        -- Build unique output name: prefix HD_/SD_ based on tag
        local tag = try ( this.ndx.CASC_GetFileTag realPath ) catch( "" )
        local rawName = cascPath
        if findString rawName "[HD] " == 1 then rawName = substring rawName 6 -1
        else if findString rawName "[SD] " == 1 then rawName = substring rawName 6 -1
        local fname = filenameFromPath rawName
        
        -- Add tag prefix to avoid HD/SD collisions in temp dir
        local prefix = ""
        if tag == "HD" then prefix = "HD_"
        else if tag == "SD" then prefix = "SD_"
        
        local outputPath = this.tempDir + prefix + fname
        
        if doesFileExist outputPath then return outputPath
        makeDir this.tempDir all:true
        
        local result = this.ndx.CASC_ExtractToDisk realPath outputPath
        if result > 0 then
        (
            format "WhiteoutDex CASC: Extracted MDX '%' (% bytes)\n" fname result
            outputPath
        )
        else
        (
            format "WhiteoutDex CASC: Failed to extract MDX '%'\n" cascPath
            undefined
        )
    ),
    
    -- ========================================================================
    -- CLEANUP
    -- ========================================================================
    
    public fn cleanup cleanTemp:false =
    (
        if this.ndx != undefined then
            try ( this.ndx.CASC_Close() ) catch()
        
        this.isInitialized = false
        this.textureCache = undefined
        this.mdxPathMap = undefined
        this.fileList = #()
        
        if cleanTemp then
        (
            try (
                local tempFiles = getFiles (this.tempDir + "*.*")
                for f in tempFiles do try ( deleteFile f ) catch()
                format "WhiteoutDex CASC: Temp cleaned (% files)\n" tempFiles.count
            ) catch()
        )
    ),
    
    public fn close =
    (
        this.cleanup()
    )
)


/* ============================================================================
   SECTION 4: GLOBAL INSTANCES
   Same variable names as before — all other scripts work unchanged.
   ============================================================================ */

global WhiteoutDexMPQ = WhiteoutDexMPQHelperStruct()
::WhiteoutDexMPQ.loadSettings()

global WhiteoutDexCASC = WhiteoutDexCASCHelperStruct()
::WhiteoutDexCASC.loadSettings()

format "WhiteoutDex Native Helper loaded (MPQ + CASC).\n"
if ::WhiteoutDexTextureBrowserDLL != undefined then
    format "  DLL: OK\n"
else
    format "  DLL: NOT FOUND — archive support disabled.\n"


/* ============================================================================
   SECTION 5: TEXTURE BROWSER (Native Form)
   Uses TextureBrowserForm from WhiteoutDexTextureBrowser.dll (C++/CLI WinForms).
   Material operations stay in MaxScript (can't create Max materials from C++).
   
   Usage:  ::WhiteoutDexTexBrowser.show()
   ============================================================================ */

-- Global event handlers (MaxScript requires these in global scope for dotNet)

-- Slot targeting: set by Wc3Material ⚙ buttons before opening browser
-- Format: #(materialRef, "slotName") or undefined for default behavior
-- slotName: "diffuse", "normal", "orm", "emissive", "reflection"
global WhiteoutDexTexBrowserTarget = undefined
global WhiteoutDexTexBrowserInitCategory = 0  -- 0=All, 1=Units, 2=Buildings, 3=Doodads, 4=Abilities, 5=Environment, 6=Icons

-- Validates whether a filename matches the expected naming convention for a slot
fn _ntbValidateSlot fileName slotName =
(
    local baseName = toLower (getFilenameFile fileName)
    case slotName of
    (
        "diffuse":    true  -- diffuse accepts anything
        "normal":     (findString baseName "_normal" != undefined)
        "orm":        (findString baseName "_orm" != undefined)
        "emissive":   (findString baseName "_emissive" != undefined)
        "reflection": (findString baseName "_environment" != undefined or findString baseName "_env" != undefined)
        default:      true
    )
)

-- Assigns a texture file to a specific slot on an existing Warcraft3 material
fn _ntbAssignToSlot mat slotName filePath archivePath:"" =
(
    local bmpTex = BitmapTexture filename:filePath
    local fName = filenameFromPath filePath
    
    -- Auto Prefix Path: extract directory from archive path if checkbox is checked
    try
    (
        if ::WhiteoutDexTexBrowser != undefined and ::WhiteoutDexTexBrowser.form != undefined and ::WhiteoutDexTexBrowser.form.AutoPrefixPath then
        (
            local srcPath = if archivePath != undefined and archivePath != "" then archivePath else filePath
            -- Strip CASC tags like [HD], [SD] from start of path
            if srcPath.count > 4 and srcPath[1] == "[" then
            (
                local closeBracket = findString srcPath "] "
                if closeBracket != undefined then
                    srcPath = substring srcPath (closeBracket + 2) -1
            )
            -- Normalize to backslashes
            srcPath = substituteString srcPath "/" "\\"
            -- Extract directory (keep trailing backslash)
            local dirPath = getFilenamePath srcPath
            if dirPath != "" then
            (
                case slotName of
                (
                    "diffuse":
                    (
                        mat.path = dirPath
                        try (mat.reforgedRollout.edt_diffPath.text = dirPath) catch()
                        try (mat.window1.Pather.text = dirPath) catch()
                    )
                    "normal":     ( mat.normalPrefixPath = dirPath ; try (mat.reforgedRollout.edt_normalPath.text = dirPath) catch() )
                    "orm":        ( mat.ormPrefixPath = dirPath ; try (mat.reforgedRollout.edt_ormPath.text = dirPath) catch() )
                    "emissive":   ( mat.emissivePrefixPath = dirPath ; try (mat.reforgedRollout.edt_emissivePath.text = dirPath) catch() )
                    "reflection": ( mat.reflectionPrefixPath = dirPath ; try (mat.reforgedRollout.edt_reflectionPath.text = dirPath) catch() )
                    "particle_texture":
                    (
                        mat.path = dirPath
                        try (mat.params1.path.text = dirPath) catch()
                    )
                )
                format "WhiteoutDex TexBrowser: Auto prefix '%' for % slot\n" dirPath slotName
            )
        )
        else
        (
            -- Checkbox off: clear the prefix path for this slot
            case slotName of
            (
                "diffuse":
                (
                    mat.path = ""
                    try (mat.reforgedRollout.edt_diffPath.text = "") catch()
                    try (mat.window1.Pather.text = "") catch()
                )
                "normal":     ( mat.normalPrefixPath = "" ; try (mat.reforgedRollout.edt_normalPath.text = "") catch() )
                "orm":        ( mat.ormPrefixPath = "" ; try (mat.reforgedRollout.edt_ormPath.text = "") catch() )
                "emissive":   ( mat.emissivePrefixPath = "" ; try (mat.reforgedRollout.edt_emissivePath.text = "") catch() )
                "reflection": ( mat.reflectionPrefixPath = "" ; try (mat.reforgedRollout.edt_reflectionPath.text = "") catch() )
                "particle_texture":
                (
                    mat.path = ""
                    try (mat.params1.path.text = "") catch()
                )
            )
        )
    ) catch()
    
    case slotName of
    (
        "diffuse":
        (
            mat.texture = bmpTex
            -- Update button text in reforged rollout
            try (mat.reforgedRollout.btnDiffuseMap.text = fName) catch()
            -- Update preview image using DLL decoder (handles BLP/DDS/PNG/JPG)
            try
            (
                local ext = toLower (getFilenameType filePath)
                local dotNetBmp = undefined
                if ext == ".blp" then
                    dotNetBmp = (dotNetClass "WhiteoutDex.WhiteoutDexTextureBrowser").DecodeBLP filePath
                else if ext == ".dds" then
                    dotNetBmp = (dotNetClass "WhiteoutDex.WhiteoutDexTextureBrowser").DecodeDDS filePath
                else
                    dotNetBmp = dotNetObject "System.Drawing.Bitmap" filePath
                
                if dotNetBmp != undefined then
                (
                    local imgBox = mat.reforgedRollout.rfImgBox
                    if imgBox.image != undefined then imgBox.image.Dispose()
                    imgBox.image = dotNetBmp
                    imgBox.BorderStyle = (dotNetClass "System.Windows.Forms.BorderStyle").FixedSingle
                    mat.reforgedRollout.rfPanel_Border.visible = true
                )
            ) catch()
        )
        "normal":
        (
            mat.normal_map = bmpTex
            try (mat.reforgedRollout.mb_normal.text = fName) catch()
        )
        "orm":
        (
            mat.orm_map = bmpTex
            try (mat.reforgedRollout.mb_orm.text = fName) catch()
        )
        "emissive":
        (
            mat.emissive_map = bmpTex
            try (mat.reforgedRollout.mb_emissive.text = fName) catch()
        )
        "reflection":
        (
            mat.reflection_map = bmpTex
            try (mat.reforgedRollout.mb_reflection.text = fName) catch()
        )
        "particle_texture":
        (
            -- Particle plugins store texture as string path, not BitmapTexture
            mat.texture = filePath
            try (mat.params1.texture.text = filePath) catch()
        )
    )
    format "WhiteoutDex TexBrowser: Assigned '%' to % slot\n" fName slotName
)

fn _ntbOnApply s e = (
    try (
        local filePath = e.ExtractedPath
        if filePath != undefined and filePath != "" and doesFileExist filePath then
        (
            -- Check if a material slot target was set (from ⚙ button)
            if ::WhiteoutDexTexBrowserTarget != undefined and ::WhiteoutDexTexBrowserTarget.count == 2 then
            (
                local targetMat = ::WhiteoutDexTexBrowserTarget[1]
                local targetSlot = ::WhiteoutDexTexBrowserTarget[2]
                
                -- Validate filename against slot naming convention
                if _ntbValidateSlot filePath targetSlot then
                (
                    _ntbAssignToSlot targetMat targetSlot filePath archivePath:e.ArchivePath
                )
                else
                (
                    -- Wrong texture type: ask user
                    local fName = filenameFromPath filePath
                    local slotLabel = case targetSlot of
                    (
                        "normal": "Normal Map (_Normal)"
                        "orm": "ORM Map (_ORM)"
                        "emissive": "Emissive Map (_Emissive)"
                        "reflection": "Environment Map (_EnvironmentMap)"
                        default: targetSlot
                    )
                    local msg = fName + " does not look like a " + slotLabel + " texture.\nAssign anyway?"
                    if (queryBox msg title:"WhiteoutDex Texture Browser" beep:false) then
                        _ntbAssignToSlot targetMat targetSlot filePath archivePath:e.ArchivePath
                )
                -- Keep target active so user can assign multiple textures
                -- Target is cleared when browser is closed
            )
            else
            (
                -- No slot target: original behavior (create new material)
                local bmpTex = BitmapTexture filename:filePath
                local mat = Warcraft3()
                mat.name = getFilenameFile filePath
                mat.texture = bmpTex
                mat.showInViewport = true

                if selection.count > 0 then
                (
                    for obj in selection do obj.material = mat
                    format "WhiteoutDex TexBrowser: Applied '%' to % objects\n" (filenameFromPath filePath) selection.count
                )
                else
                (
                    -- No selection: put to active material editor slot
                    local slot = medit.GetActiveMtlSlot()
                    meditMaterials[slot] = mat
                    medit.PutMtlToMtlEditor mat slot
                    format "WhiteoutDex TexBrowser: Applied '%' to material slot %\n" (filenameFromPath filePath) slot
                )
            )
        )
    ) catch ( format "WhiteoutDex TexBrowser: Apply error: %\n" (getCurrentException()) )
)

fn _ntbOnMtlSlot s e = (
    try (
        local filePath = e.ExtractedPath
        if filePath != undefined and filePath != "" and doesFileExist filePath then
        (
            -- Check if a material slot target was set (from ⚙ button)
            if ::WhiteoutDexTexBrowserTarget != undefined and ::WhiteoutDexTexBrowserTarget.count == 2 then
            (
                local targetMat = ::WhiteoutDexTexBrowserTarget[1]
                local targetSlot = ::WhiteoutDexTexBrowserTarget[2]
                
                if _ntbValidateSlot filePath targetSlot then
                (
                    _ntbAssignToSlot targetMat targetSlot filePath archivePath:e.ArchivePath
                )
                else
                (
                    local fName = filenameFromPath filePath
                    local slotLabel = case targetSlot of
                    (
                        "normal": "Normal Map (_Normal)"
                        "orm": "ORM Map (_ORM)"
                        "emissive": "Emissive Map (_Emissive)"
                        "reflection": "Environment Map (_EnvironmentMap)"
                        default: targetSlot
                    )
                    local msg = fName + " does not look like a " + slotLabel + " texture.\nAssign anyway?"
                    if (queryBox msg title:"WhiteoutDex Texture Browser" beep:false) then
                        _ntbAssignToSlot targetMat targetSlot filePath archivePath:e.ArchivePath
                )
            )
            else
            (
                -- No slot target: original behavior
                local bmpTex = BitmapTexture filename:filePath
                local slot = medit.GetActiveMtlSlot()
                local mat = Warcraft3()
                mat.name = getFilenameFile filePath
                mat.texture = bmpTex
                mat.showInViewport = true
                meditMaterials[slot] = mat
                medit.PutMtlToMtlEditor mat slot
                format "WhiteoutDex TexBrowser: Applied '%' to material slot %\n" (filenameFromPath filePath) slot
            )
        )
    ) catch ( format "WhiteoutDex TexBrowser: MtlSlot error: %\n" (getCurrentException()) )
)

fn _ntbOnSelected s e = ()  -- Selection only (no extract); assignment handled by Apply/MtlSlot events

-- 3ds Max keyboard fix: disable accelerators when texture browser is active
-- Without this, Max steals keyboard input after the first keystroke
fn _ntbOnActivated s e = ( enableAccelerators = false )
fn _ntbOnDeactivate s e = ( enableAccelerators = true )
fn _ntbOnFormClosed s e = (
    enableAccelerators = true
    ::WhiteoutDexTexBrowserTarget = undefined
    try (::WhiteoutDexTexBrowser.form = undefined) catch()
)

struct WhiteoutDexTexBrowserStruct
(
    public form = undefined,
    
    public fn show =
    (
        -- If browser is already open, just update the slot filter and bring to front
        if this.form != undefined then
        (
            try
            (
                local f = this.form
                if not f.IsDisposed then
                (
                    -- Update slot filter on existing form
                    if ::WhiteoutDexTexBrowserTarget != undefined and ::WhiteoutDexTexBrowserTarget.count == 2 then
                        f.SlotFilter = ::WhiteoutDexTexBrowserTarget[2]
                    else
                        f.SlotFilter = ""
                    f.BringToFront()
                    f.Focus()
                    format "WhiteoutDex TexBrowser: Updated slot filter to '%'\n" f.SlotFilter
                    return()
                )
            ) catch()
            -- Form was disposed, clear reference and recreate below
            this.form = undefined
        )
        
        if ::WhiteoutDexTextureBrowserDLL == undefined then ( messageBox "WhiteoutDexTextureBrowser.dll not loaded!" ; return() )
        
        -- Auto-init archives if needed
        local hasMPQ = (::WhiteoutDexMPQ != undefined and ::WhiteoutDexMPQ.isInitialized)
        local hasCASC = (::WhiteoutDexCASC != undefined and ::WhiteoutDexCASC.isInitialized)
        
        if not hasMPQ and ::WhiteoutDexMPQ != undefined then
            try ( ::WhiteoutDexMPQ.scanAndOpenMPQs() ; hasMPQ = ::WhiteoutDexMPQ.isInitialized ) catch()
        
        if not hasCASC and ::WhiteoutDexCASC != undefined then
        (
            try (
                local cascIni = getDir #plugcfg + "\\WhiteoutDex_Settings.ini"
                local w3dir = getINISetting cascIni "CASC" "W3Path"
                if w3dir != "" then ( ::WhiteoutDexCASC.init w3dir:w3dir ; hasCASC = ::WhiteoutDexCASC.isInitialized )
            ) catch()
        )
        
        if not hasMPQ and not hasCASC then
        (
            messageBox "No archives loaded. Set paths in WhiteoutDex Settings first."
            return()
        )
        
        -- Create native form from DLL
        this.form = dotNetObject "WhiteoutDex.TextureBrowserForm"
        local f = this.form
        dotNet.setLifetimeControl f #dotNet
        
        -- Connect events (C++ fires these, MaxScript handles material creation)
        dotNet.addEventHandler f "TextureApply" _ntbOnApply
        dotNet.addEventHandler f "TextureMtlSlot" _ntbOnMtlSlot
        dotNet.addEventHandler f "TextureSelected" _ntbOnSelected
        
        -- Fix keyboard: disable Max accelerators when form is active
        dotNet.addEventHandler f "Activated" _ntbOnActivated
        dotNet.addEventHandler f "Deactivate" _ntbOnDeactivate
        dotNet.addEventHandler f "FormClosed" _ntbOnFormClosed
        
        -- Set slot filter if targeting a specific texture slot
        if ::WhiteoutDexTexBrowserTarget != undefined and ::WhiteoutDexTexBrowserTarget.count == 2 then
            f.SlotFilter = ::WhiteoutDexTexBrowserTarget[2]
        else
            f.SlotFilter = ""
        
        -- Set initial category if requested
        if ::WhiteoutDexTexBrowserInitCategory != undefined and ::WhiteoutDexTexBrowserInitCategory > 0 then
            f.InitialCategory = ::WhiteoutDexTexBrowserInitCategory
        ::WhiteoutDexTexBrowserInitCategory = 0
        
        f.Show()
        format "WhiteoutDex Texture Browser opened.\n"
    ),
    
    public fn close =
    (
        if this.form != undefined then
        (
            enableAccelerators = true  -- restore on close
            try ( local f = this.form ; f.Close() ) catch()
        )
        this.form = undefined
        ::WhiteoutDexTexBrowserTarget = undefined
    )
)

global WhiteoutDexTexBrowser = WhiteoutDexTexBrowserStruct()
