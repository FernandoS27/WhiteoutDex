# The Exporter

The exporter saves your scene as a Warcraft III model (`.mdx` or `.mdl`). It can also convert your textures to the game formats.

## Open the exporter

- Menu **WhiteoutDex > [[t:menu_exporter]]**
- The **Export** button in the WhiteoutDex sidebar
- **File > Export** and choose *Warcraft III MDX/MDL Model*

Choose a file name. Then the export window opens.

## The export window

[[shot:exporter|The export window with the Geometry tab.]]

The top of the window shows the file name. Under it you see if the file is new or if a file with this name is already there.

| No. | What it does |
|---|---|
| [[ctl:1001]] | **Model name.** The name inside the file. The game does not show it, but other tools do. |
| [[ctl:1011]] | Export for the classic game (MDX version 800). Textures become `.blp` files. |
| [[ctl:1012]] | Export for Reforged (MDX version 1800). Textures become `.dds` files. |
| [[ctl:1015]] | Shows the mesh options (this picture). |
| [[ctl:1016]] | Shows the texture options. |
| [[ctl:1021]] | Combines meshes that use the same material into one. The model gets smaller and faster. |
| [[ctl:1027]] | Makes the normals the same where two meshes touch. This hides seams. Do not use it if you use smoothing groups for hard edges. |
| [[ctl:1028]] | Also exports bones and helpers that nothing uses. Normally the exporter leaves them out. |
| [[ctl:1032]] | Changes the skin weights so that they work in the classic game. The classic game can only use simple weights. Only for [[t:exp_format_classic_title]]. |
| [[ctl:1030]] | When the file already exists, the exporter adds `_1`, `_2` and so on to the name. Your old file stays. |
| [[ctl:1031]] | Opens the folder of the file after the export. |
| [[ctl:1]] | Starts the export. |
| [[ctl:2]] | Closes the window without exporting. |

The green box under the options tells you that WhiteoutDex found no problems in your scene. When it finds problems, the box is yellow. See *Scene problems and Fix all*.

### Textures tab

[[shot:exporter_textures|The Textures tab, here for a classic export.]]

| No. | What it does |
|---|---|
| [[ctl:1060]] | Converts the textures of your materials to `.blp` (Classic) or `.dds` (Reforged) and saves them next to the model. |
| [[ctl:1071]] | **Classic only.** [[t:exp_blp_paletted_item]] or *JPEG*. JPEG has more colors. Paletted files are smaller. With JPEG you also set the quality (1–100). |
| [[ctl:1075]] | Mixes the colors a little. This hides hard steps between colors in paletted textures. |
| [[ctl:1061]] | Makes the smaller versions of the texture that the game uses for far away models. Leave it on. |
| [[ctl:1062]] | Replaces texture files that are already there. When it is off, the exporter keeps existing files. |

For **Reforged** you choose the DDS format instead: *BC3 (DXT5)* or *BC7*. BC7 looks better. BC3 works on older graphics cards.

## Classic or Reforged?

| | Classic | Reforged |
|---|---|---|
| Game | Warcraft III 1.x and the classic graphics of Reforged | Reforged graphics (HD) |
| MDX version | 800 | 1800 |
| Textures | `.blp` | `.dds` |
| Skin | Simple weights (use [[t:exp_quantize_skin_weights_chk]]) | Full weights |

!!! tip
    The exporter remembers all options for the next export.
