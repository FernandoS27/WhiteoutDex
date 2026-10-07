// MDLXExporter — Sequence manager: read sequences from scene
#pragma once

#include <core/intermediate_types.h>
#include <max.h>
#include <vector>

class MdxSequenceManager {
public:
    // Extract sequences from WhiteoutDexSequenceData custom attribute on rootNode
    std::vector<ir::Sequence> extractSequences(Interface* gi);

    // Fill every sequence's extent whose stored box does not hold the
    // exported meshes with the bounds of those meshes over the sequence.
    void sampleExtents(std::vector<ir::Sequence>& sequences, const std::vector<INode*>& meshNodes);
};
