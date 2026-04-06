# Wc3Particles2 Plugin — Accuracy Report

Comparison of the 3ds Max `Wc3Particles2` plugin (`Particles.cpp`) against the
verified engine pseudocode for `CPlaneParticleEmitter` and `CParticleEmitter2`.

---

## 1. Parameter Mapping

| Engine Field | Plugin Param | Match | Notes |
|---|---|---|---|
| `m_width` (+0x318) | `PB_WIDTH` | **Yes** | Direct float, correct. |
| `m_height` (+0x31C) | `PB_HEIGHT` | **Yes** | Direct float, correct. |
| `m_latitude` (+0x320) | `PB_ANGLE_Y` | **Partial** | Plugin maps a single cone half-angle to latitude. See §3. |
| `m_longitude` (+0x324) | *(missing)* | **No** | Plugin has no longitude parameter. Hardcodes `angleZ = 360°`. See §3. |
| `m_particleVelocity` | `PB_SPEED` | **Partial** | Correct source, but plugin applies a `*-0.0025f` scaling factor. See §4. |
| `m_particleVelocityVariation` | `PB_VARIATION` | **No** | Parameter exists but is **never applied** during birth. See §4. |
| `m_particleEmissionRate` | `PB_INITVEL` | **Yes** | Emission rate in particles-per-second. |
| `m_particleLifeSpan` | `PB_LIFE` | **Yes** | Lifetime in seconds, converted to ticks internally. |
| `m_particleAcceleration` | `PB_GRAVITY` | **Partial** | Correct source, but scaling and integration differ. See §6. |
| `m_particleTailLength` | `PB_TAIL_LEN` | **Partial** | Engine uses tail length for velocity-based streak. Plugin uses it as a width multiplier on a birth-to-current streak. See §8. |
| `m_particleKeys[2]` | `PB_*_START/MID/END` | **Partial** | Engine uses a 2-element `CParticleKey` array with `endTime`-based keyframes. Plugin uses a 3-point linear interpolation with `midtime`. See §7. |
| `m_flags` bit 2 (head) / bit 3 (tail) | `PB_TYPE` | **Yes** | 0=Head, 1=Tail, 2=Both. Correct mapping. |
| `m_flags` bit 4 (sortZ) | `PB_SORT` | **No** | Parameter exists but sorting is **not implemented** in the simulation loop. |
| `m_flags` bit 5 (squirt) | `PB_SQUIRT` | **Partial** | Both have burst mode, but the implementations are completely different. See §5. |
| `m_flags` bit 9 (model-space) | `PB_MODELSPACE` | **Yes** | Correctly skips world-space transform when set. |
| `m_flags` bit 10 (XY quads) | `PB_XYQUAD` | **Yes** | Correctly aligns quads to XY plane. |
| `m_textureRows` / `m_textureColumns` | `PB_ROWS` / `PB_COLS` | **Yes** | Correct. Not used in viewport sim, only metadata. |
| — | `PB_LINE_EMIT` | **Extra** | Not present in engine. Changes angle sign convention. See §3. |
| — | `PB_LATITUDE` | **Unused** | Defined (TYPE_INT, -100..100) but never read in BirthParticle or UpdateParticles. |

---

## 2. Position Randomization (BirthParticle)

### Engine (CPlaneParticleEmitter::CreateParticle)

```cpp
float y = reals_(m_rndSeed) * m_height * 0.5f;  // [-h/2, +h/2]
float x = reals_(m_rndSeed) * m_width  * 0.5f;  // [-w/2, +w/2]
// position = (x, y, 0)
```

`reals_()` returns a value in **[-1, +1]**, so `reals_() * dim * 0.5` directly produces
a uniform random position on `[-dim/2, +dim/2]`.

### Plugin (GenParticle::BirthParticle)

```cpp
pos.x = -width / 2.0f + randFloat() * width;
pos.y = -height / 2.0f + randFloat() * height;
```

