/* ============================================================================
   WhiteoutDexModelBrowser.ms

   MaxScript front door to the WhiteoutDex asset picker.

   The picker itself is native: WdxPickAsset (WhiteoutDexRenderer.dlx) opens
   WhiteoutFlakes' StorageExplorer modally - the archive browser that gives
   every visible entry a live thumbnail, shown either as a grid of icons or as
   a tree beside one large preview, with search, zoom and folder navigation.
   All this file does is find the install root, call it, and unpack the answer.

   The same window serves models, particle effects and textures; the type mask
   is the only difference. A texture cell is the decoded image rather than a
   rendered scene, and the tree view's preview pane adds its dimensions and
   source format - which is what the Texture Browser is built on.

   The install root may be either generation: a Reforged CASC directory, or a
   1.2x one holding War3.mpq and friends.

   What comes back is the archive-relative path with the Warcraft III mod chain
   stripped ("units\nightelf\druid\druid.mdx") - the form MDX stores, the
   exporter round-trips, and the renderer's content provider resolves. A local
   disk path from getOpenFileName is none of those things, which is why the
   places that set a game asset on a resource come through here.

   MaxScript API:

       ::WhiteoutDexModelBrowser.pickModel()          -- .mdx / .mdl
       ::WhiteoutDexModelBrowser.pickEffect()         -- .pkb / .pkfx
       ::WhiteoutDexModelBrowser.pickTexture()        -- .blp / .dds / .tga
       ::WhiteoutDexModelBrowser.pick mode:#any initial:"" caption:"..."

   All of these return the relative path as a string, or `undefined` when the
   user cancels. `initial` is a path in the same namespace: the picker opens on
   its folder; `filter` seeds its search box.

       ::WhiteoutDexModelBrowser.pickEx mode:#model
       ::WhiteoutDexModelBrowser.extractToTemp <relPath> <archivePath>

   pickEx returns #(relPath, archivePath) instead - the original archive entry
   as well, which is what identifies the exact file rather than the HD-or-SD
   pair. extractToTemp copies one entry onto disk and returns the local name,
   for the one caller that cannot take a path: Import from CASC, since Max
   hands importers a file name.

   Lives in post_startup_scripts because it calls into the .dlx: pre-startup
   scripts are evaluated before Max loads plug-ins, so the primitive would not
   exist yet at parse time.
   ============================================================================ */

-- io::BrowseType bits, mirroring the C++ enum.
global WdxMB_TYPE_MODELS   = 1
global WdxMB_TYPE_EFFECTS  = 2
global WdxMB_TYPE_TEXTURES = 32

-- True when WhiteoutDexRenderer.dlx is loaded and its primitives registered.
-- Calling an unregistered primitive raises "call to a non-function value",
-- which is what we translate into a friendly message.
fn wdxMBPrimitivesAvailable =
(
	local avail = false
	try ( WdxBrowserIsOpen() ; avail = true ) catch ( avail = false )
	avail
)

/* Every Warcraft III install this machine has, most-preferred first, as

       #( #(<label>, <directory>), ... )

   There are two settings and either may be the one that is filled in, because
   they were written for two different readers: the CASC path is a Reforged
   install, the MPQ directory a classic one, and a machine may well have both.
   Both are offered - the picker lists them by name in its File menu, so
   switching generations is a click and not a re-typed path - and the first is
   the one it opens on.

   Reforged first when both are set: it is what the exporter and the renderer
   target. Then the classic install, then whatever the game finder turns up.

   The label is worked out from what is on disk, not from the setting that
   named it: Battle.net registers a downgraded install under the Reforged
   product, so a classic install can perfectly well be the one written to
   [CASC] W3Path. Same rule as io::ClassifyStorage native-side - a .build.info
   with no War3LegacyInstaller beside it is Reforged, anything else classic. */
