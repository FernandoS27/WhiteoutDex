# Anhangspunkt

Ein Anhangspunkt markiert eine Stelle am Modell. Dort hängt das Spiel Dinge an: die Waffe eines Helden, einen Zaubereffekt an der Hand, den Pfeil, der die Brust trifft, die Lebensleiste über dem Kopf.

Erstelle ihn über **Create > Helpers > Warcraft III Plugins > Wc3 AttachPoint**. Verbinde ihn mit dem Knochen, dem er folgen soll.

Im Viewport zeigt jeder Anhangspunkt seinen Namen in Gelb.

## Allgemeine Funktionen

[[shot:obj_attachpoint_r1|Allgemeine Funktionen.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:3]] | Klicke hierauf und dann auf ein Objekt im Viewport. Der Anhangspunkt springt zu diesem Objekt und wird mit ihm verbunden. |

## Modell-Map-Parameter

[[shot:obj_attachpoint_r2|Modell-Map-Parameter.]]

Ein Anhangspunkt kann ein eigenes Modell tragen, zum Beispiel ein Schwert, das die Einheit immer hält.

| Nr. | Was es macht |
|---|---|
| [[ctl:9]] | Wähle die Modelldatei auf deiner Festplatte. |
| [[ctl:10]] | Wähle ein Modell aus den Spielarchiven. |

Die zwei Felder über den Knöpfen enthalten den Ordner und den Dateinamen. Der Pfad ist relativ zum Warcraft-III-Ordner, zum Beispiel `Textures\Model.mdx`.

## Anhangspunkt-Parameter

[[shot:obj_attachpoint_r3|Anhangspunkt-Parameter.]]

Das Spiel findet einen Anhangspunkt an seinem **Namen**. Du baust den Namen aus vier Teilen:

| Nr. | Was es macht |
|---|---|
| [[ctl:5]] | **Typ:** die Hauptstelle: *Head*, *Overhead*, *Origin*, *Foot*, *Chest*, *Hand*, *Weapon* oder *Sprite*. |
| [[ctl:8]] | **Reittier:** für Reiter auf einem Reittier. |
| [[ctl:11]] | **Seite:** *Left* oder *Right*, für Hände und Füße. |
| [[ctl:14]] | **Zusatz:** weitere Wörter, zum Beispiel *Rear*, *Alternate*, *RallyPoint*, *Gold*, oder *First* … *Sixth* für Gebäude. |
| [[ctl:22]] | Nutzt das Modell aus *Modell-Map-Parameter*. |
| [[ctl:26]] | Sichtbarkeit (0 bis 1). Animiere sie, um das angehängte Modell zu zeigen oder zu verstecken. |
| [[ctl:27]] | Zeigt, wozu der Modellpfad relativ ist. |
| [[ctl:34]] | Eine Nummer für den Anhangspunkt. |
| [[ctl:36]] | Schreibt diese Nummer in das Modell. |

Beispiel: Typ *Hand* und Seite *Right* ergeben den Namen `Hand Right Ref`. Dort setzt das Spiel einen Zaubereffekt für die rechte Hand hin.

!!! tip
    Das Werkzeug [[t:anim_one_click_attachment_points_btn]] in *Anim: Allgemeine Werkzeuge* erstellt die üblichen Anhangspunkte für dich, wenn deine Knochen bekannte Namen haben.
