// ============================================================================
// bone_main_debug.h
//
// Single-purpose debug log that collects every operation affecting the
// 'Bone_Main' node across anim_dispatcher, bone_extractor and model_builder.
// Writes to %TEMP%\mdlx_bone_main.log in APPEND mode — the file must be
// truncated at export start (done in mdx_exporter_plugin.cpp).
//
// Usage:
//   BMLOG << "[anim] bind pose x=" << x << "\n";
//   BMFLUSH;
//
// Typical call sites should gate with: if (irNode.name == "Bone_Main")
// so only the target bone is logged, keeping the file small.
// ============================================================================
#pragma once

#include <fstream>
#include <string>
#include <windows.h>

inline std::ofstream& boneMainLog() {
    static std::ofstream log;
    if (!log.is_open()) {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "mdlx_bone_main.log";
        // APPEND mode — the first writer (mdx_exporter_plugin) truncates it
        log.open(path, std::ios::app);
    }
    return log;
}

#define BMLOG   boneMainLog()
#define BMFLUSH boneMainLog().flush()
