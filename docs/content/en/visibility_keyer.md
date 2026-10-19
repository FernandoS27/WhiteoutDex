# Visibility Keyer

In Warcraft III you show and hide parts of a model with **visibility keys**: for example a weapon that appears only in the attack animation, or a glow that appears only while the unit casts a spell. The Visibility Keyer sets these keys for you.

Open it with **WhiteoutDex > [[t:menu_visibility_keyer]]**. Select the objects first.

[[shot:visibility_keyer|The Visibility Keyer.]]

| No. | What it does |
|---|---|
| [[ctl:3]] | Shows or hides the selected objects at the current frame. |
| [[ctl:5]] | Adds a visibility controller to all selected objects. |
| [[ctl:6]] | Removes the visibility controllers from the selected objects. |
| [[ctl:7]] | Copies the visibility keys of the selected object. |
| [[ctl:8]] | Pastes them on the selected objects. |
| [[ctl:9]] | How the game changes between keys: *None* jumps at once, the others fade. |
| [[ctl:11]] | Gives the existing controllers the type you chose. |
| [[ctl:13]] | Sets a *show* key at the current frame. |
| [[ctl:14]] | Sets a *hide* key at the current frame. |
| [[ctl:16]] | Makes the object visible for the whole animation range. |
| [[ctl:17]] | Hides the object for the whole animation range. |
| [[ctl:19]] | Shows the object in the sequences you selected in the *Sequence Manager*. |
| [[ctl:20]] | Hides the object in the selected sequences. |

## Example: a weapon only for the attack

1. Select the weapon.
2. Click [[ctl:17]] to hide it everywhere.
3. In the *Sequence Manager*, select the *Attack* sequences.
4. Click [[ctl:19]].

!!! tip
    Use the interpolation *None* for most visibility keys. With other types the game fades the object, which looks wrong for most parts.
