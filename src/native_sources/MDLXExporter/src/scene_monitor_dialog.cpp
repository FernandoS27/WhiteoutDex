// MDLXExporter — Scene Monitor: Problem Details Dialog
//
// Shows a ListView with all detected problems, grouped by type. The user
// selects rows (multi-select) and presses "Fix Selected Types" to apply
// the corresponding fixes. After each fix the scene is re-scanned and
// the listview is refreshed live. "Fix All" runs all four categories.
//
// Layout (180×170 DLU):
//   ┌────────────────────────────────────────────┐
//   │  ListView  (Type, Object/Name)             │
//   │                                            │
//   │                                            │
//   ├────────────────────────────────────────────┤
//   │  [Fix Selected]  [Fix All]    [Close]      │
//   └────────────────────────────────────────────┘

#include "scene_monitor.h"
#include "resource.h"

#include <max.h>
#include <commctrl.h>
#include <string>
#include <unordered_set>

namespace scene_monitor {

namespace {

// State carried through the dialog proc via GWLP_USERDATA.
struct DialogState {
    HINSTANCE   hInstance = nullptr;
    ScanResult  result;
};

const wchar_t* problemTypeName(ProblemType t) {
    switch (t) {
    case ProblemType::DuplicateName:        return L"Duplicate Name";
    case ProblemType::EmptyMesh:             return L"Empty Mesh";
    case ProblemType::EditablePoly:          return L"Editable Poly";
    case ProblemType::EditMeshAboveSkin:     return L"Edit_Mesh ↑ Skin";
    case ProblemType::InvalidController:     return L"Invalid Controller";
    case ProblemType::UnsupportedMaterial:   return L"Unsupported Material";
    }
    return L"?";
}

void setupListView(HWND hList) {
    ListView_SetExtendedListViewStyle(hList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;

    col.pszText = const_cast<LPWSTR>(L"Problem Type");
    col.cx = 130;
    ListView_InsertColumn(hList, 0, &col);

    col.pszText = const_cast<LPWSTR>(L"Object / Name");
    col.cx = 240;
    ListView_InsertColumn(hList, 1, &col);
}

void populateListView(HWND hList, const ScanResult& result) {
    ListView_DeleteAllItems(hList);

    for (size_t i = 0; i < result.problems.size(); ++i) {
        const Problem& p = result.problems[i];

        LVITEMW it = {};
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = (int)i;
        it.iSubItem = 0;
        it.pszText = const_cast<LPWSTR>(problemTypeName(p.type));
        it.lParam = (LPARAM)(int)p.type;  // store ProblemType for selection-fix
        int row = ListView_InsertItem(hList, &it);

        // Sub-item 1: display name
        ListView_SetItemText(hList, row, 1,
            const_cast<LPWSTR>(p.displayName.c_str()));
    }
}

void updateStatusLabel(HWND hDlg, const ScanResult& result) {
    HWND hLbl = GetDlgItem(hDlg, IDC_LBL_PROBLEM_STATUS);
    if (!hLbl) return;

    if (result.empty()) {
        SetWindowTextW(hLbl, L"No problems found");
        return;
    }

    wchar_t buf[256];
    int dup = result.countByType(ProblemType::DuplicateName);
    int em  = result.countByType(ProblemType::EmptyMesh);
    int ep  = result.countByType(ProblemType::EditablePoly);
    int es  = result.countByType(ProblemType::EditMeshAboveSkin);
    int ic  = result.countByType(ProblemType::InvalidController);
    int um  = result.countByType(ProblemType::UnsupportedMaterial);

    swprintf_s(buf, 256,
        L"%d total: %d names, %d meshes, %d controllers, %d materials",
        result.count(), dup, (em + ep + es), ic, um);
    SetWindowTextW(hLbl, buf);
}

void rescanAndRefresh(HWND hDlg, DialogState* ds) {
    if (!ds) return;
    ds->result = scanScene();
    populateListView(GetDlgItem(hDlg, IDC_LV_PROBLEMS), ds->result);
    updateStatusLabel(hDlg, ds->result);

    // Disable "Fix Selected" if listview is empty
    EnableWindow(GetDlgItem(hDlg, IDC_BTN_FIX_SELECTED),
                 !ds->result.empty());
    EnableWindow(GetDlgItem(hDlg, IDC_BTN_FIX_ALL),
                 !ds->result.empty());
}

// Apply the appropriate fix for each ProblemType present in |types|.
// Builds a filtered ScanResult per category so fixers only see what's
// relevant.
void runFixForSelectedTypes(HWND hDlg, DialogState* ds,
                             const std::unordered_set<ProblemType>& types) {
    if (!ds || types.empty()) return;

    ScanResult subset;
    for (auto& p : ds->result.problems) {
        if (types.count(p.type)) subset.problems.push_back(p);
    }
    if (subset.empty()) return;

    if (types.count(ProblemType::DuplicateName))
        fixDuplicateNames(subset);
    if (types.count(ProblemType::EmptyMesh) ||
        types.count(ProblemType::EditablePoly) ||
        types.count(ProblemType::EditMeshAboveSkin))
        fixMeshProblems(subset);
    if (types.count(ProblemType::InvalidController))
        fixBoneControllers(subset);
    if (types.count(ProblemType::UnsupportedMaterial))
        fixUnsupportedMaterials(subset);

    rescanAndRefresh(hDlg, ds);
}

INT_PTR CALLBACK ProblemDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    DialogState* ds = reinterpret_cast<DialogState*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));

