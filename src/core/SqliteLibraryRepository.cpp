#include "pinloom/core/SqliteLibraryRepository.h"

#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/ResourceNormalization.h"
#include "pinloom/core/Schema.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QUuid>
#include <QUrl>
#include <QVariant>
#include <algorithm>
#include <tuple>
#include <utility>

namespace Pinloom {

namespace {

QString locatorTypeFromLegacyAnchorType(const QString &type)
{
    const QString normalized = type.trimmed().toLower();
    if (normalized == QLatin1String("file_line")) {
        return QStringLiteral("file.line");
    }
    if (normalized == QLatin1String("markdown_heading")
        || normalized == QLatin1String("text_heading")) {
        return QStringLiteral("text.heading");
    }
    if (normalized == QLatin1String("markdown_block")
        || normalized == QLatin1String("text_block")) {
        return QStringLiteral("text.block");
    }
    if (normalized == QLatin1String("marker")
        || normalized == QLatin1String("symbol_like")
        || normalized == QLatin1String("code_symbol")) {
        return QStringLiteral("marker");
    }
    if (normalized == QLatin1String("pdf_page")) {
        return QStringLiteral("pdf.page");
    }
    if (normalized == QLatin1String("pdf_region")) {
        return QStringLiteral("pdf.region");
    }
    if (normalized == QLatin1String("url_fragment")) {
        return QStringLiteral("url.fragment");
    }
    if (normalized == QLatin1String("manual")) {
        return QStringLiteral("manual");
    }
    return {};
}

QString storageTypeFromLegacyAnchorType(const QString &type)
{
    const QString locatorType = locatorTypeFromLegacyAnchorType(type);
    if (locatorType == QLatin1String("file.line")) {
        return QStringLiteral("file_line");
    }
    if (locatorType == QLatin1String("text.heading")) {
        return QStringLiteral("text_heading");
    }
    if (locatorType == QLatin1String("text.block")) {
        return QStringLiteral("text_block");
    }
    if (locatorType == QLatin1String("pdf.page")) {
        return QStringLiteral("pdf_page");
    }
    if (locatorType == QLatin1String("pdf.region")) {
        return QStringLiteral("pdf_region");
    }
    if (locatorType == QLatin1String("url.fragment")) {
        return QStringLiteral("url_fragment");
    }
    return locatorType;
}

bool looksLikeUri(const QString &location)
{
    const QUrl url(location.trimmed());
    return url.isValid() && !url.scheme().isEmpty();
}

QString legacyAnchorUsageFieldKey(const QString &type,
                                  const QString &target,
                                  int line,
                                  int page,
                                  double x,
                                  double y,
                                  double width,
                                  double height)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
        .arg(type,
             target,
             QString::number(line),
             QString::number(page),
             QString::number(x, 'f', 2),
             QString::number(y, 'f', 2),
             QString::number(width, 'f', 2),
             QString::number(height, 'f', 2));
}

QString resourceKindToString(ResourceKind kind)
{
    switch (kind) {
    case ResourceKind::File:
        return QStringLiteral("file");
    case ResourceKind::Folder:
        return QStringLiteral("folder");
    case ResourceKind::Pdf:
        return QStringLiteral("pdf");
    case ResourceKind::TextSnippet:
        return QStringLiteral("text_snippet");
    case ResourceKind::Url:
        return QStringLiteral("url");
    case ResourceKind::Note:
        return QStringLiteral("note");
    case ResourceKind::ManualAnchor:
        return QStringLiteral("manual_anchor");
    case ResourceKind::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

ResourceKind resourceKindFromString(const QString &kind)
{
    // Accept legacy markdown rows while new writes use ordinary file resources.
    if (kind == QLatin1String("file") || kind == QLatin1String("markdown")) {
        return ResourceKind::File;
    }
    if (kind == QLatin1String("folder")) {
        return ResourceKind::Folder;
    }
    if (kind == QLatin1String("pdf")) {
        return ResourceKind::Pdf;
    }
    // Accept legacy code_* rows while new writes use neutral storage names.
    if (kind == QLatin1String("text_snippet") || kind == QLatin1String("code_snippet")) {
        return ResourceKind::TextSnippet;
    }
    if (kind == QLatin1String("url")) {
        return ResourceKind::Url;
    }
    if (kind == QLatin1String("note")) {
        return ResourceKind::Note;
    }
    if (kind == QLatin1String("manual_anchor")) {
        return ResourceKind::ManualAnchor;
    }
    return ResourceKind::Unknown;
}

QStringList searchTokensFromText(const QString &text)
{
    const QRegularExpression tokenExpression(QStringLiteral("[\\p{L}\\p{N}_]+"));
    QRegularExpressionMatchIterator it = tokenExpression.globalMatch(text);
    QStringList tokens;
    while (it.hasNext()) {
        const QString token = it.next().captured(0).trimmed();
        if (!token.isEmpty()) {
            tokens.append(token + QLatin1Char('*'));
        }
    }
    return tokens;
}

QString ftsQueryFromText(const QString &text)
{
    return searchTokensFromText(text).join(QLatin1Char(' '));
}

QStringList plainSearchTokensFromText(const QString &text)
{
    const QRegularExpression tokenExpression(QStringLiteral("[\\p{L}\\p{N}_]+"));
    QRegularExpressionMatchIterator it = tokenExpression.globalMatch(text);
    QStringList tokens;
    while (it.hasNext()) {
        const QString token = it.next().captured(0).trimmed();
        if (!token.isEmpty()) {
            tokens.append(token);
        }
    }
    return tokens;
}

bool containsAnyToken(const QString &text, const QStringList &tokens)
{
    for (const QString &token : tokens) {
        if (text.contains(token, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

bool containsAllTokens(const QString &text, const QStringList &tokens)
{
    for (const QString &token : tokens) {
        if (!text.contains(token, Qt::CaseInsensitive)) {
            return false;
        }
    }
    return true;
}

bool equalsQueryText(const QString &text, const QString &queryText)
{
    return text.trimmed().compare(queryText.trimmed(), Qt::CaseInsensitive) == 0;
}

bool anyEqualsQueryText(const QStringList &values, const QString &queryText)
{
    return std::any_of(values.cbegin(), values.cend(), [&](const QString &value) {
        return equalsQueryText(value, queryText);
    });
}

double exactMatchScoreAdjustment(const QStringList &values, const QString &queryText)
{
    return anyEqualsQueryText(values, queryText) ? -0.75 : 0.0;
}

QStringList nonEmptyValues(const QStringList &values)
{
    QStringList filtered;
    for (const QString &value : values) {
        if (!value.trimmed().isEmpty()) {
            filtered.append(value);
        }
    }
    return filtered;
}

QString effectiveAnchorName(const Anchor &anchor)
{
    return anchor.name;
}

QStringList anchorMetadataValues(const Anchor &anchor)
{
    QStringList values;
    values = nonEmptyValues({anchor.targetApp,
                             anchor.targetFile,
                             anchor.targetUri,
                             anchor.locatorType,
                             anchor.locatorJson});
    return values;
}

struct AnchorMatch {
    bool matched = false;
    double score = 100.0;
    QString field;
};

std::optional<AnchorMatch> matchAnchorValues(const QStringList &values,
                                             const QString &field,
                                             double score,
                                             const QStringList &tokens,
                                             const QString &queryText)
{
    const QString text = values.join(QLatin1Char('\n'));
    if (containsAllTokens(text, tokens)) {
        return AnchorMatch{true, score + exactMatchScoreAdjustment(values, queryText), field};
    }
    if (containsAnyToken(text, tokens)) {
        return AnchorMatch{true, score + 15.0 + exactMatchScoreAdjustment(values, queryText), field};
    }
    return std::nullopt;
}

AnchorMatch classifyAnchorMatch(const Anchor &anchor, const QStringList &tokens, const QString &queryText)
{
    if (tokens.isEmpty()) {
        return {};
    }

    const QString nameField = anchor.name.trimmed().isEmpty()
        ? QStringLiteral("anchor")
        : QStringLiteral("anchor_name");
    const QList<std::tuple<QStringList, QString, double>> candidates = {
        {QStringList{effectiveAnchorName(anchor)}, nameField, 0.0},
        {anchor.aliases, QStringLiteral("anchor_alias"), 5.0},
        {anchor.tags, QStringLiteral("anchor_tag"), 8.0},
        {anchorMetadataValues(anchor), QStringLiteral("anchor_metadata"), 40.0},
    };

    for (const auto &candidate : candidates) {
        const std::optional<AnchorMatch> match = matchAnchorValues(std::get<0>(candidate),
                                                                   std::get<1>(candidate),
                                                                   std::get<2>(candidate),
                                                                   tokens,
                                                                   queryText);
        if (match.has_value()) {
            return match.value();
        }
    }

    return {};
}

QString anchorSearchText(const Anchor &anchor)
{
    QStringList values = nonEmptyValues({effectiveAnchorName(anchor)});
    values.append(anchor.aliases);
    values.append(anchor.tags);
    values.append(anchorMetadataValues(anchor));
    values.removeDuplicates();
    return values.join(QLatin1Char('\n'));
}

double classifyResourceMatch(const Resource &resource,
                             const QStringList &tokens,
                             const QString &queryText,
                             QString *matchedField)
{
    if (tokens.isEmpty()) {
        *matchedField = QStringLiteral("all");
        return 100.0;
    }

    struct MatchCandidate {
        QStringList values;
        QString field;
        double score = 100.0;
    };

    const QList<MatchCandidate> candidates = {
        {QStringList{resource.title}, QStringLiteral("title"), 10.0},
        {QStringList{QFileInfo(resource.location).fileName()}, QStringLiteral("filename"), 15.0},
        {resource.aliases, QStringLiteral("alias"), 20.0},
        {resource.tags, QStringLiteral("tag"), 30.0},
        {QStringList{resource.content}, QStringLiteral("content"), 80.0},
        {QStringList{resource.location}, QStringLiteral("path"), 90.0},
    };

    for (const MatchCandidate &candidate : candidates) {
        const QString text = candidate.values.join(QLatin1Char('\n'));
        if (containsAllTokens(text, tokens)) {
            *matchedField = candidate.field;
            return candidate.score + exactMatchScoreAdjustment(candidate.values, queryText);
        }
    }

    for (const MatchCandidate &candidate : candidates) {
        const QString text = candidate.values.join(QLatin1Char('\n'));
        if (containsAnyToken(text, tokens)) {
            *matchedField = candidate.field;
            return candidate.score + exactMatchScoreAdjustment(candidate.values, queryText);
        }
    }

    *matchedField = QStringLiteral("all");
    return 100.0;
}

bool searchResultLessThan(const SearchResult &left, const SearchResult &right)
{
    if (left.score == right.score) {
        if (left.resource.title == right.resource.title) {
            return left.resource.location < right.resource.location;
        }
        return left.resource.title < right.resource.title;
    }
    return left.score < right.score;
}

SearchResult resourceSearchResult(const Resource &resource, const QStringList &tokens, const QString &queryText)
{
    QString matchedField;
    const double score = classifyResourceMatch(resource, tokens, queryText, &matchedField);
    return SearchResult{resource, score, matchedField, std::nullopt};
}

SearchResult anchorSearchResult(const Resource &resource,
                                const Anchor &anchor,
                                const QStringList &tokens,
                                const QString &queryText)
{
    const AnchorMatch match = classifyAnchorMatch(anchor, tokens, queryText);
    return SearchResult{resource, match.score, match.field, anchor};
}

bool shouldIndexAnchor(const Resource &resource, const Anchor &anchor)
{
    Q_UNUSED(resource);

    if (anchorSearchText(anchor).trimmed().isEmpty()) {
        return false;
    }

    return !anchor.name.trimmed().isEmpty()
        || !anchor.aliases.isEmpty()
        || !anchor.tags.isEmpty()
        || !anchor.targetApp.trimmed().isEmpty()
        || !anchor.targetFile.trimmed().isEmpty()
        || !anchor.targetUri.trimmed().isEmpty()
        || !anchor.locatorType.trimmed().isEmpty()
        || !anchor.locatorJson.trimmed().isEmpty();
}

QString anchorUsageKey(const Anchor &anchor)
{
    return anchorIdentityKey(anchor);
}

QStringList anchorUsageKeys(const Anchor &anchor)
{
    return {anchorUsageKey(anchor)};
}

double usageScoreAdjustment(const ResourceUsage &usage)
{
    double adjustment = 0.0;
    if (usage.pinned) {
        adjustment -= 0.6;
    }
    adjustment -= std::min(usage.openCount, 10) * 0.03;
    if (usage.lastOpenedAt.isValid()) {
        const qint64 secondsAgo = usage.lastOpenedAt.toUTC().secsTo(QDateTime::currentDateTimeUtc());
        if (secondsAgo >= 0 && secondsAgo <= 7 * 24 * 60 * 60) {
            adjustment -= 0.2;
        } else if (secondsAgo > 0 && secondsAgo <= 30 * 24 * 60 * 60) {
            adjustment -= 0.1;
        }
    }
    return adjustment;
}

double anchorUsageScoreAdjustment(const AnchorUsage &usage)
{
    double adjustment = 0.0;
    adjustment -= std::min(usage.openCount, 10) * 0.04;
    if (usage.lastOpenedAt.isValid()) {
        const qint64 secondsAgo = usage.lastOpenedAt.toUTC().secsTo(QDateTime::currentDateTimeUtc());
        if (secondsAgo >= 0 && secondsAgo <= 7 * 24 * 60 * 60) {
            adjustment -= 0.25;
        } else if (secondsAgo > 0 && secondsAgo <= 30 * 24 * 60 * 60) {
            adjustment -= 0.12;
        }
    }
    return adjustment;
}

double anchorScoreAdjustment(const Anchor &anchor)
{
    double adjustment = 0.0;
    if (anchor.pinned) {
        adjustment -= 0.6;
    }
    if (anchor.usedAt.isValid()) {
        const qint64 secondsAgo = anchor.usedAt.toUTC().secsTo(QDateTime::currentDateTimeUtc());
        if (secondsAgo >= 0 && secondsAgo <= 7 * 24 * 60 * 60) {
            adjustment -= 0.25;
        } else if (secondsAgo > 0 && secondsAgo <= 30 * 24 * 60 * 60) {
            adjustment -= 0.12;
        }
    }
    return adjustment;
}

bool hasContextTag(const Resource &resource, const QStringList &contextTags)
{
    for (const QString &tag : contextTags) {
        if (resource.tags.contains(tag, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QString normalizedLocation(const QString &location)
{
    const QString trimmed = location.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QString normalized = QDir::cleanPath(trimmed);
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return normalized.toLower();
}

bool matchesContextLocationPrefix(const QString &location, const QStringList &prefixes)
{
    const QString normalizedResourceLocation = normalizedLocation(location);
    if (normalizedResourceLocation.isEmpty()) {
        return false;
    }

    for (const QString &prefix : prefixes) {
        const QString normalizedPrefix = normalizedLocation(prefix);
        if (normalizedPrefix.isEmpty()) {
            continue;
        }
        if (normalizedResourceLocation == normalizedPrefix
            || normalizedResourceLocation.startsWith(normalizedPrefix + QLatin1Char('/'))) {
            return true;
        }
    }
    return false;
}

bool matchesRequiredLocationPrefixes(const QString &location, const QStringList &prefixes)
{
    return prefixes.isEmpty() || matchesContextLocationPrefix(location, prefixes);
}

bool matchesRequiredKinds(ResourceKind kind, const QList<ResourceKind> &requiredKinds)
{
    return resourceKindMatchesFilter(kind, requiredKinds);
}

double contextScoreAdjustment(const Resource &resource, const SearchQuery &query)
{
    double adjustment = 0.0;
    if (hasContextTag(resource, query.contextTags)) {
        adjustment -= 0.35;
    }
    if (matchesContextLocationPrefix(resource.location, query.contextLocationPrefixes)) {
        adjustment -= 0.35;
    }
    return adjustment;
}

QList<GlobalIdentityObject> identityObjectsForResource(const Resource &resource)
{
    QList<GlobalIdentityObject> objects;
    GlobalIdentityObject file;
    file.owner.objectType = GlobalIdentityObjectType::File;
    file.owner.objectId = resource.id;
    file.owner.displayName = resource.title;
    file.owner.locator = globalIdentityLocator(GlobalIdentityObjectType::File,
                                               resource.id);
    file.active = !resource.deleted;
    file.values.append({GlobalIdentityFieldKind::Name, resource.title});
    for (const QString &alias : resource.aliases) {
        file.values.append({GlobalIdentityFieldKind::Alias, alias});
    }
    objects.append(file);
    for (const Anchor &anchor : resource.anchors) {
        GlobalIdentityObject value;
        value.owner.objectType = GlobalIdentityObjectType::Anchor;
        value.owner.objectId = anchor.id;
        value.owner.parentId = resource.id;
        value.owner.displayName = anchor.name;
        value.owner.locator = globalIdentityLocator(GlobalIdentityObjectType::Anchor,
                                                    anchor.id,
                                                    resource.id);
        value.active = !resource.deleted && !anchor.deleted;
        value.values.append({GlobalIdentityFieldKind::Name, anchor.name});
        for (const QString &alias : anchor.aliases) {
            value.values.append({GlobalIdentityFieldKind::Alias, alias});
        }
        objects.append(value);
    }
    return objects;
}

void addIdentityReplacement(QHash<QString, GlobalIdentityObject> &updates,
                            const std::optional<Resource> &before,
                            const std::optional<Resource> &after)
{
    collectGlobalIdentityReplacements(
        updates,
        before ? identityObjectsForResource(*before) : QList<GlobalIdentityObject>{},
        after ? identityObjectsForResource(*after) : QList<GlobalIdentityObject>{});
}

} // namespace

SqliteLibraryRepository::SqliteLibraryRepository()
    : connectionName_(QStringLiteral("pinloom_%1").arg(QUuid::createUuid().toString(QUuid::Id128)))
{
}

SqliteLibraryRepository::~SqliteLibraryRepository()
{
    if (database_.isValid()) {
        database_.close();
        database_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName_);
    }
}

bool SqliteLibraryRepository::open(const QString &path, const QString &identityRegistryPath)
{
    const QFileInfo databaseFile(path);
    QDir parentDir(databaseFile.absolutePath());
    if (!parentDir.exists() && !parentDir.mkpath(QStringLiteral("."))) {
        setLastError(QStringLiteral("Unable to create database directory: %1").arg(parentDir.absolutePath()));
        return false;
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

    return execute(QStringLiteral("PRAGMA foreign_keys = ON;"))
        && execute(QStringLiteral("PRAGMA busy_timeout = 5000;"))
        && attachGlobalIdentityRegistry();
}

bool SqliteLibraryRepository::initialize()
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    if (!beginTransaction()) {
        return false;
    }

    QString identityError;
    if (!initializeSqliteGlobalIdentityRegistry(database_, &identityError)) {
        setLastError(identityError);
        rollbackTransaction();
        return false;
    }

    for (const QString &statement : Schema::sqliteFts5Draft()) {
        if (!execute(statement)) {
            rollbackTransaction();
            return false;
        }
    }

    if (!ensureAnchorLocatorColumns()) {
        rollbackTransaction();
        return false;
    }

    if (!ensureSoftDeleteColumns()) {
        rollbackTransaction();
        return false;
    }

    if (!ensureResourceRetentionColumn()) {
        rollbackTransaction();
        return false;
    }

    if (!ensureLibraryRootColumns()) {
        rollbackTransaction();
        return false;
    }

    if (!migrateCanonicalAnchorSchema()) {
        rollbackTransaction();
        return false;
    }

    if (!migrateStableAnchorIdentitySchema()) {
        rollbackTransaction();
        return false;
    }

    const int currentSchemaVersion = schemaVersion();
    if (currentSchemaVersion < 0
        || (currentSchemaVersion < 13 && !migrateLifecycleSearchSchema())) {
        rollbackTransaction();
        return false;
    }

    if (!execute(QStringLiteral("DROP TABLE IF EXISTS resource_relations"))) {
        rollbackTransaction();
        return false;
    }

    if (!rebuildGlobalIdentityRegistry()) {
        rollbackTransaction();
        return false;
    }

    if (!recordMigration(1, QStringLiteral("initial_sqlite_fts5_schema"))
        || !recordMigration(2, QStringLiteral("library_roots"))
        || !recordMigration(3, QStringLiteral("anchor_fts"))
        || !recordMigration(4, QStringLiteral("resource_relations"))
        || !recordMigration(5, QStringLiteral("resource_usage"))
        || !recordMigration(6, QStringLiteral("anchor_usage"))
        || !recordMigration(7, QStringLiteral("pinned_library_roots"))
        || !recordMigration(8, QStringLiteral("anchor_locator_fields"))
        || !recordMigration(9, QStringLiteral("soft_delete_flags"))
        || !recordMigration(10, QStringLiteral("retire_directory_indexing_and_relations"))
        || !recordMigration(11, QStringLiteral("canonical_anchor_locators"))
        || !recordMigration(12, QStringLiteral("stable_anchor_identity"))
        || !recordMigration(13, QStringLiteral("lifecycle_search_index"))
        || !recordMigration(14, QStringLiteral("library_roots_and_managed_items"))
        || !recordMigration(15, QStringLiteral("explicit_anchor_library_retention"))
        || !recordMigration(16, QStringLiteral("global_name_alias_identity_registry"))) {
        rollbackTransaction();
        return false;
    }

    return commitTransaction();
}

bool SqliteLibraryRepository::isOpen() const
{
    return database_.isValid() && database_.isOpen();
}

QString SqliteLibraryRepository::lastError() const
{
    return lastError_;
}

std::optional<GlobalIdentityConflict> SqliteLibraryRepository::lastIdentityConflict() const
{
    return lastIdentityConflict_;
}

QList<GlobalIdentityConflict> SqliteLibraryRepository::identityConflicts() const
{
    QString error;
    QSqlDatabase database = database_;
    const QList<GlobalIdentityConflict> conflicts = sqliteGlobalIdentityConflicts(database, &error);
    if (!error.isEmpty()) {
        setLastError(error);
    }
    return conflicts;
}

bool SqliteLibraryRepository::upsertResource(const Resource &resource)
{
    lastError_.clear();
    lastIdentityConflict_.reset();
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }
    if (resource.id.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Resource id is required"));
        return false;
    }

    const Resource storedResource = normalizedResource(resource);
    if (!beginTransaction()) {
        return false;
    }
    const std::optional<Resource> existingResource = findResource(storedResource.id);
    if (!lastError_.isEmpty()) {
        rollbackTransaction();
        return false;
    }
    QHash<QString, GlobalIdentityObject> identityUpdates;
    addIdentityReplacement(identityUpdates, existingResource, storedResource);

    if (!identityUpdatesDeferred_) {
        QString identityError;
        if (!replaceSqliteGlobalIdentityObjects(database_,
                                                identityUpdates.values(),
                                                &lastIdentityConflict_,
                                                &identityError)) {
            setLastError(identityError);
            rollbackTransaction();
            return false;
        }
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO resources(id, kind, title, location, explicitly_retained, deleted, updated_at) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?) "
                                 "ON CONFLICT(id) DO UPDATE SET "
                                 "kind = excluded.kind,"
                                 "title = excluded.title,"
                                 "location = excluded.location,"
                                 "explicitly_retained = excluded.explicitly_retained,"
                                 "deleted = excluded.deleted,"
                                 "updated_at = excluded.updated_at"));
    query.addBindValue(storedResource.id);
    query.addBindValue(resourceKindToString(storedResource.kind));
    query.addBindValue(storedResource.title);
    query.addBindValue(storedResource.location);
    query.addBindValue(storedResource.explicitlyRetained ? 1 : 0);
    query.addBindValue(storedResource.deleted ? 1 : 0);
    query.addBindValue(storedResource.updatedAt.isValid()
                           ? storedResource.updatedAt.toUTC().toString(Qt::ISODate)
                           : QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!query.exec()) {
        setLastError(query.lastError().text());
        rollbackTransaction();
        return false;
    }

    for (const QString &table : {QStringLiteral("resource_tags"),
                                QStringLiteral("resource_aliases"),
                                QStringLiteral("anchors"),
                                QStringLiteral("resource_fts"),
                                QStringLiteral("anchor_fts")}) {
        QSqlQuery deleteQuery(database_);
        deleteQuery.prepare(QStringLiteral("DELETE FROM %1 WHERE resource_id = ?").arg(table));
        deleteQuery.addBindValue(storedResource.id);
        if (!deleteQuery.exec()) {
            setLastError(deleteQuery.lastError().text());
            rollbackTransaction();
            return false;
        }
    }

    for (const QString &tag : storedResource.tags) {
        QSqlQuery tagQuery(database_);
        tagQuery.prepare(QStringLiteral("INSERT OR IGNORE INTO resource_tags(resource_id, tag) VALUES (?, ?)"));
        tagQuery.addBindValue(storedResource.id);
        tagQuery.addBindValue(tag);
        if (!tagQuery.exec()) {
            setLastError(tagQuery.lastError().text());
            rollbackTransaction();
            return false;
        }
    }

    for (const QString &alias : storedResource.aliases) {
        QSqlQuery aliasQuery(database_);
        aliasQuery.prepare(QStringLiteral("INSERT OR IGNORE INTO resource_aliases(resource_id, alias) VALUES (?, ?)"));
        aliasQuery.addBindValue(storedResource.id);
        aliasQuery.addBindValue(alias);
        if (!aliasQuery.exec()) {
            setLastError(aliasQuery.lastError().text());
            rollbackTransaction();
            return false;
        }
    }

    for (int i = 0; i < storedResource.anchors.size(); ++i) {
        const Anchor &anchor = storedResource.anchors.at(i);
        QSqlQuery anchorQuery(database_);
        anchorQuery.prepare(QStringLiteral("INSERT INTO anchors(resource_id, anchor_order, id, name, target_app, "
                                           "target_file, target_uri, locator_type, locator_json, aliases, tags, "
                                           "pinned, deleted, created_at, updated_at, used_at) "
                                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        anchorQuery.addBindValue(storedResource.id);
        anchorQuery.addBindValue(i);
        anchorQuery.addBindValue(anchor.id);
        anchorQuery.addBindValue(anchor.name);
        anchorQuery.addBindValue(anchor.targetApp);
        anchorQuery.addBindValue(anchor.targetFile);
        anchorQuery.addBindValue(anchor.targetUri);
        anchorQuery.addBindValue(anchor.locatorType);
        anchorQuery.addBindValue(anchor.locatorJson);
        anchorQuery.addBindValue(anchor.aliases.join(QLatin1Char('\n')));
        anchorQuery.addBindValue(anchor.tags.join(QLatin1Char('\n')));
        anchorQuery.addBindValue(anchor.pinned ? 1 : 0);
        anchorQuery.addBindValue(anchor.deleted ? 1 : 0);
        anchorQuery.addBindValue(anchor.createdAt.isValid()
                                     ? anchor.createdAt.toUTC().toString(Qt::ISODate)
                                     : QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        anchorQuery.addBindValue(anchor.updatedAt.isValid()
                                     ? anchor.updatedAt.toUTC().toString(Qt::ISODate)
                                     : QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        anchorQuery.addBindValue(anchor.usedAt.isValid()
                                     ? anchor.usedAt.toUTC().toString(Qt::ISODate)
                                     : QVariant());
        if (!anchorQuery.exec()) {
            setLastError(anchorQuery.lastError().text());
            rollbackTransaction();
            return false;
        }

        if (shouldIndexAnchor(storedResource, anchor)) {
            QSqlQuery anchorFtsQuery(database_);
            anchorFtsQuery.prepare(QStringLiteral("INSERT INTO anchor_fts(resource_id, anchor_id, locator_type, text) "
                                                  "VALUES (?, ?, ?, ?)"));
            anchorFtsQuery.addBindValue(storedResource.id);
            anchorFtsQuery.addBindValue(anchor.id);
            anchorFtsQuery.addBindValue(anchorLocatorType(anchor));
            anchorFtsQuery.addBindValue(anchorSearchText(anchor));
            if (!anchorFtsQuery.exec()) {
                setLastError(anchorFtsQuery.lastError().text());
                rollbackTransaction();
                return false;
            }
        }
    }

    QSqlQuery ftsQuery(database_);
    ftsQuery.prepare(QStringLiteral("INSERT INTO resource_fts(resource_id, title, aliases, tags, location, content) "
                                    "VALUES (?, ?, ?, ?, ?, ?)"));
    ftsQuery.addBindValue(storedResource.id);
    ftsQuery.addBindValue(storedResource.title);
    ftsQuery.addBindValue(storedResource.aliases.join(QLatin1Char(' ')));
    ftsQuery.addBindValue(storedResource.tags.join(QLatin1Char(' ')));
    ftsQuery.addBindValue(storedResource.location);
    ftsQuery.addBindValue(storedResource.content);
    if (!ftsQuery.exec()) {
        setLastError(ftsQuery.lastError().text());
        rollbackTransaction();
        return false;
    }

    if (!commitTransaction()) {
        return false;
    }
    lastError_.clear();
    lastIdentityConflict_.reset();
    notifyChange(LibraryChangeKind::Content, {storedResource.id});
    return true;
}

std::optional<Resource> SqliteLibraryRepository::findResource(const QString &id) const
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return std::nullopt;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT id FROM resources WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    return hydrateResource(id);
}

QList<SearchResult> SqliteLibraryRepository::search(const SearchQuery &query) const
{
    QList<SearchResult> results;
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return results;
    }

    const QString ftsQuery = ftsQueryFromText(query.text);
    const QStringList plainTokens = plainSearchTokensFromText(query.text);
    const int candidateLimit = query.limit > 0
        ? std::max(200, query.limit * 4)
        : 0;
    QString sql;
    if (ftsQuery.isEmpty()) {
        sql = QStringLiteral("SELECT r.id, 0.0 AS score, 'all' AS matched_field "
                             "FROM resources r "
                             "LEFT JOIN resource_usage ru ON ru.resource_id = r.id "
                             "WHERE 1 = 1 ");
    } else {
        sql = QStringLiteral("SELECT r.id, bm25(resource_fts) AS score, 'fts' AS matched_field, resource_fts.content "
                             "FROM resource_fts JOIN resources r ON r.id = resource_fts.resource_id "
                             "WHERE resource_fts MATCH ? ");
    }

    for (int i = 0; i < query.requiredTags.size(); ++i) {
        sql += QStringLiteral("AND EXISTS (SELECT 1 FROM resource_tags rt%1 "
                              "WHERE rt%1.resource_id = r.id AND lower(rt%1.tag) = lower(?)) ")
                   .arg(i);
    }
    if (query.deletedOnly) {
        sql += QStringLiteral("AND r.deleted = 1 ");
    } else if (!query.includeDeleted) {
        sql += QStringLiteral("AND r.deleted = 0 ");
    }
    if (!query.requiredLocationPrefixes.isEmpty()) {
        sql += QStringLiteral("AND (");
        for (int i = 0; i < query.requiredLocationPrefixes.size(); ++i) {
            if (i > 0) sql += QStringLiteral(" OR ");
            sql += QStringLiteral(
                "instr(lower(replace(r.location, char(92), '/')), "
                "lower(replace(?, char(92), '/'))) = 1");
        }
        sql += QStringLiteral(") ");
    }
    if (!query.requiredKinds.isEmpty()) {
        sql += QStringLiteral(
            "AND CASE lower(r.kind) "
            "WHEN 'markdown' THEN 'file' "
            "WHEN 'code_snippet' THEN 'text_snippet' "
            "ELSE lower(r.kind) END IN (");
        for (int i = 0; i < query.requiredKinds.size(); ++i) {
            if (i > 0) sql += QLatin1Char(',');
            sql += QLatin1Char('?');
        }
        sql += QStringLiteral(") ");
    }

    sql += ftsQuery.isEmpty()
        ? QStringLiteral("ORDER BY COALESCE(ru.pinned, 0) DESC, "
                         "COALESCE(ru.open_count, 0) DESC, "
                         "COALESCE(ru.last_opened_at, '') DESC, lower(r.title), r.id ")
        : QStringLiteral("ORDER BY score, lower(r.title), r.id ");
    if (candidateLimit > 0) sql += QStringLiteral("LIMIT ? ");

    QSqlQuery sqlQuery(database_);
    sqlQuery.prepare(sql);
    if (!ftsQuery.isEmpty()) {
        sqlQuery.addBindValue(ftsQuery);
    }
    for (const QString &tag : query.requiredTags) {
        sqlQuery.addBindValue(tag);
    }
    for (const QString &prefix : query.requiredLocationPrefixes) {
        sqlQuery.addBindValue(prefix);
    }
    for (ResourceKind kind : query.requiredKinds) {
        sqlQuery.addBindValue(resourceKindToString(kind));
    }
    if (candidateLimit > 0) sqlQuery.addBindValue(candidateLimit);

    if (!sqlQuery.exec()) {
        setLastError(sqlQuery.lastError().text());
        return results;
    }

    while (sqlQuery.next()) {
        const QString id = sqlQuery.value(0).toString();
        std::optional<Resource> resource = findResource(id);
        if (!resource.has_value()) {
            continue;
        }
        if ((query.deletedOnly && !resource->deleted)
            || (!query.deletedOnly && !query.includeDeleted && resource->deleted)) {
            continue;
        }
        if (!matchesRequiredLocationPrefixes(resource->location, query.requiredLocationPrefixes)) {
            continue;
        }
        if (!matchesRequiredKinds(resource->kind, query.requiredKinds)) {
            continue;
        }
        if (!ftsQuery.isEmpty()) {
            resource->content = sqlQuery.value(3).toString();
        }
        SearchResult result = resourceSearchResult(resource.value(), plainTokens, query.text);
        applyRankingSignals(result, query);
        results.append(result);
    }

    if (!ftsQuery.isEmpty() || query.deletedOnly) {
        QString anchorSql;
        if (ftsQuery.isEmpty()) {
            anchorSql = QStringLiteral(
                "SELECT r.id, a.id, 0.0 AS score "
                "FROM anchors a JOIN resources r ON r.id = a.resource_id WHERE 1 = 1 ");
        } else {
            anchorSql = QStringLiteral(
                "SELECT r.id, anchor_fts.anchor_id, bm25(anchor_fts) AS score "
                "FROM anchor_fts JOIN resources r ON r.id = anchor_fts.resource_id "
                "JOIN anchors a ON a.resource_id = r.id AND a.id = anchor_fts.anchor_id "
                "WHERE anchor_fts MATCH ? ");
        }
        for (int i = 0; i < query.requiredTags.size(); ++i) {
            anchorSql += QStringLiteral("AND EXISTS (SELECT 1 FROM resource_tags art%1 "
                                        "WHERE art%1.resource_id = r.id AND lower(art%1.tag) = lower(?)) ")
                             .arg(i);
        }
        if (query.deletedOnly) {
            anchorSql += QStringLiteral("AND r.deleted = 0 AND a.deleted = 1 ");
        } else if (!query.includeDeleted) {
            anchorSql += QStringLiteral("AND r.deleted = 0 AND a.deleted = 0 ");
        }
        if (!query.requiredLocationPrefixes.isEmpty()) {
            anchorSql += QStringLiteral("AND (");
            for (int i = 0; i < query.requiredLocationPrefixes.size(); ++i) {
                if (i > 0) anchorSql += QStringLiteral(" OR ");
                anchorSql += QStringLiteral(
                    "instr(lower(replace(r.location, char(92), '/')), "
                    "lower(replace(?, char(92), '/'))) = 1");
            }
            anchorSql += QStringLiteral(") ");
        }
        if (!query.requiredKinds.isEmpty()) {
            anchorSql += QStringLiteral(
                "AND CASE lower(r.kind) "
                "WHEN 'markdown' THEN 'file' "
                "WHEN 'code_snippet' THEN 'text_snippet' "
                "ELSE lower(r.kind) END IN (");
            for (int i = 0; i < query.requiredKinds.size(); ++i) {
                if (i > 0) anchorSql += QLatin1Char(',');
                anchorSql += QLatin1Char('?');
            }
            anchorSql += QStringLiteral(") ");
        }
        anchorSql += ftsQuery.isEmpty()
            ? QStringLiteral("ORDER BY lower(r.title), r.id, a.anchor_order ")
            : QStringLiteral("ORDER BY score, lower(r.title), r.id ");
        if (candidateLimit > 0) anchorSql += QStringLiteral("LIMIT ? ");

        QSqlQuery anchorQuery(database_);
        anchorQuery.prepare(anchorSql);
        if (!ftsQuery.isEmpty()) anchorQuery.addBindValue(ftsQuery);
        for (const QString &tag : query.requiredTags) {
            anchorQuery.addBindValue(tag);
        }
        for (const QString &prefix : query.requiredLocationPrefixes) {
            anchorQuery.addBindValue(prefix);
        }
        for (ResourceKind kind : query.requiredKinds) {
            anchorQuery.addBindValue(resourceKindToString(kind));
        }
        if (candidateLimit > 0) anchorQuery.addBindValue(candidateLimit);
        if (!anchorQuery.exec()) {
            setLastError(anchorQuery.lastError().text());
            return results;
        }

        while (anchorQuery.next()) {
            const QString id = anchorQuery.value(0).toString();
            const QString anchorId = anchorQuery.value(1).toString();
            const std::optional<Resource> resource = findResource(id);
            if (!resource.has_value()) {
                continue;
            }
            if ((query.deletedOnly && resource->deleted)
                || (!query.deletedOnly && !query.includeDeleted && resource->deleted)) {
                continue;
            }
            if (!matchesRequiredLocationPrefixes(resource->location, query.requiredLocationPrefixes)) {
                continue;
            }
            if (!matchesRequiredKinds(resource->kind, query.requiredKinds)) {
                continue;
            }
            const auto anchorIt = std::find_if(resource->anchors.cbegin(),
                                               resource->anchors.cend(),
                                               [&anchorId](const Anchor &anchor) {
                                                   return anchor.id == anchorId;
                                               });
            if (anchorIt == resource->anchors.cend()) continue;
            const Anchor &anchor = *anchorIt;
            if ((query.deletedOnly && !anchor.deleted)
                || (!query.deletedOnly && !query.includeDeleted && anchor.deleted)) {
                continue;
            }
            SearchResult result = ftsQuery.isEmpty()
                ? SearchResult{resource.value(), 100.0, QStringLiteral("anchor"), anchor}
                : anchorSearchResult(resource.value(), anchor, plainTokens, query.text);
            applyRankingSignals(result, query);
            results.append(result);
        }
    }

    std::sort(results.begin(), results.end(), searchResultLessThan);

    if (query.limit > 0 && results.size() > query.limit) {
        results.erase(results.begin() + query.limit, results.end());
    }

    return results;
}

bool SqliteLibraryRepository::softDeleteResource(const QString &resourceId)
{
    const std::optional<Resource> stored = findResource(resourceId);
    if (!stored.has_value()) {
        setLastError(QStringLiteral("Resource not found"));
        return false;
    }
    Resource updated = stored.value();
    updated.deleted = true;
    updated.updatedAt = QDateTime::currentDateTimeUtc();
    return upsertResource(updated);
}

bool SqliteLibraryRepository::restoreResource(const QString &resourceId)
{
    const std::optional<Resource> stored = findResource(resourceId);
    if (!stored.has_value()) {
        setLastError(QStringLiteral("Resource not found"));
        return false;
    }
    Resource updated = stored.value();
    updated.deleted = false;
    updated.updatedAt = QDateTime::currentDateTimeUtc();
    return upsertResource(updated);
}

bool SqliteLibraryRepository::softDeleteAnchor(const QString &resourceId, const Anchor &anchor)
{
    std::optional<Resource> resource = findResource(resourceId);
    if (!resource.has_value()) {
        setLastError(QStringLiteral("Resource not found"));
        return false;
    }

    const Anchor normalized = anchor;
    for (Anchor &storedAnchor : resource->anchors) {
        if (anchorUsageKey(storedAnchor) != anchorUsageKey(normalized)) {
            continue;
        }
        storedAnchor.deleted = true;
        storedAnchor.updatedAt = QDateTime::currentDateTimeUtc();
        resource->updatedAt = storedAnchor.updatedAt;
        return upsertResource(resource.value());
    }

    setLastError(QStringLiteral("Anchor not found"));
    return false;
}

bool SqliteLibraryRepository::restoreAnchor(const QString &resourceId, const Anchor &anchor)
{
    std::optional<Resource> resource = findResource(resourceId);
    if (!resource.has_value()) {
        setLastError(QStringLiteral("Resource not found"));
        return false;
    }

    const Anchor normalized = anchor;
    for (Anchor &storedAnchor : resource->anchors) {
        if (anchorUsageKey(storedAnchor) != anchorUsageKey(normalized)) {
            continue;
        }
        storedAnchor.deleted = false;
        storedAnchor.updatedAt = QDateTime::currentDateTimeUtc();
        resource->updatedAt = storedAnchor.updatedAt;
        return upsertResource(resource.value());
    }

    setLastError(QStringLiteral("Anchor not found"));
    return false;
}

bool SqliteLibraryRepository::clearResources()
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    QHash<QString, GlobalIdentityObject> identityUpdates;
    for (const Resource &resource : allResourcesForIdentity()) {
        addIdentityReplacement(identityUpdates, resource, std::nullopt);
    }
    if (!lastError_.isEmpty()) {
        return false;
    }

    if (!beginTransaction()) {
        return false;
    }

    QString identityError;
    lastIdentityConflict_.reset();
    if (!replaceSqliteGlobalIdentityObjects(database_,
                                            identityUpdates.values(),
                                            &lastIdentityConflict_,
                                            &identityError)) {
        setLastError(identityError);
        rollbackTransaction();
        return false;
    }

    if (!clearResourceTables()) {
        rollbackTransaction();
        return false;
    }

    if (!commitTransaction()) {
        return false;
    }
    notifyChange(LibraryChangeKind::Reset);
    return true;
}

bool SqliteLibraryRepository::applyBatch(const LibraryBatchMutation &mutation)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    lastError_.clear();
    lastIdentityConflict_.reset();
    if (!beginTransaction()) return false;
    QHash<QString, std::optional<Resource>> originals;
    QHash<QString, std::optional<Resource>> replacements;
    if (mutation.clearExistingResources) {
        for (const Resource &resource : allResourcesForIdentity()) {
            originals.insert(resource.id, resource);
            replacements.insert(resource.id, std::nullopt);
        }
        if (!lastError_.isEmpty()) {
            rollbackTransaction();
            return false;
        }
    }
    for (const QString &resourceIdValue : mutation.permanentlyDeleteResourceIds) {
        const QString resourceId = resourceIdValue.trimmed();
        const std::optional<Resource> existing = findResource(resourceId);
        if (!existing.has_value()) {
            setLastError(QStringLiteral("Resource not found: %1").arg(resourceId));
            rollbackTransaction();
            return false;
        }
        originals.insert(resourceId, existing);
        replacements.insert(resourceId, std::nullopt);
    }
    for (const Resource &resource : mutation.upserts) {
        if (resource.id.trimmed().isEmpty()) {
            setLastError(QStringLiteral("Resource id is required"));
            rollbackTransaction();
            return false;
        }
        const Resource normalized = normalizedResource(resource);
        if (!originals.contains(normalized.id)) originals.insert(normalized.id, findResource(normalized.id));
        replacements.insert(normalized.id, normalized);
    }
    if (!lastError_.isEmpty()) {
        rollbackTransaction();
        return false;
    }
    QHash<QString, GlobalIdentityObject> identityUpdates;
    for (auto it = replacements.cbegin(); it != replacements.cend(); ++it) {
        addIdentityReplacement(identityUpdates, originals.value(it.key()), it.value());
    }

    ++deferredChangeDepth_;

    QString identityError;
    if (!replaceSqliteGlobalIdentityObjects(database_,
                                            identityUpdates.values(),
                                            &lastIdentityConflict_,
                                            &identityError)) {
        setLastError(identityError);
        rollbackTransaction();
        --deferredChangeDepth_;
        return false;
    }

    bool succeeded = true;
    const bool previousIdentityDeferral = identityUpdatesDeferred_;
    identityUpdatesDeferred_ = true;
    if (mutation.clearExistingResources && !clearResourceTables()) {
        succeeded = false;
    }
    for (const QString &resourceId : mutation.permanentlyDeleteResourceIds) {
        if (succeeded && !deleteResourcePermanently(resourceId)) {
            succeeded = false;
        }
    }
    for (const Resource &resource : mutation.upserts) {
        if (succeeded && !upsertResource(resource)) {
            succeeded = false;
        }
    }
    for (const ResourcePinUpdate &update : mutation.resourcePinUpdates) {
        if (succeeded && !setResourcePinned(update.resourceId, update.pinned)) {
            succeeded = false;
        }
    }

    if (!succeeded) {
        rollbackTransaction();
    } else if (!commitTransaction()) {
        succeeded = false;
    }
    identityUpdatesDeferred_ = previousIdentityDeferral;

    --deferredChangeDepth_;
    if (!succeeded) {
        if (deferredChangeDepth_ == 0) {
            deferredResourceIds_.clear();
            deferredChangeKind_ = LibraryChangeKind::Content;
        }
        return false;
    }

    if (deferredChangeDepth_ == 0) {
        const QStringList ids = deferredResourceIds_;
        const LibraryChangeKind kind = mutation.clearExistingResources
            ? LibraryChangeKind::Reset
            : deferredChangeKind_;
        deferredResourceIds_.clear();
        deferredChangeKind_ = LibraryChangeKind::Content;
        notifyChange(kind, ids);
    }
    return true;
}

bool SqliteLibraryRepository::upsertLibraryRoot(const LibraryRoot &root)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }
    LibraryRoot stored = root;
    stored.path = normalizedLibraryRootPath(root.path);
    if (stored.id.trimmed().isEmpty()) {
        stored.id = libraryRootIdForPath(stored.path);
    }
    if (stored.id.isEmpty() || stored.path.isEmpty()) {
        setLastError(QStringLiteral("Library root id and path are required"));
        return false;
    }
    if (stored.displayName.trimmed().isEmpty()) {
        stored.displayName = QFileInfo(stored.path).fileName();
    }
    if (!stored.updatedAt.isValid()) {
        stored.updatedAt = QDateTime::currentDateTimeUtc();
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "INSERT INTO library_roots(id, path, display_name, enabled, sync_root, "
        "ignored_directory_names, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET id = excluded.id, path = excluded.path, "
        "display_name = excluded.display_name, enabled = excluded.enabled, "
        "sync_root = excluded.sync_root, ignored_directory_names = excluded.ignored_directory_names, "
        "updated_at = excluded.updated_at"));
    query.addBindValue(stored.id);
    query.addBindValue(stored.path);
    query.addBindValue(stored.displayName);
    query.addBindValue(stored.enabled ? 1 : 0);
    query.addBindValue(stored.syncRoot ? 1 : 0);
    query.addBindValue(stored.ignoredDirectoryNames.join(QLatin1Char('\n')));
    query.addBindValue(stored.updatedAt.toUTC().toString(Qt::ISODate));
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
    notifyChange(LibraryChangeKind::Content);
    return true;
}

QList<LibraryRoot> SqliteLibraryRepository::libraryRoots() const
{
    QList<LibraryRoot> roots;
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return roots;
    }
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral(
            "SELECT id, path, display_name, enabled, sync_root, ignored_directory_names, updated_at "
            "FROM library_roots ORDER BY sync_root DESC, lower(display_name), lower(path)"))) {
        setLastError(query.lastError().text());
        return roots;
    }
    while (query.next()) {
        roots.append(hydrateLibraryRoot(query));
    }
    return roots;
}

