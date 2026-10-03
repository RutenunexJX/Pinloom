#include "pinloom/core/SqliteLibraryRepository.h"
#include "pinloom/core/AnchorLibraryArchive.h"
#include "pinloom/core/AnchorLibraryManagement.h"
#include "pinloom/core/AnchorLibraryPolicy.h"
#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/InboxFileCapture.h"
#include "pinloom/core/LibraryRoot.h"
#include "pinloom/core/Schema.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
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
    void assignsStableAnchorIdsIndependentOfOrderStorage();
    void persistsAndSearchesResourceMetadata();
    void persistsAndSearchesAnchorLocatorFields();
    void persistsAndSearchesInboxFiles();
    void persistsLibraryRootsAndProtectsSyncRoot();
    void cleansUnmarkedAnchorShellsWithoutDeletingFilesOrRoots();
    void ranksAnchorAndFilenameMatchesBeforePathNoise();
    void ranksExactMatchesWithinMatchType();
    void tracksUsageAndRanksRecallSignals();
    void searchHydratesEachResourceOncePerQuery();
    void searchObservesChangesBetweenQueries();
    void softDeletesAndRestoresResourcesAndAnchors();
    void persistsAnchorLibraryManagementOperations();
    void appliesAtomicBatchesAndCoalescesNotifications();
    void backsUpRestoresAndImportsAnchorLibraryData();
    void filtersByRequiredLocationPrefixes();
    void appliesSqlFiltersBeforeBoundedCandidateSelection();
    void filtersByRequiredResourceKinds();
    void ranksContextSignalsWithinMatchType();
    void tracksAnchorUsageAndRanksAnchorRecall();
    void upgradesVersionOneDatabase();
    void upgradesVersionTwoDatabaseWithRoots();
    void upgradesVersionSevenDatabaseWithLegacyAnchors();
    void upgradesConfiguredExistingDatabaseCopy();
};

static Anchor testAnchor(const QString &name,
                         const QString &locatorType = QStringLiteral("manual"),
                         int line = -1)
{
    Anchor anchor;
    anchor.name = name;
    anchor.locatorType = locatorType;
    QJsonObject locator{{QStringLiteral("type"), locatorType}};
    if (line > 0) {
        locator.insert(QStringLiteral("line"), line);
    }
    anchor.locatorJson =
        QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));
    return anchor;
}

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
    QVERIFY2(repository.integrityCheck(), qPrintable(repository.lastError()));
}

void SqliteRepositoryTest::assignsStableAnchorIdsIndependentOfOrderStorage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Resource resource;
    resource.id = QStringLiteral("stable-resource");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Stable anchors");
    resource.location = QStringLiteral("stable.txt");
    Anchor first = testAnchor(QStringLiteral("First"), QStringLiteral("file.line"), 10);
    Anchor second = testAnchor(QStringLiteral("Second"), QStringLiteral("file.line"), 20);
    first.id = QStringLiteral("duplicate-id");
    second.id = QStringLiteral("duplicate-id");
    resource.anchors = {first, second};

    QVERIFY2(repository.upsertResource(resource), qPrintable(repository.lastError()));
    const std::optional<Resource> stored = repository.findResource(resource.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->anchors.size(), 2);
    QCOMPARE(stored->anchors.first().id, QStringLiteral("duplicate-id"));
    QVERIFY(!stored->anchors.last().id.isEmpty());
    QVERIFY(stored->anchors.last().id != stored->anchors.first().id);

    Resource reordered = stored.value();
    std::reverse(reordered.anchors.begin(), reordered.anchors.end());
    QVERIFY2(repository.upsertResource(reordered), qPrintable(repository.lastError()));
    const std::optional<Resource> afterReorder = repository.findResource(resource.id);
    QVERIFY(afterReorder.has_value());
    QCOMPARE(afterReorder->anchors.first().id, stored->anchors.last().id);
    QCOMPARE(afterReorder->anchors.last().id, stored->anchors.first().id);
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
        testAnchor(QStringLiteral("Power sequencing"), QStringLiteral("text.heading")),
        testAnchor(QStringLiteral("power-block"), QStringLiteral("text.block"), 7),
        testAnchor(QStringLiteral("Debug checkpoint"), QStringLiteral("file.line"), 42),
    };

    QVERIFY2(repository.upsertResource(resource), qPrintable(repository.lastError()));

    const std::optional<Resource> stored = repository.findResource(resource.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->tags.size(), 2);
    QCOMPARE(stored->aliases.size(), 2);
    QCOMPARE(stored->anchors.size(), 3);
    QCOMPARE(stored->anchors.at(0).locatorType, QStringLiteral("text.heading"));
    QCOMPARE(stored->anchors.at(1).locatorType, QStringLiteral("text.block"));
    QCOMPARE(anchorLocatorLine(stored->anchors.at(2)), 42);

    QCOMPARE(repository.search(SearchQuery{QStringLiteral("bringup")}).size(), 1);
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("serial")}).size(), 1);

    const QList<SearchResult> anchorResults =
        repository.search(SearchQuery{QStringLiteral("Power sequencing")});
    QCOMPARE(anchorResults.size(), 1);
    QVERIFY(anchorResults.first().matchedAnchor.has_value());
    QCOMPARE(anchorResults.first().matchedAnchor->locatorType,
             QStringLiteral("text.heading"));

    const QList<SearchResult> lineResults =
        repository.search(SearchQuery{QStringLiteral("checkpoint")});
    QCOMPARE(lineResults.size(), 1);
    QVERIFY(lineResults.first().matchedAnchor.has_value());
    QCOMPARE(anchorLocatorLine(lineResults.first().matchedAnchor.value()), 42);

    Resource snippet;
    snippet.id = QStringLiteral("text-snippet");
    snippet.kind = ResourceKind::TextSnippet;
    snippet.title = QStringLiteral("Neutral Text Beacon");
    snippet.location = QStringLiteral("snippets/beacon.txt");
    snippet.anchors = {
        testAnchor(QStringLiteral("marker: neutral_beacon"), QStringLiteral("marker"), 3)};
    QVERIFY2(repository.upsertResource(snippet), qPrintable(repository.lastError()));

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
    anchor.id = QStringLiteral("anchor:clock-domain");
    anchor.name = QStringLiteral("Clock domain window");
    anchor.targetApp = QStringLiteral("SumatraPDF");
    anchor.targetFile = QStringLiteral("E:/specs/clocking.pdf");
    anchor.locatorType = QStringLiteral("sumatrapdf.rect");
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

    const QList<SearchResult> metadataResults = repository.search(SearchQuery{QStringLiteral("sumatrapdf.rect")});
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
    QVERIFY(stored->explicitlyRetained);
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

