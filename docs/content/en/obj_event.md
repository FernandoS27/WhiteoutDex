# Event Object

An event object starts something at an exact moment of an animation: a sound when the sword hits, a footprint when the foot touches the ground, blood when the unit dies, a model that appears.

Create it with **Create > Helpers > Warcraft III Plugins > Wc3 Event Object**. Place it where the event should happen and link it to a bone if it should move.

In the viewport, each event object shows its name in green.

[[shot:obj_event_r2|The Event Object rollout of a death sound from the sample model.]]

## Choose the event

| No. | What it does |
|---|---|
| [[ctl:5]] | The kind of event (see the table below). |
| [[ctl:8]] | The exact event from the game's list, for example a sound or a blood color. |
| [[ctl:13]] | Uses your choice and names the object. The line under the button shows the event code. |

You can also type an event code yourself in *Custom Data*.

| Type | Code starts with | What happens |
|---|---|---|
| FootPrint | `FPT` | A footprint on the ground. |
| BloodSplat | `SPL` | A splash on the ground, for example blood. |
| UberSplat | `UBR` | A big ground mark, for example a crater or a building base. |
| Sound | `SND` | A sound. |
| SpawnObject | `SPN` | A model appears, for example a falling body part. |

## When it happens

| No. | What it does |
|---|---|
| [[ctl:19]] | The times when the event happens. |
| [[ctl:20]] | Move the time slider to the right frame, then click this to add a time. |
| [[ctl:21]] | Removes the selected time. |
| [[ctl:22]] | Removes all times. |

!!! tip
    Use the *Sequence Manager* to jump to the animation, then move the slider frame by frame to find the exact moment, for example when the foot touches the ground.
