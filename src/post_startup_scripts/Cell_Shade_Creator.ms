try (destroyDialog WdxCellShadeCreator) catch()

/*******************************************************************
 * Wc3 Cell Shade Creator v5.0 — Full .NET WinForms UI
 *******************************************************************/

-- ================================================================
-- DPI Scaling
-- ================================================================
global WDX_CELLSHADE_DPI = 1.0
(
    try (
        local g = (dotNetClass "System.Drawing.Graphics").FromHwnd (dotNetObject "System.IntPtr" 0)
        WDX_CELLSHADE_DPI = g.DpiX / 96.0
        g.Dispose()
    ) catch ( WDX_CELLSHADE_DPI = 1.0 )
)
fn wdx_dS v = (((v as float) * WDX_CELLSHADE_DPI) as integer)

-- ================================================================
-- Bitmap Generators
-- ================================================================
fn wdx_cs_RoundedBmp w h rad clr bgClr =
(
    local bmp = dotNetObject "System.Drawing.Bitmap" w h
    local gfx = (dotNetClass "System.Drawing.Graphics").FromImage bmp
    gfx.SmoothingMode = (dotNetClass "System.Drawing.Drawing2D.SmoothingMode").AntiAlias
    local ccc = dotNetClass "System.Drawing.Color"
    gfx.Clear (ccc.FromArgb bgClr.r bgClr.g bgClr.b)
    local brush = dotNetObject "System.Drawing.SolidBrush" (ccc.FromArgb clr.r clr.g clr.b)
    if rad > 0 then
    (
        local p = dotNetObject "System.Drawing.Drawing2D.GraphicsPath"
        local d = rad * 2 ; local dw = w - 1 ; local dh = h - 1
        p.AddArc 0 0 d d 180 90 ; p.AddArc (dw-d) 0 d d 270 90
        p.AddArc (dw-d) (dh-d) d d 0 90 ; p.AddArc 0 (dh-d) d d 90 90
        p.CloseFigure() ; gfx.FillPath brush p ; p.Dispose()
    ) else ( gfx.Clear (ccc.FromArgb clr.r clr.g clr.b) )
    gfx.Dispose() ; brush.Dispose() ; bmp
)

fn wdx_cs_StrokeBmp w h rad strokeClr innerClr outerClr strokeW =
(
    local bmp = dotNetObject "System.Drawing.Bitmap" w h
    local gfx = (dotNetClass "System.Drawing.Graphics").FromImage bmp
    gfx.SmoothingMode = (dotNetClass "System.Drawing.Drawing2D.SmoothingMode").AntiAlias
    local ccc = dotNetClass "System.Drawing.Color"
    gfx.Clear (ccc.FromArgb outerClr.r outerClr.g outerClr.b)
    local sBrush = dotNetObject "System.Drawing.SolidBrush" (ccc.FromArgb strokeClr.r strokeClr.g strokeClr.b)
    local iBrush = dotNetObject "System.Drawing.SolidBrush" (ccc.FromArgb innerClr.r innerClr.g innerClr.b)
    -- Outer path
    local op = dotNetObject "System.Drawing.Drawing2D.GraphicsPath"
    local dw = w - 1 ; local dh = h - 1 ; local od = rad * 2
    op.AddArc 0 0 od od 180 90 ; op.AddArc (dw-od) 0 od od 270 90
    op.AddArc (dw-od) (dh-od) od od 0 90 ; op.AddArc 0 (dh-od) od od 90 90
    op.CloseFigure()
    gfx.FillPath sBrush op
    -- Inner path
    local ix = strokeW ; local iy = strokeW
    local iw = w - 1 - strokeW * 2 ; local ih = h - 1 - strokeW * 2
    local ir = rad - strokeW ; if ir < 0 do ir = 0
    if iw > 0 and ih > 0 do
    (
        local ip = dotNetObject "System.Drawing.Drawing2D.GraphicsPath"
        local id = ir * 2
        ip.AddArc ix iy id id 180 90 ; ip.AddArc (ix+iw-id) iy id id 270 90
        ip.AddArc (ix+iw-id) (iy+ih-id) id id 0 90 ; ip.AddArc ix (iy+ih-id) id id 90 90
        ip.CloseFigure()
        gfx.FillPath iBrush ip ; ip.Dispose()
    )
    op.Dispose() ; sBrush.Dispose() ; iBrush.Dispose() ; gfx.Dispose() ; bmp
)

