/*
    MaterialFix.ms
    ==============
    Material Fix Settings: how unsupported materials (Standard, Physical,
    PBR, OpenPBR - anything that is not a Wc3 material) become
    Wdx_Wc3Material. Two profiles, SD (Classic) and HD (Reforged).

    The conversion lives here and nowhere else:
      - the tool converts the selected objects or the whole scene with the
        profile of the open tab;
      - the exporter's "Fix all" (scene_monitor_fixes.cpp) hands the
        materials its scan found over by AnimHandle,
            ::WdxMaterialFix.fixHandles #(h1, h2, ...)
        and converts them with the profile picked under "Fix all in the
        export dialog". It only falls back to its own C++ conversion when
        this file is not loaded.

    Settings: <plugcfg>\WhiteoutDex_MaterialFix.ini. setINISetting writes
    UTF-16, so a path prefix with umlauts survives. The first load takes over
    the [MaterialFix] values the old export dialog tab saved in
    MDLXExporter.ini.

    Compatibility: 3ds Max 2016 - 2027. Struct members are always reached
    through the global instance (::WdxMaterialFix): Max 2016 cannot resolve a
    bare member inside a struct method (see WhiteoutDexSidebar.ms).
*/

-- One conversion profile. Values match the Wdx_Wc3Material parameters.
struct WdxMatFixProfile
(
    filterMode = 0,              -- 0 = automatic, 1..7 = Wc3Material.filterMode
    twoSided = false,
    unshaded = false,
    unfogged = false,
    constantColor = false,
    noDepthTest = false,
    noDepthSet = false,          -- the material allows it from Blend (3) on
    ambientOcclusion = false,    -- HD shader only
    backFacesForShadows = false, -- HD shader only
    pathPrefix = "",
    wrapMode = 1,                -- 1 keep, 2 U and V, 3 U only, 4 V only, 5 clamp
    transferNormal = true,       -- HD shader only
    transferEmissive = true,     -- HD shader only
    fromSource = true            -- two-sided, unshaded, filter mode and opacity keys from the source material
)

