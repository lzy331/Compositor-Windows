#include "io/ProjectStore.h"

#include "document/ManifestValidation.h"
#include "io/ManifestCodec.h"
#include "io/ProjectError.h"

#include <QFile>

#include <filesystem>
#include <system_error>

namespace compositor {

namespace {

namespace fs = std::filesystem;

constexpr int64_t manifestMaximumBytes = 4 * 1024 * 1024;
constexpr int64_t imageMaximumBytes = 512ll * 1024 * 1024;

QString nativePath(const fs::path& path) {
    const std::u8string utf8 = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(utf8.data()), static_cast<qsizetype>(utf8.size()));
}

fs::path utf8Path(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

std::optional<QByteArray> readFileBytes(const fs::path& file) {
    QFile input(nativePath(file));
    if (!input.open(QIODevice::ReadOnly)) return std::nullopt;
    QByteArray bytes = input.readAll();
    if (input.error() != QFileDevice::NoError) return std::nullopt;
    return bytes;
}

bool writeFileBytes(const fs::path& file, const char* data, qint64 size) {
    QFile output(nativePath(file));
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const bool written = output.write(data, size) == size && output.flush();
    output.close();
    return written && output.error() == QFileDevice::NoError;
}

// The original distinguishes metadata problems (invalid) from missing pixels (missingImage)
// when a file resource cannot be read; that choice travels in.
enum class MissingKind { Invalid, MissingImage };

ProjectError missingError(MissingKind missing) {
    return missing == MissingKind::Invalid ? ProjectError::invalid() : ProjectError::missingImage();
}

void checkFile(const fs::path& file, const fs::path& package, int64_t maximumBytes, MissingKind missing) {
    std::error_code error;
    const fs::path root = fs::weakly_canonical(package, error);
    if (error) throw ProjectError::invalid();
    const fs::path resolved = fs::weakly_canonical(file, error);
    if (error) throw missingError(missing);
    // Files must live inside the package once symlinks are resolved, as in the original.
    const fs::path inside = resolved.lexically_relative(root);
    const std::string relative = inside.generic_string();
    if (relative.empty() || relative == "." || relative.rfind("..", 0) == 0) throw ProjectError::invalid();

    const fs::file_status linkStatus = fs::symlink_status(file, error);
    if (error || !fs::exists(linkStatus)) throw missingError(missing);
    // Links, directories and anything not a plain file are rejected the same way as
    // oversized files, mirroring the original's single guard.
    if (fs::is_symlink(linkStatus) || !fs::is_regular_file(linkStatus)) throw ProjectError::tooLarge();
    const uintmax_t size = fs::file_size(file, error);
    if (error || static_cast<int64_t>(size) > maximumBytes) throw ProjectError::tooLarge();
}

void checkSize(int64_t width, int64_t height, int64_t& used) {
    if (width < 1 || width > DocumentLimits::maxSide || height < 1 || height > DocumentLimits::maxSide ||
        width * height > DocumentLimits::documentPixelBudget() - used) {
        throw ProjectError::tooLarge();
    }
    used += width * height;
}

}  // namespace

void ProjectStore::save(const ProjectSnapshot& snapshot, const std::string& path,
                        const std::optional<QuickLookImages>& quickLook) {
    validateManifest(snapshot.manifest);

    struct EncodedImage {
        std::string filename;
        QByteArray bytes;
    };
    std::vector<EncodedImage> encoded;
    int64_t pixels = 0, maskPixels = 0;
    for (const ProjectLayerRecord& layer : snapshot.manifest.layers) {
        for (const bool isMask : {false, true}) {
            const std::optional<std::string>& filename = isMask ? layer.maskFile : layer.imageFile;
            if (!filename.has_value()) continue;
            const std::map<Uuid, ImportedImage>& assets = isMask ? snapshot.masks : snapshot.images;
            const auto asset = assets.find(layer.id);
            if (asset == assets.end()) throw ProjectError::missingImage();
            if (isMask) {
                if (!isValidMaskImage(asset->second.image)) throw ProjectError::invalid();
                checkSize(asset->second.image.width(), asset->second.image.height(), maskPixels);
            } else {
                checkSize(asset->second.image.width(), asset->second.image.height(), pixels);
            }
            std::optional<QByteArray> bytes = encodePNG(asset->second.image);
            if (!bytes.has_value()) throw ProjectError::encode();
            encoded.push_back({*filename, std::move(*bytes)});
        }
    }
    const std::string metadata = ManifestCodec::encode(snapshot.manifest);
    if (metadata.size() > static_cast<size_t>(manifestMaximumBytes)) throw ProjectError::tooLarge();

    const fs::path destination = utf8Path(path);
    fs::path staging = destination;
    staging += ".tmp~";
    fs::path backup = destination;
    backup += ".backup~";
    std::error_code error;
    // Leftovers mean an earlier save was interrupted. The destination, when present, is the
    // good copy; when it is not, the backup is all that survived and comes back.
    if (fs::exists(staging, error)) fs::remove_all(staging, error);
    if (fs::exists(backup, error)) {
        if (!fs::exists(destination, error)) fs::rename(backup, destination, error);
        else fs::remove_all(backup, error);
    }
    fs::create_directories(staging / "images", error);
    const auto fail = [&]() {
        std::error_code cleanupError;
        fs::remove_all(staging, cleanupError);
        throw ProjectError::encode();
    };
    if (error) fail();
    if (!writeFileBytes(staging / "manifest.json", metadata.data(), static_cast<qint64>(metadata.size()))) fail();
    for (const EncodedImage& image : encoded) {
        if (!writeFileBytes(staging / "images" / image.filename, image.bytes.constData(), image.bytes.size())) fail();
    }
    // Quick Look's Space-bar preview reads this by name; loading ignores it.
    if (quickLook.has_value()) {
        fs::create_directories(staging / "QuickLook", error);
        if (error) fail();
        if (!writeFileBytes(staging / "QuickLook" / "Preview.jpg", quickLook->preview.constData(),
                            quickLook->preview.size())) {
            fail();
        }
    }
    // The complete package exists; only now does the previous one step aside.
    if (fs::exists(destination, error)) {
        fs::rename(destination, backup, error);
        if (error) fail();
    }
    fs::rename(staging, destination, error);
    if (error) {
        std::error_code rollbackError;
        fs::rename(backup, destination, rollbackError);
        fail();
    }
    if (fs::exists(backup, error)) {
        std::error_code cleanupError;
        fs::remove_all(backup, cleanupError);
    }
}

ProjectSnapshot ProjectStore::load(const std::string& path) {
    const fs::path url = utf8Path(path);
    std::error_code error;
    if (!fs::is_directory(url, error) || error) throw ProjectError::invalid();

    checkFile(url / "manifest.json", url, manifestMaximumBytes, MissingKind::Invalid);
    const std::optional<QByteArray> metadataBytes = readFileBytes(url / "manifest.json");
    if (!metadataBytes.has_value()) throw ProjectError::invalid();
    const std::string metadata(metadataBytes->constData(), static_cast<size_t>(metadataBytes->size()));

    ManifestCodec::Header header;
    try {
        header = ManifestCodec::decodeHeader(metadata);
    } catch (const ProjectError&) {
        throw ProjectError::invalid();
    }
    if (header.format != "com.compositor.project") throw ProjectError::invalid();
    if (header.version < ProjectManifest::supportedLow || header.version > ProjectManifest::supportedHigh) {
        throw ProjectError::version(header.version);
    }
    ProjectManifest manifest;
    try {
        manifest = ManifestCodec::decode(metadata);
    } catch (const ProjectError&) {
        throw ProjectError::invalid();
    }
    validateManifest(manifest);

    ProjectSnapshot snapshot;
    snapshot.manifest = std::move(manifest);
    int64_t pixels = 0, maskPixels = 0;
    for (const ProjectLayerRecord& layer : snapshot.manifest.layers) {
        for (const bool isMask : {false, true}) {
            const std::optional<std::string>& filename = isMask ? layer.maskFile : layer.imageFile;
            if (!filename.has_value()) continue;
            const fs::path file = url / "images" / *filename;
            checkFile(file, url, imageMaximumBytes, MissingKind::MissingImage);
            const std::optional<QByteArray> bytes = readFileBytes(file);
            if (!bytes.has_value()) throw ProjectError::missingImage();
            const std::optional<PngProbe> probe = probePNG(*bytes);
            if (!probe.has_value()) throw ProjectError::missingImage();
            if (isMask) checkSize(probe->width, probe->height, maskPixels);
            else checkSize(probe->width, probe->height, pixels);
            std::optional<ImportedImage> asset = decodePNGAsset(*bytes, layer.name);
            if (!asset.has_value()) throw ProjectError::missingImage();
            if (isMask && !isValidMaskImage(asset->image)) throw ProjectError::invalid();
            if (isMask) snapshot.masks[layer.id] = std::move(*asset);
            else snapshot.images[layer.id] = std::move(*asset);
        }
    }
    return snapshot;
}

}  // namespace compositor
