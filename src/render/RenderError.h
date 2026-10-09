#pragma once

#include "core/Types.h"

#include <stdexcept>
#include <string>

namespace compositor::render {

/// An export failure, worded exactly as the Swift ExportError so the two apps say the same thing.
class RenderError : public std::runtime_error {
public:
    enum class Kind { TooLarge, Render, Encode };

    RenderError(Kind kind, std::string message) : std::runtime_error(message), kind_(kind) {}

    Kind kind() const { return kind_; }

    static RenderError tooLarge() {
        return RenderError(Kind::TooLarge,
                           "Image export supports canvases up to " +
                               std::to_string(DocumentLimits::maxSurfacePixels / 1'000'000) +
                               " megapixels and 30,000 pixels per side.");
    }
    static RenderError render() {
        return RenderError(Kind::Render, "The canvas could not be rendered. Try a smaller canvas.");
    }
    static RenderError encode() {
        return RenderError(Kind::Encode, "The image could not be encoded.");
    }

private:
    Kind kind_;
};

}  // namespace compositor::render