struct WdxMaterialFixStruct
(
    iniPath = (getDir #plugcfg) + "\\WhiteoutDex_MaterialFix.ini",
    profiles = #(::WdxMatFixProfile(), ::WdxMatFixProfile()), -- 1 = SD, 2 = HD
    fixProfile = 1,              -- profile "Fix all" in the export dialog uses
    addSuffix = true,            -- name the new material "<old>_wc3"
    tab = 1,                     -- open tab in the tool

    -- MAXScript property names per map role, first match wins
    -- (Autodesk MAXScript help: StandardMaterial, Physical_Material,
    -- OpenPBR_Material). Physical's emission_map is the emission WEIGHT map,
    -- the colour texture is emit_color_map.
    mapProps = #(
        #(#diffuse,  #(#diffuseMap, #base_color_map)),
        #(#normal,   #(#geometry_normal_map, #bump_map, #bumpMap, #norm_map, #normal_map)),
        #(#emissive, #(#emit_color_map, #emission_color_map, #selfIllumMap)),
        #(#opacity,  #(#cutout_map, #geometry_opacity_map, #opacityMap, #opacity_map))
    ),
    -- Fallback for materials whose property names are not documented (the
    -- PBR Metal/Rough and Spec/Gloss materials among them): match the
    -- texture slot names instead, like the C++ fixer did.
    slotHints = #(
        #(#diffuse,  #("base color", "base_color", "basecolor", "diffuse", "albedo")),
        #(#normal,   #("normal", "bump")),
        #(#emissive, #("emission", "emissive", "emit", "self-illum", "selfillum")),
        #(#opacity,  #("cutout", "opacity"))
    ),

    -- ------------------------------------------------------------------
    -- Strings
    -- ------------------------------------------------------------------

    fn tr key fallback =
    (
        local s = undefined
        if ::WdxL != undefined do s = try (::WdxL.t key) catch (undefined)
        if s == undefined or s == "" or s == key then fallback else s
    ),

    fn trList key fallback =
    (
        filterString (::WdxMaterialFix.tr key fallback) "|"
    ),

    -- "%1", "%2" ... in order. Catalog strings never go through format, so a
    -- translation cannot break the call.
    fn fmt s args =
    (
        for i = 1 to args.count do
            s = substituteString s ("%" + (i as string)) (args[i] as string)
        s
    ),

    -- ------------------------------------------------------------------
    -- Settings
    -- ------------------------------------------------------------------

    fn iniBool v def = (if v == "" then def else (v == "1" or v == "true" or v == "True")),

    fn iniInt v def lo hi =
    (
        if v == "" then return def
        local n = v as integer
        if n == undefined or n < lo or n > hi then def else n
    ),

    fn loadProfile sec p =
    (
        local M = ::WdxMaterialFix
        local f = M.iniPath
        p.filterMode          = M.iniInt  (getINISetting f sec "FilterMode") p.filterMode 0 7
        p.twoSided            = M.iniBool (getINISetting f sec "TwoSided") p.twoSided
        p.unshaded            = M.iniBool (getINISetting f sec "Unshaded") p.unshaded
        p.unfogged            = M.iniBool (getINISetting f sec "Unfogged") p.unfogged
        p.constantColor       = M.iniBool (getINISetting f sec "ConstantColor") p.constantColor
        p.noDepthTest         = M.iniBool (getINISetting f sec "NoDepthTest") p.noDepthTest
        p.noDepthSet          = M.iniBool (getINISetting f sec "NoDepthSet") p.noDepthSet
        p.ambientOcclusion    = M.iniBool (getINISetting f sec "AmbientOcclusion") p.ambientOcclusion
        p.backFacesForShadows = M.iniBool (getINISetting f sec "BackFacesForShadows") p.backFacesForShadows
        p.pathPrefix          = getINISetting f sec "PathPrefix"
        p.wrapMode            = M.iniInt  (getINISetting f sec "WrapMode") p.wrapMode 1 5
        p.transferNormal      = M.iniBool (getINISetting f sec "TransferNormal") p.transferNormal
        p.transferEmissive    = M.iniBool (getINISetting f sec "TransferEmissive") p.transferEmissive
        p.fromSource          = M.iniBool (getINISetting f sec "FromSource") p.fromSource
        p
    ),

    fn saveProfile sec p =
    (
        local f = ::WdxMaterialFix.iniPath
        fn b v = (if v then "1" else "0")
        setINISetting f sec "FilterMode" (p.filterMode as string)
        setINISetting f sec "TwoSided" (b p.twoSided)
        setINISetting f sec "Unshaded" (b p.unshaded)
        setINISetting f sec "Unfogged" (b p.unfogged)
        setINISetting f sec "ConstantColor" (b p.constantColor)
        setINISetting f sec "NoDepthTest" (b p.noDepthTest)
        setINISetting f sec "NoDepthSet" (b p.noDepthSet)
        setINISetting f sec "AmbientOcclusion" (b p.ambientOcclusion)
        setINISetting f sec "BackFacesForShadows" (b p.backFacesForShadows)
        setINISetting f sec "PathPrefix" p.pathPrefix
        setINISetting f sec "WrapMode" (p.wrapMode as string)
        setINISetting f sec "TransferNormal" (b p.transferNormal)
        setINISetting f sec "TransferEmissive" (b p.transferEmissive)
        setINISetting f sec "FromSource" (b p.fromSource)
    ),

    -- The removed export dialog tab kept one set of values in
    -- MDLXExporter.ini [MaterialFix]; they seed both profiles once.
    fn migrateLegacy =
    (
        local M = ::WdxMaterialFix
        local old = (getDir #plugcfg) + "\\MDLXExporter.ini"
        if not (doesFileExist old) or not (hasINISetting old "MaterialFix") then return false
        for p in M.profiles do
        (
            p.unshaded = M.iniBool (getINISetting old "MaterialFix" "Unshaded") false
            p.unfogged = M.iniBool (getINISetting old "MaterialFix" "Unfogged") false
            p.twoSided = M.iniBool (getINISetting old "MaterialFix" "Twosided") false
            p.filterMode = M.iniInt (getINISetting old "MaterialFix" "FilterMode") 0 1 7
            p.pathPrefix = getINISetting old "MaterialFix" "PrefixPath"
            local u = M.iniBool (getINISetting old "MaterialFix" "UTile") false
            local v = M.iniBool (getINISetting old "MaterialFix" "VTile") false
            p.wrapMode = if u and v then 2 else if u then 3 else if v then 4 else 1
        )
        true
    ),

    fn save =
    (
        local M = ::WdxMaterialFix
        local f = M.iniPath
        setINISetting f "General" "SettingsVersion" "1"
        setINISetting f "General" "FixProfile" (if M.fixProfile == 2 then "HD" else "SD")
        setINISetting f "General" "AddSuffix" (if M.addSuffix then "1" else "0")
        M.saveProfile "SD" M.profiles[1]
        M.saveProfile "HD" M.profiles[2]
    ),

    fn load =
    (
        local M = ::WdxMaterialFix
        M.profiles = #(::WdxMatFixProfile(), ::WdxMatFixProfile())
        M.fixProfile = 1
        M.addSuffix = true
        if not (doesFileExist M.iniPath) then
        (
            if M.migrateLegacy() do M.save()
            return true
        )
        M.fixProfile = if (getINISetting M.iniPath "General" "FixProfile") == "HD" then 2 else 1
        M.addSuffix = M.iniBool (getINISetting M.iniPath "General" "AddSuffix") true
        M.loadProfile "SD" M.profiles[1]
        M.loadProfile "HD" M.profiles[2]
        true
    ),

    -- ------------------------------------------------------------------
    -- Finding materials
    -- ------------------------------------------------------------------

    -- Same rule as isMaterialUnsupported in scene_monitor.cpp: Wc3 and
    -- NeoDex materials are fine, wrappers (Multi/Sub, Blend, ...) are
    -- checked through their sub-materials, anything else is converted.
    fn isUnsupported m =
    (
        if m == undefined or (superClassOf m) != material then return false
        local c = classOf m
        if ::Wdx_Wc3Material != undefined and c == ::Wdx_Wc3Material then return false
        local cid = try (c.classID) catch (#(0, 0))
        -- NeoDex "Warcraft 3" material (NEODEX_MATERIAL in mdx_class_ids.h)
        if cid[1] == 0x681a50f9 and cid[2] == 0x2fae1330 then return false
        (getNumSubMtls m) == 0
    ),

    fn collectFromMtl m out =
    (
        if m == undefined then return out
        local n = getNumSubMtls m
        if n > 0 then
        (
            for i = 1 to n do ::WdxMaterialFix.collectFromMtl (getSubMtl m i) out
        )
        else if ::WdxMaterialFix.isUnsupported m do
            appendIfUnique out m
        out
    ),

    fn collectForNodes nodes =
    (
        local out = #()
        for o in nodes where isValidNode o do ::WdxMaterialFix.collectFromMtl o.material out
        out
    ),

    -- ------------------------------------------------------------------
    -- Building the Wc3 material
    -- ------------------------------------------------------------------

    fn prop obj propName =
    (
        if obj == undefined or not (isProperty obj propName) then return undefined
        try (getProperty obj propName) catch (undefined)
    ),

    fn findMap src role =
    (
        local M = ::WdxMaterialFix
        for e in M.mapProps where e[1] == role do
            for p in e[2] do
            (
                local t = M.prop src p
                if t != undefined and (superClassOf t) == textureMap then return t
            )
        local n = try (getNumSubTexmaps src) catch (0)
        for e in M.slotHints where e[1] == role do
            for i = 1 to n do
            (
                local t = getSubTexmap src i
                if t == undefined then continue
                local slot = toLower ((getSubTexmapSlotName src i) as string)
                for h in e[2] where (findString slot h) != undefined do return t
            )
        undefined
    ),

    -- A texture the Wc3 material and the exporter can use, as a new texture
    -- object so the source material stays untouched (undo, other users).
    fn toBitmap tex role =
    (
        if tex == undefined then return undefined
        local M = ::WdxMaterialFix
        -- Normal_Bump wraps the actual normal texture.
        if role == #normal and (classOf tex) == Normal_Bump do
        (
            tex = M.prop tex #normal_map
            if tex == undefined then return undefined
        )
        if (classOf tex) == Bitmaptexture then return (copy tex)
        -- Wc3Bitmap and other scripted wrappers around a Bitmaptexture
        local d = M.prop tex #delegate
        if d != undefined and (classOf d) == Bitmaptexture then return (copy tex)
        -- OSL "Bitmap Lookup" and similar: rebuild as a plain Bitmaptexture
        local file = M.prop tex #filename
        if (classOf file) == String and file != "" then return (Bitmaptexture fileName:file)
        tex
    ),

    fn nativeBitmap tex =
    (
        if tex == undefined then return undefined
        if (classOf tex) == Bitmaptexture then return tex
        local d = ::WdxMaterialFix.prop tex #delegate
        if d != undefined and (classOf d) == Bitmaptexture then d else undefined
    ),

    -- Same test as texHasAlpha in Wc3Material.ms, on a 5 x 5 grid: an alpha
    -- channel that is fully opaque (common in BLP and TGA) does not count.
    fn texHasAlpha tex =
    (
        local nb = ::WdxMaterialFix.nativeBitmap tex
        if nb == undefined then return false
        local b = try (nb.bitmap) catch (undefined)
        if b == undefined then return false
        local w = b.width, h = b.height
        for iy = 0 to 4 do
            for ix = 0 to 4 do
            (
                local px = (getPixels b [(ix * (w - 1)) / 4, (iy * (h - 1)) / 4] 1)[1]
                if px != undefined and px.alpha < 250 then return true
            )
        false
    ),

    -- 0 = no alpha, 1 = hard edges only (cut-out), 2 = soft alpha (smoke,
    -- glow): sampled like texHasAlpha on a 5 x 5 grid.
    fn texAlphaKind tex =
    (
        local nb = ::WdxMaterialFix.nativeBitmap tex
        if nb == undefined then return 0
        local b = try (nb.bitmap) catch (undefined)
        if b == undefined then return 0
        local w = b.width, h = b.height
        local kind = 0
        for iy = 0 to 4 do
            for ix = 0 to 4 do
            (
                local px = (getPixels b [(ix * (w - 1)) / 4, (iy * (h - 1)) / 4] 1)[1]
                if px != undefined and px.alpha < 250 do
                (
                    if px.alpha > 5 then return 2
                    kind = 1
                )
            )
        kind
    ),

    -- What the source material itself says. Effects ripped from a game come
    -- as Standard materials that are two-sided, fully self-illuminated and
    -- see-through (StandardMaterial: opacityType 0 Filter, 1 Subtractive,
    -- 2 Additive - MAXScript Help, "StandardMaterial"). Returns
    -- #(twoSided, unshaded, filter mode or 0 when the source says nothing).
    fn sourceFlags src diff =
    (
        local M = ::WdxMaterialFix
        local twoSided = (M.prop src #twoSided) == true or (M.prop src #thin_walled) == true
        local unshaded = false
        if classOf src == StandardMaterial do
        (
            if (M.prop src #useSelfIllumColor) == true then
            (
                local c = M.prop src #selfIllumColor
                unshaded = c != undefined and c.r > 250 and c.g > 250 and c.b > 250
            )
            else
            (
                local a = M.prop src #selfIllumAmount
                unshaded = a != undefined and a >= 100
            )
        )
        local fm = 0
        if classOf src == StandardMaterial and (M.prop src #opacityType) == 2 then fm = 4   -- Additive
        else if (M.findMap src #opacity) != undefined or (M.sourceOpacity src) < 100 then fm = 3   -- Blend
        else
        (
            local k = M.texAlphaKind diff
            fm = if k == 2 then 3 else if k == 1 then 2 else 0
        )
        #(twoSided, unshaded, fm)
    ),

    -- Keyed opacity of the source (Standard's opacity) onto the Wc3 material,
    -- which the exporter writes as the layer's alpha track.
    fn copyOpacityKeys src wm =
    (
        local c = try (getPropertyController src #opacity) catch (undefined)
        if c == undefined or (try (c.keys.count) catch (0)) == 0 then return false
        wm.opacity.controller = copy c
        wm.opacityCtrl = case classOf c of
        (
            Linear_Float: 2
            TCB_Float: 4
            default: 3
        )
        true
    ),

    -- The exporter reads the MDX wrap flags from the bitmap's U/V tiling
    -- (wc3_material_extractor.cpp, GetTextureTiling).
    fn applyWrap tex mode =
    (
        if mode == 1 then return false
        local nb = ::WdxMaterialFix.nativeBitmap tex
        if nb == undefined then return false
        nb.coords.U_Tile = (mode == 2 or mode == 3)
        nb.coords.V_Tile = (mode == 2 or mode == 4)
        true
    ),

    -- The source material's opacity in Wdx_Wc3Material's 0..100 (MAXScript
    -- help): Standard's opacity percentage, Physical's Transparency and
    -- OpenPBR's transmission_weight (0..1) inverted. The exporter reads the
    -- same values off an unconverted material (wdx_foreign_material.h), so an
    -- export gives the same layer alpha with or without Fix all.
    fn sourceOpacity src =
    (
        if classOf src == StandardMaterial then return (try (src.opacity as float) catch 100.0)
        for prop in #(#transparency, #transmission_weight) do
            if isProperty src prop then
                return (try ((1.0 - (getProperty src prop)) * 100.0) catch 100.0)
        100.0
    ),

    fn convertMaterial src p isHD =
    (
        local M = ::WdxMaterialFix
        local wm = ::Wdx_Wc3Material()
        wm.name = if M.addSuffix then (src.name + "_wc3") else src.name
        wm.shaderType = if isHD then 2 else 1

        wm.opacity = M.sourceOpacity src
        wm.twoSided = p.twoSided
        wm.unshaded = p.unshaded
        wm.unfogged = p.unfogged
        wm.constantColor = p.constantColor
        wm.noDepthTest = p.noDepthTest
        if isHD do
        (
            wm.ambientOcclusion = p.ambientOcclusion
            wm.backFacesForShadows = p.backFacesForShadows
        )

        local diff = M.toBitmap (M.findMap src #diffuse) #diffuse
        if diff != undefined do
        (
            M.applyWrap diff p.wrapMode
            wm.diffuseMap = diff
            wm.diffusePrefix = p.pathPrefix
        )
        -- The SD shader has no normal or emissive map (Wc3Material.ms
        -- updateShaderState disables those slots).
        if isHD and p.transferNormal do
        (
            local nrm = M.toBitmap (M.findMap src #normal) #normal
            if nrm != undefined do
            (
                M.applyWrap nrm p.wrapMode
                wm.normalMap = nrm
                wm.normalPrefix = p.pathPrefix
            )
        )
        if isHD and p.transferEmissive do
        (
            local emi = M.toBitmap (M.findMap src #emissive) #emissive
            if emi != undefined do
            (
                M.applyWrap emi p.wrapMode
                wm.emissiveMap = emi
                wm.emissivePrefix = p.pathPrefix
            )
        )

        -- Filter mode last: it configures the opacity map the diffuseMap
        -- handler builds from the diffuse texture. With "from the source
        -- material" on, a source that is see-through or additive decides the
        -- mode, two-sided and self-illuminated (unshaded) carry over, and so
        -- do opacity keys; the profile covers what the source does not say.
        local fm = p.filterMode
        local srcF = if p.fromSource then (M.sourceFlags src diff) else undefined
        if srcF != undefined do
        (
            if srcF[1] do wm.twoSided = true
            if srcF[2] do wm.unshaded = true
            M.copyOpacityKeys src wm
        )
        if srcF != undefined and srcF[3] > 0 then fm = srcF[3]
        else if fm == 0 do
            fm = if (M.findMap src #opacity) != undefined or (M.texHasAlpha diff) then 2 else 1
        wm.filterMode = fm
        if fm >= 3 do wm.noDepthSet = p.noDepthSet
        wm
    ),

    -- ------------------------------------------------------------------
    -- Replacing
    -- ------------------------------------------------------------------

    fn replaceInMtl holder oldM newM =
    (
        for i = 1 to (getNumSubMtls holder) do
        (
            local s = getSubMtl holder i
            if s == oldM then
                setSubMtl holder i newM
            else if s != undefined and (getNumSubMtls s) > 0 do
                ::WdxMaterialFix.replaceInMtl s oldM newM
        )
    ),

    fn replaceOnNodes nodes oldM newM =
    (
        for o in nodes where isValidNode o and o.material != undefined do
        (
            if o.material == oldM then
                o.material = newM
            else if (getNumSubMtls o.material) > 0 do
                ::WdxMaterialFix.replaceInMtl o.material oldM newM
        )
    ),

    -- Swap a Material Editor slot only once no object uses the old material
    -- any more; converting a selection can leave it on other objects.
    fn updateMedit pairs =
    (
        for i = 1 to meditMaterials.count do
            for pr in pairs where meditMaterials[i] == pr[1] do
                if (refs.dependentNodes pr[1]).count == 0 do meditMaterials[i] = pr[2]
        try (activeMeditSlot = activeMeditSlot) catch ()
    ),

    -- Converts |mats| and puts the results on |nodes|. Returns the number of
    -- converted materials, or -1 when the Wc3Material plugin is missing.
    fn convertMaterials mats nodes profIdx =
    (
        local M = ::WdxMaterialFix
        if ::Wdx_Wc3Material == undefined then return -1
        if mats.count == 0 then return 0
        local p = M.profiles[profIdx]
        local isHD = (profIdx == 2)
        local pairs = #()
        undo "Convert to Wc3 material" on
        (
            with redraw off
            (
                for src in mats do
                (
                    local nm = try (M.convertMaterial src p isHD) catch (
                        format "WhiteoutDex Material Fix: '%' failed: %\n" src.name (getCurrentException())
                        undefined
                    )
                    if nm == undefined then continue
                    M.replaceOnNodes nodes src nm
                    append pairs #(src, nm)
                )
                M.updateMedit pairs
            )
        )
        redrawViews()
        pairs.count
    ),

    fn convertSelection profIdx =
    (
        local nodes = selection as array
        ::WdxMaterialFix.convertMaterials (::WdxMaterialFix.collectForNodes nodes) nodes profIdx
    ),

    fn convertScene profIdx =
    (
        local nodes = objects as array
        ::WdxMaterialFix.convertMaterials (::WdxMaterialFix.collectForNodes nodes) nodes profIdx
    ),

    -- Called by the exporter's Fix all with the AnimHandles of the materials
    -- its scan flagged. Uses the "Fix all" profile and replaces the
    -- materials everywhere in the scene.
    fn fixHandles handles =
    (
        local M = ::WdxMaterialFix
        M.load()
        local mats = #()
        for h in handles do
        (
            local mtl = getAnimByHandle h
            if M.isUnsupported mtl do appendIfUnique mats mtl
        )
        M.convertMaterials mats (objects as array) M.fixProfile
    ),

    -- ------------------------------------------------------------------
    -- Window
    -- ------------------------------------------------------------------

    -- The member carries the rollout's own name, as in ObjectSettings.ms:
    -- inside the struct body the compiler only resolves the rollout through
    -- the struct member, so a differently named member leaves every
    -- reference to the rollout "Undeclared variable".
    mainRollout = rollout mainRollout "Material Fix Settings" width:380 height:602
    (
        label lblHeader "Converts Standard, Physical and PBR materials into Wc3 materials." pos:[10,8] width:360 height:30
        checkbutton tabSD "SD (Classic)" pos:[10,40] width:178 height:24 checked:true
        checkbutton tabHD "HD (Reforged)" pos:[192,40] width:178 height:24

        GroupBox grpRender "Rendering" pos:[10,72] width:360 height:156
        label lblFilter "Filter mode" pos:[22,95] width:104 height:16
        dropdownlist ddFilter "" pos:[130,92] width:228 height:20 items:#("Automatic (alpha -> Transparent)", "None", "Transparent", "Blend", "Additive", "Add alpha", "Modulate", "Modulate 2x")
        checkbox chkTwoSided "Two sided" pos:[22,120] width:170 height:18
        checkbox chkUnshaded "Unshaded" pos:[196,120] width:170 height:18
        checkbox chkUnfogged "Unfogged" pos:[22,140] width:170 height:18
        checkbox chkConstColor "Constant color" pos:[196,140] width:170 height:18
        checkbox chkNoDTest "No depth test" pos:[22,160] width:170 height:18
        checkbox chkNoDSet "No depth set" pos:[196,160] width:170 height:18
        checkbox chkAmbOcc "Ambient occlusion (HD)" pos:[22,180] width:170 height:18
        checkbox chkBackShadow "Back faces for shadows (HD)" pos:[196,180] width:170 height:18
        checkbox chkFromSource "Take two-sided, unshaded and transparency from the source material" pos:[22,202] width:340 height:18

        GroupBox grpTex "Textures" pos:[10,234] width:360 height:114
        label lblPrefix "Path prefix" pos:[22,257] width:104 height:16
        edittext edtPrefix "" pos:[126,254] width:152 height:20
        dropdownlist ddPreset "" pos:[282,254] width:76 height:20 items:#("Presets")
        label lblWrap "Wrapping" pos:[22,283] width:104 height:16
        dropdownlist ddWrap "" pos:[130,280] width:228 height:20 items:#("Keep from source", "Wrap U and V", "Wrap U only", "Wrap V only", "Clamp (no wrap)")
        checkbox chkNormal "Normal / bump map -> Normal (HD)" pos:[22,306] width:340 height:18
        checkbox chkEmissive "Emission map -> Emissive (HD)" pos:[22,326] width:340 height:18

        GroupBox grpFix "Fix all in the export dialog" pos:[10,354] width:360 height:70
        label lblFixUses "Uses" pos:[22,377] width:104 height:16
        dropdownlist ddFixProfile "" pos:[130,374] width:228 height:20 items:#("SD settings", "HD settings")
        checkbox chkSuffix "Add _wc3 to the material name" pos:[22,400] width:340 height:18

        GroupBox grpConvert "Convert now" pos:[10,430] width:360 height:126
        label lblUses "" pos:[22,450] width:340 height:16
        label lblSel "" pos:[22,470] width:340 height:16
        label lblScene "" pos:[22,488] width:340 height:16
        button btnConvSel "Convert selected objects" pos:[22,508] width:164 height:26
        button btnConvScene "Convert whole scene" pos:[194,508] width:164 height:26
        label lblResult "" pos:[22,538] width:340 height:16

        button btnReset "Reset tab to defaults" pos:[10,566] width:170 height:26
        button btnClose "Close" pos:[270,566] width:100 height:26

        -- Folder presets for the path prefix; entry 1 is the list's caption.
        local prefixPresets = #("", "Textures\\", "war3mapImported\\", "ReplaceableTextures\\", "UI\\")

        fn cur = ::WdxMaterialFix.profiles[::WdxMaterialFix.tab]

        fn saveNow = ::WdxMaterialFix.save()

        fn localize =
        (
            local M = ::WdxMaterialFix
            mainRollout.title = M.tr "menu_material_fix" "Material Fix Settings"
            lblHeader.text = M.tr "matfix_header" lblHeader.text
            tabSD.text = M.tr "matfix_tab_sd" tabSD.text
            tabHD.text = M.tr "matfix_tab_hd" tabHD.text
            grpRender.text = M.tr "matfix_rendering_grp" grpRender.text
            lblFilter.text = M.tr "matfix_filter_mode_lbl" lblFilter.text
            ddFilter.items = M.trList "matfix_filter_items" "Automatic (alpha -> Transparent)|None|Transparent|Blend|Additive|Add alpha|Modulate|Modulate 2x"
            -- The flags share their catalog entries with the Wc3Material UI.
            chkTwoSided.text = M.tr "mat_two_sided_chk" chkTwoSided.text
            chkUnshaded.text = M.tr "mat_unshaded_chk_2" chkUnshaded.text
            chkUnfogged.text = M.tr "mat_unfogged_chk_2" chkUnfogged.text
            chkConstColor.text = M.tr "mat_constant_color_chk" chkConstColor.text
            chkNoDTest.text = M.tr "mat_no_depth_test_chk" chkNoDTest.text
            chkNoDSet.text = M.tr "mat_no_depth_set_chk" chkNoDSet.text
            chkAmbOcc.text = M.tr "matfix_ambient_occlusion_chk" chkAmbOcc.text
            chkBackShadow.text = M.tr "matfix_back_faces_chk" chkBackShadow.text
            chkFromSource.text = M.tr "matfix_from_source_chk" chkFromSource.text
            grpTex.text = M.tr "matfix_textures_grp" grpTex.text
            lblPrefix.text = M.tr "matfix_prefix_lbl" lblPrefix.text
            lblWrap.text = M.tr "matfix_wrap_lbl" lblWrap.text
            ddWrap.items = M.trList "matfix_wrap_items" "Keep from source|Wrap U and V|Wrap U only|Wrap V only|Clamp (no wrap)"
            chkNormal.text = M.tr "matfix_normal_chk" chkNormal.text
            chkEmissive.text = M.tr "matfix_emissive_chk" chkEmissive.text
            grpFix.text = M.tr "matfix_fixall_grp" grpFix.text
            lblFixUses.text = M.tr "matfix_fixall_uses_lbl" lblFixUses.text
            ddFixProfile.items = M.trList "matfix_fixall_items" "SD settings|HD settings"
            chkSuffix.text = M.tr "matfix_suffix_chk" chkSuffix.text
            grpConvert.text = M.tr "matfix_convert_grp" grpConvert.text
            btnConvSel.text = M.tr "matfix_convert_sel_btn" btnConvSel.text
            btnConvScene.text = M.tr "matfix_convert_scene_btn" btnConvScene.text
            btnReset.text = M.tr "matfix_reset_btn" btnReset.text
            btnClose.text = M.tr "matfix_close_btn" btnClose.text

            local items = #(M.tr "matfix_presets_item" "Presets", M.tr "matfix_prefix_none_item" "(none)")
            for i = 2 to prefixPresets.count do append items prefixPresets[i]
            ddPreset.items = items
        )

        -- Profile of the open tab -> controls
        fn loadUI =
        (
            local M = ::WdxMaterialFix
            local p = cur()
            local hd = (M.tab == 2)
            tabSD.checked = not hd
            tabHD.checked = hd
            ddFilter.selection = p.filterMode + 1
            chkTwoSided.checked = p.twoSided
            chkUnshaded.checked = p.unshaded
            chkUnfogged.checked = p.unfogged
            chkConstColor.checked = p.constantColor
            chkNoDTest.checked = p.noDepthTest
            chkNoDSet.checked = p.noDepthSet
            chkNoDSet.enabled = p.filterMode >= 3
            chkAmbOcc.checked = p.ambientOcclusion
            chkBackShadow.checked = p.backFacesForShadows
            chkFromSource.checked = p.fromSource
            edtPrefix.text = p.pathPrefix
            ddPreset.selection = 1
            ddWrap.selection = p.wrapMode
            chkNormal.checked = p.transferNormal
            chkEmissive.checked = p.transferEmissive
            for c in #(chkAmbOcc, chkBackShadow, chkNormal, chkEmissive) do c.enabled = hd
            ddFixProfile.selection = M.fixProfile
            chkSuffix.checked = M.addSuffix
            lblUses.text = M.fmt (M.tr "matfix_uses_tab_fmt" "Uses the settings of the %1 tab.") \
                                 #(if hd then tabHD.text else tabSD.text)
        )

        fn refreshStatus =
        (
            local M = ::WdxMaterialFix
            local sel = selection as array
            local selMats = M.collectForNodes sel
            local sceneMats = M.collectForNodes (objects as array)
            lblSel.text = M.fmt (M.tr "matfix_status_sel_fmt" "Selected: %1 objects, %2 materials to convert") \
                                #(sel.count, selMats.count)
            lblScene.text = M.fmt (M.tr "matfix_status_scene_fmt" "Whole scene: %1 materials to convert") \
                                  #(sceneMats.count)
            btnConvSel.enabled = selMats.count > 0
            btnConvScene.enabled = sceneMats.count > 0
        )

        fn showResult n =
        (
            local M = ::WdxMaterialFix
            lblResult.text = if n < 0 then
                M.tr "matfix_no_plugin_msg" "The Wc3Material plugin is not loaded."
            else
                M.fmt (M.tr "matfix_done_fmt" "%1 materials converted.") #(n)
        )

        on tabSD changed state do ( ::WdxMaterialFix.tab = 1; loadUI() )
        on tabHD changed state do ( ::WdxMaterialFix.tab = 2; loadUI() )

        on ddFilter selected i do
        (
            local p = cur()
            p.filterMode = i - 1
            chkNoDSet.enabled = p.filterMode >= 3
            if p.filterMode < 3 do ( p.noDepthSet = false; chkNoDSet.checked = false )
            saveNow()
        )
        on chkTwoSided changed s do ( local p = cur(); p.twoSided = s; saveNow() )
        on chkUnshaded changed s do ( local p = cur(); p.unshaded = s; saveNow() )
        on chkUnfogged changed s do ( local p = cur(); p.unfogged = s; saveNow() )
        on chkConstColor changed s do ( local p = cur(); p.constantColor = s; saveNow() )
        on chkNoDTest changed s do ( local p = cur(); p.noDepthTest = s; saveNow() )
        on chkNoDSet changed s do ( local p = cur(); p.noDepthSet = s; saveNow() )
        on chkAmbOcc changed s do ( local p = cur(); p.ambientOcclusion = s; saveNow() )
        on chkBackShadow changed s do ( local p = cur(); p.backFacesForShadows = s; saveNow() )
        on chkFromSource changed s do ( local p = cur(); p.fromSource = s; saveNow() )
        on edtPrefix changed txt do ( local p = cur(); p.pathPrefix = txt; saveNow() )
        on ddPreset selected i do
        (
            if i > 1 do
            (
                local p = cur()
                p.pathPrefix = prefixPresets[i - 1]
                edtPrefix.text = p.pathPrefix
                saveNow()
            )
            ddPreset.selection = 1
        )
        on ddWrap selected i do ( local p = cur(); p.wrapMode = i; saveNow() )
        on chkNormal changed s do ( local p = cur(); p.transferNormal = s; saveNow() )
        on chkEmissive changed s do ( local p = cur(); p.transferEmissive = s; saveNow() )
        on ddFixProfile selected i do ( ::WdxMaterialFix.fixProfile = i; saveNow() )
        on chkSuffix changed s do ( ::WdxMaterialFix.addSuffix = s; saveNow() )

        on btnConvSel pressed do
        (
            showResult (::WdxMaterialFix.convertSelection ::WdxMaterialFix.tab)
            refreshStatus()
        )
        on btnConvScene pressed do
        (
            showResult (::WdxMaterialFix.convertScene ::WdxMaterialFix.tab)
            refreshStatus()
        )
        on btnReset pressed do
        (
            ::WdxMaterialFix.profiles[::WdxMaterialFix.tab] = ::WdxMatFixProfile()
            saveNow()
            loadUI()
        )
        on btnClose pressed do destroyDialog mainRollout

        on mainRollout open do
        (
            try (::WdxSetDialogIcon mainRollout) catch ()
            localize()
            loadUI()
            refreshStatus()
            callbacks.removeScripts id:#WdxMaterialFixStatus
            for ev in #(#selectionSetChanged, #filePostOpen, #systemPostNew, #systemPostReset, #sceneUndo, #sceneRedo) do
                callbacks.addScript ev "try (::WdxMaterialFix.mainRollout.refreshStatus()) catch ()" id:#WdxMaterialFixStatus
        )

        on mainRollout close do
        (
            callbacks.removeScripts id:#WdxMaterialFixStatus
            try (::WdxSetUserSetting "MaterialFix" "DialogPos" ((GetDialogPos mainRollout) as string)) catch ()
        )
    ),

    fn open =
    (
        local M = ::WdxMaterialFix
        M.load()
        try (destroyDialog M.mainRollout) catch ()
        local style = #(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox)
        local parts = filterString (::WdxGetUserSetting "MaterialFix" "DialogPos" "") "[], "
        if parts.count == 2 and (parts[1] as float) != undefined and (parts[2] as float) != undefined then
            createDialog M.mainRollout pos:[parts[1] as float, parts[2] as float] style:style
        else
            createDialog M.mainRollout style:style
    )
)

global WdxMaterialFix = WdxMaterialFixStruct()
WdxMaterialFix.load()
