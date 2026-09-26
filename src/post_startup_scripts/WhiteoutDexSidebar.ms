/*
    WhiteoutDex Sidebar — Dockable Tool Panel
    ======================================
    A vertical button bar for quick access to all WhiteoutDex tools.
    Can be docked to any side of the 3ds Max window.
    Works in 3ds Max 2016-2027.
    
    Icons: 24x24 PNG files in whiteoutdex_icons folder.
    Naming: wdx_sb_import.png, wdx_sb_export.png, etc.
*/

-- Clean up previous instance
try (cui.UnRegisterDialogBar ::WhiteoutDexSidebarRollout) catch()
try (destroyDialog ::WhiteoutDexSidebarRollout) catch()

rollout WhiteoutDexSidebarRollout "WhiteoutDex" width:38 height:590
(
    local iconPath = (::WhiteoutDexInstallRoot) + "\\whiteoutdex_icons\\"
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
    
    -- ── Tool Buttons (dotnet PictureBox for icon support) ──
    dotNetControl btn_import      "System.Windows.Forms.Button" pos:[xOfs, yStart + 0*step] width:btnW height:btnSize
    dotNetControl btn_export      "System.Windows.Forms.Button" pos:[xOfs, yStart + 1*step] width:btnW height:btnSize
    
    dotNetControl lbl_sep1 "System.Windows.Forms.Panel" pos:[5, yStart + 2*step] width:28 height:1
    
    dotNetControl btn_seqmgr      "System.Windows.Forms.Button" pos:[xOfs, yStart + 2*step+4] width:btnW height:btnSize
    dotNetControl btn_viskey      "System.Windows.Forms.Button" pos:[xOfs, yStart + 3*step+4] width:btnW height:btnSize
    dotNetControl btn_animgeneral "System.Windows.Forms.Button" pos:[xOfs, yStart + 4*step+4] width:btnW height:btnSize
    dotNetControl btn_animctrl    "System.Windows.Forms.Button" pos:[xOfs, yStart + 5*step+4] width:btnW height:btnSize
    dotNetControl btn_animskin    "System.Windows.Forms.Button" pos:[xOfs, yStart + 6*step+4] width:btnW height:btnSize
    dotNetControl btn_animscaler  "System.Windows.Forms.Button" pos:[xOfs, yStart + 7*step+4] width:btnW height:btnSize
    dotNetControl btn_skinchg     "System.Windows.Forms.Button" pos:[xOfs, yStart + 8*step+4] width:btnW height:btnSize
    dotNetControl btn_griddummy   "System.Windows.Forms.Button" pos:[xOfs, yStart + 9*step+4] width:btnW height:btnSize
    dotNetControl btn_objtools    "System.Windows.Forms.Button" pos:[xOfs, yStart + 10*step+4] width:btnW height:btnSize
    dotNetControl btn_nodemgr     "System.Windows.Forms.Button" pos:[xOfs, yStart + 11*step+4] width:btnW height:btnSize
    dotNetControl btn_kfoptimizer "System.Windows.Forms.Button" pos:[xOfs, yStart + 12*step+4] width:btnW height:btnSize
    dotNetControl btn_cellshade   "System.Windows.Forms.Button" pos:[xOfs, yStart + 13*step+4] width:btnW height:btnSize
    dotNetControl btn_matfix      "System.Windows.Forms.Button" pos:[xOfs, yStart + 14*step+4] width:btnW height:btnSize
    dotNetControl btn_objsettings "System.Windows.Forms.Button" pos:[xOfs, yStart + 15*step+4] width:btnW height:btnSize
    dotNetControl btn_renderer    "System.Windows.Forms.Button" pos:[xOfs, yStart + 16*step+4] width:btnW height:btnSize

    dotNetControl lbl_sep2 "System.Windows.Forms.Panel" pos:[5, yStart + 17*step+4] width:28 height:1

    dotNetControl btn_settings    "System.Windows.Forms.Button" pos:[xOfs, yStart + 17*step+8] width:btnW height:btnSize
    dotNetControl btn_about       "System.Windows.Forms.Button" pos:[xOfs, yStart + 18*step+8] width:btnW height:btnSize
    
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
    on btn_seqmgr Click do      macros.run "WhiteoutDex Toolkit" "WhiteoutDex_SequenceManager"
    on btn_viskey Click do      macros.run "WhiteoutDex Toolkit" "WhiteoutDex_VisibilityKeyer"
    on btn_animgeneral Click do macros.run "WhiteoutDex Toolkit" "WhiteoutDex_AnimGeneral"
    on btn_animctrl Click do    macros.run "WhiteoutDex Toolkit" "WhiteoutDex_AnimControllers"
    on btn_animskin Click do    macros.run "WhiteoutDex Toolkit" "WhiteoutDex_AnimSkin"
    on btn_animscaler Click do  macros.run "WhiteoutDex Toolkit" "WhiteoutDex_AnimScaler"
    on btn_skinchg Click do     macros.run "WhiteoutDex Toolkit" "WhiteoutDex_SkinChanger"
    on btn_griddummy Click do   macros.run "WhiteoutDex Toolkit" "WhiteoutDex_GridDummy"
    on btn_objtools Click do    macros.run "WhiteoutDex Toolkit" "WhiteoutDex_ObjectTools"
    on btn_nodemgr Click do     macros.run "WhiteoutDex Toolkit" "WhiteoutDex_NodeManager"
    on btn_kfoptimizer Click do macros.run "WhiteoutDex Toolkit" "WhiteoutDex_KeyframeOptimizer"
    on btn_cellshade Click do   macros.run "WhiteoutDex Toolkit" "WhiteoutDex_CellShadeCreator"
    on btn_matfix Click do      macros.run "WhiteoutDex Toolkit" "WhiteoutDex_MaterialFix"
    on btn_objsettings Click do macros.run "WhiteoutDex Toolkit" "WhiteoutDex_ObjectSettings"
    on btn_renderer Click do    macros.run "WhiteoutDex Toolkit" "WhiteoutDex_Renderer"
    on btn_settings Click do    macros.run "WhiteoutDex Toolkit" "WhiteoutDex_Settings"
    on btn_about Click do       macros.run "WhiteoutDex Toolkit" "WhiteoutDex_About"
    
    -- ── Rollout Open: Setup icons + dock ──
    on WhiteoutDexSidebarRollout open do
    (
        -- Toolkit icon on the dialog frame (WhiteoutDexUtility.ms).
        try (WdxSetDialogIcon WhiteoutDexSidebarRollout) catch()
        if ::WdxL != undefined do
            try (WhiteoutDexSidebarRollout.title = ::WdxL.t "mgr_whiteoutdex_manager_macbtn") catch ()
        -- Separator lines
        lbl_sep1.BackColor = lbl_sep1.BackColor.FromArgb 80 80 80
        lbl_sep2.BackColor = lbl_sep2.BackColor.FromArgb 80 80 80
        -- Setup all buttons with icons
        setupButton btn_import      "wdx_sb_import.png" "IMP" (::WdxL.t "menu_importer")
        setupButton btn_export      "wdx_sb_export.png" "EXP" (::WdxL.t "menu_exporter")
        setupButton btn_seqmgr      "wdx_sb_sequence.png" "SEQ" (::WdxL.t "menu_sequence_manager")
        setupButton btn_viskey      "wdx_sb_visibility.png" "VIS" (::WdxL.t "menu_visibility_keyer")
        setupButton btn_animgeneral "wdx_sb_animgeneral.png" "GEN" (::WdxL.t "menu_anim_general")
        setupButton btn_animctrl    "wdx_sb_animctrl.png" "CTR" (::WdxL.t "menu_anim_controllers")
        setupButton btn_animskin    "wdx_sb_animskin.png" "SKI" (::WdxL.t "menu_anim_skin")
        setupButton btn_animscaler  "wdx_sb_animscaler.png" "SCL" (::WdxL.t "menu_anim_scaler")
        setupButton btn_skinchg     "wdx_sb_skinchanger.png" "SKN" (::WdxL.t "menu_skin_changer")
        setupButton btn_griddummy   "wdx_sb_griddummy.png" "GRD" (::WdxL.t "menu_grid_dummy")
        setupButton btn_objtools    "wdx_sb_objtools.png" "OBJ" (::WdxL.t "menu_object_tools")
        setupButton btn_nodemgr     "wdx_sb_nodemanager.png" "NOD" (::WdxL.t "menu_node_manager")
        setupButton btn_kfoptimizer "wdx_sb_kfoptimizer.png" "KFO" (::WdxL.t "menu_keyframe_optimizer")
        setupButton btn_cellshade   "wdx_sb_cellshade.png" "CSH" (::WdxL.t "menu_cell_shade")
        setupButton btn_matfix      "wdx_sb_materialfix.png" "MAT" (::WdxL.t "menu_material_fix")
        setupButton btn_objsettings "wdx_sb_objsettings.png" "SET" (::WdxL.t "menu_object_settings")
        setupButton btn_renderer    "wdx_sb_renderer.png" "RND" (::WdxL.t "menu_renderer")
        setupButton btn_settings    "wdx_sb_settings.png" "CFG" (::WdxL.t "set_settings_title")
        setupButton btn_about       "wdx_sb_about.png" "(?)" (::WdxL.t "about_title")
        
        -- Dock
        -- Per-user setting (WdxGetUserSetting, WhiteoutDexGlobals.ms)
        local dockState = ::WdxGetUserSetting "Sidebar" "DockState" "cui_dock_left"
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
            ::WdxSetUserSetting "Sidebar" "DockState" (state as string)
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
        -- Max 2016 doesn't expose `this` inside struct methods AND can't
        -- resolve bare `member = value` either ("Struct member access
        -- requires instance"). Route every member touch through the
        -- global instance so it works on every Max version.
        try (cui.UnRegisterDialogBar ::WhiteoutDexSidebarRollout) catch()
        try (destroyDialog ::WhiteoutDexSidebarRollout) catch()
        createDialog ::WhiteoutDexSidebarRollout width:38 height:620 \
            style:#(#style_titlebar, #style_border, #style_sysmenu)
        ::WhiteoutDexSidebar.isOpen = true
    ),

    fn hide =
    (
        try (cui.UnRegisterDialogBar ::WhiteoutDexSidebarRollout) catch()
        try (destroyDialog ::WhiteoutDexSidebarRollout) catch()
        ::WhiteoutDexSidebar.isOpen = false
    ),

    fn toggle =
    (
        if ::WhiteoutDexSidebar.isOpen then ::WhiteoutDexSidebar.hide()
        else ::WhiteoutDexSidebar.show()
    )
)
WhiteoutDexSidebar = WhiteoutDexSidebarStruct()

-- Auto-show if enabled in settings
(
    if (::WdxGetUserSetting "Sidebar" "Enabled" "1") != "0" then WhiteoutDexSidebar.show()
)
