#include "pinloom/widgets/SumatraPdfViewerAdapter.h"

#include "pinloom/core/SumatraPdfCommand.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QProcess>
#include <QSharedPointer>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Pinloom {

namespace {

QString normalizedPdfPath(QString path)
{
    path = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}

bool samePdfPath(const QString &left, const QString &right)
{
    return !left.trimmed().isEmpty()
        && normalizedPdfPath(left) == normalizedPdfPath(right);
}

PdfCaptureRect captureRect(const QRectF &rect)
{
    return {rect.left(), rect.top(), rect.right(), rect.bottom()};
}

QRect defaultWindowGeometry(quintptr windowHandle)
{
#ifdef Q_OS_WIN
    HWND window = reinterpret_cast<HWND>(windowHandle);
    RECT client{};
    POINT topLeft{};
    if (!window || !IsWindow(window)
        || !GetClientRect(window, &client)
        || !ClientToScreen(window, &topLeft)) {
        return {};
    }
    return QRect(topLeft.x,
                 topLeft.y,
                 client.right - client.left,
                 client.bottom - client.top);
#else
    if (windowHandle == 0) return {};
    std::unique_ptr<QWindow> window(
        QWindow::fromWinId(static_cast<WId>(windowHandle)));
    return window ? window->geometry() : QRect();
#endif
}

bool defaultWindowExists(quintptr windowHandle)
{
#ifdef Q_OS_WIN
    return windowHandle != 0
        && IsWindow(reinterpret_cast<HWND>(windowHandle));
#else
    return windowHandle != 0;
#endif
}

void defaultActivateWindow(quintptr windowHandle)
{
#ifdef Q_OS_WIN
    HWND window = reinterpret_cast<HWND>(windowHandle);
    if (!window || !IsWindow(window)) return;
    if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
    BringWindowToTop(window);
    SetForegroundWindow(window);
#else
    Q_UNUSED(windowHandle)
#endif
}

SumatraPdfDdeMousePosition defaultMousePositionAt(
    const QPoint &globalPosition,
    int timeoutMilliseconds)
{
    SumatraPdfDdeMousePosition result;
    const QPoint original = QCursor::pos();
    for (int attempt = 0; attempt < 3; ++attempt) {
        QCursor::setPos(globalPosition);
        QThread::msleep(static_cast<unsigned long>(35 + attempt * 20));
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        result = requestSumatraPdfDdeMousePosition(
            std::clamp(timeoutMilliseconds, 40, 1000));
        if (result.success()) break;
    }
    QCursor::setPos(original);
    return result;
}

PdfRegionSelectionResult defaultRegionSelection(
    const QRect &targetScreenGeometry,
    PdfRegionSelectionOverlay::TargetStateProvider targetStateProvider,
    QWidget *parent,
    int timeoutMilliseconds,
    int minimumSelectionPixels)
{
    PdfRegionSelectionOverlay overlay(targetScreenGeometry,
                                      std::move(targetStateProvider),
                                      parent,
                                      timeoutMilliseconds,
                                      minimumSelectionPixels);
    overlay.exec();
    return overlay.selectionResult();
}

SumatraPdfCommand pageOnlyCommand(const SumatraPdfCommand &source)
{
    SumatraPdfCommand command;
    command.executablePath = source.executablePath;
    command.filePath = source.filePath;
    command.page = source.page;
    command.arguments = {
        QStringLiteral("-reuse-instance"),
        QStringLiteral("-page"),
        QString::number(source.page),
        source.filePath,
    };
    return command;
}

QString stateMessage(PdfViewerOperationState state,
                     const QString &fallback)
{
    switch (state) {
    case PdfViewerOperationState::Succeeded:
        return QStringLiteral("PDF operation completed");
    case PdfViewerOperationState::Canceled:
        return QStringLiteral("PDF operation canceled");
    case PdfViewerOperationState::TimedOut:
        return QStringLiteral("PDF operation timed out");
    case PdfViewerOperationState::Failed:
        return fallback;
    }
    return fallback;
}

} // namespace

bool PdfViewerObservation::isValid() const
{
    return !adapterId.trimmed().isEmpty()
        && !documentIdentity.trimmed().isEmpty()
        && !documentPath.trimmed().isEmpty()
        && page > 0;
}

