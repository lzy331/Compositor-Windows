#pragma once

// The adjustment-layer settings, ported from the original's Document/*.swift (HueSaturation,
// Levels, Curves, ImageAdjustments, LayerAdjustment). Field names, defaults and validation
// ranges are kept identical because the manifest stores these structs by name.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace compositor {

inline bool finite(double value) { return std::isfinite(value); }
inline bool inRange(double value, double low, double high) { return value >= low && value <= high; }
inline double clampDouble(double value, double low, double high, double fallback) {
    // Matches the original's clamp: non-finite input falls back, everything else clamps.
    return finite(value) ? std::min(high, std::max(low, value)) : fallback;
}

// MARK: - Hue/Saturation (HueSaturation.swift)

/// The six color ranges plus Master, as in Photoshop's Cmd+U.
enum class ColorRange { Master, Reds, Yellows, Greens, Cyans, Blues, Magentas };

inline const std::vector<ColorRange>& allColorRanges() {
    static const std::vector<ColorRange> ranges = {
        ColorRange::Master, ColorRange::Reds, ColorRange::Yellows, ColorRange::Greens,
        ColorRange::Cyans, ColorRange::Blues, ColorRange::Magentas,
    };
    return ranges;
}

/// The six adjustable ranges, without Master (ColorRange.colorRanges).
inline const std::vector<ColorRange>& colorRanges() {
    static const std::vector<ColorRange> ranges = {
        ColorRange::Reds, ColorRange::Yellows, ColorRange::Greens,
        ColorRange::Cyans, ColorRange::Blues, ColorRange::Magentas,
    };
    return ranges;
}

std::string colorRangeName(ColorRange range);
std::optional<ColorRange> colorRangeFromName(const std::string& name);

/// A hue band in degrees, wrapping at 360: full strength between `rangeStart` and
/// `rangeEnd`, fading to nothing at `falloffStart` and `falloffEnd`.
struct HueBand {
    double falloffStart = 0;
    double rangeStart = 0;
    double rangeEnd = 0;
    double falloffEnd = 0;

    /// Degrees from `from` forward to `to`, always 0…360.
    static double forward(double from, double to) {
        double delta = std::fmod(to - from, 360.0);
        return delta < 0 ? delta + 360 : delta;
    }

    /// How strongly this band claims a hue: 1 inside the range, ramping linearly through
    /// each falloff shoulder, 0 outside. Wraparound is handled by measuring forward.
    double weight(double hue) const {
        const double span = forward(falloffStart, falloffEnd);
        if (span <= 0) return 1;  // Master covers everything.
        const double position = forward(falloffStart, hue);
        if (position > span) return 0;
        const double rampIn = forward(falloffStart, rangeStart);
        const double plateauEnd = forward(falloffStart, rangeEnd);
        if (position < rampIn) return rampIn > 0 ? position / rampIn : 1;
        if (position <= plateauEnd) return 1;
        const double rampOut = span - plateauEnd;
        return rampOut > 0 ? (span - position) / rampOut : 1;
    }

    std::vector<double> handles() const { return {falloffStart, rangeStart, rangeEnd, falloffEnd}; }

    bool operator==(const HueBand& other) const {
        return falloffStart == other.falloffStart && rangeStart == other.rangeStart
            && rangeEnd == other.rangeEnd && falloffEnd == other.falloffEnd;
    }
};

/// Photoshop's starting hue band per range (ColorRange.defaultBand).
HueBand defaultBand(ColorRange range);

/// A targeted-adjustment's stored strengths for one range.
struct RangeAdjustment {
    double hue = 0;
    double saturation = 0;
    double lightness = 0;
    bool operator==(const RangeAdjustment& other) const {
        return hue == other.hue && saturation == other.saturation && lightness == other.lightness;
    }
    bool isZero() const { return *this == RangeAdjustment(); }
};