void SqliteRepositoryTest::persistsLibraryRootsAndProtectsSyncRoot()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("pinloom.sqlite3"));
    const QString syncPath = dir.filePath(QStringLiteral("PinloomRoot"));
    const QString customPath = dir.filePath(QStringLiteral("Reference"));
    QVERIFY(QDir().mkpath(syncPath));
    QVERIFY(QDir().mkpath(customPath));

    const LibraryRoot syncRoot = makeLibraryRootForPath(syncPath, true);
    LibraryRoot customRoot = makeLibraryRootForPath(customPath);
    customRoot.displayName = QStringLiteral("Reference library");
    customRoot.ignoredDirectoryNames = {QStringLiteral("build"), QStringLiteral("cache")};

    {
        SqliteLibraryRepository repository;
        QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
        QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));
        QVERIFY2(repository.upsertLibraryRoot(customRoot), qPrintable(repository.lastError()));
        QVERIFY2(repository.upsertLibraryRoot(syncRoot), qPrintable(repository.lastError()));

        const QList<LibraryRoot> roots = repository.libraryRoots();
        QCOMPARE(roots.size(), 2);
        QCOMPARE(roots.first().id, syncRoot.id);
        QVERIFY(roots.first().syncRoot);
        QVERIFY(!repository.removeLibraryRoot(syncRoot.id));
        QCOMPARE(repository.lastError(), QStringLiteral("The default sync root cannot be removed"));
    }

    {
        SqliteLibraryRepository repository;
        QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
        QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));
        const std::optional<LibraryRoot> reopened = repository.findLibraryRoot(customRoot.id);
        QVERIFY(reopened.has_value());
        QCOMPARE(reopened->path, normalizedLibraryRootPath(customPath));
        QCOMPARE(reopened->displayName, QStringLiteral("Reference library"));
        QCOMPARE(reopened->ignoredDirectoryNames,
                 (QStringList{QStringLiteral("build"), QStringLiteral("cache")}));
        QVERIFY(reopened->updatedAt.isValid());
        QVERIFY(repository.removeLibraryRoot(customRoot.id));
        QCOMPARE(repository.libraryRoots().size(), 1);
    }
}

