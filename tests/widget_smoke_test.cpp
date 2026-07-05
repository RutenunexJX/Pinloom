#include "pinloom/clip/ClipArchive.h"
#include "pinloom/clip/ClipHotkeyService.h"
#include "pinloom/clip/ClipRepository.h"
#include "pinloom/clip/ClipSearch.h"
#include "pinloom/clip/ClipTrayController.h"
#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/widgets/ClipPickerPanel.h"
#include "pinloom/widgets/ClipResidentApp.h"
#include "pinloom/widgets/ClipResidentAppConfigStore.h"
#include "pinloom/widgets/ClipResidentHost.h"
#include "pinloom/widgets/ClipResidentRuntime.h"
#include "pinloom/widgets/ClipTrayPresenter.h"
#include "pinloom/widgets/MainPanelHotkey.h"
#include "pinloom/widgets/ManualPdfAnchorDialog.h"
#include "pinloom/widgets/PinloomMainWindow.h"
#include "pinloom/widgets/PinloomPanel.h"
#include "pinloom/widgets/TextPreviewDialog.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QApplication>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <memory>
#include <optional>

using namespace Pinloom;

class WidgetSmokeTest : public QObject {
    Q_OBJECT

private slots:
    void clipPickerEmptyQueryShowsSavedPinnedRecentOnly();
    void clipPickerRefreshesForNameAliasTagAndHashTag();
    void clipPickerEnterActivatesInjectedInsertionHandler();
    void clipPickerSavedSearchEnterInsertsTextThroughService();
    void clipPickerImportedSavedClipEnterInsertsTextThroughService();
    void clipPickerShowsErrorAndStaysOpenOnInsertionFailure();
    void clipPickerCanIncludeTemporaryResults();
    void clipPickerTemporaryHistoryShowsTimestampAndHidesSavedItems();
    void clipPickerSavesTemporaryClipForNameAliasTagSearch();
    void clipTrayPresenterShowsAndRoutesTrayActions();
    void clipTrayPresenterUpdatesPauseResumeState();
    void clipTrayPresenterSyncsRuntimeStatusAndErrors();
    void clipResidentRuntimeStartsStopsCaptureHotkeyAndTray();
    void clipResidentRuntimeCanRunWithoutRegisteringClipHotkey();
    void clipResidentRuntimeShowsAndFocusesPickerFromHotkeyAndTray();
    void clipResidentRuntimeDefaultPickerShowsCapturedClipboardText();
    void clipResidentRuntimeCapturesTemporaryClipAndInsertsThroughPicker();
    void clipResidentRuntimePickerInsertionSuppressesOwnClipboardWrite();
    void clipResidentRuntimePauseResumeAndQuitActions();
    void clipResidentFactoryReportsMissingDependencies();
    void clipResidentFactoryCreatesInMemoryAndSqliteHosts();
    void clipResidentHostForwardsStartStopQuitToRuntime();
    void clipResidentHostPreservesRuntimeShowPauseAndSuppressionFlow();
    void clipResidentAppRejectsSqliteConfigWithoutPath();
    void clipResidentAppCreatesStartsStopsSqliteHostWithOptions();
    void clipResidentAppForwardsRequestQuitAndReportsStartErrors();
    void clipResidentAppPreservesHostRuntimeWorkflow();
    void clipResidentAppConfigStoreRoundTripsExplicitJsonFile();
    void clipResidentAppConfigStoreReturnsDefaultForMissingExplicitFile();
    void clipResidentAppConfigStoreReportsInvalidJsonAndFields();
    void clipResidentAppConfigStoreRejectsInvalidConfigAndWriteFailures();
    void clipResidentAppConfigStoreConfiguresAppFromExplicitFile();
    void clipResidentAppConfigStoreRequiresExplicitPathWithoutUserDataFallback();
    void mainWindowCloseHidesToTray();
    void mainPanelHotkeyRegistersAndShowsMainWindow();
    void panelUsesInjectedRepository();
    void panelLoadsSavedLibraryRoots();
    void panelExposesHostIndexingControls();
    void panelDefaultsToLauncherSurface();
    void panelSearchesSavedClipsAndEnterInserts();
    void panelClipRootCommandShowsCandidates();
    void panelClipSearchCommandSearchesHistoryAndSavedClipsAndEnterInserts();
    void panelClipNewCommandShowsTemporaryHistoryAndSaves();
    void panelAnchorCaptureCommandShowsPendingEntry();
    void panelDisplaysAnchorAwareResults();
    void panelDisplaysAnchorLocatorMetadata();
    void panelDisplaysMarkerAnchors();
    void panelDisplaysBeaconLineResults();
    void panelDisplaysFileLineResults();
    void panelDisplaysPdfPageResults();
    void panelFiltersLegacyPdfManualLineAnchors();
    void panelPreservesPdfRegionOpenTarget();
    void panelDisplaysRelationSummary();
    void panelExposesCurrentRelatedTargetsForHostPreview();
    void panelAddsManualAliasAndAnchor();
    void panelRejectsGenericManualPdfLineAnchors();
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
    void panelReportsNoPdfContextForPdfCapture();
    void panelCapturesSelectedPdfFallbackAnchorWithMetadataOnly();
    void manualPdfCaptureDialogKeepsRawCoordinatesAdvancedByDefault();
    void panelRoutesCtrlKThroughManualPdfAnchorRequestProvider();
    void panelCreatesManualPdfAnchorThroughDialogHook();
    void panelCancelsManualPdfAnchorDialogHookWithoutSaving();
    void panelReportsInvalidManualPdfAnchorDialogHookRequest();
    void panelRoutesCtrlKThroughManualExcelAnchorRequestProvider();
    void panelRoutesCtrlKThroughManualVisioAnchorRequestProvider();
    void panelRoutesCtrlKThroughManualWordAnchorRequestProvider();
    void panelRoutesCtrlKThroughManualPowerPointAnchorRequestProvider();
    void panelLaunchesExcelAnchorWithInjectedExecutor();
    void panelReportsInvalidExcelLocatorWithoutGenericOpen();
    void panelLaunchesVisioAnchorWithInjectedExecutor();
    void panelReportsInvalidVisioLocatorWithoutGenericOpen();
    void panelLaunchesWordAnchorWithInjectedExecutor();
    void panelReportsInvalidWordLocatorWithoutGenericOpen();
    void panelLaunchesPowerPointAnchorWithInjectedExecutor();
    void panelReportsInvalidPowerPointLocatorWithoutGenericOpen();
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

class FakeClipboardTextSource : public ClipboardTextSource {
    Q_OBJECT

public:
    QString text() const override
    {
        return text_;
    }

    void setText(const QString &text)
    {
        text_ = text;
        emit textChanged();
    }

private:
    QString text_;
};

class FakeClipboardTextAccessor : public ClipboardTextAccessor {
public:
    QString text() const override
    {
        return text_;
    }

    bool setText(const QString &text) override
    {
        text_ = text;
        writes_.append(text);
        return true;
    }

    bool isAvailable() const override
    {
        return true;
    }

    void setInitialText(const QString &text)
    {
        text_ = text;
    }

    QStringList writes() const
    {
        return writes_;
    }

private:
    QString text_;
    QStringList writes_;
};

class FakeClipHotkeyBackend : public ClipHotkeyBackend {
public:
    bool registerHotkey(const ClipHotkeyConfig &config, QString *error) override
    {
        ++registerCalls_;
        registeredConfig_ = config;
        if (!registerResult_) {
            if (error) {
                *error = registerError_;
            }
            return false;
        }

        registered_ = true;
        if (error) {
            error->clear();
        }
        return true;
    }

    void unregisterHotkey() override
    {
        ++unregisterCalls_;
        registered_ = false;
    }

    void activate()
    {
        emit hotkeyActivated();
    }

    void setRegisterResult(bool registerResult, const QString &error)
    {
        registerResult_ = registerResult;
        registerError_ = error;
    }

    int registerCalls() const
    {
        return registerCalls_;
    }

    int unregisterCalls() const
    {
        return unregisterCalls_;
    }

    bool registered() const
    {
        return registered_;
    }

    ClipHotkeyConfig registeredConfig() const
    {
        return registeredConfig_;
    }

private:
    bool registerResult_ = true;
    bool registered_ = false;
    int registerCalls_ = 0;
    int unregisterCalls_ = 0;
    QString registerError_ = QStringLiteral("fake hotkey registration failed");
    ClipHotkeyConfig registeredConfig_;
};

class FakeClipTrayBackend : public ClipTrayBackend {
public:
    void setToolTip(const QString &toolTip) override
    {
        toolTip_ = toolTip;
        ++toolTipChanges_;
    }

    void setActions(const QList<ClipTrayPresentedAction> &actions) override
    {
        actions_ = actions;
        ++actionChanges_;
    }

    void setVisible(bool visible) override
    {
        visible_ = visible;
        ++visibleChanges_;
    }

    void triggerAction(const QString &actionId)
    {
        emit actionTriggered(actionId);
    }

    void activatePrimary()
    {
        emit primaryActivated();
    }

    QString toolTip() const
    {
        return toolTip_;
    }

    QList<ClipTrayPresentedAction> actions() const
    {
        return actions_;
    }

    bool visible() const
    {
        return visible_;
    }

    int toolTipChanges() const
    {
        return toolTipChanges_;
    }

    int actionChanges() const
    {
        return actionChanges_;
    }

    int visibleChanges() const
    {
        return visibleChanges_;
    }

private:
    QString toolTip_;
    QList<ClipTrayPresentedAction> actions_;
    bool visible_ = false;
    int toolTipChanges_ = 0;
    int actionChanges_ = 0;
    int visibleChanges_ = 0;
};

static std::optional<ClipTrayPresentedAction> presentedActionById(const QList<ClipTrayPresentedAction> &actions,
                                                                  const QString &id)
{
    const auto it = std::find_if(actions.cbegin(), actions.cend(), [&](const ClipTrayPresentedAction &action) {
        return action.id == id;
    });
    if (it == actions.cend()) {
        return std::nullopt;
    }
    return *it;
}

static QString saveWidgetClip(InMemoryClipRepository &repository,
                              const QString &text,
                              const QString &name,
                              const QStringList &aliases,
                              const QStringList &tags,
                              bool pinned,
                              const QDateTime &capturedAt,
                              const QDateTime &savedAt)
{
    const ClipCaptureResult captured = repository.captureText(text, {}, {}, capturedAt);
    if (!captured.captured() || !captured.clip.has_value()) {
        return {};
    }

    if (!repository.saveClip(captured.clip->id, name, aliases, tags, pinned, savedAt)) {
        return {};
    }

    return captured.clip->id;
}

static ClipResidentRuntimeDependencies makeResidentRuntimeDependencies(FakeClipboardTextSource &captureClipboard,
                                                                       FakeClipboardTextAccessor &insertionClipboard,
                                                                       FakeClipHotkeyBackend &hotkeyBackend,
                                                                       FakeClipTrayBackend &trayBackend,
                                                                       int &pasteCalls)
{
    ClipResidentRuntimeDependencies dependencies;
    dependencies.captureClipboard = &captureClipboard;
    dependencies.insertionClipboard = &insertionClipboard;
    dependencies.hotkeyBackend = &hotkeyBackend;
    dependencies.trayBackend = &trayBackend;
    dependencies.pasteInvoker = [&pasteCalls]() {
        ++pasteCalls;
        return true;
    };
    return dependencies;
}

static void writeTestFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QCOMPARE(file.write(content), static_cast<qint64>(content.size()));
    file.close();
}

void WidgetSmokeTest::clipPickerEmptyQueryShowsSavedPinnedRecentOnly()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const ClipCaptureResult temporary = repository.captureText(QStringLiteral("temporary picker private text"),
                                                               {},
                                                               {},
                                                               base);
    QVERIFY(temporary.captured());
    QVERIFY(temporary.clip.has_value());

    const QString pinnedId = saveWidgetClip(repository,
                                            QStringLiteral("empty picker pinned text"),
                                            QStringLiteral("Pinned picker"),
                                            {},
                                            {QStringLiteral("favorite")},
                                            true,
                                            base.addSecs(1),
                                            base.addSecs(2));
    const QString recentId = saveWidgetClip(repository,
                                            QStringLiteral("empty picker recent text"),
                                            QStringLiteral("Recent picker"),
                                            {QStringLiteral("fresh")},
                                            {},
                                            false,
                                            base.addSecs(3),
                                            base.addSecs(4));
    const QString staleId = saveWidgetClip(repository,
                                           QStringLiteral("empty picker stale text"),
                                           QStringLiteral("Stale picker"),
                                           {},
                                           {},
                                           false,
                                           base.addSecs(5),
                                           base.addSecs(6));
    QVERIFY(!pinnedId.isEmpty());
    QVERIFY(!recentId.isEmpty());
    QVERIFY(!staleId.isEmpty());
    QVERIFY(repository.markClipUsed(staleId, base.addSecs(10)));
    QVERIFY(repository.markClipUsed(recentId, base.addSecs(100)));

    ClipSearchService search(repository);
    ClipPickerPanel picker(search);
    auto *searchEdit = picker.findChild<QLineEdit *>(QStringLiteral("clipPickerSearchEdit"));
    auto *resultsList = picker.findChild<QListWidget *>(QStringLiteral("clipPickerResultList"));
    QVERIFY(searchEdit);
    QVERIFY(resultsList);

    QCOMPARE(searchEdit->text(), QString());
    QCOMPARE(picker.resultCount(), 3);
    const QList<ClipSearchResult> results = picker.currentResults();
    QCOMPARE(results.at(0).clipId, pinnedId);
    QCOMPARE(results.at(1).clipId, recentId);
    QCOMPARE(results.at(2).clipId, staleId);
    QVERIFY(results.at(0).pinned);
    QVERIFY(std::none_of(results.cbegin(), results.cend(), [&](const ClipSearchResult &result) {
        return result.clipId == temporary.clip->id;
    }));
    QVERIFY(resultsList->item(0)->text().contains(QStringLiteral("Pinned picker")));
    QVERIFY(resultsList->item(0)->text().contains(QStringLiteral("#favorite")));
    QVERIFY(resultsList->item(0)->text().contains(QStringLiteral("rank 1")));
    QVERIFY(resultsList->item(0)->text().contains(QStringLiteral("score")));
}

void WidgetSmokeTest::clipPickerRefreshesForNameAliasTagAndHashTag()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    const QString clipId = saveWidgetClip(repository,
                                          QStringLiteral("Reusable launch command body"),
                                          QStringLiteral("Launch Command"),
                                          {QStringLiteral("launcher alias")},
                                          {QStringLiteral("ops")},
                                          true,
                                          base,
                                          base.addSecs(1));
    QVERIFY(!clipId.isEmpty());

    ClipSearchService search(repository);
    ClipPickerPanel picker(search);
    auto *searchEdit = picker.findChild<QLineEdit *>(QStringLiteral("clipPickerSearchEdit"));
    auto *resultsList = picker.findChild<QListWidget *>(QStringLiteral("clipPickerResultList"));
    QVERIFY(searchEdit);
    QVERIFY(resultsList);

    searchEdit->setText(QStringLiteral("Launch Command"));
    QCOMPARE(picker.resultCount(), 1);
    QCOMPARE(picker.currentResult().clipId, clipId);
    QCOMPARE(picker.currentResult().matchedField, QStringLiteral("name"));
    QCOMPARE(picker.currentResult().matchedValue, QStringLiteral("Launch Command"));
    QVERIFY(picker.currentResult().pinned);
    QCOMPARE(picker.currentResult().rank, 1);
    QVERIFY(picker.currentResult().score > 0.0);

    searchEdit->setText(QStringLiteral("launcher alias"));
    QCOMPARE(picker.resultCount(), 1);
    QCOMPARE(picker.currentResult().clipId, clipId);
    QCOMPARE(picker.currentResult().matchedField, QStringLiteral("alias"));

    searchEdit->setText(QStringLiteral("ops"));
    QCOMPARE(picker.resultCount(), 1);
    QCOMPARE(picker.currentResult().clipId, clipId);
    QCOMPARE(picker.currentResult().matchedField, QStringLiteral("tag"));

    searchEdit->setText(QStringLiteral("#ops"));
    QCOMPARE(picker.resultCount(), 1);
    QCOMPARE(picker.currentResult().clipId, clipId);
    QCOMPARE(picker.currentResult().matchedField, QStringLiteral("tag"));
    QVERIFY(resultsList->item(0)->text().contains(QStringLiteral("aliases: launcher alias")));
    QVERIFY(resultsList->item(0)->text().contains(QStringLiteral("#ops")));
    QVERIFY(resultsList->item(0)->text().contains(QStringLiteral("rank 1")));
    QVERIFY(resultsList->item(0)->text().contains(QStringLiteral("score")));
}

