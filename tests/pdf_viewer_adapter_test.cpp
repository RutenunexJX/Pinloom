#include "pinloom/core/AnchorCaptureDraft.h"
#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/widgets/SumatraPdfViewerAdapter.h"
#include "pinloom/widgets/PinloomOpenService.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QThread>
#include <QTimer>

using namespace Pinloom;

namespace {

constexpr auto PdfPath = "E:/docs/adapter-report.pdf";

ForegroundAppWindowContext pdfContext()
{
    ForegroundAppWindowContext context;
    context.windowTitle = QStringLiteral("adapter-report.pdf - SumatraPDF");
    context.processName = QStringLiteral("SumatraPDF.exe");
    context.processPath = QStringLiteral("C:/Tools/SumatraPDF.exe");
    context.windowHandle = 701;
    context.processId = 702;
    return context;
}

SumatraPdfViewState pdfViewState()
{
    SumatraPdfViewState state;
    state.documentPath = QString::fromLatin1(PdfPath);
    state.currentPage = 3;
    state.totalPages = 24;
    state.zoom = 175.0;
    state.source = QStringLiteral("dde");
    return state;
}

SumatraPdfDdeFileState fileState(int page = 3,
                                 const QString &path = QString::fromLatin1(PdfPath))
{
    SumatraPdfDdeFileState state;
    state.path = path;
    state.page = page;
    state.pageCount = 24;
    state.zoom = 175.0;
    state.view = QStringLiteral("continuous");
    return state;
}

PdfPageGeometryResult pageGeometry()
{
    PdfPageGeometryResult result;
    result.geometry.mediaBox = QRectF(0, 0, 612, 792);
    result.geometry.cropBox = QRectF(18, 24, 576, 744);
    result.geometry.rotation = 90;
    result.geometry.userUnit = 1.5;
    return result;
}

Anchor pageAnchor(int page)
{
    Anchor anchor;
    anchor.id = QStringLiteral("adapter-page-%1").arg(page);
    anchor.name = QStringLiteral("Adapter page %1").arg(page);
    anchor.targetApp = QStringLiteral("SumatraPDF");
    anchor.targetFile = QString::fromLatin1(PdfPath);
    anchor.locatorType = QStringLiteral("sumatrapdf.page");
    anchor.locatorJson = QStringLiteral(
        R"({"type":"sumatrapdf.page","version":3,"page":%1,"rotation":0,"userUnit":1})")
                             .arg(page);
    return anchor;
}

PdfViewerOpenRequest openRequest(int page)
{
    PdfViewerOpenRequest request;
    request.anchor = pageAnchor(page);
    request.fallbackFilePath = QString::fromLatin1(PdfPath);
    request.sourceTitle = QStringLiteral("Adapter report");
    return request;
}

SumatraPdfViewerAdapterOptions captureOptions()
{
    SumatraPdfViewerAdapterOptions options;
    options.foregroundContextProvider = pdfContext;
    options.viewStateProvider = [](const ForegroundAppWindowContext &) {
        return pdfViewState();
    };
    options.windowGeometryProvider = [](quintptr) {
        return QRect(100, 120, 900, 700);
    };
    options.windowExistsProvider = [](quintptr) { return true; };
    options.activateWindowHandler = [](quintptr) {};
    options.navigationStateProvider = [](int) { return fileState(); };
    options.pageGeometryProvider = [](const QString &, int) {
        return pageGeometry();
    };
    return options;
}

class RecordingAdapter final : public PdfViewerAdapter {
public:
    using PdfViewerAdapter::PdfViewerAdapter;

