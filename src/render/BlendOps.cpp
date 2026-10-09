#include "render/BlendOps.h"

#include <algorithm>
#include <cmath>

namespace compositor::blendops {
namespace {

float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }

// The formulas below are Photoshop's, as tabulated in the PDF spec and implemented by Core Graphics
// and Core Image. b and s are backdrop and source channels in [0, 1].

float multiply(float b, float s) { return b * s; }
float screen(float b, float s) { return b + s - b * s; }

float colorBurn(float b, float s) {
    if (b <= 0.0f) return 0.0f;
    if (s >= 1.0f) return 1.0f;
    return 1.0f - std::min(1.0f, (1.0f - b) / s);
}

float colorDodge(float b, float s) {
    if (b >= 1.0f) return 1.0f;
    if (s <= 0.0f) return 0.0f;
    return std::min(1.0f, b / (1.0f - s));
}

float hardLight(float b, float s) {
    return s <= 0.5f ? multiply(b, 2.0f * s) : screen(b, 2.0f * s - 1.0f);
}

// Photoshop's soft light; the original routes this mode through Core Image because "the formula is up
// to 25 levels off Photoshop's with a light blend color" when Core Graphics does it.
float softLight(float b, float s) {
    if (s <= 0.5f) return b - (1.0f - 2.0f * s) * b * (1.0f - b);
    float d = b <= 0.25f ? ((16.0f * b - 12.0f) * b + 4.0f) * b : std::sqrt(b);
    return b + (2.0f * s - 1.0f) * (d - b);
}

float vividLight(float b, float s) {
    return s <= 0.5f ? colorBurn(b, 2.0f * s) : colorDodge(b, 2.0f * s - 1.0f);
}

float pinLight(float b, float s) {
    return s <= 0.5f ? std::min(b, 2.0f * s) : std::max(b, 2.0f * s - 1.0f);
}

float divide(float b, float s) {
    if (s <= 0.0f) return b > 0.0f ? 1.0f : 0.0f;
    return std::min(1.0f, b / s);
}

// --- Non-separable helpers (PDF 11.3.5, the way Core Graphics computes Hue/Saturation/Color/Luminosity).

float lum(const float c[3]) { return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]; }

void clipColor(float c[3]) {
    float l = lum(c);
    float n = std::min({c[0], c[1], c[2]});
    float x = std::max({c[0], c[1], c[2]});
    if (n < 0.0f) {
        for (int i = 0; i < 3; ++i) c[i] = l + (c[i] - l) * l / (l - n);
    }
    if (x > 1.0f) {
        for (int i = 0; i < 3; ++i) c[i] = l + (c[i] - l) * (1.0f - l) / (x - l);
    }
}

void setLum(float c[3], float l) {
    float d = l - lum(c);
    for (int i = 0; i < 3; ++i) c[i] += d;
    clipColor(c);
}

float sat(const float c[3]) { return std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]}); }

void setSat(float c[3], float s) {
    int mn = 0, mx = 0;
    for (int i = 1; i < 3; ++i) {
        if (c[i] < c[mn]) mn = i;
        if (c[i] > c[mx]) mx = i;
    }
    if (c[mx] > c[mn]) {
        int md = 3 - mn - mx;
        c[md] = (c[md] - c[mn]) * s / (c[mx] - c[mn]);
        c[mx] = s;
        c[mn] = 0.0f;
    } else {
        c[0] = c[1] = c[2] = 0.0f;
    }
}

}  // namespace

float blendChannel(BlendMode mode, float b, float s) {
    switch (mode) {
        case BlendMode::Normal: return s;
        case BlendMode::Darken: return std::min(b, s);
        case BlendMode::Multiply: return multiply(b, s);
        case BlendMode::ColorBurn: return colorBurn(b, s);
        case BlendMode::LinearBurn: return clamp01(b + s - 1.0f);
        case BlendMode::Lighten: return std::max(b, s);
        case BlendMode::Screen: return screen(b, s);
        case BlendMode::ColorDodge: return colorDodge(b, s);
        case BlendMode::LinearDodge: return clamp01(b + s);
        case BlendMode::Overlay: return hardLight(s, b);
        case BlendMode::SoftLight: return softLight(b, s);
        case BlendMode::HardLight: return hardLight(b, s);
        case BlendMode::VividLight: return vividLight(b, s);
        case BlendMode::LinearLight: return clamp01(b + 2.0f * s - 1.0f);
        case BlendMode::PinLight: return pinLight(b, s);
        case BlendMode::HardMix: return b + s >= 1.0f ? 1.0f : 0.0f;
        case BlendMode::Difference: return std::abs(b - s);
        case BlendMode::Exclusion: return b + s - 2.0f * b * s;
        case BlendMode::Subtract: return clamp01(b - s);
        case BlendMode::Divide: return divide(b, s);
        default: return s;
    }
}

bool isNonSeparable(BlendMode mode) {
    return mode == BlendMode::Hue || mode == BlendMode::Saturation || mode == BlendMode::Color
        || mode == BlendMode::Luminosity;
}

void blendColor(BlendMode mode, const float backdrop[3], const float source[3], float out[3]) {
    float c[3] = {source[0], source[1], source[2]};
    switch (mode) {
        case BlendMode::Hue:
            setSat(c, sat(backdrop));
            setLum(c, lum(backdrop));
            break;
        case BlendMode::Saturation: {
            float b[3] = {backdrop[0], backdrop[1], backdrop[2]};
            setSat(b, sat(source));
            setLum(b, lum(backdrop));
            std::copy(b, b + 3, c);
            break;
        }
        case BlendMode::Color:
            setLum(c, lum(backdrop));
            break;
        case BlendMode::Luminosity: {
            float b[3] = {backdrop[0], backdrop[1], backdrop[2]};
            setLum(b, lum(source));
            std::copy(b, b + 3, c);
            break;
        }
        default:
            break;
    }
    out[0] = c[0];
    out[1] = c[1];
    out[2] = c[2];
}

uint8_t to8(float normalized) {
    return static_cast<uint8_t>(std::lround(clamp01(normalized) * 255.0f));
}

Rgba8 compositePixel(BlendMode mode, Rgba8 backdrop, Rgba8 source) {
    if (source.a == 0) return backdrop;
    if (backdrop.a == 0) return source;
    float ab = backdrop.a / 255.0f;
    float as = source.a / 255.0f;
    float cb[3] = {backdrop.r / 255.0f, backdrop.g / 255.0f, backdrop.b / 255.0f};
    float cs[3] = {source.r / 255.0f, source.g / 255.0f, source.b / 255.0f};
    float blended[3];
    if (isNonSeparable(mode)) {
        blendColor(mode, cb, cs, blended);
    } else {
        for (int i = 0; i < 3; ++i) blended[i] = blendChannel(mode, cb[i], cs[i]);
    }
    // C's = (1 - ab) * Cs + ab * B(Cb, Cs): where the backdrop is transparent the source shows through.
    float ao = as + ab * (1.0f - as);
    float co[3];
    for (int i = 0; i < 3; ++i) {
        co[i] = (as * blended[i] + ab * cb[i] * (1.0f - as)) / ao;
    }
    return Rgba8{to8(co[0]), to8(co[1]), to8(co[2]), to8(ao)};
}

}  // namespace compositor::blendops
