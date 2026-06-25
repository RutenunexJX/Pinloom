#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/widgets/PinloomPanel.h"
#include "pinloom/widgets/TextPreviewDialog.h"

#include <QDesktopServices>
#include <QFile>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <optional>

using namespace Pinloom;

class WidgetSmokeTest : public QObject {
    Q_OBJECT

private slots:
    void panelUsesInjectedRepository();
    void panelLoadsSavedLibraryRoots();
    void panelExposesHostIndexingControls();
    void panelDisplaysAnchorAwareResults();
    void panelDisplaysCodeSymbolResults();
    void panelDisplaysFileLineResults();
    void panelDisplaysPdfPageResults();
    void panelPreservesPdfRegionOpenTarget();
    void panelDisplaysRelationSummary();
    void panelExposesCurrentRelatedTargetsForHostPreview();
    void panelAddsManualAliasAndAnchor();
    void panelPinsSelectedResource();
    void panelPinsSelectedLibraryRoot();
    void panelSupportsEmbeddedChromeOptions();
    void panelAppliesRequiredTagLocationAndKindFiltering();
    void panelAppliesHostContextSnapshot();
    void panelAppliesHostContextRanking();
    void panelAppliesHostContextResourceRanking();
    void panelExposesCurrentOpenTargetForHostPreview();
    void panelNotifiesHostWhenCurrentOpenTargetChanges();
    void panelNotifiesHostWhenResultCountChanges();
    void panelAllowsHostResultNavigation();
    void panelAllowsHostToActivateCurrentOpenTarget();
    void panelAllowsHostToHandleOpenTarget();
    void panelAllowsHostToHandleUrlTarget();
    void panelFallbackOpensUrlFragmentAnchor();
    void textPreviewLoadsTargetFile();
};

class CapturingUrlHandler : public QObject {
    Q_OBJECT

public slots:
    void openUrl(const QUrl &url)
    {
        lastUrl = url;
        ++openCount;
    }

public:
    QUrl lastUrl;
    int openCount = 0;
};

static void writeTestFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QCOMPARE(file.write(content), static_cast<qint64>(content.size()));
    file.close();
}

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
    QVERIFY(!results->item(0)->text().contains(QStringLiteral("fts")));
    QVERIFY(results->item(0)->toolTip().contains(resource.location));
}

void WidgetSmokeTest::panelLoadsSavedLibraryRoots()
{
    InMemoryLibraryRepository repository;
    LibraryRoot root = makeLibraryRootForPath(QStringLiteral("E:/Pinloom/Pinloom"));
    QVERIFY(repository.upsertLibraryRoot(root));

    PinloomPanel panel(repository);
    auto *rootList = panel.findChild<QListWidget *>(QStringLiteral("libraryRootList"));
    auto *fetchWebCheck = panel.findChild<QCheckBox *>(QStringLiteral("fetchRemoteWebPagesCheck"));
    QVERIFY(rootList);
    QVERIFY(fetchWebCheck);
    QCOMPARE(rootList->count(), 1);
    QCOMPARE(rootList->item(0)->data(Qt::UserRole).toString(), root.id);
    QVERIFY(!fetchWebCheck->isChecked());
    QVERIFY(!panel.remoteWebFetchingEnabled());

    panel.setRemoteWebFetchingEnabled(true);
    QVERIFY(fetchWebCheck->isChecked());
    QVERIFY(panel.remoteWebFetchingEnabled());

    panel.setRemoteWebFetchingEnabled(false);
    QVERIFY(!fetchWebCheck->isChecked());
    QVERIFY(!panel.remoteWebFetchingEnabled());
}