    QString adapterId() const override { return QStringLiteral("recording"); }
    bool supportsContext(const ForegroundAppWindowContext &) const override {
        return true;
    }
    bool supportsAnchor(const Anchor &) const override { return true; }
    PdfViewerCaptureResult captureRectangle(
        const PdfViewerCaptureRequest &) override {
        return {};
    }
    PdfViewerCaptureResult captureText(
        const PdfViewerCaptureRequest &) override {
        return {};
    }
    PdfViewerOpenStartResult open(
        const PdfViewerOpenRequest &request,
        PdfViewerOpenCallbacks valueCallbacks) override {
        lastRequest = request;
        callbacks = std::move(valueCallbacks);
        active = true;
        return {++lastId, {}};
    }
    void cancelPending() override {
        if (!active) return;
        const quint64 canceledId = lastId;
        active = false;
        if (callbacks.completed) {
            PdfViewerOpenResult result;
            result.requestId = canceledId;
            result.state = PdfViewerOperationState::Canceled;
            result.message = QStringLiteral("recording adapter canceled");
            callbacks.completed(result);
        }
    }
    int activeOperationCount() const override { return active ? 1 : 0; }
    bool hasOriginalFallback() const override { return fallbackAvailable; }
    bool openOriginalFallback(QString *error) override {
        if (error) error->clear();
        if (!fallbackAvailable) return false;
        fallbackAvailable = false;
        ++fallbackOpenCount;
        return true;
    }

    void complete(PdfViewerOperationState state,
                  const QString &message,
                  bool hasFallback = false) {
        if (!active) return;
        active = false;
        fallbackAvailable = hasFallback;
        PdfViewerOpenResult result;
        result.requestId = lastId;
        result.state = state;
        result.sourceFilePath = lastRequest.anchor.targetFile;
        result.page = 3;
        result.message = message;
        result.originalFallbackAvailable = hasFallback;
        if (callbacks.completed) callbacks.completed(result);
    }

    PdfViewerOpenRequest lastRequest;
    PdfViewerOpenCallbacks callbacks;
    quint64 lastId = 0;
    bool active = false;
    bool fallbackAvailable = false;
    int fallbackOpenCount = 0;
};

} // namespace

class PdfViewerAdapterTest final : public QObject {
    Q_OBJECT

private slots:
    void capturesRectangleWithViewerOwnedCoordinatesAndGeometry();
    void rejectsCanceledTimedOutCrossPageAndChangedSessions();
    void capturesSelectedTextThroughAdapter();
    void opensAndVerifiesPageWithoutBlockingCaller();
    void newerOpenSupersedesOlderRequest();
    void hungNavigationTimesOutWithoutBlockingGui();
    void openServiceRoutesPdfThroughAdapterAndRecordsOnlySuccess();
};

