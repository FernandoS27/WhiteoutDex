# Scene problems and Fix all

Before you export, WhiteoutDex checks your scene. It looks for things that would break the model in the game or make the export fail. You see the result in the export window.

[[shot:exporter_problems|The export window found three problems.]]

| No. | What it does |
|---|---|
| [[ctl:1092]] | Repairs all problems at once. This changes your scene. Each kind of repair is one undo step, so you can take it back with [[key:Ctrl+Z]]. |
| [[ctl:1093]] | Opens the list of all problems. |

## The problem list

[[shot:scene_problems|Each row is one problem.]]

| No. | What it does |
|---|---|
| [[ctl:2000]] | The problems. Double-click a row to select the object in the scene. |
| [[ctl:2004]] | Checks the scene again, for example after you repaired something by hand. |
| [[ctl:2002]] | Repairs only the problems you selected in the list. |
| [[ctl:2003]] | Repairs all problems in the list. |
| [[ctl:1]] | Closes the list. |

## What WhiteoutDex repairs

| Problem | What [[t:exp_fix_all_btn]] does |
|---|---|
| Two objects have the same name | Renames them: `_2`, `_3` and so on. The game needs unique names. |
| Empty mesh | Deletes it. |
| Editable Poly | Adds an *Edit Mesh* modifier, so the exporter gets triangles. |
| *Edit Mesh* above *Skin* | Collapses it, so the skin stays correct. |
| Controller the game cannot read | Replaces it with a linear controller. |
| Mesh with a Multi/Sub-Object material | Splits the mesh, so each part has one material. |
| Material is not a Wc3Material | Converts it. The settings come from *Material Fix* (see there). |
| Two materials are the same | Merges them into one. |

!!! tip
    You do not have to repair the problems. The exporter also works without it. But the model can look wrong in the game.