std::optional<LibraryRoot> SqliteLibraryRepository::findLibraryRoot(const QString &id) const
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return std::nullopt;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, path, display_name, enabled, sync_root, ignored_directory_names, updated_at "
        "FROM library_roots WHERE id = ?"));
    query.addBindValue(id.trimmed());
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return std::nullopt;
    }
    return query.next() ? std::optional<LibraryRoot>(hydrateLibraryRoot(query)) : std::nullopt;
}

bool SqliteLibraryRepository::removeLibraryRoot(const QString &id)
{
    const std::optional<LibraryRoot> root = findLibraryRoot(id);
    if (!root.has_value()) {
        setLastError(QStringLiteral("Library root not found"));
        return false;
    }
    if (root->syncRoot) {
        setLastError(QStringLiteral("The default sync root cannot be removed"));
        return false;
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM library_roots WHERE id = ?"));
    query.addBindValue(id.trimmed());
    if (!query.exec() || query.numRowsAffected() <= 0) {
        setLastError(query.lastError().text().trimmed().isEmpty()
                         ? QStringLiteral("Unable to remove library root")
                         : query.lastError().text());
        return false;
    }
    notifyChange(LibraryChangeKind::Content);
    return true;
}

bool SqliteLibraryRepository::recordResourceOpen(const QString &resourceId)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }
    if (!findResource(resourceId).has_value()) {
        setLastError(QStringLiteral("Resource not found"));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO resource_usage(resource_id, open_count, last_opened_at, pinned) "
                                 "VALUES (?, 1, ?, 0) "
                                 "ON CONFLICT(resource_id) DO UPDATE SET "
                                 "open_count = resource_usage.open_count + 1,"
                                 "last_opened_at = excluded.last_opened_at"));
    query.addBindValue(resourceId);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
    notifyChange(LibraryChangeKind::Usage, {resourceId});
    return true;
}

