#include "pinloom/core/SqliteLibraryRepository.h"
#include "pinloom/core/InboxFileCapture.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <QVariant>
#include <algorithm>
#include <optional>

using namespace Pinloom;

class SqliteRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void initializesIdempotently();
    void persistsAndSearchesResourceMetadata();
    void persistsAndSearchesAnchorLocatorFields();
    void persistsAndSearchesInboxFiles();
    void filtersLegacyPdfManualLineAnchorsFromSearch();
    void ranksAnchorAndFilenameMatchesBeforePathNoise();
    void ranksExactMatchesWithinMatchType();
    void tracksUsageAndRanksRecallSignals();
    void filtersByRequiredLocationPrefixes();
    void filtersByRequiredResourceKinds();
    void ranksContextSignalsWithinMatchType();
    void ranksRelatedContextResourcesWithinMatchType();
    void ranksPinnedLibraryRootSignalsWithinMatchType();
    void tracksAnchorUsageAndRanksAnchorRecall();
    void managesResourceRelations();
    void managesLibraryRoots();
    void upgradesVersionOneDatabase();
    void upgradesVersionTwoDatabaseWithRoots();
    void upgradesVersionSevenDatabaseWithLegacyAnchors();
};

static void createVersionOneDatabase(const QString &path)
{
    const QString connectionName = QStringLiteral("v1_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setDatabaseName(path);
    QVERIFY(database.open());

    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE schema_migrations ("
                                      "version INTEGER PRIMARY KEY,"
                                      "name TEXT NOT NULL,"
                                      "applied_at TEXT NOT NULL"
                                      ");")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO schema_migrations(version, name, applied_at) "
                                      "VALUES (1, 'initial_sqlite_fts5_schema', '2026-01-01T00:00:00Z');")));
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE resources ("
                                      "id TEXT PRIMARY KEY,"
                                      "kind TEXT NOT NULL,"
                                      "title TEXT NOT NULL,"
                                      "location TEXT NOT NULL,"
                                      "updated_at TEXT"
                                      ");")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO resources(id, kind, title, location, updated_at) "
                                      "VALUES ('legacy', 'markdown', 'Legacy Note', 'legacy.md', '2026-01-01T00:00:00Z');")));

    database.close();
    database = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
}

static void createVersionTwoDatabase(const QString &path)
{
    const QString connectionName = QStringLiteral("v2_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setDatabaseName(path);
    QVERIFY(database.open());

    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE schema_migrations ("
                                      "version INTEGER PRIMARY KEY,"
                                      "name TEXT NOT NULL,"
                                      "applied_at TEXT NOT NULL"
                                      ");")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO schema_migrations(version, name, applied_at) "
                                      "VALUES (1, 'initial_sqlite_fts5_schema', '2026-01-01T00:00:00Z');")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO schema_migrations(version, name, applied_at) "
                                      "VALUES (2, 'library_roots', '2026-01-01T00:00:00Z');")));
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE resources ("
                                      "id TEXT PRIMARY KEY,"
                                      "kind TEXT NOT NULL,"
                                      "title TEXT NOT NULL,"
                                      "location TEXT NOT NULL,"
                                      "updated_at TEXT"
                                      ");")));
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE library_roots ("
                                      "id TEXT PRIMARY KEY,"
                                      "path TEXT NOT NULL UNIQUE,"
                                      "display_name TEXT NOT NULL,"
                                      "enabled INTEGER NOT NULL DEFAULT 1,"
                                      "last_indexed_at TEXT"
                                      ");")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO resources(id, kind, title, location, updated_at) "
                                      "VALUES ('legacy', 'markdown', 'Legacy Note', 'legacy.md', '2026-01-01T00:00:00Z');")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO library_roots(id, path, display_name, enabled, last_indexed_at) "
                                      "VALUES ('dir:test', 'E:/test', 'test', 1, NULL);")));

    database.close();
    database = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
}

static void createVersionSevenDatabaseWithLegacyAnchor(const QString &path)
{
    const QString connectionName = QStringLiteral("v7_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setDatabaseName(path);
    QVERIFY(database.open());

    QSqlQuery query(database);
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE schema_migrations ("
                                      "version INTEGER PRIMARY KEY,"
                                      "name TEXT NOT NULL,"
                                      "applied_at TEXT NOT NULL"
                                      ");")));
    for (int version = 1; version <= 7; ++version) {
        query.prepare(QStringLiteral("INSERT INTO schema_migrations(version, name, applied_at) "
                                     "VALUES (?, ?, '2026-01-01T00:00:00Z')"));
        query.addBindValue(version);
        query.addBindValue(QStringLiteral("legacy_%1").arg(version));
        QVERIFY(query.exec());
    }
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE resources ("
                                      "id TEXT PRIMARY KEY,"
                                      "kind TEXT NOT NULL,"
                                      "title TEXT NOT NULL,"
                                      "location TEXT NOT NULL,"
                                      "updated_at TEXT"
                                      ");")));
    QVERIFY(query.exec(QStringLiteral("CREATE TABLE anchors ("
                                      "resource_id TEXT NOT NULL,"
                                      "anchor_order INTEGER NOT NULL,"
                                      "type TEXT NOT NULL,"
                                      "target TEXT,"
                                      "line INTEGER,"
                                      "page INTEGER,"
                                      "x REAL,"
                                      "y REAL,"
                                      "width REAL,"
                                      "height REAL,"
                                      "PRIMARY KEY (resource_id, anchor_order),"
                                      "FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE"
                                      ");")));
    QVERIFY(query.exec(QStringLiteral("CREATE VIRTUAL TABLE resource_fts "
                                      "USING fts5(resource_id UNINDEXED, title, aliases, tags, location, content);")));
    QVERIFY(query.exec(QStringLiteral("CREATE VIRTUAL TABLE anchor_fts "
                                      "USING fts5(resource_id UNINDEXED, anchor_order UNINDEXED, type, target);")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO resources(id, kind, title, location, updated_at) "
                                      "VALUES ('legacy', 'markdown', 'Legacy Note', 'legacy.md', '2026-01-01T00:00:00Z');")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO anchors(resource_id, anchor_order, type, target, line, page, x, y, width, height) "
                                      "VALUES ('legacy', 0, 'markdown_heading', 'Legacy Jump', 17, NULL, 0, 0, 0, 0);")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO anchor_fts(resource_id, anchor_order, type, target) "
                                      "VALUES ('legacy', 0, 'markdown_heading', 'Legacy Jump');")));

    database.close();
    database = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
}

void SqliteRepositoryTest::initializesIdempotently()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));
}

