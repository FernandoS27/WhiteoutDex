/*
    Identical materials

    Two materials are identical when a model could use either without any
    difference in the MDX: same class, every property the same value (maps
    and sub-materials compared the same way, bitmaps by file), and every
    animated property the same animation - the same controller class, the
    same keys at the same frames with the same values and tangents, the same
    out-of-range types. A single differing checkbox, value, key time or key
    value keeps them apart. The material's own name does not count.

    Used by
      Scene Monitor / Fix all (MDLXExporter, scene_monitor_fixes.cpp):
        WdxDuplicateMaterialGroups() and WdxMergeDuplicateMaterials()
      Change Skin Data to WC3 > Combine Mesh + Skin (SkinChanger.ms):
        WdxMaterialsIdentical
*/

global WdxMaterialSignature
global WdxMaterialsIdentical
global WdxDuplicateMaterialGroups
global WdxMergeDuplicateMaterials
global WdxMatSigController
global WdxMatSigValue
global WdxMatSceneNodeMaterials
global WdxMatSigControllerTo
global WdxMatSigValueTo
global WdxMatSignatureTo

-- The signature is written into one stringStream: built with "s += ...",
-- every key copied the whole text so far. Ichigo's eye material has a UV
-- animation of thousands of keys (a 307,000-character signature): 39 s and
-- gigabytes of garbage per call, the export dialog took 3 minutes to open
-- and Fix all ran Max out of memory.

