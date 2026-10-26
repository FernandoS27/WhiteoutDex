# Tutorial: Dein erstes eigenes Modell

In dieser Lektion baust du ein einfaches Objekt von Grund auf: eine Kiste. Sie kann zum Beispiel ein Doodad für eine Karte sein.

## Schritt 1: Die Kiste modellieren

1. Erstelle eine **Box**: *Create > Geometry > Box*. Mach sie etwa 60 × 60 × 60 Einheiten groß. Eine Einheit in 3ds Max ist eine Einheit im Spiel.
2. Platziere sie so, dass sie auf dem Boden steht (Unterseite auf Höhe 0). Ihre Mitte liegt bei 0, 0.

## Schritt 2: Ein Material geben

1. Öffne den Material Editor ([[key:M]]).
2. Wähle einen freien Slot. Klicke auf den Knopf für den Materialtyp und wähle **Wc3Material**.
3. Klicke in [[t:mat_texmap_rollout_title]] auf den Knopf bei **Diffus** und wähle **Wc3Bitmap**.
4. Klicke auf [[t:bmp_pick_bitmap_btn]] und wähle eine Textur. Oder klicke auf [[t:bmp_open_browser_btn]] und nimm eine aus dem Spiel (suche nach `crate` oder `wood`).
5. Ziehe das Material auf die Box.

![Das Wc3Material mit seinen Textur-Slots.](mat_wc3material_r3.png)

!!! tip
    Du hast schon ein Standard- oder Physical-Material? *Material Fix* wandelt es für dich in ein Wc3Material um.

## Schritt 3: Kollision

Der Spieler soll die Kiste anklicken können.

1. Wähle die Box aus.
2. Öffne **WhiteoutDex > [[t:menu_anim_general]]** und klicke auf [[t:anim_one_click_collision_box_btn]].

## Schritt 4: Eine Stand-Animation

Jedes Modell braucht mindestens eine Sequenz, auch wenn sich nichts bewegt.

1. Öffne **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Klicke auf [[t:seq_new_btn]], wähle die Vorlage *Stand* und klicke auf [[t:seq_ok_btn]].

## Schritt 5: Exportieren und testen

1. Exportiere die Kiste als `Crate.mdx` (siehe die Lektion *Ein Modell exportieren*).
2. Sieh dir den Kasten im Export-Fenster an: Ist er gelb, klicke auf [[t:exp_fix_all_btn]].
3. Sieh dir das Ergebnis in der Vorschau an: **WhiteoutDex > [[t:menu_renderer]]**.

Du hast dein erstes Warcraft-III-Modell gebaut.
