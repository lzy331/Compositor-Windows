#pragma once

// The Photoshop blend modes, in the original's order and with its exact spelling, because the
// project manifest stores them as strings (LayerAppearance.swift: LayerBlendMode).

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace compositor {

enum class BlendMode {
    Normal,
    Darken,
    Multiply,
    ColorBurn,
    LinearBurn,
    Lighten,
    Screen,
    ColorDodge,
    LinearDodge,
    Overlay,
    SoftLight,
    HardLight,
    VividLight,
    LinearLight,
    PinLight,
    HardMix,
    Difference,
    Exclusion,
    Subtract,
    Divide,
    Hue,
    Saturation,
    Color,
    Luminosity,
};

/// Declaration order, which is also `LayerBlendMode.allCases` — the order Shift-+ steps through.
inline constexpr std::array<BlendMode, 24> allBlendModes = {
    BlendMode::Normal,     BlendMode::Darken,      BlendMode::Multiply,   BlendMode::ColorBurn,
    BlendMode::LinearBurn, BlendMode::Lighten,     BlendMode::Screen,     BlendMode::ColorDodge,
    BlendMode::LinearDodge, BlendMode::Overlay,    BlendMode::SoftLight,  BlendMode::HardLight,
    BlendMode::VividLight, BlendMode::LinearLight, BlendMode::PinLight,   BlendMode::HardMix,
    BlendMode::Difference, BlendMode::Exclusion,   BlendMode::Subtract,   BlendMode::Divide,
    BlendMode::Hue,        BlendMode::Saturation,  BlendMode::Color,      BlendMode::Luminosity,
};

/// Photoshop's grouping; the menu draws a line between each group (LayerBlendMode.groups).
inline const std::vector<std::vector<BlendMode>>& blendModeGroups() {
    static const std::vector<std::vector<BlendMode>> groups = {
        {BlendMode::Normal},
        {BlendMode::Darken, BlendMode::Multiply, BlendMode::ColorBurn, BlendMode::LinearBurn},
        {BlendMode::Lighten, BlendMode::Screen, BlendMode::ColorDodge, BlendMode::LinearDodge},
        {BlendMode::Overlay, BlendMode::SoftLight, BlendMode::HardLight, BlendMode::VividLight,
         BlendMode::LinearLight, BlendMode::PinLight, BlendMode::HardMix},
        {BlendMode::Difference, BlendMode::Exclusion, BlendMode::Subtract, BlendMode::Divide},
        {BlendMode::Hue, BlendMode::Saturation, BlendMode::Color, BlendMode::Luminosity},
    };
    return groups;
}

std::string blendModeName(BlendMode mode);
std::optional<BlendMode> blendModeFromName(const std::string& name);

}  // namespace compositor
