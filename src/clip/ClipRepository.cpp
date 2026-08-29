#include "pinloom/clip/ClipRepository.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
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
        if (!trimmed.isEmpty()
            && !std::any_of(cleaned.cbegin(), cleaned.cend(), [&trimmed](const QString &existing) {
                   return existing.compare(trimmed, Qt::CaseInsensitive) == 0;
               })) {
            cleaned.append(trimmed);
        }
    }
    return cleaned;
}

QString normalizedIdentity(const QString &value)
{
    return normalizedGlobalIdentity(value);
}

QStringList nonEmptyIdentityValues(const QStringList &values)
{
    QStringList retained;
    for (const QString &value : values) {
        if (!value.trimmed().isEmpty()) {
            retained.append(value);
        }
    }
    return retained;
}

QStringList clipIdentityValues(const QString &name, const QStringList &aliases)
{
    QStringList values;
    if (!name.trimmed().isEmpty()) {
        values.append(name);
    }
    for (const QString &alias : aliases) {
        if (!alias.trimmed().isEmpty()) {
            values.append(alias);
        }
    }
    return values;
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

QString clipActionTypeToString(ClipActionType actionType)
{
    switch (actionType) {
    case ClipActionType::InsertText:
        return QStringLiteral("insert_text");
    case ClipActionType::OpenWebUrl:
        return QStringLiteral("open_web_url");
    }
    return QStringLiteral("insert_text");
}

ClipActionType clipActionTypeFromString(const QString &actionType)
{
    return actionType.compare(QStringLiteral("open_web_url"), Qt::CaseInsensitive) == 0
        ? ClipActionType::OpenWebUrl
        : ClipActionType::InsertText;
}

QString clipStorageBackendToString(ClipStorageBackend backend)
{
    switch (backend) {
    case ClipStorageBackend::Local:
        return QStringLiteral("local");
    case ClipStorageBackend::Obsidian:
        return QStringLiteral("obsidian");
    }
    return QStringLiteral("local");
}

ClipStorageBackend clipStorageBackendFromString(const QString &backend)
{
    return backend.compare(QStringLiteral("obsidian"), Qt::CaseInsensitive) == 0
        ? ClipStorageBackend::Obsidian
        : ClipStorageBackend::Local;
}

QStringList listFromStorage(const QString &stored)
{
    return cleanStringList(stored.split(QLatin1Char('\n'), Qt::SkipEmptyParts));
}

QString listToStorage(const QStringList &values)
{
    return cleanStringList(values).join(QLatin1Char('\n'));
}

QStringList identityListFromStorage(const QString &stored)
{
    return stored.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QString identityListToStorage(const QStringList &values)
{
    return nonEmptyIdentityValues(values).join(QLatin1Char('\n'));
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

QStringList latestClipSchemaStatements()
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
                       "content_hash TEXT NOT NULL,"
                       "name TEXT,"
                       "aliases TEXT,"
                       "tags TEXT,"
                       "action_type TEXT NOT NULL DEFAULT 'insert_text',"
                       "storage_backend TEXT NOT NULL DEFAULT 'local',"
                       "pinned INTEGER NOT NULL DEFAULT 0,"
                       "created_at TEXT NOT NULL,"
                       "updated_at TEXT NOT NULL,"
                       "used_at TEXT,"
                       "expires_at TEXT,"
                       "source_app TEXT,"
                       "source_window_title TEXT,"
                       "source_uri TEXT,"
                       "size_bytes INTEGER NOT NULL DEFAULT 0"
                       ");"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_clips_content_hash "
                       "ON clips(content_hash);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_clips_state_created_at "
                       "ON clips(state, created_at);"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_clips_state_expires_at "
                       "ON clips(state, expires_at);"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS clip_identities ("
                       "identity_key TEXT NOT NULL,"
                       "clip_id TEXT NOT NULL,"
                       "identity_value TEXT NOT NULL,"
                       "identity_kind TEXT NOT NULL,"
                       "PRIMARY KEY(identity_key, clip_id, identity_kind),"
                       "FOREIGN KEY(clip_id) REFERENCES clips(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_clip_identities_clip_id "
                       "ON clip_identities(clip_id);"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS clip_tags ("
                       "clip_id TEXT NOT NULL,"
                       "tag TEXT NOT NULL,"
                       "tag_key TEXT NOT NULL,"
                       "PRIMARY KEY(clip_id, tag_key),"
                       "FOREIGN KEY(clip_id) REFERENCES clips(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_clip_tags_tag_key "
                       "ON clip_tags(tag_key, clip_id);"),
        QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS clip_fts USING fts5("
                       "clip_id UNINDEXED, name, aliases, tags, preview, text,"
                       "tokenize='unicode61 remove_diacritics 2'"
                       ");")
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

std::optional<Clip> normalizedPersistentClip(const Clip &source, QString *error)
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

    if (clip.state != ClipState::Saved && clip.state != ClipState::Deleted) {
        if (error) {
            *error = QStringLiteral("Only saved or deleted clips can be imported");
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
    clip.preview = previewForText(clip.text);
    clip.contentHash = contentHashForBytes(bytes);
    clip.name = clip.name.trimmed().isEmpty() ? clip.preview : clip.name;
    clip.aliases = nonEmptyIdentityValues(clip.aliases);
    clip.tags = cleanStringList(clip.tags);
    clip.createdAt = clip.createdAt.isValid() ? clip.createdAt.toUTC() : importedAt;
    clip.updatedAt = clip.updatedAt.isValid() ? clip.updatedAt.toUTC() : clip.createdAt;
    clip.usedAt = clip.usedAt.isValid() ? clip.usedAt.toUTC() : QDateTime{};
    clip.expiresAt = {};
    clip.sourceApp = clip.sourceApp.trimmed();
    clip.sourceWindowTitle = clip.sourceWindowTitle.trimmed();
    clip.sourceUri = clip.sourceUri.trimmed();
    clip.sizeBytes = bytes.size();
    return clip;
}

GlobalIdentityObject identityObjectForClip(const Clip &clip)
{
    GlobalIdentityObject object;
    object.owner.objectType = GlobalIdentityObjectType::Clip;
    object.owner.objectId = clip.id;
    object.owner.displayName = clip.name;
    object.owner.locator = globalIdentityLocator(GlobalIdentityObjectType::Clip,
                                                 clip.id);
    object.active = clip.state == ClipState::Saved;
    object.values.append({GlobalIdentityFieldKind::Name, clip.name});
    for (const QString &alias : clip.aliases) {
        object.values.append({GlobalIdentityFieldKind::Alias, alias});
    }
    return object;
}

} // namespace

bool ClipCaptureResult::captured() const
{
    return status == ClipCaptureStatus::Captured && clip.has_value();
}

ClipIdentityValidationResult validateClipIdentity(const QList<Clip> &clips,
                                                  const QString &clipId,
                                                  const QString &name,
                                                  const QStringList &aliases)
{
    ClipIdentityValidationResult result;
    const QStringList candidateValues = clipIdentityValues(name, aliases);
    QHash<QString, GlobalIdentityClaim> candidateClaims;
    for (int index = 0; index < candidateValues.size(); ++index) {
        const QString &value = candidateValues.at(index);
        const QString key = normalizedIdentity(value);
        if (key.isEmpty()) {
            continue;
        }
        GlobalIdentityClaim claim;
        claim.normalizedValue = key;
        claim.owner.objectType = GlobalIdentityObjectType::Clip;
        claim.owner.objectId = clipId;
        claim.owner.displayName = name;
        claim.owner.locator = globalIdentityLocator(GlobalIdentityObjectType::Clip,
                                                    clipId);
        claim.fieldKind = index == 0 ? GlobalIdentityFieldKind::Name
                                     : GlobalIdentityFieldKind::Alias;
        claim.displayValue = value;
        claim.fieldIndex = index;
        if (candidateClaims.contains(key)) {
            result.valid = false;
            result.conflictingValue = value;
            GlobalIdentityConflict conflict;
            conflict.normalizedValue = key;
            conflict.attemptedClaim = claim;
            conflict.conflictingClaims = {candidateClaims.value(key)};
            result.conflict = conflict;
            result.error = conflict.message();
            return result;
        }
        candidateClaims.insert(key, claim);
    }
    for (const Clip &existing : clips) {
        if (existing.id == clipId || existing.state != ClipState::Saved) {
            continue;
        }
        const QStringList existingValues = clipIdentityValues(existing.name, existing.aliases);
        for (int index = 0; index < existingValues.size(); ++index) {
            const QString &value = existingValues.at(index);
            const QString key = normalizedIdentity(value);
            if (!candidateClaims.contains(key)) {
                continue;
            }
            result.valid = false;
            result.conflictingClipId = existing.id;
            result.conflictingValue = value;
            GlobalIdentityClaim existingClaim;
            existingClaim.normalizedValue = key;
            existingClaim.owner.objectType = GlobalIdentityObjectType::Clip;
            existingClaim.owner.objectId = existing.id;
            existingClaim.owner.displayName = existing.name;
            existingClaim.owner.locator = globalIdentityLocator(
                GlobalIdentityObjectType::Clip, existing.id);
            existingClaim.fieldKind = index == 0 ? GlobalIdentityFieldKind::Name
                                                  : GlobalIdentityFieldKind::Alias;
            existingClaim.displayValue = value;
            existingClaim.fieldIndex = index;
            GlobalIdentityConflict conflict;
            conflict.normalizedValue = key;
            conflict.attemptedClaim = candidateClaims.value(key);
            conflict.conflictingClaims = {existingClaim};
            result.conflict = conflict;
            result.error = conflict.message();
            return result;
        }
    }
    return result;
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

bool SqliteClipRepository::open(const QString &path, const QString &identityRegistryPath)
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
    identityRegistryPath_ = identityRegistryPath.trimmed().isEmpty()
        ? defaultGlobalIdentityRegistryPath(path)
        : (identityRegistryPath == QLatin1String(":memory:")
               ? QStringLiteral(":memory:")
               : QFileInfo(identityRegistryPath).absoluteFilePath());

    if (!database_.open()) {
        setLastError(database_.lastError().text());
        return false;
    }

    if (!execute(QStringLiteral("PRAGMA foreign_keys = ON;"))) {
        return false;
    }
    if (!execute(QStringLiteral("PRAGMA busy_timeout = 5000;"))
        || !attachGlobalIdentityRegistry()) {
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

    QString identityError;
    if (!initializeSqliteGlobalIdentityRegistry(database_, &identityError)) {
        setLastError(identityError);
        database_.rollback();
        return false;
    }

    if (!execute(QStringLiteral("CREATE TABLE IF NOT EXISTS clip_schema_migrations ("
                                "version INTEGER PRIMARY KEY,"
                                "name TEXT NOT NULL,"
                                "applied_at TEXT NOT NULL"
                                ");"))) {
        database_.rollback();
        return false;
    }

    int version = schemaVersion();
    if (version < 0) {
        database_.rollback();
        return false;
    }
    if (version == 0) {
        for (const QString &statement : latestClipSchemaStatements()) {
            if (!execute(statement)) {
                database_.rollback();
                return false;
            }
        }
        if (!recordMigration(1, QStringLiteral("initial_clip_text_schema"))
            || !recordMigration(2, QStringLiteral("persistent_clip_identity_and_provenance"))
            || !recordMigration(3, QStringLiteral("clip_identity_tag_and_fts_indexes"))
            || !recordMigration(4, QStringLiteral("global_name_alias_identity_registry"))) {
            database_.rollback();
            return false;
        }
    } else {
        if (version < 2 && !migrateToVersion2()) {
            database_.rollback();
            return false;
        }
        if (!ensureSearchSchema()) {
            database_.rollback();
            return false;
        }
        if (version < 4 && !migrateIdentityIndexToVersion4()) {
            database_.rollback();
            return false;
        }
        if (version < 3
            && !recordMigration(3, QStringLiteral("clip_identity_tag_and_fts_indexes"))) {
            database_.rollback();
            return false;
        }
    }

    if (!rebuildGlobalIdentityRegistry()) {
        database_.rollback();
        return false;
    }

    if (!database_.commit()) {
        setLastError(database_.lastError().text());
        database_.rollback();
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

std::optional<GlobalIdentityConflict> SqliteClipRepository::lastIdentityConflict() const
{
    return lastIdentityConflict_;
}

QList<GlobalIdentityConflict> SqliteClipRepository::identityConflicts() const
{
    QString error;
    QSqlDatabase database = database_;
    const QList<GlobalIdentityConflict> conflicts = sqliteGlobalIdentityConflicts(database, &error);
    if (!error.isEmpty()) {
        setLastError(error);
    }
    return conflicts;
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

    if (!database_.transaction()) {
        setLastError(database_.lastError().text());
        return {ClipCaptureStatus::IgnoredBlank, std::nullopt};
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO clips("
                                 "id, kind, state, text, preview, content_hash, name, aliases, tags, "
                                 "action_type, storage_backend, pinned, created_at, updated_at, used_at, "
                                 "expires_at, source_app, source_window_title, source_uri, size_bytes) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(clip.id);
    query.addBindValue(clipKindToString(clip.kind));
    query.addBindValue(clipStateToString(clip.state));
    query.addBindValue(clip.text);
    query.addBindValue(clip.preview);
    query.addBindValue(clip.contentHash);
    query.addBindValue(clip.name);
    query.addBindValue(identityListToStorage(clip.aliases));
    query.addBindValue(listToStorage(clip.tags));
    query.addBindValue(clipActionTypeToString(clip.actionType));
    query.addBindValue(clipStorageBackendToString(clip.storageBackend));
    query.addBindValue(clip.pinned ? 1 : 0);
    query.addBindValue(dateTimeToStorageValue(clip.createdAt));
    query.addBindValue(dateTimeToStorageValue(clip.updatedAt));
    query.addBindValue(dateTimeToStorageValue(clip.usedAt));
    query.addBindValue(dateTimeToStorageValue(clip.expiresAt));
    query.addBindValue(clip.sourceApp);
    query.addBindValue(clip.sourceWindowTitle);
    query.addBindValue(clip.sourceUri);
    query.addBindValue(static_cast<qlonglong>(clip.sizeBytes));
    if (!query.exec()) {
        const QString errorText = query.lastError().text();
        database_.rollback();
        setLastError(errorText);
        if (errorText.contains(QLatin1String("UNIQUE"), Qt::CaseInsensitive)) {
            return {ClipCaptureStatus::IgnoredDuplicate, std::nullopt};
        }
        return {ClipCaptureStatus::IgnoredBlank, std::nullopt};
    }
    if (!rebuildMetadataIndex(clip) || !database_.commit()) {
        const QString errorText = lastError_.trimmed().isEmpty()
            ? database_.lastError().text()
            : lastError_;
        database_.rollback();
        setLastError(errorText);
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

    Clip updated = stored.value();
    updated.state = ClipState::Saved;
    updated.name = name.trimmed().isEmpty() ? stored->preview : name;
    updated.aliases = aliases;
    updated.tags = tags;
    updated.pinned = pinned;
    updated.updatedAt = effectiveUtcNow(now);
    updated.expiresAt = {};
    return upsertPersistentClip(updated);
}

bool SqliteClipRepository::importSavedClip(const Clip &clip)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    if (clip.state != ClipState::Saved) {
        setLastError(QStringLiteral("Only saved clips can be imported"));
        return false;
    }
    if (findClip(clip.id).has_value()) {
        setLastError(QStringLiteral("Clip already exists"));
        return false;
    }
    return upsertPersistentClip(clip);
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

    Clip updated = stored.value();
    updated.state = ClipState::Deleted;
    updated.updatedAt = effectiveUtcNow(now);
    return upsertPersistentClip(updated);
}

bool SqliteClipRepository::upsertSavedClip(const Clip &clip)
{
    if (clip.state != ClipState::Saved) {
        setLastError(QStringLiteral("Only saved clips can be upserted"));
        return false;
    }
    return upsertPersistentClip(clip);
}

bool SqliteClipRepository::upsertPersistentClip(const Clip &clip)
{
    lastError_.clear();
    lastIdentityConflict_.reset();
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    QString error;
    const std::optional<Clip> normalized = normalizedPersistentClip(clip, &error);
    if (!normalized.has_value()) {
        setLastError(error);
        return false;
    }

    if (normalized->state == ClipState::Saved) {
        const ClipIdentityValidationResult identity =
            validateClipIdentity(clips(), normalized->id, normalized->name, normalized->aliases);
        if (!identity.valid) {
            lastIdentityConflict_ = identity.conflict;
            setLastError(identity.error);
            return false;
        }
    }

    if (!database_.transaction()) {
        setLastError(database_.lastError().text());
        return false;
    }
    QString identityError;
    if (!replaceSqliteGlobalIdentityObjects(database_,
                                            {identityObjectForClip(normalized.value())},
                                            &lastIdentityConflict_,
                                            &identityError)) {
        database_.rollback();
        setLastError(identityError);
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO clips("
                                 "id, kind, state, text, preview, content_hash, name, aliases, tags, "
                                 "action_type, storage_backend, pinned, created_at, updated_at, used_at, "
                                 "expires_at, source_app, source_window_title, source_uri, size_bytes) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULL, ?, ?, ?, ?) "
                                 "ON CONFLICT(id) DO UPDATE SET "
                                 "kind = excluded.kind, state = excluded.state, text = excluded.text, "
                                 "preview = excluded.preview, content_hash = excluded.content_hash, "
                                 "name = excluded.name, aliases = excluded.aliases, tags = excluded.tags, "
                                 "action_type = excluded.action_type, storage_backend = excluded.storage_backend, "
                                 "pinned = excluded.pinned, created_at = excluded.created_at, "
                                 "updated_at = excluded.updated_at, used_at = excluded.used_at, expires_at = NULL, "
                                 "source_app = excluded.source_app, "
                                 "source_window_title = excluded.source_window_title, "
                                 "source_uri = excluded.source_uri, size_bytes = excluded.size_bytes"));
    query.addBindValue(normalized->id);
    query.addBindValue(clipKindToString(normalized->kind));
    query.addBindValue(clipStateToString(normalized->state));
    query.addBindValue(normalized->text);
    query.addBindValue(normalized->preview);
    query.addBindValue(normalized->contentHash);
    query.addBindValue(normalized->name);
    query.addBindValue(identityListToStorage(normalized->aliases));
    query.addBindValue(listToStorage(normalized->tags));
    query.addBindValue(clipActionTypeToString(normalized->actionType));
    query.addBindValue(clipStorageBackendToString(normalized->storageBackend));
    query.addBindValue(normalized->pinned ? 1 : 0);
    query.addBindValue(dateTimeToStorageValue(normalized->createdAt));
    query.addBindValue(dateTimeToStorageValue(normalized->updatedAt));
    query.addBindValue(dateTimeToStorageValue(normalized->usedAt));
    query.addBindValue(normalized->sourceApp);
    query.addBindValue(normalized->sourceWindowTitle);
    query.addBindValue(normalized->sourceUri);
    query.addBindValue(static_cast<qlonglong>(normalized->sizeBytes));
    if (!query.exec()) {
        database_.rollback();
        setLastError(query.lastError().text());
        return false;
    }
    if (!rebuildMetadataIndex(normalized.value()) || !database_.commit()) {
        const QString errorText = lastError_.trimmed().isEmpty()
            ? database_.lastError().text()
            : lastError_;
        database_.rollback();
        setLastError(errorText);
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

    Clip updated = stored.value();
    updated.state = ClipState::Saved;
    updated.updatedAt = effectiveUtcNow(now);
    return upsertPersistentClip(updated);
}

bool SqliteClipRepository::permanentlyDeleteClip(const QString &id)
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
        setLastError(QStringLiteral("Only deleted clips can be permanently removed"));
        return false;
    }
    if (!database_.transaction()) {
        setLastError(database_.lastError().text());
        return false;
    }
    QSqlQuery removeFts(database_);
    removeFts.prepare(QStringLiteral("DELETE FROM clip_fts WHERE clip_id = ?"));
    removeFts.addBindValue(id);
    QSqlQuery removeClip(database_);
    removeClip.prepare(QStringLiteral("DELETE FROM clips WHERE id = ?"));
    removeClip.addBindValue(id);
    if (!removeFts.exec() || !removeClip.exec() || !database_.commit()) {
        const QString errorText = !removeFts.lastError().text().trimmed().isEmpty()
            ? removeFts.lastError().text()
            : (!removeClip.lastError().text().trimmed().isEmpty()
                   ? removeClip.lastError().text()
                   : database_.lastError().text());
        database_.rollback();
        setLastError(errorText);
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

QList<Clip> SqliteClipRepository::searchCandidates(const ClipCandidateQuery &candidate) const
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return {};
    }
    if (candidate.limit == 0) {
        lastError_.clear();
        return {};
    }

    QStringList states;
    if (candidate.includeSaved) states.append(QStringLiteral("'saved'"));
    if (candidate.includeTemporary) states.append(QStringLiteral("'temporary'"));
    if (candidate.includeDeleted) states.append(QStringLiteral("'deleted'"));
    if (states.isEmpty()) {
        lastError_.clear();
        return {};
    }

    const QString columns = QStringLiteral(
        "c.id, c.kind, c.state, c.text, c.preview, c.content_hash, c.name, c.aliases, c.tags, "
        "c.action_type, c.storage_backend, c.pinned, c.created_at, c.updated_at, c.used_at, "
        "c.expires_at, c.source_app, c.source_window_title, c.source_uri, c.size_bytes");
    QString sql = QStringLiteral("SELECT %1 FROM clips c WHERE c.state IN (%2)")
                      .arg(columns, states.join(QStringLiteral(", ")));
    QVariantList bindings;

    if (candidate.emptyQuery) {
        sql += QStringLiteral(
            " ORDER BY c.pinned DESC, COALESCE(c.used_at, '') DESC, "
            "COALESCE(c.updated_at, '') DESC, c.created_at DESC, c.id ASC");
        if (candidate.limit > 0) {
            sql += QStringLiteral(" LIMIT ?");
            bindings.append(candidate.limit);
        }
    } else if (candidate.identityOnly) {
        if (!candidate.requiredTag.isEmpty()) {
            sql += QStringLiteral(
                " AND EXISTS (SELECT 1 FROM clip_tags t "
                "WHERE t.clip_id = c.id AND t.tag_key = ?)");
            bindings.append(candidate.requiredTag);
        }
        if (!candidate.text.isEmpty()) {
            sql += QStringLiteral(
                " AND EXISTS (SELECT 1 FROM clip_identities i "
                "WHERE i.clip_id = c.id AND instr(i.identity_key, ?) > 0)");
            bindings.append(candidate.text);
        }
    } else if (candidate.tagOnly) {
        if (candidate.text.isEmpty()) {
            lastError_.clear();
            return {};
        }
        sql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM clip_tags t "
            "WHERE t.clip_id = c.id AND instr(t.tag_key, ?) > 0)");
        bindings.append(candidate.text);
    } else {
        if (candidate.text.isEmpty()) {
            lastError_.clear();
            return {};
        }
        QString escapedFts = candidate.text;
        escapedFts.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        const bool hasFtsToken = std::any_of(candidate.text.cbegin(), candidate.text.cend(), [](QChar value) {
            return value.isLetterOrNumber();
        });
        sql += QStringLiteral(" AND c.id IN (");
        if (hasFtsToken) {
            sql += QStringLiteral("SELECT clip_id FROM clip_fts WHERE clip_fts MATCH ? UNION ");
            bindings.append(QStringLiteral("\"%1\"").arg(escapedFts));
        }
        sql += QStringLiteral(
            "SELECT id FROM clips WHERE "
            "instr(lower(COALESCE(name, '')), ?) > 0 OR "
            "instr(lower(COALESCE(aliases, '')), ?) > 0 OR "
            "instr(lower(COALESCE(tags, '')), ?) > 0 OR "
            "instr(lower(COALESCE(preview, '')), ?) > 0 OR "
            "instr(lower(text), ?) > 0)");
        for (int index = 0; index < 5; ++index) {
            bindings.append(candidate.text);
        }
    }

    if (!candidate.emptyQuery) {
        sql += QStringLiteral(" ORDER BY ");
        if (candidate.identityOnly && !candidate.text.isEmpty()) {
            sql += QStringLiteral(
                "CASE "
                "WHEN EXISTS (SELECT 1 FROM clip_identities i WHERE i.clip_id = c.id "
                "AND i.identity_kind = 'name' AND i.identity_key = ?) THEN 0 "
                "WHEN EXISTS (SELECT 1 FROM clip_identities i WHERE i.clip_id = c.id "
                "AND i.identity_kind = 'alias' AND i.identity_key = ?) THEN 1 "
                "WHEN EXISTS (SELECT 1 FROM clip_identities i WHERE i.clip_id = c.id "
                "AND i.identity_kind = 'name' AND instr(i.identity_key, ?) = 1) THEN 2 "
                "WHEN EXISTS (SELECT 1 FROM clip_identities i WHERE i.clip_id = c.id "
                "AND i.identity_kind = 'alias' AND instr(i.identity_key, ?) = 1) THEN 3 "
                "WHEN EXISTS (SELECT 1 FROM clip_identities i WHERE i.clip_id = c.id "
                "AND i.identity_kind = 'name' AND instr(i.identity_key, ?) > 0) THEN 4 "
                "ELSE 5 END, ");
            for (int index = 0; index < 5; ++index) bindings.append(candidate.text);
        } else if (candidate.tagOnly) {
            sql += QStringLiteral(
                "CASE "
                "WHEN EXISTS (SELECT 1 FROM clip_tags t WHERE t.clip_id = c.id "
                "AND t.tag_key = ?) THEN 0 "
                "WHEN EXISTS (SELECT 1 FROM clip_tags t WHERE t.clip_id = c.id "
                "AND instr(t.tag_key, ?) = 1) THEN 1 "
                "ELSE 2 END, ");
            bindings.append(candidate.text);
            bindings.append(candidate.text);
        } else if (!candidate.text.isEmpty()) {
            sql += QStringLiteral(
                "CASE "
                "WHEN EXISTS (SELECT 1 FROM clip_identities i WHERE i.clip_id = c.id "
                "AND i.identity_kind = 'name' AND i.identity_key = ?) THEN 0 "
                "WHEN EXISTS (SELECT 1 FROM clip_identities i WHERE i.clip_id = c.id "
                "AND i.identity_kind = 'alias' AND i.identity_key = ?) THEN 1 "
                "WHEN EXISTS (SELECT 1 FROM clip_tags t WHERE t.clip_id = c.id "
                "AND t.tag_key = ?) THEN 2 "
                "WHEN EXISTS (SELECT 1 FROM clip_identities i WHERE i.clip_id = c.id "
                "AND i.identity_kind = 'name' AND instr(i.identity_key, ?) = 1) THEN 3 "
                "WHEN EXISTS (SELECT 1 FROM clip_identities i WHERE i.clip_id = c.id "
                "AND i.identity_kind = 'alias' AND instr(i.identity_key, ?) = 1) THEN 4 "
                "WHEN EXISTS (SELECT 1 FROM clip_tags t WHERE t.clip_id = c.id "
                "AND instr(t.tag_key, ?) = 1) THEN 5 "
                "WHEN EXISTS (SELECT 1 FROM clip_identities i WHERE i.clip_id = c.id "
                "AND i.identity_kind = 'name' AND instr(i.identity_key, ?) > 0) THEN 6 "
                "WHEN EXISTS (SELECT 1 FROM clip_identities i WHERE i.clip_id = c.id "
                "AND i.identity_kind = 'alias' AND instr(i.identity_key, ?) > 0) THEN 7 "
                "WHEN EXISTS (SELECT 1 FROM clip_tags t WHERE t.clip_id = c.id "
                "AND instr(t.tag_key, ?) > 0) THEN 8 "
                "ELSE 9 END, ");
            for (int index = 0; index < 9; ++index) bindings.append(candidate.text);
        }
        sql += QStringLiteral(
            "c.pinned DESC, COALESCE(c.used_at, '') DESC, "
            "COALESCE(c.updated_at, '') DESC, c.created_at DESC, c.id ASC");
        if (candidate.limit > 0) {
            sql += QStringLiteral(" LIMIT ?");
            bindings.append(candidate.limit);
        }
    }

    QSqlQuery query(database_);
    query.prepare(sql);
    for (const QVariant &binding : bindings) {
        query.addBindValue(binding);
    }
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return {};
    }

    QList<Clip> results;
    while (query.next()) {
        results.append(hydrateClip(query));
    }
    lastError_.clear();
    return results;
}

std::optional<Clip> SqliteClipRepository::findClip(const QString &id) const
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return std::nullopt;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT id, kind, state, text, preview, content_hash, name, aliases, tags, "
                                 "action_type, storage_backend, pinned, created_at, updated_at, used_at, "
                                 "expires_at, source_app, source_window_title, source_uri, size_bytes "
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

QString SqliteClipRepository::databasePath() const
{
    return database_.isValid() && database_.databaseName() != QLatin1String(":memory:")
        ? QFileInfo(database_.databaseName()).absoluteFilePath()
        : QString();
}

bool SqliteClipRepository::integrityCheck()
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("PRAGMA quick_check"))) {
        setLastError(query.lastError().text());
        return false;
    }
    QStringList failures;
    while (query.next()) {
        const QString result = query.value(0).toString().trimmed();
        if (result.compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0) {
            failures.append(result);
        }
    }
    if (!failures.isEmpty()) {
        setLastError(QStringLiteral("SQLite integrity check failed: %1")
                         .arg(failures.join(QStringLiteral("; "))));
        return false;
    }
    QSqlQuery identityQuery(database_);
    if (!identityQuery.exec(QStringLiteral("PRAGMA identity_registry.quick_check"))) {
        setLastError(identityQuery.lastError().text());
        return false;
    }
    while (identityQuery.next()) {
        const QString result = identityQuery.value(0).toString().trimmed();
        if (result.compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0) {
            setLastError(QStringLiteral("Global identity registry integrity check failed: %1")
                             .arg(result));
            return false;
        }
    }
    lastError_.clear();
    return true;
}

bool SqliteClipRepository::backupDatabase(const QString &destinationPath)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }
    const QString source = databasePath();
    const QString destination = QFileInfo(destinationPath).absoluteFilePath();
    if (source.isEmpty() || destinationPath.trimmed().isEmpty()) {
        setLastError(QStringLiteral("A file-backed Clip database and backup path are required"));
        return false;
    }
    if (source.compare(destination, Qt::CaseInsensitive) == 0) {
        setLastError(QStringLiteral("Backup destination must differ from the active database"));
        return false;
    }
    if (!QDir(QFileInfo(destination).absolutePath()).mkpath(QStringLiteral("."))) {
        setLastError(QStringLiteral("Unable to create Clip backup directory"));
        return false;
    }

    QSqlQuery checkpoint(database_);
    if (!checkpoint.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"))
        || !checkpoint.next()
        || checkpoint.value(0).toInt() != 0) {
        setLastError(QStringLiteral("Unable to checkpoint the Clip database before backup"));
        return false;
    }

    const QString temporary = destination
        + QStringLiteral(".tmp-")
        + QUuid::createUuid().toString(QUuid::Id128);
    QFile::remove(temporary);
    if (!QFile::copy(source, temporary)) {
        setLastError(QStringLiteral("Unable to copy the Clip database backup"));
        return false;
    }
    if ((QFile::exists(destination) && !QFile::remove(destination))
        || !QFile::rename(temporary, destination)) {
        QFile::remove(temporary);
        setLastError(QStringLiteral("Unable to finalize the Clip database backup"));
        return false;
    }
    lastError_.clear();
    return true;
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

bool SqliteClipRepository::attachGlobalIdentityRegistry()
{
    QString error;
    if (attachSqliteGlobalIdentityRegistry(database_, identityRegistryPath_, &error)) {
        return true;
    }
    setLastError(error);
    return false;
}

bool SqliteClipRepository::rebuildGlobalIdentityRegistry()
{
    QList<GlobalIdentityObject> objects;
    for (const Clip &clip : clips()) {
        objects.append(identityObjectForClip(clip));
    }
    if (!lastError_.isEmpty()) {
        return false;
    }
    QString error;
    if (!rebuildSqliteHistoricalIdentityObjects(database_,
                                                {GlobalIdentityObjectType::Clip},
                                                objects,
                                                &error)) {
        setLastError(error);
        return false;
    }
    return true;
}

int SqliteClipRepository::schemaVersion() const
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT COALESCE(MAX(version), 0) FROM clip_schema_migrations"))
        || !query.next()) {
        setLastError(query.lastError().text());
        return -1;
    }
    return query.value(0).toInt();
}

