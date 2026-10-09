#pragma once

// The blend math behind the canvas: the PDF/Photoshop formulas the original gets from Core Graphics
// and Core Image, computed here in sRGB — the canvas's own space. The original notes why the gamma
// matters: "these two modes are not separable from the gamma they are computed in" (SeparableBlend.swift).
//
// Sources and destinations are 8-bit, non-premultiplied RGBA. Blending follows the PDF spec, section
// 11.3.5: the blended color is mixed by the backdrop's alpha, then source-over compositing with the
// result, so a soft brush over a translucent layer behaves as it does in Photoshop.

#include <cstdint>

#include "core/BlendMode.h"

namespace compositor::blendops {

struct Rgba8 {
    uint8_t r = 0, g = 0, b = 0, a = 0;
};

/// A separable mode's blend function on normalized channel values, B(Cb, Cs) (PDF 11.3.5).
/// Hue, Saturation, Color and Luminosity are not separable; don't call this for them.
float blendChannel(BlendMode mode, float b, float s);

/// A non-separable mode's blend function on normalized RGB triples (PDF 11.3.6).
void blendColor(BlendMode mode, const float backdrop[3], const float source[3], float out[3]);

/// Whether the mode mixes channels (Hue, Saturation, Color, Luminosity).
bool isNonSeparable(BlendMode mode);

/// Composites one source pixel over one backdrop pixel, non-premultiplied in, non-premultiplied out.
Rgba8 compositePixel(BlendMode mode, Rgba8 backdrop, Rgba8 source);

/// Rounds a normalized value to 8 bits, the way a drawing surface would.
uint8_t to8(float normalized);

}  // namespace compositor::blendops
