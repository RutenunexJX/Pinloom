#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/widgets/PinloomPanel.h"
#include "pinloom/widgets/TextPreviewDialog.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <optional>

using namespace Pinloom;

class WidgetSmokeTest : public QObject {
    Q_OBJECT

private slots:
    void panelUsesInjectedRepository();
    void panelLoadsSavedLibraryRoots();
    void panelExposesHostIndexingControls();
    void panelDefaultsToLauncherSurface();
    void panelDisplaysAnchorAwareResults();
    void panelDisplaysAnchorLocatorMetadata();
    void panelDisplaysMarkerAnchors();
    void panelDisplaysBeaconLineResults();
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
    void panelAllowsHostToActivateResourceById();
    void panelAllowsHostToHandleOpenTarget();
    void panelKeyboardShortcutsHaveLauncherResponses();
    void panelRoutesCtrlKThroughManualPdfAnchorRequestProvider();
    void panelCreatesManualPdfAnchorThroughDialogHook();
    void panelCancelsManualPdfAnchorDialogHookWithoutSaving();
    void panelReportsInvalidManualPdfAnchorDialogHookRequest();
    void panelLaunchesExcelAnchorWithInjectedExecutor();
    void panelReportsInvalidExcelLocatorWithoutGenericOpen();
    void panelLaunchesPdfXChangeAnchorWithInjectedExecutor();
    void panelReportsMissingPdfXChangeExecutable();
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
    resource.kind = ResourceKind::File;
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
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[File]")));
    QVERIFY(!results->item(0)->text().contains(QStringLiteral("[Markdown]")));
    QVERIFY(!results->item(0)->text().contains(QStringLiteral("fts")));
    QVERIFY(results->item(0)->toolTip().contains(resource.location));
}

void WidgetSmokeTest::panelLoadsSavedLibraryRoots()
{
    InMemoryLibraryRepository repository;
    LibraryRoot root = makeLibraryRootForPath(QStringLiteral("E:/Pinloom/Pinloom"));
    root.displayName = QStringLiteral("Pinloom Project");
    root.lastIndexedAt = QDateTime::fromString(QStringLiteral("2026-06-25T02:15:00Z"), Qt::ISODate);
    QVERIFY(repository.upsertLibraryRoot(root));

    LibraryRoot pinnedRoot = makeLibraryRootForPath(QStringLiteral("E:/Pinloom/docs"));
    pinnedRoot.displayName = QStringLiteral("Docs");
    pinnedRoot.enabled = false;
    pinnedRoot.pinned = true;
    QVERIFY(repository.upsertLibraryRoot(pinnedRoot));

    QList<PinloomLibraryRootTarget> selectedNotifications;
    QList<QList<PinloomLibraryRootTarget>> rootSnapshots;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.currentLibraryRootChangedHandler = [&](const PinloomLibraryRootTarget &target) {
        selectedNotifications.append(target);
    };
    options.libraryRootsChangedHandler = [&](const QList<PinloomLibraryRootTarget> &roots) {
        rootSnapshots.append(roots);
    };
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };

    PinloomPanel panel(repository, options);
    auto *rootList = panel.findChild<QListWidget *>(QStringLiteral("libraryRootList"));
    auto *fetchWebCheck = panel.findChild<QCheckBox *>(QStringLiteral("fetchRemoteWebPagesCheck"));
    QVERIFY(rootList);
    QVERIFY(fetchWebCheck);
    QCOMPARE(rootList->count(), 2);
    QCOMPARE(rootList->item(0)->data(Qt::UserRole).toString(), pinnedRoot.id);
    QCOMPARE(rootList->item(1)->data(Qt::UserRole).toString(), root.id);
    QCOMPARE(panel.libraryRoots().size(), 2);
    QCOMPARE(panel.libraryRoots().at(0).id, pinnedRoot.id);
    QCOMPARE(panel.libraryRoots().at(0).displayName, pinnedRoot.displayName);
    QCOMPARE(panel.libraryRoots().at(0).enabled, false);
    QCOMPARE(panel.libraryRoots().at(0).pinned, true);
    QCOMPARE(panel.libraryRoots().at(0).rootRow, 0);
    QCOMPARE(panel.libraryRoots().at(1).id, root.id);
    QCOMPARE(panel.libraryRoots().at(1).lastIndexedAt, root.lastIndexedAt);
    QVERIFY(!rootSnapshots.isEmpty());
    QCOMPARE(rootSnapshots.last().size(), 2);
    QCOMPARE(rootSnapshots.last().at(0).id, pinnedRoot.id);
    QCOMPARE(panel.selectedLibraryRoot().id, pinnedRoot.id);
    QVERIFY(!selectedNotifications.isEmpty());
    QCOMPARE(selectedNotifications.last().id, pinnedRoot.id);

    QVERIFY(panel.selectLibraryRootById(root.id));
    QCOMPARE(panel.selectedLibraryRoot().id, root.id);
    QCOMPARE(panel.selectedLibraryRoot().path, root.path);
    QCOMPARE(panel.selectedLibraryRoot().displayName, root.displayName);
    QCOMPARE(panel.selectedLibraryRoot().rootRow, 1);
    QCOMPARE(selectedNotifications.last().id, root.id);
    QVERIFY(!panel.selectLibraryRootById(QStringLiteral("missing-root")));
    QCOMPARE(panel.selectedLibraryRoot().id, root.id);

    QVERIFY(panel.setLibraryRootEnabledById(root.id, false));
    std::optional<LibraryRoot> disabledRoot = repository.findLibraryRoot(root.id);
    QVERIFY(disabledRoot.has_value());
    QVERIFY(!disabledRoot->enabled);
    QCOMPARE(panel.selectedLibraryRoot().id, root.id);
    QVERIFY(!panel.selectedLibraryRoot().enabled);
    QCOMPARE(statusNotifications.last(), QStringLiteral("Disabled folder"));
    QVERIFY(std::any_of(rootSnapshots.last().cbegin(),
                       rootSnapshots.last().cend(),
                       [&](const PinloomLibraryRootTarget &target) {
                           return target.id == root.id && !target.enabled;
                       }));

    QVERIFY(panel.setSelectedLibraryRootEnabled(true));
    std::optional<LibraryRoot> enabledRoot = repository.findLibraryRoot(root.id);
    QVERIFY(enabledRoot.has_value());
    QVERIFY(enabledRoot->enabled);
    QCOMPARE(panel.selectedLibraryRoot().id, root.id);
    QVERIFY(panel.selectedLibraryRoot().enabled);
    QCOMPARE(statusNotifications.last(), QStringLiteral("Enabled folder"));

    QVERIFY(!panel.setLibraryRootEnabledById(QStringLiteral("missing-root"), false));
    QCOMPARE(panel.selectedLibraryRoot().id, root.id);
    QCOMPARE(statusNotifications.last(), QStringLiteral("Library folder no longer exists"));

    QVERIFY(!panel.addLibraryRootPath(QString()));
    QCOMPARE(statusNotifications.last(), QStringLiteral("No library folder path provided"));

    const QString addedPath = QStringLiteral("E:/Pinloom/host-extra");
    const LibraryRoot addedRoot = makeLibraryRootForPath(addedPath);
    QVERIFY(panel.addLibraryRootPath(addedPath));
    const std::optional<LibraryRoot> storedAddedRoot = repository.findLibraryRoot(addedRoot.id);
    QVERIFY(storedAddedRoot.has_value());
    QCOMPARE(storedAddedRoot->path, addedRoot.path);
    QCOMPARE(panel.selectedLibraryRoot().id, addedRoot.id);
    QCOMPARE(rootList->count(), 3);
    QCOMPARE(statusNotifications.last(), QStringLiteral("Added library folder"));
    QVERIFY(std::any_of(rootSnapshots.last().cbegin(),
                       rootSnapshots.last().cend(),
                       [&](const PinloomLibraryRootTarget &target) {
                           return target.id == addedRoot.id && target.path == addedRoot.path;
                       }));

    QVERIFY(panel.removeSelectedLibraryRoot());
    QVERIFY(!repository.findLibraryRoot(addedRoot.id).has_value());
    QCOMPARE(rootList->count(), 2);
    QCOMPARE(statusNotifications.last(), QStringLiteral("Removed library folder; indexed resources were kept"));
    const QList<PinloomLibraryRootTarget> rootsAfterRemove = panel.libraryRoots();
    QVERIFY(std::none_of(rootsAfterRemove.cbegin(),
                        rootsAfterRemove.cend(),
                        [&](const PinloomLibraryRootTarget &target) {
                            return target.id == addedRoot.id;
                        }));

    QVERIFY(!panel.removeLibraryRootById(QStringLiteral("missing-root")));
    QCOMPARE(statusNotifications.last(), QStringLiteral("Library folder no longer exists"));
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

    QTemporaryDir directDir;
    QVERIFY(directDir.isValid());
    writeTestFile(directDir.filePath(QStringLiteral("direct.md")),
                  QByteArray("# Direct Root\nPinloom direct root refresh\n"));

    InMemoryLibraryRepository repository;
    LibraryRoot root = makeLibraryRootForPath(dir.path());
    QVERIFY(repository.upsertLibraryRoot(root));
    LibraryRoot directRoot = makeLibraryRootForPath(directDir.path());
    QVERIFY(repository.upsertLibraryRoot(directRoot));

    QList<PinloomIndexingResult> indexingNotifications;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.indexingCompletedHandler = [&](const PinloomIndexingResult &result) {
        indexingNotifications.append(result);
    };
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };

    PinloomPanel panel(repository, options);
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    auto *status = panel.findChild<QLabel *>(QStringLiteral("statusLabel"));
    QVERIFY(results);
    QVERIFY(status);
    QVERIFY(!panel.lastIndexingResult().success);
    QCOMPARE(panel.lastIndexingResult().indexedCount, 0);
    QVERIFY(indexingNotifications.isEmpty());
    QVERIFY(!panel.statusText().isEmpty());
    QCOMPARE(statusNotifications.last(), panel.statusText());

    QVERIFY(panel.selectLibraryRootById(root.id));
    const PinloomIndexingResult directResult = panel.indexLibraryRootById(directRoot.id);
    QVERIFY(directResult.success);
    QVERIFY(directResult.indexedCount >= 2);
    QVERIFY(directResult.error.isEmpty());
    QCOMPARE(indexingNotifications.size(), 1);
    QCOMPARE(indexingNotifications.last().success, directResult.success);
    QCOMPARE(indexingNotifications.last().indexedCount, directResult.indexedCount);
    QCOMPARE(panel.lastIndexingResult().indexedCount, directResult.indexedCount);
    QCOMPARE(panel.selectedLibraryRoot().id, directRoot.id);

    panel.setSearchText(QStringLiteral("direct root refresh"));
    QCOMPARE(results->count(), 1);
    QCOMPARE(statusNotifications.last(), panel.statusText());

    QVERIFY(panel.selectLibraryRootById(root.id));
    const PinloomIndexingResult selectedResult = panel.indexSelectedLibraryRoot();
    QVERIFY(selectedResult.success);
    QVERIFY(selectedResult.indexedCount >= 2);
    QVERIFY(selectedResult.error.isEmpty());
    QCOMPARE(indexingNotifications.size(), 2);
    QCOMPARE(indexingNotifications.last().success, selectedResult.success);
    QCOMPARE(indexingNotifications.last().indexedCount, selectedResult.indexedCount);
    QCOMPARE(panel.lastIndexingResult().indexedCount, selectedResult.indexedCount);
    QVERIFY(status->text().contains(QStringLiteral("Indexed")));
    QCOMPARE(status->text(), panel.statusText());
    QCOMPARE(statusNotifications.last(), panel.statusText());

    panel.setSearchText(QStringLiteral("selected root refresh"));
    QCOMPARE(results->count(), 1);
    QCOMPARE(panel.statusText(), QStringLiteral("1 result(s)"));
    QCOMPARE(statusNotifications.last(), panel.statusText());

    writeTestFile(dir.filePath(QStringLiteral("ops.log")),
                  QByteArray("Pinloom all roots refresh\n"));
    const PinloomIndexingResult allResult = panel.indexAllEnabledLibraryRoots();
    QVERIFY(allResult.success);
    QVERIFY(allResult.indexedCount >= 4);
    QCOMPARE(indexingNotifications.size(), 3);
    QCOMPARE(indexingNotifications.last().success, allResult.success);
    QCOMPARE(indexingNotifications.last().indexedCount, allResult.indexedCount);
    QCOMPARE(panel.lastIndexingResult().indexedCount, allResult.indexedCount);
    QCOMPARE(statusNotifications.last(), panel.statusText());

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
    QVERIFY(rebuildResult.indexedCount >= 4);
    QCOMPARE(indexingNotifications.size(), 4);
    QCOMPARE(indexingNotifications.last().success, rebuildResult.success);
    QCOMPARE(indexingNotifications.last().indexedCount, rebuildResult.indexedCount);
    QCOMPARE(panel.lastIndexingResult().indexedCount, rebuildResult.indexedCount);
    QCOMPARE(statusNotifications.last(), panel.statusText());
    QVERIFY(repository.search(SearchQuery{QStringLiteral("stale resource")}).isEmpty());

    InMemoryLibraryRepository emptyRepository;
    QList<PinloomIndexingResult> failedNotifications;
    QStringList failedStatusNotifications;
    PinloomPanelOptions failedOptions;
    failedOptions.indexingCompletedHandler = [&](const PinloomIndexingResult &result) {
        failedNotifications.append(result);
    };
    failedOptions.statusChangedHandler = [&](const QString &statusText) {
        failedStatusNotifications.append(statusText);
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
    QCOMPARE(emptyPanel.statusText(), missingRootResult.error);
    QCOMPARE(failedStatusNotifications.last(), missingRootResult.error);

    const PinloomIndexingResult missingDirectRootResult =
        emptyPanel.indexLibraryRootById(QStringLiteral("missing-root"));
    QVERIFY(!missingDirectRootResult.success);
    QCOMPARE(missingDirectRootResult.indexedCount, 0);
    QCOMPARE(missingDirectRootResult.error, QStringLiteral("Library folder no longer exists"));
    QCOMPARE(failedNotifications.size(), 2);
    QCOMPARE(failedNotifications.last().error, missingDirectRootResult.error);
    QCOMPARE(emptyPanel.lastIndexingResult().error, missingDirectRootResult.error);
    QCOMPARE(failedStatusNotifications.last(), missingDirectRootResult.error);
}