void PdfViewerAdapterTest::capturesRectangleWithViewerOwnedCoordinatesAndGeometry()
{
    InMemoryLibraryRepository repository;
    SumatraPdfViewerAdapterOptions options = captureOptions();
    int activationCount = 0;
    int stateChecks = 0;
    int mouseCalls = 0;
    int observedStateTimeout = 0;
    QRect observedTarget;
    int observedSelectionTimeout = 0;
    int observedMinimumPixels = 0;
    QString observedSessionFailure;
    options.activateWindowHandler = [&](quintptr handle) {
        QCOMPARE(handle, quintptr(701));
        ++activationCount;
    };
    options.navigationStateProvider = [&](int timeoutMilliseconds)
        -> SumatraPdfDdeFileState {
        observedStateTimeout = timeoutMilliseconds;
        ++stateChecks;
        return fileState();
    };
    options.regionSelectionHandler = [&](
                                         const QRect &target,
                                         PdfRegionSelectionOverlay::TargetStateProvider stateProvider,
                                         QWidget *, int timeout, int minimumPixels)
        -> PdfRegionSelectionResult {
        observedTarget = target;
        observedSelectionTimeout = timeout;
        observedMinimumPixels = minimumPixels;
        observedSessionFailure = stateProvider();
        PdfRegionSelectionResult result;
        result.state = PdfRegionSelectionState::Selected;
        result.screenRect = QRect(QPoint(120, 160), QPoint(220, 240));
        return result;
    };
    options.mousePositionProvider = [&mouseCalls](const QPoint &, int) {
        SumatraPdfDdeMousePosition position;
        position.page = 3;
        if (mouseCalls++ == 0) {
            position.x = 10;
            position.y = 20;
        } else {
            position.x = 110;
            position.y = 80;
        }
        return position;
    };

    SumatraPdfViewerAdapter adapter(repository, options);
    PdfViewerCaptureRequest request;
    request.context = pdfContext();
    const PdfViewerCaptureResult result = adapter.captureRectangle(request);

    QVERIFY2(result.succeeded(), qPrintable(result.message));
    QCOMPARE(result.observation.adapterId, QStringLiteral("sumatrapdf"));
    QCOMPARE(result.observation.documentPath, QString::fromLatin1(PdfPath));
    QCOMPARE(result.observation.page, 3);
    QCOMPARE(result.observation.rotation, 90);
    QCOMPARE(result.observation.userUnit, 1.5);
    QCOMPARE(result.anchorRequest.rect.left, 10.0);
    QCOMPARE(result.anchorRequest.rect.top, 20.0);
    QCOMPARE(result.anchorRequest.rect.right, 110.0);
    QCOMPARE(result.anchorRequest.rect.bottom, 80.0);
    QCOMPARE(result.anchorRequest.mediaBox.right, 612.0);
    QCOMPARE(result.anchorRequest.cropBox.left, 18.0);
    QCOMPARE(result.anchorRequest.cropBox.bottom, 768.0);
    QCOMPARE(result.anchorRequest.source,
             QStringLiteral("sumatrapdf-adapter-region"));
    QCOMPARE(observedTarget, QRect(100, 120, 900, 700));
    QVERIFY(observedSelectionTimeout > 0);
    QCOMPARE(observedMinimumPixels, 4);
    QVERIFY(observedSessionFailure.isEmpty());
    QVERIFY(observedStateTimeout > 0);
    QCOMPARE(activationCount, 2);
    QVERIFY(stateChecks >= 2);
    QCOMPARE(mouseCalls, 2);

    const AnchorCaptureDraft draft =
        anchorCaptureDraftFromPdfRequest(result.anchorRequest);
    const QJsonObject locator =
        QJsonDocument::fromJson(draft.locatorJson.toUtf8()).object();
    QCOMPARE(locator.value(QStringLiteral("version")).toInt(), 3);
    QCOMPARE(locator.value(QStringLiteral("rotation")).toInt(), 90);
    QCOMPARE(locator.value(QStringLiteral("userUnit")).toDouble(), 1.5);
    QCOMPARE(locator.value(QStringLiteral("mediaBox")).toArray().size(), 4);
    QCOMPARE(locator.value(QStringLiteral("cropBox")).toArray().size(), 4);
    QVERIFY(!locator.contains(QStringLiteral("zoom")));
}

