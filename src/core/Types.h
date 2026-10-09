#pragma once

// Basic geometry, identity and document limits shared by the whole port.
// Mirrors the CoreGraphics-era types the macOS original used (LayerTransform, DocumentLimits).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <random>
#include <string>

namespace compositor {

struct Point {
    double x = 0;
    double y = 0;

    Point() = default;
    Point(double x, double y) : x(x), y(y) {}

    Point operator+(const Point& other) const { return {x + other.x, y + other.y}; }
    Point operator-(const Point& other) const { return {x - other.x, y - other.y}; }
    bool operator==(const Point& other) const { return x == other.x && y == other.y; }
};

struct Size {
    double width = 0;
    double height = 0;

    Size() = default;
    Size(double w, double h) : width(w), height(h) {}

    bool operator==(const Size& other) const { return width == other.width && height == other.height; }
};

struct Rect {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;

    Rect() = default;
    Rect(double x, double y, double w, double h) : x(x), y(y), width(w), height(h) {}

    double minX() const { return x; }
    double minY() const { return y; }
    double maxX() const { return x + width; }
    double maxY() const { return y + height; }
    double midX() const { return x + width / 2; }
    double midY() const { return y + height / 2; }

    bool isEmpty() const { return width <= 0 || height <= 0; }
    bool intersects(const Rect& other) const {
        return !(other.minX() >= maxX() || other.maxX() <= minX() || other.minY() >= maxY() || other.maxY() <= minY());
    }
    Rect intersected(const Rect& other) const {
        double x0 = std::max(minX(), other.minX());
        double y0 = std::max(minY(), other.minY());
        double x1 = std::min(maxX(), other.maxX());
        double y1 = std::min(maxY(), other.maxY());
        if (x1 <= x0 || y1 <= y0) return Rect();
        return Rect(x0, y0, x1 - x0, y1 - y0);
    }
    bool contains(const Point& p) const {
        return p.x >= minX() && p.x <= maxX() && p.y >= minY() && p.y <= maxY();
    }
    bool operator==(const Rect& other) const {
        return x == other.x && y == other.y && width == other.width && height == other.height;
    }
};

/// A version-4 UUID. Serialized the way Swift's `UUID.uuidString` does: uppercase, hyphenated.
struct Uuid {
    std::array<uint8_t, 16> bytes{};

    Uuid() = default;

    static Uuid generate();
    static std::optional<Uuid> parse(const std::string& text);
    std::string toString() const;

    bool operator==(const Uuid& other) const { return bytes == other.bytes; }
    bool operator!=(const Uuid& other) const { return !(*this == other); }
    bool operator<(const Uuid& other) const { return bytes < other.bytes; }
    bool isNull() const {
        for (uint8_t b : bytes)
            if (b != 0) return false;
        return true;
    }
};

struct UuidHash {
    size_t operator()(const Uuid& id) const noexcept {
        // FNV-1a over the 16 bytes.
        size_t h = 1469598103934665603ull;
        for (uint8_t b : id.bytes) {
            h ^= b;
            h *= 1099511628211ull;
        }
        return h;
    }
};

/// The size and memory ceilings a document is held to, one place, as in the original.
struct DocumentLimits {
    /// Longest side, in pixels, of any canvas, layer, mask or generated surface.
    static constexpr int maxSide = 30000;
    /// Largest single surface.
    static constexpr int64_t maxSurfacePixels = 200'000'000;
    /// Total imported raster one document may hold, scaled to the machine's memory.
    static int64_t documentPixelBudget();
    static int documentBudgetMegapixels() { return static_cast<int>(documentPixelBudget() / 1'000'000); }
};

/// Physical memory in bytes, used for the document budget (macOS: ProcessInfo.physicalMemory / 16).
uint64_t physicalMemoryBytes();

/// Swift's JSONEncoder writes integral Doubles without a decimal part (`1`, not `1.0`), and the
/// project manifest is compared and re-read by the original app, so numbers follow the same rule.
inline bool isIntegral(double value) {
    return std::isfinite(value) && value == std::floor(value) && std::abs(value) < 9.007199254740992e15;
}

}  // namespace compositor
