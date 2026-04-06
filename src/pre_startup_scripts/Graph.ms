/*
    Graph.ms
    
    Refactored as Struct for better encapsulation and maintainability.
    Provides a simple adjacency-list graph with depth-first search.
    
    Usage:
        local g = GraphStruct.create 10
        g.addConnection 1 3
        g.addConnection 3 5 bothSided:true
        local components = g.depthFirstSearch()
*/

-- ============================================================================
-- MAIN GRAPH STRUCT
-- ============================================================================

struct GraphStruct
(
    -- ============================================================================
    -- INSTANCE PROPERTIES
    -- ============================================================================
    
    public connections = #(),
    
    -- ============================================================================
    -- FACTORY METHOD
    -- ============================================================================
    
    fn create numNodes =
    (
        local g = GraphStruct()
        g.connections[numNodes] = undefined
        for i = 1 to numNodes do
            g.connections[i] = #()
        g
    ),
    
    -- ============================================================================
    -- PRIVATE HELPER FUNCTIONS
    -- ============================================================================
    
    -- Get the first set bit from a bitarray
    fn getFirstBitArray bitarr =
    (
        local result = undefined
        for b in bitarr where result == undefined do result = b
        result
    ),
    
    -- Add a directed edge (avoid duplicates)
    fn relateNodes startNode endNode =
    (
        local index = findItem this.connections[startNode] endNode
        if index == 0 then
            append this.connections[startNode] endNode
    ),
    
    -- Recursive DFS traversal helper
    fn depthFirstSearchHelper startNode &visited &nodeGroup func:unsupplied =
    (
        if (not nodeGroup[startNode]) then
        (
            visited = visited + 1
            nodeGroup[startNode] = true
            if func != unsupplied then
                func startNode
            for c in this.connections[startNode] where c != undefined do
                this.depthFirstSearchHelper c &visited &nodeGroup func:func
        )
        OK
    ),
    
    -- ============================================================================
    -- PUBLIC FUNCTIONS
    -- ============================================================================
    
    -- Add a connection between two nodes
    -- bothSided: if true, adds edge in both directions (undirected)
    fn addConnection startNode endNode bothSided:false =
    (
        this.relateNodes startNode endNode
        if bothSided then
            this.relateNodes endNode startNode
        OK
    ),
    
    -- Perform depth-first search, returns array of bitarrays (connected components)
    -- func: optional callback function called for each visited node
    -- returnQuery: if false, returns OK instead of the component list
    fn depthFirstSearch func:unsupplied returnQuery:true =
    (
        local notSeen = #{}
        local numNodes = this.connections.count
        notSeen.count = numNodes
        notSeen = -notSeen
        local result = #()
        local visited = 0
        
        while (visited != numNodes) do
        (
            local nodeGroup = #{}
            nodeGroup.count = numNodes
            local startNode = this.getFirstBitArray notSeen
            this.depthFirstSearchHelper startNode &visited &nodeGroup func:func
            append result nodeGroup
            notSeen = notSeen - nodeGroup
        )
        
        if returnQuery then
            result
        else OK
    )
)

-- ============================================================================
-- BACKWARD COMPATIBILITY: Global factory function
-- ============================================================================

global Graph
fn Graph numNodes =
(
    GraphStruct.create numNodes
)