    switch (msg) {
    case WM_INITDIALOG: {
        ds = reinterpret_cast<DialogState*>(lParam);
        SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ds));
        setupListView(GetDlgItem(hDlg, IDC_LV_PROBLEMS));
        rescanAndRefresh(hDlg, ds);
        CenterWindow(hDlg, GetParent(hDlg));
        return TRUE;
    }

    case WM_COMMAND: {
        if (!ds) break;
        int id = LOWORD(wParam);

        switch (id) {
        case IDC_BTN_FIX_SELECTED: {
            HWND hList = GetDlgItem(hDlg, IDC_LV_PROBLEMS);
            std::unordered_set<ProblemType> types;
            int idx = -1;
            while ((idx = ListView_GetNextItem(hList, idx, LVNI_SELECTED)) != -1) {
                LVITEMW lv = {};
                lv.mask = LVIF_PARAM;
                lv.iItem = idx;
                ListView_GetItem(hList, &lv);
                types.insert(static_cast<ProblemType>(lv.lParam));
            }
            runFixForSelectedTypes(hDlg, ds, types);
            return TRUE;
        }

        case IDC_BTN_FIX_ALL: {
            fixAll(ds->result);
            rescanAndRefresh(hDlg, ds);
            return TRUE;
        }

        case IDC_BTN_RESCAN:
            rescanAndRefresh(hDlg, ds);
            return TRUE;

        case IDOK:
        case IDCANCEL:
            EndDialog(hDlg, ds ? ds->result.count() : 0);
            return TRUE;
        }
        break;
    }

    case WM_NOTIFY: {
        LPNMHDR hdr = reinterpret_cast<LPNMHDR>(lParam);
        if (hdr && hdr->idFrom == IDC_LV_PROBLEMS &&
            hdr->code == NM_DBLCLK)
        {
            // Double-click: select the offending node in the viewport
            int idx = ListView_GetNextItem(GetDlgItem(hDlg, IDC_LV_PROBLEMS),
                                            -1, LVNI_SELECTED);
            if (idx >= 0 && ds && idx < (int)ds->result.problems.size()) {
                INode* n = ds->result.problems[idx].node;
                if (n) {
                    Interface* gi = GetCOREInterface();
                    if (gi) {
                        gi->ClearNodeSelection();
                        gi->SelectNode(n);
                    }
                }
            }
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        EndDialog(hDlg, ds ? ds->result.count() : 0);
        return TRUE;
    }
    return FALSE;
}

} // anonymous namespace

int showProblemDetailsDialog(HINSTANCE hInstance, HWND hWndParent) {
    DialogState ds{};
    ds.hInstance = hInstance;
    ds.result    = scanScene();

    INT_PTR rc = DialogBoxParamW(
        hInstance, MAKEINTRESOURCEW(IDD_PROBLEM_DETAILS),
        hWndParent, ProblemDialogProc,
        reinterpret_cast<LPARAM>(&ds));

    return (int)rc;
}

} // namespace scene_monitor
