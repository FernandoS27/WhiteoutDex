struct WdxSequenceManager
(
    isPoppedOut = false,
    savedIndex = undefined,
    sequences = #(),
    filteredSequences = #(),
	lastListSelection = #{},
	ignoredSequences = #(),  -- Separate list of ignored sequences
	globalSequences = #(),   -- Array of Global Sequence durations (as time values)
	caSignature = undefined, -- Fingerprint of the last CA data read or written

    -- ============================================================================
    -- GLOBAL SEQUENCE FUNCTIONS
    -- ============================================================================

    -- Scans every object in the scene for controllers with ORT #cycle and
    -- collects their durations (time of the last keyframe) as Global Sequences.
    -- Covers: transform controllers, visibility controllers, material alpha,
    --         UV offset/rotation/scale controllers, and IFL texture animations.
    
    -- Checks a single controller for ORT #cycle and collects its duration
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
    
    -- Checks whether a BitmapTexture is an IFL file and computes its Global Sequence duration.
    -- Same formula as Wc3Animation.GetTextureAnimation:
    --   n = round(1/playback), Duration = n * fileCount
    -- Same file counting as SceneParser.GetImageArray (enumerateFiles).
    fn checkIFLForGlobalSeq tex &durations =
    (
        try (
            if tex == undefined or classOf tex != BitmapTexture then return()
            
            -- For IFL textures tex.fileName gives the path to the .ifl file
            -- (tex.bitmap.fileName would be the current frame and can be undefined)
            local texPath = tex.fileName
            if texPath == undefined or texPath.count < 5 then return()
            
            -- Check for the .ifl extension
            if stricmp (getFilenameType texPath) ".ifl" != 0 then return()
            
            -- Count the referenced image files (identical to SceneParser.GetImageArray)
            fn _collectName name arr = append arr name
            local files = #()
            enumerateFiles tex _collectName files
            local iflIdx = findItem files texPath
            if iflIdx != 0 then deleteItem files iflIdx
            
            local fileCount = files.count
            if fileCount < 2 then return()
            
            -- Compute the duration (identical to Wc3Animation.GetTextureAnimation)
            local playback = tex.playbackrate
            if playback == undefined or playback == 0 then playback = 1.0
            local n = (::WdxUtils.round (1.0 / playback)) as integer
            if n == 0 then n = 1
            local duration = (n * fileCount) as time
            if duration > 0f then
                appendIfUnique durations duration
        ) catch()
    ),
    
    -- Scans a material recursively (including sub-materials in Composite/Multi)
    -- and collects all Global Sequence durations.
    fn checkMaterialForGlobalSeq mat &durations &scannedMaterials =
    (
        if mat == undefined then return()
        if (findItem scannedMaterials mat) != 0 then return()
        append scannedMaterials mat
        
        -- Legacy StandardMaterial alpha (usually undefined on a Wc3Material)
        try (this.checkControllerForGlobalSeq mat.alpha.controller &durations) catch()

        -- Wc3Material's own animatable properties (only Opacity/KMTA now;
        -- UV animation/TXAN lives on the Wc3Bitmap texture these days)
        try (this.checkControllerForGlobalSeq mat.opacity.controller &durations) catch()

        -- Resolve the bitmap: .texture first (StandardMaterial), then .diffuseMap
        -- (Wc3Material). Accepts BitmapTexture AND Wc3Bitmap - on the scripted
        -- plugin the property accesses (coords etc.) delegate transparently to
        -- the Bitmaptexture underneath.
        fn isUVBitmap tex = (
            local cn = try ((classOf tex) as string) catch("")
            cn == "BitmapTexture" or cn == "Wc3Bitmap"
        )
        local bmp = undefined
        try (if mat.texture != undefined and isUVBitmap mat.texture then bmp = mat.texture) catch()
        if bmp == undefined do (
            try (if mat.diffuseMap != undefined and isUVBitmap mat.diffuseMap then bmp = mat.diffuseMap) catch()
        )

        -- Bitmap UV controllers (on a Wc3Bitmap the anim_* params share their
        -- controllers with coords - scanning coords is enough; anim_WOffset
        -- has no coords counterpart and is checked separately).
        -- CAUTION: scripted plugins do NOT delegate unknown properties -
        -- a Wc3Bitmap's coords/playbackrate are only reachable via .delegate.
        if bmp != undefined do
        (
            local uvBmp = bmp
            try (if bmp.delegate != undefined and classOf bmp.delegate == BitmapTexture do uvBmp = bmp.delegate) catch()
            try (
                local coords = uvBmp.coords
                this.checkControllerForGlobalSeq coords.U_offset.controller &durations
                this.checkControllerForGlobalSeq coords.V_offset.controller &durations
                this.checkControllerForGlobalSeq coords.W_angle.controller  &durations
                this.checkControllerForGlobalSeq coords.U_tiling.controller &durations
                this.checkControllerForGlobalSeq coords.V_tiling.controller &durations
            ) catch()
            try (this.checkControllerForGlobalSeq bmp.anim_WOffset.controller &durations) catch()

            -- IFL texture animation (always produces a Global Sequence)
            try (this.checkIFLForGlobalSeq uvBmp &durations) catch()
        )
        
        -- RECURSION: MultiMaterial
        try (
            if classOf mat == MultiMaterial then (
                for sm in mat.materialList do
                    this.checkMaterialForGlobalSeq sm &durations &scannedMaterials
            )
        ) catch()
        
        -- RECURSION: Composite / Blend / Shellac / Wc3Material sub-slots / ...
        try (
            local n = getNumSubMtls mat
            if n > 0 do (
                for i = 1 to n do (
                    local sm = getSubMtl mat i
                    if sm != undefined do
                        this.checkMaterialForGlobalSeq sm &durations &scannedMaterials
                )
            )
        ) catch()
    ),
    
    fn scanGlobalSequences =
    (
        local durations = #()
        local scannedMaterials = #()
        
        for obj in objects do
        (
            -- Object-Level Controller (Transform + Visibility)
            try (this.checkControllerForGlobalSeq obj.pos.controller        &durations) catch()
            try (this.checkControllerForGlobalSeq obj.rotation.controller   &durations) catch()
            try (this.checkControllerForGlobalSeq obj.scale.controller      &durations) catch()
            try (this.checkControllerForGlobalSeq obj.visibility.controller &durations) catch()
            
            -- Scan the modifier stack: Wc3VertexMod.VertexColor (KGAC tracks)
            try
            (
                for m in obj.modifiers do
                (
                    local mClass = (classOf m) as string
                    if mClass == "Wc3VertexMod" or mClass == "Wdx_Wc3VertexMod" do
                    (
                        try (this.checkControllerForGlobalSeq m.VertexColor.controller &durations) catch()
                    )
                )
            ) catch()
            
            -- Scan the material recursively
            local mat = undefined
            try (mat = obj.material) catch()
            if mat != undefined do
                this.checkMaterialForGlobalSeq mat &durations &scannedMaterials
        )
        
        -- Also cover free-standing scene materials
        try (
            for m in sceneMaterials do
                this.checkMaterialForGlobalSeq m &durations &scannedMaterials
        ) catch()
        
        sort durations
        this.globalSequences = durations
    ),

    -- ============================================================================
    -- IGNORED SEQUENCE FUNCTIONS
    -- ============================================================================

    -- Function to check whether a sequence is ignored
    fn isSequenceIgnored sequenceName =
    (
        (findItem this.ignoredSequences sequenceName) != 0
    ),

    -- Function to add/remove ignored sequences
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

    -- Save the ignore list to User Properties
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

    -- Load the ignore list from User Properties
    fn loadIgnoredSequences =
    (
        local ignoreStr = try ( getUserProp rootNode "WhiteoutDex_IgnoredSequences" ) catch ( undefined )
        this.ignoredSequences = #()
        if ignoreStr != undefined and ignoreStr != "" then
        (
            this.ignoredSequences = filterString ignoreStr ";"
        )
    ),

    -- Function for the exporter: return the filtered sequences
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
		
    -- Functions for working with sequences
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
        
        -- Check for duplicate sequence names and delete keys with no matching second key
        -- Iterate backwards: deleteItem shifts the indices, so going forwards skips entries
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
        
        -- Sort by start time (first key)
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
        
        result -- return value
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
        WdxWc3Sequence \ --animName key.time key2.time NoLoop Rarity Movespeed undefined undefined
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

		-- Step 1: gather every value and count its occurrences
		local valueCount = #()  -- Array of #(value, count) pairs

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

		-- Step 2: build the list of keys to delete
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

		-- Step 3: delete the keys (backwards, to avoid index problems)
		for counter = idsToDelete.count to 1 by -1 do
		(
			deleteNoteKey nt.keys idsToDelete[counter]
		)

		if idsToDelete.count > 0 then
		(
			print ("Cleaned " + idsToDelete.count as string + " invalid note keys")
		)
	),
	
    -- Cheap fingerprint of the sequence data currently stored on the rootNode.
    -- LoadSequences records it, saveSequences compares against it: a mismatch
    -- means something outside this dialog (the MDX importer, the rebuilder, a
    -- user script) replaced the scene's sequences while it was open, so the
    -- in-memory list is stale and must not be written back over the new data.
    fn caSignatureNow =
    (
        local sig = "-"
        try
        (
            if ::WdxSequenceStorage != undefined and ::WdxSequenceStorage.hasCA() then
            (
                local names = rootNode.seqNames
                if names != undefined then
                (
                    sig = names.count as string
                    for i = 1 to names.count do
                        sig += "|" + (names[i] as string) \
                            + ":" + (rootNode.startFrames[i] as string) \
                            + "-" + (rootNode.endFrames[i] as string)
                )
            )
            else if (HasNoteTracks rootNode) then
                sig = "nt" + ((getNoteTrack rootNode 1).keys.count as string)
        )
        catch ( sig = "?" )
        sig
    ),

    -- Re-read the scene's sequences into this dialog and refresh the list.
    -- Deliberately does NOT call saveSequences() - the scene is the source of
    -- truth here, and writing back would defeat the purpose.
    fn refreshFromScene =
    (
        this.sequences = this.LoadSequences()
        this.scanGlobalSequences()
        try ( this.mainRollout.setListItems() ) catch ()
        this.sequences.count
    ),

    fn LoadSequences =
    (
        -- Remember what the scene held at the moment we read it, so a later
        -- saveSequences() can tell whether it still owns this data.
        this.caSignature = this.caSignatureNow()

        -- Primary: Read from WhiteoutDex Custom Attributes
        if ::WdxSequenceStorage != undefined and ::WdxSequenceStorage.hasCA() then
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
                                    ext = WdxWc3Extent \
                                        minExtent:[(parts[2] as float), (parts[3] as float), (parts[4] as float)] \
                                        maxExtent:[(parts[5] as float), (parts[6] as float), (parts[7] as float)] \
                                        bound:(parts[1] as float)
                                )
                            )
                        ) catch ()
                        append result (WdxWc3Sequence \
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
        -- Refuse to overwrite sequence data that changed behind our back.
        -- Importing an MDX with this dialog open used to leave the model with no
        -- sequences at all: the importer wrote the new ones straight into the
        -- rootNode CA, the dialog kept showing (and holding) the old list, and
        -- closing it triggered a save that wiped the CA and re-appended the
        -- stale entries. Reload instead - the scene wins.
        if this.caSignature != undefined and (this.caSignatureNow()) != this.caSignature then
        (
            format "** WDX saveSequences: scene sequences changed externally - reloading instead of overwriting\n"
            this.refreshFromScene()
            return ok
        )

        -- Remove legacy NoteTracks if present
        while (HasNoteTracks Rootnode) do deleteNoteTrack Rootnode (getNoteTrack Rootnode 1)
        
        -- Save to Custom Attributes (primary storage)
        try
        (
            ::WdxSequenceStorage.removeCA()
            ::WdxSequenceStorage.ensureCA()
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
                -- Reforged v1200: store the Shared Group info
                append rootNode.sharedGroup (if seq.sharedGroup != undefined then seq.sharedGroup else "")
            )
            -- This dialog is now in sync with the scene again.
            this.caSignature = this.caSignatureNow()

            -- ...and so is a running preview. Its Animation dropdown is a view
            -- of these very ranges, so an edit made here has to reach it or the
            -- combo keeps offering sequences the timeline no longer has. Silent
            -- when the previewer is not up, which is the common case, and
            -- silent too on a build whose .dlx predates WhiteoutFlakesIsRunning.
            try
            (
                if (WhiteoutFlakesIsRunning()) do
                    WdxPushRendererSequencesFrom this.sequences
            )
            catch()
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
        local newSequence = WdxWc3Sequence sequenceName starttime endtime noLoop rarity movespeed undefined undefined
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
    
    -- An MDX import (or a merge) writes the sequence CA directly. Only reload:
    -- saving here would push this dialog's stale list back over the fresh data.
    fn importChangeCallback =
    (
        ::WdxSequenceManager.refreshFromScene()
        ::WdxSequenceManager.checkSequenceOverlapping()
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
            
            -- Remember the current selection before reloading
            local currentSelection = this.mainRollout.sequencesLbox.selection
            
            -- Reload the sequences
            this.sequences = this.LoadSequences()
			this.scanGlobalSequences()
            this.saveSequences()
            this.checkSequenceOverlapping()
            
            -- Refresh the filtered list
            this.mainRollout.setListItems()
            
            -- Special handling for the various undo types
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
                    -- For every other case, restore the selection
                    this.mainRollout.sequencesLbox.selection = currentSelection
                )
            )
            
            -- IMPORTANT: trigger selectionEnd to refresh the UI fields
            this.mainRollout.sequencesLbox.selectionEnd()
        )
    ),
	
	fn getSequenceBounds seq =
	(
		local minStart = 0
		local maxEnd = 999999
		
		-- Sort all the other sequences by time
		local otherSequences = for s in this.sequences where s != seq collect s
		
		-- Find the nearest sequence before this one
		local prevSeq = undefined
		for s in otherSequences where s.endtime <= seq.starttime do
		(
			if prevSeq == undefined or s.endtime > prevSeq.endtime then
				prevSeq = s
		)
		
		-- Find the nearest sequence after this one
		local nextSeq = undefined
		for s in otherSequences where s.starttime >= seq.endtime do
		(
			if nextSeq == undefined or s.starttime < nextSeq.starttime then
				nextSeq = s
		)
		
		-- Set the bounds based on the neighbouring sequences
		if prevSeq != undefined then
			minStart = (prevSeq.endtime.frame as integer) + 1
		
		if nextSeq != undefined then
			maxEnd = (nextSeq.starttime.frame as integer) - 1
		
		#(minStart, maxEnd)
	),

	-- New function: validates and adjusts a frame value
    fn validateFrameForSequence seq newStartFrame newEndFrame =
    (
        local validStart = newStartFrame
        local validEnd = newEndFrame
        
        -- If seq is undefined, check against ALL sequences (for new sequences)
        if seq == undefined then
        (
            -- Check whether the new range collides with existing sequences
            for s in this.sequences do
            (
                -- Check whether the start frame falls inside an existing sequence
                if newStartFrame >= s.starttime.frame and newStartFrame <= s.endtime.frame then
                (
                    -- Move the start past that sequence
                    validStart = (s.endtime.frame as integer) + 1
                )
                
                -- Check whether the end frame falls inside an existing sequence
                if newEndFrame >= s.starttime.frame and newEndFrame <= s.endtime.frame then
                (
                    -- Move the end before that sequence
                    validEnd = (s.starttime.frame as integer) - 1
                )
                
                -- Check whether the new sequence completely encloses an existing one
                if newStartFrame < s.starttime.frame and newEndFrame > s.endtime.frame then
                (
                    -- In that case, put the end before the existing sequence
                    validEnd = (s.starttime.frame as integer) - 1
                )
            )
        )
        else
        (
            -- Original code path for existing sequences
            local bounds = this.getSequenceBounds seq
            
            -- Make sure the start does not fall before the allowed bound
            if validStart < bounds[1] then
                validStart = bounds[1]
            
            -- Make sure the end does not fall after the allowed bound
            if validEnd > bounds[2] then
                validEnd = bounds[2]
        )
        
        -- Make sure start < end
        if validStart >= validEnd then
        (
            -- Keep the original values when no valid range is possible
            if seq != undefined then
                return #(seq.starttime.frame as integer, seq.endtime.frame as integer)
            else
            (
                -- For new sequences: try the smallest possible adjustment
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
        

        -- Replaces the whole newSequenceRollout with this more compact version:
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
                
                -- Preset definitions (corrected syntax)
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
                
                -- Helper: checks whether a range collides with existing sequences
                fn isRangeFree startFrame endFrame =
                (
                    for seq in self.sequences do
                    (
                        -- Check whether there is an overlap
                        if not (endFrame < seq.starttime.frame or startFrame > seq.endtime.frame) then
                            return false
                    )
                    return true
                )
                
                -- Function that applies a preset
                fn applyPreset presetIndex =
                (
                    if presetIndex > 0 and presetIndex <= presetNames.count then
                    (
                        -- Set the name (only when not Custom)
                        if presetIndex < 10 then
                        (
                            nameEdt.text = presetNames[presetIndex]
                        )
                        
                        -- Derive the start from the last sequence
                        local newStart = 10  -- ALWAYS start at 10 when there are no sequences
                        if self.sequences.count > 0 then
                        (
                            local highestFrame = amax (for seq in self.sequences collect seq.endtime.frame as integer)
                            newStart = highestFrame + presetStartOffsets[presetIndex]
                        )
                        
                        -- Set the frames (WITHOUT the -1!)
                        startFrameSpn.value = newStart
                        endFrameSpn.value = newStart + presetDurations[presetIndex]  -- No more -1!
                        lastValidStart = newStart
                        lastValidEnd = newStart + presetDurations[presetIndex]
                        
                        -- Set Non-Looping
                        nonloopingChk.checked = presetNonLooping[presetIndex]
                        
                        -- Set Rarity when the preset defines it
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
                        
                        -- Set Move Speed when the preset defines it
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
                        format "%\n" (substituteString (substituteString (::WdxL.t "seq_start_frame_adjusted_msg") "%1" (val as string)) "%2" (validated[1] as string))
                    
                    startFrameSpn.value = validated[1]
                    lastValidStart = validated[1]
                )
                
                on endFrameSpn changed val do
                (
                    local validated = self.validateFrameForSequence undefined startFrameSpn.value val
                    
                    if validated[2] != val then
                        format "%\n" (substituteString (substituteString (::WdxL.t "seq_end_frame_adjusted_msg") "%1" (val as string)) "%2" (validated[2] as string))
                    
                    endFrameSpn.value = validated[2]
                    lastValidEnd = validated[2]
                )
                
                on rarityChk changed state do raritySpn.enabled = state
                
                on movespeedChk changed state do movespeedSpn.enabled = state
                
                on okBtn pressed do
                (
                    -- Final validation through the original function, for safety
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
                    -- Toolkit icon on the dialog frame (WhiteoutDexUtility.ms).
                    try (WdxSetDialogIcon newSequenceRollout) catch()
                    self = WdxSequenceManager
                    parent = mainRollout
                    
                    -- Set the initial name for Custom
                    nameEdt.text = "New Sequence"
                    
                    -- Select and apply "Custom" by default
                    presetDDL.selection = 10
                    
                    -- ALWAYS insert after the last sequence!
                    if self.sequences.count > 0 then
                    (
                        -- Find the highest end frame across all sequences
                        local highestFrame = amax (for seq in self.sequences collect seq.endtime.frame as integer)
                        
                        -- Put the start 10 frames after the last sequence
                        startFrameSpn.value = highestFrame + 10
                        endFrameSpn.value = highestFrame + 60  -- 50 frames duration for Custom
                        lastValidStart = highestFrame + 10
                        lastValidEnd = highestFrame + 60
                    )
                    else
                    (
                        -- No sequences yet, start at frame 10
                        startFrameSpn.value = 10
                        endFrameSpn.value = 60   -- 50 frames duration (10-60)
                        lastValidStart = 10
                        lastValidEnd = 60
                    )

                    -- Localization
                    if ::WdxL != undefined then
                    (
                        newSequenceRollout.title = ::WdxL.t "seq_new_sequence_title"
                        presetLbl.text = ::WdxL.t "seq_preset_lbl"
                        try (presetDDL.tooltip = ::WdxL.t "seq_select_animation_preset_tip") catch ()
                        nameLbl.text = ::WdxL.t "seq_name_lbl"
                        try (nameEdt.tooltip = ::WdxL.t "seq_enter_the_name_of_the_new_sequ_tip") catch ()
                        startFrameLbl.text = ::WdxL.t "seq_start_frame_lbl"
                        try (startFrameSpn.tooltip = ::WdxL.t "seq_set_the_start_frame_of_the_seq_tip") catch ()
                        endFrameLbl.text = ::WdxL.t "seq_end_frame_lbl"
                        try (endFrameSpn.tooltip = ::WdxL.t "seq_set_the_end_frame_of_the_seque_tip") catch ()
                        rarityChk.text = ::WdxL.t "seq_rarity_chk"
                        try (rarityChk.tooltip = ::WdxL.t "seq_enabledisable_rarity_of_the_a_tip") catch ()
                        movespeedChk.text = ::WdxL.t "seq_move_speed_chk"
                        try (movespeedChk.tooltip = ::WdxL.t "seq_enabledisable_movement_speed_tip") catch ()
                        nonloopingChk.text = ::WdxL.t "seq_non_looping_chk"
                        okBtn.text = ::WdxL.t "seq_ok_btn"
                        try (okBtn.tooltip = ::WdxL.t "seq_create_new_sequence_and_close_tip") catch ()
                        cancelBtn.text = ::WdxL.t "seq_cancel_btn"
                        try (cancelBtn.tooltip = ::WdxL.t "seq_cancel_without_creating_a_sequ_tip") catch ()
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
        -- Event handlers for the new buttons
        on setStartFrameBtn pressed do
		(
			if sequencesLbox.selection.numberSet == 1 then
			(
				local selId = (sequencesLbox.selection as array)[1]
				-- Global Sequences are not editable
				if selId <= self.globalSequences.count then return()
				
				undo "SequenceManager Set Current Frame as Start" on
				(
					local newVal = currentTime.frame as integer
					local filteredId = selId - self.globalSequences.count
					local seq = self.filteredSequences[filteredId]
					if seq != undefined then
					(
						-- Validation with overlap protection
						local validated = self.validateFrameForSequence seq newVal (seq.endtime.frame as integer)
						
						if validated[1] != newVal then
						(
							format "%\n" (substituteString (substituteString (::WdxL.t "seq_start_frame_adjusted_msg") "%1" (newVal as string)) "%2" (validated[1] as string))
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
				-- Global Sequences are not editable
				if selId <= self.globalSequences.count then return()
				
				undo "SequenceManager Set Current Frame as End" on
				(
					local newVal = currentTime.frame as integer
					local filteredId = selId - self.globalSequences.count
					local seq = self.filteredSequences[filteredId]
					if seq != undefined then
					(
						-- Validation with overlap protection
						local validated = self.validateFrameForSequence seq (seq.starttime.frame as integer) newVal
						
						if validated[2] != newVal then
						(
							format "%\n" (substituteString (substituteString (::WdxL.t "seq_end_frame_adjusted_msg") "%1" (newVal as string)) "%2" (validated[2] as string))
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
		-- GLOBAL SEQUENCE HELPER FUNCTIONS (rollout-local)
		-- ============================================================================
		
		-- Checks whether a listbox index points at a Global Sequence
		fn isGlobalListId id = (id > 0 and id <= self.globalSequences.count)
		
		-- Converts a listbox index into a filteredSequences index
		-- Returns 0 if it is a global index or otherwise invalid
		fn toFilteredId id =
		(
			local fid = id - self.globalSequences.count
			if fid >= 1 and fid <= self.filteredSequences.count then fid else 0
		)
		
		-- Fetches the sequence for a listbox index, undefined for globals
		fn seqFromListId id =
		(
			local fid = toFilteredId id
			if fid > 0 then self.filteredSequences[fid] else undefined
		)
		
		-- Fetches the index into self.sequences for a listbox index, 0 for globals/invalid
		fn seqIndexFromListId id =
		(
			local fid = toFilteredId id
			if fid > 0 then findItem self.sequences self.filteredSequences[fid] else 0
		)
		
		-- Checks whether the current selection contains nothing but globals
		fn selectionIsOnlyGlobals =
		(
			local gc = self.globalSequences.count
			for id in sequencesLbox.selection where id > gc do return false
			true
		)
		
		-- Puts every edit field into the disabled state (for globals or an empty selection)
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
		
		-- Re-enables the edit fields for normal sequences
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
			-- raritySpn and movespeedSpn are driven separately by their own checkboxes
		)
		
		-- ============================================================================
        
        fn setListItems =
        (
			-- Always show Global Sequences at the top (independent of the filter)
			local globalItems = for i = 1 to self.globalSequences.count collect 
			(
				local dur = self.globalSequences[i].frame as integer
				"[GLOBAL " + i as string + "]  0 - " + dur as string
			)
			
			-- Filter the normal sequences
            self.filteredSequences = for sequence in self.sequences where findString sequence.name filterTxt.text != undefined collect sequence
			local normalItems = for filteredSequence in self.filteredSequences collect (
				if self.isSequenceIgnored filteredSequence.name then 
					"[IGNORED] " + filteredSequence.name 
				-- Reforged v1200: show the shared alias (only when name != master name)
				else if filteredSequence.sharedGroup != undefined and \
				        filteredSequence.sharedGroup != filteredSequence.name then
					"[Shared: " + filteredSequence.sharedGroup + "] " + filteredSequence.name
				else 
					filteredSequence.name
			)
			
			-- Combine: globals on top, then the normal sequences
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

					-- Helper: copies the keys of an animatable property via the property setter.
					-- Writing to the controller directly (copyPasteKeys / addNewKey + key.value)
					-- bypasses the on-set handler of scripted plugins AND the SetValue
					-- notifications of native ParamBlock2 plugins. The setProperty +
					-- animate-on + at-time path necessarily goes through the setter and
					-- fires every sync notification, exactly as the UI spinner does.
					fn copyParamKeysViaSetter target paramName srcInterval offset =
					(
						if target == undefined or paramName == undefined then return false
						local hasProp = false
						try ( hasProp = isProperty target paramName ) catch()
						if not hasProp then return false
						
						-- Find the controller via subAnim, NOT through (getProperty x).controller -
						-- getProperty returns the value (a float), which has no controller.
						local con = undefined
						try
						(
							local nSubs = target.numSubs
							for sIdx = 1 to nSubs while con == undefined do
							(
								try
								(
									local nmStr = (getSubAnimName target sIdx) as string
									if nmStr.count > 0 and nmStr[1] == "#" then nmStr = substring nmStr 2 -1
									if (stricmp nmStr paramName) == 0 do
									(
										local sa = target[sIdx]
										if sa != undefined do con = sa.controller
									)
								) catch()
							)
						) catch()
						
						if con == undefined or con.keys == undefined or con.keys.count == 0 then return false
						
						-- Skip global (cycle) animations
						local isGlobal = false
						try ( isGlobal = (getAfterORT con == #cycle) ) catch()
						if isGlobal then return false
						
						-- Snapshot the source keys (time + value) before anything is written
						local snapTimes = #()
						local snapVals  = #()
						for k in con.keys where k.time >= srcInterval.start and k.time <= srcInterval.end do
						(
							append snapTimes k.time
							try ( append snapVals k.value ) catch ( append snapVals undefined )
						)
						if snapTimes.count == 0 then return false
						
						-- Write the values at the offset times via setProperty.
						-- "with animate on at time T" creates the key and fires the setter.
						for sIdx = 1 to snapTimes.count do
						(
							if snapVals[sIdx] != undefined do
							(
								try
								(
									with animate on
										at time (snapTimes[sIdx] + offset)
											setProperty target paramName snapVals[sIdx]
								) catch()
							)
						)
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
							
							-- Material: Wc3Material animatable params via the property setter.
							-- Iterative worklist (instead of recursion) through MultiMaterial / Composite / Blend / Shellac.
							try
							(
								local matWorklist = #()
								local visitedMats = #()
								local visitedUVCtrls = #()
								if obj.material != undefined do append matWorklist obj.material

								while matWorklist.count > 0 do
								(
									local mat = matWorklist[matWorklist.count]
									deleteItem matWorklist matWorklist.count

									if mat != undefined and (findItem visitedMats mat) == 0 do
									(
										append visitedMats mat

										-- Wc3Material params (KMTA; TXAN lives on the texture)
										local matParams = #(
											"opacity",
											"alpha"  -- Legacy StandardMaterial fallback
										)
										for paramName in matParams do
											copyParamKeysViaSetter mat paramName srcInterval timeOffset

										-- UV animation (TXAN): lives on the diffuse texture.
										-- Wc3Bitmap: the anim_* params (controllers shared with
										-- its own coords). Plain BitmapTexture: coords controllers.
										-- Controllers are deduplicated by identity so that shared
										-- controllers are not copied more than once.
										try
										(
											local tex = undefined
											try (if mat.diffuseMap != undefined do tex = mat.diffuseMap) catch()
											if tex == undefined do ( try (if mat.texture != undefined do tex = mat.texture) catch() )
											if tex != undefined do
											(
												-- Wc3Bitmap: coords live on the delegate (no property forwarding)
												local uvTex = tex
												try (if tex.delegate != undefined and classOf tex.delegate == BitmapTexture do uvTex = tex.delegate) catch()
												local uvCtrls = #()
												try (appendIfUnique uvCtrls tex.anim_UOffset.controller) catch()
												try (appendIfUnique uvCtrls tex.anim_VOffset.controller) catch()
												try (appendIfUnique uvCtrls tex.anim_WOffset.controller) catch()
												try (appendIfUnique uvCtrls tex.anim_WAngle.controller)  catch()
												try (appendIfUnique uvCtrls tex.anim_UTiling.controller) catch()
												try (appendIfUnique uvCtrls tex.anim_VTiling.controller) catch()
												try (appendIfUnique uvCtrls uvTex.coords.U_Offset.controller) catch()
												try (appendIfUnique uvCtrls uvTex.coords.V_Offset.controller) catch()
												try (appendIfUnique uvCtrls uvTex.coords.W_Angle.controller)  catch()
												try (appendIfUnique uvCtrls uvTex.coords.U_Tiling.controller) catch()
												try (appendIfUnique uvCtrls uvTex.coords.V_Tiling.controller) catch()
												for c in uvCtrls where c != undefined do
												(
													if (findItem visitedUVCtrls c) == 0 do
													(
														append visitedUVCtrls c
														copyControllerKeys c srcInterval timeOffset self.bumpTime
													)
												)
											)
										)
										catch()

										-- Push sub-materials onto the worklist
										try
										(
											if classOf mat == MultiMaterial do
												for sm in mat.materialList where sm != undefined do append matWorklist sm
										) catch()
										try
										(
											local n = getNumSubMtls mat
											if n > 0 do
												for i = 1 to n do
												(
													local sm = getSubMtl mat i
													if sm != undefined do append matWorklist sm
												)
										) catch()
									)
								)
							)
							catch()

							-- Plugin params: try every known animatable property in turn.
							-- Class-agnostic: try/catch makes unknown props harmless.
							-- Covers both the old (Wc3Particles1, Wdx_Wc3Light) and the new plugin
							-- variants (Wc3Particles1Emitter, Wc3Particles2Emitter, RibbonObj).
							try
							(
								local bo = obj.baseObject
								local pluginParams = #(
									-- Light (Wdx_Wc3Light / Wdx_Wc3Light)
									"DecayStart", "DecayEnd", "AmbColor", "AmbValue", "ShadowColor", "ShadowValue",
									"ShadowCastingStart", "ShadowCastingEnd", "QuadraticFalloff", "LinearFalloff", "Damping",
									-- AttachPoint (Wdx_Wc3AttachPoint)
									"A_Visibility",
									-- Wc3Particles1 (legacy)
									"radius", "PartEmit",
									-- Wc3Particles1Emitter (verified against the C++)
									"Speed", "EmissionRate", "Gravity", "Latitude", "Longitude",
									-- Wc3Particles2 (property names verified from subAnim):
									-- speed, variation, width, length, Parts_Sec, Cone_Angle, gravity
									-- (Speed and Gravity are already listed above)
									"Variation", "Parts_Sec", "Cone_Angle", "width", "length",
									-- Wc3Particles2 legacy size/scale tracks (if present)
									"startscale", "middlescale", "endscale", "taillength",
									-- BlizRibbon legacy
									"slots", "Above", "Below", "vertexcolor", "alpha",
									-- Wc3Ribbon / RibbonObj (verified against the C++)
									"Height Above", "Height Below", "Edges Per Second", "Edge Lifetime", "Color", "Texture Slot"
								)
								for paramName in pluginParams do
									copyParamKeysViaSetter bo paramName srcInterval timeOffset
							)
							catch()

							-- Modifier: Wc3VertexMod / Wdx_Wc3VertexMod (KGAC VertexColor)
							try
							(
								for m in obj.modifiers do
								(
									try
									(
										local mClass = (classOf m) as string
										if mClass == "Wc3VertexMod" or mClass == "Wdx_Wc3VertexMod" do
											copyParamKeysViaSetter m "VertexColor" srcInterval timeOffset
									) catch()
								)
							)
							catch()

							-- Wc3 RefEvent keyList (Frame tab, NOT controller keys)
							try
							(
								local boCls = (classOf obj.baseObject) as string
								if boCls == "Wdx_Wc3Event" or boCls == "Wdx_Wc3Event" or boCls == "Wc3_Event" do
								(
									try
									(
										local kl = obj.baseObject.keyList
										if kl != undefined and kl.count > 0 do
										(
											local startF = (srcInterval.start as integer) / ticksperframe
											local endF   = (srcInterval.end   as integer) / ticksperframe
											local offF   = (timeOffset       as integer) / ticksperframe
											for k in kl where k >= startF and k <= endF do
											(
												local nk = k + offF
												if (findItem obj.baseObject.keyList nk) == 0 do
													append obj.baseObject.keyList nk
											)
										)
									) catch()
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
			-- Double-click on a Global Sequence: ignore it (no playback)
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
            -- Optional code to run on listbox selection
        )
        
        on sequencesLbox selectionEnd do
		(
			local numSelected = sequencesLbox.selection.numberSet
			local gc = self.globalSequences.count
			
			-- ============================================================
			-- NOTHING SELECTED
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
			-- EXACTLY 1 ITEM SELECTED
			-- ============================================================
			if numSelected == 1 then
			(
				local seqId = (sequencesLbox.selection as array)[1]
				
				-- --- GLOBAL SEQUENCE SELECTED ---
				if seqId <= gc then
				(
					disableAllEditFields()
					
					local duration = self.globalSequences[seqId]
					nameEdt.text = "Global " + seqId as string
					startFrameSpn.value = 0
					endFrameSpn.value = duration.frame as integer
					
					-- Reset the ranges for the read-only display
					startFrameSpn.range = [0, 0, 0]
					endFrameSpn.range = [0, duration.frame as integer, duration.frame as integer]
					try (startFrameSpn.tooltip = "Global Sequence - read only") catch ()
					try (endFrameSpn.tooltip = "Global Sequence - read only") catch ()
					
					ignoreChk.triState = 0
					nonloopingChk.triState = 0
					rarityChk.triState = 0
					raritySpn.value = 0
					movespeedChk.triState = 0
					movespeedSpn.value = 0
				)
				-- --- NORMAL SEQUENCE SELECTED ---
				else
				(
					local seq = seqFromListId seqId
					if seq == undefined then return()
					
					enableEditFields()
					
					startFrameSpn.value = seq.starttime.frame as integer
					endFrameSpn.value = seq.endtime.frame as integer
					
					-- Set the allowed ranges for the spinners
					local bounds = self.getSequenceBounds seq
					
					-- StartFrame may sit anywhere between the previous sequence and the current EndFrame-1
					local maxStartFrame = (seq.endtime.frame as integer) - 1
					startFrameSpn.range = [bounds[1], maxStartFrame, seq.starttime.frame as integer]
					
					-- EndFrame may sit anywhere between the current StartFrame+1 and the next sequence
					local minEndFrame = (seq.starttime.frame as integer) + 1
					endFrameSpn.range = [minEndFrame, bounds[2], seq.endtime.frame as integer]
					
					try (startFrameSpn.tooltip = "Valid range: " + bounds[1] as string + " to " + maxStartFrame as string + " (limited by adjacent sequences)") catch ()
					try (endFrameSpn.tooltip = "Valid range: " + minEndFrame as string + " to " + bounds[2] as string + " (limited by adjacent sequences)") catch ()
					
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
			-- SEVERAL ITEMS SELECTED
			-- ============================================================
			if numSelected > 1 then
			(
				startFrameSpn.enabled = endFrameSpn.enabled = false
				setStartFrameBtn.enabled = setEndFrameBtn.enabled = false
				startFrameSpn.value = endFrameSpn.value = 0
				
				-- Reset the spinner ranges for a multi-selection
				startFrameSpn.range = [-999999, 999999, 0]
				endFrameSpn.range = [-999999, 999999, 0]
				try (startFrameSpn.tooltip = "Multiple sequences selected") catch ()
				try (endFrameSpn.tooltip = "Multiple sequences selected") catch ()
				
				-- If ONLY globals are selected, disable everything
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
					-- Mixed, or normal sequences only: edit fields for a multi-selection
					-- Re-enable the checkboxes (a previous global selection may have disabled them)
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
				-- Only non-global selections count towards the naming
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
                    -- Reforged v1200: break the shared link on rename
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
					
					-- New validation with overlap protection
					local validated = self.validateFrameForSequence seq val (seq.endtime.frame as integer)
					
					if validated[1] != val then
					(
						-- Warn when the value had to be adjusted
						format "%\n" (substituteString (substituteString (::WdxL.t "seq_start_frame_adjusted_msg") "%1" (val as string)) "%2" (validated[1] as string))
					)
					
					seq.starttime = validated[1] as time
					
					-- Update the spinner if the value was adjusted
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
					
					-- New validation with overlap protection
					local validated = self.validateFrameForSequence seq (seq.starttime.frame as integer) val
					
					if validated[2] != val then
					(
						-- Warn when the value had to be adjusted
						format "%\n" (substituteString (substituteString (::WdxL.t "seq_end_frame_adjusted_msg") "%1" (val as string)) "%2" (validated[2] as string))
					)
					
					seq.endtime = validated[2] as time
					
					-- Update the spinner if the value was adjusted
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
				setListItems()  -- Refresh the list so the marker shows up
			)
		)
		
		-- Shared nudge function: which = #start or #end, delta = 1 or -1
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
						::WdxErrorReporter.logWarning "Sequence" "nudge" ("Cannot nudge further - boundary reached at frame " + validated[valIdx] as string)
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
                        local SequenceData = WdxWc3Sequence sequenceName newStartTime newEndTime noLoop rarity movespeed undefined
                        
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
        
		-- File Save: only write the normal sequences, no Global Sequences
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
			createDialog mainRollout width:444 height:402 style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox, #style_maximizebox, #style_resizing)
		)

        on mainRollout open do
        (
            -- Toolkit icon on the dialog frame (WhiteoutDexUtility.ms).
            try (WdxSetDialogIcon mainRollout) catch()
            self = WdxSequenceManager
			self.sequences = self.LoadSequences()
			self.loadIgnoredSequences()  -- Load the ignore list
			self.scanGlobalSequences()   -- Scan the scene for Global Sequences
			self.saveSequences()
			self.checkSequenceOverlapping()
            
            setListItems()
            
            callbacks.removeScripts id:#WhiteoutDex
            
            callbacks.addScript #filePostOpen sceneChangeCallback id:#WhiteoutDex
            callbacks.addScript #systemPostReset sceneChangeCallback id:#WhiteoutDex
            callbacks.addScript #systemPostNew sceneChangeCallback id:#WhiteoutDex
			
			callbacks.addScript #sceneUndo listUpdateAfterUndoRedo id:#WhiteoutDex
			callbacks.addScript #sceneRedo listUpdateAfterUndoRedo id:#WhiteoutDex

			-- An import or merge replaces the scene's sequence data without going
			-- through this dialog. Pick the new data up right away so the list is
			-- correct and the close handler has nothing stale to write back.
			-- try/catch because not every Max version broadcasts every event.
			try ( callbacks.addScript #postImport importChangeCallback id:#WhiteoutDex ) catch ()
			try ( callbacks.addScript #filePostMerge importChangeCallback id:#WhiteoutDex ) catch ()

			-- Localization
			if ::WdxL != undefined then
			(
				mainRollout.title = ::WdxL.t "seq_sequence_manager_title"
				filterGrp.text = ::WdxL.t "seq_filter_list_grp"
				try (filterGrp.tooltip = ::WdxL.t "seq_options_for_filtering_the_sequ_tip") catch ()
				try (filterTxt.tooltip = ::WdxL.t "seq_enter_a_search_term_to_filter_tip") catch ()
				try (sequencesLbox.tooltip = ::WdxL.t "seq_list_of_all_available_animatio_tip") catch ()
				selectGrp.text = ::WdxL.t "seq_select_grp"
				try (selectGrp.tooltip = ::WdxL.t "seq_options_for_selecting_sequence_tip") catch ()
				sequenceGrp.text = ::WdxL.t "seq_sequence_grp"
				try (sequenceGrp.tooltip = ::WdxL.t "seq_options_for_creating_and_manag_tip") catch ()
				editSelectedGrp.text = ::WdxL.t "seq_edit_selected_grp"
				try (editSelectedGrp.tooltip = ::WdxL.t "seq_edit_properties_of_selected_se_tip") catch ()
				try (startFrameSpn.tooltip = ::WdxL.t "seq_set_the_start_frame_of_selecte_tip") catch ()
				try (endFrameSpn.tooltip = ::WdxL.t "seq_set_the_end_frame_of_selected_tip") catch ()
				try (raritySpn.tooltip = ::WdxL.t "seq_set_the_rarity_value_040_t_tip") catch ()
				try (movespeedSpn.tooltip = ::WdxL.t "seq_set_the_movement_speed_value_tip") catch ()
				nudgeKeysGrp.text = ::WdxL.t "seq_nudge_keys_grp"
				try (nudgeKeysGrp.tooltip = ::WdxL.t "seq_fine_adjustment_of_keyframes_tip") catch ()
				fileGrp.text = ::WdxL.t "seq_file_grp"
				try (fileGrp.tooltip = ::WdxL.t "seq_file_management_for_sequences_tip") catch ()
				filterClearBtn.text = ::WdxL.t "seq_clear_btn"
				try (filterClearBtn.tooltip = ::WdxL.t "seq_clear_the_search_field_and_sho_tip") catch ()
				selectAllBtn.text = ::WdxL.t "seq_all_btn"
				try (selectAllBtn.tooltip = ::WdxL.t "seq_select_all_sequences_in_the_li_tip") catch ()
				selectNoneBtn.text = ::WdxL.t "seq_none_btn"
				try (selectNoneBtn.tooltip = ::WdxL.t "seq_deselect_all_sequences_tip") catch ()
				selectInvertBtn.text = ::WdxL.t "seq_invert_btn"
				try (selectInvertBtn.tooltip = ::WdxL.t "seq_invert_the_current_selection_tip") catch ()
				sequenceNewBtn.text = ::WdxL.t "seq_new_btn"
				try (sequenceNewBtn.tooltip = ::WdxL.t "seq_create_a_new_animation_sequenc_tip") catch ()
				sequenceDeleteBtn.text = ::WdxL.t "seq_delete_selected_btn"
				try (sequenceDeleteBtn.tooltip = ::WdxL.t "seq_delete_selected_sequences_tip") catch ()
				duplicateBtn.text = ::WdxL.t "seq_duplicate_btn"
				try (duplicateBtn.tooltip = ::WdxL.t "seq_duplicate_selected_sequences_tip") catch ()
				globalsBtn.text = ::WdxL.t "seq_globals_btn"
				try (globalsBtn.tooltip = ::WdxL.t "seq_open_the_global_sequence_manag_tip") catch ()
				nameLbl.text = ::WdxL.t "seq_name_lbl_2"
				try (nameEdt.tooltip = ::WdxL.t "seq_edit_name_of_the_selected_sequ_tip") catch ()
				startFrameLbl.text = ::WdxL.t "seq_start_frame_lbl_2"
				try (setStartFrameBtn.tooltip = ::WdxL.t "seq_set_current_frame_as_start_fra_tip") catch ()
				endFrameLbl.text = ::WdxL.t "seq_end_frame_lbl_2"
				try (setEndFrameBtn.tooltip = ::WdxL.t "seq_set_current_frame_as_end_frame_tip") catch ()
				rarityChk.text = ::WdxL.t "seq_rarity_chk_2"
				try (rarityChk.tooltip = ::WdxL.t "seq_enabledisable_rarity_of_the_a_tip_2") catch ()
				movespeedChk.text = ::WdxL.t "seq_move_speed_chk_2"
				try (movespeedChk.tooltip = ::WdxL.t "seq_enabledisable_movement_speed_tip_2") catch ()
				nonloopingChk.text = ::WdxL.t "seq_non_looping_chk_2"
				try (nonloopingChk.tooltip = ::WdxL.t "seq_animation_will_not_loop_when_p_tip") catch ()
				ignoreChk.text = ::WdxL.t "seq_ignore_chk"
				try (ignoreChk.tooltip = ::WdxL.t "seq_if_this_checkbox_is_enabled_t_tip") catch ()
				nudgeKeysStartLbl.text = ::WdxL.t "seq_start_lbl"
				nudgeKeysEndLbl.text = ::WdxL.t "seq_end_lbl"
				try (nudgeKeysStartDownBtn.tooltip = ::WdxL.t "seq_move_the_start_frame_of_the_se_tip") catch ()
				try (nudgeKeysStartUpBtn.tooltip = ::WdxL.t "seq_move_the_start_frame_of_the_se_tip_2") catch ()
				try (nudgeKeysEndDownBtn.tooltip = ::WdxL.t "seq_move_the_end_frame_of_the_sele_tip") catch ()
				try (nudgeKeysEndUpBtn.tooltip = ::WdxL.t "seq_move_the_end_frame_of_the_sele_tip_2") catch ()
				fileLoadBtn.text = ::WdxL.t "seq_load_btn"
				try (fileLoadBtn.tooltip = ::WdxL.t "seq_load_sequences_from_a_file_tip") catch ()
				fileSaveBtn.text = ::WdxL.t "seq_save_btn"
				try (fileSaveBtn.tooltip = ::WdxL.t "seq_save_sequences_to_a_file_tip") catch ()
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
			self.saveIgnoredSequences()  -- Save the ignore list
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

-- ============================================================================
-- WdxSequenceManagerRefresh
--
-- Re-read the scene's sequences into the Sequence Manager. Called by the MDLX
-- importer at the end of an import: it writes the sequence Custom Attributes
-- itself, and an already-open Sequence Manager would otherwise keep showing the
-- previous model's list and write that list back over the new one when closed.
--
-- Safe to call at any time - a no-op if the manager was never instanced.
-- ============================================================================
global WdxSequenceManagerRefresh
fn WdxSequenceManagerRefresh =
(
    if ::WdxSequenceManager == undefined then 0
    else
    (
        local n = 0
        try ( n = ::WdxSequenceManager.refreshFromScene() )
        catch ( format "WDX SequenceManager refresh failed: %\n" (getCurrentException()) )
        n
    )
)

-- ============================================================================
-- WdxPushRendererSequencesFrom <sequences>
--
-- The push itself, against a list already in hand. Split out of
-- WdxPushRendererSequences below so an edit made in the Sequence Manager can
-- reach a running preview without a LoadSequences round-trip: saveSequences
-- calls this with the very array it just wrote, and reloading there would swap
-- the sequence objects out from under the dialog's own rollout handlers.
--
-- Returns the number of ranges the previewer accepted.
-- ============================================================================
global WdxPushRendererSequencesFrom
fn WdxPushRendererSequencesFrom seqs =
(
    if seqs == undefined then return 0
    local namesArr  = #()
    local startsArr = #()
    local endsArr   = #()
    for s in seqs do
    (
        try
        (
            append namesArr  (s.name as string)
            append startsArr (s.startTime as integer)
            append endsArr   (s.endTime   as integer)
        )
        catch
        (
            format "WDX Renderer:   skipped seq (decode failed): %\n" (getCurrentException())
        )
    )
    format "WDX Renderer: pushing % sequences via WhiteoutFlakesPushSequences\n" namesArr.count
    if namesArr.count > 0 then (WhiteoutFlakesPushSequences namesArr startsArr endsArr) else 0
)

-- ============================================================================
-- WdxPushRendererSequences
--
-- Hand the running previewer the scene's current sequence ranges. Called by
-- the Renderer macroscript right after WhiteoutFlakesStart, and again by the
-- previewer itself when its Resync button rebuilds the scene: the ranges live
-- on the MaxSceneAdapter, which a resync replaces, and without them the
-- popcorn animVisibilityGuide gate has no current-animation name to match
-- against so every emitter spawns on every sequence.
--
-- Re-reads the scene first (through refreshFromScene, which also rebuilds the
-- dialog's list) so it works even when the Sequence Manager UI has never been
-- opened this session. Returns the number of ranges pushed.
-- ============================================================================
global WdxPushRendererSequences
fn WdxPushRendererSequences =
(
    local pushed = 0
    if ::WdxSequenceManager == undefined then
        format "WDX Renderer: WdxSequenceManager is undefined - no sequences pushed\n"
    else
    (
        -- refreshFromScene, not a bare LoadSequences. The load mints brand new
        -- WdxWc3Sequence objects, and an open Sequence Manager dialog is holding
        -- the OLD ones in filteredSequences. Swapping this.sequences without
        -- rebuilding the list left every row pointing at an orphan, so
        -- `findItem self.sequences seq` returned 0: double-clicking a row
        -- played nothing, and a rename edited an object no save would ever
        -- write. That was the "open the Sequence Manager, then the renderer,
        -- and the list goes dead" bug. refreshFromScene reloads AND calls
        -- setListItems, and is harmless when the dialog was never shown.
        try (::WdxSequenceManager.refreshFromScene()) catch
            (format "WDX Renderer: refreshFromScene threw: %\n" (getCurrentException()))
        local seqs = ::WdxSequenceManager.sequences
        format "WDX Renderer: WdxSequenceManager has % sequences\n" seqs.count
        pushed = WdxPushRendererSequencesFrom seqs
    )
    pushed
)
