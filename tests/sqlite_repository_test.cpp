#include "pinloom/core/SqliteLibraryRepository.h"

#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <algorithm>

using namespace Pinloom;

class SqliteRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void initializesIdempotently();
    void persistsAndSearchesResourceMetadata();
    void ranksAnchorAndFilenameMatchesBeforePathNoise();
    void managesLibraryRoots();
    void upgradesVersionOneDatabase();
    void upgradesVersionTwoDatabaseWithRoots();
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
    resource.kind = ResourceKind::Markdown;
    resource.title = QStringLiteral("FPGA Bringup Plan");
    resource.location = QStringLiteral("docs/bringup.md");
    resource.tags = {QStringLiteral("fpga"), QStringLiteral("uart")};
    resource.aliases = {QStringLiteral("serial notes"), QStringLiteral("board diary")};
    resource.anchors = {
        Anchor{AnchorType::MarkdownHeading, QStringLiteral("Power sequencing")},
        Anchor{AnchorType::FileLine, QStringLiteral("docs/bringup.md"), 42}
    };

    QVERIFY2(repository.upsertResource(resource), qPrintable(repository.lastError()));

    const std::optional<Resource> stored = repository.findResource(resource.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->tags.size(), 2);
    QCOMPARE(stored->aliases.size(), 2);
    QCOMPARE(stored->anchors.size(), 2);
    QCOMPARE(stored->anchors.at(1).line, 42);

    const QList<SearchResult> titleResults = repository.search(SearchQuery{QStringLiteral("bringup")});
    QCOMPARE(titleResults.size(), 1);
    QCOMPARE(titleResults.first().resource.id, resource.id);

    const QList<SearchResult> aliasResults = repository.search(SearchQuery{QStringLiteral("serial")});
    QCOMPARE(aliasResults.size(), 1);
    QCOMPARE(aliasResults.first().resource.id, resource.id);

    const QList<SearchResult> anchorResults = repository.search(SearchQuery{QStringLiteral("power")});
    QCOMPARE(anchorResults.size(), 1);
    QVERIFY(anchorResults.first().matchedAnchor.has_value());
    QCOMPARE(anchorResults.first().matchedAnchor->target, QStringLiteral("Power sequencing"));

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

    Resource markdown;
    markdown.id = QStringLiteral("note");
    markdown.kind = ResourceKind::Markdown;
    markdown.title = QStringLiteral("1.md");
    markdown.location = QStringLiteral("E:/test_dir/1.md");
    markdown.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("test"), 143}};
    QVERIFY2(repository.upsertResource(markdown), qPrintable(repository.lastError()));

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
    namedByPath.kind = ResourceKind::Markdown;
    namedByPath.title = QStringLiteral("Document");
    namedByPath.location = QStringLiteral("E:/test_dir/schematic.md");
    QVERIFY2(repository.upsertResource(namedByPath), qPrintable(repository.lastError()));

    const QList<SearchResult> filenameResults = repository.search(SearchQuery{QStringLiteral("schematic")});
    QCOMPARE(filenameResults.size(), 1);
    QCOMPARE(filenameResults.first().matchedField, QStringLiteral("filename"));
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
    const std::optional<Resource> legacy = repository.findResource(QStringLiteral("legacy"));
    QVERIFY(legacy.has_value());

    Resource resource;
    resource.id = QStringLiteral("anchored");
    resource.kind = ResourceKind::Markdown;
    resource.title = QStringLiteral("Anchored Note");
    resource.location = QStringLiteral("anchored.md");
    resource.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Deep Link"), 7}};
    QVERIFY2(repository.upsertResource(resource), qPrintable(repository.lastError()));

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("deep")});
    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(results.first().matchedAnchor->line, 7);
}

QTEST_MAIN(SqliteRepositoryTest)

#include "sqlite_repository_test.moc"
