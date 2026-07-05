#include "pinloom/clip/ClipRepository.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <algorithm>

namespace Pinloom {

namespace {

QDateTime effectiveUtcNow(const QDateTime &now)
{
    return now.isValid() ? now.toUTC() : QDateTime::currentDateTimeUtc();
}

QString makeClipId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString contentHashForBytes(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString previewForText(const QString &text)
{
    const QString preview = text.simplified();
    constexpr qsizetype maxPreviewLength = 80;
    if (preview.size() <= maxPreviewLength) {
        return preview;
    }
    return preview.left(maxPreviewLength - 3) + QStringLiteral("...");
}

QStringList cleanStringList(const QStringList &values)
{
    QStringList cleaned;
    for (const QString &value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty()) {
            cleaned.append(trimmed);
        }
    }
    cleaned.removeDuplicates();
    return cleaned;
}

bool containsExcludedSourceApp(const QStringList &excludedSourceApps, const QString &sourceApp)
{
    const QString normalizedSourceApp = sourceApp.trimmed();
    if (normalizedSourceApp.isEmpty()) {
        return false;
    }

    for (const QString &excluded : excludedSourceApps) {
        if (excluded.trimmed().compare(normalizedSourceApp, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

const QStringList &defaultSensitiveTextMarkers()
{
    static const QStringList markers = {
        QStringLiteral("password="),
        QStringLiteral("password:"),
        QStringLiteral("passwd="),
        QStringLiteral("passwd:"),
        QStringLiteral("api_key"),
        QStringLiteral("api-key"),
        QStringLiteral("secret="),
        QStringLiteral("secret:"),
        QStringLiteral("token="),
        QStringLiteral("authorization: bearer"),
        QStringLiteral("-----BEGIN PRIVATE KEY-----"),
        QStringLiteral("-----BEGIN RSA PRIVATE KEY-----"),
        QStringLiteral("-----BEGIN EC PRIVATE KEY-----"),
        QStringLiteral("-----BEGIN OPENSSH PRIVATE KEY-----")
    };
    return markers;
}

bool containsSensitiveTextMarker(const QString &text, const QStringList &markers)
{
    for (const QString &marker : markers) {
        const QString normalizedMarker = marker.trimmed();
        if (!normalizedMarker.isEmpty() && text.contains(normalizedMarker, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool containsSensitiveText(const QString &text, const ClipCapturePolicy &policy)
{
    if (policy.excludeSensitiveText && containsSensitiveTextMarker(text, defaultSensitiveTextMarkers())) {
        return true;
    }

    return containsSensitiveTextMarker(text, policy.sensitiveTextMarkers);
}

QString clipKindToString(ClipKind kind)
{
    switch (kind) {
    case ClipKind::Text:
        return QStringLiteral("text");
    }
    return QStringLiteral("text");
}

ClipKind clipKindFromString(const QString &kind)
{
    Q_UNUSED(kind);
    return ClipKind::Text;
}

QString clipStateToString(ClipState state)
{
    switch (state) {
    case ClipState::Temporary:
        return QStringLiteral("temporary");
    case ClipState::Saved:
        return QStringLiteral("saved");
    case ClipState::Deleted:
        return QStringLiteral("deleted");
    }
    return QStringLiteral("temporary");
}

ClipState clipStateFromString(const QString &state)
{
    if (state == QLatin1String("saved")) {
        return ClipState::Saved;
    }
    if (state == QLatin1String("deleted")) {
        return ClipState::Deleted;
    }
    return ClipState::Temporary;
}

QStringList listFromStorage(const QString &stored)
{
    return cleanStringList(stored.split(QLatin1Char('\n'), Qt::SkipEmptyParts));
}

QString listToStorage(const QStringList &values)
{
    return cleanStringList(values).join(QLatin1Char('\n'));
}

QVariant dateTimeToStorageValue(const QDateTime &dateTime)
{
    if (!dateTime.isValid()) {
        return {};
    }
    return dateTime.toUTC().toString(Qt::ISODateWithMs);
}

QDateTime dateTimeFromStorageValue(const QVariant &value)
{
    if (value.isNull() || value.toString().trimmed().isEmpty()) {
        return {};
    }
    return QDateTime::fromString(value.toString(), Qt::ISODate).toUTC();
}

QStringList clipSchemaStatements()
{
    return {
        QStringLiteral("CREATE TABLE IF NOT EXISTS clip_schema_migrations ("
                       "version INTEGER PRIMARY KEY,"
                       "name TEXT NOT NULL,"
                       "applied_at TEXT NOT NULL"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS clips ("
                       "id TEXT PRIMARY KEY,"
                       "kind TEXT NOT NULL,"
                       "state TEXT NOT NULL,"
                       "text TEXT NOT NULL,"
                       "preview TEXT NOT NULL,"
                       "content_hash TEXT NOT NULL UNIQUE,"
                       "name TEXT,"
                       "aliases TEXT,"
                       "tags TEXT,"
                       "pinned INTEGER NOT NULL DEFAULT 0,"
                       "created_at TEXT NOT NULL,"
                       "updated_at TEXT NOT NULL,"
                       "used_at TEXT,"
                       "expires_at TEXT,"
                       "source_app TEXT,"
                       "size_bytes INTEGER NOT NULL DEFAULT 0"
                       ");"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_clips_state_created_at "
                       "ON clips(state, created_at);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_clips_state_expires_at "
                       "ON clips(state, expires_at);")
    };
}

int clipIndexById(const QList<Clip> &clips, const QString &id)
{
    for (int index = 0; index < clips.size(); ++index) {
        if (clips.at(index).id == id) {
            return index;
        }
    }
    return -1;
}

QString insertModeToString(ClipInsertMode mode)
{
    switch (mode) {
    case ClipInsertMode::Paste:
        return QStringLiteral("paste");
    }
    return QStringLiteral("paste");
}

std::optional<Clip> normalizedImportedSavedClip(const Clip &source, QString *error)
{
    Clip clip = source;
    clip.id = clip.id.trimmed();
    if (clip.id.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Saved clip id is required");
        }
        return std::nullopt;
    }

    if (clip.kind != ClipKind::Text) {
        if (error) {
            *error = QStringLiteral("Only text saved clips can be imported");
        }
        return std::nullopt;
    }

    if (clip.state != ClipState::Saved) {
        if (error) {
            *error = QStringLiteral("Only saved clips can be imported");
        }
        return std::nullopt;
    }

    if (clip.text.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("Saved clip text is required");
        }
        return std::nullopt;
    }

    const QByteArray bytes = clip.text.toUtf8();
    const QDateTime importedAt = QDateTime::currentDateTimeUtc();
    clip.state = ClipState::Saved;
    clip.preview = previewForText(clip.text);
    clip.contentHash = contentHashForBytes(bytes);
    clip.name = clip.name.trimmed().isEmpty() ? clip.preview : clip.name.trimmed();
    clip.aliases = cleanStringList(clip.aliases);
    clip.tags = cleanStringList(clip.tags);
    clip.createdAt = clip.createdAt.isValid() ? clip.createdAt.toUTC() : importedAt;
    clip.updatedAt = clip.updatedAt.isValid() ? clip.updatedAt.toUTC() : clip.createdAt;
    clip.usedAt = clip.usedAt.isValid() ? clip.usedAt.toUTC() : QDateTime{};
    clip.expiresAt = {};
    clip.sourceApp = clip.sourceApp.trimmed();
    clip.sizeBytes = bytes.size();
    return clip;
}

} // namespace

bool ClipCaptureResult::captured() const
{
    return status == ClipCaptureStatus::Captured && clip.has_value();
}

SqliteClipRepository::SqliteClipRepository()
    : connectionName_(QStringLiteral("pinloom_clip_%1").arg(QUuid::createUuid().toString(QUuid::Id128)))
{
}

SqliteClipRepository::~SqliteClipRepository()
{
    if (database_.isValid()) {
        database_.close();
        database_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

bool SqliteClipRepository::open(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Database path is required"));
        return false;
    }

    if (path != QLatin1String(":memory:")) {
        const QFileInfo databaseFile(path);
        QDir parentDir(databaseFile.absolutePath());
        if (!parentDir.exists() && !parentDir.mkpath(QStringLiteral("."))) {
            setLastError(QStringLiteral("Unable to create database directory: %1").arg(parentDir.absolutePath()));
            return false;
        }
    }

    if (database_.isValid()) {
        database_.close();
        database_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName_);
    }

    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    database_.setDatabaseName(path);

    if (!database_.open()) {
        setLastError(database_.lastError().text());
        return false;
    }

    if (!execute(QStringLiteral("PRAGMA foreign_keys = ON;"))) {
        return false;
    }

    lastError_.clear();
    return true;
}

bool SqliteClipRepository::initialize()
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    if (!database_.transaction()) {
        setLastError(database_.lastError().text());
        return false;
    }

    for (const QString &statement : clipSchemaStatements()) {
        if (!execute(statement)) {
            database_.rollback();
            return false;
        }
    }

    if (!recordMigration(1, QStringLiteral("initial_clip_text_schema"))) {
        database_.rollback();
        return false;
    }

    if (!database_.commit()) {
        setLastError(database_.lastError().text());
        return false;
    }

    lastError_.clear();
    return true;
}

bool SqliteClipRepository::isOpen() const
{
    return database_.isValid() && database_.isOpen();
}

QString SqliteClipRepository::lastError() const
{
    return lastError_;
}

ClipCaptureResult SqliteClipRepository::captureText(const QString &text,
                                                    const ClipCapturePolicy &policy,
                                                    const QString &sourceApp,
                                                    const QDateTime &now)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return {ClipCaptureStatus::IgnoredBlank, std::nullopt};
    }

    const QDateTime capturedAt = effectiveUtcNow(now);
    if (!pruneTemporaryHistoryInternal(policy, capturedAt)) {
        return {ClipCaptureStatus::IgnoredBlank, std::nullopt};
    }

    if (policy.capturePaused) {
        return {ClipCaptureStatus::IgnoredPaused, std::nullopt};
    }

    if (containsExcludedSourceApp(policy.excludedSourceApps, sourceApp)) {
        return {ClipCaptureStatus::IgnoredExcludedSource, std::nullopt};
    }

    if (text.trimmed().isEmpty()) {
        return {ClipCaptureStatus::IgnoredBlank, std::nullopt};
    }

    const QByteArray bytes = text.toUtf8();
    if (policy.maxTextBytes >= 0 && bytes.size() > policy.maxTextBytes) {
        return {ClipCaptureStatus::IgnoredTooLarge, std::nullopt};
    }

    if (containsSensitiveText(text, policy)) {
        return {ClipCaptureStatus::IgnoredSensitiveContent, std::nullopt};
    }

    const QString contentHash = contentHashForBytes(bytes);
    if (hasContentHash(contentHash)) {
        return {ClipCaptureStatus::IgnoredDuplicate, std::nullopt};
    }

    Clip clip;
    clip.id = makeClipId();
    clip.kind = ClipKind::Text;
    clip.state = ClipState::Temporary;
    clip.text = text;
    clip.preview = previewForText(text);
    clip.contentHash = contentHash;
    clip.createdAt = capturedAt;
    clip.updatedAt = capturedAt;
    clip.usedAt = capturedAt;
    clip.sourceApp = sourceApp.trimmed();
    clip.sizeBytes = bytes.size();
    if (policy.temporaryTtlSeconds > 0) {
        clip.expiresAt = capturedAt.addSecs(policy.temporaryTtlSeconds);
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO clips(id, kind, state, text, preview, content_hash, name, aliases, tags, "
                                 "pinned, created_at, updated_at, used_at, expires_at, source_app, size_bytes) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(clip.id);
    query.addBindValue(clipKindToString(clip.kind));
    query.addBindValue(clipStateToString(clip.state));
    query.addBindValue(clip.text);
    query.addBindValue(clip.preview);
    query.addBindValue(clip.contentHash);
    query.addBindValue(clip.name);
    query.addBindValue(listToStorage(clip.aliases));
    query.addBindValue(listToStorage(clip.tags));
    query.addBindValue(clip.pinned ? 1 : 0);
    query.addBindValue(dateTimeToStorageValue(clip.createdAt));
    query.addBindValue(dateTimeToStorageValue(clip.updatedAt));
    query.addBindValue(dateTimeToStorageValue(clip.usedAt));
    query.addBindValue(dateTimeToStorageValue(clip.expiresAt));
    query.addBindValue(clip.sourceApp);
    query.addBindValue(static_cast<qlonglong>(clip.sizeBytes));
    if (!query.exec()) {
        const QString errorText = query.lastError().text();
        setLastError(errorText);
        if (errorText.contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
            return {ClipCaptureStatus::IgnoredDuplicate, std::nullopt};
        }
        return {ClipCaptureStatus::IgnoredBlank, std::nullopt};
    }

    if (!pruneTemporaryHistoryInternal(policy, capturedAt)) {
        return {ClipCaptureStatus::IgnoredBlank, std::nullopt};
    }

    lastError_.clear();
    return {ClipCaptureStatus::Captured, clip};
}

bool SqliteClipRepository::saveClip(const QString &id,
                                    const QString &name,
                                    const QStringList &aliases,
                                    const QStringList &tags,
                                    bool pinned,
                                    const QDateTime &now)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    const std::optional<Clip> stored = findClip(id);
    if (!stored.has_value()) {
        setLastError(QStringLiteral("Clip not found"));
        return false;
    }

    const QString trimmedName = name.trimmed();
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE clips SET "
                                 "state = ?,"
                                 "name = ?,"
                                 "aliases = ?,"
                                 "tags = ?,"
                                 "pinned = ?,"
                                 "updated_at = ?,"
                                 "expires_at = NULL "
                                 "WHERE id = ?"));
    query.addBindValue(clipStateToString(ClipState::Saved));
    query.addBindValue(trimmedName.isEmpty() ? stored->preview : trimmedName);
    query.addBindValue(listToStorage(aliases));
    query.addBindValue(listToStorage(tags));
    query.addBindValue(pinned ? 1 : 0);
    query.addBindValue(dateTimeToStorageValue(effectiveUtcNow(now)));
    query.addBindValue(id);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }

    lastError_.clear();
    return true;
}

bool SqliteClipRepository::importSavedClip(const Clip &clip)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    QString error;
    const std::optional<Clip> normalized = normalizedImportedSavedClip(clip, &error);
    if (!normalized.has_value()) {
        setLastError(error);
        return false;
    }

    if (findClip(normalized->id).has_value()) {
        setLastError(QStringLiteral("Clip already exists"));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO clips(id, kind, state, text, preview, content_hash, name, aliases, tags, "
                                 "pinned, created_at, updated_at, used_at, expires_at, source_app, size_bytes) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(normalized->id);
    query.addBindValue(clipKindToString(normalized->kind));
    query.addBindValue(clipStateToString(normalized->state));
    query.addBindValue(normalized->text);
    query.addBindValue(normalized->preview);
    query.addBindValue(normalized->contentHash);
    query.addBindValue(normalized->name);
    query.addBindValue(listToStorage(normalized->aliases));
    query.addBindValue(listToStorage(normalized->tags));
    query.addBindValue(normalized->pinned ? 1 : 0);
    query.addBindValue(dateTimeToStorageValue(normalized->createdAt));
    query.addBindValue(dateTimeToStorageValue(normalized->updatedAt));
    query.addBindValue(dateTimeToStorageValue(normalized->usedAt));
    query.addBindValue(dateTimeToStorageValue(normalized->expiresAt));
    query.addBindValue(normalized->sourceApp);
    query.addBindValue(static_cast<qlonglong>(normalized->sizeBytes));
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }

    lastError_.clear();
    return true;
}

bool SqliteClipRepository::softDeleteSavedClip(const QString &id, const QDateTime &now)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    const std::optional<Clip> stored = findClip(id);
    if (!stored.has_value()) {
        setLastError(QStringLiteral("Clip not found"));
        return false;
    }
    if (stored->state != ClipState::Saved) {
        setLastError(QStringLiteral("Only Saved Clips can be deleted"));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE clips SET state = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(clipStateToString(ClipState::Deleted));
    query.addBindValue(dateTimeToStorageValue(effectiveUtcNow(now)));
    query.addBindValue(id);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }

    lastError_.clear();
    return true;
}

bool SqliteClipRepository::restoreClip(const QString &id, const QDateTime &now)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    const std::optional<Clip> stored = findClip(id);
    if (!stored.has_value()) {
        setLastError(QStringLiteral("Clip not found"));
        return false;
    }
    if (stored->state != ClipState::Deleted) {
        setLastError(QStringLiteral("Only deleted clips can be restored"));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE clips SET state = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(clipStateToString(ClipState::Saved));
    query.addBindValue(dateTimeToStorageValue(effectiveUtcNow(now)));
    query.addBindValue(id);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }

