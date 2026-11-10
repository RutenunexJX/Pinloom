#include "pinloom/widgets/PinloomOpenService.h"

#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/InboxFileCapture.h"
#include "pinloom/widgets/TextPreviewDialog.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace Pinloom {

namespace {

QUrl urlForLocation(const QString &location)
{
    const QUrl parsed(location);
    if (parsed.isValid()
        && (parsed.scheme() == QLatin1String("http")
            || parsed.scheme() == QLatin1String("https"))) {
        return parsed;
    }
    return QUrl::fromLocalFile(location);
}

bool comClassAvailable(const QString &className)
{
#ifdef Q_OS_WIN
    QSettings applicationKey(
        QStringLiteral("HKEY_CLASSES_ROOT\\%1").arg(className),
        QSettings::NativeFormat);
    return !applicationKey.value(QStringLiteral("CLSID/.")).toString().trimmed().isEmpty()
        || !applicationKey.value(QStringLiteral("CurVer/.")).toString().trimmed().isEmpty();
#else
    Q_UNUSED(className)
    return false;
#endif
}

} // namespace

PinloomOpenService::PinloomOpenService(ILibraryRepository &repository,
                                       PinloomOpenServiceOptions options,
                                       QObject *parent)
    : QObject(parent)
    , repository_(repository)
    , options_(std::move(options))
{
}

bool PinloomOpenService::open(const PinloomOpenTarget &target,
                              QWidget *dialogParent, bool asynchronousPreview)
{
    const QPointer<PinloomOpenService> openGuard(this);
    cancelOfficeOpen(QStringLiteral("Superseded by another open request"));
    if (!openGuard) return false;
    if (activePdfOpenTarget_) {
        emit navigationCompleted(activePdfOpenTarget_->resourceId,
            activePdfOpenTarget_->anchor ? activePdfOpenTarget_->anchor->id : QString(),
            QStringLiteral("cancelled"), false, QStringLiteral("Superseded by another open request"));
        if (!openGuard) return false;
    }
    activePdfOpenRequestId_ = 0;
    activePdfOpenTarget_.reset();
    if (options_.pdfViewerAdapter) options_.pdfViewerAdapter->cancelPending();
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
    if (target.anchor.has_value() && isExcelAnchor(target.anchor.value())) {
        return openExcel(target);
    }
    if (target.anchor.has_value() && isVisioAnchor(target.anchor.value())) {
        return openVisio(target);
    }
    if (target.anchor.has_value() && isWordAnchor(target.anchor.value())) {
        return openWord(target);
    }
    if (target.anchor.has_value() && isPowerPointAnchor(target.anchor.value())) {
        return openPowerPoint(target);
    }
    if (target.anchor.has_value()
        && ((options_.pdfViewerAdapter
             && options_.pdfViewerAdapter->supportsAnchor(target.anchor.value()))
            || target.resourceKind == ResourceKind::Pdf)) {
        return openPdf(target, dialogParent);
    }

    const int anchorLine = target.anchor.has_value()
        ? anchorLocatorLine(target.anchor.value())
        : -1;
    if (anchorLine > 0) {
        auto *preview = new TextPreviewDialog(target.location, anchorLine, dialogParent);
        if (!preview->load()) {
            delete preview;
            setStatus(QStringLiteral("Unable to preview %1")
                          .arg(target.location));
            return false;
        }
        recordOpen(target);
        const bool located = preview->targetLineLocated();
        const QString state = located ? QStringLiteral("located") : QStringLiteral("failed");
        const QString message = located ? QStringLiteral("Text preview positioned at the requested line")
                                        : QStringLiteral("Requested line is outside the document");
        emit navigationCompleted(target.resourceId, target.anchor->id, state, located, message);
        if (asynchronousPreview) {
            preview->setAttribute(Qt::WA_DeleteOnClose);
            preview->show();
        } else {
            preview->exec();
            delete preview;
        }
        setStatus(QStringLiteral("Opened text target"));
        return true;
    }

    if (isInboxResourceId(target.resourceId)) {
        const QFileInfo fileInfo(target.location);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            setStatus(QStringLiteral("Inbox file no longer exists: %1")
                          .arg(target.location));
            return false;
        }
    }

    QUrl targetUrl = urlForLocation(target.location);
    if (target.anchor.has_value()
        && anchorLocatorType(target.anchor.value())
               == QLatin1String("url.fragment")) {
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
    const ExcelJumpCommandResult result = buildExcelJumpCommand(
        target.anchor.value(), target.location,
        options_.applicationLaunchSettings);
    if (!result.success()) {
        setStatus(result.error);
        return false;
    }
    QString error;
    if (!options_.excelLaunchHandler)
        return startOfficeOpen(target, QStringLiteral("Excel"), result.command.executablePath, result.command.arguments);
    const bool opened = options_.excelLaunchHandler(result.command, &error);
    if (!opened) {
        setStatus(error.trimmed().isEmpty()
                      ? QStringLiteral("Unable to launch Excel")
                      : error.trimmed());
        return false;
    }
    const QPointer<PinloomOpenService> guard(this);
    setStatus(QStringLiteral("Excel open requested"));
    if (!guard) return false;
    emit navigationCompleted(target.resourceId, target.anchor->id, QStringLiteral("open_requested"), false, statusText_);
    return true;
}