bool SqliteLibraryRepository::setResourcePinned(const QString &resourceId, bool pinned)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }
    if (!findResource(resourceId).has_value()) {
        setLastError(QStringLiteral("Resource not found"));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO resource_usage(resource_id, open_count, last_opened_at, pinned) "
                                 "VALUES (?, 0, NULL, ?) "
                                 "ON CONFLICT(resource_id) DO UPDATE SET "
                                 "pinned = excluded.pinned"));
    query.addBindValue(resourceId);
    query.addBindValue(pinned ? 1 : 0);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
    notifyChange(LibraryChangeKind::Usage, {resourceId});
    return true;
}

std::optional<ResourceUsage> SqliteLibraryRepository::resourceUsage(const QString &resourceId) const
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return std::nullopt;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT resource_id, open_count, last_opened_at, pinned "
                                 "FROM resource_usage WHERE resource_id = ?"));
    query.addBindValue(resourceId);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    return hydrateResourceUsage(query);
}

bool SqliteLibraryRepository::recordAnchorOpen(const QString &resourceId, const Anchor &anchor)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }
    if (!hasAnchorIdentity(anchor)) {
        setLastError(QStringLiteral("Anchor identity is required"));
        return false;
    }
    const std::optional<Resource> resource = findResource(resourceId);
    if (!resource.has_value()) {
        setLastError(QStringLiteral("Resource not found"));
        return false;
    }

    Anchor effective = anchor;
    for (const Anchor &storedAnchor : resource->anchors) {
        if (sameAnchorIdentity(storedAnchor, anchor)) {
            effective = storedAnchor;
            break;
        }
    }

    const QString openedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO anchor_usage(resource_id, anchor_key, open_count, last_opened_at) "
                                 "VALUES (?, ?, 1, ?) "
                                 "ON CONFLICT(resource_id, anchor_key) DO UPDATE SET "
                                 "open_count = anchor_usage.open_count + 1,"
                                 "last_opened_at = excluded.last_opened_at"));
    query.addBindValue(resourceId);
    query.addBindValue(anchorUsageKey(effective));
    query.addBindValue(openedAt);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }

    if (!effective.id.trimmed().isEmpty()) {
        QSqlQuery updateAnchor(database_);
        updateAnchor.prepare(QStringLiteral("UPDATE anchors SET used_at = ? WHERE resource_id = ? AND id = ?"));
        updateAnchor.addBindValue(openedAt);
        updateAnchor.addBindValue(resourceId);
        updateAnchor.addBindValue(effective.id);
        if (!updateAnchor.exec()) {
            setLastError(updateAnchor.lastError().text());
            return false;
        }
    }
    notifyChange(LibraryChangeKind::Usage, {resourceId});
    return true;
}

