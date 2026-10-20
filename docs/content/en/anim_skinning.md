# Anim: Skinning

Sometimes meshes are linked to bones, and you want to change the bones. If you move the bones, the linked meshes move too. This tool unlinks the meshes for a moment and links them back later.

Open it with **WhiteoutDex > [[t:menu_anim_skin]]**.

[[shot:anim_skinning|The Skinning tool.]]

| No. | What it does |
|---|---|
| [[ctl:4]] | Saves the parent of each selected object and unlinks it. |
| [[ctl:5]] | Links the selected objects back to their saved parents. |

## How to use it

1. Select the meshes that are linked to the bones.
2. Click [[ctl:4]].
3. Change the bones, for example convert them from IK to FK.
4. Select the meshes again and click [[ctl:5]].
