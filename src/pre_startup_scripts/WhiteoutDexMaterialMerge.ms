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

-- A controller's animation: class, out-of-range types and every key
-- (time, value, tangents). A controller without keys (expression, noise,
-- list...) by its value at a few frames of the animation range.
fn WdxMatSigController c =
(
	if c == undefined do return ""
	local s = (classOf c) as string
	s += " ort " + ((try (getBeforeORT c) catch (#none)) as string) + "/" + ((try (getAfterORT c) catch (#none)) as string)
	local n = try (numKeys c) catch (-1)
	if n == undefined do n = -1
	if n > 0 then
	(
		for k = 1 to n do
		(
			local t = try (getKeyTime c k) catch (undefined)
			local key = try (c.keys[k]) catch (undefined)
			local v = try (at time t c.value) catch ("?")
			s += " [" + (t as string) + "=" + (v as string)
			if key != undefined do
			(
				s += " " + ((try (key.inTangentType) catch ("")) as string) + "/" + ((try (key.outTangentType) catch ("")) as string)
				s += " " + ((try (key.inTangent) catch ("")) as string) + "/" + ((try (key.outTangent) catch ("")) as string)
				s += " " + ((try (key.tension) catch ("")) as string) + "/" + ((try (key.continuity) catch ("")) as string) + "/" + ((try (key.bias) catch ("")) as string)
			)
			s += "]"
		)
	)
	else if n < 0 then
	(
		local r = animationRange
		for t in #(r.start, (r.start + r.end) / 2, r.end) do
			s += " @" + (t as string) + "=" + ((try (at time t c.value) catch ("?")) as string)
	)
	-- sub-controllers (list controllers, XYZ splits)
	for i = 1 to (try (c.numSubs) catch (0)) do
	(
		local sc = try (c[i].controller) catch (undefined)
		if sc != undefined do s += " {" + (WdxMatSigController sc) + "}"
	)
	s
)

fn WdxMatSigValue v depth seen =
(
	if v == undefined then "u"
	else if isKindOf v Material or isKindOf v TextureMap then WdxMaterialSignature v depth:(depth + 1) seen:seen
	else if classOf v == BitMap then "bmp:" + (toLower ((try (v.filename) catch ("")) as string))
	else if classOf v == Array or classOf v == ArrayParameter then
	(
		local s = "["
		for x in v do s += (WdxMatSigValue x depth seen) + ","
		s + "]"
	)
	else if isValidNode v then "node:" + v.name
	-- coordinates, output curves and other parts of a map (tiling, offset...)
	else if isKindOf v MAXWrapper and not isKindOf v Controller then WdxMaterialSignature v depth:(depth + 1) seen:seen
	else if classOf v == String then "\"" + v + "\""
	else (classOf v) as string + ":" + (v as string)
)

-- One string that is equal for two materials exactly when they are
-- interchangeable (see above).
fn WdxMaterialSignature m depth:0 seen:#() =
(
	if m == undefined do return "u"
	if depth > 12 do return "deep"
	local h = getHandleByAnim m
	if findItem seen h > 0 do return "cycle"
	seen = seen + #(h)
	local s = (classOf m) as string + "("
	local props = try (getPropNames m) catch (#())
	for p in props do
	(
		local v = try (getProperty m p) catch ("<err>")
		s += (p as string) + "=" + (WdxMatSigValue v depth seen)
		local c = try (getPropertyController m p) catch (undefined)
		if c != undefined do s += "{" + (WdxMatSigController c) + "}"
		s += ";"
	)
	-- scripted materials and maps keep part of their state in the class
	-- they extend (Wc3Bitmap: the bitmap file in its Bitmaptexture)
	local d = try (m.delegate) catch (undefined)
	if d != undefined and d != m do s += "delegate=" + (WdxMaterialSignature d depth:(depth + 1) seen:seen)
	s + ")"
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