bool PinloomOpenService::openVisio(const PinloomOpenTarget &target)
{
    const VisioJumpCommandResult result = buildVisioJumpCommand(
        target.anchor.value(), target.location,
        options_.applicationLaunchSettings);
    if (!result.success()) {
        setStatus(result.error);
        return false;
    }
    QString error;
    if (!options_.visioLaunchHandler)
        return startOfficeOpen(target, QStringLiteral("Visio"), result.command.executablePath, result.command.arguments);
    const bool opened = options_.visioLaunchHandler(result.command, &error);
    if (!opened) {
        setStatus(error.trimmed().isEmpty()
                      ? QStringLiteral("Unable to launch Visio")
                      : error.trimmed());
        return false;
    }
    const QPointer<PinloomOpenService> guard(this);
    setStatus(QStringLiteral("Visio open requested"));
    if (!guard) return false;
    emit navigationCompleted(target.resourceId, target.anchor->id, QStringLiteral("open_requested"), false, statusText_);
    return true;
}

bool PinloomOpenService::openWord(const PinloomOpenTarget &target)
{
    const WordJumpCommandResult result = buildWordJumpCommand(
        target.anchor.value(), target.location,
        options_.applicationLaunchSettings);
    if (!result.success()) {
        setStatus(result.error);
        return false;
    }
    QString error;
    if (!options_.wordLaunchHandler)
        return startOfficeOpen(target, QStringLiteral("Word"), result.command.executablePath, result.command.arguments);
    const bool opened = options_.wordLaunchHandler(result.command, &error);
    if (!opened) {
        setStatus(error.trimmed().isEmpty()
                      ? QStringLiteral("Unable to launch Word")
                      : error.trimmed());
        return false;
    }
    const QPointer<PinloomOpenService> guard(this);
    setStatus(QStringLiteral("Word open requested"));
    if (!guard) return false;
    emit navigationCompleted(target.resourceId, target.anchor->id, QStringLiteral("open_requested"), false, statusText_);
    return true;
}

bool PinloomOpenService::openPowerPoint(const PinloomOpenTarget &target)
{
    const PowerPointJumpCommandResult result = buildPowerPointJumpCommand(
        target.anchor.value(), target.location,
        options_.applicationLaunchSettings);
    if (!result.success()) {
        setStatus(result.error);
        return false;
    }
    QString error;
    if (!options_.powerPointLaunchHandler)
        return startOfficeOpen(target, QStringLiteral("PowerPoint"), result.command.executablePath, result.command.arguments);
    const bool opened = options_.powerPointLaunchHandler(result.command, &error);
    if (!opened) {
        setStatus(error.trimmed().isEmpty()
                      ? QStringLiteral("Unable to launch PowerPoint")
                      : error.trimmed());
        return false;
    }
    const QPointer<PinloomOpenService> guard(this);
    setStatus(QStringLiteral("PowerPoint open requested"));
    if (!guard) return false;
    emit navigationCompleted(target.resourceId, target.anchor->id, QStringLiteral("open_requested"), false, statusText_);
    return true;
}

