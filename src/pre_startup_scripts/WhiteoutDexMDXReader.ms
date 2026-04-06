struct WhiteoutDexMDXReader
(
	fn readMDXAnimation &func =
	(
		::IOManager.jumpMDXReading 4
		local animdata = AnimationData()
		local numTracks = ::IOManager.readMDXLong()
		local interType = ::IOManager.readMDXLong()
		local globalSeq = ::IOManager.readMDXLong()
		case interType of
		(
			1: animdata.type = #Linear
			2: animdata.type = #Hermite
			3: animdata.type = #Bezier
			default: animdata.type = #NonInterp
		)
		if globalSeq != -1 then animdata.globalSequenceId = globalSeq
		local result = #()
		result[numTracks] = undefined
		local isSmooth = interType == 2 or interType == 3
		for i = 1 to numTracks do
		(
			local t = ::IOManager.readMDXTime()
			local val = func()
			if isSmooth then
			(
				local intan = func()
				local outtan = func()
				result[i] = SmoothKeyData t val intan outtan
			)
			else result[i] = KeyData t val
		)
		animdata.keys = result
		animdata
	),

	fn readMDXAnimateableParameter param &func =
	(
		param.type = #Animated
		param.data = this.readMDXAnimation func
	),

	fn readMDXParameter &func =
	(
		local paramVal = ParameterValue()
		paramVal.type = #Static
		paramVal.data = func()
		paramVal
	),

	fn createEmptyWc3Model =
	(
		local model = Wc3Model()
		model.version = 800
		model.name = ""
		model.extents = #()
		model.globalSequences = #()
		model.sequences = #()
		model.textures = #()
		model.materials = #()
		model.tvertexAnims = #()
		model.geosets = #()
		model.geosetAnims = #()
		model.bones = #()
		model.helpers = #()
		model.lights = #()
		model.attachments = #()
		model.particleEmitters1 = #()
		model.particleEmitters2 = #()
		model.ribbonEmitters = #()
		model.events = #()
		model.collisionShapes = #()
		model.cameras = #()
		model
	),

	fn readMDXSequences =
	(
		local seqsChunkSize = ::IOManager.readMDXLong()
		local sequenceBlockSize = 132
		local numSeqs = seqsChunkSize / sequenceBlockSize
		
		if (mod seqsChunkSize sequenceBlockSize) != 0 then
			format "Warnung: SEQS Chunk-Groesse (%) ist kein Vielfaches von %!\n" seqsChunkSize sequenceBlockSize to:listener

		local result = #()
		result[numSeqs] = undefined
		
		local currentBlockStartPos = ftell ::IOManager.instream

		for i = 1 to numSeqs do
		(
			fseek ::IOManager.instream currentBlockStartPos #seek_set

			local newAnim = Wc3Sequence()
			newAnim.name = ::IOManager.readMDXString 80
			newAnim.startTime = ::IOManager.readMDXTime()
			newAnim.endTime = ::IOManager.readMDXTime()

			local movespeed = ::IOManager.readMDXFloat()
			if movespeed != 0.0 then newAnim.movespeed = movespeed

			local flagsLong = ::IOManager.readMDXLong()
			newAnim.loop = (bit.and flagsLong 0x1) != 0   -- FIX: war == 0

			local rarity = ::IOManager.readMDXFloat()
			if rarity != 0.0 then newAnim.rarity = rarity

			-- syncPoint (unused)
			::IOManager.readMDXLong()
			-- Bounding extents per sequence
			local ext = ::IOManager.readMDXExtents()
			local v1 = ext.minExtent
			local v2 = ext.maxExtent
			ext.minExtent = [amin v1.y v2.y, amin -v1.x -v2.x, v1.z]
			ext.maxExtent = [amax v1.y v2.y, amax -v1.x -v2.x, v2.z]
			newAnim.extents = ext
			
			result[i] = newAnim
			currentBlockStartPos += sequenceBlockSize
		)

		fseek ::IOManager.instream currentBlockStartPos #seek_set
		result
	),

	fn readMDXTextures =
	(
		
		local numTextures = (::IOManager.readMDXLong()) / 268
		local result = #()
		
		result[numTextures] = undefined
		
		for i = 1 to numTextures do
		(
			
			local tex = Wc3Texture()
			
			tex.replaceableId = ::IOManager.readMDXLong() + 1
			
			tex.path = ::IOManager.readMDXString 260
			
			local flags = ::IOManager.IntToFlags (::IOManager.readMDXLong()) 2
			
			tex.wrapWidth = flags[1]
			tex.wrapHeight = flags[2]
			
			tex.id = i
			
			result[i] = tex
		)
		
		result
	),

	fn readMDXLayers =
	(
		
		local tag = ::IOManager.readMDXLong()
		
		if tag != tag_LAYS then throw "Expected a layer when reading material, got something unknown"
		
		local numLayers = ::IOManager.readMDXLong()
		
		local result = #()
		result[numLayers] = undefined
		
		for i = 1 to numLayers do
		(
			
			::IOManager.jumpMDXReading 4
			
			local l = Wc3Layer ()
			
			l.filtermode = (::IOManager.readMDXLong()) + 1
			
			local flags = ::IOManager.IntToFlags (::IOManager.readMDXLong()) 8
			
			l.unshaded = flags[1]
			l.SphereEnvMap = flags[2]
			
			
			l.twosided = flags[5]
			l.unfogged = flags[6]
			l.nodephtest = flags[7]
			l.nodephset = flags[8]
			
			l.textures = readMDXParameter ::IOManager.readMDXLong
			
			local tvertexid = ::IOManager.readMDXLong ()
			
			if tvertexid != -1 then l.tvertexid = tvertexid + 1
			
			l.coordId = (::IOManager.readMDXLong ()) + 1
			
			l.alpha = readMDXParameter ::IOManager.readMDXFloat
			
			do
			(
				
				local Anim_tag = ::IOManager.peekMDXHeader()
				local notDone = true
				case Anim_tag of
				(
					
					tag_KMTF: readMDXAnimateableParameter l.textures ::IOManager.readMDXLong
					
					tag_KMTA: readMDXAnimateableParameter l.alpha ::IOManager.readMDXFloat
					
					default: notDone = false
				)
			)
			while (notDone)
			
			::IOManager.writerTransformKeys l.alpha (fn tmp a = a*100.0)
			
			::IOManager.writerTransformKeys l.textures (fn tmp a = a + 1)
			
			result[i] = l
		)
		
		result
	),

	fn readMDXMaterials =
	(
		local result = #()
		::IOManager.MDXStartReadingChunk () 
		local i = 0
		while not (::IOManager.MDXHasChunkEnded ()) do
		(
			::IOManager.MDXStartChunkElem () 
							
			local mat = Wc3Material ()
			
			-- PriorityPlane lesen (DWORD) - ERSTE Variable nach InclusiveSize!
			local priorityPlane = ::IOManager.readMDXLong()
			mat.priorityPlane = priorityPlane
			
			-- Material Flags lesen (DWORD) - ZWEITE Variable nach InclusiveSize!
			local materialFlagsInt = ::IOManager.readMDXLong()
			
			-- Flags dekodieren nach MDX Spezifikation:
			-- Bit 0 (#1)  - ConstantColor
			-- Bit 3 (#8)  - SortPrimitivesNearZ (nicht verwendet)
			-- Bit 4 (#16) - SortPrimitivesFarZ
			-- Bit 5 (#32) - FullResolution
			local materialFlags = ::IOManager.IntToFlags materialFlagsInt 6
			
			mat.isConstantColor   = materialFlags[1]   -- Bit 0
			-- materialFlags[2] = Bit 1 (unbekannt)
			-- materialFlags[3] = Bit 2 (unbekannt)
			mat.isSortPrimsNearZ  = materialFlags[4]   -- Bit 3
			mat.isSortPrimsFarZ   = materialFlags[5]   -- Bit 4
			mat.isFullResolution  = materialFlags[6]   -- Bit 5
				
			mat.layers = readMDXLayers ()
			i += 1
			mat.id = i
			append result mat
		)
		result
	),

	fn readMDXTVertexAnimations =
	(
		
		local result = #()
		
		::IOManager.MDXStartReadingChunk()
		local i = 0
		
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			
			::IOManager.MDXStartChunkElem()
			
			local tv = Wc3TVertexAnim()
			
			tv.transformation = TransformationData()
			
			do
			(
				local Anim_tag = ::IOManager.peekMDXHeader()
				local notDone = true
				case Anim_tag of
				(
					
					tag_KTAT: tv.transformation.translation = readMDXAnimation ::IOManager.readMDXPosition
					
					tag_KTAR: tv.transformation.rotation = readMDXAnimation ::IOManager.readMDXQuaternion
					
					tag_KTAS: tv.transformation.scale = readMDXAnimation ::IOManager.readMDXScale
					
					default: notDone = false
				)
			)
			while notDone
			
			i += 1
			tv.id = i
			
			append result tv
		)
		
		result
	),

	fn readMDXGeosets =
	(
		local result = #()
		::IOManager.MDXStartReadingChunk ()
		local id = 0
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			::IOManager.MDXStartChunkElem()
			local geo = Wc3Geoset()
			local vertices, tvertices = #(), normals, vertgroups, faces = #(), matrices = #()
			local faceTypes, faceGroups
			do
			(
				local Geo_tag = ::IOManager.readMDXLong()
				local notDone = true
				case Geo_tag of
				(
					tag_VRTX:
					(
						local num = ::IOManager.readMDXLong()
						vertices = ::IOManager.readMDXArray num ::IOManager.readMDXPosition
					)

					tag_NRMS:
					(
						local num = ::IOManager.readMDXLong()
						normals = ::IOManager.readMDXArray num ::IOManager.readMDXPosition
					)

					tag_PTYP:
					(
						local num = ::IOManager.readMDXLong()
						faceTypes = ::IOManager.readMDXArray num ::IOManager.readMDXLong
					)

					tag_PCNT:
					(
						local num = ::IOManager.readMDXLong()
						faceGroups = ::IOManager.readMDXArray num ::IOManager.readMDXLong
					)

					tag_PVTX:
					(
						local num = ::IOManager.readMDXLong()
						for i = 1 to faceGroups.count do
						(
							
							local numFaces = faceGroups[i]/3
							for j = 1 to numFaces do
							(
								local face = Point3 0 0 0
								face[1] = ::IOManager.readMDXShort () + 1
								face[2] = ::IOManager.readMDXShort () + 1
								face[3] = ::IOManager.readMDXShort () + 1
								append faces face
							)
						)
					)

					tag_GNDX:
					(
						local num = ::IOManager.readMDXLong()
						vertgroups = ::IOManager.readMDXArray num (fn tmp = ::IOManager.readMDXByte() + 1)
					)

					tag_MTGC:
					(
						local num = ::IOManager.readMDXLong()
						matrices[num] = undefined
						for i = 1 to num do
						(
							local size = ::IOManager.readMDXLong()
							matrices[i] = #()
							matrices[i][size] = undefined
						)
					)

					tag_MATS:
					(
						local num = ::IOManager.readMDXLong()
						for m in matrices do
							for i = 1 to m.count do
								m[i] = ::IOManager.readMDXLong() + 1
					)

					default: notDone = false
				)
			)
			while notDone

			::IOManager.jumpMDXReading -4
			geo.vertices = vertices
			geo.normals = normals
			geo.matrixindices = vertgroups
			geo.faces = faces
			geo.matrices = matrices
			geo.material = ::IOManager.readMDXLong() + 1
			::IOManager.jumpMDXReading 4
			local flags = bit.and (::IOManager.readMDXLong()) 0x04
			geo.unselectable = (flags != 0)
			geo.mainExtents = ::IOManager.readMDXExtents()
			local numext = ::IOManager.readMDXLong()
			::IOManager.jumpMDXReading (28*numext)
			do
			(
				local Geo_tag = ::IOManager.readMDXLong()
				local notDone = true
				case Geo_tag of
				(
					tag_UVAS: local num = ::IOManager.readMDXLong()

					tag_UVBS:
					(
						local num = ::IOManager.readMDXLong()
						append tvertices (::IOManager.readMDXArray num ::IOManager.readMDXTvertex)
					)

					default: notDone = false
				)
			)
			while notDone
			::IOManager.jumpMDXReading -4
			geo.tvertices = tvertices
			geo.numUVWmaps = tvertices.count
			id += 1
			geo.id = id
			append result geo
		)
		result
	),

	fn readMDXGeosetAnimations =
	(
		
		local result = #()
		
		::IOManager.MDXStartReadingChunk()
		local id = 0
		
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			
			::IOManager.MDXStartChunkElem()
			
			local ga = Wc3GeosetAnim()
			
			ga.alpha = readMDXParameter ::IOManager.readMDXFloat
			
			local flags = ::IOManager.IntToFlags (::IOManager.readMDXLong()) 2
			
			ga.dropShadow = flags[1]
			
			ga.usesColor = flags[2]
			
			ga.color = readMDXParameter ::IOManager.readMDXColor
			
			ga.geoset = (::IOManager.readMDXLong()) + 1
			
			do
			(
				
				local Anim_tag = ::IOManager.peekMDXHeader()
				local notDone = true
				case Anim_tag of
				(
					
					tag_KGAC: readMDXAnimateableParameter ga.color ::IOManager.readMDXColor
					
					tag_KGAO: readMDXAnimateableParameter ga.alpha ::IOManager.readMDXFloat
					
					default: notDone = false
				)
			)
			while notDone
			
			append result ga
		)
		
		result
	),

	fn readMDXNode =
	(
		
		local obj = MDLObject()
		
		obj.name = ::IOManager.readMDXString 80
		
		obj.objId = ::IOManager.readMDXLong() + 1
		
		local parent = ::IOManager.readMDXLong()
		if parent != -1 then obj.parent = parent + 1
		
		local flags = ::IOManager.IntToFlags (::IOManager.readMDXLong()) 21
		
		obj.dontInheritTranslation = flags[1]
		obj.dontInheritRotation = flags[2]
		obj.dontInheritScale = flags[3]
		obj.billboarded = flags[4]
		obj.billboardedLockX = flags[5]
		obj.billboardedLockY = flags[6]
		obj.billboardedLockZ = flags[7]
		obj.cameraAnchored = flags[8]
		
		obj.transformations = TransformationData()
		
		do
		(
			local Anim_tag = ::IOManager.peekMDXHeader()
			local notDone = true
			case Anim_tag of
			(
				
				tag_KGTR: obj.transformations.translation = readMDXAnimation ::IOManager.readMDXPosition
				
				tag_KGRT: obj.transformations.rotation = readMDXAnimation ::IOManager.readMDXQuaternion
				
				tag_KGSC: obj.transformations.scale = readMDXAnimation ::IOManager.readMDXScale
				
				default: notDone = false
			)
		)
		while notDone
		
		#(obj, flags)
	),

	fn readMDXBones =
	(
		
		local result = #()
		
		::IOManager.MDXStartReadingChunk()
		
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			
			::IOManager.MDXStartChunkElem specialCase:8
			
			local bon = Wc3Bone()
			
			local tmp = readMDXNode()
			
			local mdlobj = tmp[1]
			
			bon.mdlObj = mdlobj 
			
			mdlobj.sub = bon
			
			::IOManager.jumpMDXReading 8
			
			append result mdlobj
		)
		
		result
	),

	fn readMDXLights =
	(
		local result = #()
		::IOManager.MDXStartReadingChunk()
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			::IOManager.MDXStartChunkElem()
			::IOManager.readMDXLong()
			local lite = Wc3Light()
			local tmp = readMDXNode()
			local mdlobj = tmp[1]
			lite.mdlObj = mdlobj 
			mdlobj.sub = lite
			lite.lightType = ::IOManager.readMDXLong()
			case lite.lightType of
			(
				0: lite.lightType=#Omnidirectional
				1: lite.lightType=#Directional
				2: lite.lightType=#Ambient
				default: lite.lightType=#Omnidirectional
			)
			lite.attenuationStart = readMDXParameter ::IOManager.readMDXFloat
			lite.attenuationEnd = readMDXParameter ::IOManager.readMDXFloat
			lite.color = readMDXParameter ::IOManager.readMDXColor
			lite.intensity = readMDXParameter ::IOManager.readMDXFloat
			lite.ambColor = readMDXParameter ::IOManager.readMDXColor
			lite.ambIntensity = readMDXParameter ::IOManager.readMDXFloat
			lite.visibility = ParameterValue()
			do
			(
				local Anim_tag = ::IOManager.peekMDXHeader()
				local notDone = true
				case Anim_tag of
				(
					tag_KLAV: readMDXAnimateableParameter lite.visibility ::IOManager.readMDXFloat
					tag_KLAC: readMDXAnimateableParameter lite.color ::IOManager.readMDXColor
					tag_KLAI: readMDXAnimateableParameter lite.intensity ::IOManager.readMDXFloat
					tag_KLBC: readMDXAnimateableParameter lite.ambColor ::IOManager.readMDXColor
					tag_KLBI: readMDXAnimateableParameter lite.ambIntensity ::IOManager.readMDXFloat
					tag_KLAS: readMDXAnimateableParameter lite.attenuationStart ::IOManager.readMDXFloat
					tag_KLAE: readMDXAnimateableParameter lite.attenuationEnd ::IOManager.readMDXFloat
					default: notDone = false
				)
			)
			while notDone
			append result mdlobj
		)
		result
	),

	fn readMDXHelpers =
	(
		
		local result = #()
		
		::IOManager.MDXStartReadingChunk()
		
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			
			::IOManager.MDXStartChunkElem()
			
			local help = Wc3Helper()
			
			local tmp = readMDXNode()
			
			local mdlobj = tmp[1]
			
			help.mdlObj = mdlobj
			
			mdlobj.sub = help
			
			append result mdlobj 
		)
		
		result
	),

	fn readMDXAttachments =
	(
		
		local result = #()
		
		::IOManager.MDXStartReadingChunk()
		
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			
			::IOManager.MDXStartChunkElem()
			
			::IOManager.readMDXLong()
			
			local tmp = readMDXNode()
			local mdlobj = tmp[1]
			
			local att = Wc3Attachment()
			
			att.mdlObj = mdlobj
			mdlobj.sub = att
			
			att.outPath = ::IOManager.readMDXString 260
			
			att.attachmentId = ::IOManager.readMDXLong()
			
			att.visibility = ParameterValue()
			
			local notDone = true
			do
			(
				local Anim_tag = ::IOManager.peekMDXHeader ()
				case Anim_tag of
				(
					
					tag_KATV: readMDXAnimateableParameter att.visibility ::IOManager.readMDXFloat
					
					default: notDone = false
				)
			) while notDone
			
			append result mdlobj
		)
		
		result
	),

	fn readPivotPoints =
	(
		
		
		local num = (::IOManager.readMDXLong()) / 12
		
		::IOManager.readMDXArray num ::IOManager.readMDXPosition
	),

	fn readMDXParticleEmitter =
	(
		
		local result = #()
		
		::IOManager.MDXStartReadingChunk()
		
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			
			::IOManager.MDXStartChunkElem()
			
			::IOManager.readMDXLong()
			
			local part = Wc3ParticleEmitter()
			
			local tmp = readMDXNode()
			
			local mdlobj = tmp[1]
			
			local flags = tmp[2]
			
			part.mdlObj = mdlobj 
			
			mdlobj.sub = part
			
			part.emissionRate = readMDXParameter ::IOManager.readMDXFloat
			
			part.gravity = readMDXParameter ::IOManager.readMDXFloat
			
			part.longitude = readMDXParameter ::IOManager.readMDXFloat
			
			part.latitude = readMDXParameter ::IOManager.readMDXFloat
			
			part.path = ::IOManager.readMDXString 260
			
			part.lifespan = readMDXParameter ::IOManager.readMDXFloat
			
			part.initVelocity = readMDXParameter ::IOManager.readMDXFloat
			
			part.visibility = ParameterValue()
			
			do
			(
				local Anim_tag = ::IOManager.peekMDXHeader()
				local notDone = true
				case Anim_tag of
				(
					
					tag_KPEE: readMDXAnimateableParameter part.emissionRate ::IOManager.readMDXFloat
					
					tag_KPEG: readMDXAnimateableParameter part.gravity ::IOManager.readMDXFloat
					
					tag_KPLN: readMDXAnimateableParameter part.longitude ::IOManager.readMDXFloat
					
					tag_KPEL: readMDXAnimateableParameter part.latitude ::IOManager.readMDXFloat
					
					tag_KPES: readMDXAnimateableParameter part.initVelocity ::IOManager.readMDXFloat
					
					tag_KPEV: readMDXAnimateableParameter part.visibility ::IOManager.readMDXFloat
					
					default: notDone = false
				)
			) while notDone
			
			

			
			append result mdlobj
		)
		
		result
	),

	fn readMDXParticleEmitter2 =
	(
		
		local result = #()
		
		::IOManager.MDXStartReadingChunk()
		
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			
			::IOManager.MDXStartChunkElem()
			
			::IOManager.readMDXLong()
			
			local part = Wc3ParticleEmitter2()
			
			local tmp = readMDXNode()
			
			local mdlobj = tmp[1]
			
			local flags = tmp[2]
			
			part.mdlObj = mdlobj
			
			mdlobj.sub = part
			
			part.unshaded = flags[16]
			part.sortPrimsFarZ = flags[17]
			part.lineEmitter = flags[18]
			part.unfogged = flags[19]
			part.modelSpace = flags[20]
			part.xyQuad = flags[21]
			
			part.speed = readMDXParameter ::IOManager.readMDXFloat
			part.variation = readMDXParameter ::IOManager.readMDXFloat
			part.latitude = readMDXParameter ::IOManager.readMDXFloat
			part.gravity = readMDXParameter ::IOManager.readMDXFloat
			
			part.lifeSpan = ::IOManager.readMDXFloat()
			
			part.emissionRate = readMDXParameter ::IOManager.readMDXFloat
			
			part.length = readMDXParameter ::IOManager.readMDXFloat
			part.width = readMDXParameter ::IOManager.readMDXFloat
			
			local mdxFilterMode = ::IOManager.readMDXLong()

			
			
			
			
			
			case mdxFilterMode of
			(
				0: part.filtermode = 1 
				1: part.filtermode = 2 
				2: part.filtermode = 3 
				3: part.filtermode = 4 
				4: part.filtermode = 5 
				default: part.filtermode = 1 
			)
			
			
			part.rows = ::IOManager.readMDXLong()
			part.columns = ::IOManager.readMDXLong()
			
			part.particleType = ::IOManager.readMDXLong() + 1
			
			part.tailLength = ::IOManager.readMDXFloat()
			
			part.time = ::IOManager.readMDXFloat()
			
			part.startColor = ::IOManager.readMDXColor()
			part.midColor = ::IOManager.readMDXColor()
			part.endColor = ::IOManager.readMDXColor()
			
			local alpha = point3 0 0 0
			alpha.x = ::IOManager.readMDXByte()
			alpha.y = ::IOManager.readMDXByte()
			alpha.z = ::IOManager.readMDXByte()
			part.alpha = alpha
			
			part.scaling = ::IOManager.readMDXVector3 ()
			
			part.lifeSpanUVAnim = ::IOManager.readMDXVector3Long()
			part.decayUVAnim = ::IOManager.readMDXVector3Long()
			part.TailUVAnim = ::IOManager.readMDXVector3Long()
			part.TailDecayUVAnim = ::IOManager.readMDXVector3Long()
			
			part.texture = ::IOManager.readMDXLong() + 1
			
			part.squirt = ::IOManager.readMDXLong()
			if part.squirt == 1 then part.squirt = true else part.squirt = false
			
			part.priorityPlane = ::IOManager.readMDXLong()
			
			part.replaceableId = ::IOManager.readMDXLong() + 1
			
			part.visibility = ParameterValue()
			
			do
			(
				local Anim_tag = ::IOManager.peekMDXHeader()
				local notDone = true
				case Anim_tag of
				(
					
					tag_KP2S: readMDXAnimateableParameter part.speed ::IOManager.readMDXFloat
					
					tag_KP2L: readMDXAnimateableParameter part.latitude ::IOManager.readMDXFloat
					
					tag_KP2R: readMDXAnimateableParameter part.variation ::IOManager.readMDXFloat
					
					tag_KP2G: readMDXAnimateableParameter part.gravity ::IOManager.readMDXFloat
					
					tag_KP2E: readMDXAnimateableParameter part.emissionRate ::IOManager.readMDXFloat
					
					tag_KP2N: readMDXAnimateableParameter part.length ::IOManager.readMDXFloat
					
					tag_KP2W: readMDXAnimateableParameter part.width ::IOManager.readMDXFloat
					
					tag_KP2V: readMDXAnimateableParameter part.visibility ::IOManager.readMDXFloat
					
					default: notDone = false
				)
			)
			while notDone
			
			append result mdlobj
		)
		
		result
	),

	fn readMDXRibbonEmitter =
	(
		
		local result = #()
		
		::IOManager.MDXStartReadingChunk()
		
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			
			::IOManager.MDXStartChunkElem()
			
			::IOManager.readMDXLong()
			
			local rib = Wc3RibbonEmitter()
			
			local tmp = readMDXNode()
			
			local mdlobj = tmp[1]
			local flags = tmp[2]
			
			rib.mdlObj = mdlobj
			mdlobj.sub = rib
			
			rib.heightAbove = readMDXParameter ::IOManager.readMDXFloat
			rib.heightBelow = readMDXParameter ::IOManager.readMDXFloat
			rib.alpha = readMDXParameter ::IOManager.readMDXFloat
			rib.color = readMDXParameter ::IOManager.readMDXColor
			
			rib.lifeSpan = ::IOManager.readMDXFloat()
			
			rib.textureSlot = readMDXParameter ::IOManager.readMDXLong
			
			rib.emissionRate = ::IOManager.readMDXLong()
			rib.rows = ::IOManager.readMDXLong()
			rib.columns = ::IOManager.readMDXLong()
			
			rib.material = ::IOManager.readMDXLong() + 1
			
			rib.gravity = ::IOManager.readMDXFloat()
			
			
			rib.visibility = ParameterValue()
			
			do
			(
				local Anim_tag = ::IOManager.peekMDXHeader()
				local notDone = true
				case Anim_tag of
				(
					
					tag_KRHA: readMDXAnimateableParameter rib.heightAbove ::IOManager.readMDXFloat
					
					tag_KRHB: readMDXAnimateableParameter rib.heightBelow ::IOManager.readMDXFloat
					
					tag_KRAL: readMDXAnimateableParameter rib.alpha ::IOManager.readMDXFloat
					
					tag_KRCO: readMDXAnimateableParameter rib.color ::IOManager.readMDXColor
					
					tag_KRTX: readMDXAnimateableParameter rib.textureSlot ::IOManager.readMDXLong
					
					tag_KRVS: readMDXAnimateableParameter rib.visibility ::IOManager.readMDXFloat
					
					default: notDone = false
				)
			)
			while notDone
			
			append result mdlobj
		)
		
		result
	),

	fn readMDXEventObject =
	(
		
		fn readMDXEventTrack =
		(
			
			::IOManager.jumpMDXReading 4
			
			local animdata = AnimationData()
			
			local numTracks = ::IOManager.readMDXLong()
			
			local globalSeq = ::IOManager.readMDXLong()
			
			animdata.type = #NonInterp
			
			
			if numTracks > 0 then
			(
				
				local tracks = #()
				tracks[numTracks] = undefined
				
				
				for i = 1 to numTracks do tracks[i] = KeyData (::IOManager.readMDXTime ()) true
				
				animdata.keys = tracks
			)
			else
			(
				
				format "Warnung: Event-Objekt ohne Event-Keys gefunden. Es wird ein leeres Event-Objekt erstellt.\n" to:listener
				animdata.keys = #()
			)
			
			
			::IOManager.MDXFixChunkSize (12 + 4*numTracks)
			
			animdata
		)
		
		
		local result = #()
		
		::IOManager.MDXStartReadingChunk()
		
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			
			::IOManager.MDXStartChunkElem()
			
			local tmp = readMDXNode()
			
			local mdlobj = tmp[1]
			
			local ev = Wc3Event()
			
			mdlobj.sub = ev
			
			ev.mdlObj = mdlobj
			
			do
			(
				local Anim_tag = ::IOManager.peekMDXHeader()
				local notDone = true
				case Anim_tag of
				(
					
					tag_KEVT: ev.eventTrack = readMDXEventTrack()
					
					default: notDone = false
				)
			) while notDone
			
			append result mdlobj
		)
		
		result
	),

	fn readMDXCollisionShapes =
	(
		
		local result = #()
		
		::IOManager.MDXStartReadingChunk()
		
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			
			::IOManager.MDXStartChunkElem()
			
			local tmp = readMDXNode()
			
			local mdlobj = tmp[1]
			
			local col = Wc3CollisionShape()
			
			col.mdlObj = mdlobj
			mdlobj.sub = col
			
			col.shape = ::IOManager.readMDXLong()
			
			
			if col.shape == 2 then ::IOManager.MDXFixChunkSize 20
			else ::IOManager.MDXFixChunkSize 28 
			
			col.vertices = #()
			
			append col.vertices (::IOManager.readMDXPosition ())
			
			
			if (col.shape == 2) then col.boundsRadius = ::IOManager.readMDXFloat ()
			else append col.vertices (::IOManager.readMDXPosition ()) 
			
			append result mdlobj
		)
		
		result
	),

	fn readMDXCameras =
	(
		
		local result = #()
		
		::IOManager.MDXStartReadingChunk()
		
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			
			::IOManager.MDXStartChunkElem()
			
			local cam = Wc3Camera()
			
			cam.name = ::IOManager.readMDXString 80
			
			cam.pivot = ::IOManager.readMDXPosition()
			
			cam.fieldOfView = radToDeg (::IOManager.readMDXFloat())
			
			cam.farClip = ::IOManager.readMDXFloat()
			cam.nearClip = ::IOManager.readMDXFloat()
			
			cam.targetPos = ::IOManager.readMDXPosition()
			
			cam.transformations = TransformationData()
			cam.targetTransformations = TransformationData()
			
			do
			(
				local Anim_tag = ::IOManager.peekMDXHeader()
				local notDone = true
				case Anim_tag of
				(
					
					tag_KCTR: cam.transformations.translation = readMDXAnimation ::IOManager.readMDXPosition
					
					tag_KTTR: cam.targetTransformations.translation = readMDXAnimation ::IOManager.readMDXPosition
					
					tag_KCRL: cam.rotation = readMDXAnimation ::IOManager.readMDXFloat
					
					default: notDone = false
				)
			)
			while notDone
			
			append result cam
		)
		
		result
	),

	fn restorePivots model pivots =
	(
		
		
		for n in model.bones do n.pivot = pivots[n.objId]
		for n in model.helpers do n.pivot = pivots[n.objId]
		for n in model.lights do n.pivot = pivots[n.objId]
		for n in model.attachments do n.pivot = pivots[n.objId]
		for n in model.particleEmitters1 do n.pivot = pivots[n.objId]
		for n in model.particleEmitters2 do n.pivot = pivots[n.objId]
		for n in model.ribbonEmitters do n.pivot = pivots[n.objId]
		for n in model.events do n.pivot = pivots[n.objId]
		for n in model.CollisionShapes do n.pivot = pivots[n.objId]
	),

	fn resolveKeyCollisions keys sequences =
	(
		if keys == undefined or keys.count < 2 then return keys
		
		local seqEndTimes = #()
		local seqStartTimes = #()
		for s in sequences do
		(
			appendIfUnique seqEndTimes s.endTime
			appendIfUnique seqStartTimes s.startTime
		)
		
		local i = keys.count - 1
		while i >= 1 do
		(
			if keys[i].time == keys[i+1].time then
			(
				local t = keys[i].time
				local isEnd = (findItem seqEndTimes t) > 0
				local isStart = (findItem seqStartTimes t) > 0
				
				if isEnd and not isStart then
					deleteItem keys (i+1)
				else
					deleteItem keys i
			)
			i -= 1
		)
		keys
	),

	fn resolveModelCollisions model =
	(
		fn resolveTrans obj seqs =
		(
			local trans = obj.transformations
			if trans == undefined then return undefined
			if trans.translation != undefined and trans.translation.keys != undefined then
				trans.translation.keys = resolveKeyCollisions trans.translation.keys seqs
			if trans.rotation != undefined and trans.rotation.keys != undefined then
				trans.rotation.keys = resolveKeyCollisions trans.rotation.keys seqs
			if trans.scale != undefined and trans.scale.keys != undefined then
				trans.scale.keys = resolveKeyCollisions trans.scale.keys seqs
		)
		local seqs = model.sequences
		for n in model.bones do resolveTrans n seqs
		for n in model.helpers do resolveTrans n seqs
		for n in model.lights do resolveTrans n seqs
		for n in model.attachments do resolveTrans n seqs
		for n in model.particleEmitters1 do resolveTrans n seqs
		for n in model.particleEmitters2 do resolveTrans n seqs
		for n in model.ribbonEmitters do resolveTrans n seqs
		for n in model.events do resolveTrans n seqs
	),

	fn readMDXFile =
	(
		local chunk = ::IOManager.readMDXLong()
		if chunk != tag_MDLX then return undefined
		chunk = ::IOManager.readMDXLong()
		local model = createEmptyWc3Model()
		local pivots
		local lastChunkTag = ""
		local fileSize = undefined
		try
		(
			local currentPos = ftell ::IOManager.instream
			fseek ::IOManager.instream 0 #seek_end
			fileSize = ftell ::IOManager.instream
			fseek ::IOManager.instream currentPos #seek_set
		) catch()
		while chunk != undefined do
		(
			if fileSize != undefined then
			(
				local remaining = fileSize - (ftell ::IOManager.instream) + 4
				if remaining < 8 then exit
			)
			try
			(
				lastChunkTag = ::IOManager.IntAsTag chunk
				case chunk of
				(
					tag_VERS: ::IOManager.MDXSkipChunk()
					tag_MODL: ::IOManager.MDXSkipChunk()
					tag_SEQS: model.sequences = readMDXSequences()
					tag_GLBS: ::IOManager.MDXSkipChunk()
					tag_TEXS: model.textures = readMDXTextures()
					tag_MTLS: model.materials = readMDXMaterials()
					tag_SNDS: ::IOManager.MDXSkipChunk()
					tag_TXAN: model.tvertexAnims = readMDXTVertexAnimations()
					tag_GEOS: model.geosets = readMDXGeosets()
					tag_GEOA: model.geosetAnims = readMDXGeosetAnimations()
					tag_BONE: model.bones = readMDXBones()
					tag_LITE: model.lights = readMDXLights()
					tag_HELP: model.helpers = readMDXHelpers()
					tag_ATCH: model.attachments = readMDXAttachments()
					tag_PIVT: pivots = readPivotPoints()
					tag_PREM: model.particleEmitters1 = readMDXParticleEmitter()
					tag_PRE2: model.particleEmitters2 = readMDXParticleEmitter2()
					tag_SNEM: ::IOManager.MDXSkipChunk()
					tag_RIBB: model.ribbonEmitters = readMDXRibbonEmitter()
					tag_EVTS: model.events = readMDXEventObject()
					tag_CAMS: model.cameras = readMDXCameras()
					tag_CLID: model.collisionShapes = readMDXCollisionShapes()
					default: ::IOManager.MDXSkipChunk()
				)
			)
			catch
			(
				local errMsg = getCurrentException()
				::ErrorReporter.logError "Import" lastChunkTag ("Failed to read chunk: " + errMsg)
				try (::IOManager.MDXSkipChunk()) catch (exit)
			)
			chunk = ::IOManager.readMDXLong()
		)
		if pivots != undefined then
			restorePivots model pivots
		resolveModelCollisions model
		model
	),

	fn InputMDX infile_name =
	(
		local finstream
		local model
		try
		(
			finstream = fopen infile_name "rb"
			if finstream == undefined then
			(
				::ErrorReporter.logError "Import" "File" ("Cannot open file: " + infile_name)
				return undefined
			)
			::IOManager.SetInStream finstream
			model = readMDXFile()
		)
		catch
		(
			local errMsg = getCurrentException()
			::ErrorReporter.logError "Import" "File" ("Error reading MDX: " + errMsg)
			format "*** WhiteoutDex MDX Import Error: %\n" errMsg
			model = undefined
		)
		try (if finstream != undefined then fclose finstream) catch()
		gc()
		model 
	)
)

global WhiteoutDexMDXReader = WhiteoutDexMDXReader()