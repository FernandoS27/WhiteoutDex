# Wc3Ribbon Plugin — Accuracy Report vs Engine CRibbonEmitter

## Overview

This report compares the **Wc3Ribbon** 3ds Max plugin (`Ribbon.cpp` / `Ribbon.h`) against the reverse-engineered **CRibbonEmitter** engine class (`pseudocode/CRibbonEmitter.cpp`). The engine RE was performed on `Previewd.exe` (64-bit) using IDA Pro with Hex-Rays decompiler, covering all 24 class methods and the full 384-byte struct layout.

The plugin is a GeomObject-based 3ds Max plugin that draws a ribbon trail preview in the viewport. It has **11 animatable parameters** via ParamBlock2 and reconstructs the edge trail every frame from animation data. Below is a detailed accuracy breakdown.

---

## 1. Data Structure: Ring Buffer vs Rebuild-Every-Frame

| Aspect | Engine | Plugin |
|--------|--------|--------|
| Storage | Circular ring buffer (`TSGrowableArray<float>`) of edge ages, fixed-size `ceil(eps * lifeSpan) + 2` | `std::deque<RibbonEdge>` cleared and rebuilt every frame |
| Per-edge data | `float age` (in the ring buffer) + 2 × `CRibbonVertex` (pos + texCoord) in separate vertex array | `TimeValue birthTime` only |
| Vertex positions | Stored persistently, updated in-place each frame (gravity, age) | Recomputed from scratch via `inode->GetObjectTM(edge.birthTime)` |
| Frame coherence | Yes — edges persist across frames, ages accumulate | No — full trail rebuilt from animation curves each `BuildEdgeTrail()` |

**Accuracy impact**: The engine's incremental approach means gravity and texture animation are accumulated frame-by-frame, producing slightly different results depending on frame rate. The plugin's stateless recomputation always produces the same output for a given time `t`, which is actually more stable but doesn't match the engine's frame-rate-dependent behavior.

**Recommendation**: The rebuild-every-frame approach is acceptable for a preview plugin and easier to reason about. No change needed for preview purposes. If exact engine-matching is required, switch to a persistent ring buffer with per-frame age accumulation.

---

## 2. Gravity Formula — 2× Strength Discrepancy

This is the **most significant accuracy issue** in the plugin.

### Engine (incremental per frame)
```cpp
// In Update(), for each alive edge:
float dz = (gravity * dt) * dt + (2.0f * gravity * edges[pos]) * dt;
v0->pos.z += dz;
v1->pos.z += dz;
edges[pos] += dt;  // age advances
```

Tracing for constant `dt` with starting age 0:
| Frame | Age before | dz                  | Cumulative Z offset |
|-------|-----------|---------------------|---------------------|
| 1     | 0         | g·dt²              | g·dt²               |
| 2     | dt        | g·dt² + 2g·dt·dt = 3g·dt² | 4g·dt²      |
| 3     | 2dt       | g·dt² + 2g·2dt·dt = 5g·dt² | 9g·dt²     |
| n     | (n-1)dt   | (2n-1)·g·dt²       | n²·g·dt²            |

After total time `t = n·dt`, the cumulative offset = **g·t²**.

### Plugin (analytical)
```cpp
float gravOffset = -0.5f * gravity * ageSec * ageSec;
```

This gives an offset of **−0.5·g·t²** — the standard kinematic formula.

### Discrepancy

The engine accumulates **g·t²** total, while the plugin applies **−0.5·g·t²**. Besides the sign convention difference (engine: negative gravity = down; plugin: positive gravity = down), the magnitude differs by a factor of **2×**.

For equivalent visual behavior with engine gravity `g_engine`:
- The plugin should use `gravity_plugin = -2 * g_engine` (to match both sign and magnitude)
- Or the formula should be changed to: `gravOffset = gravity * ageSec * ageSec`

**Recommendation**: Change the gravity formula in `ComputeEdgeWorldPos()` to:
```cpp
float gravOffset = gravity * ageSec * ageSec;  // matches engine's g·t²
```
And negate the sign convention if needed. The current formula produces half the expected gravity displacement.