void SqliteRepositoryTest::cleansUnmarkedAnchorShellsWithoutDeletingFilesOrRoots()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("pinloom.sqlite3"));
    const QString filePath = dir.filePath(QStringLiteral("original.pdf"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("pdf") > 0);
    file.close();
    const QString rootPath = dir.filePath(QStringLiteral("Reference"));
    QVERIFY(QDir().mkpath(rootPath));

    QString unmarkedId;
    QString rootResourceId;
    LibraryRoot root;
    {
        SqliteLibraryRepository repository;
        QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
        QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

        Resource unmarked;
        unmarked.id = inboxResourceIdForPath(filePath);
        unmarkedId = unmarked.id;
        unmarked.kind = ResourceKind::Pdf;
        unmarked.title = QStringLiteral("Internal shell");
        unmarked.location = filePath;
        Anchor fileAnchor = testAnchor(QStringLiteral("File anchor"));
        fileAnchor.id = unmarked.id + QStringLiteral("#anchor");
        fileAnchor.deleted = true;
        unmarked.anchors = {fileAnchor};
        QVERIFY2(repository.upsertResource(unmarked), qPrintable(repository.lastError()));

        root = makeLibraryRootForPath(rootPath);
        QVERIFY2(repository.upsertLibraryRoot(root), qPrintable(repository.lastError()));
        Resource rootResource;
        rootResource.id = inboxResourceIdForPath(rootPath);
        rootResourceId = rootResource.id;
        rootResource.kind = ResourceKind::Folder;
        rootResource.title = QStringLiteral("Root configuration");
        rootResource.location = rootPath;
        Anchor rootAnchor = testAnchor(QStringLiteral("Root anchor"));
        rootAnchor.id = rootResource.id + QStringLiteral("#anchor");
        rootAnchor.deleted = true;
        rootResource.anchors = {rootAnchor};
        QVERIFY2(repository.upsertResource(rootResource), qPrintable(repository.lastError()));

        AnchorLibraryManagementService service(repository);
        const AnchorLibraryOperationResult result = service.permanentlyDeleteAnchors(
            {{unmarked.id, fileAnchor}, {rootResource.id, rootAnchor}});
        QVERIFY2(result.success, qPrintable(result.message));
        QCOMPARE(result.affectedCount, 2);
        QVERIFY(!repository.findResource(unmarked.id).has_value());
        const std::optional<Resource> protectedRootResource =
            repository.findResource(rootResource.id);
        QVERIFY(protectedRootResource.has_value());
        QVERIFY(protectedRootResource->anchors.isEmpty());
        QVERIFY(!shouldProvideAnchorLibraryResource(
            protectedRootResource.value(), ResourceUsage{protectedRootResource->id}));
        QCOMPARE(repository.libraryRoots().size(), 1);
    }

    QVERIFY(QFileInfo::exists(filePath));
    SqliteLibraryRepository reopened;
    QVERIFY2(reopened.open(databasePath), qPrintable(reopened.lastError()));
    QVERIFY2(reopened.initialize(), qPrintable(reopened.lastError()));
    QVERIFY(!reopened.findResource(unmarkedId).has_value());
    const std::optional<Resource> protectedRootResource = reopened.findResource(rootResourceId);
    QVERIFY(protectedRootResource.has_value());
    QVERIFY(protectedRootResource->anchors.isEmpty());
    QCOMPARE(reopened.libraryRoots().size(), 1);
    QCOMPARE(reopened.libraryRoots().first().id, root.id);
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
    textFile.anchors = {
        testAnchor(QStringLiteral("test"), QStringLiteral("text.heading"), 143)};
    QVERIFY2(repository.upsertResource(textFile), qPrintable(repository.lastError()));

    Resource pdf;
    pdf.id = QStringLiteral("pdf");
    pdf.kind = ResourceKind::Pdf;
    pdf.title = QStringLiteral("ISO 11898-1.pdf");
    pdf.location = QStringLiteral("E:/test_dir/ISO 11898-1.pdf");
    QVERIFY2(repository.upsertResource(pdf), qPrintable(repository.lastError()));

    const QList<SearchResult> testResults = repository.search(SearchQuery{QStringLiteral("test")});
    QVERIFY(testResults.size() >= 3);
    QCOMPARE(testResults.first().matchedField, QStringLiteral("anchor_name"));
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
    partialAnchor.anchors = {
        testAnchor(QStringLiteral("Power sequencing"), QStringLiteral("text.heading"), 7)};
    QVERIFY2(repository.upsertResource(partialAnchor), qPrintable(repository.lastError()));

    Resource exactAnchor;
    exactAnchor.id = QStringLiteral("exact-anchor");
    exactAnchor.kind = ResourceKind::File;
    exactAnchor.title = QStringLiteral("b.md");
    exactAnchor.location = QStringLiteral("b.md");
    exactAnchor.anchors = {
        testAnchor(QStringLiteral("Power"), QStringLiteral("text.heading"), 3)};
    QVERIFY2(repository.upsertResource(exactAnchor), qPrintable(repository.lastError()));

    const QList<SearchResult> anchorResults = repository.search(SearchQuery{QStringLiteral("Power")});
    QVERIFY(anchorResults.size() >= 2);
    QCOMPARE(anchorResults.first().resource.id, exactAnchor.id);
    QCOMPARE(anchorResults.first().matchedField, QStringLiteral("anchor_name"));
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

static void compareSearchAnchor(const Anchor &actual, const Anchor &expected)
{
    QCOMPARE(actual.id, expected.id);
    QCOMPARE(actual.name, expected.name);
    QCOMPARE(actual.targetApp, expected.targetApp);
    QCOMPARE(actual.targetFile, expected.targetFile);
    QCOMPARE(actual.targetUri, expected.targetUri);
    QCOMPARE(actual.locatorType, expected.locatorType);
    QCOMPARE(actual.locatorJson, expected.locatorJson);
    QCOMPARE(actual.aliases, expected.aliases);
    QCOMPARE(actual.tags, expected.tags);
    QCOMPARE(actual.pinned, expected.pinned);
    QCOMPARE(actual.deleted, expected.deleted);
    QCOMPARE(actual.createdAt, expected.createdAt);
    QCOMPARE(actual.updatedAt, expected.updatedAt);
    QCOMPARE(actual.usedAt, expected.usedAt);
}

static void compareSearchResource(const Resource &actual, const Resource &expected)
{
    QCOMPARE(actual.id, expected.id);
    QCOMPARE(actual.kind, expected.kind);
    QCOMPARE(actual.title, expected.title);
    QCOMPARE(actual.location, expected.location);
    QCOMPARE(actual.aliases, expected.aliases);
    QCOMPARE(actual.tags, expected.tags);
    QCOMPARE(actual.explicitlyRetained, expected.explicitlyRetained);
    QCOMPARE(actual.deleted, expected.deleted);
    QCOMPARE(actual.updatedAt, expected.updatedAt);
    QCOMPARE(actual.anchors.size(), expected.anchors.size());
    for (qsizetype i = 0; i < actual.anchors.size(); ++i) {
        compareSearchAnchor(actual.anchors.at(i), expected.anchors.at(i));
    }
}

void SqliteRepositoryTest::searchHydratesEachResourceOncePerQuery()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SqliteLibraryRepository repository;
    QVERIFY(repository.open(dir.filePath(QStringLiteral("search.sqlite3"))));
    QVERIFY(repository.initialize());
    const QDateTime timestamp = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    QHash<QString, Resource> stored;
    for (int i = 0; i < 2; ++i) {
        Resource resource;
        resource.id = QStringLiteral("resource-%1").arg(i);
        resource.kind = ResourceKind::File;
        resource.title = QStringLiteral("Needle document %1").arg(i);
        resource.location = QStringLiteral("E:/fixtures/document-%1.txt").arg(i);
        resource.aliases = {QStringLiteral("document-alias-%1").arg(i)};
        resource.tags = {QStringLiteral("shared-tag"), QStringLiteral("tag-%1").arg(i)};
        resource.content = QStringLiteral("Needle resource content %1").arg(i);
        resource.explicitlyRetained = true;
        resource.updatedAt = timestamp;
        for (int j = 0; j < 4; ++j) {
            Anchor anchor = testAnchor(QStringLiteral("Needle point %1 %2").arg(i).arg(j),
                                       QStringLiteral("file.line"), j + 1);
            anchor.id = QStringLiteral("anchor-%1-%2").arg(i).arg(j);
            anchor.aliases = {QStringLiteral("point-alias-%1-%2").arg(i).arg(j)};
            anchor.tags = {QStringLiteral("point-tag-%1").arg(j)};
            anchor.targetApp = QStringLiteral("editor");
            anchor.targetFile = resource.location;
            anchor.createdAt = timestamp;
            anchor.updatedAt = timestamp;
            anchor.pinned = j == 0;
            resource.anchors.append(anchor);
        }
        QVERIFY2(repository.upsertResource(resource), qPrintable(repository.lastError()));
        // Exercise positive and missing usage rows in the same query.
        if (i == 1) {
            QVERIFY(repository.recordResourceOpen(resource.id));
            QVERIFY(repository.recordAnchorOpen(resource.id, resource.anchors.first()));
        }
        stored.insert(resource.id, repository.findResource(resource.id).value());
    }

    SearchQuery query{QStringLiteral("Needle")};
    query.limit = -1;
    SqliteSearchReadCounts reads{999, 999, 999};
    const QList<SearchResult> results = repository.search(query, &reads);
    QCOMPARE(results.size(), 10);
    QCOMPARE(reads.resourceHydrations, 2);
    QCOMPARE(reads.resourceUsageReads, 2);
    QCOMPARE(reads.anchorUsageReads, 8);
    int resourceResults = 0;
    int anchorResults = 0;
    for (const SearchResult &result : results) {
        const Resource expected = stored.value(result.resource.id);
        QVERIFY(!expected.id.isEmpty());
        compareSearchResource(result.resource, expected);
        if (result.matchedAnchor) {
            ++anchorResults;
            QCOMPARE(result.matchedField, QStringLiteral("anchor_name"));
            // FTS resource content must not contaminate the shared anchor snapshot.
            QVERIFY(result.resource.content.isEmpty());
            const auto anchor = std::find_if(expected.anchors.cbegin(), expected.anchors.cend(),
                                            [&](const Anchor &value) { return value.id == result.matchedAnchor->id; });
            QVERIFY(anchor != expected.anchors.cend());
            compareSearchAnchor(*result.matchedAnchor, *anchor);
        } else {
            ++resourceResults;
            QCOMPARE(result.matchedField, QStringLiteral("title"));
            QCOMPARE(result.resource.content,
                     QStringLiteral("Needle resource content %1").arg(result.resource.id.right(1)));
        }
    }
    QCOMPARE(resourceResults, 2);
    QCOMPARE(anchorResults, 8);

    query.limit = 3;
    const QList<SearchResult> limited = repository.search(query, &reads);
    QCOMPARE(limited.size(), 3);
    // Counters reset each call, and truncation still follows ranking.
    QCOMPARE(reads.resourceHydrations, 2);
    QCOMPARE(reads.resourceUsageReads, 2);
    QCOMPARE(reads.anchorUsageReads, 8);
    for (int i = 0; i < limited.size(); ++i) {
        QCOMPARE(limited.at(i).resource.id, results.at(i).resource.id);
        QCOMPARE(limited.at(i).matchedField, results.at(i).matchedField);
        QCOMPARE(limited.at(i).score, results.at(i).score);
        QVERIFY(limited.at(i).matchedAnchor.has_value());
        QVERIFY(results.at(i).matchedAnchor.has_value());
        QCOMPARE(limited.at(i).matchedAnchor->id, results.at(i).matchedAnchor->id);
    }
}