void PdfViewerAdapterTest::rejectsCanceledTimedOutCrossPageAndChangedSessions()
{
    InMemoryLibraryRepository repository;
    PdfViewerCaptureRequest request;
    request.context = pdfContext();

    {
        SumatraPdfViewerAdapterOptions options = captureOptions();
        options.regionSelectionHandler = [](
                                             const QRect &,
                                             PdfRegionSelectionOverlay::TargetStateProvider,
                                             QWidget *, int, int) {
            PdfRegionSelectionResult result;
            result.state = PdfRegionSelectionState::Canceled;
            result.diagnostics = QStringLiteral("selection canceled");
            return result;
        };
        SumatraPdfViewerAdapter adapter(repository, options);
        const PdfViewerCaptureResult result = adapter.captureRectangle(request);
        QVERIFY(result.canceled());
        QCOMPARE(result.message, QStringLiteral("selection canceled"));
    }

    {
        SumatraPdfViewerAdapterOptions options = captureOptions();
        options.regionSelectionHandler = [](
                                             const QRect &,
                                             PdfRegionSelectionOverlay::TargetStateProvider,
                                             QWidget *, int, int) {
            PdfRegionSelectionResult result;
            result.state = PdfRegionSelectionState::TimedOut;
            result.diagnostics = QStringLiteral("selection timed out");
            return result;
        };
        SumatraPdfViewerAdapter adapter(repository, options);
        const PdfViewerCaptureResult result = adapter.captureRectangle(request);
        QVERIFY(result.timedOut());
        QCOMPARE(result.message, QStringLiteral("selection timed out"));
    }

    {
        SumatraPdfViewerAdapterOptions options = captureOptions();
        options.regionSelectionHandler = [](
                                             const QRect &,
                                             PdfRegionSelectionOverlay::TargetStateProvider,
                                             QWidget *, int, int) {
            PdfRegionSelectionResult result;
            result.state = PdfRegionSelectionState::Selected;
            result.screenRect = QRect(120, 160, 100, 80);
            return result;
        };
        int mouseCalls = 0;
        options.mousePositionProvider = [&mouseCalls](const QPoint &, int) {
            SumatraPdfDdeMousePosition position;
            position.page = ++mouseCalls == 1 ? 3 : 4;
            position.x = mouseCalls == 1 ? 10 : 110;
            position.y = mouseCalls == 1 ? 20 : 80;
            return position;
        };
        SumatraPdfViewerAdapter adapter(repository, options);
        const PdfViewerCaptureResult result = adapter.captureRectangle(request);
        QVERIFY(!result.succeeded());
        QVERIFY(result.message.contains(QStringLiteral("one page"),
                                        Qt::CaseInsensitive));
    }

    {
        SumatraPdfViewerAdapterOptions options = captureOptions();
        options.regionSelectionHandler = [](
                                             const QRect &,
                                             PdfRegionSelectionOverlay::TargetStateProvider,
                                             QWidget *, int, int) {
            PdfRegionSelectionResult result;
            result.state = PdfRegionSelectionState::Selected;
            result.screenRect = QRect(120, 160, 100, 80);
            return result;
        };
        options.navigationStateProvider = [](int) {
            return fileState(3, QStringLiteral("E:/docs/switched.pdf"));
        };
        SumatraPdfViewerAdapter adapter(repository, options);
        const PdfViewerCaptureResult result = adapter.captureRectangle(request);
        QVERIFY(result.canceled());
        QVERIFY(result.message.contains(QStringLiteral("changed"),
                                        Qt::CaseInsensitive));
    }

    {
        SumatraPdfViewerAdapterOptions options = captureOptions();
        options.windowExistsProvider = [](quintptr) { return false; };
        SumatraPdfViewerAdapter adapter(repository, options);
        const PdfViewerCaptureResult result = adapter.captureRectangle(request);
        QVERIFY(result.canceled());
        QVERIFY(result.message.contains(QStringLiteral("closed"),
                                        Qt::CaseInsensitive));
    }
}

void PdfViewerAdapterTest::capturesSelectedTextThroughAdapter()
{
    InMemoryLibraryRepository repository;
    SumatraPdfViewerAdapterOptions options = captureOptions();
    options.textSelectionProvider = [](const ForegroundAppWindowContext &context,
                                       const ForegroundTextTarget &target) {
        TextSelectionCaptureResult result;
        result.state = TextSelectionState::TextSelected;
        result.text = QStringLiteral("  selected   PDF text  ");
        result.context = context;
        result.target = target;
        result.source = QStringLiteral("test-selection");
        return result;
    };
    SumatraPdfViewerAdapter adapter(repository, options);
    PdfViewerCaptureRequest request;
    request.context = pdfContext();
    request.textTarget.windowHandle = 701;
    request.textTarget.focusHandle = 703;

    const PdfViewerCaptureResult result = adapter.captureText(request);

    QVERIFY2(result.succeeded(), qPrintable(result.message));
    QCOMPARE(result.anchorRequest.locatorType,
             QStringLiteral("sumatrapdf.search"));
    QCOMPARE(result.anchorRequest.searchText,
             QStringLiteral("selected PDF text"));
    QCOMPARE(result.anchorRequest.source,
             QStringLiteral("sumatrapdf-adapter-selection"));
    QCOMPARE(result.anchorRequest.rotation, 90);
    QCOMPARE(result.textSelection.source,
             QStringLiteral("sumatrapdf-adapter-selection"));
}

