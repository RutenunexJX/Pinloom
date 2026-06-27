#include "pinloom/core/SqliteLibraryRepository.h"

#include "pinloom/core/Schema.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <algorithm>

namespace Pinloom {

namespace {

QString resourceKindToString(ResourceKind kind)
{
    switch (kind) {
    case ResourceKind::File:
        return QStringLiteral("file");
    case ResourceKind::Folder:
        return QStringLiteral("folder");
    case ResourceKind::Pdf:
        return QStringLiteral("pdf");
    case ResourceKind::Markdown:
        return QStringLiteral("markdown");
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
    if (kind == QLatin1String("file")) {
        return ResourceKind::File;
    }
    if (kind == QLatin1String("folder")) {
        return ResourceKind::Folder;
    }
    if (kind == QLatin1String("pdf")) {
        return ResourceKind::Pdf;
    }
    if (kind == QLatin1String("markdown")) {
        return ResourceKind::Markdown;
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

QString anchorTypeToString(AnchorType type)
{
    switch (type) {
    case AnchorType::FileLine:
        return QStringLiteral("file_line");
    case AnchorType::MarkdownHeading:
    case AnchorType::TextHeading:
        return QStringLiteral("text_heading");
    case AnchorType::MarkdownBlock:
    case AnchorType::TextBlock:
        return QStringLiteral("text_block");
    case AnchorType::Marker:
        return QStringLiteral("marker");
    case AnchorType::PdfPage:
        return QStringLiteral("pdf_page");
    case AnchorType::PdfRegion:
        return QStringLiteral("pdf_region");
    case AnchorType::UrlFragment:
        return QStringLiteral("url_fragment");
    case AnchorType::Manual:
        return QStringLiteral("manual");
    case AnchorType::None:
        break;
    }
    return QStringLiteral("none");
}

AnchorType anchorTypeFromString(const QString &type)
{
    if (type == QLatin1String("file_line")) {
        return AnchorType::FileLine;
    }
    if (type == QLatin1String("text_heading") || type == QLatin1String("markdown_heading")) {
        return AnchorType::TextHeading;
    }
    if (type == QLatin1String("text_block") || type == QLatin1String("markdown_block")) {
        return AnchorType::TextBlock;
    }
    // Accept legacy symbol/code rows while new writes use neutral storage names.
    if (type == QLatin1String("marker")
        || type == QLatin1String("symbol_like")
        || type == QLatin1String("code_symbol")) {
        return AnchorType::Marker;
    }
    if (type == QLatin1String("pdf_page")) {
        return AnchorType::PdfPage;
    }
    if (type == QLatin1String("pdf_region")) {
        return AnchorType::PdfRegion;
    }
    if (type == QLatin1String("url_fragment")) {
        return AnchorType::UrlFragment;
    }
    if (type == QLatin1String("manual")) {
        return AnchorType::Manual;
    }
    return AnchorType::None;
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

double exactMatchScoreAdjustment(const QString &value, const QString &queryText)
{
    return exactMatchScoreAdjustment(QStringList{value}, queryText);
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

SearchResult anchorSearchResult(const Resource &resource, const Anchor &anchor, const QString &queryText)
{
    return SearchResult{resource,
                        exactMatchScoreAdjustment(anchor.target, queryText),
                        QStringLiteral("anchor"),
                        anchor};
}

bool shouldIndexAnchorTarget(const Anchor &anchor)
{
    if (anchor.target.trimmed().isEmpty()) {
        return false;
    }

    switch (anchor.type) {
    case AnchorType::FileLine:
    case AnchorType::MarkdownHeading:
    case AnchorType::MarkdownBlock:
    case AnchorType::TextHeading:
    case AnchorType::TextBlock:
    case AnchorType::Marker:
    case AnchorType::PdfPage:
    case AnchorType::PdfRegion:
    case AnchorType::UrlFragment:
    case AnchorType::Manual:
        return true;
    case AnchorType::None:
        break;
    }
    return false;
}

QString anchorUsageKey(const Anchor &anchor)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
        .arg(anchorTypeToString(anchor.type),
             anchor.target,
             QString::number(anchor.line),
             QString::number(anchor.page),
             QString::number(anchor.region.x(), 'f', 2),
             QString::number(anchor.region.y(), 'f', 2),
             QString::number(anchor.region.width(), 'f', 2),
             QString::number(anchor.region.height(), 'f', 2));
}

QString legacyAnchorTypeToString(AnchorType type)
{
    switch (type) {
    case AnchorType::MarkdownHeading:
    case AnchorType::TextHeading:
        return QStringLiteral("markdown_heading");
    case AnchorType::MarkdownBlock:
    case AnchorType::TextBlock:
        return QStringLiteral("markdown_block");
    default:
        return anchorTypeToString(type);
    }
}

QString legacyAnchorUsageKey(Anchor anchor)
{
    const QString normalizedType = legacyAnchorTypeToString(anchor.type);
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8")
        .arg(normalizedType,
             anchor.target,
             QString::number(anchor.line),
             QString::number(anchor.page),
             QString::number(anchor.region.x(), 'f', 2),
             QString::number(anchor.region.y(), 'f', 2),
             QString::number(anchor.region.width(), 'f', 2),
             QString::number(anchor.region.height(), 'f', 2));
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
    return requiredKinds.isEmpty() || requiredKinds.contains(kind);
}

bool matchesContextRelationLabel(const ResourceRelation &relation, const QStringList &labels)
{
    const bool hasFilter = std::any_of(labels.cbegin(), labels.cend(), [](const QString &label) {
        return !label.trimmed().isEmpty();
    });
    if (!hasFilter) {
        return true;
    }
    for (const QString &label : labels) {
        const QString trimmedLabel = label.trimmed();
        if (!trimmedLabel.isEmpty()
            && relation.label.compare(trimmedLabel, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

std::optional<ResourceRelation> matchedContextRelation(const Resource &resource,
                                                       const SearchQuery &query,
                                                       const QList<ResourceRelation> &relations)
{
    for (const QString &contextResourceId : query.contextResourceIds) {
        const QString contextId = contextResourceId.trimmed();
        if (contextId.isEmpty() || contextId == resource.id) {
            continue;
        }

        for (const ResourceRelation &relation : relations) {
            if (!matchesContextRelationLabel(relation, query.contextRelationLabels)) {
                continue;
            }
            if ((relation.sourceResourceId == resource.id && relation.targetResourceId == contextId)
                || (relation.targetResourceId == resource.id && relation.sourceResourceId == contextId)) {
                return relation;
            }
        }
    }
    return std::nullopt;
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

bool resourceMatchesLibraryRoot(const Resource &resource, const LibraryRoot &root)
{
    if (!root.enabled || !root.pinned) {
        return false;
    }

    const QString normalizedResourceLocation = normalizedLocation(resource.location);
    const QString normalizedRootPath = normalizedLocation(root.path);
    return !normalizedResourceLocation.isEmpty()
        && !normalizedRootPath.isEmpty()
        && (normalizedResourceLocation == normalizedRootPath
            || normalizedResourceLocation.startsWith(normalizedRootPath + QLatin1Char('/')));
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

bool SqliteLibraryRepository::open(const QString &path)
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

    if (!database_.open()) {
        setLastError(database_.lastError().text());
        return false;
    }

    return execute(QStringLiteral("PRAGMA foreign_keys = ON;"));
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

    for (const QString &statement : Schema::sqliteFts5Draft()) {
        if (!execute(statement)) {
            rollbackTransaction();
            return false;
        }
    }

    if (!ensureLibraryRootPinnedColumn()) {
        rollbackTransaction();
        return false;
    }

    if (!recordMigration(1, QStringLiteral("initial_sqlite_fts5_schema"))
        || !recordMigration(2, QStringLiteral("library_roots"))
        || !recordMigration(3, QStringLiteral("anchor_fts"))
        || !recordMigration(4, QStringLiteral("resource_relations"))
        || !recordMigration(5, QStringLiteral("resource_usage"))
        || !recordMigration(6, QStringLiteral("anchor_usage"))
        || !recordMigration(7, QStringLiteral("pinned_library_roots"))) {
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

bool SqliteLibraryRepository::upsertResource(const Resource &resource)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }
    if (resource.id.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Resource id is required"));
        return false;
    }

    if (!beginTransaction()) {
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO resources(id, kind, title, location, updated_at) "
                                 "VALUES (?, ?, ?, ?, ?) "
                                 "ON CONFLICT(id) DO UPDATE SET "
                                 "kind = excluded.kind,"
                                 "title = excluded.title,"
                                 "location = excluded.location,"
                                 "updated_at = excluded.updated_at"));
    query.addBindValue(resource.id);
    query.addBindValue(resourceKindToString(resource.kind));
    query.addBindValue(resource.title);
    query.addBindValue(resource.location);
    query.addBindValue(resource.updatedAt.isValid()
                           ? resource.updatedAt.toUTC().toString(Qt::ISODate)
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
        deleteQuery.addBindValue(resource.id);
        if (!deleteQuery.exec()) {
            setLastError(deleteQuery.lastError().text());
            rollbackTransaction();
            return false;
        }
    }

    for (const QString &tag : resource.tags) {
        QSqlQuery tagQuery(database_);
        tagQuery.prepare(QStringLiteral("INSERT OR IGNORE INTO resource_tags(resource_id, tag) VALUES (?, ?)"));
        tagQuery.addBindValue(resource.id);
        tagQuery.addBindValue(tag);
        if (!tagQuery.exec()) {
            setLastError(tagQuery.lastError().text());
            rollbackTransaction();
            return false;
        }
    }

    for (const QString &alias : resource.aliases) {
        QSqlQuery aliasQuery(database_);
        aliasQuery.prepare(QStringLiteral("INSERT OR IGNORE INTO resource_aliases(resource_id, alias) VALUES (?, ?)"));
        aliasQuery.addBindValue(resource.id);
        aliasQuery.addBindValue(alias);
        if (!aliasQuery.exec()) {
            setLastError(aliasQuery.lastError().text());
            rollbackTransaction();
            return false;
        }
    }

    for (int i = 0; i < resource.anchors.size(); ++i) {
        const Anchor &anchor = resource.anchors.at(i);
        QSqlQuery anchorQuery(database_);
        anchorQuery.prepare(QStringLiteral("INSERT INTO anchors(resource_id, anchor_order, type, target, line, page, "
                                           "x, y, width, height) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        anchorQuery.addBindValue(resource.id);
        anchorQuery.addBindValue(i);
        anchorQuery.addBindValue(anchorTypeToString(anchor.type));
        anchorQuery.addBindValue(anchor.target);
        anchorQuery.addBindValue(anchor.line >= 0 ? QVariant(anchor.line) : QVariant());
        anchorQuery.addBindValue(anchor.page >= 0 ? QVariant(anchor.page) : QVariant());
        anchorQuery.addBindValue(anchor.region.x());
        anchorQuery.addBindValue(anchor.region.y());
        anchorQuery.addBindValue(anchor.region.width());
        anchorQuery.addBindValue(anchor.region.height());
        if (!anchorQuery.exec()) {
            setLastError(anchorQuery.lastError().text());
            rollbackTransaction();
            return false;
        }

        if (shouldIndexAnchorTarget(anchor)) {
            QSqlQuery anchorFtsQuery(database_);
            anchorFtsQuery.prepare(QStringLiteral("INSERT INTO anchor_fts(resource_id, anchor_order, type, target) "
                                                  "VALUES (?, ?, ?, ?)"));
            anchorFtsQuery.addBindValue(resource.id);
            anchorFtsQuery.addBindValue(i);
            anchorFtsQuery.addBindValue(anchorTypeToString(anchor.type));
            anchorFtsQuery.addBindValue(anchor.target);
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
    ftsQuery.addBindValue(resource.id);
    ftsQuery.addBindValue(resource.title);
    ftsQuery.addBindValue(resource.aliases.join(QLatin1Char(' ')));
    ftsQuery.addBindValue(resource.tags.join(QLatin1Char(' ')));
    ftsQuery.addBindValue(resource.location);
    ftsQuery.addBindValue(resource.content);
    if (!ftsQuery.exec()) {
        setLastError(ftsQuery.lastError().text());
        rollbackTransaction();
        return false;
    }

    return commitTransaction();
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
    QString sql;
    if (ftsQuery.isEmpty()) {
        sql = QStringLiteral("SELECT r.id, 0.0 AS score, 'all' AS matched_field "
                             "FROM resources r ");
    } else {
        sql = QStringLiteral("SELECT r.id, bm25(resource_fts) AS score, 'fts' AS matched_field, resource_fts.content "
                             "FROM resource_fts JOIN resources r ON r.id = resource_fts.resource_id "
                             "WHERE resource_fts MATCH ? ");
    }

    for (int i = 0; i < query.requiredTags.size(); ++i) {
        sql += ftsQuery.isEmpty() && i == 0 ? QStringLiteral("WHERE ") : QStringLiteral("AND ");
        sql += QStringLiteral("EXISTS (SELECT 1 FROM resource_tags rt%1 "
                              "WHERE rt%1.resource_id = r.id AND lower(rt%1.tag) = lower(?)) ")
                   .arg(i);
    }

    sql += ftsQuery.isEmpty()
        ? QStringLiteral("ORDER BY lower(r.title), r.id ")
        : QStringLiteral("ORDER BY score, lower(r.title), r.id ");

    QSqlQuery sqlQuery(database_);
    sqlQuery.prepare(sql);
    if (!ftsQuery.isEmpty()) {
        sqlQuery.addBindValue(ftsQuery);
    }
    for (const QString &tag : query.requiredTags) {
        sqlQuery.addBindValue(tag);
    }

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

    if (!ftsQuery.isEmpty()) {
        QString anchorSql = QStringLiteral("SELECT r.id, anchor_fts.anchor_order, bm25(anchor_fts) AS score "
                                           "FROM anchor_fts JOIN resources r ON r.id = anchor_fts.resource_id "
                                           "WHERE anchor_fts MATCH ? ");
        for (int i = 0; i < query.requiredTags.size(); ++i) {
            anchorSql += QStringLiteral("AND EXISTS (SELECT 1 FROM resource_tags art%1 "
                                        "WHERE art%1.resource_id = r.id AND lower(art%1.tag) = lower(?)) ")
                             .arg(i);
        }
        anchorSql += QStringLiteral("ORDER BY score, lower(r.title), r.id ");

        QSqlQuery anchorQuery(database_);
        anchorQuery.prepare(anchorSql);
        anchorQuery.addBindValue(ftsQuery);
        for (const QString &tag : query.requiredTags) {
            anchorQuery.addBindValue(tag);
        }
        if (!anchorQuery.exec()) {
            setLastError(anchorQuery.lastError().text());
            return results;
        }

        while (anchorQuery.next()) {
            const QString id = anchorQuery.value(0).toString();
            const int anchorOrder = anchorQuery.value(1).toInt();
            const std::optional<Resource> resource = findResource(id);
            if (!resource.has_value() || anchorOrder < 0 || anchorOrder >= resource->anchors.size()) {
                continue;
            }
            if (!matchesRequiredLocationPrefixes(resource->location, query.requiredLocationPrefixes)) {
                continue;
            }
            if (!matchesRequiredKinds(resource->kind, query.requiredKinds)) {
                continue;
            }
            SearchResult result = anchorSearchResult(resource.value(), resource->anchors.at(anchorOrder), query.text);
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

bool SqliteLibraryRepository::clearResources()
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    if (!beginTransaction()) {
        return false;
    }

    if (!execute(QStringLiteral("DELETE FROM anchor_fts;"))
        || !execute(QStringLiteral("DELETE FROM resource_fts;"))
        || !execute(QStringLiteral("DELETE FROM anchor_usage;"))
        || !execute(QStringLiteral("DELETE FROM resource_usage;"))
        || !execute(QStringLiteral("DELETE FROM resources;"))) {
        rollbackTransaction();
        return false;
    }

    return commitTransaction();
}

bool SqliteLibraryRepository::upsertResourceRelation(const ResourceRelation &relation)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }
    if (relation.sourceResourceId.trimmed().isEmpty()
        || relation.targetResourceId.trimmed().isEmpty()
        || relation.label.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Resource relation source, target, and label are required"));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO resource_relations(source_resource_id, target_resource_id, label, note) "
                                 "VALUES (?, ?, ?, ?) "
                                 "ON CONFLICT(source_resource_id, target_resource_id, label) DO UPDATE SET "
                                 "note = excluded.note"));
    query.addBindValue(relation.sourceResourceId);
    query.addBindValue(relation.targetResourceId);
    query.addBindValue(relation.label);
    query.addBindValue(relation.note);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
    return true;
}

QList<ResourceRelation> SqliteLibraryRepository::resourceRelations(const QString &resourceId) const
{
    QList<ResourceRelation> relations;
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return relations;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT source_resource_id, target_resource_id, label, note "
                                 "FROM resource_relations "
                                 "WHERE source_resource_id = ? OR target_resource_id = ? "
                                 "ORDER BY lower(label), lower(source_resource_id), lower(target_resource_id)"));
    query.addBindValue(resourceId);
    query.addBindValue(resourceId);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return relations;
    }

    while (query.next()) {
        relations.append(hydrateResourceRelation(query));
    }

    return relations;
}

QList<ResourceRelation> SqliteLibraryRepository::allResourceRelations() const
{
    QList<ResourceRelation> relations;
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return relations;
    }

    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("SELECT source_resource_id, target_resource_id, label, note "
                                   "FROM resource_relations "
                                   "ORDER BY lower(label), lower(source_resource_id), lower(target_resource_id)"))) {
        setLastError(query.lastError().text());
        return relations;
    }

    while (query.next()) {
        relations.append(hydrateResourceRelation(query));
    }

    return relations;
}

bool SqliteLibraryRepository::removeResourceRelation(const QString &sourceResourceId,
                                                     const QString &targetResourceId,
                                                     const QString &label)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM resource_relations "
                                 "WHERE source_resource_id = ? AND target_resource_id = ? AND label = ?"));
    query.addBindValue(sourceResourceId);
    query.addBindValue(targetResourceId);
    query.addBindValue(label);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
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
    if (anchor.type == AnchorType::None) {
        setLastError(QStringLiteral("Anchor type is required"));
        return false;
    }
    if (!findResource(resourceId).has_value()) {
        setLastError(QStringLiteral("Resource not found"));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO anchor_usage(resource_id, anchor_key, type, target, line, page, "
                                 "x, y, width, height, open_count, last_opened_at) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?) "
                                 "ON CONFLICT(resource_id, anchor_key) DO UPDATE SET "
                                 "open_count = anchor_usage.open_count + 1,"
                                 "last_opened_at = excluded.last_opened_at"));
    query.addBindValue(resourceId);
    query.addBindValue(anchorUsageKey(anchor));
    query.addBindValue(anchorTypeToString(anchor.type));
    query.addBindValue(anchor.target);
    query.addBindValue(anchor.line >= 0 ? QVariant(anchor.line) : QVariant());
    query.addBindValue(anchor.page >= 0 ? QVariant(anchor.page) : QVariant());
    query.addBindValue(anchor.region.x());
    query.addBindValue(anchor.region.y());
    query.addBindValue(anchor.region.width());
    query.addBindValue(anchor.region.height());
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
    return true;
}

std::optional<AnchorUsage> SqliteLibraryRepository::anchorUsage(const QString &resourceId, const Anchor &anchor) const
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return std::nullopt;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT resource_id, type, target, line, page, x, y, width, height, open_count, last_opened_at "
                                 "FROM anchor_usage WHERE resource_id = ? AND anchor_key = ?"));
    query.addBindValue(resourceId);
    query.addBindValue(anchorUsageKey(anchor));
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return std::nullopt;
    }
    if (query.next()) {
        return hydrateAnchorUsage(query);
    }