void SqliteRepositoryTest::searchObservesChangesBetweenQueries()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SqliteLibraryRepository repository;
    QVERIFY(repository.open(dir.filePath(QStringLiteral("fresh.sqlite3"))));
    QVERIFY(repository.initialize());
    Resource resource;
    resource.id = QStringLiteral("fresh-resource");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Needle document");
    resource.location = QStringLiteral("E:/fixtures/fresh.txt");
    Anchor anchor = testAnchor(QStringLiteral("Needle point"), QStringLiteral("file.line"), 7);
    anchor.id = QStringLiteral("fresh-anchor");
    resource.anchors = {anchor};
    QVERIFY(repository.upsertResource(resource));
    SearchQuery query{QStringLiteral("Needle")};
    SqliteSearchReadCounts reads;
    const auto initial = repository.search(query, &reads);
    QCOMPARE(initial.size(), 2);
    QCOMPARE(reads.resourceHydrations, 1);
    QVERIFY(initial.first().matchedAnchor.has_value());
    const double originalAnchorScore = initial.first().score;
    QVERIFY(repository.setResourcePinned(resource.id, true));
    const auto pinned = repository.search(query, &reads);
    QCOMPARE(pinned.size(), 2);
    QCOMPARE(reads.resourceUsageReads, 1);
    QVERIFY(pinned.first().score < originalAnchorScore);
    QVERIFY(repository.setResourcePinned(resource.id, false));
    const auto unpinned = repository.search(query, &reads);
    QCOMPARE(unpinned.size(), 2);
    QCOMPARE(unpinned.first().score, originalAnchorScore);
    QVERIFY(repository.recordResourceOpen(resource.id));
    const auto opened = repository.search(query, &reads);
    QCOMPARE(opened.size(), 2);
    QVERIFY(opened.first().score < unpinned.first().score);
    QVERIFY(repository.recordAnchorOpen(resource.id, anchor));
    const auto anchorOpened = repository.search(query, &reads);
    QCOMPARE(anchorOpened.size(), 2);
    QVERIFY(anchorOpened.first().score < opened.first().score);
    QVERIFY(anchorOpened.first().matchedAnchor.has_value());
    QVERIFY(anchorOpened.first().matchedAnchor->usedAt.isValid());
    QCOMPARE(reads.resourceHydrations, 1);
    QCOMPARE(reads.anchorUsageReads, 1);

    resource = repository.findResource(resource.id).value();
    resource.title = QStringLiteral("Needle revised document");
    resource.aliases = {QStringLiteral("fresh-alias")};
    resource.tags = {QStringLiteral("fresh-tag")};
    resource.anchors[0].name = QStringLiteral("Needle revised point");
    resource.anchors[0].aliases = {QStringLiteral("fresh-point-alias")};
    resource.anchors[0].tags = {QStringLiteral("fresh-point-tag")};
    resource.anchors[0].pinned = true;
    QVERIFY(repository.upsertResource(resource));
    const Resource edited = repository.findResource(resource.id).value();
    const auto afterEdit = repository.search(query, &reads);
    QCOMPARE(afterEdit.size(), 2);
    for (const SearchResult &result : afterEdit) compareSearchResource(result.resource, edited);
    QVERIFY(afterEdit.first().matchedAnchor.has_value());
    compareSearchAnchor(*afterEdit.first().matchedAnchor, edited.anchors.first());

    QVERIFY(repository.softDeleteAnchor(resource.id, edited.anchors.first()));
    QCOMPARE(repository.search(query, &reads).size(), 1);
    SearchQuery trash = query;
    trash.deletedOnly = true;
    auto deleted = repository.search(trash, &reads);
    QCOMPARE(deleted.size(), 1);
    QVERIFY(deleted.first().matchedAnchor.has_value());
    QVERIFY(deleted.first().matchedAnchor->deleted);
    QVERIFY(!deleted.first().resource.deleted);
    QCOMPARE(reads.resourceHydrations, 1);
    trash.text.clear();
    deleted = repository.search(trash, &reads);
    QCOMPARE(deleted.size(), 1);
    QCOMPARE(deleted.first().matchedField, QStringLiteral("anchor"));
    QVERIFY(deleted.first().matchedAnchor.has_value());
    QVERIFY(deleted.first().matchedAnchor->deleted);
    QVERIFY(repository.restoreAnchor(resource.id, edited.anchors.first()));
    QCOMPARE(repository.search(query, &reads).size(), 2);
    QVERIFY(repository.softDeleteResource(resource.id));
    QVERIFY(repository.search(query, &reads).isEmpty());
    QCOMPARE(reads.resourceHydrations, 0);
    trash.text = query.text;
    deleted = repository.search(trash, &reads);
    QCOMPARE(deleted.size(), 1);
    QVERIFY(deleted.first().resource.deleted);
    QVERIFY(!deleted.first().matchedAnchor.has_value());
    SearchQuery all = query;
    all.includeDeleted = true;
    QCOMPARE(repository.search(all, &reads).size(), 2);
    QCOMPARE(reads.resourceHydrations, 1);
    QVERIFY(repository.restoreResource(resource.id));
    QCOMPARE(repository.search(query, &reads).size(), 2);
}

