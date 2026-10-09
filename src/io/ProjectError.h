#pragma once

#include "core/Types.h"
#include "document/Manifest.h"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace compositor {

/// A .comp problem, worded exactly as the Swift ProjectError so the two apps say the same thing.
class ProjectError : public std::runtime_error {
public:
    enum class Kind { Invalid, Version, MissingImage, TooLarge, Encode };

    /// `version` carries the format version a Version error is about.
    ProjectError(Kind kind, std::string message, int64_t version = 0)
        : std::runtime_error(message), kind_(kind), version_(version) {}

    Kind kind() const { return kind_; }
    int64_t version() const { return version_; }

    static ProjectError invalid() {
        return ProjectError(Kind::Invalid,
                            "This is not a valid Compositor project, or its metadata is damaged.");
    }
    static ProjectError version(int64_t version) {
        // "This project uses format version X. This app supports versions 1–11." (en dash, U+2013)
        return ProjectError(Kind::Version,
                            "This project uses format version " + std::to_string(version) +
                                ". This app supports versions " + std::to_string(ProjectManifest::supportedLow) +
                                "\xE2\x80\x93" + std::to_string(ProjectManifest::supportedHigh) + ".",
                            version);
    }
    static ProjectError missingImage() {
        return ProjectError(Kind::MissingImage,
                            "An image inside the project is missing or damaged. The current document has not "
                            "been replaced.");
    }
    static ProjectError tooLarge() {
        return ProjectError(Kind::TooLarge,
                            "This project exceeds the supported canvas, layer, file-size, or " +
                                std::to_string(DocumentLimits::documentBudgetMegapixels()) +
                                "-megapixel document limit.");
    }
    static ProjectError encode() {
        return ProjectError(Kind::Encode,
                            "An image could not be saved. The previous project has not been replaced.");
    }

private:
    Kind kind_;
    int64_t version_;
};

}  // namespace compositor
