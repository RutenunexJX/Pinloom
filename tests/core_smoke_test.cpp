#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/core/Schema.h"

#include <QTest>
#include <optional>

using namespace Pinloom;

class CoreSmokeTest : public QObject {
    Q_OBJECT

private slots:
    void searchesAliasesAndTags();
    void ranksAnchorBeforePathMatches();
    void ranksPinnedAndOpenedResourcesWithinMatchType();
    void filtersByRequiredLocationPrefixes();
    void filtersByRequiredResourceKinds();
    void ranksContextResourcesWithinMatchType();
    void ranksOpenedAnchorsWithinAnchorMatches();
    void searchesExtractedContent();
    void exposesSqliteFts5SchemaDraft();
};

void CoreSmokeTest::searchesAliasesAndTags()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("pcie-notes");
    resource.kind = ResourceKind::Markdown;
    resource.title = QStringLiteral("PCIe UART Bringup Notes");
    resource.location = QStringLiteral("docs/pcie.md");
    resource.tags = {QStringLiteral("fpga"), QStringLiteral("uart")};
    resource.aliases = {QStringLiteral("serial debug")};

    QVERIFY(repository.upsertResource(resource));

    const QList<SearchResult> aliasResults = repository.search(SearchQuery{QStringLiteral("serial")});
    QCOMPARE(aliasResults.size(), 1);
    QCOMPARE(aliasResults.first().matchedField, QStringLiteral("alias"));

    SearchQuery taggedQuery;
    taggedQuery.text = QStringLiteral("bringup");
    taggedQuery.requiredTags = {QStringLiteral("fpga")};
    const QList<SearchResult> tagResults = repository.search(taggedQuery);
    QCOMPARE(tagResults.size(), 1);
}

void CoreSmokeTest::ranksAnchorBeforePathMatches()
{
    InMemoryLibraryRepository repository;

    Resource anchored;
    anchored.id = QStringLiteral("anchored");
    anchored.kind = ResourceKind::Markdown;
    anchored.title = QStringLiteral("note.md");
    anchored.location = QStringLiteral("E:/test_dir/note.md");
    anchored.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("test"), 12}};
    QVERIFY(repository.upsertResource(anchored));

    Resource pathOnly;
    pathOnly.id = QStringLiteral("path-only");
    pathOnly.kind = ResourceKind::Pdf;
    pathOnly.title = QStringLiteral("ISO.pdf");
    pathOnly.location = QStringLiteral("E:/test_dir/ISO.pdf");
    QVERIFY(repository.upsertResource(pathOnly));

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("test")});
    QVERIFY(results.size() >= 2);
    QCOMPARE(results.first().matchedField, QStringLiteral("anchor"));
    QVERIFY(results.first().matchedAnchor.has_value());
}

void CoreSmokeTest::ranksPinnedAndOpenedResourcesWithinMatchType()
{
    InMemoryLibraryRepository repository;

    Resource cold;
    cold.id = QStringLiteral("cold");
    cold.kind = ResourceKind::Markdown;
    cold.title = QStringLiteral("UART Alpha");
    cold.location = QStringLiteral("alpha.md");
    QVERIFY(repository.upsertResource(cold));

    Resource hot;
    hot.id = QStringLiteral("hot");
    hot.kind = ResourceKind::Markdown;
    hot.title = QStringLiteral("UART Zulu");
    hot.location = QStringLiteral("zulu.md");
    QVERIFY(repository.upsertResource(hot));

    QVERIFY(repository.recordResourceOpen(hot.id));
    QVERIFY(repository.recordResourceOpen(hot.id));
    QVERIFY(repository.setResourcePinned(hot.id, true));

    const std::optional<ResourceUsage> usage = repository.resourceUsage(hot.id);
    QVERIFY(usage.has_value());
    QCOMPARE(usage->openCount, 2);
    QVERIFY(usage->lastOpenedAt.isValid());
    QVERIFY(usage->pinned);

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("UART")});
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, hot.id);
    QCOMPARE(results.first().matchedField, QStringLiteral("title"));
}

void CoreSmokeTest::filtersByRequiredLocationPrefixes()
{
    InMemoryLibraryRepository repository;

    Resource project;
    project.id = QStringLiteral("project");
    project.kind = ResourceKind::Markdown;
    project.title = QStringLiteral("UART Project Note");
    project.location = QStringLiteral("E:/workspace/project/notes/uart.md");
    QVERIFY(repository.upsertResource(project));

    Resource other;
    other.id = QStringLiteral("other");
    other.kind = ResourceKind::Markdown;
    other.title = QStringLiteral("UART Other Note");
    other.location = QStringLiteral("E:/workspace/other/uart.md");
    QVERIFY(repository.upsertResource(other));

    SearchQuery query;
    query.text = QStringLiteral("UART");
    query.requiredLocationPrefixes = {QStringLiteral("E:/workspace/project")};

    const QList<SearchResult> results = repository.search(query);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().resource.id, project.id);

    query.requiredLocationPrefixes = {QStringLiteral("E:/workspace/missing")};
    QVERIFY(repository.search(query).isEmpty());
}

