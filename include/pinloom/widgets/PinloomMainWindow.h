#pragma once

#include <QMainWindow>
#include <QString>

class QCloseEvent;

namespace Pinloom {

struct PinloomResidentStatus {
    bool running = false;
    bool mainHotkeyRegistered = false;
    QString mainHotkeyText;
    bool hyperHotkeyRegistered = false;
    QString hyperHotkeyText;
    bool clipCaptureActive = false;
    bool clipCapturePaused = false;
    QString clipStatus;
    QString lastError;
};

QString pinloomResidentStatusSummary(const PinloomResidentStatus &status);
QString pinloomResidentDiagnosticsText(const PinloomResidentStatus &status,
                                       const QString &recentError,
                                       const QString &recentErrorDetails);

class PinloomMainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit PinloomMainWindow(QWidget *parent = nullptr);

    void setResidentStatus(const PinloomResidentStatus &status);
    PinloomResidentStatus residentStatus() const;
    QString residentStatusSummary() const;

    void setRecentError(const QString &summary, const QString &details = {});
    QString recentError() const;
    QString recentErrorDetails() const;
    QString diagnosticsText() const;

public slots:
    void showDiagnosticsDialog();

signals:
    void hiddenToTray();
    void settingsRequested();
    void diagnosticsRequested();
    void quitRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void configureMenu();

    PinloomResidentStatus residentStatus_;
    QString recentError_;
    QString recentErrorDetails_;
};

} // namespace Pinloom