void WidgetSmokeTest::panelExposesHostIndexingControls()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    writeTestFile(dir.filePath(QStringLiteral("note.md")),
                  QByteArray("# Host Indexing\nPinloom selected root refresh\n"));

    InMemoryLibraryRepository repository;
    LibraryRoot root = makeLibraryRootForPath(dir.path());
    QVERIFY(repository.upsertLibraryRoot(root));

    QList<PinloomIndexingResult> indexingNotifications;
    PinloomPanelOptions options;
    options.indexingCompletedHandler = [&](const PinloomIndexingResult &result) {
        indexingNotifications.append(result);
    };

    PinloomPanel panel(repository, options);
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    auto *status = panel.findChild<QLabel *>(QStringLiteral("statusLabel"));
    QVERIFY(results);
    QVERIFY(status);
    QVERIFY(!panel.lastIndexingResult().success);
    QCOMPARE(panel.lastIndexingResult().indexedCount, 0);
    QVERIFY(indexingNotifications.isEmpty());

    const PinloomIndexingResult selectedResult = panel.indexSelectedLibraryRoot();
    QVERIFY(selectedResult.success);
    QVERIFY(selectedResult.indexedCount >= 2);
    QVERIFY(selectedResult.error.isEmpty());
    QCOMPARE(indexingNotifications.size(), 1);
    QCOMPARE(indexingNotifications.last().success, selectedResult.success);
    QCOMPARE(indexingNotifications.last().indexedCount, selectedResult.indexedCount);
    QCOMPARE(panel.lastIndexingResult().indexedCount, selectedResult.indexedCount);
    QVERIFY(status->text().contains(QStringLiteral("Indexed")));

    panel.setSearchText(QStringLiteral("selected root refresh"));
    QCOMPARE(results->count(), 1);

    writeTestFile(dir.filePath(QStringLiteral("ops.log")),
                  QByteArray("Pinloom all roots refresh\n"));
    const PinloomIndexingResult allResult = panel.indexAllEnabledLibraryRoots();
    QVERIFY(allResult.success);
    QVERIFY(allResult.indexedCount >= 3);
    QCOMPARE(indexingNotifications.size(), 2);
    QCOMPARE(indexingNotifications.last().success, allResult.success);
    QCOMPARE(indexingNotifications.last().indexedCount, allResult.indexedCount);
    QCOMPARE(panel.lastIndexingResult().indexedCount, allResult.indexedCount);

    panel.setSearchText(QStringLiteral("all roots refresh"));
    QCOMPARE(results->count(), 1);

    Resource stale;
    stale.id = QStringLiteral("stale");
    stale.kind = ResourceKind::File;
    stale.title = QStringLiteral("stale.txt");
    stale.location = QStringLiteral("stale.txt");
    stale.content = QStringLiteral("stale resource");
    QVERIFY(repository.upsertResource(stale));
    QVERIFY(!repository.search(SearchQuery{QStringLiteral("stale resource")}).isEmpty());

    const PinloomIndexingResult rebuildResult = panel.rebuildAllEnabledLibraryRoots();
    QVERIFY(rebuildResult.success);
    QVERIFY(rebuildResult.indexedCount >= 3);
    QCOMPARE(indexingNotifications.size(), 3);
    QCOMPARE(indexingNotifications.last().success, rebuildResult.success);
    QCOMPARE(indexingNotifications.last().indexedCount, rebuildResult.indexedCount);
    QCOMPARE(panel.lastIndexingResult().indexedCount, rebuildResult.indexedCount);
    QVERIFY(repository.search(SearchQuery{QStringLiteral("stale resource")}).isEmpty());

    InMemoryLibraryRepository emptyRepository;
    QList<PinloomIndexingResult> failedNotifications;
    PinloomPanelOptions failedOptions;
    failedOptions.indexingCompletedHandler = [&](const PinloomIndexingResult &result) {
        failedNotifications.append(result);
    };
    PinloomPanel emptyPanel(emptyRepository, failedOptions);
    const PinloomIndexingResult missingRootResult = emptyPanel.indexSelectedLibraryRoot();
    QVERIFY(!missingRootResult.success);
    QCOMPARE(missingRootResult.indexedCount, 0);
    QVERIFY(!missingRootResult.error.isEmpty());
    QCOMPARE(failedNotifications.size(), 1);
    QCOMPARE(failedNotifications.last().success, missingRootResult.success);
    QCOMPARE(failedNotifications.last().error, missingRootResult.error);
    QCOMPARE(emptyPanel.lastIndexingResult().error, missingRootResult.error);
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
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Heading] Power sequencing - line 3")));
    QVERIFY(!results->item(0)->text().contains(QStringLiteral("fts")));
    QVERIFY(results->item(0)->toolTip().contains(resource.location));
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("Anchor: Heading")));
    QCOMPARE(results->item(0)->data(Qt::UserRole + 3).toInt(), 3);
}

void WidgetSmokeTest::panelDisplaysCodeSymbolResults()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("code");
    resource.kind = ResourceKind::CodeSnippet;
    resource.title = QStringLiteral("pinloom.cpp");
    resource.location = QStringLiteral("pinloom.cpp");
    resource.anchors = {Anchor{AnchorType::CodeSymbol, QStringLiteral("JumpController"), 12}};
    QVERIFY(repository.upsertResource(resource));

    PinloomPanel panel(repository);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    searchEdit->setText(QStringLiteral("JumpController"));
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Symbol] JumpController - line 12")));
    QCOMPARE(results->item(0)->data(Qt::UserRole + 3).toInt(), 12);
}

void WidgetSmokeTest::panelDisplaysFileLineResults()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("code-note");
    resource.kind = ResourceKind::CodeSnippet;
    resource.title = QStringLiteral("PinloomPanel.cpp");
    resource.location = QStringLiteral("PinloomPanel.cpp");
    resource.anchors = {Anchor{AnchorType::FileLine, QStringLiteral("TODO: wire ZeroSlack dock"), 27}};
    QVERIFY(repository.upsertResource(resource));

    PinloomPanel panel(repository);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    searchEdit->setText(QStringLiteral("ZeroSlack dock"));
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Line] TODO: wire ZeroSlack dock - line 27")));
    QCOMPARE(results->item(0)->data(Qt::UserRole + 3).toInt(), 27);
    QCOMPARE(static_cast<AnchorType>(results->item(0)->data(Qt::UserRole + 5).toInt()), AnchorType::FileLine);
}

void WidgetSmokeTest::panelDisplaysPdfPageResults()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("pdf");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Spec");
    resource.location = QStringLiteral("spec.pdf");
    Anchor page;
    page.type = AnchorType::PdfPage;
    page.target = QStringLiteral("Page 2");
    page.page = 2;
    resource.anchors = {page};
    QVERIFY(repository.upsertResource(resource));

    PinloomPanel panel(repository);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    searchEdit->setText(QStringLiteral("Page 2"));
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Page] Page 2 - page 2")));
    QVERIFY(!results->item(0)->text().contains(QStringLiteral("line -1")));
    QCOMPARE(results->item(0)->data(Qt::UserRole + 6).toInt(), 2);
}

