/*
    WhiteoutDexIO.ms
    
    Refactored as Struct for better encapsulation and maintainability.
    Stream state variables are now instance properties.
    Tag constants remain global as they are immutable format identifiers.
    
    Usage:
        global IOManager = WhiteoutDexIOStruct()
        IOManager.SetOutStream fileStream
        IOManager.writeMDLSection "Geoset" 1
*/

-- ============================================================================
-- MDX/MDL FORMAT TAG CONSTANTS (remain global - immutable identifiers)
-- ============================================================================

-- File header / version
global tag_MDLX = 0x584c444d  -- "MDLX" - File identifier
global tag_VERS = 0x53524556  -- "VERS" - Format version
global tag_MODL = 0x4c444f4d  -- "MODL" - Model info (name, extents)

-- Sequences & Global Sequences
global tag_SEQS = 0x53514553  -- "SEQS" - Animation sequences
global tag_GLBS = 0x53424c47  -- "GLBS" - Global sequences

-- Textures & Materials
global tag_TEXS = 0x53584554  -- "TEXS" - Texture definitions
global tag_LAYS = 0x5359414c  -- "LAYS" - Material layers
global tag_KMTF = 0x46544d4b  -- "KMTF" - Material texture id anim
global tag_KMTA = 0x41544d4b  -- "KMTA" - Material alpha anim
global tag_KMTE = 0x45544d4b  -- "KMTE" - Material emissive gain anim (Reforged)
global tag_MTLS = 0x534c544d  -- "MTLS" - Materials
global tag_SNDS = 0x53444e53  -- "SNDS" - Sound definitions

-- Texture Animations
global tag_TXAN = 0x4e415854  -- "TXAN" - Texture animations
global tag_KTAT = 0x5441544b  -- "KTAT" - TVertex anim translation
global tag_KTAR = 0x5241544b  -- "KTAR" - TVertex anim rotation
global tag_KTAS = 0x5341544b  -- "KTAS" - TVertex anim scale

-- Geosets
global tag_GEOS = 0x534f4547  -- "GEOS" - Geoset data
global tag_VRTX = 0x58545256  -- "VRTX" - Vertices
global tag_NRMS = 0x534d524e  -- "NRMS" - Normals
global tag_PTYP = 0x50595450  -- "PTYP" - Primitive type
global tag_PCNT = 0x544e4350  -- "PCNT" - Primitive count
global tag_PVTX = 0x58545650  -- "PVTX" - Primitive vertices (faces)
global tag_GNDX = 0x58444e47  -- "GNDX" - Group indices (matrix indices)
global tag_MTGC = 0x4347544d  -- "MTGC" - Matrix group counts
global tag_MATS = 0x5354414d  -- "MATS" - Matrix groups (bone refs)
global tag_UVAS = 0x53415655  -- "UVAS" - UV set count
global tag_UVBS = 0x53425655  -- "UVBS" - UV coordinates

-- Geoset Animations
global tag_GEOA = 0x414f4547  -- "GEOA" - Geoset animations
global tag_KGAO = 0x4f41474b  -- "KGAO" - Geoset anim alpha
global tag_KGAC = 0x4341474b  -- "KGAC" - Geoset anim color

-- Object transform tracks
global tag_KGTR = 0x5254474b  -- "KGTR" - Translation anim
global tag_KGRT = 0x5452474b  -- "KGRT" - Rotation anim
global tag_KGSC = 0x4353474b  -- "KGSC" - Scale anim

-- Bones & Helpers
global tag_BONE = 0x454e4f42  -- "BONE" - Bone definitions
global tag_HELP = 0x504c4548  -- "HELP" - Helper definitions

-- Lights
global tag_LITE = 0x4554494c  -- "LITE" - Light definitions
global tag_KLAS = 0x53414c4b  -- "KLAS" - Light attenuation start
global tag_KLAE = 0x45414c4b  -- "KLAE" - Light attenuation end
global tag_KLAC = 0x43414c4b  -- "KLAC" - Light color
global tag_KLAI = 0x49414c4b  -- "KLAI" - Light intensity
global tag_KLBI = 0x49424c4b  -- "KLBI" - Light ambient intensity
global tag_KLBC = 0x43424c4b  -- "KLBC" - Light ambient color
global tag_KLAV = 0x56414c4b  -- "KLAV" - Light visibility

