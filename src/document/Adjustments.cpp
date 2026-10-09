#include "document/Adjustments.h"

namespace compositor {

std::string colorRangeName(ColorRange range) {
    switch (range) {
        case ColorRange::Master: return "Master";
        case ColorRange::Reds: return "Reds";
        case ColorRange::Yellows: return "Yellows";
        case ColorRange::Greens: return "Greens";
        case ColorRange::Cyans: return "Cyans";
        case ColorRange::Blues: return "Blues";
        case ColorRange::Magentas: return "Magentas";
    }
    return "Master";
}

std::optional<ColorRange> colorRangeFromName(const std::string& name) {
    for (ColorRange range : allColorRanges())
        if (colorRangeName(range) == name) return range;
    return std::nullopt;
}

HueBand defaultBand(ColorRange range) {
    switch (range) {
        case ColorRange::Master: return HueBand{0, 0, 360, 360};
        case ColorRange::Reds: return HueBand{315, 345, 15, 45};
        case ColorRange::Yellows: return HueBand{15, 45, 75, 105};
        case ColorRange::Greens: return HueBand{75, 105, 135, 165};
        case ColorRange::Cyans: return HueBand{135, 165, 195, 225};
        case ColorRange::Blues: return HueBand{195, 225, 255, 285};
        case ColorRange::Magentas: return HueBand{255, 285, 315, 345};
    }
    return HueBand{0, 0, 360, 360};
}

std::string levelsChannelName(LevelsChannel channel) {
    switch (channel) {
        case LevelsChannel::Rgb: return "RGB";
        case LevelsChannel::Red: return "Red";
        case LevelsChannel::Green: return "Green";
        case LevelsChannel::Blue: return "Blue";
    }
    return "RGB";
}

std::optional<LevelsChannel> levelsChannelFromName(const std::string& name) {
    for (LevelsChannel channel : allLevelsChannels())
        if (levelsChannelName(channel) == name) return channel;
    return std::nullopt;
}

std::string adjustmentKindName(AdjustmentKind kind) {
    switch (kind) {
        case AdjustmentKind::Hsv: return "Hue/Saturation";
        case AdjustmentKind::Levels: return "Levels";
        case AdjustmentKind::Curves: return "Curves";
        case AdjustmentKind::Exposure: return "Exposure";
        case AdjustmentKind::GradientMap: return "Gradient Map";
        case AdjustmentKind::Grain: return "Grain";
        case AdjustmentKind::AddNoise: return "Add Noise";
        case AdjustmentKind::GaussianBlur: return "Gaussian Blur";
        case AdjustmentKind::MotionBlur: return "Motion Blur";
        case AdjustmentKind::Invert: return "Invert";
        case AdjustmentKind::BlackWhite: return "Black & White";
        case AdjustmentKind::ColorBalance: return "Color Balance";
    }
    return "Hue/Saturation";
}

std::optional<AdjustmentKind> adjustmentKindFromName(const std::string& name) {
    for (AdjustmentKind kind : allAdjustmentKinds())
        if (adjustmentKindName(kind) == name) return kind;
    return std::nullopt;
}

}  // namespace compositor
