#include "pinloom/core/SqliteLibraryRepository.h"

#include <QDir>
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
        Anchor{AnchorType::FileLine, QStringLiteral("Debug checkpoint"), 42}
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

    const QList<SearchResult> lineResults = repository.search(SearchQuery{QStringLiteral("checkpoint")});
    QCOMPARE(lineResults.size(), 1);
    QVERIFY(lineResults.first().matchedAnchor.has_value());
    QCOMPARE(lineResults.first().matchedAnchor->type, AnchorType::FileLine);
    QCOMPARE(lineResults.first().matchedAnchor->line, 42);

    Resource textSnippet;
    textSnippet.id = QStringLiteral("text-snippet");
    textSnippet.kind = ResourceKind::TextSnippet;
    textSnippet.title = QStringLiteral("Neutral Text Beacon");
    textSnippet.location = QStringLiteral("snippets/beacon.txt");
    textSnippet.anchors = {
        Anchor{AnchorType::SymbolLike, QStringLiteral("symbol-like: neutral_beacon"), 3}
    };
    QVERIFY2(repository.upsertResource(textSnippet), qPrintable(repository.lastError()));

    const std::optional<Resource> storedSnippet = repository.findResource(textSnippet.id);
    QVERIFY(storedSnippet.has_value());
    QCOMPARE(storedSnippet->kind, ResourceKind::TextSnippet);
    QCOMPARE(storedSnippet->anchors.size(), 1);
    QCOMPARE(storedSnippet->anchors.first().type, AnchorType::SymbolLike);

    const QString rawConnectionName =
        QStringLiteral("pinloom_raw_kind_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    QSqlDatabase rawDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), rawConnectionName);
    rawDatabase.setDatabaseName(dir.filePath(QStringLiteral("pinloom.sqlite3")));
    QVERIFY(rawDatabase.open());
    QSqlQuery rawQuery(rawDatabase);
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT kind FROM resources WHERE id = 'text-snippet'")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toString(), QStringLiteral("text_snippet"));
    QVERIFY(rawQuery.exec(QStringLiteral("SELECT type FROM anchors WHERE resource_id = 'text-snippet'")));
    QVERIFY(rawQuery.next());
    QCOMPARE(rawQuery.value(0).toString(), QStringLiteral("symbol_like"));
    QVERIFY(rawQuery.exec(QStringLiteral(
        "INSERT INTO resources(id, kind, title, location) "
        "VALUES ('legacy-code-snippet', 'code_snippet', 'Legacy Snippet', 'legacy/snippet.txt')")));
    QVERIFY(rawQuery.exec(QStringLiteral(
        "INSERT INTO anchors(resource_id, anchor_order, type, target, line) "
        "VALUES ('legacy-code-snippet', 0, 'code_symbol', 'legacy_symbol', 9)")));
    rawQuery = QSqlQuery();
    rawDatabase.close();
    rawDatabase = QSqlDatabase();
    QSqlDatabase::removeDatabase(rawConnectionName);

    const std::optional<Resource> legacySnippet = repository.findResource(QStringLiteral("legacy-code-snippet"));
    QVERIFY(legacySnippet.has_value());
    QCOMPARE(legacySnippet->kind, ResourceKind::TextSnippet);
    QCOMPARE(legacySnippet->anchors.size(), 1);
    QCOMPARE(legacySnippet->anchors.first().type, AnchorType::SymbolLike);

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
    partialTitle.kind = ResourceKind::Markdown;
    partialTitle.title = QStringLiteral("UART Bringup");
    partialTitle.location = QStringLiteral("partial.md");
    QVERIFY2(repository.upsertResource(partialTitle), qPrintable(repository.lastError()));

    Resource exactTitle;
    exactTitle.id = QStringLiteral("exact-title");
    exactTitle.kind = ResourceKind::Markdown;
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
    partialAnchor.kind = ResourceKind::Markdown;
    partialAnchor.title = QStringLiteral("a.md");
    partialAnchor.location = QStringLiteral("a.md");
    partialAnchor.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Power sequencing"), 7}};
    QVERIFY2(repository.upsertResource(partialAnchor), qPrintable(repository.lastError()));

    Resource exactAnchor;
    exactAnchor.id = QStringLiteral("exact-anchor");
    exactAnchor.kind = ResourceKind::Markdown;
    exactAnchor.title = QStringLiteral("b.md");
    exactAnchor.location = QStringLiteral("b.md");
    exactAnchor.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Power"), 3}};
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
    cold.kind = ResourceKind::Markdown;
    cold.title = QStringLiteral("UART Alpha");
    cold.location = QStringLiteral("alpha.md");
    QVERIFY2(repository.upsertResource(cold), qPrintable(repository.lastError()));

    Resource hot;
    hot.id = QStringLiteral("hot");
    hot.kind = ResourceKind::Markdown;
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
    project.kind = ResourceKind::Markdown;
    project.title = QStringLiteral("UART Project Note");
    project.location = QStringLiteral("E:/workspace/project/notes/uart.md");
    project.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Dock handoff"), 9}};
    QVERIFY2(repository.upsertResource(project), qPrintable(repository.lastError()));

    Resource other;
    other.id = QStringLiteral("other");
    other.kind = ResourceKind::Markdown;
    other.title = QStringLiteral("UART Other Note");
    other.location = QStringLiteral("E:/workspace/other/uart.md");
    other.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Dock handoff"), 4}};
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
    note.kind = ResourceKind::Markdown;
    note.title = QStringLiteral("UART Note");
    note.location = QStringLiteral("note.md");
    note.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Dock handoff"), 4}};
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
    anchorQuery.requiredKinds = {ResourceKind::Markdown};

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
    generic.kind = ResourceKind::Markdown;
    generic.title = QStringLiteral("UART Alpha");
    generic.location = QStringLiteral("E:/workspace/other/alpha.md");
    generic.tags = {QStringLiteral("notes")};
    QVERIFY2(repository.upsertResource(generic), qPrintable(repository.lastError()));

    Resource contextual;
    contextual.id = QStringLiteral("contextual");
    contextual.kind = ResourceKind::Markdown;
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
    active.kind = ResourceKind::Markdown;
    active.title = QStringLiteral("Current Note");
    active.location = QStringLiteral("E:/workspace/current.md");
    QVERIFY2(repository.upsertResource(active), qPrintable(repository.lastError()));

    Resource generic;
    generic.id = QStringLiteral("generic");
    generic.kind = ResourceKind::Markdown;
    generic.title = QStringLiteral("UART Alpha");
    generic.location = QStringLiteral("E:/workspace/other/alpha.md");
    QVERIFY2(repository.upsertResource(generic), qPrintable(repository.lastError()));

    Resource related;
    related.id = QStringLiteral("related");
    related.kind = ResourceKind::Markdown;
    related.title = QStringLiteral("UART Zulu");
    related.location = QStringLiteral("E:/workspace/project/zulu.md");
    QVERIFY2(repository.upsertResource(related), qPrintable(repository.lastError()));

    ResourceRelation relation;
    relation.sourceResourceId = active.id;
    relation.targetResourceId = related.id;
    relation.label = QStringLiteral("supports");
    relation.note = QStringLiteral("active build edge");
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
    QCOMPARE(results.first().matchedContextRelationLabel, QStringLiteral("supports"));
    QCOMPARE(results.first().matchedContextRelationNote, QStringLiteral("active build edge"));

    query.contextRelationLabels = {QStringLiteral("build-input")};
    results = repository.search(query);
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, generic.id);
    QVERIFY(results.first().matchedContextRelationLabel.isEmpty());
    QVERIFY(results.first().matchedContextRelationNote.isEmpty());

    query.contextRelationLabels = {QStringLiteral("SUPPORTS")};
    results = repository.search(query);
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, related.id);
    QCOMPARE(results.first().matchedContextRelationLabel, QStringLiteral("supports"));
    QCOMPARE(results.first().matchedContextRelationNote, QStringLiteral("active build edge"));
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
    cold.kind = ResourceKind::Markdown;
    cold.title = QStringLiteral("Bringup Checklist");
    cold.location = QStringLiteral("E:/workspace/cold/bringup.md");
    QVERIFY2(repository.upsertResource(cold), qPrintable(repository.lastError()));

    Resource hot;
    hot.id = QStringLiteral("hot-note");
    hot.kind = ResourceKind::Markdown;
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
    cold.kind = ResourceKind::Markdown;
    cold.title = QStringLiteral("Alpha");
    cold.location = QStringLiteral("alpha.md");
    cold.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Power rail"), 1}};
    QVERIFY2(repository.upsertResource(cold), qPrintable(repository.lastError()));

    Resource hot;
    hot.id = QStringLiteral("hot-anchor");
    hot.kind = ResourceKind::Markdown;
    hot.title = QStringLiteral("Zulu");
    hot.location = QStringLiteral("zulu.md");
    hot.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Power rail"), 2}};
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
    source.kind = ResourceKind::Markdown;
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
    relation.label = QStringLiteral("supports");
    relation.note = QStringLiteral("chapter 7");
    QVERIFY2(repository.upsertResourceRelation(relation), qPrintable(repository.lastError()));

    QList<ResourceRelation> sourceRelations = repository.resourceRelations(source.id);
    QCOMPARE(sourceRelations.size(), 1);
    QCOMPARE(sourceRelations.first().sourceResourceId, source.id);
    QCOMPARE(sourceRelations.first().targetResourceId, target.id);
    QCOMPARE(sourceRelations.first().label, QStringLiteral("supports"));
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

    Resource resource;
    resource.id = QStringLiteral("anchored");
    resource.kind = ResourceKind::Markdown;
    resource.title = QStringLiteral("Anchored Note");
    resource.location = QStringLiteral("anchored.md");
    resource.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Deep Link"), 7}};
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

QTEST_MAIN(SqliteRepositoryTest)

#include "sqlite_repository_test.moc"