bool PdfViewerCaptureResult::succeeded() const
{
    return state == PdfViewerOperationState::Succeeded;
}

bool PdfViewerCaptureResult::canceled() const
{
    return state == PdfViewerOperationState::Canceled;
}

bool PdfViewerCaptureResult::timedOut() const
{
    return state == PdfViewerOperationState::TimedOut;
}

bool PdfViewerOpenResult::succeeded() const
{
    return state == PdfViewerOperationState::Succeeded;
}

bool PdfViewerOpenResult::canceled() const
{
    return state == PdfViewerOperationState::Canceled;
}

bool PdfViewerOpenResult::timedOut() const
{
    return state == PdfViewerOperationState::TimedOut;
}

bool PdfViewerOpenStartResult::accepted() const
{
    return requestId != 0 && error.isEmpty();
}

PdfViewerAdapter::PdfViewerAdapter(QObject *parent)
    : QObject(parent)
{
}

PdfViewerAdapter::~PdfViewerAdapter() = default;

bool sumatraPdfJumpMatches(const SumatraPdfDdeFileState &state,
                           const SumatraPdfCommand &command,
                           QString *diagnostics)
{
    if (diagnostics) diagnostics->clear();
    if (!state.success()) {
        if (diagnostics) {
            *diagnostics = state.error.trimmed().isEmpty()
                ? QStringLiteral("SumatraPDF DDE state is unavailable")
                : state.error.trimmed();
        }
        return false;
    }
    if (!samePdfPath(state.path, command.filePath)) {
        if (diagnostics) {
            *diagnostics = QStringLiteral("active PDF changed to %1")
                               .arg(state.path);
        }
        return false;
    }
    if (state.page != command.page) {
        if (diagnostics) {
            *diagnostics = QStringLiteral("active page is %1; expected %2")
                               .arg(state.page)
                               .arg(command.page);
        }
        return false;
    }
    return true;
}

class SumatraPdfViewerAdapter::Private {
public:
    struct OpenContext {
        quint64 id = 0;
        PdfViewerOpenRequest request;
        PdfViewerOpenCallbacks callbacks;
        SumatraPdfCommand command;
        QElapsedTimer timer;
        int consecutiveMatches = 0;
        QString lastDiagnostics;
        bool finished = false;
    };

    Private(SumatraPdfViewerAdapter *owner,
            ILibraryRepository &libraryRepository,
            SumatraPdfViewerAdapterOptions adapterOptions)
        : q(owner)
        , repository(libraryRepository)
        , options(std::move(adapterOptions))
    {
        if (!options.foregroundContextProvider) {
            options.foregroundContextProvider = currentForegroundAppWindowContext;
        }
        if (!options.viewStateProvider) {
            options.viewStateProvider = captureSumatraPdfViewState;
        }
        if (!options.windowGeometryProvider) {
            options.windowGeometryProvider = defaultWindowGeometry;
        }
        if (!options.windowExistsProvider) {
            options.windowExistsProvider = defaultWindowExists;
        }
        if (!options.activateWindowHandler) {
            options.activateWindowHandler = defaultActivateWindow;
        }
        if (!options.regionSelectionHandler) {
            options.regionSelectionHandler = defaultRegionSelection;
        }
        if (!options.mousePositionProvider) {
            options.mousePositionProvider = defaultMousePositionAt;
        }
        if (!options.textTargetProvider) {
            options.textTargetProvider = captureForegroundTextTarget;
        }
        if (!options.textSelectionProvider) {
            options.textSelectionProvider = captureTextSelectionFromTarget;
        }
        if (!options.pageGeometryProvider) {
            options.pageGeometryProvider = inspectPdfPageGeometry;
        }

        presenter = options.pdfAnchorPresenter;
        if (!presenter) {
            SumatraAnnotatedCopyPresenterOptions presenterOptions;
            presenterOptions.launchHandler = options.launchHandler;
            presenterOptions.stateProvider = options.navigationStateProvider;
            presenterOptions.stateProviderRunsInWorker =
                options.navigationStateProviderRunsInWorker;
            presenterOptions.verificationTimeoutMilliseconds =
                options.navigationTimeoutMilliseconds;
            presenterOptions.verificationPollMilliseconds =
                options.navigationPollMilliseconds;
            presenterOptions.generationTimeoutMilliseconds =
                options.presentationGenerationTimeoutMilliseconds;
            ownedPresenter = std::make_unique<SumatraAnnotatedCopyPresenter>(
                std::move(presenterOptions), q);
            presenter = ownedPresenter.get();
        }
    }

