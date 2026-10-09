#include "ui/BrushStroke.h"

#include <algorithm>
#include <cmath>

namespace compositor {
namespace {

constexpr double kFalloffK = 2.5;

render::Affine invert(const render::Affine& t) {
    const double det = t.a * t.d - t.b * t.c;
    if (std::abs(det) < 1e-12) return render::Affine{};
    const double ia = t.d / det, ib = -t.b / det, ic = -t.c / det, id = t.a / det;
    return render::Affine{ia, ib, ic, id, -(ia * t.tx + ic * t.ty), -(ib * t.tx + id * t.ty)};
}

// The layer's own pixels (y-down, top-left origin) mapped onto the document, as
// LayerRenderer places them: translate(center) · rotate · scale(size / original).
render::Affine pixelToDocument(const LayerTransform& transform, int originalWidth, int originalHeight) {
    const double c = std::cos(transform.radians()), s = std::sin(transform.radians());
    const double sx = transform.size.width / originalWidth * (transform.flipX ? -1 : 1);
    const double sy = transform.size.height / originalHeight * (transform.flipY ? -1 : 1);
    const double a = sx * c, b = sx * s, cx = -sy * s, d = sy * c;
    const Point center = transform.center();
    return render::Affine{a, b, cx, d, center.x - (a * originalWidth + cx * originalHeight) / 2,
                          center.y - (b * originalWidth + d * originalHeight) / 2};
}

Rect bboxOfCorners(const render::Affine& map, const Rect& rect) {
    const Point corners[4] = {map.apply(rect.minX(), rect.minY()), map.apply(rect.maxX(), rect.minY()),
                              map.apply(rect.maxX(), rect.maxY()), map.apply(rect.minX(), rect.maxY())};
    double minX = corners[0].x, minY = corners[0].y, maxX = corners[0].x, maxY = corners[0].y;
    for (const Point& p : corners) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }
    return Rect(minX, minY, maxX - minX, maxY - minY);
}

Rect unionRect(const Rect& lhs, const Rect& rhs) {
    if (lhs.isEmpty()) return rhs;
    if (rhs.isEmpty()) return lhs;
    const double minX = std::min(lhs.minX(), rhs.minX()), minY = std::min(lhs.minY(), rhs.minY());
    const double maxX = std::max(lhs.maxX(), rhs.maxX()), maxY = std::max(lhs.maxY(), rhs.maxY());
    return Rect(minX, minY, maxX - minX, maxY - minY);
}

}  // namespace

BrushStroke::BrushStroke(const QImage& image, const LayerTransform& transform, const Size& canvas,
                         const EditorSession::BrushSettings& settings)
    : settings_(settings), canvas_(canvas), source_(transform), hasSource_(!image.isNull()) {
    originalWidth_ = hasSource_ ? image.width() : std::max(1, static_cast<int>(std::lround(transform.size.width)));
    originalHeight_ = hasSource_ ? image.height() : std::max(1, static_cast<int>(std::lround(transform.size.height)));
    const render::Affine original = pixelToDocument(transform, originalWidth_, originalHeight_);
    const render::Affine originalInverse = invert(original);

    // The grid holds the layer's own pixels plus wherever the canvas reaches, so a brush can
    // paint on a layer smaller than the canvas (and the layer grows to hold the new pixels).
    const Rect originalBounds(0, 0, originalWidth_, originalHeight_);
    const Rect canvasInPixels = bboxOfCorners(originalInverse, Rect(0, 0, canvas.width, canvas.height));
    const double minX = std::min(originalBounds.minX(), std::floor(canvasInPixels.minX()));
    const double minY = std::min(originalBounds.minY(), std::floor(canvasInPixels.minY()));
    const double maxX = std::max(originalBounds.maxX(), std::ceil(canvasInPixels.maxX()));
    const double maxY = std::max(originalBounds.maxY(), std::ceil(canvasInPixels.maxY()));
    width_ = std::max(1, static_cast<int>(std::lround(maxX - minX)));
    height_ = std::max(1, static_cast<int>(std::lround(maxY - minY)));
    sourceRect_ = Rect(-minX, -minY, originalWidth_, originalHeight_);

    // mapping_ = original ∘ translate(minX, minY): one grid pixel per layer pixel.
    mapping_ = render::Affine{original.a, original.b, original.c, original.d,
                              original.tx + original.a * minX + original.c * minY,
                              original.ty + original.b * minX + original.d * minY};
    gridStepX_ = std::hypot(mapping_.a, mapping_.b);
    gridStepY_ = std::hypot(mapping_.c, mapping_.d);

    coverage_.assign(static_cast<size_t>(width_) * height_, 0.0f);
    base_ = QImage(width_, height_, QImage::Format_RGBA8888_Premultiplied);
    base_.fill(Qt::transparent);
    if (hasSource_) {
        const QImage pixels = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        const int x0 = static_cast<int>(std::lround(sourceRect_.x));
        const int y0 = static_cast<int>(std::lround(sourceRect_.y));
        for (int y = 0; y < pixels.height(); ++y) {
            const int ty = y0 + y;
            if (ty < 0 || ty >= height_) continue;
            const QRgb* src = reinterpret_cast<const QRgb*>(pixels.constScanLine(y));
            QRgb* dst = reinterpret_cast<QRgb*>(base_.scanLine(ty));
            for (int x = 0; x < pixels.width(); ++x) {
                const int tx = x0 + x;
                if (tx >= 0 && tx < width_) dst[tx] = src[x];
            }
        }
    }
    output_ = base_.copy();
}

