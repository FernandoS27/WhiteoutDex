# Tutorial: A fire effect

In this lesson you put a fire on a torch with a **Particle Emitter 2**.

## Step 1: Create the emitter

1. Choose *Create > Geometry > Particle Systems > Wc3Particles2*.
2. Click in the viewport to place it. Move it to the top of the torch.
3. Link it to the torch (or to the bone that holds the torch), so it moves with it.

## Step 2: The texture

1. Open the **Texture Options** rollout.
2. Click **Browse MPQ / CASC** and choose a fire texture of the game (search for `fire` or `flare`).
3. Set **Texture Blend Mode** to **Additive**. Additive particles glow.

## Step 3: The movement

In **Emitter Options** and **Timing Options**:

1. **Speed:** a small value, so the flames rise slowly.
2. **Cone Angle:** small, so the flames stay together.
3. **Lifespan:** about 1 second.
4. **Emission Rate:** about 20 particles per second.

You see the particles in the viewport at once. Change the values until you like the fire.

## Step 4: Color and fading

In **Particle Options**:

1. **Color:** start yellow, middle orange, end dark red.
2. **Alpha:** start 0, middle 255, end 0. The flames fade in and out.
3. **Scale:** start small, middle bigger, end small.

![The Particle Options of an emitter.](obj_particles2_r6.png)

## Step 5: Check it

Open the previewer (**WhiteoutDex > [[t:menu_renderer]]**). It shows the fire as the game will show it.

!!! tip
    Save a fire you like with **Export** in *Particle Configuration*. Load it on other models with **Import**.