void SqliteRepositoryTest::softDeletesAndRestoresResourcesAndAnchors()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Anchor anchor;
    anchor.id = QStringLiteral("note#anchor");
    anchor.name = QStringLiteral("Soft delete anchor");
    anchor.locatorType = QStringLiteral("text.heading");
    anchor.aliases = {QStringLiteral("anchor alias")};

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Resource Sentinel");
    resource.location = QStringLiteral("E:/docs/plain.md");
    resource.anchors = {anchor};
    QVERIFY2(repository.upsertResource(resource), qPrintable(repository.lastError()));

    QCOMPARE(repository.search(SearchQuery{QStringLiteral("anchor alias")}).size(), 1);
    QVERIFY2(repository.softDeleteAnchor(resource.id, anchor), qPrintable(repository.lastError()));
    QVERIFY(repository.search(SearchQuery{QStringLiteral("anchor alias")}).isEmpty());

    SearchQuery deletedAnchorQuery{QStringLiteral("anchor alias")};
    deletedAnchorQuery.includeDeleted = true;
    const QList<SearchResult> deletedAnchorResults = repository.search(deletedAnchorQuery);
    QCOMPARE(deletedAnchorResults.size(), 1);
    QVERIFY(deletedAnchorResults.first().matchedAnchor.has_value());
    QVERIFY(deletedAnchorResults.first().matchedAnchor->deleted);

    SearchQuery deletedAnchorOnlyQuery{QStringLiteral("anchor alias")};
    deletedAnchorOnlyQuery.deletedOnly = true;
    const QList<SearchResult> deletedAnchorOnlyResults = repository.search(deletedAnchorOnlyQuery);
    QCOMPARE(deletedAnchorOnlyResults.size(), 1);
    QVERIFY(deletedAnchorOnlyResults.first().matchedAnchor.has_value());
    QVERIFY(deletedAnchorOnlyResults.first().matchedAnchor->deleted);

    SearchQuery allDeletedAnchorsQuery;
    allDeletedAnchorsQuery.deletedOnly = true;
    allDeletedAnchorsQuery.limit = 0;
    const QList<SearchResult> allDeletedAnchorResults = repository.search(allDeletedAnchorsQuery);
    QCOMPARE(allDeletedAnchorResults.size(), 1);
    QVERIFY(allDeletedAnchorResults.first().matchedAnchor.has_value());
    QVERIFY(allDeletedAnchorResults.first().matchedAnchor->deleted);

    QVERIFY2(repository.restoreAnchor(resource.id, anchor), qPrintable(repository.lastError()));
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("anchor alias")}).size(), 1);

    QVERIFY2(repository.softDeleteResource(resource.id), qPrintable(repository.lastError()));
    QVERIFY(repository.search(SearchQuery{QStringLiteral("Resource Sentinel")}).isEmpty());
    const std::optional<Resource> deletedResource = repository.findResource(resource.id);
    QVERIFY(deletedResource.has_value());
    QVERIFY(deletedResource->deleted);

    SearchQuery deletedResourceQuery{QStringLiteral("Resource Sentinel")};
    deletedResourceQuery.includeDeleted = true;
    const QList<SearchResult> deletedResourceResults = repository.search(deletedResourceQuery);
    QCOMPARE(deletedResourceResults.size(), 1);
    QVERIFY(deletedResourceResults.first().resource.deleted);

    SearchQuery deletedResourceOnlyQuery{QStringLiteral("Resource Sentinel")};
    deletedResourceOnlyQuery.deletedOnly = true;
    const QList<SearchResult> deletedResourceOnlyResults = repository.search(deletedResourceOnlyQuery);
    QCOMPARE(deletedResourceOnlyResults.size(), 1);
    QVERIFY(deletedResourceOnlyResults.first().resource.deleted);
    QVERIFY(!deletedResourceOnlyResults.first().matchedAnchor.has_value());

    QVERIFY2(repository.restoreResource(resource.id), qPrintable(repository.lastError()));
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("Resource Sentinel")}).size(), 1);
    const std::optional<Resource> restored = repository.findResource(resource.id);
    QVERIFY(restored.has_value());
    QVERIFY(!restored->deleted);
}

void SqliteRepositoryTest::persistsAnchorLibraryManagementOperations()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString originalPath = dir.filePath(QStringLiteral("original.pdf"));
    const QString replacementPath = dir.filePath(QStringLiteral("replacement.pdf"));
    for (const QString &path : {originalPath, replacementPath}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("pdf") > 0);
    }

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Anchor firstAnchor = testAnchor(QStringLiteral("First anchor"), QStringLiteral("sumatrapdf.page"));
    firstAnchor.id = QStringLiteral("first#anchor");
    firstAnchor.targetFile = originalPath;
    Anchor secondAnchor = testAnchor(QStringLiteral("Second anchor"), QStringLiteral("sumatrapdf.page"));
    secondAnchor.id = QStringLiteral("second#anchor");
    secondAnchor.targetFile = originalPath;
    Resource first;
    first.id = QStringLiteral("first");
    first.kind = ResourceKind::Pdf;
    first.title = QStringLiteral("First");
    first.location = originalPath;
    first.anchors = {firstAnchor};
    Resource second;
    second.id = QStringLiteral("second");
    second.kind = ResourceKind::Pdf;
    second.title = QStringLiteral("Second");
    second.location = originalPath;
    second.anchors = {secondAnchor};
    QVERIFY2(repository.upsertResource(first), qPrintable(repository.lastError()));
    QVERIFY2(repository.upsertResource(second), qPrintable(repository.lastError()));

    AnchorLibraryManagementService service(repository);
    AnchorMetadataUpdate update;
    update.name = QStringLiteral("Renamed anchor");
    update.tags = {QStringLiteral("managed")};
    AnchorLibraryOperationResult result = service.updateAnchorMetadata({first.id, firstAnchor}, update);
    QVERIFY2(result.success, qPrintable(result.message));
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("Renamed anchor")}).size(), 1);

    result = service.relinkResources({first.id, second.id}, replacementPath);
    QVERIFY2(result.success, qPrintable(result.message));
    result = service.mergeResources(first.id, {second.id});
    QVERIFY2(result.success, qPrintable(result.message));
    const std::optional<Resource> merged = repository.findResource(first.id);
    QVERIFY(merged.has_value());
    QCOMPARE(merged->anchors.size(), 2);
    QCOMPARE(merged->location, QDir::cleanPath(QFileInfo(replacementPath).absoluteFilePath()));
    QVERIFY(repository.findResource(second.id)->deleted);
    QVERIFY(repository.search(SearchQuery{QStringLiteral("Second anchor")}).size() == 1);
}

void SqliteRepositoryTest::appliesAtomicBatchesAndCoalescesNotifications()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Resource resource;
    resource.id = QStringLiteral("atomic");
    resource.kind = ResourceKind::Note;
    resource.title = QStringLiteral("Before");
    resource.location = QStringLiteral("note://atomic");
    resource.anchors = {testAnchor(QStringLiteral("Atomic anchor"))};
    QVERIFY(repository.upsertResource(resource));

    int notifications = 0;
    LibraryChange lastChange;
    const int listenerId = repository.addChangeListener([&](const LibraryChange &change) {
        ++notifications;
        lastChange = change;
    });
    const quint64 initialRevision = repository.changeRevision();

    Resource changed = resource;
    changed.title = QStringLiteral("Rolled back");
    Resource invalid;
    LibraryBatchMutation invalidBatch;
    invalidBatch.upserts = {changed, invalid};
    QVERIFY(!repository.applyBatch(invalidBatch));
    QCOMPARE(repository.findResource(resource.id)->title, resource.title);
    QCOMPARE(notifications, 0);
    QCOMPARE(repository.changeRevision(), initialRevision);

    changed.title = QStringLiteral("After");
    LibraryBatchMutation validBatch;
    validBatch.upserts = {changed};
    validBatch.resourcePinUpdates = {{resource.id, true}};
    QVERIFY2(repository.applyBatch(validBatch), qPrintable(repository.lastError()));
    QCOMPARE(repository.findResource(resource.id)->title, QStringLiteral("After"));
    QVERIFY(repository.resourceUsage(resource.id)->pinned);
    QCOMPARE(notifications, 1);
    QCOMPARE(lastChange.kind, LibraryChangeKind::Content);
    QVERIFY(lastChange.resourceIds.contains(resource.id));
    QCOMPARE(repository.changeRevision(), initialRevision + 1);
    const quint64 contentRevision = repository.contentRevision();
    notifications = 0;
    QVERIFY(repository.recordResourceOpen(resource.id));
    QCOMPARE(notifications, 1);
    QCOMPARE(lastChange.kind, LibraryChangeKind::Usage);
    QCOMPARE(repository.contentRevision(), contentRevision);
    repository.removeChangeListener(listenerId);
}

