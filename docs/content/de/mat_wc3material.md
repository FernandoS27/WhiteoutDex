# Wc3Material

Jedes Mesh, das du exportierst, braucht ein **Wc3Material**. Es enthält die Texturen und sagt dem Spiel, wie es sie zeichnen soll. Der Importer erstellt Wc3Materials für dich. Für deine eigenen Meshes erstellst du eins im Material Editor. Oder du wandelst deine Materialien mit *Material Fix* um.

Die Bilder zeigen ein Material des Beispielmodells im Compact Material Editor.

## Material-Grundparameter

[[shot:mat_wc3material_r1|Material-Grundparameter.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:3]] | **Shader.** *SD* für klassische Modelle, *HD* für Reforged, *SD on HD* für klassische Texturen in einem Reforged-Modell, *Crystal* für den Kristall-Shader von Reforged. |
| [[ctl:5]] | **Filtermodus** – wie das Material gezeichnet wird (siehe die Tabelle unten). |
| [[ctl:8]] | Beide Seiten der Flächen werden gezeichnet. |
| [[ctl:9]] | Ignoriert das Licht. Gut für leuchtende Teile. |
| [[ctl:10]] | Ignoriert den Nebel. |
| [[ctl:11]] | Eine Farbe für das ganze Mesh. |
| [[ctl:12]] | Setzt das Flag *Full Resolution* des Materials. |
| [[ctl:13]] | Wird über anderen Dingen gezeichnet. |
| [[ctl:14]] | Verdeckt nicht, was dahinter ist. |
| [[ctl:15]] | Der Spieler kann diesen Teil nicht anklicken. |
| [[ctl:16]] | Nutzt Ambient Occlusion (Reforged). |
| [[ctl:17]] | Rückseiten werfen Schatten (Reforged). |
| [[ctl:18]] | Überhaupt nicht beleuchtet (Reforged). |
| [[ctl:20]] | Zeichenreihenfolge transparenter Layer: von nah nach fern oder von fern nach nah. |
| [[ctl:24]] | Zeichenreihenfolge zwischen Materialien (−20 bis 20). Höher wird später gezeichnet. |
| [[ctl:26]] | Nutzt eine Textur, die das Spiel einsetzt: Teamfarbe, Team-Glühen, Klippe oder eine der Baumtexturen. |

| Filtermodus | Sieht aus | Nutze ihn für |
|---|---|---|
| [[t:mat_filter_mode_items#1]] | Undurchsichtig. | Normale Teile. |
| [[t:mat_filter_mode_items#2]] | Löcher, wo das Alpha niedrig ist, harte Kanten. | Blätter, Haare, Zäune. |
| [[t:mat_filter_mode_items#3]] | Weiche Transparenz. | Glas, weiche Kanten, Schatten. |
| [[t:mat_filter_mode_items#4]] | Fügt Licht hinzu, leuchtet. Schwarz ist unsichtbar. | Feuer, Magie, Lichtstrahlen. |
| [[t:mat_filter_mode_items#5]] | Fügt Licht hinzu, nutzt das Alpha. | Leuchten mit einer Form. |
| [[t:mat_filter_mode_items#6]] | Dunkelt ab, was dahinter ist. | Dunkle Flecken, Schatten. |
| [[t:mat_filter_mode_items#7]] | Dunkelt ab oder hellt auf. | Besondere Effekte. |

## Layer-Animation

[[shot:mat_wc3material_r2|Layer-Animation.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:6]] | Deckkraft des Layers (0 bis 1). Animiere sie, um das Material ein- und auszublenden. |
| [[ctl:7]] | Wie sich die Deckkraft zwischen den Keys ändert. |
| [[ctl:9]] | Schreibt die Deckkraft-Animation in das Modell. |
| [[ctl:11]] | Die Textur wechselt in Schritten über die Zeit, wie ein Daumenkino. |
| [[ctl:14]] | Welchen UV-Kanal die Textur nutzt. |
| [[ctl:15]] | Schreibt die Texturwechsel-Animation in das Modell. |

Bewegte Texturen (UV-Animation) stellst du auf der *Wc3Bitmap* ein, nicht hier.

## Material-Texturmaps

[[shot:mat_wc3material_r3|Material-Texturmaps.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:4]] | **Diffus:** die Farbtextur. Klassische Modelle nutzen nur diese. |
| [[ctl:5]] | Öffnet den Textur-Browser für diesen Slot (Texturen aus dem Spiel). |
| [[ctl:10]] | **Normal:** die Oberflächendetails (Reforged). |
| [[ctl:16]] | **ORM:** Occlusion, Roughness und Metal in einer Textur (Reforged). |
| [[ctl:22]] | **Emissiv:** Teile, die von selbst leuchten (Reforged). |
| [[ctl:28]] | **Teamfarbe:** eine Maske für die Teamfarbe (Reforged, optional). |
| [[ctl:34]] | **Umgebung:** die Spiegelungstextur (Reforged). |

Jeder Slot hat ein **Pfadpräfix**: den Ordner, den der Exporter vor den Dateinamen schreibt, zum Beispiel `Textures\`.

## Reforged-Einstellungen

[[shot:mat_wc3material_r4|Reforged-Einstellungen.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:5]] | Wie stark die Emissiv-Textur leuchtet. |
| [[ctl:9]] | Farbe des Fresnel-Lichts (Kantenlicht): Rot … |
| [[ctl:12]] | … Grün … |
| [[ctl:15]] | … und Blau. |
| [[ctl:18]] | Stärke des Kantenlichts. |
| [[ctl:21]] | Wie viel von der Teamfarbe das Kantenlicht nutzt. |
