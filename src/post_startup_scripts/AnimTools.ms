struct WhiteoutDexBoneResult
(
	head = undefined,
	chest = undefined,
	footLeft = undefined,
	footRight = undefined,
	handLeft = undefined,
	handRight = undefined,
	
	-- Funktion zum Ausgeben der Ergebnisse
	fn printResults =
	(
		format "===== Gefundene Bones =====\n"
		format "Head: %\n" (if head != undefined then head.name else "Nicht gefunden")
		format "Chest: %\n" (if chest != undefined then chest.name else "Nicht gefunden")
		format "Foot Left: %\n" (if footLeft != undefined then footLeft.name else "Nicht gefunden")
		format "Foot Right: %\n" (if footRight != undefined then footRight.name else "Nicht gefunden")
		format "Hand Left: %\n" (if handLeft != undefined then handLeft.name else "Nicht gefunden")
		format "Hand Right: %\n" (if handRight != undefined then handRight.name else "Nicht gefunden")
		format "==========================\n"
	)
)

struct WhiteoutDexBoneFinder
(
    scanResult,
    
    -- Funktion zum Testen mehrerer Patterns
    fn testPatterns boneName patterns =
	(
        local lowerName = toLower boneName
        local result = false
        
        for p in patterns do
		(
            if matchPattern lowerName pattern:p then
			(
                result = true
                exit
            )
        )
        result
    ),
    
    -- =============================================
    -- Hilfsfunktionen (Struct-Ebene)
    -- =============================================
    
    -- Prüft ob Keyword als ganzes Wort in den Namensteilen vorkommt
    fn isWholeWord parts keyword =
    (
        for p in parts do
            if p == keyword then return true
        false
    ),
    
    -- False-Positive Erkennung: Wörter die ein Keyword enthalten aber etwas anderes bedeuten
    fn isFalsePositive lowerName keyword =
    (
        if keyword == "head" and findString lowerName "overhead" != undefined then return true
        if keyword == "head" and findString lowerName "arrowhead" != undefined then return true
        if keyword == "hand" and findString lowerName "handle" != undefined then return true
        if keyword == "foot" and findString lowerName "footstep" != undefined then return true
        false
    ),
    
    -- Scoring: Bewertet wie gut ein Bone zu einer Kategorie passt
    -- Höherer Score = besserer Treffer
    fn scoreBone boneName keywords =
    (
        local lowerName = toLower boneName
        local parts = filterString lowerName " _.-"
        local score = 0
        
        -- Keyword-Matching
        for kw in keywords do
        (
            -- False Positives sofort überspringen
            if this.isFalsePositive lowerName kw then continue
            
            -- Ganzes Wort = starker Treffer
            if this.isWholeWord parts kw then
                score += 10
            -- Substring = schwacher Treffer (z.B. "Bip01Head" ohne Trennzeichen)
            else if findString lowerName kw != undefined then
                score += 2
        )
        
        -- Kein Keyword gefunden → kein Kandidat
        if score == 0 then return 0
        
        -- Abzug für Helper/End-Bones die keine echten Rig-Bones sind
        local penaltyWords = #("nub", "tip", "ref", "helper", "dummy", "null", "target", "goal", "ik", "pole", "twist", "roll", "effector")
        for pw in penaltyWords do
        (
            if this.isWholeWord parts pw then
                score -= 5
        )
        
        -- Bonus für kürzere Namen (eher der eigentliche Bone als ein Hilfsobjekt)
        if parts.count <= 2 then score += 3
        else if parts.count <= 3 then score += 1
        
        score
    ),
    
    -- Prüft ob es Links ist
    fn isLeft boneName =
    (
        local lowerName = toLower boneName
        local parts = filterString lowerName " _.-"
        
        for part in parts do
        (
            if part == "left" or part == "l" or part == "lft" then
                return true
        )
        
        (findString lowerName "left" != undefined) or \
        (findString lowerName "_l_" != undefined) or \
        (findString lowerName ".l" != undefined)
    ),
    
    -- Prüft ob es Rechts ist
    fn isRight boneName =
    (
        local lowerName = toLower boneName
        local parts = filterString lowerName " _.-"
        
        for part in parts do
        (
            if part == "right" or part == "r" or part == "rgt" then
                return true
        )
        
        (findString lowerName "right" != undefined) or \
        (findString lowerName "_r_" != undefined) or \
        (findString lowerName ".r" != undefined)
    ),
    
    -- Erweiterte Bone-Scan Funktion

    fn scanBonesWithRegex =
    (
        local result = ::WhiteoutDexBoneResult()
        local allBones = #()
        local highestSpine = undefined
        local highestSpineNum = -1
        
        -- Sammle alle Bones UND andere relevante Objekte
        for obj in objects do
        (
            if classof obj == BoneGeometry \
                or classof obj == Biped_Object \
                or classof obj == CAT_Bone \
                or classof obj == Dummy \
                or classof obj == Point \
                or classof obj == Helper do
            (
                append allBones obj
            )
        )
        
        -- =============================================
        -- HEAD finden (bester Score gewinnt)
        -- =============================================
        local bestHeadScore = 0
        for bone in allBones do
        (
            local s = this.scoreBone bone.name #("head")
            if s > bestHeadScore then
            (
                bestHeadScore = s
                result.head = bone
            )
        )
        
        -- =============================================
        -- CHEST finden (bester Score gewinnt)
        -- =============================================
        local bestChestScore = 0
        for bone in allBones do
        (
            local s = this.scoreBone bone.name #("chest", "thorax", "ribcage")
            if s > bestChestScore then
            (
                bestChestScore = s
                result.chest = bone
            )
        )
        
        -- =============================================
        -- SPINE suchen (Fallback für Chest)
        -- =============================================
        for bone in allBones do
        (
            local lowerName = toLower bone.name
            if findString lowerName "spine" != undefined then
            (
                local spineNum = 0
                for i = 0 to 20 do
                (
                    if findString lowerName ("spine" + (i as string)) != undefined or \
                    findString lowerName ("spine " + (i as string)) != undefined or \
                    findString lowerName ("spine_" + (i as string)) != undefined then
                    (
                        spineNum = i
                        exit
                    )
                )
                
                if spineNum > highestSpineNum then
                (
                    highestSpineNum = spineNum
                    highestSpine = bone
                )
                else if spineNum == 0 and highestSpine == undefined then
                (
                    highestSpine = bone
                )
            )
        )
        
        if result.chest == undefined and highestSpine != undefined then
            result.chest = highestSpine
        
        -- =============================================
        -- HAND finden (bester Score pro Seite)
        -- =============================================
        local handKeywords = #("hand", "finger", "wrist", "palm")
        local bestHandLeftScore = 0
        local bestHandRightScore = 0
        local bestHandUnknownScore = 0
        local bestHandUnknown = undefined
        
        for bone in allBones do
        (
            local s = this.scoreBone bone.name handKeywords
            if s > 0 then
            (
                if this.isLeft bone.name then
                (
                    if s > bestHandLeftScore then
                    (
                        bestHandLeftScore = s
                        result.handLeft = bone
                    )
                )
                else if this.isRight bone.name then
                (
                    if s > bestHandRightScore then
                    (
                        bestHandRightScore = s
                        result.handRight = bone
                    )
                )
                else
                (
                    if s > bestHandUnknownScore then
                    (
                        bestHandUnknownScore = s
                        bestHandUnknown = bone
                    )
                )
            )
        )
        
        -- Unbekannte Seite als Fallback
        if bestHandUnknown != undefined then
        (
            if result.handLeft == undefined then
                result.handLeft = bestHandUnknown
            else if result.handRight == undefined then
                result.handRight = bestHandUnknown
        )
        
        -- =============================================
        -- FOOT finden (bester Score pro Seite)
        -- =============================================
        local footKeywords = #("foot", "toe", "ankle")
        local bestFootLeftScore = 0
        local bestFootRightScore = 0
        local bestFootUnknownScore = 0
        local bestFootUnknown = undefined
        
        for bone in allBones do
        (
            local s = this.scoreBone bone.name footKeywords
            if s > 0 then
            (
                if this.isLeft bone.name then
                (
                    if s > bestFootLeftScore then
                    (
                        bestFootLeftScore = s
                        result.footLeft = bone
                    )
                )
                else if this.isRight bone.name then
                (
                    if s > bestFootRightScore then
                    (
                        bestFootRightScore = s
                        result.footRight = bone
                    )
                )
                else
                (
                    if s > bestFootUnknownScore then
                    (
                        bestFootUnknownScore = s
                        bestFootUnknown = bone
                    )
                )
            )
        )
        
        -- Unbekannte Seite als Fallback
        if bestFootUnknown != undefined then
        (
            if result.footLeft == undefined then
                result.footLeft = bestFootUnknown
            else if result.footRight == undefined then
                result.footRight = bestFootUnknown
        )
        
        result -- return
    ),
    
	fn scan =
	(
		this.scanResult = this.scanBonesWithRegex()
	)
)
global WhiteoutDexBoneFinder = WhiteoutDexBoneFinder()