void PdfViewerAdapterTest::opensAndVerifiesPageWithoutBlockingCaller()
{
    InMemoryLibraryRepository repository;
    SumatraPdfViewerAdapterOptions options;
    options.executablePathProvider = []() {
        return QStringLiteral("C:/Tools/SumatraPDF.exe");
    };
    QList<SumatraPdfCommand> launches;
    options.launchHandler = [&launches](const SumatraPdfCommand &command,
                                        QString *) {
        launches.append(command);
        return true;
    };
    options.navigationStateProvider = [](int) { return fileState(3); };
    options.navigationPollMilliseconds = 20;
    options.navigationTimeoutMilliseconds = 800;
    SumatraPdfViewerAdapter adapter(repository, options);
    QList<PdfViewerOpenResult> results;
    PdfViewerOpenCallbacks callbacks;
    callbacks.completed = [&results](const PdfViewerOpenResult &result) {
        results.append(result);
    };

    QElapsedTimer elapsed;
    elapsed.start();
    const PdfViewerOpenStartResult start = adapter.open(openRequest(3), callbacks);
    QVERIFY2(start.accepted(), qPrintable(start.error));
    QVERIFY(elapsed.elapsed() < 100);
    QCOMPARE(adapter.activeOperationCount(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 1500);
    QVERIFY2(results.first().succeeded(), qPrintable(results.first().diagnostics));
    QCOMPARE(adapter.activeOperationCount(), 0);
    QCOMPARE(launches.size(), 1);
    QCOMPARE(launches.first().page, 3);
    QVERIFY(!launches.first().arguments.contains(QStringLiteral("-zoom")));
}

void PdfViewerAdapterTest::newerOpenSupersedesOlderRequest()
{
    InMemoryLibraryRepository repository;
    SumatraPdfViewerAdapterOptions options;
    options.executablePathProvider = []() {
        return QStringLiteral("C:/Tools/SumatraPDF.exe");
    };
    options.launchHandler = [](const SumatraPdfCommand &, QString *) {
        return true;
    };
    options.navigationStateProvider = [](int) { return fileState(2); };
    options.navigationPollMilliseconds = 20;
    options.navigationTimeoutMilliseconds = 800;
    SumatraPdfViewerAdapter adapter(repository, options);
    QList<PdfViewerOpenResult> firstResults;
    QList<PdfViewerOpenResult> secondResults;
    PdfViewerOpenCallbacks firstCallbacks;
    firstCallbacks.completed = [&firstResults](const PdfViewerOpenResult &result) {
        firstResults.append(result);
    };
    PdfViewerOpenCallbacks secondCallbacks;
    secondCallbacks.completed = [&secondResults](const PdfViewerOpenResult &result) {
        secondResults.append(result);
    };

    const PdfViewerOpenStartResult first = adapter.open(openRequest(1), firstCallbacks);
    QVERIFY(first.accepted());
    const PdfViewerOpenStartResult second = adapter.open(openRequest(2), secondCallbacks);
    QVERIFY(second.accepted());
    QVERIFY(second.requestId > first.requestId);
    QCOMPARE(firstResults.size(), 1);
    QVERIFY(firstResults.first().canceled());
    QTRY_COMPARE_WITH_TIMEOUT(secondResults.size(), 1, 1500);
    QVERIFY(secondResults.first().succeeded());
    QCOMPARE(secondResults.first().page, 2);
}

void PdfViewerAdapterTest::hungNavigationTimesOutWithoutBlockingGui()
{
    InMemoryLibraryRepository repository;
    SumatraPdfViewerAdapterOptions options;
    options.executablePathProvider = []() {
        return QStringLiteral("C:/Tools/SumatraPDF.exe");
    };
    options.launchHandler = [](const SumatraPdfCommand &, QString *) {
        return true;
    };
    options.navigationStateProvider = [](int) {
        QThread::msleep(900);
        SumatraPdfDdeFileState state;
        state.error = QStringLiteral("simulated hung viewer IPC");
        return state;
    };
    options.navigationStateProviderRunsInWorker = true;
    options.navigationPollMilliseconds = 20;
    options.navigationTimeoutMilliseconds = 240;
    SumatraPdfViewerAdapter adapter(repository, options);
    QList<PdfViewerOpenResult> results;
    PdfViewerOpenCallbacks callbacks;
    callbacks.completed = [&results](const PdfViewerOpenResult &result) {
        results.append(result);
    };

    qint64 guiTimerElapsed = -1;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer::singleShot(50, &adapter, [&]() {
        guiTimerElapsed = elapsed.elapsed();
    });
    QVERIFY(adapter.open(openRequest(3), callbacks).accepted());
    QTRY_VERIFY_WITH_TIMEOUT(guiTimerElapsed >= 0, 500);
    QVERIFY2(guiTimerElapsed < 180,
             qPrintable(QStringLiteral("GUI timer delayed by %1 ms")
                            .arg(guiTimerElapsed)));
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), 1, 800);
    QVERIFY(results.first().timedOut());
    QCOMPARE(adapter.activeOperationCount(), 0);
    QVERIFY(elapsed.elapsed() < 800);
}

