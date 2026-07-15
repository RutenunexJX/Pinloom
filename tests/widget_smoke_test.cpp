#include "pinloom/clip/ClipHotkeyService.h"
#include "pinloom/clip/ClipRepository.h"
#include "pinloom/clip/ClipSearch.h"
#include "pinloom/clip/ClipTrayController.h"
#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/SumatraPdfForegroundCapture.h"
#include "pinloom/widgets/ClipResidentHost.h"
#include "pinloom/widgets/ClipResidentRuntime.h"
#include "pinloom/widgets/ClipTrayPresenter.h"
#include "pinloom/widgets/MainPanelHotkey.h"
#include "pinloom/widgets/ManualPdfAnchorDialog.h"
#include "pinloom/widgets/PinloomCommandPanel.h"
#include "pinloom/widgets/PinloomMainWindow.h"
#include "pinloom/widgets/PinloomPanel.h"
#include "pinloom/widgets/PinloomSettingsDialog.h"
#include "pinloom/widgets/PinloomSingleInstance.h"
#include "pinloom/widgets/SumatraPdfRegionCaptureOverlay.h"
#include "pinloom/widgets/TextPreviewDialog.h"

#include <QAction>
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
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
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
    void clipTrayPresenterShowsAndRoutesTrayActions();
    void clipTrayPresenterUpdatesPauseResumeState();
    void clipTrayPresenterSyncsRuntimeStatusAndErrors();
    void clipTrayControllerRoutesSettingsAndDiagnosticsActions();
    void clipResidentRuntimeStartsStopsCaptureAndTray();
    void clipResidentRuntimePauseResumeAndQuitActions();
    void clipResidentFactoryReportsMissingDependencies();
    void clipResidentFactoryCreatesInMemoryAndSqliteHosts();
    void clipResidentHostForwardsStartStopQuitToRuntime();
    void mainWindowCloseHidesToTray();
    void mainWindowReportsResidentDiagnosticsAndRecentError();
    void mainPanelHotkeyRegistersAndShowsCommandWindow();
    void singleInstanceGuardActivatesPrimaryFromSecondLaunch();
    void settingsDialogRoundTripsRuntimeSettings();
    void sumatraPdfRegionOverlayCapturesDdeRectangle();
    void sumatraPdfRegionOverlayRejectsCrossPageAndCancels();
    void panelUsesInjectedRepository();
    void panelDefaultsToLauncherSurface();
    void panelSearchesSavedClipsAndEnterInserts();
    void panelTreatsCommandPrefixesAsPlainSearchText();
    void commandPanelClipRootCommandShowsCandidates();
    void commandPanelClipSearchCommandSearchesHistoryAndSavedClipsAndEnterInserts();
    void commandPanelClipNewCommandShowsTemporaryHistoryAndSaves();
    void commandPanelAnchorCaptureCommandCallsHandler();
    void commandPanelInboxRootCommandShowsCandidates();
    void commandPanelInboxNewCommandSavesPendingFile();
    void commandPanelInboxNewReportsMissingPendingAndExplorerSelection();
    void commandPanelInboxSearchOpensSearchWindow();
    void commandPanelPlainQueryShowsUnifiedMixedResults();
    void commandPanelPlainQueryEnterDispatchesByTargetType();
    void commandPanelRightArrowShowsActionsForUnifiedResultTypes();
    void commandPanelActionListExecutesSelectedProviderAction();
    void commandPanelActionListExecutesRemoveWithInjectedConfirmation();
    void commandPanelRestoreCommandUsesDeletedEntrySearchHandler();
    void commandPanelStructuredEntryActionResultReportsDiagnostics();
    void commandPanelRightArrowPrimaryUsesStructuredEntryCommandHandler();
    void commandPanelActionListReturnsWithEscapeOrLeft();
    void commandPanelDoesNotDependOnAltCtrlDeleteActionShortcuts();
    void pinloomEntriesSortMixedResultsByMatchBucketAndSignals();
    void commandPanelPlainQueryUsesUnifiedEntrySearchHandler();
    void entryActionProviderBuildsActionsForUnifiedTypes();
    void commandPanelPlainQueryUsesUnifiedRankingOrder();
    void commandPanelAnchorCaptureCreatesForegroundPdfAnchorWithoutSelectedResult();
    void commandPanelAnchorCaptureUsesUniqueTitleFallbackWithoutFullPath();
    void commandPanelAnchorCapturePrefersForegroundPdfFallback();
    void commandPanelAnchorCaptureReportsNonPdfForegroundWithoutSelectedFallback();
    void commandPanelAnchorCaptureReportsForegroundPdfWithoutFilePath();
    void panelDisplaysAnchorAwareResults();
    void panelDisplaysAndOpensInboxFileEntries();
    void panelSearchEntriesCanIncludeDeletedEntriesForRestore();
    void panelDisplaysAnchorLocatorMetadata();
    void panelDisplaysMarkerAnchors();
    void panelDisplaysBeaconLineResults();
    void panelDisplaysFileLineResults();
    void panelDisplaysPdfPageResults();
    void panelPreservesPdfRegionOpenTarget();
    void panelAddsManualAliasAndAnchor();
    void panelRejectsGenericManualPdfLineAnchors();
    void panelPinsSelectedResource();
    void panelSupportsEmbeddedChromeOptions();
    void panelAppliesRequiredTagLocationAndKindFiltering();
    void panelAppliesHostContextSnapshot();
    void panelAppliesHostContextRanking();
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
    void manualPdfCaptureDialogExplainsForegroundViewStateFallback();
    void panelRoutesCtrlKThroughManualPdfAnchorRequestProvider();
    void panelCreatesManualPdfAnchorThroughDialogHook();
    void panelCancelsManualPdfAnchorDialogHookWithoutSaving();
    void panelReportsInvalidManualPdfAnchorDialogHookRequest();
    void panelLaunchesExcelAnchorWithInjectedExecutor();
    void panelReportsInvalidExcelLocatorWithoutGenericOpen();
    void panelLaunchesVisioAnchorWithInjectedExecutor();
    void panelReportsInvalidVisioLocatorWithoutGenericOpen();
    void panelLaunchesWordAnchorWithInjectedExecutor();
    void panelReportsInvalidWordLocatorWithoutGenericOpen();
    void panelLaunchesPowerPointAnchorWithInjectedExecutor();
    void panelReportsInvalidPowerPointLocatorWithoutGenericOpen();
    void panelLaunchesSumatraPdfAnchorWithInjectedExecutor();
    void panelReportsMissingSumatraPdfExecutable();
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

static Anchor testAnchor(const QString &name,
                         const QString &locatorType = QStringLiteral("manual"),
                         int line = -1)
{
    Anchor anchor;
    anchor.name = name;
    anchor.locatorType = locatorType;
    QJsonObject locator{{QStringLiteral("type"), locatorType}};
    if (line > 0) {
        locator.insert(QStringLiteral("line"), line);
    }
    anchor.locatorJson =
        QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));
    return anchor;
}

static QList<PinloomOpenTarget> makeCommandPanelUnifiedTargets()
{
    QList<PinloomOpenTarget> targets;

    PinloomOpenTarget anchorTarget;
    anchorTarget.resourceId = QStringLiteral("anchor-spec");
    anchorTarget.resourceKind = ResourceKind::Pdf;
    anchorTarget.title = QStringLiteral("Clock Spec");
    anchorTarget.location = QStringLiteral("E:/docs/clock.pdf");
    anchorTarget.matchedField = QStringLiteral("anchor_name");
    anchorTarget.matchSummary = QStringLiteral("Match: anchor_name");
    Anchor anchor;
    anchor.id = QStringLiteral("anchor-spec#jitter");
    anchor.name = QStringLiteral("PLL jitter budget");
    anchor.targetApp = QStringLiteral("SumatraPDF");
    anchor.targetFile = anchorTarget.location;
    anchor.locatorType = QStringLiteral("sumatrapdf.rect");
    anchor.locatorJson = QStringLiteral("{\"page\":12}");
    anchor.aliases = {QStringLiteral("pll budget")};
    anchor.tags = {QStringLiteral("clock")};
    anchorTarget.anchor = anchor;
    targets.append(anchorTarget);

    PinloomOpenTarget clipTarget;
    clipTarget.clipId = QStringLiteral("clip-launch");
    clipTarget.title = QStringLiteral("Launch Clip");
    clipTarget.location = QStringLiteral("launch clip body");
    clipTarget.matchedField = QStringLiteral("name");
    clipTarget.matchSummary = QStringLiteral("match: name=Launch Clip");
    targets.append(clipTarget);

    PinloomOpenTarget inboxTarget;
    inboxTarget.resourceId = inboxResourceIdForPath(QStringLiteral("E:/inbox/Board Spec.txt"));
    inboxTarget.resourceKind = ResourceKind::File;
    inboxTarget.title = QStringLiteral("Board Spec Inbox");
    inboxTarget.location = QStringLiteral("E:/inbox/Board Spec.txt");
    inboxTarget.matchedField = QStringLiteral("alias");
    inboxTarget.matchSummary = QStringLiteral("Match: alias");
    targets.append(inboxTarget);

    PinloomOpenTarget fileTarget;
    fileTarget.resourceId = QStringLiteral("file-schematic");
    fileTarget.resourceKind = ResourceKind::File;
    fileTarget.title = QStringLiteral("Schematic File");
    fileTarget.location = QStringLiteral("E:/docs/schematic.pdf");
    fileTarget.matchedField = QStringLiteral("title");
    fileTarget.matchSummary = QStringLiteral("Match: title");
    targets.append(fileTarget);

    return targets;
}

static QList<PinloomEntry> commandPanelEntries(const QList<PinloomOpenTarget> &targets)
{
    QList<PinloomEntry> entries;
    entries.reserve(targets.size());
    for (int index = 0; index < targets.size(); ++index) {
        PinloomEntry entry = entryFromOpenTarget(targets.at(index));
        entry.matchedField = QStringLiteral("title");
        entry.score = static_cast<double>(index);
        entries.append(entry);
    }
    return entries;
}

static QList<PinloomCommandResultAction> makeCommandPanelActionsForTarget(const PinloomOpenTarget &target)
{
    QList<PinloomCommandResultAction> actions;
    PinloomCommandResultAction primary;
    primary.id = QStringLiteral("primary");
    primary.label = target.clipId.isEmpty()
        ? (target.anchor.has_value() ? QStringLiteral("Jump") : QStringLiteral("Open"))
        : QStringLiteral("Insert");
    primary.detail = QStringLiteral("Run primary action");
    actions.append(primary);

    PinloomCommandResultAction alias;
    alias.id = QStringLiteral("add_alias");
    alias.label = QStringLiteral("Add alias");
    alias.detail = QStringLiteral("Add alias through fake provider");
    actions.append(alias);

    PinloomCommandResultAction tag;
    tag.id = QStringLiteral("add_tag");
    tag.label = QStringLiteral("Add tag");
    tag.detail = QStringLiteral("Add tag through fake provider");
    actions.append(tag);

    PinloomCommandResultAction remove;
    remove.id = QStringLiteral("remove");
    remove.label = QStringLiteral("Delete / Remove");
    remove.detail = QStringLiteral("Remove through fake provider");
    remove.enabled = false;
    remove.disabledReason = QStringLiteral("Remove is disabled in fake provider");
    actions.append(remove);
    return actions;
}

static ClipResidentRuntimeDependencies makeResidentRuntimeDependencies(FakeClipboardTextSource &captureClipboard,
                                                                       FakeClipboardTextAccessor &insertionClipboard,
                                                                       FakeClipTrayBackend &trayBackend,
                                                                       int &pasteCalls)
{
    ClipResidentRuntimeDependencies dependencies;
    dependencies.captureClipboard = &captureClipboard;
    dependencies.insertionClipboard = &insertionClipboard;
    dependencies.trayBackend = &trayBackend;
    dependencies.pasteInvoker = [&pasteCalls]() {
        ++pasteCalls;
        return true;
    };
    return dependencies;
}


void WidgetSmokeTest::clipTrayPresenterShowsAndRoutesTrayActions()
{
    ClipTrayController controller;
    FakeClipTrayBackend trayBackend;
    ClipTrayPresenter presenter(controller, trayBackend);
    int showSignalCount = 0;
    int quitSignalCount = 0;
    QObject::connect(&controller, &ClipTrayController::showClipboardRequested, [&]() {
        ++showSignalCount;
    });
    QObject::connect(&controller, &ClipTrayController::quitRequested, [&]() {
        ++quitSignalCount;
    });

    QCOMPARE(trayBackend.toolTip(),
             QStringLiteral("Pinloom\n"
                            "Clip: stopped\n"
                            "Clip capture: stopped"));
    QVERIFY(!trayBackend.visible());
    QCOMPARE(trayBackend.actionChanges(), 1);
    QVERIFY(presentedActionById(trayBackend.actions(), QStringLiteral("show_clipboard")).has_value());
    QVERIFY(presentedActionById(trayBackend.actions(), QStringLiteral("settings")).has_value());
    QVERIFY(presentedActionById(trayBackend.actions(), QStringLiteral("diagnostics")).has_value());
    const std::optional<ClipTrayPresentedAction> quitAction =
        presentedActionById(trayBackend.actions(), QStringLiteral("quit"));
    QVERIFY(quitAction.has_value());
    QCOMPARE(quitAction->title, QStringLiteral("Quit Pinloom"));

    presenter.show();
    QVERIFY(trayBackend.visible());
    presenter.hide();
    QVERIFY(!trayBackend.visible());
    QCOMPARE(trayBackend.visibleChanges(), 2);

    trayBackend.triggerAction(QStringLiteral("show_clipboard"));
    QCOMPARE(showSignalCount, 1);

    trayBackend.activatePrimary();
    QCOMPARE(showSignalCount, 2);

    trayBackend.triggerAction(QStringLiteral("quit"));
    QCOMPARE(quitSignalCount, 1);
}

void WidgetSmokeTest::clipTrayPresenterUpdatesPauseResumeState()
{
    QList<bool> pausedStates;
    ClipTrayController controller;
    QObject::connect(&controller, &ClipTrayController::capturePausedChanged, [&](bool paused) {
        pausedStates.append(paused);
    });
    FakeClipTrayBackend trayBackend;
    ClipTrayPresenter presenter(controller, trayBackend);

    std::optional<ClipTrayPresentedAction> toggleAction =
        presentedActionById(trayBackend.actions(), QStringLiteral("toggle_capture"));
    QVERIFY(toggleAction.has_value());
    QCOMPARE(toggleAction->title, QStringLiteral("Pause Capture"));
    QVERIFY(toggleAction->checkable);
    QVERIFY(!toggleAction->checked);

    QVERIFY(controller.start());
    QCOMPARE(trayBackend.toolTip(),
             QStringLiteral("Pinloom\n"
                            "Clip: running\n"
                            "Clip capture: active"));

    trayBackend.triggerAction(QStringLiteral("toggle_capture"));

    QCOMPARE(pausedStates, (QList<bool>{true}));
    QVERIFY(controller.capturePaused());
    toggleAction = presentedActionById(trayBackend.actions(), QStringLiteral("toggle_capture"));
    QVERIFY(toggleAction.has_value());
    QCOMPARE(toggleAction->title, QStringLiteral("Resume Capture"));
    QVERIFY(toggleAction->checkable);
    QVERIFY(toggleAction->checked);
    QCOMPARE(controller.status(), QStringLiteral("Running, capture paused"));
    QCOMPARE(trayBackend.toolTip(),
             QStringLiteral("Pinloom\n"
                            "Clip: running\n"
                            "Clip capture: paused"));

    trayBackend.triggerAction(QStringLiteral("toggle_capture"));

    QCOMPARE(pausedStates, (QList<bool>{true, false}));
    QVERIFY(!controller.capturePaused());
    toggleAction = presentedActionById(trayBackend.actions(), QStringLiteral("toggle_capture"));
    QVERIFY(toggleAction.has_value());
    QCOMPARE(toggleAction->title, QStringLiteral("Pause Capture"));
    QVERIFY(toggleAction->checkable);
    QVERIFY(!toggleAction->checked);
    QCOMPARE(trayBackend.toolTip(),
             QStringLiteral("Pinloom\n"
                            "Clip: running\n"
                            "Clip capture: active"));
}

