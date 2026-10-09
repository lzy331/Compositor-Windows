#pragma once

#include <algorithm>
#include <cmath>

#include "document/Manifest.h"

namespace compositor::render {

// A 2D affine transform, matching CGContext's convention:
//   x' = a*x + c*y + tx
//   y' = b*x + d*y + ty
struct Affine {
    double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;

    [[nodiscard]] Point apply(double x, double y) const { return Point{x * a + y * c + tx, x * b + y * d + ty}; }
    [[nodiscard]] Point apply(const Point& p) const { return apply(p.x, p.y); }
};

inline constexpr double kSqrt2 = 1.4142135623730951;

// The layer's local (y-up, centered) space to document space (y-down, pixel units),
// exactly the placement CGContext gets in LayerRenderer.draw:
// translate(center) * rotate(radians) * scale(flipX ? -1 : 1, flipY ? 1 : -1).
// Composition (fx = flipX ? -1 : +1, fy = flipY ? +1 : -1):
//   a = fx*cos, b = fx*sin, c = -fy*sin, d = fy*cos, tx = cx, ty = cy
inline Affine layerLocalToDocument(const LayerTransform& t) {
    const double c = std::cos(t.radians()), s = std::sin(t.radians());
    const double fx = t.flipX ? -1.0 : 1.0, fy = t.flipY ? 1.0 : -1.0;
    const Point center = t.center();
    return Affine{fx * c, fx * s, -fy * s, fy * c, center.x, center.y};
}

// The exact inverse of layerLocalToDocument (det = fx*fy = +-1, a rigid motion).
inline Affine layerDocumentToLocal(const LayerTransform& t) {
    const double c = std::cos(t.radians()), s = std::sin(t.radians());
    const double fx = t.flipX ? -1.0 : 1.0, fy = t.flipY ? 1.0 : -1.0;
    const Point center = t.center();
    return Affine{fx * c, -fy * s, fx * s, fy * c,
                  -fx * (c * center.x + s * center.y), fy * (s * center.x - c * center.y)};
}

struct Quad {
    Point p[4];
};

// Antialiased coverage of a document-space unit square (whose four corners, mapped
// into coverage space, are `corners`) against the axis-aligned rectangle `rect`:
// Sutherland-Hodgman clipping, then |shoelace| area over the pixel's own area.
inline double quadCoverage(const Quad& corners, const Rect& rect, double pixelArea) {
    Point out[8];
    int count = 4;
    for (int i = 0; i < 4; ++i) out[i] = corners.p[i];

    auto clipEdge = [&](auto inside, auto intersect) {
        Point next[8];
        int n = 0;
        for (int i = 0; i < count; ++i) {
            const Point cur = out[i], prev = out[(i + count - 1) % count];
            const bool curIn = inside(cur), prevIn = inside(prev);
            if (curIn) {
                if (!prevIn) next[n++] = intersect(prev, cur);
                next[n++] = cur;
            } else if (prevIn) {
                next[n++] = intersect(prev, cur);
            }
        }
        for (int i = 0; i < n; ++i) out[i] = next[i];
        count = n;
    };
    auto lerpTo = [](const Point& from, const Point& to, double t) {
        return Point{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t};
    };
    clipEdge([&](const Point& p) { return p.x >= rect.minX(); },
             [&](const Point& p, const Point& q) { return lerpTo(p, q, (rect.minX() - p.x) / (q.x - p.x)); });
    if (count == 0) return 0;
    clipEdge([&](const Point& p) { return p.x <= rect.maxX(); },
             [&](const Point& p, const Point& q) { return lerpTo(p, q, (rect.maxX() - p.x) / (q.x - p.x)); });
    if (count == 0) return 0;
    clipEdge([&](const Point& p) { return p.y >= rect.minY(); },
             [&](const Point& p, const Point& q) { return lerpTo(p, q, (rect.minY() - p.y) / (q.y - p.y)); });
    if (count == 0) return 0;
    clipEdge([&](const Point& p) { return p.y <= rect.maxY(); },
             [&](const Point& p, const Point& q) { return lerpTo(p, q, (rect.maxY() - p.y) / (q.y - p.y)); });
    if (count == 0) return 0;

    double area = 0;
    for (int i = 0; i < count; ++i) {
        const Point& p = out[i];
        const Point& q = out[(i + count + 1) % count];
        area += p.x * q.y - q.x * p.y;
    }
    area = std::abs(area) / 2;
    return std::clamp(area / pixelArea, 0.0, 1.0);
}

// Coverage of the document-space pixel centered at `pixelCenter` (extent `2*halfExtent`
// in document units) against `rect`, both expressed in the source space `docToSource`
// maps into: corners are mapped through `docToSource`, `rect` is the source rectangle
// (the y-up space the quad covers). Fast paths for centers well inside or outside; the
// rest clips the mapped pixel square against `rect` (Sutherland-Hodgman).
inline double pixelCoverage(const Affine& docToSource, const Point& pixelCenter, const Rect& rect,
                            double halfExtent, bool antialias) {
    const Point q = docToSource.apply(pixelCenter);
    const double edge = halfExtent * kSqrt2;
    const double d = std::min(std::min(q.x - rect.minX(), rect.maxX() - q.x), std::min(q.y - rect.minY(), rect.maxY() - q.y));
    if (d >= edge) return 1;
    if (d <= -edge) return 0;
    if (!antialias) return d >= 0 ? 1 : 0;
    const Quad corners{{docToSource.apply(pixelCenter.x - halfExtent, pixelCenter.y - halfExtent),
                        docToSource.apply(pixelCenter.x + halfExtent, pixelCenter.y - halfExtent),
                        docToSource.apply(pixelCenter.x + halfExtent, pixelCenter.y + halfExtent),
                        docToSource.apply(pixelCenter.x - halfExtent, pixelCenter.y + halfExtent)}};
    return quadCoverage(corners, rect, 2 * halfExtent * (2 * halfExtent));
}

}  // namespace compositor::render