`randFloat()` is `dist01(m_rng)`, returning **[0, 1]**. The expression
`-dim/2 + rand01 * dim` is mathematically equivalent to `[-dim/2, +dim/2]`.

### Verdict: **Correct**

The formula produces identical distributions. The axis mapping (X = width, Y = height)
also matches the engine. Minor note: the engine randomizes Y first, then X; the plugin
does X then Y. This changes the RNG sequence but has no statistical impact.

---

## 3. Angle / Velocity Direction (BirthParticle)

### Engine (CPlaneParticleEmitter::CreateParticle)

```cpp
float rotY = m_latitude  * reals_(m_rndSeed);  // [-lat, +lat]
float rotZ = m_longitude * reals_(m_rndSeed);  // [-lon, +lon]

float speed = CalcVelocity();
C4Vector vel(0, 0, speed, 0);  // along +Z

// Latitude rotation (XZ plane, around Y)
vel.x = vel.z * sinf(rotY);
vel.z = vel.z * cosf(rotY);

// Longitude rotation (XY plane, around Z)
vel.y = vel.x * sinf(rotZ);
vel.x = vel.x * cosf(rotZ);
```

Latitude and longitude are **separate parameters** (both in radians, both using `reals_` for
signed random). The decomposition is:

```
x = speed * sin(lat) * cos(lon)
y = speed * sin(lat) * sin(lon)
z = speed * cos(lat)
```

This is a **spherical coordinate** parameterization with two independent angular controls.

### Plugin (GenParticle::BirthParticle)

```cpp
constexpr float angleZ = 360.0f;
float randomAngleY;
if (line_emitter)
    randomAngleY = -angleY + randFloat() * 2.0f * angleY;
else
    randomAngleY = randFloat() * angleY;
float randomAngleZ = randFloat() * angleZ;

Matrix3 rot;
rot.SetRotateY(randomAngleY * kDegToRad);
if (!line_emitter)
    rot.RotateZ(randomAngleZ * kDegToRad);
vel = rot * vel;
```

The plugin uses **degrees**, a **single cone angle** (`PB_ANGLE_Y`), and hardcodes
the azimuth sweep to `360°`. The `line_emitter` toggle changes the randomization
from unsigned `[0, angleY]` to signed `[-angleY, +angleY]` and skips the Z rotation.

### Discrepancies

| Aspect | Engine | Plugin | Impact |
|---|---|---|---|
| **Longitude** | Independent `m_longitude` param | Hardcoded 360° | Cannot reproduce narrow azimuth fans. |
| **Angle units** | Radians (stored as-is) | Degrees (converted at use) | Unit mismatch if importing raw MDX values without conversion. |
| **Random distribution** | `reals_()` = symmetric [-1,+1] for both | `dist01()` = unsigned [0,1] for lat, unsigned for lon | Latitude is one-sided by default (only positive cone). |
| **Line emitter** | Not a concept | `PB_LINE_EMIT` toggle | Engine has no equivalent — line emission is controlled by setting longitude=0. |
| **Rotation method** | Manual sin/cos decomposition | `Matrix3::SetRotateY/RotateZ` | Matrix multiply introduces rounding, but mathematically equivalent operations. |
| **Initial direction** | Along **+Z** | Along **-Z** (via `initVel *= -0.0025f`) | Flipped emission direction. |

### Recommendations

1. **Add a longitude parameter** (`PB_LONGITUDE`) as a separate float in degrees. Replace
   the hardcoded `angleZ = 360.0f` with this parameter.
2. **Use signed random for latitude**: change from `randFloat() * angleY` to
   `(randFloat() * 2.0f - 1.0f) * angleY` (or equivalently, `reals_() * angleY`).
   The engine always randomizes latitude symmetrically around zero.
3. **Remove `line_emitter`** or document it as a plugin-only convenience. Engine line
   emission is achieved by setting longitude to 0.
