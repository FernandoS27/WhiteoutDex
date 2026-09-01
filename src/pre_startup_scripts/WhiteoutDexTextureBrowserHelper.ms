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
    local bundleRoot = (::WhiteoutDexInstallRoot)
    local altRoot    = (systemTools.getEnvVariable "ALLUSERSPROFILE") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex"
    -- (maxVersion())[8] is only populated on Max 2017+; derive year from
    -- the build number for cross-version compatibility (Max 2016+).
    local maxYear    = ((maxVersion())[1] / 1000) + 1998
    local maxYearStr = maxYear as string
    
    -- Try multiple folder naming conventions, in priority order:
    --   1. Per-year folder (e.g. "Max2027") — matches the layout used by the
    --      .dle / .dlo / .bmi native plugins listed in PackageContents.xml.
    --   2. Grouped folder ("Max2026+" for .NET 8, "Max2022-2025" for .NET FW 4.8.1)
    --      — older DLL layout from earlier WhiteoutDex builds.
    local groupFolder = if maxYear >= 2026 then "Max2026+" else "Max2022-2025"
    local candidateFolders = #( ("Max" + maxYearStr), groupFolder )
    
    local finalPath = undefined
    for folder in candidateFolders while finalPath == undefined do
    (
        local p1 = bundleRoot + "\\native plugins\\" + folder + "\\WhiteoutDexTextureBrowser.dll"
        local p2 = altRoot    + "\\native plugins\\" + folder + "\\WhiteoutDexTextureBrowser.dll"
        if doesFileExist p1 then finalPath = p1
        else if doesFileExist p2 then finalPath = p2
    )
    
    if finalPath != undefined then
    (
        try
        (
            dotNet.loadAssembly finalPath
            WhiteoutDexTextureBrowserDLL = dotNetClass "WhiteoutDex.WhiteoutDexTextureBrowser"
            format "WhiteoutDex: Native DLL loaded from '%'\n" finalPath
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
        format "  Searched folders: % under '%' and '%'\n" candidateFolders bundleRoot altRoot
        format "  MPQ/CASC support will be unavailable.\n"
    )
)


/* ============================================================================
   SECTION 1b: WARCRAFT III GAME FINDER
   Thin MaxScript facade over WhiteoutLib's BlizzardGameFinder (exposed by the
   native DLL). Used to fill in empty MPQ/CASC settings automatically so a
   fresh install works without the user hunting for their game folder.

   Flavour comes from what is on disk, not from the registry name: an install
   with a `Data` folder and no War3LegacyInstaller is Reforged, anything else
   is Classic. That classification happens native-side.
   ============================================================================ */

struct WhiteoutDexGameFinderStruct
(
    private ndx = ::WhiteoutDexTextureBrowserDLL,

    -- Every Warcraft III install found, as an array of #(kind, path) pairs
    -- where kind is "Reforged" or "Classic". Empty array when the DLL is
    -- missing or nothing is installed.
    public fn findAll =
    (
        if this.ndx == undefined then return #()
        local out = #()
        try
        (
            local raw = this.ndx.FindWarcraftInstalls()
            if raw != undefined then
            (
                for e in raw do
                (
                    local sep = findString e "|"
                    if sep != undefined and sep > 1 then
                        append out #((substring e 1 (sep - 1)), (substring e (sep + 1) -1))
                )
            )
        )
        catch
        (
            format "WhiteoutDex GameFinder: lookup failed: %\n" (getCurrentException())
        )
        out
    ),

    -- Best Reforged install root, or "" when none was found.
    public fn findReforged =
    (
        if this.ndx == undefined then return ""
        local p = ""
        try ( p = this.ndx.FindWarcraftReforgedPath() ) catch ( p = "" )
        if p == undefined then p = ""
        p
    ),

    -- Best Classic (MPQ) install root, or "" when none was found.
    public fn findClassic =
    (
        if this.ndx == undefined then return ""
        local p = ""
        try ( p = this.ndx.FindWarcraftClassicPath() ) catch ( p = "" )
        if p == undefined then p = ""
        p
    )
)

