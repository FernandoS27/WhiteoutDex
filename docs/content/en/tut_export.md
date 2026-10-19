# Tutorial: Export a model

Now you save the model as a Warcraft III file again.

**You need:** the scene from the lesson *Import a model*.

## Step 1: Start the export

1. Choose **WhiteoutDex > [[t:menu_exporter]]**.
2. Choose a folder and a file name, for example `MyPaladin.mdx`. Click **Save**.

The export window opens.

![The export window. Classic is selected.](exporter.png)

## Step 2: Choose the game

- For the classic game choose **Classic**. The textures become `.blp`.
- For Reforged choose **Reforged**. The textures become `.dds`.

The Paladin is a classic model, so choose **Classic**.

## Step 3: Check the scene

Look at the box above the buttons:

- **Green – No problems:** go on.
- **Yellow – problems:** click [[t:exp_fix_all_btn]]. WhiteoutDex repairs the scene. See *Scene problems and Fix all*.

## Step 4: Export

1. On the *Textures* tab, check that *Convert textures on export* is on.
2. Click **Export**.

The file `MyPaladin.mdx` and its textures are now in your folder. If something needs your attention, the export report opens (see *The export report*).

## Step 5: Test it in the game

1. Copy the model and its textures into the folder of your map, or import them with the **Import Manager** of the World Editor.
2. In the World Editor, give a unit your model in the *Object Editor* (field *Art – Model File*).
3. Test the map.

!!! tip
    The exporter remembers all options. The next export of the same model needs only one click.