fn wdx_cs_StrokeToggleBmp w h rad strokeClr innerClr outerClr strokeW side:#left =
(
    local bmp = dotNetObject "System.Drawing.Bitmap" w h
    local gfx = (dotNetClass "System.Drawing.Graphics").FromImage bmp
    gfx.SmoothingMode = (dotNetClass "System.Drawing.Drawing2D.SmoothingMode").AntiAlias
    local ccc = dotNetClass "System.Drawing.Color"
    gfx.Clear (ccc.FromArgb outerClr.r outerClr.g outerClr.b)
    local sBrush = dotNetObject "System.Drawing.SolidBrush" (ccc.FromArgb strokeClr.r strokeClr.g strokeClr.b)
    local iBrush = dotNetObject "System.Drawing.SolidBrush" (ccc.FromArgb innerClr.r innerClr.g innerClr.b)
    local dw = w - 1 ; local dh = h - 1 ; local od = rad * 2
    -- Outer
    local op = dotNetObject "System.Drawing.Drawing2D.GraphicsPath"
    if side == #left then
    ( op.AddArc 0 0 od od 180 90 ; op.AddLine dw 0 dw 0 ; op.AddLine dw dh dw dh ; op.AddArc 0 (dh-od) od od 90 90 )
    else
    ( op.AddLine 0 0 0 0 ; op.AddArc (dw-od) 0 od od 270 90 ; op.AddArc (dw-od) (dh-od) od od 0 90 ; op.AddLine 0 dh 0 dh )
    op.CloseFigure() ; gfx.FillPath sBrush op
    -- Inner
    local sw2 = strokeW ; local ix = sw2 ; local iy = sw2
    local iw = w - 1 - sw2 * 2 ; local ih = h - 1 - sw2 * 2
    local ir = rad - sw2 ; if ir < 0 do ir = 0 ; local id = ir * 2
    if iw > 0 and ih > 0 do
    (
        local ip = dotNetObject "System.Drawing.Drawing2D.GraphicsPath"
        if side == #left then
        ( ip.AddArc ix iy id id 180 90 ; ip.AddLine (ix+iw) iy (ix+iw) iy ; ip.AddLine (ix+iw) (iy+ih) (ix+iw) (iy+ih) ; ip.AddArc ix (iy+ih-id) id id 90 90 )
        else
        ( ip.AddLine ix iy ix iy ; ip.AddArc (ix+iw-id) iy id id 270 90 ; ip.AddArc (ix+iw-id) (iy+ih-id) id id 0 90 ; ip.AddLine ix (iy+ih) ix (iy+ih) )
        ip.CloseFigure() ; gfx.FillPath iBrush ip ; ip.Dispose()
    )
    op.Dispose() ; sBrush.Dispose() ; iBrush.Dispose() ; gfx.Dispose() ; bmp
)

fn wdx_cs_SwatchBmp sz clr borderClr borderW =
(
    local bmp = dotNetObject "System.Drawing.Bitmap" sz sz
    local gfx = (dotNetClass "System.Drawing.Graphics").FromImage bmp
    gfx.SmoothingMode = (dotNetClass "System.Drawing.Drawing2D.SmoothingMode").AntiAlias
    local ccc = dotNetClass "System.Drawing.Color"
    gfx.Clear (ccc.FromArgb 0 0 0 0)
    local rad = (sz / 4) ; local d = rad * 2 ; local dw = sz - 1 ; local dh = sz - 1
    local p = dotNetObject "System.Drawing.Drawing2D.GraphicsPath"
    p.AddArc 0 0 d d 180 90 ; p.AddArc (dw-d) 0 d d 270 90
    p.AddArc (dw-d) (dh-d) d d 0 90 ; p.AddArc 0 (dh-d) d d 90 90
    p.CloseFigure()
    local brush = dotNetObject "System.Drawing.SolidBrush" (ccc.FromArgb clr.r clr.g clr.b)
    gfx.FillPath brush p
    if borderW > 0 do
    (
        local pen = dotNetObject "System.Drawing.Pen" (ccc.FromArgb borderClr.r borderClr.g borderClr.b) (borderW as float)
        gfx.DrawPath pen p ; pen.Dispose()
    )
    brush.Dispose() ; p.Dispose() ; gfx.Dispose() ; bmp
)