---

## 3. Sub-Frame Edge Interpolation — Missing

### Engine
The engine uses a Hermite-like interpolation scheme for sub-frame edge placement:

1. `InitInterpDeltas()` computes:
   - `scale = distance(prevPos, currPos)`
   - `below0/1 = prevPos/currPos ± vertical * below`
   - `above0/1 = prevPos/currPos ± vertical * above`
   - `prevDirScaled/currDirScaled = direction * scale` (tangent vectors)

2. `InterpEdge(age, t, advance)` blends:
   ```
   lowerVertex = lerp(below0 + prevDirScaled*t, below1 - currDirScaled*(1-t), t)
   upperVertex = lerp(above0 + prevDirScaled*t, above1 - currDirScaled*(1-t), t)
   ```
   The `prevDirScaled*t` and `currDirScaled*(1-t)` terms create tangent-influenced curves, similar to Hermite splines, preventing sharp corners when the emitter changes direction.

3. Multiple edges can be emitted per frame with fractional timing (`startTime` accumulator).

### Plugin
No sub-frame interpolation. Each edge is placed at the node's transform at a discrete `birthTime`:
```cpp
Matrix3 edgeTM = inode->GetObjectTM(edge.birthTime);
Point3 pos = edgeTM.GetTrans();
Point3 localY = Normalize(edgeTM.GetRow(1));
outTop = pos + localY * ha;
outBot = pos - localY * hb;
```

3ds Max's animation system does provide smooth interpolation between keyframes when evaluating `GetObjectTM(birthTime)`, but this is purely positional — there are no tangent-based lateral offsets that the engine uses.

**Accuracy impact**: At low frame rates or with fast-moving emitters, the engine's Hermite-like interpolation produces smoother ribbon curves. The plugin will show more angular trails in the same conditions.

**Recommendation**: For a more accurate preview, implement the tangent-based interpolation by sampling two consecutive time steps per edge and computing tangent vectors. However, the 3ds Max animation system's interpolation may be sufficient for preview purposes.

---

## 4. Texture Coordinate Animation — Not Implemented

### Engine
The engine animates texture U coordinates based on edge age:
```cpp
// In Update(), for each alive edge:
float u = (tmpDU * edges[pos]) * ooLifeSpan + texSlotBox.l;
v0->texCoord = C2Vector(u, texSlotBox.t);
v1->texCoord = C2Vector(u, texSlotBox.b);
```

Where:
- `tmpDU = texBox.Width() / cols` — one texture cell width
- `ooLifeSpan = 1.0 / edgeLifeSpan`
- `texSlotBox` — UV rect from `ConvertTexSlotToTexCoords()` using `texSlot`, `rows`, `cols`

This causes the texture to scroll along the ribbon over the edge's lifetime.

### Plugin
The plugin defines `pb_tex_rows`, `pb_tex_cols`, and `pb_tex_slot` parameters but **never uses them** for UV computation. The plugin does not generate any mesh or UV coordinates — it renders as wireframe polylines only.

**Recommendation**: This is acceptable if the plugin is intended as a viewport wireframe preview only. If mesh/render output is desired in the future, implement `ConvertTexSlotToTexCoords()` and the per-edge UV animation formula.

---

## 5. Rendering — Wireframe vs Triangle Strip

### Engine
- Vertex format: `GxVBF_PNCT0` (position + normal + color + texcoord0)
- Primitive: `GxPrim_TriangleStrip`
- Shared dummy normal `(1,0,0)` and shared `diffuseClr` across all vertices
- Stride = `0x14` (sizeof CRibbonVertex = 20 bytes)
- Materials applied via `CWar3Mat::UseMaterial()` with diffuse color override
- Index buffer uses pre-computed modular indices for ring buffer wrap-around