    QString executablePath() const
    {
        return options.executablePathProvider
            ? options.executablePathProvider().trimmed()
            : resolveSumatraPdfExecutablePath(
                  options.applicationLaunchSettings);
    }

    SumatraPdfDdeFileState navigationState(int timeoutMilliseconds) const
    {
        return options.navigationStateProvider
            ? options.navigationStateProvider(timeoutMilliseconds)
            : requestSumatraPdfDdeFileState(timeoutMilliseconds);
    }

    PdfViewerCaptureResult resolveCapture(
        const PdfViewerCaptureRequest &request) const
    {
        PdfViewerCaptureResult result;
        const ForegroundAppWindowContext context = request.context.isValid()
            ? request.context
            : options.foregroundContextProvider();
        if (!isSumatraPdfForegroundWindow(context)) {
            result.message = QStringLiteral(
                "Open or focus a SumatraPDF PDF before capture");
            result.diagnostics = QStringLiteral(
                "The active window is not supported by the SumatraPDF adapter");
            return result;
        }

        SumatraPdfForegroundCaptureProvider provider(repository,
                                                     options.viewStateProvider);
        SumatraPdfForegroundCaptureResult captured = provider.capture(context);
        if (!captured.success() && captured.needsFileConfirmation) {
            if (!options.fileConfirmationHandler) {
                result.message = captured.status;
                result.diagnostics = QStringLiteral(
                    "PDF identity requires explicit file confirmation");
                return result;
            }
            const QString selected = options.fileConfirmationHandler(
                captured.documentTitle, request.parent).trimmed();
            if (selected.isEmpty()) {
                result.state = PdfViewerOperationState::Canceled;
                result.message = QStringLiteral("PDF file confirmation canceled");
                return result;
            }
            captured = sumatraPdfForegroundCaptureResultForConfirmedPdfFile(
                captured.documentTitle, selected, captured.viewState);
        }
        if (!captured.success()) {
            result.message = captured.status.trimmed().isEmpty()
                ? QStringLiteral("Unable to resolve the active PDF document")
                : captured.status.trimmed();
            return result;
        }

        result.observation.adapterId = QStringLiteral("sumatrapdf");
        result.observation.context = context;
        result.observation.documentPath = captured.request.file.trimmed();
        result.observation.documentIdentity = normalizedPdfPath(
            result.observation.documentPath);
        result.observation.documentTitle = captured.documentTitle;
        result.observation.page = captured.request.page;
        result.observation.pageCount = captured.viewState.totalPages;
        result.observation.zoom = captured.viewState.zoom;
        result.observation.windowHandle = context.windowHandle;
        result.observation.provenance = captured.request.source;
        result.anchorRequest = captured.request;
        result.anchorRequest.documentIdentity =
            result.observation.documentIdentity;
        result.anchorRequest.adapterId = QStringLiteral("sumatrapdf");
        result.state = PdfViewerOperationState::Succeeded;
        return result;
    }

    QString validateCaptureSession(const PdfViewerObservation &observation) const
    {
        if (!options.windowExistsProvider(observation.windowHandle)) {
            return QStringLiteral("PDF viewer closed during capture");
        }
        const SumatraPdfDdeFileState state = navigationState(100);
        if (!state.success()) {
            return state.error.trimmed().isEmpty()
                ? QStringLiteral("PDF viewer state became unavailable during capture")
                : QStringLiteral("PDF viewer state became unavailable: %1")
                      .arg(state.error.trimmed());
        }
        if (!samePdfPath(state.path, observation.documentPath)) {
            return QStringLiteral("Active PDF changed during capture");
        }
        return {};
    }