void SqliteRepositoryTest::persistsAndSearchesResourceMetadata()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Resource resource;
    resource.id = QStringLiteral("design-doc");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("FPGA Bringup Plan");
    resource.location = QStringLiteral("docs/bringup.txt");
    resource.tags = {QStringLiteral("fpga"), QStringLiteral("uart")};
    resource.aliases = {QStringLiteral("serial notes"), QStringLiteral("board diary")};
    resource.anchors = {
        Anchor{AnchorType::TextHeading, QStringLiteral("Power sequencing")},
        Anchor{AnchorType::TextBlock, QStringLiteral("power-block"), 7},
        Anchor{AnchorType::FileLine, QStringLiteral("Debug checkpoint"), 42}
    };

    QVERIFY2(repository.upsertResource(resource), qPrintable(repository.lastError()));

    const std::optional<Resource> stored = repository.findResource(resource.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->tags.size(), 2);
    QCOMPARE(stored->aliases.size(), 2);
    QCOMPARE(stored->anchors.size(), 3);
    QCOMPARE(stored->anchors.at(0).type, AnchorType::TextHeading);
    QCOMPARE(stored->anchors.at(1).type, AnchorType::TextBlock);
    QCOMPARE(stored->anchors.at(2).line, 42);

    const QList<SearchResult> titleResults = repository.search(SearchQuery{QStringLiteral("bringup")});
    QCOMPARE(titleResults.size(), 1);
    QCOMPARE(titleResults.first().resource.id, resource.id);

    const QList<SearchResult> aliasResults = repository.search(SearchQuery{QStringLiteral("serial")});
    QCOMPARE(aliasResults.size(), 1);
    QCOMPARE(aliasResults.first().resource.id, resource.id);

    const QList<SearchResult> anchorResults = repository.search(SearchQuery{QStringLiteral("Power sequencing")});
    QCOMPARE(anchorResults.size(), 1);
    QVERIFY(anchorResults.first().matchedAnchor.has_value());
    QCOMPARE(anchorResults.first().matchedAnchor->type, AnchorType::TextHeading);
    QCOMPARE(anchorResults.first().matchedAnchor->target, QStringLiteral("Power sequencing"));

    const QList<SearchResult> lineResults = repository.search(SearchQuery{QStringLiteral("checkpoint")});
    QCOMPARE(lineResults.size(), 1);
    QVERIFY(lineResults.first().matchedAnchor.has_value());
    QCOMPARE(lineResults.first().matchedAnchor->type, AnchorType::FileLine);
    QCOMPARE(lineResults.first().matchedAnchor->line, 42);

    const QList<SearchResult> blockResults = repository.search(SearchQuery{QStringLiteral("power-block")});
    QCOMPARE(blockResults.size(), 1);
    QVERIFY(blockResults.first().matchedAnchor.has_value());
    QCOMPARE(blockResults.first().matchedAnchor->type, AnchorType::TextBlock);

    Resource textSnippet;
    textSnippet.id = QStringLiteral("text-snippet");
    textSnippet.kind = ResourceKind::TextSnippet;
    textSnippet.title = QStringLiteral("Neutral Text Beacon");
    textSnippet.location = QStringLiteral("snippets/beacon.txt");
    textSnippet.anchors = {
        Anchor{AnchorType::Marker, QStringLiteral("marker: neutral_beacon"), 3}
    };
    QVERIFY2(repository.upsertResource(textSnippet), qPrintable(repository.lastError()));

    const std::optional<Resource> storedSnippet = repository.findResource(textSnippet.id);
    QVERIFY(storedSnippet.has_value());
    QCOMPARE(storedSnippet->kind, ResourceKind::TextSnippet);
    QCOMPARE(storedSnippet->anchors.size(), 1);
    QCOMPARE(storedSnippet->anchors.first().type, AnchorType::Marker);

    Resource legacyMarkdownInput;
    legacyMarkdownInput.id = QStringLiteral("legacy-markdown-input");
    legacyMarkdownInput.kind = ResourceKind::Markdown;
    legacyMarkdownInput.title = QStringLiteral("Legacy Markdown Kind Input");
    legacyMarkdownInput.location = QStringLiteral("notes/legacy.md");
    legacyMarkdownInput.anchors = {
        Anchor{AnchorType::MarkdownHeading, QStringLiteral("Legacy Heading"), 5}
    };
    QVERIFY2(repository.upsertResource(legacyMarkdownInput), qPrintable(repository.lastError()));

    const std::optional<Resource> storedLegacyMarkdownInput =
        repository.findResource(legacyMarkdownInput.id);
    QVERIFY(storedLegacyMarkdownInput.has_value());
    QCOMPARE(storedLegacyMarkdownInput->kind, ResourceKind::File);
    QCOMPARE(storedLegacyMarkdownInput->anchors.size(), 1);
    QCOMPARE(storedLegacyMarkdownInput->anchors.first().type, AnchorType::TextHeading);

    const QString rawConnectionName =
        QStringLiteral("pinloom_raw_kind_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    QSqlDatabase rawDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), rawConnectionName);
    rawDatabase.setDatabaseName(dir.filePath(QStringLiteral("pinloom.sqlite3")));
    QVERIFY(rawDatabase.open());
    QSqlQuery rawQuery(rawDatabase);
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT kind FROM resources WHERE id = 'text-snippet'")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toString(), QStringLiteral("text_snippet"));
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT kind FROM resources WHERE id = 'legacy-markdown-input'")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toString(), QStringLiteral("file"));
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT type FROM anchors WHERE resource_id = 'design-doc' ORDER BY anchor_order")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toString(), QStringLiteral("text_heading"));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toString(), QStringLiteral("text_block"));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toString(), QStringLiteral("file_line"));
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT type FROM anchors WHERE resource_id = 'text-snippet'")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toString(), QStringLiteral("marker"));
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT COUNT(*) FROM resources WHERE kind LIKE 'code_%'")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toInt(), 0);
    QVERIFY(rawQuery.exec(QStringLiteral(
        "SELECT COUNT(*) FROM anchors WHERE type IN ('code_symbol', 'symbol_like')")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toInt(), 0);
    QVERIFY(rawQuery.exec(QStringLiteral(
        "INSERT INTO resources(id, kind, title, location) "
        "VALUES ('legacy-text-anchors', 'file', 'Legacy Text Anchors', 'legacy/anchors.txt')")));
    QVERIFY(rawQuery.exec(QStringLiteral(
        "INSERT INTO anchors(resource_id, anchor_order, type, target, line) "
        "VALUES ('legacy-text-anchors', 0, 'markdown_heading', 'Legacy Heading', 12)")));
    QVERIFY(rawQuery.exec(QStringLiteral(
        "INSERT INTO anchors(resource_id, anchor_order, type, target, line) "
        "VALUES ('legacy-text-anchors', 1, 'markdown_block', 'legacy-block', 13)")));
    QVERIFY(rawQuery.exec(QStringLiteral(
        "INSERT INTO anchor_usage(resource_id, anchor_key, type, target, line, x, y, width, height, open_count, last_opened_at) "
        "VALUES ('legacy-text-anchors', 'markdown_heading|Legacy Heading|12|-1|0.00|0.00|0.00|0.00', "
        "'markdown_heading', 'Legacy Heading', 12, 0, 0, 0, 0, 3, '2026-01-01T00:00:00Z')")));
    QVERIFY(rawQuery.exec(QStringLiteral(
        "INSERT INTO resources(id, kind, title, location) "
        "VALUES ('legacy-neutral-snippet', 'code_snippet', 'Legacy Neutral Snippet', 'legacy/snippet.txt')")));
    QVERIFY(rawQuery.exec(QStringLiteral(
        "INSERT INTO anchors(resource_id, anchor_order, type, target, line) "
        "VALUES ('legacy-neutral-snippet', 0, 'code_symbol', 'legacy_symbol', 9)")));
    QVERIFY(rawQuery.exec(QStringLiteral(
        "INSERT INTO anchors(resource_id, anchor_order, type, target, line) "
        "VALUES ('legacy-neutral-snippet', 1, 'symbol_like', 'legacy_marker', 10)")));
    QVERIFY(rawQuery.exec(QStringLiteral(
        "INSERT INTO resources(id, kind, title, location) "
        "VALUES ('legacy-markdown-row', 'markdown', 'Legacy Markdown Row', 'legacy/row.md')")));
    rawQuery = QSqlQuery();
    rawDatabase.close();
    rawDatabase = QSqlDatabase();
    QSqlDatabase::removeDatabase(rawConnectionName);

    const std::optional<Resource> legacyMarkdownRow =
        repository.findResource(QStringLiteral("legacy-markdown-row"));
    QVERIFY(legacyMarkdownRow.has_value());
    QCOMPARE(legacyMarkdownRow->kind, ResourceKind::File);

    const std::optional<Resource> legacySnippet = repository.findResource(QStringLiteral("legacy-neutral-snippet"));
    QVERIFY(legacySnippet.has_value());
    QCOMPARE(legacySnippet->kind, ResourceKind::TextSnippet);
    QCOMPARE(legacySnippet->anchors.size(), 2);
    QCOMPARE(legacySnippet->anchors.first().type, AnchorType::Marker);
    QCOMPARE(legacySnippet->anchors.last().type, AnchorType::Marker);

    const std::optional<Resource> legacyTextAnchors = repository.findResource(QStringLiteral("legacy-text-anchors"));
    QVERIFY(legacyTextAnchors.has_value());
    QCOMPARE(legacyTextAnchors->anchors.size(), 2);
    QCOMPARE(legacyTextAnchors->anchors.first().type, AnchorType::TextHeading);
    QCOMPARE(legacyTextAnchors->anchors.last().type, AnchorType::TextBlock);
    Anchor legacyHeading;
    legacyHeading.type = AnchorType::TextHeading;
    legacyHeading.target = QStringLiteral("Legacy Heading");
    legacyHeading.line = 12;
    const std::optional<AnchorUsage> legacyHeadingUsage =
        repository.anchorUsage(QStringLiteral("legacy-text-anchors"), legacyHeading);
    QVERIFY(legacyHeadingUsage.has_value());
    QCOMPARE(legacyHeadingUsage->anchor.type, AnchorType::TextHeading);
    QCOMPARE(legacyHeadingUsage->openCount, 3);

    SearchQuery taggedQuery;
    taggedQuery.text = QStringLiteral("plan");
    taggedQuery.requiredTags = {QStringLiteral("uart")};
    const QList<SearchResult> tagResults = repository.search(taggedQuery);
    QCOMPARE(tagResults.size(), 1);

    Resource updated = resource;
    updated.title = QStringLiteral("FPGA Bringup Plan Updated");
    updated.aliases = {QStringLiteral("serial notes")};
    QVERIFY2(repository.upsertResource(updated), qPrintable(repository.lastError()));
    const std::optional<Resource> updatedStored = repository.findResource(resource.id);
    QVERIFY(updatedStored.has_value());
    QCOMPARE(updatedStored->aliases.size(), 1);
}

void SqliteRepositoryTest::persistsAndSearchesAnchorLocatorFields()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Anchor anchor;
    anchor.type = AnchorType::Manual;
    anchor.id = QStringLiteral("anchor:clock-domain");
    anchor.name = QStringLiteral("Clock domain window");
    anchor.targetApp = QStringLiteral("PDF-XChange");
    anchor.targetFile = QStringLiteral("E:/specs/clocking.pdf");
    anchor.locatorType = QStringLiteral("pdfxchange.rect");
    anchor.locatorJson = QStringLiteral("{\"page\":12,\"rect\":[420,860,780,920],\"zoom\":250}");
    anchor.aliases = {QStringLiteral("cdc zoom")};
    anchor.tags = {QStringLiteral("review-point")};
    anchor.pinned = true;
    anchor.createdAt = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    anchor.updatedAt = QDateTime::fromString(QStringLiteral("2026-01-02T00:00:00Z"), Qt::ISODate);
    anchor.usedAt = QDateTime::fromString(QStringLiteral("2026-01-03T00:00:00Z"), Qt::ISODate);

    Resource resource;
    resource.id = QStringLiteral("clocking-pdf");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Launcher Row");
    resource.location = QStringLiteral("E:/specs/clocking.pdf");
    resource.anchors = {anchor};

    QVERIFY2(repository.upsertResource(resource), qPrintable(repository.lastError()));

    const std::optional<Resource> stored = repository.findResource(resource.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->anchors.size(), 1);
    const Anchor storedAnchor = stored->anchors.first();
    QCOMPARE(storedAnchor.id, anchor.id);
    QCOMPARE(storedAnchor.name, anchor.name);
    QCOMPARE(storedAnchor.target, anchor.name);
    QCOMPARE(storedAnchor.targetApp, anchor.targetApp);
    QCOMPARE(storedAnchor.targetFile, anchor.targetFile);
    QCOMPARE(storedAnchor.locatorType, anchor.locatorType);
    QCOMPARE(storedAnchor.locatorJson, anchor.locatorJson);
    QCOMPARE(storedAnchor.aliases, anchor.aliases);
    QCOMPARE(storedAnchor.tags, anchor.tags);
    QVERIFY(storedAnchor.pinned);
    QCOMPARE(storedAnchor.createdAt, anchor.createdAt);
    QCOMPARE(storedAnchor.updatedAt, anchor.updatedAt);
    QCOMPARE(storedAnchor.usedAt, anchor.usedAt);

    const QList<SearchResult> nameResults = repository.search(SearchQuery{QStringLiteral("Clock domain window")});
    QCOMPARE(nameResults.size(), 1);
    QCOMPARE(nameResults.first().matchedField, QStringLiteral("anchor_name"));

    const QList<SearchResult> aliasResults = repository.search(SearchQuery{QStringLiteral("cdc zoom")});
    QCOMPARE(aliasResults.size(), 1);
    QCOMPARE(aliasResults.first().matchedField, QStringLiteral("anchor_alias"));

    const QList<SearchResult> tagResults = repository.search(SearchQuery{QStringLiteral("review-point")});
    QCOMPARE(tagResults.size(), 1);
    QCOMPARE(tagResults.first().matchedField, QStringLiteral("anchor_tag"));

    const QList<SearchResult> metadataResults = repository.search(SearchQuery{QStringLiteral("pdfxchange.rect")});
    QCOMPARE(metadataResults.size(), 1);
    QCOMPARE(metadataResults.first().matchedField, QStringLiteral("anchor_metadata"));

    const QString rawConnectionName =
        QStringLiteral("pinloom_raw_anchor_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    QSqlDatabase rawDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), rawConnectionName);
    rawDatabase.setDatabaseName(dir.filePath(QStringLiteral("pinloom.sqlite3")));
    QVERIFY(rawDatabase.open());
    QSqlQuery rawQuery(rawDatabase);
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT id, name, target_app, target_file, locator_type, aliases, tags, pinned "
                                         "FROM anchors WHERE resource_id = 'clocking-pdf'")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toString(), anchor.id);
    QCOMPARE(rawQuery.value(1).toString(), anchor.name);
    QCOMPARE(rawQuery.value(2).toString(), anchor.targetApp);
    QCOMPARE(rawQuery.value(3).toString(), anchor.targetFile);
    QCOMPARE(rawQuery.value(4).toString(), anchor.locatorType);
    QCOMPARE(rawQuery.value(5).toString(), QStringLiteral("cdc zoom"));
    QCOMPARE(rawQuery.value(6).toString(), QStringLiteral("review-point"));
    QCOMPARE(rawQuery.value(7).toInt(), 1);
    rawQuery = QSqlQuery();
    rawDatabase.close();
    rawDatabase = QSqlDatabase();
    QSqlDatabase::removeDatabase(rawConnectionName);
}

