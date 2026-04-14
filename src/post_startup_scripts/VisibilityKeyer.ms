struct WdxVisibilityKeyer
(
    isPoppedOut = false,
    savedIndex = undefined,
    toolName = "Visibility Keyer",
	
	visConInterTypes = #("None", "Linear", "Bezier", "Hermite (TCB)"),
	visConInterSelection = 1,
	
	visConClipboard = undefined,
	
    mainRollout = rollout mainRollout "Visibility Keyer"
    (
        local self

        checkbutton visToggleBtn "Visibility ON" pos:[8,30] enabled:false highlightColor:(color 0 64 128) width:427 height:24 align:#left tooltip:"Toggle visibility on/off for selected objects"

        groupBox visConOnSelObjGrp "Visibility Controller on Selected Objects" pos:[8,62] width:427 height:76 align:#left tooltip:"Manage visibility controllers for selected objects"
        
        -- Linke Spalte: Add und Remove
        button visConAddBtn "Add" pos:[16,82] width:133 height:22 align:#left tooltip:"Add a visibility controller to all selected objects"
        button visConRemoveBtn "Remove" pos:[16,110] width:133 height:22 align:#left tooltip:"Remove visibility controllers from all selected objects"

        -- Mittlere Spalte: Copy und Paste
        button visConCopyBtn "Copy" pos:[155,82] width:133 height:22 align:#left tooltip:"Copy visibility keyframes from the selected object"
        button visConPasteBtn "Paste" pos:[155,110] width:133 height:22 align:#left tooltip:"Paste copied visibility keyframes to the selected object"
        
        -- Rechte Spalte: Dropdown und Change Type
        dropdownList interpDropdown "" pos:[294,82] width:133 height:21 selection:1 align:#left tooltip:"Select interpolation type for visibility keys"
        button visConChangeTypeBtn "Change Type" pos:[294,110] width:133 height:22 align:#left tooltip:"Change the type of visibility controller"

        groupBox singleKeyGrp "Single Key" pos:[8,150] width:137 height:76 align:#left tooltip:"Set visibility keyframes at the current time"
        button singleKeShowBtn "Set Show Key" pos:[16,170] width:121 height:22 align:#left tooltip:"Create a visibility key at current time to show the object"
        button singleKeHideBtn "Set Hide Key" pos:[16,198] width:121 height:22 align:#left tooltip:"Create a visibility key at current time to hide the object"

        groupBox curTimeIntGrp "Current Time Interval" pos:[151,150] width:140 height:76 align:#left tooltip:"Set visibility for the entire animation range"
        button curTimeIntShowBtn "Show Interval" pos:[159,170] width:124 height:22 align:#left tooltip:"Set the object to be visible for the entire animation range"
        button curTimeIntHideBtn "Hide Interval" pos:[159,198] width:124 height:22 align:#left tooltip:"Set the object to be hidden for the entire animation range"

        groupBox selSeqGrp "Selected Sequences" pos:[297,150] width:138 height:76 align:#left tooltip:"Set visibility based on selected sequences in the Sequence Manager"
        button selSeqShowBtn "Show Sequences" pos:[305,170] width:122 height:22 align:#left tooltip:"Make objects visible during the selected sequences"
        button selSeqHideBtn "Hide Sequences" pos:[305,198] width:122 height:22 align:#left tooltip:"Make objects hidden during the selected sequences"


		-- functions
        fn selectionChanged =
		(
			visToggleBtn.checked = false
			
			if selection.count == 0 then
			(
				visToggleBtn.text = "No Object(s) selected."
				return()
			)
			
			if selection.count > 1 then
			(
				visToggleBtn.text = "More than one Object selected."
				return()
			)
			
			if getVisController selection[1] == undefined then
			(
				visToggleBtn.text = "Select Object has no Visibility Controller."
				return()
			)
			
			local visConValue = try selection[1].visibility.controller.value catch undefined
			if visConValue != undefined then
			(
				local isVisible = (visConValue >= 0.5)
				visToggleBtn.checked = isVisible
				local btnText = if isVisible then "ON (Value >= 0.5)" else "OFF (Value < 0.5)"
				visToggleBtn.text = "Visibility " + btnText
			)
		)
		
		-- Fix 2: try/catch um timeCallback, verhindert Fehler bei Playback
		fn selectionChangedtimeCB =
		(
			try ( selectionChanged() ) catch ()
		)
		
		-- Item Handlers
		on interpDropdown selected selectedId do
		(
			self.visConInterSelection = interpDropdown.selection
		)

        on visConAddBtn pressed do
        (
            undo "Add Visibility Controller" on
            (
                for obj in selection do self.addVisCon obj
            )
			selectionChanged()
        )

        on visConRemoveBtn pressed do
        (
            undo "Remove Visibility Controller" on
            (
                for obj in selection do self.removeVisCon obj
            )
			selectionChanged()
        )

        on visConCopyBtn pressed do
        (
            -- Da hier keine Szene-Änderungen vorgenommen werden, ist kein Undo-Block nötig.
            if selection.count != 1 then
            (
                messageBox (::WdxL.t "vis_please_select_exactly_one_sour_msg")
                return false
            )
            
            local src = selection[1]
            if not canConvertTo src editable_mesh then
            (
                messageBox (::WdxL.t "vis_please_select_a_mesh_msg")
                return false
            )
            
            local visCon = getVisController src
            if visCon == undefined then
            (
                messageBox (::WdxL.t "vis_no_visibility_controller_detec_msg")
                return false
            )
			
			self.visConClipboard = copy visCon
        )

        on visConPasteBtn pressed do
        (
			if self.visConClipboard == undefined then return()
			
            undo "Paste Visibility Track" on
            (
				for obj in selection do
				(
					if not canConvertTo obj editable_mesh then continue
					
					obj.visibility = copy self.visConClipboard
				)
            )
			
			selectionChanged()
        )
		
		-- Fix 3: Undo-Block hinzugefügt
        on visConChangeTypeBtn pressed do
		(
			undo "Change Visibility Controller Type" on
			(
				for obj in selection do
				(
					local visCon = getVisController obj
					if visCon != undefined then
					(
						-- Keys sichern bevor Controller getauscht wird
						local savedKeys = #()
						for key in visCon.keys do
							append savedKeys #(key.time, try(key.value) catch(1.0))
						
						obj.visibility = case interpDropdown.selected of
						(
							"None": on_off()
							"Linear": linear_float()
							"Bezier": bezier_float()
							"Hermite (TCB)": tcb_float()
						)
						
						-- Keys wiederherstellen
						local newCon = getVisController obj
						if newCon != undefined then
						(
							if classOf newCon == on_off then
							(
								-- On/Off: toggle-based key restore
								local state = true  -- starts ON
								for kd in savedKeys do
								(
									local desired = (kd[2] >= 0.5)
									if state != desired then
									(
										addNewKey newCon kd[1]
										state = not state
									)
								)
							)
							else
							(
								for kd in savedKeys do
								(
									local newKey = addNewKey newCon kd[1]
									newKey.value = kd[2]
									
									if interpDropdown.selected == "None" then
									(
										newKey.inTangentType = #step
										newKey.outTangentType = #step
									)
								)
							)
						)
					)
				)
			)
			selectionChanged()
		)
		
        on singleKeShowBtn pressed do
        (
            undo "Set Show Key" on
            (
                self.addVisKeyAtTime (selection as array) true sliderTime
            )
			selectionChanged()
        )
        on singleKeHideBtn pressed do
        (
            undo "Set Hide Key" on
            (
                self.addVisKeyAtTime (selection as array) false sliderTime
            )
			selectionChanged()
        )
        on curTimeIntShowBtn pressed do
        (
            undo "Set Show Interval" on
            (
                self.addVisKeysAtInterval (selection as array) true animationRange
            )
			selectionChanged()
        )
        on curTimeIntHideBtn pressed do
        (
            undo "Set Hide Interval" on
            (
                self.addVisKeysAtInterval (selection as array) false animationRange
            )
			selectionChanged()
        )
        on selSeqShowBtn pressed do
        (
            undo "Set Show on Selected Sequences" on
            (
                self.addVisKeysToSelectedSequences (selection as array) true
            )
			selectionChanged()
        )
        on selSeqHideBtn pressed do
        (
            undo "Set Hide on Selected Sequences" on
            (
                self.addVisKeysToSelectedSequences (selection as array) false
            )
			selectionChanged()
        )
        
		
		-- Pop Out / Dock
		on popOutBtn pressed do
		(
			if ::WdxVisibilityKeyer.isPoppedOut then
			(
				::WdxVisibilityKeyer.isPoppedOut = false
				try ( destroyDialog mainRollout ) catch()
				
				if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
				(
					local rollouts = ::whiteoutdexManagerFloater.rollouts
					local idx = ::WdxVisibilityKeyer.savedIndex
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
					if rollouts[i] == mainRollout then ( ::WdxVisibilityKeyer.savedIndex = i; exit )
				removeRollout mainRollout ::whiteoutdexManagerFloater
			)
			
			::WdxVisibilityKeyer.isPoppedOut = true
			createDialog mainRollout width:440 height:242 style:#(#style_titlebar, #style_sysmenu, #style_minimizebox, #style_resizing)
		)
		
		-- Rollout Handlers
        on mainRollout open do
        (
            self = ::WdxVisibilityKeyer
			interpDropdown.items = self.visConInterTypes
			interpDropdown.selection = self.visConInterSelection
			
			callbacks.addScript #selectionSetChanged selectionChanged id:#WhiteoutDex_VisibilityKeyer
			registerTimeCallback selectionChangedtimeCB
			selectionChanged()

			-- Localization
			if ::WdxL != undefined then
			(
				mainRollout.title = ::WdxL.t "vis_visibility_keyer_title"
				visToggleBtn.text = ::WdxL.t "vis_visibility_on_btn"
				visToggleBtn.tooltip = ::WdxL.t "vis_toggle_visibility_onoff_for_s_tip"
				visConAddBtn.text = ::WdxL.t "vis_add_btn"
				visConAddBtn.tooltip = ::WdxL.t "vis_add_a_visibility_controller_to_tip"
				visConRemoveBtn.text = ::WdxL.t "vis_remove_btn"
				visConRemoveBtn.tooltip = ::WdxL.t "vis_remove_visibility_controllers_tip"
				visConCopyBtn.text = ::WdxL.t "vis_copy_btn"
				visConCopyBtn.tooltip = ::WdxL.t "vis_copy_visibility_keyframes_from_tip"
				visConPasteBtn.text = ::WdxL.t "vis_paste_btn"
				visConPasteBtn.tooltip = ::WdxL.t "vis_paste_copied_visibility_keyfra_tip"
				interpDropdown.tooltip = ::WdxL.t "vis_select_interpolation_type_for_tip"
				visConChangeTypeBtn.text = ::WdxL.t "vis_change_type_btn"
				visConChangeTypeBtn.tooltip = ::WdxL.t "vis_change_the_type_of_visibility_tip"
				singleKeShowBtn.text = ::WdxL.t "vis_set_show_key_btn"
				singleKeShowBtn.tooltip = ::WdxL.t "vis_create_a_visibility_key_at_cur_tip"
				singleKeHideBtn.text = ::WdxL.t "vis_set_hide_key_btn"
				singleKeHideBtn.tooltip = ::WdxL.t "vis_create_a_visibility_key_at_cur_tip_2"
				curTimeIntShowBtn.text = ::WdxL.t "vis_show_interval_btn"
				curTimeIntShowBtn.tooltip = ::WdxL.t "vis_set_the_object_to_be_visible_f_tip"
				curTimeIntHideBtn.text = ::WdxL.t "vis_hide_interval_btn"
				curTimeIntHideBtn.tooltip = ::WdxL.t "vis_set_the_object_to_be_hidden_fo_tip"
				selSeqShowBtn.text = ::WdxL.t "vis_show_sequences_btn"
				selSeqShowBtn.tooltip = ::WdxL.t "vis_make_objects_visible_during_th_tip"
				selSeqHideBtn.text = ::WdxL.t "vis_hide_sequences_btn"
				selSeqHideBtn.tooltip = ::WdxL.t "vis_make_objects_hidden_during_the_tip"
			)
        )
		
		on mainRollout close do
        (
			if ::WdxVisibilityKeyer.isPoppedOut then
			(
				::WdxVisibilityKeyer.isPoppedOut = false
				local tmr = dotNetObject "System.Windows.Forms.Timer"
				tmr.Interval = 1
				dotNet.addEventHandler tmr "Tick" (fn _dockTmr s e =
				(
					s.Stop(); s.Dispose()
					try
					(
						if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
						(
							local rl = ::WdxVisibilityKeyer.mainRollout
							local idx = ::WdxVisibilityKeyer.savedIndex
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
			self.visConInterSelection = interpDropdown.selection
			callbacks.removeScripts id:#WhiteoutDex_VisibilityKeyer
			unRegisterTimeCallback selectionChangedtimeCB
        )
    ),

    -- Entfernt den Controller des SubAnims (sowohl bei ParamBlock2 als auch bei anderen Typen)
    fn removeController subAnim = if isKindOf subAnim ::SubAnim and isController subAnim.controller do
    (
        local originalValue = subAnim.value
        local REFMSG_CONTROLREF_CHANGE = 0xFA

        if isKindOf subAnim.parent ParamBlock2 then
        (
            local iGlobal = (dotNetClass "Autodesk.Max.GlobalInterface").Instance
            local pBlockNET = iGlobal.Animatable.GetAnimByHandle (getHandleByAnim subAnim.parent)
            local pValNET = pBlockNET.GetPB2Value (pBlockNET.IndexToId (subAnim.index - 1)) 0

            pValNET.Flags = bit.or pValNET.Flags 0x40 -- Parameter als nicht-animiert markieren
        )
        else
        (
            local ctrl = subAnim.controller
            local parent = subAnim.parent
            local index = for i = 1 to refs.getNumRefs parent where refs.getReference parent i == ctrl do exit with i

            refs.replaceReference parent index undefined
        )

        if classOf subAnim.value == classOf originalValue do subAnim.value = originalValue
        notifyDependents subAnim.parent msg:REFMSG_CONTROLREF_CHANGE
    ),

    mapped fn addVisCon obj =
    (
        local visCon = getVisController obj
        if visCon == undefined then
        (
			obj.visibility = case this.visConInterTypes[this.visConInterSelection] of
			(
				"None": on_off()
				"Linear": linear_float()
				"Bezier": bezier_float()
				"Hermite (TCB)": tcb_float()
			)
            visCon = getVisController obj
        )
        return visCon
    ),

    mapped fn removeVisCon obj =
    (
        local visCon = getVisController obj
        if visCon != undefined then
        (
            this.removeController obj["visibility"]
        )
    ),

    -- Einfacher linearer Scan (Original-Logik)
    fn getKeyAtTime con t =
    (
        local foundKey = undefined
        for key in con.keys do
        (
            if key.time == t then exit with foundKey = key
        )
        foundKey
    ),

    -- Checks if controller is On/Off type (toggle-based, no tangents)
    fn isOnOffMode visCon =
    (
        classOf visCon == on_off
    ),

    -- Checks if controller uses step tangents (bezier_float in "None" mode)
    fn isStepMode visCon =
    (
        local conClass = classOf visCon
        conClass == bezier_float and this.visConInterTypes[this.visConInterSelection] == "None"
    ),

    mapped fn addVisKeyAtTime obj boolVal t =
    (
        local visCon = this.addVisCon obj
        
        if this.isOnOffMode visCon then
        (
            -- On/Off controller: toggle-based
            -- State starts ON, each key toggles. Count keys before time t to determine current state.
            local state = true  -- default ON
            for key in visCon.keys where key.time < t do state = not state
            
            -- Check if there's already a key at this exact time
            local existingKey = this.getKeyAtTime visCon t
            
            if boolVal then
            (
                -- Want SHOW (ON state)
                if state then
                (
                    -- Already ON at this time, remove toggle key if exists
                    if existingKey != undefined then deleteKey visCon (findItem (for k in visCon.keys collect k.time) t)
                )
                else
                (
                    -- Currently OFF, need toggle to turn ON
                    if existingKey == undefined then addNewKey visCon t
                )
            )
            else
            (
                -- Want HIDE (OFF state)
                if not state then
                (
                    -- Already OFF, remove toggle key if exists
                    if existingKey != undefined then deleteKey visCon (findItem (for k in visCon.keys collect k.time) t)
                )
                else
                (
                    -- Currently ON, need toggle to turn OFF
                    if existingKey == undefined then addNewKey visCon t
                )
            )
        )
        else
        (
            -- Bezier/Linear/TCB: value-based
            local zeroKey = this.getKeyAtTime visCon 0f
            if zeroKey == undefined then
            (
                zeroKey = addNewKey visCon 0f
                zeroKey.value = 1
                if this.isStepMode visCon then
                (
                    zeroKey.inTangentType = #step
                    zeroKey.outTangentType = #step
                )
            )
            local currentKey = this.getKeyAtTime visCon t
            if currentKey == undefined then
            (
                currentKey = addNewKey visCon t
            )
            currentKey.value = if boolVal then 1 else 0
            if this.isStepMode visCon then
            (
                currentKey.inTangentType = #step
                currentKey.outTangentType = #step
            )
        )
    ),

    mapped fn addVisKeysAtInterval obj boolVal i =
    (
        this.addVisKeyAtTime obj boolVal i.start
        this.addVisKeyAtTime obj boolVal i.end
    ),

    mapped fn addVisKeysToSelectedSequences obj boolVal =
    (
        if ::WdxSequenceManager == undefined then
        (
            print "SequenceManager not initialized!"
            return false
        )
        
        local selectedIds = ::WdxSequenceManager.mainRollout.sequencesLbox.selection as array
        qsort selectedIds ::WdxSequenceManager.mainRollout.sortDesc
        
        local globalCount = ::WdxSequenceManager.globalSequences.count
        
        for selectedId in selectedIds do
        (
            -- Global Sequences überspringen (haben keine start/endtime)
            if selectedId <= globalCount then continue
            
            -- Offset abziehen: Listbox-Index → filteredSequences-Index
            local filteredId = selectedId - globalCount
            if filteredId < 1 or filteredId > ::WdxSequenceManager.filteredSequences.count then continue
            
            local seq = ::WdxSequenceManager.filteredSequences[filteredId]
            local seqId = findItem ::WdxSequenceManager.sequences seq
            if seqId == 0 then continue
            
            local i = interval ::WdxSequenceManager.sequences[seqId].starttime ::WdxSequenceManager.sequences[seqId].endtime
            this.addVisKeysAtInterval obj boolVal i
        )
    ),
    
    on create do
    (
    )
)
global WdxVisibilityKeyer = WdxVisibilityKeyer()