void WidgetSmokeTest::panelDefaultsToLauncherSurface()
{
    InMemoryLibraryRepository repository;

    LibraryRoot root = makeLibraryRootForPath(QStringLiteral("E:/Pinloom/Pinloom"));
    QVERIFY(repository.upsertLibraryRoot(root));

    Resource resource;
    resource.id = QStringLiteral("anchor-note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Anchor Note");
    resource.location = QStringLiteral("anchor.md");
    QVERIFY(repository.upsertResource(resource));

    PinloomPanel panel(repository);
    auto *rootControls = panel.findChild<QWidget *>(QStringLiteral("libraryRootControls"));
    auto *rootList = panel.findChild<QListWidget *>(QStringLiteral("libraryRootList"));
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    auto *manageButton = panel.findChild<QPushButton *>(QStringLiteral("manageLibraryButton"));
    QVERIFY(rootControls);
    QVERIFY(rootList);
    QVERIFY(searchEdit);
    QVERIFY(results);
    QVERIFY(manageButton);

    QVERIFY(rootControls->isHidden());
    QVERIFY(rootList->isHidden());
    QVERIFY(!searchEdit->isHidden());
    QVERIFY(!results->isHidden());
    QVERIFY(searchEdit->placeholderText().contains(QStringLiteral("anchors")));
    QCOMPARE(rootList->count(), 1);

    manageButton->click();
    QVERIFY(!rootControls->isHidden());
    QVERIFY(!rootList->isHidden());
}

void WidgetSmokeTest::panelDisplaysAnchorAwareResults()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Note");
    resource.location = QStringLiteral("note.md");
    resource.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Power sequencing"), 3}};
    QVERIFY(repository.upsertResource(resource));

    PinloomPanel panel(repository);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    searchEdit->setText(QStringLiteral("Power"));
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("Power sequencing")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("text.heading")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("\"line\":3")));
    QVERIFY(!results->item(0)->text().contains(QStringLiteral("fts")));
    QVERIFY(results->item(0)->toolTip().contains(resource.location));
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("Anchor: Heading")));
    QCOMPARE(results->item(0)->data(Qt::UserRole + 3).toInt(), 3);
}

void WidgetSmokeTest::panelDisplaysAnchorLocatorMetadata()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("pdf-clock");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Clock Spec");
    resource.location = QStringLiteral("E:/docs/clock.pdf");
    Anchor anchor;
    anchor.type = AnchorType::PdfRegion;
    anchor.name = QStringLiteral("PLL jitter budget");
    anchor.target = QStringLiteral("legacy pll target");
    anchor.targetApp = QStringLiteral("PDF-XChange");
    anchor.targetFile = resource.location;
    anchor.locatorType = QStringLiteral("pdfxchange.rect");
    anchor.locatorJson = QStringLiteral("{\"page\":12,\"rect\":[420,860,780,920],\"zoom\":250,\"unit\":\"pt\"}");
    anchor.aliases = {QStringLiteral("pll budget")};
    anchor.tags = {QStringLiteral("clock"), QStringLiteral("review")};
    resource.anchors = {anchor};
    QVERIFY(repository.upsertResource(resource));

    PinloomPanel panel(repository);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    searchEdit->setText(QStringLiteral("PLL jitter"));
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("PLL jitter budget")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("PDF-XChange")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("pdfxchange.rect")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("#clock")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("aliases: pll budget")));
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("Locator: pdfxchange.rect")));

    const PinloomOpenTarget target = panel.currentOpenTarget();
    QVERIFY(target.anchor.has_value());
    QCOMPARE(target.anchor->name, anchor.name);
    QCOMPARE(target.anchor->targetApp, anchor.targetApp);
    QCOMPARE(target.anchor->targetFile, anchor.targetFile);
    QCOMPARE(target.anchor->locatorType, anchor.locatorType);
    QCOMPARE(target.anchor->locatorJson, anchor.locatorJson);
    QCOMPARE(target.anchor->aliases, anchor.aliases);
    QCOMPARE(target.anchor->tags, anchor.tags);
}

