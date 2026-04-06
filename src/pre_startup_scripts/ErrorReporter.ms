/*
    ErrorReporter.ms
    
    Refactored as Struct for better encapsulation and maintainability.
    All global variables are now instance properties.
    
    ÄNDERUNGEN:
    - Als Struct organisiert (konsistent mit anderen WhiteoutDex-Modulen)
    - Robustes Error-Handling gegen undefined-Werte
    - Backward-Compatibility durch globale Wrapper-Funktionen
    - Zentrale Logging-Funktionen: logInfo, logWarning, logError
    
    Usage:
        global ErrorReporter = WhiteoutDexErrorReporterStruct()
        ErrorReporter.InitiateErrorReporter()
        ErrorReporter.reportError "Material" "MyObject" "Missing texture"
        ErrorReporter.logWarning "Import" "BlizzPart002" "No texture path"
        ErrorReporter.logError "Import" "BlizzPart002" "Failed to create object"
        ErrorReporter.logInfo "Import" "BlizzPart002" "Created successfully"
        ErrorReporter.promptErrorReporter()
*/

-- ============================================================================
-- MAIN ERROR REPORTER STRUCT
-- ============================================================================

struct WhiteoutDexErrorReporterStruct
(
    -- ============================================================================
    -- INSTANCE PROPERTIES (formerly global variables)
    -- ============================================================================
    
    public bugsFound = 0,
    public errors = undefined,  -- Dictionary #string instance
    
    -- ============================================================================
    -- INITIALIZATION
    -- ============================================================================
    
    fn InitiateErrorReporter =
    (
        -- Reset
        this.bugsFound = 0
        this.errors = Dictionary #string
        
        -- Alle Kategorien initialisieren
        this.InitiateErrorCategory "TCB" " The next objects have conflicting rotation keys at the showed time\n// To fix it add a key between the mentioned key and the key before, or use the WhiteoutDex Animation Tools"
        this.InitiateErrorCategory "Material" " The next objects have non Warcraft 3 materials or\n// Composites that have non Warcraft 3 materials."
        this.InitiateErrorCategory "StartEndKeys" " The next objects don't have start and end keys on the mentioned animations."
        this.InitiateErrorCategory "No Unwrap" " The next objects aren't unwrapped or have vertices without unwrap,\n// no unwrap is exported into them."
        this.InitiateErrorCategory "Bad Unwrap" " The next objects have some vertices without unwrap.\n// WhiteoutDex will try to assign them an unwrap."
        this.InitiateErrorCategory "Weights" " The next objects have unbalanced weights,\n// they may be wrongly animated."
        this.InitiateErrorCategory "Bad Skinning" " The next objects have skin modifiers with some unskinned vertices,\n// The skin modifier will be ignored until fixed."
        this.InitiateErrorCategory "No Sequences" " No sequence data was found on the scene (Custom Attributes on rootNode).\n// This warning may also show if the data is not formatted correctly"
        this.InitiateErrorCategory "Import" " Errors during model import/rebuild"
        this.InitiateErrorCategory "SkinChanger" " Errors during skin transfer operations"
        this.InitiateErrorCategory "Sequence" " Errors in sequence management"
    ),
    
    -- ============================================================================
    -- ERROR CATEGORY MANAGEMENT
    -- ============================================================================
    
    fn InitiateErrorCategory name description =
    (
        -- Sicherheitsprüfung: errors muss existieren
        if this.errors == undefined then
        (
            this.errors = Dictionary #string
        )
        
        -- Sicherheitsprüfung: name und description müssen gültig sein
        if name == undefined then name = "Unknown"
        if description == undefined then description = "No description"
        
        this.errors[name] = #(description, Dictionary #string)
    ),
    
    -- ============================================================================
    -- CENTRALIZED LOGGING
    -- ============================================================================
    
    fn logInfo category source message =
    (
        format "ℹ WhiteoutDex [%] %: %\n" category source message
    ),
    
    fn logWarning category source message =
    (
        format "⚠ WhiteoutDex [%] %: %\n" category source message
        this.reportError category source message
    ),
    
    fn logError category source message =
    (
        format "❌ WhiteoutDex [%] %: %\n" category source message
        this.reportError category source message
    ),
    
    -- ============================================================================
    -- ERROR REPORTING
    -- ============================================================================
    
    fn reportError name object value =
    (
        -- Sicherheitsprüfung: errors muss existieren
        if this.errors == undefined then
        (
            format "WARNING: Cannot report error - ErrorReporter not initialized\n"
            format "  Error: % | Object: % | Value: %\n" name object value
            return OK
        )
        
        -- Sicherheitsprüfung: name muss gültig sein
        if name == undefined then
        (
            format "WARNING: Cannot report error - no error name provided\n"
            return OK
        )
        
        -- Kategorie holen oder automatisch erstellen
        if not (hasDictValue this.errors name) then
        (
            this.InitiateErrorCategory name ("Auto-created category for: " + (name as string))
        )
        
        local err = this.errors[name]
        
        -- Nochmalige Prüfung
        if err != undefined and err[2] != undefined then
        (
            local dictio = err[2]
            if object == undefined then object = "<unknown>"
            ::Utils.DictAppend dictio object value
            this.bugsFound += 1
        )
        else
        (
            format "ERROR: Could not log error '%' for object '%'\n" name object
        )
    ),
    
    -- ============================================================================
    -- ERROR CHECKING
    -- ============================================================================
    
    fn errorReporterHasErrors = 
    (
        this.bugsFound != undefined and this.bugsFound != 0
    ),
    
    -- ============================================================================
    -- ERROR OUTPUT
    -- ============================================================================
    
    fn printError name =
    (
        -- Lokale Hilfsfunktion: undefined-Werte aus Array entfernen
        fn removeUndefineds arr =
        (
            local result = #()
            if arr != undefined then 
            (
                local count = arr.count
                if count > 0 then result[count] = undefined
                local idx = 0
                for a in arr where a != undefined do
                (
                    idx += 1
                    result[idx] = a
                )
                result.count = idx
            )
            result
        )
        
        -- Lokale Hilfsfunktion: Array zu String konvertieren (mit StringStream)
        fn arrayToString array =
        (
            if array == undefined or array.count == 0 then return ""
            if array[1] == undefined then return ""
            
            local ss = stringStream ""
            format "%" (array[1] as string) to:ss
            for i = 2 to array.count do 
                format ", %" (array[i] as string) to:ss
            ss as string
        )
        
        -- Sicherheitsprüfung: errors muss existieren
        if this.errors == undefined then return OK
        
        -- Sicherheitsprüfung: name muss gültig sein
        if name == undefined then return OK
        
        -- Kategorie muss existieren
        if not (hasDictValue this.errors name) then return OK
        
        local err = this.errors[name]
        if err == undefined then return OK
        
        local description = err[1]
        if description == undefined then description = "No description available"
        
        local dictio = err[2]
        if dictio == undefined then return OK
        
        local data = ::Utils.DictToArray dictio
        
        -- Keine Fehler in dieser Kategorie = nichts ausgeben
        if data == undefined or data.count == 0 then return OK
        
        -- Header ausgeben
        format "//////////////////////////////////////////////////////////////////////////////////\n" to:listener
        format "// %\n" name to:listener
        format "//////////////////////////////////////////////////////////////////////////////////\n" to:listener
        format "//%\n" description to:listener
        format "//////////////////////////////////////////////////////////////////////////////////\n" to:listener
        
        -- Fehler ausgeben
        for i = 1 to data.count do
        (
            if data[i] != undefined then
            (
                local objName = if data[i][1] != undefined then (data[i][1] as string) else "<unknown>"
                local info = removeUndefineds data[i][2]
                
                if info == undefined or info.count == 0 then
                    format "// %\n" objName to:listener
                else
                    format "// %: (%)\n" objName (arrayToString info) to:listener
            )
        )
        format "//////////////////////////////////////////////////////////////////////////////////\n" to:listener
    ),
    
    fn promptErrorReporter =
    (
        clearListener()
        
        -- Sicherheitsprüfung
        if this.errors == undefined then
        (
            format "// WhiteoutDex Error Reporter: No errors recorded (system not initialized)\n" to:listener
            return OK
        )
        
        -- Alle bekannten Fehler-Kategorien ausgeben
        this.printError "No Sequences"
        this.printError "Material"
        this.printError "No Unwrap"
        this.printError "Bad Unwrap"
        this.printError "Bad Skinning"
        this.printError "Weights"
        this.printError "StartEndKeys"
        this.printError "TCB"
        this.printError "Import"
        this.printError "SkinChanger"
        this.printError "Sequence"
        
        -- Zusammenfassung
        if this.bugsFound != undefined and this.bugsFound > 0 then
            format "\n// TOTAL: % issue(s) found\n" this.bugsFound to:listener
        else
            format "\n// No issues found - model looks good!\n" to:listener
    )
)

-- ============================================================================
-- GLOBAL INSTANCE (for backward compatibility)
-- ============================================================================
global ErrorReporter = WhiteoutDexErrorReporterStruct()
