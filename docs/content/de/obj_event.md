# Event-Objekt

Ein Event-Objekt startet etwas in einem genauen Moment einer Animation: einen Sound, wenn das Schwert trifft, eine Fußspur, wenn der Fuß den Boden berührt, Blut, wenn die Einheit stirbt, ein Modell, das erscheint.

Erstelle es über **Create > Helpers > Warcraft III Plugins > Wc3 Event Object**. Platziere es dort, wo das Event passieren soll. Verbinde es mit einem Knochen, wenn es sich bewegen soll.

Im Viewport zeigt jedes Event-Objekt seinen Namen in Grün.

[[shot:obj_event_r2|Das Rollout Ereignisobjekt eines Todes-Sounds aus dem Beispielmodell.]]

## Das Event wählen

| Nr. | Was es macht |
|---|---|
| [[ctl:5]] | Die Art des Events (siehe die Tabelle unten). |
| [[ctl:8]] | Das genaue Event aus der Liste des Spiels, zum Beispiel ein Sound oder eine Blutfarbe. |
| [[ctl:13]] | Übernimmt deine Wahl und benennt das Objekt. Die Zeile unter dem Knopf zeigt den Event-Code. |

Du kannst einen Event-Code auch selbst in *Eigene Daten* eingeben.

| Typ | Code beginnt mit | Was passiert |
|---|---|---|
| FootPrint | `FPT` | Eine Fußspur auf dem Boden. |
| BloodSplat | `SPL` | Ein Spritzer auf dem Boden, zum Beispiel Blut. |
| UberSplat | `UBR` | Eine große Bodenmarke, zum Beispiel ein Krater oder ein Gebäudefundament. |
| Sound | `SND` | Ein Sound. |
| SpawnObject | `SPN` | Ein Modell erscheint, zum Beispiel ein fallendes Körperteil. |

## Wann es passiert

| Nr. | Was es macht |
|---|---|
| [[ctl:19]] | Die Zeitpunkte, an denen das Event passiert. |
| [[ctl:20]] | Schiebe den Zeitschieber auf den richtigen Frame und klicke dann hierauf, um einen Zeitpunkt hinzuzufügen. |
| [[ctl:21]] | Entfernt den ausgewählten Zeitpunkt. |
| [[ctl:22]] | Entfernt alle Zeitpunkte. |

!!! tip
    Springe mit dem *Sequenz-Manager* zur Animation. Schiebe dann den Zeitschieber Frame für Frame weiter, bis du den genauen Moment findest, zum Beispiel wenn der Fuß den Boden berührt.
