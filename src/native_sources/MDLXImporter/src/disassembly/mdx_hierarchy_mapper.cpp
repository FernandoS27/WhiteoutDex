// MDLXImporter — MdxHierarchyMapper implementation (DEBUG VERSION)
#include "mdx_hierarchy_mapper.h"
#include <fstream>
#include <string>
#include <windows.h>

namespace wdx = whiteout::mdx;

// ── Shared debug log ──────────────────────────────────────
static std::ofstream& dbgLog() {
    static std::ofstream s_log;
    if (!s_log.is_open()) {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring p(tmp);
        p += L"mdlx_import_debug.log";
        s_log.open(p, std::ios::trunc);
        s_log << "=== MDLX Importer Debug Log ===\n\n";
    }
    return s_log;
}
#define DLOG dbgLog()

namespace mdx_disasm {

void MdxHierarchyMapper::build(const wdx::Model& mdx) {
    nodes_.clear();
    objectIdToIrIndex_.clear();

    DLOG << "==== HierarchyMapper::build ====\n";
    DLOG << "Bones=" << mdx.bones.size()
         << " Helpers=" << mdx.helpers.size()
         << " Pivots=" << mdx.pivotPoints.size() << "\n";

    // ── PASS 1: Register ALL nodes first ──
    struct Pending { uint32_t objId, parId; std::string name; };
    std::vector<Pending> pending;

    auto reg = [&](uint32_t objId, uint32_t parId, bool isHelper, const char* name) {
        MappedNode mn;
        mn.irIndex = static_cast<int32_t>(nodes_.size());
        mn.mdxObjectId = objId;
        mn.isHelper = isHelper;
        mn.irParent = -1; // deferred
        objectIdToIrIndex_[objId] = mn.irIndex;
        nodes_.push_back(mn);
        pending.push_back({objId, parId, name ? name : ""});
    };

    for (const auto& b : mdx.bones) {
        bool h = (b.node.type == wdx::Node::NodeType::Helper);
        reg(b.node.objectId, b.node.parentId, h, b.node.name.c_str());
    }
    for (const auto& h : mdx.helpers)
        reg(h.node.objectId, h.node.parentId, true, h.node.name.c_str());
    for (const auto& l : mdx.lights)
        reg(l.node.objectId, l.node.parentId, false, l.node.name.c_str());
    for (const auto& a : mdx.attachments)
        reg(a.node.objectId, a.node.parentId, false, a.node.name.c_str());
    for (const auto& p : mdx.particleEmitters)
        reg(p.node.objectId, p.node.parentId, false, p.node.name.c_str());
    for (const auto& p : mdx.particleEmitters2)
        reg(p.node.objectId, p.node.parentId, false, p.node.name.c_str());
    for (const auto& r : mdx.ribbonEmitters)
        reg(r.node.objectId, r.node.parentId, false, r.node.name.c_str());
    for (const auto& e : mdx.eventObjects)
        reg(e.node.objectId, e.node.parentId, false, e.node.name.c_str());
    for (const auto& c : mdx.collisionShapes)
        reg(c.node.objectId, c.node.parentId, false, c.node.name.c_str());
    for (const auto& c : mdx.cornEmitters)
        reg(c.node.objectId, c.node.parentId, false, c.node.name.c_str());

    // ── PASS 2: Resolve parents ──
    for (size_t i = 0; i < pending.size(); ++i) {
        auto& p = pending[i];
        if (p.parId != 0xFFFFFFFF) {
            auto it = objectIdToIrIndex_.find(p.parId);
            nodes_[i].irParent = (it != objectIdToIrIndex_.end()) ? it->second : -1;
        }
        DLOG << "  node[" << i << "] objId=" << p.objId
             << " parId=" << (p.parId == 0xFFFFFFFF ? -1 : (int)p.parId)
             << " -> irParent=" << nodes_[i].irParent
             << " '" << p.name << "'\n";
    }

    // ── Pivot points dump ──
    DLOG << "\nPivot points (MDX coords):\n";
    for (size_t i = 0; i < mdx.pivotPoints.size() && i < 60; ++i) {
        const auto& pp = mdx.pivotPoints[i];
        DLOG << "  pivot[" << i << "] = (" << pp.x << ", " << pp.y << ", " << pp.z << ")\n";
    }

    DLOG << "==== end HierarchyMapper ====\n\n";
    DLOG.flush();
}

int32_t MdxHierarchyMapper::irIndexForObjectId(uint32_t objectId) const {
    auto it = objectIdToIrIndex_.find(objectId);
    return (it != objectIdToIrIndex_.end()) ? it->second : -1;
}

int32_t MdxHierarchyMapper::resolveParent(uint32_t mdxParentId) const {
    if (mdxParentId == 0xFFFFFFFF) return -1;
    return irIndexForObjectId(mdxParentId);
}

} // namespace mdx_disasm