bool PinloomOpenService::startOfficeOpen(const PinloomOpenTarget &target,
                                        const QString &application,
                                        const QString &executable,
                                        const QStringList &arguments)
{
    const QString comClass = application + QStringLiteral(".Application");
    const bool available = options_.officeAvailabilityHandler
        ? options_.officeAvailabilityHandler(comClass) : comClassAvailable(comClass);
    if (!available) {
        const QPointer<PinloomOpenService> guard(this);
        setStatus(QStringLiteral("%1 COM automation is not available").arg(application));
        if (!guard) return false;
        emit navigationCompleted(target.resourceId, target.anchor->id, QStringLiteral("failed"), false, statusText_);
        return false;
    }
    // The child owns its cleanup so destroying the caller never waits for COM.
    auto *process = new QProcess;
    activeOfficeProcess_ = process;
    activeOfficeTarget_ = target;
    auto output = std::make_shared<QByteArray>();
    auto errors = std::make_shared<QByteArray>();
    const auto drain = [process, output, errors] {
        *output = (*output + process->readAllStandardOutput()).right(4096);
        *errors = (*errors + process->readAllStandardError()).right(4096);
    };
    connect(process, &QProcess::readyReadStandardOutput, process, drain);
    connect(process, &QProcess::readyReadStandardError, process, drain);
    connect(this, &QObject::destroyed, process, [process] {
        if (process->state() == QProcess::NotRunning) process->deleteLater();
        else process->kill();
    });
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    connect(process, &QProcess::errorOccurred, this, [this, process, application](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        const QPointer<QProcess> pending(process);
        finishOfficeOpen(process, QStringLiteral("failed"),
                         QStringLiteral("Unable to launch %1: %2").arg(application, process->errorString()));
        if (pending) pending->deleteLater();
    });
    connect(process, &QProcess::finished, this,
            [this, process, application, output, errors, drain](int code, QProcess::ExitStatus exitStatus) {
        drain();
        const bool located = exitStatus == QProcess::NormalExit && code == 0
            && output->split('\n').contains(QByteArray("PinloomOfficeLocated\r"));
        // Accept LF as well as Windows CRLF from a configured PowerShell host.
        const bool confirmed = located || (exitStatus == QProcess::NormalExit && code == 0
            && output->split('\n').contains(QByteArray("PinloomOfficeLocated")));
        const QString detail = QString::fromUtf8(*errors).simplified().left(600);
        finishOfficeOpen(process, confirmed ? QStringLiteral("located") : QStringLiteral("failed"),
            confirmed ? QStringLiteral("Opened %1 target").arg(application)
                      : QStringLiteral("%1 navigation failed: %2").arg(application,
                            detail.isEmpty() ? QStringLiteral("selection was not confirmed") : detail));
    });
    QTimer::singleShot(qBound(1, options_.officeTimeoutMilliseconds, 120000), process,
        [this, process, application, guard = QPointer<PinloomOpenService>(this)] {
            if (!guard || activeOfficeProcess_ != process) return;
            const QPointer<QProcess> pending(process);
            finishOfficeOpen(process, QStringLiteral("failed"), QStringLiteral("%1 navigation timed out").arg(application));
            if (pending) pending->kill();
        });
    const QPointer<PinloomOpenService> guard(this);
    setStatus(QStringLiteral("Opening %1 target").arg(application));
    if (!guard) { process->deleteLater(); return false; }
    if (activeOfficeProcess_ != process) { process->deleteLater(); return false; }
    // Even FailedToStart must arrive after the caller has registered the request.
    QTimer::singleShot(0, process, [guard, process, executable, arguments] {
        if (!guard || guard->activeOfficeProcess_ != process) { process->deleteLater(); return; }
        process->start(executable, arguments);
    });
    return true;
}

