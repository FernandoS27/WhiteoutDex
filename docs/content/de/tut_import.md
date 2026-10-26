# Tutorial: Ein Modell importieren

In dieser Lektion lädst du ein Warcraft-III-Modell in 3ds Max. Dann siehst du dir an, was WhiteoutDex daraus macht.

**Du brauchst:** eine Modelldatei. WhiteoutDex hat Beispiele im Ordner `samples` des Quellcodes, zum Beispiel `HeroPaladin.mdx`. Du kannst auch jede andere `.mdx`- oder `.mdl`-Datei nehmen, aus dem Internet oder aus deinen eigenen Karten.

## Schritt 1: Den Import starten

1. Wähle **WhiteoutDex > [[t:menu_importer]]**.
2. Wähle die Datei `HeroPaladin.mdx` und öffne sie.

Das Import-Fenster öffnet sich.

![Das Import-Fenster. Es zeigt, was das Modell enthält.](importer.png)

## Schritt 2: Die Optionen wählen

1. Wähle unter [[t:imp_fast_settings_lbl]] die Vorlage [[t:imp_fast_settings_labels#6]].
2. Wähle unter [[t:imp_mode_lbl]] den Eintrag [[t:imp_mode_labels#1]].
3. Klicke auf [[t:imp_import_btn]].

Nach ein paar Sekunden ist das Modell in deiner Szene.

![Der importierte Paladin mit seinen Anhangspunkten (gelb) und Event-Objekten (grün).](tut_model_helpers.png)

## Schritt 3: Sieh dich um

Die Szene enthält jetzt:

- **Die Meshes** des Modells mit *Wc3Materials* und den Texturen.
- **Die Knochen**, die die Meshes bewegen.
- **Anhangspunkte** mit gelben Namen, zum Beispiel *Hand Right Ref*.
- **Event-Objekte** mit grünen Namen, zum Beispiel die Fußspuren *FPT…* und die Sounds *SND…*.
- **Kollisionsformen**, **Partikelemitter** und **Ribbons**, wenn das Modell sie hat.

## Schritt 4: Die Animationen abspielen

1. Öffne **WhiteoutDex > [[t:menu_sequence_manager]]**.
2. Doppelklicke eine Sequenz, zum Beispiel *Walk*.

3ds Max spielt die Laufanimation ab.

!!! tip
    Zu viele Helfer auf dem Bildschirm? Öffne den [[t:menu_node_manager]], hake [[t:node_helpers_chk]], [[t:node_attachments_chk]] und [[t:node_event_objects_chk]] an und klicke auf [[t:node_hide_btn]].

**Nächste Lektion:** Sieh dir das Modell in der Vorschau WhiteoutFlakes an.