void WidgetSmokeTest::panelPreservesPdfRegionOpenTarget()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("pdf-region");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Annotated Spec");
    resource.location = QStringLiteral("spec.pdf");
    Anchor region;
    region.type = AnchorType::PdfRegion;
    region.target = QStringLiteral("Clock domain note");
    region.page = 4;
    region.region = QRectF(10.0, 20.0, 100.0, 40.0);
    resource.anchors = {region};
    QVERIFY(repository.upsertResource(resource));

    bool handled = false;
    PinloomOpenTarget capturedTarget;
    PinloomPanelOptions options;
    options.openTargetHandler = [&](const PinloomOpenTarget &target) {
        handled = true;
        capturedTarget = target;
        return true;
    };

    PinloomPanel panel(repository, options);
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    auto *openButton = panel.findChild<QPushButton *>(QStringLiteral("openButton"));
    QVERIFY(results);
    QVERIFY(openButton);

    panel.setSearchText(QStringLiteral("Clock"));
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[PDF Region] Clock domain note - page 4")));
    QCOMPARE(results->item(0)->data(Qt::UserRole + 6).toInt(), 4);
    QCOMPARE(results->item(0)->data(Qt::UserRole + 7).toDouble(), 10.0);
    QCOMPARE(results->item(0)->data(Qt::UserRole + 8).toDouble(), 20.0);
    QCOMPARE(results->item(0)->data(Qt::UserRole + 9).toDouble(), 100.0);
    QCOMPARE(results->item(0)->data(Qt::UserRole + 10).toDouble(), 40.0);

    results->setCurrentRow(0);
    openButton->click();

    QVERIFY(handled);
    QVERIFY(capturedTarget.anchor.has_value());
    QCOMPARE(capturedTarget.anchor->type, AnchorType::PdfRegion);
    QCOMPARE(capturedTarget.anchor->target, QStringLiteral("Clock domain note"));
    QCOMPARE(capturedTarget.anchor->page, 4);
    QCOMPARE(capturedTarget.anchor->region, QRectF(10.0, 20.0, 100.0, 40.0));
}

void WidgetSmokeTest::panelDisplaysRelationSummary()
{
    InMemoryLibraryRepository repository;

    Resource note;
    note.id = QStringLiteral("note");
    note.kind = ResourceKind::Markdown;
    note.title = QStringLiteral("Bringup Note");
    note.location = QStringLiteral("note.md");
    QVERIFY(repository.upsertResource(note));

    Resource spec;
    spec.id = QStringLiteral("spec");
    spec.kind = ResourceKind::Pdf;
    spec.title = QStringLiteral("PCIe Spec");
    spec.location = QStringLiteral("spec.pdf");
    QVERIFY(repository.upsertResource(spec));

    ResourceRelation relation;
    relation.sourceResourceId = note.id;
    relation.targetResourceId = spec.id;
    relation.label = QStringLiteral("supports");
    relation.note = QStringLiteral("chapter 7");
    QVERIFY(repository.upsertResourceRelation(relation));

    PinloomPanel panel(repository);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    auto *relationLabel = panel.findChild<QLabel *>(QStringLiteral("relationLabel"));
    QVERIFY(searchEdit);
    QVERIFY(results);
    QVERIFY(relationLabel);

    searchEdit->setText(QStringLiteral("Bringup"));
    QCOMPARE(results->count(), 1);
    results->setCurrentRow(0);
    QVERIFY(relationLabel->text().contains(QStringLiteral("Related: supports -> PCIe Spec (chapter 7)")));
}

void WidgetSmokeTest::panelExposesCurrentRelatedTargetsForHostPreview()
{
    InMemoryLibraryRepository repository;

    Resource note;
    note.id = QStringLiteral("note");
    note.kind = ResourceKind::Markdown;
    note.title = QStringLiteral("Bringup Note");
    note.location = QStringLiteral("note.md");
    QVERIFY(repository.upsertResource(note));

    Resource spec;
    spec.id = QStringLiteral("spec");
    spec.kind = ResourceKind::Pdf;
    spec.title = QStringLiteral("PCIe Spec");
    spec.location = QStringLiteral("spec.pdf");
    QVERIFY(repository.upsertResource(spec));

    ResourceRelation relation;
    relation.sourceResourceId = note.id;
    relation.targetResourceId = spec.id;
    relation.label = QStringLiteral("supports");
    relation.note = QStringLiteral("chapter 7");
    QVERIFY(repository.upsertResourceRelation(relation));

    PinloomPanel panel(repository);
    QVERIFY(panel.currentRelatedTargets().isEmpty());

    panel.setSearchText(QStringLiteral("Bringup"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(results);
    QCOMPARE(results->count(), 1);
    results->setCurrentRow(0);

    QList<PinloomRelatedTarget> related = panel.currentRelatedTargets();
    QCOMPARE(related.size(), 1);
    QCOMPARE(related.first().relationLabel, QStringLiteral("supports"));
    QCOMPARE(related.first().relationNote, QStringLiteral("chapter 7"));
    QVERIFY(related.first().currentIsSource);
    QCOMPARE(related.first().target.resourceId, spec.id);
    QCOMPARE(related.first().target.resourceKind, spec.kind);
    QCOMPARE(related.first().target.title, spec.title);
    QCOMPARE(related.first().target.location, spec.location);
    QVERIFY(!related.first().target.anchor.has_value());

    panel.setSearchText(QStringLiteral("PCIe Spec"));
    QCOMPARE(results->count(), 1);
    results->setCurrentRow(0);

    related = panel.currentRelatedTargets();
    QCOMPARE(related.size(), 1);
    QCOMPARE(related.first().relationLabel, QStringLiteral("supports"));
    QCOMPARE(related.first().relationNote, QStringLiteral("chapter 7"));
    QVERIFY(!related.first().currentIsSource);
    QCOMPARE(related.first().target.resourceId, note.id);
    QCOMPARE(related.first().target.resourceKind, note.kind);
    QCOMPARE(related.first().target.title, note.title);
    QCOMPARE(related.first().target.location, note.location);
}

void WidgetSmokeTest::panelAddsManualAliasAndAnchor()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::Markdown;
    resource.title = QStringLiteral("Bringup Note");
    resource.location = QStringLiteral("note.md");
    QVERIFY(repository.upsertResource(resource));

    PinloomPanel panel(repository);
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    auto *addAliasButton = panel.findChild<QPushButton *>(QStringLiteral("addAliasButton"));
    auto *addAnchorButton = panel.findChild<QPushButton *>(QStringLiteral("addAnchorButton"));
    QVERIFY(results);
    QVERIFY(addAliasButton);
    QVERIFY(addAnchorButton);

    panel.setSearchText(QStringLiteral("Bringup"));
    QCOMPARE(results->count(), 1);
    results->setCurrentRow(0);

    QVERIFY(panel.addAliasToSelectedResource(QStringLiteral("serial debug")));
    QVERIFY(!panel.addAliasToSelectedResource(QStringLiteral("Serial Debug")));

    panel.setSearchText(QStringLiteral("serial"));
    QCOMPARE(results->count(), 1);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), resource.id);
    results->setCurrentRow(0);

    QVERIFY(panel.addManualAnchorToSelectedResource(QStringLiteral("Power rail check"), 7));
    QVERIFY(!panel.addManualAnchorToSelectedResource(QStringLiteral("power rail check"), 7));

    panel.setSearchText(QStringLiteral("Power rail"));
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Anchor] Power rail check - line 7")));
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), resource.id);
    QCOMPARE(results->item(0)->data(Qt::UserRole + 3).toInt(), 7);
}

