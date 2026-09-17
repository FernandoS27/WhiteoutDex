# BLP plug-in for 3ds Max

`blp-max` builds `blp.bmi`, a Bitmap I/O plug-in that lets 3ds Max open
Warcraft III `.blp` textures anywhere it accepts a bitmap — the Material
Editor, a Bitmap texmap, the asset browser.

It is a thin shell around WhiteoutLib's BLP decoder: `blpio.cpp` converts a
decoded image into a Max `BitmapStorage`, and `blpdll.cpp` exposes the class
descriptor. WhiteoutLib itself is not modified.

## Layout

The plug-in is built as part of the WhiteoutDex CMake tree, which supplies both
the `MaxSDK` interface library and `whiteout_lib`. It is not meant to be
configured standalone.

```
src/native_sources/blp-max/
├── blpio.h        - decoder-to-BitmapStorage bridge
├── blpio.cpp
├── blpdll.cpp     - DLL entry points and class descriptor
├── blp.def        - exported symbols
├── CMakeLists.txt - target definition (parent supplies the dependencies)
└── Build.bat      - standalone build for every installed Max SDK
```

## Building

The whole toolkit builds through the top-level `CMakeLists.txt`, which is the
normal path. `Build.bat` is there for iterating on the plug-in on its own.

Run it from this directory. It needs no arguments and no editing: it walks the
Max versions 2016 through 2027, and for each one where
`C:\Program Files\Autodesk\3ds Max <year> SDK\maxsdk\include\max.h` exists it
configures a `build_<year>` tree and builds it in Release. A version whose SDK
is not installed is skipped.

It picks the CMake generator from the installed Visual Studio: Visual Studio
2026 when `vswhere` reports one, otherwise Visual Studio 2022.

Each successful build is copied to two places:

- `output\blp_<year>.bmi`, next to the sources.
- `%APPDATA%\Autodesk\ApplicationPlugins\WhiteoutDex\native plugins\Max<year>\blp.bmi`,
  where WhiteoutDex loads it from.

The run ends with a count of how many versions built, failed and were skipped.

Writing into the per-user `%APPDATA%` plug-in folder needs no elevation. A
system-wide install under `Program Files` does.

## Checking that it worked

1. Start 3ds Max.
2. Open the Material Editor (keyboard `M`).
3. Click a Diffuse map slot, choose Bitmap, and load a `.blp` file.

The texture should display directly. If the file dialog does not offer `.blp`,
the `.bmi` did not load — confirm it is in the `Max<year>` folder that matches
the running version.