/// Hue is −180…180, Saturation −100…100, Lightness −100…100. Each color range keeps its own
/// values; Master applies everywhere.
struct HueSaturationSettings {
    ColorRange range = ColorRange::Master;
    bool colorize = false;
    /// Applies the selected range to everything *outside* its band instead.
    bool invertRange = false;
    std::map<ColorRange, RangeAdjustment> adjustments;
    std::map<ColorRange, HueBand> bands;

    HueSaturationSettings() {
        // bands are seeded (Swift: Dictionary(uniqueKeysWithValues:)); adjustments start empty (Swift: [:]).
        for (ColorRange which : allColorRanges()) bands[which] = defaultBand(which);
    }

    /// The sliders read and write the selected range.
    RangeAdjustment adjustmentFor(ColorRange which) const {
        auto it = adjustments.find(which);
        return it == adjustments.end() ? RangeAdjustment() : it->second;
    }
    void setAdjustment(ColorRange which, const RangeAdjustment& value) { adjustments[which] = value; }

    HueBand band() const {
        auto it = bands.find(range);
        return it == bands.end() ? defaultBand(range) : it->second;
    }
    void setBand(const HueBand& value) { bands[range] = value; }

    double hue() const { return adjustmentFor(range).hue; }
    double saturation() const { return adjustmentFor(range).saturation; }
    double lightness() const { return adjustmentFor(range).lightness; }

    /// Photoshop's starting point when Colorize is switched on.
    static HueSaturationSettings colorizeStart() {
        HueSaturationSettings settings;
        settings.colorize = true;
        settings.adjustments[ColorRange::Master] = RangeAdjustment{0, 25, 0};
        return settings;
    }

    bool isIdentity() const {
        if (colorize) return false;
        for (const auto& [_, adjustment] : adjustments)
            if (!adjustment.isZero()) return false;
        return true;
    }

    /// How much a range applies to one hue: Master everywhere, others through their band.
    double weightOf(ColorRange which, double hue) const {
        if (which == ColorRange::Master) return 1;
        auto it = bands.find(which);
        const double bandWeight = (it == bands.end() ? defaultBand(which) : it->second).weight(hue);
        return invertRange && which == range ? 1 - bandWeight : bandWeight;
    }

    bool isValid() const {
        for (const auto& [_, adjustment] : adjustments) {
            if (!finite(adjustment.hue) || std::abs(adjustment.hue) > 360) return false;
            if (!finite(adjustment.saturation) || std::abs(adjustment.saturation) > 100) return false;
            if (!finite(adjustment.lightness) || std::abs(adjustment.lightness) > 100) return false;
        }
        for (const auto& [_, band] : bands)
            for (double handle : band.handles())
                if (!finite(handle)) return false;
        return true;
    }

    bool operator==(const HueSaturationSettings& other) const {
        return range == other.range && colorize == other.colorize && invertRange == other.invertRange
            && adjustments == other.adjustments && bands == other.bands;
    }
};

// MARK: - Levels (Levels.swift)

enum class LevelsChannel { Rgb, Red, Green, Blue };

inline const std::vector<LevelsChannel>& allLevelsChannels() {
    static const std::vector<LevelsChannel> channels = {
        LevelsChannel::Rgb, LevelsChannel::Red, LevelsChannel::Green, LevelsChannel::Blue,
    };
    return channels;
}

std::string levelsChannelName(LevelsChannel channel);
std::optional<LevelsChannel> levelsChannelFromName(const std::string& name);

struct LevelRange {
    double black = 0;
    double gamma = 1;
    double white = 255;
    double outputBlack = 0;
    double outputWhite = 255;

    LevelRange normalized() const {
        LevelRange result = *this;
        result.black = clampDouble(black, 0, 254, 0);
        result.white = clampDouble(white, result.black + 1, 255, 255);
        result.gamma = clampDouble(gamma, 0.1, 9.99, 1);
        result.outputBlack = clampDouble(outputBlack, 0, 255, 0);
        result.outputWhite = clampDouble(outputWhite, 0, 255, 255);
        return result;
    }