void WidgetSmokeTest::panelPinsSelectedResource()
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

    PinloomPanel panel(repository);
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    auto *pinButton = panel.findChild<QPushButton *>(QStringLiteral("pinButton"));
    QVERIFY(results);
    QVERIFY(pinButton);

    panel.setSearchText(QStringLiteral("UART"));
    QCOMPARE(results->count(), 2);

    int hotRow = -1;
    for (int row = 0; row < results->count(); ++row) {
        if (results->item(row)->data(Qt::UserRole).toString() == hot.id) {
            hotRow = row;
            break;
        }
    }
    QVERIFY(hotRow >= 0);
    results->setCurrentRow(hotRow);

    pinButton->click();

    const std::optional<ResourceUsage> usage = repository.resourceUsage(hot.id);
    QVERIFY(usage.has_value());
    QVERIFY(usage->pinned);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), hot.id);
    QCOMPARE(pinButton->text(), QStringLiteral("Unpin"));

    pinButton->click();
    const std::optional<ResourceUsage> unpinnedUsage = repository.resourceUsage(hot.id);
    QVERIFY(unpinnedUsage.has_value());
    QVERIFY(!unpinnedUsage->pinned);
    QCOMPARE(pinButton->text(), QStringLiteral("Pin"));
}

void WidgetSmokeTest::panelPinsSelectedLibraryRoot()
{
    InMemoryLibraryRepository repository;

    LibraryRoot coldRoot = makeLibraryRootForPath(QStringLiteral("E:/workspace/cold"));
    LibraryRoot hotRoot = makeLibraryRootForPath(QStringLiteral("E:/workspace/hot"));
    QVERIFY(repository.upsertLibraryRoot(coldRoot));
    QVERIFY(repository.upsertLibraryRoot(hotRoot));

    Resource cold;
    cold.id = QStringLiteral("cold-note");
    cold.kind = ResourceKind::Markdown;
    cold.title = QStringLiteral("Bringup Alpha");
    cold.location = QStringLiteral("E:/workspace/cold/bringup.md");
    QVERIFY(repository.upsertResource(cold));

    Resource hot;
    hot.id = QStringLiteral("hot-note");
    hot.kind = ResourceKind::Markdown;
    hot.title = QStringLiteral("Bringup Zulu");
    hot.location = QStringLiteral("E:/workspace/hot/bringup.md");
    QVERIFY(repository.upsertResource(hot));

    PinloomPanel panel(repository);
    auto *rootList = panel.findChild<QListWidget *>(QStringLiteral("libraryRootList"));
    auto *pinRootButton = panel.findChild<QPushButton *>(QStringLiteral("pinRootButton"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(rootList);
    QVERIFY(pinRootButton);
    QVERIFY(results);

    panel.setSearchText(QStringLiteral("Bringup"));
    QCOMPARE(results->count(), 2);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), cold.id);

    for (int row = 0; row < rootList->count(); ++row) {
        if (rootList->item(row)->data(Qt::UserRole).toString() == hotRoot.id) {
            rootList->setCurrentRow(row);
            break;
        }
    }
    QCOMPARE(rootList->currentItem()->data(Qt::UserRole).toString(), hotRoot.id);

    pinRootButton->click();

    const std::optional<LibraryRoot> pinnedRoot = repository.findLibraryRoot(hotRoot.id);
    QVERIFY(pinnedRoot.has_value());
    QVERIFY(pinnedRoot->pinned);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), hot.id);
    QVERIFY(pinRootButton->isChecked());
    QVERIFY(rootList->currentItem()->text().contains(QStringLiteral("[Pinned]")));
}

void WidgetSmokeTest::panelSupportsEmbeddedChromeOptions()
{
    InMemoryLibraryRepository repository;

    LibraryRoot root = makeLibraryRootForPath(QStringLiteral("E:/workspace/project"));
    QVERIFY(repository.upsertLibraryRoot(root));

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::Markdown;
    resource.title = QStringLiteral("UART Project Note");
    resource.location = QStringLiteral("E:/workspace/project/note.md");
    QVERIFY(repository.upsertResource(resource));

    PinloomPanelOptions options;
    options.showLibraryRootControls = false;
    options.showManualEditControls = false;
    options.showPinControls = false;
    PinloomPanel panel(repository, options);

    auto *rootControls = panel.findChild<QWidget *>(QStringLiteral("libraryRootControls"));
    auto *rootList = panel.findChild<QListWidget *>(QStringLiteral("libraryRootList"));
    auto *addAliasButton = panel.findChild<QPushButton *>(QStringLiteral("addAliasButton"));
    auto *addAnchorButton = panel.findChild<QPushButton *>(QStringLiteral("addAnchorButton"));
    auto *pinButton = panel.findChild<QPushButton *>(QStringLiteral("pinButton"));
    auto *pinRootButton = panel.findChild<QPushButton *>(QStringLiteral("pinRootButton"));
    auto *openButton = panel.findChild<QPushButton *>(QStringLiteral("openButton"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(rootControls);
    QVERIFY(rootList);
    QVERIFY(addAliasButton);
    QVERIFY(addAnchorButton);
    QVERIFY(pinButton);
    QVERIFY(pinRootButton);
    QVERIFY(openButton);
    QVERIFY(results);

    QVERIFY(rootControls->isHidden());
    QVERIFY(rootList->isHidden());
    QVERIFY(addAliasButton->isHidden());
    QVERIFY(addAnchorButton->isHidden());
    QVERIFY(pinButton->isHidden());
    QVERIFY(pinRootButton->isHidden());
    QVERIFY(!openButton->isHidden());

    panel.setSearchText(QStringLiteral("UART"));
    QCOMPARE(results->count(), 1);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), resource.id);
}

