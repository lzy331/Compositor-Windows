// Verifies the compositing engine against hand-derived goldens: the halving kernels, the
// resample rules, mask sampling, one layer's draw, clipping (live-mask) stacks, and whole
// documents through DocumentRenderer (blend modes, opacity, folders, masks, invisibility).

#include "document/Manifest.h"
#include "io/ImageAssets.h"
#include "io/ProjectError.h"
#include "io/ProjectStore.h"
#include "render/DocumentRenderer.h"
#include "render/Geometry.h"
#include "render/LayerDraw.h"
#include "render/MaskClip.h"
#include "render/Raster.h"
#include "render/RenderError.h"
#include "render/Resample.h"
#include "render/StackRenderer.h"

#include <QColor>
#include <QImage>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <vector>

using compositor::BlendMode;
using compositor::ImportedImage;
using compositor::LayerAdjustment;
using compositor::LayerSampling;
using compositor::LayerTransform;
using compositor::Point;
using compositor::ProjectError;
using compositor::ProjectLayerRecord;
using compositor::ProjectManifest;
using compositor::ProjectSnapshot;
using compositor::Rect;
using compositor::Size;
using compositor::Uuid;

using compositor::render::DocumentRenderer;
using compositor::render::DownsampleStore;
using compositor::render::FolderClip;
using compositor::render::GrayImage;
using compositor::render::Interp;
using compositor::render::MaskPlacementStore;
using compositor::render::Raster;
using compositor::render::RenderError;
using compositor::render::StackRenderer;
using compositor::render::drawLayer;
using compositor::render::extractAlpha;
using compositor::render::folderClipFactor;
using compositor::render::halveGray;
using compositor::render::halveRaster;
using compositor::render::layerDocumentToLocal;
using compositor::render::layerLocalToDocument;
using compositor::render::maskBackground;
using compositor::render::restoreAlpha;
using compositor::render::sampleGray;
using compositor::render::sampleRaster;
using compositor::render::unpremultiplyOpaque;

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

void nearD(double value, double expected, double tolerance, const std::string& what) {
    if (!(std::fabs(value - expected) <= tolerance)) {
        std::printf("FAIL %s (got %.9f want %.9f)\n", what.c_str(), value, expected);
        ++failures;
    }
}

void checkPixel(const Raster& raster, int x, int y, int r, int g, int b, int a, int tolerance,
                const std::string& what) {
    const uint8_t* p = raster.row(y) + static_cast<size_t>(x) * 4;
    const bool ok = std::abs(static_cast<int>(p[0]) - r) <= tolerance
        && std::abs(static_cast<int>(p[1]) - g) <= tolerance
        && std::abs(static_cast<int>(p[2]) - b) <= tolerance
        && std::abs(static_cast<int>(p[3]) - a) <= tolerance;
    if (!ok) {
        std::printf("FAIL %s (got %d,%d,%d,%d want %d,%d,%d,%d)\n", what.c_str(), static_cast<int>(p[0]),
                    static_cast<int>(p[1]), static_cast<int>(p[2]), static_cast<int>(p[3]), r, g, b, a);
        ++failures;
    }
}

bool throwsProject(const std::function<void()>& body, ProjectError::Kind kind) {
    try {
        body();
        return false;
    } catch (const ProjectError& error) {
        return error.kind() == kind;
    }
}

bool throwsRender(const std::function<void()>& body, RenderError::Kind kind) {
    try {
        body();
        return false;
    } catch (const RenderError& error) {
        return error.kind() == kind;
    }
}

// The 4-tap halving kernel is (-1, 9, 9, -1)/16 per axis; the goldens below are the sums it
// produces by hand, including the weight that falls off the edges.

