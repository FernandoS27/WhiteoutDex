# Sequenz-Manager

Ein Warcraft-III-Modell hat alle seine Animationen auf einer Zeitleiste. Jede Animation ist eine **Sequenz**: ein Name, ein Startframe und ein Endframe. Der Sequenz-Manager erstellt und ändert diese Sequenzen.

Öffne ihn über **WhiteoutDex > [[t:menu_sequence_manager]]** oder mit dem Knopf **Sequence** in der Seitenleiste.

[[shot:sequence_manager_selected|Der Sequenz-Manager mit der ausgewählten Sequenz „Stand - 1“.]]

## Die Liste

| Nr. | Was es macht |
|---|---|
| [[ctl:4]] | Gib ein Wort ein. Dann zeigt die Liste nur die Sequenzen mit diesem Wort im Namen. |
| [[ctl:6]] | Leert das Suchfeld und zeigt wieder alle Sequenzen. |
| [[ctl:7]] | Alle Sequenzen des Modells. Klicke, um eine auszuwählen. Halte [[key:Strg]] gedrückt, um mehrere auszuwählen. **Doppelklicke** eine Sequenz, um sie im Viewport abzuspielen. Zeilen wie `[GLOBAL 1]` sind globale Sequenzen. |
| [[ctl:10]] | Wählt alle Sequenzen aus. |
| [[ctl:11]] | Wählt keine aus. |
| [[ctl:12]] | Wählt die nicht ausgewählten aus und hebt die Auswahl der anderen auf. |

## Erstellen und entfernen

| Nr. | Was es macht |
|---|---|
| [[ctl:14]] | Erstellt eine neue Sequenz (siehe unten). |
| [[ctl:15]] | Löscht die ausgewählten Sequenzen. Die Animations-Keys bleiben auf der Zeitleiste. |
| [[ctl:16]] | Kopiert die ausgewählten Sequenzen. |
| [[ctl:17]] | Öffnet den *Globalen Sequenz-Manager*. |

## Die ausgewählte Sequenz ändern

| Nr. | Was es macht |
|---|---|
| [[ctl:20]] | Der Name. Am Namen erkennt das Spiel, wann es die Animation abspielt. Siehe *Animationsnamen*. |
| [[ctl:23]] | Nimmt den aktuellen Frame des Zeitschiebers als Startframe. |
| [[ctl:26]] | Der erste Frame der Sequenz. |
| [[ctl:28]] | Nimmt den aktuellen Frame als Endframe. |
| [[ctl:31]] | Der letzte Frame der Sequenz. |
| [[ctl:32]] | **Seltenheit** (0–40). Bei Animationen mit demselben Namen gilt: Ein höherer Wert bedeutet, dass das Spiel diese Animation seltener abspielt. |
| [[ctl:36]] | **Bewegungsgeschwindigkeit** (0–700). So schnell bewegt sich die Einheit, wenn sie diese Laufanimation abspielt. Die meisten Einheiten haben 250 bis 400. |
| [[ctl:40]] | Die Animation läuft einmal und wiederholt sich nicht. Nutze das für *Death*, *Attack* und *Spell*. |
| [[ctl:41]] | Der Exporter lässt diese Sequenz weg. |
| [[ctl:44]] | Setzt den Startframe einen Frame früher. Der Knopf [[ctl:45]] setzt ihn einen Frame später. |
| [[ctl:47]] | Setzt den Endframe einen Frame früher. Der Knopf [[ctl:48]] setzt ihn einen Frame später. |

## Speichern und laden

| Nr. | Was es macht |
|---|---|
| [[ctl:50]] | Lädt Sequenzen aus einer Textdatei. |
| [[ctl:51]] | Speichert die Sequenzen in eine Textdatei. So kopierst du sie in eine andere Szene. |

## Eine Sequenz erstellen

[[shot:sequence_new|Das Fenster für eine neue Sequenz.]]

1. Klicke auf [[t:seq_new_btn]].
2. Wähle eine Vorlage, zum Beispiel *Walk*. Die Vorlage setzt einen guten Namen und eine gute Länge.
3. Prüfe den Namen und die Frames. Die neue Sequenz beginnt nach der letzten.
4. Klicke auf [[t:seq_ok_btn]].

| Nr. | Was es macht |
|---|---|
| [[ctl:4]] | Vorlagen für die üblichen Animationen, oder *Custom*. |
| [[ctl:7]] | Der Name der neuen Sequenz. |
| [[ctl:12]] | Erster Frame. |
| [[ctl:16]] | Letzter Frame. |
| [[ctl:17]] | Schaltet den Wert für die Seltenheit ein. |
| [[ctl:21]] | Schaltet die Bewegungsgeschwindigkeit ein. |
| [[ctl:25]] | Die Animation läuft nur einmal. |

!!! tip
    Lass ein paar leere Frames zwischen zwei Sequenzen. Dann ändern die Keys der einen Animation nicht die nächste.

!!! note
    WhiteoutDex speichert die Sequenzen in deiner `.max`-Datei. Ältere Szenen mit Sequenzen in *Note Tracks* funktionieren weiter: WhiteoutDex liest sie auch.