void PdfViewerAdapterTest::openServiceRoutesPdfThroughAdapterAndRecordsOnlySuccess()
{
    InMemoryLibraryRepository repository;
    Resource resource;
    resource.id = QStringLiteral("adapter-service-resource");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Adapter service report");
    resource.location = QString::fromLatin1(PdfPath);
    resource.anchors = {pageAnchor(3)};
    QVERIFY(repository.upsertResource(resource));

    RecordingAdapter adapter;
    PinloomOpenServiceOptions options;
    options.pdfViewerAdapter = &adapter;
    PinloomOpenService service(repository, options);
    PinloomOpenTarget target;
    target.resourceId = resource.id;
    target.resourceKind = resource.kind;
    target.title = resource.title;
    target.location = resource.location;
    target.anchor = resource.anchors.first();

    QVERIFY(service.open(target));
    QCOMPARE(adapter.lastRequest.anchor.id, resource.anchors.first().id);
    QCOMPARE(service.statusText(), QStringLiteral("Opening PDF target"));
    QVERIFY(!repository.resourceUsage(resource.id).has_value());
    adapter.complete(PdfViewerOperationState::Succeeded,
                     QStringLiteral("adapter navigation verified"));
    QTRY_COMPARE_WITH_TIMEOUT(service.statusText(),
                              QStringLiteral("adapter navigation verified"),
                              500);
    QVERIFY(repository.resourceUsage(resource.id).has_value());
    QCOMPARE(repository.resourceUsage(resource.id)->openCount, 1);

    QVERIFY(service.open(target));
    adapter.complete(PdfViewerOperationState::TimedOut,
                     QStringLiteral("adapter navigation timed out"));
    QTRY_COMPARE_WITH_TIMEOUT(service.statusText(),
                              QStringLiteral("adapter navigation timed out"),
                              500);
    QCOMPARE(repository.resourceUsage(resource.id)->openCount, 1);
}

QTEST_MAIN(PdfViewerAdapterTest)

#include "pdf_viewer_adapter_test.moc"