void WidgetSmokeTest::clipPickerEnterActivatesInjectedInsertionHandler()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    const QString clipId = saveWidgetClip(repository,
                                          QStringLiteral("Insertable clip text"),
                                          QStringLiteral("Insertable clip"),
                                          {},
                                          {},
                                          false,
                                          base,
                                          base.addSecs(1));
    QVERIFY(!clipId.isEmpty());

    QStringList activatedIds;
    ClipPickerOptions options;
    options.closeOnActivationSuccess = false;
    options.insertionHandler = [&](const QString &selectedClipId, QString *error) {
        if (error) {
            error->clear();
        }
        activatedIds.append(selectedClipId);
        return true;
    };

    ClipSearchService search(repository);
    ClipPickerPanel picker(search, options);
    auto *searchEdit = picker.findChild<QLineEdit *>(QStringLiteral("clipPickerSearchEdit"));
    QVERIFY(searchEdit);
    QCOMPARE(picker.currentResult().clipId, clipId);

    QTest::keyClick(searchEdit, Qt::Key_Return);

    QCOMPARE(activatedIds, QStringList{clipId});
    QVERIFY(picker.lastActivationSucceeded());
    QVERIFY(picker.lastError().isEmpty());
    QCOMPARE(picker.statusText(), QStringLiteral("Inserted clip"));
}

void WidgetSmokeTest::clipPickerSavedSearchEnterInsertsTextThroughService()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const ClipCaptureResult temporary = repository.captureText(QStringLiteral("temporary default history"),
                                                               {},
                                                               {},
                                                               base);
    QVERIFY(temporary.captured());
    QVERIFY(temporary.clip.has_value());
    const QString savedId = saveWidgetClip(repository,
                                           QStringLiteral("Saved clip body for insertion"),
                                           QStringLiteral("Saved Insertable"),
                                           {QStringLiteral("saved insert alias")},
                                           {QStringLiteral("saved-insert")},
                                           false,
                                           base.addSecs(1),
                                           base.addSecs(2));
    QVERIFY(!savedId.isEmpty());

    FakeClipboardTextAccessor clipboard;
    int pasteCalls = 0;
    ClipInsertionService insertion(&clipboard, repository, [&]() {
        ++pasteCalls;
        return true;
    });
    ClipInsertionOptions insertionOptions;
    insertionOptions.restoreOriginalClipboardOnSuccess = false;
    insertion.setOptions(insertionOptions);

    ClipSearchOptions searchOptions;
    searchOptions.includeTemporary = true;
    ClipPickerOptions options;
    options.searchOptions = searchOptions;
    options.closeOnActivationSuccess = false;
    options.insertionHandler = makeClipPickerInsertionHandler(insertion);

    ClipSearchService search(repository);
    ClipPickerPanel picker(search, options);
    auto *searchEdit = picker.findChild<QLineEdit *>(QStringLiteral("clipPickerSearchEdit"));
    QVERIFY(searchEdit);

    QCOMPARE(picker.query(), QString());
    QCOMPARE(picker.resultCount(), 1);
    QCOMPARE(picker.currentResult().clipId, temporary.clip->id);

    picker.setQuery(QStringLiteral("saved insert alias"));
    QCOMPARE(picker.resultCount(), 1);
    QCOMPARE(picker.currentResult().clipId, savedId);
    QVERIFY(picker.currentResult().state == ClipState::Saved);

    QTest::keyClick(searchEdit, Qt::Key_Return);

    QCOMPARE(pasteCalls, 1);
    QCOMPARE(clipboard.text(), QStringLiteral("Saved clip body for insertion"));
    QCOMPARE(insertion.lastInsertedId(), savedId);
    QVERIFY(picker.lastActivationSucceeded());
    QCOMPARE(picker.statusText(), QStringLiteral("Inserted clip"));
}

void WidgetSmokeTest::clipPickerImportedSavedClipEnterInsertsTextThroughService()
{
    InMemoryClipRepository source;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const QString sourceId = saveWidgetClip(source,
                                            QStringLiteral("Imported saved clip body"),
                                            QStringLiteral("Imported Saved Insert"),
                                            {QStringLiteral("import insert alias")},
                                            {QStringLiteral("import-insert")},
                                            false,
                                            base,
                                            base.addSecs(1));
    QVERIFY(!sourceId.isEmpty());

    const QString archivePath = dir.filePath(QStringLiteral("saved-clips.json"));
    const ClipArchiveResult exported = ClipArchive(source).exportSavedClips(archivePath);
    QVERIFY2(exported.succeeded(), qPrintable(exported.error));

    SqliteClipRepository imported;
    QVERIFY2(imported.open(dir.filePath(QStringLiteral("pinloom_clip.sqlite3"))), qPrintable(imported.lastError()));
    QVERIFY2(imported.initialize(), qPrintable(imported.lastError()));
    const ClipArchiveResult importedResult = ClipArchive(imported).importSavedClips(archivePath);
    QVERIFY2(importedResult.succeeded(), qPrintable(importedResult.error));
    QCOMPARE(importedResult.imported, 1);

    FakeClipboardTextAccessor clipboard;
    int pasteCalls = 0;
    ClipInsertionService insertion(&clipboard, imported, [&]() {
        ++pasteCalls;
        return true;
    });
    ClipInsertionOptions insertionOptions;
    insertionOptions.restoreOriginalClipboardOnSuccess = false;
    insertion.setOptions(insertionOptions);

    ClipPickerOptions options;
    options.closeOnActivationSuccess = false;
    options.insertionHandler = makeClipPickerInsertionHandler(insertion);

    ClipSearchService search(imported);
    ClipPickerPanel picker(search, options);
    auto *searchEdit = picker.findChild<QLineEdit *>(QStringLiteral("clipPickerSearchEdit"));
    QVERIFY(searchEdit);

    picker.setQuery(QStringLiteral("#import-insert"));
    QCOMPARE(picker.resultCount(), 1);
    QCOMPARE(picker.currentResult().clipId, sourceId);
    QVERIFY(picker.currentResult().state == ClipState::Saved);

    QTest::keyClick(searchEdit, Qt::Key_Return);

    QCOMPARE(pasteCalls, 1);
    QCOMPARE(clipboard.text(), QStringLiteral("Imported saved clip body"));
    QCOMPARE(insertion.lastInsertedId(), sourceId);
    QVERIFY(picker.lastActivationSucceeded());
}

void WidgetSmokeTest::clipPickerShowsErrorAndStaysOpenOnInsertionFailure()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    const QString clipId = saveWidgetClip(repository,
                                          QStringLiteral("Failing insert clip text"),
                                          QStringLiteral("Failing insert"),
                                          {},
                                          {},
                                          false,
                                          base,
                                          base.addSecs(1));
    QVERIFY(!clipId.isEmpty());

    QStringList failedIds;
    ClipPickerOptions options;
    options.insertionHandler = [&](const QString &selectedClipId, QString *error) {
        failedIds.append(selectedClipId);
        if (error) {
            *error = QStringLiteral("paste target unavailable");
        }
        return false;
    };

    ClipSearchService search(repository);
    ClipPickerPanel picker(search, options);
    auto *searchEdit = picker.findChild<QLineEdit *>(QStringLiteral("clipPickerSearchEdit"));
    auto *status = picker.findChild<QLabel *>(QStringLiteral("clipPickerStatusLabel"));
    QVERIFY(searchEdit);
    QVERIFY(status);
    QStringList signalFailedIds;
    QStringList signalErrors;
    QObject::connect(&picker,
                     &ClipPickerPanel::activationFailed,
                     [&](const QString &failedClipId, const QString &error) {
                         signalFailedIds.append(failedClipId);
                         signalErrors.append(error);
                     });
    picker.show();
    QVERIFY(picker.isVisible());

    QTest::keyClick(searchEdit, Qt::Key_Return);

    QCOMPARE(failedIds, QStringList{clipId});
    QVERIFY(!picker.lastActivationSucceeded());
    QCOMPARE(picker.lastError(), QStringLiteral("paste target unavailable"));
    QCOMPARE(status->text(), QStringLiteral("paste target unavailable"));
    QCOMPARE(signalFailedIds, QStringList{clipId});
    QCOMPARE(signalErrors, QStringList{QStringLiteral("paste target unavailable")});
    QVERIFY(picker.isVisible());
}

void WidgetSmokeTest::clipPickerCanIncludeTemporaryResults()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const ClipCaptureResult temporary = repository.captureText(QStringLiteral("temporary visible needle"),
                                                               {},
                                                               {},
                                                               base);
    QVERIFY(temporary.captured());
    QVERIFY(temporary.clip.has_value());
    const QString savedId = saveWidgetClip(repository,
                                           QStringLiteral("saved visible needle"),
                                           QStringLiteral("Saved visible"),
                                           {},
                                           {},
                                           false,
                                           base.addSecs(1),
                                           base.addSecs(2));
    QVERIFY(!savedId.isEmpty());

    ClipSearchService search(repository);
    ClipPickerPanel defaultPicker(search);
    defaultPicker.setQuery(QStringLiteral("visible needle"));
    QCOMPARE(defaultPicker.resultCount(), 1);
    QCOMPARE(defaultPicker.currentResult().clipId, savedId);
    QVERIFY(defaultPicker.currentResult().state == ClipState::Saved);

    ClipSearchOptions searchOptions;
    searchOptions.includeTemporary = true;
    ClipPickerOptions options;
    options.searchOptions = searchOptions;
    ClipPickerPanel temporaryPicker(search, options);
    temporaryPicker.setQuery(QStringLiteral("visible needle"));
    const QList<ClipSearchResult> results = temporaryPicker.currentResults();
    QCOMPARE(results.size(), 2);
    QVERIFY(std::any_of(results.cbegin(), results.cend(), [&](const ClipSearchResult &result) {
        return result.clipId == temporary.clip->id && result.state == ClipState::Temporary;
    }));
    QVERIFY(std::any_of(results.cbegin(), results.cend(), [&](const ClipSearchResult &result) {
        return result.clipId == savedId && result.state == ClipState::Saved;
    }));
}

void WidgetSmokeTest::clipPickerTemporaryHistoryShowsTimestampAndHidesSavedItems()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const ClipCaptureResult temporary = repository.captureText(QStringLiteral("temporary timestamp history"),
                                                               {},
                                                               {},
                                                               base);
    QVERIFY(temporary.captured());
    QVERIFY(temporary.clip.has_value());

    const QString savedId = saveWidgetClip(repository,
                                           QStringLiteral("saved timestamp history"),
                                           QStringLiteral("Saved Timestamp History"),
                                           {QStringLiteral("saved timestamp alias")},
                                           {QStringLiteral("saved-timestamp")},
                                           false,
                                           base.addSecs(1),
                                           base.addSecs(2));
    QVERIFY(!savedId.isEmpty());

    ClipSearchOptions searchOptions;
    searchOptions.includeTemporary = true;
    ClipPickerOptions options;
    options.searchOptions = searchOptions;

    ClipSearchService search(repository);
    ClipPickerPanel picker(search, options);
    auto *resultsList = picker.findChild<QListWidget *>(QStringLiteral("clipPickerResultList"));
    QVERIFY(resultsList);

    QCOMPARE(picker.query(), QString());
    QCOMPARE(picker.resultCount(), 1);
    QCOMPARE(picker.currentResult().clipId, temporary.clip->id);
    QVERIFY(picker.currentResult().state == ClipState::Temporary);
    QVERIFY(resultsList->item(0)->text().contains(QStringLiteral("temporary")));
    QVERIFY(resultsList->item(0)->text().contains(
        base.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
    const QList<ClipSearchResult> defaultResults = picker.currentResults();
    QVERIFY(std::none_of(defaultResults.cbegin(), defaultResults.cend(), [&](const ClipSearchResult &result) {
        return result.clipId == savedId;
    }));

    QVERIFY(picker.saveCurrentClipAsSaved(QStringLiteral("Saved Temporary History"),
                                          {QStringLiteral("temporary history alias")},
                                          {QStringLiteral("temporary-history")},
                                          false));
    QCOMPARE(picker.resultCount(), 0);
    QCOMPARE(picker.statusText(), QStringLiteral("Saved clip"));

    picker.setQuery(QStringLiteral("Saved Temporary History"));
    QCOMPARE(picker.resultCount(), 1);
    QCOMPARE(picker.currentResult().clipId, temporary.clip->id);
    QVERIFY(picker.currentResult().state == ClipState::Saved);
    QCOMPARE(picker.currentResult().matchedField, QStringLiteral("name"));

    picker.setQuery(QStringLiteral("temporary history alias"));
    QCOMPARE(picker.resultCount(), 1);
    QCOMPARE(picker.currentResult().matchedField, QStringLiteral("alias"));

    picker.setQuery(QStringLiteral("#temporary-history"));
    QCOMPARE(picker.resultCount(), 1);
    QCOMPARE(picker.currentResult().matchedField, QStringLiteral("tag"));
}

void WidgetSmokeTest::clipPickerSavesTemporaryClipForNameAliasTagSearch()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    const ClipCaptureResult temporary = repository.captureText(QStringLiteral("temporary saveable body"),
                                                               {},
                                                               {},
                                                               base);
    QVERIFY(temporary.captured());
    QVERIFY(temporary.clip.has_value());

    ClipSearchOptions searchOptions;
    searchOptions.includeTemporary = true;
    ClipPickerOptions options;
    options.searchOptions = searchOptions;

    ClipSearchService search(repository);
    ClipPickerPanel picker(search, options);
    auto *saveButton = picker.findChild<QPushButton *>(QStringLiteral("clipPickerSaveButton"));
    QVERIFY(saveButton);

    picker.setQuery(QStringLiteral("saveable"));
    QCOMPARE(picker.resultCount(), 1);
    QCOMPARE(picker.currentResult().clipId, temporary.clip->id);
    QVERIFY(picker.currentResult().state == ClipState::Temporary);
    QVERIFY(saveButton->isEnabled());

    QVERIFY(picker.saveCurrentClipAsSaved(QStringLiteral("Saved Temporary"),
                                          {QStringLiteral("saved alias")},
                                          {QStringLiteral("#saved-tag")},
                                          true));

    const std::optional<Clip> saved = repository.findClip(temporary.clip->id);
    QVERIFY(saved.has_value());
    QVERIFY(saved->state == ClipState::Saved);
    QCOMPARE(saved->name, QStringLiteral("Saved Temporary"));
    QCOMPARE(saved->aliases, QStringList{QStringLiteral("saved alias")});
    QCOMPARE(saved->tags, QStringList{QStringLiteral("saved-tag")});
    QVERIFY(saved->pinned);
    QCOMPARE(picker.statusText(), QStringLiteral("Saved clip"));

    const QList<ClipSearchResult> nameResults = search.search(QStringLiteral("Saved Temporary"));
    QCOMPARE(nameResults.size(), 1);
    QCOMPARE(nameResults.first().clipId, temporary.clip->id);
    QCOMPARE(nameResults.first().matchedField, QStringLiteral("name"));

    const QList<ClipSearchResult> aliasResults = search.search(QStringLiteral("saved alias"));
    QCOMPARE(aliasResults.size(), 1);
    QCOMPARE(aliasResults.first().matchedField, QStringLiteral("alias"));

    const QList<ClipSearchResult> tagResults = search.search(QStringLiteral("#saved-tag"));
    QCOMPARE(tagResults.size(), 1);
    QCOMPARE(tagResults.first().matchedField, QStringLiteral("tag"));
}