void WidgetSmokeTest::clipTrayPresenterSyncsRuntimeStatusAndErrors()
{
    ClipTrayController controller;
    FakeClipTrayBackend trayBackend;
    ClipTrayPresenter presenter(controller, trayBackend);

    QCOMPARE(presenter.toolTipText(),
             QStringLiteral("Pinloom\n"
                            "Clip: stopped\n"
                            "Clip capture: stopped"));
    QCOMPARE(trayBackend.toolTip(), presenter.toolTipText());

    QVERIFY(controller.start());
    QVERIFY(controller.isRunning());
    QCOMPARE(trayBackend.toolTip(),
             QStringLiteral("Pinloom\n"
                            "Clip: running\n"
                            "Clip capture: active"));

    QVERIFY(!controller.triggerAction(QStringLiteral("missing")));
    QCOMPARE(controller.lastError(), QStringLiteral("Unknown tray action: missing"));
    QCOMPARE(trayBackend.toolTip(),
             QStringLiteral("Pinloom\n"
                            "Clip: running\n"
                            "Clip capture: active\n"
                            "Last error: Unknown tray action: missing"));

    controller.stop();
    QVERIFY(!controller.isRunning());
    QVERIFY(controller.start());
    QVERIFY(controller.lastError().isEmpty());
    QCOMPARE(presenter.toolTipText(), trayBackend.toolTip());
}

void WidgetSmokeTest::clipTrayControllerRoutesSettingsAndDiagnosticsActions()
{
    int settingsSignalCount = 0;
    int diagnosticsSignalCount = 0;
    ClipTrayController controller;
    QObject::connect(&controller, &ClipTrayController::settingsRequested, [&]() {
        ++settingsSignalCount;
    });
    QObject::connect(&controller, &ClipTrayController::diagnosticsRequested, [&]() {
        ++diagnosticsSignalCount;
    });

    const QList<ClipTrayAction> actions = controller.actions();
    QVERIFY(std::any_of(actions.cbegin(), actions.cend(), [](const ClipTrayAction &action) {
        return action.id == QLatin1String("settings");
    }));
    QVERIFY(std::any_of(actions.cbegin(), actions.cend(), [](const ClipTrayAction &action) {
        return action.id == QLatin1String("diagnostics");
    }));
    QVERIFY(controller.triggerAction(QStringLiteral("settings")));
    QVERIFY(controller.triggerAction(QStringLiteral("diagnostics")));

    QCOMPARE(settingsSignalCount, 1);
    QCOMPARE(diagnosticsSignalCount, 1);
}

void WidgetSmokeTest::clipResidentRuntimeStartsStopsCaptureAndTray()
{
    InMemoryClipRepository repository;
    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentRuntime runtime(repository,
                                makeResidentRuntimeDependencies(captureClipboard,
                                                                insertionClipboard,
                                                                trayBackend,
                                                                pasteCalls));
    QList<bool> runningSignals;
    QObject::connect(&runtime, &ClipResidentRuntime::runningChanged, [&](bool running) {
        runningSignals.append(running);
    });

    QVERIFY(runtime.start());

    QVERIFY(runtime.isRunning());
    QVERIFY(runtime.captureService().isRunning());
    QVERIFY(trayBackend.visible());
    QCOMPARE(trayBackend.visibleChanges(), 1);

    captureClipboard.setText(QStringLiteral("runtime captured text"));
    QCOMPARE(repository.temporaryClips().size(), 1);

    runtime.stop();
    runtime.stop();

    QVERIFY(!runtime.isRunning());
    QVERIFY(!runtime.captureService().isRunning());
    QVERIFY(!trayBackend.visible());
    QCOMPARE(trayBackend.visibleChanges(), 2);
    QCOMPARE(runningSignals, (QList<bool>{true, false}));
    QCOMPARE(pasteCalls, 0);
}

