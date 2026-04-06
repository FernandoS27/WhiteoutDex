/*
    WhiteoutDexSceneRebuilder.ms
    
    Refactored as Struct for better encapsulation and maintainability.
    All global variables are now instance properties.
    
    Usage:
        global SceneRebuilder = WhiteoutDexSceneRebuilderStruct()
        SceneRebuilder.rebuildSceneModel model originalPath
*/

-- ============================================================================
-- MAIN REBUILDER STRUCT
-- ============================================================================

struct WhiteoutDexSceneRebuilderStruct
(
    -- ============================================================================
    -- INSTANCE PROPERTIES (formerly global variables)
    -- ============================================================================
    
    public ModelPath = "",
    public mdlObjects = #(),
    public AnimatedTextures = 0,
    public bezierConstant = ((3.0*framerate)/(TicksPerFrame as float)),
    public rotationTCBObjs = #(),
    public bindPoses = #(),         
    public eventObjColor = color 224 198 87,
    
    -- BLP Support
    blpTemp = ((getDir #temp) + "\\BLP_Textures\\"),
    
    -- C# File Search Helper
    fileSearchCS = undefined,
    
    -- ============================================================================
    -- CONTROLLER TYPE FACTORY
    -- ============================================================================
    
    -- Creates the appropriate animation controller for a given interpolation 
    -- type (t) and value kind. Replaces 8 local duplicate definitions.
    --   t:    #linear, #bezier, #hermite (interpolation type from WC3 data)
    --   kind: #position, #scale, #float, #color
    fn typeToController t kind =
    (
        case kind of
        (
            #position: case t of
            (
                #linear: linear_position ()
                #bezier:  bezier_position ()
                #hermite: tcb_position ()
                default:  bezier_position ()
            )
            #scale: case t of
            (
                #linear: linear_scale ()
                #bezier:  bezier_scale ()
                #hermite: tcb_scale ()
                default:  bezier_scale ()
            )
            #float: case t of
            (
                #linear: linear_float ()
                #bezier:  bezier_float ()
                #hermite: tcb_float ()
                default:  bezier_float ()
            )
            #color: case t of
            (
                #linear: bezier_point3 ()
                #bezier:  bezier_color ()
                #hermite: tcb_point3 ()
                default:  bezier_point3 ()
            )
        )
    ),
    
    -- ============================================================================
    -- KEY WRITING FUNCTIONS
    -- ============================================================================
    
    fn writeKeys keys controller smoothType =
    (
        fn getDelta t1 t2 = (( t2 - t1 ) as float)
        case smoothType of
        (
            #Bezier:
            (
                for i = 1 to keys.count do
                (
                    local k = keys[i]
                    local key = addNewKey controller k.time
                    local tl
                    local kb, ka
                    
                    key.value = k.value
                    
                    key.inTangentType = #custom
                    key.outTangentType = #custom
                    
                    if (i > 1) then
                    (
                        kb = keys[i-1]
                        local delta = (getDelta kb.time k.time)
                        key.inTangent = this.bezierConstant*(k.intan - k.value)/delta
                    )
                    
                    if (i < keys.count) then
                    (
                        ka = keys[i+1]
                        local delta = (getDelta k.time ka.time)
                        key.outTangent = this.bezierConstant*(k.outtan - k.value)/delta
                    )
                    
                    if (classof controller == bezier_float) then
                        tl = 0.333333
                    else
                        tl = [0.333333,0.333333,0.333333]
                    key.outTangentLength = tl
                    key.inTangentLength = tl
                )
            )
            #NonInterp:
            (
                for k in keys do
                (
                    local key = addNewKey controller k.time
                    key.value = k.value
                    key.inTangentType = #step
                    key.outTangentType = #step
                )
            )
            default:
            (
                for k in keys do
                (
                    local key = addNewKey controller k.time
                    key.value = k.value
                )
            )
        )
    ),
    
    fn writeKeysColor keys controller t =
    (
        for k in keys do
        (
            local key = addNewKey controller k.time
            key.value = k.value
            
            if t then
            (
                key.inTangentType = #linear
                key.outTangentType = #linear
            )
        )
    ),
    
    -- ============================================================================
    -- ANIMATION APPLICATION FUNCTIONS
    -- ============================================================================
    
    fn applyRotationAnimations obj rotData =
    (
        obj.rotation.controller = linear_rotation ()
        local controller = obj.rotation.controller
        
        this.writeKeys rotData.keys controller #Linear
        
        if rotData.globalSequenceId != undefined then
        (
            setAfterORT controller #cycle
            enableORTs controller true
        )
        else
        (
            -- Fehlende Sequence-Boundary-Keys einfuegen
            try
            (
                local seqStartFrames = rootNode.startFrames
                local seqEndFrames = rootNode.endFrames
                
                if seqStartFrames != undefined and seqEndFrames != undefined then
                (
                    for si = 1 to seqEndFrames.count do
                    (
                        local sf = seqStartFrames[si] as time
                        local ef = seqEndFrames[si] as time
                        
                        -- End-Key pruefen
                        local hasEnd = false
                        for k in controller.keys where k.time == ef do hasEnd = true
                        
                        if not hasEnd then
                        (
                            local lastVal = undefined
                            for k in controller.keys do
                            (
                                if k.time >= sf and k.time <= ef then lastVal = k.value
                                if k.time > ef then exit
                            )
                            if lastVal != undefined then
                            (
                                local newKey = addNewKey controller ef
                                newKey.value = lastVal
                            )
                        )
                        
                        -- Start-Key pruefen
                        local hasStart = false
                        for k in controller.keys where k.time == sf do hasStart = true
                        
                        if not hasStart then
                        (
                            local nextVal = undefined
                            for k in controller.keys do
                            (
                                if k.time >= sf and k.time <= ef then (nextVal = k.value; exit)
                            )
                            if nextVal != undefined then
                            (
                                local newKey = addNewKey controller sf
                                newKey.value = nextVal
                            )
                        )
                    )
                )
            )
            catch ()
        )
    ),
    
    fn applyTranslationAnimations obj posData =
    (
        local smoothType = posData.type
        
        obj.pos.controller = this.typeToController smoothType #position
        local controller = obj.pos.controller
        
        local subPos
        in coordsys parent subPos = obj.pos
        
        for k in posData.keys do
        (
            k.value = k.value + subPos
            
            if smoothType == #Bezier then
            (
                k.intan = k.intan + subPos
                k.outtan = k.outtan + subPos
            )
        )
        
        this.writeKeys posData.keys controller smoothType
        
        if posData.globalSequenceId != undefined then
        (
            setAfterORT controller #cycle
            enableORTs controller true
        )
        else
        (
            try
            (
                local seqStartFrames = rootNode.startFrames
                local seqEndFrames = rootNode.endFrames
                
                if seqStartFrames != undefined and seqEndFrames != undefined then
                (
                    for si = 1 to seqEndFrames.count do
                    (
                        local sf = seqStartFrames[si] as time
                        local ef = seqEndFrames[si] as time
                        
                        local hasEnd = false
                        for k in controller.keys where k.time == ef do hasEnd = true
                        
                        if not hasEnd then
                        (
                            local lastVal = undefined
                            for k in controller.keys do
                            (
                                if k.time >= sf and k.time <= ef then lastVal = k.value
                                if k.time > ef then exit
                            )
                            if lastVal != undefined then
                            (
                                local newKey = addNewKey controller ef
                                newKey.value = lastVal
                            )
                        )
                        
                        local hasStart = false
                        for k in controller.keys where k.time == sf do hasStart = true
                        
                        if not hasStart then
                        (
                            local nextVal = undefined
                            for k in controller.keys do
                            (
                                if k.time >= sf and k.time <= ef then (nextVal = k.value; exit)
                            )
                            if nextVal != undefined then
                            (
                                local newKey = addNewKey controller sf
                                newKey.value = nextVal
                            )
                        )
                    )
                )
            )
            catch ()
        )
    ),
    
    fn applyScaleAnimations obj scaleData =
    (
        local smoothType = scaleData.type
        
        obj.scale.controller = this.typeToController smoothType #scale
        local controller = obj.scale.controller
        
        local subScale
        in coordsys parent subScale = obj.scale
        
        for k in scaleData.keys do
        (
            k.value = k.value/subScale
            
            if smoothType == #Bezier then
            (
                k.intan = k.intan/subScale
                k.outtan = k.outtan/subScale
            )
        )
        
        this.writeKeys scaleData.keys controller smoothType
        
        if scaleData.globalSequenceId != undefined then
        (
            setAfterORT controller #cycle
            enableORTs controller true
        )
        else
        (
            try
            (
                local seqStartFrames = rootNode.startFrames
                local seqEndFrames = rootNode.endFrames
                
                if seqStartFrames != undefined and seqEndFrames != undefined then
                (
                    for si = 1 to seqEndFrames.count do
                    (
                        local sf = seqStartFrames[si] as time
                        local ef = seqEndFrames[si] as time
                        
                        local hasEnd = false
                        for k in controller.keys where k.time == ef do hasEnd = true
                        
                        if not hasEnd then
                        (
                            local lastVal = undefined
                            for k in controller.keys do
                            (
                                if k.time >= sf and k.time <= ef then lastVal = k.value
                                if k.time > ef then exit
                            )
                            if lastVal != undefined then
                            (
                                local newKey = addNewKey controller ef
                                newKey.value = lastVal
                            )
                        )
                        
                        local hasStart = false
                        for k in controller.keys where k.time == sf do hasStart = true
                        
                        if not hasStart then
                        (
                            local nextVal = undefined
                            for k in controller.keys do
                            (
                                if k.time >= sf and k.time <= ef then (nextVal = k.value; exit)
                            )
                            if nextVal != undefined then
                            (
                                local newKey = addNewKey controller sf
                                newKey.value = nextVal
                            )
                        )
                    )
                )
            )
            catch ()
        )
    ),
    
    fn applyTransformAnimations obj =
    (
        local n = obj.maxObj
        local trans = obj.transformations
        
        if trans.rotation != undefined and ::WhiteoutDexImportSettings.importRotAnim then
        (
            this.applyRotationAnimations n trans.rotation
            
            if trans.rotation.type == #Hermite or trans.rotation.type == #Bezier then
                append this.rotationTCBObjs obj
        )
        
        if trans.translation != undefined and ::WhiteoutDexImportSettings.importTransAnim then
            this.applyTranslationAnimations n trans.translation
        
        if trans.scale != undefined and ::WhiteoutDexImportSettings.importScaleAnim then
            this.applyScaleAnimations n trans.scale
    ),
    
    fn applyFloatParamAnimations obj animData defaultValue param =
    (
        if animData == undefined then return false
        if animData.keys == undefined or animData.keys.count == 0 then return false
        
        local smoothType = animData.type
        local controller = this.typeToController smoothType #float
        
        -- FIX: Keys wurden bereits vom Relinker remapped, KEIN erneuter preProcessAnim-Call!
        local keys = copy animData.keys #nomap
        for k in keys do
        (
            k.value = k.value + defaultValue
            
            if smoothType == #Bezier then
            (
                if k.intan != undefined then k.intan = k.intan + defaultValue
                if k.outtan != undefined then k.outtan = k.outtan + defaultValue
            )
        )
        
        this.writeKeys keys controller smoothType
        
        if animData.globalSequenceId != undefined then
        (
            setAfterORT controller #cycle
            enableORTs controller true
        )
        
        setPropertyController obj param controller
        true
    ),
    

    -- ============================================================================
    -- TEXTURE ANIMATION ROTATION/SCALE FUNCTIONS
    -- ============================================================================

    -- Konvertiert Quaternion-Rotation zu W_Angle (Grad) für Texture Animation
    fn applyTextureRotationAnimation obj animData param =
    (
        if animData == undefined then return false
        if animData.keys == undefined or animData.keys.count == 0 then return false
        
        fn quatToZAngle q =
        (
            -- Extrahiere Z-Rotation aus Quaternion und konvertiere zu Grad
            local euler = quatToEuler q order:1
            -euler.z  -- Negieren wegen Koordinatensystem-Unterschied
        )
        
        local smoothType = animData.type
        local controller = this.typeToController smoothType #float
        
        -- WICHTIG: Zeit-Konvertierung durch ModelRelinker (nur bei nicht-globalen Animationen)
        local convertedKeys = ModelRelinker.preProcessAnim animData (quat 0 0 0 1)
        
        -- Konvertiere Quaternion-Keys zu Float-Keys (Grad)
        local floatKeys = #()
        for k in convertedKeys do
        (
            local angleValue = quatToZAngle k.value
            
            if smoothType == #Bezier or smoothType == #Hermite then
            (
                local inAngle = if k.intan != undefined then quatToZAngle k.intan else angleValue
                local outAngle = if k.outtan != undefined then quatToZAngle k.outtan else angleValue
                append floatKeys (SmoothKeyData k.time angleValue inAngle outAngle)
            )
            else
            (
                append floatKeys (KeyData k.time angleValue)
            )
        )
        
        this.writeKeys floatKeys controller smoothType
        
        if animData.globalSequenceId != undefined then
        (
            setAfterORT controller #cycle
            enableORTs controller true
        )
        
        setPropertyController obj param controller
        true
    ),

    -- Wendet Scale-Animation auf U_Tiling/V_Tiling an
    fn applyTextureScaleAnimation obj animData param defaultValue =
    (
        if animData == undefined then return false
        if animData.keys == undefined or animData.keys.count == 0 then return false
        
        local smoothType = animData.type
        local controller = this.typeToController smoothType #float
        
        -- WICHTIG: Zeit-Konvertierung durch ModelRelinker (nur bei nicht-globalen Animationen)
        local convertedKeys = ModelRelinker.preProcessAnim animData defaultValue
        
        -- Kopiere Keys und wende Default-Wert an (multiplizieren für Scale)
        local floatKeys = #()
        for k in convertedKeys do
        (
            local scaleValue = k.value * defaultValue
            
            if smoothType == #Bezier or smoothType == #Hermite then
            (
                local inVal = if k.intan != undefined then k.intan * defaultValue else scaleValue
                local outVal = if k.outtan != undefined then k.outtan * defaultValue else scaleValue
                append floatKeys (SmoothKeyData k.time scaleValue inVal outVal)
            )
            else
            (
                append floatKeys (KeyData k.time scaleValue)
            )
        )
        
        this.writeKeys floatKeys controller smoothType
        
        if animData.globalSequenceId != undefined then
        (
            setAfterORT controller #cycle
            enableORTs controller true
        )
        
        setPropertyController obj param controller
        true
    ),

    -- ============================================================================
    -- CAMERA ANIMATION FUNCTIONS
    -- ============================================================================
    
    fn applyCameraTranslationAnimation obj posData startPos =
    (
        if posData == undefined then return false
        if posData.keys == undefined or posData.keys.count == 0 then return false
        
        local smoothType = posData.type
        
        obj.pos.controller = this.typeToController smoothType #position
        local controller = obj.pos.controller
        
        -- WICHTIG: Zeit-Konvertierung durch ModelRelinker
        local convertedKeys = ModelRelinker.preProcessAnim posData [0,0,0]
        
        -- Store Hermite tangents in AppData BEFORE startPos modification
        if smoothType == #Hermite and posData.globalSequenceId == undefined then
        (
            try
            (
                local str = "HERMITE_POS:"
                for i = 1 to convertedKeys.count do
                (
                    local k = convertedKeys[i]
                    local tick = k.time as integer
                    local v = k.value
                    local it = if k.intan != undefined then k.intan else v
                    local ot = if k.outtan != undefined then k.outtan else v
                    if i > 1 then str += "|"
                    str += tick as string + ":"
                    str += v.x as string + "," + v.y as string + "," + v.z as string + ":"
                    str += it.x as string + "," + it.y as string + "," + it.z as string + ":"
                    str += ot.x as string + "," + ot.y as string + "," + ot.z as string
                )
                setAppData obj 9200 str
            )
            catch()
        )
        
        local keys = copy convertedKeys #nomap
        for k in keys do
        (
            k.value = k.value + startPos
            
            if smoothType == #Bezier then
            (
                if k.intan != undefined then k.intan = k.intan + startPos
                if k.outtan != undefined then k.outtan = k.outtan + startPos
            )
        )
        
        this.writeKeys keys controller smoothType
        
        if posData.globalSequenceId != undefined then
        (
            setAfterORT controller #cycle
            enableORTs controller true
        )
        
        true
    ),
    
    fn applyCameraRollAnimation cam rollData =
    (
        if rollData == undefined then return false
        if rollData.keys == undefined or rollData.keys.count == 0 then return false
        
        local smoothType = rollData.type
        local controller = this.typeToController smoothType #float
        
        -- WICHTIG: Zeit-Konvertierung durch ModelRelinker
        local convertedKeys = ModelRelinker.preProcessAnim rollData 0.0
        
        local keys = copy convertedKeys #nomap
        
        this.writeKeys keys controller smoothType
        
        if rollData.globalSequenceId != undefined then
        (
            setAfterORT controller #cycle
            enableORTs controller true
        )
        
        (getSubAnim (getSubAnim cam 3) 2).controller = controller
        
        true
    ),
    
    fn applyCameraAnimations camObj targetObj camData =
    (
        local hasAnimation = false
        
        -- Kamera-Position Animation (KCTR)
        if camData.transformations != undefined and camData.transformations.translation != undefined then
        (
            if ::WhiteoutDexImportSettings.importTransAnim then
            (
                local result = this.applyCameraTranslationAnimation camObj camData.transformations.translation camData.pivot
                if result then hasAnimation = true
            )
        )
        
        -- Target-Position Animation (KTTR)
        if camData.targetTransformations != undefined and camData.targetTransformations.translation != undefined then
        (
            if ::WhiteoutDexImportSettings.importTransAnim then
            (
                local result = this.applyCameraTranslationAnimation targetObj camData.targetTransformations.translation camData.targetPos
                if result then hasAnimation = true
            )
        )
        
        -- Roll Animation (KCRL)
        if camData.rotation != undefined then
        (
            if ::WhiteoutDexImportSettings.importRotAnim then
            (
                local result = this.applyCameraRollAnimation camObj camData.rotation
                if result then hasAnimation = true
            )
        )
        
        hasAnimation
    ),
    
    -- ============================================================================
    -- VISIBILITY ANIMATION FUNCTIONS
    -- ============================================================================
    
    fn applyVisibilityAnimations o control =
    (
        if ::WhiteoutDexImportSettings.importVisAnim then
        (
            local keys = copy control.keys #nomap
            
            local hasFrameZero = false
            for k in keys do if k.time == 0f then hasFrameZero = true
                        
            case control.type of
            (
                #Linear:
                (
                    o.visibility = linear_float()
                    local c = o.visibility.controller
                    if not hasFrameZero then addNewKey c 0f
                    
                    this.writeKeys keys c #Linear
                    
                    if control.globalSequenceId != undefined then
                    (
                        setAfterORT c #cycle
                        enableORTs c true
                    )
                )
                
                #Hermite:
                (
                    o.visibility = tcb_float()
                    local c = o.visibility.controller
                    if not hasFrameZero then addNewKey c 0f
                    
                    this.writeKeys keys c #Hermite
                    
                    if control.globalSequenceId != undefined then
                    (
                        setAfterORT c #cycle
                        enableORTs c true
                    )
                )
                
                #Bezier:
                (
                    o.visibility = bezier_float()
                    local c = o.visibility.controller
                    if not hasFrameZero then addNewKey c 0f
                    
                    this.writeKeys keys c #Bezier
                    
                    if control.globalSequenceId != undefined then
                    (
                        setAfterORT c #cycle
                        enableORTs c true
                    )
                )
                
                #NonInterp: 
                (
                    o.visibility = bezier_float()
                    local c = o.visibility.controller
                    if not hasFrameZero then
                    (
                        local zeroKey = addNewKey c 0f
                        zeroKey.value = keys[1].value
                        zeroKey.inTangentType = #step
                        zeroKey.outTangentType = #step
                    )

                    for k in keys do
                    (
                        local newKey = addNewKey c k.time
                        newKey.value = k.value
                        newKey.inTangentType = #step
                        newKey.outTangentType = #step
                    )
                    
                    if control.globalSequenceId != undefined then
                    (
                        setAfterORT c #cycle
                        enableORTs c true
                    )
                )
                
                default:
                (
                    this.applyFloatParamAnimations o control 0.0 #visibility
                )
            )
        )
    ),
    
    -- ============================================================================
    -- COLOR ANIMATION FUNCTIONS
    -- ============================================================================
    
    fn applyColorParamAnimations obj animData defaultValue param =
    (
        local smoothType = animData.type
        local controller = this.typeToController smoothType #color

        for k in animData.keys do
        (
            local originalValue = k.value
            k.value = [originalValue.z, originalValue.y, originalValue.x]/255
            
            if smoothType == #Bezier then
            (
                local originalIntan = k.intan
                local originalOuttan = k.outtan
                k.intan = [originalIntan.z, originalIntan.y, originalIntan.x]/255
                k.outtan = [originalOuttan.z, originalOuttan.y, originalOuttan.x]/255
            )
        )

        this.writeKeysColor animData.keys controller (animData.type == #linear)

        if animData.globalSequenceId != undefined then
        (
            setAfterORT controller #cycle
            enableORTs controller true
        )

        setPropertyController obj param controller
    ),
    
    -- ============================================================================
    -- PARAMETER APPLICATION FUNCTIONS
    -- ============================================================================
    
    fn applyFloatParameter obj prop param =
    (
        if param.type == #Static then
            setProperty obj prop param.data
        else
            if ::WhiteoutDexImportSettings.importParamAnim then
                this.applyFloatParamAnimations obj param.data 0.0 prop
    ),
    
    fn applyColorParameter obj prop param =
    (
        if param.type == #Static then
            setProperty obj prop param.data
        else
            if ::WhiteoutDexImportSettings.importColorAnim then 
                this.applyColorParamAnimations obj param.data [0,0,0] prop
    ),
    
    -- ============================================================================
    -- MATERIAL FUNCTIONS
    -- ============================================================================
    
    fn getCompoundMaterial = 
        CompositeMaterial(),
    

    fn ensureFileSearchCS =
    (
        if this.fileSearchCS != undefined do return true
        
        local csCode = "using System;\n"
        csCode += "using System.IO;\n"
        csCode += "using System.Collections.Generic;\n"
        csCode += "public class WhiteoutDexFileSearch {\n"
        csCode += "  public static string Find(string dir, string baseName, string[] extensions) {\n"
        csCode += "    if (!Directory.Exists(dir)) return \"\";\n"
        csCode += "    foreach (string ext in extensions) {\n"
        csCode += "      string[] files = Directory.GetFiles(dir, baseName + ext, SearchOption.AllDirectories);\n"
        csCode += "      if (files.Length > 0) return files[0];\n"
        csCode += "    }\n"
        csCode += "    return \"\";\n"
        csCode += "  }\n"
        csCode += "}\n"
        
        local maxYear = (maxVersion())[8]
        if maxYear >= 2026 then
        (
            local helper = dotNetClass "CSharpUtilities.CSharpCompilationHelper"
            helper.Compile csCode #()
        )
        else
        (
            try ( dotNet.loadAssembly "Microsoft.CSharp" ) catch ()
            try ( dotNet.loadAssembly "System.CodeDom" ) catch ()
            local provider = dotNetObject "Microsoft.CSharp.CSharpCodeProvider"
            local params = dotNetObject "System.CodeDom.Compiler.CompilerParameters"
            params.ReferencedAssemblies.Add "System.dll"
            params.ReferencedAssemblies.Add "System.Core.dll"
            params.GenerateInMemory = true
            local result = provider.CompileAssemblyFromSource params #(csCode)
            if result.Errors.HasErrors then
            (
                for i = 0 to result.Errors.Count - 1 do
                    format "FileSearch compile error: %\n" ((result.Errors.get_Item i).ToString())
                return false
            )
        )
        
        this.fileSearchCS = dotNetClass "WhiteoutDexFileSearch"
        this.fileSearchCS != undefined
    ),
    

    fn DecodeBLPFile blpPath =
    (
        if not (doesFileExist blpPath) do return undefined
        
        -- Native BLP support: Max can open .blp directly, no conversion needed
        if ::WhiteoutDexHasNativeBLP then return blpPath
        
        local baseName = getFilenameFile blpPath
        local tgaPath = this.blpTemp + baseName + ".tga"
        
        if (doesFileExist tgaPath) do return tgaPath
        
        makeDir this.blpTemp all:true
        local exePath = (systemTools.getEnvVariable "APPDATA") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex\\WhiteoutTexCLI.exe"
        
        -- Copy BLP to ASCII-safe temp path first (WhiteoutTexCLI fails on Unicode paths)
        local tempBlp = this.blpTemp + baseName + ".blp"
        try ( (dotNetClass "System.IO.File").Copy blpPath tempBlp true ) catch ( return undefined )
        
        local cmd = "cmd /c \"\"" + exePath + "\" \"" + tempBlp + "\" \"" + tgaPath + "\"\""
        local exitCode = 0
        HiddenDOSCommand cmd exitCode:&exitCode
        
        -- Clean up temp BLP
        try ( deleteFile tempBlp ) catch()
        
        if exitCode == 0 and doesFileExist tgaPath then tgaPath else undefined
    ),
    
    fn FindTexture fname mdxRelPath: =
    (
        local extensions = #(".blp", ".dds", ".tif", ".tga", ".png")
        
        -- 0) Try original MDX relative path first
        if mdxRelPath != unsupplied and mdxRelPath != undefined and mdxRelPath != "" then
        (
            local directPath = this.ModelPath + mdxRelPath
            local directExt = getFilenameType mdxRelPath
            if toLower directExt == ".blp" then
            (
                local decoded = this.DecodeBLPFile directPath
                if decoded != undefined do return decoded
            )
            else
            (
                if (doesFileExist directPath) do return directPath
            )
            local relBase = (getFilenamePath mdxRelPath) + (getFilenameFile mdxRelPath)
            for ext in extensions where toLower ext != toLower directExt do
            (
                local testPath = this.ModelPath + relBase + ext
                if ext == ".blp" then
                (
                    local decoded = this.DecodeBLPFile testPath
                    if decoded != undefined do return decoded
                )
                else
                (
                    if (doesFileExist testPath) do return testPath
                )
            )
        )
        
        -- 1+2) C# fast recursive search across all extensions
        if this.ensureFileSearchCS() then
        (
            local extArr = dotNetObject "System.String[]" extensions.count
            for i = 1 to extensions.count do extArr.set (i-1) extensions[i]
            local found = this.fileSearchCS.Find this.ModelPath fname extArr
            if found != "" then
            (
                if toLower (getFilenameType found) == ".blp" then
                (
                    local decoded = this.DecodeBLPFile found
                    if decoded != undefined do return decoded
                )
                else return found
            )
        )
        
        -- 3) MPQ-Archive durchsuchen
        if ::WhiteoutDexImportSettings.searchMPQ and ::WhiteoutDexMPQ != undefined and ::WhiteoutDexMPQ.isInitialized then
        (
            local mpqResult = ::WhiteoutDexMPQ.findTexture fname
            if mpqResult != undefined do return mpqResult
        )
        
        -- 3b) CASC-Archive durchsuchen
        if ::WhiteoutDexImportSettings.searchCASC and ::WhiteoutDexCASC != undefined and ::WhiteoutDexCASC.isInitialized then
        (
            local cascResult = ::WhiteoutDexCASC.findTexture fname
            if cascResult != undefined do return cascResult
        )
        
        -- 4) Fallback
        local fallbackDds = this.ModelPath + fname + ".dds"
        local fallbackTga = this.ModelPath + fname + ".tga"
        if (doesFileExist fallbackDds) then fallbackDds
        else fallbackTga
    ),
    
	-- Reforged: Hilfsfunktion um PBR-Textur zu laden (DDS/TGA/TIF)
    fn loadReforgedTexture texObj =
    (
        if texObj == undefined then return undefined
        local texPath = texObj.path
        if texPath == undefined or texPath == "" then return undefined
        local filename = getFilenameFile texPath
        local finalFile = this.FindTexture filename mdxRelPath:texPath
        local bm = Bitmaptexture fileName:finalFile
        try (bm.bitmap = openBitmap finalFile gamma:1.0) catch()
        if bm.filename == "" then bm.filename = finalFile
        bm
    ),

    fn createLayer layer data n =
	(
		layer.filtermode = data.filtermode
		layer.unshaded = data.unshaded
		layer.unfogged = data.unfogged
		layer.twosides = data.twosided
		layer.NoDephTest = data.nodephtest
		layer.NoDephSet = data.nodephset
		layer.SphereEnvMap = data.SphereEnvMap
		this.applyFloatParameter layer "alpha" data.alpha
		
		-- FIX: Alpha-Controller muss auch auf delegate.opacity wirken
		if data.alpha.type == #Animated then
		(
			local alphaCtrl = getPropertyController layer "alpha"
			if alphaCtrl != undefined then
			(
				try ( layer.delegate.opacity.controller = alphaCtrl ) catch( format "SceneRebuilder: Failed to assign opacity controller: %\n" (getCurrentException()) )
			)
		)
		
		-- Reforged: Fresnel + Emissive
		if data.emissiveGain != undefined then
			try ( layer.EmissiveGain = data.emissiveGain ) catch()
		if data.fresnelColor != undefined then
			try ( layer.FresnelColorR = data.fresnelColor.x; layer.FresnelColorG = data.fresnelColor.y; layer.FresnelColorB = data.fresnelColor.z ) catch()
		if data.fresnelOpacity != undefined then
			try ( layer.FresnelOpacity = data.fresnelOpacity ) catch()
		if data.fresnelTeamColor != undefined then
			try ( layer.FresnelTeamColor = data.fresnelTeamColor ) catch()
		
		-- Reforged PBR: Normal, ORM, Emissive, Environment Textures laden
		local model = ::WhiteoutDexImportedModel
		if model != undefined and model.version == 1200 then
		(
			if data.normalTexId != undefined and data.normalTexId > 0 and data.normalTexId <= model.textures.count then
			(
				local bm = loadReforgedTexture model.textures[data.normalTexId]
				if bm != undefined then
				(
					try ( layer.normal_map = bm; layer.origNormalPath = model.textures[data.normalTexId].path ) catch()
					format "  Normal Map: %\n" model.textures[data.normalTexId].path
				)
			)
			if data.ormTexId != undefined and data.ormTexId > 0 and data.ormTexId <= model.textures.count then
			(
				local bm = loadReforgedTexture model.textures[data.ormTexId]
				if bm != undefined then
				(
					try ( layer.orm_map = bm; layer.origOrmPath = model.textures[data.ormTexId].path ) catch()
					format "  ORM Map: %\n" model.textures[data.ormTexId].path
				)
			)
			if data.emissiveTexId != undefined and data.emissiveTexId > 0 and data.emissiveTexId <= model.textures.count then
			(
				local bm = loadReforgedTexture model.textures[data.emissiveTexId]
				if bm != undefined then
				(
					try ( layer.emissive_map = bm; layer.origEmissivePath = model.textures[data.emissiveTexId].path ) catch()
					format "  Emissive Map: %\n" model.textures[data.emissiveTexId].path
				)
			)
			if data.envMapTexId != undefined and data.envMapTexId > 0 and data.envMapTexId <= model.textures.count then
			(
				local bm = loadReforgedTexture model.textures[data.envMapTexId]
				if bm != undefined then
				(
					try ( layer.reflection_map = bm; layer.origReflectionPath = model.textures[data.envMapTexId].path ) catch()
					format "  Environment Map: %\n" model.textures[data.envMapTexId].path
				)
			)
			-- Reforged: TeamColor Slot (replaceable Textur, nur Index merken)
			if data.teamColorTexId != undefined and data.teamColorTexId > 0 and data.teamColorTexId <= model.textures.count then
			(
				try ( layer.teamColorTexId = data.teamColorTexId ) catch()
				format "  TeamColor: slot 4 (replaceableId=%)\n" model.textures[data.teamColorTexId].replaceableId
			)
		)
		
		-- Diffuse Texture (Standard-Logik)
		local bm = undefined
		
		if data.textures.type == #Static then
		(
			local tex = data.textures.data
			if tex == undefined then continue
			
			layer.retexture = tex.replaceableId
			
			if tex.replaceableId == 0 or tex.replaceableId == 1 or (data.tvertexid != undefined) then
			(
				local filename = getFilenameFile tex.path
				local layPath = getFilenamePath tex.path
				if layPath == undefined then layPath = ""
				
				local finalFile = this.FindTexture filename mdxRelPath:tex.path
				
				bm = Bitmaptexture fileName:finalFile
				try ( bm.bitmap = openBitmap finalFile gamma:1.0 ) catch( format "SceneRebuilder: Failed to open bitmap '%': %\n" finalFile (getCurrentException()) )
				if bm.filename == "" then bm.filename = finalFile
				layer.path = layPath
				-- Reforged: Original-Pfad fuer verlustfreien Round-Trip speichern
				try ( layer.origDiffusePath = tex.path ) catch()
				
				bm.coords.U_Tile = layer.Wrap_Height = tex.wrapHeight
				bm.coords.V_Tile = layer.Wrap_Width = tex.wrapWidth
			)
		)
		else if ::WhiteoutDexImportSettings.importTXAnim then
		(
			fn createIFL tex modelPath animTexCount rebuilder =
			(
				local texPath = (modelPath + "Textures" + (animTexCount as string) + ".ifl")
				local expFile = createfile texPath
				for t in tex do
				(
					local finalFile = rebuilder.FindTexture (getFilenameFile t.path) mdxRelPath:t.path
					format "%\n" finalFile to:expFile
				)
				close expFile
				texPath
			)
			local tex = data.textures.data
			local k = ::Utils.ArrayMap tex.keys (fn tmp3 a = a.value)
			local k2 = ::Utils.ArrayMap tex.keys (fn tmp2 a = a.time)
			local startTime = k2[1]
			for i = k2.count to 2 by -1 do k2[i] = k2[i] - k2[i-1]
			local avgT = if k2.count > 1 then ((::Utils.ArrayFold k2 (fn tmp a b = a + b) 0f) - k2[1])/(k2.count - 1) else k2[1]
			local layPath = getFilenamePath k[1].path
			if layPath == undefined then layPath = ""
			local wh = k[1].wrapHeight
			local ww = k[1].wrapWidth
			local finalFile = createIFL k this.ModelPath this.AnimatedTextures this
			this.AnimatedTextures = this.AnimatedTextures + 1
			bm = Bitmaptexture fileName:finalFile
			bm.starttime = startTime
			bm.playbackrate = (TicksPerFrame/(avgT as float))
			bm.endcondition = if tex.globalSequenceId != undefined then 0 else 2
			bm.coords.U_tile = wh
			bm.coords.V_tile = ww
			layer.path = layPath
			try ( layer.SqAnim_Many3 = getFilenamePath finalFile ) catch ()
		)
		
		if (bm != undefined and data.tvertexid != undefined and ::WhiteoutDexImportSettings.importUVAnim) then
		(
			local tv = data.tvertexid.transformation
			local c = bm.coords
			if (tv.translation != undefined) then
            (
                this.applyFloatParamAnimations c tv.translation[2] 0.0 #U_Offset
                this.applyFloatParamAnimations c tv.translation[1] 0.0 #V_Offset
                -- W translation: no bitmap coord equivalent, store on plugin parameter
                if tv.translation.count >= 3 and tv.translation[3] != undefined \
                and tv.translation[3].keys != undefined and tv.translation[3].keys.count > 0 then
                (
                    this.applyFloatParamAnimations layer tv.translation[3] 0.0 #Wc3_W_Offset
                )
            )
			if (tv.rotation != undefined and tv.rotation.keys != undefined and tv.rotation.keys.count > 0) then
				this.applyTextureRotationAnimation c tv.rotation #W_Angle
			if (tv.scale != undefined and tv.scale.keys != undefined and tv.scale.keys.count > 0) then
			(
				local uScaleData = AnimationData()
				local vScaleData = AnimationData()
				uScaleData.type = tv.scale.type
				vScaleData.type = tv.scale.type
				uScaleData.globalSequenceId = tv.scale.globalSequenceId
				vScaleData.globalSequenceId = tv.scale.globalSequenceId
				uScaleData.keys = #()
				vScaleData.keys = #()
				for k in tv.scale.keys do
				(
					if tv.scale.type == #Bezier or tv.scale.type == #Hermite then
					(
						local uIn = if k.intan != undefined then k.intan.x else k.value.x
						local uOut = if k.outtan != undefined then k.outtan.x else k.value.x
						local vIn = if k.intan != undefined then k.intan.y else k.value.y
						local vOut = if k.outtan != undefined then k.outtan.y else k.value.y
						append uScaleData.keys (SmoothKeyData k.time k.value.x uIn uOut)
						append vScaleData.keys (SmoothKeyData k.time k.value.y vIn vOut)
					)
					else
					(
						append uScaleData.keys (KeyData k.time k.value.x)
						append vScaleData.keys (KeyData k.time k.value.y)
					)
				)
				this.applyTextureScaleAnimation c uScaleData #U_Tiling 1.0
				this.applyTextureScaleAnimation c vScaleData #V_Tiling 1.0
			)
		)
		
		layer.texture = bm
		try showTextureMap layer layer.texture true catch()
		layer.showinviewport = true
		-- Re-apply wrap flags after texture assignment
		-- (on texture set handler overwrites them from delegate defaults)
		if data.textures.type == #Static and data.textures.data != undefined then
		(
			layer.Wrap_Height = data.textures.data.wrapHeight
			layer.Wrap_Width = data.textures.data.wrapWidth
			if bm != undefined then
			(
				bm.coords.U_Tile = data.textures.data.wrapHeight
				bm.coords.V_Tile = data.textures.data.wrapWidth
			)
		)
	),
    
    fn recreateMaterials mats =
    (
        local matName = "Imported Material "
        local isReforged = (::WhiteoutDexImportedModel != undefined and ::WhiteoutDexImportedModel.version == 1200)
        for i = 1 to mats.count do
        (
            local m = mats[i]
            local m2
            -- Reforged: immer Single-Material (PBR Maps in einem Material)
            if isReforged or m.layers.count == 1 then
            (
                m2 = Warcraft3 ()
                if isProperty m2 #ConstantColor then
                    m2.ConstantColor = m.isConstantColor
                if isProperty m2 #SortPrimsFarZ then
                (
                    if m.isSortPrimsFarZ then m2.SortPrimsFarZ = 3
                    else if m.isSortPrimsNearZ then m2.SortPrimsFarZ = 2
                    else m2.SortPrimsFarZ = 1
                )
                if isProperty m2 #FullResolution then
                    m2.FullResolution = m.isFullResolution
                if isProperty m2 #PriorityPlane then
                    m2.PriorityPlane = m.priorityPlane
                this.createLayer m2 m.layers[1] m2
            )
            else
            (
                -- Detect Team Color pattern: one TC layer + one texture layer
                local tcLayerIdx = 0
                local texLayerIdx = 0
                if m.layers.count == 2 then
                (
                    for j = 1 to 2 do
                    (
                        local l = m.layers[j]
                        if l.textures != undefined and l.textures.type == #Static and l.textures.data != undefined then
                        (
                            if l.textures.data.replaceableId == 2 then tcLayerIdx = j
                            else if l.textures.data.replaceableId <= 1 then texLayerIdx = j
                        )
                    )
                )
                
                if tcLayerIdx > 0 and texLayerIdx > 0 then
                (
                    -- Flatten to single Warcraft3 material with Mix map viewport display
                    m2 = Warcraft3 ()
                    if isProperty m2 #ConstantColor then
                        m2.ConstantColor = m.isConstantColor
                    if isProperty m2 #SortPrimsFarZ then
                    (
                        if m.isSortPrimsFarZ then m2.SortPrimsFarZ = 3
                        else if m.isSortPrimsNearZ then m2.SortPrimsFarZ = 2
                        else m2.SortPrimsFarZ = 1
                    )
                    if isProperty m2 #FullResolution then
                        m2.FullResolution = m.isFullResolution
                    if isProperty m2 #PriorityPlane then
                        m2.PriorityPlane = m.priorityPlane
                    -- Apply the texture layer properties first
                    this.createLayer m2 m.layers[texLayerIdx] m2
                    -- Override retexture to Team Color — triggers adjustMapping case 2 (Mix map)
                    m2.retexture = 2
                    -- Copy unshaded flag from TC layer if set
                    if m.layers[tcLayerIdx].unshaded then
                        try ( m2.unshaded = true ) catch()
                )
                else
                (
                    -- Standard composite for non-TC multi-layer materials
                    m2 = this.getCompoundMaterial ()
                    m2.mapEnables = #(false, false, false, false, false, false, false, false, false)
                    for j = 1 to m.layers.count do
                    (
                        local l = m.layers[j]
                        local sub = Warcraft3 ()
                        if isProperty sub #ConstantColor then
                            sub.ConstantColor = m.isConstantColor
                        if isProperty sub #SortPrimsFarZ then
                        (
                            if m.isSortPrimsFarZ then sub.SortPrimsFarZ = 3
                            else if m.isSortPrimsNearZ then sub.SortPrimsFarZ = 2
                            else sub.SortPrimsFarZ = 1
                        )
                        if isProperty sub #FullResolution then
                            sub.FullResolution = m.isFullResolution
                        if isProperty sub #PriorityPlane then
                            sub.PriorityPlane = m.priorityPlane
                        this.createLayer sub l m2
                        m2.mapEnables[j] = true
                        m2.materialList[j] = sub
                        
                        -- Viewport-Fix: Composite Layer Blend-Einstellungen
                        if l.filtermode >= 4 and l.filtermode <= 5 then
                        (
                            try
                            (
                                m2.mixType[j] = 1
                                m2.amount[j] = 50.0
                            ) catch()
                        )
                    )
                )
            )
            m2.name = matName + (i as string)
            m.maxObj = m2
            if (i < 25) then
            (
                meditmaterials[i] = m2
                setMTLMeditObjType m2 3
            )
        )
    ),
    
    -- ============================================================================
    -- MESH CREATION FUNCTIONS
    -- ============================================================================
    
    fn calculateSmoothGroups obj normalArray faceArray =
    (
        local numFaces = faceArray.count
        local normalThreshold = 0.01
        local posThreshold = 0.001
        local gridSize = 0.01
        
        -- Alle Vertex-Positionen sammeln
        local vertPos = #()
        local maxVert = 0
        for f = 1 to numFaces do
        (
            local face = faceArray[f]
            for c = 1 to 3 do
            (
                local vi = face[c] as integer
                if vi > maxVert then maxVert = vi
            )
        )
        vertPos[maxVert] = undefined
        for i = 1 to maxVert do
            vertPos[i] = getVert obj i
        
        -- Phase 1: Face-Corners in Grid-Zellen einsortieren (Integer-Hash)
        local grid = Dictionary #integer
        
        -- Pre-compute Cell-Koordinaten pro Vertex
        local vertCX = #()
        local vertCY = #()
        local vertCZ = #()
        vertCX[maxVert] = undefined
        vertCY[maxVert] = undefined
        vertCZ[maxVert] = undefined
        for i = 1 to maxVert do
        (
            local pos = vertPos[i]
            vertCX[i] = (floor (pos.x / gridSize)) as integer
            vertCY[i] = (floor (pos.y / gridSize)) as integer
            vertCZ[i] = (floor (pos.z / gridSize)) as integer
        )
        
        for f = 1 to numFaces do
        (
            local face = faceArray[f]
            for c = 1 to 3 do
            (
                local vi = face[c] as integer
                local key = vertCX[vi] * 73856093 + vertCY[vi] * 19349663 + vertCZ[vi] * 83492791
                if not (hasDictValue grid key) then
                    grid[key] = #(#(f, c, vi))
                else
                    append grid[key] #(f, c, vi)
            )
        )
        
        -- Phase 2: Positions-Gruppen aus Nachbar-Zellen bilden
        -- Integer-Key fuer assigned: (f-1)*3 + c ist eindeutig pro Face-Corner
        local posGroups = #()
        local assigned = Dictionary #integer
        
        for f = 1 to numFaces do
        (
            local face = faceArray[f]
            for c = 1 to 3 do
            (
                local assignKey = (f - 1) * 3 + c
                if not (hasDictValue assigned assignKey) then
                (
                    local vi = face[c] as integer
                    local pos = vertPos[vi]
                    local group = #(#(f, c))
                    assigned[assignKey] = true
                    
                    local cx = vertCX[vi]
                    local cy = vertCY[vi]
                    local cz = vertCZ[vi]
                    
                    for dx = -1 to 1 do
                    for dy = -1 to 1 do
                    for dz = -1 to 1 do
                    (
                        local nkey = (cx+dx) * 73856093 + (cy+dy) * 19349663 + (cz+dz) * 83492791
                        if hasDictValue grid nkey then
                        (
                            for entry in grid[nkey] do
                            (
                                local ef = entry[1]
                                local ec = entry[2]
                                if ef != f or ec != c then
                                (
                                    local eKey = (ef - 1) * 3 + ec
                                    if not (hasDictValue assigned eKey) then
                                    (
                                        if (distance pos vertPos[entry[3]]) < posThreshold then
                                        (
                                            append group #(ef, ec)
                                            assigned[eKey] = true
                                        )
                                    )
                                )
                            )
                        )
                    )
                    
                    if group.count > 1 then
                        append posGroups group
                )
            )
        )
        
        -- Union-Find fuer Face-Gruppen
        local parent = #()
        parent[numFaces] = undefined
        for i = 1 to numFaces do parent[i] = i
        
        for pg = 1 to posGroups.count do
        (
            local faceList = posGroups[pg]
            for a = 1 to faceList.count do
            (
                local fA = faceList[a][1]
                local cA = faceList[a][2]
                local nIdxA = (faceArray[fA])[cA] as integer
                
                for b = a + 1 to faceList.count do
                (
                    local fB = faceList[b][1]
                    local cB = faceList[b][2]
                    local nIdxB = (faceArray[fB])[cB] as integer
                    
                    if nIdxA >= 1 and nIdxA <= normalArray.count and \
                       nIdxB >= 1 and nIdxB <= normalArray.count then
                    (
                        if (distance normalArray[nIdxA] normalArray[nIdxB]) < normalThreshold then
                        (
                            local rootA = fA
                            while parent[rootA] != rootA do
                            (
                                parent[rootA] = parent[parent[rootA]]
                                rootA = parent[rootA]
                            )
                            local rootB = fB
                            while parent[rootB] != rootB do
                            (
                                parent[rootB] = parent[parent[rootB]]
                                rootB = parent[rootB]
                            )
                            if rootA != rootB then parent[rootA] = rootB
                        )
                    )
                )
            )
        )
        
        -- Gruppen -> Smooth Group Bits zuweisen
        local groupDict = Dictionary #integer
        local nextSG = 1
        
        for f = 1 to numFaces do
        (
            local root = f
            while parent[root] != root do
            (
                parent[root] = parent[parent[root]]
                root = parent[root]
            )
            
            local sg
            if hasDictValue groupDict root then
                sg = groupDict[root]
            else
            (
                sg = nextSG
                groupDict[root] = sg
                if nextSG < 32 then nextSG += 1
                else nextSG = 1
            )
            setFaceSmoothGroup obj f (bit.set 0 sg true)
        )
        
    ),

    fn createMeshWc3 g =
    (
        local origFaces = deepCopy g.faces
        
        fn createVertexMap obj tverts map uvfaces =
        (
            local chan = map
            local tvertices = tverts
            
            meshop.setMapSupport obj chan true
            meshop.setNumMapVerts obj chan tvertices.count
            local func = meshop.setMapVert
            
            for i = 1 to tvertices.count do
            (
                local tv = [tvertices[i].x, tvertices[i].y, 0]
                func obj chan i tv
            )
            
            func = meshop.setMapFace
            for i = 1 to uvfaces.count do
                func obj chan i uvfaces[i]
        )
        
        local numV = g.vertices.count
        local posThreshold = 0.001
        local normalThreshold = 0.01
        local gridSize = 0.01
        
        local grid = Dictionary #integer
        
        local cellCX = #()
        local cellCY = #()
        local cellCZ = #()
        cellCX[numV] = undefined
        cellCY[numV] = undefined
        cellCZ[numV] = undefined
        
        for i = 1 to numV do
        (
            local pos = g.vertices[i]
            cellCX[i] = (floor (pos.x / gridSize)) as integer
            cellCY[i] = (floor (pos.y / gridSize)) as integer
            cellCZ[i] = (floor (pos.z / gridSize)) as integer
        )
        
        for i = 1 to numV do
        (
            local key = cellCX[i] * 73856093 + cellCY[i] * 19349663 + cellCZ[i] * 83492791
            if not (hasDictValue grid key) then
                grid[key] = #(i)
            else
                append grid[key] i
        )
        
        local vertexRemap = #()
        vertexRemap[numV] = undefined
        for i = 1 to numV do vertexRemap[i] = -1
        
        local newVertices = #()
        local newNormals = #()
        local newMatrixIndices = #()
        local newSkinData = #()                                    -- NEU: Reforged SKIN
        local hasSkin = g.skinData != undefined and g.skinData.count == numV  -- NEU
        local currentIdx = 0
        
        for i = 1 to numV do
        (
            if vertexRemap[i] == -1 then
            (
                currentIdx += 1
                vertexRemap[i] = currentIdx
                append newVertices g.vertices[i]
                append newNormals g.normals[i]
                append newMatrixIndices g.matrixindices[i]
                if hasSkin then append newSkinData g.skinData[i]   -- NEU: skinData mit-remappen
                
                local cx = cellCX[i]
                local cy = cellCY[i]
                local cz = cellCZ[i]
                
                for dx = -1 to 1 do
                for dy = -1 to 1 do
                for dz = -1 to 1 do
                (
                    local nkey = (cx+dx) * 73856093 + (cy+dy) * 19349663 + (cz+dz) * 83492791
                    if hasDictValue grid nkey then
                    (
                        for j in grid[nkey] where j > i and vertexRemap[j] == -1 do
                        (
                            if (distance g.vertices[i] g.vertices[j]) < posThreshold and \
                               (distance g.normals[i] g.normals[j]) < normalThreshold then
                                vertexRemap[j] = currentIdx
                        )
                    )
                )
            )
        )
        
        -- Geometry-Faces remappen
        local remappedFaces = deepCopy origFaces
        for f = 1 to remappedFaces.count do
        (
            remappedFaces[f][1] = vertexRemap[remappedFaces[f][1]]
            remappedFaces[f][2] = vertexRemap[remappedFaces[f][2]]
            remappedFaces[f][3] = vertexRemap[remappedFaces[f][3]]
        )
        
        -- Geoset-Daten aktualisieren fuer Skinning
        g.vertices = newVertices
        g.normals = newNormals
        g.matrixindices = newMatrixIndices
        g.faces = remappedFaces
        if hasSkin then g.skinData = newSkinData                   -- NEU: geweldete skinData speichern
        
        local obj = mesh vertices:newVertices faces:remappedFaces
        g.mesh = obj
        
        for i = 1 to newNormals.count do
            setNormal obj i newNormals[i]
        
        this.calculateSmoothGroups obj newNormals remappedFaces
        
        -- Reforged: Map Channels fuer mehrere UV-Sets vorab allozieren
        if g.numUVWmaps > 1 then meshop.setNumMaps obj (g.numUVWmaps + 1) keep:true
        
        -- UVs mit Original-Faces (Tvertex-Indices bleiben unveraendert)
        for i = 1 to g.numUVWmaps do
            createVertexMap obj g.tvertices[i] i origFaces
        
        obj.material = g.material.maxObj
        
        -- Additiv: Edit_Normals Modifier fuer per-face-corner Normals
        if g.ex != undefined then
        (
            try
            (
                local enMod = Edit_Normals()
                addModifier obj enMod
                select obj
                max modify mode
                modPanel.setCurrentObject enMod
                
                local numNormals = enMod.GetNumNormals()
                if numNormals > 0 then
                (
                    enMod.SetSelection #{1..numNormals}
                    enMod.Break()
                )
                
                for f = 1 to obj.numFaces do
                (
                    local faceNorms = g.ex[f]
                    if faceNorms != undefined then
                    (
                        for corner = 1 to 3 do
                        (
                            local nID = enMod.GetNormalID f corner
                            if nID != undefined and nID > 0 then
                            (
                                enMod.SetNormal nID faceNorms[corner]
                                enMod.SetNormalExplicit nID explicit:true
                            )
                        )
                    )
                )
                
                max create mode
            )
            catch
            (
                try ( deleteModifier obj enMod ) catch()
                max create mode
            )
        )
        
        obj
    ),

    fn createMeshMax g =
    (
        fn createVertexMap obj tvfaces tverts map =
        (
            local tvertices = tverts
            
            meshop.setMapSupport obj map true
            meshop.setNumMapVerts obj map tvertices.count
            local func = meshop.setMapVert
            
            for i = 1 to tvertices.count do
            (
                local tv = [tvertices[i].x, tvertices[i].y, 0]
                func obj map i tv
            )
            
            func = meshop.setMapFace
            
            for i = 1 to tvfaces.count do
                func obj map i tvfaces[i]
        )
        
        local obj = mesh vertices:g.vertices faces:g.faces
        g.mesh = obj
        
        for i = 1 to g.normals.count do
            setNormal obj i g.normals[i]
        
        local faceArr = #()
        faceArr[obj.numFaces] = undefined
        for f = 1 to obj.numFaces do
            faceArr[f] = getFace obj f
        
        this.calculateSmoothGroups obj g.normals faceArr
        
        if g.numUVWmaps > 1 then meshop.setNumMaps obj (g.numUVWmaps + 1) keep:true
        
        for i = 1 to g.numUVWmaps do
        (
            createVertexMap g.mesh g.tvfaces[i] g.tvertices[i] i
        )
        
        obj.material = g.material.maxObj
        
        -- Additiv: Edit_Normals Modifier für per-face-corner Normals
        -- Kommt NACH setNormal + calculateSmoothGroups + UVs
        if g.ex != undefined then
        (
            try
            (
                local enMod = Edit_Normals()
                addModifier obj enMod
                select obj
                max modify mode
                modPanel.setCurrentObject enMod
                
                local numNormals = enMod.GetNumNormals()
                if numNormals > 0 then
                (
                    enMod.SetSelection #{1..numNormals}
                    enMod.Break()
                )
                
                for f = 1 to obj.numFaces do
                (
                    local faceNorms = g.ex[f]
                    if faceNorms != undefined then
                    (
                        for corner = 1 to 3 do
                        (
                            local nID = enMod.GetNormalID f corner
                            if nID != undefined and nID > 0 then
                            (
                                enMod.SetNormal nID faceNorms[corner]
                                enMod.SetNormalExplicit nID explicit:true
                            )
                        )
                    )
                )
                
                max create mode
            )
            catch
            (
                try ( deleteModifier obj enMod ) catch()
                max create mode
            )
        )
        
        obj
    ),
    
    fn createMesh g =
    (
        if classof g == Wc3Geoset then 
            this.createMeshWc3 g
        else 
            this.createMeshMax g
    ),
    
    -- ============================================================================
    -- SKINNING FUNCTIONS
    -- ============================================================================
    
    fn applyBindPoseToSkeleton =
    (
        if this.bindPoses == undefined or this.bindPoses.count == 0 then return false
        
        for i = 1 to this.mdlObjects.count do
        (
            local obj = this.mdlObjects[i]
            if obj == undefined or obj.maxObj == undefined then continue
            if i > this.bindPoses.count then continue
            local bp = this.bindPoses[i]
            if bp == undefined then continue
            
            try ( obj.maxObj.transform = bp ) catch ()
        )
        true
    ),
    
    fn setBindPoseKeyAtFrame0 =
    (
        if this.bindPoses == undefined or this.bindPoses.count == 0 then return false
        
        animate on at time 0
        (
            for i = 1 to this.mdlObjects.count do
            (
                local obj = this.mdlObjects[i]
                if obj == undefined or obj.maxObj == undefined then continue
                if i > this.bindPoses.count then continue
                
                local trans = obj.transformations
                if trans == undefined then continue
                local hasAnim = (trans.rotation != undefined) or (trans.translation != undefined) or (trans.scale != undefined)
                if not hasAnim then continue
                
                local bp = this.bindPoses[i]
                if bp == undefined then continue
                
                try ( obj.maxObj.transform = bp ) catch ()
            )
        )
        true
    ),

    -- v800 only: resolves mdlObject IDs in geo.matrices to bone maxObjs.
    -- IMPORTANT: mutates geo.matrices in-place, replacing objIds with
    -- 1-based skin modifier indices so applySkinning can use them directly.
    fn getBones geo =
    (
        local bns = #()
        local boneLookup = Dictionary #integer

        for m in geo.matrices do
            for i = 1 to m.count do
            (
                local b = this.mdlObjects[m[i]].maxObj
                local handle = getHandleByAnim b
                local index
                if hasDictValue boneLookup handle then
                    index = boneLookup[handle]
                else
                (
                    append bns b
                    index = bns.count
                    boneLookup[handle] = index
                )
                m[i] = index  -- mutate: replace objId with skin index
            )

        bns
    ),

    fn applySkinning g =
    (
        local obj = g.mesh
        g.mesh = obj

        select obj
        max modify mode

        local hasSkin = g.skinData != undefined and g.skinData.count > 0

        if hasSkin then
        (
            -- ==========================================
            -- Reforged: Per-Vertex SKIN Weights
            -- ==========================================

            -- Flat Bone-Liste aus Matrices BEVOR getBones sie remapped
            local flatBoneIds = #()
            for m in g.matrices do
                for bid in m do
                    append flatBoneIds bid

            -- Unique Bones sammeln und Skin Modifier aufbauen
            local boneObjList = #()
            local boneHandleMap = Dictionary #integer

            for fi = 1 to flatBoneIds.count do
            (
                local objId = flatBoneIds[fi]
                if objId >= 1 and objId <= this.mdlObjects.count and this.mdlObjects[objId] != undefined then
                (
                    local boneObj = this.mdlObjects[objId].maxObj
                    if boneObj != undefined then
                    (
                        local h = getHandleByAnim boneObj
                        if not (hasDictValue boneHandleMap h) then
                        (
                            append boneObjList boneObj
                            boneHandleMap[h] = boneObjList.count
                        )
                    )
                )
            )

            local SkinMod = Skin()
            addModifier obj SkinMod
            modPanel.setCurrentObject SkinMod
            for b in boneObjList do skinOps.addBone SkinMod b 0
            update obj
            max views redraw

            -- Flat-Index -> Skin-Modifier-Index Lookup
            local flatToSkin = #()
            if flatBoneIds.count > 0 then flatToSkin[flatBoneIds.count] = 0
            for fi = 1 to flatBoneIds.count do
            (
                local objId = flatBoneIds[fi]
                if objId >= 1 and objId <= this.mdlObjects.count and this.mdlObjects[objId] != undefined then
                (
                    local boneObj = this.mdlObjects[objId].maxObj
                    if boneObj != undefined then
                    (
                        local h = getHandleByAnim boneObj
                        if hasDictValue boneHandleMap h then
                            flatToSkin[fi] = boneHandleMap[h]
                        else
                            flatToSkin[fi] = 1
                    )
                    else flatToSkin[fi] = 1
                )
                else flatToSkin[fi] = 1
            )

            -- Per-Vertex Weights zuweisen
            local nFlatBones = flatBoneIds.count
            for i = 1 to g.vertices.count do
            (
                local sv = g.skinData[i]
                local bIdx = #()
                local bWgt = #()

                for j = 1 to 4 do
                (
                    if sv.weights[j] > 0 then
                    (
                        local fi = sv.bones[j] + 1   -- 0-based SKIN -> 1-based flat
                        if fi >= 1 and fi <= nFlatBones then
                        (
                            local si = flatToSkin[fi]
                            if si != undefined and si > 0 then
                            (
                                append bIdx si
                                append bWgt (sv.weights[j] / 255.0)
                            )
                        )
                    )
                )

                if bIdx.count > 0 then
                    skinOps.ReplaceVertexWeights SkinMod i bIdx bWgt
            )
        )
        else
        (
            -- ==========================================
            -- v800: Equal-Weights pro Matrix-Gruppe
            -- getBones mutiert g.matrices in-place:
            -- ersetzt mdlObjects-IDs durch Skin-Modifier-Indizes
            -- ==========================================

            local bns = this.getBones g

            if g.matrixIndices == undefined or g.matrixIndices.count == 0 then return()

            local SkinMod = Skin ()
            addModifier obj SkinMod
            modPanel.setCurrentObject SkinMod

            local func = skinOps.addBone
            for b in bns do func SkinMod b 0

            update obj
            max views redraw

            -- Weight-Arrays pro Matrix-Gruppe cachen statt pro Vertex neu erzeugen
            local weightCache = Dictionary #integer

            func = skinOps.ReplaceVertexWeights
            for i = 1 to g.vertices.count do
            (
                local matIdx = g.matrixIndices[i]
                local matrix = g.matrices[matIdx]  -- nach getBones: skin-Indizes

                local weights
                if hasDictValue weightCache matIdx then
                    weights = weightCache[matIdx]
                else
                (
                    local weight = (1.0 / (matrix.count as float))
                    weights = ::Utils.ArrayRepeat matrix.count weight
                    weightCache[matIdx] = weights
                )

                func SkinMod i matrix weights
            )
        )

        max create mode
    ),
    
    -- ============================================================================
    -- LAYER MANAGEMENT
    -- ============================================================================
    
    fn getOrCreateLayerIfNeeded layerName =
    (
        local layer = LayerManager.getLayerFromName layerName
        if layer == undefined then
            layer = LayerManager.newLayerFromName layerName
        layer
    ),
    
    -- ============================================================================
    -- GEOSET RECREATION
    -- ============================================================================
    
    fn recreateGeosets geos =
    (
        if geos.count > 0 then
        (
            -- Sammle Meshes nach LOD-Stufe
            local lodBuckets = Dictionary #integer   -- lod -> #(meshes)
            
            for g in geos do
            (
                local mesh = this.createMesh g
                if mesh != undefined then
                (
                    -- Mesh benennen: lodName falls vorhanden, sonst Geoset_ID
                    if g.lodName != undefined and g.lodName != "" then
                    (
                        mesh.name = g.lodName + "_" + (g.id as string)
                        setUserProp mesh "Wc3LodName" g.lodName
                    )
                    else
                        mesh.name = "Geoset_" + (g.id as string)
                    
                    local lodLevel = if g.lod != undefined then g.lod else 0
                    
                    if not (hasDictValue lodBuckets lodLevel) then
                        lodBuckets[lodLevel] = #()
                    append lodBuckets[lodLevel] mesh
                )
            )
            
            -- Layer erstellen und Meshes zuweisen
            local lodKeys = getDictKeys lodBuckets
            sort lodKeys
            
            for lodLevel in lodKeys do
            (
                local layerName = if lodLevel == 0 then "Geosets"
                                  else ("Geosets_LOD" + (lodLevel as string))
                
                local layer = this.getOrCreateLayerIfNeeded layerName
                
                for mesh in lodBuckets[lodLevel] do
                    layer.addNode mesh
                
                -- LOD > 0 standardmaessig verstecken
                if lodLevel > 0 then
                    layer.isHidden = true
            )
        )
    ),
    
    -- ============================================================================
    -- OBJECT CREATION FUNCTIONS
    -- ============================================================================
    
    fn setupNode obj n =
    (
        ::Utils.setBillboardFlags n #(obj.billboarded, obj.billboardedLockX, obj.billboardedLockY, obj.billboardedLockZ)
        setUserProp n "CameraAnchored" (::Utils.B2I obj.cameraAnchored)
        
        -- DontInherit ueber native Max Inheritance Flags setzen
        if obj.dontInheritTranslation or obj.dontInheritRotation or obj.dontInheritScale then
        (
            local f = #{1..9}
            if obj.dontInheritTranslation then ( f[1] = false; f[2] = false; f[3] = false )
            if obj.dontInheritRotation then ( f[4] = false; f[5] = false; f[6] = false )
            if obj.dontInheritScale then ( f[7] = false; f[8] = false; f[9] = false )
            setInheritanceFlags n f
        )
    ),
    
    fn createBone obj =
    (
        local p
        if ::WhiteoutDexImportSettings.importBonesAsMaxBones then
        (
            p = BoneSys.createBone [0,0,0] [0,0,0] [0,0,0.1]
            p.width = 0
            p.height = 0
            p.showLinks = true
            p.boneScaleType = #none
            
            p.pos = obj.pivot
            p.name = obj.name
        )
        else
            p = point pos:obj.pivot name:obj.name
        
        obj.maxObj = p
        
        this.setupNode obj p
        
        p
    ),
    
    fn createHelper obj pSize:20 =
    (
        local p
        
        if ::WhiteoutDexImportSettings.importHelpersAsPointHelpers then
        (
            p = point name:obj.name \
                    pos:obj.pivot \
                    box:true \
                    size:pSize
            p.wirecolor = green
        )
        else
        (
            if ::WhiteoutDexImportSettings.importBonesAsMaxBones then
            (
                p = BoneSys.createBone [0,0,0] [0,0,0] [0,0,0.1]
                p.width = 0
                p.height = 0
                p.showLinks = true
                p.boneScaleType = #none
                p.pos = obj.pivot
                p.name = obj.name
            )
            else
            (
                p = point pos:obj.pivot name:obj.name
            )
        )
        
        obj.maxObj = p
        this.setupNode obj p
        p
    ),
    
    fn getAttachmentValues name =
    (
        fn searchString sarr s =
        (
            local index = 1
            for i = 1 to sarr.count where matchPattern s pattern:sarr[i] do index = i
            index
        )
        
        -- "Ref" am Ende entfernen
        local cleaned = name
        if cleaned.count > 4 and (substring cleaned (cleaned.count - 3) 4) == " Ref" then
            cleaned = substring cleaned 1 (cleaned.count - 4)
        
        -- Erstes Wort = Type
        local s1 = filterString cleaned " "
        local n1 = searchString attachmentList s1[1]
        
        if s1.count <= 1 then return #(n1)
        
        -- Restlichen String nach dem Type-Wort
        local remainder = substring cleaned (s1[1].count + 2) -1
        
        -- Zuerst in attachmentAdd1List suchen (inkl. "Mount Left" etc.)
        local n2 = 1
        local n3 = 1
        local add1Match = ""
        
        for i = 2 to attachmentAdd1List.count do
        (
            local item = attachmentAdd1List[i]
            if remainder.count >= item.count and (substring remainder 1 item.count) == item then
            (
                if item.count > add1Match.count then
                (
                    add1Match = item
                    n2 = i
                )
            )
        )
        
        -- Rest nach add1 in attachmentAdd2List suchen
        if add1Match != "" and remainder.count > (add1Match.count + 1) then
        (
            local remainder2 = substring remainder (add1Match.count + 2) -1
            n3 = searchString attachmentAdd2List remainder2
        )
        else if add1Match == "" then
        (
            -- Kein add1 Match, vielleicht ist es ein add2?
            n3 = searchString attachmentAdd2List remainder
        )
        
        #(n1, n2, n3)
    ),
    
    fn createLight obj =
    (
        local sub = obj.sub

        local l = Wc3_Light pos:obj.pivot name:obj.name

        obj.maxObj = l

        case (sub.lightType) of
        (
            #Omnidirectional: l.LightType = 1
            #Directional: l.LightType = 2
            #Ambient: l.LightType = 3
            default: l.LightType = 1
        )

        if sub.visibility != undefined then
        (
            if sub.visibility.type == #Static then
            (
                l.visibility = sub.visibility.data
            )
            else if sub.visibility.type == #Animated and ::WhiteoutDexImportSettings.importVisAnim then 
            (
                this.applyFloatParamAnimations l sub.visibility.data 0.0 #visibility
            )
        )

        this.applyFloatParameter l "DecayStart" sub.attenuationStart
        this.applyFloatParameter l "DecayEnd" sub.attenuationEnd

        this.applyColorParameter l "AmbColor" sub.color
        this.applyFloatParameter l "AmbValue" sub.intensity
        
        this.applyColorParameter l "ShadowColor" sub.ambColor
        this.applyFloatParameter l "ShadowValue" sub.ambIntensity

        this.setupNode obj l

        l
    ),
    
    fn createAttachment obj =
    (
        local p = Wc3_AttachPoint pos:obj.pivot name:obj.name wirecolor:red
        obj.maxObj = p
        
        local sub = obj.sub
        local flags = this.getAttachmentValues obj.name
        
        if flags[1] != undefined then p.nameFirst = flags[1]
        if flags[2] != undefined then p.nameAdd1 = flags[2]
        if flags[3] != undefined then p.nameAdd2 = flags[3]
        
        -- OutPath / External Model
        if sub.outPath != undefined and sub.outPath != "" then
        (
            p.usesExternalModel = true
            p.externalModelPath = sub.outPath
        )
        
        -- AttachmentId
        if sub.attachmentId != undefined then
        (
            p.usesAttachmentId = true
            p.attachmentId = sub.attachmentId
        )
        
        -- Visibility
        if sub.visibility != undefined then
        (
            if sub.visibility.type == #Static then
                p.A_Visibility = sub.visibility.data
            else if sub.visibility.type == #Animated and ::WhiteoutDexImportSettings.importVisAnim then
                this.applyFloatParamAnimations p sub.visibility.data 0.0 #A_Visibility
        )
        
        this.setupNode obj p
        p
    ),
    
    fn createParticleEmitter obj =
    (
        local sub = obj.sub
        local p
        
        local particlePath = if sub.path != "" and sub.path != undefined then getFilenamePath sub.path else "war3mapImported\\"
        local particlePart = if sub.path != "" and sub.path != undefined then getFilenameFile sub.path else "MissingParticle"
        
        if (sub.path == "" or sub.path == undefined) then
            format "Warning: Particle Emitter 1 '%' has no texture path. Using default placeholder.\n" obj.name to:listener
        
        try
        (
            p = BlizzParticle1 pos:obj.pivot name:obj.name
            
            p.path = particlePath
            p.Part = particlePart
        )
        catch
        (
            format "ERROR: Failed to create BlizzParticle1 for '%'. Creating dummy point instead. Error: %\n" obj.name (getCurrentException()) to:listener
            
            p = point pos:obj.pivot name:(obj.name + "_PE1_Failed") wirecolor:red box:true size:10
            obj.maxObj = p
            this.setupNode obj p
            return p
        )
        
        obj.maxObj = p
        
        this.applyFloatParameter p "radius" sub.initVelocity
        this.applyFloatParameter p "longitude" sub.longitude
        this.applyFloatParameter p "latitude" sub.latitude
        this.applyFloatParameter p "Life" sub.lifespan
        this.applyFloatParameter p "Gravity" sub.gravity
        this.applyFloatParameter p "PartEmit" sub.emissionRate
        
        if sub.visibility != undefined then
        (
            if sub.visibility.type == #Static then
                p.visibility = sub.visibility.data
            else if (sub.visibility.type == #Animated) and (::WhiteoutDexImportSettings.importVisAnim) then
                this.applyFloatParamAnimations p sub.visibility.data 0.0 #visibility
        )
        
        this.setupNode obj p
        p
    ),
    
    fn createParticleEmitter2 obj =
    (
        local originalPosition = obj.pivot
        
        local initialNodeRotation = (quat 0 0 0 1)
        local hasRotation = false
        if obj.transformations != undefined and obj.transformations.rotation != undefined then
        (
            local rotData = obj.transformations.rotation
            if rotData.type == #Static then
            (
                if (isKindOf rotData.data Quat) then
                (
                    initialNodeRotation = rotData.data
                    hasRotation = true
                )
            )
            else if rotData.keys != undefined and rotData.keys.count > 0 then
            (
                if (isKindOf rotData.keys[1].value Quat) then
                (
                    initialNodeRotation = rotData.keys[1].value
                    hasRotation = true
                )
            )
        )
        local p
        try
        (
            p = BlizzParticle2 pos:originalPosition rotation:initialNodeRotation name:obj.name
            
            if p.pos != originalPosition then
            (
                p.pos = originalPosition
            )
        )
        catch
        (
            format "FEHLER: Konnte BlizzParticle2 fuer '%' nicht erstellen. Erzeuge Dummy. Ursache: %\n" obj.name (getCurrentException()) to:listener
            p = point pos:originalPosition name:(obj.name + "_PE2_Dummy") wirecolor:red
            obj.maxObj = p
            this.setupNode obj p
            return p
        )
        obj.maxObj = p
        local sub = obj.sub
        this.applyFloatParameter p "speed" sub.speed
        this.applyFloatParameter p "variation" sub.variation
        this.applyFloatParameter p "coneangle" sub.latitude
        this.applyFloatParameter p "gravity" sub.gravity
        p.life = sub.lifeSpan
        this.applyFloatParameter p "PartsEmit" sub.emissionRate
        p.Squirt = sub.squirt
        this.applyFloatParameter p "width" sub.width
        this.applyFloatParameter p "length" sub.length
        if sub.lineEmitter then
            p.LineEmitter = 2
        else
            p.LineEmitter = 1
        p.rtexture = sub.replaceableId
        -- Textur aufloesen: sub.texture kann Integer (v800) oder Wc3Texture Struct (v1200) sein
        try
        (
            local tex = sub.texture
            local texPath = undefined
            if tex != undefined then
            (
                if isKindOf tex Integer then
                (
                    -- v800: tex ist 1-basierter Index in model.textures
                    local model = ::WhiteoutDexImportedModel
                    if tex > 0 and model != undefined and model.textures != undefined and tex <= model.textures.count then
                        texPath = model.textures[tex].path
                )
                else if isProperty tex #path then
                (
                    -- v1200: tex ist bereits ein Wc3Texture Struct
                    texPath = tex.path
                )
            )
            if texPath != undefined and texPath != "" then
            (
                local filename = getFilenameFile texPath
                local layPath = getFilenamePath texPath
                if layPath == undefined then layPath = ""
                local finalFile = this.FindTexture filename mdxRelPath:texPath
                p.texture = finalFile
                p.path = layPath
            )
        )
        catch
        (
            format "Warning: Could not resolve texture for ParticleEmitter2 '%': %\n" obj.name (getCurrentException())
        )
        p.filtermode = sub.filtermode
        p.rows = sub.rows
        p.cols = sub.columns
        p.Unshaded = sub.unshaded
        p.Unfogged = sub.unfogged
        p.SortZ = sub.sortPrimsFarZ
        -- FIX: v1200 speichert particleType als Name (#Head/#Tail/#Both), Plugin erwartet Integer
        -- v800 liefert bereits Integer (1/2/3), diese werden im default-Fall durchgereicht
        p.particleType = case sub.particleType of (#Head: 1; #Tail: 2; #Both: 3; default: sub.particleType)
        p.taillength = sub.tailLength
        p.midtime = sub.time
        p.startcolor = sub.startColor
        p.middlecolor = sub.midColor
        p.endcolor = sub.endColor
        p.startalpha = sub.alpha.x
        p.middlealpha = sub.alpha.y
        p.endalpha = sub.alpha.z
        p.startscale = sub.scaling.x
        p.middlescale = sub.scaling.y
        p.endscale = sub.scaling.z
        p.startLifespanHead = sub.lifeSpanUVAnim.x
        p.endLifespanHead = sub.lifeSpanUVAnim.y
        p.repeatLifespanHead = sub.lifeSpanUVAnim.z
        p.startDecayHead = sub.decayUVAnim.x
        p.endDecayHead = sub.decayUVAnim.y
        p.repeatDecayHead = sub.decayUVAnim.z
        p.startLifespanTail = sub.TailUVAnim.x
        p.endLifespanTail = sub.TailUVAnim.y
        p.repeatLifespanTail = sub.TailUVAnim.z
        p.startDecayTail = sub.TailDecayUVAnim.x
        p.endDecayTail = sub.TailDecayUVAnim.y
        p.repeatDecayTail = sub.TailDecayUVAnim.z
        p.priorityplane = sub.priorityPlane
        p.ParticleSpace = sub.modelSpace
        p.XYQuads = sub.xyQuad
        this.setupNode obj p
        
        if p.pos != originalPosition then
        (
            p.pos = originalPosition
        )
        
        p
    ),
    
    fn createRibbonEmitter obj =
    (
        local r
        try ( r = BlizRibbon pos:obj.pivot name:obj.name )
        catch
        (
            format "ERROR: Could not create BlizRibbon for '%': %\n" obj.name (getCurrentException())
            return undefined
        )

        obj.maxObj = r

        local sub = obj.sub

        this.applyFloatParameter r "above" sub.heightAbove
        this.applyFloatParameter r "below" sub.heightBelow
        this.applyFloatParameter r "alpha" sub.alpha
        this.applyFloatParameter r "slots" sub.textureSlot
        this.applyColorParameter r "VertexColor" sub.color

        r.emission = sub.emissionRate
        r.life = sub.lifeSpan
        r.gravity = sub.gravity
        r.rows = sub.rows
        r.columns = sub.columns

        (
            local matNode = undefined
            
            try
            (
                if (sub.material != undefined and sub.material != 0) then
                (
                    if isProperty sub.material #maxObj then
                    (
                        matNode = sub.material.maxObj
                    )
                    else if isKindOf sub.material Material then
                    (
                        matNode = sub.material
                    )
                    else
                    (
                        matNode = undefined
                    )
                )
            ) catch( format "⚠ SceneRebuilder: Failed to resolve material for RibbonEmitter '%': %\n" obj.name (getCurrentException()) )

            if matNode != undefined then
            (
                r.rmaterial = matNode
                
                -- Ribbon-Plugin Properties vom Material zuruecklesen
                -- isLoading verhindert dass on-texture-set die Werte zurueck ins Material pushed
                try
                (
                    r.isLoading = true
                    if isProperty matNode #filtermode then r.filtermode = matNode.filtermode
                    if isProperty matNode #twosides then r.twosides = matNode.twosides
                    if isProperty matNode #unshaded then r.unshaded = matNode.unshaded
                    if isProperty matNode #unfogged then r.unfogged = matNode.unfogged
                    if isProperty matNode #PriorityPlane then r.PriorityPlane = matNode.PriorityPlane
                    if isProperty matNode #path then r.path = matNode.path
                    if isProperty matNode #texture then r.texture = matNode.texture
                    r.isLoading = false
                ) catch ( r.isLoading = false )
            )
            else
            (
                format "No Material ID found for RibbonEmitter '%'\n" obj.name
            )
        )

        this.setupNode obj r
        r
    ),
    
    fn createCornEmitter obj =
    (
        local originalPosition = obj.pivot

        local initialNodeRotation = (quat 0 0 0 1)
        local hasRotation = false

        if obj.transformations != undefined and obj.transformations.rotation != undefined then
        (
            local rotData = obj.transformations.rotation
            if rotData.type == #Static then
            (
                if (isKindOf rotData.data Quat) then
                (
                    initialNodeRotation = rotData.data
                    hasRotation = true
                )
            )
            else if rotData.keys != undefined and rotData.keys.count > 0 then
            (
                if (isKindOf rotData.keys[1].value Quat) then
                (
                    initialNodeRotation = rotData.keys[1].value
                    hasRotation = true
                )
            )
        )

        local p
        try
        (
            p = BlizzPopcorn pos:originalPosition rotation:initialNodeRotation name:obj.name
        )
        catch
        (
            format "ERROR: Could not create BlizzPopcorn for '%'. Creating dummy. Error: %\n" obj.name (getCurrentException()) to:listener
            p = point pos:originalPosition name:(obj.name + "_Corn_Dummy") wirecolor:(color 102 255 178)
            obj.maxObj = p
            this.setupNode obj p
            return p
        )

        obj.maxObj = p

        local sub = obj.sub

        -- Popcorn FX Pfad
        if sub.path != undefined then p.popcornPath = sub.path

        -- Alpha: animiert oder statisch
        -- Plugin und MDX beide 0-1, keine Skalierung noetig
        if sub.alphaAnim != undefined and sub.alphaAnim.type == #Animated then
            this.applyFloatParamAnimations p sub.alphaAnim.data 0.0 #alpha
        else if sub.alpha != undefined then
            p.alpha = sub.alpha
        -- EmissionRate: animiert oder statisch
        if sub.emissionRateAnim != undefined and sub.emissionRateAnim.type == #Animated then
            this.applyFloatParamAnimations p sub.emissionRateAnim.data 0.0 #emissionRate
        else if sub.emissionRate != undefined then
            p.emissionRate = sub.emissionRate
        if sub.lifeSpan != undefined then p.lifeSpan = sub.lifeSpan
        if sub.speed != undefined then p.speed = sub.speed
        if sub.replaceableId != undefined then p.replaceableId = sub.replaceableId

        -- Farbe (Reader liefert bereits 0-255, nicht nochmal multiplizieren)
        if sub.color != undefined then
        (
            try
            (
                local c = sub.color
                if isKindOf c Point3 then
                (
                    p.baseColor    = color c.x c.y c.z
                    p.YS_baseColor = p.baseColor
                )
                else if isKindOf c Color then
                (
                    p.baseColor    = c
                    p.YS_baseColor = c
                )
            ) catch()
        )

        -- Event Flags parsen (FIX: =on/=off korrekt auswerten, rawFlags speichern)
        if sub.flags != undefined then
        (
            local fl = sub.flags
            if isKindOf fl String then
            (
                -- Original-String speichern fuer verlustfreien Round-Trip
                try ( p.rawFlags = fl ) catch()
                -- Einzelne Eintraege parsen: "Always=off, Birth=on, Stand Work 1=on"
                local parts = filterString fl ","
                for part in parts do
                (
                    local trimmed = trimLeft (trimRight part)
                    local eqPos = findString trimmed "="
                    local flagName = trimmed
                    local flagValue = true
                    if eqPos != undefined then
                    (
                        flagName = trimLeft (trimRight (substring trimmed 1 (eqPos - 1)))
                        local valStr = toLower (trimLeft (trimRight (substring trimmed (eqPos + 1) -1)))
                        flagValue = (valStr == "on" or valStr == "true" or valStr == "1")
                    )
                    case flagName of
                    (
                        "Always":    p.flagAlways    = flagValue
                        "Birth":     p.flagBirth     = flagValue
                        "Death":     p.flagDeath     = flagValue
                        "Dissipate": p.flagDissipate = flagValue
                        "Portrait":  p.flagPortrait  = flagValue
                        default: ()  -- Custom Animation-Namen: in rawFlags erhalten
                    )
                )
            )
            else if isKindOf fl Integer then
            (
                p.flagAlways    = (bit.get fl 1)
                p.flagBirth     = (bit.get fl 2)
                p.flagDeath     = (bit.get fl 3)
                p.flagDissipate = (bit.get fl 4)
                p.flagPortrait  = (bit.get fl 5)
            )
        )

        -- Visibility
        this.applyFloatParameter p "visibility" sub.visibility

        -- Node Setup (Parenting, Transformations)
        this.setupNode obj p

        p
    ),

    fn createEventObject obj =
    (
        local p = Wc3_Event pos:obj.pivot name:("Obj:" + obj.name) wirecolor:this.eventObjColor
        
        obj.maxObj = p

        -- Write event keys directly to the keyList custom attribute (no Note Tracks)
        for k in obj.sub.eventTrack.keys do
        (
            local kTime = if isProperty k #time then k.time else k
            -- Convert to frame number (handle both Time and Float/Integer from readers)
            local frameNum = if classOf kTime == Time then (kTime.frame as integer) \
                             else if classOf kTime == Float then (kTime as integer) \
                             else (kTime as integer)
            -- Prevent duplicate frames
            if (findItem p.baseObject.keyList frameNum) == 0 then
                append p.baseObject.keyList frameNum
        )
        -- Sort keyList (IntTab doesn't support sort directly)
        local tmp = for v in p.baseObject.keyList collect v
        sort tmp
        p.baseObject.keyList = #()
        for v in tmp do append p.baseObject.keyList v
            
        this.setupNode obj p
        
        p
    ),
    
    fn createCollisionShapes obj =
    (
        local p
        local sub = obj.sub

        -- v1200: Collision Shapes haben eigene KGTR/KGRT und sind ROOT-parented
        -- Vertices sind lokal, Position kommt aus dem Pivot
        local hasTransAnim = obj.transformations != undefined and obj.transformations.translation != undefined

        if sub.shape == 2 then
        (
            local spherePos = sub.vertices[1]
            if hasTransAnim then spherePos = spherePos + obj.pivot
            p = Wc3CollisionSphere pos:spherePos radius:sub.boundsRadius name:obj.name wirecolor:blue
        )
        else
        (
            local center = (sub.vertices[1] + sub.vertices[2])*0.5
            center.z = ::Utils.minVal sub.vertices[1].z sub.vertices[2].z

            if hasTransAnim then center = center + obj.pivot

            local l = abs (sub.vertices[1].y - sub.vertices[2].y)
            local w = abs (sub.vertices[1].x - sub.vertices[2].x)
            local h = abs (sub.vertices[1].z - sub.vertices[2].z)

            p = Wc3CollisionBox pos:center width:w length:l height:h name:obj.name wirecolor:blue
        )

        p.pivot = obj.pivot

        obj.maxObj = p

        this.setupNode obj p

        p
    ),
    
    
    -- ============================================================================
    -- OBJECT RECREATION
    -- ============================================================================
    
    fn recreateObjects =
	(
		local bonesList = #()
		local helpersList = #()
		local lightsList = #()
		local attachmentsList = #()
		local eventsList = #()
		local particleEmitter1List = #()
		local particleEmitter2List = #()
		local ribbonEmitterList = #()
		local collisionShapesList = #()
		local cornEmitterList = #()   -- Reforged
		
		for i = 1 to this.mdlObjects.count do
		(
			local obj = this.mdlObjects[i]
			if obj != undefined then
			(
				case obj.sub.type of
				(
					#Wc3Bone:
						if ::WhiteoutDexImportSettings.importBones and \
						   (not ::WhiteoutDexImportSettings.optimizeBonesHelpers or obj.deleted != true) then
						(
							local bone = this.createBone obj
							if bone != undefined then append bonesList bone
						)
					#Wc3Helper:
						if ::WhiteoutDexImportSettings.importHelpers and \
						   (not ::WhiteoutDexImportSettings.optimizeBonesHelpers or obj.deleted != true) then
						(
							local helper = this.createHelper obj
							if helper != undefined then append helpersList helper
						)
					#Wc3Light:
						if ::WhiteoutDexImportSettings.importLights then
						(
							local light = this.createLight obj
							if light != undefined then append lightsList light
						)
					#Wc3Attachment:
						if ::WhiteoutDexImportSettings.importAttachments then
						(
							local attachment = this.createAttachment obj
							if attachment != undefined then append attachmentsList attachment
						)
					#Wc3Event:
						if ::WhiteoutDexImportSettings.importEO then
						(
							local eventObj = this.createEventObject obj
							if eventObj != undefined then append eventsList eventObj
						)
					#Wc3CollisionShape:
						if ::WhiteoutDexImportSettings.importCS then
						(
							local collision = this.createCollisionShapes obj
							if collision != undefined then append collisionShapesList collision
						)
					#Wc3ParticleEmitter:
						if ::WhiteoutDexImportSettings.importPE1 then
						(
							local pe1 = this.createParticleEmitter obj
							if pe1 != undefined then append particleEmitter1List pe1
						)
					#Wc3ParticleEmitter2:
						if ::WhiteoutDexImportSettings.importPE2 then
						(
							local pe2 = this.createParticleEmitter2 obj
							if pe2 != undefined then append particleEmitter2List pe2
						)
					#Wc3RibbonEmitter:
						if ::WhiteoutDexImportSettings.importRE then
						(
							local ribbon = this.createRibbonEmitter obj
							if ribbon != undefined then append ribbonEmitterList ribbon
						)
					-- Reforged: Corn Emitter als BlizzPopcorn erstellen
					#Wc3CornEmitter:
						if ::WhiteoutDexImportSettings.importCornEmitters then
						(
							local corn = this.createCornEmitter obj
							if corn != undefined then append cornEmitterList corn
						)
				)
			)
		)
		
		if bonesList.count > 0 then
		(
			local bonesLayer = this.getOrCreateLayerIfNeeded "Bones"
			for bone in bonesList do bonesLayer.addNode bone
		)
		
		if helpersList.count > 0 then
		(
			local helpersLayer = this.getOrCreateLayerIfNeeded "Helpers"
			for helper in helpersList do helpersLayer.addNode helper
		)
		
		if lightsList.count > 0 then
		(
			local lightsLayer = this.getOrCreateLayerIfNeeded "Lights"
			for light in lightsList do lightsLayer.addNode light
		)
		
		if attachmentsList.count > 0 then
		(
			local attachmentsLayer = this.getOrCreateLayerIfNeeded "Attachments"
			for attachment in attachmentsList do attachmentsLayer.addNode attachment
		)
		
		if eventsList.count > 0 then
		(
			local eventsLayer = this.getOrCreateLayerIfNeeded "Event Objects"
			for eventObj in eventsList do eventsLayer.addNode eventObj
		)
		
		if collisionShapesList.count > 0 then
		(
			local collisionLayer = this.getOrCreateLayerIfNeeded "Collision Objects"
			for collision in collisionShapesList do collisionLayer.addNode collision
		)
		
		local effectsList = particleEmitter1List + particleEmitter2List + ribbonEmitterList
		if effectsList.count > 0 then
		(
			local effectsLayer = this.getOrCreateLayerIfNeeded "Effects"
			for effect in effectsList do effectsLayer.addNode effect
		)
		
		-- Reforged: Corn Emitters in eigenen Layer
		if cornEmitterList.count > 0 then
		(
			local cornLayer = this.getOrCreateLayerIfNeeded "Corn Emitters"
			for corn in cornEmitterList do cornLayer.addNode corn
		)
	),
    
    -- ============================================================================
    -- OBJECT RELINKING
    -- ============================================================================
    
    fn relinkObjects =
    (
        for i = 1 to this.mdlObjects.count do
        (
            local obj = this.mdlObjects[i]
            
            if obj != undefined and obj.maxObj != undefined then
            (
                local parent = obj.parent
                while (parent != undefined and parent.maxObj == undefined) do
                    parent = parent.parent
                
                if parent != undefined then
                    obj.maxObj.parent = parent.maxObj
            )
        )
    ),
    
    fn reapplySkinning geos =
    (
        for g in geos do
            this.applySkinning g
    ),
    
    -- ============================================================================
    -- SEQUENCE RECREATION
    -- ============================================================================
    
    fn recreateSequences sequences =
    (
        -- Remove any legacy note tracks
        while( numNoteTracks rootNode != 0 ) do deleteNoteTrack rootNode (getNoteTrack rootNode 1)
        
        -- Setup Custom Attributes
        ::SequenceStorage.removeCA()
        ::SequenceStorage.ensureCA()
        
        local ftm_cnt = FrameTagManager.CreateNewTag
        local ftm_gti = FrameTagManager.GetTagID
        
        for s in Sequences do
        (
            local adjustedEndTime = s.endTime

            if s.startTime == s.endTime then
            (
                adjustedEndTime = s.endTime + 1f
                format "Warnung: Sequenz '%' Start/Ende auf gleichem Frame (%). Ende auf Frame % verschoben.\n" s.name s.startTime adjustedEndTime to:listener
            )

            local startF = if classOf s.startTime == Time then (s.startTime.frame as integer) else (s.startTime as integer)
            local endF   = if classOf adjustedEndTime == Time then (adjustedEndTime.frame as integer) else (adjustedEndTime as integer)

            append rootNode.seqNames     s.name
            append rootNode.startFrames  startF
            append rootNode.endFrames    endF
            append rootNode.nonLooping   s.loop
            append rootNode.rarity       (if s.rarity != undefined then s.rarity as float else 0.0)
            append rootNode.moveSpeed    (if s.movespeed != undefined then s.movespeed as float else 0.0)
            local extStr = ""
            if s.extents != undefined then
                extStr = (s.extents.bound as string) + " " + (s.extents.minExtent.x as string) + " " + (s.extents.minExtent.y as string) + " " + (s.extents.minExtent.z as string) + " " + (s.extents.maxExtent.x as string) + " " + (s.extents.maxExtent.y as string) + " " + (s.extents.maxExtent.z as string)
            append rootNode.seqExtents extStr
            -- Reforged v1200: Shared Group Info speichern
            append rootNode.sharedGroup (if s.sharedGroup != undefined then s.sharedGroup else "")

            local tag1 = ftm_cnt (s.name + " Start") s.startTime
            local id = ftm_gti tag1
            local tag2 = ftm_cnt (s.name + " End") adjustedEndTime lockID:id
        )
    ),
    
    -- ============================================================================
    -- ANIMATION APPLICATION
    -- ============================================================================
    
    fn applyGeosetAnimations geoAnim =
    (
        if geoAnim.alpha.type == #Animated then
            this.applyVisibilityAnimations geoAnim.geoset.mesh geoAnim.alpha.data
        if geoAnim.usesColor or geoAnim.dropShadow then
        (
            local m = Wc3VertexMod ()
            m.UsesDropShadow = if geoAnim.dropShadow == true then true else false
            m.UsesColor = if geoAnim.usesColor == true then true else false
            addModifier geoAnim.geoset.mesh m
            if geoAnim.usesColor and geoAnim.color != undefined then
                this.applyColorParameter m "VertexColor" geoAnim.color
        )
    ),
    
    fn applyObjectVisibilityAnimations obj =
    (
        if (IsProperty obj.sub #visibility) and obj.sub.visibility.type == #Animated and obj.maxObj != undefined then
            this.applyVisibilityAnimations obj.maxObj obj.sub.visibility.data
    ),
    
    fn animateObjects =
    (
        for m in this.mdlObjects where m != undefined and m.maxObj != undefined do
            this.applyTransformAnimations m
    ),
    
    -- ============================================================================
    -- TCB ROTATION FIX
    -- ============================================================================
    
    fn applyFunc nodes &func2 =
    (
        for node in nodes do
        (
            func2 node
            if node.children.count > 0 then
                this.applyFunc node.children func2
        )
    ),
    
    fn applyFuncToSelection &func =
    (
        fn isselected sel nod =
        (
            for s in sel do
                if nod == s then return true
            false
        )
        
        local rootnodes = #()
        for s in selection do
            if s.parent == undefined or not isselected selection s.parent then
                append rootnodes s
        
        this.applyFunc rootnodes func
    ),
    
    fn fixRotationTCB objs sequences:#() =
    (
        local boundaries = #()
        for seq in sequences do
        (
            appendIfUnique boundaries seq.startTime
            appendIfUnique boundaries seq.endTime
        )
        
        -- ERST fehlende Boundary-Keys auf dem LINEAR Controller einfuegen
        -- GlobalSequence-Objekte ueberspringen: die haben keine Sequence-Grenzen
        for o in objs where o.transformations.rotation.globalSequenceId == undefined do
        (
            local controller = o.maxObj.rotation.controller
            local originalKeys = o.transformations.rotation.keys
            
            for b in boundaries do
            (
                local hasKey = false
                for k in controller.keys where k.time == b do hasKey = true
                
                if not hasKey then
                (
                    local lastVal = undefined
                    for k in originalKeys do
                    (
                        if k.time <= b then lastVal = k.value
                        if k.time > b then exit
                    )
                    
                    if lastVal != undefined then
                    (
                        local newKey = addNewKey controller b
                        newKey.value = lastVal
                    )
                )
            )
        )
        
        -- DANN zu TCB konvertieren
        for o in objs do
            o.maxObj.rotation.controller = tcb_rotation()
        
        for o in objs do
        (
            local controller = o.maxObj.rotation.controller
            
            if o.transformations.rotation.globalSequenceId != undefined then
            (
                setAfterORT controller #cycle
                enableORTs controller true
            )
            else
            (
                if boundaries.count > 0 then
                (
                    local controllerKeys = controller.keys
                    local originalImportKeys = o.transformations.rotation.keys
                    local n = controllerKeys.count
                    if originalImportKeys.count < n then n = originalImportKeys.count
                    
                    for i = 1 to n do
                    (
                        if controllerKeys[i] != undefined and \
                        (findItem boundaries originalImportKeys[i].time) > 0 then
                        (
                            controllerKeys[i].tension = 50.0
                        )
                    )
                    
                    -- Tension fuer die neuen Boundary-Keys setzen
                    for k in controller.keys do
                    (
                        if (findItem boundaries k.time) > 0 then
                            k.tension = 50.0
                    )
                )
            )
            
            -- Original Hermite-Keys als appData speichern fuer verlustfreien Round-Trip
            -- Gilt fuer ALLE Objekte, auch GlobalSequence (sonst Fallback-Resampling beim Export)
            if o.transformations.rotation.type == #Hermite then
            (
                local origKeys = o.transformations.rotation.keys
                local tcbKeys = controller.keys
                
                local tcbVals = ""
                for ti = 1 to tcbKeys.count do
                (
                    local tv = tcbKeys[ti].value as quat
                    local tt = tcbKeys[ti].time as integer
                    if ti > 1 then tcbVals += "|"
                    tcbVals += (tt as string) + ":" + (tv.x as string) + "," + (tv.y as string) + "," + (tv.z as string) + "," + (tv.w as string)
                )
                
                local keyData = "HERMITE_KEYS\n"
                keyData += "TCB_VALS:" + tcbVals + "\n"
                
                for k in origKeys do
                (
                    local tStr = ((k.time as integer) as string)
                    local vStr = (k.value.x as string) + "," + (k.value.y as string) + "," + (k.value.z as string) + "," + (k.value.w as string)
                    local iStr = ""
                    local oStr = ""
                    if k.intan != undefined then
                    (
                        iStr = (k.intan.x as string) + "," + (k.intan.y as string) + "," + (k.intan.z as string) + "," + (k.intan.w as string)
                        oStr = (k.outtan.x as string) + "," + (k.outtan.y as string) + "," + (k.outtan.z as string) + "," + (k.outtan.w as string)
                    )
                    else
                    (
                        iStr = vStr
                        oStr = vStr
                    )
                    keyData += tStr + ";" + vStr + ";" + iStr + ";" + oStr + "\n"
                )
                setAppData o.maxObj 2834 keyData
            )
        )
    ),
    
    fn batchDecodeBLPs =
    (
        makeDir this.blpTemp all:true
        
        -- Collect all BLP files from model directory (recursive)
        local allBlps = #()
        if this.ensureFileSearchCS() then
        (
            local extArr = dotNetObject "System.String[]" 1
            extArr.set 0 ".blp"
            -- Use C# to find all BLPs recursively
            try
            (
                local files = (dotNetClass "System.IO.Directory").GetFiles this.ModelPath "*.blp" (dotNetClass "System.IO.SearchOption").AllDirectories
                for i = 0 to files.Length - 1 do append allBlps (files.get i)
            ) catch()
        )
        
        if allBlps.count == 0 do return 0
        
        -- Filter out already-converted ones
        local toConvert = #()
        for blp in allBlps do
        (
            local baseName = getFilenameFile blp
            local tgaPath = this.blpTemp + baseName + ".tga"
            if not (doesFileExist tgaPath) then append toConvert blp
        )
        
        if toConvert.count == 0 do return 0
        
        local exePath = (systemTools.getEnvVariable "APPDATA") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex\\WhiteoutTexCLI.exe"
        if not (doesFileExist exePath) do return 0
        
        format "WhiteoutDex: Batch converting % BLP files...\n" toConvert.count
        
        -- Copy all BLPs to temp (Unicode-safe) and launch processes in parallel
        local processes = #()
        local maxParallel = 8
        
        for i = 1 to toConvert.count do
        (
            local blp = toConvert[i]
            local baseName = getFilenameFile blp
            local tempBlp = this.blpTemp + baseName + ".blp"
            local tgaPath = this.blpTemp + baseName + ".tga"
            
            try ( (dotNetClass "System.IO.File").Copy blp tempBlp true ) catch ( continue )
            
            local startInfo = dotNetObject "System.Diagnostics.ProcessStartInfo"
            startInfo.FileName = exePath
            startInfo.Arguments = "\"" + tempBlp + "\" \"" + tgaPath + "\""
            startInfo.WindowStyle = (dotNetClass "System.Diagnostics.ProcessWindowStyle").Hidden
            startInfo.CreateNoWindow = true
            startInfo.UseShellExecute = false
            local process = dotNetObject "System.Diagnostics.Process"
            process.StartInfo = startInfo
            process.Start()
            append processes #(process, tempBlp)
            
            -- Throttle: wait when maxParallel reached
            if processes.count >= maxParallel then
            (
                for p in processes do
                (
                    p[1].WaitForExit()
                    try ( deleteFile p[2] ) catch()
                )
                processes = #()
            )
        )
        
        -- Wait for remaining
        for p in processes do
        (
            p[1].WaitForExit()
            try ( deleteFile p[2] ) catch()
        )
        
        format "WhiteoutDex: Batch conversion complete.\n"
        toConvert.count
    ),

    -- ============================================================================
    -- MAIN REBUILD FUNCTION
    -- ============================================================================
    
    fn rebuildSceneModel model originalPath =
	(
		this.rotationTCBObjs = #()
		
		-- BLP Texture Cache leeren (alte TGA-Dateien loeschen)
		try
		(
			local cacheFiles = getFiles (this.blpTemp + "*.tga")
			for f in cacheFiles do try ( deleteFile f ) catch()
		) catch()
		
		undo off (
			with redraw off
			(
				this.AnimatedTextures = 0
				this.ModelPath = originalPath
                this.batchDecodeBLPs()
				
				-- MPQ-Archive oeffnen (wenn aktiviert, nur v800 Classic)
				if ::WhiteoutDexImportSettings.searchMPQ and model.version < 1100 and ::WhiteoutDexMPQ != undefined then
				(
					if ::WhiteoutDexMPQ.mpqDirectory != "" then
						::WhiteoutDexMPQ.scanAndOpenMPQs()
				)
				
				-- CASC-Archive initialisieren (wenn aktiviert, fuer v1200 Reforged)
				if ::WhiteoutDexImportSettings.searchCASC and model.version >= 1100 and ::WhiteoutDexCASC != undefined then
				(
					if not ::WhiteoutDexCASC.isInitialized then
					(
						local cascIni = getDir #plugcfg + "\\WhiteoutDex_Settings.ini"
						local w3dir = getINISetting cascIni "CASC" "W3Path"
						if w3dir != "" then ::WhiteoutDexCASC.init w3dir:w3dir
					)
				)
				
				::Progress.downLevel 5 1
				::Progress.showItem "Building Materials"
				if ::WhiteoutDexImportSettings.importMaterials then this.recreateMaterials model.materials
				::Progress.upLevel ()
				
				::Progress.downLevel 5 1
				::Progress.showItem "Building Meshes"
				this.recreateGeosets model.geosets
				
				this.mdlObjects = model.ex
				if model.bindPoses != undefined then this.bindPoses = model.bindPoses
				-- Original Bind Poses als Name->Matrix Dictionary speichern fuer Round-Trip
				if model.version == 1200 and model.bindPoses != undefined and model.bindPoses.count > 0 then
				(
					local bpDict = Dictionary #string
					local allN = #()
					join allN model.bones
					join allN model.lights
					join allN model.helpers
					join allN model.attachments
					join allN model.particleEmitters1
					join allN model.particleEmitters2
					join allN model.ribbonEmitters
					join allN model.events
					join allN model.CollisionShapes
					if model.cornEmitters != undefined then join allN model.cornEmitters
					for n in allN do
					(
						try
						(
							if n.objId != undefined and n.objId > 0 and n.objId <= model.bindPoses.count then
								bpDict[n.name] = model.bindPoses[n.objId]
						) catch ()
					)
					-- FaceFX: letzte Eintraege nach allen Nodes
					if model.faceFxTargets != undefined then
					(
						local fxBase = allN.count
						for fi = 1 to model.faceFxTargets.count do
						(
							local bpIdx = fxBase + fi
							if bpIdx <= model.bindPoses.count then
								try ( bpDict["FAFX_" + model.faceFxTargets[fi].name] = model.bindPoses[bpIdx] ) catch ()
						)
					)
					::WhiteoutDexOrigBindPoses = bpDict
				)
				::Progress.upLevel ()
				
				::Progress.downLevel 10 1
				::Progress.showItem "Building Objects"
				if ::WhiteoutDexImportSettings.importObjects then this.recreateObjects ()
				if ::WhiteoutDexImportSettings.importObjects then this.relinkObjects ()
				
				-- Reforged: Set bones to bind pose BEFORE skinning
				--if this.bindPoses.count > 0 then this.applyBindPoseToSkeleton()
				
				if ::WhiteoutDexImportSettings.importCameras and model.cameras.count > 0 then
				(
					local camerasList = #()
					for c in model.cameras do
					(
						local targetObj = Targetobject transform:(matrix3 [1,0,0] [0,1,0] [0,0,1] c.targetPos)
						targetObj.name = c.name + "_Target"
						
						local obj = Targetcamera fov:c.fieldOfView nearclip:c.nearClip farclip:c.farClip pos:c.pivot target:targetObj
						obj.name = c.name
						
						if ::WhiteoutDexImportSettings.importAnimations then
						(
							this.applyCameraAnimations obj targetObj c
						)
						
						append camerasList obj
						append camerasList targetObj
					)
					
					if camerasList.count > 0 then
					(
						local camObjLayer = this.getOrCreateLayerIfNeeded "Cameras"
						for cam in camerasList do camObjLayer.addNode cam
					)
				)
				
				::Progress.upLevel ()
			)
			
			::Progress.downLevel 15 1
			::Progress.showItem "Skinning the Meshes"
			if ::WhiteoutDexImportSettings.importSkinning then this.reapplySkinning model.geosets
			::Progress.upLevel ()
			with redraw off
			(
				::Progress.downLevel 15 1
				::Progress.showItem "Animating the Objects"
				if ::WhiteoutDexImportSettings.importAnimations then
				(
					this.recreateSequences model.sequences
					
					this.animateObjects ()
					
					for geoAnim in model.geosetAnims do
						this.applyGeosetAnimations geoAnim
					
					for obj in this.mdlObjects do
						this.applyObjectVisibilityAnimations obj
				)
				::Progress.upLevel ()
			)
			
			max select all
			
			for i = 1 to 3 do
				max zoomext sel all
			
			max views redraw
			
			ClearSelection()
			
			if ::WhiteoutDexImportSettings.importRotAnim then this.fixRotationTCB this.rotationTCBObjs sequences:model.sequences
			
			-- Reforged: Restore bind pose key at frame 0 after animation overwrote controllers
			--if this.bindPoses.count > 0 then this.setBindPoseKeyAtFrame0()
			
			-- Reforged: FaceFX Targets als BlizzFaceFX Helper erstellen
			if ::WhiteoutDexImportSettings.importFaceFX and model.version == 1200 and model.faceFxTargets != undefined and model.faceFxTargets.count > 0 then
			(
				local fxLayer = this.getOrCreateLayerIfNeeded "FaceFX"
				for fx in model.faceFxTargets do
				(
					try
					(
						local p = BlizzFaceFX pos:[0,0,0] name:("FaceFX_" + fx.name)
						p.facefxName = fx.name
						p.facefxPath = fx.path
						fxLayer.addNode p
					)
					catch
					(
						format "ERROR: Could not create BlizzFaceFX for '%': %\n" fx.name (getCurrentException()) to:listener
					)
				)
			)
			-- Reforged: Infos im Listener ausgeben
			if model.version == 1200 then
			(
				-- Position-Keys fuer Emitter/Attachments ohne Translation setzen
				-- Muss ganz am Ende passieren nachdem alle Animationen fertig sind
				-- SliderTime auf Frame 0 setzen um korrekte Ruheposition zu erfassen
				local savedTime = sliderTime
				sliderTime = 0f
				local fixedNodes = 0
				local emitterClasses = #(BlizzParticle2, BlizRibbon, Wc3_AttachPoint)
				try ( append emitterClasses BlizzPopcorn ) catch ()
				try ( append emitterClasses BlizzParticle1 ) catch ()
				for obj in objects do
				(
					local isEmitter = false
					local objClass = classOf obj
					for ec in emitterClasses where objClass == ec do isEmitter = true
					
					if isEmitter and obj.parent != undefined and not obj.isHidden then
					(
						local hasKeys = false
						local posCtrl = obj.pos.controller
						if classOf posCtrl == Position_XYZ then
						(
							for i = 1 to 3 do
							(
								local sub = posCtrl[i]
								if sub != undefined and isProperty sub #keys and sub.keys != undefined and sub.keys.count > 0 then
									hasKeys = true
							)
						)
						else if posCtrl != undefined and isProperty posCtrl #keys and posCtrl.keys != undefined then
							hasKeys = (posCtrl.keys.count > 0)
						
							if not hasKeys then
						(
							try
							(
								-- Lokale Position relativ zum Parent berechnen
								local localTM = obj.transform * inverse obj.parent.transform
								local localPos = localTM.row4
								local newCtrl = linear_position()
								obj.pos.controller = newCtrl
								addNewKey newCtrl 0f
								newCtrl.keys[1].value = localPos
								fixedNodes += 1
								format "  Added position key for '%' local=%\n" obj.name localPos to:listener
							)
							catch ()
						)
					)
				)
				sliderTime = savedTime
				if fixedNodes > 0 then
					format "  Fixed % emitters with missing position keys\n" fixedNodes to:listener
				
				format "Reforged v1200 Import abgeschlossen.\n" to:listener
				if model.cornEmitters != undefined and model.cornEmitters.count > 0 then
					format "  Corn Emitters: %\n" model.cornEmitters.count to:listener
				if model.faceFxTargets != undefined and model.faceFxTargets.count > 0 then
					format "  FaceFX Targets: %\n" model.faceFxTargets.count to:listener
				if model.bindPoses != undefined and model.bindPoses.count > 0 then
					format "  Bind Poses: %\n" model.bindPoses.count to:listener
			)
			
			-- MPQ-Archive schliessen
			if ::WhiteoutDexMPQ != undefined then
				try ( ::WhiteoutDexMPQ.closeMPQs() ) catch()
		)
	)
)

-- ============================================================================
-- GLOBAL INSTANCE (for backward compatibility)
-- ============================================================================
global SceneRebuilder = WhiteoutDexSceneRebuilderStruct()