std::optional<AnchorUsage> SqliteLibraryRepository::anchorUsage(const QString &resourceId, const Anchor &anchor) const
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return std::nullopt;
    }

    Anchor effective = anchor;
    const std::optional<Resource> resource = findResource(resourceId);
    if (resource.has_value()) {
        for (const Anchor &storedAnchor : resource->anchors) {
            if (sameAnchorIdentity(storedAnchor, anchor)) {
                effective = storedAnchor;
                break;
            }
        }
    }

    for (const QString &key : anchorUsageKeys(effective)) {
        QSqlQuery query(database_);
        query.prepare(QStringLiteral("SELECT resource_id, open_count, last_opened_at "
                                     "FROM anchor_usage WHERE resource_id = ? AND anchor_key = ?"));
        query.addBindValue(resourceId);
        query.addBindValue(key);
        if (!query.exec()) {
            setLastError(query.lastError().text());
            return std::nullopt;
        }
        if (query.next()) {
            return hydrateAnchorUsage(query);
        }
    }

    return std::nullopt;
}

quint64 SqliteLibraryRepository::changeRevision() const
{
    return revision_;
}

quint64 SqliteLibraryRepository::contentRevision() const
{
    return contentRevision_;
}

int SqliteLibraryRepository::addChangeListener(LibraryChangeListener listener)
{
    if (!listener) {
        return 0;
    }
    const int listenerId = nextListenerId_++;
    listeners_.insert(listenerId, std::move(listener));
    return listenerId;
}