void WidgetSmokeTest::panelDisplaysMarkerAnchors()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("marker-note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("marker-notes.txt");
    resource.location = QStringLiteral("marker-notes.txt");
    resource.anchors = {Anchor{AnchorType::Marker, QStringLiteral("marker: handoff_marker"), 9}};
    QVERIFY(repository.upsertResource(resource));

    PinloomPanel panel(repository);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    searchEdit->setText(QStringLiteral("handoff_marker"));
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("marker: handoff_marker")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("marker")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("\"line\":9")));
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("Anchor: Marker")));
    QCOMPARE(results->item(0)->data(Qt::UserRole + 3).toInt(), 9);
}

void WidgetSmokeTest::panelDisplaysBeaconLineResults()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("text-beacon");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("beacon-notes.txt");
    resource.location = QStringLiteral("beacon-notes.txt");
    resource.anchors = {Anchor{AnchorType::FileLine, QStringLiteral("marker: jump target"), 12}};
    QVERIFY(repository.upsertResource(resource));

    PinloomPanel panel(repository);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    searchEdit->setText(QStringLiteral("jump target"));
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("marker: jump target")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("file.line")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("\"line\":12")));
    QCOMPARE(results->item(0)->data(Qt::UserRole + 3).toInt(), 12);
}

void WidgetSmokeTest::panelDisplaysFileLineResults()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("line-note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("dock-notes.txt");
    resource.location = QStringLiteral("dock-notes.txt");
    resource.anchors = {Anchor{AnchorType::FileLine, QStringLiteral("TODO: wire ZeroSlack dock"), 27}};
    QVERIFY(repository.upsertResource(resource));

    PinloomPanel panel(repository);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    searchEdit->setText(QStringLiteral("ZeroSlack dock"));
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("TODO: wire ZeroSlack dock")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("file.line")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("\"line\":27")));
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
    QVERIFY(results->item(0)->text().contains(QStringLiteral("Page 2")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("pdf.page")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("\"page\":2")));
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
    QVERIFY(results->item(0)->text().contains(QStringLiteral("Clock domain note")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("pdf.region")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("\"page\":4")));
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
    note.kind = ResourceKind::File;
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
    relation.label = QStringLiteral("related-to");
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
    QVERIFY(relationLabel->text().contains(QStringLiteral("Related: related-to -> PCIe Spec (chapter 7)")));
}

void WidgetSmokeTest::panelExposesCurrentRelatedTargetsForHostPreview()
{
    InMemoryLibraryRepository repository;

    Resource note;
    note.id = QStringLiteral("note");
    note.kind = ResourceKind::File;
    note.title = QStringLiteral("Bringup Note");
    note.location = QStringLiteral("note.md");
    QVERIFY(repository.upsertResource(note));

    Resource spec;
    spec.id = QStringLiteral("spec");
    spec.kind = ResourceKind::Pdf;
    spec.title = QStringLiteral("PCIe Spec");
    spec.location = QStringLiteral("spec.pdf");
    QVERIFY(repository.upsertResource(spec));

    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };
    PinloomPanel panel(repository, options);
    QVERIFY(panel.currentRelatedTargets().isEmpty());

    panel.setSearchText(QStringLiteral("Bringup"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    auto *relationLabel = panel.findChild<QLabel *>(QStringLiteral("relationLabel"));
    QVERIFY(results);
    QVERIFY(relationLabel);
    QCOMPARE(results->count(), 1);
    results->setCurrentRow(0);
    QVERIFY(panel.currentRelatedTargets().isEmpty());

    QVERIFY(!panel.upsertResourceRelation(note.id, spec.id, QString(), QStringLiteral("chapter 7")));
    QCOMPARE(statusNotifications.last(), QStringLiteral("Select related resources and enter a relation label"));

    QVERIFY(panel.upsertResourceRelation(note.id,
                                         spec.id,
                                         QStringLiteral("related-to"),
                                         QStringLiteral("chapter 7")));
    QCOMPARE(statusNotifications.last(), QStringLiteral("Saved resource relation"));
    QVERIFY(relationLabel->text().contains(QStringLiteral("related-to -> PCIe Spec (chapter 7)")));

    QList<PinloomRelatedTarget> relatedById = panel.relatedTargetsForResource(note.id);
    QCOMPARE(relatedById.size(), 1);
    QCOMPARE(relatedById.first().relationLabel, QStringLiteral("related-to"));
    QCOMPARE(relatedById.first().relationNote, QStringLiteral("chapter 7"));
    QVERIFY(relatedById.first().currentIsSource);
    QCOMPARE(relatedById.first().target.resourceId, spec.id);
    QCOMPARE(relatedById.first().target.resourceKind, spec.kind);

    QList<PinloomRelatedTarget> related = panel.currentRelatedTargets();
    QCOMPARE(related.size(), 1);
    QCOMPARE(related.first().relationLabel, QStringLiteral("related-to"));
    QCOMPARE(related.first().relationNote, QStringLiteral("chapter 7"));
    QVERIFY(related.first().currentIsSource);
    QCOMPARE(related.first().target.resourceId, spec.id);
    QCOMPARE(related.first().target.resourceKind, spec.kind);
    QCOMPARE(related.first().target.title, spec.title);
    QCOMPARE(related.first().target.location, spec.location);
    QCOMPARE(related.first().target.resultRow, -1);
    QVERIFY(!related.first().target.anchor.has_value());

    QVERIFY(panel.upsertResourceRelation(note.id,
                                         spec.id,
                                         QStringLiteral("related-to"),
                                         QStringLiteral("chapter 8")));
    related = panel.currentRelatedTargets();
    QCOMPARE(related.size(), 1);
    QCOMPARE(related.first().relationNote, QStringLiteral("chapter 8"));

    panel.setSearchText(QStringLiteral("PCIe Spec"));
    QCOMPARE(results->count(), 1);
    results->setCurrentRow(0);

    related = panel.currentRelatedTargets();
    QCOMPARE(related.size(), 1);
    QCOMPARE(related.first().relationLabel, QStringLiteral("related-to"));
    QCOMPARE(related.first().relationNote, QStringLiteral("chapter 8"));
    QVERIFY(!related.first().currentIsSource);
    QCOMPARE(related.first().target.resourceId, note.id);
    QCOMPARE(related.first().target.resourceKind, note.kind);
    QCOMPARE(related.first().target.title, note.title);
    QCOMPARE(related.first().target.location, note.location);
    QCOMPARE(related.first().target.resultRow, -1);

    relatedById = panel.relatedTargetsForResource(spec.id);
    QCOMPARE(relatedById.size(), 1);
    QCOMPARE(relatedById.first().relationNote, QStringLiteral("chapter 8"));
    QVERIFY(!relatedById.first().currentIsSource);
    QCOMPARE(relatedById.first().target.resourceId, note.id);

    QVERIFY(panel.removeResourceRelation(note.id, spec.id, QStringLiteral("related-to")));
    QCOMPARE(statusNotifications.last(), QStringLiteral("Removed resource relation"));
    QVERIFY(panel.currentRelatedTargets().isEmpty());
    QVERIFY(panel.relatedTargetsForResource(note.id).isEmpty());
    QVERIFY(panel.relatedTargetsForResource(QStringLiteral("missing")).isEmpty());
    QVERIFY(relationLabel->text().isEmpty());
    QVERIFY(!panel.removeResourceRelation(note.id, spec.id, QStringLiteral("related-to")));
    QCOMPARE(statusNotifications.last(), QStringLiteral("Unable to remove resource relation"));
}

void WidgetSmokeTest::panelAddsManualAliasAndAnchor()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Bringup Note");
    resource.location = QStringLiteral("note.md");
    QVERIFY(repository.upsertResource(resource));

    Resource hostResource;
    hostResource.id = QStringLiteral("host-note");
    hostResource.kind = ResourceKind::File;
    hostResource.title = QStringLiteral("Host Note");
    hostResource.location = QStringLiteral("host.md");
    QVERIFY(repository.upsertResource(hostResource));

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
    QVERIFY(panel.addAliasToResource(hostResource.id, QStringLiteral("host serial")));
    QVERIFY(!panel.addAliasToResource(hostResource.id, QStringLiteral("Host Serial")));
    QVERIFY(!panel.addAliasToResource(QStringLiteral("missing"), QStringLiteral("ghost")));

    panel.setSearchText(QStringLiteral("serial"));
    QCOMPARE(results->count(), 2);
    const QList<PinloomOpenTarget> serialResults = panel.currentResults();
    QVERIFY(std::any_of(serialResults.cbegin(),
                       serialResults.cend(),
                       [&](const PinloomOpenTarget &target) {
                           return target.resourceId == resource.id;
                       }));
    QVERIFY(std::any_of(serialResults.cbegin(),
                       serialResults.cend(),
                       [&](const PinloomOpenTarget &target) {
                           return target.resourceId == hostResource.id;
                       }));
    QVERIFY(panel.selectResultResource(resource.id));

    QVERIFY(panel.addManualAnchorToSelectedResource(QStringLiteral("Power rail check"), 7));
    QVERIFY(!panel.addManualAnchorToSelectedResource(QStringLiteral("power rail check"), 7));
    QVERIFY(panel.addManualAnchorToResource(hostResource.id, QStringLiteral("Host rail check"), 11));
    QVERIFY(!panel.addManualAnchorToResource(hostResource.id, QStringLiteral("host rail check"), 11));
    QVERIFY(!panel.addManualAnchorToResource(QStringLiteral("missing"), QStringLiteral("ghost rail"), 1));

    panel.setSearchText(QStringLiteral("Power rail"));
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("Power rail check")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("manual")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("\"line\":7")));
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), resource.id);
    QCOMPARE(results->item(0)->data(Qt::UserRole + 3).toInt(), 7);

    panel.setSearchText(QStringLiteral("Host rail"));
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("Host rail check")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("manual")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("\"line\":11")));
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), hostResource.id);
    QCOMPARE(results->item(0)->data(Qt::UserRole + 3).toInt(), 11);
}

