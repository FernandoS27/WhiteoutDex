# Tutorial: Your first own model

In this lesson you make a simple object from scratch: a crate, for example a doodad for a map.

## Step 1: Model the crate

1. Create a **Box**: *Create > Geometry > Box*. Make it about 60 × 60 × 60 units. One unit in 3ds Max is one unit in the game.
2. Place it so that it stands on the ground (bottom at height 0) and its center is at 0, 0.

## Step 2: Give it a material

1. Open the Material Editor ([[key:M]]).
2. Choose a free slot, click the material type button and choose **Wc3Material**.
3. In *Material Texture Maps*, click the **Diffuse** button and choose **Wc3Bitmap**.
4. Click **Pick Bitmap...** and choose a texture, or click **Open Texture Browser** and take one from the game (search for `crate` or `wood`).
5. Drag the material onto the box.

![The Wc3Material with its texture slots.](mat_wc3material_r3.png)

!!! tip
    You have a Standard or Physical material already? *Material Fix* converts it into a Wc3Material for you.

## Step 3: Collision

The player should be able to click the crate.

1. Select the box.
2. Open **WhiteoutDex > [[t:menu_anim_general]]** and click **One Click Collision Box**.

## Step 4: A Stand animation

Every model needs at least one sequence, even if nothing moves.

1. Open **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Click **New...**, choose the preset *Stand* and click **OK**.

## Step 5: Export and test

1. Export the crate as `Crate.mdx` (see the lesson *Export a model*).
2. Open the export window's box: if it is yellow, click [[t:exp_fix_all_btn]].
3. Look at the result in the previewer: **WhiteoutDex > [[t:menu_renderer]]**.

You made your first Warcraft III model.
