#pragma once
// ============================================================================
// BlsProgram / BlsProgramCatalog — VS+PS pair for a single EGxShaderID.
//
// Mirrors CGxDevice::m_materialShaders[id] from Previewd (entries populated in
// CGxDevice::ILoadShaders at 0x1403f8eb0). Each program names the BLS file to
// load for the vertex and pixel stage; Acquire() pulls them through the shared
// BlsShaderCache so a given .bls file is loaded at most once.
// ============================================================================

#include "bls_permuter.h"
#include "bls_shader_cache.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace WhiteoutDex::bls {

struct BlsProgram {
    GxShaderID   id;
    BlsShader*   vs = nullptr;
    BlsShader*   ps = nullptr;

    bool IsValid() const { return vs != nullptr && ps != nullptr; }
};

struct BlsProgramDef {
    GxShaderID  id;
    const char* vsName;
    const char* psName;
};

class BlsProgramCatalog {
public:
    explicit BlsProgramCatalog(BlsShaderCache* cache);
    ~BlsProgramCatalog();

    // Loads a single program. Returns a borrowed pointer owned by the catalog.
    // Returns nullptr if either .bls file fails to load.
    const BlsProgram* Load(const BlsProgramDef& def);

    const BlsProgram* Get(GxShaderID id) const;
    void              Clear();

private:
    BlsShaderCache*                                          cache_ = nullptr;
    std::unordered_map<uint8_t, std::unique_ptr<BlsProgram>> programs_;
};

} // namespace WhiteoutDex::bls
