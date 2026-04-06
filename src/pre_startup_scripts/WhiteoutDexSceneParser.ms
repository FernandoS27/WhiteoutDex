/*
    WhiteoutDexSceneParser.ms
    
    Refactored as Struct for better encapsulation and maintainability.
    All global variables are now instance properties.
    
    Usage:
        global SceneParser = WhiteoutDexSceneParserStruct()
        local model = SceneParser.ParseScene()
*/

-- ============================================================================
-- HELPER STRUCTS (unchanged)
-- ============================================================================

struct FakeObject (
    name,
    pos,
    parent,
    ref
)

struct FakeMesh (
    name,
    material,
    modifiers
)

struct FakeMaterial (
    texture,
    alpha = 100.0,
    filtermode = 1,
    unshaded = false,
    twosides = false,
    unfogged = false,
    unselectable = false,
    nodephtest = false,
    nodephset = false,
    retexture = 1,
    coordId = 0,
    path = "",
    UseCColor = false,
    MatColor = black,
    Wrap_Height = false,
    Wrap_Width = false,
    ConstantColor = false,
    SortPrimsNearZ = false,
    SortPrimsFarZ = false,
    FullResolution = false, 
    PriorityPlane = false,
    SphereEnvMap = false,
    ex = false
)

-- ============================================================================
-- MAIN PARSER STRUCT
-- ============================================================================