void PinloomOpenService::finishOfficeOpen(QProcess *process, const QString &state, const QString &message)
{
    if (activeOfficeProcess_ != process || !activeOfficeTarget_) return;
    const auto target = *activeOfficeTarget_;
    activeOfficeProcess_ = nullptr;
    activeOfficeTarget_.reset();
    const QPointer<PinloomOpenService> guard(this);
    if (state == QLatin1String("located")) recordOpen(target);
    if (!guard) return;
    setStatus(message);
    if (!guard) return;
    emit navigationCompleted(target.resourceId, target.anchor->id, state, state == QLatin1String("located"), message);
}

void PinloomOpenService::cancelOfficeOpen(const QString &message)
{
    const auto process = activeOfficeProcess_;
    if (!process) return;
    finishOfficeOpen(process, QStringLiteral("cancelled"), message);
    if (process) {
        if (process->state() == QProcess::NotRunning) process->deleteLater();
        else process->kill();
    }
}

bool PinloomOpenService::openPdf(const PinloomOpenTarget &target,
                                 QWidget *dialogParent)
{
    Q_UNUSED(dialogParent)
    if (!options_.pdfViewerAdapter) {
        setStatus(QStringLiteral("PDF viewer adapter is not configured"));
        return false;
    }
    if (!target.anchor.has_value()
        || !options_.pdfViewerAdapter->supportsAnchor(target.anchor.value())) {
        setStatus(QStringLiteral("No PDF viewer adapter supports this Anchor"));
        return false;
    }

    PdfViewerOpenRequest request;
    request.anchor = target.anchor.value();
    request.fallbackFilePath = target.location;
    request.sourceTitle = target.title;
    request.cacheDirectory = options_.pdfPresentationCacheDirectory;

    const QPointer<PinloomOpenService> guard(this);
    PdfViewerOpenCallbacks callbacks;
    callbacks.statusChanged = [guard](quint64 requestId,
                                      const QString &status) {
        if (!guard) return;
        QTimer::singleShot(0, guard, [guard, requestId, status]() {
            if (!guard || requestId != guard->activePdfOpenRequestId_) return;
            guard->setStatus(status);
        });
    };
    callbacks.completed = [guard, target](const PdfViewerOpenResult &result) {
        if (!guard) return;
        QTimer::singleShot(0, guard, [guard, target, result]() {
            if (!guard || result.requestId != guard->activePdfOpenRequestId_) {
                return;
            }
            guard->handlePdfViewerOpen(target, result);
        });
    };

    const PdfViewerOpenStartResult start =
        options_.pdfViewerAdapter->open(request, std::move(callbacks));
    if (!start.accepted()) {
        setStatus(start.error.trimmed().isEmpty()
                      ? QStringLiteral("Unable to start PDF navigation")
                      : start.error.trimmed());
        return false;
    }
    activePdfOpenRequestId_ = start.requestId;
    activePdfOpenTarget_ = target;
    setStatus(QStringLiteral("Opening PDF target"));
    return true;
}

