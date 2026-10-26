# The Importer

The importer loads a Warcraft III model (`.mdx` or `.mdl`) into 3ds Max. It also loads the textures of the model from your game.

## Open the importer

Use one of these ways:

- Menu **WhiteoutDex > [[t:menu_importer]]**
- The **Import** button in the WhiteoutDex sidebar
- **File > Import** and choose the file type *Warcraft III MDX/MDL Model*

Choose a file. Then the import window opens.

## The import window

[[shot:importer|The import window with the Objects tab.]]

The top of the window shows the file name, the game version of the model (*Classic* or *Reforged*) and how many bones, meshes (geosets) and animations it has.

| No. | What it does |
|---|---|
| [[ctl:1052]] | **Fast settings.** Choose a preset. It sets all switches below for you. Choose *All* to import everything. The other presets import less, for example only the mesh without materials. |
| [[ctl:1059]] | Deletes the scene that is open now and imports the model into an empty scene. |
| [[ctl:1060]] | Adds the model to the scene that is open now. Use this to combine models. |
| [[ctl:1061]] | Shows the objects of the model (this picture). |
| [[ctl:1062]] | Shows the animation options. |
| [[ctl:1063]] | Shows the options for the mesh, materials and textures. |
| [[ctl:1021]] | Switch for all objects. When it is off, no objects are imported. |
| [[ctl:1022]] | The skeleton. You need it for animations and for skinned meshes. |
| [[ctl:1023]] | Helper objects. They move other objects but you do not see them in the game. |
| [[ctl:1024]] | Lights. |
| [[ctl:1025]] | Attachment points, for example for weapons and spell effects. |
| [[ctl:1028]] | Event objects, for example sounds, footprints and blood. |
| [[ctl:1026]] | Particle Emitter 1: emitters that spawn small models. |
| [[ctl:1027]] | Particle Emitter 2: emitters for fire, smoke, sparks and similar effects. |
| [[ctl:1029]] | Ribbon emitters: trails, for example behind a sword. |
| [[ctl:1030]] | Collision shapes (boxes and spheres). The game uses them to know where you can click the model. |
| [[ctl:1031]] | Cameras, for example the portrait camera. |
| [[ctl:1]] | Starts the import. |
| [[ctl:2]] | Closes the window without importing. |

The number next to each object type tells you how many the model has. Types the model does not have are grey.

!!! note
    Reforged models can have two more object types: *Popcorn FX* emitters and *FaceFX*. You see them only when the model has them.

### Animations tab

[[shot:importer_animations|The Animations tab.]]

| No. | What it does |
|---|---|
| [[ctl:1041]] | Switch for all animations. When it is off, the model comes in without movement. |
| [[ctl:1042]] | Movement (position keys). |
| [[ctl:1043]] | Rotation keys. |
| [[ctl:1044]] | Scale keys. |
| [[ctl:1048]] | Show and hide keys. |
| [[ctl:1045]] | Animated values of objects, for example the strength of a light or the emission rate of particles. |
| [[ctl:1046]] | Moving texture coordinates (UV animation). |
| [[ctl:1047]] | Texture changes over time (flipbook). |
| [[ctl:1049]] | Color changes over time. |

### Model tab

[[shot:importer_model|The Model tab.]]

| No. | What it does |
|---|---|
| [[ctl:1001]] | Imports the skin: which bone moves which vertex. Without it the mesh does not follow the bones. |
| [[ctl:1011]] | Creates WhiteoutDex materials for the model. |
| [[ctl:1012]] | Loads the textures. WhiteoutDex looks in your game folders (see *Settings*). |
| [[ctl:1071]] | Imports helpers as *Point* objects. When it is off, helpers come in as bones. |
| [[ctl:1081]] | Welds vertices that are at the same place and makes the mesh simpler. |
| [[ctl:1082]] | Removes bones and helpers that have no animation and are not needed. |

## Where the textures come from

WhiteoutDex looks for each texture in this order:

1. In the folder of the model and its sub-folders.
2. In the game archives. For classic models (version 800) it looks in the **MPQ** archives first and then in **CASC**. For Reforged models it is the other way around.

The bottom line of the window tells you which archives it uses. You set the game folders in [[t:menu_settings]].

!!! tip
    The importer saves the textures it takes from the game archives next to the model file. So the next time it finds them at once.

## Good to know

- The importer remembers your choices for the next time.
- Import a model from the game without a file on your disk: see *Import from the game*.
