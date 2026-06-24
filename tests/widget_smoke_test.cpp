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
    auto *searchEdit = panel.findChild<QLineEdit *>();
    auto *results = panel.findChild<QListWidget *>();
    QVERIFY(searchEdit);
    QVERIFY(results);

    searchEdit->setText(QStringLiteral("README"));
    QCOMPARE(results->count(), 1);
}

QTEST_MAIN(WidgetSmokeTest)

#include "widget_smoke_test.moc"