    lastError_.clear();
    return true;
}

bool SqliteClipRepository::markClipUsed(const QString &id, const QDateTime &now)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    if (!findClip(id).has_value()) {
        setLastError(QStringLiteral("Clip not found"));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE clips SET used_at = ? WHERE id = ?"));
    query.addBindValue(dateTimeToStorageValue(effectiveUtcNow(now)));
    query.addBindValue(id);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }

    lastError_.clear();
    return true;
}

void SqliteClipRepository::pruneTemporaryHistory(const ClipCapturePolicy &policy, const QDateTime &now)
{
    pruneTemporaryHistoryInternal(policy, now);
}

QList<Clip> SqliteClipRepository::clips() const
{
    return readClips();
}

QList<Clip> SqliteClipRepository::temporaryClips() const
{
    return readClips(QStringLiteral("state = 'temporary'"));
}

QList<Clip> SqliteClipRepository::savedClips() const
{
    return readClips(QStringLiteral("state = 'saved'"));
}

std::optional<Clip> SqliteClipRepository::findClip(const QString &id) const
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return std::nullopt;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT id, kind, state, text, preview, content_hash, name, aliases, tags, "
                                 "pinned, created_at, updated_at, used_at, expires_at, source_app, size_bytes "
                                 "FROM clips WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    return hydrateClip(query);
}

