/*
	WhiteoutDexReaderReforge1200.ms
	Reforged MDX v1200 Reader fuer WhiteoutDex Toolkit
	In pre-start-up scripts parts/ ablegen
*/

global tag_TANG = 0x474e4154
global tag_SKIN = 0x4e494b53
global tag_CORN = 0x4e524f43
global tag_FAFX = 0x58464146
global tag_BPOS = 0x534f5042

struct Wc3CornEmitter (mdlObj, lifeSpan=0.0, emissionRate=0.0, speed=0.0, color=[255,255,255], alpha=1.0, replaceableId=0, path="", flags="", visibility, emissionRateAnim, alphaAnim, type=#Wc3CornEmitter, ex)
struct Wc3FaceFX (name="", path="", ex)
struct Wc3SkinVertex (bones=#(0,0,0,0), weights=#(0,0,0,0))

fn peekMDXVersion infile_name =
(
	local ver = 800
	local fs
	try
	(
		fs = fopen infile_name "rb"
		if fs == undefined then return 800
		local magic = readLong fs #unsigned
		local tag = readLong fs #unsigned
		if tag == tag_VERS then
		(
			local size = readLong fs #unsigned
			ver = readLong fs #unsigned
		)
		fclose fs
	)
	catch
	(
		try (if fs != undefined then fclose fs) catch()
		ver = 800
	)
	ver
)

struct WhiteoutDexMDXReaderV1200
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

	fn readMDXAnimateableParameter param &func = (param.type = #Animated; param.data = this.readMDXAnimation func),
	fn readMDXParameter &func = (local p = ParameterValue(); p.type = #Static; p.data = func(); p),

	fn createEmptyWc3Model =
	(
		local model = Wc3Model()
		model.version = 1200
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
		local numSeqs = seqsChunkSize / 132
		local result = #()
		result[numSeqs] = undefined
		local pos = ftell ::IOManager.instream
		for i = 1 to numSeqs do
		(
			fseek ::IOManager.instream pos #seek_set
			local a = Wc3Sequence()
			a.name = ::IOManager.readMDXString 80
			a.startTime = ::IOManager.readMDXTime()
			a.endTime = ::IOManager.readMDXTime()
			local ms = ::IOManager.readMDXFloat()
			if ms != 0.0 then a.movespeed = ms
			local fl = ::IOManager.readMDXLong()
			a.loop = (bit.and fl 0x1) != 0   -- FIX: war == 0
			local ra = ::IOManager.readMDXFloat()
			if ra != 0.0 then a.rarity = ra
			::IOManager.readMDXLong()
			local ext = ::IOManager.readMDXExtents()
			local v1 = ext.minExtent
			local v2 = ext.maxExtent
			ext.minExtent = [amin v1.y v2.y, amin -v1.x -v2.x, v1.z]
			ext.maxExtent = [amax v1.y v2.y, amax -v1.x -v2.x, v2.z]
			a.extents = ext
			result[i] = a
			pos += 132
		)
		fseek ::IOManager.instream pos #seek_set
		result
	),

	fn readMDXTextures =
	(
		local n = (::IOManager.readMDXLong()) / 268
		local result = #()
		result[n] = undefined
		for i = 1 to n do
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

	
	-- Reforged: liest zusaetzlich emissiveGain, fresnelColor, fresnelOpacity, fresnelTeamColor
	fn readMDXLayers =
	(
		local tag = ::IOManager.readMDXLong()
		if tag != tag_LAYS then throw "Expected LAYS"
		local numLayers = ::IOManager.readMDXLong()
		local result = #()
		result[numLayers] = undefined
		for i = 1 to numLayers do
		(
			local layerStart = ftell ::IOManager.instream
			local layerSize = ::IOManager.readMDXLong()
			local layerEnd = layerStart + layerSize
			local l = Wc3Layer()
			l.filtermode = (::IOManager.readMDXLong()) + 1
			local flags = ::IOManager.IntToFlags (::IOManager.readMDXLong()) 8
			l.unshaded = flags[1]
			l.SphereEnvMap = flags[2]
			l.twosided = flags[5]
			l.unfogged = flags[6]
			l.nodephtest = flags[7]
			l.nodephset = flags[8]
			l.textures = readMDXParameter ::IOManager.readMDXLong
			local tvid = ::IOManager.readMDXLong()
			if tvid != -1 then l.tvertexid = tvid + 1
			l.coordId = (::IOManager.readMDXLong()) + 1
			l.alpha = readMDXParameter ::IOManager.readMDXFloat

			-- Reforged: Extra Material-Felder + Texture Binding Table
			local nxt = ::IOManager.peekMDXHeader()
			local isAnim = (nxt == tag_KMTF or nxt == tag_KMTA or nxt == tag_KMTE)
			local left = layerEnd - (ftell ::IOManager.instream)
			if (not isAnim) and left >= 32 then
			(
				-- Fresnel-Daten (24 bytes)
				l.emissiveGain = ::IOManager.readMDXFloat()              -- Reforged
				local fr = ::IOManager.readMDXFloat()                    -- Reforged
				local fg = ::IOManager.readMDXFloat()                    -- Reforged
				local fb = ::IOManager.readMDXFloat()                    -- Reforged
				l.fresnelColor = [fr, fg, fb]                            -- Reforged
				l.fresnelOpacity = ::IOManager.readMDXFloat()            -- Reforged
				l.fresnelTeamColor = ::IOManager.readMDXFloat()          -- Reforged

				-- Texture Binding Table (8 + numBindings*8 bytes)
				local shaderFlag = ::IOManager.readMDXLong()             -- Reforged: 0 oder 1
				local numBindings = ::IOManager.readMDXLong()            -- Reforged: Anzahl Texture Slots
				format "  Layer %: % texture bindings\n" i numBindings
				for b = 1 to numBindings do
				(
					local texId = ::IOManager.readMDXLong()               -- Reforged
					local slotType = ::IOManager.readMDXLong()             -- Reforged
					case slotType of
					(
						0: () -- Diffuse: schon in l.textures vom Standard-Header
						1: l.normalTexId = texId + 1       -- Reforged PBR Normal
						2: l.ormTexId = texId + 1          -- Reforged PBR ORM
						3: l.emissiveTexId = texId + 1     -- Reforged PBR Emissive
						4: l.teamColorTexId = texId + 1    -- Reforged Team Color
						5: l.envMapTexId = texId + 1       -- Reforged Environment
					)
					-- Slot 0 Diffuse: ueberschreibt den Standard texId
					if slotType == 0 and l.textures.type == #Static then
						l.textures.data = texId
				)

				-- Rest bis zu den Animation-Tags ueberspringen
				local rem = layerEnd - (ftell ::IOManager.instream)
				local nc = ::IOManager.peekMDXHeader()
				if (nc != tag_KMTF and nc != tag_KMTA and nc != tag_KMTE) and rem > 0 then
					::IOManager.jumpMDXReading rem
			)

			do
			(
				local animTag = ::IOManager.peekMDXHeader()
				local nd = true
				case animTag of
				(
					tag_KMTF: readMDXAnimateableParameter l.textures ::IOManager.readMDXLong
					tag_KMTA: readMDXAnimateableParameter l.alpha ::IOManager.readMDXFloat
					tag_KMTE: (this.readMDXAnimation ::IOManager.readMDXFloat)  -- Reforged: lesen + verwerfen
					default: nd = false
				)
			)
			while (nd)
			-- Safety-Seek: sicherstellen dass der Stream exakt am Layer-Ende steht
			-- Faengt auch zukuenftige unbekannte Reforged-Tags ab
			local finalRem = layerEnd - (ftell ::IOManager.instream)
			if finalRem > 0 then ::IOManager.jumpMDXReading finalRem
			::IOManager.writerTransformKeys l.alpha (fn tmp a = a*100.0)
			::IOManager.writerTransformKeys l.textures (fn tmp a = a + 1)
			result[i] = l
		)
		result
	),

	fn readMDXMaterials =
	(
		local result = #()
		::IOManager.MDXStartReadingChunk()
		local i = 0
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			::IOManager.MDXStartChunkElem()
			local mat = Wc3Material()
			mat.priorityPlane = ::IOManager.readMDXLong()
			local mf = ::IOManager.IntToFlags (::IOManager.readMDXLong()) 6
			mat.isConstantColor = mf[1]
			mat.isSortPrimsNearZ = mf[4]
			mat.isSortPrimsFarZ = mf[5]
			mat.isFullResolution = mf[6]
			mat.layers = readMDXLayers()
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
				local animTag = ::IOManager.peekMDXHeader()
				local nd = true
				case animTag of
				(
					tag_KTAT: tv.transformation.translation = readMDXAnimation ::IOManager.readMDXPosition
					tag_KTAR: tv.transformation.rotation = readMDXAnimation ::IOManager.readMDXQuaternion
					tag_KTAS: tv.transformation.scale = readMDXAnimation ::IOManager.readMDXScale
					default: nd = false
				)
			)
			while nd
			i += 1
			tv.id = i
			append result tv
		)
		result
	),

	-- Reforged: konvertiert SKIN-Daten zu matrixindices
	fn buildMatrixIndicesFromSkin skinVerts nv matrices =
	(
		local mi = #()
		mi[nv] = 1
		local b2g = #()
		for gi = 1 to matrices.count do
			for bid in matrices[gi] do
			(
				if b2g[bid] == undefined then b2g[bid] = #()
				appendIfUnique b2g[bid] gi
			)
		for v = 1 to nv do
		(
			local sv = skinVerts[v]
			local bb = sv.bones[1] + 1
			local bw = sv.weights[1]
			for b = 2 to 4 do
				if sv.weights[b] > bw then (bw = sv.weights[b]; bb = sv.bones[b] + 1)
			local fg = 1
			if bb > 0 and bb <= b2g.count and b2g[bb] != undefined then fg = b2g[bb][1]
			mi[v] = fg
		)
		mi
	),

	-- Reforged: liest zusaetzlich TANG, SKIN, LOD, LODname
	fn readMDXGeosets =
	(
		local result = #()
		::IOManager.MDXStartReadingChunk()
		local id = 0
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			::IOManager.MDXStartChunkElem()
			local geo = Wc3Geoset()
			local vertices
			local tvertices = #()
			local normals
			local vertgroups = #()
			local faces = #()
			local matrices = #()
			local faceTypes
			local faceGroups
			local tangents = #()
			local skinVertices = #()
			local numVerts = 0
			do
			(
				local gt = ::IOManager.readMDXLong()
				local nd = true
				case gt of
				(
					tag_VRTX: (numVerts = ::IOManager.readMDXLong(); vertices = ::IOManager.readMDXArray numVerts ::IOManager.readMDXPosition)
					tag_NRMS: (local n = ::IOManager.readMDXLong(); normals = ::IOManager.readMDXArray n ::IOManager.readMDXPosition)
					tag_PTYP: (local n = ::IOManager.readMDXLong(); faceTypes = ::IOManager.readMDXArray n ::IOManager.readMDXLong)
					tag_PCNT: (local n = ::IOManager.readMDXLong(); faceGroups = ::IOManager.readMDXArray n ::IOManager.readMDXLong)
					tag_PVTX:
					(
						local n = ::IOManager.readMDXLong()
						for i = 1 to faceGroups.count do
						(
							local nf = faceGroups[i] / 3
							for j = 1 to nf do
							(
								local f = Point3 0 0 0
								f[1] = ::IOManager.readMDXShort() + 1
								f[2] = ::IOManager.readMDXShort() + 1
								f[3] = ::IOManager.readMDXShort() + 1
								append faces f
							)
						)
					)
					tag_GNDX: (local n = ::IOManager.readMDXLong(); if n > 0 then vertgroups = ::IOManager.readMDXArray n (fn tmp = ::IOManager.readMDXByte() + 1))
					tag_MTGC: (local n = ::IOManager.readMDXLong(); matrices[n] = undefined; for i = 1 to n do (local s = ::IOManager.readMDXLong(); matrices[i] = #(); matrices[i][s] = undefined))
					tag_MATS: (local n = ::IOManager.readMDXLong(); for m in matrices do for i = 1 to m.count do m[i] = ::IOManager.readMDXLong() + 1)
					default: nd = false
				)
			)
			while nd

			::IOManager.jumpMDXReading -4
			geo.vertices = vertices
			geo.normals = normals
			geo.faces = faces
			geo.matrices = matrices
			geo.material = ::IOManager.readMDXLong() + 1
			::IOManager.jumpMDXReading 4
			local fl = bit.and (::IOManager.readMDXLong()) 0x04
			geo.unselectable = (fl != 0)

			-- Reforged: LOD + LODname (v800 hat das nicht)
			geo.lod = ::IOManager.readMDXLong()
			geo.lodName = ::IOManager.readMDXString 80

			geo.mainExtents = ::IOManager.readMDXExtents()
			local ne = ::IOManager.readMDXLong()
			::IOManager.jumpMDXReading (28 * ne)

			-- Reforged: TANG und SKIN chunks nach Extents
			do
			(
				local gt = ::IOManager.readMDXLong()
				local nd = true
				case gt of
				(
					tag_TANG:                                              -- Reforged
					(
						local n = ::IOManager.readMDXLong()
						tangents = #()
						tangents[n] = undefined
						for i = 1 to n do tangents[i] = Point4 (::IOManager.readMDXFloat()) (::IOManager.readMDXFloat()) (::IOManager.readMDXFloat()) (::IOManager.readMDXFloat())
					)
					tag_SKIN:                                              -- Reforged
					(
						local nb = ::IOManager.readMDXLong()
						local ns = nb / 8
						skinVertices = #()
						skinVertices[ns] = undefined
						for i = 1 to ns do
						(
							local sv = Wc3SkinVertex()
							sv.bones = #(::IOManager.readMDXByte(), ::IOManager.readMDXByte(), ::IOManager.readMDXByte(), ::IOManager.readMDXByte())
							sv.weights = #(::IOManager.readMDXByte(), ::IOManager.readMDXByte(), ::IOManager.readMDXByte(), ::IOManager.readMDXByte())
							skinVertices[i] = sv
						)
					)
					tag_UVAS: (local n = ::IOManager.readMDXLong())
					tag_UVBS: (local n = ::IOManager.readMDXLong(); append tvertices (::IOManager.readMDXArray n ::IOManager.readMDXTvertex))
					default: nd = false
				)
			)
			while nd

			::IOManager.jumpMDXReading -4
			geo.tangents = tangents       -- Reforged
			geo.skinData = skinVertices   -- Reforged
			if skinVertices.count > 0 and vertgroups.count == 0 then
				vertgroups = buildMatrixIndicesFromSkin skinVertices numVerts matrices
			geo.matrixindices = vertgroups
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
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			::IOManager.MDXStartChunkElem()
			local ga = Wc3GeosetAnim()
			ga.alpha = readMDXParameter ::IOManager.readMDXFloat
			local fl = ::IOManager.IntToFlags (::IOManager.readMDXLong()) 2
			ga.dropShadow = fl[1]
			ga.usesColor = fl[2]
			ga.color = readMDXParameter ::IOManager.readMDXColor
			ga.geoset = (::IOManager.readMDXLong()) + 1
			do
			(
				local animTag = ::IOManager.peekMDXHeader()
				local nd = true
				case animTag of
				(
					tag_KGAC: readMDXAnimateableParameter ga.color ::IOManager.readMDXColor
					tag_KGAO: readMDXAnimateableParameter ga.alpha ::IOManager.readMDXFloat
					default: nd = false
				)
			)
			while nd
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
			local animTag = ::IOManager.peekMDXHeader()
			local nd = true
			case animTag of
			(
				tag_KGTR: obj.transformations.translation = readMDXAnimation ::IOManager.readMDXPosition
				tag_KGRT: obj.transformations.rotation = readMDXAnimation ::IOManager.readMDXQuaternion
				tag_KGSC: obj.transformations.scale = readMDXAnimation ::IOManager.readMDXScale
				default: nd = false
			)
		)
		while nd
		#(obj, flags)
	),

	-- Reforged: BONE chunk enthaelt auch Helpers
	-- Use node flags (bit 0x100 = Bone) instead of geosetId to classify,
	-- because Reforged bones always have geosetId==-1 (SKIN chunk handles mapping).
	fn readMDXBonesAndHelpers =
	(
		local bonesList = #()
		local helpersList = #()
		::IOManager.MDXStartReadingChunk()
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			::IOManager.MDXStartChunkElem specialCase:8
			local tmp = readMDXNode()
			local mdlobj = tmp[1]
			local flags = tmp[2]
			local gid = readLong ::IOManager.instream
			local gaid = readLong ::IOManager.instream
			if flags[9] then       -- Bit 0x100 = Bone type flag
			(
				local b = Wc3Bone()
				b.mdlObj = mdlobj
				mdlobj.sub = b
				append bonesList mdlobj
			)
			else
			(
				local h = Wc3Helper()
				h.mdlObj = mdlobj
				mdlobj.sub = h
				append helpersList mdlobj
			)
		)
		#(bonesList, helpersList)
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
			case lite.lightType of (0: lite.lightType=#Omnidirectional; 1: lite.lightType=#Directional; 2: lite.lightType=#Ambient; default: lite.lightType=#Omnidirectional)
			lite.attenuationStart = readMDXParameter ::IOManager.readMDXFloat
			lite.attenuationEnd = readMDXParameter ::IOManager.readMDXFloat
			lite.color = readMDXParameter ::IOManager.readMDXColor
			lite.intensity = readMDXParameter ::IOManager.readMDXFloat
			lite.ambColor = readMDXParameter ::IOManager.readMDXColor
			lite.ambIntensity = readMDXParameter ::IOManager.readMDXFloat
			lite.visibility = ParameterValue()
			do
			(
				local animTag = ::IOManager.peekMDXHeader()
				local nd = true
				case animTag of
				(
					tag_KLAV: readMDXAnimateableParameter lite.visibility ::IOManager.readMDXFloat
					tag_KLAC: readMDXAnimateableParameter lite.color ::IOManager.readMDXColor
					tag_KLAI: readMDXAnimateableParameter lite.intensity ::IOManager.readMDXFloat
					tag_KLBC: readMDXAnimateableParameter lite.ambColor ::IOManager.readMDXColor
					tag_KLBI: readMDXAnimateableParameter lite.ambIntensity ::IOManager.readMDXFloat
					tag_KLAS: readMDXAnimateableParameter lite.attenuationStart ::IOManager.readMDXFloat
					tag_KLAE: readMDXAnimateableParameter lite.attenuationEnd ::IOManager.readMDXFloat
					default: nd = false
				)
			)
			while nd
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
			local att = Wc3Attachment()
			local tmp = readMDXNode()
			local mdlobj = tmp[1]
			att.mdlObj = mdlobj
			mdlobj.sub = att
			att.outPath = ::IOManager.readMDXString 260
			att.attachmentId = ::IOManager.readMDXLong()
			att.visibility = ParameterValue()
			do
			(
				local animTag = ::IOManager.peekMDXHeader()
				local nd = true
				case animTag of (tag_KATV: readMDXAnimateableParameter att.visibility ::IOManager.readMDXFloat; default: nd = false)
			)
			while nd
			append result mdlobj
		)
		result
	),

	fn readMDXParticleEmitter =
	(
		local result = #()
		::IOManager.MDXStartReadingChunk()
		while not (::IOManager.MDXHasChunkEnded()) do
		(
			::IOManager.MDXStartChunkElem()
			::IOManager.readMDXLong()
			local pe = Wc3ParticleEmitter()
			local tmp = readMDXNode()
			local mdlobj = tmp[1]
			pe.mdlObj = mdlobj
			mdlobj.sub = pe
			pe.emissionRate = readMDXParameter ::IOManager.readMDXFloat
			pe.gravity = readMDXParameter ::IOManager.readMDXFloat
			pe.longitude = readMDXParameter ::IOManager.readMDXFloat
			pe.latitude = readMDXParameter ::IOManager.readMDXFloat
			pe.path = ::IOManager.readMDXString 260
			::IOManager.jumpMDXReading 4
			pe.lifespan = ::IOManager.readMDXFloat()
			pe.initVelocity = ::IOManager.readMDXFloat()
			pe.visibility = ParameterValue()
			do
			(
				local animTag = ::IOManager.peekMDXHeader()
				local nd = true
				case animTag of
				(
					tag_KPEE: readMDXAnimateableParameter pe.emissionRate ::IOManager.readMDXFloat
					tag_KPEG: readMDXAnimateableParameter pe.gravity ::IOManager.readMDXFloat
					tag_KPLN: readMDXAnimateableParameter pe.longitude ::IOManager.readMDXFloat
					tag_KPEL: readMDXAnimateableParameter pe.latitude ::IOManager.readMDXFloat
					tag_KPES: readMDXAnimateableParameter pe.visibility ::IOManager.readMDXFloat
					tag_KPEV: readMDXAnimateableParameter pe.visibility ::IOManager.readMDXFloat
					default: nd = false
				)
			)
			while nd
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
			local pe2 = Wc3ParticleEmitter2()
			local tmp = readMDXNode()
			local mdlobj = tmp[1]
			pe2.mdlObj = mdlobj
			mdlobj.sub = pe2
			pe2.speed = readMDXParameter ::IOManager.readMDXFloat
			pe2.variation = readMDXParameter ::IOManager.readMDXFloat
			pe2.latitude = readMDXParameter ::IOManager.readMDXFloat
			pe2.gravity = readMDXParameter ::IOManager.readMDXFloat
			pe2.lifeSpan = ::IOManager.readMDXFloat()
			pe2.emissionRate = readMDXParameter ::IOManager.readMDXFloat
			pe2.length = readMDXParameter ::IOManager.readMDXFloat
			pe2.width = readMDXParameter ::IOManager.readMDXFloat
			pe2.filtermode = (::IOManager.readMDXLong()) + 1
			pe2.rows = ::IOManager.readMDXLong()
			pe2.columns = ::IOManager.readMDXLong()
			local pt = ::IOManager.readMDXLong()
			case pt of (0: pe2.particleType=#Head; 1: pe2.particleType=#Tail; 2: pe2.particleType=#Both; default: pe2.particleType=#Head)
			pe2.tailLength = ::IOManager.readMDXFloat()
			pe2.time = ::IOManager.readMDXFloat()
			local sr = ::Utils.round ((::IOManager.readMDXFloat()) * 255)
			local sg = ::Utils.round ((::IOManager.readMDXFloat()) * 255)
			local sb = ::Utils.round ((::IOManager.readMDXFloat()) * 255)
			pe2.startColor = [sr, sg, sb]
			local mr = ::Utils.round ((::IOManager.readMDXFloat()) * 255)
			local mg = ::Utils.round ((::IOManager.readMDXFloat()) * 255)
			local mb = ::Utils.round ((::IOManager.readMDXFloat()) * 255)
			pe2.midColor = [mr, mg, mb]
			local er = ::Utils.round ((::IOManager.readMDXFloat()) * 255)
			local eg = ::Utils.round ((::IOManager.readMDXFloat()) * 255)
			local eb = ::Utils.round ((::IOManager.readMDXFloat()) * 255)
			pe2.endColor = [er, eg, eb]
			pe2.alpha = Point3 (::IOManager.readMDXByte()) (::IOManager.readMDXByte()) (::IOManager.readMDXByte())
			pe2.scaling = Point3 (::IOManager.readMDXFloat()) (::IOManager.readMDXFloat()) (::IOManager.readMDXFloat())
			pe2.lifeSpanUVAnim = ::IOManager.readMDXVector3Long()
			pe2.decayUVAnim = ::IOManager.readMDXVector3Long()
			pe2.TailUVAnim = ::IOManager.readMDXVector3Long()
			pe2.TailDecayUVAnim = ::IOManager.readMDXVector3Long()
			-- v1200 hat dasselbe Format wie v800:
			-- PE2 Flags kommen aus node flags (bits 15-20), nicht als packed int
			local nodeflags = tmp[2]
			pe2.unshaded = nodeflags[16]
			pe2.sortPrimsFarZ = nodeflags[17]
			pe2.lineEmitter = nodeflags[18]
			pe2.unfogged = nodeflags[19]
			pe2.modelSpace = nodeflags[20]
			pe2.xyQuad = nodeflags[21]
			pe2.texture = ::IOManager.readMDXLong() + 1
			-- squirt als einzelner Long (wie v800)
			pe2.squirt = ::IOManager.readMDXLong()
			if pe2.squirt == 1 then pe2.squirt = true else pe2.squirt = false
			-- korrekte Reihenfolge: priorityPlane zuerst, dann replaceableId
			pe2.priorityPlane = ::IOManager.readMDXLong()
			pe2.replaceableId = ::IOManager.readMDXLong() + 1
			pe2.visibility = ParameterValue()
			do
			(
				local animTag = ::IOManager.peekMDXHeader()
				local nd = true
				case animTag of
				(
					tag_KP2S: readMDXAnimateableParameter pe2.speed ::IOManager.readMDXFloat
					tag_KP2R: readMDXAnimateableParameter pe2.variation ::IOManager.readMDXFloat
					tag_KP2L: readMDXAnimateableParameter pe2.latitude ::IOManager.readMDXFloat
					tag_KP2G: readMDXAnimateableParameter pe2.gravity ::IOManager.readMDXFloat
					tag_KP2E: readMDXAnimateableParameter pe2.emissionRate ::IOManager.readMDXFloat
					tag_KP2N: readMDXAnimateableParameter pe2.length ::IOManager.readMDXFloat
					tag_KP2W: readMDXAnimateableParameter pe2.width ::IOManager.readMDXFloat
					tag_KP2V: readMDXAnimateableParameter pe2.visibility ::IOManager.readMDXFloat
					default: nd = false
				)
			)
			while nd
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
				local animTag = ::IOManager.peekMDXHeader()
				local nd = true
				case animTag of
				(
					tag_KRHA: readMDXAnimateableParameter rib.heightAbove ::IOManager.readMDXFloat
					tag_KRHB: readMDXAnimateableParameter rib.heightBelow ::IOManager.readMDXFloat
					tag_KRAL: readMDXAnimateableParameter rib.alpha ::IOManager.readMDXFloat
					tag_KRCO: readMDXAnimateableParameter rib.color ::IOManager.readMDXColor
					tag_KRTX: readMDXAnimateableParameter rib.textureSlot ::IOManager.readMDXLong
					tag_KRVS: readMDXAnimateableParameter rib.visibility ::IOManager.readMDXFloat
					default: nd = false
				)
			)
			while nd
			append result mdlobj
		)
		result
	),

	fn readMDXEventObject =
	(
		fn readMDXEventTrack =
		(
			::IOManager.jumpMDXReading 4
			local ad = AnimationData()
			local nt = ::IOManager.readMDXLong()
			local gs = ::IOManager.readMDXLong()
			ad.type = #NonInterp
			if nt > 0 then
			(
				local t = #()
				t[nt] = undefined
				for i = 1 to nt do t[i] = KeyData (::IOManager.readMDXTime()) true
				ad.keys = t
			)
			else ad.keys = #()
			::IOManager.MDXFixChunkSize (12 + 4 * nt)
			ad
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
				local animTag = ::IOManager.peekMDXHeader()
				local nd = true
				case animTag of (tag_KEVT: ev.eventTrack = readMDXEventTrack(); default: nd = false)
			)
			while nd
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
			if col.shape == 2 then ::IOManager.MDXFixChunkSize 20 else ::IOManager.MDXFixChunkSize 28
			col.vertices = #()
			append col.vertices (::IOManager.readMDXPosition())
			if (col.shape == 2) then col.boundsRadius = ::IOManager.readMDXFloat()
			else append col.vertices (::IOManager.readMDXPosition())
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
				local animTag = ::IOManager.peekMDXHeader()
				local nd = true
				case animTag of
				(
					tag_KCTR: cam.transformations.translation = readMDXAnimation ::IOManager.readMDXPosition
					tag_KTTR: cam.targetTransformations.translation = readMDXAnimation ::IOManager.readMDXPosition
					tag_KCRL: cam.rotation = readMDXAnimation ::IOManager.readMDXFloat
					default: nd = false
				)
			)
			while nd
			append result cam
		)
		result
	),

	fn readPivotPoints =
	(
		local n = (::IOManager.readMDXLong()) / 12
		local r = #()
		r[n] = undefined
		for i = 1 to n do r[i] = ::IOManager.readMDXPosition()
		r
	),

	-- Reforged: Corn Emitters (nur in v1200)
	fn readMDXCornEmitters =
	(
		local result = #()
		local chunkBytes = ::IOManager.readMDXLong()
		local chunkEnd = (ftell ::IOManager.instream) + chunkBytes
		while (ftell ::IOManager.instream) < (chunkEnd - 4) do
		(
			local elemStart = ftell ::IOManager.instream
			local elemSize = ::IOManager.readMDXLong()
			local elemEnd = elemStart + elemSize
			::IOManager.chunkSize -= elemSize
			::IOManager.readMDXLong()      -- Node-Inclusive-Size ueberspringen
			local corn = Wc3CornEmitter()
			local tmp = readMDXNode()
			local mdlobj = tmp[1]
			corn.mdlObj = mdlobj
			mdlobj.sub = corn

			-- CORN-spezifische Daten (nach Node, vor Animations-Tracks)
			corn.lifeSpan = ::IOManager.readMDXFloat()
			corn.emissionRate = ::IOManager.readMDXFloat()
			corn.speed = ::IOManager.readMDXFloat()
			local cr = ::IOManager.readMDXFloat()
			local cg = ::IOManager.readMDXFloat()
			local cb = ::IOManager.readMDXFloat()
			corn.color = [cr * 255.0, cg * 255.0, cb * 255.0]
			corn.alpha = ::IOManager.readMDXFloat()
			corn.replaceableId = ::IOManager.readMDXLong()
			corn.path = ::IOManager.readMDXString 260
			corn.flags = ::IOManager.readMDXString 260

			-- Animations-Tracks
			corn.visibility = ParameterValue()
			corn.emissionRateAnim = ParameterValue type:#Static data:corn.emissionRate
			corn.alphaAnim = ParameterValue type:#Static data:corn.alpha
			do
			(
				local animTag = ::IOManager.peekMDXHeader()
				local nd = true
				case animTag of
				(
					tag_KPPA: readMDXAnimateableParameter corn.alphaAnim ::IOManager.readMDXFloat
					tag_KPPV: readMDXAnimateableParameter corn.visibility ::IOManager.readMDXFloat
					tag_KP2V: readMDXAnimateableParameter corn.visibility ::IOManager.readMDXFloat
					tag_KPPE: readMDXAnimateableParameter corn.emissionRateAnim ::IOManager.readMDXFloat
					default: nd = false
				)
			)
			while nd
			-- Safety-Seek: sicherstellen dass der Stream exakt am Element-Ende steht
			local finalRem = elemEnd - (ftell ::IOManager.instream)
			if finalRem > 0 then ::IOManager.jumpMDXReading finalRem
			append result mdlobj
		)
		result
	),

	-- Reforged: FaceFX Targets (nur in v1200)
	fn readMDXFaceFX =
	(
		local nb = ::IOManager.readMDXLong()
		local ne = nb / 340
		local r = #()
		r[ne] = undefined
		for i = 1 to ne do
		(
			local fx = Wc3FaceFX()
			fx.name = ::IOManager.readMDXString 80
			fx.path = ::IOManager.readMDXString 260
			r[i] = fx
		)
		r
	),

	-- Reforged: Bind Poses (nur in v1200)
    fn readMDXBindPoses =
    (
        local nb = ::IOManager.readMDXLong()
        local ne = readLong ::IOManager.instream #unsigned
        local r = #()
        r[ne] = undefined
        for i = 1 to ne do
        (
            -- 4x3 row-major: X-Achse(3), Y-Achse(3), Z-Achse(3), Translation(3)
            local Xx = ::IOManager.readMDXFloat()
            local Xy = ::IOManager.readMDXFloat()
            local Xz = ::IOManager.readMDXFloat()
            local Yx = ::IOManager.readMDXFloat()
            local Yy = ::IOManager.readMDXFloat()
            local Yz = ::IOManager.readMDXFloat()
            local Zx = ::IOManager.readMDXFloat()
            local Zy = ::IOManager.readMDXFloat()
            local Zz = ::IOManager.readMDXFloat()
            local Tx = ::IOManager.readMDXFloat()
            local Ty = ::IOManager.readMDXFloat()
            local Tz = ::IOManager.readMDXFloat()

            -- MDX -> Max Koordinaten-Konvertierung
            -- Rotation: R_max = P * R_mdx * P^T
            -- wobei P: (x,y,z) -> (y,-x,z)
            -- Ergebnis: row1=[Yy,-Yx,Yz]  row2=[-Xy,Xx,-Xz]  row3=[Zy,-Zx,Zz]
            -- Translation: t_max = [Ty, -Tx, Tz]
            r[i] = matrix3 [Yy, -Yx, Yz] [-Xy, Xx, -Xz] [Zy, -Zx, Zz] [Ty, -Tx, Tz]
        )
        r
    ),

	fn restorePivots model pivots =
	(
		for n in model.bones do try (n.pivot = pivots[n.objId]) catch()
		for n in model.helpers do try (n.pivot = pivots[n.objId]) catch()
		for n in model.lights do try (n.pivot = pivots[n.objId]) catch()
		for n in model.attachments do try (n.pivot = pivots[n.objId]) catch()
		for n in model.particleEmitters1 do try (n.pivot = pivots[n.objId]) catch()
		for n in model.particleEmitters2 do try (n.pivot = pivots[n.objId]) catch()
		for n in model.ribbonEmitters do try (n.pivot = pivots[n.objId]) catch()
		for n in model.events do try (n.pivot = pivots[n.objId]) catch()
		for n in model.CollisionShapes do try (n.pivot = pivots[n.objId]) catch()
		if model.cornEmitters != undefined then                       -- Reforged
			for n in model.cornEmitters do try (n.pivot = pivots[n.objId]) catch()
	),

	fn readMDXFile =
	(
		local chunk = ::IOManager.readMDXLong()
		if chunk != tag_MDLX then return undefined
		chunk = ::IOManager.readMDXLong()
		local model = createEmptyWc3Model()
		model.cornEmitters = #()      -- Reforged
		model.faceFxTargets = #()     -- Reforged
		model.bindPoses = #()         -- Reforged
		local pivots
		local lastChunkTag = ""
		local fileSize = undefined
		try (local cp = ftell ::IOManager.instream; fseek ::IOManager.instream 0 #seek_end; fileSize = ftell ::IOManager.instream; fseek ::IOManager.instream cp #seek_set) catch()
		while chunk != undefined do
		(
			if fileSize != undefined then (local rem = fileSize - (ftell ::IOManager.instream) + 4; if rem < 8 then exit)
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
					tag_BONE: (local bh = readMDXBonesAndHelpers(); model.bones = bh[1]; model.helpers = bh[2])
					tag_HELP: (local hlp = readMDXHelpers(); join model.helpers hlp)
					tag_LITE: model.lights = readMDXLights()
					tag_ATCH: model.attachments = readMDXAttachments()
					tag_PIVT: pivots = readPivotPoints()
					tag_PREM: model.particleEmitters1 = readMDXParticleEmitter()
					tag_PRE2: model.particleEmitters2 = readMDXParticleEmitter2()
					tag_SNEM: ::IOManager.MDXSkipChunk()
					tag_RIBB: model.ribbonEmitters = readMDXRibbonEmitter()
					tag_EVTS: model.events = readMDXEventObject()
					tag_CAMS: model.cameras = readMDXCameras()
					tag_CLID: model.collisionShapes = readMDXCollisionShapes()
					tag_CORN: model.cornEmitters = readMDXCornEmitters()       -- Reforged
					tag_FAFX: model.faceFxTargets = readMDXFaceFX()            -- Reforged
					tag_BPOS: model.bindPoses = readMDXBindPoses()             -- Reforged
					default: ::IOManager.MDXSkipChunk()
				)
			)
			catch
			(
				format "*** V1200 Error in '%': %\n" lastChunkTag (getCurrentException()) to:listener
				try (::IOManager.MDXSkipChunk()) catch (exit)
			)
			chunk = ::IOManager.readMDXLong()
		)
		if pivots != undefined then restorePivots model pivots
		model
	),

	fn InputMDX infile_name =
	(
		local finstream
		local model
		try
		(
			finstream = fopen infile_name "rb"
			if finstream == undefined then (format "*** V1200: Cannot open: %\n" infile_name to:listener; return undefined)
			::IOManager.SetInStream finstream
			model = readMDXFile()
		)
		catch
		(
			format "*** V1200 Error: %\n" (getCurrentException()) to:listener
			model = undefined
		)
		try (if finstream != undefined then fclose finstream) catch()
		gc()
		model
	)
)

global WhiteoutDexMDXReaderV1200 = WhiteoutDexMDXReaderV1200()
format "WhiteoutDex V1200 Reforged Reader loaded.\n"