void SqliteLibraryRepository::removeChangeListener(int listenerId)
{
    listeners_.remove(listenerId);
}

QString SqliteLibraryRepository::databasePath() const
{
    return database_.isValid() ? QFileInfo(database_.databaseName()).absoluteFilePath() : QString();
}

bool SqliteLibraryRepository::integrityCheck()
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

bool SqliteLibraryRepository::backupDatabase(const QString &destinationPath)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }
    if (destinationPath.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Database backup path is required"));
        return false;
    }
    const QString source = databasePath();
    const QString destination = QFileInfo(destinationPath).absoluteFilePath();
    if (source.isEmpty() || destination.isEmpty() || source.compare(destination, Qt::CaseInsensitive) == 0) {
        setLastError(QStringLiteral("Backup destination must differ from the active database"));
        return false;
    }
    QDir destinationDirectory(QFileInfo(destination).absolutePath());
    if (!destinationDirectory.mkpath(QStringLiteral("."))) {
        setLastError(QStringLiteral("Unable to create backup directory"));
        return false;
    }

    QSqlQuery checkpoint(database_);
    if (!checkpoint.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"))
        || !checkpoint.next()
        || checkpoint.value(0).toInt() != 0) {
        setLastError(QStringLiteral("Unable to checkpoint the active database before backup"));
        return false;
    }

    const QString temporary = destination
        + QStringLiteral(".tmp-")
        + QUuid::createUuid().toString(QUuid::Id128);
    QFile::remove(temporary);
    if (!QFile::copy(source, temporary)) {
        setLastError(QStringLiteral("Unable to copy the active database to the backup"));
        return false;
    }
    if (QFile::exists(destination) && !QFile::remove(destination)) {
        QFile::remove(temporary);
        setLastError(QStringLiteral("Unable to replace the existing backup"));
        return false;
    }
    if (!QFile::rename(temporary, destination)) {
        QFile::remove(temporary);
        setLastError(QStringLiteral("Unable to finalize the database backup"));
        return false;
    }
    lastError_.clear();
    return true;
}

bool SqliteLibraryRepository::restoreDatabase(const QString &sourcePath)
{
    if (sourcePath.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Database restore path is required"));
        return false;
    }
    const QString source = QFileInfo(sourcePath).absoluteFilePath();
    const QString destination = databasePath();
    if (!QFileInfo::exists(source) || destination.isEmpty()
        || source.compare(destination, Qt::CaseInsensitive) == 0) {
        setLastError(QStringLiteral("A separate SQLite backup file is required"));
        return false;
    }

    const QString validationConnection = QStringLiteral("pinloom_restore_validation_%1")
                                             .arg(QUuid::createUuid().toString(QUuid::Id128));
    bool validBackup = false;
    {
        QSqlDatabase validation = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), validationConnection);
        validation.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        validation.setDatabaseName(source);
        if (validation.open()) {
            QSqlQuery query(validation);
            validBackup = query.exec(QStringLiteral(
                              "SELECT COUNT(*) FROM sqlite_master "
                              "WHERE type = 'table' AND name IN ('resources', 'schema_migrations')"))
                && query.next()
                && query.value(0).toInt() == 2;
        }
        validation.close();
    }
    QSqlDatabase::removeDatabase(validationConnection);
    if (!validBackup) {
        setLastError(QStringLiteral("The selected file is not a Pinloom SQLite backup"));
        return false;
    }

    const QString rollbackCopy = destination
        + QStringLiteral(".restore-rollback-")
        + QUuid::createUuid().toString(QUuid::Id128);
    const QString replacementCopy = destination
        + QStringLiteral(".restore-new-")
        + QUuid::createUuid().toString(QUuid::Id128);
    QFile::remove(replacementCopy);
    if (!QFile::copy(source, replacementCopy)) {
        setLastError(QStringLiteral("Unable to stage the selected database backup"));
        return false;
    }
    QSqlQuery checkpoint(database_);
    if (!checkpoint.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"))
        || !checkpoint.next()
        || checkpoint.value(0).toInt() != 0) {
        QFile::remove(replacementCopy);
        setLastError(QStringLiteral("Unable to checkpoint the active database before restore"));
        return false;
    }
    database_.close();
    transactionDepth_ = 0;
    const auto reopenActiveDatabase = [this]() {
        return database_.open()
            && execute(QStringLiteral("PRAGMA foreign_keys = ON;"))
            && execute(QStringLiteral("PRAGMA busy_timeout = 5000;"))
            && attachGlobalIdentityRegistry();
    };
    const auto removeSidecars = [&destination]() {
        bool removed = true;
        for (const QString &suffix : {QStringLiteral("-wal"), QStringLiteral("-shm")}) {
            const QString path = destination + suffix;
            if (QFile::exists(path) && !QFile::remove(path)) {
                removed = false;
            }
        }
        return removed;
    };

    QFile::remove(rollbackCopy);
    if (!QFile::copy(destination, rollbackCopy)) {
        QFile::remove(replacementCopy);
        reopenActiveDatabase();
        setLastError(QStringLiteral("Unable to create a rollback copy of the active database"));
        return false;
    }
    if (!removeSidecars() || !QFile::remove(destination)) {
        QFile::remove(replacementCopy);
        reopenActiveDatabase();
        QFile::remove(rollbackCopy);
        setLastError(QStringLiteral("Unable to replace the active database"));
        return false;
    }
    if (!QFile::rename(replacementCopy, destination)) {
        const bool rollbackRestored = QFile::copy(rollbackCopy, destination)
            && reopenActiveDatabase();
        if (rollbackRestored) {
            QFile::remove(rollbackCopy);
        }
        setLastError(rollbackRestored
                         ? QStringLiteral("Unable to move the staged backup into place")
                         : QStringLiteral("Unable to restore the active database; rollback preserved at %1")
                               .arg(rollbackCopy));
        return false;
    }

    if (!database_.open()
        || !execute(QStringLiteral("PRAGMA foreign_keys = ON;"))
        || !execute(QStringLiteral("PRAGMA busy_timeout = 5000;"))
        || !attachGlobalIdentityRegistry()
        || !initialize()) {
        const QString restoreError = lastError_;
        database_.close();
        removeSidecars();
        QFile::remove(destination);
        const bool rollbackRestored = QFile::copy(rollbackCopy, destination)
            && reopenActiveDatabase()
            && initialize();
        if (rollbackRestored) {
            QFile::remove(rollbackCopy);
        }
        setLastError(rollbackRestored
                         ? QStringLiteral("Unable to open the restored database: %1").arg(restoreError)
                         : QStringLiteral("Unable to open the restored database; rollback preserved at %1")
                               .arg(rollbackCopy));
        return false;
    }

    QFile::remove(rollbackCopy);
    lastError_.clear();
    notifyChange(LibraryChangeKind::Reset);
    return true;
}

bool SqliteLibraryRepository::execute(const QString &sql)
{
    QSqlQuery query(database_);
    if (!query.exec(sql)) {
        setLastError(query.lastError().text());
        return false;
    }
    return true;
}

bool SqliteLibraryRepository::attachGlobalIdentityRegistry()
{
    QString error;
    if (attachSqliteGlobalIdentityRegistry(database_, identityRegistryPath_, &error)) {
        return true;
    }
    setLastError(error);
    return false;
}

bool SqliteLibraryRepository::rebuildGlobalIdentityRegistry()
{
    QList<GlobalIdentityObject> objects;
    for (const Resource &resource : allResourcesForIdentity()) {
        objects.append(identityObjectsForResource(resource));
    }
    if (!lastError_.isEmpty()) {
        return false;
    }
    QString error;
    if (!rebuildSqliteHistoricalIdentityObjects(
            database_,
            {GlobalIdentityObjectType::File, GlobalIdentityObjectType::Anchor},
            objects,
            &error)) {
        setLastError(error);
        return false;
    }
    return true;
}

