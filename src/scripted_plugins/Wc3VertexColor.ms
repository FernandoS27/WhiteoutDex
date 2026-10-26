global WdxGeosetColorLoading   -- set at the end of this file
global WdxGeosetColorAnimated  -- modifiers with a keyed colour (time callback)
global WdxGeosetColorKeyed     -- defined below
global WdxGeosetColorListDirty -- WdxGeosetColorAnimated needs rebuilding
plugin modifier Wdx_Wc3VertexMod
name:"Wc3 Vertex Color" classID:#(0x7A1B2C07, 0x3D4E5F07)
extends:VertexPaint replaceUI:true version:1
(
	-- Viewport: the colour also goes into the mesh's vertex illumination
	-- channel (-1) and the object's Vertex Channel Display shows that channel,
	-- so Nitrous draws it times the texture - what the game does with a geoset
	-- colour. Channel 0, the vertex colours, stays untouched. On an Editable
	-- Mesh / Poly the base object holds the channel; a primitive has none to
	-- write, so a plain VertexPaint ("Wc3 Vertex Color (viewport)") under this
	-- modifier holds it, filled by WdxFillVertexPaint. (This modifier's own
	-- VertexPaint layer cannot be set: IVertexPaint::SetColors does not find a
	-- VertexPaint that is a scripted modifier's delegate.) Exporter and preview
	-- read VertexColor, never the channel. A keyed colour follows the time
	-- slider (WdxGeosetColorTimeChanged below). Skipped while a file loads
	-- (WdxGeosetColorLoading): the post-open callback fills every modifier
	-- once the scene is whole.
	fn fillIllumination base c =
	(
		local v = [c.r / 255.0, c.g / 255.0, c.b / 255.0]
		case classOf base of
		(
			Editable_mesh:
			(
				-- One map vertex for the whole mesh; once it is there only its
				-- colour changes, which is all the time callback pays for.
				if (meshop.getMapSupport base -1) and (meshop.getNumMapVerts base -1) == 1 then
					meshop.setMapVert base -1 1 v
				else
				(
					meshop.setMapSupport base -1 true
					meshop.setNumMapVerts base -1 1
					meshop.setMapVert base -1 1 v
					for f = 1 to (meshop.getNumFaces base) do meshop.setMapFace base -1 f [1, 1, 1]
				)
				true
			)
			Editable_Poly:
			(
				if (polyop.getMapSupport base -1) and (polyop.getNumMapVerts base -1) == 1 then
					polyop.setMapVert base -1 1 v
				else
				(
					polyop.setMapSupport base -1 true
					polyop.setNumMapVerts base -1 1
					polyop.setMapVert base -1 1 v
					for f = 1 to (polyop.getNumFaces base) do
						polyop.setMapFace base -1 f (for i = 1 to (polyop.getFaceDeg base f) collect 1)
				)
				true
			)
			default: false
		)
	)

	fn viewportHelper n create =
	(
		local h = undefined
		for m in n.modifiers where classOf m == VertexPaint and m.name == "Wc3 Vertex Color (viewport)" do h = m
		if h == undefined and create do
		(
			h = VertexPaint()
			h.name = "Wc3 Vertex Color (viewport)"
			h.mapChannel = -1
			local i = findItem (for m in n.modifiers collect m) this
			if i > 0 then addModifier n h before:i else addModifier n h
		)
		h
	)

	fn fillNode n =
	(
		local c = this.VertexColor
		if fillIllumination n.baseObject c then true
		else if WdxFillVertexPaint != undefined then
		(
			local h = viewportHelper n true
			h != undefined and (WdxFillVertexPaint n h c)
		)
		else false
	)

	fn showInViewport =
	(
		if WdxGeosetColorLoading == true do return false
		for n in (refs.dependentNodes this) where isValidNode n do
		(
			local filled = this.UsesColor and (fillNode n)
			n.vertexColorType = #illum
			n.vertexColorsShaded = true
			n.showVertexColors = filled
		)
		-- Keyed or not is decided on the next time change: a set handler runs
		-- before 3ds Max stores the value, so the key being set is not there
		-- yet.
		WdxGeosetColorListDirty = true
		true
	)

	-- The time callback's path: the colour at the current time, nothing else.
	fn showAtCurrentTime =
	(
		if not this.UsesColor do return false
		for n in (refs.dependentNodes this) where isValidNode n and n.showVertexColors do fillNode n
		true
	)

	parameters main rollout:params
	(
		VertexColor type:#color animatable:true ui:Scolor default:[255,255,255]
		UsesDropShadow type:#boolean animatable:false ui:uds1 default:false
		UsesColor type:#boolean animatable:false ui:udc1 default:true

		on VertexColor set val do showInViewport()
		on UsesColor set val do showInViewport()
	)
	rollout params "Basic Parameters"
	(
		label       l1				"—— -  Overlay Color  - ——"	align:#center
		colorPicker Scolor			                            pos:[8,25]  width:142 height:32 color:(color 255 255 255)
		label       l2              "Drop Shadow ............."   pos:[12,63] width:124 align:#left
		checkbox 	uds1                                        pos:[137,63]
		label       l3              "Use Color .................."   pos:[12,87] width:124 align:#left
		checkbox 	udc1                                        pos:[137,87]

		on params open do
		(
			-- A modifier just added from the list has no colour in the
			-- viewport yet: its set handlers ran before it had a node.
			for n in (refs.dependentNodes this) where isValidNode n and n.showVertexColors != this.UsesColor do
			( showInViewport(); exit )
			if ::WdxL != undefined then
			(
				params.title = ::WdxL.t "vtx_basic_parameters_title"
				l1.text = ::WdxL.t "vtx_overlay_color_lbl"
				l2.text = ::WdxL.t "vtx_drop_shadow_lbl"
				l3.text = ::WdxL.t "vtx_use_color_lbl"
			)
		)
	)
)

-- Every Wc3 Vertex Color modifier in the scene shows its colour in the
-- viewport: after a file is opened or merged and after an import (the
-- importer sets the colour before the modifier is on its node). Opening a
-- file does not leave it marked as changed by this.
global WdxGeosetColorLoading = false
if WdxGeosetColorAnimated == undefined do WdxGeosetColorAnimated = #()
WdxGeosetColorListDirty = true
-- From outside the plug-in: inside it `this.VertexColor` is the colour
-- value, and a value has no controller.
fn WdxGeosetColorKeyed m = ( try ( m.UsesColor and m.VertexColor.controller.keys.count > 1 ) catch ( false ) )
fn WdxShowGeosetColors =
(
	WdxGeosetColorLoading = false
	WdxGeosetColorListDirty = true
	local wasChanged = getSaveRequired()
	for m in (getClassInstances Wdx_Wc3VertexMod) do try ( m.showInViewport() ) catch ()
	if not wasChanged do setSaveRequired false
)
-- A deleted modifier takes its colour out of the viewport with it, and the
-- VertexPaint it put under a primitive goes too (after the delete, not
-- during it).
fn WdxGeosetColorModifierDeleted =
(
	local p = callbacks.notificationParam()
	if classOf p == Array and p.count >= 2 and classOf p[2] == Wdx_Wc3VertexMod and isValidNode p[1] do
	(
		p[1].showVertexColors = false
		WdxGeosetColorListDirty = true
	)
)
fn WdxGeosetColorAfterDelete =
(
	local p = callbacks.notificationParam()
	if classOf p == Array and p.count >= 2 and classOf p[2] == Wdx_Wc3VertexMod and isValidNode p[1] do
	(
		local n = p[1]
		if (for m in n.modifiers where classOf m == Wdx_Wc3VertexMod collect m).count == 0 do
			for m in (for x in n.modifiers where classOf x == VertexPaint and x.name == "Wc3 Vertex Color (viewport)" collect x) do
				deleteModifier n m
	)
)
-- A keyed colour follows the time slider: only the one map vertex (or the
-- primitive's VertexPaint) changes, and the scene is not marked as changed.
if WdxGeosetColorTimeChanged != undefined do try ( unregisterTimeCallback WdxGeosetColorTimeChanged ) catch ()
fn WdxGeosetColorTimeChanged =
(
	if WdxGeosetColorLoading == true do return ()
	if WdxGeosetColorListDirty != false do
	(
		WdxGeosetColorListDirty = false
		WdxGeosetColorAnimated = for m in (getClassInstances Wdx_Wc3VertexMod) where WdxGeosetColorKeyed m collect m
	)
	if WdxGeosetColorAnimated.count == 0 do return ()
	local wasChanged = getSaveRequired()
	with undo off
		for m in WdxGeosetColorAnimated do try ( m.showAtCurrentTime() ) catch ()
	if not wasChanged do setSaveRequired false
)
registerTimeCallback WdxGeosetColorTimeChanged
callbacks.removeScripts id:#WdxGeosetColor
callbacks.addScript #preModifierDeleted "WdxGeosetColorModifierDeleted()" id:#WdxGeosetColor
try ( callbacks.addScript #postModifierDeleted "WdxGeosetColorAfterDelete()" id:#WdxGeosetColor ) catch ()
callbacks.addScript #filePreOpen "WdxGeosetColorLoading = true" id:#WdxGeosetColor
callbacks.addScript #filePostOpen "WdxShowGeosetColors()" id:#WdxGeosetColor
callbacks.addScript #filePreMerge "WdxGeosetColorLoading = true" id:#WdxGeosetColor
callbacks.addScript #filePostMerge "WdxShowGeosetColors()" id:#WdxGeosetColor
callbacks.addScript #postImport "WdxShowGeosetColors()" id:#WdxGeosetColor
callbacks.addScript #systemPostReset "WdxGeosetColorLoading = false; WdxGeosetColorListDirty = true" id:#WdxGeosetColor
callbacks.addScript #systemPostNew "WdxGeosetColorLoading = false; WdxGeosetColorListDirty = true" id:#WdxGeosetColor
