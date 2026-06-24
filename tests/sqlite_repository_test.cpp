#include "pinloom/core/SqliteLibraryRepository.h"

#include <QTemporaryDir>
#include <QTest>

using namespace Pinloom;

class SqliteRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void initializesIdempotently();
    void persistsAndSearchesResourceMetadata();
};

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

QTEST_MAIN(SqliteRepositoryTest)

#include "sqlite_repository_test.moc"