bool SqliteLibraryRepository::ensureAnchorLocatorColumns()
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("PRAGMA table_info(anchors)"))) {
        setLastError(query.lastError().text());
        return false;
    }

    QStringList columns;
    while (query.next()) {
        columns.append(query.value(1).toString());
    }

    struct ColumnDefinition {
        QString name;
        QString definition;
    };

    const QList<ColumnDefinition> requiredColumns = {
        {QStringLiteral("id"), QStringLiteral("TEXT")},
        {QStringLiteral("name"), QStringLiteral("TEXT")},
        {QStringLiteral("target_app"), QStringLiteral("TEXT")},
        {QStringLiteral("target_file"), QStringLiteral("TEXT")},
        {QStringLiteral("target_uri"), QStringLiteral("TEXT")},
        {QStringLiteral("locator_type"), QStringLiteral("TEXT")},
        {QStringLiteral("locator_json"), QStringLiteral("TEXT")},
        {QStringLiteral("aliases"), QStringLiteral("TEXT")},
        {QStringLiteral("tags"), QStringLiteral("TEXT")},
        {QStringLiteral("pinned"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("created_at"), QStringLiteral("TEXT")},
        {QStringLiteral("updated_at"), QStringLiteral("TEXT")},
        {QStringLiteral("used_at"), QStringLiteral("TEXT")},
    };

    for (const ColumnDefinition &column : requiredColumns) {
        if (columns.contains(column.name)) {
            continue;
        }
        if (!execute(QStringLiteral("ALTER TABLE anchors ADD COLUMN %1 %2").arg(column.name, column.definition))) {
            return false;
        }
    }

    return true;
}

bool SqliteLibraryRepository::ensureSoftDeleteColumns()
{
    const auto tableColumns = [this](const QString &table) -> QStringList {
        QStringList columns;
        QSqlQuery query(database_);
        if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
            setLastError(query.lastError().text());
            return columns;
        }
        while (query.next()) {
            columns.append(query.value(1).toString());
        }
        return columns;
    };

    const QStringList resourceColumns = tableColumns(QStringLiteral("resources"));
    if (resourceColumns.isEmpty()) {
        return false;
    }
    if (!resourceColumns.contains(QStringLiteral("deleted"))
        && !execute(QStringLiteral("ALTER TABLE resources ADD COLUMN deleted INTEGER NOT NULL DEFAULT 0"))) {
        return false;
    }

    const QStringList anchorColumns = tableColumns(QStringLiteral("anchors"));
    if (anchorColumns.isEmpty()) {
        return false;
    }
    if (!anchorColumns.contains(QStringLiteral("deleted"))
        && !execute(QStringLiteral("ALTER TABLE anchors ADD COLUMN deleted INTEGER NOT NULL DEFAULT 0"))) {
        return false;
    }

    return true;
}

bool SqliteLibraryRepository::ensureResourceRetentionColumn()
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("PRAGMA table_info(resources)"))) {
        setLastError(query.lastError().text());
        return false;
    }
    bool hasRetentionColumn = false;
    while (query.next()) {
        if (query.value(1).toString() == QLatin1String("explicitly_retained")) {
            hasRetentionColumn = true;
            break;
        }
    }
    return hasRetentionColumn
        || execute(QStringLiteral(
            "ALTER TABLE resources ADD COLUMN explicitly_retained INTEGER NOT NULL DEFAULT 0"));
}

bool SqliteLibraryRepository::ensureLibraryRootColumns()
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("PRAGMA table_info(library_roots)"))) {
        setLastError(query.lastError().text());
        return false;
    }
    QStringList columns;
    while (query.next()) {
        columns.append(query.value(1).toString());
    }
    if (columns.isEmpty()) {
        setLastError(QStringLiteral("Library root table is unavailable"));
        return false;
    }
    struct ColumnDefinition {
        QString name;
        QString definition;
    };
    const QList<ColumnDefinition> requiredColumns = {
        {QStringLiteral("sync_root"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("ignored_directory_names"), QStringLiteral("TEXT")},
        {QStringLiteral("updated_at"), QStringLiteral("TEXT")},
    };
    for (const ColumnDefinition &column : requiredColumns) {
        if (!columns.contains(column.name)
            && !execute(QStringLiteral("ALTER TABLE library_roots ADD COLUMN %1 %2")
                            .arg(column.name, column.definition))) {
            return false;
        }
    }
    return true;
}

bool SqliteLibraryRepository::migrateCanonicalAnchorSchema()
{
    QSqlQuery columnsQuery(database_);
    if (!columnsQuery.exec(QStringLiteral("PRAGMA table_info(anchors)"))) {
        setLastError(columnsQuery.lastError().text());
        return false;
    }

    QStringList columns;
    while (columnsQuery.next()) {
        columns.append(columnsQuery.value(1).toString());
    }
    if (!columns.contains(QStringLiteral("type"))) {
        return true;
    }

    struct LegacyAnchorRow {
        QString resourceId;
        int order = 0;
        QString id;
        QString name;
        QString targetApp;
        QString targetFile;
        QString targetUri;
        QString locatorType;
        QString locatorJson;
        QString legacyType;
        QString legacyTarget;
        int line = -1;
        int page = -1;
        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;
        QString resourceKind;
        QString resourceLocation;
    };

    QList<LegacyAnchorRow> rows;
    QSqlQuery selectQuery(database_);
    if (!selectQuery.exec(
            QStringLiteral("SELECT a.resource_id, a.anchor_order, a.id, a.name, a.target_app, "
                           "a.target_file, a.target_uri, a.locator_type, a.locator_json, "
                           "a.type, a.target, a.line, a.page, a.x, a.y, a.width, a.height, "
                           "r.kind, r.location "
                           "FROM anchors a JOIN resources r ON r.id = a.resource_id "
                           "ORDER BY a.resource_id, a.anchor_order"))) {
        setLastError(selectQuery.lastError().text());
        return false;
    }

    while (selectQuery.next()) {
        LegacyAnchorRow row;
        row.resourceId = selectQuery.value(0).toString();
        row.order = selectQuery.value(1).toInt();
        row.id = selectQuery.value(2).toString().trimmed();
        row.name = selectQuery.value(3).toString();
        row.targetApp = selectQuery.value(4).toString().trimmed();
        row.targetFile = selectQuery.value(5).toString().trimmed();
        row.targetUri = selectQuery.value(6).toString().trimmed();
        row.locatorType = selectQuery.value(7).toString().trimmed().toLower();
        row.locatorJson = selectQuery.value(8).toString().trimmed();
        row.legacyType = selectQuery.value(9).toString().trimmed().toLower();
        row.legacyTarget = selectQuery.value(10).toString().trimmed();
        row.line = selectQuery.value(11).isNull() ? -1 : selectQuery.value(11).toInt();
        row.page = selectQuery.value(12).isNull() ? -1 : selectQuery.value(12).toInt();
        row.x = selectQuery.value(13).toDouble();
        row.y = selectQuery.value(14).toDouble();
        row.width = selectQuery.value(15).toDouble();
        row.height = selectQuery.value(16).toDouble();
        row.resourceKind = selectQuery.value(17).toString().trimmed().toLower();
        row.resourceLocation = selectQuery.value(18).toString().trimmed();
        rows.append(row);
    }

    struct UsageKeyMigration {
        QString resourceId;
        QStringList oldKeys;
        QString newKey;
    };
    QList<UsageKeyMigration> usageKeyMigrations;

    for (LegacyAnchorRow &row : rows) {
        const bool generatedId = row.id.isEmpty();
        if (generatedId) {
            row.id = QStringLiteral("%1#anchor-%2").arg(row.resourceId, QString::number(row.order));
        }
        if (row.name.isEmpty()) {
            row.name = row.legacyTarget;
        }
        if (row.locatorType.isEmpty()) {
            row.locatorType = locatorTypeFromLegacyAnchorType(row.legacyType);
        }
        if (row.locatorType.isEmpty() && row.line > 0) {
            row.locatorType = row.resourceKind == QLatin1String("pdf")
                ? QStringLiteral("manual")
                : QStringLiteral("file.line");
        }

        QJsonObject locator;
        if (!row.locatorJson.isEmpty()) {
            QJsonParseError parseError;
            const QJsonDocument document =
                QJsonDocument::fromJson(row.locatorJson.toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && document.isObject()) {
                locator = document.object();
            } else {
                locator.insert(QStringLiteral("legacy_locator_json"), row.locatorJson);
            }
        }
        if (!row.locatorType.isEmpty() && !locator.contains(QStringLiteral("type"))) {
            locator.insert(QStringLiteral("type"), row.locatorType);
        }
        if (row.line > 0 && !locator.contains(QStringLiteral("line"))) {
            locator.insert(QStringLiteral("line"), row.line);
        }
        if (row.page > 0 && !locator.contains(QStringLiteral("page"))) {
            locator.insert(QStringLiteral("page"), row.page);
        }
        if (row.width > 0.0 && row.height > 0.0
            && !locator.contains(QStringLiteral("rect"))
            && !locator.contains(QStringLiteral("highlight"))
            && !locator.contains(QStringLiteral("viewrect"))
            && !locator.contains(QStringLiteral("region"))) {
            QJsonArray region;
            region.append(row.x);
            region.append(row.y);
            region.append(row.width);
            region.append(row.height);
            locator.insert(QStringLiteral("region"), region);
        }
        if (row.locatorType == QLatin1String("url.fragment")
            && !row.legacyTarget.isEmpty()
            && !locator.contains(QStringLiteral("fragment"))) {
            locator.insert(QStringLiteral("fragment"), row.legacyTarget);
        }
        row.locatorJson = locator.isEmpty()
            ? QString()
            : QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));

        if (row.targetFile.isEmpty() && row.targetUri.isEmpty() && !row.resourceLocation.isEmpty()) {
            if (row.resourceKind == QLatin1String("url") || looksLikeUri(row.resourceLocation)) {
                row.targetUri = row.resourceLocation;
            } else {
                row.targetFile = row.resourceLocation;
            }
        }
        if (row.targetApp.isEmpty()) {
            if (row.resourceKind == QLatin1String("pdf")) {
                row.targetApp = QStringLiteral("SumatraPDF");
            } else if (row.resourceKind == QLatin1String("url")) {
                row.targetApp = QStringLiteral("browser");
            }
        }

        QSqlQuery updateQuery(database_);
        updateQuery.prepare(
            QStringLiteral("UPDATE anchors SET id = ?, name = ?, target_app = ?, target_file = ?, "
                           "target_uri = ?, locator_type = ?, locator_json = ? "
                           "WHERE resource_id = ? AND anchor_order = ?"));
        updateQuery.addBindValue(row.id);
        updateQuery.addBindValue(row.name);
        updateQuery.addBindValue(row.targetApp);
        updateQuery.addBindValue(row.targetFile);
        updateQuery.addBindValue(row.targetUri);
        updateQuery.addBindValue(row.locatorType);
        updateQuery.addBindValue(row.locatorJson);
        updateQuery.addBindValue(row.resourceId);
        updateQuery.addBindValue(row.order);
        if (!updateQuery.exec()) {
            setLastError(updateQuery.lastError().text());
            return false;
        }

        if (generatedId) {
            QStringList legacyTypes = {
                row.legacyType,
                storageTypeFromLegacyAnchorType(row.legacyType),
            };
            legacyTypes.removeAll(QString());
            legacyTypes.removeDuplicates();

            UsageKeyMigration migration;
            migration.resourceId = row.resourceId;
            migration.newKey = QStringLiteral("id|%1").arg(row.id);
            for (const QString &type : legacyTypes) {
                migration.oldKeys.append(
                    legacyAnchorUsageFieldKey(type,
                                              row.legacyTarget,
                                              row.line,
                                              row.page,
                                              row.x,
                                              row.y,
                                              row.width,
                                              row.height));
            }
            usageKeyMigrations.append(migration);
        }
    }

    if (!execute(QStringLiteral("DROP TABLE IF EXISTS anchor_fts"))
        || !execute(QStringLiteral("ALTER TABLE anchors RENAME TO anchors_v10"))
        || !execute(QStringLiteral(
            "CREATE TABLE anchors ("
            "resource_id TEXT NOT NULL,"
            "anchor_order INTEGER NOT NULL,"
            "id TEXT,"
            "name TEXT,"
            "target_app TEXT,"
            "target_file TEXT,"
            "target_uri TEXT,"
            "locator_type TEXT,"
            "locator_json TEXT,"
            "aliases TEXT,"
            "tags TEXT,"
            "pinned INTEGER NOT NULL DEFAULT 0,"
            "deleted INTEGER NOT NULL DEFAULT 0,"
            "created_at TEXT,"
            "updated_at TEXT,"
            "used_at TEXT,"
            "PRIMARY KEY (resource_id, anchor_order),"
            "FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE"
            ")"))
        || !execute(QStringLiteral(
            "INSERT INTO anchors(resource_id, anchor_order, id, name, target_app, target_file, "
            "target_uri, locator_type, locator_json, aliases, tags, pinned, deleted, created_at, "
            "updated_at, used_at) "
            "SELECT resource_id, anchor_order, id, name, target_app, target_file, target_uri, "
            "locator_type, locator_json, aliases, tags, pinned, deleted, created_at, updated_at, "
            "used_at FROM anchors_v10"))
        || !execute(QStringLiteral("DROP TABLE anchors_v10"))
        || !execute(QStringLiteral("ALTER TABLE anchor_usage RENAME TO anchor_usage_v10"))
        || !execute(QStringLiteral(
            "CREATE TABLE anchor_usage ("
            "resource_id TEXT NOT NULL,"
            "anchor_key TEXT NOT NULL,"
            "open_count INTEGER NOT NULL DEFAULT 0,"
            "last_opened_at TEXT,"
            "PRIMARY KEY (resource_id, anchor_key),"
            "FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE"
            ")"))
        || !execute(QStringLiteral(
            "INSERT INTO anchor_usage(resource_id, anchor_key, open_count, last_opened_at) "
            "SELECT resource_id, anchor_key, open_count, last_opened_at FROM anchor_usage_v10"))
        || !execute(QStringLiteral("DROP TABLE anchor_usage_v10"))) {
        return false;
    }

    for (const UsageKeyMigration &migration : usageKeyMigrations) {
        for (const QString &oldKey : migration.oldKeys) {
            if (oldKey == migration.newKey) {
                continue;
            }

            QSqlQuery usageQuery(database_);
            usageQuery.prepare(
                QStringLiteral("SELECT open_count, last_opened_at FROM anchor_usage "
                               "WHERE resource_id = ? AND anchor_key = ?"));
            usageQuery.addBindValue(migration.resourceId);
            usageQuery.addBindValue(oldKey);
            if (!usageQuery.exec()) {
                setLastError(usageQuery.lastError().text());
                return false;
            }
            if (!usageQuery.next()) {
                continue;
            }

            const int openCount = usageQuery.value(0).toInt();
            const QString lastOpenedAt = usageQuery.value(1).toString();
            QSqlQuery mergeQuery(database_);
            mergeQuery.prepare(
                QStringLiteral("INSERT INTO anchor_usage(resource_id, anchor_key, open_count, last_opened_at) "
                               "VALUES (?, ?, ?, ?) "
                               "ON CONFLICT(resource_id, anchor_key) DO UPDATE SET "
                               "open_count = anchor_usage.open_count + excluded.open_count,"
                               "last_opened_at = CASE "
                               "WHEN anchor_usage.last_opened_at IS NULL THEN excluded.last_opened_at "
                               "WHEN excluded.last_opened_at IS NULL THEN anchor_usage.last_opened_at "
                               "WHEN excluded.last_opened_at > anchor_usage.last_opened_at "
                               "THEN excluded.last_opened_at ELSE anchor_usage.last_opened_at END"));
            mergeQuery.addBindValue(migration.resourceId);
            mergeQuery.addBindValue(migration.newKey);
            mergeQuery.addBindValue(openCount);
            mergeQuery.addBindValue(lastOpenedAt);
            if (!mergeQuery.exec()) {
                setLastError(mergeQuery.lastError().text());
                return false;
            }

            QSqlQuery deleteUsage(database_);
            deleteUsage.prepare(
                QStringLiteral("DELETE FROM anchor_usage WHERE resource_id = ? AND anchor_key = ?"));
            deleteUsage.addBindValue(migration.resourceId);
            deleteUsage.addBindValue(oldKey);
            if (!deleteUsage.exec()) {
                setLastError(deleteUsage.lastError().text());
                return false;
            }
        }
    }

    return execute(QStringLiteral(
               "CREATE VIRTUAL TABLE anchor_fts "
               "USING fts5(resource_id UNINDEXED, anchor_order UNINDEXED, locator_type, text)"))
        && execute(QStringLiteral(
               "INSERT INTO anchor_fts(resource_id, anchor_order, locator_type, text) "
               "SELECT a.resource_id, a.anchor_order, COALESCE(a.locator_type, ''), "
               "COALESCE(a.name, '') || char(10) || COALESCE(a.aliases, '') || char(10) || "
               "COALESCE(a.tags, '') || char(10) || COALESCE(a.target_app, '') || char(10) || "
               "COALESCE(a.target_file, '') || char(10) || COALESCE(a.target_uri, '') || char(10) || "
               "COALESCE(a.locator_json, '') "
               "FROM anchors a JOIN resources r ON r.id = a.resource_id "
               "WHERE a.deleted = 0 AND r.deleted = 0"));
}