void PinloomOpenService::handlePdfViewerOpen(
    const PinloomOpenTarget &target,
    const PdfViewerOpenResult &result)
{
    if (result.requestId != activePdfOpenRequestId_) return;
    activePdfOpenRequestId_ = 0;
    activePdfOpenTarget_.reset();
    emit navigationCompleted(target.resourceId, target.anchor ? target.anchor->id : QString(),
        result.succeeded() ? (result.positionVerified ? QStringLiteral("located") : QStringLiteral("open_requested"))
                           : result.canceled() ? QStringLiteral("cancelled") : QStringLiteral("failed"),
        result.succeeded() && result.positionVerified,
        result.message + (result.diagnostics.isEmpty() ? QString() : QStringLiteral(": ") + result.diagnostics));
    if (result.succeeded()) {
        recordOpen(target);
        setStatus(result.message.trimmed().isEmpty()
                      ? QStringLiteral("Opened PDF target")
                      : result.message.trimmed());
        return;
    }
    if (result.canceled()) {
        setStatus(result.message.trimmed().isEmpty()
                      ? QStringLiteral("PDF operation canceled")
                      : result.message.trimmed());
        return;
    }

    QString reason = result.message.trimmed();
    if (!result.diagnostics.trimmed().isEmpty()) {
        if (!reason.isEmpty()) reason.append(QStringLiteral(": "));
        reason.append(result.diagnostics.trimmed());
    }
    if (reason.isEmpty()) {
        reason = result.timedOut()
            ? QStringLiteral("PDF navigation timed out")
            : QStringLiteral("PDF navigation failed");
    }
    pdfOriginalFallbackReason_ = reason;
    if (result.originalFallbackAvailable
        && options_.pdfViewerAdapter
        && options_.pdfViewerAdapter->hasOriginalFallback()) {
        pdfOriginalFallbackTarget_ = target;
        setStatus(QStringLiteral(
            "%1. The original PDF is unchanged; Open original PDF is available.")
                      .arg(reason));
        emit pdfOriginalFallbackAvailable(result.sourceFilePath,
                                          result.page,
                                          reason);
        if (options_.pdfOriginalFallbackPrompt
            && options_.pdfOriginalFallbackPrompt(result.sourceFilePath,
                                                  result.page,
                                                  reason)) {
            openPdfOriginalFallback();
        }
        return;
    }
    setStatus(reason);
}

bool PinloomOpenService::hasPdfOriginalFallback() const
{
    return pdfOriginalFallbackTarget_.has_value()
        && options_.pdfViewerAdapter
        && options_.pdfViewerAdapter->hasOriginalFallback();
}

bool PinloomOpenService::cancelOpen(const QString &resourceId, const QString &anchorId)
{
    if (activeOfficeTarget_ && activeOfficeTarget_->resourceId == resourceId
        && activeOfficeTarget_->anchor && activeOfficeTarget_->anchor->id == anchorId) {
        cancelOfficeOpen(QStringLiteral("Office request cancelled"));
        return true;
    }
    if (!activePdfOpenTarget_ || activePdfOpenTarget_->resourceId != resourceId
        || !activePdfOpenTarget_->anchor || activePdfOpenTarget_->anchor->id != anchorId) return false;
    if (options_.pdfViewerAdapter) options_.pdfViewerAdapter->cancelPending();
    emit navigationCompleted(resourceId, anchorId, QStringLiteral("cancelled"), false,
                             QStringLiteral("PDF request cancelled"));
    activePdfOpenTarget_.reset(); activePdfOpenRequestId_ = 0;
    return true;
}

bool PinloomOpenService::openPdfOriginalFallback()
{
    if (!hasPdfOriginalFallback()) {
        setStatus(QStringLiteral("No original PDF fallback is available"));
        return false;
    }
    QString error;
    if (!options_.pdfViewerAdapter->openOriginalFallback(&error)) {
        setStatus(error.trimmed().isEmpty()
                      ? QStringLiteral("Unable to open original PDF fallback")
                      : error.trimmed());
        return false;
    }
    recordOpen(pdfOriginalFallbackTarget_.value());
    setStatus(QStringLiteral("Opened original PDF fallback"));
    pdfOriginalFallbackTarget_.reset();
    pdfOriginalFallbackReason_.clear();
    return true;
}

void PinloomOpenService::setStatus(const QString &status)
{
    const QString normalized = status.trimmed();
    if (statusText_ == normalized) return;
    statusText_ = normalized;
    emit statusChanged(statusText_);
}

void PinloomOpenService::recordOpen(const PinloomOpenTarget &target)
{
    if (target.resourceId.trimmed().isEmpty()) return;
    repository_.recordResourceOpen(target.resourceId);
    if (target.anchor.has_value()) {
        repository_.recordAnchorOpen(target.resourceId,
                                     target.anchor.value());
    }
}

} // namespace Pinloom
