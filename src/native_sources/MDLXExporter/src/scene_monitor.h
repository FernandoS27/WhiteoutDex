// MDLXExporter — Scene Monitor
//
// Detects and fixes common scene problems before export:
//   • Duplicate object names
//   • Mesh problems (Editable Poly without converter, empty meshes,
//                    Edit_Mesh modifier above Skin)
//   • Bone controllers using non-baseline types (only Linear/TCB/Bezier/XYZ
//     allowed; Biped/CAT/IK/Wdx_-plugins exempt — they're baked at export)
//   • Material problems (Physical, Standard, OpenPBR/PBR — all incompatible
//     with the MDX format; only Wdx_Wc3Material and NeoDex Warcraft3 are supported)
//   • Meshes with a Multi/Sub-Object material: a geoset has one material, so
//     such a mesh is split into one object per sub-material
//
// Modeled on the NeoDex SceneMonitor, but rewritten in C++ and restricted
// to the WhiteoutDex Wdx_* and NeoDex Warcraft3 plugin families.
#pragma once

#include <max.h>
#include <string>
#include <vector>
#include <windows.h>

namespace scene_monitor {

// ── Problem categories ────────────────────────────────────────────────

enum class ProblemType {
    DuplicateName,
    EmptyMesh,
    EditablePoly,        // Editable_Poly base, no Edit_Mesh/Turn_to_Mesh modifier
    EditMeshAboveSkin,   // Edit_Mesh modifier sitting above a Skin modifier
    InvalidController,   // Non-baseline P/R/S controller on a bone/helper
    UnsupportedMaterial, // Physical / Standard / OpenPBR / PBR
    MultiMaterialMesh,   // Mesh carrying a Multi/Sub-Object material
    DuplicateMaterial,   // Node material identical to another one (values,
                         // maps, animation) - WhiteoutDexMaterialMerge.ms
};

// One detected problem entry.
// |node| points at the offending INode (nullptr for material problems).
// |mtl|  points at the offending Mtl  (nullptr for non-material problems).
// |displayName| is the user-facing string for the listview.
struct Problem {
    ProblemType type;
    INode*      node       = nullptr;
    Mtl*        mtl        = nullptr;
    std::wstring displayName;
};

// ── Result of a scan / fix pass ───────────────────────────────────────

struct ScanResult {
    std::vector<Problem> problems;

    int countByType(ProblemType t) const {
        int n = 0;
        for (auto& p : problems) if (p.type == t) ++n;
        return n;
    }
    bool empty() const { return problems.empty(); }
    int  count() const { return static_cast<int>(problems.size()); }
};

// ── Detection ─────────────────────────────────────────────────────────

// Walk the entire scene and produce a fresh ScanResult.
// Safe to call from the UI thread; does NOT modify the scene.
ScanResult scanScene();

// ── Fixing ────────────────────────────────────────────────────────────

// Each fix runs inside its own undo group. Returns the number of problems
// successfully fixed for that category. After a fix call, the scan should
// be re-run to refresh the result.

int fixDuplicateNames     (const ScanResult& result);
int fixMeshProblems       (const ScanResult& result);
int fixBoneControllers    (const ScanResult& result);
int fixUnsupportedMaterials(const ScanResult& result);
// Splits every flagged mesh into one object per sub-material of its
// Multi/Sub-Object material. Skinned meshes are left alone (see the .cpp).
int fixMultiMaterialMeshes(const ScanResult& result);
// Gives every node using a duplicate material the first of its identical
// group (WdxMergeDuplicateMaterials, WhiteoutDexMaterialMerge.ms).
int fixDuplicateMaterials(const ScanResult& result);

// Convenience: run all fixers, returns total count fixed. The Multi/Sub split
// runs first and the scene is re-scanned after it, so the new objects' mesh
// and material problems are fixed in the same pass.
int fixAll(const ScanResult& result);

// ── Standalone problem-details dialog ─────────────────────────────────

// Shows the modal Problem Details dialog (listview + selective fix buttons).
// Re-scans after each fix and updates the listview live.
// |hInstance| is the DLL module handle.
// |hWndParent| is the export dialog (parent for modality).
// Returns the number of problems remaining after the user closes the dialog.
int showProblemDetailsDialog(HINSTANCE hInstance, HWND hWndParent);

// ── Helpers exposed for tests / external integration ──────────────────

// Returns true if the controllers on |node| are all baseline types
// (Linear/TCB/Bezier/Position_XYZ etc.). Returns true unconditionally for
// Biped/CAT/IK/Wdx_-plugin nodes — those are exempt because they're
// resampled to per-frame TM at export time.
bool hasValidPRS(INode* node);

// Returns true if |node| is recognized as a WhiteoutDex Wdx_* plugin
// (attach point, light, event, collision shape, particle1, particle2,
// ribbon, popcorn, facefx). Vertex-color modifier is NOT a node so it's
// not checked here.
bool isWdxPluginNode(INode* node);

} // namespace scene_monitor
