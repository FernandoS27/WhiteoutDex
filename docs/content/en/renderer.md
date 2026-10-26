# WhiteoutFlakes previewer

**WhiteoutFlakes** shows your model the way Warcraft III shows it: with the game's shaders, team color, lighting, particles and ribbons. It follows 3ds Max live: when you move the time slider or change something, the previewer updates.

Open it with **WhiteoutDex > [[t:menu_renderer]]** or the **Renderer** button in the sidebar.

![WhiteoutFlakes next to 3ds Max 2027, with a Reforged model in team color.](media_1.png)

## Toolbar

| Element | What it does |
|---|---|
| **Animation** | Plays a sequence of the model, or *Free Range* to follow the Max time slider. |
| **Camera** | *Free Camera* or one of the cameras of the scene, for example the portrait camera. |
| **Team color** | The player color of the model. |
| **Lighting** | *InGame* (like the game), *Glue* (like the menus) or *Dynamic*. |
| **Sync Camera** | The previewer follows the active viewport of 3ds Max. |
| **Resync** | Reads the whole scene again, for example after you added objects. |

## Menu

| Menu | What is in it |
|---|---|
| **View** | Show or hide the grid, particles, ribbons and event objects. *Warcraft III Art*: which textures to use (*Follow Render Mode*, *Classic*, *Reforged (HD)*, *Definitive*). *Tileset*: the ground. |
| **Debug** | Markers for collision shapes and lights, special views (albedo, normals, level of detail, light count …) and a choice of the level of detail. |
| **Settings** | Background, brightness, sound volume, time of day, image based lighting, shadows, fog, the graphics backend (Direct3D 11, Direct3D 12, Vulkan) and the game folders. |

## Good to know

- Particles and ribbons are simulated as in the game. When you jump on the time slider, the previewer calculates the effects up to the new frame, so they look as if the animation had played there.
- The previewer saves its settings in `WhiteoutFlakes.ini` next to `3dsmax.exe`.
- If the previewer shows nothing or crashes, look at `WhiteoutDex_render.log` (see *Files and settings*).

!!! tip
    Choose *Classic* or *Reforged (HD)* under **View > Warcraft III Art** to see how your model looks in each version of the game.
