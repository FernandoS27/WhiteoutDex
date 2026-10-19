# Tutorial: Anhangspunkte und Events

Eine Einheit im Spiel braucht Orte für Effekte (Anhangspunkte) und Momente für Sounds und Fußspuren (Event-Objekte). In dieser Lektion fügst du beides zu einer Einheit hinzu.

**Du brauchst:** eine Einheit mit Knochen und einer *Walk*-Animation, zum Beispiel den importierten Paladin.

## Teil 1: Anhangspunkte

### Der schnelle Weg

Wenn deine Knochen übliche Namen haben (*Head*, *Chest*, *Hand*, *Foot* …):

1. Öffne **WhiteoutDex > [[t:menu_anim_general]]**.
2. Klicke auf [[t:anim_one_click_attachment_points_btn]]. WhiteoutDex erstellt die Punkte und verbindet sie mit den Knochen.

### Von Hand

1. Erstelle einen **Wc3 AttachPoint** (*Create > Helpers > Warcraft III Plugins*).
2. Klicke auf [[t:common_adsorb_btn]] und dann auf den Knochen, zum Beispiel die rechte Hand. Der Punkt springt dorthin und wird mit ihm verbunden.
3. Wähle in [[t:attach_attachment_parameters_title]] bei **Typ** den Eintrag *Hand* und bei **Seite** den Eintrag *Right*. Der Punkt heißt jetzt *Hand Right Ref*.

Dort zeigt das Spiel Zaubereffekte an der rechten Hand.

![Die Anhangspunkt-Parameter eines Anhangspunkts.](obj_attachpoint_r3.png)

**Punkte, die jede Einheit haben sollte:** *Origin*, *Head*, *Chest*, *Overhead*. Waffen nutzen *Hand Left/Right* oder *Weapon*.

## Teil 2: Fußspuren

1. Erstelle ein **Wc3 Event Object** und platziere es am linken Fuß. Verbinde es mit dem Fußknochen.
2. Wähle in [[t:event_event_type_grp]] bei **Typ** den Eintrag *FootPrint* und bei **Daten** eine Fußspur, zum Beispiel eine kleine.
3. Klicke auf [[t:event_confirm_btn]].
4. Öffne die Sequenz *Walk* im Sequenz-Manager. Schiebe den Zeitschieber auf den Frame, in dem der linke Fuß den Boden berührt.
5. Klicke auf [[t:event_add_btn]]. Wiederhole das für jeden Schritt des linken Fußes.
6. Mach dasselbe für den rechten Fuß mit einem zweiten Event-Objekt.

## Teil 3: Ein Sound

1. Erstelle ein weiteres Event-Objekt in der Nähe der Waffe.
2. Wähle den Typ *Sound* und bei **Daten** einen Treffer-Sound. Klicke dann auf [[t:event_confirm_btn]].
3. Geh in der *Attack*-Animation zu dem Frame, in dem die Waffe trifft. Klicke auf [[t:event_add_btn]].

## Prüfen

Öffne die Vorschau. Wenn **Ansicht > Ereignisobjekte** an ist, zeigt sie die Fußspuren und spielt die Sounds ab.

!!! tip
    Sieh dir ein Modell aus dem Spiel an (*Aus dem Spiel importieren*). Platziere Anhangspunkte und Events so, wie Blizzard es macht.
