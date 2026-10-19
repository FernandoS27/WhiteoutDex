# Animationsnamen

Warcraft III findet eine Animation an ihrem **Namen**. Eine Einheit, die angreift, spielt die Animation ab, die mit *Attack* beginnt. Darum sind die Namen deiner Sequenzen wichtig.

Ein Name hat ein **Hauptwort** und kann **Zusatzwörter** haben. Beispiele: `Stand`, `Stand Ready`, `Attack Slam`, `Walk Fast`, `Stand - 2`.

## Hauptwörter

| Name | Wann das Spiel ihn abspielt |
|---|---|
| Stand | Die Einheit macht nichts. |
| Walk | Die Einheit bewegt sich. |
| Attack | Die Einheit greift an. |
| Spell | Die Einheit wirkt einen Zauber. |
| Death | Die Einheit stirbt. |
| Decay | Der tote Körper verwest (*Decay Flesh*, dann *Decay Bone*). |
| Birth | Ein Gebäude wird gebaut oder eine Einheit erscheint. |
| Dissipate | Eine beschworene Einheit oder eine Geistereinheit löst sich auf, wenn sie stirbt. |
| Portrait | Das Gesicht im Porträtfenster. *Portrait Talk* läuft, während die Einheit spricht. |
| Morph | Die Einheit ändert ihre Form. |
| Sleep | Die Einheit schläft. |
| Cinematic | Nur für Zwischensequenzen und Karten. |

## Zusatzwörter

| Wort | Bedeutung |
|---|---|
| Ready | Bereit zum Kampf, zum Beispiel *Stand Ready*. |
| Slam | Ein starker Schlag, zum Beispiel *Attack Slam* oder *Spell Slam*. |
| Victory | Nach einem gewonnenen Kampf. |
| Hit | Die Einheit wird getroffen. |
| Channel | Ein Zauber, der etwas Zeit braucht. |
| Throw | Wirft etwas. |
| Fast | Schnellere Version, zum Beispiel *Walk Fast*. |
| Alternate | Die zweite Form einer Einheit, zum Beispiel nach *Metamorphosis*. |
| Upgrade First … Fifth | Ein Gebäude nach seiner 1. bis 5. Verbesserung. |
| Work, Gold, Lumber | Animationen von Arbeitern. |
| Swim | Bewegt sich im Wasser. |
| Defend | Mit eingeschalteter Fähigkeit *Defend*. |
| Talk | Spricht (mit *Portrait*). |

## Viele Animationen mit demselben Namen

Du kannst mehr als eine Animation mit demselben Hauptwort haben, zum Beispiel `Stand - 1`, `Stand - 2` und `Stand - 3`. Das Spiel wählt eine davon zufällig. Der Wert **Seltenheit** im [[t:menu_sequence_manager]] sagt dem Spiel, wie oft es eine Animation wählt: Ein höherer Wert bedeutet seltener.

## Längen, die gut funktionieren

Der [[t:menu_sequence_manager]] hat Vorlagen mit diesen Längen (bei 30 Frames pro Sekunde):

| Vorlage | Frames |
|---|---|
| Stand | 80 |
| Walk | 28 |
| Stand Ready | 20 |
| Attack | 24 |
| Attack Slam | 30 |
| Spell | 28 |
| Spell Slam | 24 |
| Death | 64 |
| Portrait Talk | 70 |

!!! note
    *Death*, *Decay*, *Birth* und *Dissipate* laufen nur einmal. Schalte für sie im Sequenz-Manager [[t:seq_non_looping_chk]] ein.
