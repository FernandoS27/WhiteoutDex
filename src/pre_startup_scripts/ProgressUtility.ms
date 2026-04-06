/*
    ProgressUtility.ms
    
    Progress tracking for WhiteoutDex operations.
    Fully encapsulated as struct - no global wrapper functions.
    
    Usage:
        ::Progress.initiate labelControl barControl
        ::Progress.downLevel 50 10
        ::Progress.showItem "Processing..."
        ::Progress.upLevel()
        ::Progress.finalize()
*/

struct WhiteoutDexProgressStruct
(
    label = undefined,
    bar = undefined,
    levels = #(100.0),
    levelsPart = #(100.0),
    preValue = 0.0,
    currentLevel = 1,
    
    fn initiate lbl br =
    (
        this.preValue = 0.0
        this.label = lbl
        this.bar = br
        br.value = 0.0
        lbl.caption = "Initiated"
        this.levels = #(100.0)
        this.levelsPart = #(100.0)
        this.currentLevel = 1
    ),
    
    fn downLevel amount items =
    (
        local lastLevel = this.levels[this.currentLevel] as float
        local newLevel = (lastLevel * amount) / 100.0
        append this.levels newLevel
        append this.levelsPart (newLevel / (items as float))
        this.currentLevel += 1
    ),
    
    fn showItem text =
    (
        if this.label != undefined then
            this.label.caption = text
        this.preValue = this.preValue + this.levelsPart[this.currentLevel]
        if this.bar != undefined then
            this.bar.value = this.preValue
    ),
    
    fn updateItem text =
    (
        if this.label != undefined then
            this.label.caption = text
    ),
    
    fn upLevel =
    (
        deleteItem this.levels this.currentLevel
        deleteItem this.levelsPart this.currentLevel
        this.currentLevel -= 1
    ),
    
    fn finalize =
    (
        if this.label != undefined then
            this.label.caption = "Done"
        if this.bar != undefined then
            this.bar.value = 100
    )
)

global Progress = WhiteoutDexProgressStruct()