bool SqliteLibraryRepository::migrateStableAnchorIdentitySchema()
{
    QSqlQuery tableInfo(database_);
    if (!tableInfo.exec(QStringLiteral("PRAGMA table_info(anchors)"))) {
        setLastError(tableInfo.lastError().text());
        return false;
    }
    QHash<QString, int> primaryKeyOrder;
    QHash<QString, bool> notNull;
    while (tableInfo.next()) {
        const QString name = tableInfo.value(1).toString();
        notNull.insert(name, tableInfo.value(3).toInt() != 0);
        primaryKeyOrder.insert(name, tableInfo.value(5).toInt());
    }

    const auto rebuildAnchorFts = [this]() {
        return execute(QStringLiteral("DROP TABLE IF EXISTS anchor_fts"))
            && execute(QStringLiteral(
                "CREATE VIRTUAL TABLE anchor_fts "
                "USING fts5(resource_id UNINDEXED, anchor_id UNINDEXED, locator_type, text)"))
            && execute(QStringLiteral(
                "INSERT INTO anchor_fts(resource_id, anchor_id, locator_type, text) "
                "SELECT a.resource_id, a.id, COALESCE(a.locator_type, ''), "
                "COALESCE(a.name, '') || char(10) || COALESCE(a.aliases, '') || char(10) || "
                "COALESCE(a.tags, '') || char(10) || COALESCE(a.target_app, '') || char(10) || "
                "COALESCE(a.target_file, '') || char(10) || COALESCE(a.target_uri, '') || char(10) || "
                "COALESCE(a.locator_json, '') "
                "FROM anchors a"));
    };

    const bool stablePrimaryKey = primaryKeyOrder.value(QStringLiteral("resource_id")) == 1
        && primaryKeyOrder.value(QStringLiteral("id")) == 2
        && notNull.value(QStringLiteral("id"));
    if (stablePrimaryKey) {
        QSqlQuery ftsInfo(database_);
        if (!ftsInfo.exec(QStringLiteral("PRAGMA table_info(anchor_fts)"))) {
            setLastError(ftsInfo.lastError().text());
            return false;
        }
        QStringList ftsColumns;
        while (ftsInfo.next()) ftsColumns.append(ftsInfo.value(1).toString());
        return ftsColumns.contains(QStringLiteral("anchor_id")) || rebuildAnchorFts();
    }

    struct AnchorRow {
        QString resourceId;
        int order = 0;
        Anchor anchor;
        QString aliases;
        QString tags;
        QString originalUsageKey;
        bool generatedId = false;
    };

    QList<AnchorRow> rows;
    QSqlQuery select(database_);
    if (!select.exec(QStringLiteral(
            "SELECT resource_id, anchor_order, id, name, target_app, target_file, target_uri, "
            "locator_type, locator_json, aliases, tags, pinned, deleted, created_at, updated_at, used_at "
            "FROM anchors ORDER BY resource_id, anchor_order"))) {
        setLastError(select.lastError().text());
        return false;
    }
    while (select.next()) {
        AnchorRow row;
        row.resourceId = select.value(0).toString();
        row.order = select.value(1).toInt();
        row.anchor.id = select.value(2).toString().trimmed();
        row.anchor.name = select.value(3).toString();
        row.anchor.targetApp = select.value(4).toString();
        row.anchor.targetFile = select.value(5).toString();
        row.anchor.targetUri = select.value(6).toString();
        row.anchor.locatorType = select.value(7).toString();
        row.anchor.locatorJson = select.value(8).toString();
        row.aliases = select.value(9).toString();
        row.tags = select.value(10).toString();
        row.anchor.pinned = select.value(11).toInt() != 0;
        row.anchor.deleted = select.value(12).toInt() != 0;
        row.anchor.createdAt = QDateTime::fromString(select.value(13).toString(), Qt::ISODate);
        row.anchor.updatedAt = QDateTime::fromString(select.value(14).toString(), Qt::ISODate);
        row.anchor.usedAt = QDateTime::fromString(select.value(15).toString(), Qt::ISODate);
        row.originalUsageKey = anchorIdentityKey(row.anchor);
        rows.append(row);
    }

    QHash<QString, QSet<QString>> idsByResource;
    for (AnchorRow &row : rows) {
        QSet<QString> &ids = idsByResource[row.resourceId];
        QString candidate = row.anchor.id;
        if (candidate.isEmpty() || ids.contains(candidate)) {
            row.generatedId = candidate.isEmpty();
            const QString base = QStringLiteral("%1#anchor-%2")
                                     .arg(row.resourceId, QString::number(row.order));
            candidate = base;
            int suffix = 2;
            while (ids.contains(candidate)) {
                candidate = QStringLiteral("%1-%2").arg(base, QString::number(suffix++));
            }
        }
        row.anchor.id = candidate;
        ids.insert(candidate);
    }

    if (!execute(QStringLiteral("DROP TABLE IF EXISTS anchor_fts"))
        || !execute(QStringLiteral("ALTER TABLE anchors RENAME TO anchors_v11"))
        || !execute(QStringLiteral(
            "CREATE TABLE anchors ("
            "resource_id TEXT NOT NULL,"
            "anchor_order INTEGER NOT NULL,"
            "id TEXT NOT NULL,"
            "name TEXT,"
            "target_app TEXT,"
            "target_file TEXT,"
            "target_uri TEXT,"
            "locator_type TEXT,"
            "locator_json TEXT,"
            "aliases TEXT,"
            "tags TEXT,"
            "pinned INTEGER NOT NULL DEFAULT 0,"
            "deleted INTEGER NOT NULL DEFAULT 0,"
            "created_at TEXT,"
            "updated_at TEXT,"
            "used_at TEXT,"
            "PRIMARY KEY (resource_id, id),"
            "UNIQUE (resource_id, anchor_order),"
            "FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE"
            ")"))) {
        return false;
    }

    QSqlQuery insert(database_);
    insert.prepare(QStringLiteral(
        "INSERT INTO anchors(resource_id, anchor_order, id, name, target_app, target_file, "
        "target_uri, locator_type, locator_json, aliases, tags, pinned, deleted, created_at, "
        "updated_at, used_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const AnchorRow &row : rows) {
        insert.bindValue(0, row.resourceId);
        insert.bindValue(1, row.order);
        insert.bindValue(2, row.anchor.id);
        insert.bindValue(3, row.anchor.name);
        insert.bindValue(4, row.anchor.targetApp);
        insert.bindValue(5, row.anchor.targetFile);
        insert.bindValue(6, row.anchor.targetUri);
        insert.bindValue(7, row.anchor.locatorType);
        insert.bindValue(8, row.anchor.locatorJson);
        insert.bindValue(9, row.aliases);
        insert.bindValue(10, row.tags);
        insert.bindValue(11, row.anchor.pinned ? 1 : 0);
        insert.bindValue(12, row.anchor.deleted ? 1 : 0);
        insert.bindValue(13, row.anchor.createdAt.isValid()
                                 ? row.anchor.createdAt.toUTC().toString(Qt::ISODate)
                                 : QVariant());
        insert.bindValue(14, row.anchor.updatedAt.isValid()
                                 ? row.anchor.updatedAt.toUTC().toString(Qt::ISODate)
                                 : QVariant());
        insert.bindValue(15, row.anchor.usedAt.isValid()
                                 ? row.anchor.usedAt.toUTC().toString(Qt::ISODate)
                                 : QVariant());
        if (!insert.exec()) {
            setLastError(insert.lastError().text());
            return false;
        }
    }
    if (!execute(QStringLiteral("DROP TABLE anchors_v11"))) return false;

    for (const AnchorRow &row : rows) {
        if (!row.generatedId) continue;
        const QString newUsageKey = anchorIdentityKey(row.anchor);
        if (newUsageKey == row.originalUsageKey) continue;
        QSqlQuery moveUsage(database_);
        moveUsage.prepare(QStringLiteral(
            "INSERT INTO anchor_usage(resource_id, anchor_key, open_count, last_opened_at) "
            "SELECT resource_id, ?, open_count, last_opened_at FROM anchor_usage "
            "WHERE resource_id = ? AND anchor_key = ? "
            "ON CONFLICT(resource_id, anchor_key) DO UPDATE SET "
            "open_count = anchor_usage.open_count + excluded.open_count,"
            "last_opened_at = CASE "
            "WHEN anchor_usage.last_opened_at IS NULL THEN excluded.last_opened_at "
            "WHEN excluded.last_opened_at IS NULL THEN anchor_usage.last_opened_at "
            "WHEN excluded.last_opened_at > anchor_usage.last_opened_at "
            "THEN excluded.last_opened_at ELSE anchor_usage.last_opened_at END"));
        moveUsage.addBindValue(newUsageKey);
        moveUsage.addBindValue(row.resourceId);
        moveUsage.addBindValue(row.originalUsageKey);
        if (!moveUsage.exec()) {
            setLastError(moveUsage.lastError().text());
            return false;
        }
        QSqlQuery removeOldUsage(database_);
        removeOldUsage.prepare(QStringLiteral(
            "DELETE FROM anchor_usage WHERE resource_id = ? AND anchor_key = ?"));
        removeOldUsage.addBindValue(row.resourceId);
        removeOldUsage.addBindValue(row.originalUsageKey);
        if (!removeOldUsage.exec()) {
            setLastError(removeOldUsage.lastError().text());
            return false;
        }
    }

    return rebuildAnchorFts();
}

bool SqliteLibraryRepository::migrateLifecycleSearchSchema()
{
    if (!execute(QStringLiteral("DELETE FROM anchor_fts"))
        || !execute(QStringLiteral(
            "INSERT INTO anchor_fts(resource_id, anchor_id, locator_type, text) "
            "SELECT a.resource_id, a.id, COALESCE(a.locator_type, ''), "
            "COALESCE(a.name, '') || char(10) || COALESCE(a.aliases, '') || char(10) || "
            "COALESCE(a.tags, '') || char(10) || COALESCE(a.target_app, '') || char(10) || "
            "COALESCE(a.target_file, '') || char(10) || COALESCE(a.target_uri, '') || char(10) || "
            "COALESCE(a.locator_json, '') FROM anchors a"))) {
        return false;
    }

    return execute(QStringLiteral(
        "INSERT INTO resource_fts(resource_id, title, aliases, tags, location, content) "
        "SELECT r.id, COALESCE(r.title, ''), "
        "COALESCE((SELECT group_concat(ra.alias, ' ') FROM resource_aliases ra "
        "WHERE ra.resource_id = r.id), ''), "
        "COALESCE((SELECT group_concat(rt.tag, ' ') FROM resource_tags rt "
        "WHERE rt.resource_id = r.id), ''), "
        "COALESCE(r.location, ''), '' FROM resources r "
        "WHERE NOT EXISTS (SELECT 1 FROM resource_fts rf WHERE rf.resource_id = r.id)"));
}

int SqliteLibraryRepository::schemaVersion() const
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT COALESCE(MAX(version), 0) FROM schema_migrations"))
        || !query.next()) {
        setLastError(query.lastError().text());
        return -1;
    }
    return query.value(0).toInt();
}

