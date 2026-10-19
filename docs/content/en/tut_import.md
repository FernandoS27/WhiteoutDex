# Tutorial: Import a model

In this lesson you load a Warcraft III model into 3ds Max and look at what WhiteoutDex makes of it.

**You need:** a model file. WhiteoutDex comes with samples in the folder `samples` of the source code, for example `HeroPaladin.mdx`. You can also take any `.mdx` or `.mdl` from the internet or from your own maps.

## Step 1: Start the import

1. Choose **WhiteoutDex > [[t:menu_importer]]**.
2. Choose the file `HeroPaladin.mdx` and click **Open**.

The import window opens.

![The import window. It shows what the model contains.](importer.png)

## Step 2: Choose the options

1. Under *Fast Settings* choose **All**.
2. Under *Mode* choose **New Scene**.
3. Click **Import**.

After a few seconds the model is in your scene.

![The imported Paladin with its attachment points (yellow) and event objects (green).](tut_model_helpers.png)

## Step 3: Look around

The scene now contains:

- **The meshes** of the model with *Wc3Materials* and the textures.
- **The bones** that move the meshes.
- **Attachment points** with yellow names, for example *Hand Right Ref*.
- **Event objects** with green names, for example the footprints *FPT…* and sounds *SND…*.
- **Collision shapes**, **particle emitters** and **ribbons**, if the model has them.

## Step 4: Play the animations

1. Open **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Double-click a sequence, for example *Walk*.

3ds Max plays the walk animation.

!!! tip
    Too many helpers on the screen? Open the [[t:menu_node_manager]], check *Helpers / Dummies*, *Attachments* and *Event Objects*, and click **Hide**.

**Next lesson:** look at the model in the WhiteoutFlakes previewer.
