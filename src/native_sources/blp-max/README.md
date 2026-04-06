# BLP Plugin für 3ds Max — Anleitung

## Ordnerstruktur

Lege den `blp-max` Ordner NEBEN deinen `WhiteoutLib` Ordner:

```
C:\Users\DeinName\Projekte\
├── WhiteoutLib\        ← dein vorhandenes Repo (unverändert!)
└── blp-max\            ← dieser Ordner hier
    ├── blpio.h
    ├── blpio.cpp
    ├── blpdll.cpp
    ├── blp.def
    ├── CMakeLists.txt
    └── BAUE_PLUGIN.bat
```

Das ist alles. WhiteoutLib wird NICHT verändert.


## Was du brauchst

1. **Visual Studio 2022 oder neuer** mit "Desktopentwicklung mit C++"
2. **3ds Max SDK** für deine Version (z.B. 2026)
3. **CMake** (kommt normalerweise mit Visual Studio mit)


## Bauen — 3 Schritte

### Schritt 1: Pfade anpassen

Öffne `BAUE_PLUGIN.bat` mit einem Texteditor und passe diese 2 Zeilen an:

```
set MAX_SDK=C:\Program Files\Autodesk\3ds Max 2026 SDK\maxsdk
set MAX_PLUGINS=C:\Program Files\Autodesk\3ds Max 2026\Plugins
```

### Schritt 2: Bauen

Rechtsklick auf `BAUE_PLUGIN.bat` → **"Als Administrator ausführen"**

Das baut automatisch WhiteoutLib UND das Plugin zusammen.

### Schritt 3: Testen

1. 3ds Max starten
2. Material Editor öffnen (Taste M)
3. Auf Diffuse Map klicken → Bitmap → eine .blp Datei laden
4. Die Textur wird direkt angezeigt!