-- ================================================================
-- WdxCellShadeOps Struct
-- ================================================================
struct WdxCellShadeOps
(
    outlineObjects = #(),
    material = undefined,
    fn getValidMeshObjects objArray =
    (
        for i in objArray where (
            isValidNode i and (classof i == Editable_Poly or classof i == Editable_mesh or classof i == PolyMeshObject)
        ) collect i
    ),
    fn getWorkingSet =
    (
        if selection.count != 0 then getValidMeshObjects (selection as array)
        else getValidMeshObjects outlineObjects
    ),
    fn getFilterModeValue idx =
    (
        case idx of
        (
            1: 1
            2: 3
            3: 4
            4: 5
            default: 1
        )
    ),
    fn ensureArchivesReady =
    (
        if ::WhiteoutDexMPQ != undefined and not ::WhiteoutDexMPQ.isInitialized then
            try ( ::WhiteoutDexMPQ.scanAndOpenMPQs() ) catch()
        if ::WhiteoutDexCASC != undefined and not ::WhiteoutDexCASC.isInitialized then
            try (
                local w3dir = getINISetting (getDir #plugcfg + "\\WhiteoutDex_Settings.ini") "CASC" "W3Path"
                if w3dir != "" do ::WhiteoutDexCASC.init w3dir:w3dir
            ) catch()
    ),
    fn resolveTexture texPath =
    (
        if doesFileExist texPath do return texPath
        local ndx = ::WhiteoutDexTextureBrowserDLL
        if ndx == undefined do return undefined
        ensureArchivesReady()
        local tempDir = (getDir #temp) + "\\CellShade\\"
        makeDir tempDir all:true
        local destPath = tempDir + (filenameFromPath texPath)
        if doesFileExist destPath do return destPath
        try (
            for h = 0 to (ndx.MPQ_Count() - 1) do
                if ndx.MPQ_HasFile h texPath do
                    if ndx.MPQ_ExtractToDisk h texPath destPath do return destPath
        ) catch()
        try (
            if ndx.CASC_IsOpen() do
                if (ndx.CASC_ExtractToDisk texPath destPath) > 0 do return destPath
        ) catch()
        undefined
    ),
    fn placeInMaterialEditor mat =
    (
        try (
            for i = 1 to meditMaterials.count do
                if classof meditMaterials[i] == Standardmaterial do
                ( meditMaterials[i] = mat ; medit.PutMtlToMtlEditor mat i ; return true )
            meditMaterials[1] = mat ; medit.PutMtlToMtlEditor mat 1
        ) catch()
        true
    ),
    fn createMaterial filterState =
    (
        local mat = Warcraft3()
        mat.name = "CellShade_Outline"
        mat.path = "Textures\\"
        mat.Unshaded = on
        mat.Unfogged = on
        mat.filtermode = getFilterModeValue filterState
        local texPath = resolveTexture "Textures\\white.blp"
        if texPath == undefined do texPath = resolveTexture "Textures\\white.tga"
        if texPath != undefined then mat.texture = Bitmaptexture fileName:texPath
        else mat.texture = Bitmaptexture fileName:"white.blp"
        try ( mat.generateOpacity mat.texture ) catch()
        try ( mat.adjustDelegate mat.retexture mat.filtermode ) catch()
        showTexturemap mat true
        placeInMaterialEditor mat
        mat
    ),
    fn hasSkinModifier obj = ( for m in obj.modifiers where classof m == Skin do return true ; false ),
    fn getOrCreateLayer =
    (
        local l = layerManager.getLayerFromName "Cellshade"
        if l == undefined do l = LayerManager.newLayerFromName "Cellshade"
        l
    ),
    fn createOutlines sourceObjects filterState normalFlip pushValue outlineColor =
    (
        material = createMaterial filterState
        outlineObjects = #()
        local skinCount = 0
        disableSceneRedraw()
        try ( undo "CellShade Outline" on
        (
            for obj in sourceObjects do
            (
                maxOps.cloneNodes obj cloneType:#copy newNodes:&nnl
                local o = nnl[1]
                o.name = obj.name + "_cellshade"
                local pm = Push() ; pm.Push_Value = pushValue ; addModifier o pm
                local nm = Normalmodifier() ; nm.flip = normalFlip ; addModifier o nm
                o.material = material
                try ( local vm = Wc3VertexMod() ; vm.VertexColor = outlineColor ; addModifier o vm ) catch()
                o.backfacecull = on
                if hasSkinModifier o do skinCount += 1
                append outlineObjects o
            )
            local layer = getOrCreateLayer()
            for obj in outlineObjects do layer.addNode obj
        )) catch()
        enableSceneRedraw()
        forceCompleteRedraw()
        if outlineObjects.count > 0 do select outlineObjects
        #(outlineObjects.count, skinCount)
    ),
    fn updateColor c     = ( for o in (getWorkingSet()) do try(o.modifiers[#Wc3_Vertex_Color].VertexColor=c)catch() ),
    fn updateThickness v = ( for o in (getWorkingSet()) do try(o.modifiers[#Push].Push_Value=v)catch() ),
    fn updateNormals f   = ( for o in (getWorkingSet()) do try(o.modifiers[#Normal].flip=f)catch() ),
    fn updateFilterMode i =
    (
        if material != undefined do
        (
            material.filtermode = getFilterModeValue i
            try(material.adjustDelegate material.retexture material.filtermode)catch()
        )
    )
)
global WdxCellShade = WdxCellShadeOps()

-- ================================================================
-- UI
-- ================================================================
rollout WdxCellShadeCreator "Wc3 Cell Shade Creator" width:192 height:360
(
    local bgRaw = (colorMan.getColor #background) * 255
    local bgCol = color bgRaw.x bgRaw.y bgRaw.z
    local cc = dotNetClass "System.Drawing.Color"
    local bgDN = cc.FromArgb (bgCol.r as integer) (bgCol.g as integer) (bgCol.b as integer)

    local flipNormals = true
    local currentColor = color 0 0 0
    local filterIndex = 1

    local swColors = #(color 226 34 34, color 255 136 0, color 238 238 0, color 0 204 204,
                       color 0 170 0, color 125 0 255, color 0 125 255, color 0 0 255)

    -- Stroke button colors
    local colStroke    = color 80 80 80
    local colInnerOn   = color 58 58 58
    local colInnerOff  = color 42 42 42

    -- Bitmaps (assigned on open)
    local bmpCreateN = undefined
    local bmpCreateH = undefined
    local bmpCreateP = undefined
    local bmpTogOnL = undefined
    local bmpTogOffL = undefined
    local bmpTogOnR = undefined
    local bmpTogOffR = undefined
    local bmpRadioOn = undefined
    local bmpRadioOff = undefined
    local swBmpsNormal = #()
    local swBmpsHover = #()

    -- Controls: Color
    dotNetControl lblColorSec  "System.Windows.Forms.Label"      width:162 height:14 pos:[14,8]
    dotNetControl pnlColor     "System.Windows.Forms.PictureBox" width:28  height:22 pos:[14,26]
    dotNetControl lblRGB       "System.Windows.Forms.Label"      width:90  height:14 pos:[50,30]
    dotNetControl sw1 "System.Windows.Forms.PictureBox" width:16 height:16 pos:[14,54]
    dotNetControl sw2 "System.Windows.Forms.PictureBox" width:16 height:16 pos:[33,54]
    dotNetControl sw3 "System.Windows.Forms.PictureBox" width:16 height:16 pos:[52,54]
    dotNetControl sw4 "System.Windows.Forms.PictureBox" width:16 height:16 pos:[71,54]
    dotNetControl sw5 "System.Windows.Forms.PictureBox" width:16 height:16 pos:[90,54]
    dotNetControl sw6 "System.Windows.Forms.PictureBox" width:16 height:16 pos:[109,54]
    dotNetControl sw7 "System.Windows.Forms.PictureBox" width:16 height:16 pos:[128,54]
    dotNetControl sw8 "System.Windows.Forms.PictureBox" width:16 height:16 pos:[147,54]
    dotNetControl sep1 "System.Windows.Forms.PictureBox" width:154 height:1 pos:[14,78]

    -- Controls: Shape
    dotNetControl lblShapeSec "System.Windows.Forms.Label" width:162 height:14 pos:[14,86]
    label lblThick "Thickness ........" pos:[16,106] width:88 height:14
    spinner spnThick "" pos:[108,105] width:56 height:16 range:[0.01,5.0,0.1] scale:0.01 type:#float

    dotNetControl lblNormSec "System.Windows.Forms.Label" width:154 height:14 pos:[14,130]
    dotNetControl btnFlipL "System.Windows.Forms.Button" width:76 height:26 pos:[14,146]
    dotNetControl btnFlipR "System.Windows.Forms.Button" width:76 height:26 pos:[90,146]
    dotNetControl sep2 "System.Windows.Forms.PictureBox" width:154 height:1 pos:[14,180]

    -- Controls: Material
    dotNetControl lblMatSec "System.Windows.Forms.Label" width:162 height:14 pos:[14,188]
    dotNetControl fltNone  "System.Windows.Forms.Button" width:154 height:22 pos:[14,206]
    dotNetControl fltBlend "System.Windows.Forms.Button" width:154 height:22 pos:[14,229]
    dotNetControl fltAdd   "System.Windows.Forms.Button" width:154 height:22 pos:[14,252]
    dotNetControl fltAddA  "System.Windows.Forms.Button" width:154 height:22 pos:[14,275]
    dotNetControl sep3 "System.Windows.Forms.PictureBox" width:154 height:1 pos:[14,304]

    -- Create button
    dotNetControl btnCreate "System.Windows.Forms.Button" width:154 height:30 pos:[14,316]

    local tooltip = undefined

    -- ============================================================
    -- Helpers
    -- ============================================================
    fn styleSectionLabel ctrl txt =
    (
        ctrl.Text = txt
        ctrl.ForeColor = cc.FromArgb 90 170 255
        ctrl.BackColor = bgDN
        ctrl.Font = dotNetObject "System.Drawing.Font" "Segoe UI" 9.0 (dotNetClass "System.Drawing.FontStyle").Bold
    )

    fn styleBtn btn img =
    (
        btn.FlatStyle = (dotNetClass "System.Windows.Forms.FlatStyle").Flat
        btn.FlatAppearance.BorderSize = 0
        btn.BackgroundImage = img
        btn.BackgroundImageLayout = (dotNetClass "System.Windows.Forms.ImageLayout").Stretch
        btn.BackColor = bgDN
        btn.FlatAppearance.MouseOverBackColor = bgDN
        btn.FlatAppearance.MouseDownBackColor = bgDN
        btn.TabStop = false
    )

    fn updateRGBLabel = ( lblRGB.Text = (currentColor.r as integer) as string + ", " + (currentColor.g as integer) as string + ", " + (currentColor.b as integer) as string )

    fn setCurrentColor c =
    (
        currentColor = c
        pnlColor.BackColor = cc.FromArgb (c.r as integer) (c.g as integer) (c.b as integer)
        updateRGBLabel()
        WdxCellShade.updateColor c
    )

    fn updateToggleVisuals =
    (
        local fOn = cc.FromArgb 210 210 210
        local fOff = cc.FromArgb 100 100 100
        if flipNormals then
        ( btnFlipL.BackgroundImage = bmpTogOnL ; btnFlipL.ForeColor = fOn ; btnFlipR.BackgroundImage = bmpTogOffR ; btnFlipR.ForeColor = fOff )
        else
        ( btnFlipL.BackgroundImage = bmpTogOffL ; btnFlipL.ForeColor = fOff ; btnFlipR.BackgroundImage = bmpTogOnR ; btnFlipR.ForeColor = fOn )
    )

    fn updateFilterVisuals =
    (
        local btns = #(fltNone, fltBlend, fltAdd, fltAddA)
        local fOn = cc.FromArgb 210 210 210
        local fOff = cc.FromArgb 100 100 100
        for i = 1 to 4 do
        (
            if i == filterIndex then ( btns[i].BackgroundImage = bmpRadioOn ; btns[i].ForeColor = fOn )
            else ( btns[i].BackgroundImage = bmpRadioOff ; btns[i].ForeColor = fOff )
        )
    )

    fn selectFilter idx = ( filterIndex = idx ; updateFilterVisuals() ; WdxCellShade.updateFilterMode idx )

    fn styleSwatch ctrl idx =
    (
        ctrl.SizeMode = (dotNetClass "System.Windows.Forms.PictureBoxSizeMode").StretchImage
        ctrl.Image = swBmpsNormal[idx]
        ctrl.BackColor = bgDN
        ctrl.Cursor = (dotNetClass "System.Windows.Forms.Cursors").Hand
    )

    fn swClick idx = ( setCurrentColor swColors[idx] )

    -- Swatch events
    on sw1 MouseEnter s e do sw1.Image = swBmpsHover[1] ; on sw1 MouseLeave s e do sw1.Image = swBmpsNormal[1] ; on sw1 MouseClick s e do swClick 1
    on sw2 MouseEnter s e do sw2.Image = swBmpsHover[2] ; on sw2 MouseLeave s e do sw2.Image = swBmpsNormal[2] ; on sw2 MouseClick s e do swClick 2
    on sw3 MouseEnter s e do sw3.Image = swBmpsHover[3] ; on sw3 MouseLeave s e do sw3.Image = swBmpsNormal[3] ; on sw3 MouseClick s e do swClick 3
    on sw4 MouseEnter s e do sw4.Image = swBmpsHover[4] ; on sw4 MouseLeave s e do sw4.Image = swBmpsNormal[4] ; on sw4 MouseClick s e do swClick 4
    on sw5 MouseEnter s e do sw5.Image = swBmpsHover[5] ; on sw5 MouseLeave s e do sw5.Image = swBmpsNormal[5] ; on sw5 MouseClick s e do swClick 5
    on sw6 MouseEnter s e do sw6.Image = swBmpsHover[6] ; on sw6 MouseLeave s e do sw6.Image = swBmpsNormal[6] ; on sw6 MouseClick s e do swClick 6
    on sw7 MouseEnter s e do sw7.Image = swBmpsHover[7] ; on sw7 MouseLeave s e do sw7.Image = swBmpsNormal[7] ; on sw7 MouseClick s e do swClick 7
    on sw8 MouseEnter s e do sw8.Image = swBmpsHover[8] ; on sw8 MouseLeave s e do sw8.Image = swBmpsNormal[8] ; on sw8 MouseClick s e do swClick 8

    -- Color picker
    on pnlColor MouseClick s e do
    (
        local dlg = dotNetObject "System.Windows.Forms.ColorDialog"
        dlg.Color = cc.FromArgb (currentColor.r as integer) (currentColor.g as integer) (currentColor.b as integer)
        dlg.FullOpen = true
        if (dlg.ShowDialog()) == (dotNetClass "System.Windows.Forms.DialogResult").OK do
        ( local c = dlg.Color ; setCurrentColor (color c.R c.G c.B) )
    )

    -- Spinner
    on spnThick changed val do WdxCellShade.updateThickness spnThick.value

    -- Toggles
    on btnFlipL MouseClick s e do ( flipNormals = true ; updateToggleVisuals() ; WdxCellShade.updateNormals true )
    on btnFlipR MouseClick s e do ( flipNormals = false ; updateToggleVisuals() ; WdxCellShade.updateNormals false )

    -- Filter
    on fltNone  MouseClick s e do selectFilter 1
    on fltBlend MouseClick s e do selectFilter 2
    on fltAdd   MouseClick s e do selectFilter 3
    on fltAddA  MouseClick s e do selectFilter 4

    -- Create button
    on btnCreate MouseEnter s e do btnCreate.BackgroundImage = bmpCreateH
    on btnCreate MouseLeave s e do btnCreate.BackgroundImage = bmpCreateN
    on btnCreate MouseDown  s e do btnCreate.BackgroundImage = bmpCreateP
    on btnCreate MouseUp    s e do btnCreate.BackgroundImage = bmpCreateH
    on btnCreate MouseClick s e do
    (
        if selection.count == 0 then ( messageBox (if ::WdxL != undefined then ::WdxL.t "csh_msg_no_selection" else "Please select the model first!") title:"Cell Shade" ; return undefined )
        local src = WdxCellShade.getValidMeshObjects (selection as array)
        if src.count == 0 then ( messageBox (if ::WdxL != undefined then ::WdxL.t "csh_msg_no_valid_mesh" else "No valid mesh objects!\n(Editable Poly/Mesh required)") title:"Cell Shade" ; return undefined )
        WdxCellShade.createOutlines src filterIndex flipNormals spnThick.value currentColor
    )

    -- ============================================================
    -- Open
    -- ============================================================
    on WdxCellShadeCreator open do
    (
        -- Toolkit icon on the dialog frame (WhiteoutDexUtility.ms).
        try (WdxSetDialogIcon WdxCellShadeCreator) catch()
        local sw = 1

        -- Create button
        bmpCreateN = wdx_cs_RoundedBmp (wdx_dS 154) (wdx_dS 30) (wdx_dS 14) (color 5 100 205)  bgCol
        bmpCreateH = wdx_cs_RoundedBmp (wdx_dS 154) (wdx_dS 30) (wdx_dS 14) (color 25 120 230) bgCol
        bmpCreateP = wdx_cs_RoundedBmp (wdx_dS 154) (wdx_dS 30) (wdx_dS 14) (color 2 85 185)   bgCol

        -- Toggle buttons (stroke outline style)
        bmpTogOnL  = wdx_cs_StrokeToggleBmp (wdx_dS 76) (wdx_dS 26) (wdx_dS 12) colStroke colInnerOn  bgCol sw side:#left
        bmpTogOffL = wdx_cs_StrokeToggleBmp (wdx_dS 76) (wdx_dS 26) (wdx_dS 12) colStroke colInnerOff bgCol sw side:#left
        bmpTogOnR  = wdx_cs_StrokeToggleBmp (wdx_dS 76) (wdx_dS 26) (wdx_dS 12) colStroke colInnerOn  bgCol sw side:#right
        bmpTogOffR = wdx_cs_StrokeToggleBmp (wdx_dS 76) (wdx_dS 26) (wdx_dS 12) colStroke colInnerOff bgCol sw side:#right

        -- Filter radio buttons (stroke outline style)
        bmpRadioOn  = wdx_cs_StrokeBmp (wdx_dS 154) (wdx_dS 22) (wdx_dS 10) colStroke colInnerOn  bgCol sw
        bmpRadioOff = wdx_cs_StrokeBmp (wdx_dS 154) (wdx_dS 22) (wdx_dS 10) colStroke colInnerOff bgCol sw

        -- Swatches
        swBmpsNormal = #()
        swBmpsHover = #()
        for c in swColors do
        (
            append swBmpsNormal (wdx_cs_SwatchBmp (wdx_dS 16) c (color 0 0 0) 0)
            append swBmpsHover  (wdx_cs_SwatchBmp (wdx_dS 16) c (color 255 255 255) 2)
        )

        -- Style sections
        styleSectionLabel lblColorSec (if ::WdxL != undefined then ::WdxL.t "csh_color_sec" else "Color")
        styleSectionLabel lblShapeSec (if ::WdxL != undefined then ::WdxL.t "csh_shape_sec" else "Shape")
        styleSectionLabel lblMatSec   (if ::WdxL != undefined then ::WdxL.t "csh_material_sec" else "Material")

        -- Normals label centered
        lblNormSec.Text = if ::WdxL != undefined then ::WdxL.t "csh_normals_sec" else "Normals"
        lblNormSec.ForeColor = cc.FromArgb 150 150 150
        lblNormSec.BackColor = bgDN
        lblNormSec.Font = dotNetObject "System.Drawing.Font" "Segoe UI" 9.0
        lblNormSec.TextAlign = (dotNetClass "System.Drawing.ContentAlignment").MiddleCenter

        -- RGB label
        lblRGB.ForeColor = cc.FromArgb 110 110 110
        lblRGB.BackColor = bgDN
        lblRGB.Font = dotNetObject "System.Drawing.Font" "Segoe UI" 8.0
        lblRGB.Text = "0, 0, 0"

        -- Color panel
        pnlColor.BackColor = cc.FromArgb 0 0 0
        pnlColor.Cursor = (dotNetClass "System.Windows.Forms.Cursors").Hand
        pnlColor.BorderStyle = (dotNetClass "System.Windows.Forms.BorderStyle").FixedSingle

        -- Swatches
        styleSwatch sw1 1 ; styleSwatch sw2 2 ; styleSwatch sw3 3 ; styleSwatch sw4 4
        styleSwatch sw5 5 ; styleSwatch sw6 6 ; styleSwatch sw7 7 ; styleSwatch sw8 8

        -- Separators
        sep1.BackColor = cc.FromArgb 62 62 62 ; sep1.Height = 1
        sep2.BackColor = cc.FromArgb 62 62 62 ; sep2.Height = 1
        sep3.BackColor = cc.FromArgb 62 62 62 ; sep3.Height = 1

        -- Toggle buttons
        styleBtn btnFlipL bmpTogOnL
        btnFlipL.Font = dotNetObject "System.Drawing.Font" "Segoe UI" 8.5
        btnFlipL.Text = if ::WdxL != undefined then ::WdxL.t "csh_flip_btn" else "Flip"
        btnFlipL.TextAlign = (dotNetClass "System.Drawing.ContentAlignment").MiddleCenter

        styleBtn btnFlipR bmpTogOffR
        btnFlipR.Font = dotNetObject "System.Drawing.Font" "Segoe UI" 8.5
        btnFlipR.Text = if ::WdxL != undefined then ::WdxL.t "csh_no_flip_btn" else "Don't Flip"
        btnFlipR.TextAlign = (dotNetClass "System.Drawing.ContentAlignment").MiddleCenter
        updateToggleVisuals()

        -- Filter radio buttons
        local filterNames = if ::WdxL != undefined then
            #(::WdxL.t "csh_filter_none", ::WdxL.t "csh_filter_blend", ::WdxL.t "csh_filter_additive", ::WdxL.t "csh_filter_additive_alpha")
        else
            #("None", "Blend (Transparent)", "Additive", "Additive Alpha")
        local filterBtns = #(fltNone, fltBlend, fltAdd, fltAddA)
        for i = 1 to 4 do
        (
            styleBtn filterBtns[i] bmpRadioOff
            filterBtns[i].Font = dotNetObject "System.Drawing.Font" "Segoe UI" 8.5
            filterBtns[i].Text = filterNames[i]
            filterBtns[i].TextAlign = (dotNetClass "System.Drawing.ContentAlignment").MiddleCenter
            filterBtns[i].ForeColor = cc.FromArgb 100 100 100
        )
        updateFilterVisuals()

        -- Create button
        styleBtn btnCreate bmpCreateN
        btnCreate.Font = dotNetObject "System.Drawing.Font" "Segoe UI" 10.0 (dotNetClass "System.Drawing.FontStyle").Bold
        btnCreate.ForeColor = cc.FromArgb 255 255 255
        btnCreate.Text = if ::WdxL != undefined then ::WdxL.t "csh_create_btn" else "Create Cell Shade"
        btnCreate.TextAlign = (dotNetClass "System.Drawing.ContentAlignment").MiddleCenter

        -- Tooltips
        tooltip = dotNetObject "System.Windows.Forms.ToolTip"
        tooltip.BackColor = cc.FromArgb 34 34 34
        tooltip.ForeColor = cc.FromArgb 210 210 210
        tooltip.SetToolTip pnlColor  (if ::WdxL != undefined then ::WdxL.t "csh_tip_color_pick" else "Click to pick outline color")
        tooltip.SetToolTip btnFlipL  (if ::WdxL != undefined then ::WdxL.t "csh_tip_flip" else "Flip normals (backface outline)")
        tooltip.SetToolTip btnFlipR  (if ::WdxL != undefined then ::WdxL.t "csh_tip_no_flip" else "Keep original normals")
        tooltip.SetToolTip btnCreate (if ::WdxL != undefined then ::WdxL.t "csh_tip_create" else "Create outline for selected meshes")
        tooltip.SetToolTip sw1 "Red"    ; tooltip.SetToolTip sw2 "Orange"
        tooltip.SetToolTip sw3 "Yellow" ; tooltip.SetToolTip sw4 "Cyan"
        tooltip.SetToolTip sw5 "Green"  ; tooltip.SetToolTip sw6 "Purple"
        tooltip.SetToolTip sw7 "Sky"    ; tooltip.SetToolTip sw8 "Blue"

        -- Localize remaining controls
        if ::WdxL != undefined then
        (
            WdxCellShadeCreator.title = ::WdxL.t "csh_title"
            lblThick.text = ::WdxL.t "csh_thickness_lbl"
        )
    )
)


-- Opened on demand from the macroscript — not at script load, so Max
-- startup doesn't pop the window.
fn wdxOpenCellShadeDialog =
(
    try ( destroyDialog WdxCellShadeCreator ) catch()
    CreateDialog WdxCellShadeCreator style:#(#style_titlebar, #style_border, #style_sysmenu, #style_minimizebox, #style_maximizebox, #style_resizing)
)