void WidgetSmokeTest::panelAppliesRequiredTagLocationAndKindFiltering()
{
    InMemoryLibraryRepository repository;

    Resource project;
    project.id = QStringLiteral("project");
    project.kind = ResourceKind::Markdown;
    project.title = QStringLiteral("UART Project Note");
    project.location = QStringLiteral("E:/workspace/project/project.md");
    project.tags = {QStringLiteral("zeroslack"), QStringLiteral("pcie")};
    QVERIFY(repository.upsertResource(project));

    Resource generic;
    generic.id = QStringLiteral("generic");
    generic.kind = ResourceKind::Url;
    generic.title = QStringLiteral("UART Generic Note");
    generic.location = QStringLiteral("https://docs.example.com/uart");
    generic.tags = {QStringLiteral("notes")};
    QVERIFY(repository.upsertResource(generic));

    PinloomPanel panel(repository);
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(results);

    panel.setSearchText(QStringLiteral("UART"));
    QCOMPARE(results->count(), 2);

    panel.setRequiredTags({QStringLiteral("zeroslack")});
    QCOMPARE(panel.requiredTags(), QStringList{QStringLiteral("zeroslack")});
    QCOMPARE(results->count(), 1);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), project.id);

    panel.setRequiredTags({QStringLiteral("missing")});
    QCOMPARE(results->count(), 0);

    panel.setRequiredTags({});
    QCOMPARE(results->count(), 2);

    panel.setRequiredLocationPrefixes({QStringLiteral("E:/workspace/project")});
    QCOMPARE(panel.requiredLocationPrefixes(), QStringList{QStringLiteral("E:/workspace/project")});
    QCOMPARE(results->count(), 1);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), project.id);

    panel.setRequiredLocationPrefixes({QStringLiteral("E:/workspace/missing")});
    QCOMPARE(results->count(), 0);

    panel.setRequiredLocationPrefixes({});
    QCOMPARE(results->count(), 2);

    panel.setRequiredResourceKinds({ResourceKind::Url});
    QCOMPARE(panel.requiredResourceKinds(), QList<ResourceKind>{ResourceKind::Url});
    QCOMPARE(results->count(), 1);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), generic.id);

    panel.setRequiredResourceKinds({ResourceKind::Pdf});
    QCOMPARE(results->count(), 0);

    panel.setRequiredResourceKinds({});
    QCOMPARE(results->count(), 2);
}

void WidgetSmokeTest::panelAppliesHostContextSnapshot()
{
    InMemoryLibraryRepository repository;

    Resource project;
    project.id = QStringLiteral("project");
    project.kind = ResourceKind::Markdown;
    project.title = QStringLiteral("UART Project Note");
    project.location = QStringLiteral("E:/workspace/project/project.md");
    project.tags = {QStringLiteral("zeroslack"), QStringLiteral("pcie")};
    QVERIFY(repository.upsertResource(project));

    Resource other;
    other.id = QStringLiteral("other");
    other.kind = ResourceKind::Markdown;
    other.title = QStringLiteral("UART Other Note");
    other.location = QStringLiteral("E:/workspace/other/other.md");
    other.tags = {QStringLiteral("zeroslack")};
    QVERIFY(repository.upsertResource(other));

    Resource web;
    web.id = QStringLiteral("web");
    web.kind = ResourceKind::Url;
    web.title = QStringLiteral("UART Web Reference");
    web.location = QStringLiteral("https://docs.example.com/uart");
    web.tags = {QStringLiteral("zeroslack"), QStringLiteral("pcie")};
    QVERIFY(repository.upsertResource(web));

    PinloomPanel panel(repository);
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(results);

    PinloomHostContext context;
    context.searchText = QStringLiteral("UART");
    context.requiredTags = {QStringLiteral("zeroslack")};
    context.requiredLocationPrefixes = {QStringLiteral("E:/workspace")};
    context.requiredResourceKinds = {ResourceKind::Markdown};
    context.contextTags = {QStringLiteral("pcie")};
    context.contextLocationPrefixes = {QStringLiteral("E:/workspace/project")};
    context.contextResourceIds = {project.id};
    context.contextRelationLabels = {QStringLiteral("links-to")};
    panel.applyHostContext(context);

    const PinloomHostContext snapshot = panel.hostContext();
    QCOMPARE(snapshot.searchText, context.searchText);
    QCOMPARE(snapshot.requiredTags, context.requiredTags);
    QCOMPARE(snapshot.requiredLocationPrefixes, context.requiredLocationPrefixes);
    QCOMPARE(snapshot.requiredResourceKinds, context.requiredResourceKinds);
    QCOMPARE(snapshot.contextTags, context.contextTags);
    QCOMPARE(snapshot.contextLocationPrefixes, context.contextLocationPrefixes);
    QCOMPARE(snapshot.contextResourceIds, context.contextResourceIds);
    QCOMPARE(snapshot.contextRelationLabels, context.contextRelationLabels);

    QCOMPARE(results->count(), 2);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), project.id);
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("Context tag: pcie")));
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("Context location: E:/workspace/project")));

    results->setCurrentRow(0);
    const PinloomOpenTarget target = panel.currentOpenTarget();
    QCOMPARE(target.resourceId, project.id);
    QCOMPARE(target.matchedContextTag, QStringLiteral("pcie"));
    QCOMPARE(target.matchedContextLocationPrefix, QStringLiteral("E:/workspace/project"));
}

