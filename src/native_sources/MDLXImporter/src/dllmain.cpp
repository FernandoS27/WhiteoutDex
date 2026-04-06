// MDLXImporter — MDX/MDL Scene Importer DLL entry point
#include <max.h>
#include <istdplug.h>
#include <iparamb2.h>
#include "mdlx_importer_plugin.h"

HINSTANCE hInstance;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID /*lpvReserved*/) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

static MdlxImporterClassDesc classDesc;

__declspec(dllexport) const TCHAR* LibDescription() {
    return _T("WhiteoutDex MDX/MDL Importer");
}

__declspec(dllexport) int LibNumberClasses() {
    return 1;
}

__declspec(dllexport) ClassDesc2* LibClassDesc(int i) {
    return (i == 0) ? &classDesc : nullptr;
}

__declspec(dllexport) ULONG LibVersion() {
    return VERSION_3DSMAX;
}

__declspec(dllexport) int LibInitialize() {
    return TRUE;
}

__declspec(dllexport) int LibShutdown() {
    return TRUE;
}
