// MDLXExporter — Sequence manager: read sequences from scene
#pragma once

#include <core/intermediate_types.h>
#include <max.h>
#include <vector>

class MdxSequenceManager {
public:
    // Extract sequences from WhiteoutDexSequenceData custom attribute on rootNode
    std::vector<ir::Sequence> extractSequences(Interface* gi);
};
