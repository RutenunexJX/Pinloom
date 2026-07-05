#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <functional>

namespace Pinloom {

class ClipHotkeyService;

struct ClipTrayAction {
    QString id;
    QString title;
    bool enabled = true;
    bool checked = false;
    bool checkable = false;
};

using ClipTrayShowPickerHandler = std::function<void()>;
using ClipTrayCapturePausedHandler = std::function<void(bool paused)>;
using ClipTrayActionHandler = std::function<void()>;

struct ClipTrayControllerOptions {
    ClipTrayShowPickerHandler showPickerHandler;
    ClipTrayCapturePausedHandler capturePausedHandler;
    ClipTrayActionHandler settingsHandler;
    ClipTrayActionHandler diagnosticsHandler;
    bool capturePaused = false;
    bool registerHotkeyOnStart = true;
};

class ClipTrayController final : public QObject {
    Q_OBJECT

public:
    using ShowPickerHandler = ClipTrayShowPickerHandler;
    using CapturePausedHandler = ClipTrayCapturePausedHandler;

    explicit ClipTrayController(ClipHotkeyService &hotkeyService, QObject *parent = nullptr);
    ClipTrayController(ClipHotkeyService &hotkeyService,
                       ClipTrayControllerOptions options,
                       QObject *parent = nullptr);

    void setShowPickerHandler(ShowPickerHandler handler);
    void setCapturePausedHandler(CapturePausedHandler handler);
    void setSettingsHandler(ClipTrayActionHandler handler);
    void setDiagnosticsHandler(ClipTrayActionHandler handler);

    bool start();
    void stop();
    bool isRunning() const;

    QString status() const;
    QString lastError() const;
    int pickerShownCount() const;
    bool hotkeyRegistrationEnabled() const;
    bool hotkeyRegistered() const;
    QString hotkeyDisplayText() const;

    bool capturePaused() const;
    QList<ClipTrayAction> actions() const;
    bool triggerAction(const QString &actionId);

public slots:
    void requestShowPicker();
    void pauseCapture();
    void resumeCapture();
    void setCapturePaused(bool paused);
    void toggleCapturePaused();
    void requestSettings();
    void requestDiagnostics();
    void requestQuit();

signals:
    void runningChanged(bool running);
    void statusChanged(const QString &status);
    void errorChanged(const QString &error);
    void showPickerRequested();
    void pickerShownCountChanged(int count);
    void capturePausedChanged(bool paused);
    void trayActionsChanged();
    void settingsRequested();
    void diagnosticsRequested();
    void quitRequested();

private:
    void handleHotkeyActivated();
    void handleHotkeyRegisteredChanged(bool registered);
    void setRunning(bool running);
    void setLastError(const QString &error);
    QString currentStatusText() const;
    void refreshStatus();

    ClipHotkeyService &hotkeyService_;
    ClipTrayControllerOptions options_;
    QString status_;
    QString lastError_;
    int pickerShownCount_ = 0;
    bool running_ = false;
};

} // namespace Pinloom