    bool attachPageGeometry(PdfViewerCaptureResult *result) const
    {
        const PdfPageGeometryResult geometry = options.pageGeometryProvider(
            result->observation.documentPath,
            result->anchorRequest.page);
        if (!geometry.success()) {
            result->state = PdfViewerOperationState::Failed;
            result->message = QStringLiteral("Unable to read PDF page geometry");
            result->diagnostics = geometry.error;
            return false;
        }
        result->observation.mediaBox = captureRect(geometry.geometry.mediaBox);
        result->observation.cropBox = captureRect(geometry.geometry.cropBox);
        result->observation.rotation = geometry.geometry.rotation;
        result->observation.userUnit = geometry.geometry.userUnit;
        result->anchorRequest.mediaBox = result->observation.mediaBox;
        result->anchorRequest.cropBox = result->observation.cropBox;
        result->anchorRequest.rotation = result->observation.rotation;
        result->anchorRequest.userUnit = result->observation.userUnit;
        return true;
    }

    bool launch(const SumatraPdfCommand &command, QString *error) const
    {
        if (options.launchHandler) return options.launchHandler(command, error);
        const QFileInfo executable(command.executablePath);
        if (!executable.exists() || !executable.isFile()) {
            if (error) *error = QStringLiteral(
                "SumatraPDF executable is not configured/found");
            return false;
        }
        return QProcess::startDetached(command.executablePath,
                                       command.arguments);
    }

    PdfViewerOpenStartResult open(const PdfViewerOpenRequest &request,
                                  PdfViewerOpenCallbacks callbacks)
    {
        cancelPending();
        PdfViewerOpenStartResult start;
        const SumatraPdfCommandResult commandResult =
            buildSumatraPdfCommand(request.anchor,
                                   request.fallbackFilePath,
                                   executablePath());
        if (!commandResult.success()) {
            start.error = commandResult.error;
            return start;
        }

        const auto context = QSharedPointer<OpenContext>::create();
        context->id = ++lastRequestId;
        context->request = request;
        context->callbacks = std::move(callbacks);
        context->command = commandResult.command;
        current = context;
        start.requestId = context->id;

        if (context->command.highlightRect.isValid()) {
            PdfAnchorPresentationRequest presentation;
            presentation.anchor = request.anchor;
            presentation.sourceTitle = request.sourceTitle;
            presentation.cacheDirectory = request.cacheDirectory.trimmed().isEmpty()
                ? options.presentationCacheDirectory
                : request.cacheDirectory;
            presentation.sourceCommand = context->command;
            PdfAnchorPresentationCallbacks presenterCallbacks;
            const QPointer<SumatraPdfViewerAdapter> guard(q);
            presenterCallbacks.statusChanged =
                [guard, id = context->id](quint64, const QString &status) {
                    if (!guard) return;
                    guard->d_->reportStatus(id, status);
                };
            presenterCallbacks.completed =
                [guard, id = context->id](
                    const PdfAnchorPresentationResult &presentationResult) {
                    if (!guard) return;
                    guard->d_->handlePresentation(id, presentationResult);
                };
            const PdfAnchorPresentationStartResult presenterStart =
                presenter->present(presentation, std::move(presenterCallbacks));
            if (!presenterStart.accepted()) {
                current.reset();
                start.requestId = 0;
                start.error = presenterStart.error;
            }
            return start;
        }

        QString error;
        if (!launch(context->command, &error)) {
            current.reset();
            start.requestId = 0;
            start.error = error.trimmed().isEmpty()
                ? QStringLiteral("Unable to launch SumatraPDF")
                : error.trimmed();
            return start;
        }

        context->timer.start();
        reportStatus(context->id,
                     QStringLiteral("Opening PDF target; verifying navigation"));
        if (options.launchHandler && !options.navigationStateProvider) {
            const QPointer<SumatraPdfViewerAdapter> guard(q);
            QTimer::singleShot(0, q, [guard, id = context->id]() {
                if (!guard) return;
                guard->d_->finishSuccess(id,
                    QStringLiteral("Opened PDF target"));
            });
            return start;
        }

        const int timeout = std::clamp(
            options.navigationTimeoutMilliseconds, 200, 30000);
        const QPointer<SumatraPdfViewerAdapter> guard(q);
        QTimer::singleShot(timeout, q, [guard, id = context->id]() {
            if (!guard) return;
            guard->d_->finishTimeout(id);
        });
        schedulePoll(context->id);
        return start;
    }

    void schedulePoll(quint64 id)
    {
        const int delay = std::clamp(
            options.navigationPollMilliseconds, 20, 1000);
        const QPointer<SumatraPdfViewerAdapter> guard(q);
        QTimer::singleShot(delay, q, [guard, id]() {
            if (!guard) return;
            guard->d_->poll(id);
        });
    }

