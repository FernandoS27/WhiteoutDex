# Sichtbarkeits-Keyer

In Warcraft III zeigst und versteckst du Teile eines Modells mit **Sichtbarkeits-Keys**. Ein Beispiel ist eine Waffe, die nur in der Angriffsanimation erscheint. Ein anderes ist ein Leuchten, das nur erscheint, während die Einheit zaubert. Der Sichtbarkeits-Keyer setzt diese Keys für dich.

Öffne ihn über **WhiteoutDex > [[t:menu_visibility_keyer]]**. Wähle vorher die Objekte aus.

[[shot:visibility_keyer|Der Sichtbarkeits-Keyer.]]

| Nr. | Was es macht |
|---|---|
| [[ctl:3]] | Zeigt oder versteckt die ausgewählten Objekte im aktuellen Frame. |
| [[ctl:5]] | Fügt allen ausgewählten Objekten einen Sichtbarkeits-Controller hinzu. |
| [[ctl:6]] | Entfernt die Sichtbarkeits-Controller von den ausgewählten Objekten. |
| [[ctl:7]] | Kopiert die Sichtbarkeits-Keys des ausgewählten Objekts. |
| [[ctl:8]] | Fügt sie auf den ausgewählten Objekten ein. |
| [[ctl:9]] | Wie das Spiel zwischen Keys wechselt: *None* springt sofort, die anderen blenden über. |
| [[ctl:11]] | Gibt den vorhandenen Controllern den Typ, den du gewählt hast. |
| [[ctl:13]] | Setzt einen Key zum *Zeigen* im aktuellen Frame. |
| [[ctl:14]] | Setzt einen Key zum *Verstecken* im aktuellen Frame. |
| [[ctl:16]] | Macht das Objekt im ganzen Animationsbereich sichtbar. |
| [[ctl:17]] | Versteckt das Objekt im ganzen Animationsbereich. |
| [[ctl:19]] | Zeigt das Objekt in den Sequenzen, die du im *Sequenz-Manager* ausgewählt hast. |
| [[ctl:20]] | Versteckt das Objekt in den ausgewählten Sequenzen. |

## Beispiel: Eine Waffe nur für den Angriff

1. Wähle die Waffe aus.
2. Klicke auf [[ctl:17]], um sie überall zu verstecken.
3. Wähle im *Sequenz-Manager* die *Attack*-Sequenzen aus.
4. Klicke auf [[ctl:19]].

!!! tip
    Nutze für die meisten Sichtbarkeits-Keys die Interpolation *None*. Bei den anderen Typen blendet das Spiel das Objekt weich ein und aus. Das sieht bei den meisten Teilen falsch aus.