    const QString legacyKey = legacyAnchorUsageKey(anchor);
    if (legacyKey == anchorUsageKey(anchor)) {
        return std::nullopt;
    }

    QSqlQuery legacyQuery(database_);
    legacyQuery.prepare(QStringLiteral("SELECT resource_id, type, target, line, page, x, y, width, height, open_count, last_opened_at "
                                       "FROM anchor_usage WHERE resource_id = ? AND anchor_key = ?"));
    legacyQuery.addBindValue(resourceId);
    legacyQuery.addBindValue(legacyKey);
    if (!legacyQuery.exec()) {
        setLastError(legacyQuery.lastError().text());
        return std::nullopt;
    }
    if (!legacyQuery.next()) {
        return std::nullopt;
    }

    return hydrateAnchorUsage(legacyQuery);
}

bool SqliteLibraryRepository::upsertLibraryRoot(const LibraryRoot &root)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }
    if (root.id.trimmed().isEmpty() || root.path.trimmed().isEmpty()) {
        setLastError(QStringLiteral("Library root id and path are required"));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO library_roots(id, path, display_name, enabled, pinned, last_indexed_at) "
                                 "VALUES (?, ?, ?, ?, ?, ?) "
                                 "ON CONFLICT(id) DO UPDATE SET "
                                 "path = excluded.path,"
                                 "display_name = excluded.display_name,"
                                 "enabled = excluded.enabled,"
                                 "pinned = excluded.pinned,"
                                 "last_indexed_at = excluded.last_indexed_at"));
    query.addBindValue(root.id);
    query.addBindValue(root.path);
    query.addBindValue(root.displayName.isEmpty() ? root.path : root.displayName);
    query.addBindValue(root.enabled ? 1 : 0);
    query.addBindValue(root.pinned ? 1 : 0);
    query.addBindValue(root.lastIndexedAt.isValid()
                           ? root.lastIndexedAt.toUTC().toString(Qt::ISODate)
                           : QVariant());
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
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
    if (!query.exec(QStringLiteral("SELECT id, path, display_name, enabled, pinned, last_indexed_at "
                                   "FROM library_roots ORDER BY pinned DESC, lower(display_name), lower(path)"))) {
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
    query.prepare(QStringLiteral("SELECT id, path, display_name, enabled, pinned, last_indexed_at "
                                 "FROM library_roots WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }

    return hydrateLibraryRoot(query);
}

bool SqliteLibraryRepository::removeLibraryRoot(const QString &id)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM library_roots WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool SqliteLibraryRepository::setLibraryRootEnabled(const QString &id, bool enabled)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE library_roots SET enabled = ? WHERE id = ?"));
    query.addBindValue(enabled ? 1 : 0);
    query.addBindValue(id);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool SqliteLibraryRepository::setLibraryRootPinned(const QString &id, bool pinned)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE library_roots SET pinned = ? WHERE id = ?"));
    query.addBindValue(pinned ? 1 : 0);
    query.addBindValue(id);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool SqliteLibraryRepository::updateLibraryRootLastIndexedAt(const QString &id, const QDateTime &indexedAt)
{
    if (!isOpen()) {
        setLastError(QStringLiteral("Database is not open"));
        return false;
    }

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("UPDATE library_roots SET last_indexed_at = ? WHERE id = ?"));
    query.addBindValue(indexedAt.isValid() ? indexedAt.toUTC().toString(Qt::ISODate) : QVariant());
    query.addBindValue(id);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() > 0;
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

bool SqliteLibraryRepository::ensureLibraryRootPinnedColumn()
{
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("PRAGMA table_info(library_roots)"))) {
        setLastError(query.lastError().text());
        return false;
    }

    while (query.next()) {
        if (query.value(1).toString() == QLatin1String("pinned")) {
            return true;
        }
    }

    return execute(QStringLiteral("ALTER TABLE library_roots ADD COLUMN pinned INTEGER NOT NULL DEFAULT 0"));
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
    if (!database_.transaction()) {
        setLastError(database_.lastError().text());
        return false;
    }
    return true;
}

