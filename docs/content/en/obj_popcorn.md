# Popcorn FX

**Popcorn FX** is the particle system of Warcraft III: Reforged. The effect itself is a `.pkfx` (or `.pkb`) file made with the PopcornFX editor. The Popcorn object in 3ds Max places this effect on the model and controls when it plays.

Create it with **Create > Helpers > Warcraft III Plugins > Wc3 Popcorn**. Popcorn only works in **Reforged** models.

## Popcorn FX File

[[shot:obj_popcorn_r2|Popcorn FX File.]]

| No. | What it does |
|---|---|
| [[ctl:9]] | Choose a `.pkb` or `.pkfx` effect from the game archives. |
| [[ctl:10]] | Choose an effect file on your disk. |
| [[ctl:11]] | Shows a short explanation. |

The two fields above the buttons hold the path of the effect and the folder in front of it.

## Animation Visibility Guide

[[shot:obj_popcorn_r3|Animation Visibility Guide.]]

| No. | What it does |
|---|---|
| [[ctl:4]] | **On:** the effect plays in all animations. **Off:** it plays in none. |
| [[ctl:6]] | Exceptions: in these animations the effect does the opposite. |
| [[ctl:8]] | Removes the selected exception. |
| [[ctl:10]] | Choose an animation of the scene … |
| [[ctl:12]] | … and add it as an exception. |

## Emission Settings

[[shot:obj_popcorn_r4|Emission Settings.]]

| No. | What it does |
|---|---|
| [[ctl:6]] | Transparency of the particles. |
| [[ctl:10]] | Particles per second. |
| [[ctl:14]] | Life time of one particle in seconds. |
| [[ctl:18]] | Speed of the particles. |

## Color Settings

[[shot:obj_popcorn_r5|Color Settings.]]

| No. | What it does |
|---|---|
| [[ctl:4]] | The base color of the effect. |
| [[ctl:12]] | Multiplies the color, so the effect gets brighter or darker. |

## Render Flags

[[shot:obj_popcorn_r6|Render Flags.]]

| No. | What it does |
|---|---|
| [[ctl:4]] | The particles ignore the light of the scene. |
| [[ctl:6]] | The particles ignore the fog. |
| [[ctl:8]] | The particles grow and shrink with the model. |
