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
    case ResourceKind::CodeSnippet:
        return QStringLiteral("code_snippet");
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
    if (kind == QLatin1String("code_snippet")) {
        return ResourceKind::CodeSnippet;
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
        return QStringLiteral("markdown_heading");
    case AnchorType::MarkdownBlock:
        return QStringLiteral("markdown_block");
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
    if (type == QLatin1String("markdown_heading")) {
        return AnchorType::MarkdownHeading;
    }
    if (type == QLatin1String("markdown_block")) {
        return AnchorType::MarkdownBlock;
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

QString ftsQueryFromText(const QString &text)
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
    return tokens.join(QLatin1Char(' '));
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

    if (!recordMigration(1, QStringLiteral("initial_sqlite_fts5_schema"))
        || !recordMigration(2, QStringLiteral("library_roots"))
        || !recordMigration(3, QStringLiteral("anchor_fts"))) {
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
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO resources(id, kind, title, location, updated_at) "
                                 "VALUES (?, ?, ?, ?, ?)"));
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

        if (anchor.type == AnchorType::MarkdownHeading || anchor.type == AnchorType::MarkdownBlock) {
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
    ftsQuery.addBindValue(QString());
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
    QString sql;
    if (ftsQuery.isEmpty()) {
        sql = QStringLiteral("SELECT r.id, 0.0 AS score, 'all' AS matched_field "
                             "FROM resources r ");
    } else {
        sql = QStringLiteral("SELECT r.id, bm25(resource_fts) AS score, 'fts' AS matched_field "
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
        const std::optional<Resource> resource = findResource(id);
        if (!resource.has_value()) {
            continue;
        }
        results.append(SearchResult{resource.value(), sqlQuery.value(1).toDouble(), sqlQuery.value(2).toString(), std::nullopt});
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
            results.append(SearchResult{resource.value(),
                                        anchorQuery.value(2).toDouble(),
                                        QStringLiteral("anchor"),
                                        resource->anchors.at(anchorOrder)});
        }
    }

    std::sort(results.begin(), results.end(), [](const SearchResult &left, const SearchResult &right) {
        if (left.score == right.score) {
            return left.resource.title < right.resource.title;
        }
        return left.score < right.score;
    });

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
        || !execute(QStringLiteral("DELETE FROM resources;"))) {
        rollbackTransaction();
        return false;
    }

    return commitTransaction();
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
    query.prepare(QStringLiteral("INSERT INTO library_roots(id, path, display_name, enabled, last_indexed_at) "
                                 "VALUES (?, ?, ?, ?, ?) "
                                 "ON CONFLICT(id) DO UPDATE SET "
                                 "path = excluded.path,"
                                 "display_name = excluded.display_name,"
                                 "enabled = excluded.enabled,"
                                 "last_indexed_at = excluded.last_indexed_at"));
    query.addBindValue(root.id);
    query.addBindValue(root.path);
    query.addBindValue(root.displayName.isEmpty() ? root.path : root.displayName);
    query.addBindValue(root.enabled ? 1 : 0);
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
    if (!query.exec(QStringLiteral("SELECT id, path, display_name, enabled, last_indexed_at "
                                   "FROM library_roots ORDER BY lower(display_name), lower(path)"))) {
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
    query.prepare(QStringLiteral("SELECT id, path, display_name, enabled, last_indexed_at "
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
    root.lastIndexedAt = QDateTime::fromString(query.value(4).toString(), Qt::ISODate);
    return root;
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