    void poll(quint64 id)
    {
        if (!current || current->id != id || current->finished) return;
        if (!options.navigationStateProviderRunsInWorker) {
            handleState(id, navigationState(140));
            return;
        }
        auto *watcher = new QFutureWatcher<SumatraPdfDdeFileState>(q);
        const QPointer<SumatraPdfViewerAdapter> guard(q);
        QObject::connect(watcher,
                         &QFutureWatcher<SumatraPdfDdeFileState>::finished,
                         watcher,
                         [guard, watcher, id]() {
            const SumatraPdfDdeFileState state = watcher->result();
            watcher->deleteLater();
            if (!guard) return;
            guard->d_->handleState(id, state);
        });
        const auto provider = options.navigationStateProvider;
        watcher->setFuture(QtConcurrent::run([provider]() {
            return provider
                ? provider(140)
                : requestSumatraPdfDdeFileState(140);
        }));
    }

    void handleState(quint64 id, const SumatraPdfDdeFileState &state)
    {
        if (!current || current->id != id || current->finished) return;
        QString diagnostics;
        if (sumatraPdfJumpMatches(state, current->command, &diagnostics)) {
            ++current->consecutiveMatches;
            if (current->consecutiveMatches >= 2) {
                finishSuccess(id,
                    QStringLiteral("Opened PDF target; navigation verified"));
                return;
            }
        } else {
            current->consecutiveMatches = 0;
            current->lastDiagnostics = diagnostics;
        }
        schedulePoll(id);
    }

    void handlePresentation(
        quint64 id,
        const PdfAnchorPresentationResult &presentation)
    {
        if (!current || current->id != id || current->finished) return;
        if (presentation.superseded) {
            finish(id,
                   PdfViewerOperationState::Canceled,
                   QStringLiteral("PDF presentation superseded"),
                   {},
                   false);
            return;
        }
        if (presentation.success()) {
            finishSuccess(id,
                QStringLiteral("Opened PDF Anchor in Pinloom Preview"));
            return;
        }
        originalFallback = pageOnlyCommand(current->command);
        finish(id,
               PdfViewerOperationState::Failed,
               QStringLiteral("Pinloom Preview failed"),
               presentation.error,
               true);
    }

    void finishSuccess(quint64 id, const QString &message)
    {
        finish(id, PdfViewerOperationState::Succeeded, message, {}, false);
    }

    void finishTimeout(quint64 id)
    {
        if (!current || current->id != id || current->finished) return;
        const QString diagnostics = current->lastDiagnostics.trimmed().isEmpty()
            ? QStringLiteral("viewer state did not reach the requested page")
            : current->lastDiagnostics;
        finish(id,
               PdfViewerOperationState::TimedOut,
               QStringLiteral("PDF navigation timed out"),
               diagnostics,
               false);
    }

    void finish(quint64 id,
                PdfViewerOperationState state,
                const QString &message,
                const QString &diagnostics,
                bool fallbackAvailable)
    {
        if (!current || current->id != id || current->finished) return;
        const auto completed = current;
        completed->finished = true;
        current.reset();
        PdfViewerOpenResult result;
        result.requestId = id;
        result.state = state;
        result.sourceFilePath = completed->command.filePath;
        result.page = completed->command.page;
        result.message = message.trimmed().isEmpty()
            ? stateMessage(state, QStringLiteral("PDF operation failed"))
            : message.trimmed();
        result.diagnostics = diagnostics.trimmed();
        result.originalFallbackAvailable = fallbackAvailable;
        if (completed->callbacks.completed) {
            completed->callbacks.completed(result);
        }
    }

    void reportStatus(quint64 id, const QString &status)
    {
        if (!current || current->id != id || current->finished) return;
        if (current->callbacks.statusChanged) {
            current->callbacks.statusChanged(id, status);
        }
    }

    void cancelPending()
    {
        if (presenter) presenter->cancelPending();
        if (!current || current->finished) return;
        const quint64 id = current->id;
        finish(id,
               PdfViewerOperationState::Canceled,
               QStringLiteral("PDF operation superseded"),
               {},
               false);
    }

