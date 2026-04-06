/*
    WhiteoutDexMDLWriter.ms
    
    Refactored as Struct for better encapsulation and maintainability.
    Handles text-based MDL file output for Warcraft 3 models.
    
    Usage:
        global MDLWriter = WhiteoutDexMDLWriterStruct()
        MDLWriter.OutputMDL model "C:\\path\\to\\file.mdl"
*/

-- ============================================================================
-- MAIN MDL WRITER STRUCT
-- ============================================================================

struct WhiteoutDexMDLWriterStruct
(
    public version = 800,
    public helpersList = #(),
    -- ============================================================================
    -- ANIMATION HELPER FUNCTIONS
    -- ============================================================================
    
    fn writeAnimation label data &transformationFunc =
    (
        local keys = data.keys
        local isSmooth = ::IOManager.isSmoothAnim data.type
        ::IOManager.writeMDLSection label keys.count
        ::IOManager.writeMDLFlag (::IOManager.ControlToString data.type)
        if data.globalSequenceId != undefined then ::IOManager.writeMDLValue "GlobalSeqId" (data.globalSequenceId - 1)
        if isSmooth then
        (
            for k in keys do
            (
                ::IOManager.writeMDLKey (::Utils.F2WS k.time) (transformationFunc k.value)
                ::IOManager.newIOLevel()
                ::IOManager.writeMDLValue "InTan" (transformationFunc k.intan)
                ::IOManager.writeMDLValue "OutTan" (transformationFunc k.outtan)
                ::IOManager.CloseIOLevel()
            )
        )
        else
            for k in keys do ::IOManager.writeMDLKey (::Utils.F2WS k.time) (transformationFunc k.value)
        ::IOManager.writeMDLSectionEnd()
    ),
    
    fn writeFloatParameter label val =
    (
        if (val.type == #Static) then
            ::IOManager.writeMDLValue ("static " + label) val.data
        else
            this.writeAnimation label val.data ::IOManager.floatToString
    ),
    
    fn writeColorParameter label val =
    (
        if (val.type == #Static) then
            ::IOManager.writeMDLValue ("static " + label) (::IOManager.ColorToString2 val.data)
        else
            this.writeAnimation label val.data ::IOManager.ColorToString
    ),
    
    fn writeTransformations trans =
    (
        if trans.translation != undefined and trans.translation.keys.count > 0 then
            this.writeAnimation "Translation" trans.translation ::IOManager.PositionToString
        if trans.rotation != undefined and trans.rotation.keys.count > 0 then
            this.writeAnimation "Rotation" trans.rotation ::IOManager.QuatToString
        if trans.scale != undefined and trans.scale.keys.count > 0 then
            this.writeAnimation "Scaling" trans.scale ::IOManager.ScaleToString
    ),
    
    -- ============================================================================
    -- HEADER
    -- ============================================================================
    
    fn writeHeader model =
    (
        -- Version
        ::IOManager.writeMDLSection "Version" ""
        ::IOManager.writeMDLValue "FormatVersion" model.version
        ::IOManager.writeMDLSectionEnd()
        
        -- Model
        ::IOManager.writeMDLSection "Model" (::IOManager.StringWithColomms model.name)
        if (model.geosets.count != 0) then ::IOManager.writeMDLValue "NumGeosets" model.geosets.count
        if (model.geosetAnims.count != 0) then ::IOManager.writeMDLValue "NumGeosetAnims" model.geosetAnims.count
        if (model.helpers.count != 0) then ::IOManager.writeMDLValue "NumHelpers" model.helpers.count
        if (model.lights.count != 0) then ::IOManager.writeMDLValue "NumLights" model.lights.count
        if (model.bones.count != 0) then ::IOManager.writeMDLValue "NumBones" model.bones.count
        if (model.attachments.count != 0) then ::IOManager.writeMDLValue "NumAttachments" model.attachments.count
        if (model.particleEmitters1.count != 0) then ::IOManager.writeMDLValue "NumParticleEmitters" model.particleEmitters1.count
        if (model.particleEmitters2.count != 0) then ::IOManager.writeMDLValue "NumParticleEmitters2" model.particleEmitters2.count
        if (model.ribbonEmitters.count != 0) then ::IOManager.writeMDLValue "NumRibbonEmitters" model.ribbonEmitters.count
        -- Reforged: Popcorn Emitters
        if model.cornEmitters != undefined and model.cornEmitters.count != 0 then ::IOManager.writeMDLValue "NumParticleEmittersPopcorn" model.cornEmitters.count
        if (model.events.count != 0) then ::IOManager.writeMDLValue "NumEvents" model.events.count
        if (model.CollisionShapes.count != 0) then ::IOManager.writeMDLValue "NumCollisionShapes" model.CollisionShapes.count
        ::IOManager.writeMDLValue "BlendTime" 150
        if ::IOManager.checkExtents model.extents then ::IOManager.writeMDLExtents model.extents
        ::IOManager.writeMDLSectionEnd()
    ),
    
    -- ============================================================================
    -- SEQUENCES
    -- ============================================================================
    
    fn writeSequences seq =
    (
        ::IOManager.writeMDLSection "Sequences" seq.count
        for s in seq do
        (
            ::IOManager.writeMDLSection "Anim" (::IOManager.StringWithColomms s.name)
            ::IOManager.writeMDLValue "Interval" (::IOManager.PairToString (::Utils.F2WS s.startTime) (::Utils.F2WS s.endTime))
            if s.loop then ::IOManager.writeMDLFlag "NonLooping"
            if s.rarity != undefined then ::IOManager.writeMDLValue "Rarity" s.rarity
            if s.movespeed != undefined then ::IOManager.writeMDLValue "MoveSpeed" s.movespeed
            if ::IOManager.checkExtents s.extents then ::IOManager.writeMDLExtents s.extents
            ::IOManager.writeMDLSectionEnd()
        )
        ::IOManager.writeMDLSectionEnd()
    ),
    
    -- ============================================================================
    -- GLOBAL SEQUENCES
    -- ============================================================================
    
    fn writeGlobalSequences seq =
    (
        if (seq.count > 0) then
        (
            ::IOManager.writeMDLSection "GlobalSequences" seq.count
            for s in seq do ::IOManager.writeMDLValue "Duration" (::Utils.F2WS s)
            ::IOManager.writeMDLSectionEnd()
        )
    ),
    
    -- ============================================================================
    -- TEXTURES
    -- ============================================================================
    
    fn writeTextures tex =
    (
        if (tex.count > 0) then
        (
            ::IOManager.writeMDLSection "Textures" tex.count
            for t in tex do
            (
                ::IOManager.writeMDLSection "Bitmap" ""
                -- Texturpfad immer schreiben (auch fuer replaceable Texturen)
                ::IOManager.writeMDLValue "Image" (::IOManager.StringWithColomms t.path)
                if t.replaceableId > 0 then
                    ::IOManager.writeMDLValue "ReplaceableId" t.replaceableId
                if t.wrapWidth then ::IOManager.writeMDLFlag "WrapWidth"
                if t.wrapHeight then ::IOManager.writeMDLFlag "WrapHeight"
                ::IOManager.writeMDLSectionEnd()
            )
            ::IOManager.writeMDLSectionEnd()
        )
    ),
    
    -- ============================================================================
    -- MATERIALS
    -- ============================================================================
    
    fn writeMaterials materials =
    (
        if (materials.count > 0) then
        (
            ::IOManager.writeMDLSection "Materials" materials.count
            for m in materials do
            (
                ::IOManager.writeMDLSection "Material" ""
                
                -- Material-Level Flags (vom ersten Layer holen)
                if m.layers.count > 0 then
                (
                    local firstLayer = m.layers[1]
                    
                    if firstLayer.ConstantColor != undefined and firstLayer.ConstantColor then
                        ::IOManager.writeMDLFlag "ConstantColor"
                    if firstLayer.SortPrimsNearZ != undefined and firstLayer.SortPrimsNearZ then
                        ::IOManager.writeMDLFlag "SortPrimsNearZ"
                    if firstLayer.SortPrimsFarZ != undefined and firstLayer.SortPrimsFarZ then
                        ::IOManager.writeMDLFlag "SortPrimsFarZ"
                    if firstLayer.FullResolution != undefined and firstLayer.FullResolution then
                        ::IOManager.writeMDLFlag "FullResolution"
                    if firstLayer.PriorityPlane != undefined and firstLayer.PriorityPlane != 0 then
                        ::IOManager.writeMDLValue "PriorityPlane" firstLayer.PriorityPlane
                )
                
                for l in m.layers do
                (
                    ::IOManager.writeMDLSection "Layer" ""
                    ::IOManager.writeMDLValue "FilterMode" (::IOManager.FilterModeToString l.filtermode)
                    if l.unshaded then ::IOManager.writeMDLFlag "Unshaded"
                    if l.SphereEnvMap != undefined and l.SphereEnvMap then ::IOManager.writeMDLFlag "SphereEnvMap"
                    if l.twosided then ::IOManager.writeMDLFlag "TwoSided"
                    if l.unfogged then ::IOManager.writeMDLFlag "Unfogged"
                    if l.nodephtest then ::IOManager.writeMDLFlag "NoDepthTest"
                    if l.nodephset then ::IOManager.writeMDLFlag "NoDepthSet"
                    
                    -- Reforged v1200: ShaderTypeId vor TextureID
                    if this.version == 1200 then
                        ::IOManager.writeMDLValue "ShaderTypeId" 1
                    
                    this.writeFloatParameter "TextureID" l.textures
                    
                    -- Reforged v1200: PBR Texture Bindings (mit "static" Prefix)
                    if this.version == 1200 then
                    (
                        if l.normalTexId != undefined then
                            ::IOManager.writeMDLValue "static NormalTextureID" l.normalTexId
                        if l.ormTexId != undefined then
                            ::IOManager.writeMDLValue "static ORMTextureID" l.ormTexId
                        if l.emissiveTexId != undefined then
                            ::IOManager.writeMDLValue "static EmissiveTextureID" l.emissiveTexId
                        if l.teamColorTexId != undefined then
                            ::IOManager.writeMDLValue "static TeamColorTextureID" l.teamColorTexId
                        if l.envMapTexId != undefined then
                            ::IOManager.writeMDLValue "static ReflectionsTextureID" l.envMapTexId
                    )
                    
                    if l.coordId != 0 then ::IOManager.writeMDLValue "CoordId" l.coordId
                    if l.tvertexid != undefined then ::IOManager.writeMDLValue "TVertexAnimId" (l.tvertexid.id)
                    this.writeFloatParameter "Alpha" (::IOManager.MaxMaterialAlphaToWc3Alpha l.alpha)
                    
                    -- Reforged v1200: Fresnel-Daten nach Alpha
                    if this.version == 1200 then
                    (
                        if l.emissiveGain != undefined and l.emissiveGain != 1.0 then
                            ::IOManager.writeMDLValue "EmissiveGain" l.emissiveGain
                        if l.fresnelColor != undefined then
                            ::IOManager.writeMDLValue "FresnelColor" ("{ " + l.fresnelColor.x as string + ", " + l.fresnelColor.y as string + ", " + l.fresnelColor.z as string + " }")
                        if l.fresnelOpacity != undefined and l.fresnelOpacity != 0.0 then
                            ::IOManager.writeMDLValue "FresnelOpacity" l.fresnelOpacity
                        if l.fresnelTeamColor != undefined and l.fresnelTeamColor != 0.0 then
                            ::IOManager.writeMDLValue "FresnelTeamColor" l.fresnelTeamColor
                    )
                    
                    ::IOManager.writeMDLSectionEnd()
                )
                ::IOManager.writeMDLSectionEnd()
            )
            ::IOManager.writeMDLSectionEnd()
        )
    ),
    
    -- ============================================================================
    -- TEXTURE ANIMATIONS
    -- ============================================================================
    
    fn writeTextureAnims tvanims =
    (
        if (tvanims.count != 0) then
        (
            ::IOManager.writeMDLSection "TextureAnims" tvanims.count
            for tv in tvanims do
            (
                ::IOManager.writeMDLSection "TVertexAnim" ""
                this.writeTransformations tv.transformation
                ::IOManager.writeMDLSectionEnd()
            )
            ::IOManager.writeMDLSectionEnd()
        )
    ),
    
    -- ============================================================================
    -- GEOSETS
    -- ============================================================================
    
    fn writeGeosets meshes =
    (
        if (meshes.count != 0) then
        (
            for m in meshes do
            (
                ::IOManager.writeMDLSection "Geoset" ""
                
                -- Vertices
                ::IOManager.writeMDLSection "Vertices" m.vertices.count
                for v in m.vertices do
                    ::IOManager.writeMDLVertex v
                ::IOManager.writeMDLSectionEnd()
                
                -- Normals
                ::IOManager.writeMDLSection "Normals" m.normals.count
                for v in m.normals do
                    ::IOManager.writeMDLVertex v
                ::IOManager.writeMDLSectionEnd()
                
                -- TVertices
                for g in m.tvertices do
                (
                    ::IOManager.writeMDLSection "TVertices" g.count
                    for v in g do
                        ::IOManager.writeMDLTVertex v
                    ::IOManager.writeMDLSectionEnd()
                )
                
                -- VertexGroup (leer bei v1200 SKIN-Modellen, aber Section muss vorhanden sein)
                ::IOManager.writeMDLSection "VertexGroup" ""
                if not (this.version == 1200 and m.skinData != undefined and m.skinData.count > 0) then
                    for v in m.matrixindices do
                        ::IOManager.writeMDLSingleValue v
                ::IOManager.writeMDLSectionEnd()
                
                -- Reforged v1200: Tangents (nach VertexGroup, vor Faces)
                if this.version == 1200 and m.tangents != undefined and m.tangents.count > 0 then
                (
                    ::IOManager.writeMDLSection "Tangents" m.tangents.count
                    for t in m.tangents do
                    (
                        if t != undefined then
                            ::IOManager.writeMDLFlag ("{ " + (-t[2]) as string + ", " + t[1] as string + ", " + t[3] as string + ", " + t[4] as string + " }")
                        else
                            ::IOManager.writeMDLFlag "{ 0, 1, 0, 1 }"
                    )
                    ::IOManager.writeMDLSectionEnd()
                )
                
                -- Reforged v1200: SkinWeights (nach Tangents, vor Faces)
                if this.version == 1200 and m.skinData != undefined and m.skinData.count > 0 then
                (
                    ::IOManager.writeMDLSection "SkinWeights" m.skinData.count
                    for sv in m.skinData do
                    (
                        if sv != undefined then
                            ::IOManager.writeMDLFlag ("{ " + sv[1] as string + ", " + sv[2] as string + ", " + sv[3] as string + ", " + sv[4] as string + ", " + sv[5] as string + ", " + sv[6] as string + ", " + sv[7] as string + ", " + sv[8] as string + " }")
                        else
                            ::IOManager.writeMDLFlag "{ 0, 0, 0, 0, 255, 0, 0, 0 }"
                    )
                    ::IOManager.writeMDLSectionEnd()
                )
                
                -- Faces
                ::IOManager.writeMDLSection "Faces 1" (m.faces.count * 3)
                ::IOManager.writeMDLSection "Triangles" ""
                ::IOManager.writeMDLFlag (::IOManager.TrianglesToString m.faces)
                ::IOManager.writeMDLSectionEnd()
                ::IOManager.writeMDLSectionEnd()
                
                -- Groups
                local tmp = ::Utils.ArrayFold m.matrices (fn tmp2 a b = a + b.count) 0
                ::IOManager.writeMDLSection "Groups" ((m.matrices.count as string) + " " + (tmp as string))
                for g in m.matrices do
                    ::IOManager.writeMDLValue "Matrices" (::IOManager.ArrayToString g (fn tmp3 a = (a as string)))
                ::IOManager.writeMDLSectionEnd()
                
                -- Extents
                ::IOManager.writeMDLExtents m.mainExtents
                
                -- Animation Extents
                if (m.extents.count != 0) then
                (
                    for n in m.extents do
                    (
                        ::IOManager.writeMDLSection "Anim" ""
                        ::IOManager.writeMDLExtents n
                        ::IOManager.writeMDLSectionEnd()
                    )
                )
                
                -- Material
                if m.material != undefined then ::IOManager.writeMDLValue "MaterialID" m.material.id else ::IOManager.writeMDLValue "MaterialID" 0
                
                -- Selection
                ::IOManager.writeMDLValue "SelectionGroup" 0
                if m.unselectable then
                    ::IOManager.writeMDLFlag "Unselectable"
                
                -- Reforged v1200: LOD + Name (nach SelectionGroup)
                if this.version == 1200 then
                (
                    ::IOManager.writeMDLValue "LevelOfDetail" (if m.lod != undefined then m.lod else 0)
                    if m.lodName != undefined and m.lodName != "" then
                        ::IOManager.writeMDLValue "Name" (::IOManager.StringWithColomms m.lodName)
                    else
                        ::IOManager.writeMDLValue "Name" (::IOManager.StringWithColomms "")
                )
                
                ::IOManager.writeMDLSectionEnd()
            )
        )
    ),
    
    -- ============================================================================
    -- GEOSET ANIMATIONS
    -- ============================================================================
    
    fn writeGeosetAnims geoAnims =
    (
        if (geoAnims.count != 0) then
        (
            for g in geoAnims do
            (
                ::IOManager.writeMDLSection "GeosetAnim" ""
                if g.dropShadow == true then ::IOManager.writeMDLFlag "DropShadow"
                if g.alpha != undefined then this.writeFloatParameter "Alpha" g.alpha
                if g.color != undefined then this.writeColorParameter "Color" g.color
                ::IOManager.writeMDLValue "GeosetId" g.geoset.id
                ::IOManager.writeMDLSectionEnd()
            )
        )
    ),
    
    -- ============================================================================
    -- MDL OBJECT (shared properties for nodes)
    -- ============================================================================
    
    fn writeMDLObj obj =
    (
        ::IOManager.writeMDLValue "ObjectId" obj.objid
        if obj.parent != undefined then ::IOManager.writeMDLValue "Parent" obj.parent.objid
        if obj.billboarded then ::IOManager.writeMDLFlag "Billboarded"
        if obj.billboardedLockX then ::IOManager.writeMDLFlag "BillboardedLockX"
        if obj.billboardedLockY then ::IOManager.writeMDLFlag "BillboardedLockY"
        if obj.billboardedLockZ then ::IOManager.writeMDLFlag "BillboardedLockZ"
        if obj.cameraAnchored then ::IOManager.writeMDLFlag "CameraAnchored"
        local tmp = #()
        if obj.dontInheritTranslation then append tmp "Translation"
        if obj.dontInheritRotation then append tmp "Rotation"
        if obj.dontInheritScale then append tmp "Scaling"
        if tmp.count != 0 then ::IOManager.writeMDLValue "DontInherit" (::IOManager.ArrayToString tmp (fn tmp2 a = a))
    ),
    
    -- ============================================================================
    -- BONES
    -- ============================================================================
    
    fn writeBones bones =
    (
        if (bones.count != 0) then
        (
            for b in bones do
            (
                ::IOManager.writeMDLSection "Bone" (::IOManager.StringWithColomms b.name)
                
                this.writeMDLObj b
                
                if (b.sub.geosets.count == 1) then
                (
                    local g = b.sub.geosets[1]
                    ::IOManager.writeMDLValue "GeosetId" g.id
                    if g.geosetAnimId != undefined then 
                        ::IOManager.writeMDLValue "GeosetAnimId" g.geosetAnimId
                    else
                        ::IOManager.writeMDLValue "GeosetAnimId" "None"
                )
                else
                (
                    ::IOManager.writeMDLValue "GeosetId" "Multiple"
                    ::IOManager.writeMDLValue "GeosetAnimId" "None"
                )
                
                this.writeTransformations b.transformations
                ::IOManager.writeMDLSectionEnd()
            )
        )
        
        -- Reforged v1200: Helpers als Bones schreiben (wie im MDX BONE-Chunk)
        if this.version == 1200 and this.helpersList != undefined and this.helpersList.count > 0 then
        (
            for h in this.helpersList do
            (
                ::IOManager.writeMDLSection "Bone" (::IOManager.StringWithColomms h.name)
                this.writeMDLObj h
                ::IOManager.writeMDLValue "GeosetId" "None"
                ::IOManager.writeMDLValue "GeosetAnimId" "None"
                this.writeTransformations h.transformations
                ::IOManager.writeMDLSectionEnd()
            )
        )
    ),
    
    -- ============================================================================
    -- LIGHTS
    -- ============================================================================
    
    fn writeLights lights =
    (
        if (lights.count != 0) then
        (
            for l in lights do
            (
                ::IOManager.writeMDLSection "Light" (::IOManager.StringWithColomms l.name)
                this.writeMDLObj l
                local sub = l.sub
                case (sub.lightType) of
                (
                    #Omnidirectional: ::IOManager.writeMDLFlag "Omnidirectional"
                    #Directional: ::IOManager.writeMDLFlag "Directional"
                    default: ::IOManager.writeMDLFlag "Ambient"
                )
                this.writeFloatParameter "AttenuationStart" sub.attenuationStart
                this.writeFloatParameter "AttenuationEnd" sub.attenuationEnd
                this.writeFloatParameter "Intensity" sub.intensity
                this.writeColorParameter "Color" sub.color
                if (sub.ambIntensity != undefined) then this.writeFloatParameter "AmbIntensity" sub.ambIntensity
                if (sub.ambColor != undefined) then this.writeColorParameter "AmbColor" sub.ambColor
                if sub.visibility != undefined then this.writeFloatParameter "Visibility" sub.visibility
                this.writeTransformations l.transformations
                ::IOManager.writeMDLSectionEnd()
            )
        )
    ),
    
    -- ============================================================================
    -- HELPERS
    -- ============================================================================
    
    fn writeHelpers helpers =
    (
        -- Reforged v1200: Helpers sind bereits in writeBones als Bones geschrieben
        if this.version == 1200 then return()
        if (helpers.count != 0) then
        (
            for h in helpers do
            (
                ::IOManager.writeMDLSection "Helper" (::IOManager.StringWithColomms h.name)
                this.writeMDLObj h
                this.writeTransformations h.transformations
                ::IOManager.writeMDLSectionEnd()
            )
        )
    ),
    
    -- ============================================================================
    -- ATTACHMENTS
    -- ============================================================================
    
    fn writeAttachments attachments =
    (
        if (attachments.count != 0) then
        (
            for a in attachments do
            (
                local sub = a.sub
                
                local savedName = a.name
                a.name = sub.name
                
                ::IOManager.writeMDLSection "Attachment" (::IOManager.StringWithColomms sub.name)
                
                this.writeMDLObj a
                
                a.name = savedName
                
                if sub.attachmentId != undefined then ::IOManager.writeMDLValue "AttachmentID" sub.attachmentId
                
                if sub.outPath != undefined and sub.outPath != "" then ::IOManager.writeMDLValue "Path" (::IOManager.StringWithColomms sub.outPath)
                
                this.writeTransformations a.transformations
                
                if sub.visibility != undefined then this.writeFloatParameter "Visibility" sub.visibility
                
                ::IOManager.writeMDLSectionEnd()
            )
        )
    ),
    
    -- ============================================================================
    -- PIVOT POINTS
    -- ============================================================================
    
    fn writePivotPoints model =
    (
        local pv = model.bones.count + \
            model.lights.count + \
            model.helpers.count + \
            model.attachments.count + \
            model.particleEmitters1.count + \
            model.particleEmitters2.count + \
            model.ribbonEmitters.count + \
            model.events.count + \
            model.CollisionShapes.count
        -- Reforged: Corn Emitter Pivots
        if model.cornEmitters != undefined then pv += model.cornEmitters.count
        
        if pv != 0 then
        (
            ::IOManager.writeMDLSection "PivotPoints" pv
            for o in model.bones do ::IOManager.writeMDLPoint (::IOManager.PositionToString o.pivot)
            for o in model.lights do ::IOManager.writeMDLPoint (::IOManager.PositionToString o.pivot)
            for o in model.helpers do ::IOManager.writeMDLPoint (::IOManager.PositionToString o.pivot)
            for o in model.attachments do ::IOManager.writeMDLPoint (::IOManager.PositionToString o.pivot)
            for o in model.particleEmitters1 do ::IOManager.writeMDLPoint (::IOManager.PositionToString o.pivot)
            for o in model.particleEmitters2 do ::IOManager.writeMDLPoint (::IOManager.PositionToString o.pivot)
            for o in model.ribbonEmitters do ::IOManager.writeMDLPoint (::IOManager.PositionToString o.pivot)
            for o in model.events do ::IOManager.writeMDLPoint (::IOManager.PositionToString o.pivot)
            for o in model.CollisionShapes do ::IOManager.writeMDLPoint (::IOManager.PositionToString o.pivot)
            -- Reforged: Corn Emitter Pivots
            if model.cornEmitters != undefined then
                for o in model.cornEmitters do ::IOManager.writeMDLPoint (::IOManager.PositionToString o.pivot)
            ::IOManager.writeMDLSectionEnd()
        )
    ),
    
    -- ============================================================================
    -- PARTICLE EMITTERS (Type 1)
    -- ============================================================================
    
    fn writeParticleEmitters particleEmitters =
    (
        if (particleEmitters.count != 0) then
        (
            for p in particleEmitters do
            (
                ::IOManager.writeMDLSection "ParticleEmitter" (::IOManager.StringWithColomms p.name)
                this.writeMDLObj p
                local sub = p.sub
                
                this.writeFloatParameter "EmissionRate" sub.emissionRate
                this.writeFloatParameter "Gravity" sub.gravity
                this.writeFloatParameter "Longitude" sub.longitude
                this.writeFloatParameter "Latitude" sub.latitude
                
                if sub.visibility != undefined then this.writeFloatParameter "Visibility" sub.visibility
                
                ::IOManager.writeMDLSection "Particle" ""
                this.writeFloatParameter "LifeSpan" sub.lifespan
                this.writeFloatParameter "InitVelocity" sub.initVelocity
                ::IOManager.writeMDLValue "Path" (::IOManager.StringWithColomms sub.path)
                ::IOManager.writeMDLSectionEnd()
                
                this.writeTransformations p.transformations
                ::IOManager.writeMDLSectionEnd()
            )
        )
    ),
    
    -- ============================================================================
    -- PARTICLE EMITTERS 2
    -- ============================================================================
    
    fn writeParticleEmitters2 particleEmitters2 =
    (
        if (particleEmitters2.count != 0) then
        (
            for p in particleEmitters2 do
            (
                ::IOManager.writeMDLSection "ParticleEmitter2" (::IOManager.StringWithColomms p.name)
                this.writeMDLObj p
                local sub = p.sub
                if sub.sortPrimsFarZ then ::IOManager.writeMDLFlag "SortPrimsFarZ"
                if sub.unshaded then ::IOManager.writeMDLFlag "Unshaded"
                if (sub.lineEmitter == 2) then ::IOManager.writeMDLFlag "LineEmitter"
                if sub.unfogged then ::IOManager.writeMDLFlag "Unfogged"
                if sub.modelSpace then ::IOManager.writeMDLFlag "ModelSpace"
                if sub.xyQuad then ::IOManager.writeMDLFlag "XYQuad"
                this.writeFloatParameter "Speed" sub.speed
                this.writeFloatParameter "Variation" sub.variation
                this.writeFloatParameter "Latitude" sub.latitude
                this.writeFloatParameter "Gravity" sub.gravity
                if sub.visibility != undefined then this.writeFloatParameter "Visibility" sub.visibility
                if sub.squirt then ::IOManager.writeMDLFlag "Squirt"
                ::IOManager.writeMDLValue "LifeSpan" sub.lifeSpan
                this.writeFloatParameter "EmissionRate" sub.emissionRate
                this.writeFloatParameter "Width" sub.width
                this.writeFloatParameter "Length" sub.length
                ::IOManager.writeMDLFlag (::IOManager.ParticleFiltermodeToString sub.filtermode)
                ::IOManager.writeMDLValue "Rows" sub.rows
                ::IOManager.writeMDLValue "Columns" sub.columns
                ::IOManager.writeMDLFlag (::IOManager.ParticleTypeToString sub.particleType)
                ::IOManager.writeMDLValue "TailLength" sub.tailLength
                ::IOManager.writeMDLValue "Time" sub.time
                ::IOManager.writeMDLSection "SegmentColor" ""
                ::IOManager.writeMDLValue "Color" (::IOManager.ColorToString sub.startColor)
                ::IOManager.writeMDLValue "Color" (::IOManager.ColorToString sub.midColor)
                ::IOManager.writeMDLValue "Color" (::IOManager.ColorToString sub.endColor)
                
                ::IOManager.CloseIOLevel()
                ::IOManager.writeMDLFlag "}"
                
                ::IOManager.writeMDLValue "Alpha" (::IOManager.IntegerPoint3ToString sub.alpha)
                ::IOManager.writeMDLValue "ParticleScaling" (::IOManager.Point3ToString sub.scaling)
                ::IOManager.writeMDLValue "LifeSpanUVAnim" (::IOManager.IntegerPoint3ToString sub.lifeSpanUVAnim)
                ::IOManager.writeMDLValue "DecayUVAnim" (::IOManager.IntegerPoint3ToString sub.decayUVAnim)
                ::IOManager.writeMDLValue "TailUVAnim" (::IOManager.IntegerPoint3ToString sub.TailUVAnim)
                ::IOManager.writeMDLValue "TailDecayUVAnim" (::IOManager.IntegerPoint3ToString sub.TailDecayUVAnim)
                ::IOManager.writeMDLValue "TextureID" sub.texture.id
                if (sub.replaceableId != 1) then ::IOManager.writeMDLValue "ReplaceableId" (sub.replaceableId - 1)
                if (sub.priorityPlane != 0) then ::IOManager.writeMDLValue "PriorityPlane" sub.priorityPlane
                this.writeTransformations p.transformations
                ::IOManager.writeMDLSectionEnd()
            )
        )
    ),
    
    -- ============================================================================
    -- RIBBON EMITTERS
    -- ============================================================================
    
    fn writeRibbonEmitters ribbonEmitters =
    (
        if (ribbonEmitters.count != 0) then
        (
            for r in ribbonEmitters do
            (
                ::IOManager.writeMDLSection "RibbonEmitter" (::IOManager.StringWithColomms r.name)
                
                this.writeMDLObj r
                
                local sub = r.sub
                
                this.writeFloatParameter "HeightAbove" sub.heightAbove
                this.writeFloatParameter "HeightBelow" sub.heightBelow
                this.writeFloatParameter "Alpha" sub.alpha
                
                this.writeColorParameter "Color" sub.color
                
                ::IOManager.writerTransformKeys sub.textureSlot (fn tmp a = (a as integer))
                
                if sub.textureSlot.type != #Static then
                    sub.textureSlot.data.type = #NonInterp
                
                this.writeFloatParameter "TextureSlot" sub.textureSlot
                
                if sub.visibility != undefined then this.writeFloatParameter "Visibility" sub.visibility
                
                ::IOManager.writeMDLValue "EmissionRate" sub.emissionRate
                ::IOManager.writeMDLValue "LifeSpan" sub.lifeSpan
                ::IOManager.writeMDLValue "Gravity" sub.gravity
                
                ::IOManager.writeMDLValue "Rows" sub.rows
                ::IOManager.writeMDLValue "Columns" sub.columns
                
                if sub.material != undefined then ::IOManager.writeMDLValue "MaterialID" sub.material.id else ::IOManager.writeMDLValue "MaterialID" 0
                
                this.writeTransformations r.transformations
                
                ::IOManager.writeMDLSectionEnd()
            )
        )
    ),
    
    -- ============================================================================
    -- CAMERAS
    -- ============================================================================
    
    fn writeCameras cameras =
    (
        if (cameras.count != 0) then
        (
            for c in cameras do
            (
                ::IOManager.writeMDLSection "Camera" (::IOManager.StringWithColomms c.name)
                
                ::IOManager.writeMDLValue "Position" (::IOManager.PositionToString c.pivot)
                
                this.writeTransformations c.transformations
                
                ::IOManager.writeMDLValue "FieldOfView" (degToRad c.fieldOfView)
                
                ::IOManager.writeMDLValue "FarClip" c.farClip
                ::IOManager.writeMDLValue "NearClip" c.nearClip
                
                if c.rotation != undefined then
                    this.writeFloatParameter "Rotation" c.rotation
                
                ::IOManager.writeMDLSection "Target" ""
                
                ::IOManager.writeMDLValue "Position" (::IOManager.PositionToString c.targetPos)
                
                this.writeTransformations c.targetTransformations
                ::IOManager.writeMDLSectionEnd()
                ::IOManager.writeMDLSectionEnd()
            )
        )
    ),
    
    -- ============================================================================
    -- EVENTS
    -- ============================================================================
    
    fn writeEvents events =
    (
        if (events.count != 0) then
        (
            for o in events do
            (
                local tmp = substring o.name 5 o.name.count
                
                ::IOManager.writeMDLSection "EventObject" (::IOManager.StringWithColomms tmp)
                
                this.writeMDLObj o
                
                if (o.sub.eventTrack != undefined and o.sub.eventTrack.count != 0) then
                (
                    ::IOManager.writeMDLSection "EventTrack" o.sub.eventTrack.count
                    for n in o.sub.eventTrack do
                        ::IOManager.writeMDLFlag ((::Utils.F2WS n) as string)
                    ::IOManager.writeMDLSectionEnd()
                )
                
                this.writeTransformations o.transformations
                
                ::IOManager.writeMDLSectionEnd()
            )
        )
    ),
    
    -- ============================================================================
    -- COLLISION SHAPES
    -- ============================================================================
    
    fn writeCollisionShapes shapes =
    (
        if (shapes.count != 0) then
        (
            for s in shapes do
            (
                ::IOManager.writeMDLSection "CollisionShape" (::IOManager.StringWithColomms s.name)
                this.writeMDLObj s
                local sub = s.sub
                
                case (sub.shape) of
                (
                    #Box: ::IOManager.writeMDLFlag "Box"
                    #Sphere: ::IOManager.writeMDLFlag "Sphere"
                    default: ::IOManager.writeMDLFlag "Box"
                )
                
                ::IOManager.writeMDLSection "Vertices" sub.vertices.count
                for v in sub.vertices do
                    ::IOManager.writeMDLFlag (::IOManager.CollisionVertexToString v)
                ::IOManager.writeMDLSectionEnd()
                
                if sub.boundsRadius != undefined then ::IOManager.writeMDLValue "BoundsRadius" sub.boundsRadius
                
                this.writeTransformations s.transformations
                ::IOManager.writeMDLSectionEnd()
            )
        )
    ),

    -- ============================================================================
    -- FACEFX TARGETS (Reforged)
    -- ============================================================================
    
    fn writeFaceFX faceFxTargets =
    (
        if faceFxTargets != undefined and faceFxTargets.count > 0 then
        (
            for fx in faceFxTargets do
            (
                ::IOManager.writeMDLSection "FaceFX" (::IOManager.StringWithColomms (if fx.name != undefined then fx.name else ""))
                if fx.path != undefined and fx.path != "" then
                    ::IOManager.writeMDLValue "Path" (::IOManager.StringWithColomms fx.path)
                ::IOManager.writeMDLSectionEnd()
            )
        )
    ),
    
    -- ============================================================================
    -- CORN EMITTERS (Reforged)
    -- ============================================================================
    
    fn writeCornEmitters cornEmitters =
    (
        if cornEmitters != undefined and cornEmitters.count > 0 then
        (
            for c in cornEmitters do
            (
                ::IOManager.writeMDLSection "ParticleEmitterPopcorn" (::IOManager.StringWithColomms c.name)
                
                this.writeMDLObj c
                
                local sub = c.sub
                
                -- LifeSpan (immer statisch)
                ::IOManager.writeMDLValue "static LifeSpan" (if sub.lifeSpan != undefined then sub.lifeSpan else 0.0)
                
                -- EmissionRate (statisch oder animiert)
                if sub.emissionRateAnim != undefined and sub.emissionRateAnim.type == #Animated then
                    this.writeAnimation "EmissionRate" sub.emissionRateAnim.data ::IOManager.floatToString
                else
                    ::IOManager.writeMDLValue "static EmissionRate" (if sub.emissionRate != undefined then sub.emissionRate else 0.0)
                
                -- Speed (immer statisch)
                ::IOManager.writeMDLValue "static Speed" (if sub.speed != undefined then sub.speed else 0.0)
                
                -- Visibility
                if sub.visibility != undefined and sub.visibility.type == #Animated then
                    this.writeFloatParameter "Visibility" sub.visibility
                
                -- Color (nur wenn nicht Default [1,1,1])
                if sub.color != undefined then
                (
                    local col = sub.color
                    if not (col.x == 1.0 and col.y == 1.0 and col.z == 1.0) then
                    (
                        local cr = (col.x * 255.0) as integer
                        local cg = (col.y * 255.0) as integer
                        local cb = (col.z * 255.0) as integer
                        ::IOManager.writeMDLValue "static Color" ("{ " + cr as string + ", " + cg as string + ", " + cb as string + " }")
                    )
                )
                
                -- Alpha (animiert oder statisch)
                if sub.alphaAnim != undefined and sub.alphaAnim.type == #Animated then
                    this.writeAnimation "Alpha" sub.alphaAnim.data ::IOManager.floatToString
                else
                    ::IOManager.writeMDLValue "static Alpha" (if sub.alpha != undefined then sub.alpha else 1.0)
                
                -- ReplaceableId (nur wenn gesetzt)
                if sub.replaceableId != undefined and sub.replaceableId > 0 then
                    ::IOManager.writeMDLValue "ReplaceableId" sub.replaceableId
                
                -- Path
                if sub.path != undefined and sub.path != "" then
                    ::IOManager.writeMDLValue "Path" (::IOManager.StringWithColomms sub.path)
                
                -- AnimVisibilityGuide (Flags)
                if sub.flags != undefined then
                    ::IOManager.writeMDLValue "AnimVisibilityGuide" (::IOManager.StringWithColomms sub.flags)
                
                -- Transformations (Rotation, Scaling, Translation)
                this.writeTransformations c.transformations
                
                ::IOManager.writeMDLSectionEnd()
            )
        )
    ),

    -- ============================================================================
    -- MAIN WRITE FUNCTION
    -- ============================================================================
    
    fn writeMDLFile model =
    (
        ::Progress.downLevel 100 19
        
        ::IOManager.resetLevel()
        
        -- Version merken fuer v1200-Checks in Sub-Funktionen
        this.version = if model.version != undefined then model.version else 800
        
        -- Reforged v1200: Helpers-Liste fuer writeBones merken
        if this.version == 1200 then this.helpersList = model.helpers
        
        ::Progress.showItem "Writing Header"
        this.writeHeader model
        
        ::Progress.showItem "Writing Animations"
        this.writeSequences model.sequences
        
        ::Progress.showItem "Writing Global Sequences"
        this.writeGlobalSequences model.globalSequences
        
        ::Progress.showItem "Writing Textures"
        this.writeTextures model.textures
        
        ::Progress.showItem "Writing Materials"
        this.writeMaterials model.materials
        
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
        
        -- Reforged: FaceFX + Corn Emitters
        ::Progress.showItem "Writing FaceFX Targets"
        this.writeFaceFX model.faceFxTargets
        
        ::Progress.showItem "Writing Corn Emitters"
        this.writeCornEmitters model.cornEmitters
        
        ::Progress.showItem "Writing Cameras"
        this.writeCameras model.cameras
        
        ::Progress.showItem "Writing Events"
        this.writeEvents model.events
        
        ::Progress.showItem "Writing Collision Shapes"
        this.writeCollisionShapes model.CollisionShapes
        
        ::Progress.upLevel()
    ),
    
    -- ============================================================================
    -- PUBLIC ENTRY POINT
    -- ============================================================================
    
    fn OutputMDL model outfile_name =
    (
        gc()
        local foutstream = createfile outfile_name
        ::IOManager.SetOutStream foutstream
        try
        (
            this.writeMDLFile model
        )
        catch
        (
            format "*** MDL Write Error: % ***\n" (getCurrentException()) to:listener
        )
        close foutstream
        gc()
        edit outfile_name
    )
)

-- ============================================================================
-- GLOBAL INSTANCE (for backward compatibility)
-- ============================================================================
global MDLWriter = WhiteoutDexMDLWriterStruct()