4. **Fix initial velocity direction**: emit along **+Z** to match the engine, and remove
   the sign flip in the velocity scaling.

---

## 4. Speed and Variation

### Engine (CParticleEmitter2::CalcVelocity)

```cpp
float randomFactor = reals_(m_rndSeed);  // [-1, +1]
return m_particleVelocity * (1.0f + randomFactor * m_particleVelocityVariation);
```

Variation randomizes speed symmetrically: for `variation = 0.5`, speed ranges from
`0.5 × base` to `1.5 × base`.

### Plugin (GenParticle::BirthParticle)

```cpp
pblock2->GetValue(PB_SPEED, bt, initVel, forever);
pblock2->GetValue(PB_VARIATION, bt, var, forever);
// ...
initVel *= -0.0025f;
Point3 vel(0.0f, 0.0f, -initVel);
```

The `var` value is **read but never used**. There is no `* (1.0f + random * var)` term.
Additionally, the speed is scaled by `-0.0025f` — a factor not present in the engine.

### Discrepancies

| Aspect | Engine | Plugin |
|---|---|---|
| **Variation applied** | Yes, `speed * (1 + reals * var)` | No, `var` is unused |
| **Scaling factor** | None — raw velocity value | `* -0.0025f` arbitrary constant |
| **Direction** | Positive (along +Z) | Negative (along -Z via double negation) |

### Recommendations

1. **Apply variation**: After reading `initVel` and `var`, compute:
   ```cpp
   float rndVar = dist_signed(m_rng);  // [-1, +1]
   initVel *= (1.0f + rndVar * var);
   ```
2. **Remove the `-0.0025f` factor**, or document its origin. The engine uses the raw MDX
   velocity value. If this factor compensates for Max-to-WC3 unit conversion, it should
   be applied uniformly via the Import/Export path, not baked into the simulation.

---

## 5. Emission Accumulation and Squirt Mode

### Engine (CParticleEmitter2::InternalUpdate)

**Normal emission:**
```cpp
float emission = elapsed * m_particleEmissionRate * ParticleSystemManager::GetScaler();
m_numNew += emission;                  // fractional accumulator
unsigned int numNew = (unsigned int)m_numNew;
// spawn up to numNew, then subtract actual spawned count
m_numNew -= (float)(int)numEmitted;
```

The fractional accumulator (`m_numNew`) ensures sub-frame precision. If the rate
is 1.5 particles/frame, it correctly alternates between 1 and 2 births over time.

**Squirt mode:**
```cpp
if (m_flags & 0x20)  // squirt flag
{
    unsigned int numToEmit = (int)(m_particleEmissionRate * GetScaler());
    // spawn all at once from dead pool
    m_flags &= ~0x20;  // clear after burst
}
```

Squirt is flag-based: set bit 5 → next update spawns a burst → clear bit 5.

### Plugin (GenParticle::UpdateParticles)

**Normal emission:**
```cpp
birth = int(float(tvalid - t0) * brate * brateFactor)
      - int(float(tvalid - t0 - dt) * brate * brateFactor);
```

This integral-counting approach avoids a persistent accumulator by computing the
difference in cumulative births between two time points. It can produce the same
results as the engine's fractional accumulator, but diverges on fractional rates
due to integer truncation boundaries.

**Squirt mode:**
```cpp
Control* emitCtrl = pblock2->GetControllerByID(PB_INITVEL);
// scan keys backward looking for zero→positive transitions
// burst total particle count at each transition
```

The plugin scans 3ds Max animation controller keys for rising-edge transitions.
This is a higher-level approach that works in the Max animation system but has
no correspondence to the engine's simple flag toggle.

### Discrepancies

| Aspect | Engine | Plugin |
|---|---|---|
| **Accumulator** | Fractional `m_numNew` float | Integer difference formula |
| **Sub-frame precision** | Carries fractional remainder across frames | Loses remainder each frame |
| **Squirt trigger** | Single flag bit (0x20) | Key-scanning on emission-rate controller |