void SqliteRepositoryTest::persistsAndSearchesInboxFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("pinloom.sqlite3"));
    const QString filePath = dir.filePath(QStringLiteral("Inbox Spec.txt"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("inbox spec");
    file.close();

    QString resourceId;
    {
        SqliteLibraryRepository repository;
        QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
        QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

        InboxFileSaveRequest request;
        request.filePath = filePath;
        request.name = QStringLiteral("Inbox Spec");
        request.aliases = {QStringLiteral("inbox alias")};
        request.tags = {QStringLiteral("#handoff")};
        const InboxFileSaveResult result = saveInboxFile(repository, request);
        QVERIFY2(result.success(), qPrintable(result.status));
        QCOMPARE(result.saveStatus, InboxFileSaveStatus::Created);
        resourceId = result.resourceId;
    }

    SqliteLibraryRepository reopened;
    QVERIFY2(reopened.open(databasePath), qPrintable(reopened.lastError()));
    QVERIFY2(reopened.initialize(), qPrintable(reopened.lastError()));

    const std::optional<Resource> stored = reopened.findResource(resourceId);
    QVERIFY(stored.has_value());
    QVERIFY(isInboxResource(stored.value()));
    QCOMPARE(stored->title, QStringLiteral("Inbox Spec"));
    QCOMPARE(stored->location, normalizedInboxFilePath(filePath));
    QCOMPARE(stored->aliases, QStringList{QStringLiteral("inbox alias")});
    QCOMPARE(stored->tags, QStringList{QStringLiteral("handoff")});

    const QList<SearchResult> nameResults = reopened.search(SearchQuery{QStringLiteral("Inbox Spec")});
    QCOMPARE(nameResults.size(), 1);
    QCOMPARE(nameResults.first().resource.id, resourceId);

    const QList<SearchResult> aliasResults = reopened.search(SearchQuery{QStringLiteral("inbox alias")});
    QCOMPARE(aliasResults.size(), 1);
    QCOMPARE(aliasResults.first().resource.id, resourceId);

    const QList<SearchResult> tagResults = reopened.search(SearchQuery{QStringLiteral("handoff")});
    QCOMPARE(tagResults.size(), 1);
    QCOMPARE(tagResults.first().resource.id, resourceId);
}

void SqliteRepositoryTest::filtersLegacyPdfManualLineAnchorsFromSearch()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Resource resource;
    resource.id = QStringLiteral("iso-pdf");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("ISO CAN Spec");
    resource.location = QStringLiteral("E:/test_dir/ISO 11898-1.pdf");

    Anchor legacy;
    legacy.type = AnchorType::Manual;
    legacy.name = QStringLiteral("legacy manual line 12");
    legacy.target = legacy.name;
    legacy.line = 12;
    legacy.locatorType = QStringLiteral("manual");
    legacy.locatorJson = QStringLiteral("{\"line\":12}");

    Anchor rect;
    rect.type = AnchorType::Manual;
    rect.name = QStringLiteral("stable PDF-XChange rect");
    rect.target = rect.name;
    rect.targetApp = QStringLiteral("PDF-XChange");
    rect.targetFile = resource.location;
    rect.locatorType = QStringLiteral("pdfxchange.rect");
    rect.locatorJson = QStringLiteral("{\"page\":12,\"rect\":[420,860,780,920]}");

    resource.anchors = {legacy, rect};
    QVERIFY2(repository.upsertResource(resource), qPrintable(repository.lastError()));

    const std::optional<Resource> stored = repository.findResource(resource.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->anchors.size(), 2);

    const QList<SearchResult> legacyResults =
        repository.search(SearchQuery{QStringLiteral("legacy manual line 12")});
    QCOMPARE(legacyResults.size(), 0);

    const QList<SearchResult> rectResults =
        repository.search(SearchQuery{QStringLiteral("stable PDF-XChange rect")});
    QCOMPARE(rectResults.size(), 1);
    QVERIFY(rectResults.first().matchedAnchor.has_value());
    QCOMPARE(rectResults.first().matchedAnchor->locatorType, QStringLiteral("pdfxchange.rect"));
}