void testHalveRaster() {
    // 2x2 uniform: only the two 9/16 taps per axis land inside, so a value of 200 becomes
    // 200 * (18/16)^2 = 253.125, rounded to 253 on every channel including alpha.
    Raster uniform(2, 2);
    std::fill(uniform.pixels.begin(), uniform.pixels.end(), 200);
    const Raster half = halveRaster(uniform);
    check(half.width == 1 && half.height == 1, "halve 2x2 -> 1x1");
    if (half.width == 1 && half.height == 1) {
        checkPixel(half, 0, 0, 253, 253, 253, 253, 0, "uniform 200 halves to 253");
    }

    // One opaque red pixel at the top-left: its single surviving tap weighs 9/16 * 9/16 = 81/256.
    Raster single(2, 2);
    single.row(0)[0] = 255;  // r
    single.row(0)[3] = 255;  // a
    const Raster halfSingle = halveRaster(single);
    checkPixel(halfSingle, 0, 0, 81, 0, 0, 81, 0, "a lone pixel keeps 81/256 of its value");

    // 4x4 with only the top-left pixel set: only the top-left output sees it.
    Raster corner(4, 4);
    corner.row(0)[0] = 100;  // r
    corner.row(0)[3] = 255;  // a
    const Raster halfCorner = halveRaster(corner);
    check(halfCorner.width == 2 && halfCorner.height == 2, "halve 4x4 -> 2x2");
    if (halfCorner.width == 2 && halfCorner.height == 2) {
        checkPixel(halfCorner, 0, 0, 32, 0, 0, 81, 0, "the corner pixel weighs 81/256");
        checkPixel(halfCorner, 1, 0, 0, 0, 0, 0, 0, "the neighbour across stays empty");
        checkPixel(halfCorner, 0, 1, 0, 0, 0, 0, 0, "the neighbour below stays empty");
        checkPixel(halfCorner, 1, 1, 0, 0, 0, 0, 0, "the far corner stays empty");
    }
}

void testHalveGray() {
    // Gray halving extends the edge instead of padding with zeroes, so the weights sum to one
    // and a uniform image keeps its value exactly.
    GrayImage uniform;
    uniform.width = 2;
    uniform.height = 2;
    uniform.pixels = {200, 200, 200, 200};
    const GrayImage half = halveGray(uniform);
    check(half.width == 1 && half.height == 1, "halve 2x2 gray -> 1x1");
    if (!half.pixels.empty()) {
        check(half.pixels[0] == 200, "uniform gray keeps its value");
    }

    // [255, 0; 0, 0]: the out-of-range tap re-samples the edge pixel, so the spike carries
    // 9/16 - 1/16 = 1/2 per axis: 255 * (1/2)^2 = 63.75, rounded to 64.
    GrayImage spike;
    spike.width = 2;
    spike.height = 2;
    spike.pixels = {255, 0, 0, 0};
    const GrayImage halfSpike = halveGray(spike);
    check(!halfSpike.pixels.empty() && halfSpike.pixels[0] == 64, "an edge spike keeps a quarter");
}

void testDownsampleLevel() {
    using compositor::render::downsampleLevel;
    check(downsampleLevel(0.5) == 0, "half size is already the boundary (strict)");
    check(downsampleLevel(0.51) == 0, "just above half needs no halvings");
    check(downsampleLevel(0.4999) == 1, "just below half wants one halving");
    check(downsampleLevel(0.25) == 2, "a quarter wants two halvings");
    check(downsampleLevel(0.2) == 2, "floor(log2(5)) = 2");
    check(downsampleLevel(1.0 / 128) == 6, "halvings are capped at six");
    check(downsampleLevel(2.0) == 0, "enlarging never halves");
    check(downsampleLevel(0.0) == 0, "zero is not a factor");
    check(downsampleLevel(-1.0) == 0, "negative is not a factor");
    check(downsampleLevel(std::nan("")) == 0, "nan is not a factor");
}

void testInterpolation() {
    using compositor::render::downsampleLevel;
    using compositor::render::interpolationFor;
    using compositor::render::rawQuality;
    (void)downsampleLevel;
    check(rawQuality(LayerSampling::Nearest) == Interp::Nearest, "raw nearest");
    check(rawQuality(LayerSampling::Smooth) == Interp::Bilinear, "raw smooth is bilinear");
    check(rawQuality(LayerSampling::High) == Interp::Bicubic, "raw high is bicubic");

    check(interpolationFor(LayerSampling::Nearest, 0.25, true) == Interp::Nearest, "nearest stays nearest");
    check(interpolationFor(LayerSampling::High, 1.0, true) == Interp::Nearest, "upright 1:1 is a copy");
    check(interpolationFor(LayerSampling::High, 1.0005, true) == Interp::Nearest, "upright near 1:1 is a copy");
    check(interpolationFor(LayerSampling::High, 0.99, true) == Interp::Bilinear, "shrinking is bilinear");
    check(interpolationFor(LayerSampling::High, 1.0, false) == Interp::Bilinear, "rotated 1:1 is bilinear");
    check(interpolationFor(LayerSampling::High, 2.0, true) == Interp::Bicubic, "enlarging reads raw quality");
    check(interpolationFor(LayerSampling::Smooth, 2.0, true) == Interp::Bilinear, "smooth enlarges bilinear");
    check(interpolationFor(LayerSampling::Nearest, 6.0, false) == Interp::Nearest, "nearest never resamples");
}

