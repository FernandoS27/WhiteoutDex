struct WhiteoutDexBoneResult
(
	head = undefined,
	chest = undefined,
	footLeft = undefined,
	footRight = undefined,
	handLeft = undefined,
	handRight = undefined,
	weapon = undefined,

	-- Prints the results
	fn printResults =
	(
		local missing = ::WdxL.t "anim_not_found_val"
		format "%\n" (::WdxL.t "anim_bones_found_hdr")
		format "Head: %\n" (if head != undefined then head.name else missing)
		format "Chest: %\n" (if chest != undefined then chest.name else missing)
		format "Foot Left: %\n" (if footLeft != undefined then footLeft.name else missing)
		format "Foot Right: %\n" (if footRight != undefined then footRight.name else missing)
		format "Hand Left: %\n" (if handLeft != undefined then handLeft.name else missing)
		format "Hand Right: %\n" (if handRight != undefined then handRight.name else missing)
		format "Weapon: %\n" (if weapon != undefined then weapon.name else missing)
		format "==========================\n"
	)
)

struct WhiteoutDexBoneFinder
(
    scanResult,
    
    -- Tests a name against several patterns
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
    -- Helper functions (struct level)
    -- =============================================
    
    -- Checks whether the keyword appears as a whole word among the name parts
    fn isWholeWord parts keyword =
    (
        for p in parts do
            if p == keyword then return true
        false
    ),
    
    -- False-positive detection: words that contain a keyword but mean something else
    fn isFalsePositive lowerName keyword =
    (
        if keyword == "head" and findString lowerName "overhead" != undefined then return true
        if keyword == "head" and findString lowerName "arrowhead" != undefined then return true
        if keyword == "hand" and findString lowerName "handle" != undefined then return true
        if keyword == "foot" and findString lowerName "footstep" != undefined then return true
        false
    ),
    
    -- Scoring: rates how well a bone fits a category
    -- Higher score = better match
    fn scoreBone boneName keywords =
    (
        local lowerName = toLower boneName
        local parts = filterString lowerName " _.-"
        local score = 0
        
        -- Keyword matching
        for kw in keywords do
        (
            -- Skip false positives right away
            if this.isFalsePositive lowerName kw then continue
            
            -- Whole word = strong match
            if this.isWholeWord parts kw then
                score += 10
            -- Substring = weak match (e.g. "Bip01Head" with no separator)
            else if findString lowerName kw != undefined then
                score += 2
        )
        
        -- No keyword found -> not a candidate
        if score == 0 then return 0
        
        -- Penalty for helper/end bones that are not real rig bones
        local penaltyWords = #("nub", "tip", "ref", "helper", "dummy", "null", "target", "goal", "ik", "pole", "twist", "roll", "effector", "finger", "thumb", "ctrl")
        for pw in penaltyWords do
        (
            if this.isWholeWord parts pw then
                score -= 5
        )

        -- isWholeWord does not catch appended suffixes: Biped names its
        -- end points "Bip001 HeadNub", "Bip001 L Toe0Nub", "Bip001 L Finger0Nub".
        -- filterString splits that into #("bip001","headnub") - "nub" is not
        -- a word of its own there, so the penalty above never fired. Result: the
        -- nub beat the real bone, the one-click tool parented attachment
        -- points to Biped end points, and those carry a
        -- BipDriven_Control whose transform.controller cannot be read.
        -- That is exactly what aborted the export.
        -- "end" is deliberately NOT in the list: a bone like "Hand Bend" would
        -- otherwise be penalized wrongly. "nub"/"tip"/"effector" are unambiguous.
        for sfx in #("nub", "tip", "effector") do
        (
            for part in parts do
            (
                local pl = part.count
                local sl = sfx.count
                if pl > sl do
                (
                    if (substring part (pl - sl + 1) sl) == sfx do
                    (
                        score -= 5
                        exit
                    )
                )
            )
        )
        
        -- Secondary chains (cloth, cape, hair...) are never a Ref's bone:
        -- "R_foot_cloth_01_bind_jnt" was taken for the right foot
        for cw in #("cloth", "cape", "cloak", "skirt", "hair", "tail", "strap", "belt", "sleeve", "tassel", "ribbon", "chain")
            where this.isWholeWord parts cw do score -= 20
        if score <= 0 then return 0

        -- Bonus for shorter names (more likely the actual bone than a helper object)
        if parts.count <= 2 then score += 3
        else if parts.count <= 3 then score += 1
        
        score
    ),
    
    -- Checks whether it is the left side
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
    
    -- Checks whether it is the right side
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
    
    -- Side of a bone: the words above, then a side letter glued to the
    -- keyword ("HandR", "RHand", "FootL" - Mixamo, CAT and many custom rigs)
    fn sideOf boneName keywords =
    (
        if this.isLeft boneName then return #left
        if this.isRight boneName then return #right
        local parts = filterString (toLower boneName) " _.-:"
        for part in parts do
            for kw in keywords do
            (
                if part == ("l" + kw) or part == (kw + "l") then return #left
                if part == ("r" + kw) or part == (kw + "r") then return #right
            )
        undefined
    ),

    -- Best left and right bone for keywords. A bone whose name gives no side
    -- goes by where it stands at frame 0: Warcraft III models face -Y in Max,
    -- so their right side is -X (Blizzard's Hand/Foot Right Ref all sit
    -- there). minScore 10 = whole words only.
    fn pickSided allBones keywords minScore:1 =
    (
        local best = #(undefined, undefined), bestScore = #(0, 0)
        local unsided = #()
        for bone in allBones do
        (
            local s = this.scoreBone bone.name keywords
            if s < minScore do continue
            local side = this.sideOf bone.name keywords
            if side == undefined then append unsided #(s, bone)
            else
            (
                local i = if side == #left then 1 else 2
                if s > bestScore[i] do (bestScore[i] = s; best[i] = bone)
            )
        )
        for u in unsided do
        (
            local x = try ((at time 0 u[2].transform.pos).x) catch (0.0)
            local i = if x < 0 then 2 else 1
            if best[i] == undefined or u[1] > bestScore[i] do (bestScore[i] = u[1]; best[i] = u[2])
        )
        best
    ),

    -- Lowest bone per side among keyword matches: the ankle end of a leg
    -- chain. Blizzard's HD rigs have no "foot" bone - the Foot Refs sit on
    -- bone_leg_right / R_leg_04_bind_jnt, the lowest leg joint.
    fn pickLowestSided allBones keywords =
    (
        local best = #(undefined, undefined), bestZ = #(1e30, 1e30)
        for bone in allBones do
        (
            if (this.scoreBone bone.name keywords) <= 0 do continue
            local p = try ((at time 0 bone.transform).pos) catch (undefined)
            if p == undefined do continue
            local side = this.sideOf bone.name keywords
            local i = if side == #left then 1 else if side == #right then 2 else (if p.x < 0 then 2 else 1)
            if p.z < bestZ[i] do (bestZ[i] = p.z; best[i] = bone)
        )
        best
    ),

    -- Extended bone scan function

    fn scanBonesWithRegex =
    (
        local result = ::WhiteoutDexBoneResult()
        local allBones = #()
        local highestSpine = undefined
        local highestSpineNum = -1
        
        -- Collect all bones AND other relevant objects
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
        -- Find HEAD (best score wins)
        -- =============================================
        local bestHeadScore = 0
        for bone in allBones do
        (
            local s = this.scoreBone bone.name #("head", "skull")
            if s > bestHeadScore then
            (
                bestHeadScore = s
                result.head = bone
            )
        )
        
        -- Blizzard HD rigs: the Head Ref sits on hd_anim_bind_jnt (below bone_head)
        for bone in allBones where findString (toLower bone.name) "hd_anim" != undefined do
            (result.head = bone; exit)

        -- =============================================
        -- Find CHEST (best score wins)
        -- =============================================
        local bestChestScore = 0
        for bone in allBones do
        (
            local s = this.scoreBone bone.name #("chest", "upperchest", "thorax", "ribcage", "ribs", "rib", "torso")
            if s > bestChestScore then
            (
                bestChestScore = s
                result.chest = bone
            )
        )
        
        -- =============================================
        -- Look for SPINE (fallback for chest)
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
        
        -- No chest bone: the spine joint at ~0.78 of the head's height
        -- (Blizzard chest/head height ratio 0.77 HD, 0.80 SD). The spine
        -- number said little: "spine_C0_0_jnt" and "spine_C0_2_jnt" both
        -- read as 0, and the pelvis spine won.
        if result.chest == undefined and highestSpine != undefined then
        (
            local headZ = try ((at time 0 result.head.transform).pos.z) catch (undefined)
            local bestD = 1e30
            for bone in allBones where findString (toLower bone.name) "spine" != undefined do
            (
                local z = try ((at time 0 bone.transform).pos.z) catch (undefined)
                if z == undefined do continue
                local d = if headZ != undefined then abs (z - 0.78 * headZ) else -z
                if d < bestD do (bestD = d; result.chest = bone)
            )
            if result.chest == undefined do result.chest = highestSpine
        )
        
        -- =============================================
        -- HANDS / FEET / WEAPON (attachment study of 2,089 Blizzard models:
        -- a Ref sits on the pivot of the wrist / ankle bone, not on fingers
        -- or toes - "Bip01 R Finger0" used to tie with "Bip01 R Hand")
        -- =============================================
        local hands = this.pickSided allBones #("hand", "wrist", "palm", "fist", "claw", "paw")
        -- No hand bone: the end of the arm chain, as Blizzard does
        -- (arm_L0_4_jnt - the deepest arm joint, not a finger)
        if hands[1] == undefined or hands[2] == undefined do
        (
            local deep = #(undefined, undefined), deepD = #(-1, -1)
            for bone in allBones where (this.scoreBone bone.name #("arm", "forearm", "lowerarm")) > 0 do
            (
                local d = 0
                local q = bone.parent
                while q != undefined do (d += 1; q = q.parent)
                local side = this.sideOf bone.name #("arm", "forearm", "lowerarm")
                local x = try ((at time 0 bone.transform).pos.x) catch (0.0)
                local i = if side == #left then 1 else if side == #right then 2 else (if x < 0 then 2 else 1)
                if d > deepD[i] do (deepD[i] = d; deep[i] = bone)
            )
            for i = 1 to 2 where hands[i] == undefined do hands[i] = deep[i]
        )
        result.handLeft = hands[1]
        result.handRight = hands[2]

        local feet = this.pickSided allBones #("foot", "ankle", "hoof", "talon")
        -- Only toes: the foot is the toe's parent (Biped "L Toe0" -> "L Foot")
        if feet[1] == undefined or feet[2] == undefined do
        (
            local toes = this.pickSided allBones #("toe", "ball")
            -- The toe's parent is the ankle only when it is near the ground:
            -- a rig without an ankle bone parents its toe to the calf, whose
            -- pivot is the knee (chernyak_archer14: 0.33 of the height;
            -- Blizzard's Foot Refs sit at 0.08) - then the toe itself.
            local headZ = try ((at time 0 result.head.transform).pos.z) catch (undefined)
            for i = 1 to 2 where feet[i] == undefined and toes[i] != undefined do
            (
                local par = toes[i].parent
                local parZ = try ((at time 0 par.transform).pos.z) catch (undefined)
                feet[i] = if isValidNode par and (headZ == undefined or (parZ != undefined and parZ < 0.2 * headZ)) then par else toes[i]
            )
        )
        if feet[1] == undefined or feet[2] == undefined do
            for bone in allBones where matchPattern bone.name pattern:"bone_leg_*" do
            (
                local side = this.sideOf bone.name #("leg")
                if side == #left and feet[1] == undefined do feet[1] = bone
                if side == #right and feet[2] == undefined do feet[2] = bone
            )
        if feet[1] == undefined or feet[2] == undefined do
        (
            local legs = this.pickLowestSided allBones #("leg", "calf", "shin")
            for i = 1 to 2 where feet[i] == undefined do feet[i] = legs[i]
        )
        result.footLeft = feet[1]
        result.footRight = feet[2]

        -- A weapon bone only by whole word: "bow" is also in "elbow"
        local bestWeaponScore = 0
        for bone in allBones do
        (
            local s = this.scoreBone bone.name #("weapon", "wep", "sword", "blade", "axe", "hammer", "mace", "staff", "spear", "lance", "bow", "gun")
            if s >= 10 and s > bestWeaponScore do (bestWeaponScore = s; result.weapon = bone)
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

		groupBox boneToolsGrpB "One Click Tools" pos:[8, 136] width:426 height:98
		button createAttachmentPointsBtn "One Click Attachment Points" pos:[16, 152] width:410 height:22 tooltip:"Automatically create Attachment Points for Bones that have known names."
		button createPortraitCamBtn "One Click Portrait Camera" pos:[16, 178] width:410 height:22 tooltip:"Creates a portrait camera framing the selected object. Without a selection the upper part of the model is used as the head."
		button createCollSphereBtn "One Click Collision Spheres" pos:[16, 204] width:202 height:22 tooltip:"Creates two collision spheres sized from the model, as Blizzard does for units."
		button createCollBoxBtn "One Click Collision Box" pos:[224, 204] width:202 height:22 tooltip:"Creates a collision box sized from the model, as Blizzard does for buildings."
		
        -- Functions
        mapped fn setContinuityToZero o time controllerFunc =
        (
            local c = controllerFunc o
            if (c != undefined) then
            (
                -- the key at this time (0 = none); built-in, works on every keyable controller
                local index = getKeyIndex c time
                if (index != 0) then c.keys[index].continuity = 0
            )
        )

        fn GetRot t o r pr hp =
        (
            at time t local v = o.rotation * r
            if hp then at time t v = (v/(o.parent.rotation * pr))
            v
        )
		
		-- Top of the visible mesh: the Overhead Ref goes there. Bones, Biped
		-- and CAT parts are GeometryClass too but no part of the model's look.
		-- Height of the Overhead Ref. Blizzard's sit a fixed step above the
		-- head bone - 0.12 of the model height (hd_anim_bind_jnt 123.8 ->
		-- 140.8, 91.5 -> 109.4, 154.6 -> 171.6) - not on the mesh top, which
		-- hair, horns or a raised weapon push up. Without a head bone: the
		-- mesh top. The model's height leaves out the portrait backdrop HD
		-- units carry ("Portrait_Background", 1200 units on a 170 unit Chen)
		-- and shadow planes; bones, Biped and CAT parts do not count either.
		fn getOverheadZ headNode =
		(
			local lo = undefined, hi = undefined
			for obj in objects where superClassOf obj == GeometryClass and not obj.isHidden \
				and classOf obj != BoneGeometry and classOf obj != Biped_Object \
				and (try (classOf obj != CATBone and classOf obj != HubObject) catch (true)) \
				and (local n = toLower obj.name; findString n "portrait" == undefined and findString n "background" == undefined and findString n "shadow" == undefined) do
			(
				try
				(
					local bb = nodeGetBoundingBox obj (matrix3 1)
					if lo == undefined or bb[1].z < lo do lo = bb[1].z
					if hi == undefined or bb[2].z > hi do hi = bb[2].z
				)
				catch ()
			)
			if hi == undefined do return 100.0
			local headZ = try ((at time 0 headNode.transform).pos.z) catch (undefined)
			if headZ == undefined then hi else headZ + 0.12 * (hi - lo)
		)

		-- Computes the model height from all visible geometry objects
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
		
		-- Blizzard's own models (attachment study, 2,089 units/buildings): a
		-- Ref sits on its bone's pivot - wrist, ankle, skull base, upper chest -
		-- with no rotation and no scale of its own; Overhead above the top of
		-- the mesh at x/y 0, Origin at 0,0,0, both without a parent. The Ref
		-- used to copy the bone's whole transform: its rotation (an Overhead
		-- turned 90 degrees) and its scale - under a rig scaled 0.01 the Refs
		-- came out huge, and the gizmo "fix" scaled them by model height / 100,
		-- which the export then wrote as a KGSC of 148.
		fn createAttachmentNode rigBone type modelTop =
		(
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
				#weapon:
				(
					obj.name = "Weapon Ref"
					obj.nameFirst = 7
				)
			)
			
			local pos = [0, 0, 0]
			if type == #overhead then pos = [0, 0, modelTop]
			else if rigBone != undefined do pos = (at time 0 rigBone.transform).pos
			-- world-aligned, world scale 1; parenting keeps that world transform
			-- at time 0: sliderTime = 0 is clamped to the start of the animation
			-- range, and a Ref aligned there turned with its bone at frame 0
			at time 0 obj.transform = transMatrix pos
			if rigBone != undefined and type != #overhead and type != #origin do
			(
				obj.parent = rigBone
				at time 0 obj.transform = transMatrix pos
			)

			local attachmentsLayer = LayerManager.getLayerFromName "Attachments"
			if attachmentsLayer == undefined then
			(
				attachmentsLayer = LayerManager.newLayerFromName "Attachments"
			)
			attachmentsLayer.addnode obj
			
			print (substituteString (substituteString (::WdxL.t "anim_attach_point_created_msg") \
					"%1" obj.name) "%2" (if obj.parent != undefined then obj.parent.name else "-"))
		)
        
        mapped fn fixSquadRotation s =
        (
            -- A target camera/light (LookAt controller) has no .rotation:
            -- skip it instead of aborting the rest of the selection
            local c = try (s.rotation.controller) catch (undefined)
            local b = c != undefined and (classOf c == tcb_rotation or classOf c == bezier_rotation)
            if (b and c.keys.count > 1) then
            (
                local faults = #()
                local keys = c.keys
                local hasParent = s.parent != undefined and (classOf s.parent != Uvwmap) and (try (s.parent.rotation; true) catch (false)) -- For UVW Gizmo / LookAt parent
                local r = (inverse s.rotation)
                local pr = (quat 0 0 0 1)
                if hasParent then pr = (inverse s.parent.rotation)
                for i = 2 to keys.count do
                (
                    local v = GetRot keys[i-1].time s r pr hasParent
                    local w = GetRot keys[i].time s r pr hasParent
                    if (::WdxUtils.CheckLock v w) then
                        append faults (interval keys[i-1].time keys[i].time)
                )
                for f in faults do addNewKey c ((f.start + f.end) * 0.5)
            )
        )
        
        -- addNewKey on a time that already has a key adds nothing, but it
        -- re-smooths that key's Bezier tangents: the in-tangent of a
        -- sequence's last key took the slope of the short gap to the next
        -- sequence and the curve overshot by hundreds of units (Footman
        -- Bone_Root in "Decay Flesh"). So only missing keys are added, and
        -- XYZ controllers get them per axis, where the keys live.
        -- (Rollout-level: a function local to createStartEndkeys could not
        -- call another one - "No outer local variable references".)
        fn seAddKeyIfMissing c t =
        (
            local has = try ((getKeyIndex c t) > 0) catch (false)
            if not has do addNewKey c t
        )
        fn seAddBoundaryKeys c =
        (
            seAddKeyIfMissing c 0f
            for i = 1 to animToolsSequences.count do
            (
                seAddKeyIfMissing c animToolsSequences[i].start
                seAddKeyIfMissing c animToolsSequences[i].end
            )
        )

        fn createStartEndkeys sel =
        (
            if animToolsSequences.count == 0 then
            (
                print (::WdxL.t "anim_no_sequences_in_scene_msg")
                return false
            )
            
            fn tmp c =
            (
                if c != undefined then
                (
                    local cls = classOf c
                    if cls == Position_XYZ or cls == Euler_XYZ or cls == ScaleXYZ then
                    (
                        for i = 1 to 3 do
                        (
                            local sc = try (c[i].controller) catch (undefined)
                            if sc != undefined do seAddBoundaryKeys sc
                        )
                    )
                    else seAddBoundaryKeys c
                )
            )
            
            for s in sel do
            (
                -- Check whether it is a Biped object
                if classOf s == Biped_Object then
                (
                    -- Biped objects have a special controller structure
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
                    -- Regular objects and cameras
                    
                    -- Position - check whether the property exists
                    try 
                    (
                        if s.pos != undefined and s.pos.controller != undefined then
                            tmp s.pos.controller
                    ) 
                    catch 
                    (
                        -- Object has no standard position property
                    )
                    
                    -- Rotation - check whether the property exists
                    try 
                    (
                        if s.rotation != undefined and s.rotation.controller != undefined then
                            tmp s.rotation.controller
                    ) 
                    catch 
                    (
                        -- Object has no rotation (e.g. a camera)
                    )
                    
                    -- Scale - check whether the property exists
                    try 
                    (
                        if s.scale != undefined and s.scale.controller != undefined then
                            tmp s.scale.controller
                    ) 
                    catch 
                    (
                        -- Object has no scale
                    )
                    
                    -- Special handling for cameras with roll
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
        
        -- Event handlers
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
			
		-- Assign layers the same way the importer does. organizeNodesIntoLayers()
		-- in mdlx_importer_plugin.cpp creates exactly these names: "Cameras" and
		-- "Collision Shapes". Any other spelling produces a second layer.
		fn putInLayer objs layerName =
		(
			try
			(
				local ly = LayerManager.getLayerFromName layerName
				if ly == undefined then ly = LayerManager.newLayerFromName layerName
				if ly != undefined then for o in objs where isValidNode o do ly.addNode o
			)
			catch ()
			ok
		)

		-- Bounding box over all visible geometry objects.
		-- "at time 0" rather than sliderTime: nodeGetBoundingBox evaluates in the
		-- current time context. If the model sits on an animation frame, we would
		-- otherwise get the size of the running animation instead of the initial pose.
		-- "at time" sets the context directly and leaves the slider alone.
		fn getModelBounds =
		(
			local lo = undefined
			local hi = undefined
			at time 0
			(
				for obj in objects where superClassOf obj == GeometryClass and not obj.isHidden do
				(
					try
					(
						local bb = nodeGetBoundingBox obj (matrix3 1)
						if lo == undefined then ( lo = bb[1] ; hi = bb[2] )
						else
						(
							lo = [ amin lo.x bb[1].x, amin lo.y bb[1].y, amin lo.z bb[1].z ]
							hi = [ amax hi.x bb[2].x, amax hi.y bb[2].y, amax hi.z bb[2].z ]
						)
					)
					catch ()
				)
			)
			if lo == undefined then #([-50,-50,0],[50,50,100]) else #(lo, hi)
		)

		on createPortraitCamBtn pressed do
		(
			-- Values derived from Blizzard's own portrait models
			-- (Footman, Knight, Arthas):
			--   FoV         45 degrees (3 of 4 models)
			--   Near/Far    8 / 1000   (identical in ALL four)
			--   View        near horizontal (-2 to -16 degrees)
			--   Direction   camera sits at +X from the target
			--   Frame width 23 to 31 units - about one head width,
			--               independent of the model height (110 to 238)
			-- So it is not the model size that drives the distance, but the
			-- size of the framed region.
			undo "Create Portrait Camera" on
			(
				local fovDeg = 45.0
				local headLo = undefined
				local headHi = undefined

				if selection.count > 0 then
				(
					-- Selection = head, likewise measured in the initial pose
					at time 0
					(
						for o in selection do
						(
							try
							(
								local bb = nodeGetBoundingBox o (matrix3 1)
								if headLo == undefined then ( headLo = bb[1] ; headHi = bb[2] )
								else
								(
									headLo = [ amin headLo.x bb[1].x, amin headLo.y bb[1].y, amin headLo.z bb[1].z ]
									headHi = [ amax headHi.x bb[2].x, amax headHi.y bb[2].y, amax headHi.z bb[2].z ]
								)
							)
							catch ()
						)
					)
				)

				if headLo == undefined then
				(
					-- Without a selection: assume the top 20 % of the model is the head
					local b = getModelBounds()
					local h = b[2].z - b[1].z
					headLo = [b[1].x, b[1].y, b[2].z - h * 0.2]
					headHi = [b[2].x, b[2].y, b[2].z]
				)

				local ctr = (headLo + headHi) / 2.0
				local w = amax (headHi.x - headLo.x) (headHi.y - headLo.y)
				local hh = headHi.z - headLo.z
				local frame = amax w hh
				if frame < 0.001 then frame = 10.0

				-- Distance chosen so the region fills the frame, plus a margin
				local dist = (frame * 0.5) / (tan (fovDeg * 0.5)) * 1.15

				-- View direction: in the MDX portraits the camera sits at +X.
				-- mdx_coord_transform::position() maps Max to MDX as
				-- (-y, x, z), so MDX +X corresponds to -Y in Max. That is also
				-- Max' front view, which is where characters usually look.
				local camPos = [ctr.x, ctr.y - dist, ctr.z]

				local cam = TargetCamera name:(uniqueName "Portrait Camera") \
				                         pos:camPos target:(TargetObject pos:ctr)
				-- In MaxScript fov is in degrees; GenCamera::GetFOV hands the
				-- exporter radians, exactly the unit of the CAMS fieldOfView.
				cam.fov = fovDeg
				-- clipManually stays off - the importer stores the distances the
				-- same way, without turning manual clipping on.
				cam.nearclip = 8
				cam.farclip = 1000
				cam.target.name = cam.name + ".Target"

				putInLayer #(cam, cam.target) "Cameras"
				select cam
				format "WhiteoutDex: Portrait camera created - target %  distance %  fov %\n" \
				       ctr (dist as integer) fovDeg
				messageBox ((::WdxL.t "anim_portrait_camera_created_msg") + "\n\n" + \
				            (::WdxL.t "anim_portrait_cam_distance_lbl") + " " + ((dist as integer) as string) + "\n" + \
				            "FoV 45, near 8, far 1000\n\n" + \
				            (::WdxL.t "anim_portrait_cam_front_hint_msg")) \
				           title:(::WdxL.t "anim_portrait_camera_ptitle") beep:false
			)
		)

		-- Collision Shapes. Dimensions derived from Blizzard's own models:
		--   Footman  spheres at 61 % / 21 % of the height, radius 0.37 / 0.37 x height
		--   Arthas   spheres at 74 % / 33 % of the height, radius 0.39 / 0.49 x height
		--   Sanctum  one box, only 17 to 33 % of the model bulk - it encloses
		--            the base, not the overhangs
		-- Both are created without a parent, on the center axis.
		fn createCollisionShapes asBox =
		(
			local b = getModelBounds()
			local lo = b[1]
			local hi = b[2]
			local h = hi.z - lo.z
			if h < 0.001 then h = 100.0
			local cx = (lo.x + hi.x) / 2.0
			local cy = (lo.y + hi.y) / 2.0
			local created = #()

			if asBox then
			(
				-- Wdx_CollisionBox has its pivot at bottom center and carries the
				-- FULL extents (wc3_collision_extractor.cpp builds min/max from
				-- them as pos +/- width/2, pos.z .. pos.z + height). The box
				-- therefore stands on lo.z, not at half the height.
				local bx = Wdx_CollisionBox name:(uniqueName "Collision Box") \
				                            pos:[cx, cy, lo.z]
				bx.width  = (hi.x - lo.x) * 0.5
				bx.length = (hi.y - lo.y) * 0.5
				bx.height = h * 0.6
				append created bx
			)
			else
			(
				local s1 = Wdx_Wc3CollisionSphere name:(uniqueName "Collision Sphere") \
				                                  pos:[cx, cy, lo.z + h * 0.65]
				s1.radius = h * 0.38
				local s2 = Wdx_Wc3CollisionSphere name:(uniqueName "Collision Sphere") \
				                                  pos:[cx, cy, lo.z + h * 0.27]
				s2.radius = h * 0.43
				append created s1
				append created s2
			)
			putInLayer created "Collision Shapes"
			select created
			format "WhiteoutDex: % collision shape(s) created, model height %\n" created.count (h as integer)
			ok
		)

		on createCollSphereBtn pressed do
		(
			undo "Create Collision Spheres" on ( createCollisionShapes false )
		)

		on createCollBoxBtn pressed do
		(
			undo "Create Collision Box" on ( createCollisionShapes true )
		)

		on createAttachmentPointsBtn pressed do
		(
			undo "Create Attachment Points" on
			(
				-- Move the slider to frame 0 so bone transforms are read from the initial pose
				local savedTime = sliderTime
				sliderTime = 0

				::WhiteoutDexBoneFinder.scan()
				local r = ::WhiteoutDexBoneFinder.scanResult
				local top = getOverheadZ r.head

				if not isValidNode $'Origin Ref' then createAttachmentNode undefined #origin top
				if not isValidNode $'Overhead Ref' then createAttachmentNode undefined #overhead top
				if not isValidNode $'Head Ref' and isValidNode r.head then createAttachmentNode r.head #head top
				if not isValidNode $'Chest Ref' and isValidNode r.chest then createAttachmentNode r.chest #chest top
				if not isValidNode $'Foot Left Ref' and isValidNode r.footLeft then createAttachmentNode r.footLeft #footLeft top
				if not isValidNode $'Foot Right Ref' and isValidNode r.footRight then createAttachmentNode r.footRight #footRight top
				if not isValidNode $'Hand Left Ref' and isValidNode r.handLeft then createAttachmentNode r.handLeft #handLeft top
				if not isValidNode $'Hand Right Ref' and isValidNode r.handRight then createAttachmentNode r.handRight #handRight top
				if not isValidNode $'Weapon Ref' and isValidNode r.weapon then createAttachmentNode r.weapon #weapon top

				-- Slider back to its original position
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
			createDialog generalRoll width:440 height:254 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox, #style_maximizebox, #style_resizing)
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
				animationGrpB.text = ::WdxL.t "anim_animation_grp"
				tcbToolsGrpB.text = ::WdxL.t "anim_tcb_tools_grp"
				smoothRotationsGrpB.text = ::WdxL.t "anim_smooth_rotations_grp"
				boneToolsGrpB.text = ::WdxL.t "anim_one_click_tools_grp"
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
				createAttachmentPointsBtn.text = ::WdxL.t "anim_one_click_attachment_points_btn"
				try (createAttachmentPointsBtn.tooltip = ::WdxL.t "anim_automatically_create_attachmen_tip") catch ()
				createPortraitCamBtn.text = ::WdxL.t "anim_one_click_portrait_camera_btn"
				try (createPortraitCamBtn.tooltip = ::WdxL.t "anim_creates_a_portrait_camera_fram_tip") catch ()
				createCollSphereBtn.text = ::WdxL.t "anim_one_click_collision_spheres_btn"
				try (createCollSphereBtn.tooltip = ::WdxL.t "anim_creates_two_collision_spheres_tip") catch ()
				createCollBoxBtn.text = ::WdxL.t "anim_one_click_collision_box_btn"
				try (createCollBoxBtn.tooltip = ::WdxL.t "anim_creates_a_collision_box_sized_tip") catch ()
			)
		)
    ),
    
    skinRoll = rollout skinRoll "Skinning"
    (
        -- UI
        
        groupBox atSkinningGrpB "AT Skinning" pos:[8,22] width:426 height:48
        button saveBtn "Save and Unlink" pos:[16,38] width:198 height:22 tooltip:"Saves parent relationships of selected objects and unlinks them from their parents\n\nSometimes you already got a bunch of meshes linked to some bones and suddenly you want to edit the bones. If you don't want to mess up the meshes, you must unlink them, do whatever you need, then relink. This tool saves the parents of the selected meshes, unlinks them and restores them when you want. Use this tool before converting bones from IK to FK, for instance."
        button restoreBtn "Restore Parents" pos:[222,38] width:198 height:22 tooltip:"Restores previously saved parent relationships for selected objects\n\nSometimes you already got a bunch of meshes linked to some bones and suddenly you want to edit the bones. If you don't want to mess up the meshes, you must unlink them, do whatever you need, then relink. This tool saves the parents of the selected meshes, unlinks them and restores them when you want. Use this tool before converting bones from IK to FK, for instance."
        
        -- Functions
        mapped fn saveParents s =
        (
            local p = s.parent
            if p != undefined then
            (
                -- The handle is the key (names are not unique);
                -- the name is kept only as a fallback for old scenes.
                setUserProp s "OriginalParentHandle" p.inode.handle
                setUserProp s "OriginalParent" p.name
            )
            s.parent = undefined
        )

        mapped fn restoreParents s =
        (
            local p = undefined
            local h = getUserProp s "OriginalParentHandle"
            if h != undefined then try ( p = maxOps.getNodeByHandle (h as integer) ) catch()
            if p == undefined then
            (
                local tmp = getUserProp s "OriginalParent"
                if tmp != undefined then p = getNodeByName tmp
            )
            if p != undefined then s.parent = p
        )
        
        -- Event handlers
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
				atSkinningGrpB.text = ::WdxL.t "anim_at_skinning_grp"
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
        
        -- Functions
        -- True if a controller or any of its sub-controllers has keys
        -- (Position_XYZ / Euler_XYZ keep theirs on X/Y/Z).
        fn controllerHasKeys c =
        (
            if c == undefined then return false
            if (try (c.keys.count) catch 0) > 0 then return true
            for j = 1 to (try (c.numSubs) catch 0) do
                if controllerHasKeys (try (c[j].controller) catch undefined) then return true
            false
        )

        -- Puts a new controller of class newCon on an object's position or
        -- rotation track.
        -- In a list controller (Position_List / Rotation_List, as Freeze
        -- Transform leaves) only its layers of layerClasses that are active or
        -- keyed: replacing the list itself threw away every layer. A keyless
        -- layer that is not active - the "Frozen" layer holding the frozen
        -- offset - stays as it is.
        -- channel is #pos or #rotation: a track passed as obj.pos would arrive
        -- as its value, not as the track.
        fn replaceTrackController obj channel newCon layerClasses =
        (
            local ctrl = if channel == #pos then obj.pos.controller else obj.rotation.controller
            local cls = classOf ctrl
            if cls == position_list or cls == rotation_list then
            (
                local active = try (ctrl.active) catch 0
                for i = 1 to ctrl.count do
                (
                    local sub = try (ctrl[i].controller) catch undefined
                    if sub != undefined and (findItem layerClasses (classOf sub)) > 0 and \
                       (i == active or controllerHasKeys sub) then
                        try ( ctrl[i].controller = newCon() ) catch()
                )
            )
            else if channel == #pos then
                obj.pos.controller = newCon()
            else
                obj.rotation.controller = newCon()
        )

        mapped fn applyControllers obj posCon rotCon scaCon =
        (
            -- Skip Biped objects only
            if classOf obj == Biped_Object then
            (
                print ("Skipping Biped object: " + obj.name + " - Biped controllers cannot be changed")
                return undefined
            )

            -- Regular objects and CAT bones - use try-catch to be safe
            try
            (
                if posCon != undefined and obj.position != undefined then
                    replaceTrackController obj #pos posCon #(Position_XYZ, bezier_position, linear_position, tcb_position)
            )
            catch
            (
                print ("Could not change position controller for: " + obj.name)
            )

            try
            (
                if rotCon != undefined and obj.rotation != undefined then
                    replaceTrackController obj #rotation rotCon #(Euler_XYZ, bezier_rotation, linear_rotation, tcb_rotation)
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
        
        -- Event handlers
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
				controllersGrpB.text = ::WdxL.t "anim_controllers_grp"
				-- `.labels` on radiobuttons resets `.state`, so each control's
				-- current choice is restored right after the swap.
				local savPos = PosRdo.state
				local savRot = RotRdo.state
				local savSca = ScaRdo.state
				PosRdo.caption = ::WdxL.t "anim_position_rdo"
				RotRdo.caption = ::WdxL.t "anim_rotation_rdo"
				ScaRdo.caption = ::WdxL.t "anim_scaling_rdo"
				PosRdo.labels = ::WdxL.tList "anim_pos_rdo_labels"
				RotRdo.labels = ::WdxL.tList "anim_rot_rdo_labels"
				ScaRdo.labels = ::WdxL.tList "anim_sca_rdo_labels"
				PosRdo.state = savPos
				RotRdo.state = savRot
				ScaRdo.state = savSca
				try (PosRdo.tooltip = ::WdxL.t "anim_choose_the_type_of_controller_tip") catch ()
				try (RotRdo.tooltip = ::WdxL.t "anim_choose_the_type_of_controller_tip_2") catch ()
				try (ScaRdo.tooltip = ::WdxL.t "anim_choose_the_type_of_controller_tip_3") catch ()
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
            if cn == "Wc3Particles1" or cn == "Wc3Particles2" or cn == "Wc3Ribbon" do return false
            if cn == "Wdx_Wc3CollisionSphere" or cn == "Wdx_CollisionBox" do return false
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
                if cn == "Wc3Ribbon" do
                ( try(setProperty obj "Height Above" ((getProperty obj "Height Above") * factor))catch(); try(setProperty obj "Height Below" ((getProperty obj "Height Below") * factor))catch(); try(obj.gravity *= factor)catch(); count += 1 )
                if cn == "Wdx_Wc3CollisionSphere" do ( try(obj.radius *= factor)catch(); count += 1 )
                if cn == "Wdx_CollisionBox" do ( try(obj.width *= factor)catch(); try(obj.length *= factor)catch(); try(obj.height *= factor)catch(); count += 1 )
                -- Distances scale with the model. The 3.0 falloff coefficients
                -- multiply d and d^2, so they scale inversely to keep the light
                -- reaching the same part of the rescaled model.
                if cn == "Wdx_Wc3Light" do
                ( try(obj.DecayStart *= factor)catch(); try(obj.DecayEnd *= factor)catch()
                  try(obj.ShadowCastingStart *= factor)catch(); try(obj.ShadowCastingEnd *= factor)catch()
                  if factor != 0 do ( try(obj.LinearFalloff /= factor)catch(); try(obj.QuadraticFalloff /= (factor * factor))catch(); try(obj.Damping /= (factor * factor))catch() )
                  count += 1 )
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
            if factor == 1.0 do ( lblStatus.text = ::WdxL.t "anim_factor_no_change_msg"; return true )
            if factor <= 0.0 do ( lblStatus.text = ::WdxL.t "anim_invalid_factor_msg"; return false )
            local d = detectRig(); local hasBiped = d[1]; local hasCat = d[2]; local stdRoots = d[3]
            if stdRoots.count == 0 and not hasBiped and not hasCat do ( messageBox (::WdxL.t "anim_no_objects_in_scene_msg") title:(::WdxL.t "anim_error_ptitle"); return false )
            if hasBiped do ( if not (queryBox (::WdxL.t "anim_biped_detected_msg") title:(::WdxL.t "anim_biped_warning_ptitle") beep:false) do return false )
            if hasCat do ( if not (queryBox (::WdxL.t "anim_cat_detected_msg") title:(::WdxL.t "anim_cat_warning_ptitle") beep:false) do return false )
            local autoKeyOn = animButtonState
            undo label:("Scale " + (factor as string) + "x") on
            (
                local frozenObjs = #()
                for obj in objects do if obj.isFrozen do ( append frozenObjs obj; obj.isFrozen = false )
                local result = if autoKeyOn then (scaleRootsAtFrame stdRoots factor) else (scaleRootsAllKeys stdRoots factor)
                local scaled = result[1]; local skinned = result[2]; local wc3n = scaleWc3 factor
                for obj in frozenObjs do try(obj.isFrozen = true)catch()
                local parts = #(); append parts ((scaled as string) + " " + (::WdxL.t "anim_status_scaled"))
                if skinned > 0 do append parts ((skinned as string) + " " + (::WdxL.t "anim_status_skins"))
                if wc3n > 0 do append parts ((wc3n as string) + " WC3")
                if autoKeyOn do append parts ("Key@F" + (sliderTime.frame as integer) as string)
                if hasBiped do append parts (::WdxL.t "anim_status_biped_skipped")
                if hasCat do append parts (::WdxL.t "anim_status_cat_skipped")
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
                else lblStatus.text = ::WdxL.t "anim_no_meshes_found_in_scene_msg"
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
				presetGrpB.text = ::WdxL.t "anim_scale_to_target_height_grp"
				manualGrpB.text = ::WdxL.t "anim_manual_factor_grp"
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
