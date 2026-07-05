#pragma once

#include "pinloom/clip/ClipRepository.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

namespace Pinloom {

struct ClipSearchOptions {
    bool includeSaved = true;
    bool includeTemporary = false;
    bool includeDeleted = false;
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
    using FindClipCallback = std::function<std::optional<Clip>(const QString &clipId)>;
    using SaveClipCallback = std::function<bool(const QString &clipId,
                                                const QString &name,
                                                const QStringList &aliases,
                                                const QStringList &tags,
                                                bool pinned,
                                                const QDateTime &now)>;
    using ClipStateMutationCallback = std::function<bool(const QString &clipId, const QDateTime &now)>;
    using LastErrorCallback = std::function<QString()>;

    explicit ClipSearchService(InMemoryClipRepository &repository);
    explicit ClipSearchService(SqliteClipRepository &repository);
    ClipSearchService(ListClipsCallback listClips,
                      ListClipsCallback listSavedClips = {},
                      FindClipCallback findClip = {},
                      SaveClipCallback saveClip = {},
                      ClipStateMutationCallback softDeleteClip = {},
                      ClipStateMutationCallback restoreClip = {},
                      LastErrorCallback lastError = {});

    QList<ClipSearchResult> search(const QString &query, const ClipSearchOptions &options = {}) const;
    std::optional<Clip> findClip(const QString &clipId) const;
    bool saveClip(const QString &clipId,
                  const QString &name,
                  const QStringList &aliases = {},
                  const QStringList &tags = {},
                  bool pinned = false,
                  const QDateTime &now = {}) const;
    bool softDeleteClip(const QString &clipId, const QDateTime &now = {}) const;
    bool restoreClip(const QString &clipId, const QDateTime &now = {}) const;
    QString lastError() const;

private:
    ListClipsCallback listClips_;
    ListClipsCallback listSavedClips_;
    FindClipCallback findClip_;
    SaveClipCallback saveClip_;
    ClipStateMutationCallback softDeleteClip_;
    ClipStateMutationCallback restoreClip_;
    LastErrorCallback lastError_;
};

} // namespace Pinloom
