/*
    WhiteoutDexMenuCallback.ms
    ==========================
    Handles registration of the WhiteoutDex menu in the 3ds Max menu bar.
    
    Dual-Path:
      Max 2016-2024  → menuMan API (legacy)
      Max 2025+      → #cuiRegisterMenus callback (GUID-based)
    
    Menu structure:
        WhiteoutDex
        ├── WhiteoutDex Manager
        ├── Settings...
        ├── ─────────────────
        └── About...
    
    Note: Import/Export are handled natively by MDLXImporter.dle / MDLXExporter.dle
          and appear automatically under File > Import / File > Export.
    
    Usage:
        ::WhiteoutDexMenu.register()
    
    Compatibility: 3ds Max 2016 - 2027
*/

-- ============================================================================
-- MAIN MENU STRUCT
-- ============================================================================

struct WhiteoutDexMenuStruct
(
    -- Max version (internal number): 18000=2016, 19000=2017, ..., 27000=2025
    private maxVer = (maxVersion())[1],
    
    -- True if we should use the new GUID-based menu system (Max 2025+)
    private useNewMenuSystem = ((maxVersion())[1] >= 27000),
    
    -- ============================================================================
    -- GUID-BASED MENU (Max 2025+)
    -- All GUIDs unique to WhiteoutDex — no overlap with NeoDex
    -- ============================================================================
    
    private actionTableId = 647394,  -- system MacroScript action table (shared by all plugins)
    
    -- Callback for the new menu system (Max 2025+)
    public fn menuCallbackNew =
    (
        local menuMgr = callbacks.notificationParam()
        local mainMenuBar = menuMgr.mainMenuBar
        local m = mainMenuBar.CreateSubMenu "BDBFDF2A-73A5-44B9-B98C-967D9C54835B" "WhiteoutDex"
        
        m.CreateAction "DD0CF245-EA3B-4CCA-ADC6-01FBDAF0F936" 647394 "WhiteoutDex_Importer`WhiteoutDex Toolkit"
        m.CreateAction "2ED521B2-6A98-45A9-A0C3-3D5D4B88EE7D" 647394 "WhiteoutDex_ImportCASC`WhiteoutDex Toolkit"
        m.CreateAction "0F62518F-5358-4D12-ABDB-3350D42094EF" 647394 "WhiteoutDex_Exporter`WhiteoutDex Toolkit"
        m.CreateSeparator "18F3B751-A532-4E6D-8AE1-7CA182A0E553"
        m.CreateAction "CE15B5A9-EBE3-49F3-9CB7-8385D3CBB515" 647394 "WhiteoutDex_SequenceManager`WhiteoutDex Toolkit"
        m.CreateAction "17FA40DC-8FCF-4D5F-B544-E804A4080950" 647394 "WhiteoutDex_VisibilityKeyer`WhiteoutDex Toolkit"
        m.CreateAction "8116A922-9F6F-4063-81F6-FEBE0FAC7066" 647394 "WhiteoutDex_NodeManager`WhiteoutDex Toolkit"
        m.CreateSeparator "35EA6CBC-120F-42BB-90D4-109AB5AE0F6B"
        m.CreateAction "CFC828A8-5E46-4946-8497-E86CBE3D5DAE" 647394 "WhiteoutDex_AnimGeneral`WhiteoutDex Toolkit"
        m.CreateAction "F153E90B-87F4-407B-A0E1-48CE33C8F66E" 647394 "WhiteoutDex_AnimControllers`WhiteoutDex Toolkit"
        m.CreateAction "E41C9C72-BFA7-4FD6-8AB8-CC96A3C6F3CA" 647394 "WhiteoutDex_AnimSkin`WhiteoutDex Toolkit"
        m.CreateAction "C1B05DD5-1434-4B25-AD70-513D80EB84BC" 647394 "WhiteoutDex_AnimScaler`WhiteoutDex Toolkit"
        m.CreateSeparator "16705361-29F9-4DE0-854D-BAA46C5B5681"
        m.CreateAction "B59300EB-790A-4335-A11E-9C821DE7BFC7" 647394 "WhiteoutDex_SkinChanger`WhiteoutDex Toolkit"
        m.CreateAction "64B61757-30BC-45F4-8365-12EEAB81C876" 647394 "WhiteoutDex_GridDummy`WhiteoutDex Toolkit"
        m.CreateAction "EAB58810-F22E-4C1E-8EDB-526362EA648C" 647394 "WhiteoutDex_ObjectTools`WhiteoutDex Toolkit"
        m.CreateAction "358E059E-5EFA-451B-92FB-969E46B8CB3C" 647394 "WhiteoutDex_ObjectSettings`WhiteoutDex Toolkit"
        m.CreateAction "A72E1B34-5D89-4F01-B6C3-E8D7F2A19045" 647394 "WhiteoutDex_KeyframeOptimizer`WhiteoutDex Toolkit"
        m.CreateAction "B83F2C45-6E9A-4012-C7D4-F9E803B2A156" 647394 "WhiteoutDex_CellShadeCreator`WhiteoutDex Toolkit"
        m.CreateSeparator "D4944802-FCEE-4586-BBF2-E839D506F4CC"
        m.CreateAction "EF9A6CC4-C7A8-4646-92D3-09DCF207D636" 647394 "WhiteoutDex_Renderer`WhiteoutDex Toolkit"
        m.CreateAction "378079A3-999E-46F6-9D2F-F8015B5BDF1A" 647394 "WhiteoutDex_Settings`WhiteoutDex Toolkit"
        m.CreateSeparator "879F02CE-A33B-49D0-9AA5-6BB7BB867336"
        m.CreateAction "3E4B0A7D-3DFB-4390-87B8-FA2CE0C8571A" 647394 "WhiteoutDex_About`WhiteoutDex Toolkit"
    ),
    
    -- ============================================================================
    -- LEGACY MENU (Max 2016-2024)
    -- ============================================================================
    
    private fn isMenuInMainBar mainBar subMenu =
    (
        local result = false
        local n = mainBar.numItems()
        for i = 1 to n do
        (
            local it = mainBar.getItem i
            if (it.getSubMenu()) == subMenu then result = true
        )
        result
    ),
    
    private fn cleanMenu m =
    (
        local n = m.numItems()
        for i = n to 1 by -1 do m.removeItemByPosition i
    ),
    
    public fn installLegacyMenu =
    (
        local main = menuMan.getMainMenuBar()
        local whiteoutDexMain = menuMan.findMenu "WhiteoutDex"
        
        if whiteoutDexMain == undefined then
        (
            whiteoutDexMain = menuMan.createMenu "WhiteoutDex"
            local subWhiteoutDex = menuMan.createSubMenuItem "WhiteoutDex" whiteoutDexMain
            main.addItem subWhiteoutDex -1
        )
        else
        (
            if not (this.isMenuInMainBar main whiteoutDexMain) then
            (
                local subWhiteoutDex = menuMan.createSubMenuItem "WhiteoutDex" whiteoutDexMain
                main.addItem subWhiteoutDex -1
            )
        )
        
        local num = whiteoutDexMain.numItems()
        if num != 0 then this.cleanMenu whiteoutDexMain
        
        local managerItem  = menuMan.createActionItem "WhiteoutDex_Manager"  "WhiteoutDex Toolkit"
        local settingsItem = menuMan.createActionItem "WhiteoutDex_Settings" "WhiteoutDex Toolkit"
        local aboutItem    = menuMan.createActionItem "WhiteoutDex_About"    "WhiteoutDex Toolkit"
        local sep1 = menuMan.createSeparatorItem()
        
        try
        (
            whiteoutDexMain.addItem managerItem -1
            whiteoutDexMain.addItem settingsItem -1
            whiteoutDexMain.addItem sep1 -1
            whiteoutDexMain.addItem aboutItem -1
            
            menuMan.updateMenuBar()
        )
        catch print (getCurrentException())
        
        print "WhiteoutDex Menu installed (legacy menuMan)"
    ),
    
    -- ============================================================================
    -- REGISTRATION (auto-selects correct method)
    -- ============================================================================
    
    public fn register =
    (
        if this.useNewMenuSystem then
        (
            -- Max 2025+: use GUID-based callback
            callbacks.removeScripts id:#WhiteoutDexMenu
            callbacks.addScript #cuiRegisterMenus "::WhiteoutDexMenu.menuCallbackNew()" id:#WhiteoutDexMenu
            print "WhiteoutDex Menu registered (cuiRegisterMenus)"
        )
        else
        (
            -- Max 2016-2024: defer to postSystemStartup so macroscripts are registered first
            callbacks.removeScripts #postSystemStartup id:#WhiteoutDexMenu
            callbacks.addScript #postSystemStartup "::WhiteoutDexMenu.installLegacyMenu()" id:#WhiteoutDexMenu persistent:false
            print "WhiteoutDex Menu deferred to postSystemStartup (legacy menuMan)"
        )
    ),
    
    -- ============================================================================
    -- UNREGISTER (cleanup)
    -- ============================================================================
    
    public fn unregister =
    (
        if this.useNewMenuSystem then
        (
            callbacks.removeScripts id:#WhiteoutDexMenu
        )
        else
        (
            -- Remove legacy menu
            local whiteoutDexMain = menuMan.findMenu "WhiteoutDex"
            if whiteoutDexMain != undefined then
            (
                menuMan.unRegisterMenu whiteoutDexMain
                menuMan.updateMenuBar()
            )
        )
    )
)

-- ============================================================================
-- GLOBAL INSTANCE & AUTO-REGISTRATION
-- ============================================================================

global WhiteoutDexMenu = WhiteoutDexMenuStruct()
::WhiteoutDexMenu.register()
