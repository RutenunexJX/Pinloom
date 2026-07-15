#include "pinloom/clip/ClipTrayController.h"

namespace Pinloom {

namespace {

constexpr auto ShowClipboardActionId = "show_clipboard";
constexpr auto ToggleCaptureActionId = "toggle_capture";
constexpr auto SettingsActionId = "settings";
constexpr auto DiagnosticsActionId = "diagnostics";
constexpr auto QuitActionId = "quit";

} // namespace

ClipTrayController::ClipTrayController(QObject *parent)
    : QObject(parent)
{
    status_ = currentStatusText();
}

bool ClipTrayController::start()
{
    setRunning(true);
    setLastError({});
    return true;
}

void ClipTrayController::stop()
{
    setRunning(false);
}

bool ClipTrayController::isRunning() const
{
    return running_;
}

QString ClipTrayController::status() const
{
    return status_;
}

QString ClipTrayController::lastError() const
{
    return lastError_;
}

bool ClipTrayController::capturePaused() const
{
    return capturePaused_;
}

QList<ClipTrayAction> ClipTrayController::actions() const
{
    return {
        {QStringLiteral("show_clipboard"), QStringLiteral("Show Clipboard"), true, false},
        {QStringLiteral("toggle_capture"),
         capturePaused_ ? QStringLiteral("Resume Capture") : QStringLiteral("Pause Capture"),
         true,
         capturePaused_,
         true},
        {QStringLiteral("settings"), QStringLiteral("Settings"), true, false},
        {QStringLiteral("diagnostics"), QStringLiteral("Diagnostics"), true, false},
        {QStringLiteral("quit"), QStringLiteral("Quit Pinloom"), true, false},
    };
}

bool ClipTrayController::triggerAction(const QString &actionId)
{
    const QString normalizedId = actionId.trimmed();
    if (normalizedId == QLatin1String(ShowClipboardActionId)) {
        requestShowClipboard();
        return true;
    }
    if (normalizedId == QLatin1String(ToggleCaptureActionId)) {
        toggleCapturePaused();
        return true;
    }
    if (normalizedId == QLatin1String(SettingsActionId)) {
        requestSettings();
        return true;
    }
    if (normalizedId == QLatin1String(DiagnosticsActionId)) {
        requestDiagnostics();
        return true;
    }
    if (normalizedId == QLatin1String(QuitActionId)) {
        requestQuit();
        return true;
    }

    setLastError(QStringLiteral("Unknown tray action: %1").arg(normalizedId));
    return false;
}

void ClipTrayController::requestShowClipboard()
{
    emit showClipboardRequested();
}

void ClipTrayController::pauseCapture()
{
    setCapturePaused(true);
}

void ClipTrayController::resumeCapture()
{
    setCapturePaused(false);
}

void ClipTrayController::setCapturePaused(bool paused)
{
    if (capturePaused_ == paused) {
        return;
    }

    capturePaused_ = paused;
    emit capturePausedChanged(capturePaused_);
    emit trayActionsChanged();
    refreshStatus();
}

void ClipTrayController::toggleCapturePaused()
{
    setCapturePaused(!capturePaused_);
}

void ClipTrayController::requestSettings()
{
    emit settingsRequested();
}

void ClipTrayController::requestDiagnostics()
{
    emit diagnosticsRequested();
}

void ClipTrayController::requestQuit()
{
    emit quitRequested();
}

void ClipTrayController::setRunning(bool running)
{
    if (running_ == running) {
        refreshStatus();
        return;
    }

    running_ = running;
    emit runningChanged(running_);
    refreshStatus();
}

void ClipTrayController::setLastError(const QString &error)
{
    if (lastError_ == error) {
        refreshStatus();
        return;
    }

    lastError_ = error;
    emit errorChanged(lastError_);
    refreshStatus();
}

QString ClipTrayController::currentStatusText() const
{
    if (running_) {
        return capturePaused_ ? QStringLiteral("Running, capture paused") : QStringLiteral("Running");
    }
    if (!lastError_.isEmpty()) {
        return QStringLiteral("Stopped: %1").arg(lastError_);
    }
    return QStringLiteral("Stopped");
}

void ClipTrayController::refreshStatus()
{
    const QString nextStatus = currentStatusText();
    if (status_ == nextStatus) {
        return;
    }

    status_ = nextStatus;
    emit statusChanged(status_);
}

} // namespace Pinloom
