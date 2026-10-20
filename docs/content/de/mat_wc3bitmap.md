# Wc3Bitmap

Die **Wc3Bitmap** ist die Textur-Map von WhiteoutDex. Lege sie in die Slots eines *Wc3Material*. Sie liest `.blp`, `.dds`, `.tga`, `.png` und die anderen Bildformate von 3ds Max. Sie enthält die Texturanimation (UV-Animation) und den Pfad der Textur im Spiel.

## WC3-Texturparameter

[[shot:mat_wc3bitmap_r1|WC3-Texturparameter.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:6]] | Wähle eine Bilddatei auf deiner Festplatte. |
| [[ctl:7]] | Wähle eine Textur aus den Spielarchiven. |
| [[ctl:9]] | Nutzt eine Textur, die das Spiel einsetzt (Teamfarbe, Team-Glühen, Klippe, Bäume). |
| [[ctl:12]] | Die Textur wiederholt sich über die Breite. |
| [[ctl:13]] | Die Textur wiederholt sich über die Höhe. |
| [[ctl:14]] | Nutzt die Textur als Spiegelung (Sphere Environment Map). |

## UV-Animation (TXAN)

[[shot:mat_wc3bitmap_r2|UV-Animation.]]

Animiere diese Werte, damit sich die Textur bewegt. *Texturanimation erstellen* kann sie für dich setzen.

| Nr. | Was es macht |
|---|---|
| [[ctl:3]] | Wie sich die Werte zwischen den Keys ändern. |
| [[ctl:8]] | Verschiebt die Textur zur Seite (U). |
| [[ctl:11]] | Verschiebt die Textur nach oben und unten (V). |
| [[ctl:14]] | W-Versatz. |
| [[ctl:17]] | Dreht die Textur. |
| [[ctl:20]] | Wiederholt die Textur zur Seite öfter oder seltener. |
| [[ctl:23]] | Wiederholt die Textur nach oben und unten öfter oder seltener. |

## Pfadpräfix

[[shot:mat_wc3bitmap_r3|Pfadpräfix.]]

Das Präfix ist der Ordner, den der Exporter vor den Texturnamen schreibt, zum Beispiel `Textures\` oder `war3mapImported\`.

| Nr. | Was es macht |
|---|---|
| [[ctl:5]] | Wähle ein gespeichertes Präfix. |
| [[ctl:6]] | Bearbeite die Liste der gespeicherten Präfixe. |

!!! tip
    Texturen in einer Karte müssen das Präfix nutzen, das die Karte nutzt. Für eigene Modelle in Karten ist das meist `war3mapImported\`.
