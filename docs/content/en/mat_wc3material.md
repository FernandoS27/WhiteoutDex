# Wc3Material

Every mesh that you export needs a **Wc3Material**. It holds the textures and tells the game how to draw them. The importer creates Wc3Materials for you. For your own meshes, create one in the Material Editor or convert your materials with *Material Fix*.

The pictures show a material of the sample model in the Compact Material Editor.

## Material Basic Parameters

[[shot:mat_wc3material_r1|Material Basic Parameters.]]

| No. | What it does |
|---|---|
| [[ctl:3]] | **Shader.** *SD* for classic models, *HD* for Reforged, *SD on HD* for classic textures in a Reforged model, *Crystal* for the crystal shader of Reforged. |
| [[ctl:5]] | **Filter mode** – how the material is drawn (see the table below). |
| [[ctl:8]] | Both sides of the faces are drawn. |
| [[ctl:9]] | Ignores the light. Good for glowing parts. |
| [[ctl:10]] | Ignores the fog. |
| [[ctl:11]] | One color for the whole mesh. |
| [[ctl:12]] | Sets the *Full Resolution* flag of the material. |
| [[ctl:13]] | Drawn on top of other things. |
| [[ctl:14]] | Does not hide what is behind it. |
| [[ctl:15]] | The player cannot click this part. |
| [[ctl:16]] | Uses ambient occlusion (Reforged). |
| [[ctl:17]] | Back faces cast shadows (Reforged). |
| [[ctl:18]] | Not lit at all (Reforged). |
| [[ctl:20]] | Draw order of transparent layers: near to far, or far to near. |
| [[ctl:24]] | Draw order between materials (−20 to 20). Higher is drawn later. |
| [[ctl:26]] | Uses a texture that the game fills in: team color, team glow, cliff or one of the tree textures. |

| Filter mode | Looks like | Use it for |
|---|---|---|
| None | Solid. | Normal parts. |
| Transparent | Holes where the alpha is low, hard edges. | Leaves, hair, fences. |
| Blend | Smooth transparency. | Glass, soft edges, shadows. |
| Additive | Adds light, glows. Black is invisible. | Fire, magic, light rays. |
| AddAlpha | Adds light, uses the alpha. | Glows with a shape. |
| Modulate | Darkens what is behind. | Dark marks, shadows. |
| Modulate 2x | Darkens or brightens. | Special effects. |

## Layer Animation

[[shot:mat_wc3material_r2|Layer Animation.]]

| No. | What it does |
|---|---|
| [[ctl:6]] | Opacity of the layer (0 to 1). Animate it to fade the material in and out. |
| [[ctl:7]] | How the opacity changes between keys. |
| [[ctl:9]] | Writes the opacity animation into the model. |
| [[ctl:11]] | The texture changes in steps over time, like a flipbook. |
| [[ctl:14]] | Which UV channel the texture uses. |
| [[ctl:15]] | Writes the texture-change animation into the model. |

Moving textures (UV animation) are set on the *Wc3Bitmap*, not here.

## Material Texture Maps

[[shot:mat_wc3material_r3|Material Texture Maps.]]

| No. | What it does |
|---|---|
| [[ctl:4]] | **Diffuse:** the color texture. Classic models use only this one. |
| [[ctl:5]] | Opens the texture browser for this slot (textures from the game). |
| [[ctl:10]] | **Normal:** the surface details (Reforged). |
| [[ctl:16]] | **ORM:** occlusion, roughness and metal in one texture (Reforged). |
| [[ctl:22]] | **Emissive:** parts that glow by themselves (Reforged). |
| [[ctl:28]] | **Team Color:** a mask for the team color (Reforged, optional). |
| [[ctl:34]] | **Environment:** the reflection texture (Reforged). |

Each slot has a **Path Prefix**: the folder that the exporter writes in front of the file name, for example `Textures\`.

## Reforged Settings

[[shot:mat_wc3material_r4|Reforged Settings.]]

| No. | What it does |
|---|---|
| [[ctl:5]] | How strong the emissive texture glows. |
| [[ctl:9]] | Fresnel (edge light) color: red … |
| [[ctl:12]] | … green … |
| [[ctl:15]] | … and blue. |
| [[ctl:18]] | Strength of the edge light. |
| [[ctl:21]] | How much of the team color the edge light uses. |
