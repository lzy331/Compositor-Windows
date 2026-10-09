#pragma once

// The port of ImageExporter.render: flattens a whole project into one document-grid raster.
// Row 0 is the document's top row, the orientation PNG encoders and the canvas both expect.

#include "io/ProjectStore.h"
#include "render/Raster.h"

namespace compositor::render {

class DocumentRenderer {
public:
    /// Flattens `snapshot` the way the original exports it. Throws ProjectError::missingImage
    /// (an image or mask the manifest references is absent), ProjectError::invalid
    /// (LiveMaskGraph's checks), RenderError::tooLarge or RenderError::render.
    static Raster render(const ProjectSnapshot& snapshot);
};

}  // namespace compositor::render