global WhiteoutDexGameFinder = WhiteoutDexGameFinderStruct()


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
    private settingsIni = (getDir #plugcfg + "\\WhiteoutDex_Settings.ini"),
    private ndx = ::WhiteoutDexTextureBrowserDLL,
    
    -- ========================================================================
    -- SETTINGS
    -- ========================================================================
    
    public fn loadSettings =
    (
        local val = getINISetting this.iniFile "MPQ" "Directory"
        if val != "" then this.mpqDirectory = val
        else this.autoDetect()
        format "WhiteoutDex MPQ: Directory = '%'\n" this.mpqDirectory
    ),
    
    -- Nothing configured yet: ask WhiteoutLib's game finder where Warcraft III
    -- Classic lives and persist the hit, so a fresh install needs no setup.
    -- Returns the directory in use.
    public fn autoDetect force:false =
    (
        if this.mpqDirectory != "" and not force then return this.mpqDirectory
        local found = ::WhiteoutDexGameFinder.findClassic()
        if found != "" and (doesFileExist found) then
        (
            this.saveSettings found
            format "WhiteoutDex MPQ: Auto-detected Warcraft III at '%'\n" found
        )
        this.mpqDirectory
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

    -- The MPQ load order the Settings dialog writes, resolved to absolute
    -- paths and in priority order. #() means the user never edited it, and
    -- callers then keep doing whatever they did before.
    --
    -- Mirrors wdx::mpq::ResolvedArchives in
    -- src/native_sources/common/wdx_mpq_settings.h: same key, same '|'
    -- separator, same "relative to the MPQ directory unless it is a full
    -- path" rule. The two have to agree - a model that previews out of one
    -- archive and imports out of another is not a bug report anyone can read.
    public fn getLoadOrder =
    (
        local out = #()
        local raw = getINISetting this.settingsIni "MPQ" "List"
        if raw == undefined or raw == "" then return out

        local base = this.mpqDirectory
        if base == "" then base = getINISetting this.settingsIni "CASC" "W3Path"
        if base != "" and base[base.count] != "\\" then base += "\\"

        for e in (filterString raw "|") do
        (
            local t = trimLeft (trimRight e)
            if t != "" then
            (
                local full = ""
                -- A drive letter or a UNC root is absolute; anything else
                -- hangs off the configured MPQ directory.
                if t.count > 1 and (t[2] == ":" or (t[1] == "\\" and t[2] == "\\")) then
                    full = t
                else if base != "" then
                    full = base + t
                if full != "" and doesFileExist full then append out full
            )
        )
        out
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
        
        -- A configured load order REPLACES the scan further down rather
        -- than being merged with it. The scan cannot express priority - it
        -- hands back whatever order the filesystem feels like, so which copy
        -- of a patched texture won was already luck - and it cannot reach an
        -- archive outside the game folder at all.
        local mpqFiles = this.getLoadOrder()

        -- With full paths configured the game folder is not needed, which is
        -- exactly the case where the archives are somewhere else.
        if mpqFiles.count == 0 and (this.mpqDirectory == "" or not (doesFileExist this.mpqDirectory)) then
        (
            format "WhiteoutDex MPQ: No valid directory set.\n"
            return false
        )
        
        this.textureCache = Dictionary #string
        makeDir this.tempDir all:true
        
        if mpqFiles.count > 0 then
            format "WhiteoutDex MPQ: using the configured load order (% archives)\n" mpqFiles.count
        else
        (
        -- Scan for MPQ files recursively
        local mpqExtensions = #("*.mpq", "*.w3mod", "*.w3x", "*.w3m", "*.mix")
        
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
        else this.autoDetect()
        format "WhiteoutDex CASC: W3Path = '%'\n" this.w3path
    ),
    
    -- Nothing configured yet: ask WhiteoutLib's game finder where Warcraft III
    -- Reforged lives and persist the hit, so a fresh install needs no setup.
    -- Returns the path in use.
    public fn autoDetect force:false =
    (
        if this.w3path != "" and not force then return this.w3path
        local found = ::WhiteoutDexGameFinder.findReforged()
        if found != "" and (doesFileExist found) then
        (
            this.w3path = found
            this.saveSettings()
            format "WhiteoutDex CASC: Auto-detected Warcraft III Reforged at '%'\n" found
        )
        this.w3path
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

        -- Note: WhiteoutLib's CASC backend doesn't require .build.info to be
        -- present at the W3Path. We used to gate here on its existence, which
        -- printed a misleading warning for installs where the backend works
        -- just fine. Trust the native open call to report success/failure.

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
   SECTION 5: TEXTURE BROWSER

   The browser itself is the native asset picker - the same grid/tree window
   the Model Browser opens (WdxPickAsset in WhiteoutDexRenderer.dlx), asked for
   textures instead of models. Every cell is the decoded image, the tree view's
   preview pane reports the file's dimensions and source format, and the search
   box, zoom and Grid/Tree switch all behave exactly as they do when picking a
   model, because they ARE the same controls.

   What is left here is the half a picker cannot do: turning the chosen archive
   entry into a Max material. The picker hands back a path inside the game
   archives; a BitmapTexture needs a file, so we extract to temp and point at
   that.

   Usage:  ::WhiteoutDexTexBrowser.show()          -- honours the slot target
           ::WhiteoutDexTexBrowser.pick slot:"normal"

   Set ::WhiteoutDexTexBrowserTarget = #(<material>, "<slot>") first and show()
   assigns to that slot; leave it undefined and show() builds a Warcraft3
   material out of the pick instead.
   ============================================================================ */

-- Slot targeting: set by the Wc3Material browse buttons before opening.
-- Format: #(materialRef, "slotName") or undefined for default behaviour.
-- slotName: "diffuse", "normal", "orm", "emissive", "reflection",
--           "environment" (alias for reflection, used by Wc3Material),
--           "team_color"  (no naming convention; targets teamColorMap),
--           "bitmap"      (a Wc3Bitmap texmap rather than a material),
--           "particle_texture" (legacy particle plugins: a string, not a map)
global WhiteoutDexTexBrowserTarget = undefined

-- Copy the chosen texture's archive folder into the material's path-prefix
-- field. Replaces the old browser form's "Copy path" checkbox: the modal
-- picker has no chrome of its own to hang one on, and the prefix is what the
-- exporter needs to write the texture's in-game path, so on is the right
-- default. Scripts that want the old off behaviour set this to false.
global WhiteoutDexTexBrowserAutoPrefix = true

-- Folder the last pick came from, so the next one opens where the user left
-- off rather than at the root every time.
global WhiteoutDexTexBrowserLastPick = ""

/* Search text to open the picker with for a given slot.

   The naming convention Reforged textures follow is the only thing that tells
   a normal map from a diffuse one, and it is the same convention
   _ntbValidateSlot checks on the way back out. Seeding the search box with it
   means the browse opens on the plausible files without HIDING the rest - the
   user can clear the box, which they could not do with the old form's fixed
   slot filter. */
fn _ntbSlotSearchSeed slotName =
(
    case slotName of
    (
        "normal":      "_normal"
        "orm":         "_orm"
        "emissive":    "_emissive"
        "reflection":  "_environment,_env"
        "environment": "_environment,_env"
        default:       ""      -- diffuse, team_color, bitmap: no convention
    )
)

-- Validates whether a filename matches the expected naming convention for a slot
fn _ntbValidateSlot fileName slotName =
(
    local baseName = toLower (getFilenameFile fileName)
    case slotName of
    (
        "diffuse":     true  -- diffuse accepts anything
        "normal":      (findString baseName "_normal" != undefined)
        "orm":         (findString baseName "_orm" != undefined)
        "emissive":    (findString baseName "_emissive" != undefined)
        "reflection":  (findString baseName "_environment" != undefined or findString baseName "_env" != undefined)
        "environment": (findString baseName "_environment" != undefined or findString baseName "_env" != undefined)
        "team_color":  true  -- TeamGlow textures don't follow the _suffix convention
        default:       true
    )
)

-- Assigns a texture file to a specific slot on an existing Warcraft3 material
fn _ntbAssignToSlot mat slotName filePath archivePath:"" =
(
    -- ── New Wc3Material plugin (Build 2.x) ───────────────────────────────
    -- Detected by presence of `diffuseMap` + `diffusePrefix` properties.
    -- Property names: diffuseMap / normalMap / ormMap / emissiveMap /
    --                 teamColorMap / environmentMap   plus  *Prefix.
    if (isProperty mat #diffuseMap) and (isProperty mat #diffusePrefix) then
    (
        local bmpTex = BitmapTexture filename:filePath
        local fName = filenameFromPath filePath

        -- Prefix path: the chosen texture's archive folder, when the
        -- WhiteoutDexTexBrowserAutoPrefix global says to copy it
        local dirPath = ""
        try (
            if ::WhiteoutDexTexBrowserAutoPrefix != false then
            (
                local srcPath = if archivePath != undefined and archivePath != "" then archivePath else filePath
                if srcPath.count > 4 and srcPath[1] == "[" then
                (
                    local closeBracket = findString srcPath "] "
                    if closeBracket != undefined then
                        srcPath = substring srcPath (closeBracket + 2) -1
                )
                srcPath = substituteString srcPath "/" "\\"
                dirPath = getFilenamePath srcPath
            )
        ) catch()

        case slotName of
        (
            "diffuse":     ( mat.diffuseMap     = bmpTex ; mat.diffusePrefix     = dirPath )
            "normal":      ( mat.normalMap      = bmpTex ; mat.normalPrefix      = dirPath )
            "orm":         ( mat.ormMap         = bmpTex ; mat.ormPrefix         = dirPath )
            "emissive":    ( mat.emissiveMap    = bmpTex ; mat.emissivePrefix    = dirPath )
            "team_color":  ( mat.teamColorMap   = bmpTex ; mat.teamColorPrefix   = dirPath )
            "environment": ( mat.environmentMap = bmpTex ; mat.environmentPrefix = dirPath )
            "reflection":  ( mat.environmentMap = bmpTex ; mat.environmentPrefix = dirPath )  -- alias
            default: ( format "WhiteoutDex TexBrowser: Unknown slot '%' for Wc3Material\n" slotName )
        )
        format "WhiteoutDex TexBrowser: Assigned '%' to % slot (Wc3Material)\n" fName slotName
        return undefined
    )

    -- ── Legacy Warcraft3 material (Build 1.x) ───────────────────────────
    local bmpTex = BitmapTexture filename:filePath
    local fName = filenameFromPath filePath
    
    -- Auto prefix path: the archive folder the texture came from
    try
    (
        if ::WhiteoutDexTexBrowserAutoPrefix != false then
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

/* Ask the user which slot label a mismatched file was meant for. The
   convention check is advice, not a rule - plenty of hand-made textures do not
   follow it - so a miss asks rather than refuses. */
fn _ntbSlotLabel slotName =
(
    case slotName of
    (
        "normal":      "Normal Map (_Normal)"
        "orm":         "ORM Map (_ORM)"
        "emissive":    "Emissive Map (_Emissive)"
        "reflection":  "Environment Map (_EnvironmentMap)"
        "environment": "Environment Map (_EnvironmentMap)"
        "team_color":  "Team Color Map"
        default:       slotName
    )
)

-- Assign to a slot, asking first when the file's name does not look like one
-- that belongs there. Returns true when something was assigned.
fn _ntbAssignChecked mat slotName filePath archivePath:"" =
(
    if not (_ntbValidateSlot filePath slotName) then
    (
        local msg = (filenameFromPath filePath) + " does not look like a " +
                    (_ntbSlotLabel slotName) + " texture." + "\n" + "Assign anyway?"
        if not (queryBox msg title:"WhiteoutDex Texture Browser" beep:false) then
            return false
    )
    _ntbAssignToSlot mat slotName filePath archivePath:archivePath
    true
)

-- Point a Wc3Bitmap texmap at the extracted file, for a caller that set a
-- #(<texmap>, "bitmap") target. Wc3Bitmap's own browse button does not come
-- through here - it calls pick() so it can refresh its file-name label with
-- the answer, which nothing reached from a global target could do.
fn _ntbAssignToBitmap bmp filePath =
(
    try
    (
        bmp.delegate.filename = filePath
        format "WhiteoutDex TexBrowser: Set bitmap to '%'\n" (filenameFromPath filePath)
        true
    )
    catch
    (
        format "WhiteoutDex TexBrowser: could not set the bitmap: %\n" (getCurrentException())
        false
    )
)

-- No slot target: build a Warcraft3 material out of the pick and put it
-- somewhere useful - on the selection if there is one, else in the active
-- material editor slot. What the old browser's "Apply" button did.
fn _ntbApplyAsMaterial filePath =
(
    local mat = Warcraft3()
    mat.name = getFilenameFile filePath
    mat.texture = BitmapTexture filename:filePath
    mat.showInViewport = true

    if selection.count > 0 then
    (
        for obj in selection do obj.material = mat
        format "WhiteoutDex TexBrowser: Applied '%' to % objects\n" (filenameFromPath filePath) selection.count
    )
    else
    (
        local slot = medit.GetActiveMtlSlot()
        meditMaterials[slot] = mat
        medit.PutMtlToMtlEditor mat slot
        format "WhiteoutDex TexBrowser: Applied '%' to material slot %\n" (filenameFromPath filePath) slot
    )
)

struct WhiteoutDexTexBrowserStruct
(
    -- True when the native picker can run at all. The old form needed
    -- WhiteoutDexTextureBrowser.dll; this needs WhiteoutDexRenderer.dlx.
    public fn isAvailable =
    (
        ::WhiteoutDexModelBrowser != undefined and ::WhiteoutDexModelBrowser.isAvailable()
    ),

    /* Open the picker and return what was chosen, as

           #(<local file>, <relative path>, <archive path>)

       or `undefined` on cancel. The local file is an extraction under the temp
       directory: a BitmapTexture cannot be given an archive entry, and the
       relative path is what the exporter writes back out.

         slot:    seeds the search box with that slot's naming convention
         initial: a relative path to open on; "" resumes where the last pick
                  left off */
    public fn pick slot:"" initial:"" =
    (
        if not this.isAvailable() then
        (
            messageBox ("The WhiteoutDex Texture Browser is not available." + "\n\n" +
                        "WhiteoutDexRenderer.dlx did not load. Reinstall WhiteoutDex " +
                        "or check the Listener for plug-in load errors.") \
                       title:"WhiteoutDex" beep:false
            return undefined
        )

        local start = if initial == undefined or initial == "" then ::WhiteoutDexTexBrowserLastPick else initial
        if start == undefined then start = ""

        local picked = ::WhiteoutDexModelBrowser.pickEx mode:#texture initial:start \
                                                        filter:(_ntbSlotSearchSeed slot)
        if picked == undefined then return undefined

        local relPath = picked[1]
        local archivePath = picked[2]
        ::WhiteoutDexTexBrowserLastPick = relPath

        local localFile = ::WhiteoutDexModelBrowser.extractToTemp relPath archivePath
        if localFile == undefined or not (doesFileExist localFile) then return undefined

        #(localFile, relPath, archivePath)
    ),

    /* Pick a texture and do the obvious thing with it.

       With ::WhiteoutDexTexBrowserTarget set to #(<material>, "<slot>") - which
       is what the browse buttons on Wc3Material and Wc3Bitmap do - the pick is
       assigned to that slot. Without one, it becomes a new Warcraft3 material
       on the selection.

       The target is cleared afterwards: the picker is modal, so one show() is
       one assignment, and a target left standing would silently capture the
       next unrelated open. */
    public fn show =
    (
        local target = ::WhiteoutDexTexBrowserTarget
        local slotName = if target != undefined and target.count == 2 then target[2] else ""

        local got = this.pick slot:slotName
        if got == undefined then
        (
            ::WhiteoutDexTexBrowserTarget = undefined
            return undefined
        )

        local localFile = got[1]
        local relPath = got[2]

        if target != undefined and target.count == 2 then
        (
            if slotName == "bitmap" then
                _ntbAssignToBitmap target[1] localFile
            else
                _ntbAssignChecked target[1] slotName localFile archivePath:relPath
        )
        else
            _ntbApplyAsMaterial localFile

        ::WhiteoutDexTexBrowserTarget = undefined
        ok
    ),

    -- Kept because callers wrote it against the old modeless form. A modal
    -- picker has already closed by the time anything can call this; all that
    -- is left to do is drop the target it would otherwise have captured.
    public fn close =
    (
        ::WhiteoutDexTexBrowserTarget = undefined
    )
)

global WhiteoutDexTexBrowser = WhiteoutDexTexBrowserStruct()