void WidgetSmokeTest::panelAppliesHostContextRanking()
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

    PinloomPanel panel(repository);
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(results);

    panel.setSearchText(QStringLiteral("UART"));
    QCOMPARE(results->count(), 2);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), generic.id);

    panel.setContextTags({QStringLiteral("pcie")});
    QCOMPARE(panel.contextTags(), QStringList{QStringLiteral("pcie")});
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), contextual.id);
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("Context tag: pcie")));

    panel.setContextTags({});
    panel.setContextLocationPrefixes({QStringLiteral("E:/workspace/project")});
    QCOMPARE(panel.contextLocationPrefixes(), QStringList{QStringLiteral("E:/workspace/project")});
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), contextual.id);
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("Context location: E:/workspace/project")));
}

void WidgetSmokeTest::panelAppliesHostContextResourceRanking()
{
    InMemoryLibraryRepository repository;

    Resource active;
    active.id = QStringLiteral("active");
    active.kind = ResourceKind::Markdown;
    active.title = QStringLiteral("Current Note");
    active.location = QStringLiteral("E:/workspace/current.md");
    QVERIFY(repository.upsertResource(active));

    Resource generic;
    generic.id = QStringLiteral("generic");
    generic.kind = ResourceKind::Markdown;
    generic.title = QStringLiteral("UART Alpha");
    generic.location = QStringLiteral("E:/workspace/other/alpha.md");
    QVERIFY(repository.upsertResource(generic));

    Resource related;
    related.id = QStringLiteral("related");
    related.kind = ResourceKind::Markdown;
    related.title = QStringLiteral("UART Zulu");
    related.location = QStringLiteral("E:/workspace/project/zulu.md");
    QVERIFY(repository.upsertResource(related));

    ResourceRelation relation;
    relation.sourceResourceId = active.id;
    relation.targetResourceId = related.id;
    relation.label = QStringLiteral("supports");
    relation.note = QStringLiteral("active build edge");
    QVERIFY(repository.upsertResourceRelation(relation));

    PinloomPanel panel(repository);
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(results);

    panel.setSearchText(QStringLiteral("UART"));
    QCOMPARE(results->count(), 2);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), generic.id);

    panel.setContextResourceIds({active.id});
    QCOMPARE(panel.contextResourceIds(), QStringList{active.id});
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), related.id);
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("Context relation: active via supports")));
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("(active build edge)")));

    panel.setContextRelationLabels({QStringLiteral("compiles")});
    QCOMPARE(panel.contextRelationLabels(), QStringList{QStringLiteral("compiles")});
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), generic.id);

    panel.setContextRelationLabels({QStringLiteral("supports")});
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), related.id);
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("Context relation: active via supports")));
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("(active build edge)")));

    results->setCurrentRow(0);
    const PinloomOpenTarget target = panel.currentOpenTarget();
    QCOMPARE(target.resourceId, related.id);
    QCOMPARE(target.matchedContextResourceId, active.id);
    QCOMPARE(target.matchedContextRelationLabel, QStringLiteral("supports"));
    QCOMPARE(target.matchedContextRelationNote, QStringLiteral("active build edge"));
}

void WidgetSmokeTest::panelExposesCurrentOpenTargetForHostPreview()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::Markdown;
    resource.title = QStringLiteral("ZeroSlack Handoff");
    resource.location = QStringLiteral("E:/workspace/project/handoff.md");
    resource.tags = {QStringLiteral("zeroslack")};
    resource.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Dock handoff"), 8}};
    QVERIFY(repository.upsertResource(resource));

    PinloomPanel panel(repository);
    QVERIFY(panel.currentOpenTarget().resourceId.isEmpty());

    panel.setContextTags({QStringLiteral("zeroslack")});
    panel.setContextLocationPrefixes({QStringLiteral("E:/workspace/project")});
    panel.setSearchText(QStringLiteral("Dock"));

    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(results);
    QCOMPARE(results->count(), 1);
    results->setCurrentRow(0);

    const PinloomOpenTarget target = panel.currentOpenTarget();
    QCOMPARE(target.resourceId, resource.id);
    QCOMPARE(target.resourceKind, resource.kind);
    QCOMPARE(target.title, resource.title);
    QCOMPARE(target.location, resource.location);
    QCOMPARE(target.matchedField, QStringLiteral("anchor"));
    QCOMPARE(target.matchedContextTag, QStringLiteral("zeroslack"));
    QCOMPARE(target.matchedContextLocationPrefix, QStringLiteral("E:/workspace/project"));
    QVERIFY(target.score < 0.0);
    QVERIFY(target.anchor.has_value());
    QCOMPARE(static_cast<int>(target.anchor->type), static_cast<int>(AnchorType::MarkdownHeading));
    QCOMPARE(target.anchor->target, QStringLiteral("Dock handoff"));
    QCOMPARE(target.anchor->line, 8);
}

