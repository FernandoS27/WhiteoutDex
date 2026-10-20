# Ribbon Emitter

A ribbon emitter draws a **trail** behind a moving point: the light trail of a sword swing, the tail of a comet, the smoke of a rocket. Link it to the bone that moves, for example the weapon.

Create it with **Create > Geometry > Particle Systems > Wc3Ribbon**.

## Ribbon Material Parameters

[[shot:obj_ribbon_r1|Ribbon Material Parameters.]]

| No. | What it does |
|---|---|
| [[ctl:1079]] | The material of the trail (a *Wc3Material*). Click to put it into the Material Editor. |
| [[ctl:1073]] | The texture is a grid of pictures: number of rows (1, 2, 4, 8 …) … |
| [[ctl:1075]] | … and columns (1, 2, 4, 8 …). |
| [[ctl:1077]] | Which picture of the grid the trail shows. Animate it to make the trail flicker. |

## Ribbon Properties

[[shot:obj_ribbon_r2|Ribbon Properties.]]

| No. | What it does |
|---|---|
| [[ctl:1066]] | The color of the trail. |
| [[ctl:1059]] | How far the trail reaches above the emitter. |
| [[ctl:1067]] | How far it reaches below the emitter. Above + below = width of the trail. |
| [[ctl:1080]] | Transparency: 0 = invisible, 1 = solid. Animate it to 0 to hide the trail. |
| [[ctl:1069]] | How many trail pieces start per second. More pieces make a smoother trail. |
| [[ctl:1071]] | How long one piece stays, in seconds. A longer life makes a longer trail. |
| [[ctl:1082]] | Pulls the trail down. |
| [[ctl:1090]] | **Controller:** how an animated value changes between keys. Each value has its own controller. |

!!! tip
    For a sword trail: emitter at the blade, *Additive* material, alpha 0 except during the swing, life span about 0.2 seconds.