bool SqliteLibraryRepository::commitTransaction()
{
    if (!database_.commit()) {
        setLastError(database_.lastError().text());
        return false;
    }
    return true;
}

void SqliteLibraryRepository::rollbackTransaction()
{
    database_.rollback();
}

Resource SqliteLibraryRepository::hydrateResource(const QString &id) const
{
    Resource resource;

    QSqlQuery query(database_);
    query.prepare(QStringLiteral("SELECT id, kind, title, location, updated_at FROM resources WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        setLastError(query.lastError().text());
        return resource;
    }

    resource.id = query.value(0).toString();
    resource.kind = resourceKindFromString(query.value(1).toString());
    resource.title = query.value(2).toString();
    resource.location = query.value(3).toString();
    resource.updatedAt = QDateTime::fromString(query.value(4).toString(), Qt::ISODate);
    resource.tags = readStrings(QStringLiteral("resource_tags"), QStringLiteral("tag"), id);
    resource.aliases = readStrings(QStringLiteral("resource_aliases"), QStringLiteral("alias"), id);
    resource.anchors = readAnchors(id);

    return resource;
}

LibraryRoot SqliteLibraryRepository::hydrateLibraryRoot(QSqlQuery &query) const
{
    LibraryRoot root;
    root.id = query.value(0).toString();
    root.path = query.value(1).toString();
    root.displayName = query.value(2).toString();
    root.enabled = query.value(3).toInt() != 0;
    root.pinned = query.value(4).toInt() != 0;
    root.lastIndexedAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODate);
    return root;
}