struct WhiteoutDexSceneParserStruct
(
    -- ============================================================================
    -- INSTANCE PROPERTIES (formerly global variables)
    -- ============================================================================
    
    public sceneObjects = #(),
    public Wc3GlobalSequences = #(),
    public Wc3Textures = #(),
    public Wc3Materials = #(),
    public Wc3TvertexAnims = #(),
    public Wc3Sequences = #(),
    public Meshes = #(),
    public BonesMeshRef = #(),
    public GeosetAnims = #(),
    public Wc3Objects = #(),
    public Wc3Bones = #(),
    public Wc3Helpers = #(),
    public Wc3Attachments = #(),
    public Wc3Lights = #(),
    public Wc3PartEmitters1 = #(),
    public Wc3PartEmitters2 = #(),
    public Wc3RibbonEmitters = #(),
    public Wc3Events = #(),
    public Wc3Cameras = #(),
    public Wc3Collisions = #(),
    public Wc3CornEmitters = #(),
    public Wc3FaceFXTargets = #(),
    public Wc3Extents = undefined,
    
    public GeosetCounts = 0,
    public BoneCounts = 0,
    public ObjectIdCount = 0,
    public MaterialCount = 0,
    public TextureCount = 0,
    public TvertexAnimCount = 0,
    public GeosetAnimCount = 0,
    
    public exportSmoothgroups = false,
    public keepUnusedBonesHelpers = false,
    public exportVersion = 800,
    
    -- Default objects
    public DefaultBone = undefined,
    public DefaultAnimation = undefined,
    public DefaultMaterial = undefined,
    
    -- ============================================================================
    -- INITIALIZATION
    -- ============================================================================
    
    fn initDefaults =
    (
        this.DefaultBone = FakeObject "Default Bone" [0,0,0] undefined undefined
        this.DefaultAnimation = Wc3Sequence "Stand" (10 as time) (40 as time) false undefined undefined undefined
        this.DefaultMaterial = FakeMaterial ()
    ),
    
    -- ============================================================================
    -- SEQUENCE FUNCTIONS
    -- ============================================================================
    
    fn GetSequenceData =
    (
        -- Auto-migrate from legacy Note Tracks if needed
        ::SequenceStorage.autoMigrate()
        rootNode.seqNames.count > 0
    ),
    
    fn readSequenceDataFromCA idx =
    (
        local ext = undefined
        try
        (
            local extStr = rootNode.seqExtents[idx]
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
        Wc3Sequence \
            rootNode.seqNames[idx] \
            (rootNode.startFrames[idx] as time) \
            (rootNode.endFrames[idx] as time) \
            rootNode.nonLooping[idx] \
            (if rootNode.rarity[idx] != 0.0 then rootNode.rarity[idx] as integer else undefined) \
            (if rootNode.moveSpeed[idx] != 0.0 then rootNode.moveSpeed[idx] as integer else undefined) \
            ext
    ),
    
   fn getFilteredSequencesForExport =
    (
        local exportSequences = #()
        try
        (
            ::SequenceManager.loadIgnoredSequences()
            local allSequences = ::SequenceManager.LoadSequences()
            for seq in allSequences do
            (
                if not ::SequenceManager.isSequenceIgnored seq.name then
                    append exportSequences seq
            )
        )
        catch
        (
            print "Warning: Could not access SequenceManager, using fallback method"
        )
        return exportSequences
    ),
    
    fn LoadSequences =
    (
        ::Progress.downLevel 10 1
        ::Progress.showItem "Loading Animations"
        
        local filteredSequences = this.getFilteredSequencesForExport()
        
        if filteredSequences.count > 0 then
        (
            this.Wc3Sequences = filteredSequences
            
            if SequenceManager != undefined then
            (
                local totalSequences = SequenceManager.sequences.count
                local ignoredCount = totalSequences - filteredSequences.count
                if ignoredCount > 0 then
                (
                    print ("Export Info: Using " + filteredSequences.count as string + " sequences")
                    print ("Export Info: Ignoring " + ignoredCount as string + " sequences")
                    for seqName in SequenceManager.ignoredSequences do
                        print ("  - Ignored: " + seqName)
                )
            )
        )
        else
        (
            print "Export Info: Using fallback sequence detection (Custom Attributes)"
            local hasData = this.GetSequenceData()
            if hasData then
            (
                local count = rootNode.seqNames.count
                this.Wc3Sequences[count] = undefined
                for i = 1 to count do
                    this.Wc3Sequences[i] = this.readSequenceDataFromCA i
            )
            else 
            (
                this.Wc3Sequences[1] = this.DefaultAnimation
                ::ErrorReporter.reportError "No Sequences" "RootNode" undefined
            )
        )
        
        if this.Wc3Sequences.count == 0 then
        (
            print "Warning: No sequences available for export, using default animation"
            this.Wc3Sequences[1] = this.DefaultAnimation
            ::ErrorReporter.reportError "No Sequences" "RootNode" "All sequences were filtered out"
        )
        
        ::Progress.upLevel ()
    ),
    
    -- ============================================================================
    -- UTILITY FUNCTIONS
    -- ============================================================================
    
    fn IsGeosetAnimated geo = 
        geo.visObj != undefined or (geo.mesh.modifiers[#Wc3_Vertex_Color] != undefined),
    
    fn IsIgnorable obj = 
        obj.isHidden,
    
    fn IsBipedNub obj =
    (
        if classOf obj == Dummy then
        (
            if obj.parent != undefined and classOf obj.parent == Biped_Object then return true
            try ( obj.pos; false ) catch ( true )
        )
        else false
    ),

    fn ObjectChecker a b = 
        a.maxObj == b,
    
    fn ObjectChecker2 a b = 
        a.mdlObj.maxObj == b,
    
    -- ============================================================================
    -- OBJECT LOADING
    -- ============================================================================
    
    fn LoadMDLObject obj =
    (
        local index = ::Utils.ArrayFindItemEx this.Wc3Objects obj this.ObjectChecker
        if index != 0 then
        (
            this.Wc3Objects[index]
        )
        else
        (
            local mdl = MDLObject ()
            mdl.maxObj = obj
            if classOf obj == Biped_Object then
                mdl.pivot = at time 0f obj.transform.pos
            else if (try(classOf obj.transform.controller == Link_Constraint) catch(false)) then
                mdl.pivot = at time 0f obj.transform.pos
            else
                mdl.pivot = try(obj.pos) catch(obj.transform.pos)
            mdl.name = obj.name
            
            if classOf obj == FakeObject then obj = obj.ref
            if obj != undefined then
            (
                mdl.transformations = ::AnimationManager.GetTransformations obj
                
                if not (::AnimationManager.isIKAffected obj) then
                (
                    local n = getInheritanceFlags obj
                    if n[1] == false and n[2] == false and n[3] == false then mdl.dontInheritTranslation = true
                    if n[4] == false and n[5] == false and n[6] == false then mdl.dontInheritRotation = true
                    if n[7] == false and n[8] == false and n[9] == false then mdl.dontInheritScale = true
                )
                local bbFlags = ::Utils.getBillboardFlags obj
                mdl.billboarded = bbFlags[1]
                mdl.billboardedLockX = bbFlags[2]
                mdl.billboardedLockY = bbFlags[3]
                mdl.billboardedLockZ = bbFlags[4]
                mdl.cameraAnchored = ::Utils.getCameraAnchored obj
            )
            else mdl.transformations = TransformationData ()
            append this.Wc3Objects mdl
            mdl
        )
    ),
    
    fn LoadBone obj geoset =
    (
        local index = ::Utils.ArrayFindItemEx this.BonesMeshRef obj this.ObjectChecker2
        if index != 0 then
        (
            local index2 = findItem this.BonesMeshRef[index].geosets geoset
            if index2 == 0 then
                append this.BonesMeshRef[index].geosets geoset
        )
        else
        (
            local mdl = this.LoadMDLObject obj
            local mdlBone = Wc3Bone ()
            mdlBone.mdlObj = mdl
            mdl.sub = mdlBone
            append mdlBone.geosets geoset
            append this.BonesMeshRef mdlBone
            this.BoneCounts = this.BoneCounts + 1
            index = this.BoneCounts
        )
        index
    ),
    
    -- ============================================================================
    -- MESH PROCESSING
    -- ============================================================================
    
    fn cleanupMeshData obj =
    (
        try
        (
            local numMaps = meshop.getNumMaps obj
            if numMaps > 2 then
                meshop.setNumMaps obj 2 keep:true
            
            if (meshop.getMapSupport obj 1) do
            (
                local tVertsToWeld = for v = 1 to (meshop.getNumMapVerts obj 1) collect v
                if tVertsToWeld.count > 0 do
                    meshop.weldMapVertsByThreshold obj 1 tVertsToWeld 0.001
            )
            
            update obj
        )
        catch (err)
        (
        )
    ),
    
    fn checkWeights weights =
    (
        local minWeight = weights[1] - 0.02
        local maxWeight = weights[1] + 0.02
        local result = false
        local balance = 0.0
        for w in weights do
        (
            result = result or (minWeight > w or w > maxWeight)
            balance += w
        )
        (result or (balance < 0.98 or balance > 1.02))
    ),
    
    fn generateFakeBone geo =
    (
        fn IsAnyAnimated o = (try(o.pos.isAnimated) catch(false)) or (try(o.rotation.IsAnimated) catch(false)) or (try(o.scale.isanimated) catch(false))
        local m = geo.mesh
        -- v1200: Keine Fake-Bones erzeugen, SKIN-Daten haben die Bone-Zuordnung
        if this.exportVersion == 1200 then
        (
            -- Nutze den Parent oder den ersten echten Bone in der Szene (NICHT DefaultBone)
            if m.parent != undefined then m.parent
            else
            (
                -- Finde irgendeinen echten Bone aus der Szene
                local fallback = undefined
                for obj in this.sceneObjects where (classOf obj == BoneGeometry or classOf obj == Dummy) and not obj.isHidden do
                (
                    fallback = obj
                    exit
                )
                if fallback != undefined then fallback
                else m  -- Mesh selbst als letzter Fallback
            )
        )
        else
        (
            if ((IsAnyAnimated m) or (this.IsGeosetAnimated geo)) then
            (
                local tmp = FakeObject ("Mesh " + m.name) (try(m.pos) catch(m.transform.pos)) m.parent m
                tmp
            )
            else if m.parent != undefined then m.parent
            else this.DefaultBone
        )
    ),
    
    fn getVisObject m =
    (
        local tmp = #(m)
        join tmp (::Utils.GetParents m)
        local i = 1
        local count = tmp.count
        local result
        while (i <= count and result == undefined) do
        (
            if tmp[i].visibility.IsAnimated then result = tmp[i]
            i += 1
        )
        result
    ),
    
    fn processSkinning obj replaceBone =
    (
        local s = undefined
        local tmpIndices
        local tmpBones = #()
        local matrices = #()
        local weightsUnbalanced = false
        local numBones
        
        if ::Utils.IsSkinned obj then
        (
            s = obj.skin
            
            local skinInstances = refs.dependentNodes s
            if skinInstances.count > 1 then
            (
                instancemgr.MakeModifiersUnique obj s #individual
                s = obj.skin
            )
            
            select obj
            modPanel.setCurrentObject s
        )
        
        local funcBoneId = skinOps.GetVertexWeightBoneID
        local funcVertexCount = skinOps.GetVertexWeightCount
        local funcGetWeight = skinOps.GetVertexWeight
        local validSkin = s != undefined and (skinOps.GetNumberVertices s) >= obj.numverts
        
        if validSkin then
        (
            numBones = skinops.GetNumberBones s
            tmpBones[numBones+1] = undefined
            
            for i = 1 to numBones do
            (
                tmpBones[i] = skinOps.GetBoneNode s i
            )
            
            numBones = numBones + 1
        ) 
        else numBones = 1
        tmpBones[numBones] = replaceBone
        
        -- v1200: Raw Skin-Daten pro Vertex erfassen (4 Bones + 4 Weights als Bytes)
        local tmpRawSkin = if this.exportVersion == 1200 then #() else undefined
        if tmpRawSkin != undefined then tmpRawSkin[obj.numverts] = undefined
        
        if validSkin then
        (
            tmpIndices = #()
            tmpIndices[obj.numverts] = undefined
            
            local matrixDict = Dictionary #string
            
            for v = 1 to obj.numverts do
            (
                local matrixindex = undefined
                local bonegroup = #()
                local count = funcVertexCount s v
                
                if count > 0 then
                (
                    local weights = #()
                    local count = (funcVertexCount s v)
                    bonegroup[count] = undefined
                    
                    for w = 1 to count do
                    (
                        bonegroup[w] = funcBoneId s v w
                        weights[w] = funcGetWeight s v w
                    )
                    
                    -- v1200: Per-Vertex Skin-Daten erfassen (vor sort!)
                    if tmpRawSkin != undefined then
                    (
                        local skinBones = #(0, 0, 0, 0)
                        local skinWeights = #(0, 0, 0, 0)
                        -- Top 4 Influences nach Weight sortiert
                        local usedIdx = #{}
                        local nInf = amin count 4
                        for i = 1 to nInf do
                        (
                            local bestIdx = 0
                            local bestW = -1.0
                            for w = 1 to count do
                            (
                                if not usedIdx[w] and weights[w] > bestW then
                                    ( bestW = weights[w]; bestIdx = w )
                            )
                            if bestIdx > 0 then
                            (
                                usedIdx[bestIdx] = true
                                skinBones[i] = bonegroup[bestIdx]
                                skinWeights[i] = (bestW * 255.0 + 0.5) as integer
                            )
                        )
                        -- Auf 255 normalisieren
                        local wSum = 0
                        for i = 1 to 4 do wSum += skinWeights[i]
                        if wSum > 0 and wSum != 255 then skinWeights[1] += (255 - wSum)
                        tmpRawSkin[v] = #(skinBones[1], skinBones[2], skinBones[3], skinBones[4], skinWeights[1], skinWeights[2], skinWeights[3], skinWeights[4])
                    )
                    
                    sort bonegroup
                    weightsUnbalanced = weightsUnbalanced or (this.checkWeights weights)
                )
                else
                (
                    bonegroup = #(numBones)
                    if tmpRawSkin != undefined then
                        tmpRawSkin[v] = #(numBones, 0, 0, 0, 255, 0, 0, 0)
                )
                
                local bgKey = bonegroup as string
                if hasDictValue matrixDict bgKey then
                (
                    matrixindex = matrixDict[bgKey] - 1
                )
                else
                (
                    append matrices bonegroup
                    matrixDict[bgKey] = matrices.count
                    matrixindex = matrices.count - 1
                )
                tmpIndices[v] = matrixindex
            )
            
            if weightsUnbalanced then
                ::ErrorReporter.reportError "Weights" obj.name undefined
        )
        else 
        (
            if (s != undefined) then
                ::ErrorReporter.reportError "Bad Skinning" obj.name undefined
            tmpBones[1] = replaceBone
            matrices = #(#(1))
            tmpIndices = ::Utils.ArrayRepeat obj.numverts 0
            if tmpRawSkin != undefined then
                for v = 1 to obj.numverts do
                    tmpRawSkin[v] = #(1, 0, 0, 0, 255, 0, 0, 0)
        )
        
        #(matrices, tmpIndices, weightsUnbalanced, tmpBones, tmpRawSkin)
    ),
    
    fn processSmoothgroups obj =
    (
        local faces
        local preCalculatedNormals
        local facegroupMapper
        
        -- Check for Edit_Normals modifier on the stack
        local enMod = undefined
        for m in obj.modifiers where classof m == Edit_Normals do
        (
            enMod = m
            exit
        )
        
        if enMod != undefined then
        (
            -- Edit_Normals present: split by normal VALUE (not ID)
            -- After Break(), each face-corner has a unique normal ID even if values match
            try
            (
                select obj
                max modify mode
                modPanel.setCurrentObject enMod
                
                local numVerts = getNumVerts obj
                local baseFaces = getFaces obj
                
                local vertexMapper = #()
                vertexMapper[numVerts] = undefined
                preCalculatedNormals = #()
                preCalculatedNormals[numVerts] = undefined
                faces = getFaces obj
                local currentVert = 0
                
                local func2 = meshop.getFacesUsingVert
                local eps = 0.001
                
                for v = 1 to numVerts do
                (
                    local usedFaces = (func2 obj #{v}) as array
                    
                    local groups = #()
                    local groupNormals = #()
                    
                    for f in usedFaces do
                    (
                        local corner = 0
                        local bf = baseFaces[f]
                        for j = 1 to 3 where (bf[j] as integer) == v do corner = j
                        
                        if corner > 0 then
                        (
                            local nID = enMod.GetNormalID f corner
                            local nVal = if (nID != undefined and nID > 0) then enMod.GetNormal nID else undefined
                            if nVal == undefined or (classof nVal) != Point3 then
                                nVal = getNormal obj v
                            if nVal == undefined or (classof nVal) != Point3 then
                                nVal = [0,0,1]
                            
                            local foundGroup = 0
                            for g = 1 to groupNormals.count where foundGroup == 0 do
                            (
                                local diff = nVal - groupNormals[g]
                                if (abs diff.x) < eps and (abs diff.y) < eps and (abs diff.z) < eps then
                                    foundGroup = g
                            )
                            
                            if foundGroup > 0 then
                                append groups[foundGroup] #(f, corner)
                            else
                            (
                                append groups #(#(f, corner))
                                append groupNormals nVal
                            )
                        )
                    )
                    
                    for g = 1 to groups.count do
                    (
                        currentVert += 1
                        vertexMapper[currentVert] = v
                        preCalculatedNormals[currentVert] = groupNormals[g]
                        
                        for fc in groups[g] do
                            faces[fc[1]][fc[2]] = currentVert
                    )
                )
                
                vertexMapper.count = currentVert
                preCalculatedNormals.count = currentVert
                
                facegroupMapper = vertexMapper
                max create mode
            )
            catch
            (
                -- Fallback: standard path
                max create mode
                if this.exportSmoothgroups then
                (
                    local tmp = gatherSmoothGroupData obj
                    faces = tmp[1]
                    preCalculatedNormals = tmp[2]
                    facegroupMapper = tmp[3]
                    for i = 1 to facegroupMapper.count where preCalculatedNormals[i] == undefined do
                        preCalculatedNormals[i] = getNormal obj facegroupMapper[i]
                )
                else
                (
                    local numVerts = getNumVerts obj
                    preCalculatedNormals = #()
                    preCalculatedNormals[numVerts] = undefined
                    for i = 1 to numVerts do
                        preCalculatedNormals[i] = getNormal obj i
                )
            )
        )
        else if this.exportSmoothgroups then
        (
            local tmp = gatherSmoothGroupData obj
            faces = tmp[1]
            preCalculatedNormals = tmp[2]
            facegroupMapper = tmp[3]
            for i = 1 to facegroupMapper.count where preCalculatedNormals[i] == undefined do
                preCalculatedNormals[i] = getNormal obj facegroupMapper[i]
        )
        else
        (
            local numVerts = getNumVerts obj
            preCalculatedNormals = #()
            preCalculatedNormals[numVerts] = undefined
            for i = 1 to numVerts do
                preCalculatedNormals[i] = getNormal obj i
        )
        
        -- Safety: ensure no undefined normals reach the writer
        for i = 1 to preCalculatedNormals.count where preCalculatedNormals[i] == undefined or (classof preCalculatedNormals[i]) != Point3 do
            preCalculatedNormals[i] = [0,0,1]
        
        #(faces, preCalculatedNormals, facegroupMapper)
    ),
    
    fn canSimplifyUnwrapCalculation o =
    (
        local numberOfMaps = 0
        for i = 1 to (meshop.getNumMaps o) do
            if (meshop.getMapSupport o i) then
                numberOfMaps += 1
        if numberOfMaps != 1 then return false
        
        -- Prüfe ob TV→Geo Mapping eindeutig ist (keine gespiegelten UVs)
        -- breakUnwrapFast kann nur 1:1 Mappings korrekt verarbeiten.
        -- Bei Konflikten (ein TVert zeigt auf verschiedene Geo-Verts)
        -- muss der Standard-Pfad (breakUnwrapMap) verwendet werden,
        -- der Extra-Vertices fuer Konflikte anlegt.
        local numTV = getNumTVerts o
        local tvToGeo = #()
        tvToGeo[numTV] = undefined
        for f = 1 to (getNumFaces o) do
        (
            local face = getFace o f
            local tvf = getTVFace o f
            for j = 1 to 3 do
            (
                local tvIdx = tvf[j] as integer
                local geoIdx = face[j] as integer
                if tvToGeo[tvIdx] == undefined then
                    tvToGeo[tvIdx] = geoIdx
                else if tvToGeo[tvIdx] != geoIdx then
                    return false
            )
        )
        true
    ),
    
    fn processUnwrapSimplified obj tmpIndices preCalculatedNormals =
    (
        local verts = #()
        local normals = #()
        local tverts = #()
        local matrixindices = #()
        local origVertMap = #()
        
        local tmp = breakUnwrapFast obj
        faces = tmp[2]
        local map = tmp[1]
        local vertexIndices = tmp[3]
        local count = vertexIndices.count
        
        verts[count] = undefined
        tverts[count] = undefined
        normals[count] = undefined
        matrixindices[count] = undefined
        origVertMap[count] = 0
        
        for i = 1 to count do
        (
            local vidx = vertexIndices[i]
            if vidx != undefined and vidx >= 1 and vidx <= map.count then
            (
                local v = map[vidx]
                if v != undefined and v >= 1 and v <= obj.numverts then
                (
                    verts[i] = getVert obj v
                    if v <= preCalculatedNormals.count then
                        normals[i] = preCalculatedNormals[v]
                    else
                        normals[i] = [0,0,1]
                    
                    tverts[i] = getTVert obj vidx
                    
                    if v <= tmpIndices.count then
                        matrixindices[i] = tmpIndices[v]
                    else
                        matrixindices[i] = 0
                    origVertMap[i] = v
                )
                else
                (
                    verts[i] = [0,0,0]
                    normals[i] = [0,0,1]
                    tverts[i] = [0,0]
                    matrixindices[i] = 0
                    origVertMap[i] = 0
                )
            )
            else
            (
                verts[i] = [0,0,0]
                normals[i] = [0,0,1]
                tverts[i] = [0,0]
                matrixindices[i] = 0
                origVertMap[i] = 0
            )
        )
        
        #(verts, normals, #(tverts), matrixindices, faces, origVertMap)
    ),
    
    fn processUnwrapStandard obj tmpIndices preCalculatedNormals faces:#() facegroupMapper:undefined =
    (
        local verts = #()
        local normals = #()
        local tverts = #()
        local matrixindices = #()
        local origVertMap = #()
        
        local tmp = breakUnwrap obj faces:faces numVerts:(if facegroupMapper != undefined then facegroupMapper.count else undefined)
        faces = tmp[1]
        tverts = tmp[2]
        local mapper = tmp[3]
        local count = mapper.count
        
        verts[count] = undefined
        normals[count] = undefined
        matrixindices[count] = undefined
        origVertMap[count] = 0
        
        if facegroupMapper != undefined then
        (
            for i = 1 to count do
            (
                local v = facegroupMapper[mapper[i]]
                verts[i] = getVert obj v
                normals[i] = preCalculatedNormals[mapper[i]]
                matrixindices[i] = tmpIndices[v]
                origVertMap[i] = v
            )
        )
        else
        (
            for i = 1 to count do
            (
                local v = mapper[i]
                if v >= 1 and v <= obj.numverts then
                (
                    verts[i] = getVert obj v
                    normals[i] = preCalculatedNormals[v]
                    matrixindices[i] = tmpIndices[v]
                    origVertMap[i] = v
                )
                else
                (
                    verts[i] = [0,0,0]
                    normals[i] = [0,0,1]
                    matrixindices[i] = 0
                    origVertMap[i] = 0
                )
            )
        )
        
        #(verts, normals, tverts, matrixindices, faces, origVertMap)
    ),
    
    fn processNoUnwrap obj tmpIndices preCalculatedNormals facegroupMapper:undefined faces:undefined =
    (
        local verts = #()
        local normals = #()
        local tverts = #()
        local matrixindices = #()
        local origVertMap = #()
        
        if facegroupMapper != undefined then
        (
            local count = facegroupMapper.count
            verts[count] = undefined
            tverts[count] = undefined
            normals[count] = undefined
            matrixindices[count] = undefined
            origVertMap[count] = 0
            
            for i = 1 to count do
            (
                local v = facegroupMapper[i]
                verts[i] = getVert obj v
                normals[i] = preCalculatedNormals[i]
                tverts[i] = [0,0]
                matrixindices[i] = tmpIndices[v]
                origVertMap[i] = v
            )
            if faces == undefined then faces = getFaces obj
        )
        else
        (
            faces = getFaces obj
            local count = obj.vertices.count
            verts[count] = undefined
            tverts[count] = undefined
            normals[count] = undefined
            matrixindices[count] = undefined
            origVertMap[count] = 0
            
            for i = 1 to count do
            (
                verts[i] = getVert obj i
                normals[i] = preCalculatedNormals[i]
                tverts[i] = [0,0]
                matrixindices[i] = tmpIndices[i]
                origVertMap[i] = i
            )
        )
        
        #(verts, normals, #(tverts), matrixindices, faces, origVertMap)
    ),

    -- Reforged: Tangenten aus Vertices, Normals, UVs und Faces berechnen

    fn computeTangents verts normals tverts faces =
    (
        local count = verts.count
        local tan1 = #()   -- Tangent Akkumulator
        local tan2 = #()   -- Bitangent Akkumulator
        tan1[count] = undefined
        tan2[count] = undefined
        for i = 1 to count do ( tan1[i] = [0,0,0]; tan2[i] = [0,0,0] )
        
        for f in faces do
        (
            local i1 = f[1] as integer
            local i2 = f[2] as integer
            local i3 = f[3] as integer
            
            if i1 >= 1 and i2 >= 1 and i3 >= 1 and i1 <= count and i2 <= count and i3 <= count then
            (
                local v1 = verts[i1]
                local v2 = verts[i2]
                local v3 = verts[i3]
                
                local w1 = tverts[i1]
                local w2 = tverts[i2]
                local w3 = tverts[i3]
                
                local x1 = v2.x - v1.x
                local x2 = v3.x - v1.x
                local y1 = v2.y - v1.y
                local y2 = v3.y - v1.y
                local z1 = v2.z - v1.z
                local z2 = v3.z - v1.z
                
                local s1 = w2.x - w1.x
                local s2 = w3.x - w1.x
                local t1 = w2.y - w1.y
                local t2 = w3.y - w1.y
                
                local denom = (s1 * t2 - s2 * t1)
                if (abs denom) >= 0.000001 then
                (
                    local r = 1.0 / denom
                    
                    local sdir = [(t2*x1 - t1*x2)*r, (t2*y1 - t1*y2)*r, (t2*z1 - t1*z2)*r]
                    local tdir = [(s1*x2 - s2*x1)*r, (s1*y2 - s2*y1)*r, (s1*z2 - s2*z1)*r]
                    
                    tan1[i1] = tan1[i1] + sdir
                    tan1[i2] = tan1[i2] + sdir
                    tan1[i3] = tan1[i3] + sdir
                    
                    tan2[i1] = tan2[i1] + tdir
                    tan2[i2] = tan2[i2] + tdir
                    tan2[i3] = tan2[i3] + tdir
                )
            )
        )
        
        -- Gram-Schmidt Orthonormalisierung + Handedness
        local result = #()
        result[count] = undefined
        for i = 1 to count do
        (
            local n = if normals[i] != undefined then normals[i] else [0,0,1]
            local t = if tan1[i] != undefined then tan1[i] else [1,0,0]
            
            -- Gram-Schmidt: t' = normalize(t - n * dot(n, t))
            local tangent = t - n * (dot n t)
            local len = length tangent
            if len > 0.000001 then
                tangent = tangent / len
            else
                tangent = [1,0,0]
            
            -- Handedness: W = sign(dot(cross(n, t), tan2))
            local w = if (dot (cross n t) tan2[i]) < 0.0 then -1.0 else 1.0
            
            result[i] = #(tangent.x, tangent.y, tangent.z, w)
        )
        result
    ),
    
    fn LoadGeoset obj =
    (
        this.cleanupMeshData obj
        
        if obj.numfaces == 0 or (getNumVerts obj) == 0 then
        (
            ::ErrorReporter.logWarning "Export" obj.name "Empty mesh skipped (0 faces/vertices)"
            return OK
        )
        
        local geoset = Wc3Geoset ()
        geoset.mesh = obj
        geoset.visObj = this.getVisObject obj
        geoset.id = this.GeosetCounts
        this.GeosetCounts = this.GeosetCounts + 1
        append this.Meshes geoset
        
        local loadName = "Loading " + obj.name + ": "
        
        ::Progress.updateItem (loadName + "Skinning")
        max modify mode
        
        local replaceBone = this.generateFakeBone geoset
        local skinResult = this.processSkinning obj replaceBone
        local matrices = skinResult[1]
        local tmpIndices = skinResult[2]
        local weightsUnbalanced = skinResult[3]
        local tmpBones = skinResult[4]
        local tmpRawSkin = if skinResult.count >= 5 then skinResult[5] else undefined
        
        max create mode
        if suspendEditing != undefined then suspendEditing ()
        
        ::Progress.updateItem (loadName + "Related Bones")
        for m in matrices do
            for i = 1 to m.count do m[i] = this.LoadBone tmpBones[m[i]] geoset
        
        -- v1200: Bone-Mapping fuer SKIN-Daten aufbauen (tmpBones Index -> BonesMeshRef Index)
        local boneRefMap = undefined
        if this.exportVersion == 1200 and tmpRawSkin != undefined then
        (
            boneRefMap = #()
            boneRefMap[tmpBones.count] = 0
            for b = 1 to tmpBones.count do
                boneRefMap[b] = this.LoadBone tmpBones[b] geoset
        )
        
        ::Progress.updateItem (loadName + "Smoothgroups")
        local smoothData = this.processSmoothgroups obj
        local faces = smoothData[1]
        local preCalculatedNormals = smoothData[2]
        local facegroupMapper = smoothData[3]
        
        ::Progress.updateItem (loadName + "Unwrap")
        local hasUnwrap = (getNumTVerts obj) > 0
        local verts, normals, tvertices, matrixindices
        local origVertMap = undefined
        
        if hasUnwrap then
        (
            if ((getNumTVerts obj) < obj.vertices.count) then
                ::ErrorReporter.reportError "Bad Unwrap" obj.name undefined
            
            if (this.canSimplifyUnwrapCalculation obj) and (not this.exportSmoothgroups) and (facegroupMapper == undefined) and ((getNumTVerts obj) >= obj.vertices.count) then
            (
                local result = this.processUnwrapSimplified obj tmpIndices preCalculatedNormals
                verts = result[1]
                normals = result[2]
                tvertices = result[3]
                matrixindices = result[4]
                faces = result[5]
                origVertMap = if result.count >= 6 then result[6] else undefined
            )
            else
            (
                local result = this.processUnwrapStandard obj tmpIndices preCalculatedNormals faces:faces facegroupMapper:facegroupMapper
                verts = result[1]
                normals = result[2]
                tvertices = result[3]
                matrixindices = result[4]
                faces = result[5]
                origVertMap = if result.count >= 6 then result[6] else undefined
            )
        )
        else
        (
            local result = this.processNoUnwrap obj tmpIndices preCalculatedNormals facegroupMapper:facegroupMapper faces:faces
            verts = result[1]
            normals = result[2]
            tvertices = result[3]
            matrixindices = result[4]
            faces = result[5]
            origVertMap = if result.count >= 6 then result[6] else undefined
            ::ErrorReporter.reportError "No Unwrap" obj.name undefined
        )
        
        geoset.vertices = verts
        geoset.matrixindices = matrixindices
        geoset.faces = faces
        geoset.normals = normals
        geoset.matrices = matrices
        geoset.tvertices = tvertices
        geoset.unselectable = ::Utils.getUnselectable obj
        
        -- v1200: SKIN-Daten aus Raw-Daten + Vertex-Map generieren
        if this.exportVersion == 1200 and tmpRawSkin != undefined and origVertMap != undefined and boneRefMap != undefined then
        (
            local skinExport = #()
            skinExport[matrixindices.count] = undefined
            for i = 1 to matrixindices.count do
            (
                local v = origVertMap[i]
                if v != undefined and v > 0 and v <= tmpRawSkin.count and tmpRawSkin[v] != undefined then
                (
                    local sv = deepCopy tmpRawSkin[v]
                    -- tmpBones-Indices zu BonesMeshRef-Indices konvertieren
                    for b = 1 to 4 do
                    (
                        if sv[b] > 0 and sv[b] <= boneRefMap.count then
                            sv[b] = boneRefMap[sv[b]]
                        else
                            sv[b] = 0
                    )
                    skinExport[i] = sv
                )
                else
                    skinExport[i] = #(0, 0, 0, 0, 255, 0, 0, 0)
            )
            geoset.skinData = skinExport
        )
        
       -- v1200: LOD-Daten setzen (Default: LOD 0)
        if this.exportVersion == 1200 then
        (
            geoset.lod = 0
            local storedName = getUserProp obj "Wc3LodName"
            geoset.lodName = if storedName != undefined then storedName else ""
            
            -- Tangenten berechnen (fuer PBR Normal Mapping)
            if tvertices.count > 0 and tvertices[1].count > 0 then
                geoset.tangents = this.computeTangents verts normals tvertices[1] faces
        )
        
        if resumeEditing != undefined then resumeEditing ()
        OK
    ),
    
    -- ============================================================================
    -- WC3 OBJECT LOADERS
    -- ============================================================================
    
    fn LoadLight obj =
    (
        local tmp = Wc3Light ()
        
        if classOf obj == Wc3_Light then
        (
            tmp.lightType = case obj.LightType of
            (
                1: #Omnidirectional
                2: #Directional
                3: #Ambient
                default: #Omnidirectional
            )
            
            tmp.attenuationStart = ::AnimationManager.FloatParameterProcess obj.DecayStart.controller obj.DecayStart
            tmp.attenuationEnd = ::AnimationManager.FloatParameterProcess obj.DecayEnd.controller obj.DecayEnd
            tmp.intensity = ::AnimationManager.FloatParameterProcess obj.AmbValue.controller obj.AmbValue
            tmp.ambIntensity = ::AnimationManager.FloatParameterProcess obj.ShadowValue.controller obj.ShadowValue
            tmp.color = ::AnimationManager.ColorParameterProcess obj.AmbColor.controller obj.AmbColor
            tmp.ambcolor = ::AnimationManager.ColorParameterProcess obj.ShadowColor.controller obj.ShadowColor
            
            if obj.Visibility.isAnimated then 
                tmp.visibility = ::AnimationManager.VisibilityParameterProcess obj
        )
        tmp
    ),
    
    fn LoadHelper obj = 
    (
        local sub = Wc3Helper ()
        sub.MDLObj = this.LoadMDLObject obj
        sub.MDLObj.sub = sub
        sub
    ),
    
    fn LoadAttachment obj =
    (
        local tmp = Wc3Attachment ()
        if (obj.usesExternalModel) then
        (
            tmp.name = obj.name
            tmp.outPath = obj.externalModelPath
        )
        else (
            local s = attachmentList[obj.nameFirst]
            if obj.nameAdd1 != 1 then s = s + " " + attachmentAdd1List[obj.nameAdd1]
            if obj.nameAdd2 != 1 then s = s + " " + attachmentAdd2List[obj.nameAdd2]
            tmp.name = s + " Ref"
        )
        if (obj.usesAttachmentId) then
            tmp.attachmentId = obj.attachmentId
        if obj.visibility.isAnimated then tmp.visibility = ::AnimationManager.VisibilityParameterProcess obj
        tmp
    ),
    
    
    fn LoadParticleEmitter obj =
    (
        local tmp = Wc3ParticleEmitter ()
        
        tmp.emissionRate = ::AnimationManager.FloatParameterProcess obj.PartEmit.controller obj.PartEmit
        tmp.gravity = ::AnimationManager.FloatParameterProcess obj.Gravity.controller obj.Gravity
        tmp.longitude = ::AnimationManager.FloatParameterProcess obj.longitude.controller obj.longitude
        tmp.latitude = ::AnimationManager.FloatParameterProcess obj.latitude.controller obj.latitude
        tmp.lifespan = ::AnimationManager.FloatParameterProcess obj.Life.controller obj.Life
        tmp.initVelocity = ::AnimationManager.FloatParameterProcess obj.radius.controller obj.radius
        
        if obj.Visibility.isAnimated then 
            tmp.visibility = ::AnimationManager.VisibilityParameterProcess obj
        
        local combinedPath = ""
        
        if obj.path != undefined then
            combinedPath = obj.path
        else
            combinedPath = "war3mapImported\\"
        
        if obj.Part != undefined and obj.Part != "" then
            combinedPath = combinedPath + obj.Part
        
        if combinedPath.count > 4 then
        (
            if (toLower (substring combinedPath (combinedPath.count - 3) 4)) == ".mdl" then
                combinedPath = substring combinedPath 1 (combinedPath.count - 4)
        )
        
        if combinedPath == "" or combinedPath == "war3mapImported\\" then
            combinedPath = "war3mapImported\\dummy"
        
        tmp.path = combinedPath + ".MDL"
        
        tmp
    ),
    
    fn LoadParticleEmitter2 obj =
    (
        local tmp = Wc3ParticleEmitter2 ()
        
        tmp.sortPrimsFarZ = obj.SortZ
        tmp.unshaded = obj.Unshaded
        tmp.lineEmitter = obj.LineEmitter
        tmp.unfogged = obj.Unfogged
        tmp.modelSpace = obj.ParticleSpace
        tmp.xyQuad = obj.XYQuads
        tmp.speed = ::AnimationManager.FloatParameterProcess obj.speed.controller obj.speed
        tmp.variation = ::AnimationManager.FloatParameterProcess obj.variation.controller obj.variation
        tmp.latitude = ::AnimationManager.FloatParameterProcess obj.coneangle.controller obj.coneangle
        tmp.gravity = ::AnimationManager.FloatParameterProcess obj.gravity.controller obj.gravity
        
        if obj.visibility.isAnimated then 
            tmp.visibility = ::AnimationManager.VisibilityParameterProcess obj
        
        tmp.squirt = obj.Squirt
        tmp.lifeSpan = obj.Life
        tmp.emissionRate = ::AnimationManager.FloatParameterProcess obj.PartsEmit.controller obj.PartsEmit
        tmp.width = ::AnimationManager.FloatParameterProcess obj.width.controller obj.width
        tmp.length = ::AnimationManager.FloatParameterProcess obj.length.controller obj.length
        tmp.filtermode = obj.filtermode
        tmp.rows = obj.rows
        tmp.columns = obj.cols
        tmp.particleType = obj.particletype
        tmp.tailLength = obj.taillength
        tmp.time = obj.midtime
        
        tmp.startColor = obj.startcolor as point3
        tmp.midColor = obj.middlecolor as point3
        tmp.endColor = obj.endcolor as point3
        
        tmp.alpha = [obj.startalpha, obj.middlealpha, obj.endalpha]
        tmp.scaling = [obj.startscale, obj.middlescale, obj.endscale]
        tmp.lifeSpanUVAnim = [obj.startLifespanHead, obj.endLifespanHead, obj.repeatLifespanHead]
        tmp.decayUVAnim = [obj.startDecayHead, obj.endDecayHead, obj.repeatDecayHead]
        tmp.TailUVAnim = [obj.startLifespanTail, obj.endLifespanTail, obj.repeatLifespanTail]
        tmp.TailDecayUVAnim = [obj.startDecayTail, obj.endDecayTail, obj.repeatDecayTail]
        -- Textur aus Plugin lesen und registrieren
        -- FIX: GetImageFile benutzen um nur Dateinamen + .blp zu extrahieren,
        -- statt den vollen lokalen Pfad (C:\Users\...) mit dem Prefix zu kombinieren.
        -- Verhindert doppelte Textur-Registrierung (dieser Pfad vs. Material-Loop Pfad).
        local texFile = ""
        try
        (
            local prefix = if obj.path != undefined then obj.path else ""
            local filename = if obj.texture != undefined then obj.texture else ""
            if filename != "" then
                texFile = prefix + (this.GetImageFile filename)
            else
                texFile = prefix + filename
        ) catch ()
        if texFile != "" and texFile != undefined then
            tmp.texture = this.LoadTexture texFile (obj.rtexture - 1) false false
        else
            tmp.texture = this.LoadTexture "Textures\\white.blp" 0 false false
        tmp.replaceableId = obj.rtexture
        tmp.priorityPlane = obj.priorityplane
        
        tmp
    ),
    
    fn LoadRibbonEmitter obj =
    (
        local tmp = Wc3RibbonEmitter ()
        
        tmp.heightAbove = ::AnimationManager.FloatParameterProcess obj.above.controller obj.above
        tmp.heightBelow = ::AnimationManager.FloatParameterProcess obj.below.controller obj.below
        tmp.alpha = ::AnimationManager.FloatParameterProcess obj.alpha.controller obj.alpha
        tmp.color = ::AnimationManager.ColorParameterProcess obj.vertexcolor.controller obj.vertexcolor
        tmp.textureSlot = ::AnimationManager.FloatParameterProcess obj.slots.controller obj.slots
        
        if obj.visibility.isAnimated then 
            tmp.visibility = ::AnimationManager.VisibilityParameterProcess obj
            
        tmp.emissionRate = obj.emission
        tmp.lifeSpan = obj.life
        tmp.gravity = obj.gravity
        tmp.rows = obj.rows
        tmp.columns = obj.columns
        tmp.material = undefined
        
        tmp
    ),
    
    fn LoadEvent obj =
    (
        local tmp = Wc3Event ()
        tmp.eventTrack = ::AnimationManager.EventProcess obj
        
        if tmp.eventTrack == undefined then
            return undefined
        
        tmp
    ),
    
    fn LoadCollisionShape obj =
    (
        local tmp = Wc3CollisionShape ()
        if (classOf obj == Wc3CollisionBox or classOf obj == Box) then
        (
            tmp.shape = #Box
            
            local minPt = obj.min
            local maxPt = obj.max
            
            -- Reforged v1200: Vertices muessen lokal zum Pivot sein.
            -- Import hatte den Pivot zum Center addiert (bei animierten Nodes),
            -- Export muss ihn wieder abziehen um lokale Vertices zu bekommen.
            if this.exportVersion == 1200 then
            (
                local pivotOffset = obj.pivot
                minPt = minPt - pivotOffset
                maxPt = maxPt - pivotOffset
            )
            
            local wc3Min = [-minPt.y, minPt.x, minPt.z]
            local wc3Max = [-maxPt.y, maxPt.x, maxPt.z]
            
            local finalMin = [amin wc3Min.x wc3Max.x, amin wc3Min.y wc3Max.y, amin wc3Min.z wc3Max.z]
            local finalMax = [amax wc3Min.x wc3Max.x, amax wc3Min.y wc3Max.y, amax wc3Min.z wc3Max.z]
            
            tmp.vertices = #(finalMin, finalMax)
        )
        else (
            tmp.shape = #Sphere
            tmp.boundsRadius = obj.radius
            
            local center = obj.center
            
            -- Reforged v1200: Lokal zum Pivot
            if this.exportVersion == 1200 then
                center = center - obj.pivot
            
            local wc3Center = [-center.y, center.x, center.z]
            tmp.vertices = #(wc3Center)
        )
        tmp
    ),
    
    fn restoreHermitePositionTangents obj transData =
    (
        if transData == undefined or transData.translation == undefined then return()
        
        local hermiteStr = getAppData obj 9200
        if hermiteStr == undefined then return()
        if (findString hermiteStr "HERMITE_POS:") != 1 then return()
        
        -- Parse stored data into array
        local dataStr = substring hermiteStr 13 -1
        local entries = filterString dataStr "|"
        local storedKeys = #()
        
        for entry in entries do
        (
            local parts = filterString entry ":"
            if parts.count >= 4 then
            (
                local vals = filterString parts[2] ","
                local tIn = filterString parts[3] ","
                local tOut = filterString parts[4] ","
                
                if vals.count >= 3 and tIn.count >= 3 and tOut.count >= 3 then
                (
                    append storedKeys #(
                        [vals[1] as float, vals[2] as float, vals[3] as float],
                        [tIn[1] as float, tIn[2] as float, tIn[3] as float],
                        [tOut[1] as float, tOut[2] as float, tOut[3] as float]
                    )
                )
            )
        )
        
        -- Match export keys to stored keys by value proximity
        local keys = transData.translation.keys
        for k in keys do
        (
            local bestIdx = 0
            local bestDist = 1.0
            
            for i = 1 to storedKeys.count do
            (
                local d = distance k.value storedKeys[i][1]
                if d < bestDist then
                (
                    bestDist = d
                    bestIdx = i
                )
            )
            
            if bestIdx > 0 then
            (
                k.intan = storedKeys[bestIdx][2]
                k.outtan = storedKeys[bestIdx][3]
                deleteItem storedKeys bestIdx
            )
        )
    ),
    
    fn LoadCamera obj =
    (
        local tmp = Wc3Camera ()
        tmp.name = obj.name
        tmp.pivot = obj.pos
        local trans = TransformationData ()
        
        if obj.pos.isAnimated then trans.translation = ::AnimationManager.GetAnimationData obj obj.pos.controller ::AnimationManager.PositionKeys ::AnimationManager.PositionType
        tmp.transformations = trans
        tmp.fieldOfView = obj.fov
        tmp.farClip = obj.farclip
        tmp.nearClip = obj.nearclip
        tmp.targetPos = obj.target.pos
        tmp.targetTransformations = ::AnimationManager.GetTransformations obj.target
        tmp.targetTransformations.rotation = undefined
        tmp.targetTransformations.scale = undefined
        
        -- Restore stored Hermite tangents for camera position (KCTR)
        try ( this.restoreHermitePositionTangents obj tmp.transformations ) catch()
        
        -- Restore stored Hermite tangents for target position (KTTR)
        try ( this.restoreHermitePositionTangents obj.target tmp.targetTransformations ) catch()
        
        -- Camera Roll auslesen (roll_angle ist SubAnim [3][2] des Transform)
        try
        (
            local rollSub = getSubAnim (getSubAnim obj 3) 2
            local rollCtrl = rollSub.controller
            if rollCtrl != undefined and rollCtrl.keys != undefined and rollCtrl.keys.count > 0 then
            (
                tmp.rotation = ::AnimationManager.GetAnimationData rollCtrl rollCtrl ::AnimationManager.FloatKeys ::AnimationManager.FloatType checkSE:false
            )
        ) catch ()
        
        tmp
    ),
    
    fn LoadCornEmitter obj =
    (
        local tmp = Wc3CornEmitterExport()
        try ( tmp.path = obj.popcornPath ) catch ( tmp.path = "" )
        try ( tmp.lifeSpan = obj.lifeSpan ) catch ( tmp.lifeSpan = 0.0 )
        try ( tmp.speed = obj.speed ) catch ( tmp.speed = 0.0 )
        -- Alpha: animiert oder statisch
        -- Plugin und MDX beide 0-1, keine Skalierung noetig
        -- Backward-Compat: alte Importe speicherten alpha als *100 (0-100 statt 0-1)
        try
        (
            local alphaVal = obj.alpha
            -- Backward-Compat: Werte >1 stammen vom alten *100 Import-Bug
            if alphaVal > 1.0 then alphaVal = alphaVal / 100.0
            local alphaCtrl = obj.alpha.controller
            if alphaCtrl != undefined and isProperty alphaCtrl #keys and alphaCtrl.keys != undefined and alphaCtrl.keys.count > 0 then
            (
                tmp.alphaAnim = ::AnimationManager.FloatParameterProcess alphaCtrl alphaVal
                tmp.alpha = alphaVal
            )
            else
            (
                tmp.alpha = alphaVal
                tmp.alphaAnim = ParameterValue type:#Static data:alphaVal
            )
        ) catch ( tmp.alpha = 1.0 )
        try ( tmp.replaceableId = obj.replaceableId - 1 ) catch ( tmp.replaceableId = 0 )
        if tmp.replaceableId < 0 then tmp.replaceableId = 0
        -- EmissionRate: animiert oder statisch
        try
        (
            tmp.emissionRate = obj.emissionRate
            local erCtrl = obj.emissionRate.controller
            if erCtrl != undefined and isProperty erCtrl #keys and erCtrl.keys != undefined and erCtrl.keys.count > 0 then
                tmp.emissionRateAnim = ::AnimationManager.FloatParameterProcess erCtrl obj.emissionRate
            else
                tmp.emissionRateAnim = ParameterValue type:#Static data:obj.emissionRate
        ) catch ( tmp.emissionRate = 0.0 )
        -- Farbe: Plugin speichert als Color (0-255), MDX erwartet 0-1
        try
        (
            local c = obj.baseColor
            tmp.color = [c.r / 255.0, c.g / 255.0, c.b / 255.0]
        ) catch ( tmp.color = [1,1,1] )
        -- Flags: rawFlags bevorzugen (enthaelt Original-String mit Custom Animation-Namen)
        local flagStr = ""
        try ( flagStr = obj.rawFlags ) catch()
        if flagStr == undefined or flagStr == "" then
        (
            -- Fallback: aus Boolean-Flags neu bauen
            local flagParts = #()
            try ( if obj.flagAlways then append flagParts "Always" ) catch()
            try ( if obj.flagBirth then append flagParts "Birth" ) catch()
            try ( if obj.flagDeath then append flagParts "Death" ) catch()
            try ( if obj.flagDissipate then append flagParts "Dissipate" ) catch()
            try ( if obj.flagPortrait then append flagParts "Portrait" ) catch()
            flagStr = ""
            for i = 1 to flagParts.count do
            (
                if i > 1 then flagStr += ","
                flagStr += flagParts[i]
            )
        )
        tmp.flags = flagStr
        -- Visibility (gleicher Pfad wie BlizzPart2, Ribbon, etc.)
        tmp.visibility = ParameterValue()
        if obj.visibility.isAnimated then 
            tmp.visibility = ::AnimationManager.VisibilityParameterProcess obj
        tmp
    ),
    
    fn LoadFaceFXFromScene =
    (
        local result = #()
        for obj in this.sceneObjects do
        (
            if (not (this.IsIgnorable obj)) and (::Utils.IsFaceFX obj) then
            (
                local fx = Wc3FaceFXExport()
                try ( fx.name = obj.facefxName ) catch ( fx.name = obj.name )
                try ( fx.path = obj.facefxPath ) catch ( fx.path = "" )
                append result fx
            )
        )
        result
    ),

    fn LoadWc3Object obj =
    (
        local tmp = true
        local sub
        if ::Utils.IsAttachment obj then
        (
            sub = this.LoadAttachment obj
        )
        else if ::Utils.IsEventObject obj then
        (
            sub = this.LoadEvent obj
        )
        else if ::Utils.IsLight obj then
        (
            sub = this.LoadLight obj
        )
        else if ::Utils.IsParticleEmitter obj then
        (
            sub = this.LoadParticleEmitter obj
        )
        else if ::Utils.IsParticleEmitter2 obj then
        (
            sub = this.LoadParticleEmitter2 obj
        )
        else if ::Utils.IsRibbonEmitter obj then
        (
            sub = this.LoadRibbonEmitter obj
        )
        else if ::Utils.IsCollisionShape obj then
        (
            sub = this.LoadCollisionShape obj
        )
       else if this.exportVersion == 1200 and (::Utils.IsPopcornEmitter obj) then
        (
            sub = this.LoadCornEmitter obj
        )
        else if this.exportVersion == 1200 and (::Utils.IsFaceFX obj) then
        (
            -- FaceFX Daten werden separat im FAFX-Chunk exportiert, nicht als Node
            tmp = false
        )
        else
            tmp = false
        if tmp and sub != undefined then
        (
            local mdl = this.LoadMDLObject obj
            sub.MDLObj = mdl
            mdl.sub = sub
        )
    ),
    
    fn AssignObjectId objCollection =
    (
        for obj in objCollection do (
            obj.objId = this.ObjectIdCount
            this.ObjectIdCount = this.ObjectIdCount + 1
        )
    ),
    
    fn LoadObjects = 
    (
        this.sceneObjects = objects
        max modify mode
        
        local progressAdder = 100.0/(this.sceneObjects.count as float)
        local progress = 0.0
        
        ::Progress.downLevel 20 this.sceneObjects.count
        for obj in this.sceneObjects do
        (
            ::Progress.showItem ("Loading: " + obj.name)
            if (not (this.IsIgnorable obj)) then
            (
                if ::Utils.IsMesh obj then
                (
                    select obj
                    this.LoadGeoset obj
                    deselect obj
                )
                else
                (
                    this.LoadWc3Object obj
                )
            )
        )
        max create mode
        ::Progress.upLevel ()
        ::Progress.downLevel 10 1
        ::Progress.showItem "Building Object ids"
        
        local FaultyBones = ::Utils.ArrayFilter this.Wc3Objects (fn func a = classOf a.maxObj == FakeObject and classOf a.maxObj.parent == Editable_mesh)
        for o in FaultyBones do
        (
            local m = ::Utils.ArrayFindItemEx this.Meshes o.maxObj.parent (fn func2 a b = a.mesh == b)
            o.maxObj.parent = this.BonesMeshRef[this.Meshes[m].matrices[1][1]].mdlObj.maxObj
        )
        
        local helpersArray = #()
        for obj in this.Wc3Objects do
        (
            join helpersArray (::Utils.GetParents obj.maxObj)
        )
        helpersArray = (::Utils.ArrayMakeUnique helpersArray)
        
        local filteredHelpers = #()
        for elem in helpersArray do
        (
            if (::Utils.ArrayFindItemEx this.Wc3Objects elem this.ObjectChecker) == 0 then
                append filteredHelpers elem
        )
        helpersArray = filteredHelpers
        for h in helpersArray do
            this.LoadHelper h
        
        -- Keep unused bones/helpers: load all remaining bone/helper objects
        -- from the scene that aren't already loaded as Wc3Objects
        if this.keepUnusedBonesHelpers then
        (
            for obj in this.sceneObjects do
            (
                if (not (this.IsIgnorable obj)) and \
                    (not (this.IsBipedNub obj)) and \
                    (not (::Utils.IsMesh obj)) and \
                   (not (::Utils.IsFaceFX obj)) and \
                   (::Utils.ArrayFindItemEx this.Wc3Objects obj this.ObjectChecker) == 0 and \
                   (superclassOf obj == Helper or \
                    (classOf obj) as string == "BoneGeometry" or \
                    classOf obj == Dummy or \
                    classOf obj == Point or \
                    classOf obj == Biped_Object or \
                    classOf obj == FakeObject) then
                (
                    this.LoadHelper obj
                )
            )
        )
        
        -- Parent-Lookup Dictionary einmal bauen statt pro Objekt linear suchen
        local parentLookup = Dictionary #string
        for i = 1 to this.Wc3Objects.count do
        (
            local mObj = this.Wc3Objects[i].maxObj
            if (classOf mObj) != FakeObject then
            (
                local key = (getHandleByAnim mObj) as string
                parentLookup[key] = i
            )
        )
        
        for obj in this.Wc3Objects do
        (
            local p = obj.maxObj.parent
            if p != undefined then
            (
                if (classOf p) != FakeObject then
                (
                    local key = (getHandleByAnim p) as string
                    if hasDictValue parentLookup key then
                        obj.parent = this.Wc3Objects[parentLookup[key]]
                )
                else
                (
                    -- FakeObject parent: fallback to linear search
                    local index = ::Utils.ArrayFindItemEx this.Wc3Objects p this.ObjectChecker
                    if index != 0 then obj.parent = this.Wc3Objects[index]
                )
            )
        )
        
        local h = Hierarchy ()
        for obj in this.Wc3Objects do
            ::Utils.HierarchyAddNode h obj
        this.Wc3Bones = ::Utils.HierarchyFilter h (fn isMdlBone a = ( a.sub.type == #Wc3Bone))
        this.AssignObjectId this.Wc3Bones
        this.Wc3Lights = ::Utils.HierarchyFilter h (fn isMdlHelper a = ( a.sub.type == #Wc3Light))
        this.AssignObjectId this.Wc3Lights
        this.Wc3Helpers = ::Utils.HierarchyFilter h (fn isMdlHelper a = ( a.sub.type == #Wc3Helper))
        this.AssignObjectId this.Wc3Helpers
        this.Wc3Attachments = ::Utils.HierarchyFilter h (fn isMdlHelper a = ( a.sub.type == #Wc3Attachment))
        this.AssignObjectId this.Wc3Attachments
        this.Wc3PartEmitters1 = ::Utils.HierarchyFilter h (fn isMdlHelper a = ( a.sub.type == #Wc3ParticleEmitter))
        this.AssignObjectId this.Wc3PartEmitters1
        this.Wc3PartEmitters2 = ::Utils.HierarchyFilter h (fn isMdlHelper a = ( a.sub.type == #Wc3ParticleEmitter2))
        this.AssignObjectId this.Wc3PartEmitters2
        this.Wc3RibbonEmitters = ::Utils.HierarchyFilter h (fn isMdlHelper a = ( a.sub.type == #Wc3RibbonEmitter))
        this.AssignObjectId this.Wc3RibbonEmitters
        this.Wc3Events = ::Utils.HierarchyFilter h (fn isMdlHelper a = ( a.sub.type == #Wc3Event))
        this.AssignObjectId this.Wc3Events
        this.Wc3Collisions = ::Utils.HierarchyFilter h (fn isMdlHelper a = ( a.sub.type == #Wc3CollisionShape))
        this.AssignObjectId this.Wc3Collisions
        
        -- Reforged: CORN Emitters
        if this.exportVersion == 1200 then
        (
            this.Wc3CornEmitters = ::Utils.HierarchyFilter h (fn isCorn a = ( a.sub.type == #Wc3CornEmitter))
            this.AssignObjectId this.Wc3CornEmitters
        )
        
        for m in this.Meshes do
        (
            for n in m.matrices do
            (
                for i = 1 to n.count do n[i] = this.BonesMeshRef[n[i]].mdlobj.objid
            )
            -- v1200: SKIN Bone-Indices zu ObjIds konvertieren
            if m.skinData != undefined then
            (
                for sv in m.skinData where sv != undefined do
                (
                    for b = 1 to 4 do
                    (
                        if sv[b] > 0 and sv[b] <= this.BonesMeshRef.count then
                            sv[b] = this.BonesMeshRef[sv[b]].mdlobj.objid
                        else
                            sv[b] = 0
                    )
                )
            )
        )
        
        this.Wc3Cameras = #()
        for c in $cameras do
        (
            if (not (this.IsIgnorable c)) then
                if classOf c == Targetcamera then append this.Wc3Cameras (this.LoadCamera c)
        )
        ::Progress.upLevel ()
    ),
    
    -- ============================================================================
    -- EXTENTS CALCULATION
    -- ============================================================================
    
    fn CalculateBoundsRadius minExtent maxExtent =
    (
        length ((maxExtent - minExtent) / 2.0)
    ),
    
    fn CalculateExtents m start end interp =
    (
        local MinExtent = undefined
        local MaxExtent = undefined
        for i = start to end by interp do
        (
            at time i
            (
                if m.visibility == true then
                (
                    if MinExtent == undefined then MinExtent = m.min else
                    (
                        if MinExtent.x > m.min.x then MinExtent.x = m.min.x
                        if MinExtent.y > m.min.y then MinExtent.y = m.min.y
                        if MinExtent.z > m.min.z then MinExtent.z = m.min.z
                    )
                    if MaxExtent == undefined then MaxExtent = m.max else
                    (
                        if MaxExtent.x < m.max.x then MaxExtent.x = m.max.x
                        if MaxExtent.y < m.max.y then MaxExtent.y = m.max.y
                        if MaxExtent.z < m.max.z then MaxExtent.z = m.max.z
                    )
                )
            )
        )
        if MinExtent == undefined then MinExtent = [0,0,0]
        if MaxExtent == undefined then MaxExtent = [0,0,0]
        Wc3Extent MinExtent MaxExtent (this.CalculateBoundsRadius MinExtent MaxExtent)
    ),
    
    fn CalculateExtentsFromExtentsList extentList =
    (
        local minExtent = undefined
        local maxExtent = undefined
        for m in extentList do
        (
            if minExtent == undefined then minExtent = copy m.minExtent else
            (
                if minExtent.x > m.minExtent.x then minExtent.x = m.minExtent.x
                if minExtent.y > m.minExtent.y then minExtent.y = m.minExtent.y
                if minExtent.z > m.minExtent.z then minExtent.z = m.minExtent.z
            )
            if maxExtent == undefined then maxExtent = copy m.maxExtent else
            (
                if maxExtent.x < m.maxExtent.x then maxExtent.x = m.maxExtent.x
                if maxExtent.y < m.maxExtent.y then maxExtent.y = m.maxExtent.y
                if maxExtent.z < m.maxExtent.z then maxExtent.z = m.maxExtent.z
            )
        )
        if minExtent == undefined then minExtent = [0,0,0]
        if maxExtent == undefined then maxExtent = [0,0,0]
        Wc3Extent minExtent maxExtent (this.CalculateBoundsRadius minExtent maxExtent)
    ),
    
    fn LoadExtents extentsCalculation:1 extentsPrecision:5 =
    (
        ::Progress.downLevel 20 this.Wc3Sequences.count
        
        local SeqInterp = (extentsCalculation == 1)
        local interp = 1
        if (not SeqInterp) then interp = (((extentsPrecision*framerate)/30) as integer)
        if interp < 1 then interp = 1
        local tmp = #()
        if (this.Wc3Sequences.count != 0) then
        (
            for s in this.Wc3Sequences do
            (
                ::Progress.showItem ("Calculating Extents: "+ s.name)
                if SeqInterp then interp = (((extentsPrecision*(s.endTime-s.startTime))/framerate) as integer)
                if interp < 1 then interp = 1
                local tmp2 = #()
                for geo in this.Meshes do
                (
                    local m = this.CalculateExtents geo.mesh s.startTime s.endTime interp
                    append geo.extents m
                    if not geo.unselectable then
                        append tmp2 m
                )
                s.extents = this.CalculateExtentsFromExtentsList tmp2
                if not((matchpattern s.name pattern:"death*") or (matchpattern s.name pattern:"decay*") or (matchpattern s.name pattern:"dissipate*")) then
                    append tmp s.extents
            )
            this.Wc3Extents = this.CalculateExtentsFromExtentsList tmp
            for geo in this.Meshes do
                geo.mainExtents = this.CalculateExtentsFromExtentsList geo.extents
        )
        else
        (
            local tmp = #()
            for geo in this.Meshes do
            (
                local m = this.CalculateExtents geo.mesh 0 10
                geo.mainExtents = m
                append tmp m
            )
            this.Wc3Extents = this.CalculateExtentsFromExtentsList tmp
        )
        ::Progress.upLevel ()
    ),
    
    -- ============================================================================
    -- GEOSET OPERATIONS
    -- ============================================================================
    
    fn mergeExtents ex1 ex2 =
    (
        local result = Wc3Extent ()
        result.minExtent = ::Utils.minPoint ex1.minExtent ex2.minExtent
        result.maxExtent = ::Utils.maxPoint ex1.maxExtent ex2.maxExtent
        result.bound = this.CalculateBoundsRadius result.minExtent result.maxExtent
        result
    ),
    
    fn compareVisibility v1 v2 =
    (
        if (v1 != undefined and v2 != undefined) then
            ::Utils.compareControllers v1.visibility.controller v2.visibility.controller
        else (v1 == undefined and v2 == undefined)
    ),
    
    fn areGeosetsSimilar geo1 geo2 =
    (
        local mesh1 = geo1.mesh
        local mesh2 = geo2.mesh
        mesh1.material == mesh2.material and (this.compareVisibility geo1.visObj geo2.visObj)
    ),
    
    fn mergeGeosets geo1 geo2 =
    (
        fn expandMatrix &matrix newValue =
        (
            local found = ::Utils.ArrayFindItem matrix newValue
            if found == 0 then
            (
                append matrix newValue
                found = matrix.count - 1
            ) else found = found - 1
            found
        )
        local matrices = deepCopy geo1.matrices
        local newGeo = Wc3Geoset ()
        local RefittedIndices = ::Utils.ArrayRepeat geo2.matrixindices.count undefined
        for j = 1 to geo2.matrices.count do
        (
            local oldId = j - 1
            local newId = expandMatrix matrices geo2.matrices[j]
            for i = 1 to RefittedIndices.count do
                if geo2.matrixindices[i] == oldId then
                    RefittedIndices[i] = newId
        )
        local faces = deepCopy geo1.faces
        local maxVert = geo1.vertices.count
        for f in geo2.faces do 
            append faces (f + maxVert)
        newGeo.vertices = #()
        join newGeo.vertices geo1.vertices
        join newGeo.vertices geo2.vertices
        newGeo.normals = #()
        join newGeo.normals geo1.normals
        join newGeo.normals geo2.normals
        newGeo.tvertices = #()
        local maxVert = geo1.tvertices.count
        if maxVert < geo2.tvertices.count then maxVert = geo2.tvertices.count
        local maxer1 = geo1.tvertices.count + 1, maxer2 = geo2.tvertices.count + 1
        for i = 1 to maxVert do
        (
            local tmp = #()
            join tmp geo1.tvertices[mod i maxer1]
            join tmp geo2.tvertices[mod i maxer2]
            append newGeo.tvertices tmp
        )
        newGeo.matrixindices = #()
        join newGeo.matrixindices geo1.matrixindices
        join newGeo.matrixindices RefittedIndices
        newGeo.faces = faces
        newGeo.matrices = matrices
        newGeo.mainExtents = this.mergeExtents geo1.mainExtents geo2.mainExtents
        newGeo.extents = #()
        for i = 1 to geo1.extents.count do
            append newGeo.extents (this.mergeExtents geo1.extents[i] geo2.extents[i])
        newGeo.visObj = geo1.visObj
        
        local tmpBones = #()
        for m in matrices do
            for b in m do
            (
                local sub = this.Wc3Bones[b+1].sub
                local i1 = findItem sub.geosets geo1
                if i1 != 0 then deleteItem sub.geosets i1
                local i2 = findItem sub.geosets geo2
                if i2 != 0 then deleteItem sub.geosets i2
                append tmpBones sub
            )
        tmpBones = ::Utils.ArrayMakeUnique tmpBones
        for b in tmpBones do append b.geosets newGeo
        newGeo.mesh = FakeMesh "Generated Geoset" geo1.mesh.material geo1.mesh.modifiers
        newGeo.unselectable = geo1.unselectable and geo2.unselectable
        local i1 = findItem this.Meshes geo1
        deleteItem this.Meshes i1
        local i2 = findItem this.Meshes geo2
        deleteItem this.Meshes i2
        append this.Meshes newGeo
        newGeo
    ),
    
    fn mergeSimilarGeosets =
    (
        local pendingGeosets = this.Meshes.count != 0
        while (pendingGeosets) do
        (
            pendingGeosets = false
            local i = 1
            local stayIn = true
            local count = this.Meshes.count
            local onMerge
            while (i != count and stayIn) do
            (
                local geo = this.Meshes[i]
                onMerge = #(geo)
                for j = (i+1) to count do
                    if j != i then
                        if (this.areGeosetsSimilar geo this.Meshes[j]) then
                        (
                            stayIn = false
                            append onMerge this.Meshes[j]
                        )
                i += 1
            )
            if (not stayIn) then
            (
                local newGeo = onMerge[1]
                deleteItem onMerge 1
                while (onMerge.count != 0) do
                (
                    newGeo = this.mergeGeosets newGeo onMerge[1]
                    deleteItem onMerge 1
                )
                pendingGeosets = true
            )
        )
    ),
    
    fn cleanMatrices geo =
    (
        local tmp = ::Utils.ArrayMakeUnique geo.matrixIndices
        sort tmp
        local newMatrices = #()
        local tmp2 = #()
        local i = 0
        local indices = geo.matrixIndices
        for t in tmp do
        (
            append newMatrices geo.matrices[t+1]
            tmp2[t+1] = i
            i += 1
        )
        for i = 1 to indices.count do
            indices[i] = tmp2[indices[i] + 1]
        geo.matrices = newMatrices
        OK
    ),
    
    fn GatherBoundingVertices geo1 geo2 thresshold =
    (
        thresshold = thresshold*0.5
        local min2 = geo2.mesh.min - thresshold
        local max2 = geo2.mesh.max + thresshold
        local result = #()
        for i = 1 to geo1.vertices.count do
            if (::Utils.IsPointInBounds min2 max2 geo1.vertices[i]) then
                append result i
        result
    ),
    
    fn applySyntheticSkin geo1 geo2 thresshold fixNormals =
    (
        fn mergeMatrices array array2 =
        (
            local result = deepCopy array
            join result array2
            result = ::Utils.ArrayMakeUnique result
            sort result
            result
        )
        fn addNewMatrix &array a =
        (
            local result = ::Utils.ArrayFindItem array a
            if (result == 0) then
            (
                append array a
                result = array.count 
            )
            (result - 1)
        )
        local vertices1 = this.GatherBoundingVertices geo1 geo2 thresshold
        local vertices2 = this.GatherBoundingVertices geo2 geo1 thresshold
        local ts = thresshold*0.5
        fn tmp p1 t = ((abs p1.x) < t and (abs p1.y) < t and (abs p1.z) < t)
        for i = 1 to vertices1.count do
        (
            local v = vertices1[i]
            for j = 1 to vertices2.count do
            (
                local w = vertices2[j]
                if (tmp (geo1.vertices[v] - geo2.vertices[w]) ts) then
                (
                    local matrix = mergeMatrices (geo2.matrices[geo2.matrixIndices[w]+1]) (geo1.matrices[geo1.matrixindices[v]+1])
                    geo1.matrixindices[v] = addNewMatrix geo1.matrices matrix
                    geo2.matrixindices[w] = addNewMatrix geo2.matrices matrix
                    if fixNormals then
                    (
                        local newNormal = (geo1.normals[v] + geo2.normals[w])*0.5
                        geo1.normals[v] = newNormal
                        geo2.normals[w] = newNormal
                    )
                )
            )
        )
        this.cleanMatrices geo1
        this.cleanMatrices geo2
    ),
    
    fn sortMatrices geo =
    (
        for m in geo.matrices do sort m
    ),
    
    fn DoSyntheticSkin FixNormals:true Thresshold:0.1 =
    (
        local objs = ::Utils.ArrayFilter this.Meshes (fn func a = (::Utils.getWc3Skin a.mesh) == #ATSkin)
        
        for g in objs do this.sortMatrices g
        
        local colMatrix = ::Utils.collideMatrix (::Utils.ArrayMap objs (fn tmp a = a.mesh)) 1
        for i = 1 to objs.count do
            for j = i + 1 to objs.count do
                if colMatrix[i][j] then
                    this.applySyntheticSkin objs[i] objs[j] Thresshold FixNormals
    ),
    
    fn averageSharedNormals thresshold:0.001 =
    (
        -- Nur nötig wenn mehr als 1 Geoset existiert
        if this.Meshes.count < 2 then return undefined
        
        -- Sammle alle Vertices + Normals aus allen Geosets mit Referenz zurück
        -- Jeder Eintrag: #(position, geosetIndex, vertexIndex)
        local totalVerts = 0
        for g = 1 to this.Meshes.count do
            totalVerts += this.Meshes[g].vertices.count
        
        if totalVerts == 0 then return undefined
        
        local allVerts = #()
        allVerts[totalVerts] = undefined  -- Pre-allokieren statt append
        
        local idx = 0
        for g = 1 to this.Meshes.count do
        (
            local geo = this.Meshes[g]
            for v = 1 to geo.vertices.count do
            (
                idx += 1
                allVerts[idx] = #(geo.vertices[v], g, v)
            )
        )
        
        -- Sortiere nach X-Koordinate → O(n log n) statt O(n²)
        -- Danach liegen Vertices an derselben Position nebeneinander
        fn sortByX a b =
        (
            local d = a[1].x - b[1].x
            if d < 0 then return -1
            if d > 0 then return 1
            d = a[1].y - b[1].y
            if d < 0 then return -1
            if d > 0 then return 1
            d = a[1].z - b[1].z
            if d < 0 then return -1
            if d > 0 then return 1
            0
        )
        qsort allVerts sortByX
        
        local fixCount = 0
        local processed = #{}
        processed.count = totalVerts
        
        for i = 1 to totalVerts where not processed[i] do
        (
            local refPos = allVerts[i][1]
            local group = #(i)
            
            -- Scanne nur vorwärts solange X innerhalb Threshold liegt
            -- Durch die Sortierung sind ALLE Kandidaten in diesem Fenster
            local j = i + 1
            while j <= totalVerts and (allVerts[j][1].x - refPos.x) < thresshold do
            (
                if not processed[j] then
                (
                    local posJ = allVerts[j][1]
                    if (abs (posJ.y - refPos.y)) < thresshold and \
                       (abs (posJ.z - refPos.z)) < thresshold then
                        append group j
                )
                j += 1
            )
            
            -- Wenn mehrere Vertices an derselben Position gefunden
            if group.count > 1 then
            (
                -- Prüfe ob Vertices aus mehr als 1 Geoset beteiligt sind
                local geosets = #{}
                for k in group do geosets[allVerts[k][2]] = true
                
                if (geosets as array).count > 1 then
                (
                    -- Berechne Durchschnitt aller Normals an dieser Position
                    local avgNormal = [0,0,0]
                    for k in group do
                    (
                        local entry = allVerts[k]
                        avgNormal += this.Meshes[entry[2]].normals[entry[3]]
                    )
                    avgNormal = avgNormal / group.count as float
                    
                    -- Setze den Durchschnitt für ALLE Vertices an dieser Position
                    for k in group do
                    (
                        local entry = allVerts[k]
                        this.Meshes[entry[2]].normals[entry[3]] = avgNormal
                    )
                    
                    fixCount += group.count
                )
            )
            
            -- Markiere gesamte Gruppe als verarbeitet
            for k in group do processed[k] = true
        )
        
        if fixCount > 0 then
            format "WhiteoutDex: Fixed % shared normals across % geosets\n" fixCount this.Meshes.count
    ),

    fn DoGeosetTweaks mergeGeosets:true ATFixNormals:true ATThresshold:0.1 fixSharedNormals:true =
    (
        ::Progress.downLevel 10 3
        if ATFixNormals then
            ::Progress.showItem "Calculating AT Skin"
        else
            ::Progress.showItem "Processing Skin Data"
        this.DoSyntheticSkin FixNormals:ATFixNormals Thresshold:ATThresshold
        if fixSharedNormals then
        (
            ::Progress.showItem "Fixing shared normals"
            this.averageSharedNormals ()
        )
        else
        (
            ::Progress.showItem "Skipping shared normals"
        )
        ::Progress.showItem "Merging Geosets"
        if mergeGeosets then this.mergeSimilarGeosets ()
        
        this.GeosetCounts = 0
        for m in this.Meshes do
        (
            m.id = this.GeosetCounts
            this.GeosetCounts += 1
        )
        ::Progress.upLevel ()
    ),
    
    -- ============================================================================
    -- TEXTURE & MATERIAL LOADING
    -- ============================================================================
    
    fn UsesAnimatedTextures T =
    (
        local s = T.filename
        (s != undefined) and (s.count > 3) and (SubString s (s.count-3) -1 == ".ifl")
    ),
    
    fn GetImageFile s = 
    (
        return (getFilenameFile s) + ".blp"
    ),
    
    fn GetImageArray T =
    (
        fn get_names name a = append a name
        local files = #()
        enumerateFiles T get_names files 
        sort files
        
        local i = findItem files T.filename
        if i != 0 then deleteItem files i
        files
    ),
    
    fn LoadTexture file replaceable wrapWidth wrapHeight =
    (
        fn TextureChecker a b = (
            a.replaceableId == b.replaceableId and a.path == b.path and 
            a.wrapWidth == b.wrapWidth and a.wrapHeight == b.wrapHeight
        )
        local texture = Wc3Texture file replaceable wrapWidth wrapHeight
        local index = ::Utils.ArrayFindItemEx this.Wc3Textures texture TextureChecker
        if index == 0 then
        (
            texture.id = this.TextureCount
            this.TextureCount = this.TextureCount + 1
            append this.Wc3Textures texture
        ) else texture = this.Wc3Textures[index]
        texture
    ),
    
    fn LoadTextures texture wc3Path replaceable Wrap_Width Wrap_Height =
    (
        local result
        local texPath
        if texture != undefined and texture.filename != undefined then
        (
            if (this.UsesAnimatedTextures texture) then
            (
                local files = ::Utils.ArrayMap (this.GetImageArray texture) this.GetImageFile
                local playBack = texture.playbackrate
                local ids = #()
                for f in files do 
                (
                    local t = (this.LoadTexture (wc3Path + f) replaceable Wrap_Width Wrap_Height)
                    append ids t.id
                )
                result = ::AnimationManager.GetTextureAnimation ids playBack
            )
            else
            (
                local t
                if (replaceable == 0) then
                    t = this.LoadTexture (wc3Path + (this.GetImageFile texture.filename)) replaceable Wrap_Width Wrap_Height
                else
                (
                    -- Texturpfad immer beibehalten, auch fuer replaceable Texturen
                    -- (Team Color etc. brauchen den Alpha-Kanal der Textur als Maske)
                    t = this.LoadTexture (wc3Path + (this.GetImageFile texture.filename)) replaceable Wrap_Width Wrap_Height
                )
                result = ParameterValue #Static t.id
            )
        )
        else
        (
            local u_tile = false, v_tile = false
            if (texture != undefined) then
            (
                u_tile = Wrap_Width 
                v_tile = Wrap_Height
            )
            local t
            if (replaceable == 0) then
                t = this.LoadTexture "Textures\white.blp" replaceable u_tile v_tile
            else
                t = this.LoadTexture "" replaceable u_tile v_tile
            result = ParameterValue #Static t.id
        )
        result
    ),
    
    fn buildTvertexAnimation cu cv crot csu csv cw:undefined =
    (
        local hasTranslationKeys = false
        local hasRotationKeys = false
        local hasScaleKeys = false
        
        -- Check Translation
        if cu != undefined and isProperty cu #keys and cu.keys != undefined and cu.keys.count > 0 then
            hasTranslationKeys = true
        if cv != undefined and isProperty cv #keys and cv.keys != undefined and cv.keys.count > 0 then
            hasTranslationKeys = true
        if cw != undefined and isProperty cw #keys and cw.keys != undefined and cw.keys.count > 0 then
            hasTranslationKeys = true
        
        -- Check Rotation
        if crot != undefined and isProperty crot #keys and crot.keys != undefined and crot.keys.count > 0 then
            hasRotationKeys = true
        
        -- Check Scale
        if csu != undefined and isProperty csu #keys and csu.keys != undefined and csu.keys.count > 0 then
            hasScaleKeys = true
        if csv != undefined and isProperty csv #keys and csv.keys != undefined and csv.keys.count > 0 then
            hasScaleKeys = true
        
        if (hasTranslationKeys or hasRotationKeys or hasScaleKeys) then
        (
            local tv = Wc3TvertexAnim()
            local trans = TransformationData()
            
            -- Translation
            if hasTranslationKeys then
            (
                trans.translation = ::AnimationManager.combineTVAnimation cu cv cw
            )
            
            -- Rotation (W_Angle zu Quaternion)
            if hasRotationKeys then
            (
                trans.rotation = this.buildTVRotationAnimation crot
            )
            
            -- Scale (U_Tiling, V_Tiling)
            if hasScaleKeys then
            (
                trans.scale = ::AnimationManager.combineTVAnimation csu csv undefined
            )
            
            local hasValidData = false
            if trans.translation != undefined and trans.translation.keys != undefined and trans.translation.keys.count > 0 then
                hasValidData = true
            if trans.rotation != undefined and trans.rotation.keys != undefined and trans.rotation.keys.count > 0 then
                hasValidData = true
            if trans.scale != undefined and trans.scale.keys != undefined and trans.scale.keys.count > 0 then
                hasValidData = true
            
            if hasValidData then
            (
                tv.transformation = trans
                append this.Wc3TvertexAnims tv
                return tv
            )
        )
        
        undefined
    ),

    -- Konvertiert W_Angle (Grad) zu Quaternion für Texture Animation Export
    fn buildTVRotationAnimation angleController =
    (
        if angleController == undefined then return undefined
        if not isProperty angleController #keys then return undefined
        if angleController.keys == undefined or angleController.keys.count == 0 then return undefined
        
        -- Convert angle (degrees) to Z-rotation quaternion.
        -- MAXScript sin/cos already take degrees, so NO degToRad needed.
        fn angleToQuat angleDeg =
        (
            local a = -angleDeg  -- Negate for coordinate system
            quat 0 0 (sin (a/2)) (cos (a/2))
        )
        
        local animData = AnimationData()
        animData.type = #Linear
        
        -- Collect all key times and their angle values
        local keyTimes = #()
        local keyAngles = #()
        for i = 1 to angleController.keys.count do
        (
            local t = angleController.keys[i].time
            local v = at time t angleController.value
            append keyTimes t
            append keyAngles v
        )
        
        -- Build final key list with intermediate keys where rotation > 120 degrees.
        -- Quaternion SLERP always takes the shortest path, so any step > 180
        -- would reverse direction. We use 120 as safe maximum step size.
        local maxStepDeg = 120.0
        local allKeys = #()
        
        for i = 1 to keyTimes.count do
        (
            if i == 1 then
            (
                append allKeys (KeyData keyTimes[i] (angleToQuat keyAngles[i]))
            )
            else
            (
                local prevAngle = keyAngles[i - 1]
                local currAngle = keyAngles[i]
                local prevTime = keyTimes[i - 1]
                local currTime = keyTimes[i]
                local deltaAngle = currAngle - prevAngle
                local absDelta = abs deltaAngle
                
                if absDelta > maxStepDeg then
                (
                    -- Insert intermediate keys
                    local numSteps = (ceil (absDelta / maxStepDeg)) as integer
                    for s = 1 to numSteps do
                    (
                        local fraction = s as float / numSteps as float
                        local interpAngle = prevAngle + deltaAngle * fraction
                        local interpTime = prevTime + (currTime - prevTime) * fraction
                        append allKeys (KeyData interpTime (angleToQuat interpAngle))
                    )
                )
                else
                (
                    append allKeys (KeyData currTime (angleToQuat currAngle))
                )
            )
        )
        
        animData.keys = allKeys
        
        -- Check for Global Sequence
        if getAfterORT angleController == #cycle then
            animData.globalSequenceId = ::AnimationManager.LoadGlobalSequence animData.keys[animData.keys.count].time
        
        animData
    ),
    
    fn LoadLayer l mat = 
    (
        local layer = Wc3Layer()
        layer.filtermode = l.filtermode
        
        if classOf l != FakeMaterial then
        (
            if l.alpha.controller != undefined and \
            isProperty l.alpha.controller #keys and \
            l.alpha.controller.keys != undefined and \
            l.alpha.controller.keys.count > 0 then
            (
                layer.alpha = ::AnimationManager.FloatParameterProcess l.alpha.controller l.alpha
            )
            else
            (
                layer.alpha = ParameterValue #Static l.alpha
            )
        )
        else
            layer.alpha = ParameterValue #Static l.alpha
        
        layer.unselectable = l.unselectable
        layer.nodephtest = l.nodephtest
        layer.nodephset = l.nodephset
        layer.twosided = l.twosides
        layer.SphereEnvMap = l.SphereEnvMap
        layer.unshaded = l.unshaded
        layer.unfogged = l.unfogged
        layer.ConstantColor = l.ConstantColor
        -- Plugin-Dropdown: 1=Not Used, 2=Near to Far, 3=Far to Near
        layer.SortPrimsNearZ = (l.SortPrimsFarZ == 2)
        layer.SortPrimsFarZ = (l.SortPrimsFarZ == 3)
        layer.FullResolution = l.FullResolution
        layer.PriorityPlane = l.PriorityPlane
        
        -- Reforged v1200: origDiffusePath bevorzugen (bewahrt Extension + Forward Slashes)
        if this.exportVersion == 1200 and (try (l.origDiffusePath != undefined and l.origDiffusePath != "") catch(false)) then
        (
            local t = this.LoadTexture l.origDiffusePath (l.retexture - 1) l.Wrap_Width l.Wrap_Height
            layer.textures = ParameterValue #Static t.id
        )
        else
            layer.textures = this.LoadTextures l.texture l.path (l.retexture - 1) l.Wrap_Width l.Wrap_Height
        
        if (l.texture != undefined and classOf l.texture == BitmapTexture) then
        (
            layer.coordId = l.texture.coords.mapChannel - 1
            
            local hasUAnimation = false
            local hasVAnimation = false
            local hasRotAnimation = false
            local hasUScaleAnimation = false
            local hasVScaleAnimation = false
            
            -- Check U_Offset
            if l.texture.coords.U_offset.controller != undefined then
            (
                if isProperty l.texture.coords.U_offset.controller #keys and \
                l.texture.coords.U_offset.controller.keys != undefined and \
                l.texture.coords.U_offset.controller.keys.count > 0 then
                (
                    hasUAnimation = true
                )
            )
            
            -- Check V_Offset
            if l.texture.coords.V_offset.controller != undefined then
            (
                if isProperty l.texture.coords.V_offset.controller #keys and \
                l.texture.coords.V_offset.controller.keys != undefined and \
                l.texture.coords.V_offset.controller.keys.count > 0 then
                (
                    hasVAnimation = true
                )
            )
            
            -- Check W_Angle (Rotation)
            if l.texture.coords.W_angle.controller != undefined then
            (
                if isProperty l.texture.coords.W_angle.controller #keys and \
                l.texture.coords.W_angle.controller.keys != undefined and \
                l.texture.coords.W_angle.controller.keys.count > 0 then
                (
                    hasRotAnimation = true
                )
            )
            
            -- Check U_Tiling (Scale X)
            if l.texture.coords.U_tiling.controller != undefined then
            (
                if isProperty l.texture.coords.U_tiling.controller #keys and \
                l.texture.coords.U_tiling.controller.keys != undefined and \
                l.texture.coords.U_tiling.controller.keys.count > 0 then
                (
                    hasUScaleAnimation = true
                )
            )
            
            -- Check V_Tiling (Scale Y)
            if l.texture.coords.V_tiling.controller != undefined then
            (
                if isProperty l.texture.coords.V_tiling.controller #keys and \
                l.texture.coords.V_tiling.controller.keys != undefined and \
                l.texture.coords.V_tiling.controller.keys.count > 0 then
                (
                    hasVScaleAnimation = true
                )
            )
            
            -- W_Offset: standalone plugin parameter (no bitmap coord equivalent)
            local wController = undefined
            local hasWAnimation = false
            try
            (
                if l.Wc3_W_Offset.controller != undefined and \
                   isProperty l.Wc3_W_Offset.controller #keys and \
                   l.Wc3_W_Offset.controller.keys != undefined and \
                   l.Wc3_W_Offset.controller.keys.count > 0 then
                (
                    wController = l.Wc3_W_Offset.controller
                    hasWAnimation = true
                )
            ) catch()
            
            if (hasUAnimation or hasVAnimation or hasRotAnimation or hasUScaleAnimation or hasVScaleAnimation or hasWAnimation) then
            (
                local uController = if hasUAnimation then l.texture.coords.U_offset.controller else undefined
                local vController = if hasVAnimation then l.texture.coords.V_offset.controller else undefined
                local rotController = if hasRotAnimation then l.texture.coords.W_angle.controller else undefined
                local uScaleController = if hasUScaleAnimation then l.texture.coords.U_tiling.controller else undefined
                local vScaleController = if hasVScaleAnimation then l.texture.coords.V_tiling.controller else undefined
                
                layer.tvertexid = this.buildTvertexAnimation vController uController rotController uScaleController vScaleController cw:wController
            )
        )
        else
            layer.coordId = 0
        
        -- Reforged: PBR Material-Properties auslesen
        if this.exportVersion == 1200 and classOf l != FakeMaterial then
        (
            -- Fresnel-Daten vom Plugin lesen
            try ( layer.emissiveGain = l.EmissiveGain ) catch ( layer.emissiveGain = 1.0 )
            try ( layer.fresnelColor = [l.FresnelColorR, l.FresnelColorG, l.FresnelColorB] ) catch ( layer.fresnelColor = [1,1,1] )
            try ( layer.fresnelOpacity = l.FresnelOpacity ) catch ( layer.fresnelOpacity = 0.0 )
            try ( layer.fresnelTeamColor = l.FresnelTeamColor ) catch ( layer.fresnelTeamColor = 0.0 )
            
            -- PBR Texturen registrieren (Round-Trip: origPfade vom Import)
            -- FIX: Wrap-Flags vom Layer uebernehmen statt hardcoded false
            local pbrWrapW = l.Wrap_Width
            local pbrWrapH = l.Wrap_Height
            try
            (
                if l.origNormalPath != undefined and l.origNormalPath != "" then
                    layer.normalTexId = (this.LoadTexture l.origNormalPath 0 pbrWrapW pbrWrapH).id
            ) catch ()
            try
            (
                if l.origOrmPath != undefined and l.origOrmPath != "" then
                    layer.ormTexId = (this.LoadTexture l.origOrmPath 0 pbrWrapW pbrWrapH).id
            ) catch ()
            try
            (
                if l.origEmissivePath != undefined and l.origEmissivePath != "" then
                    layer.emissiveTexId = (this.LoadTexture l.origEmissivePath 0 pbrWrapW pbrWrapH).id
            ) catch ()
            try
            (
                if l.origReflectionPath != undefined and l.origReflectionPath != "" then
                    layer.envMapTexId = (this.LoadTexture l.origReflectionPath 0 pbrWrapW pbrWrapH).id
            ) catch ()
            
            -- TeamColor Texture registrieren (replaceable=1, leerer Pfad)
            try
            (
                if (isProperty l #teamColorTexId) and l.teamColorTexId != undefined and l.teamColorTexId > 0 then
                    layer.teamColorTexId = (this.LoadTexture "" 1 pbrWrapW pbrWrapH).id
            ) catch ()
        )
        
        layer
    ),
    
    fn LoadLayers mat =
    (
        local layers
        if ::Utils.IsWc3Material mat.maxObj or classOf mat.maxObj == FakeMaterial then 
            layers = #(mat.maxObj) else layers = (::Utils.ArrayFilter mat.maxObj.materialList IsWc3Material)
        local Wc3Layers = #()
        for l in layers do append Wc3Layers (this.LoadLayer l mat)
        
        -- Team Color + Texture: split back into 2 layers for correct WC3 export
        -- Only for v800 Classic — Reforged uses teamColorTexId binding slot instead
        if this.exportVersion != 1200 \
           and Wc3Layers.count == 1 and layers.count == 1 \
           and ::Utils.IsWc3Material layers[1] then
        (
            local l = layers[1]
            if l.retexture == 2 and l.texture != undefined then
            (
                -- Layer 0: Team Color base (replaceableId=1, empty path, filtermode=None)
                local tcLayer = Wc3Layer()
                tcLayer.filtermode = 1
                tcLayer.alpha = ParameterValue #Static 100.0
                tcLayer.color = undefined
                tcLayer.twosided = false
                tcLayer.unshaded = true
                tcLayer.unfogged = false
                tcLayer.nodephtest = false
                tcLayer.nodephset = false
                tcLayer.unselectable = false
                tcLayer.SphereEnvMap = false
                tcLayer.ConstantColor = false
                tcLayer.SortPrimsNearZ = false
                tcLayer.SortPrimsFarZ = false
                tcLayer.FullResolution = false
                tcLayer.PriorityPlane = 0
                tcLayer.Wrap_Height = false
                tcLayer.Wrap_Width = false
                tcLayer.coordId = 0
                tcLayer.tvertexid = undefined
                tcLayer.emissiveGain = undefined
                tcLayer.fresnelColor = undefined
                tcLayer.fresnelOpacity = undefined
                tcLayer.fresnelTeamColor = undefined
                tcLayer.normalTexId = undefined
                tcLayer.ormTexId = undefined
                tcLayer.emissiveTexId = undefined
                tcLayer.teamColorTexId = undefined
                tcLayer.envMapTexId = undefined
                local tcTex = this.LoadTexture "" 1 false false
                tcLayer.textures = ParameterValue #Static tcTex.id
                
                -- Layer 1: Texture overlay (replaceableId=0, filtermode=Transparent)
                local texLayer = Wc3Layers[1]
                texLayer.textures = this.LoadTextures l.texture l.path 0 l.Wrap_Width l.Wrap_Height
                if texLayer.filtermode == 1 then texLayer.filtermode = 2
                
                Wc3Layers = #(tcLayer, texLayer)
            )
        )
        
        Wc3Layers
    ),
    
    fn LoadMaterial mat =
    (
        local index = ::Utils.ArrayFindItemEx this.Wc3Materials mat this.ObjectChecker
        if index == 0 then
        (
            local result = Wc3Material ()
            result.maxObj = mat
            result.id = this.MaterialCount
            result.layers = this.LoadLayers result
            this.MaterialCount = this.MaterialCount + 1
            append this.Wc3Materials result
            result
        )
        else 
        (
            this.Wc3Materials[index]
        )
    ),
    
    fn LoadMaterials =
    (
        ::Progress.downLevel 10 1
        ::Progress.showItem "Loading Materials and TvertexAnimations"
        for geo in this.Meshes do (
            local m = geo.mesh
            local mat = m.material
            local tvertexAnim
            
            if mat != undefined and (::Utils.IsWc3Material mat or ::Utils.IsMultiLayered mat) then
            (
                local wc3Mat = this.LoadMaterial mat
                geo.material = wc3Mat
            )
            else
            (
                local wc3Mat
                if mat != undefined and classOf mat == Standardmaterial then
                    wc3Mat = this.LoadMaterial (FakeMaterial texture:mat.diffuseMap alpha:mat.opacity)
                else
                    wc3Mat = this.LoadMaterial this.DefaultMaterial
                geo.material = wc3Mat
                ::ErrorReporter.reportError "Material" geo.mesh.name undefined
            )
        )
        for r in this.Wc3RibbonEmitters do
        (
            local mat = r.maxObj.rmaterial
            if mat != undefined and (::Utils.IsWc3Material mat or ::Utils.IsMultiLayered mat) then
            (
                local wc3Mat = this.LoadMaterial mat
                r.sub.material = wc3Mat
            )
            else
            (
                local wc3Mat
                if mat != undefined and classOf mat == Standardmaterial then
                    wc3Mat = this.LoadMaterial (FakeMaterial texture:mat.diffuseMap alpha:mat.opacity)
                else
                    wc3Mat = this.LoadMaterial this.DefaultMaterial
                r.sub.material = wc3Mat
            )
        )
        for p in this.Wc3PartEmitters2 do
        (
            local obj = p.maxObj
            -- rtexture: 1=Normal, 2=TeamColor, 3=TeamGlow
            -- TEXS replaceableId: 0=Normal, 1=TeamColor, 2=TeamGlow
            local texReplaceable = obj.rtexture - 1
            if (obj.texture != undefined and obj.texture != "") then
                p.sub.texture = this.LoadTexture (obj.path + (this.GetImageFile obj.texture)) texReplaceable false false
            else if (texReplaceable > 0) then
                -- Team Color/Glow ohne Textur
                p.sub.texture = this.LoadTexture "" texReplaceable false false
            else
                p.sub.texture = this.LoadTexture "Textures\white.blp" 0 false false
        )
        this.TvertexAnimCount = 0
        for tv in this.Wc3TvertexAnims do
        (
            tv.id = this.TvertexAnimCount
            this.TvertexAnimCount += 1
        )
        ::Progress.upLevel ()
    ),
    
    -- ============================================================================
    -- GEOSET ANIMATION LOADING
    -- ============================================================================
    
    fn LoadGeosetAnims =
    (
        ::Progress.downLevel 10 1
        ::Progress.showItem "Loading Geoset Animations"
        
        for geo in this.Meshes do 
        (
            if (this.IsGeosetAnimated geo) then
            (
                local m = geo.mesh
                local geoAnim = Wc3GeosetAnim ()
                geoAnim.id = this.GeosetAnimCount
                this.GeosetAnimCount = this.GeosetAnimCount + 1
                geoAnim.geoset = geo
                
                if geo.visObj != undefined then 
                    geoAnim.alpha = ::AnimationManager.VisibilityParameterProcess geo.visObj
                
                local vc = m.modifiers[#Wc3_Vertex_Color]
                if (vc != undefined) then 
                (
                    geoAnim.dropShadow = vc.UsesDropShadow
                    geoAnim.usesColor = vc.UsesColor
                    geoAnim.color = ::AnimationManager.ColorParameterProcess vc.VertexColor.controller vc.VertexColor
                )
                
                append this.GeosetAnims geoAnim
                geo.geosetAnimId = geoAnim.id
            )
        )
        ::Progress.upLevel ()
    ),
    
    -- ============================================================================
    -- CLEANUP & RESET
    -- ============================================================================
    
    fn CleanUpSession =
    (
        this.Wc3Extents = Wc3Extent ()
        this.Wc3GlobalSequences = #() 
        this.Wc3Textures = #()
        this.Wc3Materials = #()
        this.Wc3TvertexAnims = #()
        this.Wc3Sequences = #()
        this.Meshes = #()
        this.BonesMeshRef = #()
        this.GeosetAnims = #()
        this.Wc3Objects = #() 
        this.Wc3Bones = #()
        this.Wc3Lights = #()
        this.Wc3Helpers = #()
        this.Wc3Attachments = #()
        this.Wc3PartEmitters1 = #()
        this.Wc3PartEmitters2 = #()
        this.Wc3RibbonEmitters = #()
        this.Wc3Events = #()
        this.Wc3Cameras = #()
        this.Wc3Collisions = #()
        this.Wc3CornEmitters = #()
        this.Wc3FaceFXTargets = #()
        
        this.GeosetCounts = 0
        this.BoneCounts = 0
        this.ObjectIdCount = 0
        this.MaterialCount = 0
        this.TextureCount = 0
        this.TvertexAnimCount = 0
        this.GeosetAnimCount = 0
    ),
    
    -- Reforged v1200: Prueft ob Alias-Sequenzen noch identisch mit Master sind.
    -- Vergleicht Transform- und Visibility-Daten in beiden Ranges.
    -- Wenn Unterschiede gefunden → sharedGroup wird aufgeloest, Sequenz behaelt eigene Range.
    -- Wird einmal beim Export aufgerufen, kostet <100ms.
    fn validateSharedSequences sequences =
    (
        -- Master-Sequenzen sammeln
        local masterNames = #()
        local masterSeqs = #()
        for seq in sequences where seq.sharedGroup != undefined and seq.name == seq.sharedGroup do
        (
            append masterNames seq.sharedGroup
            append masterSeqs seq
        )
        if masterNames.count == 0 then return()

        -- Nur animierte Objekte sammeln (statische ueberspringen → Performance)
        local animObjs = #()
        for obj in objects do
        (
            local hasAnim = false
            try
            (
                local tc = obj.transform.controller
                if tc != undefined and isProperty tc #keys and tc.keys != undefined and tc.keys.count > 0 then
                    hasAnim = true
            ) catch()
            if not hasAnim then
                try ( if obj.visibility.isAnimated then hasAnim = true ) catch()
            if hasAnim then append animObjs obj
        )
        if animObjs.count == 0 then return()

        -- Jede Alias-Sequenz gegen ihren Master vergleichen
        for seq in sequences where seq.sharedGroup != undefined and seq.name != seq.sharedGroup do
        (
            local mi = findItem masterNames seq.sharedGroup
            if mi == 0 then continue
            local master = masterSeqs[mi]

            local masterS = master.startTime
            local masterE = master.endTime
            local aliasS = seq.startTime
            local aliasE = seq.endTime
            local dur = masterE - masterS

            -- Duration muss identisch sein
            if abs ((aliasE - aliasS) - dur) > 1f then
            (
                format "Reforged: Shared '%' has different duration than master '%', breaking link\n" seq.name seq.sharedGroup to:listener
                seq.sharedGroup = undefined
                continue
            )

            local isDiff = false

            for obj in animObjs while not isDiff do
            (
                -- Transform vergleichen: 11 Samples (Start, Ende, 9 Zwischenpunkte)
                try
                (
                    for s = 0 to 10 while not isDiff do
                    (
                        local ratio = s / 10.0
                        local mTime = masterS + dur * ratio
                        local aTime = aliasS + dur * ratio

                        local mPos = at time mTime obj.transform.pos
                        local aPos = at time aTime obj.transform.pos
                        if distance mPos aPos > 0.01 then isDiff = true

                        if not isDiff then
                        (
                            local mq = at time mTime obj.transform.rotationpart
                            local aq = at time aTime obj.transform.rotationpart
                            local dot = abs (mq.x*aq.x + mq.y*aq.y + mq.z*aq.z + mq.w*aq.w)
                            if dot < 0.9999 then isDiff = true
                        )

                        if not isDiff then
                        (
                            local mSc = at time mTime obj.transform.scalepart
                            local aSc = at time aTime obj.transform.scalepart
                            if distance mSc aSc > 0.001 then isDiff = true
                        )
                    )
                ) catch()

                -- Visibility vergleichen: an allen Key-Zeitpunkten beider Ranges
                if not isDiff then
                (
                    try
                    (
                        if obj.visibility.isAnimated then
                        (
                            local vc = obj.visibility.controller
                            if vc != undefined and isProperty vc #keys and vc.keys != undefined then
                            (
                                local relTimes = #()
                                for k in vc.keys do
                                (
                                    if k.time >= masterS and k.time <= masterE then
                                        appendIfUnique relTimes (k.time - masterS)
                                    if k.time >= aliasS and k.time <= aliasE then
                                        appendIfUnique relTimes (k.time - aliasS)
                                )
                                appendIfUnique relTimes 0f
                                appendIfUnique relTimes dur
                                sort relTimes

                                for rt in relTimes while not isDiff do
                                (
                                    local mv = (at time (masterS + rt) obj.visibility) as float
                                    local av = (at time (aliasS + rt) obj.visibility) as float
                                    if abs (mv - av) > 0.001 then isDiff = true
                                )
                            )
                        )
                    ) catch()
                )
            )

            if isDiff then
            (
                format "Reforged: Shared '%' has modified keyframes, keeping own range\n" seq.name seq.sharedGroup to:listener
                seq.sharedGroup = undefined
            )
        )
    ),

    -- ============================================================================
    -- MAIN PARSE FUNCTION
    -- ============================================================================
    
    fn ParseScene mergeGeosets:true extentsCalculation:1 extentsPrecision:5 ATFixNormals:true ATThresshold:0.1 faceGroups:false keepUnusedBonesHelpers:false fixSharedNormals:true exportVersion:800 =
    (
        this.initDefaults()
        this.CleanUpSession ()
        this.LoadSequences ()
        
        this.keepUnusedBonesHelpers = keepUnusedBonesHelpers
        this.exportVersion = exportVersion
        
        if this.Wc3Sequences.count == 0 then
        (
            print "Critical: No sequences loaded, adding default sequence"
            this.Wc3Sequences[1] = this.DefaultAnimation
        )
        
        -- Reforged v1200: Shared Sequences validieren und kollabieren
        local animSequences = this.Wc3Sequences
        if exportVersion == 1200 then
        (
            this.validateSharedSequences this.Wc3Sequences

            local masterRanges = #()
            local masterNames = #()
            for seq in this.Wc3Sequences where seq.sharedGroup != undefined do
            (
                if seq.name == seq.sharedGroup then
                (
                    append masterNames seq.sharedGroup
                    append masterRanges #(seq.startTime, seq.endTime)
                )
            )
            for seq in this.Wc3Sequences where seq.sharedGroup != undefined do
            (
                if seq.name != seq.sharedGroup then
                (
                    local idx = findItem masterNames seq.sharedGroup
                    if idx > 0 then
                    (
                        seq.startTime = masterRanges[idx][1]
                        seq.endTime = masterRanges[idx][2]
                    )
                )
            )
            
            -- Aliase aus Animation-Pipeline ausschliessen
            -- Keys existieren nur in der Master-Range, Aliase wuerden Duplikate erzeugen
            animSequences = for seq in this.Wc3Sequences \
                where (seq.sharedGroup == undefined or seq.name == seq.sharedGroup) collect seq
        )
        
        this.exportSmoothgroups = faceGroups
        ::AnimationManager.InitiateAnimationData (::Utils.ArrayMap animSequences (fn tmp a = interval a.startTime a.endTime)) animSequences
        this.LoadObjects ()
        this.LoadExtents extentsCalculation:extentsCalculation extentsPrecision:(11 - extentsPrecision)
        this.DoGeosetTweaks mergeGeosets:mergeGeosets ATFixNormals:ATFixNormals ATThresshold:ATThresshold fixSharedNormals:fixSharedNormals
        this.LoadMaterials ()
        this.LoadGeosetAnims ()
        this.Wc3GlobalSequences = ::AnimationManager.GetGlobalSequences()
        
        local model = Wc3Model ()
        model.version = exportVersion
        model.name = "Just Another Model"
        model.extents = this.Wc3Extents
        model.globalSequences = this.Wc3GlobalSequences
        model.sequences = this.Wc3Sequences
        model.textures = this.Wc3Textures
        model.materials = this.Wc3Materials
        model.tvertexAnims = this.Wc3TvertexAnims
        model.geosets = this.Meshes
        model.geosetAnims = this.GeosetAnims
        model.bones = this.Wc3Bones
        model.lights = this.Wc3Lights
        model.helpers = this.Wc3Helpers
        model.attachments = this.Wc3Attachments
        model.particleEmitters1 = this.Wc3PartEmitters1
        model.particleEmitters2 = this.Wc3PartEmitters2
        model.ribbonEmitters = this.Wc3RibbonEmitters
        model.events = this.Wc3Events
        model.cameras = this.Wc3Cameras
        model.CollisionShapes = this.Wc3Collisions
        
        -- Reforged: CORN, FAFX, BPOS
        if exportVersion == 1200 then
        (
            model.cornEmitters = this.Wc3CornEmitters
            model.faceFxTargets = this.LoadFaceFXFromScene()
            
            -- BPOS: Original Bind Poses wiederverwenden wenn vorhanden,
            -- sonst aus Bone-Transformation bei Frame 0 lesen
            local allNodes = #()
            join allNodes model.bones
            join allNodes model.lights
            join allNodes model.helpers
            join allNodes model.attachments
            join allNodes model.particleEmitters1
            join allNodes model.particleEmitters2
            join allNodes model.ribbonEmitters
            join allNodes model.events
            join allNodes model.CollisionShapes
            if model.cornEmitters != undefined then join allNodes model.cornEmitters
            
            local fafxCount = if model.faceFxTargets != undefined then model.faceFxTargets.count else 0
            local bposes = #()
            bposes[allNodes.count + fafxCount] = undefined
            
            -- Versuche Original Bind Poses per Name nachzuschlagen
            local hasOrigBP = (::WhiteoutDexOrigBindPoses != undefined and classOf ::WhiteoutDexOrigBindPoses == Dictionary)
            
            at time 0
            (
                for i = 1 to allNodes.count do
                (
                    local node = allNodes[i]
                    local found = false
                    if hasOrigBP and node != undefined and node.name != undefined then
                    (
                        try
                        (
                            if hasDictValue ::WhiteoutDexOrigBindPoses node.name then
                            (
                                bposes[i] = ::WhiteoutDexOrigBindPoses[node.name]
                                found = true
                            )
                        ) catch ()
                    )
                    if not found then
                    (
                        if node != undefined and node.maxObj != undefined then
                            bposes[i] = node.maxObj.transform
                        else
                            bposes[i] = matrix3 1
                    )
                )
            )
            -- FaceFX: Original oder Identity Bind Pose anhaengen
            for i = 1 to fafxCount do
            (
                local fxKey = "FAFX_" + (try (model.faceFxTargets[i].name) catch "")
                if hasOrigBP and hasDictValue ::WhiteoutDexOrigBindPoses fxKey then
                    bposes[allNodes.count + i] = ::WhiteoutDexOrigBindPoses[fxKey]
                else
                    bposes[allNodes.count + i] = matrix3 1
            )
            model.bindPoses = bposes
        )
        
        model
    )
)

-- ============================================================================
-- GLOBAL INSTANCE (for backward compatibility)
-- ============================================================================
global SceneParser = WhiteoutDexSceneParserStruct()
