# Tutorial: Animieren

In dieser Lektion gibst du der Kiste aus der letzten Lektion zwei Animationen: *Stand* (sie macht nichts) und *Death* (sie fällt auseinander).

## Schritt 1: Die Zeitleiste planen

Alle Animationen eines Modells teilen sich eine Zeitleiste. Jede Animation bekommt ihren eigenen Teil:

| Sequenz | Frames |
|---|---|
| Stand | 0 – 80 |
| Death | 100 – 164 |

Lass zwischen den Teilen ein paar Frames frei.

## Schritt 2: Die Sequenzen erstellen

1. Öffne **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Klicke auf [[t:seq_new_btn]], wähle *Stand*, setze den Start auf 0 und das Ende auf 80. Klicke auf [[t:seq_ok_btn]].
3. Klicke auf [[t:seq_new_btn]], wähle *Death*, setze den Start auf 100 und das Ende auf 164. Schalte [[t:seq_non_looping_chk]] ein und klicke auf [[t:seq_ok_btn]].

![Der Sequenz-Manager mit einer ausgewählten Sequenz.](sequence_manager_selected.png)

## Schritt 3: Keys setzen

1. Schalte in 3ds Max **Auto Key** ein.
2. Setze Keys am Anfang und am Ende jeder Sequenz: Öffne **WhiteoutDex > [[t:menu_anim_general]]**, wähle die Kiste aus und klicke auf [[t:anim_addnstart_and_ending_keysnto_selected_ob_btn]].
3. Geh zu Frame 130. Drehe die Kiste und bewege sie nach unten, als ob sie umfällt.
4. Geh zu Frame 164 und bewege sie unter den Boden.
5. Schalte **Auto Key** aus.

## Schritt 4: Testen

1. Doppelklicke im Sequenz-Manager auf *Death*. 3ds Max spielt die Animation ab.
2. Öffne die Vorschau (**WhiteoutDex > [[t:menu_renderer]]**) und wähle *Death* in der Liste **Animation**.

!!! tip
    Knochen lassen sich besser animieren als Meshes. Verbinde die Teile eines Modells mit Knochen und animiere die Knochen. Gib den Meshes dann einen **Skin**-Modifikator.

!!! note
    Das Spiel spielt *Stand* immer wieder ab. *Death* läuft einmal. Danach bleibt der Körper liegen, bis die *Decay*-Animationen kommen (wenn das Modell sie hat).