void SqliteRepositoryTest::ranksAnchorAndFilenameMatchesBeforePathNoise()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Resource folder;
    folder.id = QStringLiteral("folder");
    folder.kind = ResourceKind::Folder;
    folder.title = QStringLiteral("test_dir");
    folder.location = QStringLiteral("E:/test_dir");
    QVERIFY2(repository.upsertResource(folder), qPrintable(repository.lastError()));

    Resource textFile;
    textFile.id = QStringLiteral("note");
    textFile.kind = ResourceKind::File;
    textFile.title = QStringLiteral("1.md");
    textFile.location = QStringLiteral("E:/test_dir/1.md");
    textFile.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("test"), 143}};
    QVERIFY2(repository.upsertResource(textFile), qPrintable(repository.lastError()));

    Resource pdf;
    pdf.id = QStringLiteral("pdf");
    pdf.kind = ResourceKind::Pdf;
    pdf.title = QStringLiteral("ISO 11898-1.pdf");
    pdf.location = QStringLiteral("E:/test_dir/ISO 11898-1.pdf");
    QVERIFY2(repository.upsertResource(pdf), qPrintable(repository.lastError()));

    const QList<SearchResult> testResults = repository.search(SearchQuery{QStringLiteral("test")});
    QVERIFY(testResults.size() >= 3);
    QCOMPARE(testResults.first().matchedField, QStringLiteral("anchor"));
    QVERIFY(testResults.first().matchedAnchor.has_value());

    const auto pathOnly = std::find_if(testResults.cbegin(), testResults.cend(), [](const SearchResult &result) {
        return result.matchedField == QLatin1String("path");
    });
    QVERIFY(pathOnly != testResults.cend());
    QVERIFY(std::distance(testResults.cbegin(), pathOnly) > 0);

    Resource namedByPath;
    namedByPath.id = QStringLiteral("schematic");
    namedByPath.kind = ResourceKind::File;
    namedByPath.title = QStringLiteral("Document");
    namedByPath.location = QStringLiteral("E:/test_dir/schematic.md");
    QVERIFY2(repository.upsertResource(namedByPath), qPrintable(repository.lastError()));

    const QList<SearchResult> filenameResults = repository.search(SearchQuery{QStringLiteral("schematic")});
    QCOMPARE(filenameResults.size(), 1);
    QCOMPARE(filenameResults.first().matchedField, QStringLiteral("filename"));
}

