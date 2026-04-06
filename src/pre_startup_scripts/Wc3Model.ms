/*
    Wc3Model.ms
    
    Data model structs for Warcraft 3 MDX/MDL format.
    Defines all entity types: sequences, textures, materials, geosets,
    bones, helpers, lights, attachments, emitters, cameras, collision shapes.
    
    "ex" field convention:
        Present on most structs as an extension/extra-data slot.
        Used for storing additional format-specific flags, user data,
        or future compatibility data that doesn't fit standard fields.
        Type: typically undefined, or a struct/array with extra properties.
*/

struct Wc3Extent (
	minExtent,
	maxExtent,
	bound
)

struct Wc3Sequence (
	name, 
	startTime, 
	endTime, 
	loop, 
	rarity, 
	movespeed,
	extents,
	ex,
	sharedGroup   -- Reforged v1200: Name der Master-Sequenz (undefined = nicht shared)
)

struct Wc3Texture (
	path,
	replaceableId,
	wrapWidth,
	wrapHeight,
	id,
	ex
)

struct Wc3Layer (
	filtermode, 
	alpha, 
	color, 
	twosided, 
	unshaded, 
	unfogged, 
	nodephtest,
	nodephset, 
	unselectable,
	textures,
	coordId,
	tvertexid,
	Wrap_Height,
	Wrap_Width,
	ConstantColor,
	SortPrimsNearZ,
	SortPrimsFarZ,
	FullResolution,
	PriorityPlane,
	SphereEnvMap,
	emissiveGain,       -- Reforged
	fresnelColor,       -- Reforged
	fresnelOpacity,     -- Reforged
	fresnelTeamColor,   -- Reforged
	normalTexId,        -- Reforged PBR: Slot 1
	ormTexId,           -- Reforged PBR: Slot 2
	emissiveTexId,      -- Reforged PBR: Slot 3
	teamColorTexId,     -- Reforged PBR: Slot 4
	envMapTexId,        -- Reforged PBR: Slot 5
	ex
)

struct Wc3Material (
    maxObj,
    layers,
    id,
    tvertexId,
    ex,
    isConstantColor = false,
    isSortPrimsNearZ = false,
    isSortPrimsFarZ = false,
    isFullResolution = false,
    priorityPlane = 0
)

struct Wc3TvertexAnim (
	transformation,
	id,
	ex
)

struct Wc3Geoset (
	mesh = undefined,
	vertices = #(),
	normals = #(),
	tvertices = #(),
	matrixindices = #(),
	faces = #(),
	matrices = #(),
	material = undefined,
	id = 0,
	geosetAnimId = undefined,
	mainExtents,
	extents = #(),
	numUVWmaps = 1,
	unselectable = false,
	visObj,
	tangents,   -- Reforged
	skinData,   -- Reforged
	lod,        -- Reforged
	lodName,    -- Reforged
	ex
)

struct MaxGeoset (
	mesh = undefined,					
	vertices = #(),					
	normals = #(),
	tvertices = #(),
	matrixindices = #(),
	faces = #(),
	tvfaces = #(),
	matrices = #(),
	material = undefined,
	id = 0,
	geosetAnimId = undefined,
	numUVWmaps = 1,
	lod,
	lodName,
	skinData,
	ex
)

struct Wc3GeosetAnim (
	dropShadow,
	usesColor,
	alpha,
	color,
	geoset,
	id,
	ex
)

struct MDLObject (
	maxObj = undefined,
	name,
	objId,
	pivot,
	parent,
	billboarded = false,
	billboardedLockX = false,
	billboardedLockY = false,
	billboardedLockZ = false,
	cameraAnchored = false,
	transformations,
	dontInheritTranslation = false,
	dontInheritRotation = false,
	dontInheritScale = false,
	sub = undefined,
	deleted = undefined
)

struct Wc3Bone (
	mdlObj,
	geosets = #(),
	geosetAnimId,
	type = #Wc3Bone,
	ex
)

struct Wc3Helper (
	mdlObj,
	type = #Wc3Helper,
	ex
)

struct Wc3Light (
	mdlObj,
	lightType,
	attenuationStart,
	attenuationEnd,
	intensity,
	color,
	ambIntensity,
	ambColor,
	visibility,
	type = #Wc3Light,
	ex
)

struct Wc3Attachment (
	mdlObj,
	name,
	attachmentId,
	outPath,
	visibility,
	type = #Wc3Attachment,
	ex
)

struct Wc3Camera (
	name,
	pivot,
	transformations,
	fieldOfView,
	farClip,
	nearClip,
	targetPos,
	targetTransformations,
	rotation,
	ex
)

struct Wc3ParticleEmitter (
	mdlObj,
	emissionType = #Mdl,
	emissionRate,
	gravity,
	longitude,
	latitude,
	visibility,
	lifespan,
	initVelocity,
	path,
	type = #Wc3ParticleEmitter,
	ex
)

struct Wc3ParticleEmitter2 (
	mdlObj,
	sortPrimsFarZ,
	unshaded,
	lineEmitter,
	unfogged,
	modelSpace,
	xyQuad,
	speed,
	variation,
	latitude,
	gravity,
	visibility,
	squirt,
	lifeSpan,
	emissionRate,
	width,
	length,
	filtermode,
	rows,
	columns,
	particleType,
	tailLength,
	time,
	startColor,
	midColor,
	endColor,
	alpha,
	scaling,
	lifeSpanUVAnim,
	decayUVAnim,
	TailUVAnim,
	TailDecayUVAnim,
	texture,
	replaceableId,
	priorityPlane,
	type = #Wc3ParticleEmitter2,
	ex
)

struct Wc3RibbonEmitter (
	mdlObj,
	heightAbove,
	heightBelow,
	alpha,
	color,
	textureSlot,
	visibility,
	emissionRate,
	lifeSpan,
	gravity,
	rows,
	columns,
	material,
	type = #Wc3RibbonEmitter,
	ex
)

struct Wc3Event (
	mdlObj,
	eventTrack,
	type = #Wc3Event,
	ex
)

struct Wc3CollisionShape (
	mdlObj,
	shape = undefined,
	vertices,
	boundsRadius,
	type = #Wc3CollisionShape,
	ex
)

-- Reforged: Popcorn FX Emitter (CORN chunk)
struct Wc3CornEmitterExport (
    mdlObj,
    lifeSpan = 0.0,
    emissionRate = 0.0,
    speed = 0.0,
    color = [255,255,255],
    alpha = 1.0,
    replaceableId = 0,
    path = "",
    flags = "",
    visibility,
    emissionRateAnim,
    alphaAnim,
    type = #Wc3CornEmitter,
    ex
)

-- Reforged: FaceFX Target (FAFX chunk)
struct Wc3FaceFXExport (
    name = "",
    path = "",
    ex
)

struct Wc3Model (
	version,
	name = undefined,
	extents,
	globalSequences,
	sequences,
	textures,
	materials,
	tvertexAnims,
	geosets,
	geosetAnims,
	bones,
	lights,
	helpers,
	attachments,
	particleEmitters1,
	particleEmitters2,
	ribbonEmitters,
	events,
	cameras,
	CollisionShapes,
	cornEmitters,    -- Reforged
	faceFxTargets,   -- Reforged
	bindPoses,       -- Reforged
	ex
)