-- A controller's animation: class, out-of-range types and every key
-- (time, value, tangents). A controller without keys (expression, noise,
-- list...) by its value at a few frames of the animation range.
fn WdxMatSigControllerTo c ss =
(
	if c == undefined do return ok
	format "%" ((classOf c) as string) to:ss
	format " ort %/%" ((try (getBeforeORT c) catch (#none)) as string) ((try (getAfterORT c) catch (#none)) as string) to:ss
	local n = try (numKeys c) catch (-1)
	if n == undefined do n = -1
	if n > 0 then
	(
		local keys = try (c.keys) catch (undefined)
		-- Which of these the controller's keys have is asked once, on the
		-- first key: a Bezier key has no tension, a TCB key no tangents, and
		-- reading a missing one threw a caught exception on every key.
		local kp = #(#inTangentType, #outTangentType, #inTangent, #outTangent, #tension, #continuity, #bias)
		local has = undefined
		for k = 1 to n do
		(
			local t = try (getKeyTime c k) catch (undefined)
			local key = try (keys[k]) catch (undefined)
			local v = try (at time t c.value) catch ("?")
			format " [%=%" (t as string) (v as string) to:ss
			if key != undefined do
			(
				if has == undefined do has = for p in kp collect (try (getProperty key p; true) catch (false))
				local pv = for i = 1 to kp.count collect (if has[i] then ((try (getProperty key kp[i]) catch ("")) as string) else "")
				format " %/% %/% %/%/%" pv[1] pv[2] pv[3] pv[4] pv[5] pv[6] pv[7] to:ss
			)
			format "]" to:ss
		)
	)
	else if n < 0 then
	(
		local r = animationRange
		for t in #(r.start, (r.start + r.end) / 2, r.end) do
			format " @%=%" (t as string) ((try (at time t c.value) catch ("?")) as string) to:ss
	)
	-- sub-controllers (list controllers, XYZ splits)
	for i = 1 to (try (c.numSubs) catch (0)) do
	(
		local sc = try (c[i].controller) catch (undefined)
		if sc != undefined do
		(
			format " {" to:ss
			WdxMatSigControllerTo sc ss
			format "}" to:ss
		)
	)
	ok
)

fn WdxMatSigValueTo v depth seen ss =
(
	if v == undefined then format "u" to:ss
	else if isKindOf v Material or isKindOf v TextureMap then WdxMatSignatureTo v (depth + 1) seen ss
	else if classOf v == BitMap then format "bmp:%" (toLower ((try (v.filename) catch ("")) as string)) to:ss
	else if classOf v == Array or classOf v == ArrayParameter then
	(
		format "[" to:ss
		for x in v do (WdxMatSigValueTo x depth seen ss; format "," to:ss)
		format "]" to:ss
	)
	else if isValidNode v then format "node:%" v.name to:ss
	-- coordinates, output curves and other parts of a map (tiling, offset...)
	else if isKindOf v MAXWrapper and not isKindOf v Controller then WdxMatSignatureTo v (depth + 1) seen ss
	else if classOf v == String then format "\"%\"" v to:ss
	else format "%:%" ((classOf v) as string) (v as string) to:ss
	ok
)

fn WdxMatSignatureTo m depth seen ss =
(
	if m == undefined do (format "u" to:ss; return ok)
	if depth > 12 do (format "deep" to:ss; return ok)
	local h = getHandleByAnim m
	if findItem seen h > 0 do (format "cycle" to:ss; return ok)
	seen = seen + #(h)
	format "%(" ((classOf m) as string) to:ss
	local props = try (getPropNames m) catch (#())
	for p in props do
	(
		local v = try (getProperty m p) catch ("<err>")
		format "%=" (p as string) to:ss
		WdxMatSigValueTo v depth seen ss
		local c = try (getPropertyController m p) catch (undefined)
		if c != undefined do
		(
			format "{" to:ss
			WdxMatSigControllerTo c ss
			format "}" to:ss
		)
		format ";" to:ss
	)
	-- scripted materials and maps keep part of their state in the class
	-- they extend (Wc3Bitmap: the bitmap file in its Bitmaptexture)
	local d = try (m.delegate) catch (undefined)
	if d != undefined and d != m do
	(
		format "delegate=" to:ss
		WdxMatSignatureTo d (depth + 1) seen ss
	)
	format ")" to:ss
	ok
)

fn WdxMatSigController c =
(
	if c == undefined do return ""
	local ss = stringStream ""
	WdxMatSigControllerTo c ss
	ss as string
)

fn WdxMatSigValue v depth seen =
(
	local ss = stringStream ""
	WdxMatSigValueTo v depth seen ss
	ss as string
)

-- One string that is equal for two materials exactly when they are
-- interchangeable (see above).
fn WdxMaterialSignature m depth:0 seen:#() =
(
	local ss = stringStream ""
	WdxMatSignatureTo m depth seen ss
	ss as string
)

fn WdxMaterialsIdentical a b =
(
	if a == b then true
	else if a == undefined or b == undefined then false
	else (WdxMaterialSignature a) == (WdxMaterialSignature b)
)

-- The node materials, each once, in scene order.
fn WdxMatSceneNodeMaterials =
(
	local mats = #()
	for o in objects where isValidNode o and o.material != undefined do appendIfUnique mats o.material
	mats
)

-- Groups of identical node materials: "keep,dup,dup;keep,dup" as anim
-- handles, the first of a group is the one that stays. "" when none.
fn WdxDuplicateMaterialGroups =
(
	local mats = WdxMatSceneNodeMaterials()
	local sigs = for m in mats collect (try (WdxMaterialSignature m) catch ("<error " + (getHandleByAnim m) as string + ">"))
	local used = #{}
	local out = ""
	for i = 1 to mats.count where not used[i] do
	(
		local grp = #(i)
		for j = i + 1 to mats.count where not used[j] and sigs[j] == sigs[i] do append grp j
		if grp.count > 1 do
		(
			for j in grp do used[j] = true
			if out != "" do out += ";"
			for k = 1 to grp.count do out += (if k > 1 then "," else "") + ((getHandleByAnim mats[grp[k]]) as string)
		)
	)
	out
)

-- Every node using a duplicate gets the group's first material instead.
-- Returns the number of materials merged away.
fn WdxMergeDuplicateMaterials =
(
	local groups = filterString (WdxDuplicateMaterialGroups()) ";"
	local merged = 0
	for g in groups do
	(
		local hs = filterString g ","
		local keep = getAnimByHandle (hs[1] as integer)
		if keep == undefined do continue
		for k = 2 to hs.count do
		(
			local dup = getAnimByHandle (hs[k] as integer)
			if dup == undefined do continue
			for o in objects where isValidNode o and o.material == dup do o.material = keep
			merged += 1
		)
	)
	merged
)