void WidgetSmokeTest::clipTrayPresenterShowsAndRoutesTrayActions()
{
    FakeClipHotkeyBackend hotkeyBackend;
    ClipHotkeyService service(&hotkeyBackend);
    int showHandlerCount = 0;
    ClipTrayControllerOptions options;
    options.showPickerHandler = [&]() {
        ++showHandlerCount;
    };
    ClipTrayController controller(service, options);
    FakeClipTrayBackend trayBackend;
    ClipTrayPresenter presenter(controller, trayBackend);
    int showSignalCount = 0;
    int quitSignalCount = 0;
    QObject::connect(&controller, &ClipTrayController::showPickerRequested, [&]() {
        ++showSignalCount;
    });
    QObject::connect(&controller, &ClipTrayController::quitRequested, [&]() {
        ++quitSignalCount;
    });

    QCOMPARE(trayBackend.toolTip(), QStringLiteral("Pinloom Clip\nStopped"));
    QVERIFY(!trayBackend.visible());
    QCOMPARE(trayBackend.actionChanges(), 1);
    QVERIFY(presentedActionById(trayBackend.actions(), QStringLiteral("show_picker")).has_value());
    const std::optional<ClipTrayPresentedAction> quitAction =
        presentedActionById(trayBackend.actions(), QStringLiteral("quit"));
    QVERIFY(quitAction.has_value());
    QCOMPARE(quitAction->title, QStringLiteral("Quit Pinloom"));

    presenter.show();
    QVERIFY(trayBackend.visible());
    presenter.hide();
    QVERIFY(!trayBackend.visible());
    QCOMPARE(trayBackend.visibleChanges(), 2);

    trayBackend.triggerAction(QStringLiteral("show_picker"));
    QCOMPARE(showHandlerCount, 1);
    QCOMPARE(showSignalCount, 1);
    QCOMPARE(controller.pickerShownCount(), 1);

    trayBackend.activatePrimary();
    QCOMPARE(showHandlerCount, 2);
    QCOMPARE(showSignalCount, 2);
    QCOMPARE(controller.pickerShownCount(), 2);

    trayBackend.triggerAction(QStringLiteral("quit"));
    QCOMPARE(quitSignalCount, 1);
}

void WidgetSmokeTest::clipTrayPresenterUpdatesPauseResumeState()
{
    FakeClipHotkeyBackend hotkeyBackend;
    ClipHotkeyService service(&hotkeyBackend);
    QList<bool> pausedStates;
    ClipTrayControllerOptions options;
    options.capturePausedHandler = [&](bool paused) {
        pausedStates.append(paused);
    };
    ClipTrayController controller(service, options);
    FakeClipTrayBackend trayBackend;
    ClipTrayPresenter presenter(controller, trayBackend);

    std::optional<ClipTrayPresentedAction> toggleAction =
        presentedActionById(trayBackend.actions(), QStringLiteral("toggle_capture"));
    QVERIFY(toggleAction.has_value());
    QCOMPARE(toggleAction->title, QStringLiteral("Pause Capture"));
    QVERIFY(toggleAction->checkable);
    QVERIFY(!toggleAction->checked);

    QVERIFY(controller.start());
    QCOMPARE(trayBackend.toolTip(), QStringLiteral("Pinloom Clip\nRunning"));

    trayBackend.triggerAction(QStringLiteral("toggle_capture"));

    QCOMPARE(pausedStates, (QList<bool>{true}));
    QVERIFY(controller.capturePaused());
    toggleAction = presentedActionById(trayBackend.actions(), QStringLiteral("toggle_capture"));
    QVERIFY(toggleAction.has_value());
    QCOMPARE(toggleAction->title, QStringLiteral("Resume Capture"));
    QVERIFY(toggleAction->checkable);
    QVERIFY(toggleAction->checked);
    QCOMPARE(controller.status(), QStringLiteral("Running, capture paused"));
    QCOMPARE(trayBackend.toolTip(), QStringLiteral("Pinloom Clip\nRunning, capture paused"));

    trayBackend.triggerAction(QStringLiteral("toggle_capture"));

    QCOMPARE(pausedStates, (QList<bool>{true, false}));
    QVERIFY(!controller.capturePaused());
    toggleAction = presentedActionById(trayBackend.actions(), QStringLiteral("toggle_capture"));
    QVERIFY(toggleAction.has_value());
    QCOMPARE(toggleAction->title, QStringLiteral("Pause Capture"));
    QVERIFY(toggleAction->checkable);
    QVERIFY(!toggleAction->checked);
    QCOMPARE(trayBackend.toolTip(), QStringLiteral("Pinloom Clip\nRunning"));
}

void WidgetSmokeTest::clipTrayPresenterSyncsRuntimeStatusAndErrors()
{
    FakeClipHotkeyBackend hotkeyBackend;
    ClipHotkeyService service(&hotkeyBackend);
    ClipTrayController controller(service);
    FakeClipTrayBackend trayBackend;
    ClipTrayPresenter presenter(controller, trayBackend);

    QCOMPARE(presenter.toolTipText(), QStringLiteral("Pinloom Clip\nStopped"));
    QCOMPARE(trayBackend.toolTip(), QStringLiteral("Pinloom Clip\nStopped"));

    QVERIFY(controller.start());
    QVERIFY(controller.isRunning());
    QVERIFY(hotkeyBackend.registered());
    QCOMPARE(hotkeyBackend.registerCalls(), 1);
    QCOMPARE(trayBackend.toolTip(), QStringLiteral("Pinloom Clip\nRunning"));

    controller.stop();
    QVERIFY(!controller.isRunning());
    QVERIFY(!hotkeyBackend.registered());
    QCOMPARE(hotkeyBackend.unregisterCalls(), 1);
    QCOMPARE(trayBackend.toolTip(), QStringLiteral("Pinloom Clip\nStopped"));

    hotkeyBackend.setRegisterResult(false, QStringLiteral("fake tray hotkey failure"));
    QVERIFY(!controller.start());
    QVERIFY(!controller.isRunning());
    QCOMPARE(controller.lastError(), QStringLiteral("fake tray hotkey failure"));
    QCOMPARE(trayBackend.toolTip(), QStringLiteral("Pinloom Clip\nStopped: fake tray hotkey failure"));
    QCOMPARE(presenter.toolTipText(), trayBackend.toolTip());
}

void WidgetSmokeTest::clipResidentRuntimeStartsStopsCaptureHotkeyAndTray()
{
    InMemoryClipRepository repository;
    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentRuntime runtime(repository,
                                makeResidentRuntimeDependencies(captureClipboard,
                                                                insertionClipboard,
                                                                hotkeyBackend,
                                                                trayBackend,
                                                                pasteCalls));
    QList<bool> runningSignals;
    QObject::connect(&runtime, &ClipResidentRuntime::runningChanged, [&](bool running) {
        runningSignals.append(running);
    });

    QVERIFY(runtime.start());

    QVERIFY(runtime.isRunning());
    QVERIFY(runtime.captureService().isRunning());
    QVERIFY(runtime.hotkeyService().isRegistered());
    QVERIFY(hotkeyBackend.registered());
    QVERIFY(trayBackend.visible());
    QCOMPARE(hotkeyBackend.registerCalls(), 1);
    QCOMPARE(trayBackend.visibleChanges(), 1);

    captureClipboard.setText(QStringLiteral("runtime captured text"));
    QCOMPARE(repository.temporaryClips().size(), 1);

    runtime.stop();
    runtime.stop();

    QVERIFY(!runtime.isRunning());
    QVERIFY(!runtime.captureService().isRunning());
    QVERIFY(!runtime.hotkeyService().isRegistered());
    QVERIFY(!hotkeyBackend.registered());
    QVERIFY(!trayBackend.visible());
    QCOMPARE(hotkeyBackend.unregisterCalls(), 1);
    QCOMPARE(trayBackend.visibleChanges(), 2);
    QCOMPARE(runningSignals, (QList<bool>{true, false}));
    QCOMPARE(pasteCalls, 0);
}

void WidgetSmokeTest::clipResidentRuntimeCanRunWithoutRegisteringClipHotkey()
{
    InMemoryClipRepository repository;
    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentRuntimeOptions options;
    options.registerHotkeyOnStart = false;
    options.pickerSearchOptions.includeTemporary = true;
    ClipResidentRuntime runtime(repository,
                                makeResidentRuntimeDependencies(captureClipboard,
                                                                insertionClipboard,
                                                                hotkeyBackend,
                                                                trayBackend,
                                                                pasteCalls),
                                options);

    QVERIFY(runtime.start());

    QVERIFY(runtime.isRunning());
    QVERIFY(runtime.captureService().isRunning());
    QVERIFY(!runtime.hotkeyService().isRegistered());
    QVERIFY(!hotkeyBackend.registered());
    QCOMPARE(hotkeyBackend.registerCalls(), 0);
    QVERIFY(trayBackend.visible());

    captureClipboard.setText(QStringLiteral("runtime no hotkey captured text"));
    QCOMPARE(repository.temporaryClips().size(), 1);

    hotkeyBackend.activate();
    QApplication::processEvents();
    QCOMPARE(runtime.pickerShownCount(), 0);
    QCOMPARE(runtime.trayController().pickerShownCount(), 0);

    trayBackend.triggerAction(QStringLiteral("show_picker"));
    QApplication::processEvents();
    QCOMPARE(runtime.pickerShownCount(), 1);
    QCOMPARE(runtime.trayController().pickerShownCount(), 1);

    runtime.stop();

    QVERIFY(!runtime.isRunning());
    QVERIFY(!runtime.captureService().isRunning());
    QVERIFY(!runtime.hotkeyService().isRegistered());
    QCOMPARE(hotkeyBackend.unregisterCalls(), 0);
    QVERIFY(!trayBackend.visible());
    QCOMPARE(pasteCalls, 0);
}

void WidgetSmokeTest::clipResidentRuntimeShowsAndFocusesPickerFromHotkeyAndTray()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    const QString clipId = saveWidgetClip(repository,
                                          QStringLiteral("Runtime picker text"),
                                          QStringLiteral("Runtime picker"),
                                          {},
                                          {},
                                          false,
                                          base,
                                          base.addSecs(1));
    QVERIFY(!clipId.isEmpty());

    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentRuntime runtime(repository,
                                makeResidentRuntimeDependencies(captureClipboard,
                                                                insertionClipboard,
                                                                hotkeyBackend,
                                                                trayBackend,
                                                                pasteCalls));
    auto *searchEdit = runtime.pickerPanel().findChild<QLineEdit *>(QStringLiteral("clipPickerSearchEdit"));
    QVERIFY(searchEdit);

    QVERIFY(runtime.start());
    QVERIFY(!runtime.pickerPanel().isVisible());

    hotkeyBackend.activate();
    QApplication::processEvents();

    QVERIFY(runtime.pickerPanel().isVisible());
    QCOMPARE(runtime.pickerShownCount(), 1);
    QCOMPARE(runtime.trayController().pickerShownCount(), 1);
    QCOMPARE(runtime.pickerPanel().focusWidget(), static_cast<QWidget *>(searchEdit));

    trayBackend.triggerAction(QStringLiteral("show_picker"));
    QApplication::processEvents();

    QCOMPARE(runtime.pickerShownCount(), 2);
    QCOMPARE(runtime.trayController().pickerShownCount(), 2);
    QCOMPARE(runtime.pickerPanel().focusWidget(), static_cast<QWidget *>(searchEdit));
    QCOMPARE(runtime.pickerPanel().currentResult().clipId, clipId);
    QCOMPARE(pasteCalls, 0);
}

void WidgetSmokeTest::clipResidentRuntimeDefaultPickerShowsCapturedClipboardText()
{
    InMemoryClipRepository repository;
    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentRuntimeOptions options;
    options.pickerSearchOptions.includeTemporary = true;
    ClipResidentRuntime runtime(repository,
                                makeResidentRuntimeDependencies(captureClipboard,
                                                                insertionClipboard,
                                                                hotkeyBackend,
                                                                trayBackend,
                                                                pasteCalls),
                                options);

    QVERIFY(runtime.start());
    captureClipboard.setText(QStringLiteral("runtime default clipboard history"));

    QCOMPARE(repository.temporaryClips().size(), 1);
    const QString clipId = repository.temporaryClips().first().id;

    runtime.showPicker();

    QCOMPARE(runtime.pickerPanel().query(), QString());
    QCOMPARE(runtime.pickerPanel().resultCount(), 1);
    QCOMPARE(runtime.pickerPanel().currentResult().clipId, clipId);
    QCOMPARE(runtime.pickerPanel().currentResult().preview, QStringLiteral("runtime default clipboard history"));
    QVERIFY(runtime.pickerPanel().currentResult().state == ClipState::Temporary);
    QCOMPARE(pasteCalls, 0);
}

void WidgetSmokeTest::clipResidentRuntimeCapturesTemporaryClipAndInsertsThroughPicker()
{
    InMemoryClipRepository repository;
    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    insertionClipboard.setInitialText(QStringLiteral("original clipboard"));
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentRuntimeOptions options;
    options.pickerSearchOptions.includeTemporary = true;
    options.closePickerOnActivationSuccess = false;
    options.insertionOptions.restoreOriginalClipboardOnSuccess = false;
    ClipResidentRuntime runtime(repository,
                                makeResidentRuntimeDependencies(captureClipboard,
                                                                insertionClipboard,
                                                                hotkeyBackend,
                                                                trayBackend,
                                                                pasteCalls),
                                options);

    QVERIFY(runtime.start());
    captureClipboard.setText(QStringLiteral("runtime temporary paste text"));

    QCOMPARE(repository.temporaryClips().size(), 1);
    const QString clipId = repository.temporaryClips().first().id;

    runtime.showPicker();
    runtime.pickerPanel().setQuery(QStringLiteral("temporary paste"));
    QCOMPARE(runtime.pickerPanel().resultCount(), 1);
    QCOMPARE(runtime.pickerPanel().currentResult().clipId, clipId);
    QVERIFY(runtime.pickerPanel().currentResult().state == ClipState::Temporary);

    QVERIFY(runtime.pickerPanel().activateCurrentResult());

    QCOMPARE(pasteCalls, 1);
    QCOMPARE(insertionClipboard.text(), QStringLiteral("runtime temporary paste text"));
    QCOMPARE(insertionClipboard.writes(), QStringList{QStringLiteral("runtime temporary paste text")});
    QCOMPARE(runtime.insertionService().lastInsertedId(), clipId);
    QVERIFY(runtime.captureService().suppressingNextChange());
}

void WidgetSmokeTest::clipResidentRuntimePickerInsertionSuppressesOwnClipboardWrite()
{
    InMemoryClipRepository repository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    const QString clipId = saveWidgetClip(repository,
                                          QStringLiteral("Runtime insert text"),
                                          QStringLiteral("Runtime insert"),
                                          {},
                                          {},
                                          false,
                                          base,
                                          base.addSecs(1));
    QVERIFY(!clipId.isEmpty());

    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    insertionClipboard.setInitialText(QStringLiteral("original clipboard"));
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentRuntimeOptions options;
    options.closePickerOnActivationSuccess = false;
    options.insertionOptions.restoreOriginalClipboardOnSuccess = false;
    ClipResidentRuntime runtime(repository,
                                makeResidentRuntimeDependencies(captureClipboard,
                                                                insertionClipboard,
                                                                hotkeyBackend,
                                                                trayBackend,
                                                                pasteCalls),
                                options);

    QVERIFY(runtime.start());
    QCOMPARE(repository.clips().size(), 1);
    QVERIFY(runtime.pickerPanel().selectFirstResult());
    QCOMPARE(runtime.pickerPanel().currentResult().clipId, clipId);

    QVERIFY(runtime.pickerPanel().activateCurrentResult());

    QCOMPARE(pasteCalls, 1);
    QCOMPARE(insertionClipboard.text(), QStringLiteral("Runtime insert text"));
    QCOMPARE(insertionClipboard.writes(), QStringList{QStringLiteral("Runtime insert text")});
    QCOMPARE(runtime.insertionService().lastInsertedId(), clipId);
    QVERIFY(runtime.insertionService().lastStatus() == ClipInsertionStatus::Inserted);
    QVERIFY(runtime.captureService().suppressingNextChange());

    captureClipboard.setText(QStringLiteral("suppressed runtime self-write event"));

    QVERIFY(!runtime.captureService().suppressingNextChange());
    QCOMPARE(repository.clips().size(), 1);
}

