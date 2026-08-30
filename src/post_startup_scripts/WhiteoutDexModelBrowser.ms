/* ============================================================================
   WhiteoutDexModelBrowser.ms

   MaxScript front door to the WhiteoutDex asset picker.

   The picker itself is native: WdxPickAsset (WhiteoutDexRenderer.dlx) opens
   WhiteoutFlakes' StorageExplorer modally - the CASC browser that renders every
   visible entry as a live model thumbnail, shown either as a grid of icons or
   as a tree beside one large preview, with search, zoom and folder navigation.
   All this file does is find the install root, call it, and unpack the answer.

   What comes back is the archive-relative path with the Warcraft III mod chain
   stripped ("units\nightelf\druid\druid.mdx") - the form MDX stores, the
   exporter round-trips, and the renderer's content provider resolves. A local
   disk path from getOpenFileName is none of those things, which is why the
   places that set a game asset on a resource come through here.

   MaxScript API:

       ::WhiteoutDexModelBrowser.pickModel()          -- .mdx / .mdl
       ::WhiteoutDexModelBrowser.pickEffect()         -- .pkb / .pkfx
       ::WhiteoutDexModelBrowser.pick mode:#any initial:"" caption:"..."

   All three return the relative path as a string, or `undefined` when the user
   cancels. `initial` is a path in the same namespace: the picker opens on its
   folder.

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
global WdxMB_TYPE_MODELS  = 1
global WdxMB_TYPE_EFFECTS = 2

-- True when WhiteoutDexRenderer.dlx is loaded and its primitives registered.
-- Calling an unregistered primitive raises "call to a non-function value",
-- which is what we translate into a friendly message.
fn wdxMBPrimitivesAvailable =
(
	local avail = false
	try ( WdxBrowserIsOpen() ; avail = true ) catch ( avail = false )
	avail
)

-- The Warcraft III install the picker browses. The Settings dialog writes it;
-- an unconfigured install falls back to WhiteoutLib's game finder.
fn wdxMBCascRoot =
(
	local ini = (getDir #plugcfg) + "\\WhiteoutDex_Settings.ini"
	local w3 = ""
	try ( w3 = getINISetting ini "CASC" "W3Path" ) catch ( w3 = "" )
	if (w3 == undefined or w3 == "") and ::WhiteoutDexCASC != undefined then
		try ( w3 = ::WhiteoutDexCASC.autoDetect() ) catch ( w3 = "" )
	if w3 == undefined then "" else w3
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

	     mode:    #model (.mdx/.mdl), #effect (.pkb/.pkfx) or #any
	     initial: a path in the same namespace to open on */
	public fn pickEx mode:#any initial:"" =
	(
		if not wdxMBPrimitivesAvailable() then
		(
			messageBox ("The WhiteoutDex asset picker is not available.\n\n" +
			            "WhiteoutDexRenderer.dlx did not load. Reinstall WhiteoutDex " +
			            "or check the Listener for plug-in load errors.") \
			           title:"WhiteoutDex" beep:false
			return undefined
		)

		local root = wdxMBCascRoot()
		if root == "" then
		(
			messageBox ("No Warcraft III installation is configured.\n\n" +
			            "Set the CASC path in WhiteoutDex Settings first.") \
			           title:"WhiteoutDex" beep:false
			return undefined
		)

		local mask = case mode of
		(
			#model:  ::WdxMB_TYPE_MODELS
			#effect: ::WdxMB_TYPE_EFFECTS
			default: (bit.or ::WdxMB_TYPE_MODELS ::WdxMB_TYPE_EFFECTS)
		)

		local start = if initial == undefined then "" else initial
		local r = undefined
		try ( r = WdxPickAsset root mask start ) catch ( r = undefined )

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
	public fn pick mode:#any initial:"" caption:"" =
	(
		local p = this.pickEx mode:mode initial:initial
		if p == undefined then undefined else p[1]
	),

	public fn pickModel initial:"" caption:"" =
		this.pick mode:#model initial:initial caption:caption,

	public fn pickEffect initial:"" caption:"" =
		this.pick mode:#effect initial:initial caption:caption,

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

		local root = wdxMBCascRoot()
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