bool SqliteClipRepository::execute(const QString &sql)
{
    QSqlQuery query(database_);
    if (!query.exec(sql)) {
        setLastError(query.lastError().text());
        return false;
    }
    return true;
}

bool SqliteClipRepository::recordMigration(int version, const QString &name)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO clip_schema_migrations(version, name, applied_at) "
                                 "VALUES (?, ?, ?)"));
    query.addBindValue(version);
    query.addBindValue(name);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
    return true;
}

bool SqliteClipRepository::hasContentHash(const QString &contentHash) const
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT 1 FROM clips WHERE content_hash = ? LIMIT 1"));
    query.addBindValue(contentHash);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
    return query.next();
}

bool SqliteClipRepository::pruneTemporaryHistoryInternal(const ClipCapturePolicy &policy, const QDateTime &now)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    if (!database_.transaction()) {
        setLastError(database_.lastError().text());
        return false;
    }

    const QDateTime pruneAt = effectiveUtcNow(now);
    QSqlQuery deleteExpired(database_);
    deleteExpired.prepare(QStringLiteral("DELETE FROM clips "
                                         "WHERE state = ? "
                                         "AND expires_at IS NOT NULL "
                                         "AND expires_at <= ?"));
    deleteExpired.addBindValue(clipStateToString(ClipState::Temporary));
    deleteExpired.addBindValue(dateTimeToStorageValue(pruneAt));
    if (!deleteExpired.exec()) {
        setLastError(deleteExpired.lastError().text());
        database_.rollback();
        return false;
    }

    if (policy.maxTemporaryClips >= 0) {
        QSqlQuery deleteOverflow(database_);
        deleteOverflow.prepare(QStringLiteral("DELETE FROM clips "
                                             "WHERE state = ? "
                                             "AND id NOT IN ("
                                             "SELECT id FROM clips "
                                             "WHERE state = ? "
                                             "ORDER BY created_at DESC, id DESC "
                                             "LIMIT ?)"));
        deleteOverflow.addBindValue(clipStateToString(ClipState::Temporary));
        deleteOverflow.addBindValue(clipStateToString(ClipState::Temporary));
        deleteOverflow.addBindValue(policy.maxTemporaryClips);
        if (!deleteOverflow.exec()) {
            setLastError(deleteOverflow.lastError().text());
            database_.rollback();
            return false;
        }
    }

    if (!database_.commit()) {
        setLastError(database_.lastError().text());
        return false;
    }

    lastError_.clear();
    return true;
}