void testSampling() {
    GrayImage gray;
    gray.width = 2;
    gray.height = 2;
    gray.pixels = {10, 20, 30, 40};
    check(sampleGray(gray, 0.9, 0.9, Interp::Nearest) == 10, "nearest reads the texel under the point");
    check(sampleGray(gray, 1.2, 0.9, Interp::Nearest) == 20, "nearest reads the next column");
    check(sampleGray(gray, 1.2, 1.2, Interp::Nearest) == 40, "nearest reads the next row");
    check(sampleGray(gray, 2.5, 2.5, Interp::Nearest) == 40, "nearest clamps past the edge");
    check(sampleGray(gray, -3.0, -3.0, Interp::Nearest) == 10, "nearest clamps before the edge");
    nearD(sampleGray(gray, 1.0, 0.5, Interp::Bilinear), 15, 1e-12, "bilinear halfway across a row");
    nearD(sampleGray(gray, 0.5, 1.0, Interp::Bilinear), 20, 1e-12, "bilinear halfway down a column");
    nearD(sampleGray(gray, 1.0, 1.0, Interp::Bilinear), 25, 1e-12, "bilinear at the four-corner midpoint");

    Raster raster(2, 1);
    uint8_t* left = raster.row(0);
    left[0] = 100;
    left[3] = 200;
    uint8_t* right = left + 4;
    right[1] = 100;
    right[3] = 100;
    const auto blended = sampleRaster(raster, 1.0, 0.5, Interp::Bilinear);
    nearD(blended.r, 50, 1e-12, "bilinear mixes red");
    nearD(blended.g, 50, 1e-12, "bilinear mixes green");
    nearD(blended.b, 0, 1e-12, "bilinear keeps blue");
    nearD(blended.a, 150, 1e-12, "bilinear mixes alpha");

    // Channels past alpha are clamped to it, and everything lands in 0..255.
    Raster odd(1, 1);
    uint8_t* p = odd.row(0);
    p[0] = 200;
    p[1] = 100;
    p[2] = 50;
    p[3] = 100;
    const auto clamped = sampleRaster(odd, 0.5, 0.5, Interp::Nearest);
    nearD(clamped.r, 100, 0, "red is clamped to alpha");
    nearD(clamped.g, 100, 0, "green stays");
    nearD(clamped.b, 50, 0, "blue stays");
    nearD(clamped.a, 100, 0, "alpha stays");

    Raster pattern(3, 3);
    for (int i = 0; i < 9; ++i) {
        uint8_t* texel = pattern.row(i / 3) + (i % 3) * 4;
        texel[0] = static_cast<uint8_t>(50 + i * 20);
        texel[1] = static_cast<uint8_t>(200 - i * 20);
        texel[2] = 100;
        texel[3] = 255;
    }
    bool bounded = true;
    for (double v = -2.5; v <= 5.5; v += 0.5) {
        for (double u = -2.5; u <= 5.5; u += 0.5) {
            for (Interp interp : {Interp::Nearest, Interp::Bilinear, Interp::Bicubic}) {
                const auto s = sampleRaster(pattern, u, v, interp);
                if (s.r < 0 || s.g < 0 || s.b < 0 || s.a < 0 || s.a > 255) bounded = false;
                if (s.r > s.a + 1e-9 || s.g > s.a + 1e-9 || s.b > s.a + 1e-9) bounded = false;
            }
        }
    }
    check(bounded, "every sample stays premultiplied and in range");
}

void testAlphaOps() {
    Raster raster(2, 1);
    uint8_t* p = raster.row(0);
    p[0] = 1; p[1] = 2; p[2] = 3; p[3] = 5;
    p[4] = 4; p[5] = 5; p[6] = 6; p[7] = 250;
    const GrayImage alpha = extractAlpha(raster);
    check(alpha.width == 2 && alpha.pixels[0] == 5 && alpha.pixels[1] == 250, "extractAlpha reads p[3]");

    Raster opaque(1, 1);
    uint8_t* q = opaque.row(0);
    q[0] = 100; q[1] = 50; q[2] = 25; q[3] = 200;
    unpremultiplyOpaque(opaque);
    checkPixel(opaque, 0, 0, 128, 64, 32, 255, 0, "unpremultiply divides by alpha (integer math)");

    Raster restored(1, 1);
    uint8_t* r = restored.row(0);
    r[0] = 128; r[1] = 64; r[2] = 32; r[3] = 255;
    GrayImage half;
    half.width = 1;
    half.height = 1;
    half.pixels = {200};
    restoreAlpha(restored, half);
    checkPixel(restored, 0, 0, 100, 50, 25, 200, 0, "restoreAlpha multiplies back");

    Raster cleared(1, 1);
    uint8_t* c = cleared.row(0);
    c[0] = 100; c[1] = 50; c[2] = 25; c[3] = 255;
    GrayImage zero;
    zero.width = 1;
    zero.height = 1;
    zero.pixels = {0};
    restoreAlpha(cleared, zero);
    checkPixel(cleared, 0, 0, 0, 0, 0, 0, 0, "restoring zero alpha clears the pixel");
}

