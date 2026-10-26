# Texturanimation erstellen

Dieses Werkzeug lässt Texturen sich bewegen: fließendes Wasser, ein brennendes Feuer aus einem Sprite Sheet oder ein Daumenkino aus vielen Bildern. Das Werkzeug schreibt die Keys für dich.

Öffne es über **WhiteoutDex > [[t:menu_texture_anim_creator]]**.

Es gibt drei Arten von Animation:

| Art | Nutze sie für |
|---|---|
| [[t:tac_tab_flow]] | Die Textur gleitet, dreht sich oder wächst: Wasser, Lava, Energie. |
| [[t:tac_tab_sheet]] | Ein Bild mit vielen kleinen Bildern in einem Raster (ein *Sprite Sheet*). Das Werkzeug zeigt sie nacheinander. |
| [[t:tac_tab_seq]] | Ein Ordner mit vielen Bildern. Das Werkzeug zeigt sie nacheinander (ein *Daumenkino*). |

[[shot:texture_anim_creator|Texturanimation erstellen mit SEQ-Anim (Einzel).]]

## Die Textur wählen

| Nr. | Was es macht |
|---|---|
| [[ctl:4]] | Klicke auf ein Mesh im Viewport, um sein Material zu nutzen. |
| [[ctl:5]] | Nutzt das ausgewählte Mesh. |
| [[ctl:6]] | Nutzt das Material, das im Material Editor aktiv ist. |
| [[ctl:8]] | Das Material. |
| [[ctl:11]] | Die Textur des Materials. Die Zeile darunter zeigt ihren Namen und ihre Größe. |
| [[ctl:14]] | [[t:tac_tab_flow]]. |
| [[ctl:15]] | [[t:tac_tab_sheet]]. |
| [[ctl:16]] | [[t:tac_tab_seq]]. |

## SEQ-Anim (Einzel): ein Sprite Sheet

| Nr. | Was es macht |
|---|---|
| [[ctl:46]] | Der Frame, in dem die Animation beginnt. |
| [[ctl:50]] | Wie oft sich die Animation wiederholt. |
| [[ctl:54]] | Wie viele Bilder in einer Zeile des Sheets sind. |
| [[ctl:58]] | Wie viele Zeilen das Sheet hat. |
| [[ctl:62]] | Das erste Bild, das gezeigt wird. |
| [[ctl:66]] | Das letzte Bild, das gezeigt wird. |
| [[ctl:68]] | Überspringt Frames zwischen zwei Bildern (0 = jeder Frame zeigt ein neues Bild). |
| [[ctl:71]] | Vorwärts oder rückwärts. |
| [[ctl:74]] | Bilder pro Sekunde: 10, 15, 30 oder 60. |
| [[ctl:77]] | **AN:** Die Animation läuft ständig als globale Sequenz. **AUS:** Sie läuft in der Zeitleiste. |
| [[ctl:80]] | **AN:** Behält die Keys, die die Textur schon hat. **AUS:** Ersetzt sie. |
| [[ctl:84]] | Findet die Zahl der Spalten und Zeilen im Bild selbst. |
| [[ctl:85]] | Setzt die UV-Koordinaten des Meshes auf ein Bild des Sheets. Das brauchst du einmal. |
| [[ctl:87]] | **AN:** Fasst den UV-Modifikator nach [[t:tac_uvmap_btn]] in das Mesh zusammen. |
| [[ctl:116]] | Schreibt die Animation. Die Zeile darüber zeigt, wie viele Keys es werden und wie lang die Animation wird. |

## Fluss-Anim

[[shot:texture_anim_flow|Fluss-Anim.]]

1. Stelle die Start- und die Endzeit ein.
2. Klicke auf die Pfeile für die Richtung, in die sich die Textur bewegt. Du kannst sie auch drehen (links, rechts) und wachsen oder schrumpfen lassen (+, −).
3. Stelle [[t:tac_global_lbl]] auf AN für eine Textur, die sich immer bewegt.
4. Klicke auf [[t:tac_generate_btn]].

## SEQ-Anim (Mehrere)

[[shot:texture_anim_many|SEQ-Anim (Mehrere).]]

1. Wähle den Ordner mit den Bildern (Knopf **...**). Die Bilder werden in der Reihenfolge ihrer Namen genutzt.
2. Stelle die Reihenfolge, das Tempo und den Startframe ein.
3. Stelle das Pfad-Präfix ein, zum Beispiel `Textures\`. Das Modell nutzt diesen Pfad für jedes Bild.
4. Klicke auf [[t:tac_generate_btn]].

!!! tip
    Die Textur-Keys liegen auf der *Wc3Bitmap* (UV-Animation) oder auf dem *Wc3Material* (Daumenkino). Dort findest du sie, wenn du sie später ändern willst.
