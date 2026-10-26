# Objekt-Werkzeuge

Kopiere Positionen, Verbindungen und Vertex-Positionen von einem Objekt auf andere und füge sie dort ein. Das ist gut für symmetrische Modelle: Baue die linke Seite und kopiere sie dann auf die rechte Seite.

Öffne die Werkzeuge über **WhiteoutDex > [[t:menu_object_tools]]**.

[[shot:object_tools|Die Objekt-Werkzeuge mit einem ausgewählten Mesh.]]

## Objekte

| Nr. | Was es macht |
|---|---|
| [[ctl:5]] | **[[t:omt_hierarchy_lbl]]:** speichert die Eltern-Verbindungen der ausgewählten Objekte und trennt sie. |
| [[ctl:6]] | Verbindet die Objekte wieder mit ihren gespeicherten Elternobjekten. |
| [[ctl:8]] | **[[t:omt_position_lbl]]:** kopiert die Position des ersten ausgewählten Objekts. |
| [[ctl:9]] | Verschiebt die ausgewählten Objekte an die kopierte Position. |
| [[ctl:11]] | **[[t:omt_structure_lx2194r_lbl]]:** kopiert die Positionen aller ausgewählten Objekte mit ihren Namen. |
| [[ctl:12]] | Fügt sie auf den Objekten mit dem gespiegelten Namen ein: Eine Position von `Arm_L` geht an `Arm_R`. |
| [[ctl:14]] | Trennt die Objekte beim Einfügen für einen Moment. So bewegen sich ihre Kinder nicht mit. |
| [[ctl:15]] | Spiegelt die Position beim Einfügen entlang der Achsen, die du anhakst: [[ctl:16]], [[ctl:17]], [[ctl:18]]. |

## Vertices (Poly-Werkzeuge)

Wähle zuerst Vertices eines Editable Poly oder Editable Mesh aus. Die Zeile über den Knöpfen zeigt das Objekt und wie viele Vertices du ausgewählt hast.

| Nr. | Was es macht |
|---|---|
| [[ctl:22]] | Kopiert die Positionen der ausgewählten Vertices. |
| [[ctl:23]] | Fügt die Positionen auf den Vertices ein. |
| [[ctl:26]] | Spiegelt beim Einfügen die X-Werte. [[ctl:27]] und [[ctl:28]] machen dasselbe für Y und Z. |
| [[ctl:29]] | Spiegelt um die Mitte der Welt. |
| [[ctl:30]] | Spiegelt um den Pivot des Objekts. |
| [[ctl:31]] | Spiegelt um die Mitte der Auswahl. |
| [[ctl:35]] | Fügt auf denselben Vertex-Nummern ein. Nutze es auf demselben Mesh oder auf einer Kopie davon. |
| [[ctl:36]] | Fügt auf den Vertices ein, die du ausgewählt hast, auch auf einem anderen Mesh. |

!!! tip
    Benenne deine Objekte mit `_L` und `_R` (oder `Left` und `Right`). Dann findet [[t:omt_structure_lx2194r_lbl]] den Partner jedes Objekts selbst.
