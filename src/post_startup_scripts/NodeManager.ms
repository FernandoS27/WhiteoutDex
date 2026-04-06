struct NodeManager
(
	isPoppedOut = false,
	savedIndex = undefined,
	
	mainRollout = rollout mainRollout "Node Manager"  
	(
		-- Detection functions
		fn isGeometry o = classof o == Editable_Mesh
		fn isAttachmentPoint o = ((classof o) as string == "Wc3_AttachPoint")
		fn isCollisionShape o = ((classof o) as string == "Wc3CollisionBox") or ((classof o) as string == "Wc3CollisionSphere")
		fn isEventObject o = ((classof o) as string == "Wc3_Event")
		fn isParticle1 o = ((classof o) as string == "BlizzParticle1")
		fn isParticle2 o = ((classof o) as string == "BlizzParticle2")
		fn isRibbon o = ((classof o) as string == "BlizRibbon")
		fn isPopcorn o = ((classof o) as string == "BlizzPopcorn")
		fn isFaceFX o = ((classof o) as string == "BlizzFaceFX")
		fn isBipedBone o = classof o == Biped_Object
		fn isCATBone o =
		(
			local c = classof o
			c == CATParent or c == HubObject or c == CATBone
		)
		fn isMaxBone o = classof o == BoneGeometry
		fn isHelperOrDummy o =
		(
			local c = classof o
			if c == Dummy then return true
			if superclassof o == Helper \
				and not (isAttachmentPoint o) \
				and not (isCollisionShape o) \
				and not (isEventObject o) \
				and not (isParticle1 o) \
				and not (isParticle2 o) \
				and not (isRibbon o) \
				and not (isPopcorn o) \
				and not (isFaceFX o) \
				and not (((classof o) as string) == "Wc3_Light") \
			then return true
			false
		)
		fn isCamera o = superclassof o == Camera
		
		mapped fn grayOut o b = o.xray = b
		
		-- Dock/Undock button at very top
		button popOutBtn "Undock" pos:[350, 2] width:56 height:18 tooltip:"Undock / Dock"
		
		-- Left column: Types
		GroupBox typesGrp "Types" pos:[8,24] width:170 height:328
		
		checkbox geoCheck "Geometry" pos:[16,44] width:156
		checkbox bonesCheck "Bones" pos:[16,64] width:156
		checkbox bipedCheck "Biped Bones" pos:[16,84] width:156
		checkbox catCheck "CAT Bones" pos:[16,104] width:156
		checkbox helpersCheck "Helpers / Dummies" pos:[16,124] width:156
		checkbox attachmentsCheck "Attachments" pos:[16,144] width:156
		checkbox eventsCheck "Event Objects" pos:[16,164] width:156
		checkbox lightsCheck "Lights" pos:[16,184] width:156
		checkbox camerasCheck "Cameras" pos:[16,204] width:156
		checkbox particle1Check "Particle Emitters 1" pos:[16,224] width:156
		checkbox particle2Check "Particle Emitters 2" pos:[16,244] width:156
		checkbox ribbonCheck "Ribbon Emitters" pos:[16,264] width:156
		checkbox popcornCheck "Popcorn FX" pos:[16,284] width:156
		checkbox facefxCheck "FaceFX" pos:[16,304] width:156
		checkbox colShapesCheck "Collision Shapes" pos:[16,324] width:156
		
		-- Right column: Actions
		GroupBox actionsGrp "Actions" pos:[184, 44] width:222 height:144
		
		button showSet "Show" pos:[189, 64] width:105 height:22
		button showUnset "Hide" pos:[296, 64] width:105 height:22
		button freezeSet "Freeze" pos:[189, 96] width:105 height:22
		button freezeUnset "Unfreeze" pos:[296, 96] width:105 height:22
		button grayOutSet "See-Through" pos:[189, 128] width:105 height:22
		button grayOutUnset "View Solid" pos:[296, 128] width:105 height:22
		
		-- Quick select buttons
		GroupBox quickGrp "Quick Select" pos:[184, 196] width:222 height:80
		button selectAllBtn "Check All" pos:[189, 216] width:105 height:22
		button selectNoneBtn "Uncheck All" pos:[296, 216] width:105 height:22
		button selectInvertBtn "Invert" pos:[189, 246] width:212 height:22
		
		fn getExpectedGroup =
		(
			local result = #()
			if geoCheck.checked then
				join result (for o in objects where isGeometry o collect o)
			if bonesCheck.checked then
				join result (for o in objects where isMaxBone o collect o)
			if bipedCheck.checked then
				join result (for o in objects where isBipedBone o collect o)
			if catCheck.checked then
				join result (for o in objects where isCATBone o collect o)
			if helpersCheck.checked then
				join result (for o in objects where isHelperOrDummy o collect o)
			if attachmentsCheck.checked then
				join result (for o in objects where isAttachmentPoint o collect o)
			if eventsCheck.checked then
				join result (for o in objects where isEventObject o collect o)
			if lightsCheck.checked then
				join result (for o in objects where ((classof o) as string == "Wc3_Light") collect o)
			if camerasCheck.checked then
				join result (for o in objects where isCamera o collect o)
			if particle1Check.checked then
				join result (for o in objects where isParticle1 o collect o)
			if particle2Check.checked then
				join result (for o in objects where isParticle2 o collect o)
			if ribbonCheck.checked then
				join result (for o in objects where isRibbon o collect o)
			if popcornCheck.checked then
				join result (for o in objects where isPopcorn o collect o)
			if facefxCheck.checked then
				join result (for o in objects where isFaceFX o collect o)
			if colShapesCheck.checked then
				join result (for o in objects where isCollisionShape o collect o)
			result
		)
		
		on grayOutSet pressed do ( grayOut (getExpectedGroup()) true )
		on grayOutUnset pressed do ( grayOut (getExpectedGroup()) false )
		on freezeSet pressed do
		(
			local grp = getExpectedGroup()
			for o in grp where isValidNode o do o.showFrozenInGray = false
			freeze grp
			redrawViews()
		)
		on freezeUnset pressed do ( unfreeze (getExpectedGroup()) )
		on showSet pressed do ( unhide (getExpectedGroup()) )
		on showUnset pressed do ( hide (getExpectedGroup()) )
		
		on selectAllBtn pressed do
		(
			local checks = #(geoCheck, bonesCheck, bipedCheck, catCheck, helpersCheck, attachmentsCheck, eventsCheck, lightsCheck, camerasCheck, particle1Check, particle2Check, ribbonCheck, popcornCheck, facefxCheck, colShapesCheck)
			for c in checks do c.checked = true
		)
		on selectNoneBtn pressed do
		(
			local checks = #(geoCheck, bonesCheck, bipedCheck, catCheck, helpersCheck, attachmentsCheck, eventsCheck, lightsCheck, camerasCheck, particle1Check, particle2Check, ribbonCheck, popcornCheck, facefxCheck, colShapesCheck)
			for c in checks do c.checked = false
		)
		on selectInvertBtn pressed do
		(
			local checks = #(geoCheck, bonesCheck, bipedCheck, catCheck, helpersCheck, attachmentsCheck, eventsCheck, lightsCheck, camerasCheck, particle1Check, particle2Check, ribbonCheck, popcornCheck, facefxCheck, colShapesCheck)
			for c in checks do c.checked = not c.checked
		)
		
		-- Pop Out / Dock
		on popOutBtn pressed do
		(
			if ::NodeManager.isPoppedOut then
			(
				-- Dock back
				::NodeManager.isPoppedOut = false
				try ( destroyDialog mainRollout ) catch()
				
				if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
				(
					local rollouts = ::whiteoutdexManagerFloater.rollouts
					local idx = ::NodeManager.savedIndex
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
			
			-- Save position before pop out
			if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
			(
				local rollouts = ::whiteoutdexManagerFloater.rollouts
				for i = 1 to rollouts.count do
					if rollouts[i] == mainRollout then ( ::NodeManager.savedIndex = i; exit )
				removeRollout mainRollout ::whiteoutdexManagerFloater
			)
			
			::NodeManager.isPoppedOut = true
			popOutBtn.text = ::L.t "common_dock_btn"
			createDialog mainRollout width:420 height:365 style:#(#style_titlebar, #style_sysmenu, #style_minimizebox, #style_resizing)
		)
		
		-- Close handler: dock back into Manager
		on mainRollout close do
		(
			if ::NodeManager.isPoppedOut then
			(
				::NodeManager.isPoppedOut = false
				local tmr = dotNetObject "System.Windows.Forms.Timer"
				tmr.Interval = 1
				dotNet.addEventHandler tmr "Tick" (fn _dockTmr s e =
				(
					s.Stop(); s.Dispose()
					try
					(
						if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
						(
							local rl = ::NodeManager.mainRollout
							local idx = ::NodeManager.savedIndex
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

		-- Localization
		on mainRollout open do
		(
			if ::NodeManager.isPoppedOut then
				popOutBtn.text = ::L.t "common_dock_btn"
			else
				popOutBtn.text = ::L.t "common_undock_btn"
			popOutBtn.tooltip = ::L.t "common_undock_dock_tip"
			
			if ::L != undefined then
			(
				mainRollout.title = ::L.t "node_node_manager_title"
				geoCheck.text = ::L.t "node_geometry_chk"
				bonesCheck.text = ::L.t "node_maxbones_chk"
				lightsCheck.text = ::L.t "node_lights_chk"
				colShapesCheck.text = ::L.t "node_collision_shapes_chk"
				showSet.text = ::L.t "node_show_btn"
				showUnset.text = ::L.t "node_hide_btn"
				freezeSet.text = ::L.t "node_freeze_btn"
				freezeUnset.text = ::L.t "node_unfreeze_btn"
				grayOutSet.text = ::L.t "node_seethrough_btn"
				grayOutUnset.text = ::L.t "node_view_solid_btn"
			)
		)
	),
	
	on create do
	(
	)
)
global NodeManager = NodeManager()
