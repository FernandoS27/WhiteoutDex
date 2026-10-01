struct WdxSkinChanger
(
    isPoppedOut = false,
    savedIndex = undefined,
    toolName = "Change Skin Data to WC3",

    mainRollout = rollout mainRollout "Change Skin Data to WC3" 
    (
        local self -- This variable will point at the SkinChanger struct definition

        -- =============================================
        -- WC3 Skin Conversion
        -- =============================================
        button changeSkinBtn "Change Skin data to WC3" width:412 height:30 align:#center tooltip:("Processes selected skinned objects for WC3 compatibility:\n" + \
                                                                                                    "- Limits to max 5 bone influences per vertex (default setting).\n" + \
                                                                                                    "- Applies standard weight distributions (e.g., 1=100%, ..., 5=20% each).\n" + \
                                                                                                    "- Prioritizes strongest bones if >5 influences.\n" + \
                                                                                                    "- Ignores invalid/deleted bone references. (Undoable)")
        on changeSkinBtn pressed do
        (
            if self != undefined then
            (
                self.changeSkinData() 
            )
            else
            (
                ::WdxErrorReporter.logError "SkinChanger" "init" "SkinChanger definition not found in rollout. Tool not properly initialized."
                messageBox (::WdxL.t "skin_cannot_call_changeskindata_sc_msg")
            )
        )
        
        -- =============================================
        -- Remove Zero Weights
        -- =============================================
        group "Remove Zero Weights"
        (
            spinner spnThreshold "Remove below:" range:[0.0, 1.0, 0.0] type:#float scale:0.001 align:#center fieldWidth:60 \
                tooltip:"Weights at or below this value will be removed.\n0.0 = only exact zero weights.\nHigher = more aggressive cleanup."
            button removeZeroBtn "Remove Zero Weights" width:412 height:30 align:#center \
                tooltip:("Removes bone influences with zero or near-zero weights\n" + \
                         "from all selected skinned meshes.\n" + \
                         "Remaining weights are renormalized to sum to 1.0.\n" + \
                         "(Undoable)")
        )
        
        -- =============================================
        -- Combine Mesh + Skin
        -- =============================================
        group "Combine Mesh + Skin"
        (
            button combineBtn "Combine Mesh + Skin" width:412 height:30 align:#center \
                tooltip:("Combines the selected skinned meshes that use the same material\n" + \
                         "(same texture, values and animation) into one mesh with one Skin.\n" + \
                         "Bones, weights and bind pose are kept, so it deforms exactly like before.\n" + \
                         "(Undoable)")
        )

        on combineBtn pressed do
        (
            if self != undefined then self.combineMeshSkin()
            else messageBox (::WdxL.t "skin_cannot_call_changeskindata_sc_msg")
        )

        on removeZeroBtn pressed do
        (
            if self != undefined then
            (
                self.removeZeroWeights threshold:spnThreshold.value
            )
            else
            (
                ::WdxErrorReporter.logError "SkinChanger" "init" "SkinChanger definition not found in rollout. Tool not properly initialized."
                messageBox (::WdxL.t "skin_cannot_call_removezeroweights_msg")
            )
        )
        
        -- Pop Out / Dock
        on popOutBtn pressed do
        (
            if ::WdxSkinChanger.isPoppedOut then
            (
                ::WdxSkinChanger.isPoppedOut = false
                try ( destroyDialog mainRollout ) catch()
                
                if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
                (
                    local rollouts = ::whiteoutdexManagerFloater.rollouts
                    local idx = ::WdxSkinChanger.savedIndex
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
                    if rollouts[i] == mainRollout then ( ::WdxSkinChanger.savedIndex = i; exit )
                removeRollout mainRollout ::whiteoutdexManagerFloater
            )
            
            ::WdxSkinChanger.isPoppedOut = true
            createDialog mainRollout width:440 height:245 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox, #style_maximizebox, #style_resizing)
        )
        
        on mainRollout close do
        (
            if ::WdxSkinChanger.isPoppedOut then
            (
                ::WdxSkinChanger.isPoppedOut = false
                local tmr = dotNetObject "System.Windows.Forms.Timer"
                tmr.Interval = 1
                dotNet.addEventHandler tmr "Tick" (fn _dockTmr s e =
                (
                    s.Stop(); s.Dispose()
                    try
                    (
                        if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
                        (
                            local rl = ::WdxSkinChanger.mainRollout
                            local idx = ::WdxSkinChanger.savedIndex
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
        
        on mainRollout open do
        (
            -- Toolkit icon on the dialog frame (WhiteoutDexUtility.ms).
            try (WdxSetDialogIcon mainRollout) catch()
            self = WdxSkinChanger -- Setzt 'self' auf die SkinChanger Struct-Definition
            format "SkinChanger: Rollout opened. 'self' now refers to SkinChanger struct definition.\n"
            -- Localization
            if ::WdxL != undefined then
            (
                mainRollout.title = ::WdxL.t "skin_change_skin_data_to_wc3_title"
                changeSkinBtn.text = ::WdxL.t "skin_change_skin_data_to_wc3_btn"
                removeZeroBtn.text = ::WdxL.t "skin_remove_zero_weights_btn"
                combineBtn.text = ::WdxL.t "skin_combine_btn"
                try (combineBtn.tooltip = ::WdxL.t "skin_combine_tip") catch ()
                try (spnThreshold.tooltip = ::WdxL.t "skin_weights_at_or_below_this_value_tip") catch ()
            )
        )
    ), 
    
    -- =================================================================
    -- FUNCTION: changeSkinData
    -- Converts skin weights to WC3 compatible format (max 5 influences)
    -- =================================================================
    -- The bone nodes of a Skin, index = bone ID. skinOps.GetBoneNode does not
    -- exist in Max 2016 (its skinOps has GetBoneName only): there the Skin's
    -- node references are in bone order; a name lookup is the last resort.
    fn skinBoneNodes sk =
    (
        local nb = skinOps.GetNumberBones sk
        local out = try (for b = 1 to nb collect (skinOps.GetBoneNode sk b)) catch (undefined)
        if out != undefined do return out
        local refNodes = #()
        for r in (refs.dependsOn sk) where isValidNode r do appendIfUnique refNodes r
        out = for b = 1 to nb collect
        (
            local nm = skinOps.GetBoneName sk b 1
            local n = undefined
            if b <= refNodes.count and refNodes[b].name == nm do n = refNodes[b]
            if n == undefined do (local c = for r in refNodes where r.name == nm collect r; if c.count > 0 do n = c[1])
            if n == undefined do n = getNodeByName nm
            n
        )
        out
    ),

    fn changeSkinData limitToFourBones_param:true minWeightThreshold_param:0.001 =
    (
        theHold.Begin()
        local undoMessage = "Change Skin Data to WC3" 
        local overallSuccess = true
        local objectsProcessed = 0
        local verticesProcessed = 0
        local verticesWithIssues = 0

        if selection.count == 0 then
        (
            messageBox (::WdxL.t "skin_please_select_one_or_more_obje_msg") title:(::WdxL.t "skin_error_ptitle")
            theHold.Cancel()
            return false
        )
        
        local selArray = selection as array
        disableSceneRedraw()
        
        for obj in selArray do
        (
            local currentObjectSuccess = true
            try (
                if not canConvertTo obj PolyMeshObject and not canConvertTo obj Editable_Mesh then
                (
                    ::WdxErrorReporter.logWarning "SkinChanger" obj.name "Skipping: Cannot be converted to Mesh/Poly."
                    continue
                )

                local skinMods = for m in obj.modifiers where classof m == Skin collect m 
                if skinMods.count == 0 then
                (
                    ::WdxErrorReporter.logWarning "SkinChanger" obj.name "Skipping: No Skin modifier found."
                    continue
                )
                
                local skinMod = skinMods[1] 
                format "SkinChanger: Processing object: %\n" obj.name
                
                -- Activate the Skin modifier so skinOps can reach it
                max modify mode
                select obj
                modPanel.setCurrentObject skinMod
                
                local numVertices = skinOps.GetNumberVertices skinMod
                local skBones = skinBoneNodes skinMod
                if numVertices == 0 then 
                (
                    ::WdxErrorReporter.logWarning "SkinChanger" obj.name "Skipping: Skin modifier has 0 vertices."
                    continue
                )

                for v = 1 to numVertices do
                (
                    local originalBoneCount = skinOps.GetVertexWeightCount skinMod v
                    if originalBoneCount == 0 then continue 

                    local currentVertexBoneIDs = #()
                    local currentVertexWeights = #()
                    
                    for i = 1 to originalBoneCount do
                    (
                        local boneID = skinOps.GetVertexWeightBoneID skinMod v i
                        local weight = skinOps.GetVertexWeight skinMod v i
                        
                        local boneNode = if boneID >= 1 and boneID <= skBones.count then skBones[boneID] else undefined
                        
                        if boneNode == undefined or isDeleted boneNode then 
                        (
                            ::WdxErrorReporter.logWarning "SkinChanger" obj.name ("Vertex " + v as string + " (OrigIdx:" + i as string + ") has invalid Bone ID " + boneID as string + ". Skipping.")
                            continue 
                        )
                        
                        if weight >= minWeightThreshold_param then
                        (
                            append currentVertexBoneIDs boneID
                            append currentVertexWeights weight
                        )
                    )
                    
                    local filteredBoneCount = currentVertexBoneIDs.count
                    if filteredBoneCount == 0 then 
                    (
                        format "SkinChanger: Vertex % on '%' has no significant weights after thresholding or all bones were invalid/deleted. Original influences: %.\n" v obj.name originalBoneCount
                        verticesWithIssues +=1
                        continue
                    )

                    local finalBoneIDs = #()
                    local finalWeights = #()

                    if limitToFourBones_param then 
                    (
                        if filteredBoneCount == 1 then
                        (
                            finalBoneIDs = #(currentVertexBoneIDs[1])
                            finalWeights = #(1.0)
                        )
                        else if filteredBoneCount == 2 then
                        (
                            finalBoneIDs = for b_idx = 1 to 2 collect currentVertexBoneIDs[b_idx]
                            finalWeights = #(0.5, 0.5)
                        )
                        else if filteredBoneCount == 3 then
                        (
                            finalBoneIDs = for b_idx = 1 to 3 collect currentVertexBoneIDs[b_idx]
                            finalWeights = #(0.333333, 0.333333, 0.333334)
                        )
                        else if filteredBoneCount == 4 then
                        (
                            finalBoneIDs = for b_idx = 1 to 4 collect currentVertexBoneIDs[b_idx]
                            finalWeights = #(0.25, 0.25, 0.25, 0.25)
                        )
                        else 
                        (
                            local influencePairs = #()
                            for i = 1 to filteredBoneCount do append influencePairs #(currentVertexWeights[i], currentVertexBoneIDs[i])
                            
                            fn sortInfluences p1 p2 = if p1[1] > p2[1] then -1 else if p1[1] < p2[1] then 1 else 0
                            qsort influencePairs sortInfluences
                            
                            for i = 1 to 4 do 
                            (
                                if i <= influencePairs.count then append finalBoneIDs influencePairs[i][2]
                            )
                            
                            if finalBoneIDs.count == 4 then
                                finalWeights = #(0.25, 0.25, 0.25, 0.25)
                            else if finalBoneIDs.count > 0 then 
                            (
                                local baseWeight = 1.0 / finalBoneIDs.count
                                finalWeights = for i = 1 to finalBoneIDs.count collect baseWeight
                                local reSum = 0.0; for i = 1 to (finalWeights.count-1) do reSum += finalWeights[i]
                                if finalWeights.count > 0 then finalWeights[finalWeights.count] = 1.0 - reSum
                            )
                        )
                    )
                    else 
                    (
                        finalBoneIDs = deepcopy currentVertexBoneIDs
                        finalWeights = deepcopy currentVertexWeights
                        
                        if finalBoneIDs.count > 0 then
                        (
                            local currentSum = 0.0
                            for w_idx = 1 to finalWeights.count do currentSum += finalWeights[w_idx]
                            
                            if currentSum > 0.00001 and (abs (currentSum - 1.0)) > 0.00001 then 
                            (
                                for i_idx = 1 to finalWeights.count do finalWeights[i_idx] /= currentSum 
                                if finalWeights.count > 0 then
                                (
                                   local reSum = 0.0
                                   for i_idx = 1 to (finalWeights.count-1) do reSum += finalWeights[i_idx] 
                                   finalWeights[finalWeights.count] = 1.0 - reSum
                                )
                            )
                            else if currentSum <= 0.00001 and finalBoneIDs.count > 0 then 
                            (
                                local val = 1.0 / finalBoneIDs.count
                                for i_idx=1 to finalWeights.count do finalWeights[i_idx] = val 
                                if finalWeights.count > 0 then
                                (
                                   local reSum = 0.0
                                   for i_idx = 1 to (finalWeights.count-1) do reSum += finalWeights[i_idx] 
                                   finalWeights[finalWeights.count] = 1.0 - reSum
                                )
                                format "SkinChanger: Vertex % on '%' had zero sum weights, re-distributed evenly across % bones.\n" v obj.name finalBoneIDs.count
                            )
                        )
                    )

                    if finalBoneIDs.count == 0 or finalWeights.count == 0 or finalBoneIDs.count != finalWeights.count then
                    (
                        ::WdxErrorReporter.logError "SkinChanger" obj.name ("Vertex " + v as string + " has NO valid bones/weights. Original influences: " + originalBoneCount as string)
                        verticesWithIssues +=1
                        currentObjectSuccess = false
                        continue
                    )
                    
                    if finalBoneIDs.count == 1 and abs(finalWeights[1] - 1.0) > 0.001 and limitToFourBones_param then
                    (
                         format "SkinChanger: ALERT: Vertex % on '%' (1 bone). BoneID: %. Setting weight to 1.0. Calculated: %.\n" v obj.name finalBoneIDs[1] finalWeights[1]
                         finalWeights[1] = 1.0 
                    )

                    skinOps.ReplaceVertexWeights skinMod v finalBoneIDs finalWeights
                    verticesProcessed += 1
                ) 
                
                if currentObjectSuccess then objectsProcessed += 1
            ) 
            catch (
                ::WdxErrorReporter.logError "SkinChanger" obj.name ("Processing failed: " + (getCurrentException()) as string)
                overallSuccess = false 
            )
        ) 
        
        select selArray
        enableSceneRedraw()
        
        local finalMessage = (::WdxL.t "skin_adjustment_finished_msg") + "\n"
        finalMessage += (substituteString (::WdxL.t "skin_objects_processed_msg") "%1" (objectsProcessed as string)) + "\n"
        finalMessage += (substituteString (::WdxL.t "skin_vertices_processed_msg") "%1" (verticesProcessed as string)) + "\n"
        if verticesWithIssues > 0 then
            finalMessage += (substituteString (::WdxL.t "skin_vertices_with_issues_msg") "%1" (verticesWithIssues as string)) + "\n"

        if objectsProcessed > 0 and overallSuccess then
        (
            messageBox (finalMessage + (::WdxL.t "skin_operation_success_msg")) title:(::WdxL.t "skin_success_ptitle")
            theHold.Accept(undoMessage)
        )
        else if objectsProcessed == 0 and selection.count > 0 then
        (
             messageBox (finalMessage + (::WdxL.t "skin_no_suitable_objects_msg")) title:(::WdxL.t "skin_info_ptitle")
             theHold.Cancel()
        )
        else
        (
            messageBox (finalMessage + (::WdxL.t "skin_operation_errors_msg")) title:(::WdxL.t (if overallSuccess then "common_warning_ptitle" else "common_error_ptitle"))
            theHold.Cancel()
        )
            
        return overallSuccess and (objectsProcessed > 0)
    ),
    
    -- =================================================================
    -- FUNCTION: removeZeroWeights
    -- Removes bone influences with zero/near-zero weights from selected
    -- skinned meshes. Remaining weights are renormalized to sum to 1.0.
    -- =================================================================
    fn removeZeroWeights threshold:0.0 =
    (
        if selection.count == 0 then
        (
            messageBox (::WdxL.t "skin_no_objects_selectednplease_s_msg") title:(::WdxL.t "skin_remove_zero_weights_ptitle")
            return false
        )
        
        local processedCount = 0
        local skippedCount = 0
        local totalVertsProcessed = 0
        local weightsRemoved = 0
        local selArray = selection as array
        
        undo "Remove Zero Weights Batch" on
        (
            for obj in selArray do
            (
                -- Skin Modifier finden
                local skinMod = undefined
                for m in obj.modifiers where (classOf m == Skin) do
                (
                    skinMod = m
                    exit
                )
                
                if skinMod == undefined then
                (
                    ::WdxErrorReporter.logWarning "SkinChanger" obj.name "RemoveZeroWeights: Skipping, no Skin modifier found."
                    skippedCount += 1
                    continue
                )
                
                max modify mode
                select obj
                modPanel.setCurrentObject skinMod
                
                local numVerts = skinOps.getNumberVertices skinMod
                
                for v = 1 to numVerts do
                (
                    local numBones = skinOps.getVertexWeightCount skinMod v
                    local bonesToRemove = #()
                    
                    -- Bones mit Weight <= Threshold markieren
                    for b = 1 to numBones do
                    (
                        local w = skinOps.getVertexWeight skinMod v b
                        if w <= threshold do
                            append bonesToRemove b
                    )
                    
                    -- Only remove it if at least one bone would be left afterwards
                    if bonesToRemove.count > 0 and bonesToRemove.count < numBones then
                    (
                        local newBoneIDs = #()
                        local newWeights = #()
                        
                        for b = 1 to numBones do
                        (
                            if (findItem bonesToRemove b) == 0 then
                            (
                                local boneID = skinOps.getVertexWeightBoneID skinMod v b
                                local w = skinOps.getVertexWeight skinMod v b
                                append newBoneIDs boneID
                                append newWeights w
                            )
                        )
                        
                        -- Weights renormalisieren auf Summe 1.0
                        local totalWeight = 0.0
                        for w in newWeights do totalWeight += w
                        
                        if totalWeight > 0.0 then
                        (
                            for i = 1 to newWeights.count do
                                newWeights[i] = newWeights[i] / totalWeight
                        )
                        
                        skinOps.replaceVertexWeights skinMod v newBoneIDs newWeights
                        weightsRemoved += bonesToRemove.count
                    )
                )
                
                totalVertsProcessed += numVerts
                processedCount += 1
            )
        )
        
        select selArray
        
        local msg = substituteString (substituteString (substituteString \
                        (::WdxL.t "skin_zero_weights_result_msg") "%1" (processedCount as string)) \
                        "%2" (totalVertsProcessed as string)) "%3" (weightsRemoved as string)
        if skippedCount > 0 do
            msg += (substituteString (::WdxL.t "skin_zero_weights_skipped_msg") "%1" (skippedCount as string))
        
        ::WdxErrorReporter.logInfo "SkinChanger" "RemoveZeroWeights" msg
        messageBox msg title:(::WdxL.t "skin_remove_zero_weights_ptitle_2")
        
        return (processedCount > 0)
    ),
    
    -- =================================================================
    -- FUNCTION: combineMeshSkin
    -- Combines selected skinned meshes that use the same material into one
    -- mesh with one Skin that deforms exactly like the separate ones did.
    --
    -- Meshes combine when their materials are identical (same instance, or
    -- the same values, maps and animation - WdxMaterialsIdentical in
    -- WhiteoutDexMaterialMerge.ms), their visibility tracks animate alike
    -- and their user properties (geoset flags) are equal: one MDX geoset
    -- has one material, one geoset animation and one set of flags.
    --
    -- The Skin keeps its bind pose. Each part's base mesh (the Skin turned
    -- off) is placed where it was bound (Skin's mesh bind TM), the new Skin
    -- gets the same bone bind TMs and every vertex its old bones and
    -- weights. Parts bound to the same bone at different poses cannot share
    -- one Skin - such a group is left alone. The Skin must be the top of the
    -- modifier stack.
    --
    -- Returns #(combined groups, meshes merged away, skipped messages).
    -- =================================================================
    fn combineMeshSkin silent:false =
    (
        local skipped = #()
        local groupsDone = 0, removed = 0
        local created = #()   -- #(node, part names, part vertex counts) per group
        local cands = #()
        for obj in (selection as array) where isValidNode obj and superClassOf obj == GeometryClass do
        (
            local sk = undefined
            for m in obj.modifiers where classOf m == Skin do (sk = m; exit)
            if sk == undefined then (append skipped (obj.name + ": no Skin"); continue)
            if obj.modifiers[1] != sk then (append skipped (obj.name + ": Skin is not the top modifier"); continue)
            -- the mesh under the Skin becomes one fixed mesh: a Morpher or a
            -- keyed modifier below it would lose its animation
            local animBelow = undefined
            for m in obj.modifiers where m != sk and animBelow == undefined do
            (
                local keyed = classOf m == Morpher
                if not keyed do
                    for i = 1 to m.numSubs while not keyed do
                        keyed = (try ((numKeys m[i].controller) > 0) catch (false))
                if keyed do animBelow = m
            )
            if animBelow != undefined then (append skipped (obj.name + ": animated " + ((classOf animBelow) as string) + " below the Skin"); continue)
            append cands obj
        )
        fn sameGroup a b =
        (
            local ma = a.material, mb = b.material
            local sameMat = if ma == mb then true else if ma == undefined or mb == undefined then false \
                            else if ::WdxMaterialsIdentical != undefined then ::WdxMaterialsIdentical ma mb else false
            local visA = try (::WdxMatSigController a[1].controller) catch ("")
            local visB = try (::WdxMatSigController b[1].controller) catch ("")
            sameMat and visA == visB and (getUserPropBuffer a) == (getUserPropBuffer b)
        )
        local groups = #()
        local used = #{}
        for i = 1 to cands.count where not used[i] do
        (
            local g = #(cands[i])
            for j = i + 1 to cands.count where not used[j] and sameGroup cands[i] cands[j] do (append g cands[j]; used[j] = true)
            used[i] = true
            if g.count > 1 do append groups g
        )
        if groups.count == 0 do
        (
            if not silent do messageBox (::WdxL.t "skin_combine_nothing_msg") title:(::WdxL.t "skin_combine_ptitle")
            return #(0, 0, skipped, created)
        )

        max modify mode
        undo "Combine Mesh + Skin" on
        (
            for g in groups do
            (
                -- read every part: base mesh, bind TMs, bones and weights
                local parts = #()
                local ok = true
                for obj in g while ok do
                (
                    local sk = obj.modifiers[1]
                    select obj
                    modPanel.setCurrentObject sk
                    local nb = skinOps.GetNumberBones sk
                    local bones = skinBoneNodes sk
                    local nv = skinOps.GetNumberVertices sk
                    local vw = for v = 1 to nv collect
                    (
                        local n = skinOps.GetVertexWeightCount sk v
                        #(for k = 1 to n collect bones[skinOps.GetVertexWeightBoneID sk v k], \
                          for k = 1 to n collect (skinOps.GetVertexWeight sk v k))
                    )
                    local bindTMs = for b in bones collect (if isValidNode b then skinUtils.GetBoneBindTM obj b else undefined)
                    sk.enabled = false
                    local baseMesh = copy obj.mesh
                    sk.enabled = true
                    -- obj.mesh is in object space (under the pivot offset),
                    -- the Skin's mesh bind TM is the node's: move the
                    -- vertices into node space
                    local offTM = (scaleMatrix obj.objectOffsetScale) * (obj.objectOffsetRot as matrix3) * (transMatrix obj.objectOffsetPos)
                    if not (isIdentity offTM) do
                        for v = 1 to baseMesh.numverts do setVert baseMesh v ((getVert baseMesh v) * offTM)
                    if baseMesh.numverts != nv then
                    (
                        append skipped (obj.name + ": Skin vertex count differs from the mesh")
                        ok = false
                    )
                    else append parts #(obj, sk, bones, vw, bindTMs, baseMesh, skinUtils.GetMeshBindTM obj)
                )
                if not ok do continue
                -- one bind TM per bone
                local allBones = #(), allBind = #()
                for p in parts while ok do for b = 1 to p[3].count where isValidNode p[3][b] do
                (
                    local i = findItem allBones p[3][b]
                    if i == 0 then (append allBones p[3][b]; append allBind p[5][b])
                    else
                    (
                        local d = 0.0
                        for r = 1 to 4 do d = amax d (distance allBind[i][r] p[5][b][r])
                        if d > 0.001 do
                        (
                            append skipped (p[1].name + ": bone '" + p[3][b].name + "' bound at a different pose")
                            ok = false
                        )
                    )
                )
                if not ok do continue

                local first = parts[1][1]
                local firstBind = parts[1][7]
                local keepName = first.name
                local keepMat = first.material
                -- base meshes at their bind places, in the first part's space
                local nn = Editable_Mesh name:(keepName + "_combined")
                nn.transform = firstBind
                nn.mesh = parts[1][6]
                for k = 2 to parts.count do
                (
                    local tmp = Editable_Mesh()
                    tmp.transform = parts[k][7]
                    tmp.mesh = parts[k][6]
                    meshop.attach nn tmp attachMat:#neither condenseMat:false deleteSourceNode:true
                )
                nn.material = keepMat
                nn.parent = first.parent
                nn.wirecolor = first.wirecolor
                setUserPropBuffer nn (getUserPropBuffer first)
                try (if first[1].controller != undefined do nn[1].controller = copy first[1].controller) catch ()
                try (first.layer.addNode nn) catch ()

                -- the Skin: same settings, bones, bind pose and weights
                local sk0 = parts[1][2]
                local sk = Skin()
                addModifier nn sk
                for p in #(#bone_Limit, #rigid_Vertices, #rigid_Handles, #always_deform, #ref_frame) do
                    try (setProperty sk p (getProperty sk0 p)) catch ()
                select nn
                modPanel.setCurrentObject sk
                for b = 1 to allBones.count do skinOps.addBone sk allBones[b] (if b == allBones.count then 1 else 0)
                -- Max 2016: a new Skin knows the mesh's vertices only once the
                -- stack has been evaluated ("vertex count exceeded" otherwise),
                -- and that first evaluation takes the bind pose from the bones'
                -- current pose - so evaluate first, then set the bind TMs
                local total = 0
                for p in parts do total += p[4].count
                classOf nn
                if (skinOps.GetNumberVertices sk) < total do (update nn; completeRedraw(); modPanel.setCurrentObject sk; classOf nn)
                if (skinOps.GetNumberVertices sk) != total do
                (
                    append skipped (keepName + ": new Skin has " + (skinOps.GetNumberVertices sk) as string + " vertices, expected " + total as string)
                    delete nn
                    continue
                )
                for b = 1 to allBones.count do skinUtils.SetBoneBindTM nn allBones[b] allBind[b]
                skinUtils.SetMeshBindTM nn firstBind
                local idOf = skinBoneNodes sk
                local vOff = 0
                for p in parts do
                (
                    for v = 1 to p[4].count do
                    (
                        local ids = for bn in p[4][v][1] collect (findItem idOf bn)
                        if ids.count > 0 and findItem ids 0 == 0 do
                        (
                            -- Max 2016: ReplaceVertexWeights keeps the new
                            -- Skin's envelope influences - zero them first
                            local extra = #()
                            for q = 1 to (skinOps.GetVertexWeightCount sk (vOff + v)) do
                            (
                                local bid = skinOps.GetVertexWeightBoneID sk (vOff + v) q
                                if findItem ids bid == 0 do append extra bid
                            )
                            if extra.count > 0 do skinOps.SetVertexWeights sk (vOff + v) extra (for e in extra collect 0.0)
                            skinOps.ReplaceVertexWeights sk (vOff + v) ids p[4][v][2]
                        )
                    )
                    vOff += p[4].count
                )
                append created #(nn, for p in parts collect p[1].name, for p in parts collect p[4].count)
                for p in parts do delete p[1]
                nn.name = keepName
                groupsDone += 1
                removed += parts.count - 1
            )
        )
        if not silent do
        (
            local msg = substituteString (substituteString (::WdxL.t "skin_combine_result_msg") "%1" (groupsDone as string)) "%2" (removed as string)
            if skipped.count > 0 do
            (
                msg += "\n\n" + (::WdxL.t "skin_combine_skipped_msg")
                for s in skipped do msg += "\n  " + s
            )
            messageBox msg title:(::WdxL.t "skin_combine_ptitle")
        )
        #(groupsDone, removed, skipped, created)
    ),

    on create do
    (
        format "SkinChanger: Struct instance created.\n"
    )
)

global WdxSkinChanger = WdxSkinChanger()
-- UI anzeigen mit: createDialog SkinChanger.mainRollout