bool SqliteLibraryRepository::recordMigration(int version, const QString &name)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO schema_migrations(version, name, applied_at) "
                                 "VALUES (?, ?, ?)"));
    query.addBindValue(version);
    query.addBindValue(name);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
    return true;
}

bool SqliteLibraryRepository::beginTransaction()
{
    if (transactionDepth_ > 0) {
        ++transactionDepth_;
        return true;
    }
    if (!database_.transaction()) {
        setLastError(database_.lastError().text());
        return false;
    }
    transactionDepth_ = 1;
    return true;
}

bool SqliteLibraryRepository::commitTransaction()
{
    if (transactionDepth_ > 1) {
        --transactionDepth_;
        return true;
    }
    if (transactionDepth_ <= 0) {
        setLastError(QStringLiteral("No SQLite transaction is active"));
        return false;
    }
    if (!database_.commit()) {
        setLastError(database_.lastError().text());
        database_.rollback();
        transactionDepth_ = 0;
        return false;
    }
    transactionDepth_ = 0;
    return true;
}

void SqliteLibraryRepository::rollbackTransaction()
{
    if (transactionDepth_ > 0) {
        database_.rollback();
        transactionDepth_ = 0;
    }
}

bool SqliteLibraryRepository::deleteResourcePermanently(const QString &resourceIdValue)
{
    const QString resourceId = resourceIdValue.trimmed();
    if (resourceId.isEmpty()) {
        setLastError(QStringLiteral("Resource id is required"));
        return false;
    }
    for (const QString &table : {QStringLiteral("anchor_fts"), QStringLiteral("resource_fts")}) {
        QSqlQuery deleteIndex(database_);
        deleteIndex.prepare(QStringLiteral("DELETE FROM %1 WHERE resource_id = ?").arg(table));
        deleteIndex.addBindValue(resourceId);
        if (!deleteIndex.exec()) {
            setLastError(deleteIndex.lastError().text());
            return false;
        }
    }
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM resources WHERE id = ?"));
    query.addBindValue(resourceId);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() <= 0) {
        setLastError(QStringLiteral("Resource not found"));
        return false;
    }
    notifyChange(LibraryChangeKind::Content, {resourceId});
    return true;
}

bool SqliteLibraryRepository::clearResourceTables()
{
    return execute(QStringLiteral("DELETE FROM anchor_fts;"))
        && execute(QStringLiteral("DELETE FROM resource_fts;"))
        && execute(QStringLiteral("DELETE FROM anchor_usage;"))
        && execute(QStringLiteral("DELETE FROM resource_usage;"))
        && execute(QStringLiteral("DELETE FROM resources;"));
}

void SqliteLibraryRepository::notifyChange(LibraryChangeKind kind,
                                           const QStringList &resourceIds)
{
    if (deferredChangeDepth_ > 0) {
        if (deferredResourceIds_.isEmpty()) {
            deferredChangeKind_ = kind;
        } else if (kind == LibraryChangeKind::Reset
                   || (kind == LibraryChangeKind::Content
                       && deferredChangeKind_ == LibraryChangeKind::Usage)) {
            deferredChangeKind_ = kind;
        }
        for (const QString &resourceId : resourceIds) {
            if (!resourceId.trimmed().isEmpty()
                && !deferredResourceIds_.contains(resourceId, Qt::CaseInsensitive)) {
                deferredResourceIds_.append(resourceId);
            }
        }
        return;
    }

    ++revision_;
    if (kind != LibraryChangeKind::Usage) {
        ++contentRevision_;
    }
    const LibraryChange change{kind, resourceIds, revision_};
    const QList<LibraryChangeListener> listeners = listeners_.values();
    for (const LibraryChangeListener &listener : listeners) {
        if (listener) {
            listener(change);
        }
    }
}

QList<Resource> SqliteLibraryRepository::allResourcesForIdentity() const
{
    QList<Resource> resources;
    lastError_.clear();
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT id FROM resources ORDER BY id"))) {
        setLastError(query.lastError().text());
        return {};
    }
    QStringList ids;
    while (query.next()) {
        ids.append(query.value(0).toString());
    }
    for (const QString &id : ids) {
        resources.append(hydrateResource(id));
        if (!lastError_.isEmpty()) {
            return {};
        }
    }
    return resources;
}

Resource SqliteLibraryRepository::hydrateResource(const QString &id) const
{
    Resource resource;

    QSqlQuery query(database_);
    query.prepare(QStringLiteral(
        "SELECT id, kind, title, location, explicitly_retained, deleted, updated_at "
        "FROM resources WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        setLastError(query.lastError().text());
        return resource;
    }

    resource.id = query.value(0).toString();
    resource.kind = resourceKindFromString(query.value(1).toString());
    resource.title = query.value(2).toString();
    resource.location = query.value(3).toString();
    resource.explicitlyRetained = query.value(4).toInt() != 0;
    resource.deleted = query.value(5).toInt() != 0;
    resource.updatedAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODate);
    resource.tags = readStrings(QStringLiteral("resource_tags"), QStringLiteral("tag"), id);
    resource.aliases = readStrings(QStringLiteral("resource_aliases"), QStringLiteral("alias"), id);
    resource.anchors = readAnchors(id);

    return normalizedResource(resource);
}

LibraryRoot SqliteLibraryRepository::hydrateLibraryRoot(QSqlQuery &query) const
{
    LibraryRoot root;
    root.id = query.value(0).toString();
    root.path = normalizedLibraryRootPath(query.value(1).toString());
    root.displayName = query.value(2).toString();
    root.enabled = query.value(3).toInt() != 0;
    root.syncRoot = query.value(4).toInt() != 0;
    root.ignoredDirectoryNames =
        query.value(5).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    root.updatedAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODate);
    return root;
}

ResourceUsage SqliteLibraryRepository::hydrateResourceUsage(QSqlQuery &query) const
{
    ResourceUsage usage;
    usage.resourceId = query.value(0).toString();
    usage.openCount = query.value(1).toInt();
    usage.lastOpenedAt = QDateTime::fromString(query.value(2).toString(), Qt::ISODate);
    usage.pinned = query.value(3).toInt() != 0;
    return usage;
}

AnchorUsage SqliteLibraryRepository::hydrateAnchorUsage(QSqlQuery &query) const
{
    AnchorUsage usage;
    usage.resourceId = query.value(0).toString();
    usage.openCount = query.value(1).toInt();
    usage.lastOpenedAt = QDateTime::fromString(query.value(2).toString(), Qt::ISODate);
    return usage;
}

void SqliteLibraryRepository::applyRankingSignals(SearchResult &result, const SearchQuery &query) const
{
    result.score += contextScoreAdjustment(result.resource, query);
    const std::optional<ResourceUsage> usage = resourceUsage(result.resource.id);
    if (usage.has_value()) {
        result.score += usageScoreAdjustment(usage.value());
    }

    if (result.matchedAnchor.has_value()) {
        result.score += anchorScoreAdjustment(result.matchedAnchor.value());
        const std::optional<AnchorUsage> anchorUsageValue = anchorUsage(result.resource.id, result.matchedAnchor.value());
        if (anchorUsageValue.has_value()) {
            result.score += anchorUsageScoreAdjustment(anchorUsageValue.value());
        }
    }
}

QStringList SqliteLibraryRepository::readStrings(const QString &table, const QString &column, const QString &resourceId) const
{
    QStringList values;

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT %1 FROM %2 WHERE resource_id = ? ORDER BY lower(%1)").arg(column, table));
    query.addBindValue(resourceId);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return values;
    }

    while (query.next()) {
        values.append(query.value(0).toString());
    }

    return values;
}

QList<Anchor> SqliteLibraryRepository::readAnchors(const QString &resourceId) const
{
    QList<Anchor> anchors;

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT id, name, target_app, target_file, target_uri, locator_type, "
                                 "locator_json, aliases, tags, pinned, deleted, created_at, updated_at, used_at "
                                 "FROM anchors WHERE resource_id = ? ORDER BY anchor_order"));
    query.addBindValue(resourceId);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return anchors;
    }

    while (query.next()) {
        Anchor anchor;
        anchor.id = query.value(0).toString();
        anchor.name = query.value(1).toString();
        anchor.targetApp = query.value(2).toString();
        anchor.targetFile = query.value(3).toString();
        anchor.targetUri = query.value(4).toString();
        anchor.locatorType = query.value(5).toString();
        anchor.locatorJson = query.value(6).toString();
        anchor.aliases = query.value(7).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        anchor.tags = query.value(8).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        anchor.pinned = query.value(9).toInt() != 0;
        anchor.deleted = query.value(10).toInt() != 0;
        anchor.createdAt = QDateTime::fromString(query.value(11).toString(), Qt::ISODate);
        anchor.updatedAt = QDateTime::fromString(query.value(12).toString(), Qt::ISODate);
        anchor.usedAt = QDateTime::fromString(query.value(13).toString(), Qt::ISODate);
        anchors.append(anchor);
    }

    return anchors;
}

void SqliteLibraryRepository::setLastError(const QString &message) const
{
    lastError_ = message;
}

} // namespace Pinloom
