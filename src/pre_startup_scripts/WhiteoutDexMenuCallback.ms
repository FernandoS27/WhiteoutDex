/*
    WhiteoutDexMenuCallback.ms
    
    Refactored as Struct for better encapsulation and maintainability.
    Handles registration of the WhiteoutDex menu in the 3ds Max menu bar.
    
    Menu structure:
        WhiteoutDex
        ├── WhiteoutDex Exporter
        ├── WhiteoutDex Importer
        ├── ─────────────────
        ├── WhiteoutDex Manager
        ├── Settings...
        ├── ─────────────────
        └── About...
    
    Usage:
        ::WhiteoutDexMenu.register()
*/

-- ============================================================================
-- MAIN MENU STRUCT
-- ============================================================================

struct WhiteoutDexMenuStruct
(
    -- ============================================================================
    -- MENU GUIDS (unique identifiers for menu items)
    -- ============================================================================
    
    private menuGUID       = "14AACE63-4B4A-4D24-B0DC-CE7BE8039887",
    private exporterGUID   = "49C3AD2F-56E6-4FD7-BA75-C37DBE46F561",
    private importerGUID   = "2C130A42-9564-45C9-88B3-B4DFEAE2F583",
    private separator1GUID = "997EB101-2553-4AF0-B4E6-24D42AE680C8",
    private managerGUID    = "0C915529-C664-4849-A718-0BEE8D9DD2FB",
    private settingsGUID   = "B5C8D2E1-3F97-4A5B-8D16-9F4E6C7A2B03",
    private separator2GUID = "A7F83B12-6E41-4C9A-B2D5-8F1E3A7C9D04",
    private aboutGUID      = "D3B7E5A1-2F84-4C6E-9A13-7E2D5B8F4C01",
    
    private actionTableId = 647394,
    
    -- ============================================================================
    -- MENU CONSTRUCTION
    -- ============================================================================
    
    -- Callback function invoked by 3ds Max menu system
    public fn menuCallback =
    (
        local menuMgr = callbacks.notificationParam()
        local mainMenuBar = menuMgr.mainMenuBar
        local whiteoutDexMainMenu = mainMenuBar.CreateSubMenu "14AACE63-4B4A-4D24-B0DC-CE7BE8039887" "WhiteoutDex"
        
        whiteoutDexMainMenu.CreateAction "49C3AD2F-56E6-4FD7-BA75-C37DBE46F561" 647394 "WhiteoutDex_Exporter`WhiteoutDex Toolkit"
        whiteoutDexMainMenu.CreateAction "2C130A42-9564-45C9-88B3-B4DFEAE2F583" 647394 "WhiteoutDex_Importer`WhiteoutDex Toolkit"
        whiteoutDexMainMenu.CreateSeparator "997EB101-2553-4AF0-B4E6-24D42AE680C8"
        whiteoutDexMainMenu.CreateAction "0C915529-C664-4849-A718-0BEE8D9DD2FB" 647394 "WhiteoutDex_Manager`WhiteoutDex Toolkit"
        whiteoutDexMainMenu.CreateAction "B5C8D2E1-3F97-4A5B-8D16-9F4E6C7A2B03" 647394 "WhiteoutDex_Settings`WhiteoutDex Toolkit"
        whiteoutDexMainMenu.CreateSeparator "A7F83B12-6E41-4C9A-B2D5-8F1E3A7C9D04"
        whiteoutDexMainMenu.CreateAction "D3B7E5A1-2F84-4C6E-9A13-7E2D5B8F4C01" 647394 "WhiteoutDex_About`WhiteoutDex Toolkit"
    ),
    
    -- ============================================================================
    -- REGISTRATION
    -- ============================================================================
    
    -- Register the WhiteoutDex menu callback with 3ds Max
    public fn register =
    (
        callbacks.removeScripts id:#WhiteoutDex
        callbacks.addScript #cuiRegisterMenus "::WhiteoutDexMenu.menuCallback()" id:#WhiteoutDex
    )
)

-- ============================================================================
-- GLOBAL INSTANCE & AUTO-REGISTRATION
-- ============================================================================

global WhiteoutDexMenu = WhiteoutDexMenuStruct()
::WhiteoutDexMenu.register()
