/*
    TextureAnimCreator.ms
    =====================
    Texture Animation Creator: the animation generator of the NeoDex Wc3
    material as a WhiteoutDex tool of its own.

      Flow         UV scroll / rotation / scaling (waterfalls, lava, auras):
                   linear keys on the Wc3Bitmap's anim_* channels (TXAN).
      Sprite Sheet one texture holding a grid of frames (hits, explosions):
                   step keys (MDX DontInterp) on anim_UOffset/anim_VOffset,
                   one key per frame. Picking the object, material or
                   texture reads the texture and counts its columns, rows
                   and frames (see WdxTAC_DetectGrid).
      Sequence     a folder of single frame images: the Wc3Material's exact
                   flipbook (flipbookTextures + stepped flipbookFrame, KMTF)
                   plus an IFL on the diffuse map as viewport preview.

    "Global animation" keys the channel from frame 0 with a cycling
    out-of-range type: the exporter writes that as a Global Sequence
    (global_sequence_helper.h, getGlobalSequenceDuration).

    The Wc3Bitmap's anim_* parameters share their controllers with the
    Bitmaptexture coordinates (Wc3Bitmap.ms), so the viewport plays what the
    exporter writes. All functions are globals (WdxTAC_*): Max 2016 cannot
    reach struct members from a struct method (see MaterialFix.ms).

    Compatibility: 3ds Max 2016 - 2027.
*/

global WdxTAC_Tr
global WdxTAC_FindMaterials
global WdxTAC_MapsOf
global WdxTAC_MapFile
global WdxTAC_LoadSmall
global WdxTAC_ContentGrid
global WdxTAC_GapCount
global WdxTAC_DetectGrid
global WdxTAC_CellOrder
global WdxTAC_ClearChannel
global WdxTAC_KeyChannel
global WdxTAC_SetInterp
global WdxTAC_ApplyFlow
global WdxTAC_ApplySpriteSheet
global WdxTAC_ApplySequence
global WdxTAC_GenerateUVMap
global WdxTAC_SequenceFiles
global WdxTexAnimCreator
global WdxTAC_IconFile
global WdxTAC_ArrowImages
global WdxTAC_Dotted
global WdxTAC_FileInfo

-- Localized text with an English fallback.
fn WdxTAC_Tr key fallback =
(
	if ::WdxL == undefined then fallback
	else
	(
		local v = try (::WdxL.t key) catch (key)
		if v == undefined or v == key or v == "" then fallback else v
	)
)

