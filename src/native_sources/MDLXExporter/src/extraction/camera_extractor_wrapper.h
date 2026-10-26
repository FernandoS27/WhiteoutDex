// MDLXExporter — Camera extractor
//
// Iterates over scene nodes, detects native Max Camera objects
// (SuperClassID == CAMERA_CLASS_ID), and populates ir::IRModel::cameras
// using the existing MaxCore CameraExtractor.
//
#pragma once

#include <scene/node_classifier.h>
#include <core/intermediate_types.h>
#include <util/error_reporter.h>
#include <vector>

namespace mdx_extract {

// Needs model.sequences: the camera and target tracks are sampled per sequence.
void extractCameras(const std::vector<core::SceneNode>& nodes,
                    ir::IRModel& model,
                    core::ExportErrorReporter& reporter);

} // namespace mdx_extract
