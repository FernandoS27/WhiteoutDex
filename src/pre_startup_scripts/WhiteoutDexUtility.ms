/*
    UtilityFunctions.ms
    
    Refactored as Struct for better encapsulation and maintainability.
    Contains utility functions for conversions, array operations, type checking, etc.
    No state variables - pure functions grouped in a struct.
    
    Usage:
        global WdxUtils = WhiteoutDexUtilityStruct()
        WdxUtils.F2I frameValue
        WdxUtils.ArrayMap myArray myFunc
*/

-- ============================================================================
-- HELPER STRUCTS (remain outside main struct)
-- ============================================================================

struct WdxHierarchy
(
    object,
    children = #(),
    parent
)

struct WdxDoubleV
(
    val1,
    val2
) 

-- NOTE: DictionaryOld removed — use native Dictionary #string (3ds Max 2021+)
-- See ErrorReporter.ms for migration example.

-- ============================================================================
-- MAIN UTILITY FUNCTIONS STRUCT
-- ============================================================================

struct WhiteoutDexUtilityStruct
(
    -- ============================================================================
    -- FRAME/TIME CONVERSION FUNCTIONS
    -- ============================================================================
    
    fn F2I f = (f as integer) / TicksPerFrame,
    fn F2R f = (f as float) / (TicksPerFrame as float),
    fn F2WS f = ((((((f as float)/TicksPerFrame)*1000.0/framerate) + 0.5) as integer) as string),
    fn F2WI f = (((((f as float)/TicksPerFrame)*1000.0/framerate) + 0.5) as integer),
    
    -- ============================================================================
    -- BOOLEAN/INTEGER CONVERSION
    -- ============================================================================
    
    fn B2I b = if b then 1 else 0,
    fn I2B i = if i == 0 then false else true,
    
    -- ============================================================================
    -- MATH UTILITY FUNCTIONS
    -- ============================================================================
    
    fn round a = (ceil ((floor (a*2))/2) as integer),
    fn relativeFactor obj = (obj.max - obj.min),
    fn minVal a b = if a > b then b else a,
    fn maxVal a b = if a > b then a else b,
    fn minPoint p1 p2 = [this.minVal p1.x p2.x, this.minVal p1.y p2.y, this.minVal p1.z p2.z],
    fn maxPoint p1 p2 = [this.maxVal p1.x p2.x, this.maxVal p1.y p2.y, this.maxVal p1.z p2.z],
    
    -- ============================================================================
    -- QUATERNION FUNCTIONS
    -- ============================================================================
    fn DotQuat q1 q2 = (q1.w*q2.w + q1.x*q2.x + q1.y*q2.y + q1.z*q2.z),
    fn CheckLock q1 q2 = 
    (
        local a = (acos (this.DotQuat q1 q2))
        if a > 45 and a < 135 then true else false
    ),
    fn SumQuat q1 q2 = 
    (
        (quat (q1.x+q2.x) (q1.y+q2.y) (q1.z+q2.z) (q1.w+q2.w))
    ),
    
    fn RotatePoint p q =
    (
        local q2 = quat p.x p.y p.z 0
        q2 = q*q2*(inverse q)
        [q2.x,q2.y,q2.z]
    ),
    
    -- ============================================================================
    -- ARRAY FUNCTIONS
    -- ============================================================================
    
    fn ArrayCompare a b =
    (
        if a == b then return 0            
        if classOf a != Array and classOf b != Array then if a < b then return -1 else if a > b then return 1
        for i = 1 to a.count do
        (
            if b.count < i then return 1
            local c = this.ArrayCompare a[i] b[i]
            if c < 0 then return -1
            else if c > 0 then return 1
        )
        if a.count < b.count then return -1
        return 0
    ),
    
    fn ArrayFindItem array value =
    (
        for i = 1 to array.count do
        (
            if this.ArrayCompare array[i] value == 0 then return i
        )
        return 0
    ),
    
    fn ArrayFindItemEx array value &func =
    (
        for i = 1 to array.count do
        (
            if func array[i] value then return i
        )
        return 0
    ),
    
    fn ArrayMakeUnique array =
    (
        local count = array.count
        if count == 0 then return #()
        
        local result = #()
        result[count] = undefined  -- Pre-allocate für worst case
        local resultIdx = 0
        
        for elem in array do
        (
            -- Nur im bereits gefüllten Bereich suchen
            local found = false
            for i = 1 to resultIdx while not found do
                if result[i] == elem then found = true
            
            if not found then 
            (
                resultIdx += 1
                result[resultIdx] = elem
            )
        )
        
        -- Array auf tatsächliche Größe trimmen
        result.count = resultIdx
        return result
    ),
    fn OptimizeArray A =
    (
        local count = A.count
        if count <= 1 then return A
        
        local writeIdx = 0
        for i = 1 to count do
        (
            local isDuplicate = false
            for j = 1 to writeIdx while not isDuplicate do
                if A[j] == A[i] then isDuplicate = true
            
            if not isDuplicate then
            (
                writeIdx += 1
                A[writeIdx] = A[i]
            )
        )
        A.count = writeIdx
    ),
    
    fn ArrayFold array &func init =
    (
        for elem in array do
        (
            init = func init elem
        )
        return init
    ),
    
    fn ArrayFilter array &func =
    (
        local count = array.count
        if count == 0 then return #()
        
        local result = #()
        result[count] = undefined  -- Pre-allocate für worst case
        local resultIdx = 0
        
        for elem in array do
        (
            if (func elem) then 
            (
                resultIdx += 1
                result[resultIdx] = elem
            )
        )
        
        -- Array auf tatsächliche Größe trimmen
        result.count = resultIdx
        return result
    ),
    
    fn ArrayCheckProperty array &func =
    (
        for elem in array do
        (
            if not (func elem) then return false
        )    
        return true
    ),
    
    fn ArrayMap array &func =
    (
        local result = #()
        if array.count != 0 then result[array.count] = undefined
        local i = 1
        for elem in array do
        (
            result[i] = func elem
            i += 1
        )
        return result
    ),
    
    fn ArrayRepeat size value =
    (
        local result = #()
        if size != 0 then result[size] = value
        for i = 1 to size do
        (
            result[i] = value
        )
        return result
    ),
    fn ArrayInverse arr =
    (
        local result = #()
        for i = 1 to arr.count do
        (
            local index = arr[i]
            if result[index] == undefined then result[index] = #()
            append result[index] i
        )
        return result
    ),
    
    -- ============================================================================
    -- MAP FUNCTIONS
    -- ============================================================================
    fn HierarchyFindElement h elem =
    (
        for c in h.children do 
        (
            if c.object == elem then return c
            local result = this.HierarchyFindElement c elem
            if result != undefined then return result
        )
        return undefined
    ),
        
    fn HierarchyAddNode h obj =
    (
        local backup = this.HierarchyFindElement h obj
        
        if backup == undefined then
        (
            local parent
            
            if obj.parent != undefined then
            (
                parent = this.HierarchyAddNode h obj.parent
            )
            local result
            
            if parent == undefined then
            (
                result = WdxHierarchy obj #() undefined
                append h.children result
            )
            else
            (
                result = WdxHierarchy obj #() obj.parent
                append parent.children result
            )
            
            return result
        )
        else
        (
            return backup
        )
    ),
        
    fn _HierarchyFilterImpl h &func &result =
    (
        if h.object != undefined and func h.object then append result h.object
        for c in h.children do 
            this._HierarchyFilterImpl c func result
    ),
    
    fn HierarchyFilter h &func =
    (
        local result = #()
        this._HierarchyFilterImpl h func result
        result
    ),
    
    -- ============================================================================
    -- DICTIONARY FUNCTIONS (native Dictionary #string, 3ds Max 2021+)
    -- ============================================================================
    
    -- Add or append data to a Dictionary entry
    -- If key exists: appends data to existing array
    -- If key doesn't exist: creates new entry with #(data)
    fn DictAppend dict key data =
    (
        if hasDictValue dict key then
            append dict[key] data
        else
            dict[key] = #(data)
    ),
    
    -- Get all entries as #( #(key, valueArray), ... ) for iteration
    fn DictToArray dict =
    (
        local keys = getDictKeys dict
        local count = keys.count
        if count == 0 then return #()
        
        local result = #()
        result[count] = undefined
        
        for i = 1 to count do
            result[i] = #(keys[i], dict[keys[i]])
        
        result
    ),
    
    -- ============================================================================
    -- TYPE CHECKING FUNCTIONS
    -- ============================================================================
    
    fn IsWc3Material m = (classOf m == Warcraft3),
    
    fn IsMultiLayered m =
    (
        fn tmp m = m == undefined or this.IsWc3Material m
        if (classOf m == compositematerial) then
            this.ArrayCheckProperty m.materialList tmp
        else
            false
    ),
    
    fn IsSkinned o = (o.modifiers[#Skin] != undefined),
    fn IsMesh o = (classOf o == Editable_mesh),
    fn IsCamera o = (classOf o == Targetcamera),
    fn IsCollisionShape o = ((classof o) as string == "Wc3CollisionBox") or ((classof o) as string == "Wc3CollisionSphere"),
    fn IsAttachment o = (classOf o == Wc3_AttachPoint),
    fn IsEventObject o = ((substring o.name 1 4) == "Obj:"),
    fn IsParticleEmitter o = (classOf o == Wc3Particles1),
    fn IsParticleEmitter2 o = (classOf o == Wc3Particles2),
    fn IsRibbonEmitter o = (classOf o == BlizRibbon),
    fn IsPopcornEmitter o = (try (classOf o == BlizzPopcorn) catch false),
    fn IsFaceFX o = (try (classOf o == BlizzFaceFX) catch false),
    
    fn IsLight o =
    (
        classOf o == OmniLight \
        or classOf o == freeSpot \
        or classOf o == Directionallight \
        or classOf o == Skylight \
        or classOf o == Wc3_Light
    ),
    
    -- ============================================================================
    -- USER PROPERTY FUNCTIONS
    -- ============================================================================
    
    fn getWc3Skin o =
    (
        fn InterfaceToSkin s = if s == 2 then #ATSkin else #NormalSkin
        local s = getUserProp o "Wc3Skin"
        if s == undefined then #NormalSkin else InterfaceToSkin (s as integer)
    ),
    
    fn getBillboardFlags o =
    (
        -- Alte Property migrieren falls vorhanden
        local oldBill = getUserProp o "Billboard"
        if oldBill != undefined then
        (
            local val = oldBill as integer
            setUserProp o "Billboarded"       (if val == 2 then 1 else 0)
            setUserProp o "BillboardedLockX"  (if val == 3 then 1 else 0)
            setUserProp o "BillboardedLockY"  (if val == 4 then 1 else 0)
            setUserProp o "BillboardedLockZ"  (if val == 5 then 1 else 0)
            deleteUserProp o "Billboard"
        )
        local bb   = getUserProp o "Billboarded"
        local bbX  = getUserProp o "BillboardedLockX"
        local bbY  = getUserProp o "BillboardedLockY"
        local bbZ  = getUserProp o "BillboardedLockZ"
        #(
            if bb  == undefined then false else this.I2B (bb as integer),
            if bbX == undefined then false else this.I2B (bbX as integer),
            if bbY == undefined then false else this.I2B (bbY as integer),
            if bbZ == undefined then false else this.I2B (bbZ as integer)
        )
    ),
    
    fn setBillboardFlags o flags =
    (
        setUserProp o "Billboarded"       (this.B2I flags[1])
        setUserProp o "BillboardedLockX"  (this.B2I flags[2])
        setUserProp o "BillboardedLockY"  (this.B2I flags[3])
        setUserProp o "BillboardedLockZ"  (this.B2I flags[4])
    ),
    
    fn getCameraAnchored o =
    (
        local b = getUserProp o "CameraAnchored"
        if b == undefined then false else this.I2B (b as integer)
    ),
    fn getUnselectable o =
    (
        local b = getUserProp o "Unselectable"
        if b == undefined then false else this.I2B (b as integer)
    ),
    fn IsPointInBounds min1 max1 p =
    (
        p.x > min1.x and p.x < max1.x and \
        p.y > min1.y and p.y < max1.y and \
        p.z > min1.z and p.z < max1.z
    ),
    
    fn boundsCollide min1 max1 min2 max2 =
    (
        max1.x > min2.x and \
        min1.x < max2.x and \
        max1.y > min2.y and \
        min1.y < max2.y and \
        max1.z > min2.z and \
        min1.z < max2.z
    ),
    fn collideMatrix objs threshold =
    (
        local count = objs.count
        local result = this.ArrayRepeat count #()
        for i = 1 to count do result[i] = this.ArrayRepeat count false
        local tmp = threshold*0.5
        local func = this.boundsCollide 
        for i = 1 to count do
        (
            local min1 = objs[i].min - tmp
            local max1 = objs[i].max + tmp
            for j = i to count do
            (
                local r = (func min1 max1 (objs[j].min - tmp) (objs[j].max + tmp))
                result[i][j] = r
                result[j][i] = r
            )
        )
        result
    ),
    
    -- ============================================================================
    -- ANIMATION KEY FUNCTIONS
    -- ============================================================================
    
    fn getAnimationIntervals =
    (
        local result = #()
        -- Read from Custom Attributes on rootNode
        if ::WdxSequenceStorage != undefined then ::WdxSequenceStorage.autoMigrate()
        
        if (try (rootNode.seqNames.count > 0) catch false) then
        (
            for i = 1 to rootNode.seqNames.count do
                append result (interval (rootNode.startFrames[i] as time) (rootNode.endFrames[i] as time))
        )
        result
    ),

    fn getKeysOnSequences keys sequences inclusive:false =
    (
        if sequences.count == 0 then 
        (
            local defaultSequence = interval 10f 60f
            local result = #(#())
            for k in keys where (k.time >= 10f and k.time <= 60f) do append result[1] k
            return result
        )
        
        local func1, func2
        if inclusive then
        (
            func1 = fn tmp1 a b = a <= b 
            func2 = fn tmp2 a b = a >= b 
        )
        else
        (
            func1 = fn tmp1 a b = a < b 
            func2 = fn tmp2 a b = a > b 
        )
        
        result = #()
        result[sequences.count] = undefined
        for j = 1 to sequences.count do result[j] = #()
        
        local i = 1
        local count = keys.count
        for j = 1 to sequences.count do
        (
            local s = sequences[j]
            -- FIX: Cursor zuruecksetzen wenn aktuelle Sequenz denselben oder
            -- frueheren Start hat als die vorherige (Alias-Sequenzen / Shared Ranges)
            if j > 1 and i > 1 and s.start <= sequences[j - 1].start then
            (
                while i > 1 and (func2 keys[i - 1].time s.start) do i -= 1
            )
            while i <= count and func1 keys[i].time s.end do
            (
                if func2 keys[i].time s.start then
                    append result[j] keys[i]
                i += 1
            )
        )
        
        -- FIX: Gestohlene Boundary-Keys bei back-to-back Sequenzen zurueck verschieben
        if inclusive then
        (
            for j = 1 to (sequences.count - 1) do
            (
                if sequences[j].end == sequences[j + 1].start and result[j].count > 0 then
                (
                    local lastKey = result[j][result[j].count]
                    if lastKey.time == sequences[j].end then
                    (
                        deleteItem result[j] result[j].count
                        insertItem lastKey result[j + 1] 1
                    )
                )
            )
        )
        
        result
    ),
    
    fn getKeysOnSequences2 keys sequences inclusive:false =
    (
        local func1, func2
        if inclusive then
        (
            func1 = fn tmp1 a b = a <= b
            func2 = fn tmp2 a b = a >= b
        ) else (
            func1 = fn tmp1 a b = a < b
            func2 = fn tmp2 a b = a > b
        )
        local result = #()
        result[sequences.count] = undefined
        for j = 1 to sequences.count do result[j] = #()
        local i = 1
        local count = keys.count
        
        local useTime = count > 0 and isProperty keys[1] #time
        
        for j = 1 to sequences.count do
        (
            local s = sequences[j]
            -- FIX: Cursor zuruecksetzen fuer Alias-Sequenzen (Shared Ranges)
            if j > 1 and i > 1 and s.start <= sequences[j - 1].start then
            (
                if useTime then
                (
                    while i > 1 and (func2 keys[i - 1].time s.start) do i -= 1
                )
                else
                (
                    while i > 1 and (func2 keys[i - 1] s.start) do i -= 1
                )
            )
            if useTime then
            (
                while (i <= count and (func1 keys[i].time s.end)) do
                (
                    if (func2 keys[i].time s.start) then
                        append result[j] keys[i]
                    i += 1
                )
            )
            else
            (
                while (i <= count and (func1 keys[i] s.end)) do
                (
                    if (func2 keys[i] s.start) then
                        append result[j] keys[i]
                    i += 1
                )
            )
        )
        result
    ),
    
    fn _getParentsEx obj &tmp =
    (
        if obj.parent != undefined then
        (
            append tmp obj.parent
            this._getParentsEx obj.parent tmp
        )
    ),
    
    fn GetParents obj =
    (
        local result = #()
        this._getParentsEx obj result
        result
    ),
    
    -- ============================================================================
    -- CONTROLLER COMPARISON
    -- ============================================================================
    
    fn compareControllers c1 c2 =
    (
        if classOf c1 == classOf c2 then
        (
            local k1 = c1.keys
            local k2 = c2.keys
            local n = k1.count
            if n == k2.count then
            (
                local result = true
                for i = 1 to n where result do
                (
                    local v1,v2
                    at time k1[i].time v1 = c1.value
                    at time k2[i].time v2 = c2.value
                    result = result and ((k1[i].time == k2[i].time) and (v1 == v2))
                )
                result
            )
            else false
        )
        else false
    )
)

global WdxUtils = WhiteoutDexUtilityStruct()

-- ============================================================================
-- CALLBACK DELEGATES (required for ArrayFilter/ArrayMap function-reference pattern)
-- MaxScript cannot pass struct methods as &func references.
-- ============================================================================
fn WdxIsWc3Material obj = WdxUtils.IsWc3Material obj
fn WdxIsMultiLayered obj = WdxUtils.IsMultiLayered obj
fn WdxIsMesh obj = WdxUtils.IsMesh obj
fn WdxIsSkinned obj = WdxUtils.IsSkinned obj
fn WdxIsLight obj = WdxUtils.IsLight obj
fn WdxIsAttachment obj = WdxUtils.IsAttachment obj
fn WdxIsEventObject obj = WdxUtils.IsEventObject obj
fn WdxIsParticleEmitter obj = WdxUtils.IsParticleEmitter obj
fn WdxIsParticleEmitter2 obj = WdxUtils.IsParticleEmitter2 obj
fn WdxIsRibbonEmitter obj = WdxUtils.IsRibbonEmitter obj
fn WdxIsCollisionShape obj = WdxUtils.IsCollisionShape obj
fn WdxIsPopcornEmitter obj = WdxUtils.IsPopcornEmitter obj
fn WdxIsFaceFX obj = WdxUtils.IsFaceFX obj
fn WdxIsCamera obj = WdxUtils.IsCamera obj

-- ============================================================================
-- WINDOW ICON
-- Every WhiteoutDex tool window wears the toolkit icon. MaxScript rollouts have
-- no icon parameter, so the icon is pushed onto the hosting window with
-- WM_SETICON instead. Call WdxSetDialogIcon from a rollout's `open` handler
-- (the dialog exists by then) or WdxSetFormIcon for a .NET form.
--
-- The System.Drawing.Icon object is parked in a global on purpose: its
-- finalizer calls DestroyIcon, and the windows below are still drawing from
-- that handle.
-- ============================================================================

global WdxWindowIcon = undefined        -- System.Drawing.Icon, kept alive
global WdxWindowIconPath = undefined
global WdxWindowIconHandle = 0          -- HICON, as an int sendMessage can carry
global WdxWindowIconResolved = false

fn WdxFindWindowIconFile =
(
    local dirs = #()
    try (
        if ::WhiteoutDexInstallRoot != undefined and ::WhiteoutDexInstallRoot != "" then
            append dirs (::WhiteoutDexInstallRoot + "\\whiteoutdex_icons\\")
    ) catch ()
    try (
        append dirs ((systemTools.getEnvVariable "APPDATA") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex\\whiteoutdex_icons\\")
        append dirs ((systemTools.getEnvVariable "ALLUSERSPROFILE") + "\\Autodesk\\ApplicationPlugins\\WhiteoutDex\\whiteoutdex_icons\\")
    ) catch ()
    append dirs ((getDir #userScripts) + "\\whiteoutdex_icons\\")
    append dirs ((getDir #scripts) + "\\whiteoutdex_icons\\")

    for d in dirs do
    (
        local p = d + "whiteoutdex.ico"
        if doesFileExist p then return p
    )
    undefined
)

-- Loads the icon once. Returns true when a usable HICON is available.
fn WdxEnsureWindowIcon =
(
    if not ::WdxWindowIconResolved then
    (
        ::WdxWindowIconResolved = true
        try
        (
            local p = WdxFindWindowIconFile()
            if p != undefined then
            (
                ::WdxWindowIconPath = p
                ::WdxWindowIcon = dotNetObject "System.Drawing.Icon" p
                -- USER handles are guaranteed to fit in 32 bits, which is what
                -- windows.sendMessage can pass through. IntPtr normally comes
                -- back as a dotNetObject; older builds hand over a plain int.
                local h = ::WdxWindowIcon.Handle
                try ( ::WdxWindowIconHandle = h.ToInt32() )
                catch ( try ( ::WdxWindowIconHandle = h as integer ) catch ( ::WdxWindowIconHandle = 0 ) )
            )
            else
                format "WhiteoutDex: whiteoutdex.ico not found; dialogs keep the default icon\n"
        )
        catch
        (
            ::WdxWindowIcon = undefined
            ::WdxWindowIconHandle = 0
        )
    )
    ::WdxWindowIconHandle != 0
)

-- Applies the icon to a raw window handle.
fn WdxSetWindowIcon hwnd =
(
    if hwnd == undefined or hwnd == 0 then return false
    if not (WdxEnsureWindowIcon()) then return false
    try
    (
        -- WM_SETICON = 0x0080. wParam 0 = small (title bar), 1 = big (Alt-Tab).
        windows.sendMessage hwnd 0x0080 0 ::WdxWindowIconHandle
        windows.sendMessage hwnd 0x0080 1 ::WdxWindowIconHandle
        true
    )
    catch ( false )
)

-- Applies the icon to the dialog or floater hosting a rollout. `ro.hwnd[1]` is
-- the rollout panel, not the window with the title bar, so walk up from there.
--
-- Every ancestor short of Max's main window gets the message rather than just
-- the top one: a child window ignores WM_SETICON, and the chain is not always
-- one hop. `createDialog ro parent:<other rollout>.hwnd` makes the other
-- rollout the owner, so the walk passes through a second WhiteoutDex dialog
-- that wants the icon too.
fn WdxSetDialogIcon ro =
(
    local h = undefined
    try ( h = ro.hwnd[1] ) catch ( h = undefined )
    if h == undefined or h == 0 then return false

    local maxHwnd = 0
    try ( maxHwnd = windows.getMAXHWND() ) catch ()

    -- Four hops covers the deepest real chain: rollout panel, its dialog, the
    -- owner rollout's panel, the owner's dialog. Stopping there keeps the walk
    -- from wandering into Max's own window tree.
    local applied = false
    local cur = h
    local guard = 0
    while guard < 4 do
    (
        local p = 0
        try ( p = UIAccessor.GetParentWindow cur ) catch ( p = 0 )
        if p == undefined or p == 0 or p == maxHwnd then exit
        if WdxSetWindowIcon p then applied = true
        cur = p
        guard += 1
    )
    applied
)

-- Same, for a dotNet System.Windows.Forms.Form.
fn WdxSetFormIcon form =
(
    if form == undefined then return false
    if not (WdxEnsureWindowIcon()) then return false
    try ( form.Icon = ::WdxWindowIcon; true ) catch ( false )
)
