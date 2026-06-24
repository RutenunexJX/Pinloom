#include "pinloom/core/DirectoryLibrarySource.h"
#include "pinloom/core/IndexingService.h"
#include "pinloom/core/SqliteLibraryRepository.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

using namespace Pinloom;

class DirectorySourceTest : public QObject {
    Q_OBJECT

private slots:
    void scansOnlyExplicitRoot();
    void indexesDirectoryResourcesIdempotently();
};

static void writeFile(const QString &path, const QByteArray &content = QByteArray("test"))
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

void DirectorySourceTest::scansOnlyExplicitRoot()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/docs")));
    writeFile(dir.filePath(QStringLiteral("library/notes.md")));
    writeFile(dir.filePath(QStringLiteral("library/docs/design.pdf")));
    writeFile(dir.filePath(QStringLiteral("outside.pdf")));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(resources.size(), 4);

    for (const Resource &resource : resources) {
        QVERIFY2(resource.location.startsWith(source.rootPath()), qPrintable(resource.location));
        QVERIFY(!resource.location.endsWith(QStringLiteral("outside.pdf")));
    }

    QVERIFY(std::any_of(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Markdown && resource.title == QLatin1String("notes.md");
    }));
    QVERIFY(std::any_of(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Pdf && resource.title == QLatin1String("design.pdf");
    }));
}

void DirectorySourceTest::indexesDirectoryResourcesIdempotently()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/docs")));
    writeFile(dir.filePath(QStringLiteral("library/notes.md")));
    writeFile(dir.filePath(QStringLiteral("library/docs/design.pdf")));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));
    QCOMPARE(indexer.lastIndexedCount(), 4);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));
    QCOMPARE(indexer.lastIndexedCount(), 4);

    SearchQuery allQuery;
    allQuery.limit = 100;
    QCOMPARE(repository.search(allQuery).size(), 4);

    const QList<SearchResult> designResults = repository.search(SearchQuery{QStringLiteral("design")});
    QCOMPARE(designResults.size(), 1);
    QCOMPARE(designResults.first().resource.kind, ResourceKind::Pdf);
}

QTEST_MAIN(DirectorySourceTest)

#include "directory_source_test.moc"
