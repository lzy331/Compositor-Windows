#include "core/BlendMode.h"

namespace compositor {

using P = std::pair<BlendMode, const char*>;

// Exact spelling from LayerBlendMode's raw values — these strings go in the manifest.
static constexpr P kNames[] = {
    {BlendMode::Normal, "Normal"},
    {BlendMode::Darken, "Darken"},
    {BlendMode::Multiply, "Multiply"},
    {BlendMode::ColorBurn, "Color Burn"},
    {BlendMode::LinearBurn, "Linear Burn"},
    {BlendMode::Lighten, "Lighten"},
    {BlendMode::Screen, "Screen"},
    {BlendMode::ColorDodge, "Color Dodge"},
    {BlendMode::LinearDodge, "Linear Dodge (Add)"},
    {BlendMode::Overlay, "Overlay"},
    {BlendMode::SoftLight, "Soft Light"},
    {BlendMode::HardLight, "Hard Light"},
    {BlendMode::VividLight, "Vivid Light"},
    {BlendMode::LinearLight, "Linear Light"},
    {BlendMode::PinLight, "Pin Light"},
    {BlendMode::HardMix, "Hard Mix"},
    {BlendMode::Difference, "Difference"},
    {BlendMode::Exclusion, "Exclusion"},
    {BlendMode::Subtract, "Subtract"},
    {BlendMode::Divide, "Divide"},
    {BlendMode::Hue, "Hue"},
    {BlendMode::Saturation, "Saturation"},
    {BlendMode::Color, "Color"},
    {BlendMode::Luminosity, "Luminosity"},
};

std::string blendModeName(BlendMode mode) {
    for (const auto& [m, name] : kNames) {
        if (m == mode) return name;
    }
    return "Normal";
}

std::optional<BlendMode> blendModeFromName(const std::string& name) {
    for (const auto& [m, n] : kNames) {
        if (name == n) return m;
    }
    return std::nullopt;
}

}  // namespace compositor
