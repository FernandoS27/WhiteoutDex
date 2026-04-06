/*
    WhiteoutDex Sidebar — Dockable Tool Panel
    ======================================
    A vertical button bar for quick access to all WhiteoutDex tools.
    Can be docked to any side of the 3ds Max window.
    Works in 3ds Max 2016-2027.
    
    Icons: 24x24 PNG files in whiteoutdex_icons folder.
    Naming: ndx_sb_import.png, ndx_sb_export.png, etc.
*/

-- Clean up previous instance
try (cui.UnRegisterDialogBar ::WhiteoutDexSidebarRollout) catch()
try (destroyDialog ::WhiteoutDexSidebarRollout) catch()

rollout WhiteoutDexSidebarRollout "WhiteoutDex" width:38 height:530
(
    local iniFile = (systemTools.getEnvVariable "APPDATA") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex\\WhiteoutDex_Settings.ini"
    local iconPath = (systemTools.getEnvVariable "APPDATA") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex\\whiteoutdex_icons\\"
    local btnSize = 28
    local btnW = 32
    local xOfs = 3
    local yStart = 4
    local step = 30
    
    -- ── Helper: Load PNG icon onto a button ──
    fn loadIcon btn iconName =
    (
        try (
            local imgFile = iconPath + iconName
            if doesFileExist imgFile then
            (
                local bmp = dotNetObject "System.Drawing.Bitmap" imgFile
                btn.images = undefined
                btn.text = ""
                -- Use dotnet to set the image
                local picBox = btn
                -- MaxScript buttons don't support direct image assignment,
                -- so we keep text as fallback if icon not found
                btn.text = ""
            )
        ) catch ()
    )
    
    -- ── Helper: Open a tool as standalone dialog (same as Undock) ──
    fn openToolStandalone toolStruct rolloutRef toolWidth toolHeight =
    (
        if rolloutRef.isDisplayed and toolStruct.isPoppedOut then return()
        
        if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
        (
            local rollouts = ::whiteoutdexManagerFloater.rollouts
            for i = 1 to rollouts.count do
            (
                if rollouts[i] == rolloutRef then
                (
                    try (toolStruct.savedIndex = i) catch()
                    removeRollout rolloutRef ::whiteoutdexManagerFloater
                    exit
                )
            )
        )
        
        toolStruct.isPoppedOut = true
        createDialog rolloutRef width:toolWidth height:toolHeight \
            style:#(#style_titlebar, #style_sysmenu, #style_minimizebox, #style_resizing)
    )
    
    -- ── Helper: Open AnimTools rollout standalone ──
    fn openAnimToolRollout rolloutRef poppedOutProp savedIndexProp toolWidth toolHeight =
    (
        local atools = ::AnimTools
        if atools == undefined then return()
        
        if rolloutRef.isDisplayed and (getProperty atools poppedOutProp) then return()
        
        if ::whiteoutdexManagerFloater != undefined and ::whiteoutdexManagerFloater.open then
        (
            local rollouts = ::whiteoutdexManagerFloater.rollouts
            for i = 1 to rollouts.count do
            (
                if rollouts[i] == rolloutRef then
                (
                    setProperty atools savedIndexProp i
                    removeRollout rolloutRef ::whiteoutdexManagerFloater
                    exit
                )
            )
        )
        
        setProperty atools poppedOutProp true
        createDialog rolloutRef width:toolWidth height:toolHeight \
            style:#(#style_titlebar, #style_sysmenu, #style_minimizebox, #style_resizing)
    )
    
    -- ── Tool Buttons (dotnet PictureBox for icon support) ──
    dotNetControl btn_import      "System.Windows.Forms.Button" pos:[xOfs, yStart + 0*step] width:btnW height:btnSize
    dotNetControl btn_export      "System.Windows.Forms.Button" pos:[xOfs, yStart + 1*step] width:btnW height:btnSize
    
    dotNetControl lbl_sep1 "System.Windows.Forms.Panel" pos:[5, yStart + 2*step] width:28 height:1
    
    dotNetControl btn_manager     "System.Windows.Forms.Button" pos:[xOfs, yStart + 2*step+4] width:btnW height:btnSize
    dotNetControl btn_seqmgr      "System.Windows.Forms.Button" pos:[xOfs, yStart + 3*step+4] width:btnW height:btnSize
    dotNetControl btn_viskey      "System.Windows.Forms.Button" pos:[xOfs, yStart + 4*step+4] width:btnW height:btnSize
    dotNetControl btn_animgeneral "System.Windows.Forms.Button" pos:[xOfs, yStart + 5*step+4] width:btnW height:btnSize
    dotNetControl btn_animctrl    "System.Windows.Forms.Button" pos:[xOfs, yStart + 6*step+4] width:btnW height:btnSize
    dotNetControl btn_animskin    "System.Windows.Forms.Button" pos:[xOfs, yStart + 7*step+4] width:btnW height:btnSize
    dotNetControl btn_animscaler  "System.Windows.Forms.Button" pos:[xOfs, yStart + 8*step+4] width:btnW height:btnSize
    dotNetControl btn_skinchg     "System.Windows.Forms.Button" pos:[xOfs, yStart + 9*step+4] width:btnW height:btnSize
    dotNetControl btn_griddummy   "System.Windows.Forms.Button" pos:[xOfs, yStart + 10*step+4] width:btnW height:btnSize
    dotNetControl btn_objtools    "System.Windows.Forms.Button" pos:[xOfs, yStart + 11*step+4] width:btnW height:btnSize
    dotNetControl btn_nodemgr     "System.Windows.Forms.Button" pos:[xOfs, yStart + 12*step+4] width:btnW height:btnSize
    dotNetControl btn_teamcolor   "System.Windows.Forms.Button" pos:[xOfs, yStart + 13*step+4] width:btnW height:btnSize
    dotNetControl btn_objsettings "System.Windows.Forms.Button" pos:[xOfs, yStart + 14*step+4] width:btnW height:btnSize
    
    dotNetControl lbl_sep2 "System.Windows.Forms.Panel" pos:[5, yStart + 15*step+4] width:28 height:1
    
    dotNetControl btn_settings    "System.Windows.Forms.Button" pos:[xOfs, yStart + 15*step+8] width:btnW height:btnSize
    dotNetControl btn_about       "System.Windows.Forms.Button" pos:[xOfs, yStart + 16*step+8] width:btnW height:btnSize
    
    -- ── Setup button appearance ──
    fn setupButton btn iconFileName fallbackText tooltipText =
    (
        local flatStyle = (dotNetClass "System.Windows.Forms.FlatStyle").Flat
        btn.FlatStyle = flatStyle
        btn.FlatAppearance.BorderSize = 0
        btn.BackColor = btn.BackColor.FromArgb 60 60 60
        btn.ForeColor = btn.ForeColor.FromArgb 200 200 200
        btn.Font = dotNetObject "System.Drawing.Font" "Segoe UI" 7.0
        btn.TextAlign = (dotNetClass "System.Drawing.ContentAlignment").MiddleCenter
        btn.Cursor = (dotNetClass "System.Windows.Forms.Cursors").Hand
        
        local imgFile = iconPath + iconFileName
        if doesFileExist imgFile then
        (
            try (
                local bmp = dotNetObject "System.Drawing.Bitmap" imgFile
                btn.BackgroundImage = bmp
                btn.BackgroundImageLayout = (dotNetClass "System.Windows.Forms.ImageLayout").Zoom
                btn.Text = ""
            ) catch ( btn.Text = fallbackText )
        )
        else
        (
            btn.Text = fallbackText
        )
        
        -- Tooltip
        local tt = dotNetObject "System.Windows.Forms.ToolTip"
        tt.SetToolTip btn tooltipText
    )
    
    -- ── Button Click Handlers ──
    on btn_import Click do      macros.run "WhiteoutDex Toolkit" "WhiteoutDex_Importer"
    on btn_export Click do      macros.run "WhiteoutDex Toolkit" "WhiteoutDex_Exporter"
    on btn_manager Click do     macros.run "WhiteoutDex Toolkit" "WhiteoutDex_Manager"
    on btn_settings Click do    macros.run "WhiteoutDex Toolkit" "WhiteoutDex_Settings"
    on btn_about Click do       macros.run "WhiteoutDex Toolkit" "WhiteoutDex_About"
    
    on btn_seqmgr Click do
        try (openToolStandalone ::SequenceManager ::SequenceManager.mainRollout 444 402) catch()
    
    on btn_viskey Click do
        try (openToolStandalone ::VisibilityKeyer ::VisibilityKeyer.mainRollout 440 242) catch()
    
    on btn_animgeneral Click do
        try (openAnimToolRollout ::AnimTools.generalRoll #isGeneralPoppedOut #savedGeneralIndex 440 202) catch()
    
    on btn_animctrl Click do
        try (openAnimToolRollout ::AnimTools.controllersRoll #isControllersPoppedOut #savedControllersIndex 440 177) catch()
    
    on btn_animskin Click do
        try (openAnimToolRollout ::AnimTools.skinRoll #isSkinPoppedOut #savedSkinIndex 440 92) catch()
    
    on btn_animscaler Click do
        try (openAnimToolRollout ::AnimTools.scalerRoll #isScalerPoppedOut #savedScalerIndex 440 172) catch()
    
    on btn_skinchg Click do
        try (openToolStandalone ::SkinChanger ::SkinChanger.mainRollout 440 180) catch()
    
    on btn_griddummy Click do
        try (openToolStandalone ::GridAndDummyCreator ::GridAndDummyCreator.mainRollout 440 142) catch()
    
    on btn_objtools Click do
        try (openToolStandalone ::ObjectManipulationTools ::ObjectManipulationTools.mainRollout 440 362) catch()
    
    on btn_nodemgr Click do
        try (openToolStandalone ::NodeManager ::NodeManager.mainRollout 420 365) catch()
    
    on btn_teamcolor Click do
        try (openToolStandalone ::TeamColorManager ::TeamColorManager.mainRollout 440 82) catch()
    
    on btn_objsettings Click do
        try (openToolStandalone ::ObjectSettings ::ObjectSettings.mainRollout 440 232) catch()
    
    -- ── Rollout Open: Setup icons + dock ──
    on WhiteoutDexSidebarRollout open do
    (
        -- Separator lines
        lbl_sep1.BackColor = lbl_sep1.BackColor.FromArgb 80 80 80
        lbl_sep2.BackColor = lbl_sep2.BackColor.FromArgb 80 80 80
        -- Setup all buttons with icons
        setupButton btn_import      "ndx_sb_import.png"      "IMP" "Import MDX/MDL"
        setupButton btn_export      "ndx_sb_export.png"      "EXP" "Export MDX/MDL"
        setupButton btn_manager     "ndx_sb_manager.png"     "MGR" "WhiteoutDex Manager"
        setupButton btn_seqmgr      "ndx_sb_sequence.png"    "SEQ" "Sequence Manager"
        setupButton btn_viskey      "ndx_sb_visibility.png"  "VIS" "Visibility Keyer"
        setupButton btn_animgeneral "ndx_sb_animgeneral.png" "GEN" "Anim: General Tools"
        setupButton btn_animctrl    "ndx_sb_animctrl.png"    "CTR" "Anim: Controllers"
        setupButton btn_animskin    "ndx_sb_animskin.png"    "SKI" "Anim: Skinning"
        setupButton btn_animscaler  "ndx_sb_animscaler.png"  "SCL" "Anim: Model Scaler"
        setupButton btn_skinchg     "ndx_sb_skinchanger.png" "SKN" "Skin Changer"
        setupButton btn_griddummy   "ndx_sb_griddummy.png"   "GRD" "Grid & Dummy Creator"
        setupButton btn_objtools    "ndx_sb_objtools.png"    "OBJ" "Object Tools"
        setupButton btn_nodemgr     "ndx_sb_nodemanager.png" "NOD" "Node Manager"
        setupButton btn_teamcolor   "ndx_sb_teamcolor.png"   "TC"  "Team Color Manager"
        setupButton btn_objsettings "ndx_sb_objsettings.png" "SET" "Object Settings"
        setupButton btn_settings    "ndx_sb_settings.png"    "CFG" "WhiteoutDex Settings"
        setupButton btn_about       "ndx_sb_about.png"       "(?)" "About WhiteoutDex"
        
        -- Dock
        local dockState = getINISetting iniFile "Sidebar" "DockState"
        local dockFlag = case dockState of
        (
            "cui_dock_left":  #cui_dock_left
            "cui_dock_right": #cui_dock_right
            default:          #cui_dock_left
        )
        
        cui.RegisterDialogBar WhiteoutDexSidebarRollout \
            minSize:[38, 200] \
            maxSize:[38, -1] \
            style:#(#cui_dock_left, #cui_dock_right, #cui_floatable, #cui_handles)
        
        cui.DockDialogBar WhiteoutDexSidebarRollout dockFlag
    )
    
    on WhiteoutDexSidebarRollout close do
    (
        try (
            local state = cui.getDockState WhiteoutDexSidebarRollout
            if (maxVersion())[1] >= 20000 then
                setINISetting iniFile "Sidebar" "DockState" (state as string) forceUTF16:false
            else
                setINISetting iniFile "Sidebar" "DockState" (state as string)
        ) catch()
        try (cui.UnRegisterDialogBar WhiteoutDexSidebarRollout) catch()
    )
)

-- ── Public API ──
global WhiteoutDexSidebar
struct WhiteoutDexSidebarStruct
(
    isOpen = false,
    
    fn show =
    (
        try (cui.UnRegisterDialogBar ::WhiteoutDexSidebarRollout) catch()
        try (destroyDialog ::WhiteoutDexSidebarRollout) catch()
        createDialog ::WhiteoutDexSidebarRollout width:38 height:530 \
            style:#(#style_titlebar, #style_border, #style_sysmenu)
        this.isOpen = true
    ),
    
    fn hide =
    (
        try (cui.UnRegisterDialogBar ::WhiteoutDexSidebarRollout) catch()
        try (destroyDialog ::WhiteoutDexSidebarRollout) catch()
        this.isOpen = false
    ),
    
    fn toggle =
    (
        if this.isOpen then this.hide()
        else this.show()
    )
)
WhiteoutDexSidebar = WhiteoutDexSidebarStruct()

-- Auto-show if enabled in settings
(
    local iniFile = (systemTools.getEnvVariable "APPDATA") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex\\WhiteoutDex_Settings.ini"
    local enabled = getINISetting iniFile "Sidebar" "Enabled"
    if enabled == "1" then WhiteoutDexSidebar.show()
)
