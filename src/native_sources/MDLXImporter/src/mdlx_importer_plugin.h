// MDLXImporter — MDX/MDL Scene Importer plugin header
#pragma once

#include <max.h>
#include <impexp.h>
#include <iparamb2.h>
#include <maxversion.h>

#define MDLX_IMPORTER_CLASS_ID Class_ID(0x7A3B5C02, 0x4D2E6F01)

class MdlxImporterPlugin : public SceneImport {
public:
    int ExtCount() override;
    const TCHAR* Ext(int n) override;
    const TCHAR* LongDesc() override;
    const TCHAR* ShortDesc() override;
    const TCHAR* AuthorName() override;
    const TCHAR* CopyrightMessage() override;
    const TCHAR* OtherMessage1() override;
    const TCHAR* OtherMessage2() override;
    unsigned int Version() override;
    void ShowAbout(HWND hWnd) override;

    int DoImport(const TCHAR* name, ImpInterface* ii, Interface* gi,
                 BOOL suppressPrompts = FALSE) override;
};

class MdlxImporterClassDesc : public ClassDesc2 {
public:
    int IsPublic() override { return TRUE; }
    void* Create(BOOL /*loading*/) override { return new MdlxImporterPlugin; }
    const TCHAR* ClassName() override { return _T("WhiteoutDex MDX Importer"); }
#if MAX_VERSION_MAJOR >= 24  // Max 2022+
    const TCHAR* NonLocalizedClassName() override { return _T("WhiteoutDex MDX Importer"); }
#endif
    SClass_ID SuperClassID() override { return SCENE_IMPORT_CLASS_ID; }
    Class_ID ClassID() override { return MDLX_IMPORTER_CLASS_ID; }
    const TCHAR* Category() override { return _T(""); }
};