void SqliteRepositoryTest::ranksExactMatchesWithinMatchType()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Resource partialTitle;
    partialTitle.id = QStringLiteral("partial-title");
    partialTitle.kind = ResourceKind::File;
    partialTitle.title = QStringLiteral("UART Bringup");
    partialTitle.location = QStringLiteral("partial.md");
    QVERIFY2(repository.upsertResource(partialTitle), qPrintable(repository.lastError()));

    Resource exactTitle;
    exactTitle.id = QStringLiteral("exact-title");
    exactTitle.kind = ResourceKind::File;
    exactTitle.title = QStringLiteral("UART");
    exactTitle.location = QStringLiteral("exact.md");
    QVERIFY2(repository.upsertResource(exactTitle), qPrintable(repository.lastError()));

    const QList<SearchResult> titleResults = repository.search(SearchQuery{QStringLiteral("UART")});
    QCOMPARE(titleResults.size(), 2);
    QCOMPARE(titleResults.first().resource.id, exactTitle.id);
    QCOMPARE(titleResults.first().matchedField, QStringLiteral("title"));
    QVERIFY(titleResults.first().score < titleResults.at(1).score);

    Resource partialAnchor;
    partialAnchor.id = QStringLiteral("partial-anchor");
    partialAnchor.kind = ResourceKind::File;
    partialAnchor.title = QStringLiteral("a.md");
    partialAnchor.location = QStringLiteral("a.md");
    partialAnchor.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Power sequencing"), 7}};
    QVERIFY2(repository.upsertResource(partialAnchor), qPrintable(repository.lastError()));

    Resource exactAnchor;
    exactAnchor.id = QStringLiteral("exact-anchor");
    exactAnchor.kind = ResourceKind::File;
    exactAnchor.title = QStringLiteral("b.md");
    exactAnchor.location = QStringLiteral("b.md");
    exactAnchor.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Power"), 3}};
    QVERIFY2(repository.upsertResource(exactAnchor), qPrintable(repository.lastError()));

    const QList<SearchResult> anchorResults = repository.search(SearchQuery{QStringLiteral("Power")});
    QVERIFY(anchorResults.size() >= 2);
    QCOMPARE(anchorResults.first().resource.id, exactAnchor.id);
    QCOMPARE(anchorResults.first().matchedField, QStringLiteral("anchor"));
    QVERIFY(anchorResults.first().matchedAnchor.has_value());
    QVERIFY(anchorResults.first().score < anchorResults.at(1).score);
}

void SqliteRepositoryTest::tracksUsageAndRanksRecallSignals()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Resource cold;
    cold.id = QStringLiteral("cold");
    cold.kind = ResourceKind::File;
    cold.title = QStringLiteral("UART Alpha");
    cold.location = QStringLiteral("alpha.md");
    QVERIFY2(repository.upsertResource(cold), qPrintable(repository.lastError()));

    Resource hot;
    hot.id = QStringLiteral("hot");
    hot.kind = ResourceKind::File;
    hot.title = QStringLiteral("UART Zulu");
    hot.location = QStringLiteral("zulu.md");
    QVERIFY2(repository.upsertResource(hot), qPrintable(repository.lastError()));

    QVERIFY2(repository.recordResourceOpen(hot.id), qPrintable(repository.lastError()));
    QVERIFY2(repository.recordResourceOpen(hot.id), qPrintable(repository.lastError()));
    QVERIFY2(repository.setResourcePinned(hot.id, true), qPrintable(repository.lastError()));

    const std::optional<ResourceUsage> usage = repository.resourceUsage(hot.id);
    QVERIFY(usage.has_value());
    QCOMPARE(usage->openCount, 2);
    QVERIFY(usage->lastOpenedAt.isValid());
    QVERIFY(usage->pinned);

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("UART")});
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, hot.id);
    QCOMPARE(results.first().matchedField, QStringLiteral("title"));

    QVERIFY2(repository.setResourcePinned(hot.id, false), qPrintable(repository.lastError()));
    const std::optional<ResourceUsage> unpinnedUsage = repository.resourceUsage(hot.id);
    QVERIFY(unpinnedUsage.has_value());
    QVERIFY(!unpinnedUsage->pinned);

    QVERIFY2(repository.setResourcePinned(hot.id, true), qPrintable(repository.lastError()));
    hot.title = QStringLiteral("UART Zulu Updated");
    QVERIFY2(repository.upsertResource(hot), qPrintable(repository.lastError()));
    const std::optional<ResourceUsage> preservedUsage = repository.resourceUsage(hot.id);
    QVERIFY(preservedUsage.has_value());
    QCOMPARE(preservedUsage->openCount, 2);
    QVERIFY(preservedUsage->pinned);
}

