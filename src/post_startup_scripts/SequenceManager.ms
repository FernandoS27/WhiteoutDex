struct WdxSequenceManager
(
    isPoppedOut = false,
    savedIndex = undefined,
    sequences = #(),
    filteredSequences = #(),
	lastListSelection = #{},
	ignoredSequences = #(),  -- Separate Liste für ignorierte Sequenzen
	globalSequences = #(),   -- Array von Global Sequence Durationen (als time-Werte)

    -- ============================================================================
    -- GLOBAL SEQUENCE FUNCTIONS
    -- ============================================================================

    -- Scannt alle Objekte in der Szene nach Controllern mit ORT #cycle
    -- und sammelt deren Durationen (letzter Keyframe-Zeitpunkt) als Global Sequences.
    -- Erfasst: Transform-Controller, Visibility-Controller, Material Alpha,
    --          UV Offset/Rotation/Scale Controller, sowie IFL-Texturanimationen.
    
    -- Prüft einen einzelnen Controller auf ORT #cycle und sammelt die Duration
    fn checkControllerForGlobalSeq ctrl &durations =
    (
        try (
            if ctrl != undefined and \
               isProperty ctrl #keys and ctrl.keys != undefined and ctrl.keys.count > 0 and \
               getAfterORT ctrl == #cycle then
            (
                local lastKeyTime = ctrl.keys[ctrl.keys.count].time
                if lastKeyTime > 0f then
                    appendIfUnique durations lastKeyTime
            )
        ) catch()
    ),
    
    -- Prüft ob eine BitmapTexture eine IFL-Datei ist und berechnet deren Global Sequence Duration.
    -- Gleiche Formel wie Wc3Animation.GetTextureAnimation:
    --   n = round(1/playback), Duration = n * fileCount
    -- Gleiche Dateizählung wie SceneParser.GetImageArray (enumerateFiles).
    fn checkIFLForGlobalSeq tex &durations =
    (
        try (
            if tex == undefined or classOf tex != BitmapTexture then return()
            
            -- tex.fileName liefert bei IFL-Texturen den Pfad zur .ifl Datei
            -- (tex.bitmap.fileName wäre der aktuelle Frame und kann undefined sein)
            local texPath = tex.fileName
            if texPath == undefined or texPath.count < 5 then return()
            
            -- Prüfe auf .ifl Extension
            if stricmp (getFilenameType texPath) ".ifl" != 0 then return()
            
            -- Zähle referenzierte Bilddateien (identisch mit SceneParser.GetImageArray)
            fn _collectName name arr = append arr name
            local files = #()
            enumerateFiles tex _collectName files
            local iflIdx = findItem files texPath
            if iflIdx != 0 then deleteItem files iflIdx
            
            local fileCount = files.count
            if fileCount < 2 then return()
            
            -- Duration berechnen (identisch mit Wc3Animation.GetTextureAnimation)
            local playback = tex.playbackrate
            if playback == undefined or playback == 0 then playback = 1.0
            local n = (::WdxUtils.round (1.0 / playback)) as integer
            if n == 0 then n = 1
            local duration = (n * fileCount) as time
            if duration > 0f then
                appendIfUnique durations duration
        ) catch()
    ),
    
    fn scanGlobalSequences =
    (
        local durations = #()
        local scannedMaterials = #()
        
        for obj in objects do
        (
            -- ============================================================
            -- 1) Object-Level Controller (Transform + Visibility)
            -- ============================================================
            try (this.checkControllerForGlobalSeq obj.pos.controller &durations) catch()
            try (this.checkControllerForGlobalSeq obj.rotation.controller &durations) catch()
            try (this.checkControllerForGlobalSeq obj.scale.controller &durations) catch()
            try (this.checkControllerForGlobalSeq obj.visibility.controller &durations) catch()
            
            -- ============================================================
            -- 2) Material-Level Controller (Alpha, UV, IFL Textures)
            -- ============================================================
            local mat = undefined
            try (mat = obj.material) catch()
            
            if mat != undefined and (findItem scannedMaterials mat) == 0 then
            (
                append scannedMaterials mat
                
                -- Material Alpha
                try (this.checkControllerForGlobalSeq mat.alpha.controller &durations) catch()
                
                -- Texture Coordinate Controller (UV Scroll, Rotation, Scale)
                try (
                    if mat.texture != undefined and classOf mat.texture == BitmapTexture then
                    (
                        local coords = mat.texture.coords
                        this.checkControllerForGlobalSeq coords.U_offset.controller &durations
                        this.checkControllerForGlobalSeq coords.V_offset.controller &durations
                        this.checkControllerForGlobalSeq coords.W_angle.controller &durations
                        this.checkControllerForGlobalSeq coords.U_tiling.controller &durations
                        this.checkControllerForGlobalSeq coords.V_tiling.controller &durations
                    )
                ) catch()
                
                -- IFL Texture Animation (erzeugt immer eine Global Sequence)
                try (this.checkIFLForGlobalSeq mat.texture &durations) catch()
            )
        )
        
        sort durations
        this.globalSequences = durations
    ),

    -- ============================================================================
    -- IGNORED SEQUENCE FUNCTIONS
    -- ============================================================================

    -- Funktion zum Prüfen ob eine Sequenz ignoriert ist
    fn isSequenceIgnored sequenceName =
    (
        (findItem this.ignoredSequences sequenceName) != 0
    ),

    -- Funktion zum Hinzufügen/Entfernen von ignorierten Sequenzen
    fn setSequenceIgnored sequenceName ignored =
    (
        local index = findItem this.ignoredSequences sequenceName
        if ignored then
        (
            if index == 0 then append this.ignoredSequences sequenceName
        )
        else
        (
            if index != 0 then deleteItem this.ignoredSequences index
        )
    ),

    -- Ignore-Liste in User Properties speichern
    fn saveIgnoredSequences =
    (
        local ignoreStr = ""
        for i = 1 to this.ignoredSequences.count do
        (
            ignoreStr += this.ignoredSequences[i]
            if i < this.ignoredSequences.count then ignoreStr += ";"
        )
        try ( setUserProp rootNode "WhiteoutDex_IgnoredSequences" ignoreStr ) catch()
    ),

    -- Ignore-Liste aus User Properties laden
    fn loadIgnoredSequences =
    (
        local ignoreStr = try ( getUserProp rootNode "WhiteoutDex_IgnoredSequences" ) catch ( undefined )
        this.ignoredSequences = #()
        if ignoreStr != undefined and ignoreStr != "" then
        (
            this.ignoredSequences = filterString ignoreStr ";"
        )
    ),

    -- Funktion für den Exporter: Gefilterte Sequenzen zurückgeben
    fn getExportSequences =
    (
        local exportSequences = #()
        for seq in this.sequences do
        (
            if not this.isSequenceIgnored seq.name then
                append exportSequences seq
        )
        return exportSequences
    ),

	fn bumpTime t delta = t + delta,
		
    -- Funktionen zum Arbeiten mit Sequenzen
    fn getNoteKeyName key =
    (
        (filterString key.value "\"\n\t\x0D")[1]
    ),
    
    fn renameNoteKey &key newName =
    (
        local keyName = this.getNoteKeyName key
        local oldVal = key.value
        key.value = substituteString oldVal keyName newName
    ),
    
    fn GetSequenceData =
    (
        if not HasNoteTracks Rootnode then return #()
        
        local result = #()
        local sequenceNames = #()
        
        local nt = GetNoteTrack Rootnode 1
        local keys = nt.keys
        for k in keys do
        (
			local keyName = this.getNoteKeyName k
            appendIfUnique sequenceNames keyName
            local notFound = true
            for i = 1 to result.count where notFound do
            (
                local val1 = toLower (trimRight (trimLeft result[i][1].value))
                if val1 == "" then continue
                local val2 = toLower (trimRight (trimLeft k.value))
				if val2 == "" then continue
                if val1 == val2 then
                (
                    append result[i] k
                    notFound = false
                )
            )
            if notFound then append result #(k)
        )
        
        -- Prüfe doppelte Sequenznamen und lösche Keys ohne passendes zweites Key
        -- Rückwärts iterieren: deleteItem verschiebt Indices, vorwärts überspringt Elemente
        local duplicateSequences = #()
        for id = result.count to 1 by -1 do
        (
            local keys = result[id]
            if keys.count == 4 then
            (
                result[id] = #(keys[1], keys[2])
                
                local nameCounter = 0
                local newSequenceName = oldSequenceName = this.getNoteKeyName keys[1]
                
                do
                (
                    nameCounter += 1
                    local newSequenceName = oldSequenceName + "_" + formattedPrint nameCounter format:"03d"
                )
                while findItem sequenceNames newSequenceName != 0
                
                this.renameNoteKey keys[3] newSequenceName
                this.renameNoteKey keys[4] newSequenceName
                append duplicateSequences #(keys[3], keys[4])
            )
            else if keys.count != 2 then
            (
                deleteItem result id
            )
        )
        join result duplicateSequences
        
        -- Sortieren nach Startzeit (ersten Key)
        fn compareFN v1 v2 =
        (
            local d = v1[1].time.frame as integer - v2[1].time.frame as integer
            case of
            (
                (d < 0): -1
                (d > 0): 1
                default: 0
            )
        )
        qsort result compareFN
        
        result -- Rückgabe
    ),

    fn readSequenceData key key2 =
    (
        local data = filterString key.value "\"\n\t\x0D"
        local animName = data[1]
        local rarity, movespeed
		local noLoop = false
        for i = 2 to data.count do 
        (
            local c = data[i]
            if stricmp c "Nonlooping" == 0 then NoLoop = true
            if stricmp (substring c 1 6) "Rarity" == 0 then rarity = ((substring c 7 -1) as integer)
            if stricmp (substring c 1 9) "Movespeed" == 0 then movespeed = ((substring c 10 -1) as integer)
        )
        Wc3Sequence \ --animName key.time key2.time NoLoop Rarity Movespeed undefined undefined
			name:animName \ 
			startTime:key.time \
			endTime:key2.time \
			loop:NoLoop \
			rarity:Rarity \ 
			movespeed:Movespeed \
			extents:undefined \
			ex:undefined
    ),
    
    fn cleanEmptyKeysFromSequences =
	(
		if not HasNoteTracks Rootnode then return ok

		local nt = GetNoteTrack Rootnode 1

		-- Schritt 1: Sammle alle Values und zähle ihr Vorkommen
		local valueCount = #()  -- Array von #(value, count) Paaren

		for k in nt.keys do
		(
			local found = false
			for vc in valueCount where vc[1] == k.value do
			(
				vc[2] += 1
				found = true
				exit
			)
			if not found then
				append valueCount #(k.value, 1)
		)

		-- Schritt 2: Erstelle Liste der zu löschenden Keys
		local idsToDelete = #()

		for id = 1 to nt.keys.count do
		(
			local k = nt.keys[id]

			if k.value == "" then
				append idsToDelete id
			else
			(
				for vc in valueCount where vc[1] == k.value and vc[2] != 2 do
				(
					append idsToDelete id
					exit
				)
			)
		)

		-- Schritt 3: Lösche die Keys (rückwärts, um Index-Probleme zu vermeiden)
		for counter = idsToDelete.count to 1 by -1 do
		(
			deleteNoteKey nt.keys idsToDelete[counter]
		)

		if idsToDelete.count > 0 then
		(
			print ("Cleaned " + idsToDelete.count as string + " invalid note keys")
		)
	),
	
    fn LoadSequences =
    (
        -- Primary: Read from WhiteoutDex Custom Attributes
        if ::SequenceStorage != undefined and ::SequenceStorage.hasCA() then
        (
            format "** WDX LoadSeq: WhiteoutDex CA found on rootNode\n"
            try
            (
                local names = rootNode.seqNames
                if names != undefined and names.count > 0 then
                (
                    format "** WDX LoadSeq: Found % sequences in CA\n" names.count
                    local result = #()
                    for i = 1 to names.count do
                    (
                        local startF = rootNode.startFrames[i]
                        local endF   = rootNode.endFrames[i]
                        local noLoop = try (rootNode.nonLooping[i]) catch (false)
                        local rar    = try (local r = rootNode.rarity[i]; if r == 0.0 then undefined else r as integer) catch (undefined)
                        local mspd   = try (local m = rootNode.moveSpeed[i]; if m == 0.0 then undefined else m as integer) catch (undefined)
                        local sg     = try (local v = rootNode.sharedGroup[i]; if v == "" then undefined else v) catch (undefined)
                        local ext = undefined
                        try
                        (
                            local extStr = rootNode.seqExtents[i]
                            if extStr != undefined and extStr != "" then
                            (
                                local parts = filterString extStr " "
                                if parts.count == 7 then
                                (
                                    ext = Wc3Extent \
                                        minExtent:[(parts[2] as float), (parts[3] as float), (parts[4] as float)] \
                                        maxExtent:[(parts[5] as float), (parts[6] as float), (parts[7] as float)] \
                                        bound:(parts[1] as float)
                                )
                            )
                        ) catch ()
                        append result (Wc3Sequence \
                            name:(names[i]) \
                            startTime:(startF as time) \
                            endTime:(endF as time) \
                            loop:noLoop \
                            rarity:rar \
                            movespeed:mspd \
                            extents:ext \
                            ex:undefined \
                            sharedGroup:sg)
                    )
                    return result
                )
            ) catch (format "** WDX LoadSequences ERROR: %\n" (getCurrentException()))
        )

        
        -- Fallback: Read from NoteTracks (legacy scenes)
        format "** WDX LoadSeq: Trying NoteTracks fallback (hasNT: %)\n" (HasNoteTracks Rootnode)
        this.cleanEmptyKeysFromSequences()
        ::WdxUtils.ArrayMap (GetSequenceData ()) (fn tmp a = readSequenceData a[1] a[2])
    ),
    
    fn playSequence i =
    (
        if this.sequences != undefined then
            if i > 0 and i <= this.sequences.count then
            (
                local startTime = this.sequences[i].startTime
                local endTime = this.sequences[i].endTime
                if not startTime < endTime then return()
                animationRange = interval startTime endTime
                max time start
            )
    ),
    
    fn saveSequences  =
    (
        -- Remove legacy NoteTracks if present
        while (HasNoteTracks Rootnode) do deleteNoteTrack Rootnode (getNoteTrack Rootnode 1)
        
        -- Save to Custom Attributes (primary storage)
        try
        (
            ::SequenceStorage.removeCA()
            ::SequenceStorage.ensureCA()
            for seq in this.sequences do
            (
                local startF = if classOf seq.startTime == Time then (seq.startTime.frame as integer) else (seq.startTime as integer)
                local endF   = if classOf seq.endTime == Time then (seq.endTime.frame as integer) else (seq.endTime as integer)
                append rootNode.seqNames     seq.name
                append rootNode.startFrames  startF
                append rootNode.endFrames    endF
                append rootNode.nonLooping   seq.loop
                append rootNode.rarity       (if seq.rarity != undefined then seq.rarity as float else 0.0)
                append rootNode.moveSpeed    (if seq.movespeed != undefined then seq.movespeed as float else 0.0)
                local extStr = ""
                if seq.extents != undefined then
                    extStr = (seq.extents.bound as string) + " " + (seq.extents.minExtent.x as string) + " " + (seq.extents.minExtent.y as string) + " " + (seq.extents.minExtent.z as string) + " " + (seq.extents.maxExtent.x as string) + " " + (seq.extents.maxExtent.y as string) + " " + (seq.extents.maxExtent.z as string)
                append rootNode.seqExtents extStr
                -- Reforged v1200: Shared Group Info speichern
                append rootNode.sharedGroup (if seq.sharedGroup != undefined then seq.sharedGroup else "")
            )
        ) catch (format "** WDX saveSequences ERROR: %\n" (getCurrentException()))
    ),
	
	fn getNewSequenceName newSequenceName =
	(
		local originalName = newSequenceName
		local sequenceNames = for seq in this.sequences collect toLower seq.name
			
		local c = 1
		while findItem sequenceNames (toLower newSequenceName) != 0 do
		(
			newSequenceName = originalName + c as string
			c += 1
		)
		
		return newSequenceName
	),
    
    fn addSequence sequenceName starttime endtime noLoop rarity movespeed =
    (
        local newSequence = Wc3Sequence sequenceName starttime endtime noLoop rarity movespeed undefined undefined
        append this.sequences newSequence
    ),
    
    fn areSequencesOverlapping seq1 seq2 =
    (
        if seq2.starttime < seq1.starttime and seq2.endtime < seq1.starttime \
            or seq2.starttime > seq1.endtime and seq2.endtime > seq1.endtime \
        then false else true
    ),
    
    fn checkSequenceOverlapping =
    (
        local overlappingSequences = #()
        for id1 = 1 to this.sequences.count do
        (
            local seq1 = this.sequences[id1]
            for id2 = id1 + 1 to this.sequences.count do
            (
                local seq2 = this.sequences[id2]
                if areSequencesOverlapping seq1 seq2 then
                    append overlappingSequences #(seq1, seq2)
            )
        )
        
        if overlappingSequences.count == 0 then return()
        
        local overlappingStr = ""
        for id = 1 to overlappingSequences.count do
        (
            local o = overlappingSequences[id]
            overlappingStr += "'" + o[1].name + "' overlaps with: '" + o[2].name + "'"
            if id < overlappingSequences.count then overlappingStr += "\n"
        )             
        print overlappingStr
    ),
    
    fn sceneChangeCallback =
    (
        print "SequenceManager updated"
        ::WdxSequenceManager.sequences = ::WdxSequenceManager.LoadSequences()
		::WdxSequenceManager.scanGlobalSequences()
        ::WdxSequenceManager.saveSequences()
        ::WdxSequenceManager.mainRollout.setListItems()
        ::WdxSequenceManager.checkSequenceOverlapping()
    ),
	
    fn listUpdateAfterUndoRedo =
    (
        local entryName = callbacks.notificationParam()
        
        -- Only process undo/redo if it's related to SequenceManager
        if matchPattern entryName pattern:"SequenceManager*" then
        (
            print entryName
            
            -- Speichere die aktuelle Selektion vor dem Neuladen
            local currentSelection = this.mainRollout.sequencesLbox.selection
            
            -- Lade Sequenzen neu
            this.sequences = this.LoadSequences()
			this.scanGlobalSequences()
            this.saveSequences()
            this.checkSequenceOverlapping()
            
            -- Aktualisiere die gefilterte Liste
            this.mainRollout.setListItems()
            
            -- Spezielle Behandlung für verschiedene Undo-Typen
            case entryName of
            (
                "SequenceManager Select All": (
                    this.mainRollout.sequencesLbox.selection = this.lastListSelection
                )
                "SequenceManager Select None": (
                    this.mainRollout.sequencesLbox.selection = this.lastListSelection
                )
                "SequenceManager Invert Selection": (
                    this.mainRollout.sequencesLbox.selection = this.lastListSelection
                )
                default: (
                    -- Für alle anderen Fälle, stelle die Selektion wieder her
                    this.mainRollout.sequencesLbox.selection = currentSelection
                )
            )
            
            -- WICHTIG: Trigger selectionEnd um die UI-Felder zu aktualisieren
            this.mainRollout.sequencesLbox.selectionEnd()
        )
    ),
	
	fn getSequenceBounds seq =
	(
		local minStart = 0
		local maxEnd = 999999
		
		-- Sortiere alle anderen Sequenzen nach Zeit
		local otherSequences = for s in this.sequences where s != seq collect s
		
		-- Finde die nächste Sequenz davor
		local prevSeq = undefined
		for s in otherSequences where s.endtime <= seq.starttime do
		(
			if prevSeq == undefined or s.endtime > prevSeq.endtime then
				prevSeq = s
		)
		
		-- Finde die nächste Sequenz danach
		local nextSeq = undefined
		for s in otherSequences where s.starttime >= seq.endtime do
		(
			if nextSeq == undefined or s.starttime < nextSeq.starttime then
				nextSeq = s
		)
		
		-- Setze die Grenzen basierend auf benachbarten Sequenzen
		if prevSeq != undefined then
			minStart = (prevSeq.endtime.frame as integer) + 1
		
		if nextSeq != undefined then
			maxEnd = (nextSeq.starttime.frame as integer) - 1
		
		#(minStart, maxEnd)
	),

	-- Neue Funktion: Validiert und passt einen Frame-Wert an
    fn validateFrameForSequence seq newStartFrame newEndFrame =
    (
        local validStart = newStartFrame
        local validEnd = newEndFrame
        
        -- Wenn seq undefined ist, prüfe gegen ALLE Sequenzen (für neue Sequenzen)
        if seq == undefined then
        (
            -- Prüfe ob die neue Range mit existierenden Sequenzen kollidiert
            for s in this.sequences do
            (
                -- Prüfe ob Start Frame in eine existierende Sequenz fällt
                if newStartFrame >= s.starttime.frame and newStartFrame <= s.endtime.frame then
                (
                    -- Setze Start nach dieser Sequenz
                    validStart = (s.endtime.frame as integer) + 1
                )
                
                -- Prüfe ob End Frame in eine existierende Sequenz fällt
                if newEndFrame >= s.starttime.frame and newEndFrame <= s.endtime.frame then
                (
                    -- Setze End vor dieser Sequenz
                    validEnd = (s.starttime.frame as integer) - 1
                )
                
                -- Prüfe ob die neue Sequenz eine existierende komplett umschließt
                if newStartFrame < s.starttime.frame and newEndFrame > s.endtime.frame then
                (
                    -- In diesem Fall End vor die existierende Sequenz setzen
                    validEnd = (s.starttime.frame as integer) - 1
                )
            )
        )
        else
        (
            -- Original-Code für existierende Sequenzen
            local bounds = this.getSequenceBounds seq
            
            -- Stelle sicher, dass Start nicht vor der erlaubten Grenze liegt
            if validStart < bounds[1] then
                validStart = bounds[1]
            
            -- Stelle sicher, dass End nicht nach der erlaubten Grenze liegt  
            if validEnd > bounds[2] then
                validEnd = bounds[2]
        )
        
        -- Stelle sicher, dass Start < End
        if validStart >= validEnd then
        (
            -- Behalte die originalen Werte wenn keine gültige Range möglich
            if seq != undefined then
                return #(seq.starttime.frame as integer, seq.endtime.frame as integer)
            else
            (
                -- Für neue Sequenzen: Versuche minimale Anpassung
                if validStart >= newEndFrame then
                    validStart = newEndFrame - 1
                if validEnd <= newStartFrame then
                    validEnd = newStartFrame + 1
            )
        )
        
        #(validStart, validEnd)
    ),
    
    mainRollout = rollout mainRollout "Sequence Manager" width:444 height:402
    (
        local self
        

        -- Ersetze den kompletten newSequenceRollout mit dieser kompakteren Version:
        local newSequenceRollout =
        (
            rollout newSequenceRollout "New Sequence" width:186 height:184
            (
                local parent
                local sequenceNames
                local isValidatingStart = false
                local isValidatingEnd = false
                local lastValidStart = 10
                local lastValidEnd = 60
                
                -- Preset Definitionen (korrigierte Syntax)
                local presetNames = #("Stand", "Walk", "Stand Ready", "Attack", "Attack Slam", "Spell", "Spell Slam", "Death", "Portrait Talk", "Custom")
                local presetStartOffsets = #(10, 10, 10, 10, 10, 10, 10, 10, 10, 10)
                local presetDurations = #(80, 28, 20, 24, 30, 28, 24, 64, 70, 50)
                local presetNonLooping = #(false, false, false, true, true, true, true, true, false, false)
                local presetRarity = #(undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined)
                local presetMoveSpeed = #(undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined)
                
                label presetLbl "Preset:" pos:[8,8] width:40 height:16 align:#left
                dropdownList presetDDL "" pos:[48,4] width:130 height:20 align:#left items:#("Stand (80f)", "Walk (28f)", "Stand Ready (20f)", "Attack (24f)", "Attack Slam (30f)", "Spell (28f)", "Spell Slam (24f)", "Death (64f)", "Portrait Talk (70f)", "Custom") selection:10 tooltip:"Select animation preset"
                
                label nameLbl "Name:" pos:[8,32] width:32 height:16 align:#left
                editText nameEdt "" pos:[48,32] width:130 height:16 align:#left tooltip:"Enter the name of the new sequence"
                
                label startFrameLbl "Start Frame:" pos:[8,56] width:64 height:16 align:#left
                spinner startFrameSpn "" pos:[116,56] width:64 height:16 range:[0,999999,10] type:#integer scale:1 align:#left tooltip:"Set the start frame of the sequence"
                
                label endFrameLbl "End Frame:" pos:[8,76] width:64 height:16 align:#left
                spinner endFrameSpn "" pos:[116,76] width:64 height:16 range:[0,999999,60] type:#integer scale:1 align:#left tooltip:"Set the end frame of the sequence"
                
                checkbox rarityChk "Rarity:" pos:[8,96] width:96 height:16 align:#left tooltip:"Enable/disable rarity of the animation"
                spinner raritySpn pos:[116,96] width:64 height:16 range:[0,40,0] type:#integer scale:1 enabled:false align:#left
                
                checkbox movespeedChk "Move Speed:" pos:[8,116] width:96 height:16 align:#left tooltip:"Enable/disable movement speed"
                spinner movespeedSpn pos:[116,116] width:64 height:16 range:[0,700,0] type:#integer scale:1 enabled:false align:#left
                
                checkbox nonloopingChk "Non Looping" pos:[8,136] width:96 height:16 enabled:true align:#left
                
                button okBtn "OK" pos:[4,156] width:84 height:20 align:#left tooltip:"Create new sequence and close dialog"
                button cancelBtn "Cancel" pos:[96,156] width:84 height:20 align:#left tooltip:"Cancel without creating a sequence"
                
                -- Hilfsfunktion: Prüft ob ein Bereich mit existierenden Sequenzen kollidiert
                fn isRangeFree startFrame endFrame =
                (
                    for seq in self.sequences do
                    (
                        -- Prüfe ob es eine Überlappung gibt
                        if not (endFrame < seq.starttime.frame or startFrame > seq.endtime.frame) then
                            return false
                    )
                    return true
                )
                
                -- Funktion zum Anwenden eines Presets
                fn applyPreset presetIndex =
                (
                    if presetIndex > 0 and presetIndex <= presetNames.count then
                    (
                        -- Name setzen (nur wenn nicht Custom)
                        if presetIndex < 10 then
                        (
                            nameEdt.text = presetNames[presetIndex]
                        )
                        
                        -- Berechne Start basierend auf letzter Sequenz
                        local newStart = 10  -- IMMER bei 10 starten wenn keine Sequenzen
                        if self.sequences.count > 0 then
                        (
                            local highestFrame = amax (for seq in self.sequences collect seq.endtime.frame as integer)
                            newStart = highestFrame + presetStartOffsets[presetIndex]
                        )
                        
                        -- Setze Frames (OHNE -1!)
                        startFrameSpn.value = newStart
                        endFrameSpn.value = newStart + presetDurations[presetIndex]  -- Kein -1 mehr!
                        lastValidStart = newStart
                        lastValidEnd = newStart + presetDurations[presetIndex]
                        
                        -- Setze Non-Looping
                        nonloopingChk.checked = presetNonLooping[presetIndex]
                        
                        -- Setze Rarity wenn definiert
                        if presetRarity[presetIndex] != undefined then
                        (
                            rarityChk.checked = true
                            raritySpn.enabled = true
                            raritySpn.value = presetRarity[presetIndex]
                        )
                        else
                        (
                            rarityChk.checked = false
                            raritySpn.enabled = false
                            raritySpn.value = 0
                        )
                        
                        -- Setze Move Speed wenn definiert
                        if presetMoveSpeed[presetIndex] != undefined then
                        (
                            movespeedChk.checked = true
                            movespeedSpn.enabled = true
                            movespeedSpn.value = presetMoveSpeed[presetIndex]
                        )
                        else
                        (
                            movespeedChk.checked = false
                            movespeedSpn.enabled = false
                            movespeedSpn.value = 0
                        )
                    )
                )
                
                on presetDDL selected index do
                (
                    applyPreset index
                )
                
                on nameEdt changed newName do ()
                
                on startFrameSpn changed val do
                (
                    local validated = self.validateFrameForSequence undefined val endFrameSpn.value
                    
                    if validated[1] != val then
                        format "Warning: Start frame adjusted from % to % to prevent overlap\n" val validated[1]
                    
                    startFrameSpn.value = validated[1]
                    lastValidStart = validated[1]
                )
                
                on endFrameSpn changed val do
                (
                    local validated = self.validateFrameForSequence undefined startFrameSpn.value val
                    
                    if validated[2] != val then
                        format "Warning: End frame adjusted from % to % to prevent overlap\n" val validated[2]
                    
                    endFrameSpn.value = validated[2]
                    lastValidEnd = validated[2]
                )
                
                on rarityChk changed state do raritySpn.enabled = state
                
                on movespeedChk changed state do movespeedSpn.enabled = state
                
                on okBtn pressed do
                (
                    -- Finale Validierung mit der ursprünglichen Funktion für Sicherheit
                    local finalValidated = self.validateFrameForSequence undefined startFrameSpn.value endFrameSpn.value
                    
                    undo "SequenceManager Create New Sequence" on
                    (
                        local sequenceName = self.getNewSequenceName nameEdt.text
                        local starttime = finalValidated[1] as time
                        local endtime = finalValidated[2] as time
                        local rarity = if rarityChk.checked then raritySpn.value else undefined
                        local movespeed = if movespeedChk.checked then movespeedSpn.value else undefined
                        local noLoop = nonloopingChk.checked
                            
                        self.addSequence sequenceName starttime endtime noLoop rarity movespeed
                        
                        self.saveSequences()
                        self.checkSequenceOverlapping()
                        
                        parent.setListItems()
                    )
                    DestroyDialog newSequenceRollout
                )
                
                on cancelBtn pressed do DestroyDialog newSequenceRollout
                
                on newSequenceRollout open do
                (
                    self = WdxSequenceManager
                    parent = mainRollout
                    
                    -- Initial Name setzen für Custom
                    nameEdt.text = "New Sequence"
                    
                    -- Standardmäßig "Custom" auswählen und anwenden  
                    presetDDL.selection = 10
                    
                    -- IMMER nach der letzten Sequenz einfügen!
                    if self.sequences.count > 0 then
                    (
                        -- Finde das höchste End-Frame aller Sequenzen
                        local highestFrame = amax (for seq in self.sequences collect seq.endtime.frame as integer)
                        
                        -- Setze Start 10 Frames nach der letzten Sequenz
                        startFrameSpn.value = highestFrame + 10
                        endFrameSpn.value = highestFrame + 60  -- 50 Frames Duration für Custom
                        lastValidStart = highestFrame + 10
                        lastValidEnd = highestFrame + 60
                    )
                    else
                    (
                        -- Keine Sequenzen vorhanden, starte bei Frame 10
                        startFrameSpn.value = 10
                        endFrameSpn.value = 60   -- 50 Frames Duration (10-60)
                        lastValidStart = 10
                        lastValidEnd = 60
                    )

                    -- Localization
                    if ::WdxL != undefined then
                    (
                        newSequenceRollout.title = ::WdxL.t "seq_new_sequence_title"
                        presetLbl.text = ::WdxL.t "seq_preset_lbl"
                        presetDDL.tooltip = ::WdxL.t "seq_select_animation_preset_tip"
                        nameLbl.text = ::WdxL.t "seq_name_lbl"
                        nameEdt.tooltip = ::WdxL.t "seq_enter_the_name_of_the_new_sequ_tip"
                        startFrameLbl.text = ::WdxL.t "seq_start_frame_lbl"
                        endFrameLbl.text = ::WdxL.t "seq_end_frame_lbl"
                        rarityChk.text = ::WdxL.t "seq_rarity_chk"
                        rarityChk.tooltip = ::WdxL.t "seq_enabledisable_rarity_of_the_a_tip"
                        movespeedChk.text = ::WdxL.t "seq_move_speed_chk"
                        movespeedChk.tooltip = ::WdxL.t "seq_enabledisable_movement_speed_tip"
                        nonloopingChk.text = ::WdxL.t "seq_non_looping_chk"
                        okBtn.text = ::WdxL.t "seq_ok_btn"
                        okBtn.tooltip = ::WdxL.t "seq_create_new_sequence_and_close_tip"
                        cancelBtn.text = ::WdxL.t "seq_cancel_btn"
                        cancelBtn.tooltip = ::WdxL.t "seq_cancel_without_creating_a_sequ_tip"
                    )
                )
            )
        )
        
        ------------------------------------------------------
        groupBox filterGrp "Filter List" pos:[8,30] width:232 height:44 align:#left tooltip:"Options for filtering the sequence list"
        editText filterTxt pos:[12,48] width:168 height:16 align:#left tooltip:"Enter a search term to filter the list of sequences"
        button filterClearBtn "Clear" pos:[188,46] width:48 height:20 align:#left tooltip:"Clear the search field and show all sequences"
        
        multiListBox sequencesLbox pos:[8,78] width:232 height:20 align:#left tooltip:"List of all available animation sequences - double-click to play"
        
        groupBox selectGrp "Select" pos:[8,346] width:232 height:48 align:#left tooltip:"Options for selecting sequences"
        button selectAllBtn "All" pos:[12,366] width:72 height:20 align:#left tooltip:"Select all sequences in the list"
        button selectNoneBtn "None" pos:[88,366] width:72 height:20 align:#left tooltip:"Deselect all sequences"
        button selectInvertBtn "Invert" pos:[164,366] width:72 height:20 align:#left tooltip:"Invert the current selection"
        
        groupBox sequenceGrp "Sequence" pos:[248,30] width:188 height:68 align:#left tooltip:"Options for creating and managing sequences"
        button sequenceNewBtn "New..." pos:[252,46] width:80 height:20 align:#left tooltip:"Create a new animation sequence"
        button sequenceDeleteBtn "Delete Selected" pos:[336,46] width:96 height:20 align:#left tooltip:"Delete selected sequences"
        button duplicateBtn "Duplicate" pos:[252,70] width:88 height:20 align:#left tooltip:"Duplicate selected sequences"
        button globalsBtn "Globals..." pos:[344,70] width:88 height:20 align:#left tooltip:"Open the Global Sequence Manager"
        
        groupBox editSelectedGrp "Edit Selected" pos:[248,106] width:188 height:232 align:#left tooltip:"Edit properties of selected sequence(s)"
        label nameLbl "Name:" pos:[256,126] width:32 height:16 align:#left
        editText nameEdt pos:[288,126] width:138 height:16 align:#left tooltip:"Edit name of the selected sequence(s)"
        
        label startFrameLbl "Start Frame:" pos:[256,150] width:64 height:16 align:#left
        button setStartFrameBtn "⏰" pos:[340,150] width:20 height:16 align:#left tooltip:"Set current frame as start frame" enabled:false
        spinner startFrameSpn pos:[364,150] width:64 height:16 range:[-999999,999999,0] type:#integer scale:1 align:#left tooltip:"Set the start frame of selected sequence(s)"
        
        label endFrameLbl "End Frame:" pos:[256,170] width:64 height:16 align:#left
        button setEndFrameBtn "⏰" pos:[340,170] width:20 height:16 align:#left tooltip:"Set current frame as end frame" enabled:false
        spinner endFrameSpn pos:[364,170] width:64 height:16 range:[-999999,999999,0] type:#integer scale:1 align:#left tooltip:"Set the end frame of selected sequence(s)"
        
        checkbox rarityChk "Rarity:" pos:[256,190] width:96 height:16 align:#left tooltip:"Enable/disable rarity of the animation. Warcraft III's animation choosing system allows you to have multiple animations with the same name. The animations are randomly chosen."
        spinner raritySpn pos:[364,190] width:64 height:16 range:[0,40,0] type:#integer scale:1 align:#left tooltip:"Set the rarity value (0-40). To ensure that certain animations happen less often, add a rarity value from 1 to 9. The higher the number, the less often that animation will be chosen."
        checkbox movespeedChk "Move Speed:" pos:[256,210] width:96 height:16 align:#left tooltip:"Enable/disable movement speed. The movespeed value doesn't affect the model at all, but it is a useful tool in previews, where it will move the ground grid at that rate to simulate walking."
        spinner movespeedSpn pos:[364,210] width:64 height:16 enabled:true range:[0,700,0] type:#integer scale:1 align:#left tooltip:"Set the movement speed value (0-700). Most Warcraft III units have between 250 and 400 movement speed."
        checkbox nonloopingChk "Non Looping" pos:[256,230] width:96 height:16 enabled:true align:#left tooltip:"Animation will not loop when played. By default, all animations loop. Attack and spell animations are NonLooping since they play one time and then play the stand animation."
        checkbox ignoreChk "Ignore" pos:[364,230] width:64 height:16 enabled:true align:#left tooltip:"If this checkbox is enabled, this animation will be ignored during export"
        
        groupBox nudgeKeysGrp "Nudge Keys" pos:[256,254] width:172 height:74 align:#left tooltip:"Fine adjustment of keyframes"
        
        label nudgeKeysStartLbl "Start:" pos:[264,274] width:32 height:16 align:#left
        button nudgeKeysStartDownBtn "<" pos:[300,272] width:60 height:20 align:#left tooltip:"Move the start frame of the selected animation one frame earlier (to the left)"
        button nudgeKeysStartUpBtn ">" pos:[364,272] width:60 height:20 align:#left tooltip:"Move the start frame of the selected animation one frame later (to the right)"
        
        label nudgeKeysEndLbl "End:" pos:[264,302] width:32 height:16 align:#left
        button nudgeKeysEndDownBtn "<" pos:[300,300] width:60 height:20 align:#left tooltip:"Move the end frame of the selected animation one frame earlier (to the left)"
        button nudgeKeysEndUpBtn ">" pos:[364,300] width:60 height:20 align:#left tooltip:"Move the end frame of the selected animation one frame later (to the right)"
        
        groupBox fileGrp "File" pos:[248,346] width:188 height:48 align:#left tooltip:"File management for sequences"
        button fileLoadBtn "Load" pos:[252,366] width:88 height:20 align:#left tooltip:"Load sequences from a file"
        button fileSaveBtn "Save" pos:[344,366] width:88 height:20 align:#left tooltip:"Save sequences to a file"
        
        ---------------------------------------------------------
        -- Event Handlers für die neuen Buttons
        on setStartFrameBtn pressed do
		(
			if sequencesLbox.selection.numberSet == 1 then
			(
				local selId = (sequencesLbox.selection as array)[1]
				-- Global Sequences sind nicht editierbar
				if selId <= self.globalSequences.count then return()
				
				undo "SequenceManager Set Current Frame as Start" on
				(
					local newVal = currentTime.frame as integer
					local filteredId = selId - self.globalSequences.count
					local seq = self.filteredSequences[filteredId]
					if seq != undefined then
					(
						-- Validierung mit Überlappungsschutz
						local validated = self.validateFrameForSequence seq newVal (seq.endtime.frame as integer)
						
						if validated[1] != newVal then
						(
							format "Warning: Start frame adjusted from % to % to prevent overlap\n" newVal validated[1]
						)
						
						startFrameSpn.value = validated[1]
						seq.starttime = validated[1] as time
					)
					self.saveSequences()
					self.checkSequenceOverlapping()
				)
			)
		)
        
        on setEndFrameBtn pressed do
		(
			if sequencesLbox.selection.numberSet == 1 then
			(
				local selId = (sequencesLbox.selection as array)[1]
				-- Global Sequences sind nicht editierbar
				if selId <= self.globalSequences.count then return()
				
				undo "SequenceManager Set Current Frame as End" on
				(
					local newVal = currentTime.frame as integer
					local filteredId = selId - self.globalSequences.count
					local seq = self.filteredSequences[filteredId]
					if seq != undefined then
					(
						-- Validierung mit Überlappungsschutz
						local validated = self.validateFrameForSequence seq (seq.starttime.frame as integer) newVal
						
						if validated[2] != newVal then
						(
							format "Warning: End frame adjusted from % to % to prevent overlap\n" newVal validated[2]
						)
						
						endFrameSpn.value = validated[2]
						seq.endtime = validated[2] as time
					)
					self.saveSequences()
					self.checkSequenceOverlapping()
				)
			)
		)
        
        ---------------------------------------------------------
        fn sortAsc v1 v2 =
        (
            case of
            (
                (v1 > v2): 1
                (v1 < v2): -1
                default: 0
            )
        )
		
        fn sortDesc v1 v2 =
        (
            case of
            (
                (v1 > v2): -1
                (v1 < v2): 1
                default: 0
            )
        )

		-- ============================================================================
		-- GLOBAL SEQUENCE HELPER FUNCTIONS (Rollout-lokal)
		-- ============================================================================
		
		-- Prüft ob ein Listbox-Index auf eine Global Sequence zeigt
		fn isGlobalListId id = (id > 0 and id <= self.globalSequences.count)
		
		-- Wandelt einen Listbox-Index in einen filteredSequences-Index um
		-- Gibt 0 zurück wenn es ein Global-Index ist oder ungültig
		fn toFilteredId id =
		(
			local fid = id - self.globalSequences.count
			if fid >= 1 and fid <= self.filteredSequences.count then fid else 0
		)
		
		-- Holt die Sequenz aus einem Listbox-Index, undefined für Globals
		fn seqFromListId id =
		(
			local fid = toFilteredId id
			if fid > 0 then self.filteredSequences[fid] else undefined
		)
		
		-- Holt den Index in self.sequences aus einem Listbox-Index, 0 für Globals/ungültig
		fn seqIndexFromListId id =
		(
			local fid = toFilteredId id
			if fid > 0 then findItem self.sequences self.filteredSequences[fid] else 0
		)
		
		-- Prüft ob die aktuelle Selektion ausschließlich Globals enthält
		fn selectionIsOnlyGlobals =
		(
			local gc = self.globalSequences.count
			for id in sequencesLbox.selection where id > gc do return false
			true
		)
		
		-- Setzt alle Edit-Felder in den deaktivierten Zustand (für Globals oder leere Selektion)
		fn disableAllEditFields =
		(
			startFrameSpn.enabled = false
			endFrameSpn.enabled = false
			setStartFrameBtn.enabled = false
			setEndFrameBtn.enabled = false
			nameEdt.enabled = false
			ignoreChk.enabled = false
			nonloopingChk.enabled = false
			rarityChk.enabled = false
			raritySpn.enabled = false
			movespeedChk.enabled = false
			movespeedSpn.enabled = false
		)
		
		-- Reaktiviert die Edit-Felder für normale Sequenzen
		fn enableEditFields =
		(
			nameEdt.enabled = true
			startFrameSpn.enabled = true
			endFrameSpn.enabled = true
			setStartFrameBtn.enabled = true
			setEndFrameBtn.enabled = true
			ignoreChk.enabled = true
			nonloopingChk.enabled = true
			rarityChk.enabled = true
			movespeedChk.enabled = true
			-- raritySpn und movespeedSpn werden separat über ihre Checkboxen gesteuert
		)
		
		-- ============================================================================
        
        fn setListItems =
        (
			-- Global Sequences immer oben anzeigen (unabhaengig vom Filter)
			local globalItems = for i = 1 to self.globalSequences.count collect 
			(
				local dur = self.globalSequences[i].frame as integer
				"[GLOBAL " + i as string + "]  0 - " + dur as string
			)
			
			-- Normale Sequenzen filtern
            self.filteredSequences = for sequence in self.sequences where findString sequence.name filterTxt.text != undefined collect sequence
			local normalItems = for filteredSequence in self.filteredSequences collect (
				if self.isSequenceIgnored filteredSequence.name then 
					"[IGNORED] " + filteredSequence.name 
				-- Reforged v1200: Shared-Alias anzeigen (nur wenn Name != Master-Name)
				else if filteredSequence.sharedGroup != undefined and \
				        filteredSequence.sharedGroup != filteredSequence.name then
					"[Shared: " + filteredSequence.sharedGroup + "] " + filteredSequence.name
				else 
					filteredSequence.name
			)
			
			-- Kombinieren: Globals oben, dann normale Sequenzen
			sequencesLbox.items = join globalItems normalItems
        )
            
        on filterTxt changed txt do setListItems()
    
        on filterClearBtn pressed do
        (
            filterTxt.text = ""
            filterTxt.changed filterTxt.text
        )
        
        on selectAllBtn pressed do
        (
			self.lastListSelection = sequencesLbox.selection
			
            undo "SequenceManager Select All" on
            (
                sequencesLbox.selection = #{1..sequencesLbox.items.count}
                sequencesLbox.selectionEnd()
            )
        )
        
        on selectNoneBtn pressed do
        (
			self.lastListSelection = sequencesLbox.selection
			
            undo "SequenceManager Select None" on
            (
                sequencesLbox.selection = #{}
                sequencesLbox.selectionEnd()
            )
        )
        
        on selectInvertBtn pressed do
        (
			self.lastListSelection = sequencesLbox.selection
			
            undo "SequenceManager Invert Selection" on
            (
                sequencesLbox.selection = #{1..sequencesLbox.items.count} - sequencesLbox.selection
                sequencesLbox.selectionEnd()
            )
        )
        
        on sequenceNewBtn pressed do
        (
            if (maxVersion())[1] >= 19000 then
                CreateDialog newSequenceRollout modal:true parent:mainRollout.hwnd
            else
                CreateDialog newSequenceRollout
        )
        
        on sequenceDeleteBtn pressed do
        (
            undo "SequenceManager Delete Sequence(s)" on
            (
                local selectedIds = sequencesLbox.selection as array
                qsort selectedIds sortDesc
				local gc = self.globalSequences.count
                for selectedId in selectedIds where selectedId > gc do
                (
					local seqId = seqIndexFromListId selectedId
					if seqId > 0 then deleteItem self.sequences seqId
                )
                self.saveSequences()
                setListItems()
                sequencesLbox.selectionEnd()
            )
        )
        
        on duplicateBtn pressed do
        (
			undo "SequenceManager Duplicate Sequence(s)" on
            (
                local selectedIds = sequencesLbox.selection as array
                qsort selectedIds sortAsc
				local gc = self.globalSequences.count
                for selectedId in selectedIds where selectedId > gc do
                (
					local seqId = seqIndexFromListId selectedId
					if seqId == 0 then continue
					
					local seq = self.sequences[seqId]
					local newName = self.getNewSequenceName seq.name
					local newStartTime = self.sequences[self.sequences.count].endtime + 10f
					local newEndTime = newStartTime + (seq.endtime - seq.starttime)
					
					self.addSequence newName newStartTime newEndTime seq.loop seq.rarity seq.movespeed
					
					local timeOffset = newStartTime - seq.starttime
					
					-- Helper: copy keys of a single controller if animated and not global
					fn copyControllerKeys con srcInterval offset bumpFn =
					(
						if con == undefined then return false
						if con.keys == undefined or con.keys.count == 0 then return false
						
						local isGlobal = false
						try ( isGlobal = (getAfterORT con == #cycle) ) catch()
						if isGlobal then return false
						
						deselectKeys con
						selectKeys con srcInterval
						copyPasteKeys con bumpFn offset #replacekeys
						deselectKeys con
						true
					)
					
					try
					(
						local srcInterval = interval seq.starttime seq.endtime
						
						for obj in objects do
						(
							-- Transform keys
							local transCon = obj.transform.controller
							deselectKeys transCon
							selectKeys transCon srcInterval
							copyPasteKeys transCon self.bumpTime timeOffset #replacekeys
							deselectKeys transCon
							
							-- Visibility keys
							local visCon = getVisController obj
							if visCon != undefined then
								copyControllerKeys visCon srcInterval timeOffset self.bumpTime
							
							-- Camera FOV
							try
							(
								if (superClassOf obj == camera) then
									copyControllerKeys obj.fov.controller srcInterval timeOffset self.bumpTime
							)
							catch()
							
							-- Material: Alpha + Texture Animations
							try
							(
								local mat = obj.material
								if mat != undefined and (classOf mat == Warcraft3) then
								(
									local matCons = #()
									try ( if mat.Alpha.isAnimated then append matCons mat.Alpha.controller ) catch()
									try ( if mat.Wc3_U_Offset.isAnimated then append matCons mat.Wc3_U_Offset.controller ) catch()
									try ( if mat.Wc3_V_Offset.isAnimated then append matCons mat.Wc3_V_Offset.controller ) catch()
									try ( if mat.Wc3_W_Angle.isAnimated then append matCons mat.Wc3_W_Angle.controller ) catch()
									try ( if mat.Wc3_U_Tiling.isAnimated then append matCons mat.Wc3_U_Tiling.controller ) catch()
									try ( if mat.Wc3_V_Tiling.isAnimated then append matCons mat.Wc3_V_Tiling.controller ) catch()
									try ( if mat.MatColor.isAnimated then append matCons mat.MatColor.controller ) catch()
									
									for con in matCons do
										copyControllerKeys con srcInterval timeOffset self.bumpTime
								)
							)
							catch()
							
							-- Plugin parameters: Light, Particle1, Ribbon
							try
							(
								local bo = obj.baseObject
								local cls = classOf bo
								
								local pluginParams = case cls of
								(
									Wdx_Light: #("DecayStart", "DecayEnd", "AmbColor", "AmbValue", "ShadowColor", "ShadowValue")
									BlizzParticle1: #("radius", "longitude", "latitude", "PartEmit", "Life", "Gravity")
									BlizRibbon: #("slots", "Above", "Below", "vertexcolor", "alpha")
									default: #()
								)
								
								for paramName in pluginParams do
								(
									try
									(
										local prop = getProperty bo paramName
										if prop.isAnimated and prop.controller != undefined then
											copyControllerKeys prop.controller srcInterval timeOffset self.bumpTime
									)
									catch()
								)
							)
							catch()
						)
					)
					catch print (getCurrentException())
					
                )
                self.saveSequences()
                setListItems()
                sequencesLbox.selectionEnd()
            )
        )
        
        on globalsBtn pressed do
        (
            if ::WdxGlobalSequenceManager != undefined then
                ::WdxGlobalSequenceManager.showUI()
            else
                messageBox (::WdxL.t "seq_globalsequencemanager_not_load_msg") title:(::WdxL.t "seq_error_ptitle")
        )

        on sequencesLbox doubleClicked sel do
        (
			-- Doppelklick auf Global Sequence: ignorieren (kein Playback)
			if isGlobalListId sel then return()
            if sequencesLbox.selection.numberSet == 1 then
			(
				local seq = seqFromListId sel
				if seq != undefined then
					self.playSequence (findItem self.sequences seq)
			)
        )
    
        on sequencesLbox selected id do
        (
            -- Optionaler Code bei Listbox-Auswahl
        )
        
        on sequencesLbox selectionEnd do
		(
			local numSelected = sequencesLbox.selection.numberSet
			local gc = self.globalSequences.count
			
			-- ============================================================
			-- NICHTS SELEKTIERT
			-- ============================================================
			if numSelected == 0 then
			(
				disableAllEditFields()
				startFrameSpn.value = endFrameSpn.value = 0
				
				-- Reset Spinner Ranges
				startFrameSpn.range = [-999999, 999999, 0]
				endFrameSpn.range = [-999999, 999999, 0]

				ignoreChk.triState = 0
				nonloopingChk.triState = 0
				rarityChk.triState = 0
				raritySpn.value = 0
				movespeedChk.triState = 0
				movespeedSpn.value = 0
				
				nameEdt.text = "<<Nothing selected>>"
			)
			
			-- ============================================================
			-- GENAU 1 ELEMENT SELEKTIERT
			-- ============================================================
			if numSelected == 1 then
			(
				local seqId = (sequencesLbox.selection as array)[1]
				
				-- --- GLOBAL SEQUENCE SELEKTIERT ---
				if seqId <= gc then
				(
					disableAllEditFields()
					
					local duration = self.globalSequences[seqId]
					nameEdt.text = "Global " + seqId as string
					startFrameSpn.value = 0
					endFrameSpn.value = duration.frame as integer
					
					-- Reset Ranges für read-only Anzeige
					startFrameSpn.range = [0, 0, 0]
					endFrameSpn.range = [0, duration.frame as integer, duration.frame as integer]
					startFrameSpn.tooltip = "Global Sequence - read only"
					endFrameSpn.tooltip = "Global Sequence - read only"
					
					ignoreChk.triState = 0
					nonloopingChk.triState = 0
					rarityChk.triState = 0
					raritySpn.value = 0
					movespeedChk.triState = 0
					movespeedSpn.value = 0
				)
				-- --- NORMALE SEQUENZ SELEKTIERT ---
				else
				(
					local seq = seqFromListId seqId
					if seq == undefined then return()
					
					enableEditFields()
					
					startFrameSpn.value = seq.starttime.frame as integer
					endFrameSpn.value = seq.endtime.frame as integer
					
					-- Setze die erlaubten Ranges für die Spinner
					local bounds = self.getSequenceBounds seq
					
					-- StartFrame kann zwischen der vorherigen Sequenz und dem aktuellen EndFrame-1 liegen
					local maxStartFrame = (seq.endtime.frame as integer) - 1
					startFrameSpn.range = [bounds[1], maxStartFrame, seq.starttime.frame as integer]
					
					-- EndFrame kann zwischen dem aktuellen StartFrame+1 und der nächsten Sequenz liegen
					local minEndFrame = (seq.starttime.frame as integer) + 1
					endFrameSpn.range = [minEndFrame, bounds[2], seq.endtime.frame as integer]
					
					startFrameSpn.tooltip = "Valid range: " + bounds[1] as string + " to " + maxStartFrame as string + " (limited by adjacent sequences)"
					endFrameSpn.tooltip = "Valid range: " + minEndFrame as string + " to " + bounds[2] as string + " (limited by adjacent sequences)"
					
					ignoreChk.triState = if self.isSequenceIgnored seq.name then 1 else 0
					nonloopingChk.triState = if seq.loop then 1 else 0
					rarityChk.triState = if seq.rarity != undefined then 1 else 0
					raritySpn.value = if seq.rarity != undefined then seq.rarity else 0
					raritySpn.enabled = if seq.rarity != undefined then true else false
					movespeedChk.triState = if seq.movespeed != undefined then 1 else 0
					movespeedSpn.value = if seq.movespeed != undefined then seq.movespeed else 0
					movespeedSpn.enabled = if seq.movespeed != undefined then true else false
					
					nameEdt.text = seq.name
				)
			)
			
			-- ============================================================
			-- MEHRERE ELEMENTE SELEKTIERT
			-- ============================================================
			if numSelected > 1 then
			(
				startFrameSpn.enabled = endFrameSpn.enabled = false
				setStartFrameBtn.enabled = setEndFrameBtn.enabled = false
				startFrameSpn.value = endFrameSpn.value = 0
				
				-- Reset Spinner Ranges bei Mehrfachauswahl
				startFrameSpn.range = [-999999, 999999, 0]
				endFrameSpn.range = [-999999, 999999, 0]
				startFrameSpn.tooltip = "Multiple sequences selected"
				endFrameSpn.tooltip = "Multiple sequences selected"
				
				-- Wenn NUR Globals selektiert sind, alles deaktivieren
				if selectionIsOnlyGlobals() then
				(
					disableAllEditFields()
					nameEdt.text = "<<Global Sequences>>"
					ignoreChk.triState = 0
					nonloopingChk.triState = 0
					rarityChk.triState = 0
					raritySpn.value = 0
					movespeedChk.triState = 0
					movespeedSpn.value = 0
				)
				else
				(
					-- Mix oder nur normale: Edit-Felder für Multi-Selektion
					-- Checkboxen reaktivieren (könnten von Global-Selektion deaktiviert sein)
					ignoreChk.enabled = true
					nonloopingChk.enabled = true
					rarityChk.enabled = true
					movespeedChk.enabled = true
					nameEdt.enabled = true
					
					ignoreChk.triState = 2  -- Mixed state
					nonloopingChk.triState = 2
					rarityChk.triState = 2
					raritySpn.value = 0
					raritySpn.enabled = false
					movespeedChk.triState = 2
					movespeedSpn.value = 0
					movespeedSpn.enabled = false
					
					nameEdt.text = "<<Multiple Selection>>"
				)
			)
		)
        
        on nameEdt entered newName do
        (
            undo "SequenceManager Rename Animation" on
            (
                local counter = 0
				local gc = self.globalSequences.count
				-- Nur nicht-globale Selektionen zaehlen fuer die Namensgebung
                local selectionCount = (for id in sequencesLbox.selection where id > gc collect id).count
                for id in sequencesLbox.selection where id > gc do
                (
                    counter += 1
                    local currentName = newName + (if selectionCount > 1 then counter as string else "")
                    
                    local sequenceNames = for seq in self.sequences collect seq.name
                    local uniqueCounter = 0
                    while findItem sequenceNames currentName != 0 do
                    (
                        uniqueCounter += 1
                        currentName += "_" + uniqueCounter as string
                    )
                    
					local seq = seqFromListId id
                    if seq == undefined then continue
                    -- Reforged v1200: Shared-Link brechen bei Umbenennung
                    if seq.sharedGroup != undefined then seq.sharedGroup = undefined
                    seq.name = currentName
                )
                self.saveSequences()
                setListItems()
            )
        )
        
        on startFrameSpn changed val do
		(
			undo "SequenceManager Change Start Frame" on
			(
				local gc = self.globalSequences.count
				for id in sequencesLbox.selection where id > gc do
				(
					local seq = seqFromListId id
					if seq == undefined then continue
					
					-- Neue Validierung mit Überlappungsschutz
					local validated = self.validateFrameForSequence seq val (seq.endtime.frame as integer)
					
					if validated[1] != val then
					(
						-- Zeige Warnung wenn der Wert angepasst wurde
						format "Warning: Start frame adjusted from % to % to prevent overlap\n" val validated[1]
					)
					
					seq.starttime = validated[1] as time
					
					-- Update Spinner wenn Wert angepasst wurde
					if sequencesLbox.selection.numberSet == 1 then
						startFrameSpn.value = validated[1]
				)
				self.saveSequences()
				self.checkSequenceOverlapping()
			)
		)
        
        on endFrameSpn changed val do
		(
			undo "SequenceManager Change End Frame" on
			(
				local gc = self.globalSequences.count
				for id in sequencesLbox.selection where id > gc do
				(
					local seq = seqFromListId id
					if seq == undefined then continue
					
					-- Neue Validierung mit Überlappungsschutz
					local validated = self.validateFrameForSequence seq (seq.starttime.frame as integer) val
					
					if validated[2] != val then
					(
						-- Zeige Warnung wenn der Wert angepasst wurde
						format "Warning: End frame adjusted from % to % to prevent overlap\n" val validated[2]
					)
					
					seq.endtime = validated[2] as time
					
					-- Update Spinner wenn Wert angepasst wurde
					if sequencesLbox.selection.numberSet == 1 then
						endFrameSpn.value = validated[2]
				)
				self.saveSequences()
				self.checkSequenceOverlapping()
			)
		)
        
        on rarityChk changed state do
        (
            undo "SequenceManager Change Rarity Checkbox" on
            (
                if state != 1 then
                (
                    raritySpn.value = 0
                    raritySpn.enabled = false
                )
                else
                (
                    raritySpn.enabled = true
                )
                
				local gc = self.globalSequences.count
                for id in sequencesLbox.selection where id > gc do
                (
                    local seq = seqFromListId id
                    if seq == undefined then continue
                    seq.rarity = if state == 1 then raritySpn.value else undefined
                )
                self.saveSequences()
            )
        )
        
        on raritySpn changed val do
        (
            undo "SequenceManager Change Rarity Value" on
            (
				local gc = self.globalSequences.count
                for id in sequencesLbox.selection where id > gc do
                (
                    local seq = seqFromListId id
                    if seq == undefined then continue
                    seq.rarity = val
                )
                self.saveSequences()
            )
        )
        
        on movespeedChk changed state do
        (
            undo "SequenceManager Change Move Speed Checkbox" on
            (
                if state != 1 then
                (
                    movespeedSpn.value = 0
                    movespeedSpn.enabled = false
                )
                else
                (
                    movespeedSpn.enabled = true
                )
    
				local gc = self.globalSequences.count
                for id in sequencesLbox.selection where id > gc do
                (
                    local seq = seqFromListId id
                    if seq == undefined then continue
                    seq.movespeed = if state == 1 then movespeedSpn.value else undefined
                )
                self.saveSequences()
            )
        )
        
        on movespeedSpn changed val do
        (
            undo "SequenceManager Change Move Speed Value" on
            (
				local gc = self.globalSequences.count
                for id in sequencesLbox.selection where id > gc do
                (
                    local seq = seqFromListId id
                    if seq == undefined then continue
                    seq.movespeed = val
                )
                self.saveSequences()
            )
        )
		
		on nonloopingChk changed state do
        (
            undo "SequenceManager Change Non Looping" on
            (
                local seqs = #()
				local gc = self.globalSequences.count
                for id in sequencesLbox.selection where id > gc do
                (
                    local seq = seqFromListId id
                    if seq == undefined then continue
                    seq.loop = if state == 1 then true else false
                    append seqs seq
                )
                self.saveSequences()
            )
        )
        
		on ignoreChk changed state do
		(
			undo "SequenceManager Change Ignore" on
			(
				local gc = self.globalSequences.count
				for id in sequencesLbox.selection where id > gc do
				(
					local seq = seqFromListId id
					if seq == undefined then continue
					self.setSequenceIgnored seq.name (state == 1)
				)
				self.saveIgnoredSequences()
				setListItems()  -- Liste aktualisieren für visuelle Kennzeichnung
			)
		)
		
		-- Gemeinsame Nudge-Funktion: which = #start oder #end, delta = 1 oder -1
		fn nudgeKey which delta =
		(
			undo "SequenceManager Nudge Key" on
			(
				local gc = self.globalSequences.count
				for id in sequencesLbox.selection where id > gc do
				(
					local seq = seqFromListId id
					if seq == undefined then continue
					
					local curStart = seq.starttime.frame as integer
					local curEnd = seq.endtime.frame as integer
					
					local newStart = if which == #start then curStart + delta else curStart
					local newEnd = if which == #end then curEnd + delta else curEnd
					
					local validated = self.validateFrameForSequence seq newStart newEnd
					local valIdx = if which == #start then 1 else 2
					local newVal = if which == #start then newStart else newEnd
					
					if validated[valIdx] != newVal then
					(
						::ErrorReporter.logWarning "Sequence" "nudge" ("Cannot nudge further - boundary reached at frame " + validated[valIdx] as string)
					)
					else
					(
						if which == #start then seq.starttime = validated[1] as time
						else seq.endtime = validated[2] as time
					)
				)
				self.saveSequences()
				sequencesLbox.selectionEnd()
				self.checkSequenceOverlapping()
			)
		)
		
		on nudgeKeysStartDownBtn pressed do (nudgeKey #start -1)
        on nudgeKeysStartUpBtn pressed do (nudgeKey #start 1)
		on nudgeKeysEndDownBtn pressed do (nudgeKey #end -1)
		on nudgeKeysEndUpBtn pressed do (nudgeKey #end 1)
        
        fn parseSequenceData sequenceDataStr =
        (
            local data = filterString sequenceDataStr ";"
            data[1] = substituteString data[1] "Name:" ""
            data[2] = substituteString data[2] " loop:" ""
            data[3] = substituteString data[3] " Rarity:" ""
            data[4] = substituteString data[4] " Movespeed:" ""
            data[5] = substituteString data[5] " start:" ""
            data[6] = substituteString data[6] " end:" ""
            local formatted_data = #()
            append formatted_data data
            data
        )
        
        fn setAnimKey noteTrack sequenceData =
        (
            function sequenceToString seq =
            (
                local newLine = (bit.intAsChar 13) + (bit.intAsChar 10)
                local s = "\""+ seq.name + "\""
                if seq.loop then s += newLine + "NonLooping"
                if seq.rarity != undefined then s += (newLine + "Rarity " + ((seq.rarity as integer) as string))
                if seq.movespeed != undefined then s += (newLine + "MoveSpeed " + ((seq.movespeed as integer) as string))
                s
            )
            local val = sequenceToString sequenceData
            local key1 = addNewNoteKey noteTrack.keys sequenceData.starttime
            local key2 = addNewNoteKey noteTrack.keys sequenceData.endtime
            key1.value = val
            key2.value = val
        )
        
        fn ensureUniqueNames =
        (
            local sequenceNames = for seq in self.sequences collect seq.name
            for sourceId = 1 to sequenceNames.count do
            (
                local sourceName = sequenceNames[sourceId]
                local id = findItem sequenceNames sourceName
                local newName = sourceName
                local counter = 0
                while id != 0 and id != sourceId do
                (
                    counter += 1
                    newName = sourceName + counter as string
                    id = findItem sequenceNames newName
                )
                sequenceNames[sourceId] = newName
            )
            for id = 1 to sequenceNames.count do self.sequences[id].name = sequenceNames[id]
        )
        
        on fileLoadBtn pressed do
        (
            try
            (
                local tempSequences = #()
                local filename = getOpenFileName types:"TXT File (*.txt)|*.txt"
                if filename != undefined then
                (
                    local loadedSequences = #()
                    local input_file = openfile filename
                    while not (eof input_file) do
                    (
                        append loadedSequences (parseSequenceData (readLine input_file))
                    )
                    while numNoteTracks rootNode != 0 do deleteNoteTrack rootNode (getNoteTrack rootNode 1)
                    local NT = notetrack "animations"
                    addNoteTrack rootNode NT
                    for i = 1 to loadedSequences.count do
                    (
                        local noLoop = false
                        local rarity, movespeed
                        local sequenceName = loadedSequences[i][1]
                        if stricmp loadedSequences[i][2] "true" == 0 then noLoop = true
                        if stricmp loadedSequences[i][3] "" != 0 then rarity = (substring loadedSequences[i][3] 1 1) as integer
                        if stricmp loadedSequences[i][4] "" != 0 then movespeed = (substring loadedSequences[i][4] 1 1) as integer
                        local newStartTime = loadedSequences[i][5] as time
                        local newEndTime = loadedSequences[i][6] as time
                        local SequenceData = Wc3Sequence sequenceName newStartTime newEndTime noLoop rarity movespeed undefined
                        
                        append tempSequences SequenceData
                        animationRange = interval 0f newEndTime
                        setAnimKey NT SequenceData
                    )
                    close input_file
                    local msg = loadedSequences.count as string + " animations have been load from: " + filename
                    print msg
                    messagebox msg title:(::WdxL.t "seq_sequence_names_load_success_ptitle")
                    
                    if loadedSequences.count > 0 then
                    (
                        self.sequences = tempSequences
                        ensureUniqueNames()
                        self.saveSequences()
                    )
                )
            )
            catch
            (
                try (close input_file) catch()
                messageBox (getCurrentException())
            )
            setListItems()
        )
        
		-- File Save: Nur normale Sequenzen speichern, keine Global Sequences
        on fileSaveBtn pressed do
        (
            local filename = getSaveFileName types:"TXT File (*.txt)|*.txt"
            if filename != undefined then
            (
                local output_file = createfile filename
                try
                (
                    for s in self.sequences do
                    (
                        local dataStr = "Name:%; loop:%; Rarity:%; Movespeed:%; start:%; end:%\n"
                        format dataStr s.name s.loop s.Rarity s.Movespeed s.starttime s.endtime to:output_file
                    )
                    close output_file
                    local msg = self.sequences.count as string + " animations have been saved in: " + filename
                    print msg
                    messagebox msg title:(::WdxL.t "seq_sequence_names_save_success_ptitle")
                )
                catch
                (
                    try (close output_file) catch()
                    messageBox (getCurrentException ())
                )
            )
        )
        
		-- Pop Out / Dock
		on popOutBtn pressed do
		(
			if ::WdxSequenceManager.isPoppedOut then
			(
				::WdxSequenceManager.isPoppedOut = false
				try ( destroyDialog mainRollout ) catch()
				
				if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
				(
					local rollouts = ::whiteoutdexManagerFloater.rollouts
					local idx = ::WdxSequenceManager.savedIndex
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
					if rollouts[i] == mainRollout then ( ::WdxSequenceManager.savedIndex = i; exit )
				removeRollout mainRollout ::whiteoutdexManagerFloater
			)
			
			::WdxSequenceManager.isPoppedOut = true
			createDialog mainRollout width:444 height:402 style:#(#style_titlebar, #style_sysmenu, #style_minimizebox, #style_resizing)
		)

        on mainRollout open do
        (
            self = WdxSequenceManager
			self.sequences = self.LoadSequences()
			self.loadIgnoredSequences()  -- Ignore-Liste laden
			self.scanGlobalSequences()   -- Global Sequences aus Szene scannen
			self.saveSequences()
			self.checkSequenceOverlapping()
            
            setListItems()
            
            callbacks.removeScripts id:#WhiteoutDex
            
            callbacks.addScript #filePostOpen sceneChangeCallback id:#WhiteoutDex
            callbacks.addScript #systemPostReset sceneChangeCallback id:#WhiteoutDex
            callbacks.addScript #systemPostNew sceneChangeCallback id:#WhiteoutDex
			
			callbacks.addScript #sceneUndo listUpdateAfterUndoRedo id:#WhiteoutDex
			callbacks.addScript #sceneRedo listUpdateAfterUndoRedo id:#WhiteoutDex

			-- Localization
			if ::WdxL != undefined then
			(
				mainRollout.title = ::WdxL.t "seq_sequence_manager_title"
				filterClearBtn.text = ::WdxL.t "seq_clear_btn"
				filterClearBtn.tooltip = ::WdxL.t "seq_clear_the_search_field_and_sho_tip"
				selectAllBtn.text = ::WdxL.t "seq_all_btn"
				selectAllBtn.tooltip = ::WdxL.t "seq_select_all_sequences_in_the_li_tip"
				selectNoneBtn.text = ::WdxL.t "seq_none_btn"
				selectNoneBtn.tooltip = ::WdxL.t "seq_deselect_all_sequences_tip"
				selectInvertBtn.text = ::WdxL.t "seq_invert_btn"
				selectInvertBtn.tooltip = ::WdxL.t "seq_invert_the_current_selection_tip"
				sequenceNewBtn.text = ::WdxL.t "seq_new_btn"
				sequenceNewBtn.tooltip = ::WdxL.t "seq_create_a_new_animation_sequenc_tip"
				sequenceDeleteBtn.text = ::WdxL.t "seq_delete_selected_btn"
				sequenceDeleteBtn.tooltip = ::WdxL.t "seq_delete_selected_sequences_tip"
				duplicateBtn.text = ::WdxL.t "seq_duplicate_btn"
				duplicateBtn.tooltip = ::WdxL.t "seq_duplicate_selected_sequences_tip"
				globalsBtn.text = ::WdxL.t "seq_globals_btn"
				globalsBtn.tooltip = ::WdxL.t "seq_open_the_global_sequence_manag_tip"
				nameLbl.text = ::WdxL.t "seq_name_lbl_2"
				nameEdt.tooltip = ::WdxL.t "seq_edit_name_of_the_selected_sequ_tip"
				startFrameLbl.text = ::WdxL.t "seq_start_frame_lbl_2"
				setStartFrameBtn.tooltip = ::WdxL.t "seq_set_current_frame_as_start_fra_tip"
				endFrameLbl.text = ::WdxL.t "seq_end_frame_lbl_2"
				setEndFrameBtn.tooltip = ::WdxL.t "seq_set_current_frame_as_end_frame_tip"
				rarityChk.text = ::WdxL.t "seq_rarity_chk_2"
				rarityChk.tooltip = ::WdxL.t "seq_enabledisable_rarity_of_the_a_tip_2"
				movespeedChk.text = ::WdxL.t "seq_move_speed_chk_2"
				movespeedChk.tooltip = ::WdxL.t "seq_enabledisable_movement_speed_tip_2"
				nonloopingChk.text = ::WdxL.t "seq_non_looping_chk_2"
				nonloopingChk.tooltip = ::WdxL.t "seq_animation_will_not_loop_when_p_tip"
				ignoreChk.text = ::WdxL.t "seq_ignore_chk"
				ignoreChk.tooltip = ::WdxL.t "seq_if_this_checkbox_is_enabled_t_tip"
				nudgeKeysStartLbl.text = ::WdxL.t "seq_start_lbl"
				nudgeKeysEndLbl.text = ::WdxL.t "seq_end_lbl"
				nudgeKeysStartDownBtn.tooltip = ::WdxL.t "seq_move_the_start_frame_of_the_se_tip"
				nudgeKeysStartUpBtn.tooltip = ::WdxL.t "seq_move_the_start_frame_of_the_se_tip_2"
				nudgeKeysEndDownBtn.tooltip = ::WdxL.t "seq_move_the_end_frame_of_the_sele_tip"
				nudgeKeysEndUpBtn.tooltip = ::WdxL.t "seq_move_the_end_frame_of_the_sele_tip_2"
				fileLoadBtn.text = ::WdxL.t "seq_load_btn"
				fileLoadBtn.tooltip = ::WdxL.t "seq_load_sequences_from_a_file_tip"
				fileSaveBtn.text = ::WdxL.t "seq_save_btn"
				fileSaveBtn.tooltip = ::WdxL.t "seq_save_sequences_to_a_file_tip"
			)
        )
        
        on mainRollout close do
        (
			if ::WdxSequenceManager.isPoppedOut then
			(
				::WdxSequenceManager.isPoppedOut = false
				local tmr = dotNetObject "System.Windows.Forms.Timer"
				tmr.Interval = 1
				dotNet.addEventHandler tmr "Tick" (fn _dockTmr s e =
				(
					s.Stop(); s.Dispose()
					try
					(
						if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
						(
							local rl = ::WdxSequenceManager.mainRollout
							local idx = ::WdxSequenceManager.savedIndex
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
            callbacks.removeScripts id:#WhiteoutDex
            self.saveSequences()
			self.saveIgnoredSequences()  -- Ignore-Liste speichern
        )
    ),
    
    fn showUI =
    (
        if not this.mainRollout.isDisplayed then CreateDialog this.mainRollout
    ),
    
    fn hideUI =
    (
        if this.mainRollout.inDialog then DestroyDialog this.mainRollout
    ),
    
    fn toggleUI =
    (
        if not this.mainRollout.isDisplayed then this.showUI() else this.hideUI()
    ),
    
    on create do
    (
        print "SequenceManager instanced."
    )
)
global WdxSequenceManager = WdxSequenceManager()
