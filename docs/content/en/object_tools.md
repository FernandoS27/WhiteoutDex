# Object Manipulation Tools

Copy and paste positions, links and vertex positions between objects. Good for symmetric models: build the left side, then copy it to the right side.

Open the tools with **WhiteoutDex > [[t:menu_object_tools]]**.

[[shot:object_tools|The Object Manipulation Tools with a mesh selected.]]

## Objects

| No. | What it does |
|---|---|
| [[ctl:5]] | **Hierarchy:** saves the parent links of the selected objects and unlinks them. |
| [[ctl:6]] | Links the objects back to their saved parents. |
| [[ctl:8]] | **Position:** copies the position of the first selected object. |
| [[ctl:9]] | Moves the selected objects to the copied position. |
| [[ctl:11]] | **Structure (L↔R):** copies the positions of all selected objects with their names. |
| [[ctl:12]] | Pastes them on the objects with the mirrored name: a position copied from `Arm_L` goes to `Arm_R`. |
| [[ctl:14]] | Unlinks the objects for a moment while pasting, so their children do not move along. |
| [[ctl:15]] | Mirrors the position while pasting, along the axes you check: [[ctl:16]], [[ctl:17]], [[ctl:18]]. |

## Vertices (Poly Tools)

Select vertices of an Editable Poly or Editable Mesh first. The line above the buttons shows the object and how many vertices you selected.

| No. | What it does |
|---|---|
| [[ctl:22]] | Copies the positions of the selected vertices. |
| [[ctl:23]] | Pastes the positions on the vertices. |
| [[ctl:26]] | Mirrors the X values while pasting. [[ctl:27]] and [[ctl:28]] do the same for Y and Z. |
| [[ctl:29]] | Mirrors around the center of the world. |
| [[ctl:30]] | Mirrors around the pivot of the object. |
| [[ctl:31]] | Mirrors around the center of the selection. |
| [[ctl:35]] | Pastes on the same vertex numbers. Use it on the same mesh or on a copy of it. |
| [[ctl:36]] | Pastes on the vertices you selected, even on another mesh. |

!!! tip
    Name your objects with `_L` and `_R` (or `Left` and `Right`). Then *Structure (L↔R)* finds the partner of each object by itself.
