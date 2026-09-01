struct WdxSkinChanger
(
    isPoppedOut = false,
    savedIndex = undefined,
    toolName = "Change Skin Data to WC3",

    mainRollout = rollout mainRollout "Change Skin Data to WC3" 
    (
        local self -- Diese Variable wird auf die SkinChanger Struct-Definition zeigen

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
                ::ErrorReporter.logError "SkinChanger" "init" "SkinChanger definition not found in rollout. Tool not properly initialized."
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
        
        on removeZeroBtn pressed do
        (
            if self != undefined then
            (
                self.removeZeroWeights threshold:spnThreshold.value
            )
            else
            (
                ::ErrorReporter.logError "SkinChanger" "init" "SkinChanger definition not found in rollout. Tool not properly initialized."
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
            createDialog mainRollout width:440 height:180 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox, #style_maximizebox, #style_resizing)
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
                try (spnThreshold.tooltip = ::WdxL.t "skin_weights_at_or_below_this_value_tip") catch ()
            )
        )
    ), 
    
    -- =================================================================
    -- FUNCTION: changeSkinData
    -- Converts skin weights to WC3 compatible format (max 5 influences)
    -- =================================================================
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
                    ::ErrorReporter.logWarning "SkinChanger" obj.name "Skipping: Cannot be converted to Mesh/Poly."
                    continue
                )

                local skinMods = for m in obj.modifiers where classof m == Skin collect m 
                if skinMods.count == 0 then
                (
                    ::ErrorReporter.logWarning "SkinChanger" obj.name "Skipping: No Skin modifier found."
                    continue
                )
                
                local skinMod = skinMods[1] 
                format "SkinChanger: Processing object: %\n" obj.name
                
                -- Skin Modifier aktivieren für skinOps
                max modify mode
                select obj
                modPanel.setCurrentObject skinMod
                
                local numVertices = skinOps.GetNumberVertices skinMod
                if numVertices == 0 then 
                (
                    ::ErrorReporter.logWarning "SkinChanger" obj.name "Skipping: Skin modifier has 0 vertices."
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
                        
                        local boneNode = skinOps.GetBoneNode skinMod boneID 
                        
                        if boneNode == undefined or isDeleted boneNode then 
                        (
                            ::ErrorReporter.logWarning "SkinChanger" obj.name ("Vertex " + v as string + " (OrigIdx:" + i as string + ") has invalid Bone ID " + boneID as string + ". Skipping.")
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
                        ::ErrorReporter.logError "SkinChanger" obj.name ("Vertex " + v as string + " has NO valid bones/weights. Original influences: " + originalBoneCount as string)
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
                ::ErrorReporter.logError "SkinChanger" obj.name ("Processing failed: " + (getCurrentException()) as string)
                overallSuccess = false 
            )
        ) 
        
        select selArray
        enableSceneRedraw()
        
        local finalMessage = "Skin data adjustment finished for WC3.\n"
        finalMessage += "Objects processed successfully: " + objectsProcessed as string + "\n"
        finalMessage += "Total vertices processed: " + verticesProcessed as string + "\n"
        if verticesWithIssues > 0 then
            finalMessage += "Vertices with issues (e.g., no valid bones after filtering): " + verticesWithIssues as string + "\n"
        
        if objectsProcessed > 0 and overallSuccess then
        ( 
            messageBox (finalMessage + "Operation completed successfully.") title:(::WdxL.t "skin_success_ptitle")
            theHold.Accept(undoMessage)
        ) 
        else if objectsProcessed == 0 and selection.count > 0 then
        ( 
             messageBox (finalMessage + "No suitable objects were processed.") title:(::WdxL.t "skin_info_ptitle")
             theHold.Cancel()
        ) 
        else 
        ( 
            messageBox (finalMessage + "Operation completed with errors or no objects. Check Listener.") title:(if overallSuccess then "Warning" else "Error")
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
                    ::ErrorReporter.logWarning "SkinChanger" obj.name "RemoveZeroWeights: Skipping, no Skin modifier found."
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
                    
                    -- Nur entfernen wenn danach noch mindestens 1 Bone übrig bleibt
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
        
        local msg = processedCount as string + " mesh(es), " + totalVertsProcessed as string + " verts processed, " + weightsRemoved as string + " influences removed."
        if skippedCount > 0 do
            msg += " (" + skippedCount as string + " skipped)"
        
        ::ErrorReporter.logInfo "SkinChanger" "RemoveZeroWeights" msg
        messageBox msg title:(::WdxL.t "skin_remove_zero_weights_ptitle_2")
        
        return (processedCount > 0)
    ),
    
    on create do
    (
        format "SkinChanger: Struct instance created.\n"
    )
)

global WdxSkinChanger = WdxSkinChanger()
-- UI anzeigen mit: createDialog SkinChanger.mainRollout
