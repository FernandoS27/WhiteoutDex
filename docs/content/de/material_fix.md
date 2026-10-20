# Material Fix

Der Exporter kann nur **Wc3Material** lesen. Material Fix wandelt andere Materialien (Standard, Physical, PBR) in Wc3Materials um. Es behält die Texturen und setzt die Warcraft-III-Optionen, die du hier wählst.

[[t:exp_fix_all_btn]] im Export-Fenster nutzt dieselben Einstellungen.

Öffne es über **WhiteoutDex > [[t:menu_material_fix]]**.

[[shot:material_fix|Material Fix mit den Einstellungen für klassische Modelle (SD).]]

Es gibt zwei Sätze von Einstellungen: einen für klassische Modelle (**SD**) und einen für Reforged (**HD**).

| Nr. | Was es macht |
|---|---|
| [[ctl:4]] | Einstellungen für klassische Modelle. |
| [[ctl:5]] | Einstellungen für Reforged-Modelle. |

## Darstellung

| Nr. | Was es macht |
|---|---|
| [[ctl:8]] | Wie das Material gezeichnet wird. *Automatisch* macht Materialien mit Alphakanal transparent. Alle Modi findest du unter *Wc3Material*. |
| [[ctl:10]] | Beide Seiten der Flächen werden gezeichnet. |
| [[ctl:11]] | Das Material ignoriert Licht. |
| [[ctl:12]] | Das Material ignoriert Nebel. |
| [[ctl:13]] | Nutzt eine Farbe für das ganze Mesh. |
| [[ctl:14]] | Wird über allem gezeichnet (kein Tiefentest). |
| [[ctl:15]] | Verdeckt nicht, was dahinter ist. |
| [[ctl:16]] | Nutzt Ambient Occlusion (nur Reforged). |
| [[ctl:17]] | Rückseiten werfen Schatten (nur Reforged). |
| [[ctl:18]] | Übernimmt *zweiseitig*, *unbeleuchtet* und *Transparenz* aus dem alten Material statt aus den Kästchen oben. |

## Texturen

| Nr. | Was es macht |
|---|---|
| [[ctl:23]] | Der Ordner, der im Modell vor den Texturnamen steht, zum Beispiel `Textures\`. |
| [[ctl:26]] | Wie sich Texturen wiederholen: wie im alten Material, in beide Richtungen, nur U, nur V oder gar nicht. |
| [[ctl:28]] | Eine Normal- oder Bump-Map wird zur *Normal*-Map (Reforged). |
| [[ctl:29]] | Eine Emission-Map wird zur *Emissive*-Map (Reforged). |

## Alle beheben und jetzt umwandeln

| Nr. | Was es macht |
|---|---|
| [[ctl:32]] | Welche Einstellungen [[t:exp_fix_all_btn]] im Export-Fenster nutzt: SD oder HD. |
| [[ctl:34]] | Hängt `_wc3` an den Namen jedes umgewandelten Materials an. |
| [[ctl:39]] | Wandelt jetzt die Materialien der ausgewählten Objekte um. |
| [[ctl:40]] | Wandelt jetzt alle Materialien der Szene um. |
| [[ctl:42]] | Setzt diesen Reiter auf die Standardwerte zurück. |
| [[ctl:43]] | Schließt das Fenster. Deine Einstellungen werden gespeichert. |

!!! tip
    Speichere deine Szene, bevor du die ganze Szene umwandelst.
