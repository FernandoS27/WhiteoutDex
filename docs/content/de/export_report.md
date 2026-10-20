# Der Export-Bericht

Nach einem Export sagt dir WhiteoutDex nur das, was deine Aufmerksamkeit braucht. Ist alles in Ordnung, siehst du nichts, und die Datei ist einfach da.

Braucht etwas deine Aufmerksamkeit, öffnet sich ein Fenster [[t:report_export_ptitle]]:

- Die erste Zeile zählt die Fehler und Warnungen: *Export abgeschlossen mit 0 Fehler(n) und 2 Warnung(en).*
- Jede weitere Zeile nennt ein Objekt und das Problem, zum Beispiel `WARNUNG: [Cape] …`.
- Das Fenster zeigt höchstens 50 Zeilen. Die volle Liste steht in der Datei `mdlx_export_debug.log` (siehe *Dateien und Einstellungen*).

## Fehler und Warnungen

| Art | Bedeutung | Was du tust |
|---|---|---|
| **[[t:report_error_prefix]]** | Etwas konnte nicht exportiert werden. Das Modell kann kaputt sein. | Repariere das genannte Objekt und exportiere noch einmal. |
| **[[t:report_warning_prefix]]** | Das Modell funktioniert, aber etwas kann anders aussehen als in 3ds Max. | Lies die Zeile. Oft repariert die Szenenprüfung ([[t:exp_fix_all_btn]]) das Problem. |

!!! tip
    Starte zuerst [[t:exp_fix_all_btn]] im Export-Fenster. Das repariert die häufigsten Probleme vor dem Export. So bleibt der Bericht kurz.

Der Importer hat dieselbe Art von Bericht. Sein Fenster heißt [[t:report_import_ptitle]].