void WidgetSmokeTest::panelNotifiesHostWhenCurrentOpenTargetChanges()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::Markdown;
    resource.title = QStringLiteral("Preview Note");
    resource.location = QStringLiteral("preview.md");
    resource.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Preview target"), 4}};
    QVERIFY(repository.upsertResource(resource));

    QList<PinloomOpenTarget> notifications;
    PinloomPanelOptions options;
    options.currentOpenTargetChangedHandler = [&](const PinloomOpenTarget &target) {
        notifications.append(target);
    };

    PinloomPanel panel(repository, options);
    panel.setSearchText(QStringLiteral("Preview target"));

    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(results);
    QCOMPARE(results->count(), 1);

    results->setCurrentRow(0);
    QVERIFY(!notifications.isEmpty());

    const PinloomOpenTarget notified = notifications.last();
    const PinloomOpenTarget current = panel.currentOpenTarget();
    QCOMPARE(notified.resourceId, current.resourceId);
    QCOMPARE(notified.resourceKind, current.resourceKind);
    QCOMPARE(notified.title, current.title);
    QCOMPARE(notified.location, current.location);
    QCOMPARE(notified.matchedField, current.matchedField);
    QCOMPARE(notified.score, current.score);
    QVERIFY(notified.anchor.has_value());
    QCOMPARE(static_cast<int>(notified.anchor->type), static_cast<int>(AnchorType::MarkdownHeading));
    QCOMPARE(notified.anchor->target, QStringLiteral("Preview target"));
    QCOMPARE(notified.anchor->line, 4);
}

void WidgetSmokeTest::panelNotifiesHostWhenResultCountChanges()
{
    InMemoryLibraryRepository repository;

    Resource alpha;
    alpha.id = QStringLiteral("alpha");
    alpha.kind = ResourceKind::Markdown;
    alpha.title = QStringLiteral("UART Alpha");
    alpha.location = QStringLiteral("alpha.md");
    QVERIFY(repository.upsertResource(alpha));

    Resource zulu;
    zulu.id = QStringLiteral("zulu");
    zulu.kind = ResourceKind::Markdown;
    zulu.title = QStringLiteral("UART Zulu");
    zulu.location = QStringLiteral("zulu.md");
    QVERIFY(repository.upsertResource(zulu));

    QList<int> counts;
    PinloomPanelOptions options;
    options.resultCountChangedHandler = [&](int resultCount) {
        counts.append(resultCount);
    };

    PinloomPanel panel(repository, options);
    QVERIFY(!counts.isEmpty());
    QCOMPARE(counts.last(), 2);

    panel.setSearchText(QStringLiteral("missing"));
    QCOMPARE(counts.last(), 0);

    panel.setSearchText(QStringLiteral("UART"));
    QCOMPARE(counts.last(), 2);
    QCOMPARE(panel.resultCount(), 2);
}

void WidgetSmokeTest::panelAllowsHostResultNavigation()
{
    InMemoryLibraryRepository repository;

    Resource alpha;
    alpha.id = QStringLiteral("alpha");
    alpha.kind = ResourceKind::Markdown;
    alpha.title = QStringLiteral("UART Alpha");
    alpha.location = QStringLiteral("alpha.md");
    QVERIFY(repository.upsertResource(alpha));

    Resource zulu;
    zulu.id = QStringLiteral("zulu");
    zulu.kind = ResourceKind::Markdown;
    zulu.title = QStringLiteral("UART Zulu");
    zulu.location = QStringLiteral("zulu.md");
    QVERIFY(repository.upsertResource(zulu));

    PinloomPanel panel(repository);

    panel.setSearchText(QStringLiteral("missing"));
    QCOMPARE(panel.resultCount(), 0);
    QVERIFY(!panel.selectNextResult());
    QVERIFY(!panel.selectPreviousResult());

    panel.setSearchText(QStringLiteral("UART"));
    QCOMPARE(panel.resultCount(), 2);
    QVERIFY(panel.selectFirstResult());
    QCOMPARE(panel.currentOpenTarget().resourceId, alpha.id);

    QVERIFY(panel.selectNextResult());
    QCOMPARE(panel.currentOpenTarget().resourceId, zulu.id);
    QVERIFY(panel.selectNextResult());
    QCOMPARE(panel.currentOpenTarget().resourceId, zulu.id);

    QVERIFY(panel.selectPreviousResult());
    QCOMPARE(panel.currentOpenTarget().resourceId, alpha.id);
    QVERIFY(panel.selectPreviousResult());
    QCOMPARE(panel.currentOpenTarget().resourceId, alpha.id);
}

void WidgetSmokeTest::panelAllowsHostToActivateCurrentOpenTarget()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::Markdown;
    resource.title = QStringLiteral("Note");
    resource.location = QStringLiteral("note.md");
    resource.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Dock command"), 5}};
    QVERIFY(repository.upsertResource(resource));

    bool handled = false;
    PinloomOpenTarget capturedTarget;
    PinloomPanelOptions options;
    options.openTargetHandler = [&](const PinloomOpenTarget &target) {
        handled = true;
        capturedTarget = target;
        return true;
    };

    PinloomPanel panel(repository, options);
    panel.setSearchText(QStringLiteral("missing"));
    QVERIFY(!panel.selectFirstResult());
    QVERIFY(!panel.activateCurrentOpenTarget());

    panel.setSearchText(QStringLiteral("Dock"));

    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(results);
    QCOMPARE(results->count(), 1);

    QVERIFY(panel.selectFirstResult());
    QVERIFY(panel.activateCurrentOpenTarget());

    QVERIFY(handled);
    QCOMPARE(capturedTarget.resourceId, resource.id);
    QCOMPARE(capturedTarget.matchedField, QStringLiteral("anchor"));
    QVERIFY(capturedTarget.anchor.has_value());
    QCOMPARE(capturedTarget.anchor->target, QStringLiteral("Dock command"));
    QCOMPARE(capturedTarget.anchor->line, 5);

    const std::optional<ResourceUsage> usage = repository.resourceUsage(resource.id);
    QVERIFY(usage.has_value());
    QCOMPARE(usage->openCount, 1);

    const std::optional<AnchorUsage> anchorUsage = repository.anchorUsage(resource.id, resource.anchors.first());
    QVERIFY(anchorUsage.has_value());
    QCOMPARE(anchorUsage->openCount, 1);
}