    /// The original's LevelsSettings.apply for one value (0–1), composite included by the caller.
    double apply(double value) const {
        const LevelRange s = normalized();
        const double input = std::min(1.0, std::max(0.0, (value * 255 - s.black) / (s.white - s.black)));
        return (s.outputBlack + std::pow(input, 1 / s.gamma) * (s.outputWhite - s.outputBlack)) / 255;
    }

    bool operator==(const LevelRange& other) const {
        return black == other.black && gamma == other.gamma && white == other.white
            && outputBlack == other.outputBlack && outputWhite == other.outputWhite;
    }
};

struct LevelsSettings {
    LevelsChannel channel = LevelsChannel::Rgb;
    std::vector<LevelRange> ranges = std::vector<LevelRange>(4);

    LevelRange current() const { return ranges[channelIndex(channel)]; }
    void setCurrent(const LevelRange& value) { ranges[channelIndex(channel)] = value.normalized(); }

    bool isIdentity() const {
        for (const auto& range : ranges)
            if (!(range.normalized() == LevelRange())) return false;
        return true;
    }

    bool isValid() const {
        if (ranges.size() != 4) return false;
        for (const auto& range : ranges)
            if (!(range == range.normalized())) return false;
        return true;
    }

    /// Individual channels, followed by the composite RGB adjustment.
    double apply(double value, LevelsChannel which) const {
        return ranges[0].apply(ranges[channelIndex(which)].apply(value));
    }

    static int channelIndex(LevelsChannel channel) { return static_cast<int>(channel); }

    bool operator==(const LevelsSettings& other) const {
        return channel == other.channel && ranges == other.ranges;
    }
};

// MARK: - Curves (Curves.swift)

struct CurvePoint {
    double x = 0;
    double y = 0;
    bool operator==(const CurvePoint& other) const { return x == other.x && y == other.y; }
};

struct CurvesSettings {
    LevelsChannel channel = LevelsChannel::Rgb;
    std::vector<std::vector<CurvePoint>> channels = std::vector<std::vector<CurvePoint>>(
        4, {CurvePoint{0, 0}, CurvePoint{255, 255}});

    /// Shape-preserving cubic Hermite interpolation avoids overshoot between handles.
    double value(double x, int channelIndex) const {
        const std::vector<CurvePoint>& p = channels[channelIndex];
        int i = 0;
        for (size_t index = 0; index < p.size(); ++index) {
            if (p[index].x <= x) i = static_cast<int>(index);
        }
        i = std::min(static_cast<int>(p.size()) - 2, std::max(0, i));
        std::vector<double> d;
        d.reserve(p.size() - 1);
        for (size_t index = 0; index + 1 < p.size(); ++index)
            d.push_back((p[index + 1].y - p[index].y) / (p[index + 1].x - p[index].x));
        auto slope = [&](int j) -> double {
            if (j == 0) return d[0];
            if (j == static_cast<int>(p.size()) - 1) return d.back();
            if (d[j - 1] * d[j] <= 0) return 0;
            return 2 / (1 / d[j - 1] + 1 / d[j]);
        };
        const double h = p[i + 1].x - p[i].x;
        const double t = std::min(1.0, std::max(0.0, (x - p[i].x) / h));
        const double y = (2 * t * t * t - 3 * t * t + 1) * p[i].y + (t * t * t - 2 * t * t + t) * h * slope(i)
            + (-2 * t * t * t + 3 * t * t) * p[i + 1].y + (t * t * t - t * t) * h * slope(i + 1);
        return std::min(255.0, std::max(0.0, y));
    }

    /// The composite value for a channel, as the original composes the two tables.
    double value(double x) const {
        return value(value(x, channelIndex(channel)), channelIndex(LevelsChannel::Rgb));
    }

