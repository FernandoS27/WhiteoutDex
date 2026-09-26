// MDLXExporter — MdxExporterPlugin (SceneExport) + ClassDesc
#pragma once

#include <max.h>
#include <impexp.h>
#include <iparamb2.h>
#include <maxversion.h>

#define MDX_EXPORTER_CLASS_ID Class_ID(0x7A3B5C01, 0x4D2E6F00)

class ExportDialog;

class MdxExporterPlugin : public SceneExport {
public:
    // mdxExport() shows the options dialog itself (before the file is chosen)
    // and runs DoExport without prompts; this hands that dialog over so the
    // export's progress shows in it. DoExport closes it when done.
    void setProgressDialog(ExportDialog* dialog) { progressDialog_ = dialog; }

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
    BOOL SupportsOptions(int ext, DWORD options) override;

    int DoExport(const TCHAR* name, ExpInterface* ei, Interface* gi,
                 BOOL suppressPrompts = FALSE, DWORD options = 0) override;

private:
    ExportDialog* progressDialog_ = nullptr;
};

class MdxExporterClassDesc : public ClassDesc2 {
public:
    int IsPublic() override { return TRUE; }
    void* Create(BOOL /*loading*/) override { return new MdxExporterPlugin; }
    const TCHAR* ClassName() override { return _T("WhiteoutDex MDX Exporter"); }
#if MAX_VERSION_MAJOR >= 24  // Max 2022+
    const TCHAR* NonLocalizedClassName() override { return _T("WhiteoutDex MDX Exporter"); }
#endif
    SClass_ID SuperClassID() override { return SCENE_EXPORT_CLASS_ID; }
    Class_ID ClassID() override { return MDX_EXPORTER_CLASS_ID; }
    const TCHAR* Category() override { return _T(""); }
};
