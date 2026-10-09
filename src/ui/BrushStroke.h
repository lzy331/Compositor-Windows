#pragma once

// The port of BrushStroke.swift's deposition: evenly spaced dabs along a centripetal
// Catmull–Rom curve through the mouse samples, accumulated into one coverage plane so a
// stroke's overlapping dabs never exceed its opacity. It paints in the layer's own pixel
// grid, which grows to hold the canvas where the layer's pixels don't reach it.

#include <QImage>

#include <optional>
#include <vector>

#include "core/Types.h"
#include "document/Manifest.h"
#include "render/Geometry.h"
#include "ui/EditorSession.h"

namespace compositor {

class BrushStroke {
public:
    struct Result {
        QImage image;
        LayerTransform transform;
    };

    /// `image`: the layer's current pixels, or a null image for a blank layer. `transform`:
    /// the layer's placement, so the stroke paints 1:1 in its own grid.
    BrushStroke(const QImage& image, const LayerTransform& transform, const Size& canvas,
                const EditorSession::BrushSettings& settings);

    /// Lays dabs up to this document-space sample, replacing the provisional straight tail.
    void append(const Point& point);
    /// Turns the provisional tail into the final curve piece.
    void flush();

    /// The layer's pixels mid-stroke: the whole grid, transparent where untouched.
    const QImage& image() const { return output_; }
    /// Where that grid sits on the document.
    LayerTransform gridTransform() const;
    /// The finished pixels and placement, cropped past any untouched border.
    Result commit() const;
    bool painted() const { return painted_; }

private:
    void curve(const Point& start, const Point& end, const Point& before, const Point& after);
    void walk(const Point& point);
    void dab(const Point& point);
    void drawTail(const Point& start, const Point& end);
    void restoreTail();
    void recompose(const QRect& rect);
    double tip(double distance, double radius) const;
    LayerTransform transformFor(const Rect& bounds) const;

    EditorSession::BrushSettings settings_;
    Size canvas_;
    render::Affine mapping_;  // grid pixel -> document
    double gridStepX_ = 1;    // document units per grid pixel, x
    double gridStepY_ = 1;    // document units per grid pixel, y
    LayerTransform source_;
    int width_ = 0;
    int height_ = 0;
    int originalWidth_ = 1;
    int originalHeight_ = 1;
    Rect sourceRect_;
    bool hasSource_ = false;
    std::vector<float> coverage_;
    QImage base_;
    QImage output_;
    bool painted_ = false;
    std::optional<Point> previous_;
    std::vector<Point> samples_;
    double distanceToNext_ = 0;

    struct Tail {
        QRect box;
        std::vector<float> coverage;
        std::optional<Point> previous;
        double distanceToNext = 0;
    };
    std::optional<Tail> tail_;
};

}  // namespace compositor