void WidgetSmokeTest::panelPinsSelectedResource()
{
    InMemoryLibraryRepository repository;

    Resource cold;
    cold.id = QStringLiteral("cold");
    cold.kind = ResourceKind::File;
    cold.title = QStringLiteral("UART Alpha");
    cold.location = QStringLiteral("alpha.md");
    QVERIFY(repository.upsertResource(cold));

    Resource hot;
    hot.id = QStringLiteral("hot");
    hot.kind = ResourceKind::File;
    hot.title = QStringLiteral("UART Zulu");
    hot.location = QStringLiteral("zulu.md");
    QVERIFY(repository.upsertResource(hot));

    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };

    PinloomPanel panel(repository, options);
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    auto *pinButton = panel.findChild<QPushButton *>(QStringLiteral("pinButton"));
    QVERIFY(results);
    QVERIFY(pinButton);

    panel.setSearchText(QStringLiteral("UART"));
    QCOMPARE(results->count(), 2);

    QVERIFY(panel.setResourcePinnedById(hot.id, true));

    const std::optional<ResourceUsage> usage = repository.resourceUsage(hot.id);
    QVERIFY(usage.has_value());
    QVERIFY(usage->pinned);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), hot.id);
    QCOMPARE(panel.currentOpenTarget().resourceId, hot.id);
    QCOMPARE(pinButton->text(), QStringLiteral("Unpin"));
    QCOMPARE(statusNotifications.last(), QStringLiteral("Pinned resource"));

    QVERIFY(panel.setSelectedResourcePinned(false));
    const std::optional<ResourceUsage> unpinnedUsage = repository.resourceUsage(hot.id);
    QVERIFY(unpinnedUsage.has_value());
    QVERIFY(!unpinnedUsage->pinned);
    QCOMPARE(pinButton->text(), QStringLiteral("Pin"));
    QCOMPARE(statusNotifications.last(), QStringLiteral("Unpinned resource"));

    pinButton->click();
    const std::optional<ResourceUsage> repinnedUsage = repository.resourceUsage(hot.id);
    QVERIFY(repinnedUsage.has_value());
    QVERIFY(repinnedUsage->pinned);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), hot.id);

    QVERIFY(!panel.setResourcePinnedById(QStringLiteral("missing"), true));
    QCOMPARE(statusNotifications.last(), QStringLiteral("Resource no longer exists"));
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
    cold.kind = ResourceKind::File;
    cold.title = QStringLiteral("Bringup Alpha");
    cold.location = QStringLiteral("E:/workspace/cold/bringup.md");
    QVERIFY(repository.upsertResource(cold));

    Resource hot;
    hot.id = QStringLiteral("hot-note");
    hot.kind = ResourceKind::File;
    hot.title = QStringLiteral("Bringup Zulu");
    hot.location = QStringLiteral("E:/workspace/hot/bringup.md");
    QVERIFY(repository.upsertResource(hot));

    QStringList statusNotifications;
    QList<QList<PinloomLibraryRootTarget>> rootSnapshots;
    PinloomPanelOptions options;
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };
    options.libraryRootsChangedHandler = [&](const QList<PinloomLibraryRootTarget> &roots) {
        rootSnapshots.append(roots);
    };

    PinloomPanel panel(repository, options);
    auto *rootList = panel.findChild<QListWidget *>(QStringLiteral("libraryRootList"));
    auto *pinRootButton = panel.findChild<QPushButton *>(QStringLiteral("pinRootButton"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(rootList);
    QVERIFY(pinRootButton);
    QVERIFY(results);

    panel.setSearchText(QStringLiteral("Bringup"));
    QCOMPARE(results->count(), 2);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), cold.id);

    QVERIFY(panel.setLibraryRootPinnedById(hotRoot.id, true));

    const std::optional<LibraryRoot> pinnedRoot = repository.findLibraryRoot(hotRoot.id);
    QVERIFY(pinnedRoot.has_value());
    QVERIFY(pinnedRoot->pinned);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), hot.id);
    QCOMPARE(panel.selectedLibraryRoot().id, hotRoot.id);
    QVERIFY(pinRootButton->isChecked());
    QVERIFY(rootList->currentItem()->text().contains(QStringLiteral("[Pinned]")));
    QCOMPARE(statusNotifications.last(), QStringLiteral("Pinned folder"));
    QVERIFY(!rootSnapshots.isEmpty());
    QVERIFY(std::any_of(rootSnapshots.last().cbegin(),
                       rootSnapshots.last().cend(),
                       [&](const PinloomLibraryRootTarget &target) {
                           return target.id == hotRoot.id && target.pinned;
                       }));

    QVERIFY(panel.setSelectedLibraryRootPinned(false));
    const std::optional<LibraryRoot> unpinnedRoot = repository.findLibraryRoot(hotRoot.id);
    QVERIFY(unpinnedRoot.has_value());
    QVERIFY(!unpinnedRoot->pinned);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), cold.id);
    QVERIFY(!pinRootButton->isChecked());
    QCOMPARE(statusNotifications.last(), QStringLiteral("Unpinned folder"));

    pinRootButton->click();
    const std::optional<LibraryRoot> repinnedRoot = repository.findLibraryRoot(hotRoot.id);
    QVERIFY(repinnedRoot.has_value());
    QVERIFY(repinnedRoot->pinned);
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), hot.id);

    QVERIFY(!panel.setLibraryRootPinnedById(QStringLiteral("missing-root"), true));
    QCOMPARE(panel.selectedLibraryRoot().id, hotRoot.id);
    QCOMPARE(statusNotifications.last(), QStringLiteral("Library folder no longer exists"));
}

void WidgetSmokeTest::panelSupportsEmbeddedChromeOptions()
{
    InMemoryLibraryRepository repository;

    LibraryRoot root = makeLibraryRootForPath(QStringLiteral("E:/workspace/project"));
    QVERIFY(repository.upsertLibraryRoot(root));

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::File;
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
    project.kind = ResourceKind::File;
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
    project.kind = ResourceKind::File;
    project.title = QStringLiteral("UART Project Note");
    project.location = QStringLiteral("E:/workspace/project/project.md");
    project.tags = {QStringLiteral("zeroslack"), QStringLiteral("pcie")};
    QVERIFY(repository.upsertResource(project));

    Resource other;
    other.id = QStringLiteral("other");
    other.kind = ResourceKind::File;
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
    context.requiredResourceKinds = {ResourceKind::File};
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
    QVERIFY(target.matchSummary.contains(QStringLiteral("Match: title")));
    QVERIFY(target.matchSummary.contains(QStringLiteral("Context tag: pcie")));
    QVERIFY(target.matchSummary.contains(QStringLiteral("Context location: E:/workspace/project")));
}

