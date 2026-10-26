# Bandemitter (Ribbon)

Ein Bandemitter zeichnet eine **Spur** hinter einem Punkt, der sich bewegt: die Lichtspur eines Schwerthiebs, den Schweif eines Kometen, den Rauch einer Rakete. Verbinde ihn mit dem Knochen, der sich bewegt, zum Beispiel mit der Waffe.

Erstelle ihn über **Create > Geometry > Particle Systems > Wc3Ribbon**.

## Band-Materialparameter

[[shot:obj_ribbon_r1|Band-Materialparameter.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:1079]] | Das Material der Spur (ein *Wc3Material*). Klicke, um es in den Material Editor zu legen. |
| [[ctl:1073]] | Die Textur ist ein Raster aus Bildern: Zahl der Zeilen (1, 2, 4, 8 …) … |
| [[ctl:1075]] | … und der Spalten (1, 2, 4, 8 …). |
| [[ctl:1077]] | Welches Bild des Rasters die Spur zeigt. Animiere den Wert, damit die Spur flackert. |

## Band-Eigenschaften

[[shot:obj_ribbon_r2|Band-Eigenschaften.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:1066]] | Die Farbe der Spur. |
| [[ctl:1059]] | Wie weit die Spur über den Emitter reicht. |
| [[ctl:1067]] | Wie weit sie unter den Emitter reicht. Oben + unten = Breite der Spur. |
| [[ctl:1080]] | Transparenz: 0 = unsichtbar, 1 = undurchsichtig. Animiere den Wert auf 0, um die Spur zu verstecken. |
| [[ctl:1069]] | Wie viele Spurstücke pro Sekunde starten. Mehr Stücke machen eine glattere Spur. |
| [[ctl:1071]] | Wie lange ein Stück bleibt, in Sekunden. Ein längeres Leben macht eine längere Spur. |
| [[ctl:1082]] | Zieht die Spur nach unten. |
| [[ctl:1090]] | **Regler:** wie sich ein animierter Wert zwischen den Keys ändert. Jeder Wert hat seinen eigenen Regler. |

!!! tip
    Für eine Schwertspur: Emitter an der Klinge, Material mit Filtermodus *Additiv*, Alpha 0 außer während des Hiebs, Lebensdauer etwa 0.2 Sekunden.
