#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "io/ProjectStore.h"

namespace compositor {

bool documentsEqual(const std::optional<ProjectSnapshot>& lhs, const std::optional<ProjectSnapshot>& rhs);

class DocumentHistory {
public:
    struct Snapshot {
        std::optional<ProjectSnapshot> document;
        std::optional<Uuid> activeLayerID;
        Uuid revision;
    };

    explicit DocumentHistory(int entryLimit = 100, int64_t retainedByteLimit = 256LL * 1024 * 1024);

    bool canUndo() const;
    bool canRedo() const;
    std::string undoName() const;
    std::string redoName() const;
    bool isModified() const;
    bool isEditing() const;
    size_t undoCount() const;
    void markSaved();
    const Uuid& currentRevision() const;
    void markSaved(const Uuid& saved);
    void reset();

    void begin(const std::string& name, const std::optional<ProjectSnapshot>& document,
               const std::optional<Uuid>& selection);
    void end(const std::optional<ProjectSnapshot>& document, const std::optional<Uuid>& selection);
    std::optional<Snapshot> undo();
    std::optional<Snapshot> redo();
    int64_t retainedBytes(const std::optional<ProjectSnapshot>& current) const;

private:
    struct Entry {
        std::string name;
        Snapshot before;
        Snapshot after;
    };

    void trim(const std::optional<ProjectSnapshot>& current);

    std::vector<Entry> past_;
    std::vector<Entry> future_;
    Uuid revision_;
    std::optional<Uuid> savedRevision_;
    std::optional<Snapshot> pending_;
    std::string pendingName_ = "Edit";
    int depth_ = 0;
    int entryLimit_;
    int64_t retainedByteLimit_;
};

}  // namespace compositor