QList<Clip> SqliteClipRepository::readClips(const QString &whereClause) const
{
    QList<Clip> results;
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return results;
    }

    QString sql = QStringLiteral("SELECT id, kind, state, text, preview, content_hash, name, aliases, tags, "
                                 "pinned, created_at, updated_at, used_at, expires_at, source_app, size_bytes "
                                 "FROM clips");
    if (!whereClause.trimmed().isEmpty()) {
        sql += QStringLiteral(" WHERE ") + whereClause;
    }
    sql += QStringLiteral(" ORDER BY created_at ASC, id ASC");

    QSqlQuery query(database_);
    if (!query.exec(sql)) {
        setLastError(query.lastError().text());
        return results;
    }

    while (query.next()) {
        results.append(hydrateClip(query));
    }

    return results;
}

Clip SqliteClipRepository::hydrateClip(QSqlQuery &query) const
{
    Clip clip;
    clip.id = query.value(0).toString();
    clip.kind = clipKindFromString(query.value(1).toString());
    clip.state = clipStateFromString(query.value(2).toString());
    clip.text = query.value(3).toString();
    clip.preview = query.value(4).toString();
    clip.contentHash = query.value(5).toString();
    clip.name = query.value(6).toString();
    clip.aliases = listFromStorage(query.value(7).toString());
    clip.tags = listFromStorage(query.value(8).toString());
    clip.pinned = query.value(9).toInt() != 0;
    clip.createdAt = dateTimeFromStorageValue(query.value(10));
    clip.updatedAt = dateTimeFromStorageValue(query.value(11));
    clip.usedAt = dateTimeFromStorageValue(query.value(12));
    clip.expiresAt = dateTimeFromStorageValue(query.value(13));
    clip.sourceApp = query.value(14).toString();
    clip.sizeBytes = static_cast<qsizetype>(query.value(15).toLongLong());
    return clip;
}