void testTransformRoundTrip() {
    LayerTransform transform;
    transform.origin = Point(10, 20);
    transform.size = Size(4, 2);
    transform.rotation = 90;
    const Point local{1, 0.5};
    const Point document = layerLocalToDocument(transform).apply(local);
    const Point back = layerDocumentToLocal(transform).apply(document);
    nearD(back.x, local.x, 1e-12, "local -> document -> local keeps x");
    nearD(back.y, local.y, 1e-12, "local -> document -> local keeps y");
    // Rotation is clockwise in a y-down document: the local +x axis points down.
    nearD(document.x, 12.5, 1e-12, "rotated placement x");
    nearD(document.y, 22, 1e-12, "rotated placement y");
}

void testDownsampleStore() {
    using compositor::render::DownsampleStore;
    DownsampleStore store;
    Raster image(4, 4);
    std::fill(image.pixels.begin(), image.pixels.end(), 10);

    const auto plain = store.reduce(image, 0);
    check(plain.level == 0 && plain.image == &image, "zero levels is the original");
    const auto one = store.reduce(image, 1);
    check(one.level == 1 && one.image == store.reduce(image, 1).image && one.image->width == 2
              && one.image->height == 2,
          "one halving is cached and 2x2");
    const auto many = store.reduce(image, 6);
    check(many.level == 2 && many.image->width == 1 && many.image->height == 1,
          "halving stops at one pixel (asked for fewer than six)");

    GrayImage gray;
    gray.width = 2;
    gray.height = 2;
    gray.pixels = {0, 0, 0, 0};
    const auto grayOne = store.reduce(gray, 1);
    check(grayOne.level == 1 && grayOne.image->width == 1, "gray images halve too");
}

void testFolderClipFactor() {
    GrayImage mask;
    mask.width = 2;
    mask.height = 2;
    mask.pixels = {128, 128, 128, 128};
    LayerTransform transform;
    transform.origin = Point(0, 0);
    transform.size = Size(2, 2);

    std::vector<FolderClip> folders{FolderClip{&mask, transform}};
    nearD(folderClipFactor(Point(1, 1), folders), 128.0 / 255.0, 1e-9, "inside the folder mask its value votes");
    nearD(folderClipFactor(Point(5, 5), folders), 0.0, 1e-9, "far outside the folder mask nothing shows");

    std::vector<FolderClip> nested{FolderClip{&mask, transform}, FolderClip{&mask, transform}};
    nearD(folderClipFactor(Point(1, 1), nested), (128.0 / 255.0) * (128.0 / 255.0), 1e-9,
          "nested folder masks multiply");

    GrayImage empty;
    std::vector<FolderClip> emptyMask{FolderClip{&empty, transform}};
    nearD(folderClipFactor(Point(1, 1), emptyMask), 1.0, 1e-9, "a zero-size mask is skipped");
    LayerTransform flat;
    std::vector<FolderClip> flatFolder{FolderClip{&mask, flat}};
    nearD(folderClipFactor(Point(1, 1), flatFolder), 1.0, 1e-9, "a zero-size folder is skipped");
    std::vector<FolderClip> noMask{FolderClip{nullptr, transform}};
    nearD(folderClipFactor(Point(1, 1), noMask), 1.0, 1e-9, "a maskless folder is skipped");
}

void testMaskBackground() {
    GrayImage white;
    white.width = 1;
    white.height = 1;
    white.pixels = {255};
    check(maskBackground(white) == 255, "a full thumbnail shows through");

    GrayImage black;
    black.width = 1;
    black.height = 1;
    black.pixels = {0};
    check(maskBackground(black) == 0, "an empty thumbnail hides everything");

    GrayImage even;
    even.width = 2;
    even.height = 2;
    even.pixels = {0, 255, 255, 0};
    check(maskBackground(even) == 255, "an even border (510*2 >= 4*255) reads as shown");

    GrayImage below;
    below.width = 2;
    below.height = 2;
    below.pixels = {0, 127, 127, 0};
    check(maskBackground(below) == 0, "a border below half reads as hidden");

    GrayImage inside;
    inside.width = 3;
    inside.height = 3;
    inside.pixels = {0, 0, 0, 0, 255, 0, 0, 0, 0};
    check(maskBackground(inside) == 0, "the interior is not consulted");

    GrayImage nothing;
    check(maskBackground(nothing) == 255, "an empty thumbnail shows through");
}