    SumatraPdfViewerAdapter *q = nullptr;
    ILibraryRepository &repository;
    SumatraPdfViewerAdapterOptions options;
    std::unique_ptr<PdfAnchorPresenter> ownedPresenter;
    PdfAnchorPresenter *presenter = nullptr;
    quint64 lastRequestId = 0;
    QSharedPointer<OpenContext> current;
    std::optional<SumatraPdfCommand> originalFallback;
};

SumatraPdfViewerAdapter::SumatraPdfViewerAdapter(
    ILibraryRepository &repository,
    SumatraPdfViewerAdapterOptions options,
    QObject *parent)
    : PdfViewerAdapter(parent)
    , d_(std::make_unique<Private>(this,
                                   repository,
                                   std::move(options)))
{
}

SumatraPdfViewerAdapter::~SumatraPdfViewerAdapter()
{
    d_->cancelPending();
}

QString SumatraPdfViewerAdapter::adapterId() const
{
    return QStringLiteral("sumatrapdf");
}

bool SumatraPdfViewerAdapter::supportsContext(
    const ForegroundAppWindowContext &context) const
{
    return isSumatraPdfForegroundWindow(context);
}

bool SumatraPdfViewerAdapter::supportsAnchor(const Anchor &anchor) const
{
    return isSumatraPdfAnchor(anchor);
}

PdfViewerCaptureResult SumatraPdfViewerAdapter::captureRectangle(
    const PdfViewerCaptureRequest &request)
{
    PdfViewerCaptureResult result = d_->resolveCapture(request);
    if (!result.succeeded()) return result;
    if (result.observation.windowHandle == 0
        || !d_->options.windowExistsProvider(result.observation.windowHandle)) {
        result.state = PdfViewerOperationState::Canceled;
        result.message = QStringLiteral("PDF viewer closed before region capture");
        return result;
    }
    const QRect geometry = d_->options.windowGeometryProvider(
        result.observation.windowHandle);
    if (!geometry.isValid()) {
        result.state = PdfViewerOperationState::Failed;
        result.message = QStringLiteral("PDF viewer window geometry is unavailable");
        return result;
    }

    const PdfViewerObservation observation = result.observation;
    d_->options.activateWindowHandler(observation.windowHandle);
    const auto windowExists = d_->options.windowExistsProvider;
    const auto navigation = d_->options.navigationStateProvider
        ? d_->options.navigationStateProvider
        : std::function<SumatraPdfDdeFileState(int)>(requestSumatraPdfDdeFileState);
    const PdfRegionSelectionResult selection =
        d_->options.regionSelectionHandler(
            geometry,
            [windowExists, navigation, observation]() {
                if (!windowExists(observation.windowHandle)) return QStringLiteral("PDF viewer closed during capture");
                const auto state = navigation(100);
                if (!state.success()) return QStringLiteral("PDF viewer state became unavailable: %1").arg(state.error);
                if (!samePdfPath(state.path, observation.documentPath)) return QStringLiteral("Active PDF changed during capture");
                return QString();
            },
            request.parent,
            std::clamp(request.timeoutMilliseconds, 1, 120000),
            std::max(1, d_->options.minimumSelectionPixels));
    if (!selection.selected()) {
        result.state = selection.timedOut()
            ? PdfViewerOperationState::TimedOut
            : (selection.canceled()
                   ? PdfViewerOperationState::Canceled
                   : PdfViewerOperationState::Failed);
        result.message = selection.diagnostics.trimmed().isEmpty()
            ? stateMessage(result.state,
                           QStringLiteral("PDF region selection failed"))
            : selection.diagnostics.trimmed();
        result.diagnostics = selection.diagnostics;
        return result;
    }

    const QString sessionFailure = d_->validateCaptureSession(observation);
    if (!sessionFailure.isEmpty()) {
        result.state = PdfViewerOperationState::Canceled;
        result.message = sessionFailure;
        return result;
    }
    d_->options.activateWindowHandler(observation.windowHandle);
    const SumatraPdfDdeMousePosition start =
        d_->options.mousePositionProvider(selection.screenRect.topLeft(), 300);
    const SumatraPdfDdeMousePosition end =
        d_->options.mousePositionProvider(selection.screenRect.bottomRight(), 300);
    const SumatraPdfDdeRegion region =
        sumatraPdfDdeRegionFromMousePositions(start, end);
    if (!region.success()) {
        result.state = PdfViewerOperationState::Failed;
        result.message = region.error.trimmed().isEmpty()
            ? QStringLiteral("Unable to read PDF page coordinates")
            : region.error.trimmed();
        result.diagnostics = result.message;
        return result;
    }

    result.anchorRequest.locatorType = QStringLiteral("sumatrapdf.rect");
    result.anchorRequest.page = region.page;
    result.anchorRequest.rect = region.rect;
    result.anchorRequest.source =
        QStringLiteral("sumatrapdf-adapter-region");
    result.observation.page = region.page;
    result.observation.provenance = result.anchorRequest.source;
    if (!d_->attachPageGeometry(&result)) return result;
    result.state = PdfViewerOperationState::Succeeded;
    result.message = QStringLiteral("Captured PDF region on page %1")
                         .arg(region.page);
    return result;
}