double BrushStroke::tip(double distance, double radius) const {
    if (radius <= 0) return 0;
    const double hardness = std::clamp(settings_.hardness, 0.0, 1.0);
    if (hardness >= 1.0) return std::clamp(radius + 0.5 - distance, 0.0, 1.0);
    const double inner = radius * hardness;
    if (distance <= inner) return 1;
    if (distance >= radius) return 0;
    const double u = (distance - inner) / (radius - inner);
    return std::clamp((std::exp(-kFalloffK * u * u) - std::exp(-kFalloffK)) / (1 - std::exp(-kFalloffK)), 0.0, 1.0);
}

void BrushStroke::recompose(const QRect& rect) {
    const QRect area = rect.intersected(QRect(0, 0, width_, height_));
    if (area.isEmpty()) return;
    const double opacity = std::clamp(settings_.opacity, 0.0, 1.0);
    const double red = std::clamp(settings_.red, 0.0, 1.0);
    const double green = std::clamp(settings_.green, 0.0, 1.0);
    const double blue = std::clamp(settings_.blue, 0.0, 1.0);
    for (int y = area.top(); y <= area.bottom(); ++y) {
        uint8_t* out = output_.scanLine(y);
        const uint8_t* base = base_.constScanLine(y);
        for (int x = area.left(); x <= area.right(); ++x) {
            const double alpha = std::clamp(static_cast<double>(coverage_[static_cast<size_t>(y) * width_ + x]) * opacity, 0.0, 1.0);
            uint8_t* o = out + x * 4;
            const uint8_t* b = base + x * 4;
            if (settings_.erasing) {
                const double keep = 1 - alpha;
                o[0] = static_cast<uint8_t>(std::lround(b[0] * keep));
                o[1] = static_cast<uint8_t>(std::lround(b[1] * keep));
                o[2] = static_cast<uint8_t>(std::lround(b[2] * keep));
                o[3] = static_cast<uint8_t>(std::lround(b[3] * keep));
            } else {
                const double sr = red * alpha + b[0] / 255.0 * (1 - alpha);
                const double sg = green * alpha + b[1] / 255.0 * (1 - alpha);
                const double sb = blue * alpha + b[2] / 255.0 * (1 - alpha);
                const double sa = alpha + b[3] / 255.0 * (1 - alpha);
                o[0] = static_cast<uint8_t>(std::lround(std::clamp(sr, 0.0, 1.0) * 255));
                o[1] = static_cast<uint8_t>(std::lround(std::clamp(sg, 0.0, 1.0) * 255));
                o[2] = static_cast<uint8_t>(std::lround(std::clamp(sb, 0.0, 1.0) * 255));
                o[3] = static_cast<uint8_t>(std::lround(std::clamp(sa, 0.0, 1.0) * 255));
            }
        }
    }
}