void SqliteRepositoryTest::filtersByRequiredLocationPrefixes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Resource project;
    project.id = QStringLiteral("project");
    project.kind = ResourceKind::File;
    project.title = QStringLiteral("UART Project Note");
    project.location = QStringLiteral("E:/workspace/project/notes/uart.md");
    project.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Dock handoff"), 9}};
    QVERIFY2(repository.upsertResource(project), qPrintable(repository.lastError()));

    Resource other;
    other.id = QStringLiteral("other");
    other.kind = ResourceKind::File;
    other.title = QStringLiteral("UART Other Note");
    other.location = QStringLiteral("E:/workspace/other/uart.md");
    other.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Dock handoff"), 4}};
    QVERIFY2(repository.upsertResource(other), qPrintable(repository.lastError()));

    SearchQuery query;
    query.text = QStringLiteral("UART");
    query.requiredLocationPrefixes = {QStringLiteral("E:/workspace/project")};

    const QList<SearchResult> results = repository.search(query);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().resource.id, project.id);

    SearchQuery anchorQuery;
    anchorQuery.text = QStringLiteral("Dock handoff");
    anchorQuery.requiredLocationPrefixes = {QStringLiteral("E:/workspace/project")};

    const QList<SearchResult> anchorResults = repository.search(anchorQuery);
    QCOMPARE(anchorResults.size(), 1);
    QCOMPARE(anchorResults.first().resource.id, project.id);
    QVERIFY(anchorResults.first().matchedAnchor.has_value());

    query.requiredLocationPrefixes = {QStringLiteral("E:/workspace/missing")};
    QVERIFY(repository.search(query).isEmpty());
}

void SqliteRepositoryTest::filtersByRequiredResourceKinds()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Resource note;
    note.id = QStringLiteral("note");
    note.kind = ResourceKind::File;
    note.title = QStringLiteral("UART Note");
    note.location = QStringLiteral("note.md");
    note.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Dock handoff"), 4}};
    QVERIFY2(repository.upsertResource(note), qPrintable(repository.lastError()));

    Resource link;
    link.id = QStringLiteral("link");
    link.kind = ResourceKind::Url;
    link.title = QStringLiteral("UART Link");
    link.location = QStringLiteral("https://docs.example.com/uart#handoff");
    link.anchors = {Anchor{AnchorType::UrlFragment, QStringLiteral("Dock handoff")}};
    QVERIFY2(repository.upsertResource(link), qPrintable(repository.lastError()));

    SearchQuery query;
    query.text = QStringLiteral("UART");
    query.requiredKinds = {ResourceKind::Url};

    const QList<SearchResult> results = repository.search(query);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().resource.id, link.id);

    SearchQuery anchorQuery;
    anchorQuery.text = QStringLiteral("Dock handoff");
    anchorQuery.requiredKinds = {ResourceKind::File};

    const QList<SearchResult> anchorResults = repository.search(anchorQuery);
    QCOMPARE(anchorResults.size(), 1);
    QCOMPARE(anchorResults.first().resource.id, note.id);
    QVERIFY(anchorResults.first().matchedAnchor.has_value());

    anchorQuery.requiredKinds = {ResourceKind::Markdown};
    const QList<SearchResult> legacyKindAliasResults = repository.search(anchorQuery);
    QCOMPARE(legacyKindAliasResults.size(), 1);
    QCOMPARE(legacyKindAliasResults.first().resource.id, note.id);
}

void SqliteRepositoryTest::ranksContextSignalsWithinMatchType()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Resource generic;
    generic.id = QStringLiteral("generic");
    generic.kind = ResourceKind::File;
    generic.title = QStringLiteral("UART Alpha");
    generic.location = QStringLiteral("E:/workspace/other/alpha.md");
    generic.tags = {QStringLiteral("notes")};
    QVERIFY2(repository.upsertResource(generic), qPrintable(repository.lastError()));

    Resource contextual;
    contextual.id = QStringLiteral("contextual");
    contextual.kind = ResourceKind::File;
    contextual.title = QStringLiteral("UART Zulu");
    contextual.location = QStringLiteral("E:/workspace/project/zulu.md");
    contextual.tags = {QStringLiteral("pcie")};
    QVERIFY2(repository.upsertResource(contextual), qPrintable(repository.lastError()));

    SearchQuery query;
    query.text = QStringLiteral("UART");
    query.contextTags = {QStringLiteral("pcie")};
    query.contextLocationPrefixes = {QStringLiteral("E:/workspace/project")};

    const QList<SearchResult> results = repository.search(query);
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, contextual.id);
    QCOMPARE(results.first().matchedField, QStringLiteral("title"));
}

void SqliteRepositoryTest::ranksRelatedContextResourcesWithinMatchType()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Resource active;
    active.id = QStringLiteral("active");
    active.kind = ResourceKind::File;
    active.title = QStringLiteral("Current Note");
    active.location = QStringLiteral("E:/workspace/current.md");
    QVERIFY2(repository.upsertResource(active), qPrintable(repository.lastError()));

    Resource generic;
    generic.id = QStringLiteral("generic");
    generic.kind = ResourceKind::File;
    generic.title = QStringLiteral("UART Alpha");
    generic.location = QStringLiteral("E:/workspace/other/alpha.md");
    QVERIFY2(repository.upsertResource(generic), qPrintable(repository.lastError()));

    Resource related;
    related.id = QStringLiteral("related");
    related.kind = ResourceKind::File;
    related.title = QStringLiteral("UART Zulu");
    related.location = QStringLiteral("E:/workspace/project/zulu.md");
    QVERIFY2(repository.upsertResource(related), qPrintable(repository.lastError()));

    ResourceRelation relation;
    relation.sourceResourceId = active.id;
    relation.targetResourceId = related.id;
    relation.label = QStringLiteral("related-to");
    relation.note = QStringLiteral("active relation edge");
    QVERIFY2(repository.upsertResourceRelation(relation), qPrintable(repository.lastError()));

    SearchQuery query;
    query.text = QStringLiteral("UART");
    QList<SearchResult> results = repository.search(query);
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, generic.id);

    query.contextResourceIds = {active.id};
    results = repository.search(query);
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, related.id);
    QCOMPARE(results.first().matchedContextResourceId, active.id);
    QCOMPARE(results.first().matchedContextRelationLabel, QStringLiteral("related-to"));
    QCOMPARE(results.first().matchedContextRelationNote, QStringLiteral("active relation edge"));

    query.contextRelationLabels = {QStringLiteral("file-reference")};
    results = repository.search(query);
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, generic.id);
    QVERIFY(results.first().matchedContextRelationLabel.isEmpty());
    QVERIFY(results.first().matchedContextRelationNote.isEmpty());

    query.contextRelationLabels = {QStringLiteral("RELATED-TO")};
    results = repository.search(query);
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, related.id);
    QCOMPARE(results.first().matchedContextRelationLabel, QStringLiteral("related-to"));
    QCOMPARE(results.first().matchedContextRelationNote, QStringLiteral("active relation edge"));
}