void WidgetSmokeTest::clipResidentRuntimePauseResumeAndQuitActions()
{
    InMemoryClipRepository repository;
    FakeClipboardTextSource captureClipboard;
    FakeClipboardTextAccessor insertionClipboard;
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentRuntime runtime(repository,
                                makeResidentRuntimeDependencies(captureClipboard,
                                                                insertionClipboard,
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
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentRuntimeDependencies dependencies = makeResidentRuntimeDependencies(captureClipboard,
                                                                                  insertionClipboard,
                                                                                  trayBackend,
                                                                                  pasteCalls);

    dependencies.trayBackend = nullptr;
    const ClipResidentHostResult missingTray = factory.createHost(dependencies);
    QVERIFY(!missingTray.succeeded());
    QCOMPARE(missingTray.error, QStringLiteral("Tray backend is required"));

    dependencies = makeResidentRuntimeDependencies(captureClipboard,
                                                   insertionClipboard,
                                                   trayBackend,
                                                   pasteCalls);
    dependencies.pasteInvoker = {};
    const ClipResidentHostResult missingPaste = factory.createHost(dependencies);
    QVERIFY(!missingPaste.succeeded());
    QCOMPARE(missingPaste.error, QStringLiteral("Paste invoker is required"));

    dependencies = makeResidentRuntimeDependencies(captureClipboard,
                                                   insertionClipboard,
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
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentRuntimeFactoryOptions inMemoryOptions;
    inMemoryOptions.runtimeOptions.insertionOptions.restoreOriginalClipboardOnSuccess = false;
    inMemoryOptions.runtimeOptions.insertionOptions.markClipUsedOnSuccess = false;

    ClipResidentHostResult inMemoryResult =
        factory.createHost(makeResidentRuntimeDependencies(captureClipboard,
                                                           insertionClipboard,
                                                           trayBackend,
                                                           pasteCalls),
                           inMemoryOptions);

    QVERIFY2(inMemoryResult.succeeded(), qPrintable(inMemoryResult.error));
    QVERIFY(inMemoryResult.host->runtime());
    QVERIFY(inMemoryResult.host->inMemoryRepository());
    QVERIFY(!inMemoryResult.host->sqliteRepository());
    const ClipInsertionOptions configuredInsertion = inMemoryResult.host->runtime()->insertionService().options();
    QVERIFY(!configuredInsertion.restoreOriginalClipboardOnSuccess);
    QVERIFY(!configuredInsertion.markClipUsedOnSuccess);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FakeClipboardTextSource sqliteCaptureClipboard;
    FakeClipboardTextAccessor sqliteInsertionClipboard;
    FakeClipTrayBackend sqliteTrayBackend;
    int sqlitePasteCalls = 0;
    ClipResidentRuntimeFactoryOptions sqliteOptions;
    sqliteOptions.repositoryKind = ClipResidentRepositoryKind::SQLite;
    sqliteOptions.sqliteDatabasePath = dir.filePath(QStringLiteral("pinloom_clip.sqlite3"));

    ClipResidentHostResult sqliteResult =
        factory.createHost(makeResidentRuntimeDependencies(sqliteCaptureClipboard,
                                                           sqliteInsertionClipboard,
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
    FakeClipTrayBackend trayBackend;
    int pasteCalls = 0;
    ClipResidentRuntimeFactory factory;
    ClipResidentHostResult result =
        factory.createInMemoryHost(std::move(repository),
                                   makeResidentRuntimeDependencies(captureClipboard,
                                                                   insertionClipboard,
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
    QVERIFY(trayBackend.visible());

    host.stop();
    QVERIFY(!host.isRunning());
    QVERIFY(!host.runtime()->isRunning());
    QVERIFY(!host.runtime()->captureService().isRunning());
    QVERIFY(!trayBackend.visible());

    QVERIFY(host.start());
    host.requestQuit();

    QCOMPARE(quitSignals, 1);
    QVERIFY(host.runtime()->quitWasRequested());
    QVERIFY(!host.isRunning());
    QVERIFY(!host.runtime()->captureService().isRunning());
    QCOMPARE(runningSignals, (QList<bool>{true, false, true, false}));
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

void WidgetSmokeTest::mainWindowReportsResidentDiagnosticsAndRecentError()
{
    PinloomMainWindow window;
    PinloomResidentStatus status;
    status.running = true;
    status.mainHotkeyRegistered = true;
    status.mainHotkeyText = QStringLiteral("Ctrl+Space");
    status.hyperHotkeyRegistered = true;
    status.hyperHotkeyText = QStringLiteral("Hyper+S");
    status.clipCaptureActive = true;
    status.clipStatus = QStringLiteral("Running");
    window.setResidentStatus(status);

    QVERIFY(window.residentStatusSummary().contains(QStringLiteral("Pinloom running")));
    QVERIFY(window.residentStatusSummary().contains(QStringLiteral("Command hotkey: registered (Ctrl+Space)")));
    QVERIFY(window.residentStatusSummary().contains(QStringLiteral("Hyper hotkey: registered (Hyper+S)")));
    QVERIFY(window.residentStatusSummary().contains(QStringLiteral("Clip capture: active")));

    window.setRecentError(QStringLiteral("Hotkey conflict"),
                          QStringLiteral("Ctrl+Space registration failed with fake error 1409"));
    QCOMPARE(window.recentError(), QStringLiteral("Hotkey conflict"));
    QVERIFY(window.diagnosticsText().contains(QStringLiteral("Hotkey conflict")));
    QVERIFY(window.diagnosticsText().contains(QStringLiteral("fake error 1409")));

    int settingsSignals = 0;
    int quitSignals = 0;
    QObject::connect(&window, &PinloomMainWindow::settingsRequested, [&]() {
        ++settingsSignals;
    });
    QObject::connect(&window, &PinloomMainWindow::quitRequested, [&]() {
        ++quitSignals;
    });

    auto *settingsAction = window.findChild<QAction *>(QStringLiteral("settingsAction"));
    auto *quitAction = window.findChild<QAction *>(QStringLiteral("quitAction"));
    QVERIFY(settingsAction);
    QVERIFY(quitAction);
    settingsAction->trigger();
    quitAction->trigger();
    QCOMPARE(settingsSignals, 1);
    QCOMPARE(quitSignals, 1);
}

void WidgetSmokeTest::mainPanelHotkeyRegistersAndShowsCommandWindow()
{
    InMemoryLibraryRepository repository;
    QMainWindow searchWindow;
    auto *panel = new PinloomPanel(repository, &searchWindow);
    searchWindow.setCentralWidget(panel);
    searchWindow.hide();

    QMainWindow commandWindow;
    auto *commandPanel = new PinloomCommandPanel(&commandWindow);
    commandWindow.setCentralWidget(commandPanel);
    commandWindow.hide();

    auto *commandEdit = commandPanel->findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"));
    auto *searchEdit = panel->findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    QVERIFY(commandEdit);
    QVERIFY(searchEdit);
    commandPanel->setCommandText(QStringLiteral("c"));
    commandEdit->clearFocus();

    FakeClipHotkeyBackend hotkeyBackend;
    ClipHotkeyService service(defaultMainPanelHotkeyConfig(), &hotkeyBackend);
    MainPanelHotkeyController controller(service,
                                         [&commandWindow, commandPanel]() {
                                             showCommandPanelForHotkey(commandWindow, *commandPanel);
                                         });

    QVERIFY(service.start());
    QVERIFY(hotkeyBackend.registered());
    QCOMPARE(hotkeyBackend.registeredConfig().key, Qt::Key_Space);
    QCOMPARE(hotkeyBackend.registeredConfig().modifiers, Qt::KeyboardModifiers(Qt::ControlModifier));

    hotkeyBackend.activate();
    QApplication::processEvents();

    QVERIFY(commandWindow.isVisible());
    QVERIFY(!searchWindow.isVisible());
    QCOMPARE(commandPanel->focusWidget(), static_cast<QWidget *>(commandEdit));
    QCOMPARE(commandEdit->selectedText(), QStringLiteral("c"));
    QVERIFY(!searchEdit->hasFocus());
}

void WidgetSmokeTest::singleInstanceGuardActivatesPrimaryFromSecondLaunch()
{
    const QString serverName = QStringLiteral("pinloom-test-%1-%2")
                                   .arg(QCoreApplication::applicationPid())
                                   .arg(QDateTime::currentMSecsSinceEpoch());
    PinloomSingleInstanceGuard primary({serverName, 100});
    const PinloomSingleInstanceStartResult primaryResult = primary.start();
    QVERIFY2(primaryResult.isPrimary(), qPrintable(primaryResult.error));
    int activationCount = 0;
    QString activationMessage;
    QObject::connect(&primary, &PinloomSingleInstanceGuard::activationRequested, [&](const QString &message) {
        ++activationCount;
        activationMessage = message;
    });

    const QString customMessage = QStringLiteral("{\"type\":\"sumatrapdf.opened\",\"file\":\"E:/docs/spec.pdf\"}");
    QString sendError;
    QVERIFY2(sendPinloomSingleInstanceMessage(serverName, customMessage, 100, &sendError), qPrintable(sendError));
    QTRY_COMPARE(activationCount, 1);
    QCOMPARE(activationMessage, customMessage);

    PinloomSingleInstanceGuard secondary({serverName, 100});
    const PinloomSingleInstanceStartResult secondaryResult = secondary.start();
    QVERIFY2(secondaryResult.isSecondary(), qPrintable(secondaryResult.error));
    QVERIFY(secondaryResult.activationSent);
    QTRY_COMPARE(activationCount, 2);
    QCOMPARE(activationMessage, QStringLiteral("activate"));
}

void WidgetSmokeTest::settingsDialogRoundTripsRuntimeSettings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString settingsPath = dir.filePath(QStringLiteral("pinloom.ini"));

    PinloomAppSettings saved = pinloomDefaultAppSettings(QStringLiteral("E:/PinloomData"));
    saved.sumatraPdfExecutablePath = QStringLiteral("C:/Tools/SumatraPDF.exe");
    saved.obsidianVaultPath = QStringLiteral("E:/Notes/EngineeringVault");
    saved.obsidianArchiveDirectory = QStringLiteral("Reference/Pinloom Clips");
    saved.clipMaxTemporaryClips = 42;
    saved.clipMaxTextBytes = 4096;
    saved.clipTemporaryTtlSeconds = 3600;
    saved.clipExcludeSensitiveText = false;
    saved.clipRestoreOriginalClipboardOnInsert = false;
    saved.clipExcludedSourceApps = {QStringLiteral("secret.exe"), QStringLiteral("password-manager.exe")};
    saved.clipSensitiveTextMarkers = {QStringLiteral("INTERNAL-ONLY")};

    {
        QSettings store(settingsPath, QSettings::IniFormat);
        savePinloomAppSettings(store, saved);
        store.sync();
    }

    QSettings store(settingsPath, QSettings::IniFormat);
    const PinloomAppSettings loaded = loadPinloomAppSettings(store, saved.dataDirectory);
    QCOMPARE(loaded.sumatraPdfExecutablePath, saved.sumatraPdfExecutablePath);
    QCOMPARE(loaded.obsidianVaultPath, saved.obsidianVaultPath);
    QCOMPARE(loaded.obsidianArchiveDirectory, saved.obsidianArchiveDirectory);
    QCOMPARE(loaded.dataDirectory, saved.dataDirectory);
    QCOMPARE(loaded.clipMaxTemporaryClips, 42);
    QCOMPARE(loaded.clipMaxTextBytes, 4096);
    QCOMPARE(loaded.clipTemporaryTtlSeconds, 3600);
    QVERIFY(!loaded.clipExcludeSensitiveText);
    QVERIFY(!loaded.clipRestoreOriginalClipboardOnInsert);
    QCOMPARE(loaded.clipExcludedSourceApps, saved.clipExcludedSourceApps);
    QCOMPARE(loaded.clipSensitiveTextMarkers, saved.clipSensitiveTextMarkers);

    const ClipCapturePolicy policy = loaded.clipCapturePolicy();
    QCOMPARE(policy.maxTemporaryClips, 42);
    QCOMPARE(policy.maxTextBytes, static_cast<qsizetype>(4096));
    QCOMPARE(policy.temporaryTtlSeconds, static_cast<qint64>(3600));
    QVERIFY(!policy.excludeSensitiveText);
    QCOMPARE(policy.excludedSourceApps, saved.clipExcludedSourceApps);

    PinloomSettingsDialog dialog(loaded);
    auto *pdfProxyPathEdit = dialog.findChild<QLineEdit *>(QStringLiteral("pdfProxyPathEdit"));
    auto *pdfProxyStatusLabel = dialog.findChild<QLabel *>(QStringLiteral("pdfProxyStatusLabel"));
    auto *sumatraPdfStatusLabel = dialog.findChild<QLabel *>(QStringLiteral("sumatraPdfStatusLabel"));
    auto *pdfPathEdit = dialog.findChild<QLineEdit *>(QStringLiteral("sumatraPdfPathEdit"));
    auto *obsidianVaultEdit = dialog.findChild<QLineEdit *>(QStringLiteral("obsidianVaultPathEdit"));
    auto *obsidianArchiveEdit = dialog.findChild<QLineEdit *>(QStringLiteral("obsidianArchiveDirectoryEdit"));
    auto *obsidianStatusLabel = dialog.findChild<QLabel *>(QStringLiteral("obsidianStatusLabel"));
    auto *dataDirEdit = dialog.findChild<QLineEdit *>(QStringLiteral("dataDirectoryEdit"));
    auto *historySpin = dialog.findChild<QSpinBox *>(QStringLiteral("clipMaxTemporaryClipsSpin"));
    auto *sizeSpin = dialog.findChild<QSpinBox *>(QStringLiteral("clipMaxTextBytesSpin"));
    auto *ttlSpin = dialog.findChild<QSpinBox *>(QStringLiteral("clipTemporaryTtlSecondsSpin"));
    auto *sensitiveCheck = dialog.findChild<QCheckBox *>(QStringLiteral("clipExcludeSensitiveTextCheck"));
    auto *restoreClipboardCheck = dialog.findChild<QCheckBox *>(QStringLiteral("clipRestoreOriginalClipboardCheck"));
    auto *blacklistEdit = dialog.findChild<QLineEdit *>(QStringLiteral("clipExcludedSourceAppsEdit"));
    auto *markersEdit = dialog.findChild<QLineEdit *>(QStringLiteral("clipSensitiveTextMarkersEdit"));
    QVERIFY(pdfProxyPathEdit);
    QVERIFY(pdfProxyStatusLabel);
    QVERIFY(sumatraPdfStatusLabel);
    QVERIFY(pdfPathEdit);
    QVERIFY(obsidianVaultEdit);
    QVERIFY(obsidianArchiveEdit);
    QVERIFY(obsidianStatusLabel);
    QVERIFY(dataDirEdit);
    QVERIFY(historySpin);
    QVERIFY(sizeSpin);
    QVERIFY(ttlSpin);
    QVERIFY(sensitiveCheck);
    QVERIFY(restoreClipboardCheck);
    QVERIFY(blacklistEdit);
    QVERIFY(markersEdit);
    QVERIFY(pdfProxyPathEdit->isReadOnly());
    QVERIFY(pdfProxyPathEdit->text().contains(QStringLiteral("pinloom_pdf_proxy"), Qt::CaseInsensitive));
    QVERIFY(!pdfProxyStatusLabel->text().trimmed().isEmpty());
    QVERIFY(!sumatraPdfStatusLabel->text().trimmed().isEmpty());
    QVERIFY(!obsidianStatusLabel->text().trimmed().isEmpty());

    pdfPathEdit->setText(QStringLiteral("D:/Portable/SumatraPDF.exe"));
    QVERIFY(sumatraPdfStatusLabel->text().contains(QStringLiteral("SumatraPDF.exe")));
    obsidianVaultEdit->setText(QStringLiteral("D:/Notes/Vault"));
    obsidianArchiveEdit->setText(QStringLiteral("Snippets/Pinloom"));
    historySpin->setValue(7);
    sizeSpin->setValue(2048);
    ttlSpin->setValue(120);
    sensitiveCheck->setChecked(true);
    restoreClipboardCheck->setChecked(true);
    blacklistEdit->setText(QStringLiteral(" secret.exe, secret.exe, cad.exe "));
    markersEdit->setText(QStringLiteral("TOKEN=, PRIVATE "));

    const PinloomAppSettings edited = dialog.settings();
    QCOMPARE(edited.sumatraPdfExecutablePath, QStringLiteral("D:/Portable/SumatraPDF.exe"));
    QCOMPARE(edited.obsidianVaultPath, QStringLiteral("D:/Notes/Vault"));
    QCOMPARE(edited.obsidianArchiveDirectory, QStringLiteral("Snippets/Pinloom"));
    QCOMPARE(edited.dataDirectory, saved.dataDirectory);
    QCOMPARE(edited.clipMaxTemporaryClips, 7);
    QCOMPARE(edited.clipMaxTextBytes, 2048);
    QCOMPARE(edited.clipTemporaryTtlSeconds, 120);
    QVERIFY(edited.clipExcludeSensitiveText);
    QVERIFY(edited.clipRestoreOriginalClipboardOnInsert);
    QCOMPARE(edited.clipExcludedSourceApps, (QStringList{QStringLiteral("secret.exe"), QStringLiteral("cad.exe")}));
    QCOMPARE(edited.clipSensitiveTextMarkers, (QStringList{QStringLiteral("TOKEN="), QStringLiteral("PRIVATE")}));
}

void WidgetSmokeTest::sumatraPdfRegionOverlayCapturesDdeRectangle()
{
    QList<SumatraPdfDdeMousePosition> positions;
    SumatraPdfDdeMousePosition start;
    start.page = 12;
    start.x = 420.0;
    start.y = 860.0;
    positions.append(start);
    SumatraPdfDdeMousePosition end;
    end.page = 12;
    end.x = 780.0;
    end.y = 920.0;
    positions.append(end);

    SumatraPdfRegionCaptureOverlay overlay(
        0,
        [&positions]() {
            if (positions.isEmpty()) {
                SumatraPdfDdeMousePosition missing;
                missing.error = QStringLiteral("missing test position");
                return missing;
            }
            return positions.takeFirst();
        });
    overlay.setGeometry(100, 100, 500, 300);
    overlay.show();
    QVERIFY(QTest::qWaitForWindowExposed(&overlay));

    QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, QPoint(80, 90));
    QTest::mouseMove(&overlay, QPoint(320, 210));
    QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, QPoint(320, 210));

    QCOMPARE(overlay.result(), static_cast<int>(QDialog::Accepted));
    const SumatraPdfRegionCaptureResult result = overlay.captureResult();
    QVERIFY2(result.success(), qPrintable(result.region.error));
    QCOMPARE(result.region.page, 12);
    QCOMPARE(result.region.rect.left, 420.0);
    QCOMPARE(result.region.rect.top, 860.0);
    QCOMPARE(result.region.rect.right, 780.0);
    QCOMPARE(result.region.rect.bottom, 920.0);
}

void WidgetSmokeTest::sumatraPdfRegionOverlayRejectsCrossPageAndCancels()
{
    QList<SumatraPdfDdeMousePosition> positions;
    SumatraPdfDdeMousePosition start;
    start.page = 12;
    start.x = 120.0;
    start.y = 160.0;
    positions.append(start);
    SumatraPdfDdeMousePosition end;
    end.page = 13;
    end.x = 220.0;
    end.y = 260.0;
    positions.append(end);

    SumatraPdfRegionCaptureOverlay overlay(
        0,
        [&positions]() {
            if (positions.isEmpty()) {
                SumatraPdfDdeMousePosition missing;
                missing.error = QStringLiteral("missing test position");
                return missing;
            }
            return positions.takeFirst();
        });
    overlay.setGeometry(100, 100, 500, 300);
    overlay.show();
    QVERIFY(QTest::qWaitForWindowExposed(&overlay));

    QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, QPoint(80, 90));
    QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, QPoint(320, 210));
    QVERIFY(overlay.isVisible());
    QVERIFY(!overlay.captureResult().success());

    QTest::keyClick(&overlay, Qt::Key_Escape);
    QCOMPARE(overlay.result(), static_cast<int>(QDialog::Rejected));
    QVERIFY(overlay.captureResult().canceled);
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



void WidgetSmokeTest::panelDefaultsToLauncherSurface()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("anchor-note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Anchor Note");
    resource.location = QStringLiteral("anchor.md");
    QVERIFY(repository.upsertResource(resource));

    PinloomPanel panel(repository);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    auto *openButton = panel.findChild<QPushButton *>(QStringLiteral("openButton"));
    auto *addAliasButton = panel.findChild<QPushButton *>(QStringLiteral("addAliasButton"));
    auto *addAnchorButton = panel.findChild<QPushButton *>(QStringLiteral("addAnchorButton"));
    auto *pinButton = panel.findChild<QPushButton *>(QStringLiteral("pinButton"));
    QVERIFY(searchEdit);
    QVERIFY(results);
    QVERIFY(openButton);
    QVERIFY(addAliasButton);
    QVERIFY(addAnchorButton);
    QVERIFY(pinButton);

    QVERIFY(!searchEdit->isHidden());
    QVERIFY(results->isHidden());
    QVERIFY(openButton->isHidden());
    QVERIFY(addAliasButton->isHidden());
    QVERIFY(addAnchorButton->isHidden());
    QVERIFY(pinButton->isHidden());
    QVERIFY(searchEdit->placeholderText().contains(QStringLiteral("anchors")));
    QVERIFY(searchEdit->placeholderText().contains(QStringLiteral("Saved Clips")));
    QCOMPARE(panel.focusWidget(), static_cast<QWidget *>(searchEdit));
    QVERIFY(panel.sizeHint().height() <= 120);

    panel.setSearchText(QStringLiteral("Anchor Note"));
    QVERIFY(!results->isHidden());
    QCOMPARE(results->count(), 1);

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
    anchor.name = QStringLiteral("Anchor control heading");
    anchor.locatorType = QStringLiteral("text.heading");
    anchor.locatorJson = QStringLiteral("{\"line\":12,\"type\":\"text.heading\"}");
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

void WidgetSmokeTest::panelTreatsCommandPrefixesAsPlainSearchText()
{
    InMemoryLibraryRepository repository;

    QStringList clipQueries;
    QList<ClipSearchOptions> clipOptions;
    int manualPdfRequestCount = 0;
    PinloomPanelOptions options;
    options.clipSearchHandler = [&](const QString &query, const ClipSearchOptions &searchOptions) {
        clipQueries.append(query);
        clipOptions.append(searchOptions);
        return QList<ClipSearchResult>{};
    };
    options.manualPdfAnchorRequestProvider = [&]() -> std::optional<ManualPdfAnchorCreationRequest> {
        ++manualPdfRequestCount;
        return std::nullopt;
    };

    PinloomPanel panel(repository, options);
    auto *searchEdit = panel.findChild<QLineEdit *>(QStringLiteral("searchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(searchEdit);
    QVERIFY(results);

    panel.setSearchText(QStringLiteral("c s"));

    QCOMPARE(clipQueries, QStringList{QStringLiteral("c s")});
    QCOMPARE(clipOptions.size(), 1);
    QVERIFY(clipOptions.first().includeSaved);
    QVERIFY(!clipOptions.first().includeTemporary);
    QCOMPARE(results->count(), 0);
    QCOMPARE(panel.statusText(), QStringLiteral("0 result(s)"));

    clipQueries.clear();
    clipOptions.clear();
    panel.setSearchText(QStringLiteral("k n"));

    QCOMPARE(clipQueries, QStringList{QStringLiteral("k n")});
    QCOMPARE(clipOptions.size(), 1);
    QVERIFY(clipOptions.first().includeSaved);
    QVERIFY(!clipOptions.first().includeTemporary);
    QCOMPARE(results->count(), 0);
    QCOMPARE(panel.statusText(), QStringLiteral("0 result(s)"));

    QTest::keyClick(searchEdit, Qt::Key_Return);
    QCOMPARE(manualPdfRequestCount, 0);
    QCOMPARE(panel.statusText(), QStringLiteral("No resource selected"));
}

void WidgetSmokeTest::commandPanelClipRootCommandShowsCandidates()
{
    int clipSearchCalls = 0;
    PinloomCommandPanelOptions options;
    options.clipSearchHandler = [&](const QString &query, const ClipSearchOptions &searchOptions) {
        Q_UNUSED(query);
        Q_UNUSED(searchOptions);
        ++clipSearchCalls;
        return QList<ClipSearchResult>{};
    };

    PinloomCommandPanel panel(options);
    auto *commandEdit = panel.findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("commandResultList"));
    QVERIFY(commandEdit);
    QVERIFY(results);

    panel.setCommandText(QStringLiteral("c"));

    QCOMPARE(clipSearchCalls, 0);
    QCOMPARE(results->count(), 2);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Command] Clip Search -> Open")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("c s <query>")));
    QVERIFY(results->item(1)->text().contains(QStringLiteral("[Command] New Saved Clip -> Open")));
    QVERIFY(results->item(1)->text().contains(QStringLiteral("c n")));
    QCOMPARE(panel.statusText(), QStringLiteral("Clip commands"));

    QTest::keyClick(commandEdit, Qt::Key_Return);
    QCOMPARE(panel.commandText(), QStringLiteral("c s"));

    panel.setCommandText(QStringLiteral("c"));
    QTest::keyClick(commandEdit, Qt::Key_Down);
    QCOMPARE(results->currentRow(), 1);
    QVERIFY(QMetaObject::invokeMethod(results,
                                      "itemActivated",
                                      Qt::DirectConnection,
                                      Q_ARG(QListWidgetItem *, results->currentItem())));
    QCOMPARE(panel.commandText(), QStringLiteral("c n"));
}

void WidgetSmokeTest::commandPanelClipSearchCommandSearchesHistoryAndSavedClipsAndEnterInserts()
{
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
    PinloomCommandPanelOptions options;
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

    PinloomCommandPanel panel(options);
    auto *commandEdit = panel.findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("commandResultList"));
    QVERIFY(commandEdit);
    QVERIFY(results);

    clipQueries.clear();
    clipOptions.clear();
    panel.setCommandText(QStringLiteral("c s"));

    QCOMPARE(clipQueries, (QStringList{QString(), QString()}));
    QCOMPARE(clipOptions.size(), 2);
    QVERIFY(!clipOptions.at(0).includeSaved);
    QVERIFY(clipOptions.at(0).includeTemporary);
    QVERIFY(clipOptions.at(1).includeSaved);
    QVERIFY(!clipOptions.at(1).includeTemporary);
    QCOMPARE(results->count(), 2);
    QCOMPARE(panel.resultAt(0).clipId, temporary.clip->id);
    QCOMPARE(panel.resultAt(1).clipId, savedId);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Clip] temporary command body -> Insert")));
    QVERIFY(results->item(1)->text().contains(QStringLiteral("[Clip] Clip Command Saved -> Insert")));
    QCOMPARE(results->currentRow(), 0);
    QCOMPARE(panel.statusText(), QStringLiteral("Clip search: 2 clip(s)"));

    panel.show();
    QVERIFY(panel.isVisible());
    commandEdit->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    QCOMPARE(QApplication::focusWidget(), commandEdit);

    QVERIFY(QMetaObject::invokeMethod(commandEdit, "returnPressed", Qt::DirectConnection));
    QCOMPARE(insertedClipIds, QStringList{temporary.clip->id});
    QCOMPARE(panel.statusText(), QStringLiteral("Inserted clip"));

    QTest::keyClick(commandEdit, Qt::Key_Down);
    QCOMPARE(results->currentRow(), 1);
    QTest::keyClick(commandEdit, Qt::Key_Up);
    QCOMPARE(results->currentRow(), 0);
    QTest::keyClick(commandEdit, Qt::Key_Down);
    QCOMPARE(results->currentRow(), 1);
    results->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();
    QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(results));
    QTest::keyClick(results, Qt::Key_Return);

    QCOMPARE(insertedClipIds, (QStringList{temporary.clip->id, savedId}));
    QCOMPARE(panel.statusText(), QStringLiteral("Inserted clip"));

    clipQueries.clear();
    clipOptions.clear();
    panel.setCommandText(QStringLiteral("c s saved command alias"));

    QCOMPARE(clipQueries, QStringList{QStringLiteral("saved command alias")});
    QCOMPARE(clipOptions.size(), 1);
    QVERIFY(clipOptions.first().includeSaved);
    QVERIFY(clipOptions.first().includeTemporary);
    QCOMPARE(results->count(), 1);
    QCOMPARE(panel.currentResult().clipId, savedId);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("aliases: saved command alias")));

    QVERIFY(QMetaObject::invokeMethod(results,
                                      "itemActivated",
                                      Qt::DirectConnection,
                                      Q_ARG(QListWidgetItem *, results->currentItem())));

    QCOMPARE(insertedClipIds, (QStringList{temporary.clip->id, savedId, savedId}));
    QCOMPARE(panel.statusText(), QStringLiteral("Inserted clip"));

    panel.setCommandText(QStringLiteral("c s zzzz-no-such-clip"));
    QCOMPARE(results->count(), 0);
    QCOMPARE(panel.statusText(), QStringLiteral("No clips to insert"));
    QTest::keyClick(commandEdit, Qt::Key_Return);
    QCOMPARE(insertedClipIds, (QStringList{temporary.clip->id, savedId, savedId}));
    QCOMPARE(panel.statusText(), QStringLiteral("No clips to insert"));
}

