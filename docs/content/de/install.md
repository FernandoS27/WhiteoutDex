# Installation

Ein einziges Setup installiert WhiteoutDex in jede 3ds-Max-Version auf deinem Computer (2016 bis 2027).

## Installieren

1. Schließe 3ds Max.
2. Lade die neueste `WhiteoutDex_Setup_v<Version>.exe` herunter. Du findest sie auf GitHub unter **Releases** und auf Hive Workshop.
3. Starte das Setup. Windows fragt nach Administratorrechten. Klicke auf **Ja**.
4. Wähle deine Sprache und folge den Schritten. Das Setup findet alle deine 3ds-Max-Versionen selbst.
5. Starte 3ds Max. Jetzt siehst du das Menü **WhiteoutDex** und die WhiteoutDex-Seitenleiste.

!!! note
    3ds Max 2026 und neuer brauchen **.NET 8**. Das Setup sagt dir, wenn es fehlt.

## Wohin die Dateien kommen

WhiteoutDex wird in einen Ordner installiert:

`C:\ProgramData\Autodesk\ApplicationPlugins\WhiteoutDex`

3ds Max lädt alles von dort. Du musst keine Dateien selbst kopieren.

Deine persönlichen Einstellungen (Sprache, Spielordner, letzte Export-Optionen) liegen im 3ds-Max-Einstellungsordner `plugcfg`. Sie bleiben erhalten, wenn du WhiteoutDex aktualisierst.

## Aktualisieren

WhiteoutDex kann beim Start von 3ds Max nach neuen Versionen suchen (ab 3ds Max 2018). Wenn es eine neue Version gibt, fragt es dich:

- Klicke auf **Ja**, um sie herunterzuladen und zu installieren. 3ds Max schließt sich dafür.
- Klicke auf **Nein**, um weiterzuarbeiten. Am nächsten Tag fragt es wieder.

Du kannst das in den [[t:menu_settings]] ausschalten oder sofort suchen lassen.

## Deinstallieren

Öffne **Windows-Einstellungen > Apps**, suche **WhiteoutDex** und klicke auf **Deinstallieren**.
