# Particle Emitter 2

The Particle Emitter 2 makes most effects in Warcraft III: fire, smoke, sparks, dust, magic. It sends out many small flat pictures (particles). Each particle lives a short time and changes its color, size and transparency while it lives.

Create it with **Create > Geometry > Particle Systems > Wc3Particles2**. WhiteoutDex shows the particles right in the viewport.

The pictures below show the emitter of the sample model *HeroPaladin*.

## Particle Configuration

[[shot:obj_particles2_r1|Particle Configuration.]]

| No. | What it does |
|---|---|
| [[ctl:1030]] | Loads all settings from an `.ini` file. |
| [[ctl:1031]] | Saves all settings to an `.ini` file. Use it to copy an effect to another model. |
| [[ctl:1032]] | When on, *Import* also loads the animated keys of the values, not only the fixed values. |

## Texture Options

[[shot:obj_particles2_r2|Texture Options.]]

| No. | What it does |
|---|---|
| [[ctl:1047]] | The folder of the texture inside the game, for example `Textures\`. |
| [[ctl:3075]] | The file name of the texture. |
| [[ctl:1033]] | Choose a texture file on your disk. |
| [[ctl:1034]] | Choose a texture from the game archives. |
| [[ctl:1035]] | How the particles are drawn: *Blend* (normal transparency), *Additive* (glows, good for fire and magic), *Modulate*, *Modulate 2X* (darkens) or *Alpha Key* (hard edges). |
| [[ctl:4113]] | Particles ignore the light. |
| [[ctl:4115]] | Particles ignore the fog. |
| [[ctl:4112]] | Sorts the particles by distance. Use it when transparent particles overlap wrongly. |
| [[ctl:4114]] | Uses the team color or team glow as texture. |
| [[ctl:3094]] | The texture is a grid of pictures: number of rows … |
| [[ctl:3096]] | … and columns. The particle shows the pictures one after the other. |

## Emitter Options

[[shot:obj_particles2_r3|Emitter Options.]]

| No. | What it does |
|---|---|
| [[ctl:3048]] | How fast the particles fly away. |
| [[ctl:3050]] | How much the speed changes from particle to particle (0 = all the same). |
| [[ctl:3052]] | The angle of the cone in which the particles fly. A small angle makes a narrow beam, a large angle a wide spray. |
| [[ctl:3057]] | Pulls the particles down (or up with a negative value). |
| [[ctl:1040]] | **Control:** how an animated value changes between keys (see *Glossary*). Each value has its own control. |

## Timing Options

[[shot:obj_particles2_r4|Timing Options.]]

| No. | What it does |
|---|---|
| [[ctl:3042]] | How many seconds one particle lives. |
| [[ctl:3053]] | How many particles start per second. Animate it to 0 to stop the effect. |
| [[ctl:1020]] | Sends all particles at once at a key, like a burst. |

## Size Options

[[shot:obj_particles2_r5|Size Options.]]

| No. | What it does |
|---|---|
| [[ctl:3044]] | Width of the area where particles start. |
| [[ctl:3046]] | Length of this area. |
| [[ctl:1028]] | Particles start along a line instead of an area. |

## Particle Options

[[shot:obj_particles2_r6|Particle Options.]]

| No. | What it does |
|---|---|
| [[ctl:1023]] | The particle is a flat picture (head). |
| [[ctl:1024]] | The particle is a stretched trail behind its movement (tail). |
| [[ctl:1025]] | Both head and tail. |
| [[ctl:3073]] | Length of the tail. |
| [[ctl:6027]] | When in its life the particle reaches the *middle* values (0.5 = half of its life). |
| [[ctl:6033]] | The color at the start, middle and end of the life. Click a box to choose a color. |
| [[ctl:6037]] | Transparency at start, middle and end: 0 = invisible, 255 = solid. |
| [[ctl:6041]] | Size at start, middle and end. |
| [[ctl:6045]] | Which pictures of the texture grid the head shows while it lives … |
| [[ctl:6049]] | … and while it fades. |
| [[ctl:6053]] | The same for the tail while it lives … |
| [[ctl:6057]] | … and while it fades. |

## Other Options

[[shot:obj_particles2_r7|Other Options.]]

| No. | What it does |
|---|---|
| [[ctl:3059]] | Draw order. Higher values are drawn later (on top). |
| [[ctl:4116]] | Particles move with the model. Without it they stay where they started. |
| [[ctl:4117]] | Particles lie flat on the ground instead of facing the camera. Good for rings and waves. |

!!! tip
    For fire and magic use *Additive*, a small cone angle and a short lifespan. Let the alpha go from 0 to 255 and back to 0, so the particles fade in and out softly.