void WidgetSmokeTest::commandPanelClipNewCommandShowsTemporaryHistoryAndSaves()
{
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
    PinloomCommandPanelOptions options;
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

    PinloomCommandPanel panel(options);
    auto *commandEdit = panel.findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("commandResultList"));
    QVERIFY(commandEdit);
    QVERIFY(results);

    panel.setCommandText(QStringLiteral("c n"));

    QCOMPARE(results->count(), 1);
    QCOMPARE(panel.currentResult().clipId, temporary.clip->id);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[History] temporary save candidate body -> Save")));
    QCOMPARE(panel.statusText(), QStringLiteral("Clip save: 1 history item(s)"));

    QVERIFY(QMetaObject::invokeMethod(commandEdit, "returnPressed", Qt::DirectConnection));

    QCOMPARE(saveRequestCalls, 1);
    QVERIFY(saveRequestParentProvided);
    QCOMPARE(saveRequestClipIds, QStringList{temporary.clip->id});
    QCOMPARE(saveHandlerCalls, 1);
    QCOMPARE(panel.commandText(), QStringLiteral("c s Saved From Launcher"));
    QCOMPARE(panel.currentResult().clipId, temporary.clip->id);
    QCOMPARE(panel.statusText(), QStringLiteral("Saved clip \"Saved From Launcher\""));

    const std::optional<Clip> saved = clipRepository.findClip(temporary.clip->id);
    QVERIFY(saved.has_value());
    QVERIFY(saved->state == ClipState::Saved);
    QCOMPARE(saved->name, QStringLiteral("Saved From Launcher"));
    QCOMPARE(saved->aliases, QStringList{QStringLiteral("launcher save alias")});
    QCOMPARE(saved->tags, QStringList{QStringLiteral("launcher-save")});
    QVERIFY(saved->pinned);
    QCOMPARE(clipRepository.temporaryClips().size(), 0);

    panel.setCommandText(QStringLiteral("c n"));
    QCOMPARE(results->count(), 0);
}

void WidgetSmokeTest::commandPanelAnchorCaptureCommandCallsHandler()
{
    int captureCount = 0;
    QStringList statusNotifications;
    PinloomCommandPanelOptions options;
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };
    options.anchorCaptureHandler = [&](QString *status) {
        ++captureCount;
        if (status) {
            *status = QStringLiteral("Captured via test handler");
        }
        return true;
    };

    PinloomCommandPanel panel(options);
    auto *commandEdit = panel.findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("commandResultList"));
    QVERIFY(commandEdit);
    QVERIFY(results);

    panel.setCommandText(QStringLiteral("k"));

    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Command] New Anchor / Capture Anchor -> Open")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("k n")));
    QCOMPARE(panel.statusText(), QStringLiteral("Anchor commands"));

    QTest::keyClick(commandEdit, Qt::Key_Return);
    QCOMPARE(panel.commandText(), QStringLiteral("k n"));
    QCOMPARE(results->count(), 1);
    QCOMPARE(panel.statusText(), QStringLiteral("Capture anchor current app context pending"));

    QTest::keyClick(commandEdit, Qt::Key_Return);
    QCOMPARE(captureCount, 1);
    QCOMPARE(panel.commandText(), QStringLiteral("k n"));
    QCOMPARE(panel.statusText(), QStringLiteral("Captured via test handler"));
    QCOMPARE(statusNotifications.last(), panel.statusText());

    PinloomCommandPanelOptions noContextOptions;
    noContextOptions.anchorCaptureHandler = [](QString *status) {
        if (status) {
            *status = QStringLiteral("Open or select a PDF before capturing an anchor");
        }
        return false;
    };
    PinloomCommandPanel noContextPanel(noContextOptions);
    noContextPanel.setCommandText(QStringLiteral("k n"));

    QVERIFY(!noContextPanel.activateCurrentCommandItem());
    QCOMPARE(noContextPanel.statusText(), QStringLiteral("Open or select a PDF before capturing an anchor"));
}

void WidgetSmokeTest::commandPanelInboxRootCommandShowsCandidates()
{
    PinloomCommandPanel panel;
    auto *commandEdit = panel.findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("commandResultList"));
    QVERIFY(commandEdit);
    QVERIFY(results);

    panel.setCommandText(QStringLiteral("i"));

    QCOMPARE(results->count(), 2);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Command] New Inbox File -> Open")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("i n")));
    QVERIFY(results->item(1)->text().contains(QStringLiteral("[Command] Inbox Search -> Open")));
    QVERIFY(results->item(1)->text().contains(QStringLiteral("i s <query>")));
    QCOMPARE(panel.statusText(), QStringLiteral("Inbox commands"));

    QTest::keyClick(commandEdit, Qt::Key_Return);
    QCOMPARE(panel.commandText(), QStringLiteral("i n"));
    QCOMPARE(panel.statusText(), QStringLiteral("Inbox: drop a file or use Explorer selection"));

    panel.setCommandText(QStringLiteral("i"));
    QTest::keyClick(commandEdit, Qt::Key_Down);
    QVERIFY(QMetaObject::invokeMethod(results,
                                      "itemActivated",
                                      Qt::DirectConnection,
                                      Q_ARG(QListWidgetItem *, results->currentItem())));
    QCOMPARE(panel.commandText(), QStringLiteral("i s"));
}

void WidgetSmokeTest::commandPanelInboxNewCommandSavesPendingFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString filePath = dir.filePath(QStringLiteral("Board Spec.txt"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("board spec");
    file.close();

    InMemoryLibraryRepository repository;
    int requestProviderCalls = 0;
    bool requestParentProvided = false;
    QString requestedPath;
    InboxFileSaveRequest capturedRequest;
    QStringList savedResourceIds;

    PinloomCommandPanelOptions options;
    options.inboxSaveRequestProvider =
        [&](QWidget *parent, const QString &path) -> std::optional<InboxFileSaveRequest> {
        ++requestProviderCalls;
        requestParentProvided = parent != nullptr;
        requestedPath = path;
        InboxFileSaveRequest request;
        request.filePath = path;
        request.name = QStringLiteral("Board Spec Inbox");
        request.aliases = {QStringLiteral("board alias")};
        request.tags = {QStringLiteral("#hardware")};
        request.pinned = true;
        return request;
    };
    options.inboxSaveHandler = [&](const InboxFileSaveRequest &request, QString *status) {
        capturedRequest = request;
        const InboxFileSaveResult result = saveInboxFile(repository, request);
        if (status) {
            *status = result.status;
        }
        return result.success();
    };

    PinloomCommandPanel panel(options);
    QObject::connect(&panel, &PinloomCommandPanel::inboxSaved, [&](const QString &resourceId) {
        savedResourceIds.append(resourceId);
    });
    auto *commandEdit = panel.findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("commandResultList"));
    QVERIFY(commandEdit);
    QVERIFY(results);

    panel.setPendingInboxFiles({filePath});
    panel.setCommandText(QStringLiteral("i n"));

    QCOMPARE(panel.pendingInboxFiles(), QStringList{filePath});
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("Link mode")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("Board Spec.txt")));
    QCOMPARE(panel.statusText(), QStringLiteral("Inbox pending: Board Spec.txt"));

    QVERIFY(QMetaObject::invokeMethod(commandEdit, "returnPressed", Qt::DirectConnection));

    QCOMPARE(requestProviderCalls, 1);
    QVERIFY(requestParentProvided);
    QCOMPARE(requestedPath, filePath);
    QCOMPARE(capturedRequest.mode, InboxFileArchiveMode::Link);
    QCOMPARE(capturedRequest.name, QStringLiteral("Board Spec Inbox"));
    QCOMPARE(capturedRequest.aliases, QStringList{QStringLiteral("board alias")});
    QCOMPARE(capturedRequest.tags, QStringList{QStringLiteral("hardware")});
    QVERIFY(panel.pendingInboxFiles().isEmpty());
    QCOMPARE(panel.commandText(), QStringLiteral("i s Board Spec Inbox"));
    QCOMPARE(panel.statusText(), QStringLiteral("Saved Inbox file \"Board Spec Inbox\""));
    QCOMPARE(savedResourceIds, QStringList{inboxResourceIdForPath(filePath)});

    const QList<SearchResult> aliasResults = repository.search(SearchQuery{QStringLiteral("board alias")});
    QCOMPARE(aliasResults.size(), 1);
    QCOMPARE(aliasResults.first().resource.id, inboxResourceIdForPath(filePath));
    const std::optional<ResourceUsage> usage = repository.resourceUsage(inboxResourceIdForPath(filePath));
    QVERIFY(usage.has_value());
    QVERIFY(usage->pinned);
}

void WidgetSmokeTest::commandPanelInboxNewReportsMissingPendingAndExplorerSelection()
{
    int selectionCalls = 0;
    int saveCalls = 0;
    PinloomCommandPanelOptions options;
    options.inboxSelectionProvider = [&](QString *status) {
        ++selectionCalls;
        if (status) {
            *status = QStringLiteral("Explorer selection did not contain files");
        }
        return QStringList{};
    };
    options.inboxSaveHandler = [&](const InboxFileSaveRequest &, QString *) {
        ++saveCalls;
        return true;
    };

    PinloomCommandPanel panel(options);
    panel.setCommandText(QStringLiteral("i n"));

    QVERIFY(!panel.activateCurrentCommandItem());
    QCOMPARE(selectionCalls, 1);
    QCOMPARE(saveCalls, 0);
    QCOMPARE(panel.statusText(), QStringLiteral("Explorer selection did not contain files"));
}

void WidgetSmokeTest::commandPanelInboxSearchOpensSearchWindow()
{
    QStringList queries;
    PinloomCommandPanelOptions options;
    options.searchWindowHandler = [&](const QString &query) {
        queries.append(query);
    };

    PinloomCommandPanel panel(options);
    panel.setCommandText(QStringLiteral("i s clock alias"));

    QVERIFY(panel.activateCurrentCommandItem());
    QCOMPARE(queries, QStringList{QStringLiteral("clock alias")});
    QCOMPARE(panel.statusText(), QStringLiteral("Opened Pinloom search for \"clock alias\""));
}

void WidgetSmokeTest::commandPanelPlainQueryShowsUnifiedMixedResults()
{
    const QList<PinloomOpenTarget> targets = makeCommandPanelUnifiedTargets();
    QStringList unifiedQueries;
    int clipCommandSearchCalls = 0;
    PinloomCommandPanelOptions options;
    options.unifiedEntrySearchHandler = [&](const QString &query) {
        unifiedQueries.append(query);
        return commandPanelEntries(targets);
    };
    options.clipSearchHandler = [&](const QString &, const ClipSearchOptions &) {
        ++clipCommandSearchCalls;
        return QList<ClipSearchResult>{};
    };

    PinloomCommandPanel panel(options);
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("commandResultList"));
    QVERIFY(results);

    panel.setCommandText(QStringLiteral("launch"));

    QCOMPARE(unifiedQueries, QStringList{QStringLiteral("launch")});
    QCOMPARE(results->count(), 4);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Anchor] PLL jitter budget -> Jump")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("#clock")));
    QVERIFY(results->item(1)->text().contains(QStringLiteral("[Clip] Launch Clip -> Insert")));
    QVERIFY(results->item(2)->text().contains(QStringLiteral("[Inbox] Board Spec Inbox -> Open")));
    QVERIFY(results->item(3)->text().contains(QStringLiteral("[File] Schematic File -> Open")));
    QCOMPARE(panel.openTargetAt(0).resourceId, QStringLiteral("anchor-spec"));
    QVERIFY(panel.openTargetAt(0).anchor.has_value());
    QCOMPARE(panel.openTargetAt(1).clipId, QStringLiteral("clip-launch"));
    QCOMPARE(panel.resultAt(1).clipId, QStringLiteral("clip-launch"));
    QCOMPARE(panel.statusText(), QStringLiteral("Unified search: 4 result(s)"));

    panel.setCommandText(QStringLiteral("c"));

    QCOMPARE(unifiedQueries, QStringList{QStringLiteral("launch")});
    QCOMPARE(clipCommandSearchCalls, 0);
    QCOMPARE(results->count(), 2);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Command] Clip Search -> Open")));
    QCOMPARE(panel.statusText(), QStringLiteral("Clip commands"));
}

void WidgetSmokeTest::commandPanelPlainQueryEnterDispatchesByTargetType()
{
    const QList<PinloomOpenTarget> targets = makeCommandPanelUnifiedTargets();
    QStringList insertedClipIds;
    QList<PinloomOpenTarget> jumpedTargets;
    QList<PinloomOpenTarget> openedTargets;

    PinloomCommandPanelOptions options;
    options.unifiedEntrySearchHandler = [&](const QString &) {
        return commandPanelEntries(targets);
    };
    options.clipInsertionHandler = [&](const QString &clipId, QString *error) {
        if (error) {
            error->clear();
        }
        insertedClipIds.append(clipId);
        return true;
    };
    options.anchorJumpHandler = [&](const PinloomOpenTarget &target, QString *status) {
        jumpedTargets.append(target);
        if (status) {
            *status = QStringLiteral("Jumped anchor from command");
        }
        return true;
    };
    options.resourceOpenHandler = [&](const PinloomOpenTarget &target, QString *status) {
        openedTargets.append(target);
        if (target.resourceId == QStringLiteral("file-schematic")) {
            if (status) {
                *status = QStringLiteral("Unable to open schematic file");
            }
            return false;
        }
        if (status) {
            *status = isInboxResourceId(target.resourceId)
                ? QStringLiteral("Opened Inbox file from command")
                : QStringLiteral("Opened file from command");
        }
        return true;
    };

    PinloomCommandPanel panel(options);
    panel.setCommandText(QStringLiteral("launch"));

    QVERIFY(panel.selectResultAt(1));
    QVERIFY(panel.activateCurrentCommandItem());
    QCOMPARE(insertedClipIds, QStringList{QStringLiteral("clip-launch")});
    QCOMPARE(jumpedTargets.size(), 0);
    QCOMPARE(openedTargets.size(), 0);
    QCOMPARE(panel.statusText(), QStringLiteral("Inserted clip"));

    QVERIFY(panel.selectResultAt(0));
    QVERIFY(panel.activateCurrentCommandItem());
    QCOMPARE(jumpedTargets.size(), 1);
    QCOMPARE(jumpedTargets.first().resourceId, QStringLiteral("anchor-spec"));
    QVERIFY(jumpedTargets.first().anchor.has_value());
    QCOMPARE(panel.statusText(), QStringLiteral("Jumped anchor from command"));

    QVERIFY(panel.selectResultAt(2));
    QVERIFY(panel.activateCurrentCommandItem());
    QCOMPARE(openedTargets.size(), 1);
    QVERIFY(isInboxResourceId(openedTargets.first().resourceId));
    QCOMPARE(panel.statusText(), QStringLiteral("Opened Inbox file from command"));

    QVERIFY(panel.selectResultAt(3));
    QVERIFY(!panel.activateCurrentCommandItem());
    QCOMPARE(openedTargets.size(), 2);
    QCOMPARE(openedTargets.last().resourceId, QStringLiteral("file-schematic"));
    QCOMPARE(panel.statusText(), QStringLiteral("Unable to open schematic file"));
}

void WidgetSmokeTest::commandPanelRightArrowShowsActionsForUnifiedResultTypes()
{
    const QList<PinloomOpenTarget> targets = makeCommandPanelUnifiedTargets();
    PinloomCommandPanelOptions options;
    options.unifiedEntrySearchHandler = [&](const QString &) {
        return commandPanelEntries(targets);
    };
    options.unifiedEntryActionProvider = [](const PinloomEntry &entry) {
        return makeCommandPanelActionsForTarget(openTargetFromEntry(entry));
    };

    PinloomCommandPanel panel(options);
    auto *commandEdit = panel.findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("commandResultList"));
    QVERIFY(commandEdit);
    QVERIFY(results);

    panel.setCommandText(QStringLiteral("launch"));
    QCOMPARE(results->count(), 4);

    const QStringList primaryLabels = {
        QStringLiteral("Jump"),
        QStringLiteral("Insert"),
        QStringLiteral("Open"),
        QStringLiteral("Open")
    };
    for (int row = 0; row < targets.size(); ++row) {
        QVERIFY(panel.selectResultAt(row));
        QTest::keyClick(commandEdit, Qt::Key_Right);
        QVERIFY(panel.isShowingResultActions());
        QCOMPARE(results->count(), 4);
        QVERIFY(results->item(0)->text().contains(QStringLiteral("[Action] %1").arg(primaryLabels.at(row))));
        QVERIFY(results->item(1)->text().contains(QStringLiteral("Add alias")));
        QVERIFY(results->item(2)->text().contains(QStringLiteral("Add tag")));
        QVERIFY(results->item(3)->text().contains(QStringLiteral("Delete / Remove (disabled)")));
        QCOMPARE(panel.currentOpenTarget().clipId, targets.at(row).clipId);
        QCOMPARE(panel.currentOpenTarget().resourceId, targets.at(row).resourceId);

        QTest::keyClick(commandEdit, Qt::Key_Left);
        QVERIFY(!panel.isShowingResultActions());
        QCOMPARE(results->count(), 4);
        QCOMPARE(panel.currentOpenTarget().clipId, targets.at(row).clipId);
        QCOMPARE(panel.currentOpenTarget().resourceId, targets.at(row).resourceId);
    }
}

