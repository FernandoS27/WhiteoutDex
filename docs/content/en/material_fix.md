# Material Fix

The exporter can only read **Wc3Material**. Material Fix converts other materials (Standard, Physical, PBR) into Wc3Materials. It keeps the textures and sets the Warcraft III options you choose here.

The same settings are used by [[t:exp_fix_all_btn]] in the export window.

Open it with **WhiteoutDex > [[t:menu_material_fix]]**.

[[shot:material_fix|Material Fix with the settings for classic models (SD).]]

There are two sets of settings: one for classic models (**SD**) and one for Reforged (**HD**).

| No. | What it does |
|---|---|
| [[ctl:4]] | Settings for classic models. |
| [[ctl:5]] | Settings for Reforged models. |

## Rendering

| No. | What it does |
|---|---|
| [[ctl:8]] | How the material is drawn. *Automatic* makes materials with an alpha channel transparent. See *Wc3Material* for all modes. |
| [[ctl:10]] | Both sides of the faces are drawn. |
| [[ctl:11]] | The material ignores light. |
| [[ctl:12]] | The material ignores fog. |
| [[ctl:13]] | Uses one color for the whole mesh. |
| [[ctl:14]] | Drawn on top of everything (no depth test). |
| [[ctl:15]] | Does not hide what is behind it. |
| [[ctl:16]] | Uses ambient occlusion (Reforged only). |
| [[ctl:17]] | Back faces cast shadows (Reforged only). |
| [[ctl:18]] | Takes *two sided*, *unshaded* and *transparency* from the old material instead of the boxes above. |

## Textures

| No. | What it does |
|---|---|
| [[ctl:23]] | The folder that goes in front of the texture names in the model, for example `Textures\`. |
| [[ctl:26]] | How textures repeat: as in the old material, both ways, only U, only V, or not at all. |
| [[ctl:28]] | A normal or bump map becomes the *Normal* map (Reforged). |
| [[ctl:29]] | An emission map becomes the *Emissive* map (Reforged). |

## Fix all and convert now

| No. | What it does |
|---|---|
| [[ctl:32]] | Which settings [[t:exp_fix_all_btn]] in the export window uses: SD or HD. |
| [[ctl:34]] | Adds `_wc3` to the name of each converted material. |
| [[ctl:39]] | Converts the materials of the selected objects now. |
| [[ctl:40]] | Converts all materials of the scene now. |
| [[ctl:42]] | Sets this tab back to the default values. |
| [[ctl:43]] | Closes the window. Your settings are saved. |

!!! tip
    Save your scene before you convert the whole scene.
