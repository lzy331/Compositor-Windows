#include "ui/DocumentHistory.h"

#include <algorithm>
#include <functional>
#include <set>

namespace compositor {
namespace {

bool imageMapEqual(const std::map<Uuid, ImportedImage>& lhs, const std::map<Uuid, ImportedImage>& rhs) {
    if (lhs.size() != rhs.size()) return false;
    auto left = lhs.begin();
    auto right = rhs.begin();
    for (; left != lhs.end(); ++left, ++right) {
        if (left->first != right->first || left->second.image.cacheKey() != right->second.image.cacheKey()) return false;
    }
    return true;
}

void visitImages(const std::optional<ProjectSnapshot>& document, const std::function<void(const QImage&)>& visit) {
    if (!document) return;
    for (const auto& [id, asset] : document->images) {
        (void)id;
        visit(asset.image);
        visit(asset.thumbnail);
    }
    for (const auto& [id, asset] : document->masks) {
        (void)id;
        visit(asset.image);
        visit(asset.thumbnail);
    }
}

}  // namespace

bool documentsEqual(const std::optional<ProjectSnapshot>& lhs, const std::optional<ProjectSnapshot>& rhs) {
    if (lhs.has_value() != rhs.has_value()) return false;
    if (!lhs) return true;
    ProjectManifest leftManifest = lhs->manifest;
    ProjectManifest rightManifest = rhs->manifest;
    leftManifest.activeLayerID.reset();
    rightManifest.activeLayerID.reset();
    return leftManifest == rightManifest && imageMapEqual(lhs->images, rhs->images)
        && imageMapEqual(lhs->masks, rhs->masks);
}

DocumentHistory::DocumentHistory(int entryLimit, int64_t retainedByteLimit)
    : revision_(Uuid::generate()), entryLimit_(std::max(0, entryLimit)),
      retainedByteLimit_(std::max<int64_t>(0, retainedByteLimit)) {
    savedRevision_ = revision_;
}

bool DocumentHistory::canUndo() const { return depth_ == 0 && !past_.empty(); }
bool DocumentHistory::canRedo() const { return depth_ == 0 && !future_.empty(); }
std::string DocumentHistory::undoName() const { return past_.empty() ? std::string() : past_.back().name; }
std::string DocumentHistory::redoName() const { return future_.empty() ? std::string() : future_.back().name; }
bool DocumentHistory::isModified() const { return !savedRevision_ || revision_ != *savedRevision_; }
bool DocumentHistory::isEditing() const { return depth_ != 0; }
size_t DocumentHistory::undoCount() const { return past_.size(); }
void DocumentHistory::markSaved() { savedRevision_ = revision_; }
const Uuid& DocumentHistory::currentRevision() const { return revision_; }
void DocumentHistory::markSaved(const Uuid& saved) { savedRevision_ = saved; }

void DocumentHistory::reset() {
    past_.clear();
    future_.clear();
    pending_.reset();
    depth_ = 0;
    revision_ = Uuid::generate();
    savedRevision_ = revision_;
}

void DocumentHistory::begin(const std::string& name, const std::optional<ProjectSnapshot>& document,
                            const std::optional<Uuid>& selection) {
    if (depth_ == 0) {
        pending_ = Snapshot{document, selection, revision_};
        pendingName_ = name;
    }
    ++depth_;
}

void DocumentHistory::end(const std::optional<ProjectSnapshot>& document, const std::optional<Uuid>& selection) {
    if (depth_ <= 0) return;
    --depth_;
    if (depth_ != 0 || !pending_) return;
    Snapshot before = std::move(*pending_);
    pending_.reset();
    if (documentsEqual(before.document, document)) return;
    revision_ = Uuid::generate();
    past_.push_back(Entry{pendingName_, std::move(before), Snapshot{document, selection, revision_}});
    future_.clear();
    trim(document);
}

std::optional<DocumentHistory::Snapshot> DocumentHistory::undo() {
    if (!canUndo()) return std::nullopt;
    Entry entry = std::move(past_.back());
    past_.pop_back();
    future_.push_back(std::move(entry));
    revision_ = future_.back().before.revision;
    trim(future_.back().before.document);
    return future_.back().before;
}

std::optional<DocumentHistory::Snapshot> DocumentHistory::redo() {
    if (!canRedo()) return std::nullopt;
    Entry entry = std::move(future_.back());
    future_.pop_back();
    past_.push_back(std::move(entry));
    revision_ = past_.back().after.revision;
    trim(past_.back().after.document);
    return past_.back().after;
}

int64_t DocumentHistory::retainedBytes(const std::optional<ProjectSnapshot>& current) const {
    std::set<qint64> seen;
    auto remember = [&seen](const QImage& image) {
        const qint64 key = image.cacheKey();
        if (key != 0) seen.insert(key);
    };
    visitImages(current, remember);
    int64_t bytes = 0;
    auto count = [&seen, &bytes](const QImage& image) {
        const qint64 key = image.cacheKey();
        if (key != 0 && seen.insert(key).second) bytes += static_cast<int64_t>(image.bytesPerLine()) * image.height();
    };
    for (const Entry& entry : past_) {
        visitImages(entry.before.document, count);
        visitImages(entry.after.document, count);
    }
    for (const Entry& entry : future_) {
        visitImages(entry.before.document, count);
        visitImages(entry.after.document, count);
    }
    return bytes;
}

void DocumentHistory::trim(const std::optional<ProjectSnapshot>& current) {
    while (past_.size() + future_.size() > static_cast<size_t>(entryLimit_)
           || retainedBytes(current) > retainedByteLimit_) {
        if (!past_.empty()) past_.erase(past_.begin());
        else if (!future_.empty()) future_.erase(future_.begin());
        else break;
    }
}

}  // namespace compositor
