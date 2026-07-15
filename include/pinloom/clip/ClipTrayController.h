#pragma once

#include <QList>
#include <QObject>
#include <QString>

namespace Pinloom {

struct ClipTrayAction {
    QString id;
    QString title;
    bool enabled = true;
    bool checked = false;
    bool checkable = false;
};

class ClipTrayController final : public QObject {
    Q_OBJECT

public:
    explicit ClipTrayController(QObject *parent = nullptr);

    bool start();
    void stop();
    bool isRunning() const;

    QString status() const;
    QString lastError() const;

    bool capturePaused() const;
    QList<ClipTrayAction> actions() const;
    bool triggerAction(const QString &actionId);

public slots:
    void requestShowClipboard();
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
    void showClipboardRequested();
    void capturePausedChanged(bool paused);
    void trayActionsChanged();
    void settingsRequested();
    void diagnosticsRequested();
    void quitRequested();

private:
    void setRunning(bool running);
    void setLastError(const QString &error);
    QString currentStatusText() const;
    void refreshStatus();

    QString status_;
    QString lastError_;
    bool capturePaused_ = false;
    bool running_ = false;
};

} // namespace Pinloom
