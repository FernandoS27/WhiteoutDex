# Wc3 Vertex Color

Der Modifikator **Wc3 Vertex Color** färbt ein ganzes Mesh. Du kannst die Farbe animieren. So pulsiert zum Beispiel ein Kristall, oder ein Teil leuchtet rot, wenn die Einheit getroffen wird. Der Exporter schreibt die Farbe als Farbe der *Geoset-Animation* des Meshes.

Füge ihn aus der Modifier List hinzu: Wähle ein Mesh aus und wähle **Wc3 Vertex Color**.

[[shot:obj_vertexcolor_r1|Grundparameter des Modifikators.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:4]] | Die Farbe, mit der das Mesh getönt wird. Weiß lässt die Textur, wie sie ist. Animiere sie mit **Auto Key**. |
| [[ctl:7]] | Setzt das Flag *Drop Shadow* des Meshes. |
| [[ctl:9]] | Nutzt die Farbe. Ist es aus, wird nur das Flag geschrieben. |

!!! tip
    Willst du einen Teil ein- und ausblenden, nutze stattdessen Sichtbarkeits-Keys (*Sichtbarkeits-Keyer*) oder die *Deckkraft* des Wc3Material.