void SqliteClipRepository::setLastError(const QString &message) const
{
    lastError_ = message;
}

ClipCaptureResult InMemoryClipRepository::captureText(const QString &text,
                                                      const ClipCapturePolicy &policy,
                                                      const QString &sourceApp,
                                                      const QDateTime &now)
{
    const QDateTime capturedAt = effectiveUtcNow(now);
    pruneTemporaryHistory(policy, capturedAt);

    if (policy.capturePaused) {
        return {ClipCaptureStatus::IgnoredPaused, std::nullopt};
    }

    if (containsExcludedSourceApp(policy.excludedSourceApps, sourceApp)) {
        return {ClipCaptureStatus::IgnoredExcludedSource, std::nullopt};
    }

    if (text.trimmed().isEmpty()) {
        return {ClipCaptureStatus::IgnoredBlank, std::nullopt};
    }

    const QByteArray bytes = text.toUtf8();
    if (policy.maxTextBytes >= 0 && bytes.size() > policy.maxTextBytes) {
        return {ClipCaptureStatus::IgnoredTooLarge, std::nullopt};
    }

    if (containsSensitiveText(text, policy)) {
        return {ClipCaptureStatus::IgnoredSensitiveContent, std::nullopt};
    }

    const QString contentHash = contentHashForBytes(bytes);
    const bool duplicate = std::any_of(clips_.cbegin(), clips_.cend(), [&](const Clip &clip) {
        return clip.contentHash == contentHash;
    });
    if (duplicate) {
        return {ClipCaptureStatus::IgnoredDuplicate, std::nullopt};
    }

    Clip clip;
    clip.id = makeClipId();
    clip.kind = ClipKind::Text;
    clip.state = ClipState::Temporary;
    clip.text = text;
    clip.preview = previewForText(text);
    clip.contentHash = contentHash;
    clip.createdAt = capturedAt;
    clip.updatedAt = capturedAt;
    clip.usedAt = capturedAt;
    clip.sourceApp = sourceApp.trimmed();
    clip.sizeBytes = bytes.size();
    if (policy.temporaryTtlSeconds > 0) {
        clip.expiresAt = capturedAt.addSecs(policy.temporaryTtlSeconds);
    }

    clips_.append(clip);
    pruneTemporaryHistory(policy, capturedAt);

    return {ClipCaptureStatus::Captured, clip};
}

