# Particle Emitter 1

The Particle Emitter 1 does not send out pictures but **small models**. Use it for things like falling leaves, stones or arrows that are models themselves.

Create it with **Create > Geometry > Particle Systems > Wc3Particles1**.

## Particle Configuration

[[shot:obj_particles1_r1|Particle Configuration.]]

| No. | What it does |
|---|---|
| [[ctl:2020]] | Loads all settings from an `.ini` file. |
| [[ctl:2021]] | Saves all settings to an `.ini` file. |
| [[ctl:2022]] | When on, *Import* also loads the animated keys of the values. |

## Emitter Options

[[shot:obj_particles1_r2|Emitter Options.]]

| No. | What it does |
|---|---|
| [[ctl:5002]] | How fast the models fly away. |
| [[ctl:5004]] | Up and down angle of the direction. |
| [[ctl:5006]] | Side angle of the direction. |
| [[ctl:5008]] | Pulls the models down. |
| [[ctl:2030]] | **Control:** how an animated value changes between keys. Each value has its own control. |

## Timing Options

[[shot:obj_particles1_r3|Timing Options.]]

| No. | What it does |
|---|---|
| [[ctl:5010]] | How many seconds one model lives. |
| [[ctl:5012]] | How many models start per second. |

## Model Options

[[shot:obj_particles1_r4|Model Options.]]

| No. | What it does |
|---|---|
| [[ctl:5017]] | The folder of the model inside the game. |
| [[ctl:5016]] | The file name of the model, for example `Abilities\Weapons\Arrow\ArrowMissile.mdl`. |
| [[ctl:2024]] | Choose a model from the game archives. |
| [[ctl:2023]] | Choose a model file on your disk. |
| [[ctl:5014]] | Size of the models in the viewport preview. |

!!! note
    Classic models often write the extension `.mdl` in this path, even when the file in the game is an `.mdx`. The game finds both.
