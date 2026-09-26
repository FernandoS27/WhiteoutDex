// MDLXExporter — Export options dialog
#pragma once

#include "mdx_export_options.h"
#include <windows.h>
#include <cstddef>
#include <memory>
#include <string>

// Name of the currently open scene with the directory and the .max extension
// stripped ("D:\work\arquebus_21.max" -> "arquebus_21"). Empty when the scene
// has never been saved. This is the default MODL name for an export.
std::wstring currentSceneModelName();

// The steps DoExport reports while it runs. The dialog shows each step's text
// and moves the bar through that step's share of the whole export.
enum class ExportStep {
    Scene, Bones, Meshes, Objects, Animations, Optimizing, Textures, Building, Writing,
    Count
};

struct ExportDialogImpl;

// The export options dialog.
//
// run() shows it and returns when the user chose Export (true) or Cancel
// (false); on true, opts holds the user's choices. After Export the window
// stays open, and with the first step() its footer turns into a progress bar
// with the current step. Max stays disabled, as under a modal dialog, until
// close() or the destructor. The export runs on the main thread (3ds Max
// scene calls must), so every progress call repaints the footer directly.
//
// Every progress call does nothing when the dialog was never shown (scripted
// #noPrompt exports), so DoExport can call them unconditionally.
class ExportDialog {
public:
    // |hInstance| is the DLL module handle (from DllMain), |hWndParent| the
    // Max main window. Nothing is shown until run().
    ExportDialog(HINSTANCE hInstance, HWND hWndParent);
    ~ExportDialog();
    ExportDialog(const ExportDialog&) = delete;
    ExportDialog& operator=(const ExportDialog&) = delete;

    // |targetPath| is the file the export will write (File > Export passes
    // it). Pass nullptr when the file is chosen after the dialog (mdxExport);
    // the dialog header then says so instead of showing a file name.
    bool run(MdxExportOptions& opts, const wchar_t* targetPath = nullptr);

    // The open window (owner for a file dialog shown after run), or nullptr.
    HWND window() const;

    // Starts |s|: its text replaces the previous one and the bar jumps to the
    // start of its share.
    void step(ExportStep s);
    // |done| of |total| items of the current step are finished.
    void items(size_t done, size_t total);

    // Closes the window and enables Max again. Safe to call more than once.
    void close();

private:
    std::unique_ptr<ExportDialogImpl> impl_;
};
