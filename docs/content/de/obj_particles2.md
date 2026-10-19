# Partikelemitter 2

Der Partikelemitter 2 macht die meisten Effekte in Warcraft III: Feuer, Rauch, Funken, Staub, Magie. Er sendet viele kleine flache Bilder aus (Partikel). Jedes Partikel lebt kurz. Während es lebt, ändert es seine Farbe, Größe und Transparenz.

Erstelle ihn über **Create > Geometry > Particle Systems > Wc3Particles2**. WhiteoutDex zeigt die Partikel direkt im Viewport.

Die Bilder unten zeigen den Emitter des Beispielmodells *HeroPaladin*.

## Particle Configuration

[[shot:obj_particles2_r1|Particle Configuration.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:1030]] | Lädt alle Einstellungen aus einer `.ini`-Datei. |
| [[ctl:1031]] | Speichert alle Einstellungen in eine `.ini`-Datei. So kopierst du einen Effekt auf ein anderes Modell. |
| [[ctl:1032]] | Ist es an, lädt [[t:popcorn_import_btn]] auch die animierten Keys der Werte, nicht nur die festen Werte. |

## Texture Options

[[shot:obj_particles2_r2|Texture Options.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:1047]] | Der Ordner der Textur im Spiel, zum Beispiel `Textures\`. |
| [[ctl:3075]] | Der Dateiname der Textur. |
| [[ctl:1033]] | Wähle eine Texturdatei auf deiner Festplatte. |
| [[ctl:1034]] | Wähle eine Textur aus den Spielarchiven. |
| [[ctl:1035]] | Wie die Partikel gezeichnet werden: *Blend* (normale Transparenz), *Additive* (leuchtet, gut für Feuer und Magie), *Modulate*, *Modulate 2X* (dunkelt ab) oder *Alpha Key* (harte Kanten). |
| [[ctl:4113]] | Die Partikel ignorieren das Licht. |
| [[ctl:4115]] | Die Partikel ignorieren den Nebel. |
| [[ctl:4112]] | Sortiert die Partikel nach Entfernung. Nutze es, wenn sich transparente Partikel falsch überdecken. |
| [[ctl:4114]] | Nutzt die Teamfarbe oder das Team-Glühen als Textur. |
| [[ctl:3094]] | Die Textur ist ein Raster aus Bildern: Zahl der Zeilen … |
| [[ctl:3096]] | … und der Spalten. Das Partikel zeigt die Bilder nacheinander. |

## Emitter Options

[[shot:obj_particles2_r3|Emitter Options.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:3048]] | Wie schnell die Partikel wegfliegen. |
| [[ctl:3050]] | Wie stark sich die Geschwindigkeit von Partikel zu Partikel ändert (0 = alle gleich). |
| [[ctl:3052]] | Der Winkel des Kegels, in dem die Partikel fliegen. Ein kleiner Winkel macht einen schmalen Strahl, ein großer Winkel einen breiten Sprühnebel. |
| [[ctl:3057]] | Zieht die Partikel nach unten (oder mit einem negativen Wert nach oben). |
| [[ctl:1040]] | **Steuerung:** wie sich ein animierter Wert zwischen den Keys ändert (siehe *Glossar*). Jeder Wert hat seine eigene Steuerung. |

## Timing Options

[[shot:obj_particles2_r4|Timing Options.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:3042]] | Wie viele Sekunden ein Partikel lebt. |
| [[ctl:3053]] | Wie viele Partikel pro Sekunde starten. Animiere den Wert auf 0, um den Effekt zu stoppen. |
| [[ctl:1020]] | Sendet alle Partikel auf einmal bei einem Key aus, wie einen Stoß. |

## Size Options

[[shot:obj_particles2_r5|Size Options.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:3044]] | Breite der Fläche, auf der die Partikel starten. |
| [[ctl:3046]] | Länge dieser Fläche. |
| [[ctl:1028]] | Die Partikel starten auf einer Linie statt auf einer Fläche. |

## Particle Options

[[shot:obj_particles2_r6|Particle Options.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:1023]] | Das Partikel ist ein flaches Bild (Kopf). |
| [[ctl:1024]] | Das Partikel ist eine gestreckte Spur hinter seiner Bewegung (Schweif). |
| [[ctl:1025]] | Kopf und Schweif. |
| [[ctl:3073]] | Länge des Schweifs. |
| [[ctl:6027]] | Wann in seinem Leben das Partikel die Werte für die *Mitte* erreicht (0.5 = halbes Leben). |
| [[ctl:6033]] | Die Farbe am Anfang, in der Mitte und am Ende des Lebens. Klicke auf ein Feld, um eine Farbe zu wählen. |
| [[ctl:6037]] | Transparenz am Anfang, in der Mitte und am Ende: 0 = unsichtbar, 255 = undurchsichtig. |
| [[ctl:6041]] | Größe am Anfang, in der Mitte und am Ende. |
| [[ctl:6045]] | Welche Bilder des Texturrasters der Kopf zeigt, während er lebt … |
| [[ctl:6049]] | … und während er ausblendet. |
| [[ctl:6053]] | Dasselbe für den Schweif, während er lebt … |
| [[ctl:6057]] | … und während er ausblendet. |

## Other Options

[[shot:obj_particles2_r7|Other Options.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:3059]] | Zeichenreihenfolge. Höhere Werte werden später gezeichnet (obendrauf). |
| [[ctl:4116]] | Die Partikel bewegen sich mit dem Modell. Ohne diese Option bleiben sie, wo sie gestartet sind. |
| [[ctl:4117]] | Die Partikel liegen flach auf dem Boden, statt zur Kamera zu schauen. Gut für Ringe und Wellen. |

!!! tip
    Für Feuer und Magie nutze *Additive*, einen kleinen Kegelwinkel und eine kurze Lebensdauer. Lass Alpha von 0 auf 255 und zurück auf 0 gehen. Dann blenden die Partikel weich ein und aus.
