# Der Importer

Der Importer lädt ein Warcraft-III-Modell (`.mdx` oder `.mdl`) in 3ds Max. Er lädt auch die Texturen des Modells aus deinem Spiel.

## Importer öffnen

Du hast drei Wege:

- Menü **WhiteoutDex > [[t:menu_importer]]**
- Der Knopf **Import** in der WhiteoutDex-Seitenleiste
- **Datei > Importieren** und den Dateityp *Warcraft III MDX/MDL Model* wählen

Wähle eine Datei. Dann öffnet sich das Import-Fenster.

## Das Import-Fenster

[[shot:importer|Das Import-Fenster mit dem Reiter Objekte.]]

Oben im Fenster siehst du den Dateinamen, die Spielversion des Modells (*Klassisch* oder *Reforged*) und wie viele Knochen, Meshes (Geosets) und Animationen es hat.

| Nr. | Was es macht |
|---|---|
| [[ctl:1052]] | **Schnelleinstellungen.** Wähle eine Vorlage. Sie stellt alle Schalter darunter für dich ein. *Alles* importiert alles. Die anderen Vorlagen importieren weniger, zum Beispiel nur das Mesh ohne Materialien. |
| [[ctl:1059]] | Löscht die offene Szene und importiert das Modell in eine leere Szene. |
| [[ctl:1060]] | Fügt das Modell zur offenen Szene hinzu. So kombinierst du Modelle. |
| [[ctl:1061]] | Zeigt die Objekte des Modells (dieses Bild). |
| [[ctl:1062]] | Zeigt die Animations-Optionen. |
| [[ctl:1063]] | Zeigt die Optionen für Mesh, Materialien und Texturen. |
| [[ctl:1021]] | Schalter für alle Objekte. Ist er aus, werden keine Objekte importiert. |
| [[ctl:1022]] | Das Skelett. Du brauchst es für Animationen und für geskinnte Meshes. |
| [[ctl:1023]] | Helfer-Objekte. Sie bewegen andere Objekte, sind im Spiel aber unsichtbar. |
| [[ctl:1024]] | Lichter. |
| [[ctl:1025]] | Anhangspunkte, zum Beispiel für Waffen und Zaubereffekte. |
| [[ctl:1028]] | Event-Objekte, zum Beispiel Sounds, Fußspuren und Blut. |
| [[ctl:1026]] | Partikelemitter 1: Emitter, die kleine Modelle erzeugen. |
| [[ctl:1027]] | Partikelemitter 2: Emitter für Feuer, Rauch, Funken und Ähnliches. |
| [[ctl:1029]] | Bandemitter (Ribbons): Spuren, zum Beispiel hinter einem Schwert. |
| [[ctl:1030]] | Kollisionsformen (Boxen und Kugeln). Daran erkennt das Spiel, wo du das Modell anklicken kannst. |
| [[ctl:1031]] | Kameras, zum Beispiel die Porträt-Kamera. |
| [[ctl:1]] | Startet den Import. |
| [[ctl:2]] | Schließt das Fenster ohne Import. |

Die Zahl neben jedem Objekttyp zeigt, wie viele das Modell hat. Typen, die das Modell nicht hat, sind grau.

!!! note
    Reforged-Modelle können zwei weitere Objekttypen haben: *Popcorn-FX*-Emitter und *FaceFX*. Du siehst sie nur, wenn das Modell sie hat.

### Reiter Animationen

[[shot:importer_animations|Der Reiter Animationen.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:1041]] | Schalter für alle Animationen. Ist er aus, kommt das Modell ohne Bewegung. |
| [[ctl:1042]] | Bewegung (Positions-Keys). |
| [[ctl:1043]] | Rotations-Keys. |
| [[ctl:1044]] | Skalierungs-Keys. |
| [[ctl:1048]] | Keys zum Ein- und Ausblenden. |
| [[ctl:1045]] | Animierte Werte von Objekten, zum Beispiel die Stärke eines Lichts oder die Emissionsrate von Partikeln. |
| [[ctl:1046]] | Bewegte Texturkoordinaten (UV-Animation). |
| [[ctl:1047]] | Texturwechsel über die Zeit (Daumenkino). |
| [[ctl:1049]] | Farbwechsel über die Zeit. |

### Reiter Modell

[[shot:importer_model|Der Reiter Modell.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:1001]] | Importiert den Skin: welcher Knochen welchen Vertex bewegt. Ohne ihn folgt das Mesh den Knochen nicht. |
| [[ctl:1011]] | Erstellt WhiteoutDex-Materialien für das Modell. |
| [[ctl:1012]] | Lädt die Texturen. WhiteoutDex sucht in deinen Spielordnern (siehe *Einstellungen*). |
| [[ctl:1071]] | Importiert Helfer als *Point*-Objekte. Ist es aus, kommen Helfer als Knochen. |
| [[ctl:1081]] | Verschweißt Vertices an derselben Stelle und macht das Mesh einfacher. |
| [[ctl:1082]] | Entfernt Knochen und Helfer ohne Animation, die nicht gebraucht werden. |

## Woher die Texturen kommen

WhiteoutDex sucht jede Textur in dieser Reihenfolge:

1. Im Ordner des Modells und in seinen Unterordnern.
2. In den Spielarchiven. Bei klassischen Modellen (Version 800) zuerst in den **MPQ**-Archiven, dann in **CASC**. Bei Reforged-Modellen umgekehrt.

Die unterste Zeile des Fensters zeigt dir, welche Archive es nutzt. Die Spielordner stellst du unter [[t:menu_settings]] ein.

!!! tip
    Der Importer speichert die Texturen, die er aus den Spielarchiven holt, neben der Modelldatei. So findet er sie beim nächsten Mal sofort.

## Gut zu wissen

- Der Importer merkt sich deine Auswahl für das nächste Mal.
- Ein Modell direkt aus dem Spiel importieren, ohne Datei auf deiner Festplatte: siehe *Aus dem Spiel importieren*.
