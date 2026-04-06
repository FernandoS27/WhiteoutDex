/*
	WhiteoutDexModelRelinker.ms
	
	Refactored as Struct for better encapsulation and maintainability.
	All global variables are now instance properties.
	
	Usage:
		global ModelRelinker = WhiteoutDexModelRelinkerStruct()
		ModelRelinker.relinkModel &myModel
*/

struct BasicKey
(
	value,
	time,
	lock = false,
	intan,
	outtan
)

struct WhiteoutDexModelRelinkerStruct
(
	-- ============================================================================
	-- INSTANCE PROPERTIES (formerly global variables)
	-- ============================================================================
	public OriginalSequences = #(),
	public OldSequences = #(),
	public NewSequences = #(),
	
	-- ============================================================================
	-- TEXTURE TRANSFORMATION
	-- ============================================================================
	
	fn TransformTextures &Keys &Textures =
	(
		if (Keys.type == #Static) then
		(
			local idx = Keys.data
			if idx >= 1 and idx <= Textures.count then
				Keys.data = Textures[idx]
			else
				Keys.data = undefined
		)
		else
		(
			local data = Keys.data
			local IsSmooth = ::IOManager.isSmoothAnim data.type
			
			for k in data.keys do
			(
				local v = k.value
				if v >= 1 and v <= Textures.count then
					k.value = Textures[v]
				else
					k.value = undefined
				
				if IsSmooth then
				(
					local it = k.intan
					if it >= 1 and it <= Textures.count then
						k.intan = Textures[it]
					else
						k.intan = undefined
					local ot = k.outtan
					if ot >= 1 and ot <= Textures.count then
						k.outtan = Textures[ot]
					else
						k.outtan = undefined
				)
			)
		)
		
		Keys
	),
	
	
	-- ============================================================================
	-- RELINKING FUNCTIONS
	-- ============================================================================
	
	fn relinkMaterial m model =
	(
		for l in m.layers do
		(
			this.TransformTextures l.textures model.textures
			
			if l.tvertexid != undefined then 
			(
				if l.tvertexid > 0 and l.tvertexid <= model.tvertexAnims.count then
					l.tvertexid = model.tvertexAnims[l.tvertexid]
				else if model.tvertexAnims.count > 0 then
				(
					format "Warning: Layer tvertexid % out of range (% TXAN entries), using first entry\n" l.tvertexid model.tvertexAnims.count
					l.tvertexid = model.tvertexAnims[1]
				)
				else
					l.tvertexid = undefined
			)
		)
		
		if m.tvertexid != undefined then 
		(
			if m.tvertexid > 0 and m.tvertexid <= model.tvertexAnims.count then
				m.tvertexid = model.tvertexAnims[m.tvertexid]
			else if model.tvertexAnims.count > 0 then
			(
				format "Warning: Material tvertexid % out of range (% TXAN entries), using first entry\n" m.tvertexid model.tvertexAnims.count
				m.tvertexid = model.tvertexAnims[1]
			)
			else
				m.tvertexid = undefined
		)
	),
	
	fn relinkGeosetAnimations g model =
	(
		g.geoset = model.geosets[g.geoset]
	),
	
	fn relinkGeoset g model =
	(
		local idx = undefined
		try ( idx = g.material ) catch ( idx = undefined )
		
		local gname = (try (g.name) catch ("<unknown geoset>"))
		
		if (isKindOf model.materials Array and idx != undefined and idx > 0 and idx <= model.materials.count and model.materials[idx] != undefined) then
		(
			g.material = model.materials[idx]
		)
		else
		(
			format "No Material ID found for Geoset '%' (index: %)\n" gname idx
			try ( g.material = undefined ) catch()
		)
	),
	
	fn relinkObject o mdlObjects = 
	(
		if o.parent != undefined then 
			o.parent = mdlObjects[o.parent]
	),
	
	-- ============================================================================
	-- QUATERNION HELPERS
	-- ============================================================================
	
	fn fixQuaternions keys =
	(
		for i = 2 to keys.count do
		(
			local q1 = keys[i].value
			local q2 = keys[i-1].value
			
			if (::Utils.DotQuat q1 q2) < 0 then 
				keys[i].value = -q1
		)
	),
	
	-- ============================================================================
	-- SEQUENCE CONVERSION
	-- ============================================================================
	
	fn convertSequences sequences =
	(
		this.NewSequences = #()
		this.OldSequences = #()
		-- WICHTIG: Kopie statt Referenz, damit model.sequences nicht umsortiert wird
		this.OriginalSequences = for s in sequences collect s
		
		local start = 10
		
		for s in sequences do
		(
			local end = start + s.endTime - s.startTime
			
			append this.OldSequences (interval s.startTime s.endTime)
			
			s.startTime = start
			
			append this.NewSequences (interval start end)
			
			start = ((end/10)*10 + 20) as time
			
			s.endTime = end
		)
		
		-- FIX: OldSequences muessen nach Startzeit sortiert sein!
		-- getKeysOnSequences benutzt einen Vorwaerts-Cursor der nie zurueckspringt.
		-- v1200 Reforged hat Alias-Sequenzen (z.B. "Stand First" bei 3547f) mitten
		-- in der Liste -> Cursor springt vor, kann nicht zurueck -> alle Keys verloren.
		-- Loesung: Alle drei Arrays synchron nach OldSequences.start sortieren.
		if this.OldSequences.count > 1 then
		(
			local n = this.OldSequences.count
			local swapped = true
			while swapped do
			(
				swapped = false
				for i = 1 to (n - 1) do
				(
					if this.OldSequences[i].start > this.OldSequences[i + 1].start then
					(
						local tmpOld = this.OldSequences[i]
						this.OldSequences[i] = this.OldSequences[i + 1]
						this.OldSequences[i + 1] = tmpOld
						
						local tmpNew = this.NewSequences[i]
						this.NewSequences[i] = this.NewSequences[i + 1]
						this.NewSequences[i + 1] = tmpNew
						
						local tmpOrig = this.OriginalSequences[i]
						this.OriginalSequences[i] = this.OriginalSequences[i + 1]
						this.OriginalSequences[i + 1] = tmpOrig
						
						swapped = true
					)
				)
				n -= 1
			)
		)
		
		-- Reforged v1200: Shared Ranges erkennen und auf Sequenzen markieren.
		-- Wenn mehrere Sequenzen im Original denselben Zeitbereich haben,
		-- wird die erste als Master markiert und alle weiteren als Alias.
		-- sharedGroup enthaelt den Namen der Master-Sequenz.
		if this.OldSequences.count > 1 then
		(
			for i = 2 to this.OldSequences.count do
			(
				for j = 1 to (i - 1) do
				(
					if this.OldSequences[i].start == this.OldSequences[j].start and \
					   this.OldSequences[i].end == this.OldSequences[j].end then
					(
						-- Master = die erste Sequenz mit dieser Range
						local masterSeq = this.OriginalSequences[j]
						if masterSeq.sharedGroup == undefined then
							masterSeq.sharedGroup = masterSeq.name
						this.OriginalSequences[i].sharedGroup = masterSeq.sharedGroup
						exit
					)
				)
			)
		)
	),
	
	-- ============================================================================
	-- KEY PROCESSING HELPERS
	-- ============================================================================
	
	fn preProcessKey key t lock:false =
	(
		if classOf key == KeyData then 
			BasicKey value:key.value time:t lock:lock intan:undefined outtan:undefined
		else 
			BasicKey value:key.value time:t lock:lock intan:key.intan outtan:key.outtan
	),
	
	fn createStubKey defaultVal t =
	(
		local d = copy defaultVal
		BasicKey value:d time:t lock:false intan:d outtan:d
	),
	
	
	-- ============================================================================
	-- VISIBILITY PREPROCESSING
	-- ============================================================================
	
	fn preProcessVisibility keys defaultVisible:1.0 =
	(
		if this.OriginalSequences == undefined or this.OriginalSequences.count == 0 then
		(
			local defaultKey = this.createStubKey defaultVisible 10f
			return #(defaultKey)
		)
		
		local newKeys = #()
		
		-- Start key at frame 0
		append newKeys (this.createStubKey defaultVisible 0f)
		
		-- Get keys separated by sequences
		local sepKeys = ::Utils.getKeysOnSequences keys this.OldSequences inclusive:true
		
		for i = 1 to sepKeys.count do
		(
			local isLoop = false
			if i <= this.OriginalSequences.count then 
				isLoop = this.OriginalSequences[i].loop
			
			local seqKeys = sepKeys[i]
			
			local oldAnim, newAnim
			if i <= this.OldSequences.count then 
				oldAnim = this.OldSequences[i]
			else 
				continue
			
			if i <= this.NewSequences.count then 
				newAnim = this.NewSequences[i]
			else 
				continue
			
			local factor1 = newAnim.start - oldAnim.start
			local start = newAnim.start
			local end = newAnim.end
			
			if seqKeys.count == 0 then
			(
				-- Keine Keys in dieser Sequenz → defaultVisible durchgehend
				append newKeys (this.createStubKey defaultVisible start)
				append newKeys (this.createStubKey defaultVisible end)
			)
			else
			(
				local hasStartKey = false
				local lastValue = defaultVisible
				
				for j = 1 to seqKeys.count do
				(
					local k = seqKeys[j]
					local t = k.time + factor1
					
					if abs(t - start) < 0.1f then
					(
						hasStartKey = true
						lastValue = k.value
						append newKeys (this.preProcessKey k start lock:false)
					)
					else
					(
						lastValue = k.value
						append newKeys (this.preProcessKey k t lock:false)
					)
				)
				
				-- Start-Key einfügen wenn keiner vorhanden
				if not hasStartKey then
				(
					local startValue = seqKeys[1].value
					append newKeys (this.createStubKey startValue start)
				)
				
				-- End-Key: Letzten bekannten Zustand bis zum Ende durchziehen
				local lastKey = seqKeys[seqKeys.count]
				local lastKeyTime = lastKey.time + factor1
				
				if abs(lastKeyTime - end) > 0.1f then
				(
					append newKeys (this.createStubKey lastValue end)
				)
			)
		)
		
		-- Sort keys by time
		fn compareKeyTime a b = 
		(
			if a.time < b.time then -1
			else if a.time > b.time then 1
			else 0
		)
		qsort newKeys compareKeyTime
		
		-- Remove duplicate times
		local cleanKeys = #()
		local lastTime = -999999f
		for k in newKeys do
		(
			if abs(k.time - lastTime) > 0.1f then
			(
				append cleanKeys k
				lastTime = k.time
			)
		)
		
		-- Ensure at least one key
		if cleanKeys.count == 0 then 
			append cleanKeys (this.createStubKey defaultVisible 10f)
		
		cleanKeys
	),
	
	fn preProcessColor keys defaultColor =
	(
		if this.OriginalSequences == undefined or this.OriginalSequences.count == 0 then
		(
			local defaultKey = this.createStubKey defaultColor 10f
			return #(defaultKey)
		)
		
		local newKeys = #()
		
		-- Start key at frame 0
		append newKeys (this.createStubKey defaultColor 0f)
		
		-- Get keys separated by sequences
		local sepKeys = ::Utils.getKeysOnSequences keys this.OldSequences inclusive:true
		
		for i = 1 to sepKeys.count do
		(
			local isLoop = false
			if i <= this.OriginalSequences.count then 
				isLoop = this.OriginalSequences[i].loop
			
			local seqKeys = sepKeys[i]
			
			local oldAnim, newAnim
			if i <= this.OldSequences.count then 
				oldAnim = this.OldSequences[i]
			else 
				continue
			
			if i <= this.NewSequences.count then 
				newAnim = this.NewSequences[i]
			else 
				continue
			
			local factor1 = newAnim.start - oldAnim.start
			local start = newAnim.start
			local end = newAnim.end
			
			if seqKeys.count == 0 then
			(
				-- Keine Keys in dieser Sequenz → defaultColor durchgehend
				append newKeys (this.createStubKey defaultColor start)
				append newKeys (this.createStubKey defaultColor end)
			)
			else
			(
				local hasStartKey = false
				local lastValue = defaultColor
				
				for j = 1 to seqKeys.count do
				(
					local k = seqKeys[j]
					local t = k.time + factor1
					
					if abs(t - start) < 0.1f then
					(
						hasStartKey = true
						lastValue = k.value
						append newKeys (this.preProcessKey k start lock:false)
					)
					else
					(
						lastValue = k.value
						append newKeys (this.preProcessKey k t lock:false)
					)
				)
				
				-- Start-Key einfügen wenn keiner vorhanden
				if not hasStartKey then
				(
					local startValue = seqKeys[1].value
					append newKeys (this.createStubKey startValue start)
				)
				
				-- End-Key: Letzten bekannten Zustand bis zum Ende durchziehen
				local lastKey = seqKeys[seqKeys.count]
				local lastKeyTime = lastKey.time + factor1
				
				if abs(lastKeyTime - end) > 0.1f then
				(
					append newKeys (this.createStubKey lastValue end)
				)
			)
		)
		
		-- Sort keys by time
		fn compareKeyTime a b = 
		(
			if a.time < b.time then -1
			else if a.time > b.time then 1
			else 0
		)
		qsort newKeys compareKeyTime
		
		-- Remove duplicate times
		local cleanKeys = #()
		local lastTime = -999999f
		for k in newKeys do
		(
			if abs(k.time - lastTime) > 0.1f then
			(
				append cleanKeys k
				lastTime = k.time
			)
		)
		
		-- Ensure at least one key
		if cleanKeys.count == 0 then 
			append cleanKeys (this.createStubKey defaultColor 10f)
		
		cleanKeys
	),

	-- ============================================================================
	-- KEY PREPROCESSING
	-- ============================================================================
	
	fn preProcessKeys keys defaultVal =
	(
		if this.OriginalSequences == undefined or this.OriginalSequences.count == 0 then
		(
			local defaultStart = this.createStubKey defaultVal 10f
			local defaultEnd = this.createStubKey defaultVal 60f
			return #(defaultStart, defaultEnd)
		)
		
		local newKeys = #(this.createStubKey defaultVal 0f)
		
		local sepKeys = ::Utils.getKeysOnSequences keys this.OldSequences inclusive:true
		
		for i = 1 to sepKeys.count do
		(
			local isLoop = false
			if i <= this.OriginalSequences.count then 
				isLoop = this.OriginalSequences[i].loop
			
			local seqKeys = sepKeys[i]
			
			local oldAnim, newAnim
			if i <= this.OldSequences.count then 
				oldAnim = this.OldSequences[i]
			else 
				continue
			
			if i <= this.NewSequences.count then 
				newAnim = this.NewSequences[i]
			else 
				continue
			
			local factor1 = newAnim.start - oldAnim.start
			local start = newAnim.start
			local end = newAnim.end
			
			if seqKeys.count == 0 then
			(
				append newKeys (this.createStubKey defaultVal start)
				append newKeys (this.createStubKey defaultVal end)
			)
			else
			(
				-- FIX 1: Start-Key typabhaengig
				-- Float/Integer (Visibility) → defaultVal (sicher, bleibt sichtbar)
				-- Alles andere (Pos/Rot/Scale) → seqKeys[1].value (korrekte Pose)
				if start != (seqKeys[1].time + factor1) then
				(
					local startVal = defaultVal
					if (classof defaultVal != Float and classof defaultVal != Integer) then
						startVal = seqKeys[1].value
					append newKeys (this.createStubKey startVal start)
				)
				
				for j = 1 to seqKeys.count do
				(
					local k = seqKeys[j]
					local t = k.time + factor1
					local lock = (t == start or t == end)
					
					append newKeys (this.preProcessKey k t lock:lock)
				)
				
				-- Handle looping animations
				local firstKey = newKeys[newKeys.count - seqKeys.count + 1]
				local lastKey = newKeys[newKeys.count]
				
				if isLoop and lastKey.time != end then
				(
					local endKey = this.createStubKey firstKey.value end
					endKey.lock = true
					append newKeys endKey
				)
			)
			
			-- Remap inter-sequence gap keys proportionally into the new (shorter) gap
			if i < this.OldSequences.count and i < this.NewSequences.count then
			(
				local oldGapStart = this.OldSequences[i].end
				local oldGapEnd = this.OldSequences[i+1].start
				local newGapStart = this.NewSequences[i].end
				local newGapEnd = this.NewSequences[i+1].start
				
				local oldGapLen = (oldGapEnd - oldGapStart) as float
				local newGapLen = (newGapEnd - newGapStart) as float
				
				if oldGapLen > 0 and newGapLen > 0 then
				(
					for k in keys do
					(
						if k.time > oldGapStart and k.time < oldGapEnd then
						(
							local ratio = ((k.time - oldGapStart) as float) / oldGapLen
							local newTime = newGapStart + (ratio * newGapLen) as time
							append newKeys (this.preProcessKey k newTime)
						)
					)
				)
			)
		)
		
		-- Sort by time (gap keys may be out of order)
		fn sortByTime a b = (if a.time < b.time then -1 else if a.time > b.time then 1 else 0)
		qsort newKeys sortByTime
		
		newKeys
	),
	
	-- ============================================================================
	-- ANIMATION PREPROCESSING
	-- ============================================================================
	
	fn preProcessAnim animData defaultVal =
	(
		local keys = animData.keys
		
		if animData.globalSequenceId != undefined then
			keys
		else
			this.preProcessKeys keys defaultVal
	),
	
	fn preProcessEventKeys keys =
	(
		if this.OriginalSequences == undefined or this.OriginalSequences.count == 0 then
			return #()
		
		local newKeys = #()
		local sepKeys = ::Utils.getKeysOnSequences2 keys this.OldSequences inclusive:true
		
		-- Pass 1: Remap keys that fall inside a sequence (existing logic)
		for i = 1 to sepKeys.count do
		(
			local seqKeys = sepKeys[i]
			
			local oldAnim, newAnim
			if i <= this.OldSequences.count then 
				oldAnim = this.OldSequences[i]
			else 
				continue
			
			if i <= this.NewSequences.count then 
				newAnim = this.NewSequences[i]
			else 
				continue
			
			local factor1 = newAnim.start - oldAnim.start
			
			for k in seqKeys do
			(
				local kTime = if isProperty k #time then k.time else k
				append newKeys (kTime + factor1)
			)
		)
		
		-- Pass 2: Remap event keys that fall in gaps between sequences.
		-- getKeysOnSequences2 silently drops keys that are not inside any
		-- sequence range. For regular animation data this is fine (gap data
		-- is meaningless), but Event keys placed in gaps are intentional
		-- (e.g. a death sound between the last portrait and decay sequence).
		local nSeq = this.OldSequences.count
		
		for k in keys do
		(
			local kTime = if isProperty k #time then k.time else k
			
			-- Check whether this key was already handled (lies inside a sequence)
			local inSequence = false
			for s in this.OldSequences while not inSequence do
			(
				if kTime >= s.start and kTime <= s.end then
					inSequence = true
			)
			
			if not inSequence then
			(
				local handled = false
				
				-- Case A: Key sits in a gap between two consecutive sequences
				for i = 1 to (nSeq - 1) while not handled do
				(
					if kTime > this.OldSequences[i].end and kTime < this.OldSequences[i + 1].start then
					(
						if i < this.NewSequences.count then
						(
							local oldGap = this.OldSequences[i + 1].start - this.OldSequences[i].end
							local offset = kTime - this.OldSequences[i].end
							local newGap = this.NewSequences[i + 1].start - this.NewSequences[i].end
							
							-- Scale the offset proportionally into the (smaller) new gap
							if oldGap > 0 and newGap > 0 then
							(
								local ratio = (offset as float) / (oldGap as float)
								append newKeys (this.NewSequences[i].end + (newGap * ratio))
							)
							else
								append newKeys (this.NewSequences[i].end)
						)
						handled = true
					)
				)
				
				-- Case B: Key is before the first sequence
				if not handled and nSeq > 0 and kTime < this.OldSequences[1].start then
				(
					local factor = this.NewSequences[1].start - this.OldSequences[1].start
					append newKeys (kTime + factor)
					handled = true
				)
				
				-- Case C: Key is after the last sequence
				if not handled and nSeq > 0 and kTime > this.OldSequences[nSeq].end then
				(
					local factor = this.NewSequences[nSeq].end - this.OldSequences[nSeq].end
					append newKeys (kTime + factor)
				)
			)
		)
		
		-- Sort merged result (gap keys may interleave with sequence keys)
		fn sortByTime a b =
		(
			local ta = if isProperty a #time then a.time else a
			local tb = if isProperty b #time then b.time else b
			if ta < tb then -1 else if ta > tb then 1 else 0
		)
		qsort newKeys sortByTime
		
		newKeys
	),
	
	-- ============================================================================
	-- OBJECT ANIMATION CONVERSION
	-- ============================================================================
	
	fn convertAnimations o =
	(
		local trans = o.transformations
		
		if trans.rotation != undefined then
		(
			trans.rotation.keys = this.preProcessAnim trans.rotation (quat 0 0 0 1)
			this.fixQuaternions trans.rotation.keys
		)
		
		if trans.translation != undefined then
			trans.translation.keys = this.preProcessAnim trans.translation [0,0,0]
		
		if trans.scale != undefined then
			trans.scale.keys = this.preProcessAnim trans.scale [1,1,1]
	),
	
	fn preProcessParamAnim param default =
	(
		if (param.type == #Animated) then
			param.data.keys = (this.preProcessAnim param.data default)
	),
	
	-- ============================================================================
	-- SPECIFIC OBJECT TYPE ANIMATIONS
	-- ============================================================================
	
	fn convertLightAnimations l =
	(
		local colorDefault = [255,255,255]
		if l.color.type == #Animated and l.color.data.keys.count > 0 then
			colorDefault = l.color.data.keys[1].value
		else if l.color.type == #Static then
			colorDefault = l.color.data
		
		local ambColorDefault = [255,255,255]
		if l.ambColor.type == #Animated and l.ambColor.data.keys.count > 0 then
			ambColorDefault = l.ambColor.data.keys[1].value
		else if l.ambColor.type == #Static then
			ambColorDefault = l.ambColor.data
		
		if l.color.type == #Animated then
			l.color.data.keys = (this.preProcessColor l.color.data.keys colorDefault)
		if l.ambColor.type == #Animated then
			l.ambColor.data.keys = (this.preProcessColor l.ambColor.data.keys ambColorDefault)
		
		this.preProcessParamAnim l.intensity 0.0
		this.preProcessParamAnim l.ambIntensity 0.0
		this.preProcessParamAnim l.attenuationStart 0.0
		this.preProcessParamAnim l.attenuationEnd 0.0
	),
	
	fn convertParticleEmitter1Animations p =
	(
		this.preProcessParamAnim p.emissionRate 0.0
		this.preProcessParamAnim p.gravity 0.0
		this.preProcessParamAnim p.longitude 0.0
		this.preProcessParamAnim p.latitude 0.0
		this.preProcessParamAnim p.lifespan 0.0
	),
	
	fn convertParticleEmitter2Animations p =
	(
		this.preProcessParamAnim p.speed 0.0
		this.preProcessParamAnim p.gravity 0.0
		this.preProcessParamAnim p.variation 0.0
		this.preProcessParamAnim p.latitude 0.0
		this.preProcessParamAnim p.emissionRate 0.0
		this.preProcessParamAnim p.width 0.0
		this.preProcessParamAnim p.length 0.0
		-- Visibility is already processed in convertParameterAnimations!
	),
	
	fn convertRibbonEmitterAnimations r =
	(
		local colorDefault = [0,0,0]
		if r.color.type == #Animated and r.color.data.keys.count > 0 then
			colorDefault = r.color.data.keys[1].value
		else if r.color.type == #Static then
			colorDefault = r.color.data
		
		this.preProcessParamAnim r.heightAbove 0.0
		this.preProcessParamAnim r.heightBelow 0.0
		this.preProcessParamAnim r.alpha 0.0
		this.preProcessParamAnim r.textureSlot 0.0
		
		if r.color.type == #Animated then
			r.color.data.keys = (this.preProcessColor r.color.data.keys colorDefault)
	),
	
	fn convertEventObjectAnimations o =
	(
		if o.eventTrack.globalSequenceId == undefined then
			o.eventTrack.keys = this.preProcessEventKeys o.eventTrack.keys
	),
	
	fn convertParameterAnimations o =
	(
		local s = o.sub
		
		if (isProperty s #visibility) and (s.visibility.type == #Animated) then
			if s.visibility.data.globalSequenceId == undefined then
				s.visibility.data.keys = (this.preProcessVisibility s.visibility.data.keys)
		
		-- Reforged: CORN EmissionRate Animation remappen
		if (isProperty s #emissionRateAnim) and s.emissionRateAnim != undefined then
			this.preProcessParamAnim s.emissionRateAnim 0.0
	),
	
	fn convertGeosetAnimations geoAnim =
	(
		if (geoAnim.alpha.type == #animated) then
		(
			if geoAnim.alpha.data.globalSequenceId == undefined then
				geoAnim.alpha.data.keys = (this.preProcessVisibility geoAnim.alpha.data.keys)
		)
		
		if (geoAnim.usesColor and geoAnim.color.type == #animated) then
		(
			if geoAnim.color.data.globalSequenceId == undefined then
			(
				local colorDefault = [255,255,255]
				if geoAnim.color.data.keys.count > 0 then
					colorDefault = geoAnim.color.data.keys[1].value
				geoAnim.color.data.keys = (this.preProcessColor geoAnim.color.data.keys colorDefault)
			)
		)
	),
	
	-- ============================================================================
	-- PARTICLE EMITTER 2 RELINKING
	-- ============================================================================
	
	fn RelinkParticleEmitter2 p textures =
	(
		local idx = undefined
		try ( idx = p.sub.texture ) catch ( idx = undefined )
		
		local pname = (try (p.name) catch ("<unknown>"))
		
		if (isKindOf textures Array) then
		(
			if (idx != undefined and idx > 0 and idx <= textures.count and textures[idx] != undefined) then
			(
				p.sub.texture = textures[idx]
			)
			else
			(
				format "No texture found for ParticleEmitter2 '%' (index: %)\n" pname idx
			)
		)
		else
		(
			format "No texture list provided for ParticleEmitter2 '%'\n" pname
		)
	),
	
	-- ============================================================================
	-- TVERTEX ANIMATION SPLITTING
	-- ============================================================================
	
	fn splitTvertexAnimation tv =
	(
		if (tv.transformation.translation != undefined) then
		(
			local u = AnimationData ()
			local v = AnimationData ()
			local w = AnimationData ()
			
			local t = tv.transformation.translation.type
			local g = tv.transformation.translation.globalSequenceId
			
			local result = #(u, v, w)
			local keys = tv.transformation.translation.keys
			local isSmooth = (t == #Bezier or t == #Hermite)
			local s = keys.count
			
			for i = 1 to 3 do
			(
				local r = result[i]
				
				r.type = t
				r.globalSequenceId = g
				
				local newKeys = #()
				newKeys[s] = undefined
				
				for j = 1 to s do
				(
					local k = keys[j]
					local p = k.value[i]
					
					if isSmooth then
						newKeys[j] = SmoothKeyData k.time p (k.intan[i]) (k.outtan[i])
					else
						newKeys[j] = KeyData k.time p
				)
				
				r.keys = newKeys
				r.keys = this.preProcessAnim r 0.0
			)
			
			tv.transformation.translation = result
		)
	),

	
	-- ============================================================================
	-- MAIN RELINK FUNCTION
	-- ============================================================================
	
	fn relinkModel &model =
	(
		-- Link Materials
		::Progress.downLevel 5 1
		::Progress.showItem "Linking Materials"
		
		for m in model.materials do
			this.relinkMaterial m model
		
		::Progress.upLevel ()
		
		-- Link Geosets
		::Progress.downLevel 10 1
		::Progress.showItem "Linking Geosets"
		
		for g in model.geosets do
			this.relinkGeoset g model
		
		::Progress.upLevel ()
		
		-- Link Geoset Animations
		::Progress.downLevel 5 1
		::Progress.showItem "Linking Geoset Animations"
		
		for g in model.geosetAnims do
			this.relinkGeosetAnimations g model
		
		::Progress.upLevel ()
		
		-- Link Objects
		::Progress.downLevel 10 1
		::Progress.showItem "Linking Objects"
		
		local mdlObjects = #()
		
		for n in model.bones do
			mdlObjects[n.objId] = n
		for n in model.helpers do
			mdlObjects[n.objId] = n
		for n in model.lights do
			mdlObjects[n.objId] = n
		for n in model.attachments do
			mdlObjects[n.objId] = n
		for n in model.events do
			mdlObjects[n.objId] = n
		for n in model.particleEmitters1 do
			mdlObjects[n.objId] = n
		for n in model.particleEmitters2 do
			mdlObjects[n.objId] = n
		for n in model.ribbonEmitters do
			mdlObjects[n.objId] = n
		for n in model.CollisionShapes do
			mdlObjects[n.objId] = n
		-- Reforged: Corn Emitters registrieren
		if model.cornEmitters != undefined then
			for n in model.cornEmitters do
				mdlObjects[n.objId] = n
		
		for o in mdlObjects where o != undefined do
			this.relinkObject o mdlObjects
		
		model.ex = mdlObjects
		
		-- Relink ParticleEmitter2 textures
		for p in model.particleEmitters2 do
			this.RelinkParticleEmitter2 p model.textures
		
		
		-- Relink Ribbon Emitters (convert happens later, after convertSequences)
		for r in model.ribbonEmitters do
			this.relinkGeoset r.sub model
		
		::Progress.upLevel ()
		
		-- Convert Animations
		::Progress.downLevel 20 1
		::Progress.showItem "Converting Animations"
		
		this.convertSequences model.sequences
		
		for t in model.tvertexAnims do
			this.splitTvertexAnimation t
		
		-- Convert Material Layer Animations
		for mat in model.materials do
		(
			for layer in mat.layers do
			(
				if layer.alpha != undefined and layer.alpha.type == #Animated then
				(
					if layer.alpha.data.globalSequenceId == undefined then
						layer.alpha.data.keys = this.preProcessVisibility layer.alpha.data.keys defaultVisible:100.0
				)
			)
		)
		
		gc ()
		
		-- Convert all object animations
		for o in mdlObjects where o != undefined do
		(
			this.convertAnimations o
			this.convertParameterAnimations o
		)
		
		for g in model.geosetAnims do
			this.convertGeosetAnimations g
		
		for l in model.lights do
			this.convertLightAnimations l.sub
		
		for p in model.particleEmitters1 do
			this.convertParticleEmitter1Animations p.sub
		
		for p in model.particleEmitters2 do
			this.convertParticleEmitter2Animations p.sub
		
		for r in model.ribbonEmitters do
			this.convertRibbonEmitterAnimations r.sub
		
		for o in model.events do
			this.convertEventObjectAnimations o.sub
		
		::Progress.upLevel ()
	)
	
)

-- ============================================================================
-- GLOBAL INSTANCE (for backward compatibility)
-- ============================================================================
global ModelRelinker = WhiteoutDexModelRelinkerStruct()
