# Problems and solutions

## The WhiteoutDex menu is missing

- Check that your 3ds Max version is between 2016 and 2027.
- Install WhiteoutDex again while 3ds Max is closed.
- 3ds Max 2026 and newer need **.NET 8**. Install it from Microsoft and start 3ds Max again.

## The model has no textures (it is white or pink)

- Open [[t:menu_settings]] and check the game folders. The status line must say that archives were found.
- Classic models look in the MPQ archives, Reforged models in CASC. Set the folder of the game the model comes from.
- Textures of custom models must be next to the model file or in a sub-folder with the path the model uses (for example `Textures\`).

## The model looks inside-out in the game

The triangles face the wrong way. The exporter turns meshes around that face against their own normals. If one part still looks wrong, select it and use **Flip** on its normals in 3ds Max.

## The model is invisible or flickers at the edge of the screen

The **extents** (the size of each animation) are too small. The exporter measures them on every frame. If you moved the model after the import, export again. The exporter replaces extents that do not hold the model.

## Parts of the model do not move with the bones

- The mesh needs a **Skin** modifier with the bones.
- In the import window, the option *Skinned* must be on.
- For classic models, turn on *Quantize skin weights* in the export window.

## The export shows warnings

The export report lists what needs your attention. Each line names the object. The full list is in `mdlx_export_debug.log` (see *Files and settings*).

## 3ds Max shows "Startup Failure Detection"

3ds Max did not close properly last time. Click **Continue Without Restore**. Do not choose *Restore Factory Settings* unless you want to reset all of 3ds Max.

## Where can I get help?

- **Discord:** the WhiteoutDex server (link in [[t:menu_about]]).
- **Hive Workshop:** the WhiteoutDex thread.
- **GitHub:** report bugs on the *Issues* page. Add the log files from *Files and settings*.
