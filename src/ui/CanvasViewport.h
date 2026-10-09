#pragma once

// A port of CanvasViewport.swift: the canvas's zoom/pan state. Document: pixels with a
// top-left origin. View: widget points. Zoom 1 shows one document pixel per device pixel.

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "core/Types.h"

namespace compositor {

struct CanvasViewport {
    Size viewSize;
    double backingScale = 1;
    double zoomValue = 1;
    Size pan;
    bool followsFitValue = true;

    double zoom() const { return zoomValue; }
    bool followsFit() const { return followsFitValue; }

    static constexpr double minZoom = 0.001;
    static constexpr double maxZoom = 32;

    static const std::vector<double>& keyboardZoomLevels() {
        static const std::vector<double> levels = {0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1,
                                                   1.25, 1.5, 2, 3, 4, 5, 6, 8, 12, 16};
        return levels;
    }

    double pointsPerPixel() const { return zoomValue / backingScale; }
    Point center() const { return {viewSize.width / 2, viewSize.height / 2}; }

    Rect documentRect(const Size& size) const {
        const double ppp = pointsPerPixel();
        const Size scaled(size.width * ppp, size.height * ppp);
        const Point c = center();
        return Rect(c.x - scaled.width / 2 + pan.width, c.y - scaled.height / 2 + pan.height, scaled.width,
                    scaled.height);
    }

    Point documentPoint(const Point& point, const Size& documentSize) const {
        const Rect rect = documentRect(documentSize);
        const double ppp = pointsPerPixel();
        return {(point.x - rect.x) / ppp, (point.y - rect.y) / ppp};
    }

    Point viewPoint(const Point& point, const Size& documentSize) const {
        const Rect rect = documentRect(documentSize);
        const double ppp = pointsPerPixel();
        return {rect.x + point.x * ppp, rect.y + point.y * ppp};
    }

    void fit(const Size& documentSize) {
        if (!(viewSize.width > 0) || !(viewSize.height > 0)) {
            followsFitValue = true;
            return;
        }
        zoomValue = clampZoom(std::min(std::max(1.0, viewSize.width - 96) / documentSize.width,
                                       std::max(1.0, viewSize.height - 96) / documentSize.height)
                              * backingScale);
        pan = Size();
        followsFitValue = true;
    }

    /// Preserve the center document point when the widget (or its display scale) changes.
    void resize(const Size& size, double newBackingScale, const std::optional<Size>& documentSize) {
        const double oldScale = pointsPerPixel();
        viewSize = size;
        backingScale = std::max(1.0, newBackingScale);
        if (followsFitValue && documentSize) {
            fit(*documentSize);
        } else if (oldScale > 0) {
            const double ratio = pointsPerPixel() / oldScale;
            pan = Size(pan.width * ratio, pan.height * ratio);
        }
    }

    void setZoom(double value, const Point& anchor, const Size& documentSize) {
        if (!std::isfinite(value)) return;
        const Point pixel = documentPoint(anchor, documentSize);
        zoomValue = clampZoom(value);
        const Point moved = viewPoint(pixel, documentSize);
        pan.width += anchor.x - moved.x;
        pan.height += anchor.y - moved.y;
        followsFitValue = false;
    }

    double keyboardZoomTarget(int step) const {
        if (step == 0) return zoomValue;
        const double tolerance = std::max(1e-9, std::abs(zoomValue) * 1e-9);
        if (step > 0) {
            for (double level : keyboardZoomLevels()) {
                if (level > zoomValue + tolerance) return level;
            }
            return zoomValue;
        }
        for (auto it = keyboardZoomLevels().rbegin(); it != keyboardZoomLevels().rend(); ++it) {
            if (*it < zoomValue - tolerance) return *it;
        }
        return zoomValue;
    }

    void translate(const Size& delta) {
        pan.width += delta.width;
        pan.height += delta.height;
        followsFitValue = false;
    }

private:
    static double clampZoom(double value) { return std::min(maxZoom, std::max(minZoom, value)); }
};

}  // namespace compositor