-- Attachments
global tag_ATCH = 0x48435441  -- "ATCH" - Attachment definitions
global tag_KATV = 0x5654414b  -- "KATV" - Attachment visibility

-- Pivots
global tag_PIVT = 0x54564950  -- "PIVT" - Pivot points

-- Particle Emitter 1
global tag_PREM = 0x4d455250  -- "PREM" - Particle emitter 1
global tag_KPEE = 0x4545504b  -- "KPEE" - PE1 emission rate
global tag_KPEG = 0x4745504b  -- "KPEG" - PE1 gravity
global tag_KPLN = 0x4e4c504b  -- "KPLN" - PE1 longitude
global tag_KPEL = 0x4c45504b  -- "KPEL" - PE1 latitude
global tag_KPES = 0x5345504b  -- "KPES" - PE1 speed
global tag_KPEV = 0x5645504b  -- "KPEV" - PE1 visibility

-- Particle Emitter 2
global tag_PRE2 = 0x32455250  -- "PRE2" - Particle emitter 2
global tag_KP2S = 0x5332504b  -- "KP2S" - PE2 speed
global tag_KP2R = 0x5232504b  -- "KP2R" - PE2 variation
global tag_KP2L = 0x4c32504b  -- "KP2L" - PE2 latitude
global tag_KP2G = 0x4732504b  -- "KP2G" - PE2 gravity
global tag_KP2E = 0x4532504b  -- "KP2E" - PE2 emission rate
global tag_KP2N = 0x4e32504b  -- "KP2N" - PE2 length
global tag_KP2W = 0x5732504b  -- "KP2W" - PE2 width
global tag_KP2V = 0x5632504b  -- "KP2V" - PE2 visibility
global tag_KPPA = 0x4150504B  -- "KPPA" - Popcorn alpha Reforged
global tag_KPPV = 0x5650504B  -- "KPPV" - Popcorn visibility Reforged
global tag_KPPE = 0x4550504B  -- "KPPE" - Popcorn emission rate anim (Reforged)

-- Sound Emitter (unused in most models)
global tag_SNEM = 0x4d454e53  -- "SNEM" - Sound emitter (Corn)
global tag_KESK = 0x4b53454b  -- "KESK" - Sound emitter keys (Corn)

-- Ribbon Emitter
global tag_RIBB = 0x42424952  -- "RIBB" - Ribbon emitter
global tag_KRHA = 0x4148524b  -- "KRHA" - Ribbon height above
global tag_KRHB = 0x4248524b  -- "KRHB" - Ribbon height below
global tag_KRAL = 0x4c41524b  -- "KRAL" - Ribbon alpha
global tag_KRCO = 0x4f43524b  -- "KRCO" - Ribbon color
global tag_KRTX = 0x5854524b  -- "KRTX" - Ribbon texture slot
global tag_KRVS = 0x5356524b  -- "KRVS" - Ribbon visibility

-- Events
global tag_EVTS = 0x53545645  -- "EVTS" - Event objects
global tag_KEVT = 0x5456454b  -- "KEVT" - Event track

-- Cameras
global tag_CAMS = 0x534d4143  -- "CAMS" - Camera definitions
global tag_KCTR = 0x5254434b  -- "KCTR" - Camera translation
global tag_KTTR = 0x5254544b  -- "KTTR" - Camera target translation
global tag_KCRL = 0x4c52434b  -- "KCRL" - Camera rotation

-- Collision Shapes
global tag_CLID = 0x44494c43  -- "CLID" - Collision shapes

-- ============================================================================
-- MAIN IO STRUCT
-- ============================================================================