-- Wc3 materials of a node (also inside Multi/Sub-Object and Composite).
fn WdxTAC_FindMaterials m =
(
	local out = #()
	if m == undefined do return out
	if classOf m == Wdx_Wc3Material then append out m
	else
	(
		local subs = try (m.materialList) catch (undefined)
		if subs == undefined do subs = try (for i = 1 to getNumSubMtls m collect (getSubMtl m i)) catch (#())
		for s in subs where s != undefined do join out (WdxTAC_FindMaterials s)
	)
	makeUniqueArray out
)

-- #(slot name, Wc3Bitmap) of every Wc3Bitmap map of a Wc3 material.
fn WdxTAC_MapsOf mat =
(
	local out = #()
	if mat == undefined do return out
	for p in #(#diffuseMap, #emissiveMap, #teamColorMap, #normalMap, #ormMap, #environmentMap) do
	(
		local m = try (getProperty mat p) catch (undefined)
		if m != undefined and classOf m == Wc3Bitmap do
			append out #(substituteString (p as string) "Map" "", m)
	)
	out
)

-- The image file behind a Wc3Bitmap ("" when none).
fn WdxTAC_MapFile map =
(
	if map == undefined do return ""
	local f = try (map.delegate.filename) catch (undefined)
	if f == undefined do f = try (map.filename) catch ("")
	if f == undefined then "" else f
)

-- The image scaled so its long side is at most maxSide pixels, and its
-- original size: #(bitmap, width, height), undefined when it cannot load.
fn WdxTAC_LoadSmall fname maxSide =
(
	local src = undefined
	if fname != "" and doesFileExist fname do src = try (openBitMap fname) catch (undefined)
	if src == undefined do return undefined
	local w = src.width, h = src.height
	local s = amin #(1.0, maxSide as float / (amax #(w, h)))
	local sw = amax #(1, (w * s + 0.5) as integer)
	local sh = amax #(1, (h * s + 0.5) as integer)
	local small = bitmap sw sh
	copy src small
	close src
	#(small, w, h)
)

-- How much "picture" each pixel holds: its alpha when the image has an
-- alpha channel that is used, else its brightness difference to the
-- background (the median of the border pixels). #(width, height, values)
-- with values[y * width + x + 1] in 0..1.
fn WdxTAC_ContentGrid bm =
(
	local w = bm.width, h = bm.height
	local rows = for y = 0 to h - 1 collect (getPixels bm [0, y] w)
	local useAlpha = false
	for r in rows while not useAlpha do for c in r while not useAlpha do if c.a < 250 do useAlpha = true
	local bgLum = 0.0
	if not useAlpha do
	(
		local border = #()
		for c in rows[1] do append border ((c.r + c.g + c.b) / 3.0)
		for c in rows[h] do append border ((c.r + c.g + c.b) / 3.0)
		for y = 1 to h do (append border ((rows[y][1].r + rows[y][1].g + rows[y][1].b) / 3.0); append border ((rows[y][w].r + rows[y][w].g + rows[y][w].b) / 3.0))
		sort border
		bgLum = border[(border.count + 1) / 2]
	)
	local vals = #()
	vals[w * h] = 0.0
	for y = 1 to h do
		for x = 1 to w do
		(
			local c = rows[y][x]
			vals[(y - 1) * w + x] = if useAlpha then c.a / 255.0 else (abs (((c.r + c.g + c.b) / 3.0) - bgLum)) / 255.0
		)
	#(w, h, vals)
)

-- The largest cell count 2..16 along a profile whose cell borders are all
-- (nearly) empty; 1 when no count has empty borders.
fn WdxTAC_GapCount prof =
(
	local n = prof.count
	local mean = 0.0
	for v in prof do mean += v
	mean /= n
	if mean <= 0.0 do return 1
	local best = 1
	for cells = 2 to 16 where n / cells >= 3 do
	(
		local ok = true
		for k = 1 to cells - 1 while ok do
		(
			local pos = k * n as float / cells
			local lo = amax #(1, (floor pos) as integer), hi = amin #(n, ((ceil pos) as integer) + 1)
			local m = 1e9
			for i = lo to hi do m = amin #(m, prof[i])
			if m > 0.08 * mean do ok = false
		)
		if ok do best = cells
	)
	best
)

-- Columns, rows and used frames of a sprite sheet:
-- #(columns, rows, frames, "how it was found").
-- 1. A size in the file name ("fire_5x5.tga", "Smoke8x4") wins.
-- 2. Empty lines between the frames: the most cells with empty borders.
-- 3. Only one axis found: the frames are square.
-- Frames: the cells that hold picture, up to the last one (a 5x5 sheet
-- with 23 pictures has 23 frames).
fn WdxTAC_DetectGrid fname =
(
	local cols = 1, rows = 1, how = ""
	local nm = getFilenameFile fname
	local rx = try (dotNetObject "System.Text.RegularExpressions.Regex" "(\\d{1,2})\\s*[xX\\u00D7]\\s*(\\d{1,2})") catch (undefined)
	if rx != undefined do
	(
		local mt = rx.Match nm
		if mt.Success do
		(
			cols = mt.Groups.Item[1].Value as integer
			rows = mt.Groups.Item[2].Value as integer
			how = "name"
		)
	)
	local ld = WdxTAC_LoadSmall fname 256
	if ld == undefined do return #(amax #(cols, 1), amax #(rows, 1), cols * rows, (if how == "" then "none" else how))
	local g = WdxTAC_ContentGrid ld[1]
	local w = g[1], h = g[2], vals = g[3]
	if how == "" do
	(
		local colP = for x = 1 to w collect (local s = 0.0; for y = 1 to h do s += vals[(y - 1) * w + x]; s)
		local rowP = for y = 1 to h collect (local s = 0.0; for x = 1 to w do s += vals[(y - 1) * w + x]; s)
		cols = WdxTAC_GapCount colP
		rows = WdxTAC_GapCount rowP
		how = "gaps"
		-- one axis only: square frames
		if cols > 1 and rows == 1 do rows = amax #(1, ((ld[3] as float) / (ld[2] as float / cols) + 0.5) as integer)
		if rows > 1 and cols == 1 do cols = amax #(1, ((ld[2] as float) / (ld[3] as float / rows) + 0.5) as integer)
		if cols == 1 and rows == 1 do how = "none"
	)
	-- used frames: cells with picture, up to the last one
	local cellMean = #()
	for r = 0 to rows - 1 do for c = 0 to cols - 1 do
	(
		local x0 = (c * w / cols) + 1, x1 = ((c + 1) * w / cols)
		local y0 = (r * h / rows) + 1, y1 = ((r + 1) * h / rows)
		local s = 0.0, n = 0
		for y = y0 to y1 do for x = x0 to x1 do (s += vals[(y - 1) * w + x]; n += 1)
		append cellMean (if n > 0 then s / n else 0.0)
	)
	local top = 0.0
	for v in cellMean do top = amax #(top, v)
	local used = 0
	for i = 1 to cellMean.count where cellMean[i] > 0.1 * top do used = i
	if used == 0 do used = cols * rows
	free ld[1]
	#(cols, rows, used, how)
)

-- Cell indices (0-based) in play order: range, every (skip + 1)th, reversed
-- when asked, repeated loops times.
fn WdxTAC_CellOrder firstKey lastKey skip reverseOrder loops =
(
	local cells = #()
	local i = 0
	for c = firstKey to lastKey do
	(
		if mod i (skip + 1) == 0 do append cells (c - 1)
		i += 1
	)
	if reverseOrder do cells = for k = cells.count to 1 by -1 collect cells[k]
	local out = #()
	for l = 1 to amax #(1, loops) do join out cells
	out
)

-- Removes the keys of an anim_* channel (keeps the value).
fn WdxTAC_ClearChannel map prop =
(
	local c = try (getPropertyController map prop) catch (undefined)
	if c != undefined do try (deleteKeys c #allKeys) catch ()
)

-- Keys values[i] at times[i] on an anim_* channel of a Wc3Bitmap with a
-- controller of class ctrlClass; keep:false removes the old keys first.
-- Returns the controller.
fn WdxTAC_KeyChannel map prop times values ctrlClass keep:false cycle:false =
(
	local c = try (getPropertyController map prop) catch (undefined)
	if c == undefined or classOf c != ctrlClass do
	(
		setPropertyController map prop (ctrlClass())
		c = getPropertyController map prop
	)
	if not keep do deleteKeys c #allKeys
	for i = 1 to times.count do
	(
		local k = addNewKey c times[i]
		k.value = values[i]
	)
	setBeforeORT c #constant
	setAfterORT c (if cycle then #cycle else #constant)
	-- the viewport side: the Bitmaptexture coordinates share the controller
	local coordProp = case prop of
	(
		#anim_UOffset: #U_Offset
		#anim_VOffset: #V_Offset
		#anim_WAngle: #W_Angle
		#anim_UTiling: #U_Tiling
		#anim_VTiling: #V_Tiling
		default: undefined
	)
	if coordProp != undefined do try (setPropertyController map.delegate.coords coordProp c) catch ()
	c
)

-- Step (hold) tangents on every key of a Bezier_Float: MDX DontInterp.
fn WdxTAC_SetInterp c stepKeys =
(
	if stepKeys and classOf c == Bezier_Float do
		for k in c.keys do (k.inTangentType = #step; k.outTangentType = #step)
)

-- Flow: scroll (dirU, dirV in -1/0/1), rotate (dirRot -1/0/1, one full turn)
-- and scale (dirScale -1/0/1: 0.5x / 2x) over [t0, t1]. Global: from frame 0
-- for t1 - t0 frames, cycling. Returns the number of channels keyed.
fn WdxTAC_ApplyFlow map t0 t1 dirU dirV dirRot dirScale isGlobal keep =
(
	if map == undefined or t1 <= t0 do return 0
	local a = if isGlobal then 0 else t0
	local b = if isGlobal then t1 - t0 else t1
	local times = #(a, b)
	local n = 0
	if dirU != 0 do (WdxTAC_KeyChannel map #anim_UOffset times #(0.0, dirU as float) Linear_Float keep:keep cycle:isGlobal; map.wrapU = true; n += 1)
	if dirV != 0 do (WdxTAC_KeyChannel map #anim_VOffset times #(0.0, dirV as float) Linear_Float keep:keep cycle:isGlobal; map.wrapV = true; n += 1)
	-- a full turn keyed every 90 degrees: the MDX stores rotations as
	-- quaternions, and 0 and 360 degrees are the same one - two keys would
	-- not turn at all in the game
	if dirRot != 0 do
	(
		local rt = for k = 0 to 4 collect (a + (b - a) * k / 4.0)
		local rv = for k = 0 to 4 collect (90.0 * k * dirRot)
		WdxTAC_KeyChannel map #anim_WAngle rt rv Linear_Float keep:keep cycle:isGlobal
		n += 1
	)
	if dirScale != 0 do
	(
		local s = if dirScale > 0 then 2.0 else 0.5
		WdxTAC_KeyChannel map #anim_UTiling times #(1.0, s) Linear_Float keep:keep cycle:isGlobal
		WdxTAC_KeyChannel map #anim_VTiling times #(1.0, s) Linear_Float keep:keep cycle:isGlobal
		n += 2
	)
	try (map.uvCtrlType = 2) catch ()   -- Linear
	n
)

-- Sprite sheet: one step key per frame on anim_UOffset/anim_VOffset. The
-- mesh UVs show the top left cell (WdxTAC_GenerateUVMap); frame c,r moves
-- the texture by -c/columns in U and +r/rows in V (Bitmaptexture offsets
-- move the picture, not the lookup). frameStep: Max frames per picture.
-- Global: from frame 0, with a closing key back on the first picture so the
-- cycle length covers the last picture too. Returns the key count.
fn WdxTAC_ApplySpriteSheet map cols rows cells t0 frameStep isGlobal keep =
(
	if map == undefined or cells.count == 0 do return 0
	local start = if isGlobal then 0 else t0
	local times = #(), us = #(), vs = #()
	for i = 1 to cells.count do
	(
		local r = cells[i] / cols, c = cells[i] - r * cols
		append times (start + (i - 1) * frameStep)
		append us (-(c as float) / cols)
		append vs ((r as float) / rows)
	)
	if isGlobal do
	(
		append times (start + cells.count * frameStep)
		append us us[1]; append vs vs[1]
	)
	local cu = WdxTAC_KeyChannel map #anim_UOffset times us Bezier_Float keep:keep cycle:isGlobal
	local cv = WdxTAC_KeyChannel map #anim_VOffset times vs Bezier_Float keep:keep cycle:isGlobal
	WdxTAC_SetInterp cu true
	WdxTAC_SetInterp cv true
	try (map.uvCtrlType = 1) catch ()   -- None (DontInterp)
	times.count
)

-- UVs of the nodes shrunk to the top left cell of a cols x rows sheet (a
-- UVW_Xform, collapsed into the stack bottom when collapse is on).
fn WdxTAC_GenerateUVMap nodes cols rows collapse =
(
	local n = 0
	for o in nodes where isValidNode o and superClassOf o == GeometryClass do
	(
		local xf = UVW_Xform()
		xf.U_Tile = 1.0 / cols
		xf.V_Tile = 1.0 / rows
		xf.V_Offset = (rows - 1) as float / rows
		if collapse then
		(
			addModifier o xf before:o.modifiers.count
			maxOps.CollapseNodeTo o o.modifiers.count off
		)
		else addModifier o xf
		n += 1
	)
	n
)

-- The image files of a frame sequence folder, sorted by name.
fn WdxTAC_SequenceFiles folder =
(
	local out = #()
	if folder == undefined or folder == "" do return out
	for ext in #("*.blp", "*.dds", "*.tga", "*.png", "*.jpg", "*.bmp") do join out (getFiles (folder + "\\" + ext))
	sort out
	out
)

-- Sequence: the material's exact flipbook - one texture per file (MDX path
-- prefix + file name) and a stepped flipbookFrame
-- key per picture - plus an IFL next to the pictures so the viewport plays
-- them. Returns the picture count.
fn WdxTAC_ApplySequence mat files prefix t0 frameStep reverseOrder isGlobal keep =
(
	if mat == undefined or files.count == 0 do return 0
	local order = if reverseOrder then (for k = files.count to 1 by -1 collect files[k]) else files
	-- the exporter gives the path the extension of the export format
	local entries = for f in order collect ("0|0|" + prefix + (filenameFromPath f))
	mat.flipbookTextures = entries
	mat.flipbook = true
	local start = if isGlobal then 0 else t0
	local c = try (getPropertyController mat #flipbookFrame) catch (undefined)
	if c == undefined or classOf c != Bezier_Float do
	(
		setPropertyController mat #flipbookFrame (Bezier_Float())
		c = getPropertyController mat #flipbookFrame
	)
	if not keep do deleteKeys c #allKeys
	for i = 1 to entries.count do (local k = addNewKey c (start + (i - 1) * frameStep); k.value = (i - 1) as float)
	if isGlobal do (local k = addNewKey c (start + entries.count * frameStep); k.value = 0.0)
	WdxTAC_SetInterp c true
	setAfterORT c (if isGlobal then #cycle else #constant)
	-- viewport preview: an IFL of the pictures on the diffuse map
	try
	(
		local ifl = (getFilenamePath order[1]) + "WdxSequence.ifl"
		local fs = createFile ifl encoding:#utf8 writeBOM:true
		for f in order do format "%\n" f to:fs
		close fs
		local dm = mat.diffuseMap
		if dm == undefined or classOf dm != Wc3Bitmap do (dm = Wc3Bitmap(); mat.diffuseMap = dm)
		dm.delegate.filename = ifl
		try (dm.delegate.startTime = start; dm.delegate.playBackRate = 1.0 / frameStep; dm.delegate.endCondition = (if isGlobal then 0 else 2)) catch ()
	)
	catch ()
	entries.count
)

-------------------------------------------------------------------------------
-- UI (layout after the NeoDex material's animation panel: coloured tabs,
-- labels in the tab colour with dotted leaders, ON/OFF pairs, arrow icons,
-- an info box with the animation length and one big button)
-------------------------------------------------------------------------------

-- The toolkit's icon folder (installed package), "" when unknown.
fn WdxTAC_IconFile nm =
(
	local root = try (::WhiteoutDexInstallRoot) catch (undefined)
	local f = if root == undefined then "" else root + "\\whiteoutdex_icons\\" + nm
	if f != "" and doesFileExist f then f else ""
)

-- The two-state image strip of an arrow checkbutton (off / on, 20x20 each);
-- a plain grey strip when the icons are not installed.
fn WdxTAC_ArrowImages nm =
(
	local f = WdxTAC_IconFile nm
	if f != "" then #(f, undefined, 2, 1, 2, 1, 2)
	else #(bitmap 40 20 color:(color 70 70 70), undefined, 2, 1, 2, 1, 2)
)

-- "name.tga  (320 x 320)" for the target line; the full path is too long.
fn WdxTAC_FileInfo f =
(
	if f == undefined or f == "" do return ""
	local s = filenameFromPath f
	local b = try (openBitMap f) catch (undefined)
	if b != undefined do (s += "   (" + (b.width as string) + " x " + (b.height as string) + ")"; close b)
	s
)

-- Text padded with dots to a fixed character width ("Direction ........").
fn WdxTAC_Dotted txt n =
(
	local s = trimRight txt ": "
	s += " "
	for i = s.count to n do s += "."
	s
)

rollout WdxTexAnimCreatorRoll "Texture Animation Creator"
(
	local mats = #(), maps = #(), curTab = 2
	local isGlobal = false, keepKeys = false, collapseUV = true
	-- colours after the NeoDex panel: tab highlights, label text, ON green, OFF red

	fn hasMaterial o = (o.material != undefined)

	-- ── Target ────────────────────────────────────────────────────────────
	groupBox grpTarget "Target" pos:[6, 4] width:328 height:118
	pickbutton pbObj "Pick Object" pos:[14, 22] width:100 height:22 filter:hasMaterial tooltip:"Pick an object with a Wc3 material"
	button btnSel "Selection" pos:[120, 22] width:100 height:22 tooltip:"Use the material of the selected object"
	button btnMedit "Material Editor" pos:[226, 22] width:100 height:22 tooltip:"Use the material in the active Material Editor slot"
	label lblMat "Material" pos:[14, 54]
	dropdownlist ddMat "" pos:[80, 50] width:246
	label lblMap "Texture" pos:[14, 80]
	dropdownlist ddMap "" pos:[80, 76] width:246
	label lblFile "" pos:[14, 102] width:312

	-- ── Tabs ──────────────────────────────────────────────────────────────
	checkbutton tabFlow "Flow Anim" pos:[6, 130] width:104 height:24 highlightColor:(color 176 97 19) tooltip:"Scroll, rotate or scale the texture (water, lava, auras)"
	checkbutton tabSheet "SEQ Anim (Solo)" pos:[118, 130] width:104 height:24 highlightColor:(color 51 131 65) checked:true tooltip:"One texture with a grid of frames (hits, explosions)"
	checkbutton tabSeq "SEQ Anim (Many)" pos:[230, 130] width:104 height:24 highlightColor:(color 10 130 144) tooltip:"A folder of single frame pictures (flipbook)"
	groupBox grpPanel "" pos:[6, 156] width:328 height:232

	-- ── Flow ──────────────────────────────────────────────────────────────
	hyperLink flStartLbl "Start time:" pos:[16, 174] width:74 color:(color 255 165 86) enabled:false
	spinner flStart "" pos:[92, 172] fieldwidth:60 range:[-100000, 100000, 0] type:#integer
	hyperLink flEndLbl "End time:" pos:[180, 174] width:70 color:(color 255 165 86) enabled:false
	spinner flEnd "" pos:[252, 172] fieldwidth:60 range:[-100000, 100000, 30] type:#integer
	hyperLink flDirLbl "Direction" pos:[16, 201] width:190 color:(color 255 165 86) enabled:false
	checkbutton flUp "" pos:[222, 198] width:22 height:22 images:(WdxTAC_ArrowImages "wdx_up.bmp") highlightColor:(color 176 97 19) tooltip:"Scroll up"
	checkbutton flDown "" pos:[248, 198] width:22 height:22 images:(WdxTAC_ArrowImages "wdx_down.bmp") highlightColor:(color 176 97 19) tooltip:"Scroll down"
	checkbutton flLeft "" pos:[274, 198] width:22 height:22 images:(WdxTAC_ArrowImages "wdx_left.bmp") highlightColor:(color 176 97 19) tooltip:"Scroll left"
	checkbutton flRight "" pos:[300, 198] width:22 height:22 images:(WdxTAC_ArrowImages "wdx_right.bmp") highlightColor:(color 176 97 19) tooltip:"Scroll right"
	hyperLink flRotLbl "Rotation" pos:[16, 228] width:190 color:(color 255 165 86) enabled:false
	checkbutton flRotL "L" pos:[222, 225] width:48 height:22 highlightColor:(color 176 97 19) tooltip:"One turn counter-clockwise"
	checkbutton flRotR "R" pos:[274, 225] width:48 height:22 highlightColor:(color 176 97 19) tooltip:"One turn clockwise"
	hyperLink flSclLbl "Scaling" pos:[16, 255] width:190 color:(color 255 165 86) enabled:false
	checkbutton flSclUp "+" pos:[222, 252] width:48 height:22 highlightColor:(color 176 97 19) tooltip:"Grow to 2x"
	checkbutton flSclDn "-" pos:[274, 252] width:48 height:22 highlightColor:(color 176 97 19) tooltip:"Shrink to 0.5x"
	hyperLink flGlobLbl "Global animation" pos:[16, 282] width:190 color:(color 255 165 86) enabled:false
	checkbutton flGlobOn "ON" pos:[222, 279] width:48 height:22 highlightColor:(color 76 141 0)
	checkbutton flGlobOff "OFF" pos:[274, 279] width:48 height:22 highlightColor:(color 144 34 34) checked:true
	hyperLink flKeepLbl "Keep existing animation" pos:[16, 309] width:190 color:(color 255 165 86) enabled:false
	checkbutton flKeepOn "ON" pos:[222, 306] width:48 height:22 highlightColor:(color 76 141 0)
	checkbutton flKeepOff "OFF" pos:[274, 306] width:48 height:22 highlightColor:(color 144 34 34) checked:true

	-- ── Sprite sheet (SEQ Solo) ───────────────────────────────────────────
	hyperLink shStartLbl "Start time:" pos:[16, 174] width:74 color:(color 106 225 127) enabled:false
	spinner shStart "" pos:[92, 172] fieldwidth:60 range:[-100000, 100000, 0] type:#integer
	hyperLink shLoopsLbl "Loop:" pos:[180, 174] width:70 color:(color 106 225 127) enabled:false
	spinner shLoops "" pos:[252, 172] fieldwidth:60 range:[1, 100, 1] type:#integer
	hyperLink shColsLbl "Columns:" pos:[16, 201] width:74 color:(color 106 225 127) enabled:false
	spinner shCols "" pos:[92, 199] fieldwidth:60 range:[1, 64, 4] type:#integer
	hyperLink shRowsLbl "Rows:" pos:[180, 201] width:70 color:(color 106 225 127) enabled:false
	spinner shRows "" pos:[252, 199] fieldwidth:60 range:[1, 64, 4] type:#integer
	hyperLink shFirstLbl "Start key:" pos:[16, 228] width:74 color:(color 106 225 127) enabled:false
	spinner shFirst "" pos:[92, 226] fieldwidth:60 range:[1, 4096, 1] type:#integer
	hyperLink shLastLbl "End key:" pos:[180, 228] width:70 color:(color 106 225 127) enabled:false
	spinner shLast "" pos:[252, 226] fieldwidth:60 range:[1, 4096, 16] type:#integer
	hyperLink shSkipLbl "Frame skipping" pos:[16, 255] width:190 color:(color 106 225 127) enabled:false
	dropdownlist shSkip "" pos:[214, 251] width:108 items:#("0f", "1f", "2f", "3f", "4f", "5f", "6f", "7f", "8f")
	hyperLink shOrdLbl "Play order" pos:[16, 282] width:190 color:(color 106 225 127) enabled:false
	dropdownlist shOrder "" pos:[214, 278] width:108 items:#("Forward play", "Reverse play")
	hyperLink shSpdLbl "Play speed" pos:[16, 309] width:190 color:(color 106 225 127) enabled:false
	dropdownlist shSpeed "" pos:[214, 305] width:108 items:#("10F/S", "15F/S", "30F/S", "60F/S") selection:3
	hyperLink shGlobLbl "Global animation" pos:[16, 336] width:190 color:(color 106 225 127) enabled:false
	checkbutton shGlobOn "ON" pos:[222, 333] width:48 height:22 highlightColor:(color 76 141 0)
	checkbutton shGlobOff "OFF" pos:[274, 333] width:48 height:22 highlightColor:(color 144 34 34) checked:true
	hyperLink shKeepLbl "Keep existing animation" pos:[16, 363] width:190 color:(color 106 225 127) enabled:false
	checkbutton shKeepOn "ON" pos:[222, 360] width:48 height:22 highlightColor:(color 76 141 0)
	checkbutton shKeepOff "OFF" pos:[274, 360] width:48 height:22 highlightColor:(color 144 34 34) checked:true
	groupBox grpUV "" pos:[6, 390] width:328 height:92
	hyperLink shGridLbl "" pos:[16, 405] width:230 color:(color 106 225 127) enabled:false
	button btnDetect "Detect" pos:[256, 402] width:66 height:20 tooltip:"Count columns, rows and frames of the texture again"
	button btnUV "Generate UV Map" pos:[14, 428] width:312 height:24 tooltip:"Shrink the selected objects' UVs to the first cell of the grid"
	hyperLink shColLbl "Collapse modifier" pos:[16, 461] width:190 color:(color 106 225 127) enabled:false
	checkbutton shColOn "ON" pos:[222, 458] width:48 height:22 highlightColor:(color 76 141 0) checked:true
	checkbutton shColOff "OFF" pos:[274, 458] width:48 height:22 highlightColor:(color 144 34 34)

	-- ── Sequence (SEQ Many) ───────────────────────────────────────────────
	hyperLink sqOrdLbl "Play order" pos:[16, 174] width:190 color:(color 85 204 217) enabled:false
	dropdownlist sqOrder "" pos:[214, 170] width:108 items:#("Forward play", "Reverse play")
	hyperLink sqSpdLbl "Play speed" pos:[16, 201] width:190 color:(color 85 204 217) enabled:false
	dropdownlist sqSpeed "" pos:[214, 197] width:108 items:#("10F/S", "15F/S", "30F/S", "60F/S") selection:3
	hyperLink sqStartLbl "Start time" pos:[16, 228] width:190 color:(color 85 204 217) enabled:false
	spinner sqStart "" pos:[252, 226] fieldwidth:60 range:[-100000, 100000, 0] type:#integer
	hyperLink sqPrefixLbl "MDX path" pos:[16, 255] width:80 color:(color 85 204 217) enabled:false
	edittext sqPrefix "" pos:[96, 253] width:226 text:"Textures\\"
	hyperLink sqGlobLbl "Global animation" pos:[16, 282] width:190 color:(color 85 204 217) enabled:false
	checkbutton sqGlobOn "ON" pos:[222, 279] width:48 height:22 highlightColor:(color 76 141 0)
	checkbutton sqGlobOff "OFF" pos:[274, 279] width:48 height:22 highlightColor:(color 144 34 34) checked:true
	hyperLink sqKeepLbl "Keep existing animation" pos:[16, 309] width:190 color:(color 85 204 217) enabled:false
	checkbutton sqKeepOn "ON" pos:[222, 306] width:48 height:22 highlightColor:(color 76 141 0)
	checkbutton sqKeepOff "OFF" pos:[274, 306] width:48 height:22 highlightColor:(color 144 34 34) checked:true
	groupBox grpSeq "" pos:[6, 336] width:328 height:66
	hyperLink sqPathLbl "Sequence frame folder path:" pos:[16, 350] width:230 color:(color 85 204 217) enabled:false
	edittext sqPath "" pos:[12, 370] width:274 readOnly:true
	button btnSqPath "..." pos:[292, 369] width:30 height:20

	-- ── Info + generate ───────────────────────────────────────────────────
	groupBox grpInfo "" pos:[6, 488] width:328 height:40
	label lblKeys "" pos:[16, 505] width:150
	label lblTime "" pos:[176, 505] width:150
	button btnGenerate "Generate Animation" pos:[6, 536] width:328 height:30
	label lblResult "" pos:[10, 572] width:320

	fn tabControls t =
	(
		case t of
		(
			1: #(flStartLbl, flStart, flEndLbl, flEnd, flDirLbl, flUp, flDown, flLeft, flRight, flRotLbl, flRotL, flRotR, flSclLbl, flSclUp, flSclDn, flGlobLbl, flGlobOn, flGlobOff, flKeepLbl, flKeepOn, flKeepOff)
			2: #(shStartLbl, shStart, shLoopsLbl, shLoops, shColsLbl, shCols, shRowsLbl, shRows, shFirstLbl, shFirst, shLastLbl, shLast, shSkipLbl, shSkip, shOrdLbl, shOrder, shSpdLbl, shSpeed, shGlobLbl, shGlobOn, shGlobOff, shKeepLbl, shKeepOn, shKeepOff, grpUV, shGridLbl, btnDetect, btnUV, shColLbl, shColOn, shColOff)
			default: #(sqOrdLbl, sqOrder, sqSpdLbl, sqSpeed, sqStartLbl, sqStart, sqPrefixLbl, sqPrefix, sqGlobLbl, sqGlobOn, sqGlobOff, sqKeepLbl, sqKeepOn, sqKeepOff, grpSeq, sqPathLbl, sqPath, btnSqPath)
		)
	)

	-- frames of the timeline per picture at the chosen speed
	fn frameStep =
	(
		local sel = if curTab == 3 then sqSpeed.selection else shSpeed.selection
		local fps = case sel of (1: 10.0; 2: 15.0; 3: 30.0; default: 60.0)
		amax #(1, (frameRate / fps + 0.5) as integer)
	)

	fn updateTime =
	(
		local len = 0
		case curTab of
		(
			1: len = flEnd.value - flStart.value
			2: len = (WdxTAC_CellOrder shFirst.value shLast.value (shSkip.selection - 1) (shOrder.selection == 2) shLoops.value).count * frameStep()
			default: len = (WdxTAC_SequenceFiles sqPath.text).count * frameStep()
		)
		lblKeys.text = (WdxTAC_Tr "tac_anim_keys" "Anim Keys: ") + (len as string) + " F"
		lblTime.text = (WdxTAC_Tr "tac_anim_time" "Anim Time: ") + (formattedPrint (len as float / frameRate) format:".1f") + " S"
	)

	-- panel height and the controls below it follow the tab
	fn layoutFor t =
	(
		local panelH = case t of (1: 178; 2: 232; default: 178)
		grpPanel.height = panelH
		local y = case t of (1: 156 + panelH + 6; 2: 488; default: 408)
		grpInfo.pos = [6, y]
		lblKeys.pos = [16, y + 17]
		lblTime.pos = [176, y + 17]
		btnGenerate.pos = [6, y + 48]
		lblResult.pos = [10, y + 84]
		WdxTexAnimCreatorRoll.height = y + 104
	)

	fn showTab t =
	(
		curTab = t
		tabFlow.checked = (t == 1); tabSheet.checked = (t == 2); tabSeq.checked = (t == 3)
		for k = 1 to 3 do for c in (tabControls k) do c.visible = (k == t)
		layoutFor t
		updateTime()
	)

	-- ON/OFF pairs: one state, shown on every tab
	fn setGlobal v = (isGlobal = v; for p in #(#(flGlobOn, flGlobOff), #(shGlobOn, shGlobOff), #(sqGlobOn, sqGlobOff)) do (p[1].checked = v; p[2].checked = not v))
	fn setKeep v = (keepKeys = v; for p in #(#(flKeepOn, flKeepOff), #(shKeepOn, shKeepOff), #(sqKeepOn, sqKeepOff)) do (p[1].checked = v; p[2].checked = not v))
	fn setCollapse v = (collapseUV = v; shColOn.checked = v; shColOff.checked = not v)

	fn curMat = (if ddMat.selection > 0 and ddMat.selection <= mats.count then mats[ddMat.selection] else undefined)
	fn curMap = (if ddMap.selection > 0 and ddMap.selection <= maps.count then maps[ddMap.selection][2] else undefined)

	fn detect =
	(
		local m = curMap()
		if m == undefined do (shGridLbl.text = ""; return ok)
		local g = WdxTAC_DetectGrid (WdxTAC_MapFile m)
		shCols.value = g[1]; shRows.value = g[2]
		shFirst.value = 1; shLast.value = amax #(1, g[3])
		shGridLbl.text = if g[4] == "none" then (WdxTAC_Tr "tac_grid_none" "Grid not found - set columns and rows") \
			else (WdxTAC_Tr "tac_grid_found" "Grid: ") + (g[1] as string) + " x " + (g[2] as string) + ", " + (g[3] as string) + " " + (WdxTAC_Tr "tac_frames_word" "frames")
		updateTime()
	)

	fn fillMaps =
	(
		maps = WdxTAC_MapsOf (curMat())
		ddMap.items = for m in maps collect (m[1] + ": " + (filenameFromPath (WdxTAC_MapFile m[2])))
		ddMap.selection = if maps.count > 0 then 1 else 0
		local m = curMap()
		lblFile.text = if m == undefined then (WdxTAC_Tr "tac_no_texture" "No Wc3Bitmap on this material") else (WdxTAC_FileInfo (WdxTAC_MapFile m))
		detect()
	)

	fn setTarget mtl =
	(
		mats = WdxTAC_FindMaterials mtl
		ddMat.items = for m in mats collect m.name
		ddMat.selection = if mats.count > 0 then 1 else 0
		fillMaps()
		if mats.count == 0 do lblFile.text = WdxTAC_Tr "tac_no_wc3mat" "No Wc3 material found"
	)

	on pbObj picked o do (setTarget o.material; pbObj.text = o.name)
	on btnSel pressed do (if selection.count > 0 do setTarget selection[1].material)
	on btnMedit pressed do (try (setTarget (medit.GetCurMtl())) catch ())
	on ddMat selected i do fillMaps()
	on ddMap selected i do (lblFile.text = WdxTAC_FileInfo (WdxTAC_MapFile (curMap())); detect())
	on btnDetect pressed do detect()

	on tabFlow changed st do showTab 1
	on tabSheet changed st do showTab 2
	on tabSeq changed st do showTab 3
	-- opposite directions exclude each other
	on flUp changed st do (if st do flDown.checked = false)
	on flDown changed st do (if st do flUp.checked = false)
	on flLeft changed st do (if st do flRight.checked = false)
	on flRight changed st do (if st do flLeft.checked = false)
	on flRotL changed st do (if st do flRotR.checked = false)
	on flRotR changed st do (if st do flRotL.checked = false)
	on flSclUp changed st do (if st do flSclDn.checked = false)
	on flSclDn changed st do (if st do flSclUp.checked = false)
	on flGlobOn changed st do setGlobal true
	on flGlobOff changed st do setGlobal false
	on shGlobOn changed st do setGlobal true
	on shGlobOff changed st do setGlobal false
	on sqGlobOn changed st do setGlobal true
	on sqGlobOff changed st do setGlobal false
	on flKeepOn changed st do setKeep true
	on flKeepOff changed st do setKeep false
	on shKeepOn changed st do setKeep true
	on shKeepOff changed st do setKeep false
	on sqKeepOn changed st do setKeep true
	on sqKeepOff changed st do setKeep false
	on shColOn changed st do setCollapse true
	on shColOff changed st do setCollapse false
	on flStart changed v do (if flEnd.value <= v do flEnd.value = v + 1; updateTime())
	on flEnd changed v do (if flStart.value >= v do flStart.value = v - 1; updateTime())
	on shCols changed v do (shLast.value = amin #(shLast.value, v * shRows.value); updateTime())
	on shRows changed v do (shLast.value = amin #(shLast.value, v * shCols.value); updateTime())
	on shFirst changed v do (if shLast.value < v do shLast.value = v; updateTime())
	on shLast changed v do (if shFirst.value > v do shFirst.value = v; updateTime())
	on shLoops changed v do updateTime()
	on shSkip selected i do updateTime()
	on shOrder selected i do updateTime()
	on shSpeed selected i do updateTime()
	on sqSpeed selected i do updateTime()
	on btnSqPath pressed do
	(
		local p = getSavePath caption:(WdxTAC_Tr "tac_pick_folder" "Folder with the frame pictures") initialDir:(if sqPath.text != "" then sqPath.text else maxFilePath)
		if p != undefined do
		(
			sqPath.text = p
			sqPathLbl.text = (WdxTAC_Tr "tac_seq_folder_lbl" "Sequence frame folder path:") + " " + ((WdxTAC_SequenceFiles p).count as string) + " " + (WdxTAC_Tr "tac_pictures_word" "pictures")
			updateTime()
		)
	)

	on btnUV pressed do
	(
		local sel = selection as array
		if sel.count == 0 then lblResult.text = WdxTAC_Tr "tac_select_objects" "Select the objects first"
		else
		(
			local n = 0
			undo "Generate UV Map" on (n = WdxTAC_GenerateUVMap sel shCols.value shRows.value collapseUV)
			lblResult.text = (WdxTAC_Tr "tac_uv_done" "UV map set on objects: ") + (n as string)
		)
	)

	on btnGenerate pressed do
	(
		local n = 0
		undo "Texture Animation" on
		(
			case curTab of
			(
				1:
				(
					local dU = if flRight.checked then 1 else if flLeft.checked then -1 else 0
					local dV = if flUp.checked then 1 else if flDown.checked then -1 else 0
					local dR = if flRotR.checked then 1 else if flRotL.checked then -1 else 0
					local dS = if flSclUp.checked then 1 else if flSclDn.checked then -1 else 0
					n = WdxTAC_ApplyFlow (curMap()) flStart.value flEnd.value dU dV dR dS isGlobal keepKeys
				)
				2:
				(
					local cells = WdxTAC_CellOrder shFirst.value (amin #(shLast.value, shCols.value * shRows.value)) (shSkip.selection - 1) (shOrder.selection == 2) shLoops.value
					n = WdxTAC_ApplySpriteSheet (curMap()) shCols.value shRows.value cells shStart.value (frameStep()) isGlobal keepKeys
				)
				default:
				(
					local files = WdxTAC_SequenceFiles sqPath.text
					n = WdxTAC_ApplySequence (curMat()) files sqPrefix.text sqStart.value (frameStep()) (sqOrder.selection == 2) isGlobal keepKeys
				)
			)
		)
		lblResult.text = if n > 0 then (WdxTAC_Tr "tac_done" "Animation created, keys: ") + (n as string) \
			else (WdxTAC_Tr "tac_nothing" "Nothing created - pick a target and a direction or folder")
	)

	-- texts in the Max language (WhiteoutDexLocalization.ms, "tac_" keys)
	fn applyTexts =
	(
		WdxTexAnimCreatorRoll.title = WdxTAC_Tr "menu_texture_anim_creator" "Texture Animation Creator"
		local plain = #(
			#(grpTarget, "tac_target_grp"), #(btnSel, "tac_selection_btn"), #(btnMedit, "tac_medit_btn"),
			#(tabFlow, "tac_tab_flow"), #(tabSheet, "tac_tab_sheet"), #(tabSeq, "tac_tab_seq"),
			#(lblMat, "tac_material_lbl"), #(lblMap, "tac_texture_lbl"), #(btnDetect, "tac_detect_btn"),
			#(btnUV, "tac_uvmap_btn"), #(btnGenerate, "tac_generate_btn"), #(sqPathLbl, "tac_seq_folder_lbl"),
			#(flStartLbl, "tac_start_time_lbl"), #(shStartLbl, "tac_start_time_lbl"), #(flEndLbl, "tac_end_time_lbl"),
			#(shLoopsLbl, "tac_loop_lbl"), #(shColsLbl, "tac_columns_lbl"), #(shRowsLbl, "tac_rows_lbl"),
			#(shFirstLbl, "tac_start_key_lbl"), #(shLastLbl, "tac_end_key_lbl"), #(sqPrefixLbl, "tac_mdxpath_lbl"))
		for p in plain do try (p[1].text = WdxTAC_Tr p[2] p[1].text) catch ()
		if pbObj.text == "Pick Object" do pbObj.text = WdxTAC_Tr "tac_pick_object_btn" "Pick Object"
		-- labels with a dotted leader up to the buttons
		local dotted = #(
			#(flDirLbl, "tac_direction_lbl"), #(flRotLbl, "tac_rotation_lbl"), #(flSclLbl, "tac_scaling_lbl"),
			#(flGlobLbl, "tac_global_lbl"), #(shGlobLbl, "tac_global_lbl"), #(sqGlobLbl, "tac_global_lbl"),
			#(flKeepLbl, "tac_keep_lbl"), #(shKeepLbl, "tac_keep_lbl"), #(sqKeepLbl, "tac_keep_lbl"),
			#(shSkipLbl, "tac_skip_lbl"), #(shOrdLbl, "tac_order_lbl"), #(sqOrdLbl, "tac_order_lbl"),
			#(shSpdLbl, "tac_speed_lbl"), #(sqSpdLbl, "tac_speed_lbl"), #(shColLbl, "tac_collapse_lbl"),
			#(sqStartLbl, "tac_start_time_lbl"))
		for p in dotted do try (p[1].text = WdxTAC_Dotted (WdxTAC_Tr p[2] p[1].text) 44) catch ()
		local ord = #(WdxTAC_Tr "tac_forward" "Forward play", WdxTAC_Tr "tac_reverse" "Reverse play")
		shOrder.items = ord
		sqOrder.items = ord
		local onTxt = WdxTAC_Tr "tac_on" "ON", offTxt = WdxTAC_Tr "tac_off" "OFF"
		for b in #(flGlobOn, shGlobOn, sqGlobOn, flKeepOn, shKeepOn, sqKeepOn, shColOn) do b.text = onTxt
		for b in #(flGlobOff, shGlobOff, sqGlobOff, flKeepOff, shKeepOff, sqKeepOff, shColOff) do b.text = offTxt
		-- rotation arrows (the file is not saved as UTF-8 with BOM)
		try
		(
			local ch = dotNetClass "System.Char"
			flRotL.text = ch.ConvertFromUtf32 0x21BA
			flRotR.text = ch.ConvertFromUtf32 0x21BB
		) catch ()
		-- arrows as text when the icons are not installed
		if WdxTAC_IconFile "wdx_up.bmp" == "" do (flUp.text = "^"; flDown.text = "v"; flLeft.text = "<"; flRight.text = ">")
	)

	on WdxTexAnimCreatorRoll open do
	(
		try (WdxSetDialogIcon WdxTexAnimCreatorRoll) catch ()
		try (applyTexts()) catch ()
		showTab 2
		if selection.count > 0 and selection[1].material != undefined do setTarget selection[1].material
	)
)

struct WdxTexAnimCreatorStruct
(
	fn open =
	(
		try (destroyDialog ::WdxTexAnimCreatorRoll) catch ()
		createDialog ::WdxTexAnimCreatorRoll 340 592 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox)
	)
)
WdxTexAnimCreator = WdxTexAnimCreatorStruct()
