#include "io/ImageAssets.h"

#include <QBuffer>
#include <QImageReader>
#include <QImageWriter>

#include <algorithm>
#include <cstring>
#include <memory>

namespace compositor {

namespace {

uint32_t bigEndian32(const char* p) {
    return (static_cast<uint32_t>(static_cast<unsigned char>(p[0])) << 24) |
           (static_cast<uint32_t>(static_cast<unsigned char>(p[1])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(p[2])) << 8) |
           static_cast<uint32_t>(static_cast<unsigned char>(p[3]));
}

// QBuffer over bytes we only ever read; QImageReader must see a seekable device.
QBuffer* readingBuffer(const QByteArray& bytes) {
    QBuffer* buffer = new QBuffer(const_cast<QByteArray*>(&bytes));
    buffer->open(QIODevice::ReadOnly);
    return buffer;
}

bool validColorType(int colorType) {
    return colorType == 0 || colorType == 2 || colorType == 3 || colorType == 4 || colorType == 6;
}

}  // namespace

std::optional<PngProbe> probePNG(const QByteArray& bytes) {
    static const unsigned char signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (bytes.size() < 26 || std::memcmp(bytes.constData(), signature, 8) != 0) return std::nullopt;
    if (std::memcmp(bytes.constData() + 12, "IHDR", 4) != 0) return std::nullopt;

    PngProbe probe;
    probe.width = bigEndian32(bytes.constData() + 16);
    probe.height = bigEndian32(bytes.constData() + 20);
    probe.bitDepth = static_cast<unsigned char>(bytes[24]);
    probe.colorType = static_cast<unsigned char>(bytes[25]);
    // The original's guard reads ImageIO's pixel properties: positive size, depth up to 8.
    if (probe.width <= 0 || probe.height <= 0) return std::nullopt;
    if (probe.bitDepth <= 0 || probe.bitDepth > 8) return std::nullopt;
    if (!validColorType(probe.colorType)) return std::nullopt;

    // The file must be recognized as a PNG and hold a single frame, as ImageIO reports.
    std::unique_ptr<QBuffer> buffer(readingBuffer(bytes));
    if (!buffer->isOpen()) return std::nullopt;
    QImageReader reader(buffer.get(), QByteArrayLiteral("png"));
    if (reader.format() != QByteArrayLiteral("png")) return std::nullopt;
    if (reader.imageCount() > 1) return std::nullopt;
    return probe;
}

std::optional<ImportedImage> decodePNGAsset(const QByteArray& bytes, const std::string& name) {
    std::unique_ptr<QBuffer> buffer(readingBuffer(bytes));
    if (!buffer->isOpen()) return std::nullopt;
    QImageReader reader(buffer.get(), QByteArrayLiteral("png"));
    QImage image = reader.read();
    if (image.isNull()) return std::nullopt;
    // Masks rely on Format_Grayscale8 surviving; every other image joins the renderer's
    // uniform RGBA layout. The index/palette formats Qt hands back otherwise would leak
    // into compositing.
    if (image.format() != QImage::Format_Grayscale8) {
        image = image.convertToFormat(QImage::Format_RGBA8888);
        if (image.isNull()) return std::nullopt;
    }
    QImage thumbnail = thumbnailFor(image);
    return ImportedImage{std::move(image), std::move(thumbnail), name};
}

std::optional<QByteArray> encodePNG(const QImage& image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) return std::nullopt;
    QImageWriter writer(&buffer, QByteArrayLiteral("png"));
    if (!writer.write(image)) return std::nullopt;
    buffer.close();
    return bytes;
}

bool isValidMaskImage(const QImage& image) {
    return image.format() == QImage::Format_Grayscale8;
}

QImage thumbnailFor(const QImage& image) {
    const int longest = std::max(image.width(), image.height());
    if (longest <= 96) return image;
    const double factor = 96.0 / longest;
    const int width = std::max(1, static_cast<int>(image.width() * factor));
    const int height = std::max(1, static_cast<int>(image.height() * factor));
    return image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

}  // namespace compositor
