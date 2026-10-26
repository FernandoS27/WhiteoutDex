# Skin-Wechsler

Bereitet den Skin deiner Meshes für Warcraft III vor.

Öffne ihn über **WhiteoutDex > [[t:menu_skin_changer]]**. Wähle vorher die geskinnten Meshes aus.

[[shot:skin_changer|Der Skin-Wechsler.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:3]] | Ändert den Skin so, dass jeder Vertex höchstens 5 Knochen mit sauberen Gewichten hat. Du kannst es rückgängig machen. |
| [[ctl:7]] | Gewichte bis zu diesem Wert werden entfernt. 0.0 entfernt nur Gewichte, die genau null sind. |
| [[ctl:8]] | Entfernt die kleinen Gewichte und verteilt ihren Anteil auf die anderen Knochen des Vertex. |
| [[ctl:11]] | Fasst die ausgewählten geskinnten Meshes mit demselben Material zu einem Mesh mit einem Skin zusammen. Knochen, Gewichte und Pose bleiben gleich. Das Mesh bewegt sich also genau wie vorher. Du kannst es rückgängig machen. |

!!! tip
    Weniger Meshes machen ein schnelleres Modell. Nutze [[t:skin_combine_btn]], wenn ein Modell viele kleine Teile mit derselben Textur hat.