void WidgetSmokeTest::commandPanelActionListExecutesSelectedProviderAction()
{
    const QList<PinloomOpenTarget> targets = makeCommandPanelUnifiedTargets();
    QList<PinloomOpenTarget> actionTargets;
    QStringList actionIds;
    PinloomCommandPanelOptions options;
    options.unifiedEntrySearchHandler = [&](const QString &) {
        return commandPanelEntries(targets);
    };
    options.unifiedEntryActionProvider = [](const PinloomEntry &entry) {
        return makeCommandPanelActionsForTarget(openTargetFromEntry(entry));
    };
    options.unifiedEntryCommandHandler =
        [&](QWidget *, const PinloomEntry &entry, const PinloomCommandResultAction &action) {
        const PinloomOpenTarget target = openTargetFromEntry(entry);
        actionTargets.append(target);
        actionIds.append(action.id);
        PinloomCommandActionResult result;
        if (action.id == QLatin1String("add_tag")) {
            result.message = QStringLiteral("Fake provider refused tag");
            return result;
        }
        result.success = true;
        result.message = QStringLiteral("Ran %1").arg(action.id);
        return result;
    };

    PinloomCommandPanel panel(options);
    auto *commandEdit = panel.findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"));
    QVERIFY(commandEdit);

    panel.setCommandText(QStringLiteral("launch"));
    QVERIFY(panel.selectResultAt(0));
    QTest::keyClick(commandEdit, Qt::Key_Right);
    QVERIFY(panel.isShowingResultActions());

    QTest::keyClick(commandEdit, Qt::Key_Down);
    QVERIFY(panel.activateCurrentCommandItem());
    QCOMPARE(actionIds, QStringList{QStringLiteral("add_alias")});
    QCOMPARE(actionTargets.first().resourceId, QStringLiteral("anchor-spec"));
    QCOMPARE(panel.statusText(), QStringLiteral("Ran add_alias"));

    QTest::keyClick(commandEdit, Qt::Key_Down);
    QVERIFY(!panel.activateCurrentCommandItem());
    const QStringList expectedActions = {QStringLiteral("add_alias"), QStringLiteral("add_tag")};
    QCOMPARE(actionIds, expectedActions);
    QCOMPARE(panel.statusText(), QStringLiteral("Fake provider refused tag"));
}

void WidgetSmokeTest::commandPanelActionListExecutesRemoveWithInjectedConfirmation()
{
    const QList<PinloomOpenTarget> targets = makeCommandPanelUnifiedTargets();
    bool confirmRemove = false;
    QStringList removedResourceIds;
    QStringList actionIds;

    PinloomCommandPanelOptions options;
    options.unifiedEntrySearchHandler = [&](const QString &) {
        return commandPanelEntries(targets);
    };
    options.unifiedEntryActionProvider = [](const PinloomEntry &entry) {
        const PinloomOpenTarget target = openTargetFromEntry(entry);
        QList<PinloomCommandResultAction> actions = makeCommandPanelActionsForTarget(target);
        actions.last().enabled = true;
        actions.last().disabledReason.clear();
        return actions;
    };
    options.unifiedEntryCommandHandler =
        [&](QWidget *, const PinloomEntry &entry, const PinloomCommandResultAction &action) {
        const PinloomOpenTarget target = openTargetFromEntry(entry);
        actionIds.append(action.id);
        PinloomCommandActionResult result;
        if (action.id != QLatin1String("remove")) {
            result.success = true;
            return result;
        }
        if (!confirmRemove) {
            result.message = QStringLiteral("Remove canceled by fake confirmation");
            return result;
        }
        removedResourceIds.append(target.resourceId);
        result.success = true;
        result.message = QStringLiteral("Removed by fake handler");
        return result;
    };

    PinloomCommandPanel panel(options);
    auto *commandEdit = panel.findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"));
    QVERIFY(commandEdit);

    panel.setCommandText(QStringLiteral("launch"));
    QVERIFY(panel.selectResultAt(0));
    QTest::keyClick(commandEdit, Qt::Key_Right);
    QVERIFY(panel.isShowingResultActions());
    QCOMPARE(panel.resultCount(), 4);
    QVERIFY(panel.selectResultAt(3));

    QVERIFY(!panel.activateCurrentCommandItem());
    QCOMPARE(actionIds, QStringList{QStringLiteral("remove")});
    QVERIFY(removedResourceIds.isEmpty());
    QCOMPARE(panel.statusText(), QStringLiteral("Remove canceled by fake confirmation"));

    confirmRemove = true;
    QVERIFY(panel.activateCurrentCommandItem());
    QCOMPARE(actionIds, (QStringList{QStringLiteral("remove"), QStringLiteral("remove")}));
    QCOMPARE(removedResourceIds, QStringList{QStringLiteral("anchor-spec")});
    QCOMPARE(panel.statusText(), QStringLiteral("Removed by fake handler"));
}

void WidgetSmokeTest::commandPanelRestoreCommandUsesDeletedEntrySearchHandler()
{
    PinloomEntry deletedClip;
    deletedClip.id = QStringLiteral("clip:deleted");
    deletedClip.type = PinloomEntryType::SavedClip;
    deletedClip.name = QStringLiteral("Deleted Clip");
    deletedClip.clipId = QStringLiteral("deleted-clip");
    deletedClip.location = QStringLiteral("deleted body");
    deletedClip.deleted = true;
    deletedClip.matchedField = QStringLiteral("name");

    QString capturedQuery;
    QStringList actionIds;
    PinloomCommandPanelOptions options;
    options.deletedEntrySearchHandler = [&](const QString &query) {
        capturedQuery = query;
        return QList<PinloomEntry>{deletedClip};
    };
    options.unifiedEntryActionProvider = [](const PinloomEntry &entry) {
        return defaultActionsForPinloomEntry(entry);
    };
    options.unifiedEntryCommandHandler =
        [&](QWidget *, const PinloomEntry &entry, const PinloomCommandResultAction &action) {
        actionIds.append(action.id);
        PinloomCommandActionResult result;
        result.success = action.id == QLatin1String("restore") && entry.deleted;
        result.message = result.success
            ? QStringLiteral("Restored %1").arg(entry.name)
            : QStringLiteral("Unexpected action");
        return result;
    };

    PinloomCommandPanel panel(options);
    auto *commandEdit = panel.findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("commandResultList"));
    QVERIFY(commandEdit);
    QVERIFY(results);

    panel.setCommandText(QStringLiteral("restore Deleted"));
    QCOMPARE(capturedQuery, QStringLiteral("Deleted"));
    QCOMPARE(panel.resultCount(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("[Deleted Clip]")));

    QVERIFY(!panel.activateCurrentCommandItem());
    QCOMPARE(panel.statusText(), QStringLiteral("Entry is deleted; press Right Arrow and choose Restore"));

    QTest::keyClick(commandEdit, Qt::Key_Right);
    QVERIFY(panel.isShowingResultActions());
    QCOMPARE(panel.resultCount(), 7);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("Insert (disabled)")));
    QVERIFY(results->item(5)->text().contains(QStringLiteral("Restore")));
    QVERIFY(panel.selectResultAt(5));
    QVERIFY(panel.activateCurrentCommandItem());
    QCOMPARE(actionIds, QStringList{QStringLiteral("restore")});
    QCOMPARE(panel.statusText(), QStringLiteral("Restored Deleted Clip"));
}

void WidgetSmokeTest::commandPanelStructuredEntryActionResultReportsDiagnostics()
{
    PinloomEntry file;
    file.id = QStringLiteral("resource:file");
    file.type = PinloomEntryType::FileResource;
    file.name = QStringLiteral("Diagnostic File");
    file.resourceId = QStringLiteral("file-resource");
    file.resourceKind = ResourceKind::File;
    file.location = QStringLiteral("E:/docs/diagnostic.txt");
    file.matchedField = QStringLiteral("title");

    PinloomCommandPanelOptions options;
    options.unifiedEntrySearchHandler = [&](const QString &) {
        return QList<PinloomEntry>{file};
    };
    options.unifiedEntryActionProvider = [](const PinloomEntry &entry) {
        return defaultActionsForPinloomEntry(entry);
    };
    options.unifiedEntryCommandHandler =
        [](QWidget *, const PinloomEntry &, const PinloomCommandResultAction &) {
        PinloomCommandActionResult result;
        result.success = false;
        result.message = QStringLiteral("Rename failed");
        result.diagnostics = QStringLiteral("repository write lock");
        result.nextUiHint = QStringLiteral("keep-actions-open");
        return result;
    };

    PinloomCommandPanel panel(options);
    auto *commandEdit = panel.findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"));
    QVERIFY(commandEdit);

    panel.setCommandText(QStringLiteral("diagnostic"));
    QTest::keyClick(commandEdit, Qt::Key_Right);
    QVERIFY(panel.isShowingResultActions());
    QVERIFY(panel.selectResultAt(1));
    QVERIFY(!panel.activateCurrentCommandItem());
    QVERIFY(panel.statusText().contains(QStringLiteral("Rename failed")));
    QVERIFY(panel.statusText().contains(QStringLiteral("Diagnostics: repository write lock")));
    QVERIFY(panel.statusText().contains(QStringLiteral("Next: keep-actions-open")));
}

void WidgetSmokeTest::commandPanelRightArrowPrimaryUsesStructuredEntryCommandHandler()
{
    PinloomEntry file;
    file.id = QStringLiteral("resource:primary");
    file.type = PinloomEntryType::FileResource;
    file.name = QStringLiteral("Primary File");
    file.resourceId = QStringLiteral("primary-resource");
    file.resourceKind = ResourceKind::File;
    file.location = QStringLiteral("E:/docs/primary.txt");
    file.matchedField = QStringLiteral("title");

    int structuredCalls = 0;
    int openCalls = 0;
    QStringList actionIds;
    PinloomCommandPanelOptions options;
    options.unifiedEntrySearchHandler = [&](const QString &) {
        return QList<PinloomEntry>{file};
    };
    options.unifiedEntryActionProvider = [](const PinloomEntry &entry) {
        return defaultActionsForPinloomEntry(entry);
    };
    options.resourceOpenHandler = [&](const PinloomOpenTarget &, QString *status) {
        ++openCalls;
        if (status) {
            *status = QStringLiteral("legacy open path");
        }
        return false;
    };
    options.unifiedEntryCommandHandler =
        [&](QWidget *, const PinloomEntry &entry, const PinloomCommandResultAction &action) {
        ++structuredCalls;
        actionIds.append(action.id);
        PinloomCommandActionResult result;
        result.success = false;
        result.message = QStringLiteral("Structured primary failed for %1").arg(entry.name);
        result.diagnostics = QStringLiteral("structured diagnostics");
        result.nextUiHint = QStringLiteral("keep-actions-open");
        return result;
    };

    PinloomCommandPanel panel(options);
    auto *commandEdit = panel.findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"));
    QVERIFY(commandEdit);

    panel.setCommandText(QStringLiteral("primary"));
    QTest::keyClick(commandEdit, Qt::Key_Right);
    QVERIFY(panel.isShowingResultActions());
    QVERIFY(panel.selectResultAt(0));
    QVERIFY(!panel.activateCurrentCommandItem());

    QCOMPARE(structuredCalls, 1);
    QCOMPARE(openCalls, 0);
    QCOMPARE(actionIds, QStringList{QStringLiteral("primary")});
    QVERIFY(panel.statusText().contains(QStringLiteral("Structured primary failed for Primary File")));
    QVERIFY(panel.statusText().contains(QStringLiteral("Diagnostics: structured diagnostics")));
    QVERIFY(panel.statusText().contains(QStringLiteral("Next: keep-actions-open")));
}

void WidgetSmokeTest::commandPanelActionListReturnsWithEscapeOrLeft()
{
    const QList<PinloomOpenTarget> targets = makeCommandPanelUnifiedTargets();
    PinloomCommandPanelOptions options;
    options.unifiedEntrySearchHandler = [&](const QString &) {
        return commandPanelEntries(targets);
    };
    options.unifiedEntryActionProvider = [](const PinloomEntry &entry) {
        return makeCommandPanelActionsForTarget(openTargetFromEntry(entry));
    };

    PinloomCommandPanel panel(options);
    auto *commandEdit = panel.findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("commandResultList"));
    QVERIFY(commandEdit);
    QVERIFY(results);

    panel.setCommandText(QStringLiteral("launch"));
    QVERIFY(panel.selectResultAt(2));
    QTest::keyClick(commandEdit, Qt::Key_Right);
    QVERIFY(panel.isShowingResultActions());
    QTest::keyClick(commandEdit, Qt::Key_Escape);
    QVERIFY(!panel.isShowingResultActions());
    QCOMPARE(results->count(), 4);
    QCOMPARE(panel.currentOpenTarget().resourceId, targets.at(2).resourceId);

    QVERIFY(panel.selectResultAt(3));
    QTest::keyClick(commandEdit, Qt::Key_Right);
    QVERIFY(panel.isShowingResultActions());
    QTest::keyClick(commandEdit, Qt::Key_Left);
    QVERIFY(!panel.isShowingResultActions());
    QCOMPARE(results->count(), 4);
    QCOMPARE(panel.currentOpenTarget().resourceId, targets.at(3).resourceId);
}

void WidgetSmokeTest::commandPanelDoesNotDependOnAltCtrlDeleteActionShortcuts()
{
    const QList<PinloomOpenTarget> targets = makeCommandPanelUnifiedTargets();
    int actionCalls = 0;
    PinloomCommandPanelOptions options;
    options.unifiedEntrySearchHandler = [&](const QString &) {
        return commandPanelEntries(targets);
    };
    options.unifiedEntryActionProvider = [](const PinloomEntry &entry) {
        return makeCommandPanelActionsForTarget(openTargetFromEntry(entry));
    };
    options.unifiedEntryCommandHandler =
        [&](QWidget *, const PinloomEntry &, const PinloomCommandResultAction &) {
        ++actionCalls;
        PinloomCommandActionResult result;
        result.success = true;
        return result;
    };

    PinloomCommandPanel panel(options);
    auto *commandEdit = panel.findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("commandResultList"));
    QVERIFY(commandEdit);
    QVERIFY(results);

    panel.setCommandText(QStringLiteral("launch"));
    QCOMPARE(results->count(), 4);

    QTest::keyClick(commandEdit, Qt::Key_A, Qt::AltModifier);
    QVERIFY(!panel.isShowingResultActions());
    QCOMPARE(actionCalls, 0);

    QTest::keyClick(commandEdit, Qt::Key_E, Qt::ControlModifier);
    QVERIFY(!panel.isShowingResultActions());
    QCOMPARE(actionCalls, 0);

    commandEdit->setCursorPosition(commandEdit->text().size());
    QTest::keyClick(commandEdit, Qt::Key_Delete);
    QVERIFY(!panel.isShowingResultActions());
    QCOMPARE(actionCalls, 0);
}

void WidgetSmokeTest::pinloomEntriesSortMixedResultsByMatchBucketAndSignals()
{
    const QDateTime recent = QDateTime::fromString(QStringLiteral("2026-01-02T00:00:00Z"), Qt::ISODate);
    const QDateTime older = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);

    PinloomEntry metadata;
    metadata.id = QStringLiteral("metadata");
    metadata.type = PinloomEntryType::FileResource;
    metadata.name = QStringLiteral("Metadata path");
    metadata.matchedField = QStringLiteral("path");

    PinloomEntry tag;
    tag.id = QStringLiteral("tag");
    tag.type = PinloomEntryType::Inbox;
    tag.name = QStringLiteral("Tagged inbox");
    tag.matchedField = QStringLiteral("tag");

    PinloomEntry aliasCold;
    aliasCold.id = QStringLiteral("alias-cold");
    aliasCold.type = PinloomEntryType::SavedClip;
    aliasCold.name = QStringLiteral("Alias cold");
    aliasCold.matchedField = QStringLiteral("alias");
    aliasCold.usedAt = older;

    PinloomEntry aliasPinned;
    aliasPinned.id = QStringLiteral("alias-pinned");
    aliasPinned.type = PinloomEntryType::Anchor;
    aliasPinned.name = QStringLiteral("Alias pinned");
    aliasPinned.matchedField = QStringLiteral("anchor_alias");
    aliasPinned.pinned = true;
    aliasPinned.usedAt = recent;

    PinloomEntry exact;
    exact.id = QStringLiteral("exact");
    exact.type = PinloomEntryType::Anchor;
    exact.name = QStringLiteral("Exact anchor");
    exact.matchedField = QStringLiteral("anchor_name");

    const QList<PinloomEntry> sorted = sortedPinloomEntries({metadata, tag, aliasCold, aliasPinned, exact});
    QCOMPARE(sorted.at(0).id, QStringLiteral("exact"));
    QCOMPARE(sorted.at(1).id, QStringLiteral("alias-pinned"));
    QCOMPARE(sorted.at(2).id, QStringLiteral("alias-cold"));
    QCOMPARE(sorted.at(3).id, QStringLiteral("tag"));
    QCOMPARE(sorted.at(4).id, QStringLiteral("metadata"));
}

