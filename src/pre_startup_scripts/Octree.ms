/*
    Octree.ms
    
    Refactored as Structs for better encapsulation and maintainability.
    Provides spatial partitioning via Octree (3D) and Quadtree (2D).
    
    Usage:
        -- Create an Octree for spatial queries
        local tree = Octree myDataSet minPoint maxPoint maxLevel:8 maxSize:16
        tree.insertPoint 42
        local blocks = tree.getBlocks()
        
        -- Create a Quadtree for 2D spatial queries
        local tree2d = Quadtree myDataSet minPoint2d maxPoint2d maxLevel:6
        tree2d.insertPoint 7
*/

-- ============================================================================
-- OCTREE STRUCT (3D spatial partitioning)
-- ============================================================================

struct OctreeStruct
(
    -- ============================================================================
    -- INSTANCE PROPERTIES
    -- ============================================================================
    
    public data = #(),
    public dataSet,         -- Reference to the source data array
    public origin,          -- Center point of this node [x, y, z]
    public halfDimension,   -- Half-size of this node's bounding box
    public level,           -- Current depth level
    public maxLevel,        -- Maximum allowed depth
    public maxSize,         -- Maximum points per leaf before subdivision
    public children,        -- Array of 8 child nodes (undefined if leaf)
    
    -- ============================================================================
    -- FACTORY METHOD
    -- ============================================================================
    
    fn create level origin halfDimension dataSet maxLevel:0x7FFFFFF maxSize:0x7FFFFFF =
    (
        local o = OctreeStruct()
        o.level = level
        o.origin = origin
        o.halfDimension = halfDimension
        o.dataSet = dataSet
        o.maxLevel = maxLevel
        o.maxSize = maxSize
        o
    ),
    
    -- ============================================================================
    -- QUERY FUNCTIONS
    -- ============================================================================
    
    -- Check if this node is a leaf (no children)
    fn isLeafNode =
    (
        this.children == undefined
    ),
    
    -- Determine which octant a point belongs to (1-8)
    fn getOctant p =
    (
        local oct = 0
        if (p.x >= this.origin.x) then oct = bit.or oct 4
        if (p.y >= this.origin.y) then oct = bit.or oct 2
        if (p.z >= this.origin.z) then oct = bit.or oct 1
        (oct + 1)
    ),
    
    -- ============================================================================
    -- INSERTION
    -- ============================================================================
    
    -- Insert a point index into the tree
    fn insertPoint p =
    (
        if (this.isLeafNode()) then
        (
            if (this.data.count < this.maxSize or this.level >= this.maxLevel) then
                append this.data p
            else
            (
                -- Subdivide: create 8 children
                this.children = #()
                this.children[8] = undefined
                
                for i = 0 to 7 do
                (
                    local n = [0.5, 0.5, 0.5]
                    if (bit.get i 3) then n.x = -n.x
                    if (bit.get i 2) then n.y = -n.y
                    if (bit.get i 1) then n.z = -n.z
                    local newOrigin = this.origin - this.halfDimension * n
                    this.children[i+1] = OctreeStruct.create (this.level+1) newOrigin (this.halfDimension*0.5) this.dataSet \
                        maxLevel:this.maxLevel maxSize:this.maxSize
                )
                
                -- Redistribute existing data into children
                for d in this.data do
                    this.children[(this.getOctant this.dataSet[d])].insertPoint d
                this.data = undefined
                
                -- Insert the new point
                this.children[(this.getOctant this.dataSet[p])].insertPoint p
            )
        )
        else
        (
            -- Not a leaf: pass down to appropriate child
            this.children[(this.getOctant this.dataSet[p])].insertPoint p
        )
    ),
    
    -- ============================================================================
    -- RETRIEVAL
    -- ============================================================================
    
    -- Get all leaf data blocks as an array of arrays
    fn getBlocks =
    (
        if (this.isLeafNode()) then
            #(this.data)
        else
        (
            local result = #()
            for c in this.children do
            (
                local unitBlock = c.getBlocks()
                if (unitBlock[1].count != 0) then
                    join result unitBlock
            )
            result
        )
    ),
    
    -- ============================================================================
    -- DEBUG
    -- ============================================================================
    
    -- Print tree structure to listener for debugging
    fn printInfo =
    (
        format "Octree Level %\n" this.level to:listener
        format "  Origin %\n" this.origin to:listener
        format "  HalfDim %\n" this.halfDimension to:listener
        if this.isLeafNode() then
        (
            format "  Leaf Node\n" to:listener
            for d in this.data do
                format "  %\n" this.dataSet[d] to:listener
        )
        else
        (
            format "  Parent Node\n" to:listener
            for c in this.children do
                c.printInfo()
        )
    )
)

-- ============================================================================
-- QUADTREE STRUCT (2D spatial partitioning)
-- ============================================================================

