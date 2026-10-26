# Light

A Warcraft III light shines on the model and on the ground around it, for example the glow of a torch or a magic crystal.

Create it with **Create > Helpers > Warcraft III Plugins > Wc3 Light**.

## General Functions

[[shot:obj_light_r1|General Functions.]]

| No. | What it does |
|---|---|
| [[ctl:3]] | Click this, then click an object. The light jumps to that object and links to it. |

## Basic Parameters

[[shot:obj_light_r3|Basic Parameters.]]

| No. | What it does |
|---|---|
| [[ctl:4]] | *Omnidirectional* shines in all directions from a point. *Directional* shines in one direction like the sun. *Ambient* lights everything evenly. |
| [[ctl:9]] | The distance where the light starts to get weaker. |
| [[ctl:16]] | The distance where the light ends. |
| [[ctl:21]] | The color of the light. |
| [[ctl:29]] | The strength of the light. |
| [[ctl:35]] | The color of the ambient part. |
| [[ctl:43]] | The strength of the ambient part. |
| [[ctl:11]] | **Control:** how an animated value changes between keys. Each value has its own control. |

## Reforged Parameters

[[shot:obj_light_r2|Reforged Parameters.]]

These values are used only by Reforged models.

| No. | What it does |
|---|---|
| [[ctl:6]] | Shadow intensity. On the main light of a scene it also scales the image based lighting. |
| [[ctl:8]] | The light casts shadows. |
| [[ctl:12]] | Distance where the shadow starts. |
| [[ctl:16]] | Distance where the shadow ends. |
| [[ctl:21]] | How fast the light gets weaker: quadratic part … |
| [[ctl:25]] | … linear part … |
| [[ctl:29]] | … and damping. |
