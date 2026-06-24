#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/widgets/PinloomPanel.h"

#include <QLineEdit>
#include <QListWidget>
#include <QTest>

using namespace Pinloom;

class WidgetSmokeTest : public QObject {
    Q_OBJECT

private slots:
    void panelUsesInjectedRepository();
    void panelLoadsSavedLibraryRoots();
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

QTEST_MAIN(WidgetSmokeTest)

#include "widget_smoke_test.moc"