### Recommendations

1. **Use a fractional accumulator** like the engine. Add a `float m_numNew = 0.0f` member
   and accumulate `elapsed * rate` each step, spawning `(int)m_numNew` and subtracting
   the integer part.
2. **Simplify squirt mode** to a flag-based burst. When squirt is enabled and the emission
   rate transitions from 0 to positive, set a burst flag. On the next update, spawn
   `rate * scaler` particles and clear the flag.

---

## 6. Physics Integration (MoveParticle / Gravity)

### Engine (CParticleEmitter2::MoveParticle)

```cpp
C3Vector accel(0, 0, -m_particleAcceleration);
C3Vector displacement = p.m_velocity * elapsed + accel * (0.5f * elapsed * elapsed);
p.m_position += displacement;
p.m_velocity += accel * elapsed;
```

This is **Euler integration with the kinematic correction term**
(`½at²`), giving second-order accuracy for constant acceleration.

### Plugin (GenParticle::UpdateParticles)

```cpp
float gAccel = gravity * -0.0025f / float(TICKS_PER_SEC);
parts.vels[g].z += gAccel * float(dt);
// ...
parts[n] += parts.vels[n] * float(dt);
```

The plugin uses **semi-implicit Euler**: update velocity first, then integrate position
with the *new* velocity. This is a different integration scheme. Additionally:

- Gravity uses the `-0.0025f` scaling factor (same as velocity)
- Velocity update and position integration happen in **separate loops**
- Gravity is only applied on **full frames** (`if (fullframe)`), not every sub-step

### Discrepancies

| Aspect | Engine | Plugin |
|---|---|---|
| **Integration** | Kinematic (`½at²` term) | Semi-implicit Euler (no `½at²`) |
| **Gravity scaling** | Raw `m_particleAcceleration` | `gravity * -0.0025f / TICKS_PER_SEC` |
| **Gravity frequency** | Every update call | Only on full frames |
| **Loop structure** | Single per-particle loop (age+move+kill) | Separate loops for age, gravity, integrate |

### Recommendations

1. **Add the `½at²` displacement term** to match the engine's kinematic integration:
   ```cpp
   float halfAT2 = 0.5f * gAccel * dt * dt;
   parts[n] += parts.vels[n] * dt;
   parts[n].z += halfAT2;
   parts.vels[n].z += gAccel * dt;
   ```
2. **Apply gravity every sub-step**, not just on full frames.
3. **Remove the `-0.0025f` factor** from gravity, same as for velocity.

---

## 7. Keyframe Interpolation

### Engine (CParticleKey::Interpolate)

The engine uses a **2-element array** of `CParticleKey` structures, each containing:
- `m_endTime` — absolute time boundary
- Color (start/end `CImVector`), alpha, scale, and sprite-sheet cell indices

Particles track a `m_keyFrame` index (0 or 1). When `p.m_age > key[kf].m_endTime`,
the keyframe index advances. The `Interpolate` function computes a normalized `t`
within the current key's time range and lerps all visual properties.

The keyframe advance is a **while loop**:
```cpp
while (p.m_keyFrame < 2 && p.m_age > m_particleKeys[p.m_keyFrame].m_endTime)
    ++p.m_keyFrame;
```

If `m_keyFrame` reaches 2, the particle is killed.

### Plugin (InterpOverLife)

```cpp
static float InterpOverLife(float u, float midtime, float start, float mid, float end)
{
    if (u <= midtime)
        return start + (mid - start) * (u / midtime);
    else
        return mid + (end - mid) * ((u - midtime) / (1.0f - midtime));
}
```

The plugin uses a **3-point interpolation** with a single `midtime` parameter.
`u` is normalized particle age (`age / lifespan`). There is no keyframe index,
no `endTime`-based advancement, and no concept of killing a particle when it passes
a keyframe boundary.