fn wdxMBAssetRoots =
(
	local settingsIni = (getDir #plugcfg) + "\\WhiteoutDex_Settings.ini"
	local mpqIni      = (getDir #plugcfg) + "\\WhiteoutDexMPQ.ini"

	-- A configured path that no longer exists is worse than none at all: it
	-- would be opened, fail, and blame the storage rather than the setting.
	fn wdxUsableDir p = ( p != undefined and p != "" and (doesFileExist p) )

	fn wdxRootLabel p =
	(
		local legacy = false
		try ( legacy = (getFiles (p + "\\War3LegacyInstaller.*")).count > 0 ) catch ( legacy = false )
		local casc = (doesFileExist (p + "\\.build.info")) or (doesFileExist (p + "\\Data\\.build.info"))
		if casc and not legacy then "Warcraft III Reforged" else "Warcraft III Classic"
	)

	-- One directory named twice - both INIs may point at it - is one entry.
	fn wdxSameDir a b = ((toLower (trimRight a "\\/")) == (toLower (trimRight b "\\/")))

	local candidates = #()
	local v = ""
	try ( v = getINISetting settingsIni "CASC" "W3Path" ) catch ( v = "" )
	append candidates v
	try ( v = getINISetting mpqIni "MPQ" "Directory" ) catch ( v = "" )
	append candidates v
	if ::WhiteoutDexCASC != undefined then
		try ( append candidates (::WhiteoutDexCASC.autoDetect()) ) catch()

	local found = #()
	for c in candidates where (wdxUsableDir c) do
	(
		local dup = false
		for e in found where (wdxSameDir e[2] c) do dup = true
		if not dup do append found #((wdxRootLabel c), c)
	)

	-- Two installs of the same generation would be indistinguishable in the
	-- menu; the folder is what the user tells them apart by.
	for i = 1 to found.count do
	(
		local clash = false
		for j = 1 to found.count where j != i and found[j][1] == found[i][1] do clash = true
		if clash do
			found[i][1] = found[i][1] + " (" + (filenameFromPath (trimRight found[i][2] "\\/")) + ")"
	)

	found
)

-- Just the directory the picker opens on, for a caller that takes one root.
fn wdxMBAssetRoot =
(
	local roots = wdxMBAssetRoots()
	if roots.count == 0 then "" else roots[1][2]
)

struct WhiteoutDexModelBrowserStruct
(
	-- True when the picker can run at all.
	public fn isAvailable = wdxMBPrimitivesAvailable(),

	/* Show the picker and return BOTH spellings of what the user chose:

	       #(<relative path>, <archive path>)

	   The relative path is what a resource stores; the archive path is the
	   original CASC entry, mod chain and all ("war3.w3mod:_hd.w3mod:units\..."),
	   which is what a reader needs to get at the exact file - the relative form
	   is ambiguous between the HD and SD copies of a model. `undefined` on
	   cancel.

	     mode:    #model (.mdx/.mdl), #effect (.pkb/.pkfx), #texture
	              (.blp/.dds/.tga) or #any
	     initial: a path in the same namespace to open on
	     filter:  seeds the picker's search box - substrings, `*`/`?` globs,
	              comma-separated alternatives, a leading `-` to exclude. For a
	              caller that knows more than the mode does: the Texture
	              Browser's normal-map button passes "_normal". */
	public fn pickEx mode:#any initial:"" filter:"" =
	(
		if not wdxMBPrimitivesAvailable() then
		(
			messageBox ("The WhiteoutDex asset picker is not available.\n\n" +
			            "WhiteoutDexRenderer.dlx did not load. Reinstall WhiteoutDex " +
			            "or check the Listener for plug-in load errors.") \
			           title:"WhiteoutDex" beep:false
			return undefined
		)

		local roots = wdxMBAssetRoots()
		local root = if roots.count == 0 then "" else roots[1][2]
		if root == "" then
		(
			messageBox ("No Warcraft III installation is configured.\n\n" +
			            "Set the CASC path in WhiteoutDex Settings first.") \
			           title:"WhiteoutDex" beep:false
			return undefined
		)

		local mask = case mode of
		(
			#model:   ::WdxMB_TYPE_MODELS
			#effect:  ::WdxMB_TYPE_EFFECTS
			#texture: ::WdxMB_TYPE_TEXTURES
			default:  (bit.or ::WdxMB_TYPE_MODELS ::WdxMB_TYPE_EFFECTS)
		)

		local start = if initial == undefined then "" else initial
		local seed = if filter == undefined then "" else filter
		local r = undefined
		-- The 5th argument (the named installs) is newer than the 4th. Falling
		-- back keeps a script package that has got ahead of the .dlx working,
		-- minus only the File menu shortcut.
		try ( r = WdxPickAsset root mask start seed roots ) \
		catch ( try ( r = WdxPickAsset root mask start seed ) catch ( r = undefined ) )

		if r == undefined or r.count < 3 do return undefined

		-- A 4th element means the picker could not run - see WdxPickAsset.
		if r.count >= 4 do
		(
			messageBox r[4] title:"WhiteoutDex" beep:false
			return undefined
		)

		if r[1] then #(r[2], r[3]) else undefined
	),

	/* The common case: just the relative path, or undefined on cancel.

	     caption: accepted for source compatibility; the native window titles
	              itself from `mode`. */
	public fn pick mode:#any initial:"" caption:"" filter:"" =
	(
		local p = this.pickEx mode:mode initial:initial filter:filter
		if p == undefined then undefined else p[1]
	),

	public fn pickModel initial:"" caption:"" =
		this.pick mode:#model initial:initial caption:caption,

	public fn pickEffect initial:"" caption:"" =
		this.pick mode:#effect initial:initial caption:caption,

	/* .blp / .dds / .tga. The same window, showing the decoded image in every
	   cell instead of a rendered scene, and adding the file's dimensions and
	   source format to the tree view's preview pane.

	   The Texture Browser (WhiteoutDexTextureBrowserHelper.ms) is a thin layer
	   over this: it picks, extracts to temp, and assigns to a material slot. */
	public fn pickTexture initial:"" caption:"" filter:"" =
		this.pick mode:#texture initial:initial caption:caption filter:filter,

	/* Copy one archive entry onto disk and return the local file name, or
	   undefined (having said why) when it could not be read.

	   For hosts that need a FILE rather than a path: Max's importer machinery
	   hands a plug-in a name and the plug-in opens it with the CRT, so nothing
	   in the import path can be given an archive entry directly. The extraction
	   mirrors the archive's folders under the temp directory, so two models with
	   the same leaf name do not collide and a re-import overwrites its own copy
	   rather than accumulating. Only the one file is extracted - textures are
	   resolved out of CASC by whoever reads it.

	     relPath     the stored form, used to place the file under temp
	     archivePath the original entry to read; pass relPath again if that is
	                 all you have and HD-first resolution is acceptable */
	public fn extractToTemp relPath archivePath =
	(
		if not wdxMBPrimitivesAvailable() then return undefined
		if relPath == undefined or relPath == "" then return undefined

		local root = wdxMBAssetRoot()
		if root == "" then return undefined

		local src = if archivePath == undefined or archivePath == "" then relPath else archivePath
		local dest = (getDir #temp) + "\\WhiteoutDexCASC\\" + relPath

		local err = "The WhiteoutDex extractor is not available."
		try ( err = WdxExtractAsset root src dest ) catch ( err = "Extraction failed." )

		if err != "" then
		(
			messageBox err title:"WhiteoutDex" beep:false
			return undefined
		)
		dest
	),

	-- Release the headless browser's archive handle and walked tree (the
	-- WdxBrowser* listing API, not the picker - that opens and closes its own).
	public fn release = ( try ( WdxBrowserClose() ) catch() )
)

global WhiteoutDexModelBrowser = WhiteoutDexModelBrowserStruct()
