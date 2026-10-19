# Tutorial: Animate

In this lesson you give the crate from the last lesson two animations: *Stand* (it does nothing) and *Death* (it falls apart).

## Step 1: Plan the timeline

All animations of a model share one timeline. Each animation gets its own part:

| Sequence | Frames |
|---|---|
| Stand | 0 – 80 |
| Death | 100 – 164 |

Leave a few frames free between the parts.

## Step 2: Create the sequences

1. Open **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Click **New...**, choose *Stand*, set start 0 and end 80, click **OK**.
3. Click **New...**, choose *Death*, set start 100 and end 164, turn on **Non Looping**, click **OK**.

![The Sequence Manager with a selected sequence.](sequence_manager_selected.png)

## Step 3: Set keys

1. Turn on **Auto Key** in 3ds Max.
2. Set keys at the start and the end of each sequence: open **WhiteoutDex > [[t:menu_anim_general]]**, select the crate and click **Add start and ending keys**.
3. Go to frame 130. Rotate the crate and move it down, as if it falls over.
4. Go to frame 164 and move it below the ground.
5. Turn off **Auto Key**.

## Step 4: Test

1. In the Sequence Manager, double-click *Death*. 3ds Max plays it.
2. Open the previewer (**WhiteoutDex > [[t:menu_renderer]]**) and choose *Death* in the **Animation** list.

!!! tip
    Bones animate better than meshes: link the parts of a model to bones and animate the bones. Then use a **Skin** modifier on the meshes.

!!! note
    The game plays *Stand* again and again. *Death* plays once, then the body stays until the *Decay* animations (if the model has them).