void WidgetSmokeTest::commandPanelPlainQueryUsesUnifiedEntrySearchHandler()
{
    PinloomEntry file;
    file.id = QStringLiteral("file-entry");
    file.type = PinloomEntryType::FileResource;
    file.name = QStringLiteral("Launch Path");
    file.resourceId = QStringLiteral("file-resource");
    file.resourceKind = ResourceKind::File;
    file.location = QStringLiteral("E:/docs/launch.txt");
    file.matchedField = QStringLiteral("path");

    PinloomEntry clip;
    clip.id = QStringLiteral("clip-entry");
    clip.type = PinloomEntryType::SavedClip;
    clip.name = QStringLiteral("Launch Clip");
    clip.clipId = QStringLiteral("clip-1");
    clip.location = QStringLiteral("clip preview");
    clip.matchedField = QStringLiteral("alias");

    PinloomEntry anchor;
    anchor.id = QStringLiteral("anchor-entry");
    anchor.type = PinloomEntryType::Anchor;
    anchor.name = QStringLiteral("Launch Anchor");
    anchor.resourceId = QStringLiteral("anchor-resource");
    anchor.resourceKind = ResourceKind::Pdf;
    anchor.location = QStringLiteral("E:/docs/launch.pdf");
    anchor.matchedField = QStringLiteral("anchor_name");
    Anchor storedAnchor;
    storedAnchor.id = QStringLiteral("anchor-1");
    storedAnchor.name = anchor.name;
    storedAnchor.targetFile = anchor.location;
    anchor.anchor = storedAnchor;

    PinloomCommandPanelOptions options;
    int entrySearchCalls = 0;
    options.unifiedEntrySearchHandler = [&](const QString &) {
        ++entrySearchCalls;
        return QList<PinloomEntry>{file, clip, anchor};
    };

    PinloomCommandPanel panel(options);
    panel.setCommandText(QStringLiteral("launch"));

    QCOMPARE(entrySearchCalls, 1);
    QCOMPARE(panel.resultCount(), 3);
    QVERIFY(panel.selectResultAt(0));
    QCOMPARE(panel.currentOpenTarget().resourceId, QStringLiteral("anchor-resource"));
    QVERIFY(panel.openTargetAt(0).anchor.has_value());
    QCOMPARE(panel.openTargetAt(1).clipId, QStringLiteral("clip-1"));
    QCOMPARE(panel.openTargetAt(2).resourceId, QStringLiteral("file-resource"));
}

void WidgetSmokeTest::entryActionProviderBuildsActionsForUnifiedTypes()
{
    PinloomEntry anchor;
    anchor.type = PinloomEntryType::Anchor;
    anchor.name = QStringLiteral("Anchor");
    anchor.resourceId = QStringLiteral("anchor-resource");
    anchor.anchor = Anchor{};

    PinloomEntry clip;
    clip.type = PinloomEntryType::SavedClip;
    clip.name = QStringLiteral("Clip");
    clip.clipId = QStringLiteral("clip-id");

    PinloomEntry inbox;
    inbox.type = PinloomEntryType::Inbox;
    inbox.name = QStringLiteral("Inbox");
    inbox.resourceId = QStringLiteral("inbox:file");

    PinloomEntry file;
    file.type = PinloomEntryType::FileResource;
    file.name = QStringLiteral("File");
    file.resourceId = QStringLiteral("file-resource");

    QCOMPARE(defaultActionsForPinloomEntry(anchor).first().label, QStringLiteral("Jump"));
    QCOMPARE(defaultActionsForPinloomEntry(clip).first().label, QStringLiteral("Insert"));
    QCOMPARE(defaultActionsForPinloomEntry(inbox).first().label, QStringLiteral("Open"));
    QCOMPARE(defaultActionsForPinloomEntry(file).first().label, QStringLiteral("Open"));
    QCOMPARE(defaultActionsForPinloomEntry(anchor).last().id, QStringLiteral("remove"));
    QVERIFY(defaultActionsForPinloomEntry(anchor).last().enabled);

    clip.metadata.insert(QStringLiteral("clipSourceApp"), QStringLiteral("Obsidian"));
    const QList<PinloomCommandResultAction> obsidianClipActions = defaultActionsForPinloomEntry(clip);
    QVERIFY(std::any_of(obsidianClipActions.cbegin(), obsidianClipActions.cend(), [](const auto &action) {
        return action.id == QLatin1String("open_source") && action.enabled;
    }));

    Clip deletedClip;
    deletedClip.id = QStringLiteral("clip-deleted");
    deletedClip.state = ClipState::Deleted;
    deletedClip.name = QStringLiteral("Deleted Clip");
    deletedClip.aliases = {QStringLiteral("snippet")};
    deletedClip.tags = {QStringLiteral("daily")};
    deletedClip.pinned = true;
    deletedClip.preview = QStringLiteral("deleted preview");
    PinloomEntry rawClip;
    rawClip.type = PinloomEntryType::SavedClip;
    rawClip.clipId = deletedClip.id;
    const PinloomEntry enrichedClip = enrichedPinloomEntryForAction(rawClip, deletedClip);
    QCOMPARE(enrichedClip.name, QStringLiteral("Deleted Clip"));
    QCOMPARE(enrichedClip.aliases, QStringList{QStringLiteral("snippet")});
    QVERIFY(enrichedClip.deleted);
    QVERIFY(defaultActionsForPinloomEntry(enrichedClip).first().disabledReason.contains(QStringLiteral("Restore")));
    QCOMPARE(defaultActionsForPinloomEntry(enrichedClip).at(5).id, QStringLiteral("restore"));

    Resource indexedResource;
    indexedResource.id = QStringLiteral("inbox:file:file-1");
    indexedResource.kind = ResourceKind::File;
    indexedResource.title = QStringLiteral("Inbox File");
    indexedResource.location = QStringLiteral("E:/docs/inbox.txt");
    indexedResource.aliases = {QStringLiteral("drop")};
    indexedResource.tags = {QStringLiteral("inbox")};
    ResourceUsage usage;
    usage.resourceId = indexedResource.id;
    usage.pinned = true;
    PinloomEntry rawResource;
    rawResource.resourceId = indexedResource.id;
    const PinloomEntry enrichedResource =
        enrichedPinloomEntryForAction(rawResource, std::nullopt, indexedResource, usage);
    QCOMPARE(static_cast<int>(enrichedResource.type),
             static_cast<int>(PinloomEntryType::Inbox));
    QCOMPARE(enrichedResource.name, QStringLiteral("Inbox File"));
    QCOMPARE(enrichedResource.aliases, QStringList{QStringLiteral("drop")});
    QVERIFY(enrichedResource.pinned);
    QCOMPARE(defaultActionsForPinloomEntry(enrichedResource).at(4).id, QStringLiteral("unpin"));

    QList<PinloomEntry> actionEntries;
    QStringList actionIds;
    PinloomCommandPanelOptions options;
    options.unifiedEntrySearchHandler = [&](const QString &) {
        return QList<PinloomEntry>{anchor};
    };
    options.unifiedEntryActionProvider = [](const PinloomEntry &entry) {
        return defaultActionsForPinloomEntry(entry);
    };
    options.unifiedEntryCommandHandler =
        [&](QWidget *, const PinloomEntry &entry, const PinloomCommandResultAction &action) {
        actionEntries.append(entry);
        actionIds.append(action.id);
        PinloomCommandActionResult result;
        result.success = true;
        result.message = QStringLiteral("Entry action %1").arg(action.id);
        return result;
    };

    PinloomCommandPanel panel(options);
    auto *commandEdit = panel.findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"));
    QVERIFY(commandEdit);
    panel.setCommandText(QStringLiteral("anchor"));
    QTest::keyClick(commandEdit, Qt::Key_Right);
    QVERIFY(panel.isShowingResultActions());
    QVERIFY(panel.selectResultAt(5));
    QVERIFY(panel.activateCurrentCommandItem());
    QCOMPARE(actionIds, QStringList{QStringLiteral("remove")});
    QCOMPARE(static_cast<int>(actionEntries.first().type),
             static_cast<int>(PinloomEntryType::Anchor));
    QCOMPARE(panel.statusText(), QStringLiteral("Entry action remove"));
}

void WidgetSmokeTest::commandPanelPlainQueryUsesUnifiedRankingOrder()
{
    InMemoryLibraryRepository repository;

    Resource exact;
    exact.id = QStringLiteral("exact-title");
    exact.kind = ResourceKind::File;
    exact.title = QStringLiteral("Launch");
    exact.location = QStringLiteral("E:/docs/exact.txt");
    QVERIFY(repository.upsertResource(exact));

    Resource alias;
    alias.id = QStringLiteral("alias-result");
    alias.kind = ResourceKind::File;
    alias.title = QStringLiteral("Alias Result");
    alias.location = QStringLiteral("E:/docs/alias.txt");
    alias.aliases = {QStringLiteral("Launch")};
    QVERIFY(repository.upsertResource(alias));

    Resource tagged;
    tagged.id = QStringLiteral("tag-result");
    tagged.kind = ResourceKind::File;
    tagged.title = QStringLiteral("Tag Result");
    tagged.location = QStringLiteral("E:/docs/tag.txt");
    tagged.tags = {QStringLiteral("Launch")};
    QVERIFY(repository.upsertResource(tagged));
    QVERIFY(repository.setResourcePinned(tagged.id, true));
    QVERIFY(repository.recordResourceOpen(tagged.id));

    Resource pathOnly;
    pathOnly.id = QStringLiteral("path-result");
    pathOnly.kind = ResourceKind::File;
    pathOnly.title = QStringLiteral("Path Result");
    pathOnly.location = QStringLiteral("E:/docs/launch/path.txt");
    QVERIFY(repository.upsertResource(pathOnly));

    InMemoryClipRepository clipRepository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    const QString clipId = saveWidgetClip(clipRepository,
                                          QStringLiteral("body text"),
                                          QStringLiteral("Launch"),
                                          {},
                                          {},
                                          false,
                                          base,
                                          base.addSecs(1));
    QVERIFY(!clipId.isEmpty());
    ClipSearchService clipSearch(clipRepository);

    PinloomPanelOptions panelOptions;
    panelOptions.clipSearchHandler = [&](const QString &query, const ClipSearchOptions &options) {
        return clipSearch.search(query, options);
    };
    PinloomPanel mainPanel(repository, panelOptions);

    PinloomCommandPanelOptions commandOptions;
    commandOptions.unifiedEntrySearchHandler = [&](const QString &query) {
        mainPanel.setSearchText(query);
        return mainPanel.currentEntries();
    };
    PinloomCommandPanel commandPanel(commandOptions);
    commandPanel.setCommandText(QStringLiteral("Launch"));

    QCOMPARE(commandPanel.resultCount(), 5);
    const QList<PinloomOpenTarget> firstTwo = {
        commandPanel.openTargetAt(0),
        commandPanel.openTargetAt(1)
    };
    QVERIFY(std::any_of(firstTwo.cbegin(), firstTwo.cend(), [&](const PinloomOpenTarget &target) {
        return target.clipId == clipId;
    }));
    QVERIFY(std::any_of(firstTwo.cbegin(), firstTwo.cend(), [&](const PinloomOpenTarget &target) {
        return target.resourceId == exact.id;
    }));
    QCOMPARE(commandPanel.openTargetAt(2).resourceId, alias.id);
    QCOMPARE(commandPanel.openTargetAt(3).resourceId, tagged.id);
    QCOMPARE(commandPanel.openTargetAt(4).resourceId, pathOnly.id);
}

void WidgetSmokeTest::commandPanelAnchorCaptureCreatesForegroundPdfAnchorWithoutSelectedResult()
{
    InMemoryLibraryRepository repository;

    ForegroundAppWindowContext foregroundContext;
    foregroundContext.windowTitle = QStringLiteral("E:/docs/live-foreground.pdf - SumatraPDF");
    foregroundContext.processName = QStringLiteral("SumatraPDF.exe");

    int foregroundRequestCount = 0;
    ManualPdfAnchorCreationRequest capturedSuggested;
    PinloomPanelOptions panelOptions;
    panelOptions.foregroundPdfAnchorCaptureRequestProvider =
        [&](QString *status) -> std::optional<ManualPdfAnchorCreationRequest> {
        ++foregroundRequestCount;
        const SumatraPdfForegroundCaptureResult capture =
            captureSumatraPdfForegroundContext(repository, foregroundContext, SumatraPdfViewState{});
        if (status) {
            *status = capture.status;
        }
        if (!capture.success()) {
            return std::nullopt;
        }
        return capture.request;
    };
    panelOptions.pdfAnchorCaptureRequestProvider =
        [&](const ManualPdfAnchorCreationRequest &suggested) -> std::optional<ManualPdfAnchorCreationRequest> {
        capturedSuggested = suggested;
        ManualPdfAnchorCreationRequest request = suggested;
        request.name = QStringLiteral("Live foreground anchor");
        request.aliases = {QStringLiteral("live foreground alias")};
        request.tags = {QStringLiteral("#foreground-tag")};
        request.pinned = true;
        return request;
    };

    PinloomPanel panel(repository, panelOptions);
    QCOMPARE(panel.resultCount(), 0);

    PinloomCommandPanelOptions commandOptions;
    commandOptions.anchorCaptureHandler = [&panel](QString *status) {
        const bool captured = panel.captureForegroundPdfAnchor();
        if (status) {
            *status = panel.statusText();
        }
        return captured;
    };
    PinloomCommandPanel commandPanel(commandOptions);
    commandPanel.setCommandText(QStringLiteral("k n"));

    QVERIFY(commandPanel.activateCurrentCommandItem());

    QCOMPARE(foregroundRequestCount, 1);
    QCOMPARE(capturedSuggested.file, QStringLiteral("E:/docs/live-foreground.pdf"));
    QCOMPARE(capturedSuggested.locatorType, QStringLiteral("sumatrapdf.page"));
    QVERIFY(!capturedSuggested.rect.isValid());
    QCOMPARE(capturedSuggested.source, QStringLiteral("foreground-sumatrapdf-fallback"));
    QCOMPARE(commandPanel.statusText(),
             QStringLiteral("Captured PDF anchor \"Live foreground anchor\" (foreground SumatraPDF fallback)"));

    const QList<SearchResult> byName = repository.search(SearchQuery{QStringLiteral("Live foreground anchor")});
    QCOMPARE(byName.size(), 1);
    QVERIFY(byName.first().matchedAnchor.has_value());
    QCOMPARE(byName.first().matchedAnchor->targetApp, QStringLiteral("SumatraPDF"));
    QCOMPARE(byName.first().matchedAnchor->targetFile, QStringLiteral("E:/docs/live-foreground.pdf"));
    QCOMPARE(byName.first().matchedAnchor->locatorType, QStringLiteral("sumatrapdf.page"));
    QVERIFY(byName.first().matchedAnchor->pinned);

    const QList<SearchResult> byAlias = repository.search(SearchQuery{QStringLiteral("live foreground alias")});
    QCOMPARE(byAlias.size(), 1);
    const QList<SearchResult> byTag = repository.search(SearchQuery{QStringLiteral("#foreground-tag")});
    QCOMPARE(byTag.size(), 1);
}

