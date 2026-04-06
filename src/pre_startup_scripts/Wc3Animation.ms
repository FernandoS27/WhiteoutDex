/*
    Wc3Animation.ms
    
    Refactored as Struct for better encapsulation and maintainability.
    All global variables are now instance properties.
    
    Usage:
        global AnimationManager = Wc3AnimationStruct()
        AnimationManager.InitiateAnimationData sequences sequencesOriginal
        local trans = AnimationManager.GetTransformations obj
*/

-- ============================================================================
-- HELPER STRUCTS (remain outside main struct)
-- ============================================================================

struct TCBKeys (
    time, 
    value, 
    tension, 
    continuity, 
    bias
)

struct ParameterValue (
    type, 
    data
)

struct KeyData (
    time,
    value
)

struct SmoothKeyData (
    time,
    value,
    intan,
    outtan
)

struct AnimationData (
    type,
    globalSequenceId = undefined,
    keys
)

struct TransformationData (
    translation,
    rotation,
    scale
)

-- ============================================================================
-- MAIN ANIMATION STRUCT
-- ============================================================================

struct Wc3AnimationStruct
(
    -- ============================================================================
    -- INSTANCE PROPERTIES (formerly global variables)
    -- ============================================================================
    
    public GlobalSequencesGathered = #(),
    public GlobalSequenceCount = 0,
    public Sequences = #(),
    public SequencesOriginal = #(),
    public IKChains = #(),
    public IKChainsKeys = #(),
    public IKChainsBones = #(),
    
    -- ============================================================================
    -- GLOBAL SEQUENCE FUNCTIONS
    -- ============================================================================
    
    fn LoadGlobalSequence value =
    (
        local index = findItem this.GlobalSequencesGathered value
        if index == 0 then
        (
            append this.GlobalSequencesGathered value
            this.GlobalSequenceCount = this.GlobalSequenceCount + 1
            index = this.GlobalSequenceCount
        )
        index
    ),
    
    fn GetGlobalSequences = 
        this.GlobalSequencesGathered,
    
    -- ============================================================================
    -- TANGENT COMPUTATION FUNCTIONS
    -- ============================================================================
    
    fn ComputeTangents prevkey currentkey nextkey =
    (
        local qprev, qnext, q, p3, qcurrent
        local qp, qm, qa, qb, qae, qbe
        local tm,cm,cp,bm,bp,tmcm,tmcp,ksm,ksp,kdm,kdp,c
        local dt,f_p,f_n,pt,nt
        qcurrent = copy currentkey.value
        if prevkey != undefined then
        (
            if qcurrent.angle > 360.0 then
            (
                p3 = qcurrent.axis
                q = quat p3.x p3.y 0.0 0.0
                qm = LogN q
            )
            else
            (
                qprev = copy prevkey.value
                if (::Utils.DotQuat qcurrent qprev) < 0 then qprev = - qprev
                qm = LnDif qprev qcurrent
            )
        )
        if nextkey != undefined then
        (
            qnext = copy nextkey.value
            if (qnext.angle) > 360.0 then
            (
                p3 = qnext.axis
                q = quat p3.x p3.y 0.0 0.0
                qp = LogN q
            )
            else
            (
                if (::Utils.DotQuat qcurrent qnext) < 0 then qnext = - qnext
                qp = LnDif qcurrent qnext
            )
        )
        if prevkey == undefined then qm = copy qp
        if nextkey == undefined then qp = copy qm
        
        if nextkey != undefined and prevkey != undefined then 
        (
            pt = prevkey.time
            nt = nextkey.time
            dt = 0.5*(::Utils.F2R (nt - pt))
            f_p = (::Utils.F2R (currentkey.time - pt))/dt 
            f_n = (::Utils.F2R (nt- currentkey.time))/dt
            c = (abs (currentkey.continuity))
            f_p = f_p + c - c*f_p
            f_n = f_n + c - c*f_n
        )
        else
        (
            f_p = 1.0
            f_n = 1.0
        )
        
        cm = 1.0 - currentkey.continuity
        tm = 0.5*(1.0 - currentkey.tension)
        cp = 2.0 - cm
        bm = 1.0 - currentkey.bias
        bp = 2.0 - bm
        tmcm = tm*cm
        tmcp = tm*cp
        ksm = 1.0 - tmcm*bp*f_p
        ksp = (-tmcp)*bm*f_p
        kdm = tmcp*bp*f_n
        kdp = tmcm*bm*f_n - 1.0
        
        qa = (::Utils.SumQuat (qm*kdm) (qp*kdp))*0.5
        qb = (::Utils.SumQuat (qm*ksm) (qp*ksp))*0.5
        qae = qcurrent*(exp qa)
        qbe = qcurrent*(exp qb)
        (DoubleV qae qbe)
    ),
    
    fn InTan p1 p2 p3 t c b = 
    (
        local S = ((1-t)*(1-c)*(1+b)*0.5)
        local R = ((1-t)*(1+c)*(1-b)*0.5)
        (((p2-p1)*S)+((p3-p2)*R))
    ),

    fn OutTan p1 p2 p3 t c b =
    (
        local S = ((1-t)*(1+c)*(1+b)*0.5)
        local R = ((1-t)*(1-c)*(1-b)*0.5)
        (((p2-p1)*S)+((p3-p2)*R))
    ),
    
    fn BezierOutTan pe pd a d = 3*pe + (2*d - 5*a - 9*pd)/6.0,
    
    fn BezierInTan pe pd a d = 3*pd + (2*a - 9*pe - 5*d)/6.0,
    
    -- ============================================================================
    -- UTILITY FUNCTIONS
    -- ============================================================================
    
    fn controllerIsNonInterp c =
    (
        local result = true
        for k in c.keys where result do
            result = result and (k.inTangentType == #step) and (k.outTangentType == #step)
        result
    ),
    
    fn E2P e1 = point3 e1.x e1.y e1.z,
    fn P2E p1 = eulerangles p1.x p1.y p1.z,
    
    fn GetTangentSmooth p1 p2 p3 =
    (
        local s1 = this.E2P (p1 as eulerangles)
        local s2 = this.E2P (p2 as eulerangles)
        local s3 = this.E2P (p3 as eulerangles)
        local e1 = (s2 + (s2-s1)*0.25 - (s3-s2)*0.25)
        ((this.P2E e1) as quat)
    ),
    
    fn GetRelativePosition t o parent p =
    (
        at time t
        (
            local i = o.pos
            local r = ::Utils.relativeFactor parent
            local c = parent.center
        )
        ((i-c)/r - p)
    ),
    
    fn GetRot t o r pr hp =
    (
        local parentHasConstraint = false
        if o.parent != undefined then
        (
            local c = o.parent.rotation.controller
            if classof c == rotation_list then
            (
                local n = c.count
                for i = 1 to n do
                (
                    local tmp = classOf (this.getSubControl c i) 
                    if (tmp == Orientation_Constraint) or (tmp == LookAt_Constraint) then
                    (
                        parentHasConstraint = true
                        exit
                    )
                )
            )
        )
        
        if parentHasConstraint then
        (
            local localTM = at time t (o.transform * inverse o.parent.transform)
            local localTM0 = at time 0f (o.transform * inverse o.parent.transform)
            local localRot = localTM.rotationPart as quat
            local localRot0 = localTM0.rotationPart as quat
            localRot * inverse(localRot0)
        )
        else
        (
            at time t local v = o.rotation*r
            if hp then at time t v = (v/(o.parent.rotation*pr))
            v
        )
    ),
    
    fn GetRotBiped t o r pr =
    (
        at time t local v = (inverse (biped.getTransform o #rotation))*r
        if o.parent != undefined then
        (
            if classOf o.parent == Biped_Object then
                at time t v = (v/((inverse (biped.getTransform o.parent #rotation))*pr))
            else
                at time t v = (v/((inverse o.parent.rotation)*pr))
        )
        v
    ),
    
    fn ProcessBezier keys =
    (
        local k = #()
        local x
        local outtan
        local intan
        if keys.count != 0 then k[keys.count] = undefined
        for x = 1 to keys.count do
        (
            if x != 1 then intan = (this.BezierInTan keys[x-1].outtan keys[x].intan keys[x-1].value keys[x].value) else intan = keys[x].value
            if x != keys.count then outtan = (this.BezierOutTan keys[x].outtan keys[x+1].intan keys[x].value keys[x+1].value) else outtan = keys[x].value
            
            -- Snap tangents that are nearly equal to value (floating point noise)
            local val = keys[x].value
            if (classOf val == Point3 or classOf val == Color) then
            (
                if (distance intan val) < 0.01 then intan = copy val
                if (distance outtan val) < 0.01 then outtan = copy val
            )
            else if (classOf val == Float or classOf val == Integer) then
            (
                if (abs (intan - val)) < 0.01 then intan = copy val
                if (abs (outtan - val)) < 0.01 then outtan = copy val
            )
            
            k[x] = (SmoothKeyData keys[x].time keys[x].value intan outtan)
        )
        k
    ),
    
    fn getSubControl c i =
    (
        if (isProperty c #getSubCtrl) then c.getSubCtrl i
        else (
            getPropertyController c (c.GetName i)
        )
    ),
    
    fn isIKAffected o = classOf o.transform.controller == IKControl,
    
    fn isObjectConstrained o =
    (
        local c = o.rotation.controller
        if classof c == rotation_list then
        (
            local n = c.count
            local result = false
            for i = 1 to n do
            (
                local tmp = classOf (this.getSubControl c i) 
                result = result or (tmp == Orientation_Constraint) or (tmp == LookAt_Constraint)
            )
            result
        )
        else false
    ),
    
    fn isLinkConstrained o =
    (
        classOf o.transform.controller == Link_Constraint
    ),

    fn parentIsLinkConstrained obj =
    (
        obj.parent != undefined and (try(classOf obj.parent.transform.controller == Link_Constraint) catch(false))
    ),

        fn RotationKeysLinkParent o =
    (
        local keys = #()
        local c = o.rotation.controller
        if c == undefined or not isProperty c #keys then return #()
        local k = c.keys
        if k == undefined or k.count == 0 then return #()
        
        local hasParent = o.parent != undefined
        local r = at time 0f o.transform.rotationPart
        local pr = (quat 0 0 0 1)
        if hasParent then pr = at time 0f o.parent.transform.rotationPart
        
        for i = 1 to k.count do
        (
            local v = this.GetRotIK k[i].time o r pr hasParent
            append keys (KeyData k[i].time v)
        )
        keys
    ),
    
    fn getLinkConstraintKeyTimes o =
    (
        local lc = o.transform.controller
        local times = #()
        
        try
        (
            local numLinks = lc.getNumTargets()
            for i = 1 to numLinks do
            (
                try
                (
                    local f = lc.getFrameNo i
                    append times (f as time)
                )
                catch()
            )
        )
        catch()
        
        sort times
        makeUniqueArray times
    ),
    
    fn LinkConstraintPositionKeys o =
    (
        local keys = #()
        local hasParent = o.parent != undefined
        
        local worldPos0 = at time 0f o.transform.pos
        local parentTM0 = if hasParent then (at time 0f o.parent.transform) else (matrix3 1)
        local parentPos0 = parentTM0.pos
        local parentRot0 = parentTM0.rotationPart as quat
        
        local localPos0 = if hasParent then \
            ((worldPos0 - parentPos0) * (inverse parentRot0)) \
            else worldPos0
        
        local correctionQuat = if hasParent then \
            (at time 0f o.parent.transform.rotationPart as quat) \
            else (quat 1)
        
        local parentScale = if hasParent then (at time 0f o.parent.scale) else [1,1,1]
        
        local linkTimes = this.getLinkConstraintKeyTimes o
        local allTimes = #()
        
        for seq in this.Sequences where seq != undefined do
        (
            appendIfUnique allTimes seq.start
            appendIfUnique allTimes seq.end
            
            for lt in linkTimes do
            (
                if lt >= seq.start and lt <= seq.end then
                    appendIfUnique allTimes lt
            )
            
            -- Always dense-sample: the bone follows an animated target,
            -- so we need keys even without a target switch within the sequence.
            local t = seq.start + 1
            while t < seq.end do
            (
                appendIfUnique allTimes t
                t += 2
            )
        )
        
        sort allTimes
        allTimes = makeUniqueArray allTimes
        
        for t in allTimes do
        (
            local worldPosT = at time t o.transform.pos
            local parentTMt = if hasParent then (at time t o.parent.transform) else (matrix3 1)
            local parentPosT = parentTMt.pos
            local parentRotT = parentTMt.rotationPart as quat
            
            local localPosT = if hasParent then \
                ((worldPosT - parentPosT) * (inverse parentRotT)) \
                else worldPosT
            
            local delta = localPosT - localPos0
            
            if hasParent then
                delta = delta * parentScale
            delta *= correctionQuat
            
            append keys (KeyData t delta)
        )
        
        keys
    ),
    
    fn LinkConstraintRotationKeys o =
    (
        local hasParent = o.parent != undefined
        local r = at time 0f o.transform.rotationPart
        local pr = (quat 0 0 0 1)
        if hasParent then pr = at time 0f o.parent.transform.rotationPart
        
        local linkTimes = this.getLinkConstraintKeyTimes o
        
        local allTimes = #()
        for i = 1 to this.Sequences.count do
        (
            local seq = this.Sequences[i]
            if seq == undefined then continue
            
            appendIfUnique allTimes seq.start
            appendIfUnique allTimes seq.end
            
            for lt in linkTimes do
            (
                if lt >= seq.start and lt <= seq.end then
                    appendIfUnique allTimes lt
            )
            
            -- Always dense-sample: the bone follows an animated target,
            -- so we need keys even without a target switch within the sequence.
            local t = seq.start + 1
            while t < seq.end do
            (
                appendIfUnique allTimes t
                t += 2
            )
        )
        
        sort allTimes
        allTimes = makeUniqueArray allTimes
        
        local keys = #()
        for t in allTimes do
        (
            local v = this.GetRotIK t o r pr hasParent
            append keys (KeyData t v)
        )
        
        keys
    ),

    fn getKeysFromObject o pos:true rot:true sca:true =
    (
        local result = #()
        local keys
        local count = 0
        
        -- Link_Constraint hat keine normalen PRS sub-controller keys
        if classOf o.transform.controller == Link_Constraint then
        (
            local lc = o.transform.controller
            try
            (
                local numLinks = lc.getNumTargets()
                for i = 1 to numLinks do
                (
                    local t = undefined
                    try ( t = (lc.getFrameNo i) as time ) catch()
                    if t != undefined then
                    (
                        count += 1
                        result[count] = t
                    )
                )
            )
            catch()
            
            sort result
            return (makeUniqueArray result)
        )
        
        if pos then
        (
            keys = o.pos.controller.keys
            count = keys.count
            if count > 0 then result[count] = undefined
            for i = 1 to count do result[i] = keys[i].time
        )
        if rot then
        (
            keys = o.rotation.controller.keys
            local count2 = keys.count
            if count2 > 0 then result[count + count2] = undefined
            for i = 1 to count2 do result[count + i] = keys[i].time
            count += count2
        )
        if sca then
        (
            keys = o.scale.controller.keys
            local count2 = keys.count
            if count2 > 0 then result[count + count2] = undefined
            for i = 1 to count2 do result[count + i] = keys[i].time
            count += count2
        )
        sort result
        makeUniqueArray result
    ),
    
    -- ============================================================================
    -- IK FUNCTIONS
    -- ============================================================================
    
    fn getIKId o =
    (
        local result = #()
        for i = 1 to this.IKChains.count do
            if (findItem this.IKChainsBones[i] o) != 0 then
                append result i
        result
    ),
    
    fn getGoalDistance o t =
    (
        local p1, p2
        at time t p1 = o.pos
        at time t p2 = o.controller.startjoint.pos
        distance p1 p2
    ),
    
    fn getAngleofGoalChange o t1 t2 =
    (
        local v1, v2
        at time t1 v1 = o.controller.startjoint.pos - o.pos
        at time t2 v2 = o.controller.startjoint.pos - o.pos
        acos ((dot v1 v2)/((sqrt (dot v1 v1)) * (sqrt (dot v2 v2))))
    ),
    
    fn getObjectConstraintKeys obj =
    (
        local c = obj.rotation.controller
        local n = c.count
        local objChain = #(obj)
        join objChain (::Utils.GetParents obj)
        local orChain = #()
        local laChain = #()
        for i = 1 to n do
        (
            local c2 = this.getSubControl c i
            if (classof c2 == Orientation_Constraint) then
            (
                local n2 = c2.getNumTargets()
                for j = 1 to n2 do
                (
                    local o = c2.getNode j
                    local tmp = #(o)
                    join tmp (::Utils.GetParents o)
                    join orChain tmp
                )
            )
            else if (classof c2 == LookAt_Constraint) then
            (
                local n2 = c2.getNumTargets()
                for j = 1 to n2 do
                (
                    local o = c2.getNode j
                    local tmp = #(o)
                    join tmp (::Utils.GetParents o)
                    join laChain tmp
                )
            )
        )
        orChain = ::Utils.ArrayMakeUnique orChain
        laChain = ::Utils.ArrayMakeUnique laChain
        local needPos = laChain.count != 0
        local keys = #()
        for s in orChain where s != undefined do
            join keys (this.getKeysFromObject s pos:false sca:false)
        for s in laChain where s != undefined do
            join keys (this.getKeysFromObject s sca:false)
        for s in objChain where s != undefined do
            join keys (this.getKeysFromObject s sca:false)
        keys = ::Utils.ArrayMakeUnique keys
        sort keys
        keys
    ),
    
    fn IKRefineKeys o &arr t1 t2 =
    (
        local d1 = (this.getGoalDistance o t1)
        local d2 = (this.getGoalDistance o t2)
        local maxDist = if d1 > d2 then d1 else d2
        local d = (abs (d2 - d1))/maxDist
        if (d > 0.25) or (this.getAngleofGoalChange o t1 t2) > 30.0 then
        (
            local th = t1 + (t2-t1)*0.5
            append arr th
            this.IKRefineKeys o arr t1 th
            this.IKRefineKeys o arr th t2
        )
    ),
    
    -- ============================================================================
    -- POSITION KEYS FUNCTIONS
    -- ============================================================================
    
    fn PositionKeys o =
    (
        local keys = #()
        local c = o.position.controller
        local hasParent = o.parent != undefined and (classOf o.parent != Uvwmap)
        
        local scaleKeyTimes = #()
        if o.scale.isAnimated and o.scale.controller != undefined and o.scale.controller.keys != undefined then
        (
            for sk in o.scale.controller.keys do
                append scaleKeyTimes sk.time
        )
        
        local posKeyTimes = #()
        for pk in c.keys do
            append posKeyTimes pk.time
        
        local additionalKeyTimes = #()
        for st in scaleKeyTimes do
        (
            if (findItem posKeyTimes st) == 0 then
                append additionalKeyTimes st
        )
        
        local allKeyTimes = #()
        join allKeyTimes posKeyTimes
        join allKeyTimes additionalKeyTimes
        sort allKeyTimes
        allKeyTimes = makeUniqueArray allKeyTimes
        
        local correctionQuat = quat 1
        local parentScale = [1, 1, 1]
        
        if o.parent != undefined then
        (
            correctionQuat = at time 0f o.parent.transform.rotationPart as quat
            parentScale = at time 0f o.parent.scale  
        )

        local p = at time 0f c.value

        case (classOf c) of
        (
            bezier_position:
            (
                local keys2 = #()
                keys2[allKeyTimes.count] = undefined
                
                for i = 1 to allKeyTimes.count do  
                (
                    local t = allKeyTimes[i]
                    local relativeAnimVector = (at time t c.value) - p
                    
                    local in_tangent = relativeAnimVector
                    if i > 1 then  
                    (
                        local sample_time = allKeyTimes[i-1] + 2.0/3.0 * (t - allKeyTimes[i-1])
                        in_tangent = (at time sample_time c.value) - p
                    )
                    
                    local out_tangent = relativeAnimVector
                    if i < allKeyTimes.count then  
                    (
                        local sample_time = t + 1.0/3.0 * (allKeyTimes[i+1] - t)
                        out_tangent = (at time sample_time c.value) - p
                    )

                    if o.parent != undefined then
                    (
                        relativeAnimVector = relativeAnimVector * parentScale
                        in_tangent = in_tangent * parentScale
                        out_tangent = out_tangent * parentScale
                    )

                    relativeAnimVector *= correctionQuat
                    in_tangent *= correctionQuat
                    out_tangent *= correctionQuat
                    
                    keys2[i] = (SmoothKeyData t relativeAnimVector in_tangent out_tangent)
                )
                keys = this.ProcessBezier keys2
            )
            
            tcb_position: 
            (
                local temp_keys = #()
                
                local keyValues = #()
                for t in allKeyTimes do
                (
                    local relativeAnimVector = (at time t c.value) - p
                    append keyValues relativeAnimVector
                )
                
                local firstVal = if keyValues.count > 0 then keyValues[1] else [0,0,0]
                append temp_keys (SmoothKeyData 0f firstVal undefined undefined)
                
                for i = 1 to allKeyTimes.count do
                    append temp_keys (SmoothKeyData allKeyTimes[i] keyValues[i] undefined undefined)
                
                local lastVal = if keyValues.count > 0 then keyValues[keyValues.count] else [0,0,0]
                append temp_keys (SmoothKeyData 999999999f lastVal undefined undefined)
                
                -- TCB-Lookup: einmal bauen statt pro Key durch alle c.keys scannen
                local tcbLookup = Dictionary #string
                for j = 1 to c.keys.count do
                (
                    local tKey = c.keys[j].time as string
                    tcbLookup[tKey] = #(
                        (c.keys[j].tension - 25) / 25.0,
                        (c.keys[j].continuity - 25) / 25.0,
                        (c.keys[j].bias - 25) / 25.0
                    )
                )
                
                for k_idx = 1 to allKeyTimes.count do
                (
                    local t_val = 0.0
                    local c_val = 0.0
                    local b_val = 0.0
                    
                    local tKey = allKeyTimes[k_idx] as string
                    if hasDictValue tcbLookup tKey then
                    (
                        local tcb = tcbLookup[tKey]
                        t_val = tcb[1]
                        c_val = tcb[2]
                        b_val = tcb[3]
                    )
                    
                    temp_keys[k_idx+1].intan = (this.InTan temp_keys[k_idx].value temp_keys[k_idx+1].value temp_keys[k_idx+2].value t_val c_val b_val)
                    temp_keys[k_idx+1].outtan = (this.OutTan temp_keys[k_idx].value temp_keys[k_idx+1].value temp_keys[k_idx+2].value t_val c_val b_val)
                )
                
                deleteItem temp_keys (temp_keys.count)
                deleteItem temp_keys 1

                if o.parent != undefined then
                (
                    for k in temp_keys do
                    (
                        k.value = (k.value * parentScale) * correctionQuat
                        if k.intan != undefined then
                            k.intan = (k.intan * parentScale) * correctionQuat
                        if k.outtan != undefined then
                            k.outtan = (k.outtan * parentScale) * correctionQuat
                    )
                )
                else
                (
                    for k in temp_keys do
                    (
                        k.value *= correctionQuat
                        if k.intan != undefined then k.intan *= correctionQuat
                        if k.outtan != undefined then k.outtan *= correctionQuat
                    )
                )
                
                keys = temp_keys
            )

            default: 
            (
                for t in allKeyTimes do 
                (
                    local relativeAnimVector = (at time t c.value) - p
                    
                    if o.parent != undefined then
                        relativeAnimVector = relativeAnimVector * parentScale
                    
                    relativeAnimVector *= correctionQuat
                    
                    append keys (KeyData t relativeAnimVector)
                )
            )
        )
        return keys
    ),
    
    fn PositionType controller =
    (
        local type
        case (classOf controller) of
        (
            bezier_position: 
            (
                if (this.controllerIsNonInterp controller) then
                    type = #NonInterp
                else
                    type = #Bezier
            )
            tcb_position: type = #Hermite
            linear_position: type = #Linear
            default: type = #NonInterp
        )
        type
    ),
    
    fn PositionKeysBiped o =
    (
        local keys = #()
        
        if (not (hasProperty o.transform.controller "vertical" or hasProperty o.transform.controller "horizontal")) then
        (
            return keys
        )
        
        append keys (SmoothKeyData 0f [0,0,0] undefined undefined)
        
        local p = o.transform.pos
        
        if o.parent != undefined then
        (
            p -= o.parent.transform.pos
            pr = o.parent.transform.rotation
        )
        
        local i_h = 1
        local i_v = 1
        
        keys_v = o.transform.controller.vertical.controller.keys
        keys_h = o.transform.controller.horizontal.controller.keys
        
        local keys_t = #()
        local keys_c = #()
        local keys_b = #()
        
        while (i_h <= keys_h.count) or (i_v <= keys_v.count) do
        (
            local translated_pos
            if i_h > keys_h.count then
            (
                at time keys_v[i_v].time translated_pos = o.transform.pos - p
                if o.parent != undefined then 
                    at time keys_v[i_v].time translated_pos = ((o.transform.pos - o.parent.transform.pos) * inverse(o.parent.transform.rotation * inverse(pr))) - p
                append keys (SmoothKeyData keys_v[i_v].time translated_pos undefined undefined)
                
                append keys_t (biped.getKey o.transform.controller.vertical.controller i_v).tension
                append keys_c (biped.getKey o.transform.controller.vertical.controller i_v).continuity
                append keys_b (biped.getKey o.transform.controller.vertical.controller i_v).bias
                
                i_v += 1
            )
            else if i_v > keys_v.count then
            (
                at time keys_h[i_h].time translated_pos = o.transform.pos - p
                if o.parent != undefined then 
                    at time keys_h[i_h].time translated_pos = ((o.transform.pos - o.parent.transform.pos) * inverse(o.parent.transform.rotation * inverse(pr))) - p
                append keys (SmoothKeyData keys_h[i_h].time translated_pos undefined undefined)
                
                append keys_t (biped.getKey o.transform.controller.horizontal.controller i_h).tension
                append keys_c (biped.getKey o.transform.controller.horizontal.controller i_h).continuity
                append keys_b (biped.getKey o.transform.controller.horizontal.controller i_h).bias
                
                i_h += 1
            )
            else if keys_v[i_v].time < keys_h[i_h].time then
            (
                at time keys_v[i_v].time translated_pos = o.transform.pos - p
                if o.parent != undefined then 
                    at time keys_v[i_v].time translated_pos = ((o.transform.pos - o.parent.transform.pos) * inverse(o.parent.transform.rotation * inverse(pr))) - p
                append keys (SmoothKeyData keys_v[i_v].time translated_pos undefined undefined)
                
                append keys_t (biped.getKey o.transform.controller.vertical.controller i_v).tension
                append keys_c (biped.getKey o.transform.controller.vertical.controller i_v).continuity
                append keys_b (biped.getKey o.transform.controller.vertical.controller i_v).bias
                
                i_v += 1
            )
            else if keys_v[i_v].time > keys_h[i_h].time then
            (
                at time keys_h[i_h].time translated_pos = o.transform.pos - p
                if o.parent != undefined then 
                    at time keys_h[i_h].time translated_pos = ((o.transform.pos - o.parent.transform.pos) * inverse(o.parent.transform.rotation * inverse(pr))) - p
                append keys (SmoothKeyData keys_h[i_h].time translated_pos undefined undefined)
                
                append keys_t (biped.getKey o.transform.controller.horizontal.controller i_h).tension
                append keys_c (biped.getKey o.transform.controller.horizontal.controller i_h).continuity
                append keys_b (biped.getKey o.transform.controller.horizontal.controller i_h).bias
                
                i_h += 1
            )
            else 
            (
                at time keys_h[i_h].time translated_pos = o.transform.pos - p
                if o.parent != undefined then 
                    at time keys_h[i_h].time translated_pos = ((o.transform.pos - o.parent.transform.pos) * inverse(o.parent.transform.rotation * inverse(pr))) - p
                append keys (SmoothKeyData keys_h[i_h].time translated_pos undefined undefined)
                
                append keys_t (biped.getKey o.transform.controller.vertical.controller i_v).tension
                append keys_c (biped.getKey o.transform.controller.vertical.controller i_v).continuity
                append keys_b (biped.getKey o.transform.controller.vertical.controller i_v).bias
                
                i_h += 1
                i_v += 1
            )
        )
        append keys (SmoothKeyData 999999999f [0,0,0] undefined undefined)
        for k = 1 to (keys.count - 2) do
        (
            local t = (keys_t[k] - 25) / 25
            local c_ = (keys_c[k] - 25) / 25                
            local b = (keys_b[k] - 25) / 25
            keys[k+1].intan = (this.InTan keys[k].value keys[k+1].value keys[k+2].value t c_ b)
            keys[k+1].outtan = (this.OutTan keys[k].value keys[k+1].value keys[k+2].value t c_ b)
        )
        deleteItem keys (keys.count)
        deleteItem keys 1
        keys    
    ),
    
    fn PositionKeysHybrid o =
    (
        local keys = #()
        local c = o.pos.controller
        
        if c == undefined or not isProperty c #keys then
            return #()
        
        local k = c.keys
        if k == undefined or k.count == 0 then
            return #()
        
        local correctionQuat = quat 1
        
        if o.parent != undefined then
        (
            correctionQuat = at time 0f o.parent.transform.rotationPart as quat
        )
        
        local p = at time 0f in coordsys parent o.pos
        
        case (classOf c) of
        (
            bezier_position:
            (
                local keys2 = #()
                for i = 1 to k.count do
                (
                    local relativeAnimVector = (at time k[i].time in coordsys parent o.pos) - p
                    
                    local in_tangent = relativeAnimVector
                    if i > 1 then
                    (
                        local sample_time = k[i-1].time + 2.0/3.0 * (k[i].time - k[i-1].time)
                        in_tangent = (at time sample_time in coordsys parent o.pos) - p
                    )
                    
                    local out_tangent = relativeAnimVector
                    if i < k.count then
                    (
                        local sample_time = k[i].time + 1.0/3.0 * (k[i+1].time - k[i].time)
                        out_tangent = (at time sample_time in coordsys parent o.pos) - p
                    )
                    
                    relativeAnimVector *= correctionQuat
                    in_tangent *= correctionQuat
                    out_tangent *= correctionQuat
                    
                    keys2[i] = (SmoothKeyData k[i].time relativeAnimVector in_tangent out_tangent)
                )
                keys = this.ProcessBezier keys2
            )
            
            tcb_position:
            (
                local temp_keys = #()
                append temp_keys (SmoothKeyData 0f [0,0,0] undefined undefined)
                
                for key_tcb in k do
                (
                    local relativeAnimVector = (at time key_tcb.time in coordsys parent o.pos) - p
                    append temp_keys (SmoothKeyData key_tcb.time relativeAnimVector undefined undefined)
                )
                
                append temp_keys (SmoothKeyData 999999999f [0,0,0] undefined undefined)
                
                for k_idx = 1 to k.count do
                (
                    local t = (k[k_idx].tension - 25) / 25.0
                    local c_ = (k[k_idx].continuity - 25) / 25.0
                    local b = (k[k_idx].bias - 25) / 25.0
                    temp_keys[k_idx+1].intan = this.InTan temp_keys[k_idx].value temp_keys[k_idx+1].value temp_keys[k_idx+2].value t c_ b
                    temp_keys[k_idx+1].outtan = this.OutTan temp_keys[k_idx].value temp_keys[k_idx+1].value temp_keys[k_idx+2].value t c_ b
                )
                
                deleteItem temp_keys (temp_keys.count)
                deleteItem temp_keys 1
                
                for key_item in temp_keys do
                (
                    key_item.value *= correctionQuat
                    if key_item.intan != undefined then
                        key_item.intan *= correctionQuat
                    if key_item.outtan != undefined then
                        key_item.outtan *= correctionQuat
                )
                
                keys = temp_keys
            )
            
            linear_position:
            (
                for key_lin in k do
                (
                    local relativeAnimVector = (at time key_lin.time in coordsys parent o.pos) - p
                    relativeAnimVector *= correctionQuat
                    append keys (KeyData key_lin.time relativeAnimVector)
                )
            )
            
            default:
            (
                for key_def in k do
                (
                    local relativeAnimVector = (at time key_def.time in coordsys parent o.pos) - p
                    relativeAnimVector *= correctionQuat
                    append keys (KeyData key_def.time relativeAnimVector)
                )
            )
        )
        
        keys
    ),
    
    -- ============================================================================
    -- ROTATION KEYS FUNCTIONS
    -- ============================================================================
    
    fn RotationKeys o =
    (
        local keys = #()
        local keys2 = #()
        local tangents
        local c = o.rotation.controller
        local hasParent = o.parent != undefined and (classOf o.parent != Uvwmap) 
        local r = (inverse o.rotation)
        local k = c.keys
        local pr = (quat 0 0 0 1)
        if hasParent and (classOf o.parent != Uvwmap) then pr = (inverse o.parent.rotation)
        
        local scaleKeyTimes = #()
        if o.scale.isAnimated and o.scale.controller != undefined and o.scale.controller.keys != undefined then
        (
            for sk in o.scale.controller.keys do
                append scaleKeyTimes sk.time
        )
        
        local rotKeyTimes = #()
        for rk in k do
            append rotKeyTimes rk.time
        
        local additionalKeyTimes = #()
        for st in scaleKeyTimes do
        (
            if (findItem rotKeyTimes st) == 0 then
                append additionalKeyTimes st
        )
        
        local allKeyTimes = #()
        join allKeyTimes rotKeyTimes
        join allKeyTimes additionalKeyTimes
        sort allKeyTimes
        allKeyTimes = makeUniqueArray allKeyTimes
        
        -- Fix: PE2 with constant rotation on Global Sequence.
        -- Import sets bind-pose AND controller keys to same value.
        -- GetRot would cancel them out to Identity, losing the rotation.
        -- Export controller keys directly instead.
        if (classOf o == BlizzParticle2) and k.count > 0 then
        (
            local isGlobalSeq = false
            try ( isGlobalSeq = getAfterORT c == #cycle ) catch()
            if isGlobalSeq then
            (
                local allConstant = true
                local firstVal = k[1].value
                for i = 2 to k.count while allConstant do
                (
                    local dot = abs(firstVal.x * k[i].value.x + firstVal.y * k[i].value.y + firstVal.z * k[i].value.z + firstVal.w * k[i].value.w)
                    if dot < 0.9999 then allConstant = false
                )
                if allConstant then
                (
                    for t in allKeyTimes do
                        append keys (KeyData t firstVal)
                    return keys
                )
            )
        )
        
        case (classOf c) of
        (
            tcb_rotation: 
            (
                local useOriginal = false
                
                local tcbLookup = Dictionary #string
                for j = 1 to c.keys.count do
                (
                    local tKey = c.keys[j].time as string
                    tcbLookup[tKey] = #(
                        (c.keys[j].tension - 25.0) / 25.0,
                        (c.keys[j].continuity - 25.0) / 25.0,
                        (c.keys[j].bias - 25.0) / 25.0
                    )
                )
                
                try
                (
                    local storedData = getAppData o 2834
                    if storedData != undefined and storedData.count > 15 then
                    (
                        local lines = filterString storedData "\n"
                        if lines.count > 2 and lines[1] == "HERMITE_KEYS" then
                        (
                            local valsLine = lines[2]
                            if (findString valsLine "TCB_VALS:") == 1 then
                            (
                                local valsStr = substring valsLine 10 -1
                                local valParts = filterString valsStr "|"
                                
                                local storedByTime = Dictionary #string
                                for vp in valParts do
                                (
                                    local colonPos = findString vp ":"
                                    if colonPos != undefined then
                                    (
                                        local timeStr = substring vp 1 (colonPos - 1)
                                        local quatStr = substring vp (colonPos + 1) -1
                                        local vComps = filterString quatStr ","
                                        if vComps.count == 4 then
                                        (
                                            local sv = quat (vComps[1] as float) (vComps[2] as float) (vComps[3] as float) (vComps[4] as float)
                                            storedByTime[timeStr] = sv
                                        )
                                    )
                                )
                                
                                local editedKeyTimes = #()
                                local curKeys = c.keys
                                for ci = 1 to curKeys.count do
                                (
                                    local curTime = curKeys[ci].time
                                    local curTimeStr = (curTime as integer) as string
                                    
                                    if hasDictValue storedByTime curTimeStr then
                                    (
                                        local sv = storedByTime[curTimeStr]
                                        local cv = curKeys[ci].value as quat
                                        local dot = abs(sv.x*cv.x + sv.y*cv.y + sv.z*cv.z + sv.w*cv.w)
                                        if dot < 0.9999 then
                                            append editedKeyTimes curTime
                                    )
                                    else
                                    (
                                        append editedKeyTimes curTime
                                    )
                                )
                                
                                local editedSeqIndices = #()
                                if this.Sequences.count > 0 then
                                (
                                    for si = 1 to this.Sequences.count do
                                    (
                                        local seq = this.Sequences[si]
                                        if seq != undefined then
                                        (
                                            local seqEdited = false
                                            for et in editedKeyTimes while not seqEdited do
                                            (
                                                if et >= seq.start and et <= seq.end then
                                                    seqEdited = true
                                            )
                                            if seqEdited then append editedSeqIndices si
                                        )
                                    )
                                )
                                
                                local origKeys = #()
                                for li = 3 to lines.count do
                                (
                                    local parts = filterString lines[li] ";"
                                    if parts.count >= 4 then
                                    (
                                        local ticks = parts[1] as integer
                                        local kTime = ticks as time
                                        local vp2 = filterString parts[2] ","
                                        local ip = filterString parts[3] ","
                                        local op = filterString parts[4] ","
                                        if vp2.count == 4 and ip.count == 4 and op.count == 4 then
                                        (
                                            local v = quat (vp2[1] as float) (vp2[2] as float) (vp2[3] as float) (vp2[4] as float)
                                            local it = quat (ip[1] as float) (ip[2] as float) (ip[3] as float) (ip[4] as float)
                                            local ot = quat (op[1] as float) (op[2] as float) (op[3] as float) (op[4] as float)
                                            append origKeys (SmoothKeyData kTime v it ot)
                                        )
                                    )
                                )
                                
                                if origKeys.count > 0 then
                                (
                                    if editedSeqIndices.count == 0 then
                                    (
                                        -- getKeysOnSequences fuer korrekte Unit-Konvertierung verwenden
                                        local origKeysBySeq = ::Utils.getKeysOnSequences origKeys this.Sequences inclusive:true
                                        local missingSeqIndices = #()
                                        for si = 1 to this.Sequences.count do
                                        (
                                            if origKeysBySeq[si].count == 0 then
                                                append missingSeqIndices si
                                        )
                                        
                                        if missingSeqIndices.count == 0 then
                                        (
                                            -- Alle Sequences abgedeckt: originals direkt verwenden
                                            keys = origKeys
                                            useOriginal = true
                                        )
                                        else
                                        (
                                            -- Fehlende Sequences resamplen und mit originals mergen
                                            local resampledKeys = #()
                                            for si in missingSeqIndices do
                                            (
                                                local seq = this.Sequences[si]
                                                if seq == undefined then continue
                                                local rangeTimes = #()
                                                for t in allKeyTimes where t >= seq.start and t <= seq.end do
                                                    append rangeTimes t
                                                local rangeKeys2 = #()
                                                -- prevQuat mit letztem origKey vor dieser Sequence initialisieren
                                                local prevQuat = undefined
                                                for ok in origKeys where ok.time < seq.start do
                                                    prevQuat = ok.value
                                                for t in rangeTimes do
                                                (
                                                    local v = this.GetRot t o r pr hasParent
                                                    if prevQuat != undefined and (::Utils.DotQuat prevQuat v) < 0 do v = -v
                                                    prevQuat = v
                                                    local t_val = 0.0; local c_val = 0.0; local b_val = 0.0
                                                    local tKey = t as string
                                                    if hasDictValue tcbLookup tKey then
                                                    (
                                                        local tcb = tcbLookup[tKey]
                                                        t_val = tcb[1]; c_val = tcb[2]; b_val = tcb[3]
                                                    )
                                                    append rangeKeys2 (TCBKeys t v t_val c_val b_val)
                                                )
                                                if rangeKeys2.count > 1 then
                                                (
                                                    for i = 1 to rangeKeys2.count do
                                                    (
                                                        local tans
                                                        if i == 1 then tans = this.ComputeTangents undefined rangeKeys2[i] rangeKeys2[i+1]
                                                        else if i == rangeKeys2.count then tans = this.ComputeTangents rangeKeys2[i-1] rangeKeys2[i] undefined
                                                        else tans = this.ComputeTangents rangeKeys2[i-1] rangeKeys2[i] rangeKeys2[i+1]
                                                        append resampledKeys (SmoothKeyData rangeKeys2[i].time rangeKeys2[i].value tans.val1 tans.val2)
                                                    )
                                                )
                                                else if rangeKeys2.count == 1 then
                                                    append resampledKeys (SmoothKeyData rangeKeys2[1].time rangeKeys2[1].value rangeKeys2[1].value rangeKeys2[1].value)
                                            )
                                            keys = #()
                                            join keys origKeys
                                            join keys resampledKeys
                                            fn sortByTime a b = (if a.time < b.time then -1 else if a.time > b.time then 1 else 0)
                                            qsort keys sortByTime
                                            useOriginal = true
                                        )
                                    )
                                    else
                                    (
                                        -- Per-sequence hybrid: originals + resampled
                                        local editedRanges = #()
                                        for si in editedSeqIndices do
                                        (
                                            local seq = this.Sequences[si]
                                            if seq != undefined then
                                                append editedRanges #(seq.start, seq.end)
                                        )
                                        
                                        fn isInEditedRange t ranges =
                                        (
                                            for rng in ranges do
                                                if t >= rng[1] and t <= rng[2] then return true
                                            false
                                        )
                                        
                                        local protectedKeys = #()
                                        for ok in origKeys do
                                        (
                                            if not (isInEditedRange ok.time editedRanges) then
                                                append protectedKeys ok
                                        )
                                        
                                        local resampledKeys = #()
                                        for er in editedRanges do
                                        (
                                            local rangeTimes = #()
                                            for t in allKeyTimes do
                                            (
                                                if t >= er[1] and t <= er[2] then
                                                    append rangeTimes t
                                            )
                                            
                                            local rangeKeys2 = #()
                                            local prevQuat = undefined
                                            
                                            for t in rangeTimes do
                                            (
                                                local v = this.GetRot t o r pr hasParent
                                                if prevQuat != undefined then
                                                (
                                                    if (::Utils.DotQuat prevQuat v) < 0 then
                                                        v = -v
                                                )
                                                prevQuat = v
                                                
                                                local t_val = 0.0
                                                local c_val = 0.0
                                                local b_val = 0.0
                                                
                                                local tKey = t as string
                                                if hasDictValue tcbLookup tKey then
                                                (
                                                    local tcb = tcbLookup[tKey]
                                                    t_val = tcb[1]
                                                    c_val = tcb[2]
                                                    b_val = tcb[3]
                                                )
                                                
                                                append rangeKeys2 (TCBKeys t v t_val c_val b_val)
                                            )
                                            
                                            if rangeKeys2.count > 1 then
                                            (
                                                for i = 1 to rangeKeys2.count do
                                                (
                                                    if i != 1 then
                                                        if (::Utils.CheckLock rangeKeys2[i-1].value rangeKeys2[i].value) then
                                                            ::ErrorReporter.reportError "TCB" o.name rangeKeys2[i].time
                                                    
                                                    local tans
                                                    if i == 1 then tans = this.ComputeTangents undefined rangeKeys2[i] rangeKeys2[i+1]
                                                    else if i == rangeKeys2.count then tans = this.ComputeTangents rangeKeys2[i-1] rangeKeys2[i] undefined
                                                    else tans = this.ComputeTangents rangeKeys2[i-1] rangeKeys2[i] rangeKeys2[i+1]
                                                    
                                                    append resampledKeys (SmoothKeyData rangeKeys2[i].time rangeKeys2[i].value tans.val1 tans.val2)
                                                )
                                            )
                                            else if rangeKeys2.count == 1 then
                                            (
                                                append resampledKeys (SmoothKeyData rangeKeys2[1].time rangeKeys2[1].value rangeKeys2[1].value rangeKeys2[1].value)
                                            )
                                        )
                                        
                                        keys = #()
                                        join keys protectedKeys
                                        join keys resampledKeys
                                        
                                        fn sortByTime a b = (if a.time < b.time then -1 else if a.time > b.time then 1 else 0)
                                        qsort keys sortByTime
                                        
                                        useOriginal = true
                                    )
                                )
                                
                                try
                                (
                                    if ::HermiteEditedSequences == undefined then
                                        ::HermiteEditedSequences = Dictionary #string
                                    ::HermiteEditedSequences[o.name] = editedSeqIndices
                                )
                                catch()
                            )
                        )
                    )
                )
                catch()
                
                -- Fallback: full TCB resampling
                if not useOriginal then
                (
                    local prevQuat = undefined
                    
                    for i = 1 to allKeyTimes.count do
                    (
                        local t = allKeyTimes[i]
                        local v = this.GetRot t o r pr hasParent
                        
                        if prevQuat != undefined then
                        (
                            if (::Utils.DotQuat prevQuat v) < 0 then
                                v = -v
                        )
                        prevQuat = v
                        
                        local t_val = 0.0
                        local c_val = 0.0
                        local b_val = 0.0
                        
                        local tKey = t as string
                        if hasDictValue tcbLookup tKey then
                        (
                            local tcb = tcbLookup[tKey]
                            t_val = tcb[1]
                            c_val = tcb[2]
                            b_val = tcb[3]
                        )
                        
                        append keys2 (TCBKeys t v t_val c_val b_val)
                    )
                    
                    if keys2.count > 1 then
                    (
                        keys[keys2.count] = undefined
                        for i = 1 to keys2.count do
                        (
                            if i != 1 then
                                if (::Utils.CheckLock keys2[i-1].value keys2[i].value) then
                                    ::ErrorReporter.reportError "TCB" o.name keys2[i].time
                            
                            if i == 1 then tangents = this.ComputeTangents undefined keys2[i] keys2[i+1] else \
                            if i == keys2.count then tangents = this.ComputeTangents keys2[i-1] keys2[i] undefined else \
                            tangents = this.ComputeTangents keys2[i-1] keys2[i] keys2[i+1]
                            
                            keys[i] = (SmoothKeyData keys2[i].time keys2[i].value tangents.val1 tangents.val2)
                        )
                    )
                    else if keys2.count == 1 then keys[1] = (SmoothKeyData keys2[1].time keys2[1].value keys2[1].value keys2[1].value)
                )
            )
            
            bezier_rotation: 
            ( 
                local prevQuat = undefined
                
                for t in allKeyTimes do
                (
                    local v = this.GetRot t o r pr hasParent
                    
                    if prevQuat != undefined then
                    (
                        if (::Utils.DotQuat prevQuat v) < 0 then
                            v = -v
                    )
                    prevQuat = v
                    
                    append keys (SmoothKeyData t v undefined undefined)
                )
                
                if keys.count > 1 then
                (
                    for k = 1 to (keys.count) do
                    (
                        if k != 1 then
                        (
                            if (::Utils.CheckLock keys[k-1].value keys[k].value) == true then
                                ::ErrorReporter.reportError "TCB" o.name keys[k].time
                        )
                        if k == 1 then tangent = (this.GetTangentSmooth keys[k].value keys[k].value keys[k+1].value) else
                        (
                            if k == keys.count then tangent = (this.GetTangentSmooth keys[k-1].value keys[k].value keys[k-1].value) else \
                            tangent = (this.GetTangentSmooth keys[k-1].value keys[k].value keys[k+1].value)
                        )
                        keys[k].intan = tangent
                        keys[k].outtan = tangent
                    )
                )
                else if keys.count == 1 then
                (
                    keys[1].intan = keys[1].value
                    keys[1].outtan = keys[1].value
                )
            )
            
            default: 
            (
                local prevQuat = undefined
                
                for t in allKeyTimes do
                (
                    local v = this.GetRot t o r pr hasParent
                    
                    if prevQuat != undefined then
                    (
                        if (::Utils.DotQuat prevQuat v) < 0 then
                            v = -v
                    )
                    prevQuat = v
                    
                    append keys (KeyData t v)
                )
            )
        )
        keys
    ),
    
    fn RotationType controller =
    (
        local type
        case (classOf controller) of
        (
            bezier_rotation: type = #Hermite
            tcb_rotation: type = #Hermite
            linear_rotation: type = #Linear
            default: type = #NonInterp
        )
        type
    ),
    
    fn RotationKeysBiped o =
    (
        local keys = #()
        local keys2 = #()
        local tangents
        
        -- Eigene Controller-Keys holen
        local k
        if (hasProperty o.transform.controller "turning") then
            k = o.transform.controller.turning.controller.keys
        else
            k = o.transform.controller.keys
        
        -- FIX: Referenz-Rotation bei Frame 0 lesen
        local r
        at time 0f r = (biped.getTransform o #rotation)
        
        local pr = (quat 0 0 0 1)
        if o.parent != undefined then
        (
            if classOf o.parent == Biped_Object then
                at time 0f pr = (biped.getTransform o.parent #rotation)
            else
                at time 0f pr = (inverse o.parent.rotation)
        )
        
        -- FIX: Sammle ALLE relevanten Key-Zeiten (eigene + Parent-Chain + Root)
        local allTimes = #()
        
        -- A) Eigene Key-Zeiten
        for i = 1 to k.count do
            appendIfUnique allTimes k[i].time
        
        -- B) Key-Zeiten aus der gesamten Parent-Chain
        local parent = o.parent
        while parent != undefined and classOf parent == Biped_Object do
        (
            local parentCtrl = parent.transform.controller
            local parentKeys = undefined
            
            if hasProperty parentCtrl "vertical" then
            (
                try
                (
                    local vk = parentCtrl.vertical.controller.keys
                    if vk != undefined then
                        for pk in vk do appendIfUnique allTimes pk.time
                ) catch ()
                try
                (
                    local hk = parentCtrl.horizontal.controller.keys
                    if hk != undefined then
                        for pk in hk do appendIfUnique allTimes pk.time
                ) catch ()
            )
            
            if hasProperty parentCtrl "turning" then
                try ( parentKeys = parentCtrl.turning.controller.keys ) catch ()
            else
                try ( parentKeys = parentCtrl.keys ) catch ()
            
            if parentKeys != undefined then
                for pk in parentKeys do appendIfUnique allTimes pk.time
            
            parent = parent.parent
        )
        
        sort allTimes
        
        -- FIX: Zwischen-Samples alle 2 Frames einfuegen
        local step = 2
        local filledTimes = #()
        for i = 1 to allTimes.count do
        (
            appendIfUnique filledTimes allTimes[i]
            if i < allTimes.count then
            (
                local t1 = allTimes[i].frame as integer
                local t2 = allTimes[i + 1].frame as integer
                if (t2 - t1) > step then
                (
                    local t = t1 + step
                    while t < t2 do
                    (
                        appendIfUnique filledTimes (t as time)
                        t += step
                    )
                )
            )
        )
        allTimes = filledTimes
        
        -- Eigene Key-Zeiten Lookup
        local ownKeyTimes = #()
        for i = 1 to k.count do append ownKeyTimes k[i].time
        
        -- Sample Rotation an allen Zeiten
        for t in allTimes do
        (
            local v = this.GetRotBiped t o r pr
            
            local tension = 0.0
            local continuity = 0.0
            local bias = 0.0
            
            local ownIdx = findItem ownKeyTimes t
            if ownIdx != 0 then
            (
                local biped_k
                if (hasProperty o.transform.controller "turning") then
                    biped_k = biped.getKey o.transform.controller.turning.controller ownIdx
                else
                    biped_k = biped.getKey o.transform.controller ownIdx
                
                tension = (biped_k.tension - 25.0) / 25.0
                continuity = (biped_k.continuity - 25.0) / 25.0
                bias = (biped_k.bias - 25.0) / 25.0
            )
            
            append keys2 (TCBKeys t v tension continuity bias)
        )
        
        -- Tangenten berechnen
        if keys2.count > 1 then
        (
            for i = 1 to keys2.count do
            (
                if i != 1 then
                    if (::Utils.CheckLock keys2[i-1].value keys2[i].value) then
                        ::ErrorReporter.reportError "TCB" o.name keys2[i].time
                
                if i == 1 then tangents = this.ComputeTangents undefined keys2[i] keys2[i+1] else \
                if i == keys2.count then tangents = this.ComputeTangents keys2[i-1] keys2[i] undefined else \
                tangents = this.ComputeTangents keys2[i-1] keys2[i] keys2[i+1]
                
                append keys (SmoothKeyData keys2[i].time keys2[i].value tangents.val1 tangents.val2)
            )
        )
        else if keys2.count == 1 then append keys (SmoothKeyData keys2[1].time keys2[1].value keys2[1].value keys2[1].value)
        keys
    ),
    
    fn RotationKeysHybrid o =
    (
        local keys = #()
        local c = o.rotation.controller
        
        if c == undefined or not isProperty c #keys then
            return #()
        
        local k = c.keys
        if k == undefined or k.count == 0 then
            return #()
        
        -- FIX: T-Pose Korrektur fuer Biped-Parents
        -- "in coordsys parent" berechnet relativ zum 90-Grad rotierten Biped,
        -- aber im MDX ist der Biped bei Frame 0 Identity. Wir muessen das
        -- Koordinatensystem des Parents kompensieren.
        local isBipedParent = o.parent != undefined and classOf o.parent == Biped_Object
        local bipedParentTPose = if isBipedParent then (at time 0f (biped.getTransform o.parent #rotation)) else (quat 0 0 0 1)
        local invTPose = inverse bipedParentTPose
        
        local localRotBase = at time 0f (in coordsys parent o.rotation)
        local r = inverse localRotBase
        
        case (classOf c) of
        (
            tcb_rotation:
            (
                local keys2 = #()
                for i = 1 to k.count do
                (
                    local v = at time k[i].time (in coordsys parent o.rotation) * r
                    if isBipedParent then v = invTPose * v * bipedParentTPose
                    keys2[i] = (TCBKeys k[i].time v ((k[i].tension - 25.0)/25.0) ((k[i].continuity - 25.0)/25.0) ((k[i].bias - 25.0)/25.0))
                )
                
                if keys2.count > 1 then
                (
                    keys[keys2.count] = undefined
                    for i = 1 to keys2.count do
                    (
                        local tangents
                        if i == 1 then tangents = this.ComputeTangents undefined keys2[i] keys2[i+1]
                        else if i == keys2.count then tangents = this.ComputeTangents keys2[i-1] keys2[i] undefined
                        else tangents = this.ComputeTangents keys2[i-1] keys2[i] keys2[i+1]
                        
                        keys[i] = (SmoothKeyData keys2[i].time keys2[i].value tangents.val1 tangents.val2)
                    )
                )
                else if keys2.count == 1 then
                (
                    keys[1] = (SmoothKeyData keys2[1].time keys2[1].value keys2[1].value keys2[1].value)
                )
            )
            
            bezier_rotation:
            (
                local tangent = quat 0 0 0 1
                keys[k.count] = undefined
                
                for i = 1 to k.count do
                (
                    local v = at time k[i].time (in coordsys parent o.rotation) * r
                    if isBipedParent then v = invTPose * v * bipedParentTPose
                    keys[i] = (SmoothKeyData k[i].time v undefined undefined)
                )
                
                if keys.count > 1 then
                (
                    for i = 1 to keys.count do
                    (
                        if i == 1 then tangent = this.GetTangentSmooth keys[i].value keys[i].value keys[i+1].value
                        else if i == keys.count then tangent = this.GetTangentSmooth keys[i-1].value keys[i].value keys[i-1].value
                        else tangent = this.GetTangentSmooth keys[i-1].value keys[i].value keys[i+1].value
                        
                        keys[i].intan = tangent
                        keys[i].outtan = tangent
                    )
                )
                else
                (
                    keys[1].intan = keys[1].value
                    keys[1].outtan = keys[1].value
                )
            )
            
            Euler_XYZ:
            (
                local tangent = quat 0 0 0 1
                keys[k.count] = undefined
                
                for i = 1 to k.count do
                (
                    local v = at time k[i].time (in coordsys parent o.rotation) * r
                    if isBipedParent then v = invTPose * v * bipedParentTPose
                    keys[i] = (SmoothKeyData k[i].time v undefined undefined)
                )
                
                if keys.count > 1 then
                (
                    for i = 1 to keys.count do
                    (
                        if i == 1 then tangent = this.GetTangentSmooth keys[i].value keys[i].value keys[i+1].value
                        else if i == keys.count then tangent = this.GetTangentSmooth keys[i-1].value keys[i].value keys[i+1].value
                        else tangent = this.GetTangentSmooth keys[i-1].value keys[i].value keys[i+1].value
                        
                        keys[i].intan = tangent
                        keys[i].outtan = tangent
                    )
                )
                else
                (
                    keys[1].intan = keys[1].value
                    keys[1].outtan = keys[1].value
                )
            )
            
            default:
            (
                for i = 1 to k.count do
                (
                    local v = at time k[i].time (in coordsys parent o.rotation) * r
                    if isBipedParent then v = invTPose * v * bipedParentTPose
                    append keys (KeyData k[i].time v)
                )
            )
        )
        
        keys
    ),
    
    fn isBipedWithNonBipedParent obj =
    (
        classOf obj == Biped_Object \
        and obj.parent != undefined \
        and classOf obj.parent != Biped_Object
    ),
    
    fn RotationKeysBipedNonBipedParent o =
    (
        local keys = #()
        local keys2 = #()
        
        local k
        if (hasProperty o.transform.controller "turning") then
            k = o.transform.controller.turning.controller.keys
        else
            k = o.transform.controller.keys
        
        -- Referenz-Rotation bei Frame 0
        local r
        at time 0f r = (biped.getTransform o #rotation)
        
        -- Parent-Rotation bei Frame 0
        local pr = (quat 0 0 0 1)
        at time 0f pr = o.parent.rotation
        local invPr = inverse pr
        
        local allTimes = #()
        for i = 1 to k.count do
            appendIfUnique allTimes k[i].time
        
        -- Parent-Keys einsammeln (Dummy/Bone kann animiert sein)
        local parent = o.parent
        if parent != undefined then
        (
            try
            (
                local pc = parent.rotation.controller
                if pc != undefined and isProperty pc #keys and pc.keys != undefined then
                    for pk in pc.keys do appendIfUnique allTimes pk.time
            ) catch ()
            try
            (
                local ppc = parent.pos.controller
                if ppc != undefined and isProperty ppc #keys and ppc.keys != undefined then
                    for pk in ppc.keys do appendIfUnique allTimes pk.time
            ) catch ()
        )
        
        sort allTimes
        
        -- Zwischen-Samples alle 2 Frames
        local step = 2
        local filledTimes = #()
        for i = 1 to allTimes.count do
        (
            appendIfUnique filledTimes allTimes[i]
            if i < allTimes.count then
            (
                local t1 = allTimes[i].frame as integer
                local t2 = allTimes[i + 1].frame as integer
                if (t2 - t1) > step then
                (
                    local t = t1 + step
                    while t < t2 do
                    (
                        appendIfUnique filledTimes (t as time)
                        t += step
                    )
                )
            )
        )
        allTimes = filledTimes
        
        local ownKeyTimes = #()
        for i = 1 to k.count do append ownKeyTimes k[i].time
        
        local prevQuat = undefined
        for t in allTimes do
        (
            -- Step 1: Biped World-Delta (gleich wie GetRotBiped Step 1)
            local bipRot = at time t (biped.getTransform o #rotation)
            local v = (inverse bipRot) * r
            
            -- Step 2: Parent-Delta rausrechnen
            -- FIX: biped.getTransform nutzt umgekehrte Quaternion-Multiplikation
            -- gegenueber Max-Standard. Bei Biped-Parents gleicht sich das aus,
            -- bei Dummy-Parents muss die Reihenfolge getauscht werden:
            -- ALT (falsch):  inv(parentRot_t) * parentRot_0
            -- NEU (richtig): parentRot_t * inv(parentRot_0)
            if o.parent != undefined then
            (
                local parentRotT = at time t o.parent.rotation
                v = v / (parentRotT * invPr)
            )
            
            if prevQuat != undefined then
                if (::Utils.DotQuat prevQuat v) < 0 then v = -v
            prevQuat = v
            
            local tension = 0.0
            local continuity = 0.0
            local bias = 0.0
            
            local ownIdx = findItem ownKeyTimes t
            if ownIdx != 0 then
            (
                local biped_k
                if (hasProperty o.transform.controller "turning") then
                    biped_k = biped.getKey o.transform.controller.turning.controller ownIdx
                else
                    biped_k = biped.getKey o.transform.controller ownIdx
                
                tension = (biped_k.tension - 25.0) / 25.0
                continuity = (biped_k.continuity - 25.0) / 25.0
                bias = (biped_k.bias - 25.0) / 25.0
            )
            
            append keys2 (TCBKeys t v tension continuity bias)
        )
        
        if keys2.count > 1 then
        (
            for i = 1 to keys2.count do
            (
                if i != 1 then
                    if (::Utils.CheckLock keys2[i-1].value keys2[i].value) then
                        ::ErrorReporter.reportError "TCB" o.name keys2[i].time
                
                local tangents
                if i == 1 then tangents = this.ComputeTangents undefined keys2[i] keys2[i+1]
                else if i == keys2.count then tangents = this.ComputeTangents keys2[i-1] keys2[i] undefined
                else tangents = this.ComputeTangents keys2[i-1] keys2[i] keys2[i+1]
                
                append keys (SmoothKeyData keys2[i].time keys2[i].value tangents.val1 tangents.val2)
            )
        )
        else if keys2.count == 1 then 
            append keys (SmoothKeyData keys2[1].time keys2[1].value keys2[1].value keys2[1].value)
        
        keys
    ),
    
    fn PositionKeysBipedNonBipedParent o =
    (
        local keys = #()
        
        if (not (hasProperty o.transform.controller "vertical" or hasProperty o.transform.controller "horizontal")) then
            return keys
        
        local worldPos0 = at time 0f o.transform.pos
        local parentPos0 = at time 0f o.parent.transform.pos
        local parentRot0 = at time 0f o.parent.transform.rotationPart as quat
        local invParentRot0 = inverse parentRot0
        local localPos0 = (worldPos0 - parentPos0) * invParentRot0
        
        local correctionQuat = parentRot0
        
        append keys (SmoothKeyData 0f [0,0,0] undefined undefined)
        
        local keys_v = o.transform.controller.vertical.controller.keys
        local keys_h = o.transform.controller.horizontal.controller.keys
        
        local keys_t = #()
        local keys_c = #()
        local keys_b = #()
        
        local i_h = 1
        local i_v = 1
        
        while (i_h <= keys_h.count) or (i_v <= keys_v.count) do
        (
            local sampleTime
            local biped_ctrl
            local biped_idx
            
            if i_h > keys_h.count then
            (
                sampleTime = keys_v[i_v].time
                biped_ctrl = o.transform.controller.vertical.controller
                biped_idx = i_v
                i_v += 1
            )
            else if i_v > keys_v.count then
            (
                sampleTime = keys_h[i_h].time
                biped_ctrl = o.transform.controller.horizontal.controller
                biped_idx = i_h
                i_h += 1
            )
            else if keys_v[i_v].time < keys_h[i_h].time then
            (
                sampleTime = keys_v[i_v].time
                biped_ctrl = o.transform.controller.vertical.controller
                biped_idx = i_v
                i_v += 1
            )
            else if keys_v[i_v].time > keys_h[i_h].time then
            (
                sampleTime = keys_h[i_h].time
                biped_ctrl = o.transform.controller.horizontal.controller
                biped_idx = i_h
                i_h += 1
            )
            else
            (
                sampleTime = keys_h[i_h].time
                biped_ctrl = o.transform.controller.vertical.controller
                biped_idx = i_v
                i_h += 1
                i_v += 1
            )
            
            local worldPosT = at time sampleTime o.transform.pos
            local parentPosT = at time sampleTime o.parent.transform.pos
            local parentRotT = at time sampleTime o.parent.transform.rotationPart as quat
            local localPosT = (worldPosT - parentPosT) * (inverse parentRotT)
            local translated_pos = (localPosT - localPos0) * correctionQuat
            
            append keys (SmoothKeyData sampleTime translated_pos undefined undefined)
            
            local bk = biped.getKey biped_ctrl biped_idx
            append keys_t bk.tension
            append keys_c bk.continuity
            append keys_b bk.bias
        )
        
        append keys (SmoothKeyData 999999999f [0,0,0] undefined undefined)
        
        for k_idx = 1 to keys_t.count do
        (
            local t = (keys_t[k_idx] - 25) / 25.0
            local c_ = (keys_c[k_idx] - 25) / 25.0
            local b = (keys_b[k_idx] - 25) / 25.0
            keys[k_idx+1].intan = (this.InTan keys[k_idx].value keys[k_idx+1].value keys[k_idx+2].value t c_ b)
            keys[k_idx+1].outtan = (this.OutTan keys[k_idx].value keys[k_idx+1].value keys[k_idx+2].value t c_ b)
        )
        
        deleteItem keys (keys.count)
        deleteItem keys 1
        
        keys
    ),



    fn GetRotIK t o r pr hp =
    (
        at time t local v = (inverse o.transform.rotationPart)*r 
        if hp then at time t v = (v/((inverse o.parent.transform.rotationPart)*pr))
        v
    ),
    
    fn RotationIKKeys o =
    (
        local tmp = #()
        local hasParent = o.parent != undefined 
        local r = at time 0f o.transform.rotationPart
        local pr = (quat 0 0 0 1)
        if hasParent then pr = at time 0f o.parent.transform.rotationPart
        if (this.isIKAffected o) then
        (
            local chains = this.getIKId o
            for i in chains do
                join tmp this.IKChainsKeys[i]
        )
        if (this.isObjectConstrained o) then join tmp (this.getObjectConstraintKeys o)
        tmp = ::Utils.ArrayMakeUnique tmp
        sort tmp
        local keys = #()
        tmp = ::Utils.getKeysOnSequences2 tmp this.Sequences inclusive:false
        local j = 1
        for i = 1 to tmp.count do
        (
            if tmp[i].count != 0 then
            (
                local s = this.Sequences[i]
                local v = this.GetRotIK s.start o r pr hasParent
                keys[j] = (KeyData s.start v)
                j += 1
                for k in tmp[i] do
                (
                    v = this.GetRotIK k o r pr hasParent
                    keys[j] = (KeyData k v)
                    j += 1
                )
                v = this.GetRotIK s.end o r pr hasParent
                keys[j] = (KeyData s.end v)
                j += 1
            )
        )
        keys
    ),
    
    -- ============================================================================
    -- SCALE FUNCTIONS
    -- ============================================================================
    
    fn getSafeParentScale parent =
    (
        for i = 1 to 3 collect (
            if abs (parent.scale[i]) < 0.0001 then 1 else parent.scale[i]
        )
    ),
    
    fn isLocalScaleConstant obj =
    (
        local k = obj.scale.controller.keys
        if k.count == 0 then true else
        (
            local first = k[1].value
            for i = 2 to k.count do
            (
                if (distance first k[i].value) > 0.01 then return false
            )
            true
        )
    ),
    
    fn getWorldScale obj =
    (
        if this.isLocalScaleConstant obj then
            point3 1 1 1
        else if obj.parent != undefined then
        (
             local localScale = obj.scale  
             local pScale = this.getSafeParentScale obj.parent
             point3 (localScale.x / pScale[1]) (localScale.y / pScale[2]) (localScale.z / pScale[3])
        )
        else
            obj.scale
    ),
    
    fn ScaleKeys o =
    (
        local keys = #()
        local c = o.scale.controller
        
        fn validateScale scaleVal minScale =
        (
            if scaleVal == undefined then return [1,1,1]
            
            local result
            try
            (
                result = point3 scaleVal.x scaleVal.y scaleVal.z
            )
            catch
            (
                return [1,1,1]
            )
            
            if abs(result.x) < minScale then result.x = minScale
            if abs(result.y) < minScale then result.y = minScale
            if abs(result.z) < minScale then result.z = minScale
            return result
        )
        
        local MIN_SCALE = 0.001
        
        -- ============================================================
        -- ScaleXYZ: Compound-Controller mit 3 Sub-Controllern
        -- Hat keine eigenen .keys - muss ueber Sub-Controller gehen
        -- ============================================================
        if (classOf c) == ScaleXYZ then
        (
            -- Key-Zeiten aus allen 3 Achsen sammeln
            local allKeyTimes = #()
            for i = 1 to 3 do
            (
                local subC = c[i].controller
                if subC != undefined and isProperty subC #keys and subC.keys != undefined then
                (
                    for k in subC.keys do
                        appendIfUnique allKeyTimes k.time
                )
            )
            sort allKeyTimes
            
            if allKeyTimes.count == 0 then return #()
            
            local baseline = validateScale (at time 0f (point3 o.scale.x o.scale.y o.scale.z)) MIN_SCALE
            
            for t in allKeyTimes do
            (
                at time t
                (
                    local v = validateScale (point3 o.scale.x o.scale.y o.scale.z) MIN_SCALE
                    local relativeScale = point3 (v.x / baseline.x) (v.y / baseline.y) (v.z / baseline.z)
                    append keys (KeyData t relativeScale)
                )
            )
            
            return keys
        )
        
        -- ============================================================
        -- Ab hier: Original-Logik fuer normale Controller
        -- ============================================================
        if c == undefined or not isProperty c #keys or c.keys == undefined or c.keys.count == 0 then
            return #()
        
        local baseline = validateScale (at time 0 this.getWorldScale o) MIN_SCALE
        
        case (classOf c) of
        (
            bezier_scale:
            (
                local keys2 = #()
                local k = c.keys
                for i = 1 to k.count do
                (
                    local t = k[i].time
                    at time t
                    (
                        local v = validateScale (this.getWorldScale o) MIN_SCALE
                        local relativeScale = point3 (v.x / baseline.x) (v.y / baseline.y) (v.z / baseline.z)
                        
                        local in1 = relativeScale
                        if i != 1 then
                        (
                            local inTime = k[i-1].time + 2.0*(t - k[i-1].time)/3.0
                            at time inTime
                            (
                                local vIn = validateScale (this.getWorldScale o) MIN_SCALE
                                in1 = point3 (vIn.x / baseline.x) (vIn.y / baseline.y) (vIn.z / baseline.z)
                            )
                        )
                        
                        local out1 = relativeScale
                        if i != k.count then
                        (
                            local outTime = t + (k[i+1].time - t)/3.0
                            at time outTime
                            (
                                local vOut = validateScale (this.getWorldScale o) MIN_SCALE
                                out1 = point3 (vOut.x / baseline.x) (vOut.y / baseline.y) (vOut.z / baseline.z)
                            )
                        )
                        keys2[i] = (SmoothKeyData t relativeScale in1 out1)
                    )
                )
                keys = this.ProcessBezier keys2
            );
            
            tcb_scale:
            (
                append keys (SmoothKeyData 0f [1,1,1] undefined undefined)
                local k = c.keys
                for i = 1 to k.count do
                (
                    local t = k[i].time
                    at time t
                    (
                        local v = validateScale (this.getWorldScale o) MIN_SCALE
                        local relativeScale = point3 (v.x / baseline.x) (v.y / baseline.y) (v.z / baseline.z)
                        append keys (SmoothKeyData t relativeScale undefined undefined)
                    )
                )
                append keys (SmoothKeyData 999999999f [1,1,1] undefined undefined)
                
                for i = 1 to k.count do
                (
                    local tensionVal = (k[i].tension - 25)/25.0
                    local continuityVal = (k[i].continuity - 25)/25.0
                    local biasVal = (k[i].bias - 25)/25.0
                    keys[i+1].intan = this.InTan keys[i].value keys[i+1].value keys[i+2].value tensionVal continuityVal biasVal
                    keys[i+1].outtan = this.OutTan keys[i].value keys[i+1].value keys[i+2].value tensionVal continuityVal biasVal
                )
                deleteItem keys (keys.count)
                deleteItem keys 1
            );
            
            default: 
            (
                local k = c.keys
                for i = 1 to k.count do
                (
                    local t = k[i].time
                    at time t
                    (
                        local v = validateScale (this.getWorldScale o) MIN_SCALE
                        local relativeScale = point3 (v.x / baseline.x) (v.y / baseline.y) (v.z / baseline.z)
                        append keys (KeyData t relativeScale)
                    )
                )
            )
        )
        keys
    ),
    
    fn ScaleType controller =
    (
        local type
        case (classOf controller) of
        (
            bezier_scale: 
            (
                if (this.controllerIsNonInterp controller) then
                    type = #NonInterp
                else
                    type = #Bezier
            )
            tcb_scale: type = #Hermite
            linear_scale: type = #Linear
            ScaleXYZ: 
            (
                -- Sub-Controller-Typ pruefen fuer korrekte Interpolation
                local subC = controller[1].controller
                if subC != undefined then
                (
                    case (classOf subC) of
                    (
                        bezier_float: 
                        (
                            if (this.controllerIsNonInterp subC) then
                                type = #NonInterp
                            else
                                type = #Bezier
                        )
                        linear_float: type = #Linear
                        default: type = #NonInterp
                    )
                )
                else type = #NonInterp
            )
            default: type = #NonInterp
        )
        type
    ),
    
    fn ScaleKeysHybrid o =
    (
        this.ScaleKeys o
    ),
    
    -- ============================================================================
    -- FLOAT KEYS FUNCTIONS
    -- ============================================================================
    
    fn FloatKeys c =
    (
        if c == undefined or not isProperty c #keys or c.keys == undefined then
        (
            return #()
        )

        local k = c.keys
        if k.count == 0 then return #()
        
        local keys = #()

        case (classOf c) of
        (
            bezier_float: 
            (
                local keys2 = #()
                keys2[k.count] = undefined 
                for i = 1 to k.count do
                (
                    at time k[i].time local v = c.value
                    local in1, out1
                    if i != 1 then
                        at time (k[i-1].time + 2.0*(k[i].time - k[i-1].time)/3.0) in1 = c.value
                    else
                        in1 = v
                    if i != k.count then
                        at time (k[i].time + (k[i+1].time - k[i].time)/3.0) out1 = c.value
                    else
                        out1 = v
                    keys2[i] = (SmoothKeyData k[i].time v in1 out1)
                )
                keys = this.ProcessBezier keys2
            )

            tcb_float: 
            (
                local keyCount = k.count
                
                local temp_keys = #()
                temp_keys[keyCount + 2] = undefined

                temp_keys[1] = (SmoothKeyData 0f 0.0 undefined undefined)
                for i = 1 to keyCount do
                (
                    temp_keys[i+1] = (SmoothKeyData k[i].time k[i].value undefined undefined)
                )
                temp_keys[keyCount + 2] = (SmoothKeyData 999999999f 0.0 undefined undefined)

                if temp_keys.count >= 3 and keyCount > 0 then
                (
                    for k_idx = 1 to keyCount do
                    (
                        local t = (k[k_idx].tension - 25.0) / 25.0
                        local c_ = (k[k_idx].continuity - 25.0) / 25.0
                        local b = (k[k_idx].bias - 25.0) / 25.0
                        temp_keys[k_idx+1].intan = (this.InTan temp_keys[k_idx].value temp_keys[k_idx+1].value temp_keys[k_idx+2].value t c_ b)
                        temp_keys[k_idx+1].outtan = (this.OutTan temp_keys[k_idx].value temp_keys[k_idx+1].value temp_keys[k_idx+2].value t c_ b)
                    )
                )
                
                keys = for i = 2 to (temp_keys.count - 1) collect temp_keys[i]
            )

            default: 
            (
                keys.count = k.count 
                for i = 1 to k.count do keys[i] = (KeyData k[i].time k[i].value)
            )
        )
        keys
    ),
    
    fn FloatType controller =
    (
        local type
        case (classOf controller) of
        (
            bezier_float: 
            (
                if (this.controllerIsNonInterp controller) then
                    type = #NonInterp
                else
                    type = #Bezier
            )
            tcb_float: type = #Hermite
            linear_float: type = #Linear
            default: type = #NonInterp
        )
        type
    ),
    
    -- ============================================================================
    -- VISIBILITY KEYS FUNCTIONS
    -- ============================================================================
    
    fn VisibilityKeys obj =
    (
        local keys = #()
        local c = obj.visibility.controller
        case (classOf c) of
        (
            On_Off:
            (
                if this.Sequences[1] != undefined then
                (
                    local contkeys = c.keys
                    local i = ::Utils.ArrayFindItemEx this.Sequences contkeys[1].time (fn tmp a b = b < a.end)
                    if (i != 0) then
                    (
                        local top = this.Sequences.count
                        local j = 1
                        local lastKey = contkeys.count
                        while (i <= top) do
                        (
                            local state = false
                            local end = this.Sequences[i].end, start = this.Sequences[i].start
                            local vis
                            at time start vis = obj.visibility
                            append keys (KeyData start vis)
                            j = ::Utils.ArrayFindItemEx contkeys end (fn tmp a b = a.time < b)
                            while (j != 0 and j <= lastKey and contkeys[j].time < end) do
                            (
                                if (contkeys[j].time > start) then
                                (
                                    state = true
                                    if vis then vis = false else vis = true
                                    append keys (KeyData contkeys[j].time vis)
                                )
                                j = j +1
                            )
                            if j <= lastKey and contkeys[j].time == end then state = true
                            if state then
                            (
                                at time end vis = obj.visibility
                                append keys (KeyData end vis)
                            )
                            i = i + 1
                        )
                        keys = ::Utils.ArrayMap keys (fn tmp a = (KeyData a.time (::Utils.B2I a.value)))
                    )
                )
            )
            default: (
                keys = this.FloatKeys c
            )
        )
        keys
    ),
    
    fn VisibilityType controller = this.FloatType controller,
    
    -- ============================================================================
    -- COLOR KEYS FUNCTIONS
    -- ============================================================================
    
    fn ColorKeys c =
    (
        if c == undefined or not isProperty c #keys or c.keys == undefined then
        (
            return #()
        )

        local keys = #()
        local k = c.keys 

        if k.count == 0 then
        (
            return #()
        )

        case (classOf c) of
        (
            bezier_color: (
                local keys2 = #()
                keys2[k.count] = undefined
                for i = 1 to k.count do
                (
                    at time k[i].time local v = k[i].value 
                    local in1, out1
                    if i != 1 then
                        at time (k[i-1].time + 2.0*(k[i].time - k[i-1].time)/3.0) in1 = c.value
                    else
                        in1 = v
                    if i != k.count then 
                        at time (k[i].time + (k[i+1].time - k[i].time)/3.0) out1 = c.value
                    else
                        out1 = v
                    keys2[i] = (SmoothKeyData k[i].time v in1 out1)
                )
                keys = this.ProcessBezier keys2
            )

            bezier_position: (
                local keys2 = #()
                keys2[k.count] = undefined
                for i = 1 to k.count do
                (
                    at time k[i].time local v = k[i].value 
                    local in1, out1
                    if i != 1 then
                        at time (k[i-1].time + 2.0*(k[i].time - k[i-1].time)/3.0) in1 = c.value
                    else
                        in1 = v
                    if i != k.count then 
                        at time (k[i].time + (k[i+1].time - k[i].time)/3.0) out1 = c.value
                    else
                        out1 = v
                    keys2[i] = (SmoothKeyData k[i].time v in1 out1)
                )
                keys = this.ProcessBezier keys2
            )

            tcb_point3: (
                append keys (SmoothKeyData 0f [0,0,0] undefined undefined)
                for key_tcb in k do
                (
                    append keys (SmoothKeyData key_tcb.time key_tcb.value undefined undefined)
                )
                append keys (SmoothKeyData 999999999f [0,0,0] undefined undefined)

                if keys.count >= 3 and k.count > 0 then
                (
                    for k_idx = 1 to k.count do
                    (
                        local t = (k[k_idx].tension - 25.0) / 25.0
                        local c_ = (k[k_idx].continuity - 25.0) / 25.0
                        local b = (k[k_idx].bias - 25.0) / 25.0
                        keys[k_idx+1].intan = (this.InTan keys[k_idx].value keys[k_idx+1].value keys[k_idx+2].value t c_ b)
                        keys[k_idx+1].outtan = (this.OutTan keys[k_idx].value keys[k_idx+1].value keys[k_idx+2].value t c_ b)
                    )
                )
                if keys.count >= 2 then
                (
                    deleteItem keys (keys.count)
                    deleteItem keys 1
                )
            )
            default: (
                for key_default in k do append keys (KeyData key_default.time key_default.value)
            )
        )
        keys
    ),
    
    fn ColorType controller =
    (
        local type
        case (classOf controller) of
        (
            bezier_color: 
            (
                if (this.controllerIsNonInterp controller) then
                    type = #NonInterp
                else
                    type = #Bezier
            )
            tcb_point3: type = #Hermite
            default: type = #Linear
        )
        type
    ),
    
    -- ============================================================================
    -- TEXTURE ANIMATION
    -- ============================================================================
    
    fn GetTextureAnimation idsList playback =
    (
        local n = 1.0 / playback
        local m = 0.0
        local result = #()
        for i in idsList do
        (
            append result (KeyData (m as time) i)
            m += n
        )
        local animData = AnimationData ()
        animData.keys = result
        animData.type = #Linear
        animData.globalSequenceId = this.LoadGlobalSequence (m as time)
        ParameterValue #Animated animData
    ),
    
    -- ============================================================================
    -- ANIMATION DATA FUNCTIONS
    -- ============================================================================
    
    fn checkStartEndKeys keys objName controllerName =
    (
        local errName = objName + "->" + controllerName
        local resampledKeys = ::Utils.getKeysOnSequences keys this.Sequences inclusive:true
        local count = this.SequencesOriginal.count
        for i = 1 to count do
        (
            local tmp = resampledKeys[i]
            local err = (tmp.count == 0 or tmp[1].time != this.Sequences[i].start)
            if tmp.count > 1 then
                err = err or (tmp[tmp.count].time != this.Sequences[i].end)
            if err then
                ::ErrorReporter.reportError "StartEndKeys" errName this.SequencesOriginal[i].name
        )
    ),
    
    fn GetAnimationData obj controller &processFunc &typeFunc checkSE:true =
    (
        local animData = AnimationData()
        
        local hasAnimationData = false
        try
        (
            animData.keys = processFunc obj
            hasAnimationData = animData.keys != undefined and animData.keys.count > 0
        )
        catch
        (
            hasAnimationData = false
        )
        
        -- Wenn keine Animation vorhanden ist, undefined zurückgeben
        if not hasAnimationData then
        (
            return undefined
        )
        
        animData.type = typeFunc controller
        
        local usesGlobalSequences = false
        try
        (
            usesGlobalSequences = getAfterORT controller == #cycle
        )
        catch()
        
        if usesGlobalSequences then 
            animData.globalSequenceId = this.LoadGlobalSequence animData.keys[animData.keys.count].time
        
        if animData.keys.count != 0 then
        (
            local s
            fn getName t = if isProperty t #name then t.name else t as string
            s = getName obj
            if checkSE and (not usesGlobalSequences) then 
                this.checkStartEndKeys animData.keys s (controller as string)
            animData
        )
        else undefined
    ),

    fn ensureBipedBoundaryKeys keys =
    (
        if keys.count == 0 or this.Sequences.count == 0 then return keys
        
        local modified = false
        local isQuat = (classOf keys[1].value == quat)
        
        for seq in this.Sequences where seq != undefined do
        (
            local hasStart = false
            local hasEnd = false
            for k in keys do
            (
                if k.time == seq.start then hasStart = true
                if k.time == seq.end then hasEnd = true
            )
            
            if not hasStart then
            (
                local prev = undefined
                local nxt = undefined
                for k in keys do
                (
                    if k.time < seq.start then prev = k
                    if k.time > seq.start and nxt == undefined then nxt = k
                )
                
                local val, tan
                if prev != undefined and nxt != undefined then
                (
                    local frac = (seq.start - prev.time) as float / (nxt.time - prev.time) as float
                    if isQuat then
                    (
                        val = slerp prev.value nxt.value frac
                        tan = val
                    )
                    else
                    (
                        val = prev.value + (nxt.value - prev.value) * frac
                        tan = (nxt.value - prev.value) * 0.5
                    )
                )
                else if prev != undefined then ( val = prev.value; tan = if isQuat then val else [0,0,0] )
                else if nxt != undefined then ( val = nxt.value; tan = if isQuat then val else [0,0,0] )
                else continue
                
                append keys (SmoothKeyData seq.start val tan tan)
                modified = true
            )
            
            if not hasEnd then
            (
                local prev = undefined
                local nxt = undefined
                for k in keys do
                (
                    if k.time < seq.end then prev = k
                    if k.time > seq.end and nxt == undefined then nxt = k
                )
                
                local val, tan
                if prev != undefined and nxt != undefined then
                (
                    local frac = (seq.end - prev.time) as float / (nxt.time - prev.time) as float
                    if isQuat then
                    (
                        val = slerp prev.value nxt.value frac
                        tan = val
                    )
                    else
                    (
                        val = prev.value + (nxt.value - prev.value) * frac
                        tan = (nxt.value - prev.value) * 0.5
                    )
                )
                else if prev != undefined then ( val = prev.value; tan = if isQuat then val else [0,0,0] )
                else if nxt != undefined then ( val = nxt.value; tan = if isQuat then val else [0,0,0] )
                else continue
                
                append keys (SmoothKeyData seq.end val tan tan)
                modified = true
            )
        )
        
        if modified then
        (
            fn sortByTime a b = (if a.time < b.time then -1 else if a.time > b.time then 1 else 0)
            qsort keys sortByTime
        )
        
        keys
    ),

    fn fixBipedLoopTangents keys isRotation:false =
    (
        if keys.count < 3 or this.Sequences.count == 0 then return keys
        
        for seq in this.Sequences where seq != undefined do
        (
            local startIdx = 0
            local endIdx = 0
            for i = 1 to keys.count do
            (
                if keys[i].time == seq.start then startIdx = i
                if keys[i].time == seq.end then endIdx = i
            )
            
            if startIdx == 0 or endIdx == 0 or startIdx >= endIdx then continue
            if (endIdx - startIdx) < 2 then continue
            
            local startKey = keys[startIdx]
            local endKey = keys[endIdx]
            
            local isLoop = false
            if isRotation then
                isLoop = abs(::Utils.DotQuat startKey.value endKey.value) > 0.9999
            else
                isLoop = length(startKey.value - endKey.value) < 0.01
            
            if not isLoop then continue
            
            local prevOfEnd = keys[endIdx - 1]
            local nextOfStart = keys[startIdx + 1]
            
            if isRotation then
            (
                local wrapNextTime = endKey.time + (nextOfStart.time - startKey.time)
                local wrapPrevTime = startKey.time - (endKey.time - prevOfEnd.time)
                
                local tangents = this.ComputeTangents \
                    (TCBKeys prevOfEnd.time prevOfEnd.value 0 0 0) \
                    (TCBKeys endKey.time endKey.value 0 0 0) \
                    (TCBKeys wrapNextTime nextOfStart.value 0 0 0)
                endKey.intan = tangents.val1
                endKey.outtan = tangents.val2
                
                tangents = this.ComputeTangents \
                    (TCBKeys wrapPrevTime prevOfEnd.value 0 0 0) \
                    (TCBKeys startKey.time startKey.value 0 0 0) \
                    (TCBKeys nextOfStart.time nextOfStart.value 0 0 0)
                startKey.intan = tangents.val1
                startKey.outtan = tangents.val2
            )
            else
            (
                endKey.intan = this.InTan prevOfEnd.value endKey.value nextOfStart.value 0 0 0
                endKey.outtan = this.OutTan prevOfEnd.value endKey.value nextOfStart.value 0 0 0
                
                startKey.intan = this.InTan prevOfEnd.value startKey.value nextOfStart.value 0 0 0
                startKey.outtan = this.OutTan prevOfEnd.value startKey.value nextOfStart.value 0 0 0
            )
        )
        
        keys
    ),
    
    fn GetAnimationDataBiped obj &processFunc checkSE:true =
    (
        local animData = AnimationData ()
        animData.keys = processFunc obj
        animData.type = #Hermite
        if (animData.keys.count != 0) then
        (
            animData.keys = this.ensureBipedBoundaryKeys animData.keys
            local isRot = (classOf animData.keys[1].value == quat)
            animData.keys = this.fixBipedLoopTangents animData.keys isRotation:isRot
            if checkSE then this.checkStartEndKeys animData.keys obj.name "Biped Controller"
            animData
        )
        else undefined
    ),
    
    -- ============================================================================
    -- PARTICLE2 ROTATION FUNCTIONS
    -- ============================================================================
    
    fn isQuatNearIdentity q threshold:0.01 =
    (
        local isIdentity = (abs q.x < threshold) and (abs q.y < threshold) and \
                           (abs q.z < threshold) and (abs (q.w - 1.0) < threshold)
        local isNegIdentity = (abs q.x < threshold) and (abs q.y < threshold) and \
                              (abs q.z < threshold) and (abs (q.w + 1.0) < threshold)
        (isIdentity or isNegIdentity)
    ),
    
    fn getParticle2Rotation obj =
    (
        local worldRot
        at time 0f worldRot = obj.rotation
        
        if obj.parent != undefined and classof obj.parent == BlizzParticle2 then
        (
            local parentRot
            at time 0f parentRot = obj.parent.rotation
            return (inverse parentRot) * worldRot
        )
        else
            return worldRot
    ),
    
    fn createParticle2RotationFix obj =
    (
        local targetRot = this.getParticle2Rotation obj
        
        if this.isQuatNearIdentity targetRot then return undefined
        
        local times = #(0f)
        if this.Sequences != undefined then
        (
            for seq in this.Sequences where seq != undefined do
            (
                if isProperty seq #start then appendIfUnique times seq.start
                if isProperty seq #end then appendIfUnique times seq.end
            )
        )
        sort times
        
        local keys = #()
        for t in times do
        (
            local rotValue = if t == 0f then (quat 0 0 0 1) else targetRot
            append keys (KeyData t rotValue)
        )
        
        local animData = AnimationData()
        animData.type = #Linear
        animData.keys = keys
        return animData
    ),
    
    fn fixAnimatedParticle2Rotation obj animData =
    (
        if animData == undefined or animData.keys == undefined or animData.keys.count == 0 then
            return animData
        
        local targetRot = this.getParticle2Rotation obj
        
        if this.isQuatNearIdentity targetRot then return animData
        
        -- Skip fix if rotation is constant (all keys identical = intentional placement)
        if animData.keys.count > 1 then
        (
            local allSame = true
            local firstVal = animData.keys[1].value
            for i = 2 to animData.keys.count while allSame do
            (
                local dot = abs(firstVal.x * animData.keys[i].value.x + firstVal.y * animData.keys[i].value.y + firstVal.z * animData.keys[i].value.z + firstVal.w * animData.keys[i].value.w)
                if dot < 0.9999 then allSame = false
            )
            if allSame then return animData
        )
        
        local rotCtrl = obj.rotation.controller
        local ctrlClass = classOf rotCtrl
        
        -- ============================================================
        -- EULER: modify original, resample, restore
        -- ============================================================
        if ctrlClass == Euler_XYZ then
        (
            local savedSubs = #()
            for i = 1 to 3 do
            (
                local sub = rotCtrl[i].controller
                local subHadKey = false
                local subSavedVal = at time 0f sub.value
                for k = 1 to sub.keys.count where sub.keys[k].time == 0f do subHadKey = true
                append savedSubs #(sub, subHadKey, subSavedVal)
            )
            
            with animate on at time 0f
            (
                rotCtrl[1].controller.value = 0.0
                rotCtrl[2].controller.value = 0.0
                rotCtrl[3].controller.value = 0.0
            )
            
            local newAnimData = this.GetAnimationData obj rotCtrl this.RotationKeys this.RotationType
            
            for i = 1 to 3 do
            (
                local info = savedSubs[i]
                with animate on at time 0f info[1].value = info[3]
                if not info[2] then
                    for k = info[1].keys.count to 1 by -1 where info[1].keys[k].time == 0f do
                        deleteKey info[1] k
            )
            
            if newAnimData != undefined and newAnimData.keys != undefined and newAnimData.keys.count > 0 then
                return newAnimData
            else
                return animData
        )
        -- ============================================================
        -- LINEAR / BEZIER: modify original, resample, restore
        -- ============================================================
        else if ctrlClass == linear_rotation or ctrlClass == bezier_rotation then
        (
            local hadKeyAtFrame0 = false
            local savedVal = undefined
            
            for k = 1 to rotCtrl.keys.count where rotCtrl.keys[k].time == 0f do
            (
                hadKeyAtFrame0 = true
                savedVal = rotCtrl.keys[k].value
            )
            if not hadKeyAtFrame0 then
                savedVal = at time 0f rotCtrl.value
            
            with animate on
            (
                local newKey = addNewKey rotCtrl 0f
                newKey.value = (quat 0 0 0 1)
            )
            
            local newAnimData = this.GetAnimationData obj rotCtrl this.RotationKeys this.RotationType
            
            if hadKeyAtFrame0 then
            (
                for k = 1 to rotCtrl.keys.count where rotCtrl.keys[k].time == 0f do
                    rotCtrl.keys[k].value = savedVal
            )
            else
            (
                for k = rotCtrl.keys.count to 1 by -1 where rotCtrl.keys[k].time == 0f do
                    deleteKey rotCtrl k
            )
            
            if newAnimData != undefined and newAnimData.keys != undefined and newAnimData.keys.count > 0 then
                return newAnimData
            else
                return animData
        )
        -- ============================================================
        -- TCB: never addNewKey (destroys interpolation).
        -- Use copy: if frame 0 key exists, change its value.
        -- If no frame 0 key, bake world-space as linear.
        -- ============================================================
        else if ctrlClass == tcb_rotation then
        (
            local tempObj = undefined
            local newAnimData = undefined
            
            try
            (
                tempObj = copy obj
                tempObj.name = "__whiteoutdex_p2_temp__"
                tempObj.isHidden = true
                
                local tempCtrl = tempObj.rotation.controller
                
                local hasFrame0Key = false
                for k = 1 to tempCtrl.keys.count where tempCtrl.keys[k].time == 0f do
                    hasFrame0Key = true
                
                if hasFrame0Key then
                (
                    -- Safe: change existing key value, no addNewKey
                    for k = 1 to tempCtrl.keys.count where tempCtrl.keys[k].time == 0f do
                        tempCtrl.keys[k].value = (quat 0 0 0 1)
                    
                    newAnimData = this.GetAnimationData tempObj tempCtrl this.RotationKeys this.RotationType
                )
                else
                (
                    -- No frame 0 key: bake world-space rotation as linear.
                    local hasParent = obj.parent != undefined and (classOf obj.parent != Uvwmap)
                    
                    local allTimes = #()
                    for k in tempCtrl.keys do appendIfUnique allTimes k.time
                    if this.Sequences != undefined then
                    (
                        for seq in this.Sequences where seq != undefined do
                        (
                            appendIfUnique allTimes seq.start
                            appendIfUnique allTimes seq.end
                        )
                    )
                    sort allTimes
                    
                    local identityRot = (quat 0 0 0 1)
                    local parentRot0 = if hasParent then (at time 0f obj.parent.transform.rotationPart) else (quat 0 0 0 1)
                    
                    local bakedKeys = #()
                    for t in allTimes do
                    (
                        local worldRotT = at time t obj.transform.rotationPart
                        local v = (inverse worldRotT) * identityRot
                        
                        if hasParent then
                        (
                            local parentRotT = at time t obj.parent.transform.rotationPart
                            v = v / ((inverse parentRotT) * parentRot0)
                        )
                        
                        append bakedKeys (KeyData t v)
                    )
                    
                    newAnimData = AnimationData()
                    newAnimData.type = #Linear
                    newAnimData.keys = bakedKeys
                )
            )
            catch
            (
                format "Warning: fixAnimatedParticle2Rotation TCB failed: %\n" (getCurrentException())
            )
            
            try (if tempObj != undefined then delete tempObj) catch()
            
            if newAnimData != undefined and newAnimData.keys != undefined and newAnimData.keys.count > 0 then
                return newAnimData
            else
                return animData
        )
        else
        (
            format "Warning: fixAnimatedParticle2Rotation skipped for unknown controller type: %\n" ctrlClass
            return animData
        )
    ),
    
    -- ============================================================================
    -- GET TRANSFORMATIONS (MAIN FUNCTION)
    -- ============================================================================
    
    fn GetTransformations obj =
    (
        local trans = TransformationData()
        local isParticle2 = (classof obj == BlizzParticle2)
        
        -- NEU: Biped mit Dummy/Bone Parent - eigene Funktionen
        if (this.isBipedWithNonBipedParent obj) then
        (
            trans.translation = this.GetAnimationDataBiped obj this.PositionKeysBipedNonBipedParent
            trans.rotation = this.GetAnimationDataBiped obj this.RotationKeysBipedNonBipedParent
        )
        else if (classOf obj == Biped_Object) then
        (
            trans.translation = this.GetAnimationDataBiped obj this.PositionKeysBiped
            trans.rotation = this.GetAnimationDataBiped obj this.RotationKeysBiped
        )
        else if (this.isLinkConstrained obj) then
        (
            local posKeys = this.LinkConstraintPositionKeys obj
            if posKeys != undefined and posKeys.count > 0 then
            (
                local posAnim = AnimationData()
                posAnim.type = #Linear
                posAnim.keys = posKeys
                this.checkStartEndKeys posKeys obj.name "Link_Constraint Position"
                trans.translation = posAnim
            )
            
            local rotKeys = this.LinkConstraintRotationKeys obj
            if rotKeys != undefined and rotKeys.count > 0 then
            (
                local rotAnim = AnimationData()
                rotAnim.type = #Linear
                rotAnim.keys = rotKeys
                this.checkStartEndKeys rotKeys obj.name "Link_Constraint Rotation"
                trans.rotation = rotAnim
            )
            
            try
            (
                if obj.scale.isAnimated and obj.scale.controller != undefined then
                    trans.scale = this.GetAnimationData obj obj.scale.controller this.ScaleKeys this.ScaleType
            )
            catch()
        )
        else if obj.parent == undefined or (classOf obj.parent != Biped_Object) then
        (
            if (try(obj.pos.isAnimated) catch(false)) then 
                trans.translation = this.GetAnimationData obj obj.pos.controller this.PositionKeys this.PositionType
            
            if ((this.isIKAffected obj) or (this.isObjectConstrained obj)) then
                trans.rotation = this.GetAnimationData obj obj.rotation.controller this.RotationIKKeys (fn tmp a = #linear)
            else if (try(obj.rotation.isAnimated) catch(false)) then 
            (
                if (this.parentIsLinkConstrained obj) then
                    trans.rotation = this.GetAnimationData obj obj.rotation.controller this.RotationKeysLinkParent (fn tmp a = #linear)
                else
                (
                    trans.rotation = this.GetAnimationData obj obj.rotation.controller this.RotationKeys this.RotationType
                    if isParticle2 and (getAppData obj 2834) == undefined then
                        trans.rotation = this.fixAnimatedParticle2Rotation obj trans.rotation
                )
            )
            else if isParticle2 then
                trans.rotation = this.createParticle2RotationFix obj
            
            if (try(obj.scale.isAnimated) catch(false)) then 
                trans.scale = this.GetAnimationData obj obj.scale.controller this.ScaleKeys this.ScaleType
        )
        else
        (
            if (try(obj.pos.isAnimated) catch(false)) then 
                trans.translation = this.GetAnimationData obj obj.pos.controller this.PositionKeysHybrid this.PositionType
            
            if (try(obj.rotation.isAnimated) catch(false)) then 
            (
                trans.rotation = this.GetAnimationData obj obj.rotation.controller this.RotationKeysHybrid this.RotationType
                if isParticle2 and (getAppData obj 2834) == undefined then
                    trans.rotation = this.fixAnimatedParticle2Rotation obj trans.rotation
            )
            else if isParticle2 then
                trans.rotation = this.createParticle2RotationFix obj
            
            if (try(obj.scale.isAnimated) catch(false)) then 
                trans.scale = this.GetAnimationData obj obj.scale.controller this.ScaleKeysHybrid this.ScaleType
        )
        
        trans
    ),
    
    -- ============================================================================
    -- PARAMETER PROCESSING FUNCTIONS
    -- ============================================================================
    
    fn _ProcessSingleValue controller val &ProcessFunc &TypeFunc =
    (
        local ParamValue = ParameterValue ()
        if (controller != undefined) then
        (
            ParamValue.type = #Animated
            ParamValue.data = this.GetAnimationData controller controller ProcessFunc TypeFunc checkSE:false
            if (ParamValue.data == undefined) then
            (
                ParamValue.type = #Static
                ParamValue.data = val
            )
        )
        else
        (
            ParamValue.type = #Static
            ParamValue.data = val
        )
        ParamValue
    ),
    
    fn FloatParameterProcess controller val =
    (
        local ParamValue = ParameterValue()
        
        local isReallyAnimated = false
        
        if controller != undefined then
        (
            if isProperty controller #keys then
            (
                if controller.keys != undefined then
                (
                    if controller.keys.count > 0 then
                    (
                        isReallyAnimated = true
                    )
                )
            )
        )
        
        if isReallyAnimated then
        (
            ParamValue.type = #Animated
            ParamValue.data = this.GetAnimationData controller controller this.FloatKeys this.FloatType checkSE:false
            
            if ParamValue.data == undefined or ParamValue.data.keys == undefined or ParamValue.data.keys.count == 0 then
            (
                ParamValue.type = #Static
                ParamValue.data = val
            )
        )
        else
        (
            ParamValue.type = #Static
            ParamValue.data = val
        )
        
        ParamValue
    ),
    
    fn ColorParameterProcess controller val =
    (
        local ParamValue = ParameterValue()
        
        local isReallyAnimated = false
        
        if controller != undefined then
        (
            if isProperty controller #keys then
            (
                if controller.keys != undefined then
                (
                    if controller.keys.count > 0 then
                    (
                        isReallyAnimated = true
                    )
                )
            )
        )
        
        if isReallyAnimated then
        (
            ParamValue.type = #Animated
            ParamValue.data = this.GetAnimationData controller controller this.ColorKeys this.ColorType checkSE:false
            
            if ParamValue.data == undefined or ParamValue.data.keys == undefined or ParamValue.data.keys.count == 0 then
            (
                ParamValue.type = #Static
                ParamValue.data = val
            )
        )
        else
        (
            ParamValue.type = #Static
            ParamValue.data = val
        )
        
        ParamValue
    ),
    
    fn VisibilityParameterProcess obj =
    (
        local controller = obj.visibility.controller
        local ParamValue = ParameterValue()
        
        if controller != undefined then
        (
            if isProperty controller #keys and controller.keys != undefined and controller.keys.count > 0 then
            (
                ParamValue.type = #Animated
                ParamValue.data = this.GetAnimationData obj controller this.VisibilityKeys this.VisibilityType checkSE:(classof controller != ON_OFF)
                if ParamValue.data == undefined then ParamValue = undefined
            )
            else
            (
                ParamValue = undefined
            )
        )
        else 
        (
            ParamValue = undefined
        )
        ParamValue
    ),
    
    fn EventProcess obj =
    (
        local keys
        -- Read event keys from keyList custom attribute
        if isProperty obj.baseObject #keyList and obj.baseObject.keyList.count > 0 then
        (
            keys = #()
            for k in obj.baseObject.keyList do append keys (k as time)
            if (keys.count == 0) then keys = undefined
        )
        -- Fallback: check legacy Note Tracks for old scenes
        else if hasNoteTracks obj then
        (
            local track = getNoteTrack obj 1
            keys = #()
            for k in track.keys do append keys k.time
            if (keys.count == 0) then keys = undefined
            -- Migrate: copy to keyList and remove note track
            if keys != undefined and isProperty obj.baseObject #keyList then
            (
                for k in keys do
                (
                    local frameNum = if classOf k == Time then (k.frame as integer) \
                                     else ((k as integer) / ticksperframe)
                    if (findItem obj.baseObject.keyList frameNum) == 0 then
                        append obj.baseObject.keyList frameNum
                )
                -- Sort keyList (IntTab doesn't support sort directly)
                local tmp = for v in obj.baseObject.keyList collect v
                sort tmp
                obj.baseObject.keyList = #()
                for v in tmp do append obj.baseObject.keyList v
                deleteNoteTrack obj (getNoteTrack obj 1)
            )
        )
        keys
    ),
    
    -- ============================================================================
    -- IK CHAIN GATHERING
    -- ============================================================================
    
    fn GatherIKChains =
    (
        fn getBoneChains start end =
        (
            local result = #(end)
            end = end.parent
            while (end != undefined and end != start) do
            (
                append result end
                end = end.parent
            )
            append result start
            result
        )
        this.IKChains = #()
        this.IKChainsKeys = #()
        this.IKChainsBones = #()
        fn checkHILimb o = (classof o == IK_Chain_Object) and (classof o.controller == IKChainControl)
        fn checkSpline o = (classof o == IK_Chain_Object) and (classof o.controller == SplineIKChain)
        for o in Helpers where checkHILimb o do
        (
            try
            (
                append this.IKChains o
                local target = o.controller.VHTarget
                local tmp = #(o)
                join tmp (::Utils.GetParents o)
                
                try
                (
                    local topBone = o.controller.startjoint.parent
                    if topBone != undefined then
                    (
                        local cycle = #(topBone)
                        join cycle (::Utils.GetParents topBone)
                        local out = true
                        local i = 1
                        while (i <= cycle.count and out) do
                        (
                            local index = findItem tmp cycle[i]
                            if index == 0 then
                                append tmp cycle[i]
                            else
                            (
                                deleteItem tmp index
                                out = false
                            )
                            i += 1
                        )
                    )
                )
                catch()
                
                if target != undefined and o.controller.useVHTarget then
                (
                    append tmp target
                    join tmp (::Utils.GetParents target)
                )
                tmp = ::Utils.ArrayMakeUnique tmp
                local keys = #()
                for s in tmp where s != undefined do
                    join keys (this.getKeysFromObject s)
                keys = ::Utils.ArrayMakeUnique keys
                sort keys
                local extraKeys = #()
                for i = 2 to keys.count do
                    this.IKRefineKeys o &extraKeys keys[i-1] keys[i]
                join keys extraKeys
                sort keys
                this.IKChainsKeys[this.IKChains.count] = keys
                
                try
                (
                    this.IKChainsBones[this.IKChains.count] = getBoneChains o.controller.startjoint o.controller.endjoint
                )
                catch
                (
                    this.IKChainsBones[this.IKChains.count] = #()
                )
            )
            catch
            (
                format "Warning: Could not process IK Chain '%', skipping.\n" o.name
                if this.IKChains.count > this.IKChainsKeys.count then
                (
                    this.IKChainsKeys[this.IKChains.count] = #()
                    this.IKChainsBones[this.IKChains.count] = #()
                )
            )
        )
        for o in helpers where checkSpline o do
        (
            try
            (
                append this.IKChains o
                local tmp = #(o)
                local cpoints = o.controller.pickShape.modifiers[#Spline_IK_Control].helper_list
                join tmp cpoints
                local topBone = if cpoints.count > 0 then cpoints[1].parent else undefined
                if topBone != undefined then
                (
                    local cycle = #(topBone)
                    join cycle (::Utils.GetParents topBone)
                    local out = true
                    local i = 1
                    while (i <= cycle.count and out) do
                    (
                        local index = findItem tmp cycle[i]
                        if index == 0 then
                            append tmp cycle[i]
                        else
                        (
                            deleteItem tmp index
                            out = false
                        )
                        i += 1
                    )
                )
                tmp = ::Utils.ArrayMakeUnique tmp
                local keys = #()
                for s in tmp where s != undefined do
                    join keys (this.getKeysFromObject s)
                keys = ::Utils.ArrayMakeUnique keys
                sort keys
                local extraKeys = #()
                for i = 2 to keys.count do
                    this.IKRefineKeys o &extraKeys keys[i-1] keys[i]
                join keys extraKeys
                sort keys
                this.IKChainsKeys[this.IKChains.count] = keys
                
                try
                (
                    this.IKChainsBones[this.IKChains.count] = getBoneChains o.controller.startjoint o.controller.endjoint
                )
                catch
                (
                    this.IKChainsBones[this.IKChains.count] = #()
                )
            )
            catch
            (
                format "Warning: Could not process Spline IK Chain '%', skipping.\n" o.name
                if this.IKChains.count > this.IKChainsKeys.count then
                (
                    this.IKChainsKeys[this.IKChains.count] = #()
                    this.IKChainsBones[this.IKChains.count] = #()
                )
            )
        )
    ),
    
    -- ============================================================================
    -- TV ANIMATION FUNCTIONS
    -- ============================================================================
    
    fn isSmoothType t = ((t == #Bezier) or (t == #TCB)),
    
    fn combineTVAnimation c1 c2 c3 =
    (
        fn getTimes c =
        (
            local result = #()
            local n = c.keys.count
            if (n > 0) then result[n] = undefined
            for i = 1 to n do
                result[i] = c.keys[i].time
            result
        )
        fn produceKeys times isSmooth =
        (
            local result = #()
            result[times.count]
            local p = [0,0,0]
            for i = 1 to times.count do
                if isSmooth then
                    result[i] = SmoothKeyData times[i] (copy p) (copy p) (copy p)
                else
                    result[i] = KeyData times[i] (copy p)
            result
        )
        fn saveKeyData keys ct isSmooth index =
        (
            fn getControllerVal c t = at time t (c.value)
            local k
            if isSmooth then
            (
                k = keys
                for i = 1 to k.count do
                (
                    local t = k[i].time
                    local in1 
                    local out1
                    local v = getControllerVal ct t
                    if i != 1 then in1 = getControllerVal ct (k[i-1].time + 2*(t - k[i-1].time)/3) else in1 = v
                    if i != k.count then out1 = getControllerVal ct (k[i].time + (k[i+1].time - t)/3) else out1 = v
                    k[i].value[index] = v
                    k[i].intan[index] = in1
                    k[i].outtan[index] = out1
                )
            ) else (
                k = keys
                for i = 1 to k.count do
                (
                    local v = getControllerVal ct k[i].time
                    k[i].value[index] = v
                )
            )
        )
        fn isSmoothGet t1 t2 t3 = ((this.isSmoothType t1) or (this.isSmoothType t2) or (this.isSmoothType t3))
        
        local t1 = this.FloatType c1
        local t2 = this.FloatType c2
        local t3 = this.FloatType c3
        
        if c1 == undefined then c1 = linear_float ()
        if c2 == undefined then c2 = linear_float ()
        if c3 == undefined then c3 = linear_float ()
        
        local t = getTimes c1
        join t (getTimes c2)
        join t (getTimes c3)
        sort t
        ::Utils.OptimizeArray t
        
        local ism = (isSmoothGet t1 t2 t3)
        local keys = produceKeys t ism
        
        saveKeyData keys c1 ism 1
        saveKeyData keys c2 ism 2
        saveKeyData keys c3 ism 3
        
        if ism then keys = this.ProcessBezier keys
        
        local animData = AnimationData ()
        animData.keys = keys
        
        if (t1 == #NonInterp and t2 == #NonInterp and t3 == #NonInterp) then 
            animData.type = #NonInterp
        else if (t1 == #Hermite or t2 == #Hermite or t3 == #Hermite) then
            animData.type = #Hermite
        else if (t1 == #Bezier or t2 == #Bezier or t3 == #Bezier) then
            animData.type = #Bezier
        else
            animData.type = #Linear
        
        local usesGlobalSequences = (getAfterORT c1 == #cycle or getAfterORT c2 == #cycle or getAfterORT c3 == #cycle)
        if usesGlobalSequences then animData.globalSequenceId = this.LoadGlobalSequence animData.keys[animData.keys.count].time
        
        animData
    ),
    
    -- ============================================================================
    -- INITIALIZATION
    -- ============================================================================
    
    fn InitiateAnimationData s s2 =
    (
        this.Sequences = s
        this.SequencesOriginal = s2
        this.GlobalSequencesGathered = #()
        this.GlobalSequenceCount = 0
        this.GatherIKChains ()
    )
)

-- ============================================================================
-- GLOBAL INSTANCE (for backward compatibility)
-- ============================================================================
global AnimationManager = Wc3AnimationStruct()
