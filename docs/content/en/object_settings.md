# Object Settings

Warcraft III objects have extra settings that 3ds Max does not have, for example *billboard*. The Object Settings set them on the selected objects. WhiteoutDex saves them in the object (as user properties) and the exporter writes them into the model.

Open them with **WhiteoutDex > [[t:menu_object_settings]]**. Select one or more objects first.

[[shot:object_settings|The Object Settings with a bone selected.]]

## Flags

| No. | What it does |
|---|---|
| [[ctl:4]] | The object does not move with its parent. |
| [[ctl:5]] | The object does not turn with its parent. |
| [[ctl:6]] | The object does not scale with its parent. |
| [[ctl:7]] | The object always faces the camera. Good for glows and flat effects. |
| [[ctl:8]] | Faces the camera, but only turns around the X axis. |
| [[ctl:9]] | … only around the Y axis. |
| [[ctl:10]] | … only around the Z axis. |
| [[ctl:11]] | The object stays in place in the camera view. |

## Mesh

| No. | What it does |
|---|---|
| [[ctl:13]] | **Normal skinning:** uses the Skin modifier or the linked bones. |
| [[ctl:14]] | **ArtTools skinning:** joins the vertices of separate meshes at the export. Use it only on meshes that you want to blend into each other. |
| [[ctl:17]] | The player cannot click this mesh in the game. |
