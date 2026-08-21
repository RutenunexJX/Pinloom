#pragma once

#include "pinloom/clip/ClipRepository.h"

#include <functional>
#include <optional>

namespace Pinloom {

class ObsidianClipStore;

class PersistentClipService {
public:
    using ListCallback = std::function<QList<Clip>()>;
    using FindCallback = std::function<std::optional<Clip>(const QString &)>;
    using UpsertCallback = std::function<bool(const Clip &)>;
    using RemoveCallback = std::function<bool(const QString &)>;
    using ErrorCallback = std::function<QString()>;

    PersistentClipService(InMemoryClipRepository &repository, ObsidianClipStore &obsidianStore);
    PersistentClipService(SqliteClipRepository &repository, ObsidianClipStore &obsidianStore);
    PersistentClipService(ListCallback list,
                          FindCallback find,
                          UpsertCallback upsert,
                          RemoveCallback remove,
                          ErrorCallback repositoryError,
                          ObsidianClipStore &obsidianStore);

    QList<Clip> clips() const;
    std::optional<Clip> findClip(const QString &clipId) const;

    bool saveClip(Clip clip, QString *error = nullptr) const;
    bool upsertPersistentClip(const Clip &clip, QString *error = nullptr) const;
    bool changeState(const QString &clipId, ClipState state, QString *error = nullptr) const;
    bool permanentlyRemove(const QString &clipId, QString *error = nullptr) const;

private:
    bool fail(QString *error, const QString &message) const;
    bool rollbackObsidianWrite(const QString &clipId,
                               const std::optional<Clip> &previousDocument,
                               QString *error) const;
    QString repositoryError(const QString &fallback) const;

    ListCallback list_;
    FindCallback find_;
    UpsertCallback upsert_;
    RemoveCallback remove_;
    ErrorCallback repositoryError_;
    ObsidianClipStore *obsidianStore_ = nullptr;
};

} // namespace Pinloom