void SqliteRepositoryTest::backsUpRestoresAndImportsAnchorLibraryData()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = dir.filePath(QStringLiteral("pinloom.sqlite3"));
    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Resource resource;
    resource.id = QStringLiteral("backup-resource");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Before backup");
    resource.location = dir.filePath(QStringLiteral("reference.pdf"));
    QFile target(resource.location);
    QVERIFY(target.open(QIODevice::WriteOnly));
    target.write("pdf");
    target.close();
    Anchor anchor;
    anchor.id = QStringLiteral("backup-resource#page");
    anchor.name = QStringLiteral("Page seven");
    anchor.targetApp = QStringLiteral("SumatraPDF");
    anchor.targetFile = resource.location;
    anchor.locatorType = QStringLiteral("sumatrapdf.page");
    anchor.locatorJson = QStringLiteral("{\"page\":7}");
    anchor.tags = {QStringLiteral("backup")};
    resource.anchors = {anchor};
    QVERIFY(repository.upsertResource(resource));
    QVERIFY(repository.recordResourceOpen(resource.id));
    QVERIFY(repository.recordAnchorOpen(resource.id, anchor));

    AnchorLibraryArchiveService archive(repository);
    QVERIFY(!archive.backupDatabase(QString()).success);
    QVERIFY(!archive.restoreDatabase(QString()).success);
    const QString jsonPath = dir.filePath(QStringLiteral("portable.json"));
    QVERIFY2(archive.exportJson(jsonPath).success, qPrintable(repository.lastError()));
    const QString backupPath = dir.filePath(QStringLiteral("snapshot.sqlite3"));
    AnchorLibraryOperationResult result = archive.backupDatabase(backupPath);
    QVERIFY2(result.success, qPrintable(result.message));
    QVERIFY(QFileInfo::exists(backupPath));
    const QString invalidBackupPath = dir.filePath(QStringLiteral("not-a-database.sqlite3"));
    QFile invalidBackup(invalidBackupPath);
    QVERIFY(invalidBackup.open(QIODevice::WriteOnly));
    invalidBackup.write("not sqlite");
    invalidBackup.close();
    result = archive.restoreDatabase(invalidBackupPath);
    QVERIFY(!result.success);
    QCOMPARE(repository.findResource(resource.id)->title, QStringLiteral("Before backup"));

    Resource changed = repository.findResource(resource.id).value();
    changed.title = QStringLiteral("After backup");
    changed.tags = {QStringLiteral("changed")};
    QVERIFY(repository.upsertResource(changed));
    QVERIFY(repository.recordResourceOpen(resource.id));
    QCOMPARE(repository.resourceUsage(resource.id)->openCount, 2);

    int notifications = 0;
    const int listenerId = repository.addChangeListener([&](const LibraryChange &change) {
        if (change.kind == LibraryChangeKind::Reset) ++notifications;
    });
    result = archive.restoreDatabase(backupPath);
    QVERIFY2(result.success, qPrintable(result.message));
    QCOMPARE(notifications, 1);
    QCOMPARE(repository.findResource(resource.id)->title, QStringLiteral("Before backup"));
    QCOMPARE(repository.resourceUsage(resource.id)->openCount, 1);
    QCOMPARE(repository.anchorUsage(resource.id, repository.findResource(resource.id)->anchors.first())->openCount, 1);

    QVERIFY(repository.clearResources());
    QVERIFY(!repository.findResource(resource.id).has_value());
    result = archive.importJson(jsonPath, AnchorLibraryImportMode::Replace);
    QVERIFY2(result.success, qPrintable(result.message));
    const std::optional<Resource> imported = repository.findResource(resource.id);
    QVERIFY(imported.has_value());
    QCOMPARE(imported->title, QStringLiteral("Before backup"));
    QCOMPARE(imported->anchors.first().tags, QStringList{QStringLiteral("backup")});
    QVERIFY(!repository.resourceUsage(resource.id).has_value());

    const QString automaticDirectory = dir.filePath(QStringLiteral("automatic"));
    QVERIFY(archive.createAutomaticBackup(automaticDirectory, 2).success);
    QTest::qWait(2);
    QVERIFY(archive.createAutomaticBackup(automaticDirectory, 2).success);
    QTest::qWait(2);
    QVERIFY(archive.createAutomaticBackup(automaticDirectory, 2).success);
    QCOMPARE(QDir(automaticDirectory).entryList({QStringLiteral("pinloom-auto-*.sqlite3")}, QDir::Files).size(), 2);
    repository.removeChangeListener(listenerId);
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
    project.anchors = {
        testAnchor(QStringLiteral("Dock handoff"), QStringLiteral("text.heading"), 9)};
    QVERIFY2(repository.upsertResource(project), qPrintable(repository.lastError()));

    Resource other;
    other.id = QStringLiteral("other");
    other.kind = ResourceKind::File;
    other.title = QStringLiteral("UART Other Note");
    other.location = QStringLiteral("E:/workspace/other/uart.md");
    other.anchors = {
        testAnchor(QStringLiteral("Dock handoff other"), QStringLiteral("text.heading"), 4)};
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

