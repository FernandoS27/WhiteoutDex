/*
    Keyframe Optimizer for 3ds Max  (Performance Edition)
    A smarter keyframe optimization tool for Warcraft III animations.
    Reads animation sequences from Custom Attributes (new) or Note Tracks (legacy).

    Performance techniques applied:
      - Embedded C# RDP KeyReducer (no external .cs file needed)
      - disableSceneRedraw / suspendEditing (no 'with redraw off' to keep UI alive)
      - Controller existence checks (skip missing controllers)
      - Throttled UI updates (every N objects, not every object)
      - Cached locals instead of repeated property access
      - Pre-calculated gap intervals (computed once, applied N times)
      - Timing output to MAXScript Listener for benchmarking
      - Undo context wrapping all destructive operations
      - Confirmation dialogs on destructive operations
*/

-----------------------------------------------------------------------
-- Round helper – .NET classes cached once at load time
-----------------------------------------------------------------------
(
    dotNet.loadAssembly "mscorlib"
    global _wdxMathClass = dotNetClass "System.Math"
    global _wdxRoundMode = (dotNetClass "System.MidpointRounding").AwayFromZero
)

fn wdx_RoundDecimal num digits =
(
    (_wdxMathClass.Round num digits _wdxRoundMode) as float
)

-----------------------------------------------------------------------
-- C# KeyReducer – compile from embedded source at startup
-- Uses Ramer-Douglas-Peucker (RDP) algorithm for curve simplification.
-- No external .cs file needed – compiled in-memory.
-----------------------------------------------------------------------
global _wdxKeyReducer = undefined
global _wdxKeyReducerDebug = true
(
    local csSource = "
using System;
using System.Collections.Generic;

namespace KeyReducer
{
    public class Reducer
    {
        // ============================================================
        // RDP for Point3 (position, scale)
        // ============================================================
        public int[] ReducePoint3(float[] times, float[] px, float[] py, float[] pz, float threshold)
        {
            int n = times.Length;
            if (n <= 2) return n == 0 ? new int[0] : (n == 1 ? new int[]{0} : new int[]{0, n-1});

            bool[] keep = new bool[n];
            keep[0] = true;
            keep[n-1] = true;
            RdpPoint3(times, px, py, pz, 0, n-1, threshold, keep);

            List<int> result = new List<int>();
            for (int i = 0; i < n; i++)
                if (keep[i]) result.Add(i);
            return result.ToArray();
        }

        private void RdpPoint3(float[] t, float[] px, float[] py, float[] pz,
                               int first, int last, float threshold, bool[] keep)
        {
            if (last - first < 2) return;

            float maxDist = 0;
            int maxIdx = first;

            float dt = t[last] - t[first];
            if (dt == 0) dt = 1;

            for (int i = first + 1; i < last; i++)
            {
                float alpha = (t[i] - t[first]) / dt;
                float ix = px[first] + (px[last] - px[first]) * alpha;
                float iy = py[first] + (py[last] - py[first]) * alpha;
                float iz = pz[first] + (pz[last] - pz[first]) * alpha;
                float dx = px[i] - ix;
                float dy = py[i] - iy;
                float dz = pz[i] - iz;
                float dist = (float)Math.Sqrt(dx*dx + dy*dy + dz*dz);
                if (dist > maxDist) { maxDist = dist; maxIdx = i; }
            }

            if (maxDist > threshold)
            {
                keep[maxIdx] = true;
                RdpPoint3(t, px, py, pz, first, maxIdx, threshold, keep);
                RdpPoint3(t, px, py, pz, maxIdx, last,  threshold, keep);
            }
        }

        // ============================================================
        // RDP for Float (single channel)
        // ============================================================
        public int[] ReduceFloat(float[] times, float[] vals, float threshold)
        {
            int n = times.Length;
            if (n <= 2) return n == 0 ? new int[0] : (n == 1 ? new int[]{0} : new int[]{0, n-1});

            bool[] keep = new bool[n];
            keep[0] = true;
            keep[n-1] = true;
            RdpFloat(times, vals, 0, n-1, threshold, keep);

            List<int> result = new List<int>();
            for (int i = 0; i < n; i++)
                if (keep[i]) result.Add(i);
            return result.ToArray();
        }

        private void RdpFloat(float[] t, float[] v, int first, int last, float threshold, bool[] keep)
        {
            if (last - first < 2) return;

            float maxDist = 0;
            int maxIdx = first;

            float dt = t[last] - t[first];
            if (dt == 0) dt = 1;

            for (int i = first + 1; i < last; i++)
            {
                float alpha = (t[i] - t[first]) / dt;
                float interp = v[first] + (v[last] - v[first]) * alpha;
                float dist = Math.Abs(v[i] - interp);
                if (dist > maxDist) { maxDist = dist; maxIdx = i; }
            }

            if (maxDist > threshold)
            {
                keep[maxIdx] = true;
                RdpFloat(t, v, first, maxIdx, threshold, keep);
                RdpFloat(t, v, maxIdx, last,  threshold, keep);
            }
        }

        // ============================================================
        // RDP for Quaternion (rotation)
        // Deviation = angle between interpolated quat and actual quat
        // ============================================================
        public int[] ReduceQuat(float[] times, float[] qx, float[] qy, float[] qz, float[] qw, float threshold)
        {
            int n = times.Length;
            if (n <= 2) return n == 0 ? new int[0] : (n == 1 ? new int[]{0} : new int[]{0, n-1});

            bool[] keep = new bool[n];
            keep[0] = true;
            keep[n-1] = true;
            RdpQuat(times, qx, qy, qz, qw, 0, n-1, threshold, keep);

            List<int> result = new List<int>();
            for (int i = 0; i < n; i++)
                if (keep[i]) result.Add(i);
            return result.ToArray();
        }

        private void RdpQuat(float[] t, float[] qx, float[] qy, float[] qz, float[] qw,
                             int first, int last, float threshold, bool[] keep)
        {
            if (last - first < 2) return;

            float maxDev = 0;
            int maxIdx = first;

            float dt = t[last] - t[first];
            if (dt == 0) dt = 1;

            for (int i = first + 1; i < last; i++)
            {
                float alpha = (t[i] - t[first]) / dt;

                // SLERP between first and last
                float dot = qx[first]*qx[last] + qy[first]*qy[last] +
                            qz[first]*qz[last] + qw[first]*qw[last];

                float sx, sy, sz, sw;
                float lx = qx[last], ly = qy[last], lz = qz[last], lw = qw[last];

                if (dot < 0) { lx = -lx; ly = -ly; lz = -lz; lw = -lw; dot = -dot; }

                if (dot > 0.9995f)
                {
                    sx = qx[first] + (lx - qx[first]) * alpha;
                    sy = qy[first] + (ly - qy[first]) * alpha;
                    sz = qz[first] + (lz - qz[first]) * alpha;
                    sw = qw[first] + (lw - qw[first]) * alpha;
                }
                else
                {
                    float theta = (float)Math.Acos(dot);
                    float sinT  = (float)Math.Sin(theta);
                    float w1 = (float)Math.Sin((1 - alpha) * theta) / sinT;
                    float w2 = (float)Math.Sin(alpha * theta) / sinT;
                    sx = qx[first]*w1 + lx*w2;
                    sy = qy[first]*w1 + ly*w2;
                    sz = qz[first]*w1 + lz*w2;
                    sw = qw[first]*w1 + lw*w2;
                }

                // Normalize
                float len = (float)Math.Sqrt(sx*sx + sy*sy + sz*sz + sw*sw);
                if (len > 0) { sx /= len; sy /= len; sz /= len; sw /= len; }

                // Angle deviation between slerp result and actual key
                float d = Math.Abs(sx*qx[i] + sy*qy[i] + sz*qz[i] + sw*qw[i]);
                if (d > 1.0f) d = 1.0f;
                float angle = (float)Math.Acos(d) * 2.0f;  // radians
                float dev = angle * 57.2957795f;            // to degrees

                if (dev > maxDev) { maxDev = dev; maxIdx = i; }
            }

            if (maxDev > threshold)
            {
                keep[maxIdx] = true;
                RdpQuat(t, qx, qy, qz, qw, first, maxIdx, threshold, keep);
                RdpQuat(t, qx, qy, qz, qw, maxIdx, last,  threshold, keep);
            }
        }

        // ============================================================
        // Static detection helpers
        // ============================================================
        public bool IsStaticPoint3(float[] px, float[] py, float[] pz, float threshold)
        {
            if (px.Length < 2) return true;
            for (int i = 1; i < px.Length; i++)
            {
                float dx = px[i] - px[0], dy = py[i] - py[0], dz = pz[i] - pz[0];
                if (Math.Sqrt(dx*dx + dy*dy + dz*dz) > threshold) return false;
            }
            return true;
        }

        public bool IsStaticFloat(float[] vals, float threshold)
        {
            if (vals.Length < 2) return true;
            for (int i = 1; i < vals.Length; i++)
                if (Math.Abs(vals[i] - vals[0]) > threshold) return false;
            return true;
        }

        public bool IsStaticQuat(float[] qx, float[] qy, float[] qz, float[] qw, float threshold)
        {
            if (qx.Length < 2) return true;
            for (int i = 1; i < qx.Length; i++)
            {
                float d = Math.Abs(qx[0]*qx[i] + qy[0]*qy[i] + qz[0]*qz[i] + qw[0]*qw[i]);
                if (d > 1.0f) d = 1.0f;
                float angle = (float)Math.Acos(d) * 2.0f * 57.2957795f;
                if (angle > threshold) return false;
            }
            return true;
        }
        // ============================================================
        // Combined: parse string data, detect static, run RDP
        // Single interop call replaces IsStatic + Reduce + marshalling
        //
        // Input format:  TYPE|threshold|t,t,...|c1,c1,...|c2,c2,...[|c3|c4]
        //   TYPE = QUAT, POINT3, or FLOAT
        //   threshold = RDP threshold (float)
        //   Channels: FLOAT=1, POINT3=3, QUAT=4
        //
        // Returns: \"STATIC\" or \"0,3,7,...\" (0-based keep indices)
        //          \"ERROR:...\" on failure
        // ============================================================
        private static readonly char[] pipeSep  = new char[]{ '|' };
        private static readonly char[] commaSep = new char[]{ ',' };
        private static readonly System.Globalization.CultureInfo inv =
            System.Globalization.CultureInfo.InvariantCulture;

        private float[] ParseFloats(string s)
        {
            string[] parts = s.Split(commaSep);
            float[] arr = new float[parts.Length];
            for (int i = 0; i < parts.Length; i++)
                arr[i] = Single.Parse(parts[i], inv);
            return arr;
        }

        public string AnalyzeAndReduce(string packed)
        {
            try
            {
                string[] sections = packed.Split(pipeSep);
                string valType    = sections[0];
                float threshold   = Single.Parse(sections[1], inv);
                float[] times     = ParseFloats(sections[2]);
                int n = times.Length;

                if (n <= 2)
                {
                    // Nothing meaningful to reduce
                    if (n == 0) return \"\";
                    if (n == 1) return \"0\";
                    return \"0,\" + (n-1).ToString();
                }

                float staticThresh = 0.01f;
                int[] keepIndices;

                if (valType == \"QUAT\")
                {
                    float[] qx = ParseFloats(sections[3]);
                    float[] qy = ParseFloats(sections[4]);
                    float[] qz = ParseFloats(sections[5]);
                    float[] qw = ParseFloats(sections[6]);
                    if (IsStaticQuat(qx, qy, qz, qw, staticThresh)) return \"STATIC\";
                    keepIndices = ReduceQuat(times, qx, qy, qz, qw, threshold);
                }
                else if (valType == \"FLOAT\")
                {
                    float[] vals = ParseFloats(sections[3]);
                    if (IsStaticFloat(vals, staticThresh)) return \"STATIC\";
                    keepIndices = ReduceFloat(times, vals, threshold);
                }
                else // POINT3
                {
                    float[] px = ParseFloats(sections[3]);
                    float[] py = ParseFloats(sections[4]);
                    float[] pz = ParseFloats(sections[5]);
                    if (IsStaticPoint3(px, py, pz, staticThresh)) return \"STATIC\";
                    keepIndices = ReducePoint3(times, px, py, pz, threshold);
                }

                // Build result string
                System.Text.StringBuilder sb = new System.Text.StringBuilder();
                for (int i = 0; i < keepIndices.Length; i++)
                {
                    if (i > 0) sb.Append(',');
                    sb.Append(keepIndices[i]);
                }
                return sb.ToString();
            }
            catch (Exception ex)
            {
                return \"ERROR:\" + ex.Message;
            }
        }
        // ============================================================
        // Batch: process multiple controllers in one call + parallel
        // Input:  block1###block2###block3  (each block = AnalyzeAndReduce format)
        // Output: result1###result2###result3
        // ============================================================
        private static readonly string[] batchSep = new string[]{ \"\\n\" };

        public string AnalyzeAndReduceBatch(string packedBatch)
        {
            try
            {
                string[] blocks = packedBatch.Split(batchSep, StringSplitOptions.None);
                int count = blocks.Length;
                string[] results = new string[count];

                System.Threading.Tasks.Parallel.For(0, count, i =>
                {
                    results[i] = AnalyzeAndReduce(blocks[i]);
                });

                return String.Join(\"\\n\", results);
            }
            catch (Exception ex)
            {
                return \"ERROR:\" + ex.Message;
            }
        }
    }
}
"

    format "[KeyReducer] Compiling C# from embedded source...\n"
    -- (maxVersion())[8] is only populated on Max 2017+; derive year from
    -- the build number for cross-version compatibility (Max 2016+).
    local maxVer = ((maxVersion())[1] / 1000) + 1998
    format "[KeyReducer] Detected 3ds Max %\n" maxVer
    try
    (
        if maxVer >= 2026 then
        (
            -- 3ds Max 2026+: .NET Core 8, use CSharpCompilationHelper (Roslyn)
            format "[KeyReducer] Using CSharpCompilationHelper (.NET Core 8)...\n"
            local netCompHelper = dotNetClass "CSharpUtilities.CSharpCompilationHelper"
            netCompHelper.compile csSource #()
            _wdxKeyReducer = dotNetObject "KeyReducer.Reducer"
            format "[KeyReducer] Compiled and loaded successfully (CSharpCompilationHelper).\n"
        )
        else
        (
            -- 3ds Max 2022-2025: .NET Framework 4.8, use CSharpCodeProvider (CodeDom)
            format "[KeyReducer] Using CSharpCodeProvider (.NET Framework)...\n"
            local codeProvider = dotNetObject "Microsoft.CSharp.CSharpCodeProvider"
            local params = dotNetObject "System.CodeDom.Compiler.CompilerParameters"
            params.GenerateExecutable = false
            params.GenerateInMemory = true
            params.CompilerOptions = "/optimize+"
            local results = codeProvider.CompileAssemblyFromSource params #(csSource)
            if results.Errors.HasErrors then
            (
                format "[KeyReducer] ERROR: Compilation failed:\n"
                for i = 0 to (results.Errors.Count - 1) do
                    format "  %\n" (results.Errors.Item[i].ToString())
            )
            else
            (
                _wdxKeyReducer = results.CompiledAssembly.CreateInstance "KeyReducer.Reducer"
                format "[KeyReducer] Compiled and loaded successfully (CSharpCodeProvider).\n"
            )
        )
    )
    catch
    (
        format "[KeyReducer] ERROR: Compiler not available: %\n" (getCurrentException())
    )
)

-----------------------------------------------------------------------
-- Performance helpers
-----------------------------------------------------------------------
global _wdxWasAnimateEnabled = false

fn wdx_BeginPerformanceMode =
(
    _wdxWasAnimateEnabled = animButtonState
    animButtonState = false
    disableSceneRedraw()
    suspendEditing()
)

fn wdx_EndPerformanceMode =
(
    resumeEditing()
    enableSceneRedraw()
    animButtonState = _wdxWasAnimateEnabled
    forceCompleteRedraw()
)

-----------------------------------------------------------------------
-- Parameters rollout
-----------------------------------------------------------------------
rollout wdxPanelParams "Optimization Parameters"
(
    group ""
    (
        HyperLink lblThreshold "Threshold:" align:#left pos:[12,18]  width:55  height:18 color:(color 104 192 0) enabled:false
        spinner   spnThreshold              align:#left pos:[65,18]  fieldwidth:40 height:18 type:#float   range:[0, 1, 0.5]  \
            tooltip:"Curve simplification amount.\nHigher = more keys removed."
        HyperLink lblStep      "Step:"      align:#left pos:[128,18] width:55  height:18 color:(color 104 192 0) enabled:false
        spinner   spnStep                   align:#left pos:[181,18] fieldwidth:40 height:18 type:#float   range:[0, 1, 0.49] \
            tooltip:"Sampling precision (frames between samples).\nRecommend below 0.49."
    )
    checkbox chkDebug "Debug" align:#right pos:[190,48] height:16 checked:true \
        tooltip:"Print detailed debug info to MAXScript Listener.\nDisable for faster execution."
    on chkDebug changed state do ( _wdxKeyReducerDebug = state )
    on wdxPanelParams open do
    (
        if ::WdxL != undefined then
        (
            wdxPanelParams.title = ::WdxL.t "kfo_params_title"
            lblThreshold.text = ::WdxL.t "kfo_threshold_lbl"
            lblStep.text = ::WdxL.t "kfo_step_lbl"
            try (spnThreshold.tooltip = ::WdxL.t "kfo_threshold_tip") catch ()
            try (spnStep.tooltip = ::WdxL.t "kfo_step_tip") catch ()
            chkDebug.text = ::WdxL.t "kfo_debug_chk"
            try (chkDebug.tooltip = ::WdxL.t "kfo_debug_tip") catch ()
        )
    )
)

-----------------------------------------------------------------------
-- Methods rollout (main logic)
-----------------------------------------------------------------------
rollout wdxPanelMethods "Optimization Methods"
(
    dropdownlist   sortDropdown "" items:#("Sort: Time first", "Sort: Name first") width:220 height:24 pos:[12,8]
    dotNetControl  listView "System.Windows.Forms.ListView" align:#center width:220 height:180 pos:[12,35]

    group "Keyframe types to process"
    (
        checkbox  posKey checked:true align:#left pos:[12,238]  height:20
        checkbox  rotKey checked:true align:#left pos:[92,238]  height:20
        checkbox  scaKey checked:true align:#left pos:[173,238] height:20
        HyperLink lblPos "Position"   align:#left pos:[36,240]  height:20 color:(color 255 78 30)  enabled:false
        HyperLink lblRot "Rotation"   align:#left pos:[116,240] height:20 color:(color 104 192 0)  enabled:false
        HyperLink lblSca "Scale"      align:#left pos:[197,240] height:20 color:(color 44 183 241) enabled:false
    )

    button btnOptSelected    "Optimize selected animation(s)"    width:220 height:24 pos:[12,278]
    button btnOptAll         "Optimize all animations"          width:220 height:24
    button btnDeleteOutside  "Delete keys outside anim ranges"  width:220 height:24

    label       lblProgress "Progress:"  pos:[12,358]  width:175 align:#left
    label       lblPercent  "0%"         pos:[193,359] width:40  align:#right
    progressBar progBar     height:10 width:220 range:[0, 100] pos:[12,378] color:(color 150 86 255)

    -------------------------------------------------------------------
    -- Local state
    -------------------------------------------------------------------
    local selectedAnimItem
    local selectedAnimTrack

    local MSG_DELETE_DONE    = "Delete completed!"
    local MSG_OPT_DONE      = "Optimization completed!"
    local MSG_NO_SELECTION   = "Please select objects first!"
    local MSG_NO_ANIM_SEQ    = "No animation sequences in this scene!"
    local MSG_NO_ANIM_CHOSEN = "No animation selected!"

    fn initLocalization =
    (
        if ::WdxL != undefined then
        (
            MSG_DELETE_DONE    = ::WdxL.t "kfo_msg_delete_done"
            MSG_OPT_DONE      = ::WdxL.t "kfo_msg_opt_done"
            MSG_NO_SELECTION   = ::WdxL.t "kfo_msg_no_selection"
            MSG_NO_ANIM_SEQ    = ::WdxL.t "kfo_msg_no_anim_seq"
            MSG_NO_ANIM_CHOSEN = ::WdxL.t "kfo_msg_no_anim_chosen"
        )
    )

    -------------------------------------------------------------------
    -- Progress
    -------------------------------------------------------------------
    local _progressInterval = 1
    local _progressT0 = 0

    fn ResetProgress total =
    (
        progBar.value    = 0
        lblPercent.text   = "0%"
        lblProgress.text  = if ::WdxL != undefined then ::WdxL.t "kfo_progress_lbl" else "Progress:"
        _progressInterval = amax 1 (total / 50)
        _progressT0       = timestamp()
    )

    fn SetProgress val step statusText =
    (
        if mod step _progressInterval == 0 or val >= 99.9 do
        (
            progBar.value   = wdx_RoundDecimal val 0
            lblPercent.text  = (wdx_RoundDecimal val 1) as string + "%"
            if statusText != unsupplied and statusText != undefined do
                lblProgress.text = statusText
            windows.processPostedMessages()
        )
    )

    fn FinalizeProgress =
    (
        progBar.value    = 100
        lblPercent.text   = "100%"
        lblProgress.text  = if ::WdxL != undefined then ::WdxL.t "kfo_done_lbl" else "Done!"
        windows.processPostedMessages()
    )

    fn EnsureNoteTrack =
    (
        if selectedAnimTrack == undefined or selectedAnimTrack == 0 do
            selectedAnimTrack = 1
    )

    -------------------------------------------------------------------
    -- Unified sequence reader: Custom Attributes first, Note Tracks fallback.
    -------------------------------------------------------------------
    fn GetAllSequences =
    (
        local result = #()
        try
        (
            local names = rootNode.seqNames
            if names != undefined and names.count > 0 then
            (
                if _wdxKeyReducerDebug do format "[DBG] Reading % sequences from Custom Attributes\n" names.count
                for i = 1 to names.count do
                    append result #(names[i], rootNode.startFrames[i], rootNode.endFrames[i])
                return result
            )
        ) catch ()

        EnsureNoteTrack()
        local track = undefined
        try ( track = getNoteTrack rootNode selectedAnimTrack ) catch()
        if track == undefined do ( if _wdxKeyReducerDebug do format "[DBG] No sequences found (no CA, no NoteTracks)\n" ; return result )

        local keys = track.Keys
        if _wdxKeyReducerDebug do format "[DBG] Reading % sequences from Note Tracks (% keys)\n" (keys.count / 2) keys.count
        for i = 1 to keys.count by 2 do
        (
            local note   = keys[i].value
            local parts  = filterString note "\r\n"
            local parts2 = filterString parts[1] "\""
            local aName  = if parts2.count > 0 then parts2[1] else ("Anim " + ((i/2+1) as string))
            local st     = (keys[i].time   as integer) / TicksPerFrame
            local en     = (keys[i+1].time as integer) / TicksPerFrame
            append result #(aName, st, en)
        )
        result
    )

    fn HasSequences =
    (
        try ( if rootNode.seqNames != undefined and rootNode.seqNames.count > 0 then return true ) catch()
        (numNoteTracks rootNode > 0)
    )

    -------------------------------------------------------------------
    -- Safe controller access
    -------------------------------------------------------------------
    fn GetAnimControllers obj =
    (
        local ctrls = #()
        if posKey.checked do ( try ( local c = obj.pos.controller      ; if c != undefined do append ctrls c ) catch() )
        if rotKey.checked do ( try ( local c = obj.rotation.controller  ; if c != undefined do append ctrls c ) catch() )
        if scaKey.checked do ( try ( local c = obj.scale.controller     ; if c != undefined do append ctrls c ) catch() )
        ctrls
    )

    fn GetTopController obj =
    (
        local ctrl = undefined
        try ( ctrl = obj.controller ) catch()
        ctrl
    )

    fn CountKeys objArray =
    (
        local total = 0
        for obj in objArray do
        (
            if posKey.checked do ( try ( local c = obj.pos.controller      ; if c != undefined do total += c.keys.count ) catch() )
            if rotKey.checked do ( try ( local c = obj.rotation.controller  ; if c != undefined do total += c.keys.count ) catch() )
            if scaKey.checked do ( try ( local c = obj.scale.controller     ; if c != undefined do total += c.keys.count ) catch() )
        )
        total
    )

    fn PrintKeyframeStats label keysBefore keysAfter ms =
    (
        local removed = keysBefore - keysAfter
        local pct = if keysBefore > 0 then (wdx_RoundDecimal (removed * 100.0 / keysBefore) 1) else 0.0
        format ">> %: % keys -> % keys (% removed, -% percent) in %ms\n" label keysBefore keysAfter removed pct ms
    )

    -------------------------------------------------------------------
    -- Key operations
    -------------------------------------------------------------------
    fn DeleteKeysByType obj =
    (
        for c in (GetAnimControllers obj) do
            try ( deleteKeys c #selection ) catch()
    )

    -------------------------------------------------------------------
    -- Smart Mode: Batch C# RDP with string-packed marshalling
    -- One interop call per OBJECT (all controllers at once)
    -------------------------------------------------------------------
    fn DeleteStaticKeys ctrl iFirst iLast =
    (
        local tFirst = ctrl.keys[iFirst].time
        local tLast  = ctrl.keys[iLast].time
        deselectKeys ctrl (interval 0 99999)
        selectKeys   ctrl (interval tFirst tLast)
        deselectKeys ctrl tFirst
        deselectKeys ctrl tLast
        deleteKeys   ctrl #selection
    )

    -------------------------------------------------------------------
    -- Serialize key data with integrated range filtering.
    -- Scans keys once, filters by range AND serializes in one pass.
    -- Returns #(packedString, iFirst, iLast, keyCount) or undefined if <2 keys in range.
    -------------------------------------------------------------------
    fn SerializeControllerInRange ctrl rangeStart rangeEnd threshold =
    (
        local allKeys = ctrl.keys
        local n = allKeys.count
        if n == 0 do return undefined

        -- Find range bounds and test value readability in one scan
        local iFirst = 0
        local iLast  = 0
        for k = 1 to n do
        (
            local kt = allKeys[k].time as integer / TicksPerFrame
            if kt >= rangeStart and kt <= rangeEnd do
            (
                if iFirst == 0 do iFirst = k
                iLast = k
            )
        )
        if iFirst == 0 or iLast == 0 or (iLast - iFirst) < 2 do return undefined

        -- Test value readability
        local firstVal = undefined
        try ( firstVal = allKeys[iFirst].value ) catch ( return undefined )
        local valClass = classOf firstVal

        -- Serialize directly (no intermediate arrays)
        local ss = stringStream ""
        if valClass == Quat then
        (
            format "QUAT|%|" threshold to:ss
            for k = iFirst to iLast do
            (
                if k > iFirst do format "," to:ss
                format "%" ((allKeys[k].time as integer / TicksPerFrame) as float) to:ss
            )
            for ch = 1 to 4 do
            (
                format "|" to:ss
                for k = iFirst to iLast do
                (
                    if k > iFirst do format "," to:ss
                    local v = allKeys[k].value
                    local c = case ch of ( 1: v.x ; 2: v.y ; 3: v.z ; 4: v.w )
                    format "%" c to:ss
                )
            )
        )
        else if valClass == Float or valClass == Integer then
        (
            format "FLOAT|%|" threshold to:ss
            for k = iFirst to iLast do
            (
                if k > iFirst do format "," to:ss
                format "%" ((allKeys[k].time as integer / TicksPerFrame) as float) to:ss
            )
            format "|" to:ss
            for k = iFirst to iLast do
            (
                if k > iFirst do format "," to:ss
                format "%" (allKeys[k].value as float) to:ss
            )
        )
        else
        (
            format "POINT3|%|" threshold to:ss
            for k = iFirst to iLast do
            (
                if k > iFirst do format "," to:ss
                format "%" ((allKeys[k].time as integer / TicksPerFrame) as float) to:ss
            )
            for ch = 1 to 3 do
            (
                format "|" to:ss
                for k = iFirst to iLast do
                (
                    if k > iFirst do format "," to:ss
                    local v = allKeys[k].value
                    local c = case ch of ( 1: v.x ; 2: v.y ; 3: v.z )
                    format "%" c to:ss
                )
            )
        )
        #(ss as string, iFirst, iLast, iLast - iFirst + 1)
    )

    -------------------------------------------------------------------
    -- Apply C# result to a controller: delete/keep keys
    -------------------------------------------------------------------
    fn ApplyReduceResult ctrl iFirst iLast result =
    (
        local n = iLast - iFirst + 1
        if result == "STATIC" then
        (
            if _wdxKeyReducerDebug do format "    [DBG]   -> STATIC, batch-deleting middle keys\n"
            DeleteStaticKeys ctrl iFirst iLast
        )
        else if (findString result "ERROR:" == 1) then
        (
            format "    [DBG]   -> C# ERROR: %\n" result
        )
        else
        (
            local keepSet = #{}
            local parts = filterString result ","
            local numKeep = parts.count
            for p in parts do keepSet[(p as integer) + 1] = true

            if _wdxKeyReducerDebug do
                format "    [DBG]   C# RDP: % keys -> keep % (remove %)\n" n numKeep (n - numKeep)

            local keyTimes = for k = iFirst to iLast collect ctrl.keys[k].time
            local tFirst = keyTimes[1]
            local tLast  = keyTimes[n]
            deselectKeys ctrl (interval 0 99999)
            selectKeys   ctrl (interval tFirst tLast)
            for i = 1 to n do
            (
                if keepSet[i] do deselectKeys ctrl keyTimes[i]
            )
            deleteKeys ctrl #selection
        )
    )

    -------------------------------------------------------------------
    -- Collect all processable controllers (direct + sub-anims)
    -------------------------------------------------------------------
    fn CollectAllControllers obj =
    (
        local result = #()  -- array of #(ctrl, classOf)
        local ctrls = GetAnimControllers obj
        for ci = 1 to ctrls.count do
        (
            local ctrl = ctrls[ci]
            try
            (
                local hasKeys  = ctrl.keys != undefined
                local keyCount = if hasKeys then ctrl.keys.count else 0
                local canRead  = false
                if hasKeys and keyCount > 0 do
                    try ( local _t = ctrl.keys[1].value ; canRead = true ) catch()

                if canRead and keyCount > 2 then
                (
                    append result ctrl
                )
                else
                (
                    -- Compound controllers: probe sub-anims
                    local nSubs = 0
                    try ( nSubs = numSubs ctrl ) catch
                    (
                        for probe = 1 to 3 do
                            try ( local _t = getSubAnim ctrl probe ; nSubs = probe ) catch ( exit )
                    )
                    for s = 1 to nSubs do
                    (
                        try
                        (
                            local sub = (getSubAnim ctrl s).controller
                            if sub != undefined and sub.keys != undefined and sub.keys.count > 2 do
                                append result sub
                        ) catch()
                    )
                )
            ) catch()
        )
        result
    )

    -------------------------------------------------------------------
    -- SmartOptimize: batch all controllers of an object in one C# call
    -------------------------------------------------------------------
    fn SmartOptimize obj rangeStart rangeEnd threshold step =
    (
        if _wdxKeyReducer == undefined do
        (
            if _wdxKeyReducerDebug do
                format "    [DBG] % : C# not loaded, fallback to native ReduceKeys\n" obj.name
            local animRange = interval (rangeStart * TicksPerFrame) (rangeEnd * TicksPerFrame)
            for c in (GetAnimControllers obj) do
                try ( ReduceKeys c threshold step animRange ) catch()
            return()
        )

        -- Collect all processable controllers for this object
        local allCtrls = CollectAllControllers obj
        if allCtrls.count == 0 do return()

        -- Serialize all controllers with integrated range filtering
        local serialized = #()  -- #( #(packedStr, iFirst, iLast, n), ... )
        local validCtrls = #()  -- corresponding controllers
        for ctrl in allCtrls do
        (
            local data = SerializeControllerInRange ctrl rangeStart rangeEnd threshold
            if data != undefined do
            (
                append serialized data
                append validCtrls ctrl
            )
        )
        if serialized.count == 0 do return()

        -- Build batch string: block1\nblock2\nblock3 (StringStream for performance)
        local batchSS = stringStream ""
        for i = 1 to serialized.count do
        (
            if i > 1 do format "\n" to:batchSS
            format "%" serialized[i][1] to:batchSS
        )
        local batchStr = batchSS as string

        -- Single C# call for ALL controllers (with Parallel.For inside)
        local batchResult = _wdxKeyReducer.AnalyzeAndReduceBatch batchStr

        -- Parse and apply results
        local results = filterString batchResult "\n"
        local numToApply = if results.count < validCtrls.count then results.count else validCtrls.count
        for i = 1 to numToApply do
        (
            try
            (
                ApplyReduceResult validCtrls[i] serialized[i][2] serialized[i][3] results[i]
            )
            catch
            (
                if _wdxKeyReducerDebug do
                    format "    [DBG]   ERROR applying result[%]: %\n" i (getCurrentException())
            )
        )
    )

    -------------------------------------------------------------------
    -- Sequence list / ListView
    -------------------------------------------------------------------
    fn ResetTrackList =
    (
        if HasSequences() do selectedAnimTrack = 1
    )

    fn ResetListView =
    (
        local items = #()
        listView.Items.Clear()
        listView.ListViewItemSorter = undefined
        listView.Sorting = undefined

        local seqs = GetAllSequences()
        if seqs.count > 0 then
        (
            local animNames = for s in seqs collect s[1]
            if sortDropdown.selection == 2 do animNames = sort animNames
            for a in animNames do
            (
                local item = dotNetObject "System.Windows.Forms.ListViewItem"
                item.Text = a
                append items item
            )
            listView.Items.AddRange items
        )
    )

    fn InitializeListView =
    (
        listView.View          = (dotNetClass "System.Windows.Forms.View").Details
        listView.FullRowSelect = true
        listView.MultiSelect   = true
        listView.HideSelection = false
        listView.Columns.Add "Animation Name" -2
        listView.Sorting = (dotNetClass "System.Windows.Forms.SortOrder").Ascending
        ResetTrackList()
        ResetListView()
    )

    fn FindAnimRangeFromSeqs seqs animName =
    (
        for s in seqs do
        (
            if s[1] == animName then
            (
                local st = s[2]
                local en = s[3]
                if st == en do en = st + 1
                return (interval st en)
            )
        )
        undefined
    )

    -------------------------------------------------------------------
    -- UI events
    -------------------------------------------------------------------
    on listView ItemSelectionChanged arg do
    (
        EnsureNoteTrack() ; stopAnimation() ; selectedAnimItem = arg.Item
    )
    on sortDropdown selected arg do ( ResetTrackList() ; ResetListView() )
    on wdxPanelMethods open do
    (
        InitializeListView()
        initLocalization()
        if ::WdxL != undefined then
        (
            wdxPanelMethods.title = ::WdxL.t "kfo_methods_title"
            sortDropdown.items = #(::WdxL.t "kfo_sort_time", ::WdxL.t "kfo_sort_name")
            lblPos.text = ::WdxL.t "kfo_position"
            lblRot.text = ::WdxL.t "kfo_rotation"
            lblSca.text = ::WdxL.t "kfo_scale"
            btnOptSelected.text = ::WdxL.t "kfo_opt_selected_btn"
            btnOptAll.text = ::WdxL.t "kfo_opt_all_btn"
            btnDeleteOutside.text = ::WdxL.t "kfo_delete_outside_btn"
            lblProgress.text = ::WdxL.t "kfo_progress_lbl"
        )
    )

    -------------------------------------------------------------------
    -- BTN: Optimize selected animation(s)
    -------------------------------------------------------------------
    on btnOptSelected pressed do
    (
        local selItems = listView.SelectedItems
        if selItems.Count == 0 then ( MessageBox MSG_NO_ANIM_CHOSEN ; return() )
        local objKeys = selection as array
        if objKeys.count == 0 then ( MessageBox MSG_NO_SELECTION ; return() )

        EnsureNoteTrack()
        local seqs = GetAllSequences()
        if seqs.count == 0 then ( MessageBox MSG_NO_ANIM_SEQ ; return() )

        local animNames  = #()
        local animRanges = #()
        local animStarts = #()
        local animEnds   = #()
        for s = 0 to (selItems.Count - 1) do
        (
            local aName = selItems.Item[s].Text
            local aRange = FindAnimRangeFromSeqs seqs aName
            if aRange != undefined do
            (
                append animNames  aName
                append animRanges aRange
                append animStarts (aRange.start as integer / TicksPerFrame)
                append animEnds   (aRange.end   as integer / TicksPerFrame)
            )
        )
        if animRanges.count == 0 then ( MessageBox MSG_NO_ANIM_CHOSEN ; return() )

        local threshVal    = wdxPanelParams.spnThreshold.value
        local stepVal      = wdxPanelParams.spnStep.value
        local total        = objKeys.count
        local numAnims     = animRanges.count
        local totalSteps   = total * numAnims
        local keysBefore   = CountKeys objKeys

        local modeLabel = if _wdxKeyReducer != undefined then "Threshold (C#)" else "Threshold"
        format "\n>> Starting % optimization...\n" modeLabel
        if _wdxKeyReducerDebug do format ">> [DBG] _wdxKeyReducer loaded: %\n" (_wdxKeyReducer != undefined)
        if _wdxKeyReducerDebug do format ">> [DBG] Objects: %, Animations: %, Keys before: %\n" total numAnims keysBefore
        for a = 1 to numAnims do
            if _wdxKeyReducerDebug do format ">> [DBG]   Anim \"%\": frames %..%\n" animNames[a] animStarts[a] animEnds[a]

        ResetProgress totalSteps
        local t0 = timestamp()

        wdx_BeginPerformanceMode()
        try ( undo "Optimize Selected Animation(s)" on
        (
            local pv  = 0.0
            local inc = 100.0 / totalSteps
            local stepCounter = 0
            for a = 1 to numAnims do
            (
                local animT0 = timestamp()
                format "  [%/%] \"%\" ...\n" a numAnims animNames[a]
                for i = 1 to total do
                (
                    SmartOptimize objKeys[i] animStarts[a] animEnds[a] threshVal stepVal
                    pv += inc
                    stepCounter += 1
                    SetProgress pv stepCounter (animNames[a] + "  (" + (a as string) + "/" + (numAnims as string) + ")")
                )
                format "    done in %ms\n" (timestamp() - animT0)
            )
        )) catch ( format "*** Error: %\n" (getCurrentException()) )
        wdx_EndPerformanceMode()

        local keysAfter = CountKeys objKeys
        FinalizeProgress()
        for a = 1 to numAnims do format "  [%] %\n" a animNames[a]
        PrintKeyframeStats ("Optimize " + (numAnims as string) + " anim(s) on " + (total as string) + " objects") keysBefore keysAfter (timestamp() - t0)
        MessageBox MSG_OPT_DONE
    )

    -------------------------------------------------------------------
    -- BTN: Optimize all animations
    -------------------------------------------------------------------
    on btnOptAll pressed do
    (
        local objKeys = selection as array
        if objKeys.count == 0 then ( MessageBox MSG_NO_SELECTION ; return() )

        local seqs = GetAllSequences()
        if seqs.count == 0 then ( MessageBox MSG_NO_ANIM_SEQ ; return() )

        local threshVal    = wdxPanelParams.spnThreshold.value
        local stepVal      = wdxPanelParams.spnStep.value
        local total        = objKeys.count
        local numAnims     = seqs.count
        local totalSteps   = total * numAnims

        local animRangeStart = #()
        local animRangeEnd   = #()
        local allAnimNames   = #()
        for s in seqs do
        (
            local st = s[2]
            local en = s[3]
            append animRangeStart st
            append animRangeEnd   en
            append allAnimNames   s[1]
        )

        local modeLabel = if _wdxKeyReducer != undefined then "Threshold (C#)" else "Threshold"
        format "\n>> Starting % optimization on ALL animations...\n" modeLabel

        ResetProgress totalSteps
        local keysBefore = CountKeys objKeys
        if _wdxKeyReducerDebug do format ">> [DBG] _wdxKeyReducer loaded: %\n" (_wdxKeyReducer != undefined)
        if _wdxKeyReducerDebug do format ">> [DBG] Objects: %, Animations: %, Keys before: %\n" total numAnims keysBefore
        for a = 1 to numAnims do
            if _wdxKeyReducerDebug do format ">> [DBG]   Anim \"%\": frames %..%\n" allAnimNames[a] animRangeStart[a] animRangeEnd[a]

        local t0 = timestamp()

        wdx_BeginPerformanceMode()
        try ( undo "Optimize All Animations" on
        (
            local pv  = 0.0
            local inc = 100.0 / totalSteps
            local stepCounter = 0
            for a = 1 to numAnims do
            (
                local animT0 = timestamp()
                local animLabel = allAnimNames[a]
                format "  [%/%] \"%\" ...\n" a numAnims animLabel
                for i = 1 to total do
                (
                    SmartOptimize objKeys[i] animRangeStart[a] animRangeEnd[a] threshVal stepVal
                    pv += inc
                    stepCounter += 1
                    SetProgress pv stepCounter (animLabel + "  (" + (a as string) + "/" + (numAnims as string) + ")")
                )
                format "    done in %ms\n" (timestamp() - animT0)
            )
        )) catch ( format "*** Error: %\n" (getCurrentException()) )
        wdx_EndPerformanceMode()

        local keysAfter = CountKeys objKeys
        FinalizeProgress()
        PrintKeyframeStats ("Optimize ALL " + (numAnims as string) + " anims on " + (total as string) + " objects") keysBefore keysAfter (timestamp() - t0)
        MessageBox MSG_OPT_DONE
    )

    -------------------------------------------------------------------
    -- BTN: Delete keys outside animation ranges
    -------------------------------------------------------------------
    on btnDeleteOutside pressed do
    (
        local objKeys = selection as array
        if objKeys.count == 0 then ( MessageBox MSG_NO_SELECTION ; return() )

        local seqs = GetAllSequences()
        if seqs.count == 0 then ( MessageBox MSG_NO_ANIM_SEQ ; return() )

        local total = objKeys.count

        fn CompareSequenceStart a b = (a[2] - b[2])
        local sortedSeqs = deepCopy seqs
        qsort sortedSeqs CompareSequenceStart

        local gapIntervals = #()
        append gapIntervals (interval 1 (sortedSeqs[1][2] - 1))
        for j = 1 to (sortedSeqs.count - 1) do
            append gapIntervals (interval (sortedSeqs[j][3] + 1) (sortedSeqs[j+1][2] - 1))
        append gapIntervals (interval (sortedSeqs[sortedSeqs.count][3] + 1) 99999)

        ResetProgress total
        local keysBefore = CountKeys objKeys
        local t0 = timestamp()

        wdx_BeginPerformanceMode()
        try ( undo "Delete Keys Outside Anim Ranges" on
        (
            local pv  = 0.0
            local inc = 100.0 / total
            for i = 1 to total do
            (
                local ctrl = GetTopController objKeys[i]
                if ctrl != undefined do
                (
                    deselectKeys ctrl (interval 0 99999)
                    for gi in gapIntervals do selectKeys ctrl gi
                    DeleteKeysByType objKeys[i]
                    deselectKeys ctrl (interval 0 99999)
                )
                pv += inc
                SetProgress pv i ("Object " + (i as string) + "/" + (total as string))
            )
        )) catch ( format "*** Error: %\n" (getCurrentException()) )
        wdx_EndPerformanceMode()

        local keysAfter = CountKeys objKeys
        FinalizeProgress()
        PrintKeyframeStats ("Delete outside on " + (total as string) + " objects") keysBefore keysAfter (timestamp() - t0)
        MessageBox MSG_DELETE_DONE
    )
)

-----------------------------------------------------------------------
-- Create the floating dialog
-----------------------------------------------------------------------
try ( closeRolloutFloater wdxKfoFloater ) catch()
global wdxKfoFloater = newRolloutFloater (if ::WdxL != undefined then ::WdxL.t "kfo_title" else "Keyframe Optimizer") 257 510
(
    addRollout wdxPanelParams  wdxKfoFloater
    addRollout wdxPanelMethods wdxKfoFloater
)