bool SqliteClipRepository::migrateToVersion2()
{
    for (const QString &statement : {
             QStringLiteral("DROP TABLE IF EXISTS clip_fts"),
             QStringLiteral("DROP TABLE IF EXISTS clip_identities"),
             QStringLiteral("DROP TABLE IF EXISTS clip_tags"),
             QStringLiteral("ALTER TABLE clips RENAME TO clips_v1"),
             QStringLiteral("CREATE TABLE clips ("
                            "id TEXT PRIMARY KEY,"
                            "kind TEXT NOT NULL,"
                            "state TEXT NOT NULL,"
                            "text TEXT NOT NULL,"
                            "preview TEXT NOT NULL,"
                            "content_hash TEXT NOT NULL,"
                            "name TEXT,"
                            "aliases TEXT,"
                            "tags TEXT,"
                            "action_type TEXT NOT NULL DEFAULT 'insert_text',"
                            "storage_backend TEXT NOT NULL DEFAULT 'local',"
                            "pinned INTEGER NOT NULL DEFAULT 0,"
                            "created_at TEXT NOT NULL,"
                            "updated_at TEXT NOT NULL,"
                            "used_at TEXT,"
                            "expires_at TEXT,"
                            "source_app TEXT,"
                            "source_window_title TEXT,"
                            "source_uri TEXT,"
                            "size_bytes INTEGER NOT NULL DEFAULT 0"
                            ")"),
             QStringLiteral("INSERT INTO clips("
                            "id, kind, state, text, preview, content_hash, name, aliases, tags, "
                            "action_type, storage_backend, pinned, created_at, updated_at, used_at, "
                            "expires_at, source_app, source_window_title, source_uri, size_bytes) "
                            "SELECT id, kind, state, text, preview, content_hash, name, aliases, tags, "
                            "CASE WHEN instr(char(10) || lower(COALESCE(tags, '')) || char(10), "
                            "                char(10) || 'wb' || char(10)) > 0 "
                            "     THEN 'open_web_url' ELSE 'insert_text' END, "
                            "CASE WHEN lower(trim(COALESCE(source_app, ''))) = 'obsidian' "
                            "     THEN 'obsidian' ELSE 'local' END, "
                            "pinned, created_at, updated_at, used_at, expires_at, "
                            "CASE WHEN lower(trim(COALESCE(source_app, ''))) = 'obsidian' "
                            "     THEN NULL ELSE source_app END, "
                            "NULL, NULL, size_bytes FROM clips_v1"),
             QStringLiteral("DROP TABLE clips_v1")}) {
        if (!execute(statement)) {
            return false;
        }
    }

    for (const QString &statement : latestClipSchemaStatements()) {
        if (!execute(statement)) {
            return false;
        }
    }
    if (!recordMigration(2, QStringLiteral("persistent_clip_identity_and_provenance"))) {
        return false;
    }
    return true;
}