    bool isValid() const {
        if (channels.size() != 4) return false;
        for (const auto& points : channels) {
            if (points.size() < 2 || points.size() > 32) return false;
            if (points.front().x != 0 || points.back().x != 255) return false;
            for (const auto& point : points)
                if (!finite(point.x) || !finite(point.y) || !inRange(point.x, 0, 255) || !inRange(point.y, 0, 255))
                    return false;
            for (size_t index = 0; index + 1 < points.size(); ++index)
                if (!(points[index].x < points[index + 1].x)) return false;
        }
        return true;
    }

    static int channelIndex(LevelsChannel channel) { return static_cast<int>(channel); }

    bool operator==(const CurvesSettings& other) const {
        return channel == other.channel && channels == other.channels;
    }
};

// MARK: - Straight-color adjustments (ImageAdjustments.swift)

/// A straight sRGB color stored with an adjustment, 0–1 per channel.
struct AdjustmentColor {
    double red = 0;
    double green = 0;
    double blue = 0;
    bool isValid() const {
        return finite(red) && inRange(red, 0, 1) && finite(green) && inRange(green, 0, 1)
            && finite(blue) && inRange(blue, 0, 1);
    }
    bool operator==(const AdjustmentColor& other) const {
        return red == other.red && green == other.green && blue == other.blue;
    }
};

/// Photoshop's Exposure: `exposure` (stops) scales linear light and `offset` shifts it, then
/// gamma correction bends the result.
struct ExposureSettings {
    static constexpr double exposureLow = -20, exposureHigh = 20;
    static constexpr double offsetLow = -0.5, offsetHigh = 0.5;
    static constexpr double gammaLow = 0.01, gammaHigh = 9.99;

    double exposure = 0;
    double offset = 0;
    double gamma = 1;

    bool isValid() const {
        return finite(exposure) && inRange(exposure, exposureLow, exposureHigh)
            && finite(offset) && inRange(offset, offsetLow, offsetHigh)
            && finite(gamma) && inRange(gamma, gammaLow, gammaHigh);
    }
    bool operator==(const ExposureSettings& other) const {
        return exposure == other.exposure && offset == other.offset && gamma == other.gamma;
    }
};

struct GradientMapSettings {
    AdjustmentColor shadows{0, 0, 0};
    AdjustmentColor highlights{1, 1, 1};
    bool reversed = false;
    bool isValid() const { return shadows.isValid() && highlights.isValid(); }
    bool operator==(const GradientMapSettings& other) const {
        return shadows == other.shadows && highlights == other.highlights && reversed == other.reversed;
    }
};

/// Black & White, as Photoshop's is: a choice of how bright each family of colors becomes in gray.
struct BlackWhiteSettings {
    static constexpr double rangeLow = -200, rangeHigh = 300;
    double reds = 40;
    double yellows = 60;
    double greens = 40;
    double cyans = 60;
    double blues = 20;
    double magentas = 80;
    bool tint = false;
    double tintHue = 40;
    double tintSaturation = 20;

    bool isValid() const {
        for (double value : {reds, yellows, greens, cyans, blues, magentas})
            if (!finite(value) || !inRange(value, rangeLow, rangeHigh)) return false;
        return finite(tintHue) && inRange(tintHue, 0, 360)
            && finite(tintSaturation) && inRange(tintSaturation, 0, 100);
    }
    bool operator==(const BlackWhiteSettings& other) const {
        return reds == other.reds && yellows == other.yellows && greens == other.greens
            && cyans == other.cyans && blues == other.blues && magentas == other.magentas
            && tint == other.tint && tintHue == other.tintHue && tintSaturation == other.tintSaturation;
    }
};

struct ColorBalanceSettings {
    static constexpr double rangeLow = -100, rangeHigh = 100;
    double shadowCyanRed = 0, shadowMagentaGreen = 0, shadowYellowBlue = 0;
    double midCyanRed = 0, midMagentaGreen = 0, midYellowBlue = 0;
    double highlightCyanRed = 0, highlightMagentaGreen = 0, highlightYellowBlue = 0;
    bool preserveLuminosity = true;

