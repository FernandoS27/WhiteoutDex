# Tutorial: Attachments and events

A unit in the game needs places for effects (attachment points) and moments for sounds and footprints (event objects). In this lesson you add both to a unit.

**You need:** a unit with bones and a *Walk* animation, for example the imported Paladin.

## Part 1: Attachment points

### The quick way

If your bones have usual names (*Head*, *Chest*, *Hand*, *Foot* …):

1. Open **WhiteoutDex > [[t:menu_anim_general]]**.
2. Click **One Click Attachment Points**. WhiteoutDex creates the points and links them to the bones.

### By hand

1. Create a **Wc3 AttachPoint** (*Create > Helpers > Warcraft III Plugins*).
2. Click **Select Object (Adsorb)** and click the bone, for example the right hand. The point jumps there and links to it.
3. In **Attachment Parameters** choose **Type** *Hand* and **Side** *Right*. The point is now called *Hand Right Ref*.

The game shows spell effects on the right hand there.

![Attachment Parameters of an attachment point.](obj_attachpoint_r3.png)

**Points every unit should have:** *Origin*, *Head*, *Chest*, *Overhead*. Weapons use *Hand Left/Right* or *Weapon*.

## Part 2: Footprints

1. Create a **Wc3 Event Object** and place it at the left foot. Link it to the foot bone.
2. In **Event Type** choose the type *FootPrint* and in **Data** a footprint, for example a small one.
3. Click **Confirm**.
4. Open the *Walk* sequence in the Sequence Manager. Move the time slider to the frame where the left foot touches the ground.
5. Click **Add**. Repeat for every step of the left foot.
6. Do the same for the right foot with a second event object.

## Part 3: A sound

1. Create another event object near the weapon.
2. Choose the type *Sound* and a hit sound in **Data**, then **Confirm**.
3. Go to the frame in the *Attack* animation where the weapon hits, and click **Add**.

## Check it

Open the previewer. With **View > Event Objects** on, it shows the footprints and plays the sounds.

!!! tip
    Look at a model of the game (*Import from the game*): copy how Blizzard places attachment points and events.