void WidgetSmokeTest::panelAppliesHostContextRanking()
{
    InMemoryLibraryRepository repository;

    Resource generic;
    generic.id = QStringLiteral("generic");
    generic.kind = ResourceKind::File;
    generic.title = QStringLiteral("UART Alpha");
    generic.location = QStringLiteral("E:/workspace/other/alpha.md");
    generic.tags = {QStringLiteral("notes")};
    QVERIFY(repository.upsertResource(generic));

    Resource contextual;
    contextual.id = QStringLiteral("contextual");
    contextual.kind = ResourceKind::File;
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
    active.kind = ResourceKind::File;
    active.title = QStringLiteral("Current Note");
    active.location = QStringLiteral("E:/workspace/current.md");
    QVERIFY(repository.upsertResource(active));

    Resource generic;
    generic.id = QStringLiteral("generic");
    generic.kind = ResourceKind::File;
    generic.title = QStringLiteral("UART Alpha");
    generic.location = QStringLiteral("E:/workspace/other/alpha.md");
    QVERIFY(repository.upsertResource(generic));

    Resource related;
    related.id = QStringLiteral("related");
    related.kind = ResourceKind::File;
    related.title = QStringLiteral("UART Zulu");
    related.location = QStringLiteral("E:/workspace/project/zulu.md");
    QVERIFY(repository.upsertResource(related));

    ResourceRelation relation;
    relation.sourceResourceId = active.id;
    relation.targetResourceId = related.id;
    relation.label = QStringLiteral("related-to");
    relation.note = QStringLiteral("active relation edge");
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
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("Context relation: active via related-to")));
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("(active relation edge)")));

    panel.setContextRelationLabels({QStringLiteral("file-reference")});
    QCOMPARE(panel.contextRelationLabels(), QStringList{QStringLiteral("file-reference")});
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), generic.id);

    panel.setContextRelationLabels({QStringLiteral("related-to")});
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), related.id);
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("Context relation: active via related-to")));
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("(active relation edge)")));

    results->setCurrentRow(0);
    const PinloomOpenTarget target = panel.currentOpenTarget();
    QCOMPARE(target.resourceId, related.id);
    QCOMPARE(target.matchedContextResourceId, active.id);
    QCOMPARE(target.matchedContextRelationLabel, QStringLiteral("related-to"));
    QCOMPARE(target.matchedContextRelationNote, QStringLiteral("active relation edge"));
}

void WidgetSmokeTest::panelExposesCurrentOpenTargetForHostPreview()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("ZeroSlack Handoff");
    resource.location = QStringLiteral("E:/workspace/project/handoff.md");
    resource.tags = {QStringLiteral("zeroslack")};
    resource.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Dock handoff"), 8}};
    QVERIFY(repository.upsertResource(resource));

    int activationCount = 0;
    PinloomPanelOptions options;
    options.openTargetHandler = [&](const PinloomOpenTarget &) {
        ++activationCount;
        return true;
    };

    PinloomPanel panel(repository, options);
    const PinloomOpenTarget directTarget = panel.openTargetForResourceId(resource.id);
    QCOMPARE(directTarget.resourceId, resource.id);
    QCOMPARE(directTarget.resourceKind, resource.kind);
    QCOMPARE(directTarget.title, resource.title);
    QCOMPARE(directTarget.location, resource.location);
    QCOMPARE(directTarget.resultRow, -1);
    QVERIFY(!directTarget.anchor.has_value());
    QCOMPARE(activationCount, 0);
    QVERIFY(panel.openTargetForResourceId(QStringLiteral("missing")).resourceId.isEmpty());
    QVERIFY(panel.openTargetForResourceId(QString()).resourceId.isEmpty());

    QCOMPARE(panel.currentOpenTarget().resourceId, resource.id);
    QCOMPARE(panel.currentOpenTarget().resultRow, 0);
    QCOMPARE(panel.resultAt(0).resourceId, resource.id);
    QCOMPARE(panel.resultAt(0).resultRow, 0);
    QVERIFY(panel.resultAt(-1).resourceId.isEmpty());
    QCOMPARE(panel.resultAt(-1).resultRow, -1);
    QCOMPARE(panel.currentResults().size(), 1);

    panel.setSearchText(QStringLiteral("missing"));
    QVERIFY(panel.resultAt(0).resourceId.isEmpty());
    QVERIFY(panel.currentResults().isEmpty());
    QCOMPARE(activationCount, 0);

    panel.setContextTags({QStringLiteral("zeroslack")});
    panel.setContextLocationPrefixes({QStringLiteral("E:/workspace/project")});
    panel.setSearchText(QStringLiteral("Dock"));

    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(results);
    QCOMPARE(results->count(), 1);
    results->setCurrentRow(0);

    const PinloomOpenTarget rowTarget = panel.resultAt(0);
    QCOMPARE(rowTarget.resultRow, 0);
    QCOMPARE(rowTarget.resourceId, resource.id);
    QCOMPARE(rowTarget.resourceKind, resource.kind);
    QCOMPARE(rowTarget.title, resource.title);
    QCOMPARE(rowTarget.location, resource.location);
    QCOMPARE(rowTarget.matchedField, QStringLiteral("anchor"));
    QCOMPARE(rowTarget.matchedContextTag, QStringLiteral("zeroslack"));
    QCOMPARE(rowTarget.matchedContextLocationPrefix, QStringLiteral("E:/workspace/project"));
    QVERIFY(rowTarget.matchSummary.contains(QStringLiteral("Match: anchor")));
    QVERIFY(rowTarget.matchSummary.contains(QStringLiteral("Anchor: Heading")));
    QVERIFY(rowTarget.matchSummary.contains(QStringLiteral("Context tag: zeroslack")));
    QVERIFY(rowTarget.matchSummary.contains(QStringLiteral("Context location: E:/workspace/project")));
    QVERIFY(rowTarget.anchor.has_value());
    QCOMPARE(rowTarget.anchor->target, QStringLiteral("Dock handoff"));
    QCOMPARE(rowTarget.anchor->line, 8);
    QVERIFY(panel.resultAt(1).resourceId.isEmpty());
    QCOMPARE(activationCount, 0);

    const QList<PinloomOpenTarget> currentResults = panel.currentResults();
    QCOMPARE(currentResults.size(), 1);
    QCOMPARE(currentResults.first().resourceId, resource.id);
    QCOMPARE(currentResults.first().resultRow, 0);
    QCOMPARE(currentResults.first().matchSummary, rowTarget.matchSummary);
    QVERIFY(currentResults.first().anchor.has_value());
    QCOMPARE(currentResults.first().anchor->target, QStringLiteral("Dock handoff"));
    QCOMPARE(currentResults.first().location, resource.location);
    QCOMPARE(activationCount, 0);

    const PinloomOpenTarget target = panel.currentOpenTarget();
    QCOMPARE(target.resultRow, 0);
    QCOMPARE(target.resourceId, resource.id);
    QCOMPARE(target.resourceKind, resource.kind);
    QCOMPARE(target.title, resource.title);
    QCOMPARE(target.location, resource.location);
    QCOMPARE(target.matchedField, QStringLiteral("anchor"));
    QCOMPARE(target.matchedContextTag, QStringLiteral("zeroslack"));
    QCOMPARE(target.matchedContextLocationPrefix, QStringLiteral("E:/workspace/project"));
    QCOMPARE(target.matchSummary, rowTarget.matchSummary);
    QVERIFY(target.score < 0.0);
    QVERIFY(target.anchor.has_value());
    QCOMPARE(static_cast<int>(target.anchor->type), static_cast<int>(AnchorType::TextHeading));
    QCOMPARE(target.anchor->target, QStringLiteral("Dock handoff"));
    QCOMPARE(target.anchor->line, 8);
    QCOMPARE(activationCount, 0);
}

void WidgetSmokeTest::panelNotifiesHostWhenCurrentOpenTargetChanges()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Preview Note");
    resource.location = QStringLiteral("preview.md");
    resource.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Preview target"), 4}};
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
    QCOMPARE(static_cast<int>(notified.anchor->type), static_cast<int>(AnchorType::TextHeading));
    QCOMPARE(notified.anchor->target, QStringLiteral("Preview target"));
    QCOMPARE(notified.anchor->line, 4);
}

