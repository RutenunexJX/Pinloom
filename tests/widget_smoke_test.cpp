#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/widgets/PinloomPanel.h"
#include "pinloom/widgets/TextPreviewDialog.h"

#include <QLineEdit>
#include <QListWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QFile>

using namespace Pinloom;

class WidgetSmokeTest : public QObject {
    Q_OBJECT

private slots:
    void panelUsesInjectedRepository();
    void panelLoadsSavedLibraryRoots();
    void panelDisplaysAnchorAwareResults();
    void textPreviewLoadsTargetFile();
};

void WidgetSmokeTest::panelUsesInjectedRepository()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("readme");
    resource.kind = ResourceKind::Markdown;
    resource.title = QStringLiteral("Pinloom README");
    resource.location = QStringLiteral("readme.md");
    QVERIFY(repository.upsertResource(resource));

    PinloomPanel panel(repository);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    searchEdit->setText(QStringLiteral("README"));
    QCOMPARE(results->count(), 1);
}

void WidgetSmokeTest::panelLoadsSavedLibraryRoots()
{
    InMemoryLibraryRepository repository;
    LibraryRoot root = makeLibraryRootForPath(QStringLiteral("E:/Pinloom/Pinloom"));
    QVERIFY(repository.upsertLibraryRoot(root));

    PinloomPanel panel(repository);
    auto *rootList = panel.findChild<QListWidget *>(QStringLiteral("libraryRootList"));
    QVERIFY(rootList);
    QCOMPARE(rootList->count(), 1);
    QCOMPARE(rootList->item(0)->data(Qt::UserRole).toString(), root.id);
}

void WidgetSmokeTest::panelDisplaysAnchorAwareResults()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::Markdown;
    resource.title = QStringLiteral("Note");
    resource.location = QStringLiteral("note.md");
    resource.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Power sequencing"), 3}};
    QVERIFY(repository.upsertResource(resource));

    PinloomPanel panel(repository);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    searchEdit->setText(QStringLiteral("Power"));
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("Heading: Power sequencing")));
    QCOMPARE(results->item(0)->data(Qt::UserRole + 3).toInt(), 3);
}

void WidgetSmokeTest::textPreviewLoadsTargetFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.filePath(QStringLiteral("note.md"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("# Top\nbody\n");
    file.close();

    TextPreviewDialog preview(path, 1);
    QVERIFY(preview.load());
}

QTEST_MAIN(WidgetSmokeTest)

#include "widget_smoke_test.moc"