    std::vector<double> all() const {
        return {shadowCyanRed, shadowMagentaGreen, shadowYellowBlue,
                midCyanRed, midMagentaGreen, midYellowBlue,
                highlightCyanRed, highlightMagentaGreen, highlightYellowBlue};
    }
    bool isValid() const {
        for (double value : all())
            if (!finite(value) || !inRange(value, rangeLow, rangeHigh)) return false;
        return true;
    }
    bool isIdentity() const {
        for (double value : all())
            if (value != 0) return false;
        return true;
    }
    bool operator==(const ColorBalanceSettings& other) const {
        return all() == other.all() && preserveLuminosity == other.preserveLuminosity;
    }
};

/// Film grain: brightness noise, strongest in the midtones, fixed in document space by `seed`.
struct GrainSettings {
    static constexpr double amountLow = 0, amountHigh = 100;
    static constexpr double sizeLow = 0.5, sizeHigh = 20;
    static constexpr double roughnessLow = 0, roughnessHigh = 100;
    double amount = 25;
    double size = 1.5;
    double roughness = 50;
    uint32_t seed = 0;

    bool isValid() const {
        return finite(amount) && inRange(amount, amountLow, amountHigh)
            && finite(size) && inRange(size, sizeLow, sizeHigh)
            && finite(roughness) && inRange(roughness, roughnessLow, roughnessHigh);
    }
    GrainSettings normalized() const {
        GrainSettings result = *this;
        result.amount = clampDouble(amount, amountLow, amountHigh, 25);
        result.size = clampDouble(size, sizeLow, sizeHigh, 1.5);
        result.roughness = clampDouble(roughness, roughnessLow, roughnessHigh, 50);
        return result;
    }
    bool operator==(const GrainSettings& other) const {
        return amount == other.amount && size == other.size && roughness == other.roughness && seed == other.seed;
    }
};

// MARK: - The polymorphic adjustment (LayerAdjustment.swift)

enum class AdjustmentKind {
    Hsv, Levels, Curves, Exposure, GradientMap, Grain, AddNoise,
    GaussianBlur, MotionBlur, Invert, BlackWhite, ColorBalance,
};

inline const std::vector<AdjustmentKind>& allAdjustmentKinds() {
    static const std::vector<AdjustmentKind> kinds = {
        AdjustmentKind::Hsv, AdjustmentKind::Levels, AdjustmentKind::Curves,
        AdjustmentKind::Exposure, AdjustmentKind::GradientMap, AdjustmentKind::Grain,
        AdjustmentKind::AddNoise, AdjustmentKind::GaussianBlur, AdjustmentKind::MotionBlur,
        AdjustmentKind::Invert, AdjustmentKind::BlackWhite, AdjustmentKind::ColorBalance,
    };
    return kinds;
}

std::string adjustmentKindName(AdjustmentKind kind);
std::optional<AdjustmentKind> adjustmentKindFromName(const std::string& name);

/// One non-destructive adjustment layer's settings. Every field is the original's, optional
/// ones included, because they decide exactly which keys a save writes.
struct LayerAdjustment {
    AdjustmentKind kind = AdjustmentKind::Hsv;
    double hue = 0;
    double saturation = 0;
    double lightness = 0;
    bool colorize = false;
    /// Optional so projects saved before range-aware HSV adjustments still decode.
    std::optional<HueSaturationSettings> hsvSettings;
    LevelsSettings levels;
    CurvesSettings curves;
    std::optional<ExposureSettings> exposureSettings;
    std::optional<GradientMapSettings> gradientMapSettings;
    std::optional<GrainSettings> grainSettings;
    std::optional<BlackWhiteSettings> blackWhiteSettings;
    std::optional<ColorBalanceSettings> colorBalanceSettings;
    std::optional<double> blurRadius;
    std::optional<double> motionAngle;
    std::optional<double> motionDistance;
    std::optional<double> noiseAmount;
    std::optional<bool> noiseGaussian;
    std::optional<bool> noiseMonochromatic;
    std::optional<uint32_t> noiseSeed;