bool InMemoryClipRepository::saveClip(const QString &id,
                                      const QString &name,
                                      const QStringList &aliases,
                                      const QStringList &tags,
                                      bool pinned,
                                      const QDateTime &now)
{
    const int index = clipIndexById(clips_, id);
    if (index < 0) {
        return false;
    }

    Clip &clip = clips_[index];
    const QString trimmedName = name.trimmed();
    clip.state = ClipState::Saved;
    clip.name = trimmedName.isEmpty() ? clip.preview : trimmedName;
    clip.aliases = cleanStringList(aliases);
    clip.tags = cleanStringList(tags);
    clip.pinned = pinned;
    clip.updatedAt = effectiveUtcNow(now);
    clip.expiresAt = {};
    return true;
}

bool InMemoryClipRepository::importSavedClip(const Clip &clip)
{
    QString error;
    const std::optional<Clip> normalized = normalizedImportedSavedClip(clip, &error);
    if (!normalized.has_value()) {
        return false;
    }

    if (clipIndexById(clips_, normalized->id) >= 0) {
        return false;
    }

    clips_.append(*normalized);
    return true;
}

bool InMemoryClipRepository::softDeleteSavedClip(const QString &id, const QDateTime &now)
{
    const int index = clipIndexById(clips_, id);
    if (index < 0 || clips_.at(index).state != ClipState::Saved) {
        return false;
    }

    clips_[index].state = ClipState::Deleted;
    clips_[index].updatedAt = effectiveUtcNow(now);
    return true;
}