void CoreSmokeTest::filtersByRequiredResourceKinds()
{
    InMemoryLibraryRepository repository;

    Resource note;
    note.id = QStringLiteral("note");
    note.kind = ResourceKind::Markdown;
    note.title = QStringLiteral("UART Note");
    note.location = QStringLiteral("note.md");
    note.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Dock handoff"), 4}};
    QVERIFY(repository.upsertResource(note));

    Resource link;
    link.id = QStringLiteral("link");
    link.kind = ResourceKind::Url;
    link.title = QStringLiteral("UART Link");
    link.location = QStringLiteral("https://docs.example.com/uart");
    link.anchors = {Anchor{AnchorType::UrlFragment, QStringLiteral("Dock handoff")}};
    QVERIFY(repository.upsertResource(link));

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

void CoreSmokeTest::ranksContextResourcesWithinMatchType()
{
    InMemoryLibraryRepository repository;

    Resource generic;
    generic.id = QStringLiteral("generic");
    generic.kind = ResourceKind::Markdown;
    generic.title = QStringLiteral("UART Alpha");
    generic.location = QStringLiteral("E:/workspace/other/alpha.md");
    generic.tags = {QStringLiteral("notes")};
    QVERIFY(repository.upsertResource(generic));

    Resource contextual;
    contextual.id = QStringLiteral("contextual");
    contextual.kind = ResourceKind::Markdown;
    contextual.title = QStringLiteral("UART Zulu");
    contextual.location = QStringLiteral("E:/workspace/project/zulu.md");
    contextual.tags = {QStringLiteral("pcie")};
    QVERIFY(repository.upsertResource(contextual));

    SearchQuery query;
    query.text = QStringLiteral("UART");
    query.contextTags = {QStringLiteral("pcie")};
    query.contextLocationPrefixes = {QStringLiteral("E:/workspace/project")};

    const QList<SearchResult> results = repository.search(query);
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, contextual.id);
    QCOMPARE(results.first().matchedField, QStringLiteral("title"));
}

void CoreSmokeTest::ranksOpenedAnchorsWithinAnchorMatches()
{
    InMemoryLibraryRepository repository;

    Resource cold;
    cold.id = QStringLiteral("cold-anchor");
    cold.kind = ResourceKind::Markdown;
    cold.title = QStringLiteral("Alpha");
    cold.location = QStringLiteral("alpha.md");
    cold.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Power rail"), 1}};
    QVERIFY(repository.upsertResource(cold));

    Resource hot;
    hot.id = QStringLiteral("hot-anchor");
    hot.kind = ResourceKind::Markdown;
    hot.title = QStringLiteral("Zulu");
    hot.location = QStringLiteral("zulu.md");
    hot.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Power rail"), 2}};
    QVERIFY(repository.upsertResource(hot));

    QVERIFY(repository.recordAnchorOpen(hot.id, hot.anchors.first()));
    QVERIFY(repository.recordAnchorOpen(hot.id, hot.anchors.first()));

    const std::optional<AnchorUsage> usage = repository.anchorUsage(hot.id, hot.anchors.first());
    QVERIFY(usage.has_value());
    QCOMPARE(usage->openCount, 2);
    QVERIFY(usage->lastOpenedAt.isValid());

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("Power")});
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, hot.id);
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(results.first().matchedAnchor->line, 2);
}

void CoreSmokeTest::searchesExtractedContent()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("html");
    resource.kind = ResourceKind::Url;
    resource.title = QStringLiteral("Local Guide");
    resource.location = QStringLiteral("guide.html");
    resource.content = QStringLiteral("This page explains browser launch routing.");
    QVERIFY(repository.upsertResource(resource));

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("browser launch")});
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().resource.id, resource.id);
    QCOMPARE(results.first().matchedField, QStringLiteral("content"));
}

void CoreSmokeTest::exposesSqliteFts5SchemaDraft()
{
    const QString ddl = Schema::sqliteFts5Draft().join(QLatin1Char('\n')).toLower();
    QVERIFY(ddl.contains(QStringLiteral("create virtual table")));
    QVERIFY(ddl.contains(QStringLiteral("fts5")));
    QVERIFY(ddl.contains(QStringLiteral("anchors")));
}

QTEST_MAIN(CoreSmokeTest)

#include "core_smoke_test.moc"
