#pragma once

// Reads and writes the .comp package: manifest.json plus images/<uuid>.png (and
// <uuid>.mask.png), with the same validation order and the same ProjectError kinds the
// original raises. Writes stage a sibling folder and swap it in only once complete, so a
// failed save never replaces the previous project.

#include "core/Types.h"
#include "document/Manifest.h"
#include "io/ImageAssets.h"

#include <QByteArray>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace compositor {

/// Everything a document needs to persist: the manifest and the pixels each layer references.
struct ProjectSnapshot {
    ProjectManifest manifest;
    std::map<Uuid, ImportedImage> images;
    std::map<Uuid, ImportedImage> masks;
};

/// The Space-bar preview JPEG that rides along in QuickLook/Preview.jpg. Loading ignores it.
struct QuickLookImages {
    QByteArray preview;
};

struct ProjectStore {
    /// Saves `snapshot` as a package at `path`, replacing whatever is there. Throws
    /// ProjectError: invalid (a mask image is not 8-bit grayscale), missingImage (the
    /// manifest references pixels that are not in the snapshot), tooLarge (canvas, an
    /// image, or the metadata exceeds its budget) or encode (an image or the package
    /// itself could not be written; the previous project is left in place).
    static void save(const ProjectSnapshot& snapshot, const std::string& path,
                     const std::optional<QuickLookImages>& quickLook = std::nullopt);

    /// Loads a package. Throws ProjectError: invalid (not a package, damaged metadata, or a
    /// mask image that is not valid grayscale), version (unsupported format version),
    /// missingImage (a referenced image is absent or not a single-frame PNG), or tooLarge
    /// (metadata or image sizes or the pixel budget).
    static ProjectSnapshot load(const std::string& path);
};

}  // namespace compositor
