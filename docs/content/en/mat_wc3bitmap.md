# Wc3Bitmap

The **Wc3Bitmap** is the texture map of WhiteoutDex. Put it into the slots of a *Wc3Material*. It reads `.blp`, `.dds`, `.tga`, `.png` and the other image formats of 3ds Max, and it holds the texture animation (UV animation) and the path of the texture in the game.

## WC3 Texture Parameters

[[shot:mat_wc3bitmap_r1|WC3 Texture Parameters.]]

| No. | What it does |
|---|---|
| [[ctl:6]] | Choose an image file on your disk. |
| [[ctl:7]] | Choose a texture from the game archives. |
| [[ctl:9]] | Uses a texture that the game fills in (team color, team glow, cliff, trees). |
| [[ctl:12]] | The texture repeats across the width. |
| [[ctl:13]] | The texture repeats across the height. |
| [[ctl:14]] | Uses the texture as a reflection (sphere environment map). |

## UV Animation (TXAN)

[[shot:mat_wc3bitmap_r2|UV Animation.]]

Animate these values to make the texture move. The *Texture Animation Creator* can set them for you.

| No. | What it does |
|---|---|
| [[ctl:3]] | How the values change between keys. |
| [[ctl:8]] | Moves the texture sideways (U). |
| [[ctl:11]] | Moves the texture up and down (V). |
| [[ctl:14]] | W offset. |
| [[ctl:17]] | Turns the texture. |
| [[ctl:20]] | Repeats the texture more or less often sideways. |
| [[ctl:23]] | Repeats the texture more or less often up and down. |

## Prefix Path

[[shot:mat_wc3bitmap_r3|Prefix Path.]]

The prefix is the folder the exporter writes in front of the texture name, for example `Textures\` or `war3mapImported\`.

| No. | What it does |
|---|---|
| [[ctl:5]] | Choose a saved prefix. |
| [[ctl:6]] | Edit the list of saved prefixes. |

!!! tip
    Textures in a map must use the prefix the map uses. For custom models in maps this is usually `war3mapImported\`.
