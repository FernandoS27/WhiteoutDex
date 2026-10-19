# Sequence Manager

A Warcraft III model keeps all its animations on one timeline. Each animation is a **sequence**: a name, a start frame and an end frame. The Sequence Manager creates and changes these sequences.

Open it with **WhiteoutDex > [[t:menu_sequence_manager]]** or the **Sequence** button in the sidebar.

[[shot:sequence_manager_selected|The Sequence Manager with the sequence "Stand - 1" selected.]]

## The list

| No. | What it does |
|---|---|
| [[ctl:4]] | Type a word to show only the sequences with this word in the name. |
| [[ctl:6]] | Empties the search field and shows all sequences again. |
| [[ctl:7]] | All sequences of the model. Click to select one. Hold [[key:Ctrl]] to select more. **Double-click** a sequence to play it in the viewport. Lines like `[GLOBAL 1]` are global sequences. |
| [[ctl:10]] | Selects all sequences. |
| [[ctl:11]] | Selects none. |
| [[ctl:12]] | Selects the ones that are not selected, and the other way around. |

## Create and remove

| No. | What it does |
|---|---|
| [[ctl:14]] | Creates a new sequence (see below). |
| [[ctl:15]] | Deletes the selected sequences. The animation keys stay on the timeline. |
| [[ctl:16]] | Copies the selected sequences. |
| [[ctl:17]] | Opens the *Global Sequence Manager*. |

## Change the selected sequence

| No. | What it does |
|---|---|
| [[ctl:20]] | The name. The name tells the game when to play the animation. See *Animation names*. |
| [[ctl:23]] | Uses the current frame of the time slider as the start frame. |
| [[ctl:26]] | The first frame of the sequence. |
| [[ctl:28]] | Uses the current frame as the end frame. |
| [[ctl:31]] | The last frame of the sequence. |
| [[ctl:32]] | **Rarity** (0–40). For animations with the same name, a higher value means the game plays this one less often. |
| [[ctl:36]] | **Move speed** (0–700). How fast the unit moves when it plays this walk animation. Most units have 250 to 400. |
| [[ctl:40]] | The animation plays once and does not repeat. Use it for *Death*, *Attack* and *Spell*. |
| [[ctl:41]] | The exporter leaves this sequence out. |
| [[ctl:44]] | Moves the start frame one frame earlier. The [[ctl:45]] button moves it one frame later. |
| [[ctl:47]] | Moves the end frame one frame earlier. The [[ctl:48]] button moves it one frame later. |

## Save and load

| No. | What it does |
|---|---|
| [[ctl:50]] | Loads sequences from a text file. |
| [[ctl:51]] | Saves the sequences to a text file. Use this to copy them to another scene. |

## Create a sequence

[[shot:sequence_new|The window for a new sequence.]]

1. Click [[t:seq_new_btn]].
2. Choose a preset, for example *Walk*. The preset sets a good name and length.
3. Check the name and the frames. The new sequence starts after the last one.
4. Click **OK**.

| No. | What it does |
|---|---|
| [[ctl:4]] | Presets for the common animations, or *Custom*. |
| [[ctl:7]] | The name of the new sequence. |
| [[ctl:12]] | First frame. |
| [[ctl:16]] | Last frame. |
| [[ctl:17]] | Turns on the rarity value. |
| [[ctl:21]] | Turns on the move speed. |
| [[ctl:25]] | The animation plays only once. |

!!! tip
    Leave a few empty frames between two sequences. Then the keys of one animation do not change the next one.

!!! note
    WhiteoutDex saves the sequences in your `.max` file. Older scenes with sequences in *note tracks* still work: WhiteoutDex reads them too.