struct WdxAnimTools
(
    isGeneralPoppedOut = false,
    savedGeneralIndex = undefined,
    isControllersPoppedOut = false,
    savedControllersIndex = undefined,
    isSkinPoppedOut = false,
    savedSkinIndex = undefined,
    isScalerPoppedOut = false,
    savedScalerIndex = undefined,
    
    generalRoll = rollout generalRoll "General Tools"
    (
        local animToolsSequences
        -- UI
        
        groupBox animationGrpB "Animation" pos:[8, 22] width:138 height:110
        button addSEkeysbtn "Add\nstart and ending keys\nto selected objects" pos:[16,38] width:122 height:86 tooltip:"Adds keyframes to frame 0 and at the start and end of all animation sequences for selected objects"

        groupBox tcbToolsGrpB "TCB Tools" pos:[152, 22] width:138 height:110
        checkbox PosChk "Position" pos:[160,38] width:120 height:22 tooltip:"Apply continuity adjustment to position controllers"
        checkbox RotChk "Rotation" pos:[160,58] width:120 height:22 tooltip:"Apply continuity adjustment to rotation controllers"
        checkbox ScaChk "Scaling" pos:[160,78] width:120 height:22 tooltip:"Apply continuity adjustment to scaling controllers"
        button continuityBtn "Set Continuity to Zero" pos:[160,102] width:122 height:22 tooltip:"Sets the continuity parameter to zero for the current keyframes of selected objects"

        groupBox smoothRotationsGrpB "Smooth Rotations" pos:[296, 22] width:138 height:110
        button fixSquadbtn "Fix non-linear\nrotation problems\non selection" pos:[304,38] width:122 height:86 tooltip:"Non-linear rotation keys may animate improperly in wc3 if they go beyond 90\xC2\xB0. This tool will apply fixes to those keys by creating a new key between them. Note that you may have to run the tool more than once in some weird occasions where you created rotations of over 180\xC2\xB0"

		groupBox boneToolsGrpB "Bone Tools" pos:[8, 136] width:426 height:46
		button createAttachmentPointsBtn "Create Attachment Points" pos:[16, 152] width:410 height:22 tooltip:"Automatically create Attachment Points for Bones that have known names."
		
        -- Funktionen
        mapped fn setContinuityToZero o time &controllerFunc =
        (
            local c = controllerFunc o
            if (c != undefined) then
            (
                local index = ArrayFindItemEx c.keys time (fn tmp a b = a.time == b)
                if (index != 0) then c.keys[index].continuity = 0
            )
        )

        fn GetRot t o r pr hp =
        (
            at time t local v = o.rotation * r
            if hp then at time t v = (v/(o.parent.rotation * pr))
            v
        )
		
		-- Berechnet die Modellhoehe aus allen sichtbaren Geometrie-Objekten
		fn getModelHeight =
		(
			local minZ = 0.0
			local maxZ = 0.0
			local found = false
			for obj in objects where superClassOf obj == GeometryClass and not obj.isHidden do
			(
				try
				(
					local bb = nodeGetBoundingBox obj (matrix3 1)
					if not found then
					(
						minZ = bb[1].z
						maxZ = bb[2].z
						found = true
					)
					else
					(
						if bb[1].z < minZ then minZ = bb[1].z
						if bb[2].z > maxZ then maxZ = bb[2].z
					)
				)
				catch ()
			)
			if found then (maxZ - minZ) else 100.0
		)
		
		fn createAttachmentNode rigBone type modelHeight =
		(
			-- Skalierungsfaktor: Gizmo ist ~10 Units, bei 100 Unit Modell passt das
			local scaleFactor = modelHeight / 100.0
			if scaleFactor < 0.01 then scaleFactor = 0.01
			
			local obj = Wdx_Wc3AttachPoint()
			case type of
			(
				#head:
				(
					obj.name = "Head Ref"
					obj.nameFirst = 1
					obj.nameAdd1 = 1
				)
				#chest:
				(
					obj.name = "Chest Ref"
					obj.nameFirst = 5
					obj.nameAdd1 = 1
				)
				#footLeft:
				(
					obj.name = "Foot Left Ref"
					obj.nameFirst = 4
					obj.nameAdd1 = 3
				)
				#footRight:
				(
					obj.name = "Foot Right Ref"
					obj.nameFirst = 4
					obj.nameAdd1 = 2
				)
				#handLeft:
				(
					obj.name = "Hand Left Ref"
					obj.nameFirst = 6
					obj.nameAdd1 = 3
				)
				#handRight:
				(
					obj.name = "Hand Right Ref"
					obj.nameFirst = 6
					obj.nameAdd1 = 2
				)
				#origin:
				(
					obj.name = "Origin Ref"
					obj.nameFirst = 3
				)
				#overhead:
				(
					obj.name = "Overhead Ref"
					obj.nameFirst = 2
				)
			)
			
			if rigBone != undefined then
			(
				obj.transform = rigBone.transform
				obj.parent = rigBone
				
				if type == #overhead then
				(
					-- Proportionaler Overhead-Offset
					obj.pos.z += 50.0 * scaleFactor
				)
			)
			
			-- Gizmo-Groesse proportional zum Modell skalieren
			obj.scale = [scaleFactor, scaleFactor, scaleFactor]
			
			local attachmentsLayer = LayerManager.getLayerFromName "Attachments"
			if attachmentsLayer == undefined then
			(
				attachmentsLayer = LayerManager.newLayerFromName "Attachments"
			)
			attachmentsLayer.addnode obj
			
			print ("Attachment Point: " + obj.name + " erstellt. (Scale: " + (scaleFactor as string) + ")")
		)
        
        mapped fn fixSquadRotation s =
        (
            local c = s.rotation.controller
            local b = (classOf c == tcb_rotation or classOf c == bezier_rotation)
            if (b and c.keys.count > 1) then
            (
                local faults = #()
                local keys = c.keys
                local hasParent = s.parent != undefined and (classOf s.parent != Uvwmap) -- For UVW Gizmo
                local r = (inverse s.rotation)
                local pr = (quat 0 0 0 1)
                if hasParent then pr = (inverse s.parent.rotation)
                for i = 2 to keys.count do
                (
                    local v = GetRot keys[i-1].time s r pr hasParent
                    local w = GetRot keys[i].time s r pr hasParent
                    if (CheckLock v w) then
                        append faults (interval keys[i-1].time keys[i].time)
                )
                for f in faults do addNewKey c ((f.start + f.end) * 0.5)
            )
        )
        
        fn createStartEndkeys sel =
        (
            if animToolsSequences.count == 0 then
            (
                print "No sequences found in the scene."
                return false
            )
            
            fn tmp c =
            (
                if c != undefined then
                (
                    local superKeys = ::WdxUtils.getKeysOnSequences c.keys animToolsSequences
                    local count = animToolsSequences.count
                    addNewKey c 0f
                    for i = 1 to count do
                    (
                        addNewKey c animToolsSequences[i].start 
                        addNewKey c animToolsSequences[i].end 
                    )
                )
            )
            
            for s in sel do
            (
                -- Prüfe ob es ein Biped-Objekt ist
                if classOf s == Biped_Object then
                (
                    -- Biped-Objekte haben eine spezielle Controller-Struktur
                    try
                    (
                        -- Biped Position Controller
                        if s.controller.vertical != undefined and s.controller.vertical.controller != undefined then
                            tmp s.controller.vertical.controller
                        if s.controller.horizontal != undefined and s.controller.horizontal.controller != undefined then
                            tmp s.controller.horizontal.controller
                        if s.controller.turning != undefined and s.controller.turning.controller != undefined then
                            tmp s.controller.turning.controller
                    )
                    catch()
                )
                else
                (
                    -- Normale Objekte und Kameras
                    
                    -- Position - prüfe ob Property existiert
                    try 
                    (
                        if s.pos != undefined and s.pos.controller != undefined then
                            tmp s.pos.controller
                    ) 
                    catch 
                    (
                        -- Objekt hat keine Standard-Position Property
                    )
                    
                    -- Rotation - prüfe ob Property existiert
                    try 
                    (
                        if s.rotation != undefined and s.rotation.controller != undefined then
                            tmp s.rotation.controller
                    ) 
                    catch 
                    (
                        -- Objekt hat keine Rotation (z.B. Kamera)
                    )
                    
                    -- Scale - prüfe ob Property existiert
                    try 
                    (
                        if s.scale != undefined and s.scale.controller != undefined then
                            tmp s.scale.controller
                    ) 
                    catch 
                    (
                        -- Objekt hat kein Scale
                    )
                    
                    -- Spezialbehandlung für Kameras mit Roll
                    if classOf s == Targetcamera or classOf s == Freecamera then
                    (
                        try 
                        (
                            if s.controller != undefined and s.controller.roll_angle != undefined and s.controller.roll_angle.controller != undefined then
                                tmp s.controller.roll_angle.controller
                        ) 
                        catch ()
                    )
                )
            )
        )
        
        -- Event-Handler
        on continuityBtn pressed do
        (
            fn PosController o =
            (
                local c = o.pos.controller
                if classOf c != tcb_position then c = undefined
                c
            )
            fn RotController o =
            (
                local c = o.rotation.controller
                if classOf c != tcb_rotation then c = undefined
                c
            )
            fn ScaController o =
            (
                local c = o.scale.controller
                if classOf c != tcb_scale then c = undefined
                c
            )
            if PosChk.checked then setContinuityToZero selection currentTime PosController
            if RotChk.checked then setContinuityToZero selection currentTime RotController
            if ScaChk.checked then setContinuityToZero selection currentTime ScaController
        )
        
        on addSEkeysbtn pressed do
        (
            undo "Add Start/End Keys" on
            (
                animToolsSequences = ::WdxUtils.getAnimationIntervals()
                createStartEndkeys selection
            )
        )
        
        on fixSquadbtn pressed do fixSquadRotation selection
			
		on createAttachmentPointsBtn pressed do
		(
			undo "Create Attachment Points" on
			(
				-- Slider auf Frame 0 bewegen damit Bone-Transforms der Initial-Pose gelesen werden
				local savedTime = sliderTime
				sliderTime = 0
				
				::WhiteoutDexBoneFinder.scan()
				local mHeight = getModelHeight()
				
				if not isValidNode $'Origin Ref' then createAttachmentNode undefined #origin mHeight
				
				if not isValidNode $'Head Ref' \
					and isValidNode ::WhiteoutDexBoneFinder.scanResult.head then
				(
					createAttachmentNode ::WhiteoutDexBoneFinder.scanResult.head #head mHeight
					createAttachmentNode ::WhiteoutDexBoneFinder.scanResult.head #overhead mHeight
				)
				if not isValidNode $'Chest Ref' \
					and isValidNode ::WhiteoutDexBoneFinder.scanResult.chest then createAttachmentNode ::WhiteoutDexBoneFinder.scanResult.chest #chest mHeight
				if not isValidNode $'Foot Left Ref' \
					and isValidNode ::WhiteoutDexBoneFinder.scanResult.footLeft then createAttachmentNode ::WhiteoutDexBoneFinder.scanResult.footLeft #footLeft mHeight
				if not isValidNode $'Foot Right Ref' \
					and isValidNode ::WhiteoutDexBoneFinder.scanResult.footRight then createAttachmentNode ::WhiteoutDexBoneFinder.scanResult.footRight #footRight mHeight
				if not isValidNode $'Hand Left Ref' \
					and isValidNode ::WhiteoutDexBoneFinder.scanResult.handLeft then createAttachmentNode ::WhiteoutDexBoneFinder.scanResult.handLeft #handLeft mHeight
				if not isValidNode $'Hand Right Ref' \
					and isValidNode ::WhiteoutDexBoneFinder.scanResult.handRight then createAttachmentNode ::WhiteoutDexBoneFinder.scanResult.handRight #handRight mHeight
				
				-- Slider zurück zur ursprünglichen Position
				sliderTime = savedTime
			)
		)

		-- Pop Out / Dock
		on popOutBtn pressed do
		(
			if ::WdxAnimTools.isGeneralPoppedOut then
			(
				::WdxAnimTools.isGeneralPoppedOut = false
				try ( destroyDialog generalRoll ) catch()
				
				if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
				(
					local rollouts = ::whiteoutdexManagerFloater.rollouts
					local idx = ::WdxAnimTools.savedGeneralIndex
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
						addRollout generalRoll ::whiteoutdexManagerFloater
						for i = movedRollouts.count to 1 by -1 do
							addRollout movedRollouts[i] ::whiteoutdexManagerFloater
					)
					else
						addRollout generalRoll ::whiteoutdexManagerFloater
				)
				return()
			)
			
			if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
			(
				local rollouts = ::whiteoutdexManagerFloater.rollouts
				for i = 1 to rollouts.count do
					if rollouts[i] == generalRoll then ( ::WdxAnimTools.savedGeneralIndex = i; exit )
				removeRollout generalRoll ::whiteoutdexManagerFloater
			)
			
			::WdxAnimTools.isGeneralPoppedOut = true
			createDialog generalRoll width:440 height:202 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox, #style_maximizebox, #style_resizing)
		)
		
		on generalRoll close do
		(
			if ::WdxAnimTools.isGeneralPoppedOut then
			(
				::WdxAnimTools.isGeneralPoppedOut = false
				local tmr = dotNetObject "System.Windows.Forms.Timer"
				tmr.Interval = 1
				dotNet.addEventHandler tmr "Tick" (fn _dockTmr s e =
				(
					s.Stop(); s.Dispose()
					try
					(
						if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
						(
							local rl = ::WdxAnimTools.generalRoll
							local idx = ::WdxAnimTools.savedGeneralIndex
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
		on generalRoll open do
		(
			-- Toolkit icon on the dialog frame (WhiteoutDexUtility.ms).
			try (WdxSetDialogIcon generalRoll) catch()
			if ::WdxL != undefined then
			(
				generalRoll.title = ::WdxL.t "anim_general_tools_title"
				addSEkeysbtn.text = ::WdxL.t "anim_addnstart_and_ending_keysnto_selected_ob_btn"
				try (addSEkeysbtn.tooltip = ::WdxL.t "anim_adds_keyframes_to_frame_0_and_tip") catch ()
				PosChk.text = ::WdxL.t "anim_position_chk"
				try (PosChk.tooltip = ::WdxL.t "anim_apply_continuity_adjustment_to_tip") catch ()
				RotChk.text = ::WdxL.t "anim_rotation_chk"
				try (RotChk.tooltip = ::WdxL.t "anim_apply_continuity_adjustment_to_tip_2") catch ()
				ScaChk.text = ::WdxL.t "anim_scaling_chk"
				try (ScaChk.tooltip = ::WdxL.t "anim_apply_continuity_adjustment_to_tip_3") catch ()
				continuityBtn.text = ::WdxL.t "anim_set_continuity_to_zero_btn"
				try (continuityBtn.tooltip = ::WdxL.t "anim_sets_the_continuity_parameter_tip") catch ()
				fixSquadbtn.text = ::WdxL.t "anim_fix_nonlinearnrotation_problemsnon_selec_btn"
				try (fixSquadbtn.tooltip = ::WdxL.t "anim_nonlinear_rotation_keys_may_a_tip") catch ()
				createAttachmentPointsBtn.text = ::WdxL.t "anim_create_attachment_points_btn"
				try (createAttachmentPointsBtn.tooltip = ::WdxL.t "anim_automatically_create_attachmen_tip") catch ()
			)
		)
    ),
    
    skinRoll = rollout skinRoll "Skinning"
    (
        -- UI
        
        groupBox atSkinningGrpB "AT Skinning" pos:[8,22] width:426 height:48
        button saveBtn "Save and Unlink" pos:[16,38] width:198 height:22 tooltip:"Saves parent relationships of selected objects and unlinks them from their parents\n\nSometimes you already got a bunch of meshes linked to some bones and suddenly you want to edit the bones. If you don't want to mess up the meshes, you must unlink them, do whatever you need, then relink. This tool saves the parents of the selected meshes, unlinks them and restores them when you want. Use this tool before converting bones from IK to FK, for instance."
        button restoreBtn "Restore Parents" pos:[222,38] width:198 height:22 tooltip:"Restores previously saved parent relationships for selected objects\n\nSometimes you already got a bunch of meshes linked to some bones and suddenly you want to edit the bones. If you don't want to mess up the meshes, you must unlink them, do whatever you need, then relink. This tool saves the parents of the selected meshes, unlinks them and restores them when you want. Use this tool before converting bones from IK to FK, for instance."
        
        -- Funktionen
        mapped fn saveParents s =
        (
            local p = s.parent
            if p != undefined then setUserProp s "OriginalParent" p.name
            s.parent = undefined
        )

        mapped fn restoreParents s =
        (
            local tmp = getUserProp s "OriginalParent"
            if tmp != undefined then
            (
                local p = getNodeByName tmp
                s.parent = p
            )
        )
        
        -- Event-Handler
        on saveBtn pressed do saveParents selection
        on restoreBtn pressed do restoreParents selection

		-- Pop Out / Dock
		on popOutBtn pressed do
		(
			if ::WdxAnimTools.isSkinPoppedOut then
			(
				::WdxAnimTools.isSkinPoppedOut = false
				try ( destroyDialog skinRoll ) catch()
				
				if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
				(
					local rollouts = ::whiteoutdexManagerFloater.rollouts
					local idx = ::WdxAnimTools.savedSkinIndex
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
						addRollout skinRoll ::whiteoutdexManagerFloater
						for i = movedRollouts.count to 1 by -1 do
							addRollout movedRollouts[i] ::whiteoutdexManagerFloater
					)
					else
						addRollout skinRoll ::whiteoutdexManagerFloater
				)
				return()
			)
			
			if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
			(
				local rollouts = ::whiteoutdexManagerFloater.rollouts
				for i = 1 to rollouts.count do
					if rollouts[i] == skinRoll then ( ::WdxAnimTools.savedSkinIndex = i; exit )
				removeRollout skinRoll ::whiteoutdexManagerFloater
			)
			
			::WdxAnimTools.isSkinPoppedOut = true
			createDialog skinRoll width:440 height:92 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox, #style_maximizebox, #style_resizing)
		)
		
		on skinRoll close do
		(
			if ::WdxAnimTools.isSkinPoppedOut then
			(
				::WdxAnimTools.isSkinPoppedOut = false
				local tmr = dotNetObject "System.Windows.Forms.Timer"
				tmr.Interval = 1
				dotNet.addEventHandler tmr "Tick" (fn _dockTmr s e =
				(
					s.Stop(); s.Dispose()
					try
					(
						if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
						(
							local rl = ::WdxAnimTools.skinRoll
							local idx = ::WdxAnimTools.savedSkinIndex
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
		on skinRoll open do
		(
			-- Toolkit icon on the dialog frame (WhiteoutDexUtility.ms).
			try (WdxSetDialogIcon skinRoll) catch()
			if ::WdxL != undefined then
			(
				skinRoll.title = ::WdxL.t "anim_skinning_title"
				saveBtn.text = ::WdxL.t "anim_save_and_unlink_btn"
				try (saveBtn.tooltip = ::WdxL.t "anim_saves_parent_relationships_of_tip") catch ()
				restoreBtn.text = ::WdxL.t "anim_restore_parents_btn"
				try (restoreBtn.tooltip = ::WdxL.t "anim_restores_previously_saved_pare_tip") catch ()
			)
		)
    ),
        
    controllersRoll = rollout controllersRoll "Controllers" 
    (
        -- UI
        
        groupBox controllersGrpB "Controllers" pos:[8,22] width:426 height:140
        
        radiobuttons PosRdo "Position:" labels:#("Linear", "Bezier", "TCB", "Ignore") columns:1 default:4 across:3 pos:[20,42] width:120 tooltip:"Choose the type of controller to apply to position tracks"
        radiobuttons RotRdo "Rotation:" labels:#("Linear", "Smooth", "TCB", "Ignore") columns:1 default:4 pos:[160,42] width:120 tooltip:"Choose the type of controller to apply to rotation tracks"
        radiobuttons ScaRdo "Scaling" labels:#("Linear", "Bezier", "TCB", "Ignore") columns:1 default:4 pos:[300,42] width:120 tooltip:"Choose the type of controller to apply to scaling tracks"
        
        button controllerBtn "Apply to selected" width:410 height:22 pos:[16,132] tooltip:"Applies the selected controller types to the selected objects"
        
        -- Funktionen
        mapped fn applyControllers obj posCon rotCon scaCon =
        (
            -- Überspringe nur Biped-Objekte
            if classOf obj == Biped_Object then
            (
                print ("Skipping Biped object: " + obj.name + " - Biped controllers cannot be changed")
                return undefined
            )
            
            -- Normale Objekte und CAT Bones - verwende try-catch für Sicherheit
            try
            (
                if posCon != undefined and obj.position != undefined then 
                    obj.position.controller = posCon()
            )
            catch
            (
                print ("Could not change position controller for: " + obj.name)
            )
            
            try
            (
                if rotCon != undefined and obj.rotation != undefined then 
                    obj.rotation.controller = rotCon()
            )
            catch
            (
                print ("Could not change rotation controller for: " + obj.name)
            )
            
            try
            (
                if scaCon != undefined and obj.scale != undefined then 
                    obj.scale.controller = scaCon()
            )
            catch
            (
                print ("Could not change scale controller for: " + obj.name)
            )
        )
        
        -- Event-Handler
        on controllerBtn pressed do
        (
            undo "Apply Controllers" on
            (
                local posCon = case PosRdo.state of
                (
                    1: linear_position
                    2: bezier_position
                    3: tcb_position
                    default: undefined
                )
                
                local rotCon = case RotRdo.state of
                (
                    1: linear_rotation
                    2: bezier_rotation
                    3: tcb_rotation
                    default: undefined
                )
                local scaCon = case ScaRdo.state of
                (
                    1: linear_scale
                    2: bezier_scale
                    3: tcb_scale
                    default: undefined
                )
                
                applyControllers selection posCon rotCon scaCon
            )
        )

		-- Pop Out / Dock
		on popOutBtn pressed do
		(
			if ::WdxAnimTools.isControllersPoppedOut then
			(
				::WdxAnimTools.isControllersPoppedOut = false
				try ( destroyDialog controllersRoll ) catch()
				
				if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
				(
					local rollouts = ::whiteoutdexManagerFloater.rollouts
					local idx = ::WdxAnimTools.savedControllersIndex
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
						addRollout controllersRoll ::whiteoutdexManagerFloater
						for i = movedRollouts.count to 1 by -1 do
							addRollout movedRollouts[i] ::whiteoutdexManagerFloater
					)
					else
						addRollout controllersRoll ::whiteoutdexManagerFloater
				)
				return()
			)
			
			if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
			(
				local rollouts = ::whiteoutdexManagerFloater.rollouts
				for i = 1 to rollouts.count do
					if rollouts[i] == controllersRoll then ( ::WdxAnimTools.savedControllersIndex = i; exit )
				removeRollout controllersRoll ::whiteoutdexManagerFloater
			)
			
			::WdxAnimTools.isControllersPoppedOut = true
			createDialog controllersRoll width:440 height:177 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox, #style_maximizebox, #style_resizing)
		)
		
		on controllersRoll close do
		(
			if ::WdxAnimTools.isControllersPoppedOut then
			(
				::WdxAnimTools.isControllersPoppedOut = false
				local tmr = dotNetObject "System.Windows.Forms.Timer"
				tmr.Interval = 1
				dotNet.addEventHandler tmr "Tick" (fn _dockTmr s e =
				(
					s.Stop(); s.Dispose()
					try
					(
						if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
						(
							local rl = ::WdxAnimTools.controllersRoll
							local idx = ::WdxAnimTools.savedControllersIndex
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
		on controllersRoll open do
		(
			-- Toolkit icon on the dialog frame (WhiteoutDexUtility.ms).
			try (WdxSetDialogIcon controllersRoll) catch()
			if ::WdxL != undefined then
			(
				controllersRoll.title = ::WdxL.t "anim_controllers_title"
				controllerBtn.text = ::WdxL.t "anim_apply_to_selected_btn"
				try (controllerBtn.tooltip = ::WdxL.t "anim_applies_the_selected_controlle_tip") catch ()
			)
		)
    ),
    
    scalerRoll = rollout scalerRoll "Model Scaler"
    (
        -- UI
        
        groupBox presetGrpB "Scale to Target Height" pos:[8,22] width:426 height:70
        dropdownlist ddPreset "" items:#(\
            "-- Wc3 Classic (800 poly) --", \
            "Wc3 Male Character 800", \
            "Wc3 Female Character 800", \
            "Wc3 Footman 800", \
            "Wc3 Farm 800", \
            "Wc3 Human Barracks 800", \
            "Wc3 Townhall 800", \
            "-- Wc3 Reforged --", \
            "Wc3 Male Character Reforged", \
            "Wc3 Female Character Reforged", \
            "Wc3 Infernal Reforged", \
            "Wc3 Townhall Reforged" \
        ) pos:[16,40] width:310

        button btnPreset "Scale" pos:[334,40] width:90 height:22

        groupBox manualGrpB "Manual Factor" pos:[8,94] width:426 height:46
        spinner spnScale "Factor:" pos:[16,112] range:[0.01, 100.0, 1.25] type:#float scale:0.01 width:160 fieldWidth:60
        button btnApply "Apply" pos:[334,110] width:90 height:22

        label lblStatus "" pos:[16,144] width:420 height:14

        -- Preset Heights (measured with ModelHeightScanner)
        fn getPresetHeight name =
        (
            case name of
            (
                "Wc3 Male Character 800":       120.941
                "Wc3 Female Character 800":      113.454
                "Wc3 Footman 800":               102.327
                "Wc3 Farm 800":                  268.318
                "Wc3 Human Barracks 800":        394.272
                "Wc3 Townhall 800":              568.847
                "Wc3 Male Character Reforged":   114.074
                "Wc3 Female Character Reforged": 103.114
                "Wc3 Infernal Reforged":         247.783
                "Wc3 Townhall Reforged":         564.922
                default: 0.0
            )
        )

        -- Detection
        fn isBipedNode obj = ( classOf obj == Biped_Object )
        fn isCATNode obj =
        (
            local cn = (classOf obj) as string
            cn == "CATParent" or cn == "CATBone" or cn == "HubObject" or cn == "IKTarget"
        )
        fn hasSkinMod obj =
        (
            for i = 1 to obj.modifiers.count do
                if classOf obj.modifiers[i] == Skin do return true
            false
        )
        fn isVisibleMesh obj =
        (
            if superClassOf obj != GeometryClass do return false
            local cn = (classOf obj) as string
            if cn == "BoneGeometry" or cn == "Biped_Object" do return false
            if cn == "CATParent" or cn == "CATBone" or cn == "HubObject" or cn == "IKTarget" do return false
            if cn == "Wc3Particles1" or cn == "Wc3Particles2" or cn == "BlizRibbon" do return false
            if cn == "Wdx_CollisionSphere" or cn == "Wdx_CollisionBox" or cn == "Wc3CollisionCylinder" do return false
            if cn == "Wdx_Wc3Light" or cn == "Wdx_Wc3Popcorn" do return false
            true
        )
        fn measureMeshHeight =
        (
            local minZ = 1e9
            local maxZ = -1e9
            local count = 0
            for obj in objects where (isVisibleMesh obj) do
            ( try ( local bb = nodeGetBoundingBox obj (matrix3 1); if bb[1].z < minZ do minZ = bb[1].z; if bb[2].z > maxZ do maxZ = bb[2].z; count += 1 ) catch() )
            if count == 0 then 0.0 else (maxZ - minZ)
        )
        fn detectRig =
        (
            local hasBiped = false; local hasCat = false; local stdRoots = #()
            for obj in objects do
            (
                if classOf obj == Biped_Object do hasBiped = true
                if isCATNode obj do hasCat = true
                if obj.parent == undefined do
                    if not (isBipedNode obj) and not (isCATNode obj) do append stdRoots obj
            )
            #(hasBiped, hasCat, stdRoots)
        )

        -- WC3 Plugin Scaling
        fn scaleWc3 factor =
        (
            local count = 0
            for obj in objects do
            (
                local cn = (classOf obj) as string
                if cn == "Wc3Particles2" do
                ( try(obj.width *= factor)catch(); try(obj.length *= factor)catch(); try(obj.speed *= factor)catch(); try(obj.gravity *= factor)catch(); try(obj.startscale *= factor)catch(); try(obj.middlescale *= factor)catch(); try(obj.endscale *= factor)catch(); try(obj.taillength *= factor)catch(); count += 1 )
                if cn == "Wc3Particles1" do
                ( try(obj.radius *= factor)catch(); try(obj.Gravity *= factor)catch(); count += 1 )
                if cn == "BlizRibbon" do
                ( try(obj.Above *= factor)catch(); try(obj.Below *= factor)catch(); try(obj.gravity *= factor)catch(); count += 1 )
                if cn == "Wdx_CollisionSphere" do ( try(obj.radius *= factor)catch(); count += 1 )
                if cn == "Wdx_CollisionBox" do ( try(obj.width *= factor)catch(); try(obj.length *= factor)catch(); try(obj.height *= factor)catch(); count += 1 )
                if cn == "Wdx_Wc3Light" do ( try(obj.DS_Spn *= factor)catch(); try(obj.DE_Spn *= factor)catch(); count += 1 )
                if cn == "Wdx_Wc3Popcorn" do ( try(obj.speed *= factor)catch(); count += 1 )
            )
            count
        )

        -- Scale Keys
        fn scalePositionKeys obj factor =
        (
            local posCtrl = obj.pos.controller
            if posCtrl == undefined do return false
            local cn = classOf posCtrl
            if cn == Position_XYZ do
            ( for axis = 1 to 3 do try ( local sub = posCtrl[axis].controller; for k = 1 to (numKeys sub) do (getKey sub k).value *= factor ) catch(); return true )
            if cn == bezier_position or cn == linear_position or cn == tcb_position do
            ( try ( for k = 1 to (numKeys posCtrl) do (getKey posCtrl k).value *= factor; return true ) catch() )
            if cn == position_list do
            ( try ( for i = 1 to posCtrl.count do ( local sub = posCtrl[i].controller; local scn = classOf sub;
                if scn == Position_XYZ do ( for axis = 1 to 3 do try ( local ax = sub[axis].controller; for k = 1 to (numKeys ax) do (getKey ax k).value *= factor ) catch() )
                if scn == bezier_position or scn == linear_position or scn == tcb_position do ( try ( for k = 1 to (numKeys sub) do (getKey sub k).value *= factor ) catch() )
            ); return true ) catch() )
            false
        )
        fn scaleScaleKeys obj factor =
        ( try ( local sclCtrl = obj.scale.controller; local cn = classOf sclCtrl;
            if cn == bezier_scale or cn == linear_scale do ( for k = 1 to (numKeys sclCtrl) do (getKey sclCtrl k).value *= [factor, factor, factor] )
        ) catch() )

        -- Scale Roots
        fn scaleRootsAllKeys roots factor =
        (
            local skinned = 0; local scaled = 0
            for obj in roots do
            (
                if hasSkinMod obj then ( skinned += 1 )
                else ( try (
                    local posAnim = false; try ( posAnim = obj.pos.isAnimated ) catch()
                    if posAnim then (scalePositionKeys obj factor) else (obj.pos = obj.pos * factor)
                    local sclAnim = false; try ( sclAnim = obj.scale.isAnimated ) catch()
                    if sclAnim then (scaleScaleKeys obj factor) else (obj.scale *= [factor, factor, factor])
                    scaled += 1
                ) catch() )
            )
            #(scaled, skinned)
        )
        fn scaleRootsAtFrame roots factor =
        (
            local skinned = 0; local scaled = 0
            with animate on at time (sliderTime.frame as integer)
            ( for obj in roots do (
                if hasSkinMod obj then ( skinned += 1 )
                else ( try ( obj.pos = obj.pos * factor; obj.scale = obj.scale * [factor, factor, factor]; scaled += 1 ) catch() )
            ) )
            #(scaled, skinned)
        )

        -- Main
        fn doScale factor =
        (
            if factor == 1.0 do ( lblStatus.text = "Factor 1.0 - no change."; return true )
            if factor <= 0.0 do ( lblStatus.text = "Invalid factor."; return false )
            local d = detectRig(); local hasBiped = d[1]; local hasCat = d[2]; local stdRoots = d[3]
            if stdRoots.count == 0 and not hasBiped and not hasCat do ( messageBox (::WdxL.t "anim_no_objects_in_scene_msg") title:(::WdxL.t "anim_error_ptitle"); return false )
            if hasBiped do ( if not (queryBox "Biped detected!\nBiped + Skin cannot be auto-scaled.\nStandard objects will still be scaled.\nContinue?" title:(::WdxL.t "anim_biped_warning_ptitle") beep:false) do return false )
            if hasCat do ( if not (queryBox "CAT rig detected!\nCAT + Skin cannot be auto-scaled.\nStandard objects will still be scaled.\nContinue?" title:(::WdxL.t "anim_cat_warning_ptitle") beep:false) do return false )
            local autoKeyOn = animButtonState
            undo label:("Scale " + (factor as string) + "x") on
            (
                local frozenObjs = #()
                for obj in objects do if obj.isFrozen do ( append frozenObjs obj; obj.isFrozen = false )
                local result = if autoKeyOn then (scaleRootsAtFrame stdRoots factor) else (scaleRootsAllKeys stdRoots factor)
                local scaled = result[1]; local skinned = result[2]; local wc3n = scaleWc3 factor
                for obj in frozenObjs do try(obj.isFrozen = true)catch()
                local parts = #(); append parts ((scaled as string) + " scaled")
                if skinned > 0 do append parts ((skinned as string) + " Skin(s)")
                if wc3n > 0 do append parts ((wc3n as string) + " WC3")
                if autoKeyOn do append parts ("Key@F" + (sliderTime.frame as integer) as string)
                if hasBiped do append parts "Biped skipped"
                if hasCat do append parts "CAT skipped"
                local s2 = ""
                for i = 1 to parts.count do ( s2 += parts[i]; if i < parts.count do s2 += " | " )
                lblStatus.text = ((((factor * 1000.0) as integer / 1000.0) as string) + "x | " + s2)
            )
            true
        )

        -- Events
        on ddPreset selected idx do
        (
            local name = ddPreset.items[idx]; local targetH = getPresetHeight name
            if targetH > 0 then
            ( local currentH = measureMeshHeight()
                if currentH > 0.01 then
                ( local factorStr = ((((targetH / currentH) * 1000.0) as integer / 1000.0) as string)
                    lblStatus.text = (((currentH * 10.0) as integer / 10.0) as string) + " -> " + (((targetH * 10.0) as integer / 10.0) as string) + " = " + factorStr + "x" )
                else lblStatus.text = "No meshes found!"
            ) else lblStatus.text = ""
        )
        on btnPreset pressed do
        (
            local name = ddPreset.items[ddPreset.selection]; local targetH = getPresetHeight name
            if targetH <= 0 do ( messageBox (::WdxL.t "anim_select_a_preset_not_a_header_msg") title:(::WdxL.t "anim_error_ptitle_2"); return false )
            local currentH = measureMeshHeight()
            if currentH < 0.01 do ( messageBox (::WdxL.t "anim_no_meshes_found_in_scene_msg") title:(::WdxL.t "anim_error_ptitle_3"); return false )
            doScale (targetH / currentH)
        )
        on btnApply pressed do ( doScale spnScale.value )

		-- Pop Out / Dock
		on popOutBtn pressed do
		(
			if ::WdxAnimTools.isScalerPoppedOut then
			(
				::WdxAnimTools.isScalerPoppedOut = false
				try ( destroyDialog scalerRoll ) catch()
				
				if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
				(
					local rollouts = ::whiteoutdexManagerFloater.rollouts
					local idx = ::WdxAnimTools.savedScalerIndex
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
						addRollout scalerRoll ::whiteoutdexManagerFloater
						for i = movedRollouts.count to 1 by -1 do
							addRollout movedRollouts[i] ::whiteoutdexManagerFloater
					)
					else
						addRollout scalerRoll ::whiteoutdexManagerFloater
				)
				return()
			)
			
			if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
			(
				local rollouts = ::whiteoutdexManagerFloater.rollouts
				for i = 1 to rollouts.count do
					if rollouts[i] == scalerRoll then ( ::WdxAnimTools.savedScalerIndex = i; exit )
				removeRollout scalerRoll ::whiteoutdexManagerFloater
			)
			
			::WdxAnimTools.isScalerPoppedOut = true
			createDialog scalerRoll width:440 height:172 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox, #style_maximizebox, #style_resizing)
		)
		
		on scalerRoll close do
		(
			if ::WdxAnimTools.isScalerPoppedOut then
			(
				::WdxAnimTools.isScalerPoppedOut = false
				local tmr = dotNetObject "System.Windows.Forms.Timer"
				tmr.Interval = 1
				dotNet.addEventHandler tmr "Tick" (fn _dockTmr s e =
				(
					s.Stop(); s.Dispose()
					try
					(
						if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
						(
							local rl = ::WdxAnimTools.scalerRoll
							local idx = ::WdxAnimTools.savedScalerIndex
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
		on scalerRoll open do
		(
			-- Toolkit icon on the dialog frame (WhiteoutDexUtility.ms).
			try (WdxSetDialogIcon scalerRoll) catch()
			if ::WdxL != undefined then
			(
				scalerRoll.title = ::WdxL.t "anim_model_scaler_title"
				spnScale.caption = ::WdxL.t "anim_factor_spn"
				btnPreset.text = ::WdxL.t "anim_scale_btn"
				btnApply.text = ::WdxL.t "anim_apply_btn"
			)
		)
    ),
    
    on create do
    (
    )
)
global WdxAnimTools = WdxAnimTools()