void testClipImage() {
    DownsampleStore downsamples;
    MaskPlacementStore store(downsamples);
    GrayImage raw;
    raw.width = 1;
    raw.height = 1;
    raw.pixels = {200};
    GrayImage thumbnail;
    thumbnail.width = 1;
    thumbnail.height = 1;
    thumbnail.pixels = {200};

    LayerTransform layer;
    layer.origin = Point(0, 0);
    layer.size = Size(2, 2);
    check(store.clipImage(raw, thumbnail, std::nullopt, layer, 2, 2) == &raw, "a linked mask draws as it is");
    check(store.clipImage(raw, thumbnail, layer, layer, 2, 2) == &raw, "a mask sitting on its layer draws as it is");
    check(store.clipImage(raw, thumbnail, std::nullopt, layer, 2, 2) == &raw, "another lookup agrees");

    // Placed over the layer's top-left quadrant: the covered corner keeps the mask, the rest
    // shows what the mask's edge votes (its 1x1 thumbnail is 200, so more than half bright).
    LayerTransform placement;
    placement.origin = Point(0, 0);
    placement.size = Size(1, 1);
    const GrayImage* placed = store.clipImage(raw, thumbnail, placement, layer, 2, 2);
    check(placed != &raw && placed->width == 2 && placed->height == 2, "a moved mask lands on the layer grid");
    if (placed != nullptr && placed->width == 2 && placed->height == 2) {
        check(placed->pixels[0] == 200, "the covered corner keeps the mask's value");
        check(placed->pixels[1] == 255 && placed->pixels[2] == 255 && placed->pixels[3] == 255,
              "the uncovered part shows what the mask's edge votes");
    }
}

void testDrawLayer() {
    Raster image(1, 1);
    uint8_t* pixel = image.row(0);
    pixel[0] = 100;
    pixel[1] = 50;
    pixel[2] = 25;
    pixel[3] = 200;

    LayerTransform transform;
    transform.origin = Point(0, 0);
    transform.size = Size(1, 1);
    transform.sampling = LayerSampling::Nearest;

    DownsampleStore downsamples;
    Raster target(1, 1);
    drawLayer(image, transform, 1, BlendMode::Normal, nullptr, nullptr, nullptr, downsamples, target);
    checkPixel(target, 0, 0, 100, 50, 25, 200, 0, "an untouched pixel is copied straight across");

    // A gray mask at 128/255 scales the whole blend factor.
    GrayImage mask;
    mask.width = 1;
    mask.height = 1;
    mask.pixels = {128};
    Raster masked(1, 1);
    drawLayer(image, transform, 1, BlendMode::Normal, &mask, nullptr, nullptr, downsamples, masked);
    checkPixel(masked, 0, 0, 50, 25, 13, 100, 0, "a half mask halves the pixel");

    // Shrinking to exactly half does not halve yet (the threshold is strict); the final
    // bilinear step averages the four texels: (0+100+100+200)/4 = 100.
    Raster quad(2, 2);
    uint8_t* q0 = quad.row(0);
    q0[1] = 10; q0[3] = 255;
    q0[4] = 100; q0[5] = 10; q0[7] = 255;
    uint8_t* q1 = quad.row(1);
    q1[0] = 100; q1[1] = 10; q1[3] = 255;
    q1[4] = 200; q1[5] = 10; q1[7] = 255;
    LayerTransform half;
    half.origin = Point(0, 0);
    half.size = Size(1, 1);
    half.sampling = LayerSampling::High;
    Raster shrunk(1, 1);
    drawLayer(quad, half, 1, BlendMode::Normal, nullptr, nullptr, nullptr, downsamples, shrunk);
    checkPixel(shrunk, 0, 0, 100, 10, 0, 255, 0, "half size draws bilinear from the original");
}