void SqliteRepositoryTest::appliesSqlFiltersBeforeBoundedCandidateSelection()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    for (int index = 0; index < 220; ++index) {
        Resource resource;
        resource.id = QStringLiteral("noise-%1").arg(index, 3, 10, QLatin1Char('0'));
        resource.kind = ResourceKind::File;
        resource.title = QStringLiteral("Noise %1").arg(index, 3, 10, QLatin1Char('0'));
        resource.location = QStringLiteral("E:/wantedA-noise/%1.txt").arg(index);
        QVERIFY2(repository.upsertResource(resource), qPrintable(repository.lastError()));
    }
    Resource matching;
    matching.id = QStringLiteral("wanted-after-candidate-boundary");
    matching.kind = ResourceKind::Pdf;
    matching.title = QStringLiteral("ZZZ wanted document");
    matching.location = QStringLiteral("E:/wanted_%/document.pdf");
    QVERIFY2(repository.upsertResource(matching), qPrintable(repository.lastError()));

    SearchQuery query;
    query.requiredLocationPrefixes = {QStringLiteral("E:/wanted_%")};
    query.limit = 5;
    const QList<SearchResult> results = repository.search(query);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().resource.id, matching.id);

    query.requiredLocationPrefixes.clear();
    query.requiredKinds = {ResourceKind::Pdf};
    const QList<SearchResult> kindResults = repository.search(query);
    QCOMPARE(kindResults.size(), 1);
    QCOMPARE(kindResults.first().resource.id, matching.id);

    QVERIFY2(repository.setResourcePinned(matching.id, true), qPrintable(repository.lastError()));
    query.requiredKinds.clear();
    query.limit = 1;
    const QList<SearchResult> pinnedResults = repository.search(query);
    QCOMPARE(pinnedResults.size(), 1);
    QCOMPARE(pinnedResults.first().resource.id, matching.id);
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
    note.anchors = {
        testAnchor(QStringLiteral("Dock handoff"), QStringLiteral("text.heading"), 4)};
    QVERIFY2(repository.upsertResource(note), qPrintable(repository.lastError()));

    Resource link;
    link.id = QStringLiteral("link");
    link.kind = ResourceKind::Url;
    link.title = QStringLiteral("UART Link");
    link.location = QStringLiteral("https://docs.example.com/uart#handoff");
    link.anchors = {testAnchor(QStringLiteral("Dock handoff link"), QStringLiteral("url.fragment"))};
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
    cold.anchors = {
        testAnchor(QStringLiteral("Power rail"), QStringLiteral("text.heading"), 1)};
    QVERIFY2(repository.upsertResource(cold), qPrintable(repository.lastError()));

    Resource hot;
    hot.id = QStringLiteral("hot-anchor");
    hot.kind = ResourceKind::File;
    hot.title = QStringLiteral("Zulu");
    hot.location = QStringLiteral("zulu.md");
    hot.anchors = {
        testAnchor(QStringLiteral("Power rail hot"), QStringLiteral("text.heading"), 2)};
    QVERIFY2(repository.upsertResource(hot), qPrintable(repository.lastError()));

    QVERIFY2(repository.recordAnchorOpen(hot.id, hot.anchors.first()), qPrintable(repository.lastError()));
    QVERIFY2(repository.recordAnchorOpen(hot.id, hot.anchors.first()), qPrintable(repository.lastError()));

    const std::optional<AnchorUsage> usage = repository.anchorUsage(hot.id, hot.anchors.first());
    QVERIFY(usage.has_value());
    QCOMPARE(usage->openCount, 2);
    QVERIFY(usage->lastOpenedAt.isValid());

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("Power")});
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, hot.id);
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(anchorLocatorLine(results.first().matchedAnchor.value()), 2);

    hot.title = QStringLiteral("Zulu Updated");
    QVERIFY2(repository.upsertResource(hot), qPrintable(repository.lastError()));
    const std::optional<AnchorUsage> preservedUsage = repository.anchorUsage(hot.id, hot.anchors.first());
    QVERIFY(preservedUsage.has_value());
    QCOMPARE(preservedUsage->openCount, 2);
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

    const std::optional<Resource> legacy = repository.findResource(QStringLiteral("legacy"));
    QVERIFY(legacy.has_value());
    QCOMPARE(legacy->title, QStringLiteral("Legacy Note"));
    QCOMPARE(legacy->kind, ResourceKind::File);
    QVERIFY(!legacy->explicitlyRetained);
    SearchQuery fileQuery;
    fileQuery.requiredKinds = {ResourceKind::File};
    const QList<SearchResult> fileResults = repository.search(fileQuery);
    QCOMPARE(fileResults.size(), 1);
    QCOMPARE(fileResults.first().resource.id, QStringLiteral("legacy"));
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

    const std::optional<Resource> legacy = repository.findResource(QStringLiteral("legacy"));
    QVERIFY(legacy.has_value());
    QCOMPARE(legacy->kind, ResourceKind::File);

    QList<LibraryRoot> roots = repository.libraryRoots();
    QCOMPARE(roots.size(), 1);
    QCOMPARE(roots.first().id, QStringLiteral("dir:test"));
    QCOMPARE(roots.first().path, normalizedLibraryRootPath(QStringLiteral("E:/test")));
    QVERIFY(!roots.first().syncRoot);

    LibraryRoot upgradedRoot = makeLibraryRootForPath(QStringLiteral("E:/test"));
    upgradedRoot.enabled = true;
    upgradedRoot.displayName = QStringLiteral("Migrated test root");
    upgradedRoot.ignoredDirectoryNames = {QStringLiteral("cache")};
    QVERIFY2(repository.upsertLibraryRoot(upgradedRoot), qPrintable(repository.lastError()));
    roots = repository.libraryRoots();
    QCOMPARE(roots.size(), 1);
    QCOMPARE(roots.first().id, upgradedRoot.id);
    QCOMPARE(roots.first().displayName, QStringLiteral("Migrated test root"));
    QCOMPARE(roots.first().ignoredDirectoryNames, QStringList{QStringLiteral("cache")});

    Resource resource;
    resource.id = QStringLiteral("anchored");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Anchored Note");
    resource.location = QStringLiteral("anchored.md");
    resource.anchors = {
        testAnchor(QStringLiteral("Deep Link"), QStringLiteral("text.heading"), 7)};
    QVERIFY2(repository.upsertResource(resource), qPrintable(repository.lastError()));

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("deep")});
    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(anchorLocatorLine(results.first().matchedAnchor.value()), 7);

    const QString rawConnectionName =
        QStringLiteral("pinloom_v2_retirement_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    QSqlDatabase rawDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), rawConnectionName);
    rawDatabase.setDatabaseName(databasePath);
    QVERIFY(rawDatabase.open());
    QSqlQuery rawQuery(rawDatabase);
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT COUNT(*) FROM sqlite_master "
                                         "WHERE type = 'table' AND name = 'library_roots'")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toInt(), 1);
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT COUNT(*) FROM library_roots")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toInt(), 1);
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT COUNT(*) FROM schema_migrations WHERE version = 14")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toInt(), 1);
    rawQuery = QSqlQuery();
    rawDatabase.close();
    rawDatabase = QSqlDatabase();
    QSqlDatabase::removeDatabase(rawConnectionName);
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
    QCOMPARE(legacy->anchors.first().name, QStringLiteral("Legacy Jump"));
    QCOMPARE(legacy->anchors.first().id, QStringLiteral("legacy#anchor-0"));
    QCOMPARE(legacy->anchors.first().targetFile, QStringLiteral("legacy.md"));
    QCOMPARE(legacy->anchors.first().locatorType, QStringLiteral("text.heading"));
    QVERIFY(!legacy->anchors.first().locatorJson.isEmpty());

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("Legacy Jump")});
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().resource.id, QStringLiteral("legacy"));
    QCOMPARE(results.first().matchedField, QStringLiteral("anchor_name"));
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(anchorLocatorLine(results.first().matchedAnchor.value()), 17);

    const QString rawConnectionName =
        QStringLiteral("pinloom_raw_migration_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    QSqlDatabase rawDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), rawConnectionName);
    rawDatabase.setDatabaseName(databasePath);
    QVERIFY(rawDatabase.open());
    QSqlQuery rawQuery(rawDatabase);
    QVERIFY(rawQuery.exec(QStringLiteral("PRAGMA table_info(anchors)")));
    QStringList columns;
    QHash<QString, int> primaryKeyOrder;
    while (rawQuery.next()) {
        const QString column = rawQuery.value(1).toString();
        columns.append(column);
        primaryKeyOrder.insert(column, rawQuery.value(5).toInt());
    }
    QVERIFY(columns.contains(QStringLiteral("locator_json")));
    QVERIFY(columns.contains(QStringLiteral("target_app")));
    QVERIFY(columns.contains(QStringLiteral("used_at")));
    QVERIFY(columns.contains(QStringLiteral("deleted")));
    QVERIFY(!columns.contains(QStringLiteral("type")));
    QVERIFY(!columns.contains(QStringLiteral("target")));
    QVERIFY(!columns.contains(QStringLiteral("line")));
    QVERIFY(!columns.contains(QStringLiteral("page")));
    QCOMPARE(primaryKeyOrder.value(QStringLiteral("resource_id")), 1);
    QCOMPARE(primaryKeyOrder.value(QStringLiteral("id")), 2);
    QVERIFY(rawQuery.exec(QStringLiteral("PRAGMA table_info(anchor_fts)")));
    QStringList anchorFtsColumns;
    while (rawQuery.next()) anchorFtsColumns.append(rawQuery.value(1).toString());
    QVERIFY(anchorFtsColumns.contains(QStringLiteral("anchor_id")));
    QVERIFY(!anchorFtsColumns.contains(QStringLiteral("anchor_order")));
    QVERIFY(rawQuery.exec(QStringLiteral("PRAGMA table_info(resources)")));
    QStringList resourceColumns;
    while (rawQuery.next()) {
        resourceColumns.append(rawQuery.value(1).toString());
    }
    QVERIFY(resourceColumns.contains(QStringLiteral("deleted")));
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT COUNT(*) FROM schema_migrations WHERE version = 8")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toInt(), 1);
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT COUNT(*) FROM schema_migrations WHERE version = 9")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toInt(), 1);
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT COUNT(*) FROM schema_migrations WHERE version = 10")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toInt(), 1);
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT COUNT(*) FROM schema_migrations WHERE version = 11")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toInt(), 1);
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT COUNT(*) FROM schema_migrations WHERE version = 12")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toInt(), 1);
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT COUNT(*) FROM schema_migrations WHERE version = 13")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toInt(), 1);
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT COUNT(*) FROM schema_migrations WHERE version = 14")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toInt(), 1);
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT COUNT(*) FROM sqlite_master "
                                         "WHERE type = 'table' AND name IN ('library_roots', 'resource_relations')")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toInt(), 1);
    rawQuery = QSqlQuery();
    rawDatabase.close();
    rawDatabase = QSqlDatabase();
    QSqlDatabase::removeDatabase(rawConnectionName);
}

