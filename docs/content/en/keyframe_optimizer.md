# Keyframe Optimizer

Animations from other programs or from motion capture often have a key on every frame. That makes the model big. The Keyframe Optimizer removes keys that are not needed, and the movement stays almost the same.

Open it with **WhiteoutDex > [[t:menu_keyframe_optimizer]]**. Select the animated objects first.

[[shot:keyframe_optimizer|The Keyframe Optimizer.]]

## Settings

| No. | What it does |
|---|---|
| [[ctl:7#2]] | **Threshold:** how much the curve may change. A higher value removes more keys. Start with 0.5. |
| [[ctl:11#2]] | **Step:** how exactly the tool measures, in frames. Stay below 0.49. |
| [[ctl:13#2]] | Writes details into the MAXScript Listener. Leave it off; it is slower. |

## Optimize

| No. | What it does |
|---|---|
| [[ctl:3]] | Sorts the list by time or by name. |
| [[ctl:5]] | The sequences of the model. Select the ones you want to optimize. |
| [[ctl:7]] | Optimize position keys. |
| [[ctl:8]] | Optimize rotation keys. |
| [[ctl:9]] | Optimize scale keys. |
| [[ctl:14]] | Optimizes the selected sequences. |
| [[ctl:15]] | Optimizes all sequences. |
| [[ctl:16]] | Deletes keys that are not inside any sequence. They do nothing in the game. |

!!! tip
    Save your scene before you optimize. Check the animation afterwards. If something looks wrong, undo and try a smaller threshold.
