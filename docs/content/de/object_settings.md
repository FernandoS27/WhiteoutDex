# Objekteinstellungen

Warcraft-III-Objekte haben zusätzliche Einstellungen, die 3ds Max nicht hat, zum Beispiel *Billboard*. Die Objekteinstellungen setzen sie auf den ausgewählten Objekten. WhiteoutDex speichert sie im Objekt (als User Properties), und der Exporter schreibt sie in das Modell.

Öffne sie über **WhiteoutDex > [[t:menu_object_settings]]**. Wähle vorher ein oder mehrere Objekte aus.

[[shot:object_settings|Die Objekteinstellungen mit einem ausgewählten Knochen.]]

## Flags

| Nr. | Was es macht |
|---|---|
| [[ctl:4]] | Das Objekt bewegt sich nicht mit seinem Elternobjekt. |
| [[ctl:5]] | Das Objekt dreht sich nicht mit seinem Elternobjekt. |
| [[ctl:6]] | Das Objekt skaliert nicht mit seinem Elternobjekt. |
| [[ctl:7]] | Das Objekt schaut immer zur Kamera. Gut für Leuchten und flache Effekte. |
| [[ctl:8]] | Schaut zur Kamera, dreht sich aber nur um die X-Achse. |
| [[ctl:9]] | … nur um die Y-Achse. |
| [[ctl:10]] | … nur um die Z-Achse. |
| [[ctl:11]] | Das Objekt bleibt in der Kameraansicht an seinem Platz. |

## Mesh

| Nr. | Was es macht |
|---|---|
| [[ctl:13]] | **Normales Skinning:** nutzt den Skin-Modifikator oder die verbundenen Knochen. |
| [[ctl:14]] | **ArtTools-Skinning:** verbindet beim Export die Vertices getrennter Meshes. Nutze es nur bei Meshes, die ineinander übergehen sollen. |
| [[ctl:17]] | Der Spieler kann dieses Mesh im Spiel nicht anklicken. |
