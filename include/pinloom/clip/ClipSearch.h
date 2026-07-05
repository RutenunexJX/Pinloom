#pragma once

#include "pinloom/clip/ClipRepository.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

namespace Pinloom {

struct ClipSearchOptions {
    bool includeSaved = true;
    bool includeTemporary = false;
    bool emptyQueryReturnsPinnedAndRecent = true;
    int limit = 20;
};

struct ClipSearchResult {
    QString clipId;
    QString displayName;
    QString preview;
    QString matchedField;
    QString matchedValue;
    double score = 0.0;
    int rank = 0;
    ClipState state = ClipState::Temporary;
    QStringList tags;
    QStringList aliases;
    bool pinned = false;
    QDateTime createdAt;
    QDateTime updatedAt;
    QDateTime usedAt;
};

QList<ClipSearchResult> searchClips(const QList<Clip> &clips,
                                    const QString &query,
                                    const ClipSearchOptions &options = {});

class ClipSearchService {
public:
    using ListClipsCallback = std::function<QList<Clip>()>;

    explicit ClipSearchService(InMemoryClipRepository &repository);
    explicit ClipSearchService(SqliteClipRepository &repository);
    ClipSearchService(ListClipsCallback listClips, ListClipsCallback listSavedClips = {});

    QList<ClipSearchResult> search(const QString &query, const ClipSearchOptions &options = {}) const;

private:
    ListClipsCallback listClips_;
    ListClipsCallback listSavedClips_;
};

} // namespace Pinloom