### Plugin
- Viewport wireframe rendering via `gw->polyline()`
- Draws per-edge vertical bars (top↔bottom) and connecting rails (top↔top, bottom↔bottom)
- No triangle mesh generation — `mesh` member is empty, `IsRenderable()` returns `FALSE`
- Color follows node wire color or pb_color swatch; per-edge color sampling from animation curves

**Accuracy impact**: The plugin cannot produce the engine's filled triangle strip appearance. It provides a structural wireframe preview that shows edge layout and gravity correctly.

**Recommendation**: For better visual accuracy, generate a `Mesh` with triangle strips in `Eval()` or `BuildMesh()`. Each pair of consecutive edges forms a quad (2 triangles), with UV coordinates computed per the engine formula. This would also make the object renderable.

---

## 6. Edge Emission Timing

### Engine
```cpp
float endTime = startTime + (dt * edgesPerSec);
if (endTime >= 1.0f) {
    float ooDenom = 1.0f / (endTime - startTime);
    unsigned int numNewEdges = (unsigned int)floor(endTime - 1.0f) + 1;
    InitInterpDeltas();
    while (numNewEdges--) {
        float interpTime = (newEdgeTime - startTime) * ooDenom;
        InterpEdge(-dt * interpTime, interpTime, 1);
        newEdgeTime += 1.0f;
    }
}
startTime = endTime - floor(endTime);
```
Uses a fractional accumulator to emit edges at precise sub-frame intervals.

### Plugin
```cpp
int tickInterval = (int)(kTicksPerSec / (float)edges_per_second);
for (int i = 0; i < maxEdges; i++) {
    TimeValue birthTime = t - i * tickInterval;
    // ...
}
```
Evenly spaced backward from current time at tick-aligned intervals.

**Accuracy impact**: Minimal — both produce edges at roughly `1/edgesPerSec` intervals. The engine's fractional accumulator handles frame-rate-independent sub-frame timing; the plugin's approach is stable since it derives from Max's tick system.

**Recommendation**: No change needed.

---

## 7. Default Parameter Values

| Parameter | Engine Default | Plugin Default | Match? |
|-----------|---------------|----------------|--------|
| Height Above | 10.0 | 20.0 | **No** — 2× |
| Height Below | 10.0 | 20.0 | **No** — 2× |
| Gravity | 0.0 | 0.0 | Yes |
| Edges Per Second | min clamp 1.0 | 10 (default) | N/A |
| Edge Lifetime | min clamp 0.25 | 2.0 (default) | N/A |
| Color | BGRA 0 (black) | RGB(1,1,1) (white) | **No** |
| Tex Rows | (from model data) | 1 | N/A |
| Tex Cols | (from model data) | 1 | N/A |
| Tex Slot | 0 | 0 | Yes |

The engine's `above`/`below` default to 10.0 in `Initialize()`. The plugin defaults to 20.0. In practice this doesn't matter since MDL import will override these, but the defaults should ideally match.

**Recommendation**: Change `p_default` for `pb_height_above` and `pb_height_below` to `10.0f`.

---

## 8. Vertical Direction / Axis Convention

### Engine
- Vertical = `orient.Row1()` (Y axis of the C34Matrix)
- Direction = `orient.Row2()` (Z axis / forward direction)
- Gravity applied to **Z axis** (`v->pos.z += dz`)

### Plugin
- Vertical = `Normalize(edgeTM.GetRow(1))` (Y axis of the 3ds Max transform)
- Gravity applied to **Z axis** (`Point3(0,0,gravOffset)`)

**Accuracy**: The axis mapping is consistent. Both use the Y axis for vertical ribbon extent and apply gravity along world Z. This is correct.

---

## 9. Edge Count Capping

### Engine
Ring buffer size: `ceil(edgesPerSec * edgeLifeSpan) + 2` — no hard cap.

### Plugin
```cpp
int maxEdges = (int)(edges_per_second * edge_lifetime);
if (maxEdges < 2)  maxEdges = 2;
if (maxEdges > 128) maxEdges = 128;
```