ResourceRelation SqliteLibraryRepository::hydrateResourceRelation(QSqlQuery &query) const
{
    ResourceRelation relation;
    relation.sourceResourceId = query.value(0).toString();
    relation.targetResourceId = query.value(1).toString();
    relation.label = query.value(2).toString();
    relation.note = query.value(3).toString();
    return relation;
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
    usage.anchor.type = anchorTypeFromString(query.value(1).toString());
    usage.anchor.target = query.value(2).toString();
    usage.anchor.line = query.value(3).isNull() ? -1 : query.value(3).toInt();
    usage.anchor.page = query.value(4).isNull() ? -1 : query.value(4).toInt();
    usage.anchor.region = QRectF(query.value(5).toDouble(),
                                 query.value(6).toDouble(),
                                 query.value(7).toDouble(),
                                 query.value(8).toDouble());
    usage.openCount = query.value(9).toInt();
    usage.lastOpenedAt = QDateTime::fromString(query.value(10).toString(), Qt::ISODate);
    return usage;
}

void SqliteLibraryRepository::applyRankingSignals(SearchResult &result, const SearchQuery &query) const
{
    result.score += contextScoreAdjustment(result.resource, query);
    const std::optional<ResourceRelation> contextRelation =
        matchedContextRelation(result.resource, query, resourceRelations(result.resource.id));
    if (contextRelation.has_value()) {
        result.score -= 0.4;
        result.matchedContextRelationLabel = contextRelation->label;
        result.matchedContextRelationNote = contextRelation->note;
        result.matchedContextResourceId = contextRelation->sourceResourceId == result.resource.id
            ? contextRelation->targetResourceId
            : contextRelation->sourceResourceId;
    }

    for (const LibraryRoot &root : libraryRoots()) {
        if (resourceMatchesLibraryRoot(result.resource, root)) {
            result.score -= 0.45;
            break;
        }
    }

    const std::optional<ResourceUsage> usage = resourceUsage(result.resource.id);
    if (usage.has_value()) {
        result.score += usageScoreAdjustment(usage.value());
    }

    if (result.matchedAnchor.has_value()) {
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
    query.prepare(QStringLiteral("SELECT type, target, line, page, x, y, width, height "
                                 "FROM anchors WHERE resource_id = ? ORDER BY anchor_order"));
    query.addBindValue(resourceId);
    if (!query.exec()) {
        setLastError(query.lastError().text());
        return anchors;
    }

    while (query.next()) {
        Anchor anchor;
        anchor.type = anchorTypeFromString(query.value(0).toString());
        anchor.target = query.value(1).toString();
        anchor.line = query.value(2).isNull() ? -1 : query.value(2).toInt();
        anchor.page = query.value(3).isNull() ? -1 : query.value(3).toInt();
        anchor.region = QRectF(query.value(4).toDouble(),
                               query.value(5).toDouble(),
                               query.value(6).toDouble(),
                               query.value(7).toDouble());
        anchors.append(anchor);
    }

    return anchors;
}

void SqliteLibraryRepository::setLastError(const QString &message) const
{
    lastError_ = message;
}

} // namespace Pinloom
