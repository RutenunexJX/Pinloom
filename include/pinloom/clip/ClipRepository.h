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
    bool pinned = false;
    QDateTime createdAt;
    QDateTime updatedAt;
    QDateTime usedAt;
    QDateTime expiresAt;
    QString sourceApp;
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
    bool softDeleteSavedClip(const QString &id, const QDateTime &now = {});
    bool restoreClip(const QString &id, const QDateTime &now = {});
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
    bool softDeleteSavedClip(const QString &id, const QDateTime &now = {});
    bool restoreClip(const QString &id, const QDateTime &now = {});
    bool markClipUsed(const QString &id, const QDateTime &now = {});
    void pruneTemporaryHistory(const ClipCapturePolicy &policy, const QDateTime &now = {});

    QList<Clip> clips() const;
    QList<Clip> temporaryClips() const;
    QList<Clip> savedClips() const;
    std::optional<Clip> findClip(const QString &id) const;

private:
    bool execute(const QString &sql);
    bool recordMigration(int version, const QString &name);
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