void WidgetSmokeTest::panelNotifiesHostWhenResultCountChanges()
{
    InMemoryLibraryRepository repository;

    Resource alpha;
    alpha.id = QStringLiteral("alpha");
    alpha.kind = ResourceKind::File;
    alpha.title = QStringLiteral("UART Alpha");
    alpha.location = QStringLiteral("alpha.md");
    QVERIFY(repository.upsertResource(alpha));

    Resource zulu;
    zulu.id = QStringLiteral("zulu");
    zulu.kind = ResourceKind::File;
    zulu.title = QStringLiteral("UART Zulu");
    zulu.location = QStringLiteral("zulu.md");
    QVERIFY(repository.upsertResource(zulu));

    QList<int> counts;
    QList<QList<PinloomOpenTarget>> resultSnapshots;
    PinloomPanelOptions options;
    options.resultCountChangedHandler = [&](int resultCount) {
        counts.append(resultCount);
    };
    options.resultsChangedHandler = [&](const QList<PinloomOpenTarget> &results) {
        resultSnapshots.append(results);
    };

    PinloomPanel panel(repository, options);
    QVERIFY(!counts.isEmpty());
    QCOMPARE(counts.last(), 2);
    QVERIFY(!resultSnapshots.isEmpty());
    QCOMPARE(resultSnapshots.last().size(), 2);
    QCOMPARE(resultSnapshots.last().at(0).resultRow, 0);
    QCOMPARE(resultSnapshots.last().at(1).resultRow, 1);
    QVERIFY(resultSnapshots.last().at(0).matchSummary.contains(QStringLiteral("Match: all")));

    panel.setSearchText(QStringLiteral("missing"));
    QCOMPARE(counts.last(), 0);
    QVERIFY(resultSnapshots.last().isEmpty());

    panel.setSearchText(QStringLiteral("UART"));
    QCOMPARE(counts.last(), 2);
    QCOMPARE(panel.resultCount(), 2);
    QCOMPARE(resultSnapshots.last().size(), 2);
    QCOMPARE(resultSnapshots.last().at(0).resultRow, 0);
    QCOMPARE(resultSnapshots.last().at(1).resultRow, 1);
    QVERIFY(resultSnapshots.last().at(0).matchSummary.contains(QStringLiteral("Match: title")));
    QVERIFY(std::any_of(resultSnapshots.last().cbegin(),
                       resultSnapshots.last().cend(),
                       [&](const PinloomOpenTarget &target) {
                           return target.resourceId == alpha.id;
                       }));
    QVERIFY(std::any_of(resultSnapshots.last().cbegin(),
                       resultSnapshots.last().cend(),
                       [&](const PinloomOpenTarget &target) {
                           return target.resourceId == zulu.id;
                       }));
}

void WidgetSmokeTest::panelAllowsHostResultNavigation()
{
    InMemoryLibraryRepository repository;

    Resource alpha;
    alpha.id = QStringLiteral("alpha");
    alpha.kind = ResourceKind::File;
    alpha.title = QStringLiteral("UART Alpha");
    alpha.location = QStringLiteral("alpha.md");
    QVERIFY(repository.upsertResource(alpha));

    Resource zulu;
    zulu.id = QStringLiteral("zulu");
    zulu.kind = ResourceKind::File;
    zulu.title = QStringLiteral("UART Zulu");
    zulu.location = QStringLiteral("zulu.md");
    QVERIFY(repository.upsertResource(zulu));

    PinloomPanel panel(repository);

    panel.setSearchText(QStringLiteral("missing"));
    QCOMPARE(panel.resultCount(), 0);
    QVERIFY(!panel.selectResultAt(0));
    QVERIFY(!panel.selectResultResource(alpha.id));
    QVERIFY(!panel.selectNextResult());
    QVERIFY(!panel.selectPreviousResult());

    panel.setSearchText(QStringLiteral("UART"));
    QCOMPARE(panel.resultCount(), 2);
    QVERIFY(!panel.selectResultAt(-1));
    QVERIFY(!panel.selectResultAt(2));
    QVERIFY(panel.selectFirstResult());
    QCOMPARE(panel.currentOpenTarget().resourceId, alpha.id);
    QCOMPARE(panel.currentOpenTarget().resultRow, 0);

    QVERIFY(panel.selectResultAt(1));
    QCOMPARE(panel.currentOpenTarget().resourceId, zulu.id);
    QCOMPARE(panel.currentOpenTarget().resultRow, 1);
    QVERIFY(!panel.selectResultAt(2));
    QCOMPARE(panel.currentOpenTarget().resourceId, zulu.id);

    QVERIFY(panel.selectResultResource(alpha.id));
    QCOMPARE(panel.currentOpenTarget().resourceId, alpha.id);
    QVERIFY(!panel.selectResultResource(QStringLiteral("missing")));
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
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Note");
    resource.location = QStringLiteral("note.md");
    resource.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Dock command"), 5}};
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

    const std::optional<AnchorUsage> anchorUsage = repository.anchorUsage(resource.id, capturedTarget.anchor.value());
    QVERIFY(anchorUsage.has_value());
    QCOMPARE(anchorUsage->openCount, 1);
}

void WidgetSmokeTest::panelAllowsHostToActivateResourceById()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Direct Note");
    resource.location = QStringLiteral("direct-note.md");
    QVERIFY(repository.upsertResource(resource));

    int handledCount = 0;
    PinloomOpenTarget capturedTarget;
    PinloomPanelOptions options;
    options.openTargetHandler = [&](const PinloomOpenTarget &target) {
        ++handledCount;
        capturedTarget = target;
        return true;
    };

    PinloomPanel panel(repository, options);
    panel.setSearchText(QStringLiteral("missing"));
    QVERIFY(panel.currentOpenTarget().resourceId.isEmpty());
    QCOMPARE(panel.resultCount(), 0);

    QVERIFY(panel.activateResourceById(resource.id));
    QCOMPARE(handledCount, 1);
    QCOMPARE(capturedTarget.resourceId, resource.id);
    QCOMPARE(capturedTarget.resourceKind, resource.kind);
    QCOMPARE(capturedTarget.title, resource.title);
    QCOMPARE(capturedTarget.location, resource.location);
    QCOMPARE(capturedTarget.resultRow, -1);
    QVERIFY(capturedTarget.matchedField.isEmpty());
    QVERIFY(!capturedTarget.anchor.has_value());
    QVERIFY(panel.currentOpenTarget().resourceId.isEmpty());

    const std::optional<ResourceUsage> usage = repository.resourceUsage(resource.id);
    QVERIFY(usage.has_value());
    QCOMPARE(usage->openCount, 1);

    QVERIFY(!panel.activateResourceById(QStringLiteral("missing")));
    QCOMPARE(panel.statusText(), QStringLiteral("Resource no longer exists"));
    QCOMPARE(handledCount, 1);

    QVERIFY(!panel.activateResourceById(QString()));
    QCOMPARE(panel.statusText(), QStringLiteral("No resource selected"));
    QCOMPARE(handledCount, 1);
}

void WidgetSmokeTest::panelAllowsHostToHandleOpenTarget()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Note");
    resource.location = QStringLiteral("note.md");
    resource.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Power sequencing"), 3}};
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
    QVERIFY(capturedTarget.matchSummary.contains(QStringLiteral("Match: anchor")));
    QVERIFY(capturedTarget.matchSummary.contains(QStringLiteral("Anchor: Heading")));
    QVERIFY(capturedTarget.anchor.has_value());
    QCOMPARE(static_cast<int>(capturedTarget.anchor->type), static_cast<int>(AnchorType::TextHeading));
    QCOMPARE(capturedTarget.anchor->target, QStringLiteral("Power sequencing"));
    QCOMPARE(capturedTarget.anchor->line, 3);

    const std::optional<ResourceUsage> usage = repository.resourceUsage(resource.id);
    QVERIFY(usage.has_value());
    QCOMPARE(usage->openCount, 1);
    QVERIFY(usage->lastOpenedAt.isValid());

    const std::optional<AnchorUsage> anchorUsage = repository.anchorUsage(resource.id, capturedTarget.anchor.value());
    QVERIFY(anchorUsage.has_value());
    QCOMPARE(anchorUsage->openCount, 1);
    QVERIFY(anchorUsage->lastOpenedAt.isValid());
}