void SqliteRepositoryTest::ranksPinnedLibraryRootSignalsWithinMatchType()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    LibraryRoot coldRoot = makeLibraryRootForPath(QStringLiteral("E:/workspace/cold"));
    LibraryRoot hotRoot = makeLibraryRootForPath(QStringLiteral("E:/workspace/hot"));
    hotRoot.pinned = true;
    QVERIFY2(repository.upsertLibraryRoot(coldRoot), qPrintable(repository.lastError()));
    QVERIFY2(repository.upsertLibraryRoot(hotRoot), qPrintable(repository.lastError()));

    Resource cold;
    cold.id = QStringLiteral("cold-note");
    cold.kind = ResourceKind::File;
    cold.title = QStringLiteral("Bringup Checklist");
    cold.location = QStringLiteral("E:/workspace/cold/bringup.md");
    QVERIFY2(repository.upsertResource(cold), qPrintable(repository.lastError()));

    Resource hot;
    hot.id = QStringLiteral("hot-note");
    hot.kind = ResourceKind::File;
    hot.title = QStringLiteral("Bringup Checklist");
    hot.location = QStringLiteral("E:/workspace/hot/bringup.md");
    QVERIFY2(repository.upsertResource(hot), qPrintable(repository.lastError()));

    const QList<SearchResult> pinnedResults = repository.search(SearchQuery{QStringLiteral("Bringup")});
    QCOMPARE(pinnedResults.size(), 2);
    QCOMPARE(pinnedResults.first().resource.id, hot.id);

    QVERIFY2(repository.setLibraryRootPinned(hotRoot.id, false), qPrintable(repository.lastError()));
    QVERIFY2(repository.setLibraryRootPinned(coldRoot.id, true), qPrintable(repository.lastError()));
    const QList<SearchResult> repinnedResults = repository.search(SearchQuery{QStringLiteral("Bringup")});
    QCOMPARE(repinnedResults.size(), 2);
    QCOMPARE(repinnedResults.first().resource.id, cold.id);
}

void SqliteRepositoryTest::tracksAnchorUsageAndRanksAnchorRecall()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Resource cold;
    cold.id = QStringLiteral("cold-anchor");
    cold.kind = ResourceKind::File;
    cold.title = QStringLiteral("Alpha");
    cold.location = QStringLiteral("alpha.md");
    cold.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Power rail"), 1}};
    QVERIFY2(repository.upsertResource(cold), qPrintable(repository.lastError()));

    Resource hot;
    hot.id = QStringLiteral("hot-anchor");
    hot.kind = ResourceKind::File;
    hot.title = QStringLiteral("Zulu");
    hot.location = QStringLiteral("zulu.md");
    hot.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Power rail"), 2}};
    QVERIFY2(repository.upsertResource(hot), qPrintable(repository.lastError()));

    QVERIFY2(repository.recordAnchorOpen(hot.id, hot.anchors.first()), qPrintable(repository.lastError()));
    QVERIFY2(repository.recordAnchorOpen(hot.id, hot.anchors.first()), qPrintable(repository.lastError()));

    const std::optional<AnchorUsage> usage = repository.anchorUsage(hot.id, hot.anchors.first());
    QVERIFY(usage.has_value());
    QCOMPARE(usage->openCount, 2);
    QVERIFY(usage->lastOpenedAt.isValid());
    QCOMPARE(usage->anchor.line, 2);

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("Power")});
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, hot.id);
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(results.first().matchedAnchor->line, 2);

    hot.title = QStringLiteral("Zulu Updated");
    QVERIFY2(repository.upsertResource(hot), qPrintable(repository.lastError()));
    const std::optional<AnchorUsage> preservedUsage = repository.anchorUsage(hot.id, hot.anchors.first());
    QVERIFY(preservedUsage.has_value());
    QCOMPARE(preservedUsage->openCount, 2);
}

void SqliteRepositoryTest::managesResourceRelations()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Resource source;
    source.id = QStringLiteral("note");
    source.kind = ResourceKind::File;
    source.title = QStringLiteral("Bringup Note");
    source.location = QStringLiteral("note.md");
    QVERIFY2(repository.upsertResource(source), qPrintable(repository.lastError()));

    Resource target;
    target.id = QStringLiteral("spec");
    target.kind = ResourceKind::Pdf;
    target.title = QStringLiteral("PCIe Spec");
    target.location = QStringLiteral("spec.pdf");
    QVERIFY2(repository.upsertResource(target), qPrintable(repository.lastError()));

    ResourceRelation relation;
    relation.sourceResourceId = source.id;
    relation.targetResourceId = target.id;
    relation.label = QStringLiteral("related-to");
    relation.note = QStringLiteral("chapter 7");
    QVERIFY2(repository.upsertResourceRelation(relation), qPrintable(repository.lastError()));

    QList<ResourceRelation> sourceRelations = repository.resourceRelations(source.id);
    QCOMPARE(sourceRelations.size(), 1);
    QCOMPARE(sourceRelations.first().sourceResourceId, source.id);
    QCOMPARE(sourceRelations.first().targetResourceId, target.id);
    QCOMPARE(sourceRelations.first().label, QStringLiteral("related-to"));
    QCOMPARE(sourceRelations.first().note, QStringLiteral("chapter 7"));

    relation.note = QStringLiteral("chapter 8");
    QVERIFY2(repository.upsertResourceRelation(relation), qPrintable(repository.lastError()));
    sourceRelations = repository.resourceRelations(target.id);
    QCOMPARE(sourceRelations.size(), 1);
    QCOMPARE(sourceRelations.first().note, QStringLiteral("chapter 8"));
    QCOMPARE(repository.allResourceRelations().size(), 1);

    source.title = QStringLiteral("Bringup Note Updated");
    QVERIFY2(repository.upsertResource(source), qPrintable(repository.lastError()));
    QCOMPARE(repository.resourceRelations(source.id).size(), 1);

    QVERIFY2(repository.removeResourceRelation(source.id, target.id, relation.label), qPrintable(repository.lastError()));
    QVERIFY(repository.resourceRelations(source.id).isEmpty());
}