The 128-edge cap is an artificial limitation. For high `edgesPerSec * edgeLifeSpan` values, the trail will be truncated.

**Recommendation**: Remove or increase the 128 cap. Consider matching the engine formula: `ceil(eps * lifeSpan) + 2`.

---

## 10. Alpha Parameter — Unused in Display

The plugin defines `pb_alpha` (float 0–1) and queries it in `ObjectValidity()`, but it is **never applied** in `DrawAndHit()` or `ComputeEdgeWorldPos()`. The engine applies alpha via `CRibbonEmitter::SetAlpha()` which sets `diffuseClr.a = a * 255`, and the color is used as a uniform vertex attribute during rendering.

**Recommendation**: If mesh rendering is ever added, apply alpha to vertex colors. For the current wireframe preview, 3ds Max's `gw` doesn't natively support per-edge alpha, so this is a known limitation.

---

## 11. Dual Enable Flags — Not Represented

The engine has two independent enable bits (`bit0` = Enabled, `bit1` = Enabled2). Both must be set for the emitter to produce edges. `SetPos()` checks both flags; disabling either clears `posSet`.

The plugin uses `inode->IsNodeHidden()` and `inode->GetVisibility(birthTime)` for visibility, which is the idiomatic 3ds Max approach. There is no need to replicate the dual-flag system since those are engine-internal concepts controlled by the game's animation system.

**Recommendation**: No change needed.

---

## 12. Timer / Time Source

### Engine
`OsGetAsyncTimeMsPrecise()` — ignores the `elapsedSec` parameter entirely. Uses wall-clock time.

### Plugin
3ds Max's `TimeValue` system (4800 ticks/sec). `kTicksPerSec = 4800.0f` is correctly defined.

**Accuracy**: Both approaches are correct for their respective environments. The engine uses real time because it's a game; the plugin uses animation time because it's a DCC tool. This is appropriate.

---

## 13. Per-Edge Color Sampling — Plugin-Only Feature

The plugin queries `pb_color` at each edge's `birthTime`:
```cpp
pblock2->GetValue(pb_color, edge.birthTime, edgeColor, civld);
gw->setColor(LINE_COLOR, (Point3)edgeColor);
```

The engine does **not** have per-edge color variation — it uses a single uniform `diffuseClr` for all vertices. Per-edge color is a plugin-added feature that provides useful artistic feedback when color is animated over time.

**Recommendation**: This is a nice enhancement. Keep it.

---

## Summary — Accuracy Scorecard

| Feature | Accuracy | Priority |
|---------|----------|----------|
| Edge structure (deque vs ring buffer) | Acceptable for preview | Low |
| **Gravity formula (½ strength)** | **Wrong — 2× error** | **High** |
| Sub-frame Hermite interpolation | Missing | Medium |
| Texture UV animation | Not implemented | Low (wireframe-only) |
| Rendering (wireframe vs tri-strip) | Different purpose | Low |
| Edge emission timing | Adequate | Low |
| Default above/below (20 vs 10) | Wrong defaults | Low |
| Axis conventions | Correct | — |
| Edge count cap (128) | Artificial limit | Low |
| Alpha not applied | Minor omission | Low |

---

## Recommended Changes (Priority Order)

1. **Fix gravity formula** in `ComputeEdgeWorldPos()`:
   ```cpp
   // Before:
   float gravOffset = -0.5f * gravity * ageSec * ageSec;
   // After (matches engine's g·t² accumulation):
   float gravOffset = -gravity * ageSec * ageSec;
   ```

2. **Fix default heights** — change `p_default` for `pb_height_above` and `pb_height_below` from `20.0f` to `10.0f`.

3. **Remove or raise the 128 edge cap** in `BuildEdgeTrail()`.

4. **(Optional) Add mesh generation** for renderable output with proper UV animation matching the engine formula:
   ```
   u = tmpDU * age * ooLifeSpan + texSlotBox.l
   ```

5. **(Optional) Add tangent-based interpolation** to better match the engine's Hermite-like sub-frame edge placement.