struct QuadtreeStruct
(
    -- ============================================================================
    -- INSTANCE PROPERTIES
    -- ============================================================================
    
    public data = #(),
    public dataSet,         -- Reference to the source data array
    public origin,          -- Center point of this node [x, y]
    public halfDimension,   -- Half-size of this node's bounding box
    public level,           -- Current depth level
    public maxLevel,        -- Maximum allowed depth
    public maxSize,         -- Maximum points per leaf before subdivision
    public children,        -- Array of 4 child nodes (undefined if leaf)
    public conditionFunc,   -- Optional custom condition for leaf behavior
    
    -- ============================================================================
    -- FACTORY METHOD
    -- ============================================================================
    
    fn create level origin halfDimension dataSet maxLevel:0x7FFFFFF maxSize:0x7FFFFFF conditionFunc:undefined =
    (
        local o = QuadtreeStruct()
        o.level = level
        o.origin = origin
        o.halfDimension = halfDimension
        o.dataSet = dataSet
        o.maxLevel = maxLevel
        o.maxSize = maxSize
        o.conditionFunc = conditionFunc
        o
    ),
    
    -- ============================================================================
    -- QUERY FUNCTIONS
    -- ============================================================================
    
    -- Check if this node is a leaf (no children)
    fn isLeafNode =
    (
        this.children == undefined
    ),
    
    -- Determine which quadrant a point belongs to (1-4)
    fn getQuadrant p =
    (
        local oct = 0
        if (p.x >= this.origin.x) then oct = bit.or oct 2
        if (p.y >= this.origin.y) then oct = bit.or oct 1
        (oct + 1)
    ),
    
    -- ============================================================================
    -- INSERTION
    -- ============================================================================
    
    -- Insert a point index into the tree
    fn insertPoint p =
    (
        if (this.isLeafNode()) then
        (
            local condMet = if this.conditionFunc != undefined then (this.conditionFunc this) else false
            if condMet or (this.data.count < this.maxSize or this.level >= this.maxLevel) then
                append this.data p
            else
            (
                -- Subdivide: create 4 children
                this.children = #()
                this.children[4] = undefined
                
                for i = 0 to 3 do
                (
                    local n = [0.5, 0.5]
                    if (bit.get i 2) then n.x = -n.x
                    if (bit.get i 1) then n.y = -n.y
                    local newOrigin = this.origin - this.halfDimension * n
                    this.children[i+1] = QuadtreeStruct.create (this.level+1) newOrigin (this.halfDimension*0.5) this.dataSet \
                        maxLevel:this.maxLevel maxSize:this.maxSize conditionFunc:this.conditionFunc
                )
                
                -- Redistribute existing data into children
                for d in this.data do
                    this.children[(this.getQuadrant this.dataSet[d])].insertPoint d
                this.data = undefined
                
                -- Insert the new point
                this.children[(this.getQuadrant this.dataSet[p])].insertPoint p
            )
        )
        else
        (
            -- Not a leaf: pass down to appropriate child
            this.children[(this.getQuadrant this.dataSet[p])].insertPoint p
        )
    ),
    
    -- ============================================================================
    -- RETRIEVAL
    -- ============================================================================
    
    -- Get all leaf data blocks as an array of arrays
    fn getBlocks =
    (
        if (this.isLeafNode()) then
            #(this.data)
        else
        (
            local result = #()
            for c in this.children do
            (
                local unitBlock = c.getBlocks()
                if (unitBlock[1].count != 0) then
                    join result unitBlock
            )
            result
        )
    ),
    
    -- ============================================================================
    -- DEBUG
    -- ============================================================================
    
    -- Print tree structure to listener for debugging
    fn printInfo =
    (
        format "Quadtree Level %\n" this.level to:listener
        format "  Origin %\n" this.origin to:listener
        format "  HalfDim %\n" this.halfDimension to:listener
        if this.isLeafNode() then
        (
            format "  Leaf Node\n" to:listener
            for d in this.data do
                format "  %\n" this.dataSet[d] to:listener
        )
        else
        (
            format "  Parent Node\n" to:listener
            for c in this.children do
                c.printInfo()
        )
    )
)

-- ============================================================================
-- BACKWARD COMPATIBILITY: Global factory functions
-- ============================================================================

global Octree
fn Octree dataSet minimum maximum maxLevel:0x7FFFFFF maxSize:0x7FFFFFF =
(
    local center = (maximum - minimum) * 0.5
    OctreeStruct.create 0 center (maximum - center) dataSet maxLevel:maxLevel maxSize:maxSize
)

global Quadtree
fn Quadtree dataSet minimum maximum maxLevel:0x7FFFFFF maxSize:0x7FFFFFF =
(
    local center = (maximum - minimum) * 0.5
    QuadtreeStruct.create 0 center (maximum - center) dataSet maxLevel:maxLevel maxSize:maxSize
)
