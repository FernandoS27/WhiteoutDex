# Attachment Point

An attachment point marks a place on the model. The game attaches things there: the weapon of a hero, a spell effect on the hand, the arrow that hits the chest, the health bar over the head.

Create it with **Create > Helpers > Warcraft III Plugins > Wc3 AttachPoint**. Link it to the bone it should follow.

In the viewport, each attachment point shows its name in yellow.

## General Functions

[[shot:obj_attachpoint_r1|General Functions.]]

| No. | What it does |
|---|---|
| [[ctl:3]] | Click this, then click an object in the viewport. The attachment point jumps to that object and links to it. |

## Model Map Parameters

[[shot:obj_attachpoint_r2|Model Map Parameters.]]

An attachment point can carry its own model, for example a sword that the unit always holds.

| No. | What it does |
|---|---|
| [[ctl:9]] | Choose the model file on your disk. |
| [[ctl:10]] | Choose a model from the game archives. |

The two fields above the buttons hold the folder and the file name. The path is relative to the Warcraft III folder, for example `Textures\Model.mdx`.

## Attachment Parameters

[[shot:obj_attachpoint_r3|Attachment Parameters.]]

The game finds an attachment point by its **name**. You build the name from four parts:

| No. | What it does |
|---|---|
| [[ctl:5]] | **Type:** the main place: *Head*, *Overhead*, *Origin*, *Foot*, *Chest*, *Hand*, *Weapon* or *Sprite*. |
| [[ctl:8]] | **Mount:** for riders on a mount. |
| [[ctl:11]] | **Side:** *Left* or *Right*, for hands and feet. |
| [[ctl:14]] | **Extra:** more words, for example *Rear*, *Alternate*, *RallyPoint*, *Gold*, or *First* … *Sixth* for buildings. |
| [[ctl:22]] | Uses the model from *Model Map Parameters*. |
| [[ctl:26]] | Visibility (0 to 1). Animate it to show or hide the attached model. |
| [[ctl:27]] | Shows where the model path is relative to. |
| [[ctl:34]] | A number for the attachment point. |
| [[ctl:36]] | Writes this number into the model. |

Example: Type *Hand*, Side *Right* gives the name `Hand Right Ref`. The game puts a spell effect for the right hand there.

!!! tip
    The *One Click Attachment Points* tool in *Anim: General Tools* creates the usual attachment points for you, when your bones have known names.
