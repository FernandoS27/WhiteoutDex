# Anim: Controllers

Warcraft III knows four ways to move between keys: *None* (jump), *Linear*, *Bezier* and *Hermite* (TCB). This tool gives the selected objects the controllers you choose, for position, rotation and scale at once.

Open it with **WhiteoutDex > [[t:menu_anim_controllers]]**.

[[shot:anim_controllers|The Controllers tool.]]

| No. | What it does |
|---|---|
| [[ctl:4]] | Position moves in straight lines between keys. |
| [[ctl:5]] | Position moves on smooth curves. |
| [[ctl:6]] | Position uses TCB keys (smooth, with tension, continuity and bias). |
| [[ctl:7]] | Leaves the position controller as it is. |
| [[ctl:10]] | Rotation turns evenly between keys. |
| [[ctl:11]] | Rotation turns smoothly. |
| [[ctl:12]] | Rotation uses TCB keys. |
| [[ctl:13]] | Leaves the rotation controller as it is. |
| [[ctl:16]] | Scale changes evenly. |
| [[ctl:17]] | Scale changes on smooth curves. |
| [[ctl:18]] | Scale uses TCB keys. |
| [[ctl:19]] | Leaves the scale controller as it is. |
| [[ctl:22]] | Gives the selected objects these controllers. |

!!! note
    The exporter also replaces controllers the game cannot read (see *Scene problems*). With this tool you choose the type yourself.
