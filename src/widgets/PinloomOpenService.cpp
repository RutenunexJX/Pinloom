#include "pinloom/widgets/PinloomOpenService.h"

#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/InboxFileCapture.h"
#include "pinloom/widgets/TextPreviewDialog.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>

namespace Pinloom {

namespace {

QUrl urlForLocation(const QString &location)
{
    const QUrl parsed(location);
    if (parsed.isValid()
        && (parsed.scheme() == QLatin1String("http") || parsed.scheme() == QLatin1String("https"))) {
        return parsed;
    }
    return QUrl::fromLocalFile(location);
}

bool comClassAvailable(const QString &className)
{
#ifdef Q_OS_WIN
    QSettings applicationKey(QStringLiteral("HKEY_CLASSES_ROOT\\%1").arg(className),
                             QSettings::NativeFormat);
    return !applicationKey.value(QStringLiteral("CLSID/.")).toString().trimmed().isEmpty()
        || !applicationKey.value(QStringLiteral("CurVer/.")).toString().trimmed().isEmpty();
#else
    Q_UNUSED(className)
    return false;
#endif
}

QString normalizedJumpPath(QString path)
{
    path = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}

QString quotedDdeValue(QString value)
{
    value.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(value);
}

SumatraPdfCommand pageOnlyPdfCommand(const SumatraPdfCommand &source,
                                     const QString &filePath)
{
    SumatraPdfCommand command;
    command.executablePath = source.executablePath;
    command.filePath = filePath;
    command.page = source.page;
    command.arguments = {
        QStringLiteral("-reuse-instance"),
        QStringLiteral("-page"),
        QString::number(command.page),
        command.filePath,
    };
    return command;
}

} // namespace

bool sumatraPdfJumpMatches(const SumatraPdfDdeFileState &state,
                           const SumatraPdfCommand &command,
                           QString *diagnostics)
{
    if (diagnostics) {
        diagnostics->clear();
    }
    if (!state.success()) {
        if (diagnostics) {
            *diagnostics = state.error.trimmed().isEmpty()
                ? QStringLiteral("SumatraPDF did not return a readable active document state")
                : state.error.trimmed();
        }
        return false;
    }
    if (normalizedJumpPath(state.path)
        != normalizedJumpPath(command.filePath)) {
        if (diagnostics) {
            *diagnostics = QStringLiteral("active file is %1").arg(state.path);
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
    if (command.zoom > 0.0
        && (!std::isfinite(state.zoom)
            || state.zoom <= 0.0
            || std::abs(state.zoom - command.zoom) > 0.75)) {
        if (diagnostics) {
            *diagnostics = QStringLiteral("active zoom is %1%; expected %2%")
                               .arg(state.zoom)
                               .arg(command.zoom);
        }
        return false;
    }
    return true;
}

QString sumatraPdfRetryDdeCommand(
    const SumatraPdfCommand &command,
    const SumatraPdfDdeFileState &lastState)
{
    Q_UNUSED(lastState)
    if (command.filePath.trimmed().isEmpty() || command.page <= 0) {
        return {};
    }
    const QString path = quotedDdeValue(
        QDir::toNativeSeparators(command.filePath));
    QString retry;
    if (!command.searchText.trimmed().isEmpty()) {
        retry = QStringLiteral("[GotoPageWord(%1,%2,%3)]")
                    .arg(path,
                         QString::number(command.page),
                         quotedDdeValue(command.searchText));
    } else {
        retry = QStringLiteral("[GotoPage(%1,%2)]")
                    .arg(path, QString::number(command.page));
    }
    return retry;
}

PinloomOpenService::PinloomOpenService(ILibraryRepository &repository,
                                       PinloomOpenServiceOptions options,
                                       QObject *parent)
    : QObject(parent)
    , repository_(repository)
    , options_(std::move(options))
{
    pdfAnchorPresenter_ = options_.pdfAnchorPresenter;
    if (!pdfAnchorPresenter_) {
        SumatraAnnotatedCopyPresenterOptions presenterOptions;
        presenterOptions.launchHandler = options_.sumatraPdfLaunchHandler;
        presenterOptions.stateProvider = options_.sumatraPdfStateProvider;
        presenterOptions.stateProviderRunsInWorker =
            options_.sumatraPdfStateProviderRunsInWorker;
        presenterOptions.verificationTimeoutMilliseconds =
            options_.sumatraPdfVerificationTimeoutMilliseconds;
        presenterOptions.verificationPollMilliseconds =
            options_.sumatraPdfVerificationPollMilliseconds;
        presenterOptions.generationTimeoutMilliseconds =
            options_.pdfPresentationGenerationTimeoutMilliseconds;
        ownedPdfAnchorPresenter_ = std::make_unique<SumatraAnnotatedCopyPresenter>(
            std::move(presenterOptions), this);
        pdfAnchorPresenter_ = ownedPdfAnchorPresenter_.get();
    }
}

bool PinloomOpenService::open(const PinloomOpenTarget &target, QWidget *dialogParent)
{
    activePdfPresentationRequestId_ = 0;
    if (pdfAnchorPresenter_) pdfAnchorPresenter_->cancelPending();
    pdfOriginalFallbackCommand_.reset();
    pdfOriginalFallbackTarget_.reset();
    pdfOriginalFallbackReason_.clear();
    if (!target.clipId.trimmed().isEmpty()) {
        if (!options_.clipInsertionHandler) {
            setStatus(QStringLiteral("Clip insertion is not configured"));
            return false;
        }
        QString operationStatus;
        if (!options_.clipInsertionHandler(target.clipId, &operationStatus)) {
            setStatus(operationStatus.trimmed().isEmpty()
                          ? QStringLiteral("Clip insertion failed")
                          : operationStatus.trimmed());
            return false;
        }
        setStatus(operationStatus.trimmed().isEmpty()
                      ? QStringLiteral("Paste invoked")
                      : operationStatus.trimmed());
        return true;
    }

    if (target.location.trimmed().isEmpty()) {
        setStatus(QStringLiteral("No resource selected"));
        return false;
    }
    if (options_.hostOpenHandler && options_.hostOpenHandler(target)) {
        recordOpen(target);
        setStatus(QStringLiteral("Opened target"));
        return true;
    }
    if (target.anchor.has_value() && isExcelAnchor(target.anchor.value())) return openExcel(target);
    if (target.anchor.has_value() && isVisioAnchor(target.anchor.value())) return openVisio(target);
    if (target.anchor.has_value() && isWordAnchor(target.anchor.value())) return openWord(target);
    if (target.anchor.has_value() && isPowerPointAnchor(target.anchor.value())) return openPowerPoint(target);
    if (target.anchor.has_value() && isSumatraPdfAnchor(target.anchor.value())) {
        return openSumatraPdf(target, dialogParent);
    }

    const int anchorLine = target.anchor.has_value() ? anchorLocatorLine(target.anchor.value()) : -1;
    if (anchorLine > 0) {
        TextPreviewDialog preview(target.location, anchorLine, dialogParent);
        if (!preview.load()) {
            setStatus(QStringLiteral("Unable to preview %1").arg(target.location));
            return false;
        }
        recordOpen(target);
        preview.exec();
        setStatus(QStringLiteral("Opened text target"));
        return true;
    }

    if (isInboxResourceId(target.resourceId)) {
        const QFileInfo fileInfo(target.location);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            setStatus(QStringLiteral("Inbox file no longer exists: %1").arg(target.location));
            return false;
        }
    }

    QUrl targetUrl = urlForLocation(target.location);
    if (target.anchor.has_value()
        && anchorLocatorType(target.anchor.value()) == QLatin1String("url.fragment")) {
        targetUrl.setFragment(anchorLocatorFragment(target.anchor.value()));
    }
    if (!QDesktopServices::openUrl(targetUrl)) {
        setStatus(QStringLiteral("Unable to open %1").arg(target.location));
        return false;
    }
    recordOpen(target);
    setStatus(QStringLiteral("Opened target"));
    return true;
}

QString PinloomOpenService::statusText() const
{
    return statusText_;
}

bool PinloomOpenService::openExcel(const PinloomOpenTarget &target)
{
    const ExcelJumpCommandResult result =
        buildExcelJumpCommand(target.anchor.value(), target.location, options_.applicationLaunchSettings);
    if (!result.success()) {
        setStatus(result.error);
        return false;
    }
    QString error;
    const bool opened = options_.excelLaunchHandler
        ? options_.excelLaunchHandler(result.command, &error)
        : (comClassAvailable(QStringLiteral("Excel.Application"))
           && QProcess::startDetached(result.command.executablePath, result.command.arguments));
    if (!opened) {
        setStatus(error.trimmed().isEmpty() ? QStringLiteral("Unable to launch Excel") : error.trimmed());
        return false;
    }
    recordOpen(target);
    setStatus(QStringLiteral("Opened Excel target"));
    return true;
}

bool PinloomOpenService::openVisio(const PinloomOpenTarget &target)
{
    const VisioJumpCommandResult result =
        buildVisioJumpCommand(target.anchor.value(), target.location, options_.applicationLaunchSettings);
    if (!result.success()) {
        setStatus(result.error);
        return false;
    }
    QString error;
    const bool opened = options_.visioLaunchHandler
        ? options_.visioLaunchHandler(result.command, &error)
        : (comClassAvailable(QStringLiteral("Visio.Application"))
           && QProcess::startDetached(result.command.executablePath, result.command.arguments));
    if (!opened) {
        setStatus(error.trimmed().isEmpty() ? QStringLiteral("Unable to launch Visio") : error.trimmed());
        return false;
    }
    recordOpen(target);
    setStatus(QStringLiteral("Opened Visio target"));
    return true;
}

bool PinloomOpenService::openWord(const PinloomOpenTarget &target)
{
    const WordJumpCommandResult result =
        buildWordJumpCommand(target.anchor.value(), target.location, options_.applicationLaunchSettings);
    if (!result.success()) {
        setStatus(result.error);
        return false;
    }
    QString error;
    const bool opened = options_.wordLaunchHandler
        ? options_.wordLaunchHandler(result.command, &error)
        : (comClassAvailable(QStringLiteral("Word.Application"))
           && QProcess::startDetached(result.command.executablePath, result.command.arguments));
    if (!opened) {
        setStatus(error.trimmed().isEmpty() ? QStringLiteral("Unable to launch Word") : error.trimmed());
        return false;
    }
    recordOpen(target);
    setStatus(QStringLiteral("Opened Word target"));
    return true;
}

bool PinloomOpenService::openPowerPoint(const PinloomOpenTarget &target)
{
    const PowerPointJumpCommandResult result =
        buildPowerPointJumpCommand(target.anchor.value(), target.location, options_.applicationLaunchSettings);
    if (!result.success()) {
        setStatus(result.error);
        return false;
    }
    QString error;
    const bool opened = options_.powerPointLaunchHandler
        ? options_.powerPointLaunchHandler(result.command, &error)
        : (comClassAvailable(QStringLiteral("PowerPoint.Application"))
           && QProcess::startDetached(result.command.executablePath, result.command.arguments));
    if (!opened) {
        setStatus(error.trimmed().isEmpty()
                      ? QStringLiteral("Unable to launch PowerPoint")
                      : error.trimmed());
        return false;
    }
    recordOpen(target);
    setStatus(QStringLiteral("Opened PowerPoint target"));
    return true;
}

bool PinloomOpenService::openSumatraPdf(const PinloomOpenTarget &target,
                                       QWidget *dialogParent)
{
    Q_UNUSED(dialogParent)
    const SumatraPdfCommandResult result = options_.sumatraPdfExecutablePathProvider
        ? buildSumatraPdfCommand(target.anchor.value(),
                                 target.location,
                                 options_.sumatraPdfExecutablePathProvider().trimmed())
        : buildSumatraPdfCommand(target.anchor.value(),
                                 target.location,
                                 options_.applicationLaunchSettings);
    if (!result.success()) {
        setStatus(result.error);
        return false;
    }

    if (result.command.highlightRect.isValid()) {
        ++sumatraPdfVerificationGeneration_;
        PdfAnchorPresentationRequest presentationRequest;
        presentationRequest.anchor = target.anchor.value();
        presentationRequest.sourceTitle = target.title;
        presentationRequest.cacheDirectory = options_.pdfPresentationCacheDirectory;
        presentationRequest.sourceCommand = result.command;

        const QPointer<PinloomOpenService> guard(this);
        PdfAnchorPresentationCallbacks callbacks;
        callbacks.statusChanged = [guard](quint64 requestId, const QString &status) {
            if (!guard) return;
            QTimer::singleShot(0, guard, [guard, requestId, status]() {
                if (!guard || requestId != guard->activePdfPresentationRequestId_) return;
                guard->setStatus(status);
            });
        };
        callbacks.completed = [guard, target, sourceCommand = result.command](
                                  const PdfAnchorPresentationResult &presentation) {
            if (!guard) return;
            QTimer::singleShot(0, guard,
                               [guard, target, sourceCommand, presentation]() {
                if (!guard || presentation.superseded
                    || presentation.requestId
                        != guard->activePdfPresentationRequestId_) {
                    return;
                }
                guard->handlePdfAnchorPresentation(
                    target, sourceCommand, presentation);
            });
        };
        const PdfAnchorPresentationStartResult start = pdfAnchorPresenter_->present(
            presentationRequest, std::move(callbacks));
        if (!start.accepted()) {
            setStatus(start.error.trimmed().isEmpty()
                          ? QStringLiteral("Unable to start Pinloom Preview")
                          : start.error.trimmed());
            return false;
        }
        activePdfPresentationRequestId_ = start.requestId;
        setStatus(QStringLiteral("Preparing %1 — Pinloom Preview")
                      .arg(QFileInfo(result.command.filePath).fileName()));
        return true;
    }

    activePdfPresentationRequestId_ = 0;
    if (pdfAnchorPresenter_) pdfAnchorPresenter_->cancelPending();
    QString error;
    bool opened = false;
    if (options_.sumatraPdfLaunchHandler) {
        opened = options_.sumatraPdfLaunchHandler(result.command, &error);
    } else {
        const QFileInfo executable(result.command.executablePath);
        opened = executable.exists() && executable.isFile()
            && QProcess::startDetached(result.command.executablePath, result.command.arguments);
    }
    if (!opened) {
        setStatus(error.trimmed().isEmpty()
                      ? QStringLiteral("Unable to launch SumatraPDF")
                      : error.trimmed());
        return false;
    }
    recordOpen(target);
    ++sumatraPdfVerificationGeneration_;
    sumatraPdfVerificationTimer_.restart();
    if (options_.sumatraPdfLaunchHandler
        && !options_.sumatraPdfStateProvider) {
        setStatus(QStringLiteral("Opened SumatraPDF target"));
        return true;
    }

    setStatus(QStringLiteral("Opening SumatraPDF target; verifying jump"));
    const quint64 generation = sumatraPdfVerificationGeneration_;
    const int pollMilliseconds = std::clamp(
        options_.sumatraPdfVerificationPollMilliseconds, 80, 1000);
    QTimer::singleShot(pollMilliseconds, this,
                       [this, command = result.command, generation,
                        pollMilliseconds]() {
        verifySumatraPdfJump(command,
                             generation,
                             pollMilliseconds,
                             0);
    });
    const int timeoutMilliseconds = std::clamp(
        options_.sumatraPdfVerificationTimeoutMilliseconds, 600, 15000);
    QTimer::singleShot(timeoutMilliseconds, this, [this, generation]() {
        if (generation != sumatraPdfVerificationGeneration_) {
            return;
        }
        ++sumatraPdfVerificationGeneration_;
        setStatus(QStringLiteral(
            "SumatraPDF opened, but jump verification failed: "
            "timed out waiting for DDE state"));
    });
    return true;
}

void PinloomOpenService::verifySumatraPdfJump(
    const SumatraPdfCommand &command,
    quint64 generation,
    int elapsedMilliseconds,
    int consecutiveMatches,
    const QString &lastDiagnostics)
{
    if (generation != sumatraPdfVerificationGeneration_) {
        return;
    }

    if (options_.sumatraPdfStateProvider
        && !options_.sumatraPdfStateProviderRunsInWorker) {
        handleSumatraPdfVerificationState(
            command,
            generation,
            elapsedMilliseconds,
            consecutiveMatches,
            lastDiagnostics,
            options_.sumatraPdfStateProvider(140));
        return;
    }

    const auto stateProvider = options_.sumatraPdfStateProvider;
    auto *watcher = new QFutureWatcher<SumatraPdfDdeFileState>(this);
    const QPointer<PinloomOpenService> guard(this);
    connect(watcher, &QFutureWatcher<SumatraPdfDdeFileState>::finished,
            watcher,
            [guard, watcher, command, generation,
             elapsedMilliseconds, consecutiveMatches,
             lastDiagnostics]() {
        const SumatraPdfDdeFileState state = watcher->result();
        watcher->deleteLater();
        if (!guard) {
            return;
        }
        guard->handleSumatraPdfVerificationState(
            command,
            generation,
            elapsedMilliseconds,
            consecutiveMatches,
            lastDiagnostics,
            state);
    });
    watcher->setFuture(QtConcurrent::run([stateProvider]() {
        return stateProvider
            ? stateProvider(140)
            : requestSumatraPdfDdeFileState(140);
    }));
}

void PinloomOpenService::handleSumatraPdfVerificationState(
    const SumatraPdfCommand &command,
    quint64 generation,
    int elapsedMilliseconds,
    int consecutiveMatches,
    const QString &lastDiagnostics,
    const SumatraPdfDdeFileState &state)
{
    if (generation != sumatraPdfVerificationGeneration_) {
        return;
    }

    QString diagnostics;
    const int observedElapsedMilliseconds = sumatraPdfVerificationTimer_.isValid()
        ? std::max(elapsedMilliseconds,
                   static_cast<int>(sumatraPdfVerificationTimer_.elapsed()))
        : elapsedMilliseconds;
    const bool matches = sumatraPdfJumpMatches(state, command, &diagnostics);
    const int timeoutMilliseconds = std::clamp(
        options_.sumatraPdfVerificationTimeoutMilliseconds, 600, 15000);
    if (matches) {
        ++consecutiveMatches;
        if (consecutiveMatches >= 2) {
            ++sumatraPdfVerificationGeneration_;
            setStatus(QStringLiteral("Opened SumatraPDF target; jump verified"));
            return;
        }
    } else if (!matches) {
        consecutiveMatches = 0;
    }

    if (observedElapsedMilliseconds >= timeoutMilliseconds) {
        const QString reason = diagnostics.trimmed().isEmpty()
            ? lastDiagnostics.trimmed()
            : diagnostics.trimmed();
        ++sumatraPdfVerificationGeneration_;
        setStatus(QStringLiteral("SumatraPDF opened, but jump verification failed: %1")
                      .arg(reason.isEmpty()
                               ? QStringLiteral("expected page was not observed")
                               : reason));
        return;
    }

    const int pollMilliseconds = std::clamp(
        options_.sumatraPdfVerificationPollMilliseconds, 80, 1000);
    QTimer::singleShot(pollMilliseconds, this,
                       [this, command, generation,
                        observedElapsedMilliseconds, pollMilliseconds,
                         consecutiveMatches, diagnostics]() {
        verifySumatraPdfJump(command,
                             generation,
                             observedElapsedMilliseconds + pollMilliseconds,
                             consecutiveMatches,
                             diagnostics);
    });
}

void PinloomOpenService::handlePdfAnchorPresentation(
    const PinloomOpenTarget &target,
    const SumatraPdfCommand &sourceCommand,
    const PdfAnchorPresentationResult &result)
{
    if (result.requestId != activePdfPresentationRequestId_) {
        return;
    }
    activePdfPresentationRequestId_ = 0;
    const QString sourceName = QFileInfo(sourceCommand.filePath).fileName();
    if (result.success()) {
        recordOpen(target);
        setStatus(QStringLiteral("Opened %1 — Pinloom Preview at page %2%3")
                      .arg(sourceName.isEmpty() ? QStringLiteral("PDF") : sourceName)
                      .arg(sourceCommand.page)
                      .arg(result.cacheHit ? QStringLiteral(" (cached)") : QString()));
        return;
    }

    pdfOriginalFallbackCommand_ = pageOnlyPdfCommand(
        sourceCommand, sourceCommand.filePath);
    pdfOriginalFallbackTarget_ = target;
    pdfOriginalFallbackReason_ = result.error.trimmed().isEmpty()
        ? QStringLiteral("Pinloom Preview failed")
        : result.error.trimmed();
    setStatus(QStringLiteral(
        "Pinloom Preview failed for %1: %2. The original PDF is unchanged; "
        "Open original PDF is available.")
                  .arg(sourceName.isEmpty() ? QStringLiteral("PDF") : sourceName,
                       pdfOriginalFallbackReason_));
    emit pdfOriginalFallbackAvailable(sourceCommand.filePath,
                                      sourceCommand.page,
                                      pdfOriginalFallbackReason_);
    if (options_.pdfOriginalFallbackPrompt
        && options_.pdfOriginalFallbackPrompt(sourceCommand.filePath,
                                              sourceCommand.page,
                                              pdfOriginalFallbackReason_)) {
        openPdfOriginalFallback();
    }
}

bool PinloomOpenService::hasPdfOriginalFallback() const
{
    return pdfOriginalFallbackCommand_.has_value()
        && pdfOriginalFallbackTarget_.has_value();
}

bool PinloomOpenService::openPdfOriginalFallback()
{
    if (!hasPdfOriginalFallback()) {
        setStatus(QStringLiteral("No original PDF fallback is available"));
        return false;
    }
    const SumatraPdfCommand command = pdfOriginalFallbackCommand_.value();
    QString error;
    const bool opened = options_.sumatraPdfLaunchHandler
        ? options_.sumatraPdfLaunchHandler(command, &error)
        : (QFileInfo(command.executablePath).isFile()
           && QProcess::startDetached(command.executablePath, command.arguments));
    if (!opened) {
        setStatus(error.trimmed().isEmpty()
                      ? QStringLiteral("Unable to open original PDF fallback")
                      : error.trimmed());
        return false;
    }
    recordOpen(pdfOriginalFallbackTarget_.value());
    setStatus(QStringLiteral("Opened original PDF fallback at page %1")
                  .arg(command.page));
    pdfOriginalFallbackCommand_.reset();
    pdfOriginalFallbackTarget_.reset();
    pdfOriginalFallbackReason_.clear();
    return true;
}

void PinloomOpenService::setStatus(const QString &status)
{
    const QString normalized = status.trimmed();
    if (statusText_ == normalized) {
        return;
    }
    statusText_ = normalized;
    emit statusChanged(statusText_);
}

void PinloomOpenService::recordOpen(const PinloomOpenTarget &target)
{
    if (target.resourceId.trimmed().isEmpty()) return;
    repository_.recordResourceOpen(target.resourceId);
    if (target.anchor.has_value()) repository_.recordAnchorOpen(target.resourceId, target.anchor.value());
}

} // namespace Pinloom
