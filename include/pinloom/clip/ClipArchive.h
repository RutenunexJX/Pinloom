#pragma once

#include "pinloom/clip/ClipRepository.h"

#include <QList>
#include <QString>
#include <functional>
#include <optional>

namespace Pinloom {

struct ClipArchiveResult {
    bool success = false;
    int exported = 0;
    int imported = 0;
    int skippedConflictingIds = 0;
    QString error;

    bool succeeded() const;
};

class ClipArchive {
public:
    using ListSavedClipsCallback = std::function<QList<Clip>()>;
    using FindClipCallback = std::function<std::optional<Clip>(const QString &)>;
    using ImportSavedClipCallback = std::function<bool(const Clip &, QString *)>;

    explicit ClipArchive(InMemoryClipRepository &repository);
    explicit ClipArchive(SqliteClipRepository &repository);
    ClipArchive(ListSavedClipsCallback listSavedClips,
                FindClipCallback findClip,
                ImportSavedClipCallback importSavedClip);

    ClipArchiveResult exportSavedClips(const QString &filePath) const;
    ClipArchiveResult importSavedClips(const QString &filePath) const;

    static QString schemaName();
    static int schemaVersion();

private:
    ListSavedClipsCallback listSavedClips_;
    FindClipCallback findClip_;
    ImportSavedClipCallback importSavedClip_;
};

} // namespace Pinloom