bool InMemoryClipRepository::restoreClip(const QString &id, const QDateTime &now)
{
    const int index = clipIndexById(clips_, id);
    if (index < 0 || clips_.at(index).state != ClipState::Deleted) {
        return false;
    }

    clips_[index].state = ClipState::Saved;
    clips_[index].updatedAt = effectiveUtcNow(now);
    return true;
}

bool InMemoryClipRepository::markClipUsed(const QString &id, const QDateTime &now)
{
    const int index = clipIndexById(clips_, id);
    if (index < 0) {
        return false;
    }

    clips_[index].usedAt = effectiveUtcNow(now);
    return true;
}

void InMemoryClipRepository::pruneTemporaryHistory(const ClipCapturePolicy &policy, const QDateTime &now)
{
    const QDateTime pruneAt = effectiveUtcNow(now);
    QSet<QString> removeIds;
    QList<Clip> retainedTemporaryClips;

    for (const Clip &clip : clips_) {
        if (clip.state != ClipState::Temporary) {
            continue;
        }
        if (clip.expiresAt.isValid() && clip.expiresAt.toUTC() <= pruneAt) {
            removeIds.insert(clip.id);
            continue;
        }
        retainedTemporaryClips.append(clip);
    }

    if (policy.maxTemporaryClips >= 0 && retainedTemporaryClips.size() > policy.maxTemporaryClips) {
        std::sort(retainedTemporaryClips.begin(), retainedTemporaryClips.end(), [](const Clip &left, const Clip &right) {
            if (left.createdAt == right.createdAt) {
                return left.id > right.id;
            }
            return left.createdAt > right.createdAt;
        });

        for (int index = policy.maxTemporaryClips; index < retainedTemporaryClips.size(); ++index) {
            removeIds.insert(retainedTemporaryClips.at(index).id);
        }
    }

    if (removeIds.isEmpty()) {
        return;
    }

    for (int index = clips_.size() - 1; index >= 0; --index) {
        if (removeIds.contains(clips_.at(index).id)) {
            clips_.removeAt(index);
        }
    }
}