void WidgetSmokeTest::panelKeyboardShortcutsHaveLauncherResponses()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Launcher Note");
    resource.location = QStringLiteral("launcher.md");
    Anchor anchor;
    anchor.type = AnchorType::TextHeading;
    anchor.target = QStringLiteral("Keyboard command");
    anchor.line = 6;
    resource.anchors = {anchor};
    QVERIFY(repository.upsertResource(resource));

    int activationCount = 0;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.openTargetHandler = [&](const PinloomOpenTarget &target) {
        ++activationCount;
        return target.resourceId == resource.id && target.anchor.has_value();
    };
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };
    bool dialogParentProvided = false;
    options.manualPdfAnchorDialogHandler = [&](QWidget *parent) -> std::optional<ManualPdfAnchorCreationRequest> {
        dialogParentProvided = parent != nullptr;
        return std::nullopt;
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    searchEdit->setText(QStringLiteral("Keyboard command"));
    QCOMPARE(results->count(), 1);
    QCOMPARE(panel.currentOpenTarget().resourceId, resource.id);

    QTest::keyClick(searchEdit, Qt::Key_Return);
    QCOMPARE(activationCount, 1);

    QTest::keyClick(searchEdit, Qt::Key_K, Qt::ControlModifier);
    QVERIFY(dialogParentProvided);
    QCOMPARE(statusNotifications.last(), QStringLiteral("Capture canceled"));

    QTest::keyClick(searchEdit, Qt::Key_A, Qt::AltModifier);
    std::optional<Resource> withAlias = repository.findResource(resource.id);
    QVERIFY(withAlias.has_value());
    QVERIFY(withAlias->anchors.first().aliases.contains(QStringLiteral("Keyboard command")));
    QVERIFY(statusNotifications.last().contains(QStringLiteral("Added anchor alias")));

    QTest::keyClick(searchEdit, Qt::Key_T, Qt::AltModifier);
    std::optional<Resource> withTag = repository.findResource(resource.id);
    QVERIFY(withTag.has_value());
    QVERIFY(withTag->anchors.first().tags.contains(QStringLiteral("Keyboard command")));
    QVERIFY(statusNotifications.last().contains(QStringLiteral("Added tag")));

    bool editDialogHandled = false;
    QTimer::singleShot(0, [&]() {
        QWidget *dialog = QApplication::activeModalWidget();
        if (!dialog) {
            return;
        }
        auto *nameEdit = dialog->findChild<QLineEdit *>(QStringLiteral("anchorNameEdit"));
        auto *aliasesEdit = dialog->findChild<QLineEdit *>(QStringLiteral("anchorAliasesEdit"));
        auto *tagsEdit = dialog->findChild<QLineEdit *>(QStringLiteral("anchorTagsEdit"));
        auto *buttons = dialog->findChild<QDialogButtonBox *>(QStringLiteral("anchorEditButtons"));
        if (!nameEdit || !aliasesEdit || !tagsEdit || !buttons) {
            return;
        }
        nameEdit->setText(QStringLiteral("Edited keyboard command"));
        aliasesEdit->setText(QStringLiteral("edited alias"));
        tagsEdit->setText(QStringLiteral("edited-tag"));
        editDialogHandled = true;
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    QTest::keyClick(searchEdit, Qt::Key_E, Qt::ControlModifier);
    QVERIFY(editDialogHandled);
    std::optional<Resource> edited = repository.findResource(resource.id);
    QVERIFY(edited.has_value());
    QCOMPARE(edited->anchors.first().name, QStringLiteral("Edited keyboard command"));
    QCOMPARE(edited->anchors.first().aliases, QStringList{QStringLiteral("edited alias")});
    QCOMPARE(edited->anchors.first().tags, QStringList{QStringLiteral("edited-tag")});
    QVERIFY(statusNotifications.last().contains(QStringLiteral("Updated anchor")));

    panel.setSearchText(QStringLiteral("Edited keyboard command"));
    QTest::keyClick(searchEdit, Qt::Key_Delete);
    QVERIFY(statusNotifications.last().contains(QStringLiteral("Anchor deletion is not implemented yet")));
}

void WidgetSmokeTest::panelRoutesCtrlKThroughManualPdfAnchorRequestProvider()
{
    InMemoryLibraryRepository repository;

    int requestCount = 0;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };
    options.manualPdfAnchorRequestProvider = [&]() -> std::optional<ManualPdfAnchorCreationRequest> {
        ++requestCount;
        ManualPdfAnchorCreationRequest request;
        request.name = QStringLiteral("Manual flow window");
        request.file = QStringLiteral("E:/docs/manual-flow.pdf");
        request.page = 3;
        request.rect = {10.0, 20.0, 110.0, 80.0};
        request.zoom = 175.0;
        request.aliases = {QStringLiteral("flow alias")};
        request.tags = {QStringLiteral("#phase4")};
        request.pinned = true;
        return request;
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    QVERIFY(searchEdit);

    QTest::keyClick(searchEdit, Qt::Key_K, Qt::ControlModifier);

    QCOMPARE(requestCount, 1);
    QVERIFY(statusNotifications.last().contains(QStringLiteral("Created PDF anchor")));

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("flow alias")});
    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(results.first().matchedAnchor->name, QStringLiteral("Manual flow window"));
    QCOMPARE(results.first().matchedAnchor->targetFile, QStringLiteral("E:/docs/manual-flow.pdf"));
    QCOMPARE(results.first().matchedAnchor->locatorType, QStringLiteral("pdfxchange.rect"));
    QCOMPARE(results.first().matchedAnchor->tags, QStringList{QStringLiteral("phase4")});
    QVERIFY(results.first().matchedAnchor->pinned);
}

void WidgetSmokeTest::panelCreatesManualPdfAnchorThroughDialogHook()
{
    InMemoryLibraryRepository repository;

    int dialogCount = 0;
    bool parentProvided = false;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };
    options.manualPdfAnchorDialogHandler = [&](QWidget *parent) -> std::optional<ManualPdfAnchorCreationRequest> {
        ++dialogCount;
        parentProvided = parent != nullptr;
        ManualPdfAnchorCreationRequest request;
        request.name = QStringLiteral("Dialog pick window");
        request.file = QStringLiteral("E:/docs/dialog-pick.pdf");
        request.page = 7;
        request.rect = {12.0, 24.0, 220.0, 140.0};
        request.zoom = 150.0;
        request.aliases = {QStringLiteral("dialog alias")};
        request.tags = {QStringLiteral("#dialog-tag")};
        request.pinned = true;
        return request;
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    QVERIFY(searchEdit);

    panel.setSearchText(QStringLiteral("unrelated-filter"));
    QCOMPARE(panel.resultCount(), 0);

    QTest::keyClick(searchEdit, Qt::Key_K, Qt::ControlModifier);

    QCOMPARE(dialogCount, 1);
    QVERIFY(parentProvided);
    QCOMPARE(panel.statusText(), QStringLiteral("Created PDF anchor \"Dialog pick window\""));
    QCOMPARE(statusNotifications.last(), panel.statusText());
    QCOMPARE(panel.searchText(), QStringLiteral("Dialog pick window"));
    QCOMPARE(panel.resultCount(), 1);
    QCOMPARE(panel.currentOpenTarget().resourceKind, ResourceKind::Pdf);
    QVERIFY(panel.currentOpenTarget().anchor.has_value());
    QCOMPARE(panel.currentOpenTarget().anchor->name, QStringLiteral("Dialog pick window"));
    QCOMPARE(panel.currentOpenTarget().anchor->targetApp, QStringLiteral("PDF-XChange"));
    QCOMPARE(panel.currentOpenTarget().anchor->targetFile, QStringLiteral("E:/docs/dialog-pick.pdf"));
    QCOMPARE(panel.currentOpenTarget().anchor->locatorType, QStringLiteral("pdfxchange.rect"));
    QVERIFY(panel.currentOpenTarget().anchor->pinned);

    const QList<SearchResult> aliasResults = repository.search(SearchQuery{QStringLiteral("dialog alias")});
    QCOMPARE(aliasResults.size(), 1);
    QVERIFY(aliasResults.first().matchedAnchor.has_value());
    QCOMPARE(aliasResults.first().matchedAnchor->tags, QStringList{QStringLiteral("dialog-tag")});
}

void WidgetSmokeTest::panelCancelsManualPdfAnchorDialogHookWithoutSaving()
{
    InMemoryLibraryRepository repository;

    int dialogCount = 0;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };
    options.manualPdfAnchorDialogHandler = [&](QWidget *parent) -> std::optional<ManualPdfAnchorCreationRequest> {
        Q_UNUSED(parent);
        ++dialogCount;
        return std::nullopt;
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    QVERIFY(searchEdit);

    QTest::keyClick(searchEdit, Qt::Key_K, Qt::ControlModifier);

    QCOMPARE(dialogCount, 1);
    QCOMPARE(panel.statusText(), QStringLiteral("Capture canceled"));
    QCOMPARE(statusNotifications.last(), panel.statusText());
    QCOMPARE(panel.resultCount(), 0);
    QVERIFY(repository.search(SearchQuery{}).isEmpty());
}

void WidgetSmokeTest::panelReportsInvalidManualPdfAnchorDialogHookRequest()
{
    InMemoryLibraryRepository repository;

    int dialogCount = 0;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };
    options.manualPdfAnchorDialogHandler = [&](QWidget *parent) -> std::optional<ManualPdfAnchorCreationRequest> {
        Q_UNUSED(parent);
        ++dialogCount;
        ManualPdfAnchorCreationRequest request;
        request.name = QStringLiteral("Invalid dialog request");
        request.file = QStringLiteral("E:/docs/invalid-dialog.pdf");
        request.page = 0;
        request.rect = {10.0, 20.0, 110.0, 80.0};
        request.zoom = 125.0;
        return request;
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    QVERIFY(searchEdit);

    QTest::keyClick(searchEdit, Qt::Key_K, Qt::ControlModifier);

    QCOMPARE(dialogCount, 1);
    QCOMPARE(panel.statusText(), QStringLiteral("PDF-XChange capture page is missing"));
    QCOMPARE(statusNotifications.last(), panel.statusText());
    QCOMPARE(panel.resultCount(), 0);
    QVERIFY(repository.search(SearchQuery{}).isEmpty());
}

