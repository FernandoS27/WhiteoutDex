struct WdxObjectSettings
(
	isPoppedOut = false,
	savedIndex = undefined,
	mainRollout = rollout mainRollout "Settings" width:440 height:232
	(
		
		-- Flags (links) - Alle Bone-Flags wie im Reforged Editor
		GroupBox grpFlags "Flags" pos:[8,33] width:212 height:192
		checkbox dontInhTransChk "Don't Inherit Translation" pos:[28,52] width:180 height:18 tooltip:"Ignore parent translation"
		checkbox dontInhRotChk "Don't Inherit Rotation" pos:[28,72] width:180 height:18 tooltip:"Ignore parent rotation"
		checkbox dontInhScaleChk "Don't Inherit Scaling" pos:[28,92] width:180 height:18 tooltip:"Ignore parent scaling"
		checkbox bbFullChk "Billboarded" pos:[28,117] width:180 height:18 tooltip:"Object always faces the camera"
		checkbox bbLockXChk "Billboarded Lock X" pos:[28,137] width:180 height:18 tooltip:"Lock billboard rotation around X axis"
		checkbox bbLockYChk "Billboarded Lock Y" pos:[28,157] width:180 height:18 tooltip:"Lock billboard rotation around Y axis"
		checkbox bbLockZChk "Billboarded Lock Z" pos:[28,177] width:180 height:18 tooltip:"Lock billboard rotation around Z axis"
		checkbox cameraChk "Camera Anchored" pos:[28,197] width:180 height:18 enabled:true checked:false tooltip:"Makes the object stay in place relative to the camera view"

		-- Mesh (rechts)
		GroupBox grpMesh "Mesh" pos:[224,33] width:212 height:192
		radiobuttons skinRdo "Skinning Method:" pos:[236,52] width:180 height:46 labels:#("Normal", "ArtTools") default:0 columns:1 tooltip:"Normal Skinning uses any skin modifier or linked bones.\nArtTools Skinning unifies vertices of detached meshes at export.\nOnly use ArtTools on meshes you want to blend."
		checkbox selectChk "Unselectable" pos:[236,110] width:140 height:18 tooltip:"Makes the object non-selectable in game"

		fn B2I b =
		(
			if b then 1 else 0
		)

		on skinRdo changed newState do
			for s in selection do setUserProp s "Wc3Skin" newState
		on bbFullChk changed newState do
			for s in selection do setUserProp s "Billboarded" (B2I newState)
		on bbLockXChk changed newState do
			for s in selection do setUserProp s "BillboardedLockX" (B2I newState)
		on bbLockYChk changed newState do
			for s in selection do setUserProp s "BillboardedLockY" (B2I newState)
		on bbLockZChk changed newState do
			for s in selection do setUserProp s "BillboardedLockZ" (B2I newState)
		on cameraChk changed newState do
			for s in selection do setUserProp s "CameraAnchored" (::WdxUtils.B2I newState)
		on selectChk changed newState do
			for s in selection do setUserProp s "Unselectable" (::WdxUtils.B2I newState)
		on dontInhTransChk changed newState do
			for s in selection do
			(
				local f = getInheritanceFlags s
				local val = not newState
				f[1] = val; f[2] = val; f[3] = val
				setInheritanceFlags s f
			)
		on dontInhRotChk changed newState do
			for s in selection do
			(
				local f = getInheritanceFlags s
				local val = not newState
				f[4] = val; f[5] = val; f[6] = val
				setInheritanceFlags s f
			)
		on dontInhScaleChk changed newState do
			for s in selection do
			(
				local f = getInheritanceFlags s
				local val = not newState
				f[7] = val; f[8] = val; f[9] = val
				setInheritanceFlags s f
			)

		-- Pop Out / Dock
		on popOutBtn pressed do
		(
			if ::WdxObjectSettings.isPoppedOut then
			(
				::WdxObjectSettings.isPoppedOut = false
				try ( destroyDialog mainRollout ) catch()
				
				if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
				(
					local rollouts = ::whiteoutdexManagerFloater.rollouts
					local idx = ::WdxObjectSettings.savedIndex
					if idx != undefined and idx <= rollouts.count then
					(
						local movedRollouts = #()
						for i = rollouts.count to idx by -1 do
						(
							if rollouts[i].title != "WhiteoutDex Manager Floater Rollout" then
							(
								append movedRollouts rollouts[i]
								removeRollout rollouts[i] ::whiteoutdexManagerFloater
							)
						)
						addRollout mainRollout ::whiteoutdexManagerFloater
						for i = movedRollouts.count to 1 by -1 do
							addRollout movedRollouts[i] ::whiteoutdexManagerFloater
					)
					else
						addRollout mainRollout ::whiteoutdexManagerFloater
				)
				return()
			)
			
			if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
			(
				local rollouts = ::whiteoutdexManagerFloater.rollouts
				for i = 1 to rollouts.count do
					if rollouts[i] == mainRollout then ( ::WdxObjectSettings.savedIndex = i; exit )
				removeRollout mainRollout ::whiteoutdexManagerFloater
			)
			
			::WdxObjectSettings.isPoppedOut = true
			createDialog mainRollout width:440 height:232 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox, #style_maximizebox, #style_resizing)
		)
		
		on mainRollout close do
		(
			if ::WdxObjectSettings.isPoppedOut then
			(
				::WdxObjectSettings.isPoppedOut = false
				local tmr = dotNetObject "System.Windows.Forms.Timer"
				tmr.Interval = 1
				dotNet.addEventHandler tmr "Tick" (fn _dockTmr s e =
				(
					s.Stop(); s.Dispose()
					try
					(
						if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
						(
							local rl = ::WdxObjectSettings.mainRollout
							local idx = ::WdxObjectSettings.savedIndex
							local rollouts = ::whiteoutdexManagerFloater.rollouts
							if idx != undefined and idx <= rollouts.count then
							(
								local moved = #()
								for i = rollouts.count to idx by -1 do (if rollouts[i].title != "WhiteoutDex Manager Floater Rollout" then (append moved rollouts[i]; removeRollout rollouts[i] ::whiteoutdexManagerFloater))
								addRollout rl ::whiteoutdexManagerFloater
								for i = moved.count to 1 by -1 do addRollout moved[i] ::whiteoutdexManagerFloater
							)
							else addRollout rl ::whiteoutdexManagerFloater
						)
					) catch()
				))
				tmr.Start()
			)
		)

		-- Localization + initial value load
		on mainRollout open do
		(
			if ::WdxL != undefined then
			(
				mainRollout.title = ::WdxL.t "obj_settings_title"
				dontInhTransChk.text = ::WdxL.t "obj_dont_inherit_translation_chk"
				try (dontInhTransChk.tooltip = ::WdxL.t "obj_ignore_parent_translation_tip") catch ()
				dontInhRotChk.text = ::WdxL.t "obj_dont_inherit_rotation_chk"
				try (dontInhRotChk.tooltip = ::WdxL.t "obj_ignore_parent_rotation_tip") catch ()
				dontInhScaleChk.text = ::WdxL.t "obj_dont_inherit_scaling_chk"
				try (dontInhScaleChk.tooltip = ::WdxL.t "obj_ignore_parent_scaling_tip") catch ()
				bbFullChk.text = ::WdxL.t "obj_billboarded_chk"
				try (bbFullChk.tooltip = ::WdxL.t "obj_object_always_faces_the_camera_tip") catch ()
				bbLockXChk.text = ::WdxL.t "obj_billboarded_lock_x_chk"
				try (bbLockXChk.tooltip = ::WdxL.t "obj_lock_billboard_rotation_around_tip") catch ()
				bbLockYChk.text = ::WdxL.t "obj_billboarded_lock_y_chk"
				try (bbLockYChk.tooltip = ::WdxL.t "obj_lock_billboard_rotation_around_tip_2") catch ()
				bbLockZChk.text = ::WdxL.t "obj_billboarded_lock_z_chk"
				try (bbLockZChk.tooltip = ::WdxL.t "obj_lock_billboard_rotation_around_tip_3") catch ()
				cameraChk.text = ::WdxL.t "obj_camera_anchored_chk"
				try (cameraChk.tooltip = ::WdxL.t "obj_makes_the_object_stay_in_place_tip") catch ()
				try (skinRdo.tooltip = ::WdxL.t "obj_normal_skinning_uses_any_skin_tip") catch ()
				selectChk.text = ::WdxL.t "obj_unselectable_chk"
				try (selectChk.tooltip = ::WdxL.t "obj_makes_the_object_nonselectabl_tip") catch ()
			)
			-- Refresh checkbox state from the current selection on every open.
			-- Without this, the dialog only updates on #selectionSetChanged,
			-- so if you select a node FIRST and THEN open the dialog (or if
			-- nodes get their user props set programmatically by the importer
			-- before the selection changes), the checkboxes show stale values.
			try ( ::WdxObjectSettings.whiteoutDexSTLoadFNSettings() ) catch()
		)
	),

	-- Alias for NeoDex compatibility (NeoDex callbacks may reference this name)
	fn neoDexSTLoadFNSettings = (this.whiteoutDexSTLoadFNSettings()),
	fn whiteoutDexSTLoadFNSettings =
	(
		if not this.mainRollout.open then return()

		fn WdxIsMesh o = (classOf o == Editable_mesh)
		fn tmp a b =
		(
			if a == #NoDefinition then b
			else if a == b then b else undefined
		)
		fn tmp2 o p =
		(
			local b = getUserProp o p
			if b == undefined then 0 else b
		)
		fn tmp3 h = if h == undefined then 0 else h
		fn tmp4 o = if false and o == 0 then 1 else o
		fn tmp5 i = if i == 0 then false else true

		local bbFull = #NoDefinition
		local bbLockX = #NoDefinition
		local bbLockY = #NoDefinition
		local bbLockZ = #NoDefinition
		local skin = #NoDefinition
		local cam = #NoDefinition
		local unsel = #NoDefinition
		local dontInhTrans = #NoDefinition
		local dontInhRot = #NoDefinition
		local dontInhScale = #NoDefinition
		local skinEnabled = true
		local flagsEnabled = true
		local camEnabled = true

		for s in selection do
		(
			-- Alte "Billboard" Property migrieren falls vorhanden
			try ( ::WdxUtils.getBillboardFlags s ) catch()
			
			bbFull  = tmp bbFull  (tmp2 s "Billboarded")
			bbLockX = tmp bbLockX (tmp2 s "BillboardedLockX")
			bbLockY = tmp bbLockY (tmp2 s "BillboardedLockY")
			bbLockZ = tmp bbLockZ (tmp2 s "BillboardedLockZ")
			skin = tmp skin (tmp2 s "Wc3Skin")
			cam = tmp cam (tmp2 s "CameraAnchored")
			unsel = tmp unsel (tmp2 s "Unselectable")
			skinEnabled = skinEnabled and (WdxIsMesh s)
			
			-- DontInherit aus den nativen Max Inheritance Flags lesen
			try
			(
				local inhFlags = getInheritanceFlags s
				local dit = if (inhFlags[1] == false and inhFlags[2] == false and inhFlags[3] == false) then 1 else 0
				local dir = if (inhFlags[4] == false and inhFlags[5] == false and inhFlags[6] == false) then 1 else 0
				local dis = if (inhFlags[7] == false and inhFlags[8] == false and inhFlags[9] == false) then 1 else 0
				dontInhTrans = tmp dontInhTrans dit
				dontInhRot = tmp dontInhRot dir
				dontInhScale = tmp dontInhScale dis
			)
			catch()
		)

		if bbFull == #NoDefinition then
		(
			flagsEnabled = false
			bbFull = 0
		)
		if bbLockX == #NoDefinition then bbLockX = 0
		if bbLockY == #NoDefinition then bbLockY = 0
		if bbLockZ == #NoDefinition then bbLockZ = 0
		if dontInhTrans == #NoDefinition then dontInhTrans = 0
		if dontInhRot == #NoDefinition then dontInhRot = 0
		if dontInhScale == #NoDefinition then dontInhScale = 0
		if skin == #NoDefinition then
		(
			skinEnabled = false
			skin = 0
		)
		if unsel == #NoDefinition then
			unsel = 0
		if cam == #NoDefinition then
		(
			camEnabled = false
			cam = 0
		)

		this.mainRollout.dontInhTransChk.enabled = flagsEnabled
		this.mainRollout.dontInhRotChk.enabled = flagsEnabled
		this.mainRollout.dontInhScaleChk.enabled = flagsEnabled
		this.mainRollout.bbFullChk.enabled = flagsEnabled
		this.mainRollout.bbLockXChk.enabled = flagsEnabled
		this.mainRollout.bbLockYChk.enabled = flagsEnabled
		this.mainRollout.bbLockZChk.enabled = flagsEnabled
		this.mainRollout.skinRdo.enabled = skinEnabled
		this.mainRollout.selectChk.enabled = skinEnabled
		this.mainRollout.cameraChk.enabled = camEnabled
		
		this.mainRollout.dontInhTransChk.checked = tmp5 (tmp3 dontInhTrans)
		this.mainRollout.dontInhRotChk.checked = tmp5 (tmp3 dontInhRot)
		this.mainRollout.dontInhScaleChk.checked = tmp5 (tmp3 dontInhScale)
		this.mainRollout.bbFullChk.checked  = tmp5 (tmp3 bbFull)
		this.mainRollout.bbLockXChk.checked = tmp5 (tmp3 bbLockX)
		this.mainRollout.bbLockYChk.checked = tmp5 (tmp3 bbLockY)
		this.mainRollout.bbLockZChk.checked = tmp5 (tmp3 bbLockZ)
		this.mainRollout.skinRdo.state = tmp4 (tmp3 skin)
		this.mainRollout.cameraChk.checked = tmp5 (tmp3 cam)
		this.mainRollout.selectChk.checked = tmp5 (tmp3 unsel)
	),

	on create do
	(
		this.whiteoutDexSTLoadFNSettings()
		callbacks.removeScripts id:#WhiteoutDexObjectSettings
		callbacks.addscript #selectionSetChanged "::WdxObjectSettings.whiteoutDexSTLoadFNSettings()" id:#WhiteoutDexObjectSettings
	)
)
global WdxObjectSettings = WdxObjectSettings()
