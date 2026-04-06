/*
    WhiteoutDexOptimize.ms
    
    Refactored as Struct for better encapsulation and maintainability.
    This file contains optimization functions for model data.
    No state variables - pure functions grouped in a struct.
    
    Usage:
        global Optimizer = WhiteoutDexOptimizeStruct()
        Optimizer.optimizeImportModel model true true
        Optimizer.optimizeExportModel model
*/

-- ============================================================================
-- MAIN OPTIMIZE STRUCT
-- ============================================================================

struct WhiteoutDexOptimizeStruct
(
    -- ============================================================================
    -- COMPARISON UTILITY FUNCTIONS
    -- ============================================================================
    
    fn compValues a b t = (abs (a-b)) < t,
    fn compPoint a b t = (distance a b) < t,
    
    fn areAnimDataEqual ad ad2 =
    (
        if (ad != undefined and ad2 != undefined and ad.type == ad2.type) then
        (
            local b1 = ad.globalSequenceId != undefined
            local b2 = ad2.globalSequenceId != undefined
            if (b1 == b2) then
            (
                local k1 = ad.keys
                local k2 = ad2.keys
                if (k1.count == k2.count) then
                (
                    local ism = ad.type == #Bezier or ad.type == #Hermite
                    local n = k1.count
                    local result = true
                    local t = (classof k1[1].value)
                    local isEqualValue = if (t == point2 or t == point3) then this.compPoint else this.compValues
                    for i = 1 to n where result do
                    (
                        result = result and (k1[i].time == k2[i].time)
                        result = result and isEqualValue k1[i].value k2[i].value 0.00001
                        if ism then
                        (
                            result = result and isEqualValue k1[i].intan k2[i].intan 0.00001
                            result = result and isEqualValue k1[i].outtan k2[i].outtan 0.00001
                        )
                    )
                    result
                )
                else false
            ) else false
        ) else (ad == ad2)
    ),
    
    -- ============================================================================
    -- TVERTEX ANIMATION OPTIMIZATION
    -- ============================================================================
    
    fn optimizeTvertexAnimations tv mats =
    (
        fn isTvertexAnimEqual tv1 tv2 =
        (
            local trans1 = tv1.transformation
            local trans2 = tv2.transformation
            local result = true
            result = result and (this.areAnimDataEqual trans1.translation trans2.translation)
            result = result and (this.areAnimDataEqual trans1.rotation trans2.rotation)
            result = result and (this.areAnimDataEqual trans1.scale trans2.scale)
            result
        )
        for m in mats do
            for l in m.layers do
                if l.tvertexid != undefined then 
                    l.tvertexid = l.tvertexid.id + 1
        local n = tv.count
        for i = 1 to n do
            for j = i+1 to n do
                if (isTvertexAnimEqual tv[i] tv[j]) then tv[j] = tv[i]
        for m in mats do
            for l in m.layers do
                if l.tvertexid != undefined then 
                    l.tvertexid = tv[l.tvertexid]
        ::Utils.OptimizeArray tv
        for i = 1 to tv.count do tv[i].id = i - 1
    ),
    
    -- ============================================================================
    -- KEY OPTIMIZATION
    -- ============================================================================
    
    fn optimizeKeys keys sequences defaultVal:unsupplied =
    (
        if keys == undefined or keys.count == 0 then
            return #()
        
        if sequences == undefined or sequences.count == 0 then
            return keys
        
        for k in keys where k == undefined do
            return keys
        
        -- Hermite/Bezier: aggressive Optimierung (wie bisherige neue Version)
        local isSmooth = (isProperty keys[1] #intan) and (isProperty keys[1] #outtan) \
            and keys[1].intan != undefined and keys[1].outtan != undefined
        
        if isSmooth then
        (
            local sepKeys = ::Utils.getKeysOnSequences keys sequences inclusive:true
            local keysNew = #()
            if defaultVal == unsupplied then
                defaultVal = keys[1].value - keys[1].value
            for s in sepKeys do
            (
                if (s.count == 1) then
                (
                    if s[1].value != defaultVal then
                        append keysNew s[1]
                )
                else if (s.count == 2) then
                (
                    local b = s[1].value != defaultVal
                    if s[2].value != s[1].value then
                    (
                        append keysNew s[1]
                        append keysNew s[2]
                    )
                    else
                        if b then append keysNew s[1]
                )
                else
                    join keysNew s
            )
            return keysNew
        )
        
        -- Linear/NonInterp: konservative Optimierung (alte Version)
        local sepKeys = ::Utils.getKeysOnSequences keys sequences inclusive:true
        
        local validSepKeys = #()
        for s in sepKeys do
        (
            if s != undefined and s.count > 0 then
            (
                local allValid = true
                for k in s where k == undefined do
                    allValid = false
                
                if allValid then
                    append validSepKeys s
            )
        )
        
        if validSepKeys.count == 0 then
            return keys
        
        local keysNew = #()
        
        local hasFrame0Key = false
        if keys.count > 0 and keys[1].time == 0f then
        (
            append keysNew keys[1]
            hasFrame0Key = true
        )
        
        if defaultVal == unsupplied then
        (
            if keys.count > 0 and keys[1] != undefined then
                defaultVal = keys[1].value - keys[1].value
            else
                defaultVal = 0
        )
        
        for s in validSepKeys do
        (
            if (s.count == 1) then
            (
                if s[1] != undefined then
                (
                    if s[1].value != defaultVal or s[1].time == 0f then
                        if not (hasFrame0Key and s[1].time == 0f) then
                            append keysNew s[1]
                )
            )
            else if (s.count == 2) then
            (
                if s[1] != undefined and s[2] != undefined then
                (
                    local b = s[1].value != defaultVal
                    
                    if s[2].value != s[1].value then
                    (
                        if not (hasFrame0Key and s[1].time == 0f) then
                            append keysNew s[1]
                        append keysNew s[2]
                    )
                    else
                    (
                        if b then
                            if not (hasFrame0Key and s[1].time == 0f) then
                                append keysNew s[1]
                        append keysNew s[2]
                    )
                )
            )
            else if s.count > 2 then
            (
                local allValid = true
                for k in s where k == undefined do
                    allValid = false
                
                if allValid then
                (
                    if not (hasFrame0Key and s[1].time == 0f) then
                        append keysNew s[1]
                    
                    for i = 2 to (s.count - 1) do
                    (
                        if s[i] != undefined then
                        (
                            if s[i].value != s[i-1].value or s[i].value != s[i+1].value then
                                append keysNew s[i]
                        )
                    )
                    
                    if s[s.count] != undefined then
                        append keysNew s[s.count]
                )
            )
        )
        
        local finalKeys = #()
        local lastTime = -1f
        for k in keysNew where k != undefined do
        (
            if k.time != lastTime then
            (
                append finalKeys k
                lastTime = k.time
            )
        )
        
        if finalKeys.count == 0 and keys.count > 0 then
            return keys
        
        finalKeys
    ),
    

    -- ============================================================================
    -- OBJECT ANIMATION OPTIMIZATION
    -- ============================================================================
    
    fn optimizeObjectAnimations o sequences param:false =
    (
        local trans = o.transformations
        
        if trans != undefined then
        (
            local t = trans.translation
            local r = trans.rotation
            local s = trans.scale
            
            local skipRotOpt = false
            try
            (
                if ::HermiteEditedSequences != undefined and \
                hasDictValue ::HermiteEditedSequences o.name then
                (
                    local editedSeqs = ::HermiteEditedSequences[o.name]
                    if editedSeqs.count == 0 then
                        skipRotOpt = true
                )
            )
            catch()
            
            if r != undefined and r.keys != undefined and r.globalSequenceId == undefined and (not skipRotOpt) then
                r.keys = this.optimizeKeys r.keys sequences
                
            if t != undefined and t.keys != undefined and t.globalSequenceId == undefined then
                t.keys = this.optimizeKeys t.keys sequences
                
            if s != undefined and s.keys != undefined and s.globalSequenceId == undefined then
                s.keys = this.optimizeKeys s.keys sequences defaultVal:[1,1,1]
        )
        
        if param then
        (
            local sub = o.sub
            if sub == undefined then
                return undefined
                
            -- ... rest bleibt gleich
        )
    ),
    
    fn optimizeAnimations model =
    (
        -- Fuer v1200: Alias-Sequenzen ausschliessen (teilen Keys mit Master)
        local seqList = model.sequences
        if model.version == 1200 then
            seqList = for s in model.sequences \
                where (s.sharedGroup == undefined or s.name == s.sharedGroup) collect s
        
        local sequences = ::Utils.ArrayMap seqList (fn tmp s = (interval s.startTime s.endTime))
        for o in model.bones do
            this.optimizeObjectAnimations o sequences
        for o in model.helpers do
            this.optimizeObjectAnimations o sequences
        for o in model.lights do
            this.optimizeObjectAnimations o sequences param:true
        for o in model.attachments do
            this.optimizeObjectAnimations o sequences param:true
        for o in model.particleEmitters1 do
            this.optimizeObjectAnimations o sequences param:true
        for o in model.particleEmitters2 do
            this.optimizeObjectAnimations o sequences param:true
        for o in model.ribbonEmitters do
            this.optimizeObjectAnimations o sequences param:true
    ),
    
    fn optimizeExportModel model =
    (
        this.optimizeTvertexAnimations model.tvertexAnims model.materials
        this.optimizeAnimations model
    ),
    
    -- ============================================================================
    -- GEOMETRY UTILITY FUNCTIONS
    -- ============================================================================
    
    fn extractMinMax3D mm p =
    (
        local minimum = mm[1]
        local maximum = mm[2]
        if minimum.x > p.x then minimum.x = p.x
        if minimum.y > p.y then minimum.y = p.y
        if minimum.z > p.z then minimum.z = p.z
        if maximum.x < p.x then maximum.x = p.x
        if maximum.y < p.y then maximum.y = p.y
        if maximum.z < p.z then maximum.z = p.z
        mm
    ),
    
    -- ============================================================================
    -- VERTEX OPTIMIZATION
    -- ============================================================================
    
    fn optimizeVertices v minimum:unsupplied maximum:unsupplied =
    (
        local result = #()
        local n = v.count
        result[n] = n
        for i = 1 to n do result[i] = i
        local coff = n*(n-1)/2
        local thresshold
        local compFunc
        if classof v[1] == point3 then
        (
            compFunc = distance
            thresshold = 0.001
        )
        else
        (
            compFunc = distance
            thresshold = 0.001
        )
        local Blocks
        if coff < n*10 then
            Blocks = #((copy result #nomap))
        else
        (
            local maxLevel = ((log10 n)*8) as integer
            local maxSize = ((log n)*3) as integer
            local constructor
            if classof v[1] == point3 then
            (
                if minimum == unsupplied then
                (
                    local tmp = #(copy v[1], copy v[1])
                    tmp = ::Utils.ArrayFold v this.extractMinMax3D tmp
                    minimum = tmp[1]
                    maximum = tmp[2]
                )
                constructor = Octree
            )
            else constructor = Quadtree
            local o = constructor v minimum maximum maxSize:maxSize maxLevel:maxLevel
            for i = 1 to v.count do
                o.insertPoint i
            Blocks = o.getBlocks ()
        )
        for block in Blocks do
        (
            n = block.count
            for i = 1 to n do
            (
                local k = block[i]
                for j = i + 1 to n where result[k] == k do
                    if ((compFunc v[k] v[block[j]]) < thresshold) then
                        result[block[j]] = k
            )
        )
        result
    ),
    
    fn optimizeTVertices v origV minimum:unsupplied maximum:unsupplied =
    (
        local result = #()
        local n = v.count
        result[n] = n
        for i = 1 to n do result[i] = i
        
        local coff = n*(n-1)/2
        
        local thresshold
        local compFunc
        compFunc = distance
        thresshold = 0.001
        
        local Blocks
        if coff < n*10 then
            Blocks = #((copy result #nomap))
        else
        (
            local maxLevel = ((log10 n)*8) as integer
            local maxSize = ((log n)*3) as integer
            local constructor = Quadtree
            local o = constructor v minimum maximum maxSize:maxSize maxLevel:maxLevel
            for i = 1 to v.count do
                o.insertPoint i
            Blocks = o.getBlocks ()
        )
        
        for block in Blocks do
        (
            n = block.count
            for i = 1 to n do
            (
                local k = block[i]
                for j = i + 1 to n where result[k] == k do
                    if ((compFunc v[k] v[block[j]]) < thresshold and origV[k] == origV[block[j]]) then
                        result[block[j]] = k
            )
        )
        result
    ),
    
    fn fixVertexindices vt =
    (
        local n = 1
        local r = #()
        r[vt.count] = undefined
        for i = 1 to vt.count do
        (
            if (vt[i] == i) then
            (
                r[vt[i]] = n
                vt[i] = n
                n = n + 1
            ) else vt[i] = r[vt[i]]
        )
    ),
    
    -- ============================================================================
    -- GEOSET OPTIMIZATION
    -- ============================================================================
    
    fn optimizeGeoset g =
    (
        local g2 = MaxGeoset ()
        
        local newV = this.optimizeVertices g.vertices
        
        local tmp = ::Utils.ArrayInverse newV
        tmp = ::Utils.ArrayFilter tmp (fn tmp2 a = a != undefined)
        
        -- Store per-face-corner normals BEFORE any remapping
        -- These will be applied via Edit_Normals modifier to preserve hard edges
        local faceNormals = #()
        faceNormals[g.faces.count] = undefined
        for i = 1 to g.faces.count do
        (
            local f = g.faces[i]
            faceNormals[i] = #(copy g.normals[f[1]], copy g.normals[f[2]], copy g.normals[f[3]])
        )
        
        -- Per-vertex normals (fallback, averaged per unique vertex)
        local normals2 = #()
        normals2[tmp.count] = undefined
        for i = 1 to tmp.count do
        (
            local nset = tmp[i]
            local newNormal = [0,0,0]
            for n in nset do newNormal = newNormal + g.normals[n]
            newNormal = normalize (newNormal/nset.count)
            normals2[i] = newNormal
        )
        
        local faces2 = deepCopy g.faces
        tmp = copy newV #nomap
        
        this.fixVertexindices tmp
        for f in faces2 do
        (
            f[1] = tmp[f[1]]
            f[2] = tmp[f[2]]
            f[3] = tmp[f[3]]
        )
        
        local tvfaces = #()
        local tvertices2 = #()
        for uvSet in g.tvertices do
        (
            local tf = deepCopy g.faces
            local newTV = this.optimizeTVertices uvSet newV minimum:[0.0,0.0] maximum:[1.0,1.0]
            tmp = copy newTV #nomap
            this.fixVertexindices tmp
            for f in tf do
            (
                f[1] = tmp[f[1]]
                f[2] = tmp[f[2]]
                f[3] = tmp[f[3]]
            )
            ::Utils.OptimizeArray newTV
            local uvSetNew = #()
            uvSetNew[newTV.count] = undefined
            for i = 1 to newTV.count do
                uvSetNew[i] = uvSet[newTV[i]]
            append tvfaces tf
            append tvertices2 uvSetNew
        )
        ::Utils.OptimizeArray newV
        
        local vertices2 = #(), matrixindices2 = #()
        vertices2[newV.count] = undefined
        matrixindices2[newV.count] = undefined
        for i = 1 to newV.count do
        (
            vertices2[i] = g.vertices[newV[i]]
            matrixindices2[i] = g.matrixindices[newV[i]]
        )
        
        -- Reforged v1200: skinData mit-remappen (per-vertex Bone-Weights)
        local skinData2 = undefined
        if g.skinData != undefined and g.skinData.count > 0 then
        (
            skinData2 = #()
            skinData2[newV.count] = undefined
            for i = 1 to newV.count do
                skinData2[i] = g.skinData[newV[i]]
        )
        
        g2.vertices = vertices2
        g2.normals = normals2
        g2.tvertices = tvertices2
        g2.matrixindices = matrixindices2
        g2.skinData = skinData2
        g2.faces = faces2
        g2.tvfaces = tvfaces
        g2.matrices = g.matrices
        g2.material = g.material
        g2.id = g.id
        g2.geosetAnimId = g.geosetAnimId
        g2.numUVWmaps = g.numUVWmaps
        g2.lod = g.lod
        g2.lodName = g.lodName
        g2.ex = faceNormals
        g2
    ),
    
    fn optimizeGeosets model =
    (
        local geos = model.geosets
        for i = 1 to geos.count do
        (
            ::Progress.updateItem ("Optimizing Model: Mesh " + (i as string))
            geos[i] = this.optimizeGeoset geos[i]
        )
        local ganim = model.geosetAnims
        for i = 1 to ganim.count do
            ganim[i].geoset = geos[ganim[i].geoset.id]
    ),
    
    -- ============================================================================
    -- BONE/HELPER OPTIMIZATION
    -- ============================================================================
    
    fn setBoneUnusefull obj =
    (
        if (obj.deleted == undefined) then
        (
            local t = obj.transformations.translation == undefined and \
                      obj.transformations.rotation == undefined and \
                      obj.transformations.scale == undefined and \
                      obj.billboarded == false and \
                      obj.billboardedLockX == false and \
                      obj.billboardedLockY == false and \
                      obj.billboardedLockZ == false
            
            if t then
            (
                local p = obj.parent
                local t2 = true
                
                while (p != undefined and t2) do
                (
                    t2 = t2 and (this.setBoneUnusefull p)
                    p = p.parent 
                )
                
                t = t and (not t2)
            )
            
            obj.deleted = t
            t
        )
        else obj.deleted
    ),
    
    fn resetGeometrySkinning model =
    (
        local MdlObjects = model.ex
        for g in model.geosets do
        (
            for m in g.matrices do
            (
                for i = 1 to m.count do
                (
                    local b = MdlObjects[m[i]]
                    if b.deleted then
                    (
                        local p = b.parent
                        while (p.deleted) do p = p.parent
                        m[i] = p.objId
                    )
                )
                ::Utils.OptimizeArray m
            )
        )
    ),
    
    -- ============================================================================
    -- ELEMENT DETECTION (for mesh splitting)
    -- ============================================================================
    
    fn getElements faces numVerts =
    (
        fn addFace f index mapper =
        (
            if mapper[index] == undefined then
                mapper[index] = #()
            append mapper[index] f
        )
        
        local mapper = #()
        mapper[numVerts] = undefined
        for i = 1 to faces.count do
        (
            local f = faces[i]
            addFace i f[1] mapper
            addFace i f[2] mapper
            addFace i f[3] mapper
        )
        
        local g = Graph faces.count
        
        for m in mapper where m != undefined do
            for i = 1 to m.count do
                for j = i + 1 to m.count do
                    g.addConnection m[i] m[j] bothSided:true
        
        g.depthFirstSearch ()    
    ),
    
    fn splitGeosets model =
    (
        local i = 1
        
        for g in model.geosets do
        (
            i = i + 1
        )
    ),
    
    fn optimizeBonesAndHelpers model =
    (
        ::Progress.updateItem ("Optimizing Model: Bones & Helpers ")
        for b in model.helpers do
            this.setBoneUnusefull b
        for b in model.bones do
            this.setBoneUnusefull b
        this.resetGeometrySkinning model
    ),
    
    -- ============================================================================
    -- MAIN OPTIMIZATION FUNCTIONS
    -- ============================================================================
    
    fn optimizeImportModel model optimizeGeometry optimizeBonesHelpers =
    (
        if optimizeGeometry then this.optimizeGeosets model
        if optimizeBonesHelpers then this.optimizeBonesAndHelpers model
    )
)

-- ============================================================================
-- GLOBAL INSTANCE (for backward compatibility)
-- ============================================================================
global Optimizer = WhiteoutDexOptimizeStruct()