void BrushStroke::dab(const Point& point) {
    const double radius = settings_.diameter / 2;
    const Rect circle(point.x - radius, point.y - radius, radius * 2, radius * 2);
    const Rect clipped = circle.intersected(Rect(0, 0, canvas_.width, canvas_.height));
    if (clipped.isEmpty()) return;

    const render::Affine inverse = invert(mapping_);
    // One grid pixel past the dab's reach, so a rotated grid still catches every pixel.
    const Rect reach(point.x - radius - gridStepX_, point.y - radius - gridStepY_, (radius + gridStepX_) * 2,
                     (radius + gridStepY_) * 2);
    const Rect affected = bboxOfCorners(inverse, reach.intersected(Rect(0, 0, canvas_.width, canvas_.height)))
                              .intersected(Rect(0, 0, width_, height_));
    if (affected.isEmpty()) return;
    const QRect area(static_cast<int>(std::floor(affected.minX())), static_cast<int>(std::floor(affected.minY())),
                     std::max(1, static_cast<int>(std::ceil(affected.maxX())) - static_cast<int>(std::floor(affected.minX()))),
                     std::max(1, static_cast<int>(std::ceil(affected.maxY())) - static_cast<int>(std::floor(affected.minY()))));
    const QRect bounded = area.intersected(QRect(0, 0, width_, height_));
    if (bounded.isEmpty()) return;

    const bool hard = settings_.hardness >= 1.0;
    for (int y = bounded.top(); y <= bounded.bottom(); ++y) {
        for (int x = bounded.left(); x <= bounded.right(); ++x) {
            const Point center = mapping_.apply(x + 0.5, y + 0.5);
            const double sample = tip(std::hypot(center.x - point.x, center.y - point.y), radius);
            if (sample <= 0) continue;
            float& value = coverage_[static_cast<size_t>(y) * width_ + x];
            value = hard ? std::max(value, static_cast<float>(sample))
                         : value + static_cast<float>(sample) - value * static_cast<float>(sample);
        }
    }
    painted_ = true;
    recompose(bounded);
}

void BrushStroke::walk(const Point& point) {
    const double spacing = std::max(0.25, settings_.diameter * (settings_.hardness >= 1 ? 0.015 : 0.025));
    if (previous_) {
        const double dx = point.x - previous_->x, dy = point.y - previous_->y;
        const double length = std::hypot(dx, dy);
        if (length > 0) {
            double distance = distanceToNext_;
            while (distance <= length) {
                dab(Point(previous_->x + dx * distance / length, previous_->y + dy * distance / length));
                distance += spacing;
            }
            distanceToNext_ = distance - length;
        }
    } else {
        dab(point);
        distanceToNext_ = spacing;
    }
    previous_ = point;
}

void BrushStroke::curve(const Point& start, const Point& end, const Point& before, const Point& after) {
    auto knot = [](double t, const Point& a, const Point& b) {
        return t + std::max(0.0001, std::sqrt(std::hypot(b.x - a.x, b.y - a.y)));
    };
    auto mix = [](const Point& a, const Point& b, double ta, double tb, double t) {
        const double wa = (tb - t) / (tb - ta), wb = (t - ta) / (tb - ta);
        return Point(a.x * wa + b.x * wb, a.y * wa + b.y * wb);
    };
    const double t0 = 0, t1 = knot(t0, before, start), t2 = knot(t1, start, end), t3 = knot(t2, end, after);
    const int pieces = std::max(1, static_cast<int>(std::ceil(std::hypot(end.x - start.x, end.y - start.y) / 2)));
    for (int index = 1; index <= pieces; ++index) {
        const double t = t1 + (t2 - t1) * index / pieces;
        const Point a1 = mix(before, start, t0, t1, t), a2 = mix(start, end, t1, t2, t), a3 = mix(end, after, t2, t3, t);
        const Point b1 = mix(a1, a2, t0, t2, t), b2 = mix(a2, a3, t1, t3, t);
        walk(index == pieces ? end : mix(b1, b2, t1, t2, t));
    }
}

void BrushStroke::drawTail(const Point& start, const Point& end) {
    const double reach = settings_.diameter / 2 + 2;
    const Rect box(std::min(start.x, end.x) - reach, std::min(start.y, end.y) - reach,
                   std::abs(end.x - start.x) + reach * 2, std::abs(end.y - start.y) + reach * 2);
    const Rect clipped = box.intersected(Rect(0, 0, canvas_.width, canvas_.height));
    Tail tail;
    tail.previous = previous_;
    tail.distanceToNext = distanceToNext_;
    if (!clipped.isEmpty()) {
        const render::Affine inverse = invert(mapping_);
        const Rect affected = bboxOfCorners(inverse, clipped).intersected(Rect(0, 0, width_, height_));
        if (!affected.isEmpty()) {
            const int x0 = std::max(0, static_cast<int>(std::floor(affected.minX())));
            const int y0 = std::max(0, static_cast<int>(std::floor(affected.minY())));
            const int x1 = std::min(width_, static_cast<int>(std::ceil(affected.maxX())));
            const int y1 = std::min(height_, static_cast<int>(std::ceil(affected.maxY())));
            tail.box = QRect(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));
            tail.coverage.resize(static_cast<size_t>(tail.box.width()) * tail.box.height());
            for (int y = 0; y < tail.box.height(); ++y) {
                for (int x = 0; x < tail.box.width(); ++x) {
                    tail.coverage[static_cast<size_t>(y) * tail.box.width() + x] =
                        coverage_[static_cast<size_t>(y + y0) * width_ + (x + x0)];
                }
            }
        }
    }
    walk(end);
    previous_ = tail.previous;
    distanceToNext_ = tail.distanceToNext;
    tail_ = std::move(tail);
}