PdfViewerCaptureResult SumatraPdfViewerAdapter::captureText(
    const PdfViewerCaptureRequest &request)
{
    PdfViewerCaptureResult result = d_->resolveCapture(request);
    if (!result.succeeded()) return result;
    if (result.observation.windowHandle == 0
        || !d_->options.windowExistsProvider(result.observation.windowHandle)) {
        result.state = PdfViewerOperationState::Canceled;
        result.message = QStringLiteral("PDF viewer closed before text capture");
        return result;
    }
    QString sessionFailure = d_->validateCaptureSession(result.observation);
    if (!sessionFailure.isEmpty()) {
        result.state = PdfViewerOperationState::Canceled;
        result.message = sessionFailure;
        return result;
    }
    const ForegroundAppWindowContext context = result.observation.context;
    const ForegroundTextTarget target = request.textTarget.isValid()
        ? request.textTarget
        : d_->options.textTargetProvider();
    result.textSelection = d_->options.textSelectionProvider(context, target);
    if (!result.textSelection.hasSelectedText()) {
        result.state = PdfViewerOperationState::Failed;
        result.message = result.textSelection.diagnostics.trimmed().isEmpty()
            ? QStringLiteral("Select PDF text before capture")
            : result.textSelection.diagnostics.trimmed();
        result.diagnostics = result.textSelection.diagnostics;
        return result;
    }
    sessionFailure = d_->validateCaptureSession(result.observation);
    if (!sessionFailure.isEmpty()) {
        result.state = PdfViewerOperationState::Canceled;
        result.message = sessionFailure;
        return result;
    }
    result.textSelection.source =
        QStringLiteral("sumatrapdf-adapter-selection");
    result.anchorRequest.locatorType = QStringLiteral("sumatrapdf.search");
    result.anchorRequest.searchText =
        result.textSelection.text.simplified();
    result.anchorRequest.name = result.anchorRequest.searchText.left(64);
    result.anchorRequest.source = result.textSelection.source;
    result.observation.provenance = result.anchorRequest.source;
    if (request.requirePageGeometry
        && !d_->attachPageGeometry(&result)) {
        return result;
    }
    result.state = PdfViewerOperationState::Succeeded;
    result.message = QStringLiteral("Captured selected PDF text");
    return result;
}

PdfViewerOpenStartResult SumatraPdfViewerAdapter::open(
    const PdfViewerOpenRequest &request,
    PdfViewerOpenCallbacks callbacks)
{
    return d_->open(request, std::move(callbacks));
}

void SumatraPdfViewerAdapter::cancelPending()
{
    d_->cancelPending();
}

int SumatraPdfViewerAdapter::activeOperationCount() const
{
    return d_->current && !d_->current->finished ? 1 : 0;
}

bool SumatraPdfViewerAdapter::hasOriginalFallback() const
{
    return d_->originalFallback.has_value();
}

bool SumatraPdfViewerAdapter::openOriginalFallback(QString *error)
{
    if (error) error->clear();
    if (!d_->originalFallback.has_value()) {
        if (error) *error = QStringLiteral(
            "No original PDF fallback is available");
        return false;
    }
    const SumatraPdfCommand command = d_->originalFallback.value();
    if (!d_->launch(command, error)) return false;
    d_->originalFallback.reset();
    return true;
}

} // namespace Pinloom
