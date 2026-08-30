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
                              QWidget *dialogParent)
{
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
        TextPreviewDialog preview(target.location, anchorLine, dialogParent);
        if (!preview.load()) {
            setStatus(QStringLiteral("Unable to preview %1")
                          .arg(target.location));
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
    const bool opened = options_.excelLaunchHandler
        ? options_.excelLaunchHandler(result.command, &error)
        : (comClassAvailable(QStringLiteral("Excel.Application"))
           && QProcess::startDetached(result.command.executablePath,
                                      result.command.arguments));
    if (!opened) {
        setStatus(error.trimmed().isEmpty()
                      ? QStringLiteral("Unable to launch Excel")
                      : error.trimmed());
        return false;
    }
    recordOpen(target);
    setStatus(QStringLiteral("Opened Excel target"));
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
    const bool opened = options_.visioLaunchHandler
        ? options_.visioLaunchHandler(result.command, &error)
        : (comClassAvailable(QStringLiteral("Visio.Application"))
           && QProcess::startDetached(result.command.executablePath,
                                      result.command.arguments));
    if (!opened) {
        setStatus(error.trimmed().isEmpty()
                      ? QStringLiteral("Unable to launch Visio")
                      : error.trimmed());
        return false;
    }
    recordOpen(target);
    setStatus(QStringLiteral("Opened Visio target"));
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
    const bool opened = options_.wordLaunchHandler
        ? options_.wordLaunchHandler(result.command, &error)
        : (comClassAvailable(QStringLiteral("Word.Application"))
           && QProcess::startDetached(result.command.executablePath,
                                      result.command.arguments));
    if (!opened) {
        setStatus(error.trimmed().isEmpty()
                      ? QStringLiteral("Unable to launch Word")
                      : error.trimmed());
        return false;
    }
    recordOpen(target);
    setStatus(QStringLiteral("Opened Word target"));
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
    const bool opened = options_.powerPointLaunchHandler
        ? options_.powerPointLaunchHandler(result.command, &error)
        : (comClassAvailable(QStringLiteral("PowerPoint.Application"))
           && QProcess::startDetached(result.command.executablePath,
                                      result.command.arguments));
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
