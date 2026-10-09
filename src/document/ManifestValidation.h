#pragma once

// The semantic validation the original runs on every save and load (ProjectStore.validate plus
// validateGuides), ported guard for guard: version gates, text and adjustment ownership, mask
// rules, hierarchy and clipping structure, guide limits, then the id/name/transform pass.

#include "document/Manifest.h"

namespace compositor {

/// Throws ProjectError (Invalid/Version/TooLarge) exactly where the original throws.
void validateManifest(const ProjectManifest& manifest);

}  // namespace compositor
