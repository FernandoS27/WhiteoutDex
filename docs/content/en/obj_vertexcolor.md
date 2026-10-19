# Wc3 Vertex Color

The **Wc3 Vertex Color** modifier colors a whole mesh. You can animate the color, for example to let a crystal pulse or to make a part glow red when the unit is hit. The exporter writes it as the *geoset animation* color of the mesh.

Add it from the modifier list: select a mesh and choose **Wc3 Vertex Color**.

[[shot:obj_vertexcolor_r1|Basic Parameters of the modifier.]]

| No. | What it does |
|---|---|
| [[ctl:4]] | The color the mesh is tinted with. White leaves the texture as it is. Animate it with **Auto Key**. |
| [[ctl:7]] | Sets the *Drop Shadow* flag of the mesh. |
| [[ctl:9]] | Uses the color. When it is off, only the flag is written. |

!!! tip
    To make a part fade in and out, use visibility keys (*Visibility Keyer*) or the *Opacity* of the Wc3Material instead.
