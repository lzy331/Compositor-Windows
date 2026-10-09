#include "render/Raster.h"

#include <algorithm>

#include "render/BlendOps.h"

namespace compositor::render {

void unpremultiplyOpaque(Raster& raster) {
    for (int y = 0; y < raster.height; ++y) {
        uint8_t* row = raster.row(y);
        for (int x = 0; x < raster.width; ++x) {
            uint8_t* p = row + x * 4;
            const unsigned a = p[3];
            if (a) {
                for (int c = 0; c < 3; ++c) {
                    p[c] = static_cast<uint8_t>(std::min(255u, (static_cast<unsigned>(p[c]) * 255u + a / 2) / a));
                }
            } else {
                p[0] = p[1] = p[2] = 0;
            }
            p[3] = 255;
        }
    }
}

void restoreAlpha(Raster& raster, const GrayImage& alpha) {
    for (int y = 0; y < raster.height; ++y) {
        uint8_t* row = raster.row(y);
        const uint8_t* arow = alpha.pixels.data() + static_cast<size_t>(y) * alpha.width;
        for (int x = 0; x < raster.width; ++x) {
            uint8_t* p = row + x * 4;
            const unsigned a = arow[x];
            for (int c = 0; c < 3; ++c) {
                p[c] = static_cast<uint8_t>((static_cast<unsigned>(p[c]) * a + 127) / 255);
            }
            p[3] = static_cast<uint8_t>(a);
        }
    }
}

GrayImage extractAlpha(const Raster& raster) {
    GrayImage result;
    result.width = raster.width;
    result.height = raster.height;
    result.pixels.resize(static_cast<size_t>(raster.width) * raster.height);
    for (int y = 0; y < raster.height; ++y) {
        const uint8_t* row = raster.row(y);
        uint8_t* out = result.pixels.data() + static_cast<size_t>(y) * raster.width;
        for (int x = 0; x < raster.width; ++x) out[x] = row[x * 4 + 3];
    }
    return result;
}

Raster rasterFromQImage(const QImage& image) {
    const QImage source = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    Raster result(source.width(), source.height());
    for (int y = 0; y < source.height(); ++y) {
        std::copy_n(source.constScanLine(y), static_cast<size_t>(source.width()) * 4, result.row(y));
    }
    return result;
}

GrayImage grayFromQImage(const QImage& image) {
    const QImage source = image.convertToFormat(QImage::Format_Grayscale8);
    GrayImage result;
    result.width = source.width();
    result.height = source.height();
    result.pixels.resize(static_cast<size_t>(source.width()) * source.height());
    for (int y = 0; y < source.height(); ++y) {
        std::copy_n(source.constScanLine(y), static_cast<size_t>(source.width()),
                    result.pixels.data() + static_cast<size_t>(y) * source.width());
    }
    return result;
}

void blendOver(Raster& target, int x, int y, BlendMode mode,
               double sr, double sg, double sb, double sa, double f) {
    const double alpha = f * sa / 255.0;
    if (alpha <= 0) return;
    uint8_t* p = target.row(y) + x * 4;
    const double dr = p[0] / 255.0, dg = p[1] / 255.0, db = p[2] / 255.0, da = p[3] / 255.0;
    const double srp = f * sr / 255.0, sgp = f * sg / 255.0, sbp = f * sb / 255.0;
    double outR = srp, outG = sgp, outB = sbp;
    if (da > 0) {
        const float backdrop[3] = {static_cast<float>(dr / da), static_cast<float>(dg / da), static_cast<float>(db / da)};
        const float source[3] = {static_cast<float>(sr / sa), static_cast<float>(sg / sa), static_cast<float>(sb / sa)};
        float blended[3];
        if (blendops::isNonSeparable(mode)) {
            blendops::blendColor(mode, backdrop, source, blended);
        } else {
            for (int c = 0; c < 3; ++c) blended[c] = blendops::blendChannel(mode, backdrop[c], source[c]);
        }
        outR = (1 - alpha) * dr + (1 - da) * srp + da * alpha * blended[0];
        outG = (1 - alpha) * dg + (1 - da) * sgp + da * alpha * blended[1];
        outB = (1 - alpha) * db + (1 - da) * sbp + da * alpha * blended[2];
    }
    const double outA = alpha + da * (1 - alpha);
    const uint8_t a8 = blendops::to8(static_cast<float>(outA));
    p[0] = std::min(blendops::to8(static_cast<float>(outR)), a8);
    p[1] = std::min(blendops::to8(static_cast<float>(outG)), a8);
    p[2] = std::min(blendops::to8(static_cast<float>(outB)), a8);
    p[3] = a8;
}

}  // namespace compositor::render
