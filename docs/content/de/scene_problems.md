# Szenenprobleme und Alle beheben

Vor dem Export prüft WhiteoutDex deine Szene. Es sucht nach Dingen, die das Modell im Spiel kaputt machen oder den Export scheitern lassen. Das Ergebnis siehst du im Export-Fenster.

[[shot:exporter_problems|Das Export-Fenster hat drei Probleme gefunden.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:1092]] | Behebt alle Probleme auf einmal. Das ändert deine Szene. Jede Art von Reparatur ist ein Rückgängig-Schritt, du kannst sie also mit [[key:Strg+Z]] zurücknehmen. |
| [[ctl:1093]] | Öffnet die Liste aller Probleme. |

## Die Problemliste

[[shot:scene_problems|Jede Zeile ist ein Problem.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:2000]] | Die Probleme. Doppelklicke eine Zeile, um das Objekt in der Szene auszuwählen. |
| [[ctl:2004]] | Prüft die Szene neu, zum Beispiel nachdem du etwas von Hand repariert hast. |
| [[ctl:2002]] | Behebt nur die Probleme, die du in der Liste ausgewählt hast. |
| [[ctl:2003]] | Behebt alle Probleme der Liste. |
| [[ctl:1]] | Schließt die Liste. |

## Was WhiteoutDex repariert

| Problem | Was [[t:exp_fix_all_btn]] macht |
|---|---|
| Zwei Objekte haben denselben Namen | Benennt sie um: `_2`, `_3` und so weiter. Das Spiel braucht eindeutige Namen. |
| Leeres Mesh | Löscht es. |
| Editable Poly | Fügt einen *Edit Mesh*-Modifikator hinzu, damit der Exporter Dreiecke bekommt. |
| *Edit Mesh* über *Skin* | Fasst ihn zusammen, damit der Skin stimmt. |
| Controller, den das Spiel nicht lesen kann | Ersetzt ihn durch einen linearen Controller. |
| Mesh mit Multi/Unterobjekt-Material | Teilt das Mesh, damit jeder Teil ein Material hat. |
| Material ist kein Wc3Material | Wandelt es um. Die Einstellungen kommen aus *Material Fix* (siehe dort). |
| Zwei Materialien sind gleich | Fasst sie zu einem zusammen. |

!!! tip
    Du musst die Probleme nicht beheben. Der Exporter funktioniert auch so. Aber das Modell kann im Spiel falsch aussehen.