void WidgetSmokeTest::clipResidentRuntimePauseResumeAndQuitActions()
{
    InMemoryClipRepository repository;
    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentRuntime runtime(repository,
                                makeResidentRuntimeDependencies(captureClipboard,
                                                                insertionClipboard,
                                                                hotkeyBackend,
                                                                trayBackend,
                                                                pasteCalls));
    int quitSignals = 0;
    QList<bool> runningSignals;
    QObject::connect(&runtime, &ClipResidentRuntime::quitRequested, [&]() {
        ++quitSignals;
    });
    QObject::connect(&runtime, &ClipResidentRuntime::runningChanged, [&](bool running) {
        runningSignals.append(running);
    });

    QVERIFY(runtime.start());

    trayBackend.triggerAction(QStringLiteral("toggle_capture"));
    QVERIFY(runtime.trayController().capturePaused());
    QVERIFY(runtime.captureService().capturePaused());

    captureClipboard.setText(QStringLiteral("paused runtime capture"));
    QVERIFY(repository.clips().isEmpty());
    QVERIFY(runtime.captureService().lastStatus() == ClipCaptureStatus::IgnoredPaused);

    trayBackend.triggerAction(QStringLiteral("toggle_capture"));
    QVERIFY(!runtime.trayController().capturePaused());
    QVERIFY(!runtime.captureService().capturePaused());

    captureClipboard.setText(QStringLiteral("resumed runtime capture"));
    QCOMPARE(repository.temporaryClips().size(), 1);

    trayBackend.triggerAction(QStringLiteral("quit"));

    QCOMPARE(quitSignals, 1);
    QVERIFY(runtime.quitWasRequested());
    QVERIFY(!runtime.isRunning());
    QVERIFY(!runtime.captureService().isRunning());
    QVERIFY(!runtime.hotkeyService().isRegistered());
    QVERIFY(!hotkeyBackend.registered());
    QVERIFY(!trayBackend.visible());
    QCOMPARE(runningSignals, (QList<bool>{true, false}));
}

void WidgetSmokeTest::clipResidentFactoryReportsMissingDependencies()
{
    ClipResidentRuntimeFactory factory;

    const ClipResidentHostResult missingAll = factory.createHost({});
    QVERIFY(!missingAll.succeeded());
    QCOMPARE(missingAll.error, QStringLiteral("Clipboard text source is required"));

    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentRuntimeDependencies dependencies = makeResidentRuntimeDependencies(captureClipboard,
                                                                                  insertionClipboard,
                                                                                  hotkeyBackend,
                                                                                  trayBackend,
                                                                                  pasteCalls);

    dependencies.trayBackend = nullptr;
    const ClipResidentHostResult missingTray = factory.createHost(dependencies);
    QVERIFY(!missingTray.succeeded());
    QCOMPARE(missingTray.error, QStringLiteral("Tray backend is required"));

    dependencies = makeResidentRuntimeDependencies(captureClipboard,
                                                   insertionClipboard,
                                                   hotkeyBackend,
                                                   trayBackend,
                                                   pasteCalls);
    dependencies.pasteInvoker = {};
    const ClipResidentHostResult missingPaste = factory.createHost(dependencies);
    QVERIFY(!missingPaste.succeeded());
    QCOMPARE(missingPaste.error, QStringLiteral("Paste invoker is required"));

    dependencies = makeResidentRuntimeDependencies(captureClipboard,
                                                   insertionClipboard,
                                                   hotkeyBackend,
                                                   trayBackend,
                                                   pasteCalls);
    ClipResidentRuntimeFactoryOptions sqliteOptions;
    sqliteOptions.repositoryKind = ClipResidentRepositoryKind::SQLite;
    const ClipResidentHostResult missingSqlitePath = factory.createHost(dependencies, sqliteOptions);
    QVERIFY(!missingSqlitePath.succeeded());
    QCOMPARE(missingSqlitePath.error, QStringLiteral("SQLite database path is required"));
}

void WidgetSmokeTest::clipResidentFactoryCreatesInMemoryAndSqliteHosts()
{
    ClipResidentRuntimeFactory factory;

    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentRuntimeFactoryOptions inMemoryOptions;
    inMemoryOptions.runtimeOptions.pickerSearchOptions.includeTemporary = true;
    inMemoryOptions.runtimeOptions.pickerSearchOptions.limit = 7;
    inMemoryOptions.runtimeOptions.insertionOptions.restoreOriginalClipboardOnSuccess = false;
    inMemoryOptions.runtimeOptions.insertionOptions.markClipUsedOnSuccess = false;
    inMemoryOptions.runtimeOptions.closePickerOnActivationSuccess = false;

    ClipResidentHostResult inMemoryResult =
        factory.createHost(makeResidentRuntimeDependencies(captureClipboard,
                                                           insertionClipboard,
                                                           hotkeyBackend,
                                                           trayBackend,
                                                           pasteCalls),
                           inMemoryOptions);

    QVERIFY2(inMemoryResult.succeeded(), qPrintable(inMemoryResult.error));
    QVERIFY(inMemoryResult.host->runtime());
    QVERIFY(inMemoryResult.host->inMemoryRepository());
    QVERIFY(!inMemoryResult.host->sqliteRepository());
    const ClipSearchOptions configuredSearch = inMemoryResult.host->runtime()->pickerPanel().searchOptions();
    QVERIFY(configuredSearch.includeTemporary);
    QCOMPARE(configuredSearch.limit, 7);
    const ClipInsertionOptions configuredInsertion = inMemoryResult.host->runtime()->insertionService().options();
    QVERIFY(!configuredInsertion.restoreOriginalClipboardOnSuccess);
    QVERIFY(!configuredInsertion.markClipUsedOnSuccess);
    QVERIFY(!inMemoryResult.host->runtime()->pickerPanel().closeOnActivationSuccess());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FakeClipboardTextSource sqliteCaptureClipboard;
    FakeClipboardTextAccessor sqliteInsertionClipboard;
    FakeClipHotkeyBackend sqliteHotkeyBackend;
    FakeClipTrayBackend sqliteTrayBackend;
    int sqlitePasteCalls = 0;
    ClipResidentRuntimeFactoryOptions sqliteOptions;
    sqliteOptions.repositoryKind = ClipResidentRepositoryKind::SQLite;
    sqliteOptions.sqliteDatabasePath = dir.filePath(QStringLiteral("pinloom_clip.sqlite3"));

    ClipResidentHostResult sqliteResult =
        factory.createHost(makeResidentRuntimeDependencies(sqliteCaptureClipboard,
                                                           sqliteInsertionClipboard,
                                                           sqliteHotkeyBackend,
                                                           sqliteTrayBackend,
                                                           sqlitePasteCalls),
                           sqliteOptions);

    QVERIFY2(sqliteResult.succeeded(), qPrintable(sqliteResult.error));
    QVERIFY(!sqliteResult.host->inMemoryRepository());
    QVERIFY(sqliteResult.host->sqliteRepository());
    QVERIFY(sqliteResult.host->sqliteRepository()->isOpen());

    QVERIFY(sqliteResult.host->start());
    sqliteCaptureClipboard.setText(QStringLiteral("sqlite host captured text"));
    QCOMPARE(sqliteResult.host->sqliteRepository()->temporaryClips().size(), 1);
    QCOMPARE(sqlitePasteCalls, 0);
    sqliteResult.host->stop();
}

void WidgetSmokeTest::clipResidentHostForwardsStartStopQuitToRuntime()
{
    auto repository = std::make_unique<InMemoryClipRepository>();
    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentRuntimeFactory factory;
    ClipResidentHostResult result =
        factory.createInMemoryHost(std::move(repository),
                                   makeResidentRuntimeDependencies(captureClipboard,
                                                                   insertionClipboard,
                                                                   hotkeyBackend,
                                                                   trayBackend,
                                                                   pasteCalls));
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    ClipResidentHost &host = *result.host;
    QList<bool> runningSignals;
    int quitSignals = 0;
    QObject::connect(&host, &ClipResidentHost::runningChanged, [&](bool running) {
        runningSignals.append(running);
    });
    QObject::connect(&host, &ClipResidentHost::quitRequested, [&]() {
        ++quitSignals;
    });

    QVERIFY(host.start());
    QVERIFY(host.isRunning());
    QVERIFY(host.runtime()->isRunning());
    QVERIFY(host.runtime()->captureService().isRunning());
    QVERIFY(host.runtime()->hotkeyService().isRegistered());
    QVERIFY(hotkeyBackend.registered());
    QVERIFY(trayBackend.visible());

    host.stop();
    QVERIFY(!host.isRunning());
    QVERIFY(!host.runtime()->isRunning());
    QVERIFY(!host.runtime()->captureService().isRunning());
    QVERIFY(!host.runtime()->hotkeyService().isRegistered());
    QVERIFY(!hotkeyBackend.registered());
    QVERIFY(!trayBackend.visible());

    QVERIFY(host.start());
    host.requestQuit();

    QCOMPARE(quitSignals, 1);
    QVERIFY(host.runtime()->quitWasRequested());
    QVERIFY(!host.isRunning());
    QVERIFY(!host.runtime()->captureService().isRunning());
    QVERIFY(!host.runtime()->hotkeyService().isRegistered());
    QCOMPARE(runningSignals, (QList<bool>{true, false, true, false}));
    QCOMPARE(pasteCalls, 0);
}

void WidgetSmokeTest::clipResidentHostPreservesRuntimeShowPauseAndSuppressionFlow()
{
    auto repository = std::make_unique<InMemoryClipRepository>();
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    const QString clipId = saveWidgetClip(*repository,
                                          QStringLiteral("Host insert text"),
                                          QStringLiteral("Host insert"),
                                          {},
                                          {},
                                          false,
                                          base,
                                          base.addSecs(1));
    QVERIFY(!clipId.isEmpty());

    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    insertionClipboard.setInitialText(QStringLiteral("original host clipboard"));
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentRuntimeOptions options;
    options.closePickerOnActivationSuccess = false;
    options.insertionOptions.restoreOriginalClipboardOnSuccess = false;
    ClipResidentRuntimeFactory factory;
    ClipResidentHostResult result =
        factory.createInMemoryHost(std::move(repository),
                                   makeResidentRuntimeDependencies(captureClipboard,
                                                                   insertionClipboard,
                                                                   hotkeyBackend,
                                                                   trayBackend,
                                                                   pasteCalls),
                                   options);
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    ClipResidentHost &host = *result.host;
    QVERIFY(host.start());
    QCOMPARE(host.inMemoryRepository()->clips().size(), 1);

    hotkeyBackend.activate();
    QApplication::processEvents();
    QCOMPARE(host.runtime()->pickerShownCount(), 1);
    QCOMPARE(host.runtime()->trayController().pickerShownCount(), 1);

    trayBackend.triggerAction(QStringLiteral("show_picker"));
    QApplication::processEvents();
    QCOMPARE(host.runtime()->pickerShownCount(), 2);
    QCOMPARE(host.runtime()->trayController().pickerShownCount(), 2);

    trayBackend.triggerAction(QStringLiteral("toggle_capture"));
    QVERIFY(host.runtime()->trayController().capturePaused());
    QVERIFY(host.runtime()->captureService().capturePaused());
    captureClipboard.setText(QStringLiteral("paused host capture"));
    QCOMPARE(host.inMemoryRepository()->clips().size(), 1);
    QVERIFY(host.runtime()->captureService().lastStatus() == ClipCaptureStatus::IgnoredPaused);

    trayBackend.triggerAction(QStringLiteral("toggle_capture"));
    QVERIFY(!host.runtime()->trayController().capturePaused());
    QVERIFY(!host.runtime()->captureService().capturePaused());
    captureClipboard.setText(QStringLiteral("resumed host capture"));
    QCOMPARE(host.inMemoryRepository()->clips().size(), 2);

    QVERIFY(host.runtime()->pickerPanel().selectFirstResult());
    QCOMPARE(host.runtime()->pickerPanel().currentResult().clipId, clipId);
    QVERIFY(host.runtime()->pickerPanel().activateCurrentResult());

    QCOMPARE(pasteCalls, 1);
    QCOMPARE(insertionClipboard.text(), QStringLiteral("Host insert text"));
    QCOMPARE(insertionClipboard.writes(), QStringList{QStringLiteral("Host insert text")});
    QCOMPARE(host.runtime()->insertionService().lastInsertedId(), clipId);
    QVERIFY(host.runtime()->captureService().suppressingNextChange());

    captureClipboard.setText(QStringLiteral("host suppressed self-write event"));

    QVERIFY(!host.runtime()->captureService().suppressingNextChange());
    QCOMPARE(host.inMemoryRepository()->clips().size(), 2);
}

void WidgetSmokeTest::clipResidentAppRejectsSqliteConfigWithoutPath()
{
    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentApp app(makeResidentRuntimeDependencies(captureClipboard,
                                                        insertionClipboard,
                                                        hotkeyBackend,
                                                        trayBackend,
                                                        pasteCalls));

    QVERIFY(!app.configure(clipResidentSqliteAppConfig(QStringLiteral("  "))));

    QVERIFY(app.status() == ClipResidentAppStatus::Error);
    QCOMPARE(app.statusText(), QStringLiteral("Error"));
    QCOMPARE(app.lastError(), QStringLiteral("SQLite database path is required"));

    QVERIFY(!app.start());
    QVERIFY(!app.host());
    QCOMPARE(hotkeyBackend.registerCalls(), 0);
    QVERIFY(!trayBackend.visible());
    QCOMPARE(pasteCalls, 0);
}

void WidgetSmokeTest::clipResidentAppCreatesStartsStopsSqliteHostWithOptions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString databasePath = clipResidentDatabasePathForAppDataLocation(dir.path());
    QVERIFY(databasePath.endsWith(QStringLiteral("pinloom_clip.sqlite3")));

    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentApp app(makeResidentRuntimeDependencies(captureClipboard,
                                                        insertionClipboard,
                                                        hotkeyBackend,
                                                        trayBackend,
                                                        pasteCalls));
    QList<bool> runningSignals;
    QObject::connect(&app, &ClipResidentApp::runningChanged, [&](bool running) {
        runningSignals.append(running);
    });

    ClipResidentAppConfig config = clipResidentSqliteAppConfig(databasePath);
    config.hotkeyConfig.key = Qt::Key_B;
    config.hotkeyConfig.modifiers = Qt::ControlModifier | Qt::AltModifier;
    config.pickerSearchOptions.includeTemporary = true;
    config.pickerSearchOptions.limit = 3;
    config.insertionOptions.restoreOriginalClipboardOnSuccess = false;
    config.insertionOptions.markClipUsedOnSuccess = false;
    config.closePickerOnActivationSuccess = false;

    QVERIFY(app.configure(config));
    QVERIFY(app.status() == ClipResidentAppStatus::Ready);
    QCOMPARE(app.statusText(), QStringLiteral("Ready"));
    QVERIFY(!app.host());

    QVERIFY(app.start());

    QVERIFY(app.isRunning());
    QVERIFY(app.status() == ClipResidentAppStatus::Running);
    QVERIFY(app.host());
    QVERIFY(app.host()->runtime());
    QVERIFY(!app.host()->inMemoryRepository());
    QVERIFY(app.host()->sqliteRepository());
    QVERIFY(app.host()->sqliteRepository()->isOpen());
    QVERIFY(hotkeyBackend.registered());
    QVERIFY(hotkeyBackend.registeredConfig() == config.hotkeyConfig);
    QVERIFY(trayBackend.visible());
    QCOMPARE(app.host()->runtime()->pickerPanel().searchOptions().limit, 3);
    QVERIFY(app.host()->runtime()->pickerPanel().searchOptions().includeTemporary);
    QVERIFY(!app.host()->runtime()->insertionService().options().restoreOriginalClipboardOnSuccess);
    QVERIFY(!app.host()->runtime()->insertionService().options().markClipUsedOnSuccess);
    QVERIFY(!app.host()->runtime()->pickerPanel().closeOnActivationSuccess());

    captureClipboard.setText(QStringLiteral("sqlite app captured text"));
    QCOMPARE(app.host()->sqliteRepository()->temporaryClips().size(), 1);

    app.stop();

    QVERIFY(!app.isRunning());
    QVERIFY(app.status() == ClipResidentAppStatus::Stopped);
    QVERIFY(!hotkeyBackend.registered());
    QVERIFY(!trayBackend.visible());
    QCOMPARE(runningSignals, (QList<bool>{true, false}));
    QCOMPARE(pasteCalls, 0);
}