struct WhiteoutDexIOStruct
(
    -- ============================================================================
    -- INSTANCE PROPERTIES (formerly global variables)
    -- ============================================================================
    
    public outstream = undefined,
    public instream = undefined,
    public IOLevel = 1,
    public Tabs = #("", "\t", "\t\t", "\t\t\t", "\t\t\t\t", "\t\t\t\t\t", "\t\t\t\t\t\t", "\t\t\t\t\t\t\t"),
    public chunkSize = 0,
    
    -- ============================================================================
    -- STREAM MANAGEMENT
    -- ============================================================================
    
    fn SetOutStream os = this.outstream = os,
    fn SetInStream os = this.instream = os,
    
    -- ============================================================================
    -- UTILITY FUNCTIONS
    -- ============================================================================
    
    fn isSmoothAnim type = (type == #Bezier or type == #Hermite),
    
    fn checkExtents ex = ex.minExtent != ex.maxExtent,
    
    fn MaxMaterialAlphaToWc3Alpha &alphaKeys =
    (
        if (alphaKeys.type == #Static) then \
            alphaKeys.data = alphaKeys.data/100.0
        else
        (
            local data = alphaKeys.data
            local IsSmooth = this.isSmoothAnim data.type
            for k in data.keys do
            (
                k.value = k.value/100.0
                if IsSmooth then
                (
                    k.intan = k.intan /100.0
                    k.outtan = k.outtan /100.0
                )
            )
        )
        alphaKeys
    ),
    
    fn writerTransformKeys &Keys &func =
    (
        if (Keys.type == #Static) then \
            Keys.data = func Keys.data
        else
        (
            local data = Keys.data
            local IsSmooth = this.isSmoothAnim data.type
            for k in data.keys do
            (
                k.value = func k.value
                if IsSmooth then
                (
                    k.intan = func k.intan
                    k.outtan = func k.outtan
                )
            )
        )
        Keys
    ),
    
    fn IOFixColor val =
    (
        if (val.type == #Static) then
        (
            if classOf val.data == Color then
                val.data = Point3 val.data.r val.data.g val.data.b  
            else if classOf val.data == Point3 then
                val.data = Point3 val.data.x val.data.y val.data.z  
        )
        else if (val.type == #Animated) then
        (
            if val.data != undefined and val.data.keys != undefined then
            (
                for k in val.data.keys do
                (
                    if classOf k.value == Color then
                        k.value = Point3 k.value.b k.value.g k.value.r  
                    else if classOf k.value == Point3 then
                        k.value = Point3 k.value.z k.value.y k.value.x  

                    if isProperty k #intan then
                    (
                        if classOf k.intan == Color then
                            k.intan = Point3 k.intan.b k.intan.g k.intan.r
                        else if classOf k.intan == Point3 then
                            k.intan = Point3 k.intan.z k.intan.y k.intan.x
                    )
                    if isProperty k #outtan then
                    (
                        if classOf k.outtan == Color then
                            k.outtan = Point3 k.outtan.b k.outtan.g k.outtan.r
                        else if classOf k.outtan == Point3 then
                            k.outtan = Point3 k.outtan.z k.outtan.y k.outtan.x
                    )
                )
            )
        )
    ),
    
    fn rotateCCW v1 =
    (
        local tmp = -v1.y
        v1.y = v1.x
        v1.x = tmp
    ),
    
    fn transformExtents extents =
    (
        local v1 = extents.maxExtent
        local v2 = extents.minExtent
        this.rotateCCW v1
        this.rotateCCW v2
        if v1.x < v2.x then
        (
            local tmp = v1.x
            v1.x = v2.x
            v2.x = tmp
        )
        if v1.y < v2.y then
        (
            local tmp = v1.y
            v1.y = v2.y
            v2.y = tmp
        )
        extents.maxExtent = v1
        extents.minExtent = v2
    ),
    
    -- ============================================================================
    -- STRING CONVERSION FUNCTIONS
    -- ============================================================================
    
    fn swrite outstring = format outstring to:this.outstream,
    
    fn PairToString x y =
    (
        local ss = stringstream ""
        format "{ %, % }" x y to:ss
        ss as string
    ),
    
    fn TripletToString x y z =
    (
        local ss = stringstream ""
        format "{ %, %, % }" x y z to:ss
        ss as string
    ),
    
    fn Point3ToString p = this.TripletToString p.x p.y p.z,
    fn IntegerPoint3ToString p = this.TripletToString (p.x as integer) (p.y as integer) (p.z as integer),
    
    fn PositionToString p = this.TripletToString (-p.y) p.x p.z,
    fn CollisionVertexToString p = this.TripletToString p.x p.y p.z,
    
    fn ScaleToString p = this.TripletToString p.y p.x p.z,
    fn ColorToString p = this.TripletToString (p.z/255) (p.y/255) (p.x/255),
    fn ColorToString2 c = this.TripletToString (c.b/255) (c.g/255) (c.r/255),
    
    fn writeMDLTVertex p =
        format "\t\t{ %, % },\n" p.x (1-p.y) to:this.outstream,
    
    fn writeMDLVertex p =
        format "\t\t{ %, %, % },\n" (-p.y) p.x p.z to:this.outstream,
    
    fn writeMDLSingleValue v =
        format "\t\t%,\n" v to:this.outstream,
    
    
    fn QuatToString q = 
    (
        local len = sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w)
        if len > 0 and (abs(len - 1.0)) > 0.0001 then
        (
            local inv = 1.0 / len
            q = quat (q.x*inv) (q.y*inv) (q.z*inv) (q.w*inv)
        )
        local ss = stringstream ""
        format "{ %, %, %, % }" (q.y * -1) q.x q.z q.w to:ss
        ss as string
    ),
    
    fn TrianglesToString tris =
    (
        if tris.count == 0 then return "{ }"

        local ss = stringStream "" 
        
        format "{ %, %, %" ((tris[1].x - 1) as integer) ((tris[1].y - 1) as integer) ((tris[1].z - 1) as integer) to:ss
        
        for i = 2 to tris.count do
        (
            format ", %, %, %" ((tris[i].x - 1) as integer) ((tris[i].y - 1) as integer) ((tris[i].z - 1) as integer) to:ss
        )
        
        format " }" to:ss
        
        return ss as string 
    ),
    
    
    fn ArrayToString array &func =
    (
        if (array[1] != undefined) then
        (
            local ss = stringstream ""
            format "{ %" (func array[1]) to:ss
            for i = 2 to array.count do
                format ", %" (func array[i]) to:ss
            format " }" to:ss
            ss as string
        ) else "{ }"
    ),
    
    fn BillboardToString b =
    (
        case (b) of
        (
            #Full: "Billboarded"
            #X: "BillboardedLockX"
            #Y: "BillboardedLockY"
            #Z: "BillboardedLockZ"
            default: "Billboarded"
        )
    ),
    
    fn ControlToString c =
    (
        case (c) of
        (
            #Bezier: "Bezier"
            #Hermite: "Hermite"
            #Linear: "Linear"
            #NonInterp: "DontInterp"
            default: "DontInterp"
        )
    ),
    
    fn FilterModeToString f =
    (
        case (f) of
        (
            1: "None"
            2: "Transparent"
            3: "Blend"
            4: "Additive"
            5: "AddAlpha"
            6: "Modulate"
            7: "Modulate2x"
            default: "None"
        )
    ),
    
    fn ParticleFiltermodeToString i =
    (
        case i of
        (
            1: "Blend"
            2: "Additive"
            3: "Modulate"
            4: "Modulate2x"
            5: "AlphaKey"
        )
    ),
    
    fn ParticleTypeToString i =
    (
        case i of
        (
            1: "Head"
            2: "Tail"
            3: "Both"
        )
    ),
    
    fn StringWithColomms s = "\"" + s + "\"",
    
    -- ============================================================================
    -- MDL WRITING FUNCTIONS
    -- ============================================================================
    
    fn writeMDLSection label data =
    (
        this.swrite (this.Tabs[this.IOLevel] + label + " " + (data as string) + " {\n")
        this.IOLevel = this.IOLevel + 1
    ),
    
    fn newIOLevel =
    (
        this.IOLevel = this.IOLevel + 1
    ),
    
    fn CloseIOLevel =
    (
        this.IOLevel = this.IOLevel - 1
    ),
    
    fn writeMDLValue label data =
    (
        this.swrite (this.Tabs[this.IOLevel] + label + " " + (data as string) + ",\n")
    ),
    
    
    fn writeMDLPoint data =
    (
        this.swrite (this.Tabs[this.IOLevel] + (data as string) + ",\n")
    ),
    
    fn writeMDLSectionEnd =
    (
        this.IOLevel = this.IOLevel - 1
        this.swrite (this.Tabs[this.IOLevel] + "}\n")
    ),
    
    fn writeMDLFlag label =
    (
        this.swrite (this.Tabs[this.IOLevel] + label + ",\n")
    ),
    
    fn writeMDLKey t p =
    (
        this.swrite (this.Tabs[this.IOLevel] + (t as string) + ": " + p + ",\n")
    ),
    
    fn writeMDLExtents extents =
    (
        local tmp2 = extents.maxExtent.y
        extents.maxExtent.y = extents.minExtent.y
        extents.minExtent.y = tmp2
        local tmp = this.Tabs[this.IOLevel]
        this.swrite ( \
            tmp + "MinimumExtent " + (this.PositionToString extents.minExtent) + ",\n" + \
            tmp + "MaximumExtent " + (this.PositionToString extents.maxExtent) + ",\n" + \
            tmp + "BoundsRadius " + (extents.bound as string) + ",\n" \
        )
    ),
    
    fn resetLevel = this.IOLevel = 1,
    
    fn floatToString f = (f as string),
    
    -- ============================================================================
    -- TAG CONVERSION
    -- ============================================================================
    
    fn IntAsTag i = (
        fn getP a s = bit.and (bit.shift a (-s)) 0xFF
        (bit.intAsChar (getP i 0)) + (bit.intAsChar (getP i 8)) + (bit.intAsChar (getP i 16)) + (bit.intAsChar (getP i 24))
    ),
    
    fn ControlToInt c =
    (
        case (c) of
        (
            #Bezier: 3
            #Hermite: 2
            #Linear: 1
            #NonInterp: 0
            default: 0
        )
    ),
    
    -- ============================================================================
    -- MDX WRITING FUNCTIONS
    -- ============================================================================
    
    fn writeMDXHead string =
    (
        writeByte this.outstream (bit.charAsInt string[1])
        writeByte this.outstream (bit.charAsInt string[2])
        writeByte this.outstream (bit.charAsInt string[3])
        writeByte this.outstream (bit.charAsInt string[4])
    ),
    
    fn writeMDXString string n =
    (
        local stream = this.outstream
        local len = string.count
        if len > n then len = n
        for i = 1 to len do
            writeByte stream (bit.charAsInt string[i])
        for i = len + 1 to n do
            writeByte stream 0
    ),
    
    fn writeMDXFlags bitArr = 
    (
        local result = 0
        for b in bitArr do result += b
        writeLong this.outstream result
    ),
    
    fn writeMDXLong val = writeLong this.outstream val,
    fn writeMDXFloat val = writeFloat this.outstream val,
    fn writeMDXByte val = writeByte this.outstream val,
    
    fn writeMDXFace val =
    (
        writeShort this.outstream ((val.x as integer) - 1)
        writeShort this.outstream ((val.y as integer) - 1)
        writeShort this.outstream ((val.z as integer) - 1)
    ),
    
    fn writeMDXPoint3Long val =
    (
        writeLong this.outstream (val.x as integer)
        writeLong this.outstream (val.y as integer)
        writeLong this.outstream (val.z as integer)
    ),
    
    fn writeMDXCollisionVertex val =
    (
        writeFloat this.outstream val.x
        writeFloat this.outstream val.y
        writeFloat this.outstream val.z
    ),
    
    fn writeMDXPoint3 val =
    (
        writeFloat this.outstream val.y
        writeFloat this.outstream val.x
        writeFloat this.outstream val.z
    ),
    
    fn writeMDXPosition val =
    (
        writeFloat this.outstream (-val.y)
        writeFloat this.outstream val.x
        writeFloat this.outstream val.z
    ),
    
    fn writeMDXQuaternion val =
    (
        local len = sqrt(val.x*val.x + val.y*val.y + val.z*val.z + val.w*val.w)
        if len > 0 and (abs(len - 1.0)) > 0.0001 then
        (
            local inv = 1.0 / len
            val = quat (val.x*inv) (val.y*inv) (val.z*inv) (val.w*inv)
        )
        writeFloat this.outstream (-val.y)
        writeFloat this.outstream val.x
        writeFloat this.outstream val.z
        writeFloat this.outstream val.w
    ),
    
    fn writeMDXTVertex val =
    (
        writeFloat this.outstream val.x
        writeFloat this.outstream (1-val.y)
    ),
    
    fn writeMDXColor val =
    (
        writeFloat this.outstream (val.x/255.0)
        writeFloat this.outstream (val.y/255.0)
        writeFloat this.outstream (val.z/255.0)
    ),
    
    fn writeMDXExtents extents =
    (
        this.transformExtents extents
        writeFloat this.outstream extents.bound
        writeFloat this.outstream extents.minExtent.x
        writeFloat this.outstream extents.minExtent.y
        writeFloat this.outstream extents.minExtent.z
        writeFloat this.outstream extents.maxExtent.x
        writeFloat this.outstream extents.maxExtent.y
        writeFloat this.outstream extents.maxExtent.z
    ),
    
    fn posStamp = (ftell this.outstream),
    
    fn InitiateChunk name includeName:true =
    (
        if includeName then this.writeMDXHead name
        local pos = (ftell this.outstream)
        writeLong this.outstream 0
        pos
    ),
    
    fn FinishChunk oldPos inclusive:4 =
    (
        local currentPos = ftell this.outstream
        fseek this.outstream oldPos #seek_set
        this.writeMDXLong (currentPos - oldPos - inclusive)
        fseek this.outstream currentPos #seek_set
    ),
    
    -- ============================================================================
    -- MDX READING FUNCTIONS
    -- ============================================================================
    
    fn readMDXByte = readByte this.instream #unsigned,
    fn readMDXLong = readLong this.instream #unsigned,
    fn readMDXShort = readShort this.instream #unsigned,
    fn readMDXFloat = readFloat this.instream,
    
    fn readMDXString size =
    (
        local ss = StringStream ""
        for i = 1 to size do
            format "%" (bit.intaschar (readByte this.instream #unsigned)) to:ss
        ss as string
    ),
    
    fn readMDXPosition =
    (
        local y = -(readFloat this.instream)
        local x = readFloat this.instream
        local z = readFloat this.instream
        [x, y, z]
    ),
    
    fn readMDXVector3 =
    (
        local x = readFloat this.instream
        local y = readFloat this.instream
        local z = readFloat this.instream
        [x, y, z]
    ),
    
    fn readMDXExtents =
    (
        local ex = Wc3Extent ()
        ex.bound = this.readMDXFloat ()
        ex.minExtent = this.readMDXVector3 ()
        ex.maxExtent = this.readMDXVector3 ()
        ex
    ),
    
    fn readMDXVector3Long =
    (
        local x = readLong this.instream #unsigned
        local y = readLong this.instream #unsigned
        local z = readLong this.instream #unsigned
        [x, y, z]
    ),
    
    fn readMDXScale =
    (
        local y = readFloat this.instream
        local x = readFloat this.instream
        local z = readFloat this.instream
        [x, y, z]
    ),
    
    fn readMDXQuaternion =
    (
        local y = -(readFloat this.instream)
        local x = readFloat this.instream
        local z = readFloat this.instream
        local w = readFloat this.instream
        quat x y z w
    ),
    
    fn readMDXTvertex =
    (
        local u = readFloat this.instream
        local v = (1 - (readFloat this.instream))
        [u, v]
    ),
    
    fn readMDXColor =
    (
        local r = ::Utils.round ((readFloat this.instream)*255.0)
        local g = ::Utils.round ((readFloat this.instream)*255.0)
        local b = ::Utils.round ((readFloat this.instream)*255.0)
        [r, g, b]
    ),
    
    fn readMDXTime =
    (
        local t = (readLong this.instream #unsigned) * framerate
        t = ::Utils.round (t as float / 100.0)
        (t/10.0 as time)
    ),
    
    fn peekMDXHeader =
    (
        local i = readLong this.instream #unsigned
        fseek this.instream -4 #seek_cur
        if i == undefined then i = 0
        i
    ),
    
    fn jumpMDXReading bytes =
    (
        fseek this.instream bytes #seek_cur
    ),
    
    fn readMDXArray size &readFunc =
    (
        local result = #()
        result[size] = undefined
        for i = 1 to size do result[i] = readFunc ()
        result
    ),
    
    fn MDXSkipChunk =
    (
        local chunksize = this.readMDXLong ()
        fseek this.instream chunksize #seek_cur
    ),
    
    -- ============================================================================
    -- CHUNK READING FUNCTIONS
    -- ============================================================================
    
    fn MDXStartReadingChunk = this.chunkSize = (this.readMDXLong ()),
    fn MDXHasChunkEnded = this.chunkSize <= 0,
    fn MDXStartChunkElem specialCase:0 = this.chunkSize -= ((this.readMDXLong ()) + specialCase),
    fn MDXFixChunkSize rest = this.chunkSize -= rest,
    
    -- ============================================================================
    -- FLAG CONVERSION
    -- ============================================================================
    
    fn IntToFlags num mx =
    (
        local result = #()
        result[mx] = false
        for i = 1 to mx do
            result[i] = ::Utils.I2B (bit.and (bit.shift num (1-i)) 0x01)
        result
    )
)

-- ============================================================================
-- GLOBAL INSTANCE (for backward compatibility)
-- ============================================================================
global IOManager = WhiteoutDexIOStruct()