    HueSaturationSettings resolvedHSV() const {
        if (hsvSettings) return *hsvSettings;
        HueSaturationSettings settings;
        settings.colorize = colorize;
        settings.adjustments[ColorRange::Master] = RangeAdjustment{hue, saturation, lightness};
        return settings;
    }
    ExposureSettings exposure() const { return exposureSettings.value_or(ExposureSettings()); }
    GradientMapSettings gradientMap() const { return gradientMapSettings.value_or(GradientMapSettings()); }
    GrainSettings grain() const { return grainSettings.value_or(GrainSettings()); }
    BlackWhiteSettings blackWhite() const { return blackWhiteSettings.value_or(BlackWhiteSettings()); }
    ColorBalanceSettings colorBalance() const { return colorBalanceSettings.value_or(ColorBalanceSettings()); }
    double gaussianRadius() const { return blurRadius.value_or(10); }
    double resolvedMotionAngle() const { return motionAngle.value_or(0); }
    double resolvedMotionDistance() const { return motionDistance.value_or(10); }
    double resolvedNoiseAmount() const { return noiseAmount.value_or(10); }
    bool resolvedNoiseGaussian() const { return noiseGaussian.value_or(false); }
    bool resolvedNoiseMonochromatic() const { return noiseMonochromatic.value_or(false); }
    uint32_t resolvedNoiseSeed() const { return noiseSeed.value_or(0); }

    /// Document-pixel halo needed so a partial canvas redraw can sample beyond its dirty rectangle.
    double samplingMargin() const {
        switch (kind) {
            case AdjustmentKind::GaussianBlur: return gaussianRadius() * 3 + 2;
            case AdjustmentKind::MotionBlur: return resolvedMotionDistance() / 2 + 2;
            default: return 0;
        }
    }

    bool isValid() const {
        if (!finite(hue) || std::abs(hue) > 360) return false;
        if (!finite(saturation) || std::abs(saturation) > 100) return false;
        if (!finite(lightness) || std::abs(lightness) > 100) return false;
        if (!resolvedHSV().isValid()) return false;
        if (!levels.isValid() || !curves.isValid()) return false;
        if (!exposure().isValid() || !gradientMap().isValid() || !grain().isValid()) return false;
        if (!blackWhite().isValid() || !colorBalance().isValid()) return false;
        if (!finite(gaussianRadius()) || !inRange(gaussianRadius(), 0.1, 250)) return false;
        if (!finite(resolvedMotionAngle()) || !inRange(resolvedMotionAngle(), -90, 90)) return false;
        if (!finite(resolvedMotionDistance()) || !inRange(resolvedMotionDistance(), 1, 2000)) return false;
        if (!finite(resolvedNoiseAmount()) || !inRange(resolvedNoiseAmount(), 0.1, 400)) return false;
        return true;
    }

    /// Field-by-field, the way Swift's synthesized Equatable compares the original.
    bool operator==(const LayerAdjustment& other) const {
        return kind == other.kind && hue == other.hue && saturation == other.saturation
            && lightness == other.lightness && colorize == other.colorize && hsvSettings == other.hsvSettings
            && levels == other.levels && curves == other.curves && exposureSettings == other.exposureSettings
            && gradientMapSettings == other.gradientMapSettings && grainSettings == other.grainSettings
            && blackWhiteSettings == other.blackWhiteSettings && colorBalanceSettings == other.colorBalanceSettings
            && blurRadius == other.blurRadius && motionAngle == other.motionAngle
            && motionDistance == other.motionDistance && noiseAmount == other.noiseAmount
            && noiseGaussian == other.noiseGaussian && noiseMonochromatic == other.noiseMonochromatic
            && noiseSeed == other.noiseSeed;
    }
};

}  // namespace compositor
