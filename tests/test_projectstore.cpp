// Verifies ProjectStore.save/load against real packages on disk: manifest and pixel round-trip,
// the package layout (images/, masks, QuickLook/Preview.jpg), thumbnail rules, the staging/backup
// swap (stale-file removal, crash recovery, no leftovers), and the ProjectError kinds for damaged,
// missing, oversized and unsupported projects.

#include "document/Manifest.h"
#include "io/ImageAssets.h"
#include "io/ManifestCodec.h"
#include "io/ProjectError.h"
#include "io/ProjectStore.h"

#include <QByteArray>
#include <QColor>
#include <QImage>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

using compositor::BlendMode;
using compositor::ImportedImage;
using compositor::ManifestCodec;
using compositor::PngProbe;
using compositor::ProjectError;
using compositor::ProjectLayerRecord;
using compositor::ProjectManifest;
using compositor::ProjectSnapshot;
using compositor::ProjectStore;
using compositor::QuickLookImages;
using compositor::Uuid;

namespace {

namespace fs = std::filesystem;

int failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

bool throwsKind(const std::function<void()>& body, ProjectError::Kind kind) {
    try {
        body();
        return false;
    } catch (const ProjectError& error) {
        return error.kind() == kind;
    }
}

bool throwsVersion(const std::function<void()>& body, int64_t version) {
    try {
        body();
        return false;
    } catch (const ProjectError& error) {
        return error.kind() == ProjectError::Kind::Version && error.version() == version;
    }
}

std::string utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

struct TempDir {
    fs::path path = fs::temp_directory_path() / ("compositor_test_" + Uuid::generate().toString());
    TempDir() { fs::create_directories(path); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    ~TempDir() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

std::string readAll(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void writeAll(const fs::path& file, const std::string& bytes) {
    if (file.has_parent_path()) fs::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeAll(const fs::path& file, const QByteArray& bytes) {
    writeAll(file, std::string(bytes.constData(), static_cast<size_t>(bytes.size())));
}

QImage patternImage(int width, int height) {
    QImage image(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.setPixelColor(x, y,
                                QColor((x * 7) % 256, (y * 5) % 256, ((x + y) * 3) % 256, 200 + (x % 2) * 55));
        }
    }
    return image;
}

QImage grayMask(int width, int height, int value) {
    QImage image(width, height, QImage::Format_Grayscale8);
    image.fill(static_cast<uint>(value));
    return image;
}

bool pixelsMatch(const QImage& left, const QImage& right) {
    if (left.size() != right.size()) return false;
    return left.convertToFormat(QImage::Format_RGBA8888) == right.convertToFormat(QImage::Format_RGBA8888);
}

/// A bare 26-byte PNG signature + IHDR prefix: enough for the sniffer, never enough to decode.
QByteArray pngHeader(int width, int height, int bitDepth, int colorType) {
    QByteArray bytes(26, '\0');
    const unsigned char signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::memcpy(bytes.data(), signature, 8);
    bytes[12] = 'I';
    bytes[13] = 'H';
    bytes[14] = 'D';
    bytes[15] = 'R';
    bytes[16] = static_cast<char>(width >> 24);
    bytes[17] = static_cast<char>(width >> 16);
    bytes[18] = static_cast<char>(width >> 8);
    bytes[19] = static_cast<char>(width);
    bytes[20] = static_cast<char>(height >> 24);
    bytes[21] = static_cast<char>(height >> 16);
    bytes[22] = static_cast<char>(height >> 8);
    bytes[23] = static_cast<char>(height);
    bytes[24] = static_cast<char>(bitDepth);
    bytes[25] = static_cast<char>(colorType);
    return bytes;
}

ProjectLayerRecord raster(const Uuid& id, const std::string& name) {
    ProjectLayerRecord record;
    record.id = id;
    record.name = name;
    record.imageFile = id.toString() + ".png";
    record.transform.size = compositor::Size{8, 8};
    return record;
}

ProjectManifest document(int64_t version = 11) {
    ProjectManifest manifest;
    manifest.version = version;
    manifest.documentID = Uuid::generate();
    manifest.width = 1920;
    manifest.height = 1080;
    return manifest;
}

void testProbe() {
    const std::optional<QByteArray> encoded = compositor::encodePNG(patternImage(4, 6));
    check(encoded.has_value(), "pattern encodes");
    if (encoded.has_value()) {
        const std::optional<PngProbe> probe = compositor::probePNG(*encoded);
        check(probe.has_value() && probe->width == 4 && probe->height == 6 && probe->bitDepth == 8,
              "probe reads a real png");
    }

    check(!compositor::probePNG(pngHeader(0, 2, 8, 6)).has_value(), "zero width rejected");
    check(!compositor::probePNG(pngHeader(2, 0, 8, 6)).has_value(), "zero height rejected");
    check(!compositor::probePNG(pngHeader(2, 2, 16, 6)).has_value(), "16-bit depth rejected");
    check(!compositor::probePNG(pngHeader(2, 2, 0, 6)).has_value(), "zero depth rejected");
    check(!compositor::probePNG(pngHeader(2, 2, 8, 5)).has_value(), "unknown color type rejected");
    check(!compositor::probePNG(QByteArray("not a png at all, but long enough")).has_value(), "non-png rejected");
    check(!compositor::probePNG(QByteArray(25, '\0')).has_value(), "short bytes rejected");
}

void testThumbnails() {
    check(compositor::thumbnailFor(patternImage(300, 100)).size() == QSize(96, 32), "300x100 shrinks to 96x32");
    check(compositor::thumbnailFor(patternImage(100, 300)).size() == QSize(32, 96), "100x300 shrinks to 32x96");
    check(compositor::thumbnailFor(patternImage(193, 7)).size() == QSize(96, 3), "193x7 shrinks to 96x3");
    check(compositor::thumbnailFor(patternImage(96, 96)).size() == QSize(96, 96), "96 square is left alone");
    check(compositor::thumbnailFor(patternImage(50, 20)).size() == QSize(50, 20), "small images are not upscaled");
}

void testRoundTrip() {
    TempDir dir;
    const fs::path package = dir.path / "Project.comp";

    const Uuid idA = Uuid::generate(), idB = Uuid::generate(), idC = Uuid::generate(), groupID = Uuid::generate();
    ProjectLayerRecord a = raster(idA, "Layer One");
    a.maskFile = idA.toString() + ".mask.png";
    a.opacity = 0.75;
    a.blendMode = BlendMode::Multiply;
    ProjectLayerRecord b = raster(idB, "Layer Two");
    ProjectLayerRecord c = raster(idC, "Child");
    c.parentID = groupID;
    ProjectLayerRecord group;
    group.id = groupID;
    group.name = "Folder";
    group.isGroup = true;
    group.opacity = 0.5;
    group.transform.size = compositor::Size{8, 8};

    ProjectSnapshot snapshot;
    snapshot.manifest = document();
    snapshot.manifest.layers = {a, b, group, c};
    snapshot.manifest.activeLayerID = idB;
    snapshot.images[idA] = ImportedImage{patternImage(64, 48), QImage(), "Layer One"};
    snapshot.images[idB] = ImportedImage{patternImage(300, 100), QImage(), "Layer Two"};
    snapshot.images[idC] = ImportedImage{patternImage(50, 20), QImage(), "Child"};
    snapshot.masks[idA] = ImportedImage{grayMask(64, 48, 128), QImage(), "Layer One"};
    QuickLookImages quickLook;
    quickLook.preview = QByteArrayLiteral("\xFF\xD8\xFF\xE0preview-bytes");

    ProjectStore::save(snapshot, utf8(package), quickLook);

    check(fs::exists(package / "manifest.json"), "manifest.json written");
    check(fs::exists(package / "images" / (idA.toString() + ".png")), "layer image written");
    check(fs::exists(package / "images" / (idA.toString() + ".mask.png")), "mask image written");
    check(fs::exists(package / "images" / (idC.toString() + ".png")), "nested layer image written");
    check(fs::exists(package / "QuickLook" / "Preview.jpg"), "preview written");
    check(readAll(package / "QuickLook" / "Preview.jpg") ==
              std::string(quickLook.preview.constData(), static_cast<size_t>(quickLook.preview.size())),
          "preview bytes preserved");
    check(readAll(package / "manifest.json") == ManifestCodec::encode(snapshot.manifest),
          "manifest.json is the codec's bytes");

    const ProjectSnapshot loaded = ProjectStore::load(utf8(package));
    check(loaded.manifest == snapshot.manifest, "manifest round-trips");
    check(loaded.images.size() == 3, "three layer images load");
    check(loaded.masks.size() == 1, "one mask loads");
    check(pixelsMatch(loaded.images.at(idA).image, snapshot.images.at(idA).image), "layer A pixels round-trip");
    check(pixelsMatch(loaded.images.at(idB).image, snapshot.images.at(idB).image), "layer B pixels round-trip");
    check(pixelsMatch(loaded.images.at(idC).image, snapshot.images.at(idC).image), "layer C pixels round-trip");
    check(loaded.masks.at(idA).image.format() == QImage::Format_Grayscale8, "mask stays grayscale");
    check(loaded.masks.at(idA).image == snapshot.masks.at(idA).image, "mask pixels round-trip");
    check(loaded.images.at(idA).name == "Layer One", "layer name carried through");
    check(loaded.images.at(idA).thumbnail.size() == QSize(64, 48), "small image thumbnail untouched");
    check(loaded.images.at(idB).thumbnail.size() == QSize(96, 32), "large image thumbnail scaled");
    check(loaded.masks.at(idA).thumbnail.size() == QSize(64, 48), "mask thumbnail follows the same rule");
}

void testSaveErrors() {
    TempDir dir;
    const fs::path package = dir.path / "Guarded.comp";

    const Uuid id = Uuid::generate();
    ProjectSnapshot good;
    good.manifest = document();
    good.manifest.layers.push_back(raster(id, "Fine"));
    good.images[id] = ImportedImage{patternImage(8, 8), QImage(), "Fine"};
    ProjectStore::save(good, utf8(package));
    check(fs::exists(package / "manifest.json"), "baseline package saves");

    // Manifest references pixels the snapshot does not carry.
    {
        ProjectSnapshot snapshot;
        snapshot.manifest = document();
        snapshot.manifest.layers.push_back(raster(Uuid::generate(), "Missing"));
        check(throwsKind([&] { ProjectStore::save(snapshot, utf8(package)); }, ProjectError::Kind::MissingImage),
              "save without pixels is missingImage");
    }
    // A mask must be 8-bit grayscale.
    {
        ProjectSnapshot snapshot;
        snapshot.manifest = document();
        const Uuid maskID = Uuid::generate();
        ProjectLayerRecord layer = raster(maskID, "Masked");
        layer.maskFile = maskID.toString() + ".mask.png";
        snapshot.manifest.layers.push_back(layer);
        snapshot.images[maskID] = ImportedImage{patternImage(16, 16), QImage(), "Masked"};
        snapshot.masks[maskID] = ImportedImage{patternImage(16, 16), QImage(), "Masked"};
        check(throwsKind([&] { ProjectStore::save(snapshot, utf8(package)); }, ProjectError::Kind::Invalid),
              "colorful mask is invalid");
    }
    // Semantic failures are refused before anything is written.
    {
        ProjectSnapshot bad = good;
        bad.manifest.layers[0].name = "   ";
        check(throwsKind([&] { ProjectStore::save(bad, utf8(package)); }, ProjectError::Kind::Invalid),
              "blank name invalid on save");
    }
    check(ProjectStore::load(utf8(package)).manifest == good.manifest, "previous project survives failed saves");
}

void testLoadErrors() {
    TempDir dir;

    // A plain file is not a package.
    {
        const fs::path file = dir.path / "not-a-package.comp";
        writeAll(file, std::string("hello"));
        check(throwsKind([&] { ProjectStore::load(utf8(file)); }, ProjectError::Kind::Invalid),
              "plain file is invalid");
    }
    // An empty folder is not a package.
    {
        const fs::path empty = dir.path / "empty.comp";
        fs::create_directories(empty);
        check(throwsKind([&] { ProjectStore::load(utf8(empty)); }, ProjectError::Kind::Invalid),
              "folder without manifest is invalid");
    }
    // Damaged metadata.
    {
        const fs::path package = dir.path / "damaged.comp";
        fs::create_directories(package);
        writeAll(package / "manifest.json", std::string("{ not json"));
        check(throwsKind([&] { ProjectStore::load(utf8(package)); }, ProjectError::Kind::Invalid),
              "damaged manifest is invalid");
    }
    // Oversized metadata.
    {
        const fs::path package = dir.path / "huge.comp";
        fs::create_directories(package);
        writeAll(package / "manifest.json", std::string(4 * 1024 * 1024 + 1, 'x'));
        check(throwsKind([&] { ProjectStore::load(utf8(package)); }, ProjectError::Kind::TooLarge),
              "oversized manifest is tooLarge");
    }
    // A version from the future reaches the version gate.
    {
        const fs::path package = dir.path / "future.comp";
        fs::create_directories(package);
        writeAll(package / "manifest.json", std::string(R"({"format":"com.compositor.project","version":12})"));
        check(throwsVersion([&] { ProjectStore::load(utf8(package)); }, 12), "version 12 is a version error");
    }
    // A foreign format identifier.
    {
        const fs::path package = dir.path / "foreign.comp";
        fs::create_directories(package);
        writeAll(package / "manifest.json", std::string(R"({"format":"com.example.other","version":11})"));
        check(throwsKind([&] { ProjectStore::load(utf8(package)); }, ProjectError::Kind::Invalid),
              "foreign format is invalid");
    }

    // Raster layer whose image file is absent.
    {
        const fs::path package = dir.path / "gone.comp";
        fs::create_directories(package / "images");
        const Uuid id = Uuid::generate();
        ProjectManifest manifest = document();
        manifest.layers.push_back(raster(id, "Gone"));
        writeAll(package / "manifest.json", ManifestCodec::encode(manifest));
        check(throwsKind([&] { ProjectStore::load(utf8(package)); }, ProjectError::Kind::MissingImage),
              "missing image file is missingImage");
    }
    // A header-only PNG: sniffing survives, decoding does not.
    {
        const fs::path package = dir.path / "truncated.comp";
        const Uuid id = Uuid::generate();
        ProjectManifest manifest = document();
        manifest.layers.push_back(raster(id, "Truncated"));
        writeAll(package / "manifest.json", ManifestCodec::encode(manifest));
        writeAll(package / "images" / (id.toString() + ".png"), pngHeader(4, 4, 8, 6));
        check(throwsKind([&] { ProjectStore::load(utf8(package)); }, ProjectError::Kind::MissingImage),
              "truncated png is missingImage");
    }
    // A 16-bit PNG is beyond the supported depth.
    {
        const fs::path package = dir.path / "deep.comp";
        const Uuid id = Uuid::generate();
        ProjectManifest manifest = document();
        manifest.layers.push_back(raster(id, "Deep"));
        writeAll(package / "manifest.json", ManifestCodec::encode(manifest));
        writeAll(package / "images" / (id.toString() + ".png"), pngHeader(4, 4, 16, 0));
        check(throwsKind([&] { ProjectStore::load(utf8(package)); }, ProjectError::Kind::MissingImage),
              "16-bit png is missingImage");
    }
    // JPEG bytes wearing a .png name.
    {
        const fs::path package = dir.path / "jpeg.comp";
        const Uuid id = Uuid::generate();
        ProjectManifest manifest = document();
        manifest.layers.push_back(raster(id, "Jpeg"));
        writeAll(package / "manifest.json", ManifestCodec::encode(manifest));
        writeAll(package / "images" / (id.toString() + ".png"), std::string("\xFF\xD8\xFF\xE0jpeg-junk-bytes"));
        check(throwsKind([&] { ProjectStore::load(utf8(package)); }, ProjectError::Kind::MissingImage),
              "jpeg bytes are missingImage");
    }
    // A colorful PNG where a mask should be.
    {
        const fs::path package = dir.path / "colorful.comp";
        const Uuid id = Uuid::generate();
        ProjectLayerRecord layer = raster(id, "Masked");
        layer.maskFile = id.toString() + ".mask.png";
        ProjectManifest manifest = document();
        manifest.layers.push_back(layer);
        writeAll(package / "manifest.json", ManifestCodec::encode(manifest));
        const std::optional<QByteArray> imageBytes = compositor::encodePNG(patternImage(8, 8));
        check(imageBytes.has_value(), "colorful mask encodes");
        if (imageBytes.has_value()) {
            writeAll(package / "images" / (id.toString() + ".png"), *imageBytes);
            writeAll(package / "images" / (id.toString() + ".mask.png"), *imageBytes);
            check(throwsKind([&] { ProjectStore::load(utf8(package)); }, ProjectError::Kind::Invalid),
                  "colorful mask file is invalid");
        }
    }
    // A folder where an image file should be.
    {
        const fs::path package = dir.path / "blocked.comp";
        const Uuid id = Uuid::generate();
        ProjectManifest manifest = document();
        manifest.layers.push_back(raster(id, "Blocked"));
        writeAll(package / "manifest.json", ManifestCodec::encode(manifest));
        fs::create_directories(package / "images" / (id.toString() + ".png"));
        check(throwsKind([&] { ProjectStore::load(utf8(package)); }, ProjectError::Kind::TooLarge),
              "directory named like an image is tooLarge");
    }
}

void testSwapSemantics() {
    TempDir dir;
    const fs::path package = dir.path / "Swap.comp";
    fs::path staging = package;
    staging += ".tmp~";
    fs::path backup = package;
    backup += ".backup~";

    const Uuid keep = Uuid::generate(), drop = Uuid::generate();
    ProjectSnapshot first;
    first.manifest = document();
    first.manifest.layers = {raster(keep, "Keep"), raster(drop, "Drop")};
    first.images[keep] = ImportedImage{patternImage(8, 8), QImage(), "Keep"};
    first.images[drop] = ImportedImage{patternImage(8, 8), QImage(), "Drop"};

    ProjectSnapshot second;
    second.manifest = document();
    second.manifest.layers = {raster(keep, "Keep")};
    second.images[keep] = ImportedImage{patternImage(8, 8), QImage(), "Keep"};

    ProjectStore::save(first, utf8(package));
    ProjectStore::save(second, utf8(package));
    check(!fs::exists(package / "images" / (drop.toString() + ".png")), "replaced package drops stale images");
    check(!fs::exists(staging) && !fs::exists(backup), "no staging or backup leftovers");
    check(ProjectStore::load(utf8(package)).manifest == second.manifest, "second save wins");

    // Leftovers of an interrupted save (destination renamed away, stale staging present) are
    // cleaned up, the backup is restored, and the new save still lands.
    ProjectStore::save(first, utf8(package));
    fs::rename(package, backup);
    fs::create_directories(staging);
    ProjectStore::save(second, utf8(package));
    check(!fs::exists(staging) && !fs::exists(backup), "interrupted-save leftovers cleaned");
    check(ProjectStore::load(utf8(package)).manifest == second.manifest, "recovered and saved");
}

}  // namespace

int main() {
    testProbe();
    testThumbnails();
    testRoundTrip();
    testSaveErrors();
    testLoadErrors();
    testSwapSemantics();
    if (failures == 0) std::printf("test_projectstore: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