void WidgetSmokeTest::panelAllowsHostToHandleOpenTarget()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::Markdown;
    resource.title = QStringLiteral("Note");
    resource.location = QStringLiteral("note.md");
    resource.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Power sequencing"), 3}};
    QVERIFY(repository.upsertResource(resource));

    bool handled = false;
    PinloomOpenTarget capturedTarget;
    PinloomPanelOptions options;
    options.openTargetHandler = [&](const PinloomOpenTarget &target) {
        handled = true;
        capturedTarget = target;
        return true;
    };

    PinloomPanel panel(repository, options);
    panel.setSearchText(QStringLiteral("Power"));
    QCOMPARE(panel.searchText(), QStringLiteral("Power"));

    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    auto *openButton = panel.findChild<QPushButton *>(QStringLiteral("openButton"));
    QVERIFY(results);
    QVERIFY(openButton);
    QCOMPARE(results->count(), 1);

    results->setCurrentRow(0);
    openButton->click();

    QVERIFY(handled);
    QCOMPARE(capturedTarget.resourceId, resource.id);
    QCOMPARE(capturedTarget.resourceKind, resource.kind);
    QCOMPARE(capturedTarget.title, resource.title);
    QCOMPARE(capturedTarget.location, resource.location);
    QCOMPARE(capturedTarget.matchedField, QStringLiteral("anchor"));
    QCOMPARE(capturedTarget.score, 0.0);
    QVERIFY(capturedTarget.anchor.has_value());
    QCOMPARE(static_cast<int>(capturedTarget.anchor->type), static_cast<int>(AnchorType::MarkdownHeading));
    QCOMPARE(capturedTarget.anchor->target, QStringLiteral("Power sequencing"));
    QCOMPARE(capturedTarget.anchor->line, 3);

    const std::optional<ResourceUsage> usage = repository.resourceUsage(resource.id);
    QVERIFY(usage.has_value());
    QCOMPARE(usage->openCount, 1);
    QVERIFY(usage->lastOpenedAt.isValid());

    const std::optional<AnchorUsage> anchorUsage = repository.anchorUsage(resource.id, resource.anchors.first());
    QVERIFY(anchorUsage.has_value());
    QCOMPARE(anchorUsage->openCount, 1);
    QVERIFY(anchorUsage->lastOpenedAt.isValid());
}

void WidgetSmokeTest::panelAllowsHostToHandleUrlTarget()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("docs");
    resource.kind = ResourceKind::Url;
    resource.title = QStringLiteral("Pinloom Docs");
    resource.location = QStringLiteral("https://docs.example.com/pinloom/setup#install");
    resource.tags = {QStringLiteral("zeroslack")};
    QVERIFY(repository.upsertResource(resource));

    bool handled = false;
    PinloomOpenTarget capturedTarget;
    PinloomPanelOptions options;
    options.openTargetHandler = [&](const PinloomOpenTarget &target) {
        handled = true;
        capturedTarget = target;
        return true;
    };

    PinloomPanel panel(repository, options);
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    auto *openButton = panel.findChild<QPushButton *>(QStringLiteral("openButton"));
    QVERIFY(results);
    QVERIFY(openButton);

    panel.setSearchText(QStringLiteral("Docs"));
    panel.setContextTags({QStringLiteral("zeroslack")});
    panel.setContextLocationPrefixes({QStringLiteral("https://docs.example.com")});
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[URL] Pinloom Docs")));

    results->setCurrentRow(0);
    openButton->click();

    QVERIFY(handled);
    QCOMPARE(capturedTarget.resourceId, resource.id);
    QCOMPARE(capturedTarget.resourceKind, resource.kind);
    QCOMPARE(capturedTarget.title, resource.title);
    QCOMPARE(capturedTarget.location, resource.location);
    QCOMPARE(capturedTarget.matchedField, QStringLiteral("title"));
    QCOMPARE(capturedTarget.matchedContextTag, QStringLiteral("zeroslack"));
    QCOMPARE(capturedTarget.matchedContextLocationPrefix, QStringLiteral("https://docs.example.com"));
    QVERIFY(capturedTarget.score < 10.0);
    QVERIFY(!capturedTarget.anchor.has_value());

    const std::optional<ResourceUsage> usage = repository.resourceUsage(resource.id);
    QVERIFY(usage.has_value());
    QCOMPARE(usage->openCount, 1);
}

void WidgetSmokeTest::panelFallbackOpensUrlFragmentAnchor()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("docs-fragment");
    resource.kind = ResourceKind::Url;
    resource.title = QStringLiteral("Pinloom Docs");
    resource.location = QStringLiteral("https://docs.example.com/pinloom/setup");
    Anchor fragment;
    fragment.type = AnchorType::UrlFragment;
    fragment.target = QStringLiteral("install");
    resource.anchors = {fragment};
    QVERIFY(repository.upsertResource(resource));

    PinloomPanel panel(repository);
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    auto *openButton = panel.findChild<QPushButton *>(QStringLiteral("openButton"));
    QVERIFY(results);
    QVERIFY(openButton);

    panel.setSearchText(QStringLiteral("install"));
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Fragment] install")));

    CapturingUrlHandler handler;
    QDesktopServices::setUrlHandler(QStringLiteral("https"), &handler, "openUrl");
    results->setCurrentRow(0);
    openButton->click();
    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));

    QCOMPARE(handler.openCount, 1);
    QCOMPARE(handler.lastUrl.adjusted(QUrl::RemoveFragment).toString(), resource.location);
    QCOMPARE(handler.lastUrl.fragment(), QStringLiteral("install"));

    const std::optional<ResourceUsage> usage = repository.resourceUsage(resource.id);
    QVERIFY(usage.has_value());
    QCOMPARE(usage->openCount, 1);

    const std::optional<AnchorUsage> anchorUsage = repository.anchorUsage(resource.id, fragment);
    QVERIFY(anchorUsage.has_value());
    QCOMPARE(anchorUsage->openCount, 1);
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