void SqliteRepositoryTest::managesLibraryRoots()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    LibraryRoot root = makeLibraryRootForPath(dir.filePath(QStringLiteral("library")));
    QVERIFY2(repository.upsertLibraryRoot(root), qPrintable(repository.lastError()));
    QVERIFY2(repository.upsertLibraryRoot(root), qPrintable(repository.lastError()));
    QCOMPARE(repository.libraryRoots().size(), 1);

    QVERIFY2(repository.setLibraryRootEnabled(root.id, false), qPrintable(repository.lastError()));
    std::optional<LibraryRoot> stored = repository.findLibraryRoot(root.id);
    QVERIFY(stored.has_value());
    QVERIFY(!stored->enabled);

    QVERIFY2(repository.setLibraryRootPinned(root.id, true), qPrintable(repository.lastError()));
    stored = repository.findLibraryRoot(root.id);
    QVERIFY(stored.has_value());
    QVERIFY(stored->pinned);
    QCOMPARE(repository.libraryRoots().first().id, root.id);

    const QDateTime indexedAt = QDateTime::currentDateTimeUtc();
    QVERIFY2(repository.updateLibraryRootLastIndexedAt(root.id, indexedAt), qPrintable(repository.lastError()));
    stored = repository.findLibraryRoot(root.id);
    QVERIFY(stored.has_value());
    QVERIFY(stored->lastIndexedAt.isValid());

    QVERIFY2(repository.removeLibraryRoot(root.id), qPrintable(repository.lastError()));
    QVERIFY(repository.libraryRoots().isEmpty());
}

void SqliteRepositoryTest::upgradesVersionOneDatabase()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString databasePath = dir.filePath(QStringLiteral("pinloom.sqlite3"));
    createVersionOneDatabase(databasePath);

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    QVERIFY(repository.libraryRoots().isEmpty());
    const std::optional<Resource> legacy = repository.findResource(QStringLiteral("legacy"));
    QVERIFY(legacy.has_value());
    QCOMPARE(legacy->title, QStringLiteral("Legacy Note"));
    QCOMPARE(legacy->kind, ResourceKind::File);

    LibraryRoot root = makeLibraryRootForPath(dir.path());
    QVERIFY2(repository.upsertLibraryRoot(root), qPrintable(repository.lastError()));
    QCOMPARE(repository.libraryRoots().size(), 1);
}

void SqliteRepositoryTest::upgradesVersionTwoDatabaseWithRoots()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString databasePath = dir.filePath(QStringLiteral("pinloom.sqlite3"));
    createVersionTwoDatabase(databasePath);

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    QCOMPARE(repository.libraryRoots().size(), 1);
    QVERIFY(!repository.libraryRoots().first().pinned);
    QVERIFY2(repository.setLibraryRootPinned(QStringLiteral("dir:test"), true), qPrintable(repository.lastError()));
    QCOMPARE(repository.libraryRoots().first().pinned, true);
    const std::optional<Resource> legacy = repository.findResource(QStringLiteral("legacy"));
    QVERIFY(legacy.has_value());
    QCOMPARE(legacy->kind, ResourceKind::File);

    Resource resource;
    resource.id = QStringLiteral("anchored");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Anchored Note");
    resource.location = QStringLiteral("anchored.md");
    resource.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Deep Link"), 7}};
    QVERIFY2(repository.upsertResource(resource), qPrintable(repository.lastError()));

    ResourceRelation relation;
    relation.sourceResourceId = QStringLiteral("legacy");
    relation.targetResourceId = resource.id;
    relation.label = QStringLiteral("relates");
    relation.note = QStringLiteral("upgrade path");
    QVERIFY2(repository.upsertResourceRelation(relation), qPrintable(repository.lastError()));
    QCOMPARE(repository.resourceRelations(QStringLiteral("legacy")).size(), 1);

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("deep")});
    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(results.first().matchedAnchor->line, 7);
}

void SqliteRepositoryTest::upgradesVersionSevenDatabaseWithLegacyAnchors()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString databasePath = dir.filePath(QStringLiteral("pinloom.sqlite3"));
    createVersionSevenDatabaseWithLegacyAnchor(databasePath);

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    const std::optional<Resource> legacy = repository.findResource(QStringLiteral("legacy"));
    QVERIFY(legacy.has_value());
    QCOMPARE(legacy->kind, ResourceKind::File);
    QCOMPARE(legacy->anchors.size(), 1);
    QCOMPARE(legacy->anchors.first().type, AnchorType::TextHeading);
    QCOMPARE(legacy->anchors.first().target, QStringLiteral("Legacy Jump"));
    QCOMPARE(legacy->anchors.first().line, 17);
    QCOMPARE(legacy->anchors.first().id, QStringLiteral("legacy#anchor-0"));
    QCOMPARE(legacy->anchors.first().targetFile, QStringLiteral("legacy.md"));
    QCOMPARE(legacy->anchors.first().locatorType, QStringLiteral("text.heading"));
    QVERIFY(!legacy->anchors.first().locatorJson.isEmpty());

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("Legacy Jump")});
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().resource.id, QStringLiteral("legacy"));
    QCOMPARE(results.first().matchedField, QStringLiteral("anchor"));
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(results.first().matchedAnchor->line, 17);

    const QString rawConnectionName =
        QStringLiteral("pinloom_raw_migration_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    QSqlDatabase rawDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), rawConnectionName);
    rawDatabase.setDatabaseName(databasePath);
    QVERIFY(rawDatabase.open());
    QSqlQuery rawQuery(rawDatabase);
    QVERIFY(rawQuery.exec(QStringLiteral("PRAGMA table_info(anchors)")));
    QStringList columns;
    while (rawQuery.next()) {
        columns.append(rawQuery.value(1).toString());
    }
    QVERIFY(columns.contains(QStringLiteral("locator_json")));
    QVERIFY(columns.contains(QStringLiteral("target_app")));
    QVERIFY(columns.contains(QStringLiteral("used_at")));
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT COUNT(*) FROM schema_migrations WHERE version = 8")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toInt(), 1);
    rawQuery = QSqlQuery();
    rawDatabase.close();
    rawDatabase = QSqlDatabase();
    QSqlDatabase::removeDatabase(rawConnectionName);
}

QTEST_MAIN(SqliteRepositoryTest)

#include "sqlite_repository_test.moc"
