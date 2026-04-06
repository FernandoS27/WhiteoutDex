// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Fernando Sahmkow

#include "blpio.h"
#include <max.h>
#include <maxversion.h>

HINSTANCE g_hInstance = nullptr;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hInstance = hinstDLL;
        MaxSDK::Util::UseLanguagePackLocale();
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

// Use ClassDesc (not ClassDesc2) — simpler, no paramblk2.lib needed
class BlpClassDesc : public ClassDesc {
public:
    int           IsPublic()       override { return TRUE; }
    void*         Create(BOOL)     override { return new BlpBitmapIO(); }
    const MCHAR*  ClassName()      override { return _T("BLP Texture"); }
#if MAX_VERSION_MAJOR >= 24  // Max 2022+
    const MCHAR*  NonLocalizedClassName() override { return _T("BLP Texture"); }
#endif
    SClass_ID     SuperClassID()   override { return BMM_IO_CLASS_ID; }
    Class_ID      ClassID()        override { return BLP_IO_CLASS_ID; }
    const MCHAR*  Category()       override { return _T("Bitmap I/O"); }
    HINSTANCE     HInstance()      override { return g_hInstance; }
};

static BlpClassDesc g_blpClassDesc;

extern "C" {

__declspec(dllexport) int LibNumberClasses()         { return 1; }
__declspec(dllexport) ClassDesc* LibClassDesc(int i) { return (i == 0) ? &g_blpClassDesc : nullptr; }
__declspec(dllexport) const MCHAR* LibDescription()  { return _T("Blizzard BLP Texture I/O"); }
__declspec(dllexport) ULONG LibVersion()             { return VERSION_3DSMAX; }
__declspec(dllexport) ULONG CanAutoDefer()           { return FALSE; }

} // extern "C"