QList<Clip> InMemoryClipRepository::clips() const
{
    return clips_;
}

QList<Clip> InMemoryClipRepository::temporaryClips() const
{
    QList<Clip> temporary;
    for (const Clip &clip : clips_) {
        if (clip.state == ClipState::Temporary) {
            temporary.append(clip);
        }
    }
    return temporary;
}

QList<Clip> InMemoryClipRepository::savedClips() const
{
    QList<Clip> saved;
    for (const Clip &clip : clips_) {
        if (clip.state == ClipState::Saved) {
            saved.append(clip);
        }
    }
    return saved;
}

std::optional<Clip> InMemoryClipRepository::findClip(const QString &id) const
{
    const int index = clipIndexById(clips_, id);
    if (index < 0) {
        return std::nullopt;
    }
    return clips_.at(index);
}

QString clipTargetApp()
{
    return QStringLiteral("pinloom.clip");
}

QString clipLocatorType()
{
    return QStringLiteral("clip.insert");
}

QString clipTargetUri(const QString &clipId)
{
    return QStringLiteral("clip://%1").arg(clipId);
}

QString clipLocatorJson(const QString &clipId, ClipInsertMode mode)
{
    QJsonObject locator;
    locator.insert(QStringLiteral("clip_id"), clipId);
    locator.insert(QStringLiteral("mode"), insertModeToString(mode));
    return QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));
}

std::optional<Anchor> savedClipAnchor(const Clip &clip, ClipInsertMode mode)
{
    if (clip.state != ClipState::Saved || clip.id.trimmed().isEmpty()) {
        return std::nullopt;
    }

    Anchor anchor;
    anchor.type = AnchorType::Manual;
    anchor.id = QStringLiteral("clip:%1").arg(clip.id);
    anchor.name = clip.name.trimmed().isEmpty() ? clip.preview : clip.name.trimmed();
    anchor.target = anchor.name;
    anchor.targetApp = clipTargetApp();
    anchor.targetUri = clipTargetUri(clip.id);
    anchor.locatorType = clipLocatorType();
    anchor.locatorJson = clipLocatorJson(clip.id, mode);
    anchor.aliases = clip.aliases;
    anchor.tags = clip.tags;
    anchor.pinned = clip.pinned;
    anchor.createdAt = clip.createdAt;
    anchor.updatedAt = clip.updatedAt;
    anchor.usedAt = clip.usedAt;
    return anchor;
}

} // namespace Pinloom
