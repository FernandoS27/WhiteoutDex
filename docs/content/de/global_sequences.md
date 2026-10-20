# Globale Sequenzen

Eine **globale Sequenz** ist eine Animation, die ständig läuft. Es ist egal, welche Animation die Einheit gerade abspielt. Nutze sie für Dinge, die nie aufhören: einen drehenden Kristall, eine brennende Fackel, eine Fahne im Wind.

Jede globale Sequenz hat eine Länge in Frames. Keys eines Objekts können zu einer globalen Sequenz gehören statt zur normalen Zeitleiste.

Öffne den Globalen Sequenz-Manager mit dem Knopf [[t:seq_globals_btn]] im *Sequenz-Manager*.

[[shot:global_sequences|Der Globale Sequenz-Manager mit den zwei globalen Sequenzen des Beispielmodells.]]

Die Liste oben zeigt alle globalen Sequenzen: Nummer, Frames, Typ und die Objekte, die sie nutzen.

| Nr. | Was es macht |
|---|---|
| [[ctl:7]] | Der Endframe (die Länge) der ausgewählten globalen Sequenz. |
| [[ctl:8]] | Liest die Szene neu. |
| [[ctl:9]] | Entfernt die ausgewählte globale Sequenz. |
| [[ctl:15]] | Die Länge einer neuen globalen Sequenz, in Frames. |
| [[ctl:24]] | Erstellt eine neue globale Sequenz auf den ausgewählten Objekten. |
| [[ctl:25]] | Ändert die ausgewählte globale Sequenz. |

## Ein Objekt ständig drehen lassen

1. Wähle das Objekt aus.
2. Stelle die **Dauer** ein, zum Beispiel 60 Frames.
3. Klicke auf [[t:gseq_new_btn]]. Der Zeitschieber springt auf Frame 0. Die Optionen für Regler, Kanäle und Drehung erscheinen.
4. Wähle den **Regler**, zum Beispiel *Linear*, und die Achse.
5. Schiebe den Zeitschieber auf den letzten Frame einer Umdrehung, zum Beispiel 60.
6. Klicke auf [[t:gseq__cw_btn]] (im Uhrzeigersinn) oder [[t:gseq_ccw_btn]] (gegen den Uhrzeigersinn).
7. Klicke auf [[t:gseq_finish_btn]].

Das Objekt dreht sich jetzt einmal in 60 Frames, in jeder Animation.

Du kannst auch von Hand animieren: Setze nach [[t:gseq_new_btn]] wie gewohnt Keys auf das Objekt, innerhalb der Dauer. Klicke dann auf [[t:gseq_finish_btn]]. WhiteoutDex verschiebt diese Keys in die neue globale Sequenz.

!!! note
    Das Spiel zählt globale Sequenzen in Millisekunden. WhiteoutDex rechnet die Frames für dich um.
