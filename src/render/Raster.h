#pragma once

#include <QImage>

#include <cstdint>
#include <vector>

#include "core/BlendMode.h"

namespace compositor::render {

// 8-bit premultiplied RGBA, row 0 at the top, in document pixel order.
// The format matches QImage::Format_RGBA8888_Premultiplied exactly.
struct Raster {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;

    Raster() = default;
    Raster(int w, int h) { resize(w, h); }

    void resize(int w, int h) {
        width = w;
        height = h;
        pixels.assign(static_cast<size_t>(w) * h * 4, 0);
    }
    [[nodiscard]] bool isEmpty() const { return width <= 0 || height <= 0; }
    [[nodiscard]] const uint8_t* row(int y) const { return pixels.data() + static_cast<size_t>(y) * width * 4; }
    [[nodiscard]] uint8_t* row(int y) { return pixels.data() + static_cast<size_t>(y) * width * 4; }
};

// 8-bit single-channel coverage, row 0 at the top.
struct GrayImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

// p[3] = 255 and the channels divided by alpha; a byte copy of BrushPixels.c's
// layer_unpremultiply_opaque: a ? (p[c]*255u + a/2)/a : 0.
void unpremultiplyOpaque(Raster& raster);

// p[c] = (p[c]*a + 127)/255, p[3] = a: BrushPixels.c's layer_restore_alpha.
void restoreAlpha(Raster& raster, const GrayImage& alpha);

// A byte copy of BrushPixels.c's layer_extract_alpha.
GrayImage extractAlpha(const Raster& raster);

// Sample image -> premultiplied RGBA raster; gray images become a coverage plane.
Raster rasterFromQImage(const QImage& image);
GrayImage grayFromQImage(const QImage& image);

// Composites one source pixel over the target pixel at (x, y), the PDF 11.3.5.2 formula:
//   as' = f*sa, sr' = f*sr (0..1, premultiplied)
//   co = (1 - as')*db + (1 - ab)*sr' + ab*as'*B(Cb, Cs)
//   ao = as' + ab*(1 - as')
// `sr`..`sa` are the source's premultiplied 0..255 values; `f` folds in opacity,
// coverage and mask samples. Blends in straight color, stores premultiplied.
void blendOver(Raster& target, int x, int y, BlendMode mode,
               double sr, double sg, double sb, double sa, double f);

}  // namespace compositor::render
