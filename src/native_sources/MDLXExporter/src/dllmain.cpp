// MDLXExporter — MDX/MDL Scene Exporter DLL entry point
#include <max.h>
#include <istdplug.h>
#include <iparamb2.h>
#include <notify.h>
#include "mdx_exporter_plugin.h"

HINSTANCE hInstance;

HINSTANCE GetDllInstance() { return hInstance; }

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID /*lpvReserved*/) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

static MdxExporterClassDesc classDesc;

__declspec(dllexport) const TCHAR* LibDescription() {
    return _T("WhiteoutDex MDX/MDL Exporter");
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

// The importer keeps the model's code page as the root node's user property
// Wc3CodePage. Max 2016 keeps root user properties through Reset and New, so
// a scene made afterwards would still export its names in that code page:
// cleared there, without marking the scene as changed.
static void clearSceneCodePage(void*, NotifyInfo*) {
    Interface* ip = GetCOREInterface();
    INode* root = ip ? ip->GetRootNode() : nullptr;
    if (!root) return;
    int cp = 0;
    if (root->GetUserPropInt(_T("Wc3CodePage"), cp) && cp != 0) {
        const BOOL dirty = GetSaveRequiredFlag();
        root->SetUserPropInt(_T("Wc3CodePage"), 0);
        if (!dirty) SetSaveRequiredFlag(FALSE);
    }
}

__declspec(dllexport) int LibInitialize() {
    RegisterNotification(clearSceneCodePage, nullptr, NOTIFY_SYSTEM_POST_RESET);
    RegisterNotification(clearSceneCodePage, nullptr, NOTIFY_SYSTEM_POST_NEW);
    return TRUE;
}

__declspec(dllexport) int LibShutdown() {
    UnRegisterNotification(clearSceneCodePage, nullptr, NOTIFY_SYSTEM_POST_RESET);
    UnRegisterNotification(clearSceneCodePage, nullptr, NOTIFY_SYSTEM_POST_NEW);
    return TRUE;
}
