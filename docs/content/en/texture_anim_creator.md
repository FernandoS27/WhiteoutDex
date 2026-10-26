# Texture Animation Creator

Makes textures move: flowing water, a burning fire from a sprite sheet, or a flipbook from many images. The tool writes the keys for you.

Open it with **WhiteoutDex > [[t:menu_texture_anim_creator]]**.

There are three kinds of animation:

| Kind | Use it for |
|---|---|
| **Flow Anim** | The texture slides, turns or grows: water, lava, energy. |
| **SEQ Anim (Solo)** | One image with many small pictures in a grid (a *sprite sheet*). The tool shows them one after the other. |
| **SEQ Anim (Many)** | A folder with many images. The tool shows them one after the other (a *flipbook*). |

[[shot:texture_anim_creator|The Texture Animation Creator with SEQ Anim (Solo).]]

## Choose the texture

| No. | What it does |
|---|---|
| [[ctl:4]] | Click a mesh in the viewport to use its material. |
| [[ctl:5]] | Uses the selected mesh. |
| [[ctl:6]] | Uses the material that is active in the Material Editor. |
| [[ctl:8]] | The material. |
| [[ctl:11]] | The texture of the material. The line under it shows its name and size. |
| [[ctl:14]] | Flow Anim. |
| [[ctl:15]] | SEQ Anim (Solo). |
| [[ctl:16]] | SEQ Anim (Many). |

## SEQ Anim (Solo): a sprite sheet

| No. | What it does |
|---|---|
| [[ctl:46]] | The frame where the animation starts. |
| [[ctl:50]] | How often the animation repeats. |
| [[ctl:54]] | How many pictures are in one row of the sheet. |
| [[ctl:58]] | How many rows the sheet has. |
| [[ctl:62]] | The first picture to show. |
| [[ctl:66]] | The last picture to show. |
| [[ctl:68]] | Skips frames between two pictures (0 = every frame shows a new picture). |
| [[ctl:71]] | Forward or backward. |
| [[ctl:74]] | Pictures per second: 10, 15, 30 or 60. |
| [[ctl:77]] | **ON:** the animation loops all the time as a global sequence. **OFF:** it runs in the timeline. |
| [[ctl:80]] | **ON:** keeps the keys the texture already has. **OFF:** replaces them. |
| [[ctl:84]] | Finds the number of columns and rows in the image by itself. |
| [[ctl:85]] | Sets the UV coordinates of the mesh to one picture of the sheet. You need this once. |
| [[ctl:87]] | **ON:** collapses the UV modifier into the mesh after *Generate UV Map*. |
| [[ctl:116]] | Writes the animation. The line above shows how many keys and how long it will be. |

## Flow Anim

[[shot:texture_anim_flow|Flow Anim.]]

1. Set the start and end time.
2. Click the arrows for the direction the texture moves. You can also turn it (left, right) and make it grow or shrink (+, −).
3. Choose *Global animation* ON for a texture that always moves.
4. Click **Generate Animation**.

## SEQ Anim (Many)

[[shot:texture_anim_many|SEQ Anim (Many).]]

1. Choose the folder with the images (button **...**). The images are used in the order of their names.
2. Set the play order, the speed and the start frame.
3. Set the path prefix, for example `Textures\`. The model uses this path for each image.
4. Click **Generate Animation**.

!!! tip
    The texture keys are on the *Wc3Bitmap* (UV animation) or the *Wc3Material* (flipbook). Look there to change them later.