void testStackRenderer() {
    const Uuid base = Uuid::generate();
    const Uuid child = Uuid::generate();
    auto source = [&base, &child](const Uuid& id) -> std::optional<Uuid> {
        return id == child ? std::optional<Uuid>(base) : std::nullopt;
    };
    auto drawOwn = [&base, &child](const Uuid& id, Raster& target, Point, const std::vector<FolderClip>*,
                                   const GrayImage*) {
        if (id == base) {
            // Half-transparent blue at the top-left only.
            uint8_t* p = target.row(0);
            p[2] = 128;
            p[3] = 128;
        } else if (id == child) {
            for (int y = 0; y < target.height; ++y) {
                uint8_t* p = target.row(y);
                for (int x = 0; x < target.width; ++x) {
                    p[x * 4] = 255;
                    p[x * 4 + 1] = 0;
                    p[x * 4 + 2] = 0;
                    p[x * 4 + 3] = 255;
                }
            }
        }
    };
    StackRenderer live(Rect(0, 0, 2, 2), source, drawOwn);
    live.prepareStacks({base, child}, [](const Uuid&) { return std::optional<Uuid>{}; },
                       [](const Uuid&) { return BlendMode::Normal; });

    Raster target(2, 2);
    live.drawComposite(base, target);
    checkPixel(target, 0, 0, 128, 0, 0, 128, 1, "the child's red shows only inside the base's alpha");
    checkPixel(target, 1, 0, 0, 0, 0, 0, 0, "outside the base's alpha the child vanishes");
    checkPixel(target, 0, 1, 0, 0, 0, 0, 0, "outside the base's alpha the child vanishes");
    checkPixel(target, 1, 1, 0, 0, 0, 0, 0, "outside the base's alpha the child vanishes");

    // Once drawn with its base, a stacked child draws nothing on its own.
    Raster second(2, 2);
    live.drawComposite(child, second);
    checkPixel(second, 0, 0, 0, 0, 0, 0, 0, "a stacked child is skipped on its own");
}

ProjectManifest documentManifest(int64_t width, int64_t height) {
    ProjectManifest manifest;
    manifest.version = 11;
    manifest.documentID = Uuid::generate();
    manifest.width = width;
    manifest.height = height;
    return manifest;
}

ProjectLayerRecord placedLayer(const Uuid& id, const std::string& name, double x, double y, double width,
                               double height) {
    ProjectLayerRecord layer;
    layer.id = id;
    layer.name = name;
    layer.transform.origin = Point(x, y);
    layer.transform.size = Size(width, height);
    layer.imageFile = id.toString() + ".png";
    return layer;
}

QImage solidImage(int width, int height, int r, int g, int b, int a) {
    QImage image(width, height, QImage::Format_RGBA8888);
    image.fill(QColor(r, g, b, a));
    return image;
}

QImage grayMaskImage(int width, int height, int value) {
    QImage image(width, height, QImage::Format_Grayscale8);
    image.fill(static_cast<uint>(value));
    return image;
}

