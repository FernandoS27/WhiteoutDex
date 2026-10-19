# Skin Changer

Prepares the skin of your meshes for Warcraft III.

Open it with **WhiteoutDex > [[t:menu_skin_changer]]**. Select the skinned meshes first.

[[shot:skin_changer|The Skin Changer.]]

| No. | What it does |
|---|---|
| [[ctl:3]] | Changes the skin so that every vertex has at most 5 bones with clean weights. You can undo it. |
| [[ctl:7]] | Weights at or below this value are removed. 0.0 removes only weights that are exactly zero. |
| [[ctl:8]] | Removes the small weights and spreads their part over the other bones of the vertex. |
| [[ctl:11]] | Combines the selected skinned meshes that use the same material into one mesh with one Skin. Bones, weights and pose stay the same, so the mesh moves exactly as before. You can undo it. |

!!! tip
    Fewer meshes make a faster model. Use *Combine Mesh + Skin* when a model has many small parts with the same texture.