bool SqliteClipRepository::migrateIdentityIndexToVersion4()
{
    if (!execute(QStringLiteral("DROP TABLE IF EXISTS clip_identities"))
        || !execute(QStringLiteral(
            "CREATE TABLE clip_identities ("
            "identity_key TEXT NOT NULL, clip_id TEXT NOT NULL, identity_value TEXT NOT NULL, "
            "identity_kind TEXT NOT NULL, PRIMARY KEY(identity_key, clip_id, identity_kind), "
            "FOREIGN KEY(clip_id) REFERENCES clips(id) ON DELETE CASCADE)"))
        || !execute(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_clip_identities_clip_id ON clip_identities(clip_id)"))
        || !rebuildAllMetadataIndexes()
        || !recordMigration(4, QStringLiteral("global_name_alias_identity_registry"))) {
        return false;
    }
    return true;
}

bool SqliteClipRepository::ensureSearchSchema()
{
    for (const QString &statement : latestClipSchemaStatements()) {
        if (!execute(statement)) {
            return false;
        }
    }
    return true;
}

bool SqliteClipRepository::rebuildMetadataIndex(const Clip &clip)
{
    for (const QString &sql : {
             QStringLiteral("DELETE FROM clip_identities WHERE clip_id = ?"),
             QStringLiteral("DELETE FROM clip_tags WHERE clip_id = ?"),
             QStringLiteral("DELETE FROM clip_fts WHERE clip_id = ?")}) {
        QSqlQuery remove(database_);
        remove.prepare(sql);
        remove.addBindValue(clip.id);
        if (!remove.exec()) {
            setLastError(remove.lastError().text());
            return false;
        }
    }

    if (clip.state != ClipState::Temporary) {
        const QStringList identities = clipIdentityValues(clip.name, clip.aliases);
        for (int index = 0; index < identities.size(); ++index) {
            QSqlQuery identity(database_);
            identity.prepare(QStringLiteral("INSERT OR IGNORE INTO clip_identities("
                                            "identity_key, clip_id, identity_value, identity_kind) "
                                            "VALUES (?, ?, ?, ?)"));
            identity.addBindValue(normalizedIdentity(identities.at(index)));
            identity.addBindValue(clip.id);
            identity.addBindValue(identities.at(index));
            identity.addBindValue(index == 0 ? QStringLiteral("name") : QStringLiteral("alias"));
            if (!identity.exec()) {
                setLastError(identity.lastError().nativeErrorCode().isEmpty()
                                 ? identity.lastError().text()
                                 : QStringLiteral("Clip name or alias already exists: %1")
                                       .arg(identities.at(index)));
                return false;
            }
        }
    }

    for (const QString &tag : cleanStringList(clip.tags)) {
        QSqlQuery insertTag(database_);
        insertTag.prepare(QStringLiteral("INSERT INTO clip_tags(clip_id, tag, tag_key) VALUES (?, ?, ?)"));
        insertTag.addBindValue(clip.id);
        insertTag.addBindValue(tag);
        insertTag.addBindValue(normalizedIdentity(tag));
        if (!insertTag.exec()) {
            setLastError(insertTag.lastError().text());
            return false;
        }
    }

    QSqlQuery fts(database_);
    fts.prepare(QStringLiteral("INSERT INTO clip_fts(clip_id, name, aliases, tags, preview, text) "
                               "VALUES (?, ?, ?, ?, ?, ?)"));
    fts.addBindValue(clip.id);
    fts.addBindValue(clip.name);
    fts.addBindValue(cleanStringList(clip.aliases).join(QLatin1Char(' ')));
    fts.addBindValue(cleanStringList(clip.tags).join(QLatin1Char(' ')));
    fts.addBindValue(clip.preview);
    fts.addBindValue(clip.text);
    if (!fts.exec()) {
        setLastError(fts.lastError().text());
        return false;
    }
    return true;
}

bool SqliteClipRepository::rebuildAllMetadataIndexes()
{
    if (!execute(QStringLiteral("DELETE FROM clip_identities"))
        || !execute(QStringLiteral("DELETE FROM clip_tags"))
        || !execute(QStringLiteral("DELETE FROM clip_fts"))) {
        return false;
    }
    for (const Clip &clip : clips()) {
        if (!rebuildMetadataIndex(clip)) {
            return false;
        }
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

    QSqlQuery deleteOrphanedSearchRows(database_);
    if (!deleteOrphanedSearchRows.exec(QStringLiteral(
            "DELETE FROM clip_fts WHERE clip_id NOT IN (SELECT id FROM clips)"))) {
        setLastError(deleteOrphanedSearchRows.lastError().text());
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

QList<Clip> SqliteClipRepository::readClips(const QString &whereClause) const
{
    QList<Clip> results;
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return results;
    }

    QString sql = QStringLiteral("SELECT id, kind, state, text, preview, content_hash, name, aliases, tags, "
                                 "action_type, storage_backend, pinned, created_at, updated_at, used_at, "
                                 "expires_at, source_app, source_window_title, source_uri, size_bytes "
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
    clip.aliases = identityListFromStorage(query.value(7).toString());
    clip.tags = listFromStorage(query.value(8).toString());
    clip.actionType = clipActionTypeFromString(query.value(9).toString());
    clip.storageBackend = clipStorageBackendFromString(query.value(10).toString());
    clip.pinned = query.value(11).toInt() != 0;
    clip.createdAt = dateTimeFromStorageValue(query.value(12));
    clip.updatedAt = dateTimeFromStorageValue(query.value(13));
    clip.usedAt = dateTimeFromStorageValue(query.value(14));
    clip.expiresAt = dateTimeFromStorageValue(query.value(15));
    clip.sourceApp = query.value(16).toString();
    clip.sourceWindowTitle = query.value(17).toString();
    clip.sourceUri = query.value(18).toString();
    clip.sizeBytes = static_cast<qsizetype>(query.value(19).toLongLong());
    return clip;
}

void SqliteClipRepository::setLastError(const QString &message) const
{
    lastError_ = message;
}

InMemoryClipRepository::InMemoryClipRepository(
    SharedInMemoryGlobalIdentityRegistry identityRegistry)
    : identityRegistry_(identityRegistry ? std::move(identityRegistry)
                                         : createInMemoryGlobalIdentityRegistry())
{
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
    lastError_.clear();
    lastIdentityConflict_.reset();
    const int index = clipIndexById(clips_, id);
    if (index < 0) {
        lastError_ = QStringLiteral("Clip not found");
        return false;
    }

    Clip updated = clips_.at(index);
    const QString savedName = name.trimmed().isEmpty() ? updated.preview : name;
    const ClipIdentityValidationResult validation =
        validateClipIdentity(clips_, id, savedName, aliases);
    if (!validation.valid) {
        lastIdentityConflict_ = validation.conflict;
        lastError_ = validation.error;
        return false;
    }
    updated.state = ClipState::Saved;
    updated.name = savedName;
    updated.aliases = nonEmptyIdentityValues(aliases);
    updated.tags = cleanStringList(tags);
    updated.pinned = pinned;
    updated.updatedAt = effectiveUtcNow(now);
    updated.expiresAt = {};
    if (!identityRegistry_->replaceObjects({identityObjectForClip(updated)},
                                           &lastIdentityConflict_)) {
        lastError_ = lastIdentityConflict_.has_value()
            ? lastIdentityConflict_->message()
            : QStringLiteral("Unable to update global identity registry");
        return false;
    }
    clips_[index] = updated;
    return true;
}

bool InMemoryClipRepository::importSavedClip(const Clip &clip)
{
    lastError_.clear();
    lastIdentityConflict_.reset();
    if (clip.state != ClipState::Saved) {
        lastError_ = QStringLiteral("Only saved clips can be imported");
        return false;
    }
    QString error;
    const std::optional<Clip> normalized = normalizedPersistentClip(clip, &error);
    if (!normalized.has_value()) {
        lastError_ = error;
        return false;
    }

    if (clipIndexById(clips_, normalized->id) >= 0) {
        lastError_ = QStringLiteral("Clip already exists");
        return false;
    }

    const ClipIdentityValidationResult validation =
        validateClipIdentity(clips_, normalized->id, normalized->name, normalized->aliases);
    if (!validation.valid) {
        lastIdentityConflict_ = validation.conflict;
        lastError_ = validation.error;
        return false;
    }
    if (!identityRegistry_->replaceObjects({identityObjectForClip(normalized.value())},
                                           &lastIdentityConflict_)) {
        lastError_ = lastIdentityConflict_.has_value()
            ? lastIdentityConflict_->message()
            : QStringLiteral("Unable to update global identity registry");
        return false;
    }

    clips_.append(*normalized);
    return true;
}

bool InMemoryClipRepository::upsertSavedClip(const Clip &clip)
{
    if (clip.state != ClipState::Saved) {
        return false;
    }
    return upsertPersistentClip(clip);
}

bool InMemoryClipRepository::upsertPersistentClip(const Clip &clip)
{
    lastError_.clear();
    lastIdentityConflict_.reset();
    QString error;
    const std::optional<Clip> normalized = normalizedPersistentClip(clip, &error);
    if (!normalized.has_value()) {
        lastError_ = error;
        return false;
    }

    const int index = clipIndexById(clips_, normalized->id);
    if (normalized->state == ClipState::Saved) {
        const ClipIdentityValidationResult validation =
            validateClipIdentity(clips_, normalized->id, normalized->name, normalized->aliases);
        if (!validation.valid) {
            lastIdentityConflict_ = validation.conflict;
            lastError_ = validation.error;
            return false;
        }
    }
    if (!identityRegistry_->replaceObjects({identityObjectForClip(normalized.value())},
                                           &lastIdentityConflict_)) {
        lastError_ = lastIdentityConflict_.has_value()
            ? lastIdentityConflict_->message()
            : QStringLiteral("Unable to update global identity registry");
        return false;
    }
    if (index < 0) {
        clips_.append(normalized.value());
    } else {
        clips_[index] = normalized.value();
    }
    return true;
}

bool InMemoryClipRepository::softDeleteSavedClip(const QString &id, const QDateTime &now)
{
    lastError_.clear();
    lastIdentityConflict_.reset();
    const int index = clipIndexById(clips_, id);
    if (index < 0 || clips_.at(index).state != ClipState::Saved) {
        lastError_ = QStringLiteral("Only Saved Clips can be deleted");
        return false;
    }

    Clip updated = clips_.at(index);
    updated.state = ClipState::Deleted;
    updated.updatedAt = effectiveUtcNow(now);
    if (!identityRegistry_->replaceObjects({identityObjectForClip(updated)},
                                           &lastIdentityConflict_)) {
        lastError_ = lastIdentityConflict_.has_value()
            ? lastIdentityConflict_->message()
            : QStringLiteral("Unable to update global identity registry");
        return false;
    }
    clips_[index] = updated;
    return true;
}

bool InMemoryClipRepository::restoreClip(const QString &id, const QDateTime &now)
{
    lastError_.clear();
    lastIdentityConflict_.reset();
    const int index = clipIndexById(clips_, id);
    if (index < 0 || clips_.at(index).state != ClipState::Deleted) {
        lastError_ = QStringLiteral("Only deleted clips can be restored");
        return false;
    }

    Clip updated = clips_.at(index);
    updated.state = ClipState::Saved;
    updated.updatedAt = effectiveUtcNow(now);
    const ClipIdentityValidationResult validation =
        validateClipIdentity(clips_, updated.id, updated.name, updated.aliases);
    if (!validation.valid) {
        lastIdentityConflict_ = validation.conflict;
        lastError_ = validation.error;
        return false;
    }
    if (!identityRegistry_->replaceObjects({identityObjectForClip(updated)},
                                           &lastIdentityConflict_)) {
        lastError_ = lastIdentityConflict_.has_value()
            ? lastIdentityConflict_->message()
            : QStringLiteral("Unable to update global identity registry");
        return false;
    }

    clips_[index] = updated;
    return true;
}

bool InMemoryClipRepository::permanentlyDeleteClip(const QString &id)
{
    lastError_.clear();
    lastIdentityConflict_.reset();
    const int index = clipIndexById(clips_, id);
    if (index < 0 || clips_.at(index).state != ClipState::Deleted) {
        lastError_ = QStringLiteral("Only deleted clips can be permanently removed");
        return false;
    }
    if (!identityRegistry_->replaceObjects({identityObjectForClip(clips_.at(index))},
                                           &lastIdentityConflict_)) {
        lastError_ = lastIdentityConflict_.has_value()
            ? lastIdentityConflict_->message()
            : QStringLiteral("Unable to update global identity registry");
        return false;
    }
    clips_.removeAt(index);
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

QString InMemoryClipRepository::lastError() const
{
    return lastError_;
}

std::optional<GlobalIdentityConflict> InMemoryClipRepository::lastIdentityConflict() const
{
    return lastIdentityConflict_;
}

QList<GlobalIdentityConflict> InMemoryClipRepository::identityConflicts() const
{
    return identityRegistry_ ? identityRegistry_->conflicts()
                             : QList<GlobalIdentityConflict>{};
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
    anchor.id = QStringLiteral("clip:%1").arg(clip.id);
    anchor.name = clip.name.trimmed().isEmpty() ? clip.preview : clip.name;
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