void WidgetSmokeTest::commandPanelAnchorCaptureUsesUniqueTitleFallbackWithoutFullPath()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("hb0823");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("HB0823_MIV_RV32IMA_L1_AXI");
    resource.location = QStringLiteral("E:/docs/HB0823_MIV_RV32IMA_L1_AXI.pdf");
    QVERIFY(repository.upsertResource(resource));

    ForegroundAppWindowContext foregroundContext;
    foregroundContext.windowTitle = QStringLiteral("HB0823_MIV_RV32IMA_L1_AXI - SumatraPDF");
    foregroundContext.processName = QStringLiteral("SumatraPDF.exe");

    SumatraPdfViewState viewState;
    viewState.currentPage = 26;
    viewState.totalPages = 31;
    viewState.zoom = 300.0;
    viewState.source = QStringLiteral("test-toolbar");

    int foregroundRequestCount = 0;
    ManualPdfAnchorCreationRequest capturedSuggested;
    PinloomPanelOptions panelOptions;
    panelOptions.foregroundPdfAnchorCaptureRequestProvider =
        [&](QString *status) -> std::optional<ManualPdfAnchorCreationRequest> {
        ++foregroundRequestCount;
        const SumatraPdfForegroundCaptureResult capture =
            captureSumatraPdfForegroundContext(repository, foregroundContext, viewState);
        if (status) {
            *status = capture.status;
        }
        if (!capture.success()) {
            return std::nullopt;
        }
        return capture.request;
    };
    panelOptions.pdfAnchorCaptureRequestProvider =
        [&](const ManualPdfAnchorCreationRequest &suggested) -> std::optional<ManualPdfAnchorCreationRequest> {
        capturedSuggested = suggested;
        ManualPdfAnchorCreationRequest request = suggested;
        request.name = QStringLiteral("HB0823 selected text");
        request.aliases = {QStringLiteral("hb0823 note")};
        return request;
    };

    PinloomPanel panel(repository, panelOptions);
    PinloomCommandPanelOptions commandOptions;
    commandOptions.anchorCaptureHandler = [&panel](QString *status) {
        const bool captured = panel.captureForegroundPdfAnchor();
        if (status) {
            *status = panel.statusText();
        }
        return captured;
    };
    PinloomCommandPanel commandPanel(commandOptions);
    commandPanel.setCommandText(QStringLiteral("k n"));

    QVERIFY(commandPanel.activateCurrentCommandItem());

    QCOMPARE(foregroundRequestCount, 1);
    QCOMPARE(capturedSuggested.file, resource.location);
    QCOMPARE(capturedSuggested.page, 26);
    QCOMPARE(capturedSuggested.zoom, 300.0);
    QCOMPARE(capturedSuggested.source, QStringLiteral("foreground-sumatrapdf-viewstate"));
    QCOMPARE(commandPanel.statusText(),
             QStringLiteral("Captured PDF anchor \"HB0823 selected text\" (foreground SumatraPDF page/zoom)"));

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("hb0823 note")});
    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(results.first().matchedAnchor->targetFile, resource.location);
    QVERIFY(results.first().matchedAnchor->locatorJson.contains(QStringLiteral("\"page\":26")));
    QVERIFY(results.first().matchedAnchor->locatorJson.contains(QStringLiteral("\"zoom\":300")));
    QVERIFY(results.first().matchedAnchor->locatorJson.contains(
        QStringLiteral("\"source\":\"foreground-sumatrapdf-viewstate\"")));
}

void WidgetSmokeTest::commandPanelAnchorCapturePrefersForegroundPdfFallback()
{
    InMemoryLibraryRepository repository;

    Resource selectedResource;
    selectedResource.id = QStringLiteral("selected-pdf");
    selectedResource.kind = ResourceKind::Pdf;
    selectedResource.title = QStringLiteral("Selected PDF");
    selectedResource.location = QStringLiteral("E:/docs/selected.pdf");
    QVERIFY(repository.upsertResource(selectedResource));

    int foregroundRequestCount = 0;
    ManualPdfAnchorCreationRequest capturedSuggested;
    PinloomPanelOptions panelOptions;
    panelOptions.foregroundPdfAnchorCaptureRequestProvider =
        [&](QString *status) -> std::optional<ManualPdfAnchorCreationRequest> {
        ++foregroundRequestCount;
        if (status) {
            *status = QStringLiteral("foreground provider ready");
        }
        ManualPdfAnchorCreationRequest request;
        request.name = QStringLiteral("Foreground command anchor");
        request.file = QStringLiteral("E:/docs/foreground.pdf");
        request.locatorType = QStringLiteral("sumatrapdf.page");
        request.page = 1;
        request.source = QStringLiteral("foreground-sumatrapdf-fallback");
        request.targetApp = QStringLiteral("SumatraPDF");
        return request;
    };
    panelOptions.pdfAnchorCaptureRequestProvider =
        [&](const ManualPdfAnchorCreationRequest &suggested) -> std::optional<ManualPdfAnchorCreationRequest> {
        capturedSuggested = suggested;
        ManualPdfAnchorCreationRequest request = suggested;
        request.name = QStringLiteral("Accepted foreground command anchor");
        request.aliases = {QStringLiteral("foreground command alias")};
        return request;
    };

    PinloomPanel panel(repository, panelOptions);
    panel.setSearchText(QStringLiteral("Selected PDF"));
    QVERIFY(panel.selectFirstResult());

    PinloomCommandPanelOptions commandOptions;
    commandOptions.anchorCaptureHandler = [&panel](QString *status) {
        const bool captured = panel.captureForegroundPdfAnchor();
        if (status) {
            *status = panel.statusText();
        }
        return captured;
    };
    PinloomCommandPanel commandPanel(commandOptions);
    commandPanel.setCommandText(QStringLiteral("k n"));

    QVERIFY(commandPanel.activateCurrentCommandItem());

    QCOMPARE(foregroundRequestCount, 1);
    QCOMPARE(capturedSuggested.file, QStringLiteral("E:/docs/foreground.pdf"));
    QCOMPARE(capturedSuggested.source, QStringLiteral("foreground-sumatrapdf-fallback"));
    QCOMPARE(commandPanel.statusText(),
             QStringLiteral("Captured PDF anchor \"Accepted foreground command anchor\" (foreground SumatraPDF fallback)"));
    QCOMPARE(panel.statusText(), commandPanel.statusText());

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("foreground command alias")});
    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(results.first().matchedAnchor->targetFile, QStringLiteral("E:/docs/foreground.pdf"));
    QVERIFY(results.first().matchedAnchor->locatorJson.contains(
        QStringLiteral("\"source\":\"foreground-sumatrapdf-fallback\"")));
}

void WidgetSmokeTest::commandPanelAnchorCaptureReportsNonPdfForegroundWithoutSelectedFallback()
{
    InMemoryLibraryRepository repository;

    Resource selectedResource;
    selectedResource.id = QStringLiteral("selected-pdf");
    selectedResource.kind = ResourceKind::Pdf;
    selectedResource.title = QStringLiteral("Selected PDF");
    selectedResource.location = QStringLiteral("E:/docs/selected.pdf");
    QVERIFY(repository.upsertResource(selectedResource));

    int foregroundRequestCount = 0;
    PinloomPanelOptions panelOptions;
    panelOptions.foregroundPdfAnchorCaptureRequestProvider =
        [&](QString *status) -> std::optional<ManualPdfAnchorCreationRequest> {
        ++foregroundRequestCount;
        if (status) {
            *status = QStringLiteral("Open or focus a SumatraPDF PDF before k n");
        }
        return std::nullopt;
    };

    PinloomPanel panel(repository, panelOptions);
    panel.setSearchText(QStringLiteral("Selected PDF"));
    QVERIFY(panel.selectFirstResult());

    PinloomCommandPanelOptions commandOptions;
    commandOptions.anchorCaptureHandler = [&panel](QString *status) {
        const bool captured = panel.captureForegroundPdfAnchor();
        if (status) {
            *status = panel.statusText();
        }
        return captured;
    };
    PinloomCommandPanel commandPanel(commandOptions);
    commandPanel.setCommandText(QStringLiteral("k n"));

    QVERIFY(!commandPanel.activateCurrentCommandItem());

    QCOMPARE(foregroundRequestCount, 1);
    QCOMPARE(commandPanel.statusText(), QStringLiteral("Open or focus a SumatraPDF PDF before k n"));
    QCOMPARE(panel.statusText(), commandPanel.statusText());
    const std::optional<Resource> selected = repository.findResource(selectedResource.id);
    QVERIFY(selected.has_value());
    QVERIFY(selected->anchors.isEmpty());
}

void WidgetSmokeTest::commandPanelAnchorCaptureReportsForegroundPdfWithoutFilePath()
{
    InMemoryLibraryRepository repository;

    ForegroundAppWindowContext foregroundContext;
    foregroundContext.windowTitle = QStringLiteral("clock - SumatraPDF");
    foregroundContext.processName = QStringLiteral("SumatraPDF.exe");

    int foregroundRequestCount = 0;
    PinloomPanelOptions panelOptions;
    panelOptions.foregroundPdfAnchorCaptureRequestProvider =
        [&](QString *status) -> std::optional<ManualPdfAnchorCreationRequest> {
        ++foregroundRequestCount;
        const SumatraPdfForegroundCaptureResult capture =
            captureSumatraPdfForegroundContext(repository, foregroundContext, SumatraPdfViewState{});
        if (status) {
            *status = capture.status;
        }
        if (!capture.success()) {
            return std::nullopt;
        }
        return capture.request;
    };

    PinloomPanel panel(repository, panelOptions);

    PinloomCommandPanelOptions commandOptions;
    commandOptions.anchorCaptureHandler = [&panel](QString *status) {
        const bool captured = panel.captureForegroundPdfAnchor();
        if (status) {
            *status = panel.statusText();
        }
        return captured;
    };
    PinloomCommandPanel commandPanel(commandOptions);
    commandPanel.setCommandText(QStringLiteral("k n"));

    QVERIFY(!commandPanel.activateCurrentCommandItem());

    QCOMPARE(foregroundRequestCount, 1);
    QVERIFY(commandPanel.statusText().contains(QStringLiteral("Confirm the PDF file")));
    QVERIFY(commandPanel.statusText().contains(QStringLiteral("no indexed PDF matched")));
    QVERIFY(repository.search(SearchQuery{}).isEmpty());
}

void WidgetSmokeTest::panelDisplaysAnchorAwareResults()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("Note");
    resource.location = QStringLiteral("note.md");
    resource.anchors = {
        testAnchor(QStringLiteral("Power sequencing"), QStringLiteral("text.heading"), 3)};
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
    QCOMPARE(results->item(0)->data(Qt::UserRole + 26).toString(),
             QStringLiteral("text.heading"));
}

void WidgetSmokeTest::panelDisplaysAndOpensInboxFileEntries()
{
    CapturingUrlHandler fileHandler;
    QDesktopServices::setUrlHandler(QStringLiteral("file"), &fileHandler, "openUrl");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString filePath = dir.filePath(QStringLiteral("Inbox Launch.txt"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("launch me");
    file.close();

    InMemoryLibraryRepository repository;
    InboxFileSaveRequest request;
    request.filePath = filePath;
    request.name = QStringLiteral("Inbox Launch");
    request.aliases = {QStringLiteral("launch alias")};
    request.tags = {QStringLiteral("inbox-open")};
    const InboxFileSaveResult saveResult = saveInboxFile(repository, request);
    QVERIFY(saveResult.success());

    {
        PinloomPanel panel(repository);
        auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
        QVERIFY(results);

        panel.setSearchText(QStringLiteral("launch alias"));
        QCOMPARE(results->count(), 1);
        QVERIFY(results->item(0)->text().contains(QStringLiteral("[Inbox] Inbox Launch")));
        QCOMPARE(panel.currentOpenTarget().resourceId, saveResult.resourceId);
        QCOMPARE(panel.currentOpenTarget().location, normalizedInboxFilePath(filePath));

        QVERIFY(panel.activateCurrentOpenTarget());
        QCOMPARE(fileHandler.openCount, 1);
        QCOMPARE(fileHandler.lastUrl.toLocalFile(), normalizedInboxFilePath(filePath));
        const std::optional<ResourceUsage> usage = repository.resourceUsage(saveResult.resourceId);
        QVERIFY(usage.has_value());
        QCOMPARE(usage->openCount, 1);
    }

    QVERIFY(QFile::remove(filePath));

    {
        PinloomPanel panel(repository);
        panel.setSearchText(QStringLiteral("Inbox Launch"));
        QVERIFY(!panel.activateCurrentOpenTarget());
        QCOMPARE(panel.statusText(),
                 QStringLiteral("Inbox file no longer exists: %1").arg(normalizedInboxFilePath(filePath)));
        QCOMPARE(fileHandler.openCount, 1);
    }

    QDesktopServices::unsetUrlHandler(QStringLiteral("file"));
}

void WidgetSmokeTest::panelSearchEntriesCanIncludeDeletedEntriesForRestore()
{
    InMemoryLibraryRepository repository;

    Resource deletedResource;
    deletedResource.id = QStringLiteral("deleted-resource");
    deletedResource.kind = ResourceKind::File;
    deletedResource.title = QStringLiteral("Deleted Resource");
    deletedResource.location = QStringLiteral("E:/docs/deleted-resource.txt");
    deletedResource.deleted = true;
    QVERIFY(repository.upsertResource(deletedResource));

    Resource anchorResource;
    anchorResource.id = QStringLiteral("anchor-resource");
    anchorResource.kind = ResourceKind::Pdf;
    anchorResource.title = QStringLiteral("Anchor Container");
    anchorResource.location = QStringLiteral("E:/docs/anchor.pdf");
    Anchor deletedAnchor;
    deletedAnchor.id = QStringLiteral("anchor-resource#deleted");
    deletedAnchor.name = QStringLiteral("Deleted Anchor");
    deletedAnchor.targetFile = anchorResource.location;
    deletedAnchor.locatorType = QStringLiteral("sumatrapdf.rect");
    deletedAnchor.locatorJson = QStringLiteral("{\"page\":3}");
    deletedAnchor.deleted = true;
    anchorResource.anchors = {deletedAnchor};
    QVERIFY(repository.upsertResource(anchorResource));

    InMemoryClipRepository clipRepository;
    const QDateTime base = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    const QString clipId = saveWidgetClip(clipRepository,
                                          QStringLiteral("deleted clip text"),
                                          QStringLiteral("Deleted Clip"),
                                          {QStringLiteral("deleted clip alias")},
                                          {},
                                          false,
                                          base,
                                          base.addSecs(1));
    QVERIFY(!clipId.isEmpty());
    QVERIFY(clipRepository.softDeleteSavedClip(clipId, base.addSecs(2)));
    ClipSearchService clipSearch(clipRepository);

    PinloomPanelOptions options;
    options.clipSearchHandler = [&clipSearch](const QString &query, const ClipSearchOptions &searchOptions) {
        return clipSearch.search(query, searchOptions);
    };
    PinloomPanel panel(repository, options);

    const QList<PinloomEntry> ordinaryEntries = panel.searchEntries(QStringLiteral("Deleted"), false);
    QVERIFY(std::none_of(ordinaryEntries.cbegin(), ordinaryEntries.cend(), [](const PinloomEntry &entry) {
        return entry.deleted;
    }));

    const QList<PinloomEntry> deletedEntries = panel.searchEntries(QStringLiteral("Deleted"), true);
    QVERIFY(std::any_of(deletedEntries.cbegin(), deletedEntries.cend(), [](const PinloomEntry &entry) {
        return entry.type == PinloomEntryType::FileResource
            && entry.name == QStringLiteral("Deleted Resource")
            && entry.deleted;
    }));
    QVERIFY(std::any_of(deletedEntries.cbegin(), deletedEntries.cend(), [](const PinloomEntry &entry) {
        return entry.type == PinloomEntryType::Anchor
            && entry.name == QStringLiteral("Deleted Anchor")
            && entry.deleted
            && entry.anchor.has_value()
            && entry.anchor->deleted;
    }));
    QVERIFY(std::any_of(deletedEntries.cbegin(), deletedEntries.cend(), [clipId](const PinloomEntry &entry) {
        return entry.type == PinloomEntryType::SavedClip
            && entry.clipId == clipId
            && entry.deleted;
    }));
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
    anchor.name = QStringLiteral("PLL jitter budget");
    anchor.targetApp = QStringLiteral("SumatraPDF");
    anchor.targetFile = resource.location;
    anchor.locatorType = QStringLiteral("sumatrapdf.rect");
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
    QVERIFY(results->item(0)->text().contains(QStringLiteral("SumatraPDF")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("sumatrapdf.rect")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("#clock")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("aliases: pll budget")));
    QVERIFY(results->item(0)->toolTip().contains(QStringLiteral("Locator: sumatrapdf.rect")));

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
    resource.anchors = {
        testAnchor(QStringLiteral("marker: handoff_marker"), QStringLiteral("marker"), 9)};
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
    QCOMPARE(results->item(0)->data(Qt::UserRole + 26).toString(), QStringLiteral("marker"));
}

void WidgetSmokeTest::panelDisplaysBeaconLineResults()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("text-beacon");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("beacon-notes.txt");
    resource.location = QStringLiteral("beacon-notes.txt");
    resource.anchors = {
        testAnchor(QStringLiteral("marker: jump target"), QStringLiteral("file.line"), 12)};
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
    QCOMPARE(results->item(0)->data(Qt::UserRole + 26).toString(), QStringLiteral("file.line"));
}

void WidgetSmokeTest::panelDisplaysFileLineResults()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("line-note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("dock-notes.txt");
    resource.location = QStringLiteral("dock-notes.txt");
    resource.anchors = {
        testAnchor(QStringLiteral("TODO: wire ZeroSlack dock"), QStringLiteral("file.line"), 27)};
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
    QCOMPARE(results->item(0)->data(Qt::UserRole + 26).toString(), QStringLiteral("file.line"));
}

void WidgetSmokeTest::panelDisplaysPdfPageResults()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("pdf");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Spec");
    resource.location = QStringLiteral("spec.pdf");
    Anchor page = testAnchor(QStringLiteral("Page 2"), QStringLiteral("pdf.page"));
    page.locatorJson = QStringLiteral("{\"page\":2,\"type\":\"pdf.page\"}");
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
    QCOMPARE(results->item(0)->data(Qt::UserRole + 26).toString(), QStringLiteral("pdf.page"));
}