void WidgetSmokeTest::clipResidentAppForwardsRequestQuitAndReportsStartErrors()
{
    FakeClipboardTextSource failingCaptureClipboard;
    FakeClipboardTextAccessor failingInsertionClipboard;
    FakeClipHotkeyBackend failingHotkeyBackend;
    failingHotkeyBackend.setRegisterResult(false, QStringLiteral("fake app hotkey failure"));
    FakeClipTrayBackend failingTrayBackend;
    int failingPasteCalls = 0;
    ClipResidentApp failingApp(makeResidentRuntimeDependencies(failingCaptureClipboard,
                                                               failingInsertionClipboard,
                                                               failingHotkeyBackend,
                                                               failingTrayBackend,
                                                               failingPasteCalls));

    QVERIFY(!failingApp.start());

    QVERIFY(failingApp.status() == ClipResidentAppStatus::Error);
    QCOMPARE(failingApp.lastError(), QStringLiteral("fake app hotkey failure"));
    QVERIFY(failingApp.host());
    QVERIFY(!failingApp.isRunning());
    QVERIFY(!failingHotkeyBackend.registered());
    QVERIFY(!failingTrayBackend.visible());
    QCOMPARE(failingHotkeyBackend.registerCalls(), 1);
    QCOMPARE(failingPasteCalls, 0);

    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentApp app(makeResidentRuntimeDependencies(captureClipboard,
                                                        insertionClipboard,
                                                        hotkeyBackend,
                                                        trayBackend,
                                                        pasteCalls));
    int quitSignals = 0;
    QObject::connect(&app, &ClipResidentApp::quitRequested, [&]() {
        ++quitSignals;
    });

    QVERIFY(app.start());
    app.requestQuit();

    QCOMPARE(quitSignals, 1);
    QVERIFY(app.host()->runtime()->quitWasRequested());
    QVERIFY(!app.isRunning());
    QVERIFY(app.status() == ClipResidentAppStatus::QuitRequested);
    QVERIFY(!hotkeyBackend.registered());
    QVERIFY(!trayBackend.visible());
    QCOMPARE(pasteCalls, 0);
}

void WidgetSmokeTest::clipResidentAppPreservesHostRuntimeWorkflow()
{
    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    insertionClipboard.setInitialText(QStringLiteral("original app clipboard"));
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentApp app(makeResidentRuntimeDependencies(captureClipboard,
                                                        insertionClipboard,
                                                        hotkeyBackend,
                                                        trayBackend,
                                                        pasteCalls));
    ClipResidentAppConfig config;
    config.closePickerOnActivationSuccess = false;
    config.insertionOptions.restoreOriginalClipboardOnSuccess = false;
    QVERIFY(app.configure(config));
    QVERIFY(app.start());
    QVERIFY(app.host());
    QVERIFY(app.host()->inMemoryRepository());
    QVERIFY(app.host()->runtime());

    InMemoryClipRepository *repository = app.host()->inMemoryRepository();
    ClipResidentRuntime *runtime = app.host()->runtime();
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    const QString clipId = saveWidgetClip(*repository,
                                          QStringLiteral("App insert text"),
                                          QStringLiteral("App insert"),
                                          {},
                                          {},
                                          false,
                                          base,
                                          base.addSecs(1));
    QVERIFY(!clipId.isEmpty());
    QCOMPARE(repository->clips().size(), 1);

    hotkeyBackend.activate();
    QApplication::processEvents();
    QCOMPARE(runtime->pickerShownCount(), 1);
    QCOMPARE(runtime->trayController().pickerShownCount(), 1);

    trayBackend.triggerAction(QStringLiteral("show_picker"));
    QApplication::processEvents();
    QCOMPARE(runtime->pickerShownCount(), 2);
    QCOMPARE(runtime->trayController().pickerShownCount(), 2);

    trayBackend.triggerAction(QStringLiteral("toggle_capture"));
    QVERIFY(runtime->trayController().capturePaused());
    QVERIFY(runtime->captureService().capturePaused());
    captureClipboard.setText(QStringLiteral("paused app capture"));
    QCOMPARE(repository->clips().size(), 1);
    QVERIFY(runtime->captureService().lastStatus() == ClipCaptureStatus::IgnoredPaused);

    trayBackend.triggerAction(QStringLiteral("toggle_capture"));
    QVERIFY(!runtime->trayController().capturePaused());
    QVERIFY(!runtime->captureService().capturePaused());
    captureClipboard.setText(QStringLiteral("resumed app capture"));
    QCOMPARE(repository->clips().size(), 2);

    runtime->pickerPanel().setQuery(QStringLiteral("App insert"));
    QVERIFY(runtime->pickerPanel().selectFirstResult());
    QCOMPARE(runtime->pickerPanel().currentResult().clipId, clipId);
    QVERIFY(runtime->pickerPanel().activateCurrentResult());

    QCOMPARE(pasteCalls, 1);
    QCOMPARE(insertionClipboard.text(), QStringLiteral("App insert text"));
    QCOMPARE(insertionClipboard.writes(), QStringList{QStringLiteral("App insert text")});
    QCOMPARE(runtime->insertionService().lastInsertedId(), clipId);
    QVERIFY(runtime->captureService().suppressingNextChange());

    captureClipboard.setText(QStringLiteral("app suppressed self-write event"));

    QVERIFY(!runtime->captureService().suppressingNextChange());
    QCOMPARE(repository->clips().size(), 2);
}

void WidgetSmokeTest::clipResidentAppConfigStoreRoundTripsExplicitJsonFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString configPath = dir.filePath(QStringLiteral("resident-config.json"));
    const QString databasePath = dir.filePath(QStringLiteral("clip.sqlite3"));

    ClipResidentAppConfig config = clipResidentSqliteAppConfig(databasePath);
    config.initializeSqlite = false;
    config.hotkeyConfig.key = Qt::Key_F2;
    config.hotkeyConfig.modifiers = Qt::ControlModifier | Qt::AltModifier;
    config.pickerSearchOptions.includeSaved = false;
    config.pickerSearchOptions.includeTemporary = true;
    config.pickerSearchOptions.emptyQueryReturnsPinnedAndRecent = false;
    config.pickerSearchOptions.limit = 11;
    config.insertionOptions.restoreOriginalClipboardOnSuccess = false;
    config.insertionOptions.markClipUsedOnSuccess = false;
    config.closePickerOnActivationSuccess = false;
    config.showTrayOnStart = false;
    config.hideTrayOnStop = false;
    config.hidePickerOnStop = false;
    config.stopOnQuitRequested = false;

    ClipResidentAppConfigStore store;
    QString error;
    QVERIFY2(store.save(configPath, config, &error), qPrintable(error));
    QVERIFY(QFile::exists(configPath));

    const ClipResidentAppConfigLoadResult loaded = store.load(configPath);
    QVERIFY2(loaded.succeeded(), qPrintable(loaded.error));
    QVERIFY(loaded.loadedFromFile);

    QCOMPARE(static_cast<int>(loaded.config.repositoryKind), static_cast<int>(config.repositoryKind));
    QCOMPARE(loaded.config.sqliteDatabasePath, databasePath);
    QVERIFY(!loaded.config.initializeSqlite);
    QCOMPARE(loaded.config.hotkeyConfig.key, Qt::Key_F2);
    QCOMPARE(static_cast<int>(loaded.config.hotkeyConfig.modifiers),
             static_cast<int>(Qt::ControlModifier | Qt::AltModifier));
    QVERIFY(!loaded.config.pickerSearchOptions.includeSaved);
    QVERIFY(loaded.config.pickerSearchOptions.includeTemporary);
    QVERIFY(!loaded.config.pickerSearchOptions.emptyQueryReturnsPinnedAndRecent);
    QCOMPARE(loaded.config.pickerSearchOptions.limit, 11);
    QVERIFY(!loaded.config.insertionOptions.restoreOriginalClipboardOnSuccess);
    QVERIFY(!loaded.config.insertionOptions.markClipUsedOnSuccess);
    QVERIFY(!loaded.config.closePickerOnActivationSuccess);
    QVERIFY(!loaded.config.showTrayOnStart);
    QVERIFY(!loaded.config.hideTrayOnStop);
    QVERIFY(!loaded.config.hidePickerOnStop);
    QVERIFY(!loaded.config.stopOnQuitRequested);
}

void WidgetSmokeTest::clipResidentAppConfigStoreReturnsDefaultForMissingExplicitFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString missingPath = dir.filePath(QStringLiteral("missing-resident-config.json"));
    QVERIFY(!QFile::exists(missingPath));

    ClipResidentAppConfigStore store;
    const ClipResidentAppConfigLoadResult loaded = store.load(missingPath);

    QVERIFY2(loaded.succeeded(), qPrintable(loaded.error));
    QVERIFY(!loaded.loadedFromFile);
    QVERIFY(!QFile::exists(missingPath));
    QCOMPARE(static_cast<int>(loaded.config.repositoryKind), static_cast<int>(ClipResidentRepositoryKind::InMemory));
    QVERIFY(loaded.config.sqliteDatabasePath.isEmpty());
    QVERIFY(loaded.config.initializeSqlite);
    QCOMPARE(loaded.config.hotkeyConfig.key, Qt::Key_V);
    QCOMPARE(static_cast<int>(loaded.config.hotkeyConfig.modifiers),
             static_cast<int>(Qt::ControlModifier | Qt::ShiftModifier));
    QVERIFY(loaded.config.pickerSearchOptions.includeSaved);
    QVERIFY(!loaded.config.pickerSearchOptions.includeTemporary);
    QVERIFY(loaded.config.pickerSearchOptions.emptyQueryReturnsPinnedAndRecent);
    QCOMPARE(loaded.config.pickerSearchOptions.limit, 20);
    QVERIFY(loaded.config.insertionOptions.restoreOriginalClipboardOnSuccess);
    QVERIFY(loaded.config.insertionOptions.markClipUsedOnSuccess);
    QVERIFY(loaded.config.closePickerOnActivationSuccess);
    QVERIFY(loaded.config.showTrayOnStart);
    QVERIFY(loaded.config.hideTrayOnStop);
    QVERIFY(loaded.config.hidePickerOnStop);
    QVERIFY(loaded.config.stopOnQuitRequested);
}

void WidgetSmokeTest::clipResidentAppConfigStoreReportsInvalidJsonAndFields()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ClipResidentAppConfigStore store;

    const QString invalidJsonPath = dir.filePath(QStringLiteral("invalid-json.json"));
    writeTestFile(invalidJsonPath, QByteArray("{ broken json"));
    ClipResidentAppConfigLoadResult loaded = store.load(invalidJsonPath);
    QVERIFY(!loaded.succeeded());
    QVERIFY(loaded.error.contains(QStringLiteral("Invalid clip resident app config JSON")));

    const QString unknownRepositoryPath = dir.filePath(QStringLiteral("unknown-repository.json"));
    writeTestFile(unknownRepositoryPath, QByteArray(R"({"repository":{"kind":"registry"}})"));
    loaded = store.load(unknownRepositoryPath);
    QVERIFY(!loaded.succeeded());
    QCOMPARE(loaded.error, QStringLiteral("Unknown clip repository kind: registry"));

    const QString invalidHotkeyPath = dir.filePath(QStringLiteral("invalid-hotkey.json"));
    writeTestFile(invalidHotkeyPath, QByteArray(R"({"hotkey":{"key":""}})"));
    loaded = store.load(invalidHotkeyPath);
    QVERIFY(!loaded.succeeded());
    QCOMPARE(loaded.error, QStringLiteral("Hotkey key is required"));

    const QString invalidFieldPath = dir.filePath(QStringLiteral("invalid-field.json"));
    writeTestFile(invalidFieldPath, QByteArray(R"({"pickerSearchOptions":{"includeSaved":"yes"}})"));
    loaded = store.load(invalidFieldPath);
    QVERIFY(!loaded.succeeded());
    QCOMPARE(loaded.error, QStringLiteral("includeSaved must be a bool"));
}

void WidgetSmokeTest::clipResidentAppConfigStoreRejectsInvalidConfigAndWriteFailures()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ClipResidentAppConfigStore store;
    QString error;

    ClipResidentAppConfig invalidHotkeyConfig;
    invalidHotkeyConfig.hotkeyConfig.key = Qt::Key_unknown;
    QVERIFY(!store.save(dir.filePath(QStringLiteral("invalid-hotkey-save.json")), invalidHotkeyConfig, &error));
    QCOMPARE(error, QStringLiteral("Hotkey key is required"));

    ClipResidentAppConfig unsupportedRepositoryConfig;
    unsupportedRepositoryConfig.repositoryKind = static_cast<ClipResidentRepositoryKind>(999);
    QVERIFY(!store.save(dir.filePath(QStringLiteral("unsupported-repository-save.json")),
                        unsupportedRepositoryConfig,
                        &error));
    QCOMPARE(error, QStringLiteral("Unsupported clip repository kind"));

    const QString missingParentPath = dir.filePath(QStringLiteral("missing-parent/resident-config.json"));
    QVERIFY(!store.save(missingParentPath, ClipResidentAppConfig{}, &error));
    QVERIFY(error.contains(QStringLiteral("Unable to write clip resident app config file")));
}

void WidgetSmokeTest::clipResidentAppConfigStoreConfiguresAppFromExplicitFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString configPath = dir.filePath(QStringLiteral("resident-config.json"));
    const QString databasePath = dir.filePath(QStringLiteral("clip.sqlite3"));

    ClipResidentAppConfig config = clipResidentSqliteAppConfig(databasePath);
    config.hotkeyConfig.key = Qt::Key_B;
    config.hotkeyConfig.modifiers = Qt::ControlModifier | Qt::AltModifier;
    config.pickerSearchOptions.includeTemporary = true;
    config.pickerSearchOptions.limit = 3;
    config.insertionOptions.restoreOriginalClipboardOnSuccess = false;
    config.closePickerOnActivationSuccess = false;
    config.showTrayOnStart = false;

    ClipResidentAppConfigStore store;
    QString error;
    QVERIFY2(store.save(configPath, config, &error), qPrintable(error));

    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentApp app(makeResidentRuntimeDependencies(captureClipboard,
                                                        insertionClipboard,
                                                        hotkeyBackend,
                                                        trayBackend,
                                                        pasteCalls));

    QVERIFY2(configureClipResidentAppFromConfigFile(app, configPath, store, &error), qPrintable(error));

    QVERIFY(app.status() == ClipResidentAppStatus::Ready);
    QVERIFY(!app.host());
    QCOMPARE(static_cast<int>(app.config().repositoryKind), static_cast<int>(ClipResidentRepositoryKind::SQLite));
    QCOMPARE(app.config().sqliteDatabasePath, databasePath);
    QCOMPARE(app.config().hotkeyConfig.key, Qt::Key_B);
    QCOMPARE(static_cast<int>(app.config().hotkeyConfig.modifiers),
             static_cast<int>(Qt::ControlModifier | Qt::AltModifier));
    QVERIFY(app.config().pickerSearchOptions.includeTemporary);
    QCOMPARE(app.config().pickerSearchOptions.limit, 3);
    QVERIFY(!app.config().insertionOptions.restoreOriginalClipboardOnSuccess);
    QVERIFY(!app.config().closePickerOnActivationSuccess);
    QVERIFY(!app.config().showTrayOnStart);
    QCOMPARE(hotkeyBackend.registerCalls(), 0);
    QVERIFY(!trayBackend.visible());
    QCOMPARE(pasteCalls, 0);
}

void WidgetSmokeTest::clipResidentAppConfigStoreRequiresExplicitPathWithoutUserDataFallback()
{
    ClipResidentAppConfigStore store;

    ClipResidentAppConfigLoadResult loaded = store.load(QStringLiteral("   "));
    QVERIFY(!loaded.succeeded());
    QCOMPARE(loaded.error, QStringLiteral("Clip resident app config path is required"));
    QVERIFY(!loaded.loadedFromFile);

    QString error;
    QVERIFY(!store.save(QString(), ClipResidentAppConfig{}, &error));
    QCOMPARE(error, QStringLiteral("Clip resident app config path is required"));

    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    FakeClipHotkeyBackend hotkeyBackend;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentApp app(makeResidentRuntimeDependencies(captureClipboard,
                                                        insertionClipboard,
                                                        hotkeyBackend,
                                                        trayBackend,
                                                        pasteCalls));

    QVERIFY(!configureClipResidentAppFromConfigFile(app, QString(), store, &error));
    QCOMPARE(error, QStringLiteral("Clip resident app config path is required"));
    QVERIFY(app.status() == ClipResidentAppStatus::Ready);
    QVERIFY(!app.host());
    QCOMPARE(hotkeyBackend.registerCalls(), 0);
    QVERIFY(!trayBackend.visible());
    QCOMPARE(pasteCalls, 0);
}

