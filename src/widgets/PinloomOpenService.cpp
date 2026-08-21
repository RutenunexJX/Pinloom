#include "pinloom/widgets/PinloomOpenService.h"

#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/InboxFileCapture.h"
#include "pinloom/widgets/SumatraPdfRegionCaptureOverlay.h"
#include "pinloom/widgets/TextPreviewDialog.h"

#include <QDesktopServices>
#include <QFileInfo>
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

} // namespace

PinloomOpenService::PinloomOpenService(ILibraryRepository &repository,
                                       PinloomOpenServiceOptions options,
                                       QObject *parent)
    : QObject(parent)
    , repository_(repository)
    , options_(std::move(options))
{
}

bool PinloomOpenService::open(const PinloomOpenTarget &target, QWidget *dialogParent)
{
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
    if (target.anchor.has_value() && isSumatraPdfAnchor(target.anchor.value())) return openSumatraPdf(target);

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

bool PinloomOpenService::openSumatraPdf(const PinloomOpenTarget &target)
{
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
    if (result.command.highlightRect.isValid()
        && result.command.page > 0
        && result.command.zoom > 0.0) {
        const QRectF rectangle = result.command.highlightRect;
        const int page = result.command.page;
        const double zoom = result.command.zoom;
        QTimer::singleShot(450, this, [rectangle, page, zoom]() {
            showSumatraPdfRectHighlight(rectangle, page, zoom);
        });
    }
    recordOpen(target);
    setStatus(QStringLiteral("Opened SumatraPDF target"));
    return true;
}

void PinloomOpenService::setStatus(const QString &status)
{
    statusText_ = status.trimmed();
}

void PinloomOpenService::recordOpen(const PinloomOpenTarget &target)
{
    if (target.resourceId.trimmed().isEmpty()) return;
    repository_.recordResourceOpen(target.resourceId);
    if (target.anchor.has_value()) repository_.recordAnchorOpen(target.resourceId, target.anchor.value());
}

} // namespace Pinloom
