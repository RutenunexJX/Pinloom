#include "pinloom/clip/ClipTrayController.h"

#include "pinloom/clip/ClipHotkeyService.h"

#include <utility>

namespace Pinloom {

namespace {

constexpr auto ShowPickerActionId = "show_picker";
constexpr auto ToggleCaptureActionId = "toggle_capture";
constexpr auto SettingsActionId = "settings";
constexpr auto DiagnosticsActionId = "diagnostics";
constexpr auto QuitActionId = "quit";

} // namespace

ClipTrayController::ClipTrayController(ClipHotkeyService &hotkeyService, QObject *parent)
    : ClipTrayController(hotkeyService, {}, parent)
{
}

ClipTrayController::ClipTrayController(ClipHotkeyService &hotkeyService,
                                       ClipTrayControllerOptions options,
                                       QObject *parent)
    : QObject(parent)
    , hotkeyService_(hotkeyService)
    , options_(std::move(options))
{
    running_ = hotkeyService_.isRegistered();
    status_ = currentStatusText();

    connect(&hotkeyService_, &ClipHotkeyService::activated, this, &ClipTrayController::handleHotkeyActivated);
    connect(&hotkeyService_,
            &ClipHotkeyService::registeredChanged,
            this,
            &ClipTrayController::handleHotkeyRegisteredChanged);
}

void ClipTrayController::setShowPickerHandler(ShowPickerHandler handler)
{
    options_.showPickerHandler = std::move(handler);
}

void ClipTrayController::setCapturePausedHandler(CapturePausedHandler handler)
{
    options_.capturePausedHandler = std::move(handler);
}

void ClipTrayController::setSettingsHandler(ClipTrayActionHandler handler)
{
    options_.settingsHandler = std::move(handler);
}

void ClipTrayController::setDiagnosticsHandler(ClipTrayActionHandler handler)
{
    options_.diagnosticsHandler = std::move(handler);
}

bool ClipTrayController::start()
{
    if (!options_.registerHotkeyOnStart) {
        setRunning(true);
        setLastError({});
        return true;
    }

    if (running_ && hotkeyService_.isRegistered()) {
        return true;
    }

    if (!hotkeyService_.start()) {
        setRunning(false);
        const QString error = hotkeyService_.lastError().trimmed();
        setLastError(error.isEmpty() ? QStringLiteral("Unable to start clip hotkey runtime") : error);
        return false;
    }

    setRunning(true);
    setLastError({});
    return true;
}

void ClipTrayController::stop()
{
    if (options_.registerHotkeyOnStart) {
        hotkeyService_.stop();
    }
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

int ClipTrayController::pickerShownCount() const
{
    return pickerShownCount_;
}

bool ClipTrayController::hotkeyRegistrationEnabled() const
{
    return options_.registerHotkeyOnStart;
}

bool ClipTrayController::hotkeyRegistered() const
{
    return hotkeyService_.isRegistered();
}

QString ClipTrayController::hotkeyDisplayText() const
{
    return hotkeyService_.displayText();
}

bool ClipTrayController::capturePaused() const
{
    return options_.capturePaused;
}

QList<ClipTrayAction> ClipTrayController::actions() const
{
    return {
        {QStringLiteral("show_picker"), QStringLiteral("Show Clipboard"), true, false},
        {QStringLiteral("toggle_capture"),
         options_.capturePaused ? QStringLiteral("Resume Capture") : QStringLiteral("Pause Capture"),
         true,
         options_.capturePaused,
         true},
        {QStringLiteral("settings"), QStringLiteral("Settings"), true, false},
        {QStringLiteral("diagnostics"), QStringLiteral("Diagnostics"), true, false},
        {QStringLiteral("quit"), QStringLiteral("Quit Pinloom"), true, false},
    };
}

bool ClipTrayController::triggerAction(const QString &actionId)
{
    const QString normalizedId = actionId.trimmed();
    if (normalizedId == QLatin1String(ShowPickerActionId)) {
        requestShowPicker();
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

void ClipTrayController::requestShowPicker()
{
    if (options_.showPickerHandler) {
        options_.showPickerHandler();
    }

    ++pickerShownCount_;
    emit pickerShownCountChanged(pickerShownCount_);
    emit showPickerRequested();
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
    if (options_.capturePaused == paused) {
        return;
    }

    options_.capturePaused = paused;
    if (options_.capturePausedHandler) {
        options_.capturePausedHandler(options_.capturePaused);
    }

    emit capturePausedChanged(options_.capturePaused);
    emit trayActionsChanged();
    refreshStatus();
}

void ClipTrayController::toggleCapturePaused()
{
    setCapturePaused(!options_.capturePaused);
}

void ClipTrayController::requestSettings()
{
    if (options_.settingsHandler) {
        options_.settingsHandler();
    }
    emit settingsRequested();
}

void ClipTrayController::requestDiagnostics()
{
    if (options_.diagnosticsHandler) {
        options_.diagnosticsHandler();
    }
    emit diagnosticsRequested();
}

void ClipTrayController::requestQuit()
{
    emit quitRequested();
}

void ClipTrayController::handleHotkeyActivated()
{
    if (!options_.registerHotkeyOnStart) {
        return;
    }

    requestShowPicker();
}

void ClipTrayController::handleHotkeyRegisteredChanged(bool registered)
{
    setRunning(registered);
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
        return options_.capturePaused ? QStringLiteral("Running, capture paused") : QStringLiteral("Running");
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