void testDocumentRenderer() {
    // A 2x2 document with one layer covering it: pixel-for-pixel copy, the top row on top.
    {
        ProjectSnapshot snapshot;
        snapshot.manifest = documentManifest(2, 2);
        const Uuid id = Uuid::generate();
        snapshot.manifest.layers.push_back(placedLayer(id, "Base", 0, 0, 2, 2));
        QImage image(2, 2, QImage::Format_RGBA8888);
        image.setPixelColor(0, 0, QColor(255, 0, 0, 255));
        image.setPixelColor(1, 0, QColor(0, 255, 0, 255));
        image.setPixelColor(0, 1, QColor(0, 0, 255, 255));
        image.setPixelColor(1, 1, QColor(255, 255, 0, 255));
        snapshot.images[id] = ImportedImage{image, QImage(), "Base"};
        const Raster out = DocumentRenderer::render(snapshot);
        check(out.width == 2 && out.height == 2, "render keeps the canvas size");
        checkPixel(out, 0, 0, 255, 0, 0, 255, 0, "top-left is the image's top-left");
        checkPixel(out, 1, 0, 0, 255, 0, 255, 0, "top-right is the image's top-right");
        checkPixel(out, 0, 1, 0, 0, 255, 255, 0, "bottom-left is the image's bottom-left");
        checkPixel(out, 1, 1, 255, 255, 0, 255, 0, "bottom-right is the image's bottom-right");
    }
    // Half-opacity red over blue: the mix lands both channels on 128.
    {
        ProjectSnapshot snapshot;
        snapshot.manifest = documentManifest(1, 1);
        const Uuid blue = Uuid::generate();
        const Uuid red = Uuid::generate();
        snapshot.manifest.layers.push_back(placedLayer(blue, "Blue", 0, 0, 1, 1));
        ProjectLayerRecord top = placedLayer(red, "Red", 0, 0, 1, 1);
        top.opacity = 0.5;
        snapshot.manifest.layers.push_back(top);
        snapshot.images[blue] = ImportedImage{solidImage(1, 1, 0, 0, 255, 255), QImage(), "Blue"};
        snapshot.images[red] = ImportedImage{solidImage(1, 1, 255, 0, 0, 255), QImage(), "Red"};
        const Raster out = DocumentRenderer::render(snapshot);
        checkPixel(out, 0, 0, 128, 0, 128, 255, 0, "half red over blue mixes the two");
    }
    // Multiply: red multiplied onto blue is black.
    {
        ProjectSnapshot snapshot;
        snapshot.manifest = documentManifest(1, 1);
        const Uuid blue = Uuid::generate();
        const Uuid red = Uuid::generate();
        snapshot.manifest.layers.push_back(placedLayer(blue, "Blue", 0, 0, 1, 1));
        ProjectLayerRecord top = placedLayer(red, "Red", 0, 0, 1, 1);
        top.blendMode = BlendMode::Multiply;
        snapshot.manifest.layers.push_back(top);
        snapshot.images[blue] = ImportedImage{solidImage(1, 1, 0, 0, 255, 255), QImage(), "Blue"};
        snapshot.images[red] = ImportedImage{solidImage(1, 1, 255, 0, 0, 255), QImage(), "Red"};
        const Raster out = DocumentRenderer::render(snapshot);
        checkPixel(out, 0, 0, 0, 0, 0, 255, 0, "red multiplied onto blue is black");
    }
    // An adjustment layer renders nothing (M0): the red base stays visible.
    {
        ProjectSnapshot snapshot;
        snapshot.manifest = documentManifest(1, 1);
        const Uuid red = Uuid::generate();
        const Uuid adjust = Uuid::generate();
        snapshot.manifest.layers.push_back(placedLayer(red, "Red", 0, 0, 1, 1));
        ProjectLayerRecord top = placedLayer(adjust, "Adjust", 0, 0, 1, 1);
        top.adjustment = LayerAdjustment{};
        snapshot.manifest.layers.push_back(top);
        snapshot.images[red] = ImportedImage{solidImage(1, 1, 255, 0, 0, 255), QImage(), "Red"};
        snapshot.images[adjust] = ImportedImage{solidImage(1, 1, 0, 255, 0, 255), QImage(), "Adjust"};
        const Raster out = DocumentRenderer::render(snapshot);
        checkPixel(out, 0, 0, 255, 0, 0, 255, 0, "an adjustment layer paints nothing yet");
    }
    // A hidden layer is skipped.
    {
        ProjectSnapshot snapshot;
        snapshot.manifest = documentManifest(1, 1);
        const Uuid red = Uuid::generate();
        const Uuid hidden = Uuid::generate();
        snapshot.manifest.layers.push_back(placedLayer(red, "Red", 0, 0, 1, 1));
        ProjectLayerRecord top = placedLayer(hidden, "Hidden", 0, 0, 1, 1);
        top.isVisible = false;
        snapshot.manifest.layers.push_back(top);
        snapshot.images[red] = ImportedImage{solidImage(1, 1, 255, 0, 0, 255), QImage(), "Red"};
        snapshot.images[hidden] = ImportedImage{solidImage(1, 1, 0, 255, 0, 255), QImage(), "Hidden"};
        const Raster out = DocumentRenderer::render(snapshot);
        checkPixel(out, 0, 0, 255, 0, 0, 255, 0, "a hidden layer is skipped");
    }
    // A folder at 75% over a layer at 50%: 0.375 of white, both opacities multiplied.
    {
        ProjectSnapshot snapshot;
        snapshot.manifest = documentManifest(1, 1);
        const Uuid folder = Uuid::generate();
        const Uuid child = Uuid::generate();
        ProjectLayerRecord group;
        group.id = folder;
        group.name = "Folder";
        group.isGroup = true;
        group.opacity = 0.75;
        snapshot.manifest.layers.push_back(group);
        ProjectLayerRecord inner = placedLayer(child, "Child", 0, 0, 1, 1);
        inner.parentID = folder;
        inner.opacity = 0.5;
        snapshot.manifest.layers.push_back(inner);
        snapshot.images[child] = ImportedImage{solidImage(1, 1, 255, 255, 255, 255), QImage(), "Child"};
        const Raster out = DocumentRenderer::render(snapshot);
        checkPixel(out, 0, 0, 96, 96, 96, 96, 0, "folder and layer opacities multiply (0.75 * 0.5)");
    }
    // A hidden folder hides its contents.
    {
        ProjectSnapshot snapshot;
        snapshot.manifest = documentManifest(1, 1);
        const Uuid folder = Uuid::generate();
        const Uuid child = Uuid::generate();
        ProjectLayerRecord group;
        group.id = folder;
        group.name = "Folder";
        group.isGroup = true;
        group.isVisible = false;
        snapshot.manifest.layers.push_back(group);
        ProjectLayerRecord inner = placedLayer(child, "Child", 0, 0, 1, 1);
        inner.parentID = folder;
        snapshot.manifest.layers.push_back(inner);
        snapshot.images[child] = ImportedImage{solidImage(1, 1, 255, 255, 255, 255), QImage(), "Child"};
        const Raster out = DocumentRenderer::render(snapshot);
        checkPixel(out, 0, 0, 0, 0, 0, 0, 0, "a hidden folder hides its contents");
    }
    // A layer mask at 128/255 halves the alpha of a white pixel.
    {
        ProjectSnapshot snapshot;
        snapshot.manifest = documentManifest(1, 1);
        const Uuid id = Uuid::generate();
        ProjectLayerRecord layer = placedLayer(id, "Masked", 0, 0, 1, 1);
        layer.maskFile = id.toString() + ".mask.png";
        snapshot.manifest.layers.push_back(layer);
        snapshot.images[id] = ImportedImage{solidImage(1, 1, 255, 255, 255, 255), QImage(), "Masked"};
        snapshot.masks[id] = ImportedImage{grayMaskImage(1, 1, 128), QImage(), "Masked"};
        const Raster out = DocumentRenderer::render(snapshot);
        checkPixel(out, 0, 0, 128, 128, 128, 128, 0, "a half layer mask halves the pixel");
    }
}