void SqliteRepositoryTest::upgradesConfiguredExistingDatabaseCopy()
{
    const QString sourcePath = qEnvironmentVariable("PINLOOM_TEST_EXISTING_LIBRARY_DB").trimmed();
    if (sourcePath.isEmpty()) {
        QSKIP("PINLOOM_TEST_EXISTING_LIBRARY_DB is not configured");
    }
    QVERIFY2(QFileInfo::exists(sourcePath), qPrintable(QStringLiteral("Database not found: %1").arg(sourcePath)));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString copyPath = dir.filePath(QStringLiteral("pinloom.sqlite3"));
    QVERIFY2(QFile::copy(sourcePath, copyPath), qPrintable(QStringLiteral("Unable to copy %1").arg(sourcePath)));

    int resourcesBefore = 0;
    int anchorsBefore = 0;
    qlonglong anchorOpensBefore = 0;
    const QString beforeConnection =
        QStringLiteral("existing_before_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), beforeConnection);
        database.setDatabaseName(copyPath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM resources")));
        QVERIFY(query.next());
        resourcesBefore = query.value(0).toInt();
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM anchors")));
        QVERIFY(query.next());
        anchorsBefore = query.value(0).toInt();
        QVERIFY(query.exec(QStringLiteral("SELECT COALESCE(SUM(open_count), 0) FROM anchor_usage")));
        QVERIFY(query.next());
        anchorOpensBefore = query.value(0).toLongLong();
        query = QSqlQuery();
        database.close();
    }
    QSqlDatabase::removeDatabase(beforeConnection);

    {
        SqliteLibraryRepository repository;
        QVERIFY2(repository.open(copyPath), qPrintable(repository.lastError()));
        QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));
    }

    const QString afterConnection =
        QStringLiteral("existing_after_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), afterConnection);
        database.setDatabaseName(copyPath);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM resources")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), resourcesBefore);
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM anchors")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), anchorsBefore);
        QVERIFY(query.exec(QStringLiteral("SELECT COALESCE(SUM(open_count), 0) FROM anchor_usage")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toLongLong(), anchorOpensBefore);
        QVERIFY(query.exec(QStringLiteral("SELECT COALESCE(MAX(version), 0) FROM schema_migrations")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), Schema::currentVersion());

        QVERIFY(query.exec(QStringLiteral("PRAGMA table_info(anchors)")));
        QStringList columns;
        while (query.next()) {
            columns.append(query.value(1).toString());
        }
        QVERIFY(columns.contains(QStringLiteral("id")));
        QVERIFY(columns.contains(QStringLiteral("locator_type")));
        QVERIFY(columns.contains(QStringLiteral("locator_json")));
        QVERIFY(!columns.contains(QStringLiteral("type")));
        QVERIFY(!columns.contains(QStringLiteral("target")));
        QVERIFY(!columns.contains(QStringLiteral("line")));
        QVERIFY(!columns.contains(QStringLiteral("page")));

        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM anchors "
                                          "WHERE id = '' OR name = '' OR locator_type = ''")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 0);
        query = QSqlQuery();
        database.close();
    }
    QSqlDatabase::removeDatabase(afterConnection);
}

QTEST_MAIN(SqliteRepositoryTest)

#include "sqlite_repository_test.moc"
