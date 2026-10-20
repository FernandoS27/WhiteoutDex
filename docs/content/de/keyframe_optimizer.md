# Keyframe-Optimierer

Animationen aus anderen Programmen oder aus Motion Capture haben oft auf jedem Frame einen Key. Das macht das Modell groß. Der Keyframe-Optimierer entfernt Keys, die nicht gebraucht werden. Die Bewegung bleibt fast gleich.

Öffne ihn über **WhiteoutDex > [[t:menu_keyframe_optimizer]]**. Wähle vorher die animierten Objekte aus.

[[shot:keyframe_optimizer|Der Keyframe-Optimierer.]]

## Einstellungen

| Nr. | Was es macht |
|---|---|
| [[ctl:7#2]] | **Schwellwert:** wie stark sich die Kurve ändern darf. Ein höherer Wert entfernt mehr Keys. Fang mit 0.5 an. |
| [[ctl:11#2]] | **Schritt:** wie genau das Werkzeug misst, in Frames. Bleib unter 0.49. |
| [[ctl:13#2]] | Schreibt Details in den MAXScript Listener. Lass es aus, es ist langsamer. |

## Optimieren

| Nr. | Was es macht |
|---|---|
| [[ctl:3]] | Sortiert die Liste nach Zeit oder nach Name. |
| [[ctl:5]] | Die Sequenzen des Modells. Wähle die aus, die du optimieren willst. |
| [[ctl:7]] | Positions-Keys optimieren. |
| [[ctl:8]] | Rotations-Keys optimieren. |
| [[ctl:9]] | Skalierungs-Keys optimieren. |
| [[ctl:14]] | Optimiert die ausgewählten Sequenzen. |
| [[ctl:15]] | Optimiert alle Sequenzen. |
| [[ctl:16]] | Löscht Keys, die in keiner Sequenz liegen. Sie tun im Spiel nichts. |

!!! tip
    Speichere deine Szene, bevor du optimierst. Prüfe danach die Animation. Sieht etwas falsch aus, mach es rückgängig und versuche einen kleineren Schwellwert.
