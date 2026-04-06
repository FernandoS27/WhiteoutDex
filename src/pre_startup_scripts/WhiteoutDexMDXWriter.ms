/*
    WhiteoutDexMDXWriter.ms
    
    Refactored as Struct for better encapsulation and maintainability.
    Handles binary MDX file output for Warcraft 3 models.
    
    Usage:
        global MDXWriter = WhiteoutDexMDXWriterStruct()
        MDXWriter.OutputMDX model "C:\\path\\to\\file.mdx"
*/

-- ============================================================================
-- HELPER STRUCT
-- ============================================================================

struct MDXAnimChunk (
    name,
    data,
    writeFunc
)

-- ============================================================================
-- MAIN MDX WRITER STRUCT
-- ============================================================================

struct WhiteoutDexMDXWriterStruct
(
    public version = 800,
    public helpersList = #(),
    
    -- ============================================================================
    -- ANIMATION HELPER FUNCTIONS
    -- ============================================================================
    
    fn writeAnimateableParameter val defaultVal name &anims &writeFunc =
    (
        if (val.type == #Static) then
            writeFunc val.data
        else 
        (
            local a = MDXAnimChunk name:name data:val.data writeFunc:writeFunc
            writeFunc defaultVal
            append anims a
        )
    ),
    
    fn writeAnimation name data &writeFunc =
    (
        ::IOManager.writeMDXHead name
        local keys = data.keys
        local isSmooth = ::IOManager.isSmoothAnim data.type
        ::IOManager.writeMDXLong keys.count
        ::IOManager.writeMDXLong (::IOManager.ControlToInt data.type)
        
        if data.globalSequenceId == undefined then
            ::IOManager.writeMDXLong -1
        else
            ::IOManager.writeMDXLong (data.globalSequenceId - 1)
        
        if isSmooth then
        (
            local hasValidTangents = true
            if keys.count > 0 then
            (
                local firstKey = keys[1]
                hasValidTangents = (isProperty firstKey #intan) and (isProperty firstKey #outtan)
            )
            
            for k in keys do
            (
                ::IOManager.writeMDXLong (::Utils.F2WI k.time)
                writeFunc k.value
                
                if hasValidTangents and (isProperty k #intan) and (isProperty k #outtan) then
                (
                    writeFunc k.intan
                    writeFunc k.outtan
                )
                else
                (
                    writeFunc k.value
                    writeFunc k.value
                    
                    if hasValidTangents then
                    (
                        hasValidTangents = false
                    )
                )
            )
        )
        else
        (
            for k in keys do
            (
                ::IOManager.writeMDXLong (::Utils.F2WI k.time)
                writeFunc k.value
            )
        )
    ),
    
    fn writeAnimations anims =
    (
        for a in anims do
            this.writeAnimation a.name a.data a.writeFunc
    ),
    
    -- ============================================================================
    -- VERSION & HEADER
    -- ============================================================================
    
    fn writeVersion model =
    (
        local pos = ::IOManager.InitiateChunk "VERS"
        ::IOManager.writeMDXLong model.version
        ::IOManager.FinishChunk pos
    ),
    
    fn writeHeader model =
    (
        local pos = ::IOManager.InitiateChunk "MODL"
        ::IOManager.writeMDXString model.name 80
        ::IOManager.writeMDXString "" 260
        ::IOManager.writeMDXExtents model.extents
        ::IOManager.writeMDXLong 150
        ::IOManager.FinishChunk pos
    ),
    
    -- ============================================================================
    -- SEQUENCES
    -- ============================================================================
    
    fn writeSequences seq =
    (
        local pos = ::IOManager.InitiateChunk "SEQS"
        
        for s in seq do
        (
            ::IOManager.writeMDXString s.name 80
            ::IOManager.writeMDXLong (::Utils.F2WI s.startTime)
            ::IOManager.writeMDXLong (::Utils.F2WI s.endTime)
            
            if s.movespeed == undefined then 
                ::IOManager.writeMDXFloat 0.0
            else
                ::IOManager.writeMDXFloat (s.movespeed as float)
            
            ::IOManager.writeMDXLong (::Utils.B2I s.loop)
            
            if s.rarity == undefined then
                ::IOManager.writeMDXFloat 0.0
            else
                ::IOManager.writeMDXFloat (s.rarity as float)
            
            ::IOManager.writeMDXLong 0
            ::IOManager.writeMDXExtents s.extents
        )
        
        ::IOManager.FinishChunk pos
    ),
    
    -- ============================================================================
    -- GLOBAL SEQUENCES
    -- ============================================================================
    
    fn writeGlobalSequences seq =
    (
        if (seq.count > 0) then
        (
            local pos = ::IOManager.InitiateChunk "GLBS"
            for s in seq do ::IOManager.writeMDXLong (::Utils.F2WI s)
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- TEXTURES
    -- ============================================================================
    
    fn writeTextures tex =
    (
        fn Flags t =
        (
            local b = #{}
            b[1] = t.wrapWidth
            b[2] = t.wrapHeight
            b
        )
        
        if (tex.count > 0) then
        (
            local pos = ::IOManager.InitiateChunk "TEXS"
            
            for t in tex do
            (
                ::IOManager.writeMDXLong t.replaceableId
                ::IOManager.writeMDXString t.path 260
                ::IOManager.writeMDXFlags (Flags t)
            )
            
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- LAYERS
    -- ============================================================================
    
    fn writeLayers layers =
    (
        -- Layer ShadingFlags nach MDX Spezifikation:
        -- Bit 0 (#1)   - Unshaded
        -- Bit 1 (#2)   - SphereEnvironmentMap
        -- Bit 4 (#16)  - TwoSided
        -- Bit 5 (#32)  - Unfogged
        -- Bit 6 (#64)  - NoDepthTest
        -- Bit 7 (#128) - NoDepthSet
        
        fn Flags l =
        (
            local b = #{}
            b[1] = l.unshaded
            b[2] = l.SphereEnvMap
            b[16] = l.twosided
            b[32] = l.unfogged
            b[64] = l.nodephtest
            b[128] = l.nodephset
            b
        )
        
        ::IOManager.writeMDXHead "LAYS"
        ::IOManager.writeMDXLong layers.count
        
        for l in layers do
        (
            local pos = ::IOManager.InitiateChunk "" includeName:false
            local animations = #()
            
            ::IOManager.writeMDXLong (l.filtermode-1)
            ::IOManager.writeMDXFlags (Flags l)
            
            -- Alpha zu Wc3-Format konvertieren
            -- Plugin speichert Alpha immer im 0-100 Bereich, MDX erwartet 0-1
            ::IOManager.writerTransformKeys l.alpha (fn tmp a = a/100.0)
            
            -- TextureID (statisch oder animiert)
            -- v1200: headerTexId wird vom Spiel ignoriert, immer 0 schreiben
            if this.version == 1200 and l.textures.type == #Static then
                ::IOManager.writeMDXLong 0
            else
                this.writeAnimateableParameter l.textures 0 "KMTF" animations ::IOManager.writeMDXLong
            
            -- TVertexAnimId
            if l.tvertexid != undefined then 
                ::IOManager.writeMDXLong l.tvertexid.id
            else
                ::IOManager.writeMDXLong -1
            
            -- CoordId
            ::IOManager.writeMDXLong l.coordId
            
            -- Alpha (statisch oder animiert)
            this.writeAnimateableParameter l.alpha 1.0 "KMTA" animations ::IOManager.writeMDXFloat
            
            -- Reforged v1200: Fresnel-Daten + Texture Binding Table
            if this.version == 1200 then
            (
                -- Fresnel-Daten (24 bytes)
                ::IOManager.writeMDXFloat (if l.emissiveGain != undefined then l.emissiveGain else 1.0)
                local fc = if l.fresnelColor != undefined then l.fresnelColor else [1,1,1]
                ::IOManager.writeMDXFloat fc.x
                ::IOManager.writeMDXFloat fc.y
                ::IOManager.writeMDXFloat fc.z
                ::IOManager.writeMDXFloat (if l.fresnelOpacity != undefined then l.fresnelOpacity else 0.0)
                ::IOManager.writeMDXFloat (if l.fresnelTeamColor != undefined then l.fresnelTeamColor else 0.0)
                
                -- Texture Binding Table
                local bindings = #()
                -- Slot 0: Diffuse (immer vorhanden)
                local diffuseId = if l.textures.type == #Static then l.textures.data else 0
                append bindings #(diffuseId, 0)
                -- Slot 1: Normal
                if l.normalTexId != undefined then append bindings #(l.normalTexId, 1)
                -- Slot 2: ORM
                if l.ormTexId != undefined then append bindings #(l.ormTexId, 2)
                -- Slot 3: Emissive
                if l.emissiveTexId != undefined then append bindings #(l.emissiveTexId, 3)
                -- Slot 4: Team Color
                if l.teamColorTexId != undefined then append bindings #(l.teamColorTexId, 4)
                -- Slot 5: Environment Map
                if l.envMapTexId != undefined then append bindings #(l.envMapTexId, 5)
                
                -- ShaderFlag: v1200 verwendet immer ShaderTypeId 1 (HD/PBR Shader)
                local shaderFlag = 1
                ::IOManager.writeMDXLong shaderFlag
                ::IOManager.writeMDXLong bindings.count  -- NumBindings
                for b in bindings do
                (
                    ::IOManager.writeMDXLong b[1]        -- TextureId
                    ::IOManager.writeMDXLong b[2]        -- SlotType
                )
            )
            
            this.writeAnimations animations
            
            ::IOManager.FinishChunk pos inclusive:0
        )
    ),
    
    -- ============================================================================
    -- MATERIALS
    -- ============================================================================
    
    fn writeMaterials materials =
    (
        -- Material Flags nach MDX Spezifikation:
        -- Bit 0 (#1)  - ConstantColor
        -- Bit 4 (#16) - SortPrimitivesFarZ
        -- Bit 5 (#32) - FullResolution
        
        fn MaterialFlags m =
        (
            local b = #{}
            if m.layers.count > 0 then
            (
                local l = m.layers[1]
                b[1] = if l.ConstantColor != undefined then l.ConstantColor else false
                b[8] = if l.SortPrimsNearZ != undefined then l.SortPrimsNearZ else false
                b[16] = if l.SortPrimsFarZ != undefined then l.SortPrimsFarZ else false
                b[32] = if l.FullResolution != undefined then l.FullResolution else false
            )
            b
        )
        
        fn GetPriorityPlane m =
        (
            if m.layers.count > 0 and m.layers[1].PriorityPlane != undefined then
                m.layers[1].PriorityPlane
            else
                0
        )
        
        if (materials.count > 0) then
        (
            local pos = ::IOManager.InitiateChunk "MTLS"
            for m in materials do
            (
                local pos2 = ::IOManager.InitiateChunk "" includeName:false
                
                -- PriorityPlane (DWORD)
                ::IOManager.writeMDXLong (GetPriorityPlane m)
                
                -- Material Flags (DWORD)
                ::IOManager.writeMDXFlags (MaterialFlags m)
                
                -- Layers
                this.writeLayers m.layers
                
                ::IOManager.FinishChunk pos2 inclusive:0
            )
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- TEXTURE ANIMATIONS
    -- ============================================================================
    
    fn writeTextureAnims tvanims =
    (
        if (tvanims.count != 0) then
        (
            local pos = ::IOManager.InitiateChunk "TXAN"
            for tv in tvanims do
            (
                local pos2 = ::IOManager.InitiateChunk "" includeName:false
                local trans = tv.transformation
                if trans != undefined then
                (
                    if trans.translation != undefined then
                        this.writeAnimation "KTAT" trans.translation ::IOManager.writeMDXPosition
                    if trans.rotation != undefined then
                        this.writeAnimation "KTAR" trans.rotation ::IOManager.writeMDXQuaternion
                    if trans.scale != undefined then
                        this.writeAnimation "KTAS" trans.scale ::IOManager.writeMDXPoint3
                )
                ::IOManager.FinishChunk pos2 inclusive:0
            )
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- GEOSETS
    -- ============================================================================
    
    fn writeGeosets meshes =
    (
        if (meshes.count != 0) then
        (
            local pos = ::IOManager.InitiateChunk "GEOS"
            
            for mIdx = 1 to meshes.count do
            (
                local m = meshes[mIdx]
                local pos2 = ::IOManager.InitiateChunk "" includeName:false
                
                ::IOManager.writeMDXHead "VRTX"
                ::IOManager.writeMDXLong m.vertices.count
                for v in m.vertices do
                    ::IOManager.writeMDXPosition v
                
                ::IOManager.writeMDXHead "NRMS"
                ::IOManager.writeMDXLong m.normals.count
                for v in m.normals do
                    ::IOManager.writeMDXPosition v
                
                ::IOManager.writeMDXHead "PTYP"
                ::IOManager.writeMDXLong 1
                ::IOManager.writeMDXLong 4
                
                ::IOManager.writeMDXHead "PCNT"
                ::IOManager.writeMDXLong 1
                ::IOManager.writeMDXLong (m.faces.count*3)
                
                ::IOManager.writeMDXHead "PVTX"
                ::IOManager.writeMDXLong (m.faces.count*3)
                for f in m.faces do
                    ::IOManager.writeMDXFace f
                
                ::IOManager.writeMDXHead "GNDX"
                if this.version == 1200 and m.skinData != undefined then
                    ::IOManager.writeMDXLong 0
                else
                (
                    ::IOManager.writeMDXLong m.matrixindices.count
                    for v in m.matrixindices do
                        ::IOManager.writeMDXByte (v as integer)
                )
                
           -- v1200: Alle Bones sequenziell auflisten (SKIN-BoneId = MATS-Index = ObjId)
                if this.version == 1200 and m.skinData != undefined then
                (
                    -- Max ObjId aus SKIN-Daten ermitteln
                    local maxBoneId = 0
                    for sv in m.skinData where sv != undefined do
                        for b = 1 to 4 do
                            if sv[5+b-1] > 0 and sv[b] > maxBoneId then maxBoneId = sv[b]
                    local numBones = maxBoneId + 1
                    
                    ::IOManager.writeMDXHead "MTGC"
                    ::IOManager.writeMDXLong numBones
                    for i = 1 to numBones do
                        ::IOManager.writeMDXLong 1
                    
                    ::IOManager.writeMDXHead "MATS"
                    ::IOManager.writeMDXLong numBones
                    for i = 0 to (numBones - 1) do
                        ::IOManager.writeMDXLong i
                )
                else
                (
                    ::IOManager.writeMDXHead "MTGC"
                    ::IOManager.writeMDXLong m.matrices.count
                    
                    local tmp = 0
                    for gIdx = 1 to m.matrices.count do
                    (
                        local g = m.matrices[gIdx]
                        ::IOManager.writeMDXLong g.count
                        tmp += g.count
                    )
                    
                    ::IOManager.writeMDXHead "MATS"
                    ::IOManager.writeMDXLong tmp
                    
                    for gIdx = 1 to m.matrices.count do
                    (
                        local g = m.matrices[gIdx]
                        for i in g do
                            ::IOManager.writeMDXLong i
                    )
                )
                
                if m.material != undefined then 
                    ::IOManager.writeMDXLong m.material.id
                else 
                    ::IOManager.writeMDXLong 0
                
                ::IOManager.writeMDXLong 0
                
                if m.unselectable then
                    ::IOManager.writeMDXLong 4
                else
                    ::IOManager.writeMDXLong 0
                
                -- v1200: LOD + LODname nach Unselectable
                if this.version == 1200 then
                (
                    ::IOManager.writeMDXLong (if m.lod != undefined then m.lod else 0)
                    ::IOManager.writeMDXString (if m.lodName != undefined then m.lodName else "") 80
                )
                
                ::IOManager.writeMDXExtents m.mainExtents
                ::IOManager.writeMDXLong m.extents.count
                for n in m.extents do
                    ::IOManager.writeMDXExtents n
                
                -- v1200: TANG-Chunk nach Extents
                if this.version == 1200 and m.tangents != undefined and m.tangents.count > 0 then
                (
                    ::IOManager.writeMDXHead "TANG"
                    ::IOManager.writeMDXLong m.tangents.count
                    for t in m.tangents do
                    (
                        if t != undefined then
                        (
                            -- Max -> MDX Koordinatentransform: [-Max.y, Max.x, Max.z]
                            ::IOManager.writeMDXFloat (-t[2])
                            ::IOManager.writeMDXFloat t[1]
                            ::IOManager.writeMDXFloat t[3]
                            ::IOManager.writeMDXFloat t[4]  -- Handedness unveraendert
                        )
                        else
                        (
                            -- Fallback in MDX-Space: Y-Achse
                            ::IOManager.writeMDXFloat 0.0
                            ::IOManager.writeMDXFloat 1.0
                            ::IOManager.writeMDXFloat 0.0
                            ::IOManager.writeMDXFloat 1.0
                        )
                    )
                )
                
                -- v1200: SKIN-Chunk nach TANG (vor UVAS)
                if this.version == 1200 and m.skinData != undefined and m.skinData.count > 0 then
                (
                    ::IOManager.writeMDXHead "SKIN"
                    ::IOManager.writeMDXLong (m.skinData.count * 8)
                    for sv in m.skinData do
                    (
                        if sv != undefined then
                        (
                            -- 4 Bone-Bytes (ObjId, 0-basiert)
                            for b = 1 to 4 do ::IOManager.writeMDXByte sv[b]
                            -- 4 Weight-Bytes (0-255, Summe = 255)
                            for b = 5 to 8 do ::IOManager.writeMDXByte sv[b]
                        )
                        else
                        (
                            -- Fallback: Bone 0 mit vollem Gewicht
                            ::IOManager.writeMDXByte 0
                            ::IOManager.writeMDXByte 0
                            ::IOManager.writeMDXByte 0
                            ::IOManager.writeMDXByte 0
                            ::IOManager.writeMDXByte 255
                            ::IOManager.writeMDXByte 0
                            ::IOManager.writeMDXByte 0
                            ::IOManager.writeMDXByte 0
                        )
                    )
                )
                
                ::IOManager.writeMDXHead "UVAS"
                ::IOManager.writeMDXLong m.tvertices.count
                
                for tvertexGroup in m.tvertices do
                (
                    ::IOManager.writeMDXHead "UVBS"
                    ::IOManager.writeMDXLong tvertexGroup.count
                    for v in tvertexGroup do
                        ::IOManager.writeMDXTVertex v
                )
                
                ::IOManager.FinishChunk pos2 inclusive:0
            )
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- GEOSET ANIMATIONS
    -- ============================================================================
    
    fn writeGeosetAnims geoAnims =
    (
        fn Flags g =
        (
            local b = #{}
            b[1] = g.dropShadow == true
            b[2] = g.usesColor == true
            b
        )
        
        if (geoAnims.count != 0) then
        (
            local pos = ::IOManager.InitiateChunk "GEOA"
            for g in geoAnims do
            (
                local pos2 = ::IOManager.InitiateChunk "" includeName:false
                local animations = #()
                
                if g.alpha != undefined then
                    this.writeAnimateableParameter g.alpha 1.0 "KGAO" animations ::IOManager.writeMDXFloat
                else
                    ::IOManager.writeMDXFloat 1.0
                
                ::IOManager.writeMDXFlags (Flags g)
                
                if g.color != undefined then
                (
                    ::IOManager.IOFixColor g.color
                    this.writeAnimateableParameter g.color [255,255,255] "KGAC" animations ::IOManager.writeMDXColor
                )
                else
                    ::IOManager.writeMDXColor [255,255,255]
                
                ::IOManager.writeMDXLong g.geoset.id
                this.writeAnimations animations
                ::IOManager.FinishChunk pos2 inclusive:0
            )
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- NODE (shared by bones, helpers, lights, etc.)
    -- ============================================================================
    
    fn writeNode obj =
    (
        fn Flags obj =
        (
            fn typeId sub =
            (
                case (sub.type) of
                (
                    #Wc3Bone: 256
                    #Wc3Light: 512
                    #Wc3Attachment: 2048
                    #Wc3ParticleEmitter: 4096
                    #Wc3ParticleEmitter2: 4096
                    #Wc3CornEmitter: 4096
                    #Wc3RibbonEmitter: 16384
                    #Wc3Event: 1024
                    #Wc3CollisionShape: 8192
                    default: 0
                )
            )
            local sub = obj.sub
            local type = typeId sub
            local b = #{}
            if type != 0 then b[type] = true
            if (sub.type == #Wc3ParticleEmitter) then
                b[32768] = (sub.emissionType == #Mdl)
            if (sub.type == #Wc3ParticleEmitter2) then
            (
                b[65536] = sub.sortPrimsFarZ
                b[32768] = sub.unshaded
                b[131072] = (sub.lineEmitter == 2)
                b[262144] = sub.unfogged
                b[524288] = sub.modelSpace
                b[1048576] = sub.xyQuad
            )
            b[1] = obj.dontInheritTranslation
            b[2] = obj.dontInheritRotation
            b[4] = obj.dontInheritScale
            b[8] = obj.billboarded
            b[16] = obj.billboardedLockX
            b[32] = obj.billboardedLockY
            b[64] = obj.billboardedLockZ
            b[128] = obj.cameraAnchored
            b
        )
        
        local sub = obj.sub
        local pos = ::IOManager.InitiateChunk "" includeName:false
        ::IOManager.writeMDXString obj.name 80
        ::IOManager.writeMDXLong obj.objid
        if obj.parent != undefined then 
            ::IOManager.writeMDXLong obj.parent.objid
        else
            ::IOManager.writeMDXLong -1
        ::IOManager.writeMDXFlags (Flags obj)
        
        local trans = obj.transformations
        if trans != undefined then
        (
            if trans.translation != undefined and trans.translation.keys.count > 0 then
                this.writeAnimation "KGTR" trans.translation ::IOManager.writeMDXPosition
            if trans.rotation != undefined and trans.rotation.keys.count > 0 then
                this.writeAnimation "KGRT" trans.rotation ::IOManager.writeMDXQuaternion
            if trans.scale != undefined and trans.scale.keys.count > 0 then
                this.writeAnimation "KGSC" trans.scale ::IOManager.writeMDXPoint3
        )
        ::IOManager.FinishChunk pos inclusive:0
    ),
    
    -- ============================================================================
    -- BONES
    -- ============================================================================
    
    fn writeBones bones =
    (
        if (bones.count != 0) then
        (
            local pos = ::IOManager.InitiateChunk "BONE"
            for b in bones do
            (
                this.writeNode b
                if (b.sub.geosets.count == 1) then
                (
                    local g = b.sub.geosets[1]
                    ::IOManager.writeMDXLong g.id
                    if g.geosetAnimId != undefined then 
                        ::IOManager.writeMDXLong g.geosetAnimId
                    else
                        ::IOManager.writeMDXLong -1
                )
                else
                (
                    ::IOManager.writeMDXLong -1
                    ::IOManager.writeMDXLong -1
                )
            )
            -- v1200: Helpers auch in BONE schreiben (mit geosetId=-1)
            if this.version == 1200 then
            (
                for h in this.helpersList do
                (
                    this.writeNode h
                    ::IOManager.writeMDXLong -1
                    ::IOManager.writeMDXLong -1
                )
            )
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- LIGHTS
    -- ============================================================================
    
    fn writeLights lights =
    (
        if (lights.count != 0) then
        (
            local pos = ::IOManager.InitiateChunk "LITE"
            for l in lights do
            (
                local pos2 = ::IOManager.InitiateChunk "" includeName:false
                this.writeNode l
                local sub = l.sub
                local animations = #()
                
                case (sub.lightType) of
                (
                    #Omnidirectional: ::IOManager.writeMDXLong 0
                    #Directional: ::IOManager.writeMDXLong 1
                    default: ::IOManager.writeMDXLong 2
                )
                
                this.writeAnimateableParameter sub.attenuationStart 0.0 "KLAS" animations ::IOManager.writeMDXFloat
                this.writeAnimateableParameter sub.attenuationEnd 0.0 "KLAE" animations ::IOManager.writeMDXFloat
                
                ::IOManager.IOFixColor sub.color
                this.writeAnimateableParameter sub.color [255,255,255] "KLAC" animations ::IOManager.writeMDXColor
                this.writeAnimateableParameter sub.intensity 0.0 "KLAI" animations ::IOManager.writeMDXFloat
                
                ::IOManager.IOFixColor sub.ambColor
                this.writeAnimateableParameter sub.ambColor [255,255,255] "KLBC" animations ::IOManager.writeMDXColor
                this.writeAnimateableParameter sub.ambIntensity 0.0 "KLBI" animations ::IOManager.writeMDXFloat
                
                if sub.visibility != undefined then
                    this.writeAnimation "KLAV" sub.visibility.data ::IOManager.writeMDXFloat
                
                this.writeAnimations animations
                ::IOManager.FinishChunk pos2 inclusive:0
            )
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- HELPERS
    -- ============================================================================
    
    fn writeHelpers helpers =
    (
        -- v1200: Helpers sind bereits in BONE geschrieben
        if this.version == 1200 then return()
        if (helpers.count != 0) then
        (
            local pos = ::IOManager.InitiateChunk "HELP"
            for h in helpers do
                this.writeNode h
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- ATTACHMENTS
    -- ============================================================================
    
    fn writeAttachments attachments =
    (
        if (attachments.count != 0) then
        (
            local pos = ::IOManager.InitiateChunk "ATCH"
            local id = 0
            for a in attachments do
            (
                local pos2 = ::IOManager.InitiateChunk "" includeName:false
                local sub = a.sub
                local savedName = a.name
                a.name = sub.name
                this.writeNode a
                a.name = savedName
                local path = ""
                if sub.outPath != undefined then path = sub.outPath
                ::IOManager.writeMDXString path 260
                if sub.attachmentId != undefined then
                    ::IOManager.writeMDXLong sub.attachmentId
                else
                (
                    ::IOManager.writeMDXLong id
                    id += 1
                )
                if sub.visibility != undefined then
                    this.writeAnimation "KATV" sub.visibility.data ::IOManager.writeMDXFloat
                ::IOManager.FinishChunk pos2 inclusive:0
            )
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- PIVOT POINTS
    -- ============================================================================
    
    fn writePivotPoints model =
    (
        local pos = ::IOManager.InitiateChunk "PIVT"
        
        for o in model.bones do ::IOManager.writeMDXPosition o.pivot
        for o in model.lights do ::IOManager.writeMDXPosition o.pivot
        for o in model.helpers do ::IOManager.writeMDXPosition o.pivot
        for o in model.attachments do ::IOManager.writeMDXPosition o.pivot
        for o in model.particleEmitters1 do ::IOManager.writeMDXPosition o.pivot
        for o in model.particleEmitters2 do ::IOManager.writeMDXPosition o.pivot
        for o in model.ribbonEmitters do ::IOManager.writeMDXPosition o.pivot
        for o in model.events do ::IOManager.writeMDXPosition o.pivot
        for o in model.CollisionShapes do ::IOManager.writeMDXPosition o.pivot
        if this.version == 1200 and model.cornEmitters != undefined then
            for o in model.cornEmitters do ::IOManager.writeMDXPosition o.pivot
        
        ::IOManager.FinishChunk pos
    ),
    
    -- ============================================================================
    -- PARTICLE EMITTERS (Type 1)
    -- ============================================================================
    
    fn writeParticleEmitters particleEmitters =
    (
        if (particleEmitters.count != 0) then
        (
            local pos = ::IOManager.InitiateChunk "PREM"
            for p in particleEmitters do
            (
                local pos2 = ::IOManager.InitiateChunk "" includeName:false
                this.writeNode p
                local sub = p.sub
                local animations = #()
                
                this.writeAnimateableParameter sub.emissionRate 0.0 "KPEE" animations ::IOManager.writeMDXFloat
                this.writeAnimateableParameter sub.gravity 0.0 "KPEG" animations ::IOManager.writeMDXFloat
                this.writeAnimateableParameter sub.longitude 0.0 "KPLN" animations ::IOManager.writeMDXFloat
                this.writeAnimateableParameter sub.latitude 0.0 "KPEL" animations ::IOManager.writeMDXFloat
                
                ::IOManager.writeMDXString sub.path 260
                
                -- LifeSpan ist im PE1-Format immer statisch (kein Animations-Tag definiert)
                if sub.lifespan.type == #Static then
                    ::IOManager.writeMDXFloat sub.lifespan.data
                else
                    ::IOManager.writeMDXFloat sub.lifespan.data
                
                -- InitVelocity = KPES (Speed)
                this.writeAnimateableParameter sub.initVelocity 0.0 "KPES" animations ::IOManager.writeMDXFloat
                
                if sub.visibility != undefined then
                    this.writeAnimation "KPEV" sub.visibility.data ::IOManager.writeMDXFloat
                
                this.writeAnimations animations
                ::IOManager.FinishChunk pos2 inclusive:0
            )
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- PARTICLE EMITTERS 2
    -- ============================================================================
    
    fn writeParticleEmitters2 particleEmitters2 =
    (
        if (particleEmitters2.count != 0) then
        (
            local pos = ::IOManager.InitiateChunk "PRE2"
            for p2 in particleEmitters2 do
            (
                local pos2 = ::IOManager.InitiateChunk "" includeName:false
                this.writeNode p2
                local sub = p2.sub
                local animations = #()
                
                this.writeAnimateableParameter sub.speed 0.0 "KP2S" animations ::IOManager.writeMDXFloat
                this.writeAnimateableParameter sub.variation 0.0 "KP2R" animations ::IOManager.writeMDXFloat
                this.writeAnimateableParameter sub.latitude 0.0 "KP2L" animations ::IOManager.writeMDXFloat
                this.writeAnimateableParameter sub.gravity 0.0 "KP2G" animations ::IOManager.writeMDXFloat
                
                ::IOManager.writeMDXFloat sub.lifeSpan
                this.writeAnimateableParameter sub.emissionRate 0.0 "KP2E" animations ::IOManager.writeMDXFloat
                this.writeAnimateableParameter sub.length 0.0 "KP2N" animations ::IOManager.writeMDXFloat
                this.writeAnimateableParameter sub.width 0.0 "KP2W" animations ::IOManager.writeMDXFloat
                
                ::IOManager.writeMDXLong (sub.filtermode-1)
                ::IOManager.writeMDXLong sub.rows
                ::IOManager.writeMDXLong sub.columns
                ::IOManager.writeMDXLong (sub.particleType - 1)
                ::IOManager.writeMDXFloat sub.tailLength
                ::IOManager.writeMDXFloat sub.time
                
                ::IOManager.writeMDXColor (sub.startColor as point3)
                ::IOManager.writeMDXColor (sub.midColor as point3)
                ::IOManager.writeMDXColor (sub.endColor as point3)
                ::IOManager.writeMDXByte sub.alpha.x
                ::IOManager.writeMDXByte sub.alpha.y
                ::IOManager.writeMDXByte sub.alpha.z
                ::IOManager.writeMDXFloat sub.scaling.x
                ::IOManager.writeMDXFloat sub.scaling.y
                ::IOManager.writeMDXFloat sub.scaling.z
                ::IOManager.writeMDXPoint3Long sub.lifeSpanUVAnim
                ::IOManager.writeMDXPoint3Long sub.decayUVAnim
                ::IOManager.writeMDXPoint3Long sub.TailUVAnim
                ::IOManager.writeMDXPoint3Long sub.TailDecayUVAnim
                
                ::IOManager.writeMDXLong sub.texture.id
                if sub.squirt then 
                    ::IOManager.writeMDXLong 1
                else 
                    ::IOManager.writeMDXLong 0
                ::IOManager.writeMDXLong sub.priorityPlane
                ::IOManager.writeMDXLong (sub.replaceableId-1)
                
                if sub.visibility != undefined then
                    this.writeAnimation "KP2V" sub.visibility.data ::IOManager.writeMDXFloat
                this.writeAnimations animations
                ::IOManager.FinishChunk pos2 inclusive:0
            )
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- RIBBON EMITTERS
    -- ============================================================================
    
    fn writeRibbonEmitters ribbonEmitters =
    (
        if (ribbonEmitters.count != 0) then
        (
            local pos = ::IOManager.InitiateChunk "RIBB"
            for r in ribbonEmitters do
            (
                local pos2 = ::IOManager.InitiateChunk "" includeName:false
                this.writeNode r
                local sub = r.sub
                local animations = #()
                
                this.writeAnimateableParameter sub.heightAbove 0.0 "KRHA" animations ::IOManager.writeMDXFloat
                this.writeAnimateableParameter sub.heightBelow 0.0 "KRHB" animations ::IOManager.writeMDXFloat
                this.writeAnimateableParameter sub.alpha 0.0 "KRAL" animations ::IOManager.writeMDXFloat
                ::IOManager.IOFixColor sub.color
                this.writeAnimateableParameter sub.color [255,255,255] "KRCO" animations ::IOManager.writeMDXColor
                ::IOManager.writeMDXFloat sub.lifeSpan
                ::IOManager.writerTransformKeys sub.textureSlot (fn tmp a = (a as integer))
                if sub.textureSlot.type != #Static then
                    sub.textureSlot.data.type = #NonInterp
                this.writeAnimateableParameter sub.textureSlot 0 "KRTX" animations ::IOManager.writeMDXLong
                ::IOManager.writeMDXLong sub.emissionRate
                ::IOManager.writeMDXLong sub.rows
                ::IOManager.writeMDXLong sub.columns
                if sub.material != undefined then 
                    ::IOManager.writeMDXLong sub.material.id
                else 
                    ::IOManager.writeMDXLong 0
                ::IOManager.writeMDXFloat sub.gravity
                if sub.visibility != undefined then
                    this.writeAnimation "KRVS" sub.visibility.data ::IOManager.writeMDXFloat
                this.writeAnimations animations
                ::IOManager.FinishChunk pos2 inclusive:0
            )
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- EVENTS
    -- ============================================================================
    
    fn writeEvents events =
    (
        if (events.count != 0) then
        (
            local pos = ::IOManager.InitiateChunk "EVTS"
            for o in events do
            (
                local savedName = o.name
                o.name = substring o.name 5 o.name.count
                this.writeNode o
                o.name = savedName
                local track = o.sub.eventTrack
                if (track != undefined and track.count != 0) then
                (
                    ::IOManager.writeMDXHead "KEVT"
                    ::IOManager.writeMDXLong track.count
                    ::IOManager.writeMDXLong -1
                    for t in track do
                        ::IOManager.writeMDXLong (::Utils.F2WI t)
                )
            )
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- CAMERAS
    -- ============================================================================
    
    fn writeCameras cameras =
    (
        if (cameras.count != 0) then
        (
            local pos = ::IOManager.InitiateChunk "CAMS"
            for c in cameras do
            (
                local pos2 = ::IOManager.InitiateChunk "" includeName:false
                ::IOManager.writeMDXString c.name 80
                ::IOManager.writeMDXPosition c.pivot
                ::IOManager.writeMDXFloat (degToRad c.fieldOfView)
                ::IOManager.writeMDXFloat c.farClip
                ::IOManager.writeMDXFloat c.nearClip
                ::IOManager.writeMDXPosition c.targetPos
                local trans = c.transformations
                if trans != undefined then
                    if trans.translation != undefined then 
                        this.writeAnimation "KCTR" trans.translation ::IOManager.writeMDXPosition
                trans = c.targetTransformations
                if trans != undefined then
                    if trans.translation != undefined then 
                        this.writeAnimation "KTTR" trans.translation ::IOManager.writeMDXPosition
                if c.rotation != undefined then
                    this.writeAnimation "KCRL" c.rotation ::IOManager.writeMDXFloat
                ::IOManager.FinishChunk pos2 inclusive:0
            )
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- COLLISION SHAPES
    -- ============================================================================
    
    fn writeCollisionShapes shapes =
    (
        if (shapes.count != 0) then
        (
            local pos = ::IOManager.InitiateChunk "CLID"
            for s in shapes do
            (
                this.writeNode s
                local sub = s.sub
                case (sub.shape) of
                (
                    #Box: ::IOManager.writeMDXLong 0
                    #Sphere: ::IOManager.writeMDXLong 2
                    default: ::IOManager.writeMDXLong 1
                )
                for v in sub.vertices do
                    ::IOManager.writeMDXCollisionVertex v
                if sub.boundsRadius != undefined then
                    ::IOManager.writeMDXFloat sub.boundsRadius
            )
            ::IOManager.FinishChunk pos
        )
    ),
    
     -- ============================================================================
    -- CORN EMITTERS (Reforged)
    -- ============================================================================
    
    fn writeCornEmitters cornEmitters =
    (
        if cornEmitters != undefined and cornEmitters.count > 0 then
        (
            local pos = ::IOManager.InitiateChunk "CORN"
            for c in cornEmitters do
            (
                local pos2 = ::IOManager.InitiateChunk "" includeName:false
                local sub = c.sub
                this.writeNode c
                -- CORN-spezifische Daten
                ::IOManager.writeMDXFloat (if sub.lifeSpan != undefined then sub.lifeSpan else 0.0)
                ::IOManager.writeMDXFloat (if sub.emissionRate != undefined then sub.emissionRate else 0.0)
                ::IOManager.writeMDXFloat (if sub.speed != undefined then sub.speed else 0.0)
                -- Farbe (0-1)
                local col = if sub.color != undefined then sub.color else [1,1,1]
                ::IOManager.writeMDXFloat col.x
                ::IOManager.writeMDXFloat col.y
                ::IOManager.writeMDXFloat col.z
                -- Alpha (0-1): statischer Header-Wert
                ::IOManager.writeMDXFloat (if sub.alpha != undefined then sub.alpha else 1.0)
                -- ReplaceableId
                ::IOManager.writeMDXLong (if sub.replaceableId != undefined then sub.replaceableId else 0)
                -- Path (260 bytes)
                ::IOManager.writeMDXString (if sub.path != undefined then sub.path else "") 260
                -- Flags (260 bytes)
                ::IOManager.writeMDXString (if sub.flags != undefined then sub.flags else "") 260
                -- Alpha Animation (KPPA) — muss VOR KPPE und KPPV stehen (Original-Reihenfolge)
                if sub.alphaAnim != undefined and sub.alphaAnim.type == #Animated then
                    this.writeAnimation "KPPA" sub.alphaAnim.data ::IOManager.writeMDXFloat
                -- EmissionRate Animation (Reforged)
                if sub.emissionRateAnim != undefined and sub.emissionRateAnim.type == #Animated then
                    this.writeAnimation "KPPE" sub.emissionRateAnim.data ::IOManager.writeMDXFloat
                -- Visibility Animation
                if sub.visibility != undefined and sub.visibility.type == #Animated then
                    this.writeAnimation "KPPV" sub.visibility.data ::IOManager.writeMDXFloat
                ::IOManager.FinishChunk pos2 inclusive:0
            )
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- FACEFX TARGETS (Reforged)
    -- ============================================================================
    
    fn writeFaceFX faceFxTargets =
    (
        if faceFxTargets != undefined and faceFxTargets.count > 0 then
        (
            local pos = ::IOManager.InitiateChunk "FAFX"
            -- FAFX: Kein Sub-Chunk, Entries direkt hintereinander
            -- Chunk-Size (= numEntries * 340) wird von FinishChunk gesetzt
            for fx in faceFxTargets do
            (
                ::IOManager.writeMDXString (if fx.name != undefined then fx.name else "") 80
                ::IOManager.writeMDXString (if fx.path != undefined then fx.path else "") 260
            )
            ::IOManager.FinishChunk pos
        )
    ),
    
    -- ============================================================================
    -- BIND POSES (Reforged)
    -- ============================================================================
    
    fn writeBindPoses bindPoses =
    (
        if bindPoses != undefined and bindPoses.count > 0 then
        (
            local pos = ::IOManager.InitiateChunk "BPOS"
            -- Chunk-Size wird von FinishChunk gesetzt
            -- Erstes DWORD: numEntries
            ::IOManager.writeMDXLong bindPoses.count
            for bp in bindPoses do
            (
                -- Max -> MDX Koordinaten-Konvertierung (Inverse von Reader)
                -- Reader: matrix3 [Yy,-Yx,Yz] [-Xy,Xx,-Xz] [Zy,-Zx,Zz] [Ty,-Tx,Tz]
                -- Writer: extrahiere MDX Werte zurueck
                local r1 = bp.row1   -- [Yy, -Yx, Yz]
                local r2 = bp.row2   -- [-Xy, Xx, -Xz]
                local r3 = bp.row3   -- [Zy, -Zx, Zz]
                local r4 = bp.row4   -- [Ty, -Tx, Tz]
                -- X-Achse: Xx=r2.y, Xy=-r2.x, Xz=-r2.z
                ::IOManager.writeMDXFloat r2.y
                ::IOManager.writeMDXFloat (-r2.x)
                ::IOManager.writeMDXFloat (-r2.z)
                -- Y-Achse: Yx=-r1.y, Yy=r1.x, Yz=r1.z
                ::IOManager.writeMDXFloat (-r1.y)
                ::IOManager.writeMDXFloat r1.x
                ::IOManager.writeMDXFloat r1.z
                -- Z-Achse: Zx=-r3.y, Zy=r3.x, Zz=r3.z
                ::IOManager.writeMDXFloat (-r3.y)
                ::IOManager.writeMDXFloat r3.x
                ::IOManager.writeMDXFloat r3.z
                -- Translation: Tx=-r4.y, Ty=r4.x, Tz=r4.z
                ::IOManager.writeMDXFloat (-r4.y)
                ::IOManager.writeMDXFloat r4.x
                ::IOManager.writeMDXFloat r4.z
            )
            ::IOManager.FinishChunk pos
        )
    ),

    -- ============================================================================
    -- MAIN WRITE FUNCTION
    -- ============================================================================
    
    fn writeMDXFile model =
    (
        this.version = model.version
        this.helpersList = model.helpers
        ::Progress.downLevel 100 20
        ::IOManager.writeMDXHead "MDLX"
        ::Progress.showItem "Writing Version"
        this.writeVersion model
        ::Progress.showItem "Writing Header"
        this.writeHeader model
        ::Progress.showItem "Writing Animations"
        this.writeSequences model.sequences
        ::Progress.showItem "Writing Global Sequences"
        this.writeGlobalSequences model.globalSequences
        ::Progress.showItem "Writing Materials"
        this.writeMaterials model.materials
        ::Progress.showItem "Writing Textures"
        this.writeTextures model.textures
        ::Progress.showItem "Writing TVertex Animations"
        this.writeTextureAnims model.tvertexAnims
        ::Progress.showItem "Writing Geosets"
        this.writeGeosets model.geosets
        ::Progress.showItem "Writing Geoset Animations"
        this.writeGeosetAnims model.geosetAnims
        ::Progress.showItem "Writing Bones"
        this.writeBones model.bones
        ::Progress.showItem "Writing Lights"
        this.writeLights model.lights
        ::Progress.showItem "Writing Helpers"
        this.writeHelpers model.helpers
        ::Progress.showItem "Writing Attachments"
        this.writeAttachments model.attachments
        ::Progress.showItem "Writing Pivot Points"
        this.writePivotPoints model
        ::Progress.showItem "Writing Particle Emitters 1"
        this.writeParticleEmitters model.particleEmitters1
        ::Progress.showItem "Writing Particle Emitters 2"
        this.writeParticleEmitters2 model.particleEmitters2
        ::Progress.showItem "Writing Ribbon Emitters"
        this.writeRibbonEmitters model.ribbonEmitters
        ::Progress.showItem "Writing Cameras"
        this.writeCameras model.cameras
        ::Progress.showItem "Writing Events"
        this.writeEvents model.events
        ::Progress.showItem "Writing Collision Shapes"
        this.writeCollisionShapes model.CollisionShapes
        -- Reforged v1200: CORN, FAFX, BPOS
        if this.version == 1200 then
        (
            ::Progress.showItem "Writing Corn Emitters"
            this.writeCornEmitters model.cornEmitters
            ::Progress.showItem "Writing FaceFX Targets"
            this.writeFaceFX model.faceFxTargets
            ::Progress.showItem "Writing Bind Poses"
            this.writeBindPoses model.bindPoses
        )
        ::Progress.upLevel()
    ),
    
    -- ============================================================================
    -- PUBLIC ENTRY POINT
    -- ============================================================================
    
    fn OutputMDX model outfile_name =
    (
        gc()
        local foutstream = fopen outfile_name "wb"
        ::IOManager.SetOutStream foutstream
        try
        (
            local size = ::IOManager.posStamp()
            this.writeMDXFile model
            size = size - (::IOManager.posStamp())
        )
        catch
        (
            format "*** MDX Write Error: % ***\n" (getCurrentException()) to:listener
        )
        fclose foutstream
        gc()
    )
)

-- ============================================================================
-- GLOBAL INSTANCE (for backward compatibility)
-- ============================================================================
global MDXWriter = WhiteoutDexMDXWriterStruct()