void WidgetSmokeTest::mainWindowCloseHidesToTray()
{
    PinloomMainWindow window;
    int hiddenSignals = 0;
    QObject::connect(&window, &PinloomMainWindow::hiddenToTray, [&]() {
        ++hiddenSignals;
    });

    window.show();
    QApplication::processEvents();
    QVERIFY(window.isVisible());

    QCloseEvent closeEvent;
    QApplication::sendEvent(&window, &closeEvent);
    QApplication::processEvents();

    QVERIFY(!closeEvent.isAccepted());
    QVERIFY(!window.isVisible());
    QCOMPARE(hiddenSignals, 1);

    window.show();
    QApplication::processEvents();
    QVERIFY(window.isVisible());

    QVERIFY(!window.close());
    QApplication::processEvents();
    QVERIFY(!window.isVisible());
    QCOMPARE(hiddenSignals, 2);
}

void WidgetSmokeTest::mainPanelHotkeyRegistersAndShowsMainWindow()
{
    InMemoryLibraryRepository repository;
    QMainWindow window;
    auto *panel = new PinloomPanel(repository, &window);
    window.setCentralWidget(panel);
    window.hide();

    auto *searchEdit = panel->findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    QVERIFY(searchEdit);
    panel->setSearchText(QStringLiteral("direct typing target"));
    searchEdit->clearFocus();

    FakeClipHotkeyBackend hotkeyBackend;
    ClipHotkeyService service(defaultMainPanelHotkeyConfig(), &hotkeyBackend);
    MainPanelHotkeyController controller(service, window, *panel);
    int showRequestedCount = 0;
    QObject::connect(&controller, &MainPanelHotkeyController::showRequested, [&]() {
        ++showRequestedCount;
    });

    QVERIFY(service.start());
    QVERIFY(hotkeyBackend.registered());
    QCOMPARE(hotkeyBackend.registeredConfig().key, Qt::Key_Space);
    QCOMPARE(hotkeyBackend.registeredConfig().modifiers, Qt::KeyboardModifiers(Qt::ControlModifier));

    hotkeyBackend.activate();
    QApplication::processEvents();

    QVERIFY(window.isVisible());
    QCOMPARE(showRequestedCount, 1);
    QCOMPARE(panel->focusWidget(), static_cast<QWidget *>(searchEdit));
    QCOMPARE(searchEdit->selectedText(), QStringLiteral("direct typing target"));
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
    auto *openButton = panel.findChild<QPushButton *>(QStringLiteral("openButton"));
    auto *addAliasButton = panel.findChild<QPushButton *>(QStringLiteral("addAliasButton"));
    auto *addAnchorButton = panel.findChild<QPushButton *>(QStringLiteral("addAnchorButton"));
    auto *pinButton = panel.findChild<QPushButton *>(QStringLiteral("pinButton"));
    QVERIFY(rootControls);
    QVERIFY(rootList);
    QVERIFY(searchEdit);
    QVERIFY(results);
    QVERIFY(manageButton);
    QVERIFY(openButton);
    QVERIFY(addAliasButton);
    QVERIFY(addAnchorButton);
    QVERIFY(pinButton);

    QVERIFY(rootControls->isHidden());
    QVERIFY(rootList->isHidden());
    QVERIFY(!searchEdit->isHidden());
    QVERIFY(results->isHidden());
    QVERIFY(openButton->isHidden());
    QVERIFY(addAliasButton->isHidden());
    QVERIFY(addAnchorButton->isHidden());
    QVERIFY(pinButton->isHidden());
    QVERIFY(manageButton->isHidden());
    QVERIFY(searchEdit->placeholderText().contains(QStringLiteral("anchors")));
    QVERIFY(searchEdit->placeholderText().contains(QStringLiteral("Saved Clips")));
    QCOMPARE(panel.focusWidget(), static_cast<QWidget *>(searchEdit));
    QVERIFY(panel.sizeHint().height() <= 120);
    QCOMPARE(rootList->count(), 1);

    panel.setSearchText(QStringLiteral("Anchor Note"));
    QVERIFY(!results->isHidden());
    QCOMPARE(results->count(), 1);

    PinloomPanelOptions managementOptions;
    managementOptions.showLibraryRootManagementButton = true;
    PinloomPanel managementPanel(repository, managementOptions);
    auto *managementRootControls = managementPanel.findChild<QWidget *>(QStringLiteral("libraryRootControls"));
    auto *managementRootList = managementPanel.findChild<QListWidget *>(QStringLiteral("libraryRootList"));
    auto *managementButton = managementPanel.findChild<QPushButton *>(QStringLiteral("manageLibraryButton"));
    QVERIFY(managementRootControls);
    QVERIFY(managementRootList);
    QVERIFY(managementButton);
    QVERIFY(!managementButton->isHidden());
    managementButton->click();
    QVERIFY(!managementRootControls->isHidden());
    QVERIFY(!managementRootList->isHidden());
}

void WidgetSmokeTest::panelSearchesSavedClipsAndEnterInserts()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("anchor-note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Anchor Control Note");
    resource.location = QStringLiteral("anchor-control.md");
    Anchor anchor;
    anchor.type = AnchorType::TextHeading;
    anchor.target = QStringLiteral("Anchor control heading");
    anchor.line = 12;
    resource.anchors = {anchor};
    QVERIFY(repository.upsertResource(resource));

    InMemoryClipRepository clipRepository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    const QString clipId = saveWidgetClip(clipRepository,
                                          QStringLiteral("Primary launcher saved clip body"),
                                          QStringLiteral("Launcher Saved Clip"),
                                          {QStringLiteral("primary clip alias")},
                                          {QStringLiteral("launcher-clip")},
                                          true,
                                          base,
                                          base.addSecs(1));
    QVERIFY(!clipId.isEmpty());

    ClipSearchService clipSearch(clipRepository);
    QStringList insertedClipIds;
    QList<PinloomOpenTarget> openedTargets;
    PinloomPanelOptions options;
    options.clipSearchHandler = [&](const QString &query, const ClipSearchOptions &searchOptions) {
        return clipSearch.search(query, searchOptions);
    };
    options.clipInsertionHandler = [&](const QString &selectedClipId, QString *error) {
        if (error) {
            error->clear();
        }
        insertedClipIds.append(selectedClipId);
        return true;
    };
    options.openTargetHandler = [&](const PinloomOpenTarget &target) {
        openedTargets.append(target);
        return true;
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    panel.setSearchText(QStringLiteral("Launcher Saved Clip"));
    QCOMPARE(results->count(), 1);
    QCOMPARE(panel.currentOpenTarget().clipId, clipId);
    QCOMPARE(panel.currentOpenTarget().resourceId, QString());
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Clip] Launcher Saved Clip")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("Primary launcher saved clip body")));

    panel.setSearchText(QStringLiteral("primary clip alias"));
    QCOMPARE(results->count(), 1);
    QCOMPARE(panel.currentOpenTarget().clipId, clipId);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("aliases: primary clip alias")));

    panel.setSearchText(QStringLiteral("#launcher-clip"));
    QCOMPARE(results->count(), 1);
    QCOMPARE(panel.currentOpenTarget().clipId, clipId);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("#launcher-clip")));

    QTest::keyClick(searchEdit, Qt::Key_Return);
    QCOMPARE(insertedClipIds, QStringList{clipId});
    QVERIFY(openedTargets.isEmpty());
    QCOMPARE(panel.statusText(), QStringLiteral("Inserted clip"));

    panel.setSearchText(QStringLiteral("Anchor control heading"));
    QCOMPARE(results->count(), 1);
    QVERIFY(panel.currentOpenTarget().clipId.isEmpty());
    QCOMPARE(panel.currentOpenTarget().resourceId, resource.id);

    QTest::keyClick(searchEdit, Qt::Key_Return);
    QCOMPARE(insertedClipIds, QStringList{clipId});
    QCOMPARE(openedTargets.size(), 1);
    QCOMPARE(openedTargets.first().resourceId, resource.id);
    QVERIFY(openedTargets.first().anchor.has_value());
}

void WidgetSmokeTest::panelClipRootCommandShowsCandidates()
{
    InMemoryLibraryRepository repository;

    int clipSearchCalls = 0;
    PinloomPanelOptions options;
    options.clipSearchHandler = [&](const QString &query, const ClipSearchOptions &searchOptions) {
        Q_UNUSED(query);
        Q_UNUSED(searchOptions);
        ++clipSearchCalls;
        return QList<ClipSearchResult>{};
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    panel.setSearchText(QStringLiteral("c"));

    QCOMPARE(clipSearchCalls, 0);
    QCOMPARE(results->count(), 2);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Command] Clip Search -> Open")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("c s <query>")));
    QVERIFY(results->item(1)->text().contains(QStringLiteral("[Command] New Saved Clip -> Open")));
    QVERIFY(results->item(1)->text().contains(QStringLiteral("c n")));
    QCOMPARE(panel.statusText(), QStringLiteral("Clip commands"));

    QTest::keyClick(searchEdit, Qt::Key_Return);
    QCOMPARE(panel.searchText(), QStringLiteral("c s"));

    panel.setSearchText(QStringLiteral("c"));
    QTest::keyClick(searchEdit, Qt::Key_Down);
    QCOMPARE(results->currentRow(), 1);
    QTest::keyClick(searchEdit, Qt::Key_Return);
    QCOMPARE(panel.searchText(), QStringLiteral("c n"));
}

void WidgetSmokeTest::panelClipSearchCommandSearchesHistoryAndSavedClipsAndEnterInserts()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("clip-command-anchor");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Unrelated Anchor");
    resource.location = QStringLiteral("unrelated-anchor.md");
    resource.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Unrelated heading"), 4}};
    QVERIFY(repository.upsertResource(resource));

    InMemoryClipRepository clipRepository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    const ClipCaptureResult temporary = clipRepository.captureText(QStringLiteral("temporary command body"),
                                                                   {},
                                                                   {},
                                                                   base);
    QVERIFY(temporary.captured());
    QVERIFY(temporary.clip.has_value());

    const QString savedId = saveWidgetClip(clipRepository,
                                           QStringLiteral("saved command body"),
                                           QStringLiteral("Clip Command Saved"),
                                           {QStringLiteral("saved command alias")},
                                           {QStringLiteral("command-tag")},
                                           true,
                                           base.addSecs(1),
                                           base.addSecs(2));
    QVERIFY(!savedId.isEmpty());

    ClipSearchService clipSearch(clipRepository);
    QStringList clipQueries;
    QList<ClipSearchOptions> clipOptions;
    QStringList insertedClipIds;
    QList<PinloomOpenTarget> openedTargets;
    PinloomPanelOptions options;
    options.clipSearchHandler = [&](const QString &query, const ClipSearchOptions &searchOptions) {
        clipQueries.append(query);
        clipOptions.append(searchOptions);
        return clipSearch.search(query, searchOptions);
    };
    options.clipInsertionHandler = [&](const QString &selectedClipId, QString *error) {
        if (error) {
            error->clear();
        }
        insertedClipIds.append(selectedClipId);
        return true;
    };
    options.openTargetHandler = [&](const PinloomOpenTarget &target) {
        openedTargets.append(target);
        return true;
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    panel.setSearchText(QStringLiteral("Clip Command Saved"));
    QCOMPARE(results->count(), 1);
    QVERIFY(!clipOptions.isEmpty());
    QVERIFY(!clipOptions.last().includeTemporary);

    clipQueries.clear();
    clipOptions.clear();
    panel.setSearchText(QStringLiteral("c s"));

    QCOMPARE(clipQueries, (QStringList{QString(), QString()}));
    QCOMPARE(clipOptions.size(), 2);
    QVERIFY(!clipOptions.at(0).includeSaved);
    QVERIFY(clipOptions.at(0).includeTemporary);
    QVERIFY(clipOptions.at(1).includeSaved);
    QVERIFY(!clipOptions.at(1).includeTemporary);
    QCOMPARE(results->count(), 2);
    QCOMPARE(panel.resultAt(0).clipId, temporary.clip->id);
    QCOMPARE(panel.resultAt(0).matchSummary, QStringLiteral("Clip History"));
    QCOMPARE(panel.resultAt(1).clipId, savedId);
    QCOMPARE(panel.resultAt(1).matchSummary, QStringLiteral("Saved Clip"));
    QVERIFY(panel.resultAt(0).resourceId.isEmpty());
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Clip] temporary command body -> Insert")));
    QVERIFY(results->item(1)->text().contains(QStringLiteral("[Clip] Clip Command Saved -> Insert")));
    QCOMPARE(panel.statusText(), QStringLiteral("Clip search: 2 clip(s)"));

    QTest::keyClick(searchEdit, Qt::Key_Down);
    QCOMPARE(results->currentRow(), 1);
    QTest::keyClick(searchEdit, Qt::Key_Up);
    QCOMPARE(results->currentRow(), 0);
    QTest::keyClick(searchEdit, Qt::Key_Down);
    QCOMPARE(results->currentRow(), 1);
    QTest::keyClick(searchEdit, Qt::Key_Return);

    QCOMPARE(insertedClipIds, QStringList{savedId});
    QVERIFY(openedTargets.isEmpty());
    QCOMPARE(panel.statusText(), QStringLiteral("Inserted clip"));

    clipQueries.clear();
    clipOptions.clear();
    panel.setSearchText(QStringLiteral("c s saved command alias"));

    QCOMPARE(clipQueries, QStringList{QStringLiteral("saved command alias")});
    QCOMPARE(clipOptions.size(), 1);
    QVERIFY(clipOptions.first().includeSaved);
    QVERIFY(clipOptions.first().includeTemporary);
    QCOMPARE(results->count(), 1);
    QCOMPARE(panel.currentOpenTarget().clipId, savedId);
    QVERIFY(panel.currentOpenTarget().resourceId.isEmpty());
    QVERIFY(results->item(0)->text().contains(QStringLiteral("aliases: saved command alias")));

    QTest::keyClick(searchEdit, Qt::Key_Return);

    QCOMPARE(insertedClipIds, (QStringList{savedId, savedId}));
    QVERIFY(openedTargets.isEmpty());
    QCOMPARE(panel.statusText(), QStringLiteral("Inserted clip"));
}

