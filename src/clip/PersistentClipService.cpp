#include "pinloom/clip/PersistentClipService.h"

#include "pinloom/clip/ClipAction.h"
#include "pinloom/clip/ObsidianClipStore.h"

#include <QDateTime>
#include <utility>

namespace Pinloom {

PersistentClipService::PersistentClipService(InMemoryClipRepository &repository,
                                             ObsidianClipStore &obsidianStore)
    : PersistentClipService([&repository]() { return repository.clips(); },
                            [&repository](const QString &id) { return repository.findClip(id); },
                            [&repository](const Clip &clip) {
                                return repository.upsertPersistentClip(clip);
                            },
                            [&repository](const QString &id) {
                                return repository.permanentlyDeleteClip(id);
                            },
                            [&repository]() { return repository.lastError(); },
                            obsidianStore)
{
}

PersistentClipService::PersistentClipService(SqliteClipRepository &repository,
                                             ObsidianClipStore &obsidianStore)
    : PersistentClipService([&repository]() { return repository.clips(); },
                            [&repository](const QString &id) { return repository.findClip(id); },
                            [&repository](const Clip &clip) {
                                return repository.upsertPersistentClip(clip);
                            },
                            [&repository](const QString &id) {
                                return repository.permanentlyDeleteClip(id);
                            },
                            [&repository]() { return repository.lastError(); },
                            obsidianStore)
{
}

PersistentClipService::PersistentClipService(ListCallback list,
                                             FindCallback find,
                                             UpsertCallback upsert,
                                             RemoveCallback remove,
                                             ErrorCallback repositoryError,
                                             ObsidianClipStore &obsidianStore)
    : list_(std::move(list))
    , find_(std::move(find))
    , upsert_(std::move(upsert))
    , remove_(std::move(remove))
    , repositoryError_(std::move(repositoryError))
    , obsidianStore_(&obsidianStore)
{
}

QList<Clip> PersistentClipService::clips() const
{
    return list_ ? list_() : QList<Clip>{};
}

std::optional<Clip> PersistentClipService::findClip(const QString &clipId) const
{
    return find_ ? find_(clipId) : std::nullopt;
}

bool PersistentClipService::saveClip(Clip clip, QString *error) const
{
    const std::optional<Clip> existingIndex = findClip(clip.id);
    clip.state = ClipState::Saved;
    if (clip.name.trimmed().isEmpty()) {
        clip.name = clip.preview.trimmed();
    }
    const ClipIdentityValidationResult identity =
        validateClipIdentity(clips(), clip.id, clip.name, clip.aliases);
    if (!identity.valid) {
        return fail(error, identity.error);
    }

    clip.updatedAt = QDateTime::currentDateTimeUtc();
    clip.expiresAt = {};
    if (!existingIndex.has_value() || existingIndex->state == ClipState::Temporary) {
        clip.actionType = taggedActionForTags(clip.tags);
    }
    const bool mustUseObsidian = clip.storageBackend == ClipStorageBackend::Obsidian;
    const bool obsidianEnabled = obsidianStore_ && obsidianStore_->config().isEnabled();
    if (mustUseObsidian && !obsidianEnabled) {
        return fail(error, QStringLiteral("Configure the Obsidian vault before changing this Clip"));
    }
    if (obsidianEnabled) {
        clip.storageBackend = ClipStorageBackend::Obsidian;
        QString snapshotError;
        const std::optional<ObsidianClipDocument> previous =
            obsidianStore_->findClip(clip.id, &snapshotError);
        if (!snapshotError.trimmed().isEmpty()) return fail(error, snapshotError);
        const ObsidianClipWriteResult written = obsidianStore_->writeClip(clip);
        if (!written.succeeded()) {
            return fail(error, written.error);
        }
        if (!upsert_ || !upsert_(clip)) {
            QString rollbackError;
            const bool rolledBack = rollbackObsidianWrite(
                clip.id,
                previous.has_value() ? std::optional<Clip>(previous->clip) : std::nullopt,
                &rollbackError);
            const QString indexError = repositoryError(
                QStringLiteral("Unable to update the Saved Clip index"));
            return fail(error,
                        rolledBack
                            ? indexError
                            : QStringLiteral("%1; Obsidian rollback failed: %2")
                                  .arg(indexError, rollbackError));
        }
        if (error) error->clear();
        return true;
    } else {
        clip.storageBackend = ClipStorageBackend::Local;
    }
    return upsertPersistentClip(clip, error);
}

bool PersistentClipService::upsertPersistentClip(const Clip &clip, QString *error) const
{
    if (!upsert_ || !upsert_(clip)) {
        return fail(error, repositoryError(QStringLiteral("Unable to update the Saved Clip index")));
    }
    if (error) error->clear();
    return true;
}

bool PersistentClipService::changeState(const QString &clipId,
                                        ClipState state,
                                        QString *error) const
{
    const std::optional<Clip> existing = findClip(clipId);
    if (!existing.has_value() || existing->state == ClipState::Temporary) {
        return fail(error, QStringLiteral("Saved Clip was not found"));
    }
    if (state != ClipState::Saved && state != ClipState::Deleted) {
        return fail(error, QStringLiteral("Unsupported persistent Clip state"));
    }

    Clip updated = existing.value();
    updated.state = state;
    updated.updatedAt = QDateTime::currentDateTimeUtc();
    updated.expiresAt = {};
    if (isObsidianBackedClip(updated)) {
        if (!obsidianStore_ || !obsidianStore_->config().isEnabled()) {
            return fail(error, QStringLiteral("Configure the Obsidian vault before changing this Clip"));
        }
        QString snapshotError;
        const std::optional<ObsidianClipDocument> previous =
            obsidianStore_->findClip(clipId, &snapshotError);
        if (!snapshotError.trimmed().isEmpty()) return fail(error, snapshotError);
        const ObsidianClipWriteResult written = obsidianStore_->writeClip(updated);
        if (!written.succeeded()) {
            return fail(error, written.error);
        }
        if (!upsert_ || !upsert_(updated)) {
            QString rollbackError;
            const bool rolledBack = rollbackObsidianWrite(
                clipId,
                previous.has_value() ? std::optional<Clip>(previous->clip) : std::nullopt,
                &rollbackError);
            const QString indexError = repositoryError(
                QStringLiteral("Unable to update the Saved Clip index"));
            return fail(error,
                        rolledBack
                            ? indexError
                            : QStringLiteral("%1; Obsidian rollback failed: %2")
                                  .arg(indexError, rollbackError));
        }
        if (error) error->clear();
        return true;
    }
    return upsertPersistentClip(updated, error);
}

bool PersistentClipService::permanentlyRemove(const QString &clipId, QString *error) const
{
    const std::optional<Clip> existing = findClip(clipId);
    if (!existing.has_value() || existing->state != ClipState::Deleted) {
        return fail(error, QStringLiteral("Only a Clip in Trash can be removed permanently"));
    }
    if (isObsidianBackedClip(existing.value())) {
        if (!obsidianStore_ || !obsidianStore_->config().isEnabled()) {
            return fail(error, QStringLiteral("Configure the Obsidian vault before removing this Clip"));
        }
        if (!obsidianStore_->forgetClip(clipId, error)) {
            return false;
        }
    }
    if (!remove_ || !remove_(clipId)) {
        const QString indexError = repositoryError(
            QStringLiteral("Unable to remove Clip from the local index"));
        if (isObsidianBackedClip(existing.value())) {
            const ObsidianClipWriteResult restored = obsidianStore_->writeClip(existing.value());
            if (!restored.succeeded()) {
                return fail(error,
                            QStringLiteral("%1; Obsidian rollback failed: %2")
                                .arg(indexError, restored.error));
            }
        }
        return fail(error, indexError);
    }
    if (error) error->clear();
    return true;
}

bool PersistentClipService::rollbackObsidianWrite(
    const QString &clipId,
    const std::optional<Clip> &previousDocument,
    QString *error) const
{
    if (!obsidianStore_) {
        if (error) *error = QStringLiteral("Obsidian store is unavailable");
        return false;
    }
    if (previousDocument.has_value()) {
        const ObsidianClipWriteResult restored = obsidianStore_->writeClip(previousDocument.value());
        if (!restored.succeeded()) {
            if (error) *error = restored.error;
            return false;
        }
    } else if (!obsidianStore_->forgetClip(clipId, error)) {
        return false;
    }
    if (error) error->clear();
    return true;
}

bool PersistentClipService::fail(QString *error, const QString &message) const
{
    if (error) {
        *error = message.trimmed().isEmpty() ? QStringLiteral("Clip operation failed") : message.trimmed();
    }
    return false;
}

QString PersistentClipService::repositoryError(const QString &fallback) const
{
    const QString detail = repositoryError_ ? repositoryError_().trimmed() : QString();
    return detail.isEmpty() ? fallback : detail;
}

} // namespace Pinloom
