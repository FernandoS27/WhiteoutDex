# Tutorial: Ein Modell exportieren

Jetzt speicherst du das Modell wieder als Warcraft-III-Datei.

**Du brauchst:** die Szene aus der Lektion *Ein Modell importieren*.

## Schritt 1: Den Export starten

1. Wähle **WhiteoutDex > [[t:menu_exporter]]**.
2. Wähle einen Ordner und einen Dateinamen, zum Beispiel `MyPaladin.mdx`. Klicke auf **Speichern**.

Das Export-Fenster öffnet sich.

![Das Export-Fenster. Klassisch ist ausgewählt.](exporter.png)

## Schritt 2: Das Spiel wählen

- Für das klassische Spiel wählst du [[t:exp_format_classic_title]]. Die Texturen werden zu `.blp`.
- Für Reforged wählst du [[t:exp_format_reforged_title]]. Die Texturen werden zu `.dds`.

Der Paladin ist ein klassisches Modell. Wähle also [[t:exp_format_classic_title]].

## Schritt 3: Die Szene prüfen

Sieh dir den Kasten über den Knöpfen an:

- **Grün – [[t:exp_banner_no_problems]]:** Mach weiter.
- **Gelb – Probleme:** Klicke auf [[t:exp_fix_all_btn]]. WhiteoutDex repariert die Szene. Siehe *Szenenprobleme und Alle beheben*.

## Schritt 4: Exportieren

1. Prüfe im Reiter [[t:exp_textures_tab]], dass [[t:exp_convert_textures_chk]] an ist.
2. Klicke auf [[t:exp_export_btn]].

Die Datei `MyPaladin.mdx` und ihre Texturen sind jetzt in deinem Ordner. Wenn etwas deine Aufmerksamkeit braucht, öffnet sich der Export-Bericht (siehe *Der Export-Bericht*).

## Schritt 5: Im Spiel testen

1. Kopiere das Modell und seine Texturen in den Ordner deiner Karte. Oder importiere sie mit dem **Import-Manager** des World Editors.
2. Gib im World Editor einer Einheit dein Modell. Das machst du im *Objekt-Editor* (Feld *Grafik – Modelldatei*, englisch *Art – Model File*).
3. Teste die Karte.

!!! tip
    Der Exporter merkt sich alle Optionen. Der nächste Export desselben Modells braucht nur einen Klick.