void WidgetSmokeTest::panelClipNewCommandShowsTemporaryHistoryAndSaves()
{
    InMemoryLibraryRepository repository;
    InMemoryClipRepository clipRepository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    const ClipCaptureResult temporary = clipRepository.captureText(QStringLiteral("temporary save candidate body"),
                                                                   {},
                                                                   {},
                                                                   base);
    QVERIFY(temporary.captured());
    QVERIFY(temporary.clip.has_value());

    const QString savedId = saveWidgetClip(clipRepository,
                                           QStringLiteral("already saved body"),
                                           QStringLiteral("Already Saved"),
                                           {},
                                           {},
                                           false,
                                           base.addSecs(1),
                                           base.addSecs(2));
    QVERIFY(!savedId.isEmpty());

    ClipSearchService clipSearch(clipRepository);
    int saveRequestCalls = 0;
    int saveHandlerCalls = 0;
    bool saveRequestParentProvided = false;
    QStringList saveRequestClipIds;
    PinloomPanelOptions options;
    options.clipSearchHandler = [&](const QString &query, const ClipSearchOptions &searchOptions) {
        return clipSearch.search(query, searchOptions);
    };
    options.clipSaveRequestProvider = [&](QWidget *parent,
                                          const ClipSearchResult &result) -> std::optional<PinloomClipSaveRequest> {
        saveRequestParentProvided = parent != nullptr;
        saveRequestClipIds.append(result.clipId);
        ++saveRequestCalls;

        PinloomClipSaveRequest request;
        request.clipId = result.clipId;
        request.name = QStringLiteral("Saved From Launcher");
        request.aliases = {QStringLiteral("launcher save alias")};
        request.tags = {QStringLiteral("#launcher-save")};
        request.pinned = true;
        return request;
    };
    options.clipSaveHandler = [&](const PinloomClipSaveRequest &request, QString *error) {
        ++saveHandlerCalls;
        if (error) {
            error->clear();
        }
        return clipSearch.saveClip(request.clipId,
                                   request.name,
                                   request.aliases,
                                   request.tags,
                                   request.pinned);
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    panel.setSearchText(QStringLiteral("c n"));

    QCOMPARE(results->count(), 1);
    QCOMPARE(panel.currentOpenTarget().clipId, temporary.clip->id);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[History] temporary save candidate body -> Save")));
    QCOMPARE(panel.statusText(), QStringLiteral("Clip save: 1 history item(s)"));

    QTest::keyClick(searchEdit, Qt::Key_Return);

    QCOMPARE(saveRequestCalls, 1);
    QVERIFY(saveRequestParentProvided);
    QCOMPARE(saveRequestClipIds, QStringList{temporary.clip->id});
    QCOMPARE(saveHandlerCalls, 1);
    QCOMPARE(panel.searchText(), QStringLiteral("c s Saved From Launcher"));
    QCOMPARE(panel.currentOpenTarget().clipId, temporary.clip->id);
    QCOMPARE(panel.statusText(), QStringLiteral("Saved clip \"Saved From Launcher\""));

    const std::optional<Clip> saved = clipRepository.findClip(temporary.clip->id);
    QVERIFY(saved.has_value());
    QVERIFY(saved->state == ClipState::Saved);
    QCOMPARE(saved->name, QStringLiteral("Saved From Launcher"));
    QCOMPARE(saved->aliases, QStringList{QStringLiteral("launcher save alias")});
    QCOMPARE(saved->tags, QStringList{QStringLiteral("launcher-save")});
    QVERIFY(saved->pinned);
    QCOMPARE(clipRepository.temporaryClips().size(), 0);

    panel.setSearchText(QStringLiteral("c n"));
    QCOMPARE(results->count(), 0);
}

void WidgetSmokeTest::panelAnchorCaptureCommandShowsPendingEntry()
{
    InMemoryLibraryRepository repository;
    PinloomPanel panel(repository);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    panel.setSearchText(QStringLiteral("k"));

    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Command] New Anchor / Capture Anchor -> Open")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("k n")));
    QCOMPARE(panel.statusText(), QStringLiteral("Anchor commands"));

    QTest::keyClick(searchEdit, Qt::Key_Return);
    QCOMPARE(panel.searchText(), QStringLiteral("k n"));
    QCOMPARE(results->count(), 1);
    QCOMPARE(panel.statusText(), QStringLiteral("Capture anchor current app context pending"));

    QTest::keyClick(searchEdit, Qt::Key_Return);
    QVERIFY(panel.statusText().contains(QStringLiteral("current app context pending")));
    QVERIFY(panel.statusText().contains(QStringLiteral("migrate to k n")));
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

void WidgetSmokeTest::panelFiltersLegacyPdfManualLineAnchors()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("iso-pdf");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("ISO CAN Spec");
    resource.location = QStringLiteral("E:/test_dir/ISO 11898-1.pdf");
    Anchor legacy;
    legacy.type = AnchorType::Manual;
    legacy.name = QStringLiteral("legacy manual line 12");
    legacy.target = legacy.name;
    legacy.line = 12;
    legacy.locatorType = QStringLiteral("manual");
    legacy.locatorJson = QStringLiteral("{\"line\":12}");
    Anchor page;
    page.type = AnchorType::Manual;
    page.name = QStringLiteral("stable PDF-XChange page");
    page.target = page.name;
    page.targetApp = QStringLiteral("PDF-XChange");
    page.targetFile = resource.location;
    page.locatorType = QStringLiteral("pdfxchange.page");
    page.locatorJson = QStringLiteral("{\"page\":12}");
    resource.anchors = {legacy, page};
    QVERIFY(repository.upsertResource(resource));

    PinloomPanel panel(repository);
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(results);

    panel.setSearchText(QStringLiteral("legacy manual line 12"));
    QCOMPARE(results->count(), 0);

    panel.setSearchText(QStringLiteral("stable PDF-XChange page"));
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("stable PDF-XChange page")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("pdfxchange.page")));
    QCOMPARE(results->item(0)->data(Qt::UserRole + 26).toString(), QStringLiteral("pdfxchange.page"));
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

void WidgetSmokeTest::panelRejectsGenericManualPdfLineAnchors()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("pdf");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Spec PDF");
    resource.location = QStringLiteral("E:/docs/spec.pdf");
    QVERIFY(repository.upsertResource(resource));

    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };

    PinloomPanel panel(repository, options);

    QVERIFY(!panel.addManualAnchorToResource(resource.id, QStringLiteral("Page twelve"), 12));
    QCOMPARE(panel.statusText(), QStringLiteral("PDF line anchors are deprecated; use PDF-XChange anchor capture"));
    QCOMPARE(statusNotifications.last(), panel.statusText());

    const std::optional<Resource> stored = repository.findResource(resource.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->anchors.size(), 0);
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
    QVERIFY(openButton->isHidden());

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

    QVERIFY(panel.currentOpenTarget().resourceId.isEmpty());
    QVERIFY(panel.currentResults().isEmpty());

    panel.setSearchText(QStringLiteral("ZeroSlack"));
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
    QCOMPARE(counts.last(), 0);
    QVERIFY(!resultSnapshots.isEmpty());
    QVERIFY(resultSnapshots.last().isEmpty());

    panel.setSearchText(QStringLiteral("UART"));
    QCOMPARE(counts.last(), 2);
    QCOMPARE(resultSnapshots.last().size(), 2);
    QCOMPARE(resultSnapshots.last().at(0).resultRow, 0);
    QCOMPARE(resultSnapshots.last().at(1).resultRow, 1);
    QVERIFY(resultSnapshots.last().at(0).matchSummary.contains(QStringLiteral("Match:")));

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

void WidgetSmokeTest::panelReportsNoPdfContextForPdfCapture()
{
    InMemoryLibraryRepository repository;

    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    QVERIFY(searchEdit);

    QTest::keyClick(searchEdit, Qt::Key_K, Qt::ControlModifier);

    QCOMPARE(panel.statusText(), QStringLiteral("Open or select a PDF before capturing an anchor"));
    QCOMPARE(statusNotifications.last(), panel.statusText());
    QVERIFY(repository.search(SearchQuery{}).isEmpty());
}

void WidgetSmokeTest::panelCapturesSelectedPdfFallbackAnchorWithMetadataOnly()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("pdf-spec");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Spec PDF");
    resource.location = QStringLiteral("E:/docs/spec.pdf");
    QVERIFY(repository.upsertResource(resource));

    int requestCount = 0;
    ManualPdfAnchorCreationRequest capturedSuggested;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };
    options.pdfAnchorCaptureRequestProvider =
        [&](const ManualPdfAnchorCreationRequest &suggested) -> std::optional<ManualPdfAnchorCreationRequest> {
        ++requestCount;
        capturedSuggested = suggested;

        ManualPdfAnchorCreationRequest request = suggested;
        request.name = QStringLiteral("Fallback anchor");
        request.aliases = {QStringLiteral("fallback alias")};
        request.tags = {QStringLiteral("#pdf-tag")};
        request.pinned = true;
        return request;
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *addAnchorButton = panel.findChild<QPushButton *>(QStringLiteral("addAnchorButton"));
    QVERIFY(searchEdit);
    QVERIFY(addAnchorButton);

    panel.setSearchText(QStringLiteral("Spec PDF"));
    QVERIFY(panel.selectFirstResult());
    QCOMPARE(addAnchorButton->text(), QStringLiteral("Capture PDF Anchor"));

    QTest::keyClick(searchEdit, Qt::Key_K, Qt::ControlModifier);

    QCOMPARE(requestCount, 1);
    QCOMPARE(capturedSuggested.file, resource.location);
    QCOMPARE(capturedSuggested.page, 1);
    QCOMPARE(capturedSuggested.rect.left, 0.0);
    QCOMPARE(capturedSuggested.rect.top, 0.0);
    QCOMPARE(capturedSuggested.rect.right, 612.0);
    QCOMPARE(capturedSuggested.rect.bottom, 792.0);
    QCOMPARE(capturedSuggested.source, QStringLiteral("selected-pdf-fallback"));
    QCOMPARE(panel.statusText(), QStringLiteral("Captured PDF anchor \"Fallback anchor\" (selected-PDF fallback)"));
    QCOMPARE(statusNotifications.last(), panel.statusText());

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("fallback alias")});
    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().matchedAnchor.has_value());
    const Anchor anchor = results.first().matchedAnchor.value();
    QCOMPARE(anchor.name, QStringLiteral("Fallback anchor"));
    QCOMPARE(anchor.targetFile, resource.location);
    QCOMPARE(anchor.locatorType, QStringLiteral("pdfxchange.rect"));
    QCOMPARE(anchor.page, 1);
    QCOMPARE(anchor.region.x(), 0.0);
    QCOMPARE(anchor.region.y(), 0.0);
    QCOMPARE(anchor.region.width(), 612.0);
    QCOMPARE(anchor.region.height(), 792.0);
    QCOMPARE(anchor.tags, QStringList{QStringLiteral("pdf-tag")});
    QVERIFY(anchor.pinned);
    QVERIFY(anchor.locatorJson.contains(QStringLiteral("\"source\":\"selected-pdf-fallback\"")));
}

void WidgetSmokeTest::manualPdfCaptureDialogKeepsRawCoordinatesAdvancedByDefault()
{
    ManualPdfAnchorCreationRequest suggested;
    suggested.file = QStringLiteral("E:/docs/spec.pdf");
    suggested.page = 1;
    suggested.rect = {0.0, 0.0, 612.0, 792.0};
    suggested.source = QStringLiteral("selected-pdf-fallback");

    ManualPdfAnchorDialog dialog(suggested);
    dialog.show();
    QApplication::processEvents();

    auto *nameEdit = dialog.findChild<QLineEdit *>(QStringLiteral("manualPdfAnchorNameEdit"));
    auto *summary = dialog.findChild<QLabel *>(QStringLiteral("manualPdfAnchorSummaryLabel"));
    auto *advancedWidget = dialog.findChild<QWidget *>(QStringLiteral("manualPdfAnchorAdvancedWidget"));
    auto *leftSpin = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("manualPdfAnchorLeftSpin"));
    auto *advancedToggle = dialog.findChild<QPushButton *>(QStringLiteral("manualPdfAnchorAdvancedToggleButton"));
    QVERIFY(nameEdit);
    QVERIFY(summary);
    QVERIFY(advancedWidget);
    QVERIFY(leftSpin);
    QVERIFY(advancedToggle);

    QVERIFY(nameEdit->isVisible());
    QVERIFY(summary->text().contains(QStringLiteral("selected-PDF fallback")));
    QVERIFY(!advancedWidget->isVisible());
    QVERIFY(!leftSpin->isVisible());
    QVERIFY(advancedToggle->isVisible());

    advancedToggle->click();
    QApplication::processEvents();

    QVERIFY(advancedWidget->isVisible());
    QVERIFY(leftSpin->isVisible());
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
    QVERIFY(statusNotifications.last().contains(QStringLiteral("Captured PDF anchor")));

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
    QCOMPARE(panel.statusText(), QStringLiteral("Captured PDF anchor \"Dialog pick window\""));
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

void WidgetSmokeTest::panelRoutesCtrlKThroughManualExcelAnchorRequestProvider()
{
    InMemoryLibraryRepository repository;

    int requestCount = 0;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };
    options.manualExcelAnchorRequestProvider = [&]() -> std::optional<ManualExcelAnchorCreationRequest> {
        ++requestCount;
        ManualExcelAnchorCreationRequest request;
        request.name = QStringLiteral("Manual Excel budget table");
        request.file = QStringLiteral("E:/books/manual-budget.xlsx");
        request.sheet = QStringLiteral("Sheet1");
        request.rangeAddress = QStringLiteral("B12:D18");
        request.aliases = {QStringLiteral("manual budget")};
        request.tags = {QStringLiteral("#phase5")};
        request.pinned = true;
        return request;
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    QVERIFY(searchEdit);

    QTest::keyClick(searchEdit, Qt::Key_K, Qt::ControlModifier);

    QCOMPARE(requestCount, 1);
    QCOMPARE(panel.statusText(), QStringLiteral("Created Excel anchor \"Manual Excel budget table\""));
    QCOMPARE(statusNotifications.last(), panel.statusText());

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("manual budget")});
    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(results.first().matchedAnchor->name, QStringLiteral("Manual Excel budget table"));
    QCOMPARE(results.first().matchedAnchor->targetApp, QStringLiteral("Microsoft Excel"));
    QCOMPARE(results.first().matchedAnchor->targetFile, QStringLiteral("E:/books/manual-budget.xlsx"));
    QCOMPARE(results.first().matchedAnchor->locatorType, QStringLiteral("excel.range"));
    QCOMPARE(results.first().matchedAnchor->tags, QStringList{QStringLiteral("phase5")});
    QVERIFY(results.first().matchedAnchor->pinned);
}

void WidgetSmokeTest::panelRoutesCtrlKThroughManualVisioAnchorRequestProvider()
{
    InMemoryLibraryRepository repository;

    int requestCount = 0;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };
    options.manualVisioAnchorRequestProvider = [&]() -> std::optional<ManualVisioAnchorCreationRequest> {
        ++requestCount;
        ManualVisioAnchorCreationRequest request;
        request.name = QStringLiteral("Manual Visio power shape");
        request.file = QStringLiteral("E:/drawings/manual-power.vsdx");
        request.page = QStringLiteral("Page-1");
        request.shapeUniqueId = QStringLiteral("{22222222-3333-4444-5555-666666666666}");
        request.aliases = {QStringLiteral("manual visio shape")};
        request.tags = {QStringLiteral("#phase5")};
        request.pinned = true;
        return request;
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    QVERIFY(searchEdit);

    QTest::keyClick(searchEdit, Qt::Key_K, Qt::ControlModifier);

    QCOMPARE(requestCount, 1);
    QCOMPARE(panel.statusText(), QStringLiteral("Created Visio anchor \"Manual Visio power shape\""));
    QCOMPARE(statusNotifications.last(), panel.statusText());

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("manual visio shape")});
    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(results.first().matchedAnchor->name, QStringLiteral("Manual Visio power shape"));
    QCOMPARE(results.first().matchedAnchor->targetApp, QStringLiteral("Microsoft Visio"));
    QCOMPARE(results.first().matchedAnchor->targetFile, QStringLiteral("E:/drawings/manual-power.vsdx"));
    QCOMPARE(results.first().matchedAnchor->locatorType, QStringLiteral("visio.shape"));
    QCOMPARE(results.first().matchedAnchor->tags, QStringList{QStringLiteral("phase5")});
    QVERIFY(results.first().matchedAnchor->locatorJson.contains(QStringLiteral("shape_unique_id")));
    QVERIFY(results.first().matchedAnchor->pinned);
}

void WidgetSmokeTest::panelRoutesCtrlKThroughManualWordAnchorRequestProvider()
{
    InMemoryLibraryRepository repository;

    int requestCount = 0;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };
    options.manualWordAnchorRequestProvider = [&]() -> std::optional<ManualWordAnchorCreationRequest> {
        ++requestCount;
        ManualWordAnchorCreationRequest request;
        request.name = QStringLiteral("Manual Word requirement");
        request.file = QStringLiteral("E:/docs/manual-requirements.docx");
        request.bookmark = QStringLiteral("Requirement_12");
        request.targetApp = QStringLiteral("MS Word");
        request.aliases = {QStringLiteral("manual word bookmark")};
        request.tags = {QStringLiteral("#phase5")};
        request.pinned = true;
        return request;
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    QVERIFY(searchEdit);

    QTest::keyClick(searchEdit, Qt::Key_K, Qt::ControlModifier);

    QCOMPARE(requestCount, 1);
    QCOMPARE(panel.statusText(), QStringLiteral("Created Word anchor \"Manual Word requirement\""));
    QCOMPARE(statusNotifications.last(), panel.statusText());

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("manual word bookmark")});
    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(results.first().matchedAnchor->name, QStringLiteral("Manual Word requirement"));
    QCOMPARE(results.first().matchedAnchor->targetApp, QStringLiteral("MS Word"));
    QCOMPARE(results.first().matchedAnchor->targetFile, QStringLiteral("E:/docs/manual-requirements.docx"));
    QCOMPARE(results.first().matchedAnchor->locatorType, QStringLiteral("word.bookmark"));
    QCOMPARE(results.first().matchedAnchor->tags, QStringList{QStringLiteral("phase5")});
    QVERIFY(results.first().matchedAnchor->locatorJson.contains(QStringLiteral("Requirement_12")));
    QVERIFY(results.first().matchedAnchor->pinned);
}

