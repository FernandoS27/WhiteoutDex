// MDLXImporter — Import options dialog
#pragma once

#include "mdlx_import_options.h"
#include <windows.h>
#include <cstddef>
#include <memory>
#include <string>

// What the parsed file contains. DoImport parses the model before the dialog
// opens, so the dialog can show the file, its version and how many objects of
// each kind it holds; a category the file does not contain is shown unchecked
// and disabled.
struct ImportFileInfo {
    std::wstring path;          // file being imported
    uint32_t version = 0;       // true on-disk version (before the parser's upgrade)
    bool isReforged = false;    // version >= 900: CASC wording, FaceFX / Corn rows

    int geosets = 0, materials = 0, textures = 0, sequences = 0;
    int bones = 0, helpers = 0, lights = 0, attachments = 0;
    int particleEmitters1 = 0, particleEmitters2 = 0, ribbonEmitters = 0;
    int collisionShapes = 0, eventObjects = 0, cameras = 0;
    int cornEmitters = 0, faceEffects = 0;
};

// The steps DoImport reports while it runs. The dialog shows each step's text
// and moves the bar through that step's share of the whole import.
enum class ImportStep {
    Preparing, Skeleton, Meshes, Textures, Materials, Objects, Skin, Animations, Finishing,
    Count
};

struct ImportDialogImpl;

// The import options dialog.
//
// run() shows it and returns when the user chose Import (true) or Cancel
// (false); on true, opts holds the user's choices. After Import the window
// stays open, and with the first step() its footer turns into a progress bar
// with the current step. Max stays disabled, as under a modal dialog, until
// close() or the destructor. The import runs on the main thread (3ds Max scene calls must),
// so every progress call repaints the footer directly.
//
// Every progress call does nothing when the dialog was never shown (scripted
// #noPrompt imports), so DoImport can call them unconditionally.
class ImportDialog {
public:
    // |hInstance| is the DLL module handle (from DllMain), |hWndParent| the
    // Max main window. Nothing is shown until run().
    ImportDialog(HINSTANCE hInstance, HWND hWndParent);
    ~ImportDialog();
    ImportDialog(const ImportDialog&) = delete;
    ImportDialog& operator=(const ImportDialog&) = delete;

    bool run(MdlxImportOptions& opts, const ImportFileInfo& file);

    // Starts |s|: its text replaces the previous one and the bar jumps to the
    // start of its share.
    void step(ImportStep s);
    // |done| of |total| items of the current step are finished.
    void items(size_t done, size_t total);

    // Closes the window and enables Max again. Safe to call more than once.
    void close();

private:
    std::unique_ptr<ImportDialogImpl> impl_;
};