### Discrepancies

| Aspect | Engine | Plugin |
|---|---|---|
| **Structure** | 2 CParticleKeys with endTime boundaries | 3-point interpolation with midtime ratio |
| **Time basis** | Absolute age vs. `key.m_endTime` | Normalized `age / lifespan` |
| **Keyframe advance** | While-loop advance, kills particle at kf=2 | No keyframe tracking |
| **Per-key properties** | Color, alpha, scale, head/tail cells (all per-key) | Color, scale use InterpOverLife; alpha not interpolated in viewport |

### Verdict

The plugin's 3-point system is a **reasonable approximation** for preview purposes.
The engine's 2-keyframe system with absolute times is more flexible (asymmetric keys,
early termination on keyframe overflow), but the visual result for typical emitters
is similar when `midtime ≈ key[0].m_endTime / lifespan`.

### Recommendation

This is acceptable for a viewport preview plugin. A more accurate approach would be:
1. Track a `keyFrame` index per particle.
2. Store two CParticleKey-style entries with explicit `endTime` values.
3. Kill particles when keyFrame >= 2.

---

## 8. Tail Rendering

### Engine (RenderParticle)

The engine computes tail geometry using the **particle's velocity direction** and a
`m_particleTailLength` multiplier:
- Tail direction = normalized velocity vector
- Tail extent = position ± tailLength along velocity
- Quad width from camera-facing cross product

### Plugin (DrawParticle)

```cpp
Point3 head = parts[i];
Point3 tail = obj->birthPos[i];
Point3 dir = head - tail;
```

The plugin draws a streak from **birth position to current position**. The `tailLen`
parameter is used as a **width multiplier** on the perpendicular cross product, not
as a length along the velocity vector.

### Discrepancies

| Aspect | Engine | Plugin |
|---|---|---|
| **Tail direction** | Along velocity vector | Birth-to-current position |
| **Tail length** | `m_particleTailLength` * velocity | Fixed length (birth→current distance) |
| **`tailLen` role** | Streak length multiplier | Cross-section width multiplier |

### Recommendations

1. **Use velocity-based tails**: Replace `birthPos[i]` with a point offset along the
   velocity direction:
   ```cpp
   Point3 tailDir = Normalize(parts.vels[i]);
   Point3 tail = head - tailDir * tailLen;
   ```
2. **Repurpose `PB_TAIL_LEN`** as the actual tail length along velocity, matching the
   engine's `m_particleTailLength`.

---

## 9. Sub-frame Age Randomization

### Engine (CPlaneParticleEmitter::CreateParticle)

```cpp
float rnd = real_(m_rndSeed);  // [0, 1]
p.m_age = elapsed * rnd;
```

Newly born particles get a random initial age in `[0, elapsed]`. This distributes
births across the time step, avoiding visible "popping" where all particles in a
frame start at age 0.

### Plugin (GenParticle::BirthParticle)

```cpp
parts.ages[index] = 0;
```

All particles born in a given step start at age 0.

### Recommendation

Add sub-frame age randomization:
```cpp
float rndAge = dist01(m_rng);
parts.ages[index] = static_cast<int>(rndAge * static_cast<float>(dt));
```

---

## 10. Alive/Dead Pool Management

### Engine

Uses a dual-stack system:
- `m_alive` (CParticleStack): indices of living particles
- `m_dead` (CParticleStack): indices of available slots
- `m_particles` (TSGrowableArray): the actual particle data

Birth pops from `m_dead`, pushes to `m_alive`. Death removes from `m_alive`, pushes
to `m_dead`. A compaction pass (`m_particles.Clear()`) fires probabilistically when
all particles are dead.

### Plugin

Uses the SimpleParticle `parts` array with `ages[i] = -1` as a sentinel for dead
particles. Birth scans for the first dead slot linearly. No separate alive/dead
tracking.

### Verdict

