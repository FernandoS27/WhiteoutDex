/*
    GeometryUtilities.ms
    
    Refactored as Struct for better encapsulation and maintainability.
    Contains geometry utility functions for mesh operations, UV handling,
    smooth group processing, and face element detection.
    
    No state variables - pure functions grouped in a struct.
    
    Usage:
        ::GeoUtils.getFaces myMesh
        ::GeoUtils.breakUnwrap myObj
        ::GeoUtils.gatherSmoothGroupData myObj
        ::GeoUtils.getFaceElements myFaces
*/

-- ============================================================================
-- MAIN GEOMETRY UTILITIES STRUCT
-- ============================================================================

struct GeometryUtilitiesStruct
(
    -- ============================================================================
    -- FACE/VERTEX RETRIEVAL
    -- ============================================================================
    
    -- Get texture vertex faces for a specific map channel
    fn getTVFaces m map =
    (
        local faces = #()
        local num = getNumFaces m
        if num != 0 then faces[num] = undefined
        for i = 1 to num do faces[i] = meshop.getMapFace m map i
        faces
    ),
    
    -- Get texture vertex faces using legacy getTVFace (map channel 1)
    fn getTVFacesOld m =
    (
        local faces = #()
        local num = getNumFaces m
        if num != 0 then faces[num] = undefined
        for i = 1 to num do faces[i] = getTVFace m i
        faces
    ),
    
    -- Get geometric faces as array of Point3 (vertex indices)
    fn getFaces m =
    (
        local faces = #()
        local num = getNumFaces m
        if num != 0 then faces[num] = undefined
        for i = 1 to num do faces[i] = getFace m i
        faces
    ),
    
    -- ============================================================================
    -- SMOOTH GROUP OPERATIONS
    -- ============================================================================
    
    -- Split a face set into groups by connected smooth groups
    -- Returns array of face-index arrays, one per smooth group cluster
    fn splitBySmoothGroup obj faces =
    (
        local result = #()
        local tmp = #()
        for f in faces do
        (
            local sg = getFaceSmoothGroup obj f
            local index = #()
            for i = 1 to tmp.count where (bit.and tmp[i] sg) != 0 do append index i
            if index.count != 0 then
            (
                local first = index[1]
                for i = index.count to 2 by -1 do
                (
                    local j = index[i]
                    join result[first] result[j]
                    tmp[first] = bit.or tmp[first] tmp[j]
                    deleteItem tmp j
                    deleteItem result j
                )
                tmp[first] = bit.or tmp[first] sg
                append result[first] f
            )
            else
            (
                append tmp sg
                append result #(f)
            )
        )
        result
    ),
    
    -- Gather smooth group data: split vertices by smooth groups, compute normals
    -- Returns: #(faces, normals, vertexMapper)
    fn gatherSmoothGroupData obj =
    (
        local numVerts = getNumVerts obj
        local baseFaces = this.getFaces obj
        local vertexMapper = #()
        local normals = #()
        vertexMapper[numVerts] = undefined
        normals[numVerts] = undefined
        local faces = this.getFaces obj
        local currentVert = 0
        local faceNormals = #()
        faceNormals[faces.count] = undefined
        local fnGetNormals = meshop.getFaceRNormals
        for f = 1 to faces.count do
            faceNormals[f] = fnGetNormals obj f
        local fnSplit = this.splitBySmoothGroup
        local fnGetFacesUsing = meshop.getFacesUsingVert
        for i = 1 to numVerts do
        (
            local g = fnSplit obj (fnGetFacesUsing obj #{i})
            for a in g do
            (
                local k = 1
                for j = 1 to 3 where baseFaces[a[1]][j] == i do k = j
                currentVert += 1
                vertexMapper[currentVert] = i
                normals[currentVert] = faceNormals[a[1]][k]
                for f in a do
                (
                    k = 1
                    for j = 1 to 3 where baseFaces[f][j] == i do k = j
                    faces[f][k] = currentVert
                )
            )
        )
        -- Trimmen statt rückwärts deleteItem
        vertexMapper.count = currentVert
        normals.count = currentVert
        #(faces, normals, vertexMapper)
    ),
    
    -- ============================================================================
    -- UV UNWRAP OPERATIONS
    -- ============================================================================
    
    -- Fast unwrap break for single UV channel (channel 1)
    -- Returns: #(vertices, tvfaces, indexMap)
    fn breakUnwrapFast obj =
    (
        local vertices = #()
        local texNum = getNumTVerts obj
        vertices[texNum] = undefined
        local faces = this.getFaces obj
        local tvfaces = this.getTVFacesOld obj
        local tmp = #()
        local reverseTmp = #()
        reverseTmp[texNum] = undefined
        local count = faces.count
        for i = 1 to count do
        (
            local tvf = tvfaces[i]
            local f = faces[i]
            vertices[tvf[1]] = f[1]
            vertices[tvf[2]] = f[2]
            vertices[tvf[3]] = f[3]
        )
        for f in tvfaces do
            for i = 1 to 3 do
            (
                local tvIdx = f[i]
                local index = if tvIdx <= reverseTmp.count and reverseTmp[tvIdx] != undefined then reverseTmp[tvIdx] else 0
                if index != 0 then f[i] = index else
                (
                    append tmp tvIdx
                    reverseTmp[tvIdx] = tmp.count
                    f[i] = tmp.count
                )
            )
        #(vertices, tvfaces, tmp)
    ),
    
    -- Break unwrap for a specific map channel
    -- Returns: #(vertices, tvfaces, indexMap, tvertices)
    fn breakUnwrapMap obj faces map =
    (
        local vertices = #()
        local texNum = meshop.getNumMapVerts obj map
        vertices[texNum] = undefined
        local tvfaces = this.getTVFaces obj map
        local tmp = #()
        local count = faces.count
        local extraVertices = #()
        local indexer = #()
        local reverseIndexer = #()
        reverseIndexer[texNum] = undefined
        for i = 1 to count do
        (
            local tvf = tvfaces[i]
            local f = faces[i]
            for j = 1 to 3 do
            (
                if vertices[tvf[j]] == undefined then
                    vertices[tvf[j]] = f[j]
                else
                    if vertices[tvf[j]] != f[j] then
                    (
                        local tvIdx = tvf[j]
                        local index = if tvIdx <= reverseIndexer.count and reverseIndexer[tvIdx] != undefined then reverseIndexer[tvIdx] else 0
                        if index != 0 then
                            tvf[j] = texNum + index
                        else
                        (
                            append indexer tvIdx
                            reverseIndexer[tvIdx] = indexer.count
                            tvf[j] = indexer.count + texNum
                            append extraVertices f[j]
                        )
                    )
            )
        )
        join vertices extraVertices
        local maxTvIdx = texNum + indexer.count
        local reverseTmp = #()
        reverseTmp[maxTvIdx] = undefined
        for f in tvfaces do
            for i = 1 to 3 do
            (
                local tvIdx = f[i]
                local index = if tvIdx <= reverseTmp.count and reverseTmp[tvIdx] != undefined then reverseTmp[tvIdx] else 0
                if index != 0 then f[i] = index else
                (
                    append tmp tvIdx
                    reverseTmp[tvIdx] = tmp.count
                    f[i] = tmp.count
                )
            )
        local tvertices = #()
        tvertices[tmp.count] = undefined
        local fnGetMapVert = meshop.getMapVert
        for i = 1 to tmp.count do
        (
            local index = tmp[i]
            if index > texNum then index = indexer[index-texNum]
            tvertices[i] = fnGetMapVert obj map index
        )
        #(vertices, tvfaces, tmp, tvertices)
    ),
    
    -- Full unwrap break across all supported map channels
    -- Returns: #(faces, tverticesPerChannel, vertexMapper)
    fn breakUnwrap obj faces:undefined numVerts:undefined =
    (
        fn compareMaps a b = if (a[2] < b[2]) then -1 else 1
        
        if faces == undefined then
            faces = this.getFaces obj
        if numVerts == undefined then numVerts = getNumVerts obj
        
        local oldMapper = #()
        oldMapper[numVerts] = undefined
        for i = 1 to numVerts do oldMapper[i] = i
        local Maps = #()
        local tmp = #()
        
        for i = 1 to (meshop.getNumMaps obj) do
            if (meshop.getMapSupport obj i) then
                append tmp #(i, (meshop.getNumMapVerts obj i))
        
        qsort tmp compareMaps
        Maps[tmp.count] = undefined
        for i = 1 to tmp.count do Maps[i] = tmp[i][1]
        local oldTVertices = #()
        
        for map in Maps do
        (
            local tmp = this.breakUnwrapMap obj faces map
            faces = tmp[2]
            local transformer = tmp[1]
            local vertexIndices = tmp[3]
            local count = vertexIndices.count
            local newMapper = #()
            local currentMaps = oldTVertices.count
            local newTVertices = #()
            local maptvertices = #()
            local tverts = tmp[4]
            if (currentMaps != 0) then
                newTVertices = ::Utils.ArrayRepeat currentMaps #()
            for i = 1 to oldTVertices.count do
                newTVertices[i][count] = undefined
            newMapper[count] = undefined
            for i = 1 to count do
            (
                local v = transformer[vertexIndices[i]]
                newMapper[i] = oldMapper[v]
                for j = 1 to currentMaps do
                    newTVertices[j][i] = oldTVertices[j][v]
            )
            oldMapper = newMapper
            append newTVertices tverts
            oldTVertices = newTVertices
        )
        
        for i = 1 to Maps.count do
            Maps[i] = #(oldTVertices[i], Maps[i])
        qsort Maps compareMaps
        for i = 1 to Maps.count do
            oldTVertices[i] = Maps[i][1]
        
        #(faces, oldTVertices, oldMapper)
    ),
    
    -- ============================================================================
    -- VERTEX COLOR
    -- ============================================================================
    
    -- Set all vertex colors on an object to a uniform color
    fn ColorVertices obj newColor =
    (
        defaultVCFaces obj
        local vertNum = getNumCPVVerts obj
        for i = 1 to vertNum do setVertColor obj i newColor
    ),
    
    -- ============================================================================
    -- FACE ELEMENT DETECTION
    -- ============================================================================
    
    -- Find connected face elements (groups of faces sharing vertices)
    -- Returns array of bitarrays, each representing one connected element
    fn getFaceElements faces =
    (
        fn faceToBitArray f = #{f[1], f[2], f[3]}
        
        local mapper = #{}
        mapper.count = faces.count
        mapper = -mapper
        mapper[mapper.count] = false
        local bitFaces = #()
        bitFaces[faces.count] = undefined
        for i = 1 to faces.count do
            bitFaces[i] = faceToBitArray faces[i]
        local result = #(bitFaces[mapper.count])
        local result2 = #(#{mapper.count})
        
        while (not mapper.isEmpty) do
        (
            local grouped = false
            for i = 1 to result.count do
            (
                local found = false
                local elem = result[i]
                for b in mapper do
                (
                    local tmp = elem * bitFaces[b]
                    if (not tmp.isEmpty) then
                    (
                        elem = elem + bitFaces[b]
                        mapper[b] = false
                        result2[i][b] = true
                        found = true
                    )
                )
                result[i] = elem
                grouped = grouped or found
            )
            if ((not grouped) and (not mapper.isEmpty)) then
            (
                local newFace
                local i = mapper.count
                while (i != 0) do
                    if mapper[i] then
                    (
                        newFace = bitFaces[i]
                        mapper[i] = false
                        append result newFace
                        append result2 #{i}
                        i = 0
                    ) else i = i - 1
            )
        )
        result2
    )
)

-- ============================================================================
-- GLOBAL INSTANCE
-- ============================================================================

global GeoUtils = GeometryUtilitiesStruct()

-- ============================================================================
-- BACKWARD COMPATIBILITY: Global wrapper functions
-- REQUIRED: Used by WhiteoutDexSceneParser.ms as plain function calls.
-- Do NOT remove these without updating all callers first.
-- ============================================================================

fn getFaces m = ::GeoUtils.getFaces m
fn gatherSmoothGroupData obj = ::GeoUtils.gatherSmoothGroupData obj
fn breakUnwrapFast obj = ::GeoUtils.breakUnwrapFast obj
fn breakUnwrap obj faces:undefined numVerts:undefined = ::GeoUtils.breakUnwrap obj faces:faces numVerts:numVerts