void WidgetSmokeTest::panelLaunchesExcelAnchorWithInjectedExecutor()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("excel-budget");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Budget Workbook");
    resource.location = QStringLiteral("E:/books/budget.xlsx");
    Anchor anchor;
    anchor.type = AnchorType::Manual;
    anchor.name = QStringLiteral("Q3 budget table");
    anchor.targetApp = QStringLiteral("mIcRoSoFt Excel");
    anchor.targetFile = resource.location;
    anchor.locatorType = QStringLiteral("excel.range");
    anchor.locatorJson = QStringLiteral("{\"sheet\":\"Sheet1\",\"range\":\"B12:D18\"}");
    resource.anchors = {anchor};
    QVERIFY(repository.upsertResource(resource));

    bool launched = false;
    ExcelJumpCommand capturedCommand;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.excelLaunchHandler = [&](const ExcelJumpCommand &command, QString *error) {
        Q_UNUSED(error);
        launched = true;
        capturedCommand = command;
        return true;
    };
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    QVERIFY(searchEdit);

    panel.setSearchText(QStringLiteral("Q3 budget"));
    QCOMPARE(panel.resultCount(), 1);

    QTest::keyClick(searchEdit, Qt::Key_Return);

    QVERIFY(launched);
    QCOMPARE(capturedCommand.workbookPath, resource.location);
    QCOMPARE(capturedCommand.locatorType, QStringLiteral("excel.range"));
    QCOMPARE(capturedCommand.sheetName, QStringLiteral("Sheet1"));
    QCOMPARE(capturedCommand.rangeAddress, QStringLiteral("B12:D18"));
    QCOMPARE(panel.statusText(), QStringLiteral("Opened Excel target"));
    QCOMPARE(statusNotifications.last(), panel.statusText());

    const std::optional<ResourceUsage> usage = repository.resourceUsage(resource.id);
    QVERIFY(usage.has_value());
    QCOMPARE(usage->openCount, 1);

    const std::optional<Resource> openedResource = repository.findResource(resource.id);
    QVERIFY(openedResource.has_value());
    const std::optional<AnchorUsage> anchorUsage = repository.anchorUsage(resource.id, openedResource->anchors.first());
    QVERIFY(anchorUsage.has_value());
    QCOMPARE(anchorUsage->openCount, 1);
}

void WidgetSmokeTest::panelReportsInvalidExcelLocatorWithoutGenericOpen()
{
    CapturingUrlHandler fileHandler;
    QDesktopServices::setUrlHandler(QStringLiteral("file"), &fileHandler, "openUrl");

    {
        InMemoryLibraryRepository repository;

        Resource resource;
        resource.id = QStringLiteral("excel-unsupported");
        resource.kind = ResourceKind::File;
        resource.title = QStringLiteral("Unsupported Workbook");
        resource.location = QStringLiteral("E:/books/unsupported.xlsx");
        Anchor anchor;
        anchor.type = AnchorType::Manual;
        anchor.name = QStringLiteral("Unsupported Excel jump");
        anchor.targetApp = QStringLiteral("Excel");
        anchor.targetFile = resource.location;
        anchor.locatorType = QStringLiteral("excel.cell");
        anchor.locatorJson = QStringLiteral("{\"sheet\":\"Sheet1\",\"range\":\"B12\"}");
        resource.anchors = {anchor};
        QVERIFY(repository.upsertResource(resource));

        PinloomPanel panel(repository);
        panel.setSearchText(QStringLiteral("Unsupported Excel"));

        QVERIFY(!panel.activateCurrentOpenTarget());
        QCOMPARE(panel.statusText(), QStringLiteral("Excel locator type is unsupported"));
        QCOMPARE(fileHandler.openCount, 0);
        QVERIFY(!repository.resourceUsage(resource.id).has_value());
    }

    {
        InMemoryLibraryRepository repository;

        Resource resource;
        resource.id = QStringLiteral("excel-missing-range");
        resource.kind = ResourceKind::File;
        resource.title = QStringLiteral("Missing Range Workbook");
        resource.location = QStringLiteral("E:/books/missing-range.xlsx");
        Anchor anchor;
        anchor.type = AnchorType::Manual;
        anchor.name = QStringLiteral("Missing Excel range");
        anchor.targetFile = resource.location;
        anchor.locatorType = QStringLiteral("excel.range");
        anchor.locatorJson = QStringLiteral("{\"sheet\":\"Sheet1\"}");
        resource.anchors = {anchor};
        QVERIFY(repository.upsertResource(resource));

        PinloomPanel panel(repository);
        panel.setSearchText(QStringLiteral("Missing Excel"));

        QVERIFY(!panel.activateCurrentOpenTarget());
        QCOMPARE(panel.statusText(), QStringLiteral("Excel range address is missing"));
        QCOMPARE(fileHandler.openCount, 0);
        QVERIFY(!repository.resourceUsage(resource.id).has_value());
    }

    QDesktopServices::unsetUrlHandler(QStringLiteral("file"));
}

void WidgetSmokeTest::panelLaunchesPdfXChangeAnchorWithInjectedExecutor()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("pdf-clock");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Clock Spec");
    resource.location = QStringLiteral("E:/docs/clock.pdf");
    Anchor anchor;
    anchor.type = AnchorType::PdfRegion;
    anchor.name = QStringLiteral("PLL jitter budget");
    anchor.targetApp = QStringLiteral("PDF-XChange");
    anchor.targetFile = resource.location;
    anchor.locatorType = QStringLiteral("pdfxchange.rect");
    anchor.locatorJson = QStringLiteral("{\"page\":12,\"rect\":[420,860,780,920],\"zoom\":250,\"unit\":\"pt\"}");
    resource.anchors = {anchor};
    QVERIFY(repository.upsertResource(resource));

    bool launched = false;
    PdfXChangeCommand capturedCommand;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.pdfXChangeExecutablePathProvider = []() {
        return QStringLiteral("C:/Tools/PDFXEdit.exe");
    };
    options.pdfXChangeLaunchHandler = [&](const PdfXChangeCommand &command, QString *error) {
        Q_UNUSED(error);
        launched = true;
        capturedCommand = command;
        return true;
    };
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };

    PinloomPanel panel(repository, options);
    panel.setSearchText(QStringLiteral("PLL jitter"));
    QVERIFY(panel.selectFirstResult());

    QVERIFY(panel.activateCurrentOpenTarget());
    QVERIFY(launched);
    QCOMPARE(capturedCommand.executablePath, QStringLiteral("C:/Tools/PDFXEdit.exe"));
    QCOMPARE(capturedCommand.action, QStringLiteral("page=12;zoom=250;highlight=420,860,780,920;usept=yes"));
    QCOMPARE(capturedCommand.arguments,
             QStringList({QStringLiteral("/A"),
                          QStringLiteral("page=12;zoom=250;highlight=420,860,780,920;usept=yes"),
                          resource.location}));
    QCOMPARE(panel.statusText(), QStringLiteral("Opened PDF-XChange target"));
    QCOMPARE(statusNotifications.last(), panel.statusText());

    const std::optional<ResourceUsage> usage = repository.resourceUsage(resource.id);
    QVERIFY(usage.has_value());
    QCOMPARE(usage->openCount, 1);

    const std::optional<Resource> openedResource = repository.findResource(resource.id);
    QVERIFY(openedResource.has_value());
    const std::optional<AnchorUsage> anchorUsage = repository.anchorUsage(resource.id, openedResource->anchors.first());
    QVERIFY(anchorUsage.has_value());
    QCOMPARE(anchorUsage->openCount, 1);
}

void WidgetSmokeTest::panelReportsMissingPdfXChangeExecutable()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("pdf-page");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Spec");
    resource.location = QStringLiteral("E:/docs/spec.pdf");
    Anchor anchor;
    anchor.type = AnchorType::PdfPage;
    anchor.target = QStringLiteral("Page 7");
    anchor.page = 7;
    resource.anchors = {anchor};
    QVERIFY(repository.upsertResource(resource));

    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.pdfXChangeExecutablePathProvider = []() {
        return QString();
    };
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };

    PinloomPanel panel(repository, options);
    panel.setSearchText(QStringLiteral("Page 7"));
    QVERIFY(panel.selectFirstResult());

    QVERIFY(!panel.activateCurrentOpenTarget());
    QCOMPARE(panel.statusText(), QStringLiteral("PDF-XChange executable is not configured/found"));
    QCOMPARE(statusNotifications.last(), panel.statusText());
    QVERIFY(!repository.resourceUsage(resource.id).has_value());
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
    QVERIFY(capturedTarget.matchSummary.contains(QStringLiteral("Match: title")));
    QVERIFY(capturedTarget.matchSummary.contains(QStringLiteral("Context tag: zeroslack")));
    QVERIFY(capturedTarget.matchSummary.contains(QStringLiteral("Context location: https://docs.example.com")));
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
    QVERIFY(results->item(0)->text().contains(QStringLiteral("install")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("url.fragment")));

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

    const std::optional<Resource> openedResource = repository.findResource(resource.id);
    QVERIFY(openedResource.has_value());
    const std::optional<AnchorUsage> anchorUsage = repository.anchorUsage(resource.id, openedResource->anchors.first());
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