The plugin's approach is acceptable for a preview tool. The linear scan for dead slots
is `O(count)` per birth but `count` is capped at 500. No change needed for correctness.

---

## 11. RNG System

### Engine

```cpp
NTempest::CRndSeed m_rndSeed;
// Seeded once: seed = (rand() << 16) | (rand() & 0xFFFF)
// Uses CRandom::real_() for [0,1], reals_() for [-1,+1], C3Vector_() for unit random
```

Each emitter has its own `CRndSeed` instance, seeded once at construction. All random
calls advance the same seed sequentially, giving deterministic per-emitter sequences.

### Plugin

```cpp
std::mt19937 m_rng;
// Re-seeded every sub-step from a Permutation table:
//   combinedSeed = Perm(seed1)<<24 + Perm(seed2)<<16 + Perm(seed3)<<8 + Perm(seed4) + PARTICLE_SEED
```

The plugin **re-seeds the RNG every sub-step** based on the current time. This makes
the output time-deterministic (scrubbing the timeline gives the same result) but
produces a fundamentally different random sequence from the engine.

### Verdict

The re-seeding approach is a deliberate design choice for Max timeline scrubbing
support. The engine's single-seed approach is not compatible with Max's non-linear
time evaluation. This divergence is **acceptable and intentional**.

---

## 12. Missing Features

| Feature | Engine | Plugin |
|---|---|---|
| `SyncAllocation()` | Resizes particle pool on rate/life change | Not implemented — pool is fixed at PB_COUNT |
| `Flush()` | Kills all alive particles | Not implemented |
| `DestroyParticle()` virtual | Override point for cleanup | Not implemented |
| Depth sorting in simulation | `CPriorityQ` sort in `RenderSort()` | `PB_SORT` exists but not used |
| Compact on all-dead | 1-in-32 chance clears arrays | Not implemented |
| `CWar3Mat` material pipeline | Full texture/blend setup | Viewport only — no material system |
| Sprite-sheet UV animation | Head/tail cell indices from CParticleKey | Parameters exist but not rendered in viewport |

---

## 13. Summary of Accuracy

| Category | Rating | Notes |
|---|---|---|
| **Parameter set** | Good | All major parameters present. Missing longitude. |
| **Position randomization** | Excellent | Mathematically equivalent to engine. |
| **Angle/velocity direction** | Poor | Single angle instead of lat+lon; wrong direction; no variation. |
| **Speed variation** | Broken | Parameter read but never applied. |
| **Emission accumulation** | Fair | Integer counting vs. fractional accumulator. |
| **Squirt mode** | Poor | Completely different mechanism. |
| **Physics integration** | Fair | Missing `½at²` term; gravity only on full frames. |
| **Keyframe interpolation** | Fair | 3-point approx vs. 2-key system. Acceptable for preview. |
| **Tail rendering** | Poor | Birth-to-current streak vs. velocity-based tail. |
| **Sub-frame age** | Missing | All particles born at age 0. |
| **Billboard rendering** | Good | Camera-facing + XY quad + model-space all correct. |
| **Alive/dead management** | Acceptable | Different but functionally similar for preview. |

---

## 14. Priority Improvements

Ranked by visual impact and implementation effort:

1. **Apply speed variation** — one-line fix, currently broken.
2. **Add longitude parameter** — new `PB_LONGITUDE` float, replace hardcoded `angleZ`.
3. **Fix velocity direction** — emit along +Z, remove `-0.0025f` factor.
4. **Use signed random for latitude** — `(rand * 2 - 1) * angle` instead of `rand * angle`.
5. **Velocity-based tail rendering** — use velocity direction instead of birth position.
6. **Add `½at²` gravity term** — small physics accuracy improvement.
7. **Sub-frame age randomization** — reduces visible particle popping.
8. **Fractional emission accumulator** — better sub-frame birth distribution.
9. **Remove magic `-0.0025f` factor** — or move to Import/Export conversion only.
