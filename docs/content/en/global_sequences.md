# Global sequences

A **global sequence** is an animation that loops all the time, no matter which animation the unit plays. Use it for things that never stop: a spinning crystal, a burning torch, a flag in the wind.

Each global sequence has a length in frames. Keys of an object can belong to a global sequence instead of the normal timeline.

Open the Global Sequence Manager with the [[t:seq_globals_btn]] button in the *Sequence Manager*.

[[shot:global_sequences|The Global Sequence Manager with the two global sequences of the sample model.]]

The list at the top shows all global sequences: number, frames, type and the objects that use it.

| No. | What it does |
|---|---|
| [[ctl:7]] | The end frame (length) of the selected global sequence. |
| [[ctl:8]] | Reads the scene again. |
| [[ctl:9]] | Removes the selected global sequence. |
| [[ctl:15]] | The length of a new global sequence, in frames. |
| [[ctl:24]] | Creates a new global sequence on the selected objects. |
| [[ctl:25]] | Changes the selected global sequence. |

## Make an object spin all the time

1. Select the object.
2. Set the **Duration**, for example 60 frames.
3. Click **New**. The time slider goes to frame 0, and the options for controller, channels and spin appear.
4. Choose the **Controller**, for example *Linear*, and the axis.
5. Move the time slider to the last frame of one turn, for example 60.
6. Click **> CW** (clockwise) or **CCW <** (counter-clockwise).
7. Click **Finish**.

The object now turns once in 60 frames, in every animation.

You can also animate by hand: after **New**, set keys on the object as usual inside the duration, then click **Finish**. WhiteoutDex moves these keys into the new global sequence.

!!! note
    The game counts global sequences in milliseconds. WhiteoutDex converts frames for you.
