-- Opens the WhiteoutDex windows one by one for capture.ps1 (see there).
-- Reads capture_job.txt next to this file: out=, langs=, only=, samples=.
-- Never shows an error box: every step is guarded and logged to _ms.log.
(
	SetQuietMode true
	local here = getFilenamePath (getSourceFileName())
	local job = #()
	local fs = openFile (here + "capture_job.txt") encoding:#utf8
	while not eof fs do append job (readLine fs)
	close fs
	fn jobVal job key = (for l in job where (matchPattern l pattern:(key + "=*")) do return (substring l (key.count + 2) -1); "")
	local out = (jobVal job "out") + "\\"
	local langs = filterString (jobVal job "langs") ","
	local only = filterString (jobVal job "only") ","
	local samples = (jobVal job "samples") + "\\"
	local work = out + "_work\\"
	makeDir work
	local lg = createFile (out + "_ms.log") encoding:#utf8 writeBOM:false

	fn waitFile f ms = (local t0 = timeStamp(); while not (doesFileExist f) and (timeStamp() - t0) < ms do (windows.processPostedMessages(); sleep 0.05); local ok = doesFileExist f; if ok do deleteFile f; ok)
	-- Ask capture.ps1 to watch for the next window; true once it is ready.
	-- mode: modal (closed by capture.ps1), close (non-modal, closed by
	-- capture.ps1 after its shots), self (closed here). steps: see capture.ps1.
	fn arm out lang id mode steps:"" = (local f = createFile (out + "_want.txt") encoding:#utf8 writeBOM:false; format "%|%|%|%" lang id mode steps to:f; close f; waitFile (out + "_armed.txt") 30000)
	fn done out = waitFile (out + "_done.txt") 90000
	fn wanted only id = only.count == 0 or (findItem only id) > 0

	-- A model to work with, copied so the importer's texture extraction
	-- lands in the work folder and never next to the samples.
	for f in #("HeroPaladin.mdx", "Heropaladin.tga") do
		if not (doesFileExist (work + f)) do copyFile (samples + f) (work + f)
	local model = work + "HeroPaladin.mdx"
	local exportTo = work + "export\\HeroPaladin.mdx"
	makeDir (work + "export")
	fn loadModel model = (resetMaxFile #noPrompt; importFile model #noPrompt using:WhiteoutDex_MDX_Importer)
	fn firstMesh = (for o in geometry where isValidNode o and (canConvertTo o Editable_Mesh) and not (isKindOf o BoneGeometry) do return o; undefined)
	fn firstBone = (for o in objects where isValidNode o and (isKindOf o BoneGeometry or isKindOf o Dummy or isKindOf o Point) do return o; undefined)

	-- id, macro, scene (#none, #model, #mesh = model + a mesh selected,
	-- #bone = model + a bone selected), steps
	local tools = #(
		#("visibility_keyer",     "WhiteoutDex_VisibilityKeyer",    #mesh, ""),
		#("anim_general",         "WhiteoutDex_AnimGeneral",        #model, ""),
		#("anim_controllers",     "WhiteoutDex_AnimControllers",    #model, ""),
		#("anim_skinning",        "WhiteoutDex_AnimSkin",           #model, ""),
		#("model_scaler",         "WhiteoutDex_AnimScaler",         #model, ""),
		#("skin_changer",         "WhiteoutDex_SkinChanger",        #mesh, ""),
		#("grid_dummy",           "WhiteoutDex_GridDummy",          #none, ""),
		#("object_tools",         "WhiteoutDex_ObjectTools",        #mesh, ""),
		#("node_manager",         "WhiteoutDex_NodeManager",        #model, ""),
		#("object_settings",      "WhiteoutDex_ObjectSettings",     #bone, ""),
		#("keyframe_optimizer",   "WhiteoutDex_KeyframeOptimizer",  #bone, ""),
		#("cell_shade",           "WhiteoutDex_CellShadeCreator",   #mesh, ""),
		#("material_fix",         "WhiteoutDex_MaterialFix",        #none, "clickat:5:30:12:material_fix_hd"),
		#("texture_anim_creator", "WhiteoutDex_TextureAnimCreator", #mesh, "clickat:14:30:12:texture_anim_flow;clickat:16:30:12:texture_anim_many"),
		#("about",                "WhiteoutDex_About",              #none, "")
	)

	for lang in langs do
	(
		try (::WdxL.setLanguage lang) catch (format "% setLanguage failed: %\n" lang (getCurrentException()) to:lg)
		format "== % (WdxL language %)\n" lang (try (::WdxL.getLanguage()) catch "?") to:lg

		-- Settings (modal on Max 2017+). capture.ps1 shows the standard game
		-- folder instead of the one of whoever runs this.
		if wanted only "settings" do try
		(
			resetMaxFile #noPrompt
			if arm out lang "settings" "modal" steps:"open:16:mpq_load_order" do macros.run "WhiteoutDex Toolkit" "WhiteoutDex_Settings"
			done out
		) catch (format "settings: %\n" (getCurrentException()) to:lg)

		-- Importer dialog (modal; Cancel aborts the import)
		if wanted only "importer" do try
		(
			resetMaxFile #noPrompt
			if arm out lang "importer" "modal" steps:"tab:1062:importer_animations;tab:1063:importer_model" do importFile model using:WhiteoutDex_MDX_Importer
			done out
		) catch (format "importer: %\n" (getCurrentException()) to:lg)

		-- Exporter dialog on the imported model (modal)
		if wanted only "exporter" do try
		(
			loadModel model
			if arm out lang "exporter" "modal" steps:"tab:1016:exporter_textures;tab:1012:exporter_textures_reforged" do exportFile exportTo using:WhiteoutDex_MDX_Exporter
			done out
		) catch (format "exporter: %\n" (getCurrentException()) to:lg)

		-- Exporter with scene problems: the banner, then its Details list
		if wanted only "exporter_problems" do try
		(
			loadModel model
			local b = box name:"Box_Crate" length:40 width:40 height:40 pos:[80, 0, 0]
			b.material = StandardMaterial name:"Crate"
			convertToPoly b
			local b2 = copy b
			b2.name = "Box_Crate"
			b2.pos = [140, 0, 0]
			if arm out lang "exporter_problems" "modal" steps:"open:1093:scene_problems" do exportFile exportTo using:WhiteoutDex_MDX_Exporter
			done out
		) catch (format "exporter_problems: %\n" (getCurrentException()) to:lg)

		-- Sequence Manager: a sequence selected, then the New and Globals windows
		if wanted only "sequence_manager" do try
		(
			loadModel model
			if arm out lang "sequence_manager" "self" steps:"clickat:7:40:47:sequence_manager_selected;open:14:sequence_new;open:17:global_sequences" do macros.run "WhiteoutDex Toolkit" "WhiteoutDex_SequenceManager"
			done out
			try (destroyDialog ::WdxSequenceManager.mainRollout) catch ()
		) catch (format "sequence_manager: %\n" (getCurrentException()) to:lg)

		-- Objects in the Modify panel: one picture per rollout (capture.ps1
		-- scrolls the panel). Objects the sample model has are taken from it,
		-- so the rollouts show real values; the others are created.
		-- id, class name, create-if-missing
		local objs = #(
			#("obj_attachpoint",       "Wdx_Wc3AttachPoint"),
			#("obj_event",             "Wdx_Wc3Event"),
			#("obj_collision_sphere",  "Wdx_Wc3CollisionSphere"),
			#("obj_collision_box",     "Wdx_CollisionBox"),
			#("obj_light",             "Wdx_Wc3Light"),
			#("obj_popcorn",           "Wdx_Wc3Popcorn"),
			#("obj_facefx",            "Wdx_FaceFX"),
			#("obj_particles1",        "Wc3Particles1"),
			#("obj_particles2",        "Wc3Particles2"),
			#("obj_ribbon",            "Wc3Ribbon")
		)
		for ob in objs where wanted only ob[1] or wanted only "objects" do try
		(
			loadModel model
			local cls = try (execute ob[2]) catch undefined
			if cls == undefined then format "%: no class %
" ob[1] ob[2] to:lg else
			(
				local o = undefined
				for n in objects where isValidNode n and classOf n == cls while o == undefined do o = n
				if o == undefined do o = cls pos:[0, 0, 0]
				clearSelection()
				select o
				max modify mode
				completeRedraw()
				if arm out lang ob[1] "panel" do ()
				done out
			)
		) catch (format "%: %
" ob[1] (getCurrentException()) to:lg)

		-- Wc3 Vertex Color: a modifier, shown on a box
		if wanted only "obj_vertexcolor" or wanted only "objects" do try
		(
			resetMaxFile #noPrompt
			local bx = box length:40 width:40 height:40
			local cls = try (execute "Wdx_Wc3VertexMod") catch undefined
			if cls != undefined do
			(
				addModifier bx (cls())
				select bx
				max modify mode
				modPanel.setCurrentObject bx.modifiers[1]
				completeRedraw()
				if arm out lang "obj_vertexcolor" "panel" do ()
				done out
			)
		) catch (format "obj_vertexcolor: %
" (getCurrentException()) to:lg)

		-- Materials: the model's first Wc3Material, then one of its Wc3Bitmaps,
		-- each alone in the Compact Material Editor.
		if wanted only "materials" do try
		(
			loadModel model
			local mcls = try (execute "Wdx_Wc3Material") catch undefined
			local bcls = try (execute "Wc3Bitmap") catch undefined
			local mtl = undefined
			if mcls != undefined do (local all = getClassInstances mcls; if all.count > 0 do mtl = all[1])
			local bmp = undefined
			if bcls != undefined do (local all = getClassInstances bcls; if all.count > 0 do bmp = all[1])
			format "materials: % / %
" mtl bmp to:lg
			MatEditor.Close()
			MatEditor.mode = #basic
			for it in #(#("mat_wc3material", mtl), #("mat_wc3bitmap", bmp)) where it[2] != undefined do
			(
				meditMaterials[1] = it[2]
				activeMeditSlot = 1
				if arm out lang it[1] "mtlpanel" do MatEditor.Open()
				done out
				MatEditor.Close()
			)
		) catch (format "materials: %
" (getCurrentException()) to:lg)

		-- The sidebar, docked in the main window
		if wanted only "sidebar" do try
		(
			resetMaxFile #noPrompt
			if arm out lang "sidebar" ("docked=" + (::WdxL.t "mgr_whiteoutdex_manager_macbtn")) do ()
			done out
		) catch (format "sidebar: %
" (getCurrentException()) to:lg)

		-- Checks of suspected bugs (results in _ms.log and the shots).
		if (findItem only "verify") > 0 do try
		(
			-- Degree sign in the "Fix non-linear rotation" tooltip
			format "VERIFY degree tip [%]: %
" lang (::WdxL.t "anim_nonlinear_rotation_keys_may_a_tip") to:lg
			-- Global Sequence Manager: channels chosen after New
			resetMaxFile #noPrompt
			local bx = box name:"GsmTest" length:20 width:20 height:20
			select bx
			if arm out lang "gsm_before" "self" steps:"tab:New:gsm_recording" do ::WdxGlobalSequenceManager.showUI()
			done out
			local r = ::WdxGlobalSequenceManager.mainRollout
			format "VERIFY gsm recording: ctrl visible % pos % rot % scl % | recorded pos/rot/scl: % % %
" r.ctrlDdl.visible r.posChk.checked r.rotChk.checked r.sclChk.checked (try ::WdxGlobalSequenceManager.recordPos catch "?") (try ::WdxGlobalSequenceManager.recordRot catch "?") (try ::WdxGlobalSequenceManager.recordScale catch "?") to:lg
			-- What a user does now: switch the channel to P and key the position
			r.posChk.checked = true
			r.rotChk.checked = false
			with animate on (at time 0 bx.pos = [0,0,0]; at time 20 bx.pos = [60,0,0])
			if arm out lang "gsm_finish" "existing=Global Sequence Manager" steps:"open:Finish:gsm_result" do ()
			done out
			format "VERIFY gsm after finish: globals % | pos keys %
" (try ::WdxGlobalSequenceManager.globalEntries.count catch "?") (try (numKeys bx.pos.controller) catch "?") to:lg
			try (destroyDialog r) catch ()
		) catch (format "verify: %
" (getCurrentException()) to:lg)

		-- Pictures of the model for the tutorials: the viewport, shaded, from a
		-- camera in front of the model; once with its helpers, once without.
		if wanted only "viewport" do try
		(
			loadModel model
			local vdir = out + lang + "\\"
			makeDir vdir
			viewport.setLayout #layout_1
			local geo = for o in geometry where isValidNode o and not o.isHidden and not (isKindOf o BoneGeometry) collect o
			local bb = if geo.count > 0 then (local b0 = geo[1].max; local a0 = geo[1].min; for g in geo do (a0 = [amin a0.x g.min.x, amin a0.y g.min.y, amin a0.z g.min.z]; b0 = [amax b0.x g.max.x, amax b0.y g.max.y, amax b0.z g.max.z]); #(a0, b0)) else #([-50,-50,0], [50,50,100])
			local ctr = (bb[1] + bb[2]) / 2.0
			local size = length (bb[2] - bb[1])
			-- Warcraft III models face +X; look at the front, a little from the side and above.
			local cam = Targetcamera pos:(ctr + [size * 1.1, -size * 0.7, size * 0.45]) target:(Targetobject pos:ctr) fov:40
			viewport.setCamera cam
			viewport.SetRenderLevel #smoothhighlights
			viewport.SetShowEdgeFaces false
			fn grab vdir name = (completeRedraw(); local d = gw.getViewportDib(); d.filename = vdir + name; save d)
			grab vdir "tut_model_helpers.png"
			-- Hidden one by one: the name labels of attachment points and events
			-- are drawn for objects hidden by category (seen 2026-10-10).
			local shown = for o in objects where isValidNode o and not o.isHidden and not (isKindOf o GeometryClass and not (isKindOf o BoneGeometry)) and o != cam and o != cam.target collect o
			for o in shown do o.isHidden = true
			grab vdir "tut_model.png"
			for o in shown do o.isHidden = false
			delete cam
			format "viewport pictures saved (bbox % %)
" bb[1] bb[2] to:lg
		) catch (format "viewport: %
" (getCurrentException()) to:lg)

		-- Every other tool window
		for t in tools where wanted only t[1] do try
		(
			case t[3] of
			(
				#none: resetMaxFile #noPrompt
				#model: loadModel model
				#mesh: (loadModel model; local m = firstMesh(); if m != undefined do select m)
				#bone: (loadModel model; local bn = firstBone(); if bn != undefined do select bn)
			)
			if arm out lang t[1] "close" steps:t[4] do macros.run "WhiteoutDex Toolkit" t[2]
			done out
		) catch (format "%: %\n" t[1] (getCurrentException()) to:lg)
		flush lg
	)
	close lg
	(createFile (out + "_finished.txt"))
	setSaveRequired false
	resetMaxFile #noPrompt
	quitMax #noPrompt
)