void WidgetSmokeTest::panelPreservesPdfRegionOpenTarget()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("pdf-region");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Annotated Spec");
    resource.location = QStringLiteral("spec.pdf");
    Anchor region = testAnchor(QStringLiteral("Clock domain note"), QStringLiteral("pdf.region"));
    region.locatorJson =
        QStringLiteral("{\"page\":4,\"region\":[10,20,100,40],\"type\":\"pdf.region\"}");
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
    QCOMPARE(results->item(0)->data(Qt::UserRole + 26).toString(), QStringLiteral("pdf.region"));

    results->setCurrentRow(0);
    openButton->click();

    QVERIFY(handled);
    QVERIFY(capturedTarget.anchor.has_value());
    QCOMPARE(capturedTarget.anchor->name, QStringLiteral("Clock domain note"));
    QCOMPARE(anchorLocatorPage(capturedTarget.anchor.value()), 4);
    const std::optional<QRectF> capturedRegion =
        anchorLocatorRegion(capturedTarget.anchor.value());
    QVERIFY(capturedRegion.has_value());
    QCOMPARE(capturedRegion.value(), QRectF(10.0, 20.0, 100.0, 40.0));
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
    QVERIFY(results->item(0)->text().contains(QStringLiteral("file.line")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("\"line\":7")));
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), resource.id);
    QCOMPARE(results->item(0)->data(Qt::UserRole + 26).toString(),
             QStringLiteral("file.line"));

    panel.setSearchText(QStringLiteral("Host rail"));
    QCOMPARE(results->count(), 1);
    QVERIFY(results->item(0)->text().contains(QStringLiteral("Host rail check")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("file.line")));
    QVERIFY(results->item(0)->text().contains(QStringLiteral("\"line\":11")));
    QCOMPARE(results->item(0)->data(Qt::UserRole).toString(), hostResource.id);
    QCOMPARE(results->item(0)->data(Qt::UserRole + 26).toString(),
             QStringLiteral("file.line"));
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
    QCOMPARE(panel.statusText(), QStringLiteral("PDF line anchors are deprecated; use SumatraPDF anchor capture"));
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


void WidgetSmokeTest::panelSupportsEmbeddedChromeOptions()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("UART Project Note");
    resource.location = QStringLiteral("E:/workspace/project/note.md");
    QVERIFY(repository.upsertResource(resource));

    PinloomPanelOptions options;
    options.showManualEditControls = false;
    options.showPinControls = false;
    PinloomPanel panel(repository, options);

    auto *addAliasButton = panel.findChild<QPushButton *>(QStringLiteral("addAliasButton"));
    auto *addAnchorButton = panel.findChild<QPushButton *>(QStringLiteral("addAnchorButton"));
    auto *pinButton = panel.findChild<QPushButton *>(QStringLiteral("pinButton"));
    auto *openButton = panel.findChild<QPushButton *>(QStringLiteral("openButton"));
    auto *results = panel.findChild<QListWidget *>(QStringLiteral("resultList"));
    QVERIFY(addAliasButton);
    QVERIFY(addAnchorButton);
    QVERIFY(pinButton);
    QVERIFY(openButton);
    QVERIFY(results);

    QVERIFY(addAliasButton->isHidden());
    QVERIFY(addAnchorButton->isHidden());
    QVERIFY(pinButton->isHidden());
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
    panel.applyHostContext(context);

    const PinloomHostContext snapshot = panel.hostContext();
    QCOMPARE(snapshot.searchText, context.searchText);
    QCOMPARE(snapshot.requiredTags, context.requiredTags);
    QCOMPARE(snapshot.requiredLocationPrefixes, context.requiredLocationPrefixes);
    QCOMPARE(snapshot.requiredResourceKinds, context.requiredResourceKinds);
    QCOMPARE(snapshot.contextTags, context.contextTags);
    QCOMPARE(snapshot.contextLocationPrefixes, context.contextLocationPrefixes);

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


void WidgetSmokeTest::panelExposesCurrentOpenTargetForHostPreview()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("note");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("ZeroSlack Handoff");
    resource.location = QStringLiteral("E:/workspace/project/handoff.md");
    resource.tags = {QStringLiteral("zeroslack")};
    resource.anchors = {
        testAnchor(QStringLiteral("Dock handoff"), QStringLiteral("text.heading"), 8)};
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
    QCOMPARE(rowTarget.matchedField, QStringLiteral("anchor_name"));
    QCOMPARE(rowTarget.matchedContextTag, QStringLiteral("zeroslack"));
    QCOMPARE(rowTarget.matchedContextLocationPrefix, QStringLiteral("E:/workspace/project"));
    QVERIFY(rowTarget.matchSummary.contains(QStringLiteral("Match: anchor")));
    QVERIFY(rowTarget.matchSummary.contains(QStringLiteral("Anchor: Heading")));
    QVERIFY(rowTarget.matchSummary.contains(QStringLiteral("Context tag: zeroslack")));
    QVERIFY(rowTarget.matchSummary.contains(QStringLiteral("Context location: E:/workspace/project")));
    QVERIFY(rowTarget.anchor.has_value());
    QCOMPARE(rowTarget.anchor->name, QStringLiteral("Dock handoff"));
    QCOMPARE(anchorLocatorLine(rowTarget.anchor.value()), 8);
    QVERIFY(panel.resultAt(1).resourceId.isEmpty());
    QCOMPARE(activationCount, 0);

    const QList<PinloomOpenTarget> currentResults = panel.currentResults();
    QCOMPARE(currentResults.size(), 1);
    QCOMPARE(currentResults.first().resourceId, resource.id);
    QCOMPARE(currentResults.first().resultRow, 0);
    QCOMPARE(currentResults.first().matchSummary, rowTarget.matchSummary);
    QVERIFY(currentResults.first().anchor.has_value());
    QCOMPARE(currentResults.first().anchor->name, QStringLiteral("Dock handoff"));
    QCOMPARE(currentResults.first().location, resource.location);
    QCOMPARE(activationCount, 0);

    const PinloomOpenTarget target = panel.currentOpenTarget();
    QCOMPARE(target.resultRow, 0);
    QCOMPARE(target.resourceId, resource.id);
    QCOMPARE(target.resourceKind, resource.kind);
    QCOMPARE(target.title, resource.title);
    QCOMPARE(target.location, resource.location);
    QCOMPARE(target.matchedField, QStringLiteral("anchor_name"));
    QCOMPARE(target.matchedContextTag, QStringLiteral("zeroslack"));
    QCOMPARE(target.matchedContextLocationPrefix, QStringLiteral("E:/workspace/project"));
    QCOMPARE(target.matchSummary, rowTarget.matchSummary);
    QVERIFY(target.score < 0.0);
    QVERIFY(target.anchor.has_value());
    QCOMPARE(target.anchor->locatorType, QStringLiteral("text.heading"));
    QCOMPARE(target.anchor->name, QStringLiteral("Dock handoff"));
    QCOMPARE(anchorLocatorLine(target.anchor.value()), 8);
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
    resource.anchors = {
        testAnchor(QStringLiteral("Preview target"), QStringLiteral("text.heading"), 4)};
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
    QCOMPARE(notified.anchor->locatorType, QStringLiteral("text.heading"));
    QCOMPARE(notified.anchor->name, QStringLiteral("Preview target"));
    QCOMPARE(anchorLocatorLine(notified.anchor.value()), 4);
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
    resource.anchors = {
        testAnchor(QStringLiteral("Dock command"), QStringLiteral("text.heading"), 5)};
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
    QCOMPARE(capturedTarget.matchedField, QStringLiteral("anchor_name"));
    QVERIFY(capturedTarget.anchor.has_value());
    QCOMPARE(capturedTarget.anchor->name, QStringLiteral("Dock command"));
    QCOMPARE(anchorLocatorLine(capturedTarget.anchor.value()), 5);

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
    resource.anchors = {
        testAnchor(QStringLiteral("Power sequencing"), QStringLiteral("text.heading"), 3)};
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
    QCOMPARE(capturedTarget.matchedField, QStringLiteral("anchor_name"));
    QCOMPARE(capturedTarget.score, 0.0);
    QVERIFY(capturedTarget.matchSummary.contains(QStringLiteral("Match: anchor")));
    QVERIFY(capturedTarget.matchSummary.contains(QStringLiteral("Anchor: Heading")));
    QVERIFY(capturedTarget.anchor.has_value());
    QCOMPARE(capturedTarget.anchor->locatorType, QStringLiteral("text.heading"));
    QCOMPARE(capturedTarget.anchor->name, QStringLiteral("Power sequencing"));
    QCOMPARE(anchorLocatorLine(capturedTarget.anchor.value()), 3);

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
    anchor.name = QStringLiteral("Keyboard command");
    anchor.locatorType = QStringLiteral("text.heading");
    anchor.locatorJson = QStringLiteral("{\"line\":6,\"type\":\"text.heading\"}");
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
    QTimer::singleShot(0, [&]() {
        auto *messageBox = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        QVERIFY(messageBox);
        auto *noButton = messageBox->button(QMessageBox::No);
        QVERIFY(noButton);
        noButton->click();
    });
    QTest::keyClick(searchEdit, Qt::Key_Delete);
    QVERIFY(statusNotifications.last().contains(QStringLiteral("Delete canceled")));
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("Edited keyboard command")}).size(), 1);
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
    QCOMPARE(anchor.locatorType, QStringLiteral("sumatrapdf.rect"));
    QCOMPARE(anchorLocatorPage(anchor), 1);
    const std::optional<QRectF> region = anchorLocatorRegion(anchor);
    QVERIFY(region.has_value());
    QCOMPARE(region->x(), 0.0);
    QCOMPARE(region->y(), 0.0);
    QCOMPARE(region->width(), 612.0);
    QCOMPARE(region->height(), 792.0);
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

    QCOMPARE(dialog.request().source, QStringLiteral("selected-pdf-fallback"));

    suggested.source = QStringLiteral("foreground-sumatrapdf-fallback");
    suggested.name = QStringLiteral("Foreground fallback");
    ManualPdfAnchorDialog foregroundDialog(suggested);
    foregroundDialog.show();
    QApplication::processEvents();

    auto *foregroundSummary = foregroundDialog.findChild<QLabel *>(QStringLiteral("manualPdfAnchorSummaryLabel"));
    QVERIFY(foregroundSummary);
    QVERIFY(foregroundSummary->text().contains(QStringLiteral("foreground SumatraPDF fallback")));
    QVERIFY(foregroundSummary->text().contains(QStringLiteral("page defaults to 1")));
    QCOMPARE(foregroundDialog.request().source, QStringLiteral("foreground-sumatrapdf-fallback"));
}

void WidgetSmokeTest::manualPdfCaptureDialogExplainsForegroundViewStateFallback()
{
    ManualPdfAnchorCreationRequest suggested;
    suggested.file = QStringLiteral("E:/docs/spec.pdf");
    suggested.locatorType = QStringLiteral("sumatrapdf.page");
    suggested.page = 37;
    suggested.zoom = 175.0;
    suggested.source = QStringLiteral("foreground-sumatrapdf-viewstate");

    ManualPdfAnchorDialog dialog(suggested);
    dialog.show();
    QApplication::processEvents();

    auto *summary = dialog.findChild<QLabel *>(QStringLiteral("manualPdfAnchorSummaryLabel"));
    QVERIFY(summary);
    QVERIFY(summary->text().contains(QStringLiteral("foreground SumatraPDF page/zoom")));
    QVERIFY(!summary->text().contains(QStringLiteral("rectangle")));
    QVERIFY(summary->text().contains(QStringLiteral("page 37")));
    QVERIFY(summary->text().contains(QStringLiteral("zoom 175")));
    QCOMPARE(dialog.request().source, QStringLiteral("foreground-sumatrapdf-viewstate"));
    QCOMPARE(dialog.request().page, 37);
    QCOMPARE(dialog.request().zoom, 175.0);
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
    QCOMPARE(results.first().matchedAnchor->locatorType, QStringLiteral("sumatrapdf.rect"));
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
    QCOMPARE(panel.currentOpenTarget().anchor->targetApp, QStringLiteral("SumatraPDF"));
    QCOMPARE(panel.currentOpenTarget().anchor->targetFile, QStringLiteral("E:/docs/dialog-pick.pdf"));
    QCOMPARE(panel.currentOpenTarget().anchor->locatorType, QStringLiteral("sumatrapdf.rect"));
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
    QCOMPARE(panel.statusText(), QStringLiteral("SumatraPDF capture page is missing"));
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

void WidgetSmokeTest::panelLaunchesSumatraPdfAnchorWithInjectedExecutor()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("pdf-clock");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Clock Spec");
    resource.location = QStringLiteral("E:/docs/clock.pdf");
    Anchor anchor;
    anchor.name = QStringLiteral("PLL jitter budget");
    anchor.targetApp = QStringLiteral("SumatraPDF");
    anchor.targetFile = resource.location;
    anchor.locatorType = QStringLiteral("sumatrapdf.rect");
    anchor.locatorJson = QStringLiteral("{\"page\":12,\"rect\":[420,860,780,920],\"zoom\":250,\"unit\":\"pt\"}");
    resource.anchors = {anchor};
    QVERIFY(repository.upsertResource(resource));

    bool launched = false;
    SumatraPdfCommand capturedCommand;
    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.applicationLaunchSettings.sumatraPdfExecutablePath =
        QStringLiteral("C:/Tools/SumatraPDF.exe");
    options.sumatraPdfLaunchHandler = [&](const SumatraPdfCommand &command, QString *error) {
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
    QCOMPARE(capturedCommand.executablePath, QStringLiteral("C:/Tools/SumatraPDF.exe"));
    QCOMPARE(capturedCommand.arguments,
             QStringList({QStringLiteral("-reuse-instance"),
                          QStringLiteral("-page"),
                          QStringLiteral("12"),
                          QStringLiteral("-zoom"),
                          QStringLiteral("250"),
                          QStringLiteral("-scroll"),
                          QStringLiteral("420,860"),
                          resource.location}));
    QCOMPARE(panel.statusText(), QStringLiteral("Opened SumatraPDF target"));
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

void WidgetSmokeTest::panelReportsMissingSumatraPdfExecutable()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("pdf-page");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Spec");
    resource.location = QStringLiteral("E:/docs/spec.pdf");
    Anchor anchor = testAnchor(QStringLiteral("Page 7"), QStringLiteral("sumatrapdf.page"));
    anchor.locatorJson =
        QStringLiteral("{\"page\":7,\"type\":\"sumatrapdf.page\"}");
    resource.anchors = {anchor};
    QVERIFY(repository.upsertResource(resource));

    QStringList statusNotifications;
    PinloomPanelOptions options;
    options.sumatraPdfExecutablePathProvider = []() {
        return QString();
    };
    options.statusChangedHandler = [&](const QString &statusText) {
        statusNotifications.append(statusText);
    };

    PinloomPanel panel(repository, options);
    panel.setSearchText(QStringLiteral("Page 7"));
    QVERIFY(panel.selectFirstResult());

    QVERIFY(!panel.activateCurrentOpenTarget());
    QCOMPARE(panel.statusText(), QStringLiteral("SumatraPDF executable is not configured/found"));
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
    Anchor fragment = testAnchor(QStringLiteral("install"), QStringLiteral("url.fragment"));
    fragment.locatorJson =
        QStringLiteral("{\"fragment\":\"install\",\"type\":\"url.fragment\"}");
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
