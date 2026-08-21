#pragma once

#include "pinloom/core/Anchor.h"

#include <QDateTime>
#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <optional>

class QSqlQuery;

namespace Pinloom {

enum class ClipKind {
    Text
};

enum class ClipState {
    Temporary,
    Saved,
    Deleted
};

enum class ClipActionType {
    InsertText,
    OpenWebUrl
};

enum class ClipStorageBackend {
    Local,
    Obsidian
};

enum class ClipCaptureStatus {
    Captured,
    IgnoredPaused,
    IgnoredBlank,
    IgnoredTooLarge,
    IgnoredDuplicate,
    IgnoredExcludedSource,
    IgnoredSensitiveContent
};

enum class ClipInsertMode {
    Paste
};

struct Clip {
    QString id;
    ClipKind kind = ClipKind::Text;
    ClipState state = ClipState::Temporary;
    QString text;
    QString preview;
    QString contentHash;
    QString name;
    QStringList aliases;
    QStringList tags;
    ClipActionType actionType = ClipActionType::InsertText;
    ClipStorageBackend storageBackend = ClipStorageBackend::Local;
    bool pinned = false;
    QDateTime createdAt;
    QDateTime updatedAt;
    QDateTime usedAt;
    QDateTime expiresAt;
    QString sourceApp;
    QString sourceWindowTitle;
    QString sourceUri;
    qsizetype sizeBytes = 0;
};

struct ClipCapturePolicy {
    qsizetype maxTextBytes = 256 * 1024;
    int maxTemporaryClips = 100;
    qint64 temporaryTtlSeconds = 24 * 60 * 60;
    bool capturePaused = false;
    QStringList excludedSourceApps;
    bool excludeSensitiveText = true;
    QStringList sensitiveTextMarkers;
};

struct ClipCaptureResult {
    ClipCaptureStatus status = ClipCaptureStatus::IgnoredBlank;
    std::optional<Clip> clip;

    bool captured() const;
};

struct ClipCandidateQuery {
    QString text;
    QString requiredTag;
    bool identityOnly = false;
    bool tagOnly = false;
    bool includeSaved = true;
    bool includeTemporary = false;
    bool includeDeleted = false;
    bool emptyQuery = false;
    int limit = -1;
};

struct ClipIdentityValidationResult {
    bool valid = true;
    QString conflictingClipId;
    QString conflictingValue;
    QString error;
};

ClipIdentityValidationResult validateClipIdentity(const QList<Clip> &clips,
                                                  const QString &clipId,
                                                  const QString &name,
                                                  const QStringList &aliases);

class InMemoryClipRepository {
public:
    ClipCaptureResult captureText(const QString &text,
                                  const ClipCapturePolicy &policy = {},
                                  const QString &sourceApp = {},
                                  const QDateTime &now = {});

    bool saveClip(const QString &id,
                  const QString &name,
                  const QStringList &aliases = {},
                  const QStringList &tags = {},
                  bool pinned = false,
                  const QDateTime &now = {});
    bool importSavedClip(const Clip &clip);
    bool upsertSavedClip(const Clip &clip);
    bool upsertPersistentClip(const Clip &clip);
    bool softDeleteSavedClip(const QString &id, const QDateTime &now = {});
    bool restoreClip(const QString &id, const QDateTime &now = {});
    bool permanentlyDeleteClip(const QString &id);
    bool markClipUsed(const QString &id, const QDateTime &now = {});
    void pruneTemporaryHistory(const ClipCapturePolicy &policy, const QDateTime &now = {});

    QList<Clip> clips() const;
    QList<Clip> temporaryClips() const;
    QList<Clip> savedClips() const;
    std::optional<Clip> findClip(const QString &id) const;

private:
    QList<Clip> clips_;
};

class SqliteClipRepository {
public:
    SqliteClipRepository();
    ~SqliteClipRepository();

    bool open(const QString &path);
    bool initialize();
    bool isOpen() const;
    QString lastError() const;

    ClipCaptureResult captureText(const QString &text,
                                  const ClipCapturePolicy &policy = {},
                                  const QString &sourceApp = {},
                                  const QDateTime &now = {});

    bool saveClip(const QString &id,
                  const QString &name,
                  const QStringList &aliases = {},
                  const QStringList &tags = {},
                  bool pinned = false,
                  const QDateTime &now = {});
    bool importSavedClip(const Clip &clip);
    bool upsertSavedClip(const Clip &clip);
    bool upsertPersistentClip(const Clip &clip);
    bool softDeleteSavedClip(const QString &id, const QDateTime &now = {});
    bool restoreClip(const QString &id, const QDateTime &now = {});
    bool permanentlyDeleteClip(const QString &id);
    bool markClipUsed(const QString &id, const QDateTime &now = {});
    void pruneTemporaryHistory(const ClipCapturePolicy &policy, const QDateTime &now = {});

    QList<Clip> clips() const;
    QList<Clip> temporaryClips() const;
    QList<Clip> savedClips() const;
    QList<Clip> searchCandidates(const ClipCandidateQuery &query) const;
    std::optional<Clip> findClip(const QString &id) const;
    QString databasePath() const;
    bool integrityCheck();
    bool backupDatabase(const QString &destinationPath);

private:
    bool execute(const QString &sql);
    bool recordMigration(int version, const QString &name);
    int schemaVersion() const;
    bool migrateToVersion2();
    bool ensureSearchSchema();
    bool rebuildMetadataIndex(const Clip &clip);
    bool rebuildAllMetadataIndexes();
    bool hasContentHash(const QString &contentHash) const;
    bool pruneTemporaryHistoryInternal(const ClipCapturePolicy &policy, const QDateTime &now);
    QList<Clip> readClips(const QString &whereClause = {}) const;
    Clip hydrateClip(QSqlQuery &query) const;
    void setLastError(const QString &message) const;

    QString connectionName_;
    QSqlDatabase database_;
    mutable QString lastError_;
};

QString clipTargetApp();
QString clipLocatorType();
QString clipTargetUri(const QString &clipId);
QString clipLocatorJson(const QString &clipId, ClipInsertMode mode = ClipInsertMode::Paste);
std::optional<Anchor> savedClipAnchor(const Clip &clip, ClipInsertMode mode = ClipInsertMode::Paste);

} // namespace Pinloom
