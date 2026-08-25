#include "pinloom/widgets/PinloomOpenService.h"

#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/InboxFileCapture.h"
#include "pinloom/core/SumatraPdfForegroundCapture.h"
#include "pinloom/widgets/SumatraPdfRegionCaptureOverlay.h"
#include "pinloom/widgets/TextPreviewDialog.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUrl>
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

QString safeSumatraView(QString view)
{
    view = view.trimmed().toLower();
    static const QStringList supported = {
        QStringLiteral("single page"),
        QStringLiteral("facing"),
        QStringLiteral("book view"),
        QStringLiteral("continuous"),
        QStringLiteral("continuous facing"),
        QStringLiteral("continuous book view"),
    };
    return supported.contains(view) ? view : QStringLiteral("continuous");
}

QString decimalDdeValue(double value)
{
    return QString::number(value, 'f', 4).remove(
        QRegularExpression(QStringLiteral("0+$"))).remove(
        QRegularExpression(QStringLiteral("\\.$")));
}

bool retrySumatraPdfJumpWithDdeProcess(const SumatraPdfCommand &command,
                                       const SumatraPdfDdeFileState &lastState,
                                       QString *error)
{
    const QString dde = sumatraPdfRetryDdeCommand(command, lastState);
    if (command.executablePath.trimmed().isEmpty() || dde.isEmpty()) {
        if (error) {
            *error = QStringLiteral("SumatraPDF retry command is unavailable");
        }
        return false;
    }
    const bool started = QProcess::startDetached(
        command.executablePath,
        {QStringLiteral("-dde"), dde});
    if (!started && error) {
        *error = QStringLiteral("Unable to send SumatraPDF DDE retry");
    }
    return started;
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
    if (command.highlightRect.isValid()) {
        const QString view = quotedDdeValue(safeSumatraView(lastState.view));
        const QString zoom = command.zoom > 0.0
            ? decimalDdeValue(command.zoom)
            : QStringLiteral("0");
        retry.append(QStringLiteral("[SetView(%1,%2,%3,%4,%5)]")
                         .arg(path,
                              view,
                              zoom,
                              decimalDdeValue(command.highlightRect.left()),
                              decimalDdeValue(command.highlightRect.top())));
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
    recordOpen(target);
    ++sumatraPdfVerificationGeneration_;
    if (options_.sumatraPdfLaunchHandler
        && !options_.sumatraPdfStateProvider) {
        registerSumatraPdfHighlight(target, result.command);
        setStatus(QStringLiteral("Opened SumatraPDF target"));
        return true;
    }

    setStatus(QStringLiteral("Opening SumatraPDF target; verifying jump"));
    const quint64 generation = sumatraPdfVerificationGeneration_;
    const int pollMilliseconds = std::clamp(
        options_.sumatraPdfVerificationPollMilliseconds, 80, 1000);
    QTimer::singleShot(pollMilliseconds, this,
                       [this, target, command = result.command, generation,
                        pollMilliseconds]() {
        verifySumatraPdfJump(target,
                             command,
                             generation,
                             pollMilliseconds,
                             false);
    });
    return true;
}

void PinloomOpenService::verifySumatraPdfJump(
    const PinloomOpenTarget &target,
    const SumatraPdfCommand &command,
    quint64 generation,
    int elapsedMilliseconds,
    bool retryIssued,
    const QString &lastDiagnostics)
{
    if (generation != sumatraPdfVerificationGeneration_) {
        return;
    }
    std::function<SumatraPdfDdeFileState(int)> stateProvider =
        options_.sumatraPdfStateProvider;
    if (!stateProvider) {
        stateProvider = [](int timeoutMilliseconds) {
            return requestSumatraPdfDdeFileState(timeoutMilliseconds);
        };
    }
    const SumatraPdfDdeFileState state = stateProvider(180);
    QString diagnostics;
    if (sumatraPdfJumpMatches(state, command, &diagnostics)) {
        registerSumatraPdfHighlight(target, command, state);
        setStatus(QStringLiteral("Opened SumatraPDF target; jump verified"));
        return;
    }

    const int timeoutMilliseconds = std::clamp(
        options_.sumatraPdfVerificationTimeoutMilliseconds, 600, 15000);
    bool issued = retryIssued;
    if (!issued && elapsedMilliseconds >= 540) {
        QString retryError;
        const bool retried = options_.sumatraPdfRetryHandler
            ? options_.sumatraPdfRetryHandler(command, state, &retryError)
            : retrySumatraPdfJumpWithDdeProcess(command, state, &retryError);
        issued = true;
        if (!retried && !retryError.trimmed().isEmpty()) {
            diagnostics.append(QStringLiteral("; retry: %1").arg(retryError.trimmed()));
        }
    }
    if (elapsedMilliseconds >= timeoutMilliseconds) {
        const QString reason = diagnostics.trimmed().isEmpty()
            ? lastDiagnostics.trimmed()
            : diagnostics.trimmed();
        setStatus(QStringLiteral("SumatraPDF opened, but jump verification failed: %1")
                      .arg(reason.isEmpty()
                               ? QStringLiteral("expected page was not observed")
                               : reason));
        return;
    }

    const int pollMilliseconds = std::clamp(
        options_.sumatraPdfVerificationPollMilliseconds, 80, 1000);
    QTimer::singleShot(pollMilliseconds, this,
                       [this, target, command, generation,
                        elapsedMilliseconds, pollMilliseconds, issued,
                        diagnostics]() {
        verifySumatraPdfJump(target,
                             command,
                             generation,
                             elapsedMilliseconds + pollMilliseconds,
                             issued,
                             diagnostics);
    });
}

void PinloomOpenService::registerSumatraPdfHighlight(
    const PinloomOpenTarget &target,
    const SumatraPdfCommand &command,
    const SumatraPdfDdeFileState &state)
{
    const double effectiveZoom = std::isfinite(state.zoom) && state.zoom > 0.0
        ? state.zoom
        : command.zoom;
    if (!command.highlightRect.isValid()
        || command.page <= 0
        || !std::isfinite(effectiveZoom)
        || effectiveZoom <= 0.0) {
        return;
    }
    SumatraPdfPersistentHighlight highlight;
    highlight.key = target.anchor.has_value()
        ? target.anchor->id.trimmed()
        : QString();
    if (highlight.key.isEmpty()) {
        highlight.key = QStringLiteral("%1#page-%2@%3,%4")
                            .arg(target.resourceId,
                                 QString::number(command.page),
                                 QString::number(command.highlightRect.left()),
                                 QString::number(command.highlightRect.top()));
    }
    highlight.targetFile = command.filePath;
    highlight.pdfRect = command.highlightRect;
    highlight.page = command.page;
    highlight.zoom = effectiveZoom;
    const ForegroundAppWindowContext foreground =
        currentForegroundAppWindowContext();
    if (isSumatraPdfForegroundWindow(foreground)) {
        highlight.targetWindowHandle = foreground.windowHandle;
    }
    if (options_.sumatraPdfHighlightHandler) {
        options_.sumatraPdfHighlightHandler(highlight);
    } else {
        registerSumatraPdfPersistentHighlight(highlight);
    }
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