void WidgetSmokeTest::panelRoutesCtrlKThroughManualPowerPointAnchorRequestProvider()
{
    InMemoryLibraryRepository repository;

    int requestCount = 0;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };
    options.manualPowerPointAnchorRequestProvider = [&]() -> std::optional<ManualPowerPointAnchorCreationRequest> {
        ++requestCount;
        ManualPowerPointAnchorCreationRequest request;
        request.name = QStringLiteral("Manual PowerPoint valve callout");
        request.file = QStringLiteral("E:/slides/manual-process.pptx");
        request.slide = 12;
        request.shapeName = QStringLiteral("Valve A");
        request.targetApp = QStringLiteral("MS PowerPoint");
        request.aliases = {QStringLiteral("manual ppt shape")};
        request.tags = {QStringLiteral("#phase5")};
        request.pinned = true;
        return request;
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    QVERIFY(searchEdit);

    QTest::keyClick(searchEdit, Qt::Key_K, Qt::ControlModifier);

    QCOMPARE(requestCount, 1);
    QCOMPARE(panel.statusText(), QStringLiteral("Created PowerPoint anchor \"Manual PowerPoint valve callout\""));
    QCOMPARE(statusNotifications.last(), panel.statusText());

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("manual ppt shape")});
    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(results.first().matchedAnchor->name, QStringLiteral("Manual PowerPoint valve callout"));
    QCOMPARE(results.first().matchedAnchor->targetApp, QStringLiteral("MS PowerPoint"));
    QCOMPARE(results.first().matchedAnchor->targetFile, QStringLiteral("E:/slides/manual-process.pptx"));
    QCOMPARE(results.first().matchedAnchor->locatorType, QStringLiteral("powerpoint.shape"));
    QCOMPARE(results.first().matchedAnchor->tags, QStringList{QStringLiteral("phase5")});
    QVERIFY(results.first().matchedAnchor->locatorJson.contains(QStringLiteral("shape_name")));
    QVERIFY(results.first().matchedAnchor->locatorJson.contains(QStringLiteral("Valve A")));
    QVERIFY(results.first().matchedAnchor->pinned);
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
    options.applicationLaunchSettings.powerShellExecutablePath =
        QStringLiteral("C:/Tools/PowerShell/powershell.exe");
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
    QCOMPARE(capturedCommand.executablePath,
             QStringLiteral("C:/Tools/PowerShell/powershell.exe"));
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

void WidgetSmokeTest::panelLaunchesVisioAnchorWithInjectedExecutor()
{
    InMemoryLibraryRepository repository;

    const QString shapeId = QStringLiteral("{00000000-0000-0000-0000-000000000000}");

    Resource resource;
    resource.id = QStringLiteral("visio-power");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Power Drawing");
    resource.location = QStringLiteral("E:/drawings/power.vsdx");
    Anchor anchor;
    anchor.type = AnchorType::Manual;
    anchor.name = QStringLiteral("Power gate symbol");
    anchor.targetApp = QStringLiteral("MS Visio");
    anchor.targetFile = resource.location;
    anchor.locatorType = QStringLiteral("visio.shape");
    anchor.locatorJson = QStringLiteral("{\"page\":\"Page-1\",\"shape_unique_id\":\"%1\"}").arg(shapeId);
    resource.anchors = {anchor};
    QVERIFY(repository.upsertResource(resource));

    bool launched = false;
    VisioJumpCommand capturedCommand;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.applicationLaunchSettings.powerShellExecutablePath =
        QStringLiteral("C:/Tools/PowerShell/powershell.exe");
    options.visioLaunchHandler = [&](const VisioJumpCommand &command, QString *error) {
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

    panel.setSearchText(QStringLiteral("Power gate"));
    QCOMPARE(panel.resultCount(), 1);

    QTest::keyClick(searchEdit, Qt::Key_Return);

    QVERIFY(launched);
    QCOMPARE(capturedCommand.executablePath,
             QStringLiteral("C:/Tools/PowerShell/powershell.exe"));
    QCOMPARE(capturedCommand.documentPath, resource.location);
    QCOMPARE(capturedCommand.locatorType, QStringLiteral("visio.shape"));
    QCOMPARE(capturedCommand.pageName, QStringLiteral("Page-1"));
    QCOMPARE(capturedCommand.shapeUniqueId, shapeId);
    QCOMPARE(panel.statusText(), QStringLiteral("Opened Visio target"));
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

void WidgetSmokeTest::panelReportsInvalidVisioLocatorWithoutGenericOpen()
{
    CapturingUrlHandler fileHandler;
    QDesktopServices::setUrlHandler(QStringLiteral("file"), &fileHandler, "openUrl");

    {
        InMemoryLibraryRepository repository;

        Resource resource;
        resource.id = QStringLiteral("visio-unsupported");
        resource.kind = ResourceKind::File;
        resource.title = QStringLiteral("Unsupported Visio Drawing");
        resource.location = QStringLiteral("E:/drawings/unsupported.vsdx");
        Anchor anchor;
        anchor.type = AnchorType::Manual;
        anchor.name = QStringLiteral("Unsupported Visio jump");
        anchor.targetApp = QStringLiteral("Visio");
        anchor.targetFile = resource.location;
        anchor.locatorType = QStringLiteral("visio.page");
        anchor.locatorJson =
            QStringLiteral("{\"page\":\"Page-1\",\"shape_unique_id\":\"{00000000-0000-0000-0000-000000000000}\"}");
        resource.anchors = {anchor};
        QVERIFY(repository.upsertResource(resource));

        PinloomPanel panel(repository);
        panel.setSearchText(QStringLiteral("Unsupported Visio"));

        QVERIFY(!panel.activateCurrentOpenTarget());
        QCOMPARE(panel.statusText(), QStringLiteral("Visio locator type is unsupported"));
        QCOMPARE(fileHandler.openCount, 0);
        QVERIFY(!repository.resourceUsage(resource.id).has_value());
    }

    {
        InMemoryLibraryRepository repository;

        Resource resource;
        resource.id = QStringLiteral("visio-missing-shape");
        resource.kind = ResourceKind::File;
        resource.title = QStringLiteral("Missing Shape Visio Drawing");
        resource.location = QStringLiteral("E:/drawings/missing-shape.vsdx");
        Anchor anchor;
        anchor.type = AnchorType::Manual;
        anchor.name = QStringLiteral("Missing Visio shape");
        anchor.targetFile = resource.location;
        anchor.locatorType = QStringLiteral("visio.shape");
        anchor.locatorJson = QStringLiteral("{\"page\":\"Page-1\"}");
        resource.anchors = {anchor};
        QVERIFY(repository.upsertResource(resource));

        PinloomPanel panel(repository);
        panel.setSearchText(QStringLiteral("Missing Visio"));

        QVERIFY(!panel.activateCurrentOpenTarget());
        QCOMPARE(panel.statusText(), QStringLiteral("Visio shape UniqueID is missing"));
        QCOMPARE(fileHandler.openCount, 0);
        QVERIFY(!repository.resourceUsage(resource.id).has_value());
    }

    QDesktopServices::unsetUrlHandler(QStringLiteral("file"));
}

void WidgetSmokeTest::panelLaunchesWordAnchorWithInjectedExecutor()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("word-requirements");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Requirements Document");
    resource.location = QStringLiteral("E:/docs/requirements.docx");
    Anchor anchor;
    anchor.type = AnchorType::Manual;
    anchor.name = QStringLiteral("Requirement 12");
    anchor.targetApp = QStringLiteral("MS Word");
    anchor.targetFile = resource.location;
    anchor.locatorType = QStringLiteral("word.bookmark");
    anchor.locatorJson = QStringLiteral("{\"bookmark\":\"Requirement_12\"}");
    resource.anchors = {anchor};
    QVERIFY(repository.upsertResource(resource));

    bool launched = false;
    WordJumpCommand capturedCommand;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.applicationLaunchSettings.powerShellExecutablePath =
        QStringLiteral("C:/Tools/PowerShell/powershell.exe");
    options.wordLaunchHandler = [&](const WordJumpCommand &command, QString *error) {
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

    panel.setSearchText(QStringLiteral("Requirement 12"));
    QCOMPARE(panel.resultCount(), 1);

    QTest::keyClick(searchEdit, Qt::Key_Return);

    QVERIFY(launched);
    QCOMPARE(capturedCommand.executablePath,
             QStringLiteral("C:/Tools/PowerShell/powershell.exe"));
    QCOMPARE(capturedCommand.documentPath, resource.location);
    QCOMPARE(capturedCommand.locatorType, QStringLiteral("word.bookmark"));
    QCOMPARE(capturedCommand.bookmarkName, QStringLiteral("Requirement_12"));
    QCOMPARE(panel.statusText(), QStringLiteral("Opened Word target"));
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

void WidgetSmokeTest::panelReportsInvalidWordLocatorWithoutGenericOpen()
{
    CapturingUrlHandler fileHandler;
    QDesktopServices::setUrlHandler(QStringLiteral("file"), &fileHandler, "openUrl");

    {
        InMemoryLibraryRepository repository;

        Resource resource;
        resource.id = QStringLiteral("word-unsupported");
        resource.kind = ResourceKind::File;
        resource.title = QStringLiteral("Unsupported Word Document");
        resource.location = QStringLiteral("E:/docs/unsupported.docx");
        Anchor anchor;
        anchor.type = AnchorType::Manual;
        anchor.name = QStringLiteral("Unsupported Word jump");
        anchor.targetApp = QStringLiteral("Word");
        anchor.targetFile = resource.location;
        anchor.locatorType = QStringLiteral("word.heading");
        anchor.locatorJson = QStringLiteral("{\"bookmark\":\"Requirement_12\"}");
        resource.anchors = {anchor};
        QVERIFY(repository.upsertResource(resource));

        PinloomPanel panel(repository);
        panel.setSearchText(QStringLiteral("Unsupported Word"));

        QVERIFY(!panel.activateCurrentOpenTarget());
        QCOMPARE(panel.statusText(), QStringLiteral("Word locator type is unsupported"));
        QCOMPARE(fileHandler.openCount, 0);
        QVERIFY(!repository.resourceUsage(resource.id).has_value());
    }

    {
        InMemoryLibraryRepository repository;

        Resource resource;
        resource.id = QStringLiteral("word-missing-bookmark");
        resource.kind = ResourceKind::File;
        resource.title = QStringLiteral("Missing Bookmark Word Document");
        resource.location = QStringLiteral("E:/docs/missing-bookmark.docx");
        Anchor anchor;
        anchor.type = AnchorType::Manual;
        anchor.name = QStringLiteral("Missing Word bookmark");
        anchor.targetFile = resource.location;
        anchor.locatorType = QStringLiteral("word.bookmark");
        anchor.locatorJson = QStringLiteral("{}");
        resource.anchors = {anchor};
        QVERIFY(repository.upsertResource(resource));

        PinloomPanel panel(repository);
        panel.setSearchText(QStringLiteral("Missing Word"));

        QVERIFY(!panel.activateCurrentOpenTarget());
        QCOMPARE(panel.statusText(), QStringLiteral("Word bookmark is missing"));
        QCOMPARE(fileHandler.openCount, 0);
        QVERIFY(!repository.resourceUsage(resource.id).has_value());
    }

    QDesktopServices::unsetUrlHandler(QStringLiteral("file"));
}

void WidgetSmokeTest::panelLaunchesPowerPointAnchorWithInjectedExecutor()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("powerpoint-process");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Process Presentation");
    resource.location = QStringLiteral("E:/slides/process.pptx");
    Anchor anchor;
    anchor.type = AnchorType::Manual;
    anchor.name = QStringLiteral("Valve A callout");
    anchor.targetApp = QStringLiteral("MS PowerPoint");
    anchor.targetFile = resource.location;
    anchor.locatorType = QStringLiteral("powerpoint.shape");
    anchor.locatorJson = QStringLiteral("{\"slide\":12,\"shape_name\":\"Valve A\"}");
    resource.anchors = {anchor};
    QVERIFY(repository.upsertResource(resource));

    bool launched = false;
    PowerPointJumpCommand capturedCommand;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.applicationLaunchSettings.powerShellExecutablePath =
        QStringLiteral("C:/Tools/PowerShell/powershell.exe");
    options.powerPointLaunchHandler = [&](const PowerPointJumpCommand &command, QString *error) {
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

    panel.setSearchText(QStringLiteral("Valve A"));
    QCOMPARE(panel.resultCount(), 1);

    QTest::keyClick(searchEdit, Qt::Key_Return);

    QVERIFY(launched);
    QCOMPARE(capturedCommand.executablePath,
             QStringLiteral("C:/Tools/PowerShell/powershell.exe"));
    QCOMPARE(capturedCommand.presentationPath, resource.location);
    QCOMPARE(capturedCommand.locatorType, QStringLiteral("powerpoint.shape"));
    QCOMPARE(capturedCommand.slideIndex, 12);
    QCOMPARE(capturedCommand.shapeId, -1);
    QCOMPARE(capturedCommand.shapeName, QStringLiteral("Valve A"));
    QCOMPARE(panel.statusText(), QStringLiteral("Opened PowerPoint target"));
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

void WidgetSmokeTest::panelReportsInvalidPowerPointLocatorWithoutGenericOpen()
{
    CapturingUrlHandler fileHandler;
    QDesktopServices::setUrlHandler(QStringLiteral("file"), &fileHandler, "openUrl");

    {
        InMemoryLibraryRepository repository;

        Resource resource;
        resource.id = QStringLiteral("powerpoint-unsupported");
        resource.kind = ResourceKind::File;
        resource.title = QStringLiteral("Unsupported PowerPoint Deck");
        resource.location = QStringLiteral("E:/slides/unsupported.pptx");
        Anchor anchor;
        anchor.type = AnchorType::Manual;
        anchor.name = QStringLiteral("Unsupported PowerPoint jump");
        anchor.targetApp = QStringLiteral("PowerPoint");
        anchor.targetFile = resource.location;
        anchor.locatorType = QStringLiteral("powerpoint.slide");
        anchor.locatorJson = QStringLiteral("{\"slide\":12,\"shape_id\":42}");
        resource.anchors = {anchor};
        QVERIFY(repository.upsertResource(resource));

        PinloomPanel panel(repository);
        panel.setSearchText(QStringLiteral("Unsupported PowerPoint"));

        QVERIFY(!panel.activateCurrentOpenTarget());
        QCOMPARE(panel.statusText(), QStringLiteral("PowerPoint locator type is unsupported"));
        QCOMPARE(fileHandler.openCount, 0);
        QVERIFY(!repository.resourceUsage(resource.id).has_value());
    }

    {
        InMemoryLibraryRepository repository;

        Resource resource;
        resource.id = QStringLiteral("powerpoint-missing-shape");
        resource.kind = ResourceKind::File;
        resource.title = QStringLiteral("Missing Shape PowerPoint Deck");
        resource.location = QStringLiteral("E:/slides/missing-shape.pptx");
        Anchor anchor;
        anchor.type = AnchorType::Manual;
        anchor.name = QStringLiteral("Missing PowerPoint shape");
        anchor.targetFile = resource.location;
        anchor.locatorType = QStringLiteral("powerpoint.shape");
        anchor.locatorJson = QStringLiteral("{\"slide\":12}");
        resource.anchors = {anchor};
        QVERIFY(repository.upsertResource(resource));

        PinloomPanel panel(repository);
        panel.setSearchText(QStringLiteral("Missing PowerPoint"));

        QVERIFY(!panel.activateCurrentOpenTarget());
        QCOMPARE(panel.statusText(), QStringLiteral("PowerPoint shape id or name is missing"));
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
    options.applicationLaunchSettings.pdfXChangeExecutablePath =
        QStringLiteral("C:/Tools/PDFXEdit.exe");
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
    QCOMPARE(capturedCommand.action, QStringLiteral("page=12;zoom=250;highlight=420,780,860,920;usept=yes"));
    QCOMPARE(capturedCommand.arguments,
             QStringList({QStringLiteral("/A"),
                          QStringLiteral("page=12;zoom=250;highlight=420,780,860,920;usept=yes"),
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
