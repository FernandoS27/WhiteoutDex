# Tutorial: Ein Feuereffekt

In dieser Lektion setzt du mit einem **Partikelemitter 2** ein Feuer auf eine Fackel.

## Schritt 1: Den Emitter erstellen

1. Wähle *Create > Geometry > Particle Systems > Wc3Particles2*.
2. Klicke in den Viewport, um ihn zu platzieren. Schiebe ihn an die Spitze der Fackel.
3. Verbinde ihn mit der Fackel (oder mit dem Knochen, der die Fackel hält). Dann bewegt er sich mit ihr.

## Schritt 2: Die Textur

1. Öffne das Rollout **Texture Options**.
2. Klicke auf [[t:bp2_browse_archives_btn]] und wähle eine Feuertextur aus dem Spiel (suche nach `fire` oder `flare`).
3. Stelle [[t:bp2_texture_blend_mode_lbl]] auf **Additive**. Additive Partikel leuchten.

## Schritt 3: Die Bewegung

In **Emitter Options** und **Timing Options**:

1. **Geschw.:** ein kleiner Wert, damit die Flammen langsam steigen.
2. **Kegelwinkel:** klein, damit die Flammen zusammenbleiben.
3. **Lebensdauer:** etwa 1 Sekunde.
4. **Emissionsrate:** etwa 20 Partikel pro Sekunde.

Du siehst die Partikel sofort im Viewport. Ändere die Werte, bis dir das Feuer gefällt.

## Schritt 4: Farbe und Ausblenden

In **Particle Options**:

1. **Farbe:** am Anfang gelb, in der Mitte orange, am Ende dunkelrot.
2. **Alpha:** Anfang 0, Mitte 255, Ende 0. Die Flammen blenden ein und aus.
3. **Skalierung:** am Anfang klein, in der Mitte größer, am Ende klein.

![Die Particle Options eines Emitters.](obj_particles2_r6.png)

## Schritt 5: Prüfen

Öffne die Vorschau (**WhiteoutDex > [[t:menu_renderer]]**). Sie zeigt das Feuer so, wie das Spiel es zeigen wird.

!!! tip
    Ein Feuer, das dir gefällt, speicherst du mit [[t:bp2_export_btn]] in *Particle Configuration*. Mit [[t:bp2_import_btn]] lädst du es auf andere Modelle.