void BrushStroke::restoreTail() {
    if (!tail_) return;
    if (tail_->box.width() > 0 && tail_->box.height() > 0) {
        for (int y = 0; y < tail_->box.height(); ++y) {
            for (int x = 0; x < tail_->box.width(); ++x) {
                coverage_[static_cast<size_t>(y + tail_->box.top()) * width_ + (tail_->box.left() + x)] =
                    tail_->coverage[static_cast<size_t>(y) * tail_->box.width() + x];
            }
        }
        recompose(tail_->box);
    }
    tail_.reset();
}

void BrushStroke::append(const Point& point) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x) > 1e7 || std::abs(point.y) > 1e7) return;
    if (!samples_.empty() && samples_.back() == point) return;
    restoreTail();
    samples_.push_back(point);
    if (samples_.size() > 4) samples_.erase(samples_.begin());
    const int count = static_cast<int>(samples_.size());
    if (count == 1) {
        walk(point);
    } else if (count >= 3) {
        curve(samples_[count - 3], samples_[count - 2], samples_[std::max(0, count - 4)], samples_[count - 1]);
    }
    if (count >= 2) drawTail(samples_[count - 2], point);
}

void BrushStroke::flush() {
    restoreTail();
    const int count = static_cast<int>(samples_.size());
    if (count >= 2) {
        curve(samples_[count - 2], samples_[count - 1], samples_[std::max(0, count - 3)], samples_[count - 1]);
        samples_.assign(1, samples_[count - 1]);
    }
}

LayerTransform BrushStroke::gridTransform() const {
    LayerTransform result = source_;
    result.size = Size(width_ * source_.size.width / originalWidth_, height_ * source_.size.height / originalHeight_);
    const Point center = mapping_.apply(width_ / 2.0, height_ / 2.0);
    result.origin = Point(center.x - result.size.width / 2, center.y - result.size.height / 2);
    return result;
}

LayerTransform BrushStroke::transformFor(const Rect& bounds) const {
    const LayerTransform whole = gridTransform();
    LayerTransform result = whole;
    result.size = Size(bounds.width * whole.size.width / width_, bounds.height * whole.size.height / height_);
    const Point center = mapping_.apply(bounds.midX(), bounds.midY());
    result.origin = Point(center.x - result.size.width / 2, center.y - result.size.height / 2);
    return result;
}

BrushStroke::Result BrushStroke::commit() const {
    Rect bounds = hasSource_ ? sourceRect_ : Rect();
    int left = width_, top = height_, right = 0, bottom = 0;
    for (int y = 0; y < height_; ++y) {
        const uint8_t* row = output_.constScanLine(y);
        for (int x = 0; x < width_; ++x) {
            if (row[x * 4 + 3] == 0) continue;
            left = std::min(left, x);
            right = std::max(right, x + 1);
            top = std::min(top, y);
            bottom = std::max(bottom, y + 1);
        }
    }
    if (right > left && bottom > top) bounds = unionRect(bounds, Rect(left, top, right - left, bottom - top));
    if (bounds.isEmpty()) bounds = Rect(0, 0, width_, height_);
    const int x0 = std::max(0, static_cast<int>(std::floor(bounds.minX())));
    const int y0 = std::max(0, static_cast<int>(std::floor(bounds.minY())));
    const int x1 = std::min(width_, static_cast<int>(std::ceil(bounds.maxX())));
    const int y1 = std::min(height_, static_cast<int>(std::ceil(bounds.maxY())));
    const QRect bounded(x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0));
    Result result;
    result.image = output_.copy(bounded);
    result.transform = transformFor(Rect(bounded.x(), bounded.y(), bounded.width(), bounded.height()));
    return result;
}

}  // namespace compositor