void testRenderErrors() {
    {
        ProjectSnapshot snapshot;
        snapshot.manifest = documentManifest(0, 10);
        check(throwsRender([&] { DocumentRenderer::render(snapshot); }, RenderError::Kind::TooLarge),
              "zero width is too large");
    }
    {
        ProjectSnapshot snapshot;
        snapshot.manifest = documentManifest(30001, 1);
        check(throwsRender([&] { DocumentRenderer::render(snapshot); }, RenderError::Kind::TooLarge),
              "a side past 30,000 is too large");
    }
    {
        ProjectSnapshot snapshot;
        snapshot.manifest = documentManifest(20000, 10001);
        check(throwsRender([&] { DocumentRenderer::render(snapshot); }, RenderError::Kind::TooLarge),
              "past 200 megapixels is too large");
    }
    {
        std::string message;
        try {
            ProjectSnapshot snapshot;
            snapshot.manifest = documentManifest(30001, 1);
            DocumentRenderer::render(snapshot);
        } catch (const RenderError& error) {
            message = error.what();
        }
        check(message == "Image export supports canvases up to 200 megapixels and 30,000 pixels per side.",
              "the too-large message matches the original");
    }
    {
        ProjectSnapshot snapshot;
        snapshot.manifest = documentManifest(1, 1);
        snapshot.manifest.layers.push_back(placedLayer(Uuid::generate(), "Gone", 0, 0, 1, 1));
        check(throwsProject([&] { DocumentRenderer::render(snapshot); }, ProjectError::Kind::MissingImage),
              "a layer without pixels is missingImage");
    }
    {
        ProjectSnapshot snapshot;
        snapshot.manifest = documentManifest(1, 1);
        const Uuid id = Uuid::generate();
        ProjectLayerRecord layer = placedLayer(id, "GoneMask", 0, 0, 1, 1);
        layer.maskFile = id.toString() + ".mask.png";
        snapshot.manifest.layers.push_back(layer);
        snapshot.images[id] = ImportedImage{solidImage(1, 1, 0, 0, 0, 255), QImage(), "GoneMask"};
        check(throwsProject([&] { DocumentRenderer::render(snapshot); }, ProjectError::Kind::MissingImage),
              "a mask without pixels is missingImage");
    }
    {
        ProjectSnapshot snapshot;
        snapshot.manifest = documentManifest(1, 1);
        const Uuid a = Uuid::generate();
        const Uuid b = Uuid::generate();
        snapshot.manifest.layers.push_back(placedLayer(a, "A", 0, 0, 1, 1));
        ProjectLayerRecord chain = placedLayer(b, "B", 0, 0, 1, 1);
        chain.maskSourceID = Uuid::generate();
        snapshot.manifest.layers.push_back(chain);
        snapshot.images[a] = ImportedImage{solidImage(1, 1, 0, 0, 0, 255), QImage(), "A"};
        snapshot.images[b] = ImportedImage{solidImage(1, 1, 0, 0, 0, 255), QImage(), "B"};
        check(throwsProject([&] { DocumentRenderer::render(snapshot); }, ProjectError::Kind::Invalid),
              "a clipping chain pointing nowhere is invalid");
    }
}

}  // namespace

int main() {
    testHalveRaster();
    testHalveGray();
    testDownsampleLevel();
    testInterpolation();
    testSampling();
    testAlphaOps();
    testTransformRoundTrip();
    testDownsampleStore();
    testFolderClipFactor();
    testMaskBackground();
    testClipImage();
    testDrawLayer();
    testStackRenderer();
    testDocumentRenderer();
    testRenderErrors();
    if (failures == 0) std::printf("test_renderer: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
