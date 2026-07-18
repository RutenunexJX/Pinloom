#include "pinloom/widgets/PinloomMainWindow.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QStringList>
#include <QVBoxLayout>

namespace Pinloom {

QString pinloomResidentStatusSummary(const PinloomResidentStatus &status)
{
    QStringList parts;
    parts.append(status.running ? QStringLiteral("Pinloom running") : QStringLiteral("Pinloom stopped"));
    const QString hotkeyText = status.mainHotkeyText.trimmed();
    parts.append(QStringLiteral("Command hotkey: %1%2")
                     .arg(status.mainHotkeyRegistered ? QStringLiteral("registered")
                                                      : QStringLiteral("not registered"),
                          hotkeyText.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(hotkeyText)));
    const QString hyperHotkeyText = status.hyperHotkeyText.trimmed();
    parts.append(QStringLiteral("Hyper hotkey: %1%2")
                     .arg(status.hyperHotkeyRegistered ? QStringLiteral("registered")
                                                       : QStringLiteral("not registered"),
                          hyperHotkeyText.isEmpty()
                              ? QString()
                              : QStringLiteral(" (%1)").arg(hyperHotkeyText)));

    QString clipCapture = QStringLiteral("stopped");
    if (status.clipCaptureActive) {
        clipCapture = status.clipCapturePaused ? QStringLiteral("paused") : QStringLiteral("active");
    }
    parts.append(QStringLiteral("Clip capture: %1").arg(clipCapture));

    const QString error = status.lastError.trimmed();
    if (!error.isEmpty()) {
        parts.append(QStringLiteral("Last error: %1").arg(error));
    }
    return parts.join(QStringLiteral(" | "));
}

QString pinloomResidentDiagnosticsText(const PinloomResidentStatus &status,
                                       const QString &recentError,
                                       const QString &recentErrorDetails)
{
    QStringList lines;
    lines.append(QStringLiteral("Pinloom diagnostics"));
    lines.append(QStringLiteral("Status: %1").arg(status.running ? QStringLiteral("running")
                                                                 : QStringLiteral("stopped")));
    lines.append(QStringLiteral("Command hotkey: %1").arg(status.mainHotkeyRegistered
                                                              ? QStringLiteral("registered")
                                                              : QStringLiteral("not registered")));
    if (!status.mainHotkeyText.trimmed().isEmpty()) {
        lines.append(QStringLiteral("Command hotkey key: %1").arg(status.mainHotkeyText.trimmed()));
    }
    lines.append(QStringLiteral("Hyper hotkey: %1").arg(status.hyperHotkeyRegistered
                                                            ? QStringLiteral("registered")
                                                            : QStringLiteral("not registered")));
    if (!status.hyperHotkeyText.trimmed().isEmpty()) {
        lines.append(QStringLiteral("Hyper hotkey key: %1").arg(status.hyperHotkeyText.trimmed()));
    }
    lines.append(QStringLiteral("Clip capture: %1").arg(status.clipCaptureActive
                                                            ? (status.clipCapturePaused
                                                                   ? QStringLiteral("paused")
                                                                   : QStringLiteral("active"))
                                                            : QStringLiteral("stopped")));
    if (!status.clipStatus.trimmed().isEmpty()) {
        lines.append(QStringLiteral("Clip status: %1").arg(status.clipStatus.trimmed()));
    }
    if (!status.lastError.trimmed().isEmpty()) {
        lines.append(QStringLiteral("Last error: %1").arg(status.lastError.trimmed()));
    }
    if (!recentError.trimmed().isEmpty()) {
        lines.append(QStringLiteral("Recent error: %1").arg(recentError.trimmed()));
    }
    if (!recentErrorDetails.trimmed().isEmpty()) {
        lines.append(QStringLiteral("Details:"));
        lines.append(recentErrorDetails.trimmed());
    }
    return lines.join(QLatin1Char('\n'));
}

PinloomMainWindow::PinloomMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    configureMenu();
    statusBar()->showMessage(pinloomResidentStatusSummary(residentStatus_));
}

void PinloomMainWindow::setLauncherMode(bool enabled)
{
    launcherMode_ = enabled;
    menuBar()->setVisible(!launcherMode_);
    statusBar()->setVisible(!launcherMode_);
}

bool PinloomMainWindow::launcherMode() const
{
    return launcherMode_;
}

void PinloomMainWindow::setResidentStatus(const PinloomResidentStatus &status)
{
    residentStatus_ = status;
    const QString summary = pinloomResidentStatusSummary(residentStatus_);
    statusBar()->showMessage(summary);
    setToolTip(summary);
}

PinloomResidentStatus PinloomMainWindow::residentStatus() const
{
    return residentStatus_;
}

QString PinloomMainWindow::residentStatusSummary() const
{
    return pinloomResidentStatusSummary(residentStatus_);
}

void PinloomMainWindow::setRecentError(const QString &summary, const QString &details)
{
    recentError_ = summary.trimmed();
    recentErrorDetails_ = details.trimmed().isEmpty() ? recentError_ : details.trimmed();
    residentStatus_.lastError = recentError_;
    setResidentStatus(residentStatus_);
}

QString PinloomMainWindow::recentError() const
{
    return recentError_;
}

QString PinloomMainWindow::recentErrorDetails() const
{
    return recentErrorDetails_;
}

QString PinloomMainWindow::diagnosticsText() const
{
    return pinloomResidentDiagnosticsText(residentStatus_, recentError_, recentErrorDetails_);
}

void PinloomMainWindow::showDiagnosticsDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Pinloom Diagnostics"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QPlainTextEdit(diagnosticsText(), &dialog);
    text->setObjectName(QStringLiteral("diagnosticsTextEdit"));
    text->setReadOnly(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *copyButton = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
    copyButton->setObjectName(QStringLiteral("copyDiagnosticsButton"));

    layout->addWidget(text);
    layout->addWidget(buttons);

    connect(copyButton, &QPushButton::clicked, &dialog, [text]() {
        if (QClipboard *clipboard = QApplication::clipboard()) {
            clipboard->setText(text->toPlainText());
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(560, 360);
    dialog.exec();
}

void PinloomMainWindow::closeEvent(QCloseEvent *event)
{
    hide();
    if (event) {
        event->ignore();
    }
    emit hiddenToTray();
}

void PinloomMainWindow::configureMenu()
{
    QMenu *menu = menuBar()->addMenu(tr("Pinloom"));
    QAction *settingsAction = menu->addAction(tr("Settings"));
    settingsAction->setObjectName(QStringLiteral("settingsAction"));
    QAction *diagnosticsAction = menu->addAction(tr("Diagnostics"));
    diagnosticsAction->setObjectName(QStringLiteral("diagnosticsAction"));
    menu->addSeparator();
    QAction *quitAction = menu->addAction(tr("Quit"));
    quitAction->setObjectName(QStringLiteral("quitAction"));

    connect(settingsAction, &QAction::triggered, this, &PinloomMainWindow::settingsRequested);
    connect(diagnosticsAction, &QAction::triggered, this, &PinloomMainWindow::diagnosticsRequested);
    connect(diagnosticsAction, &QAction::triggered, this, &PinloomMainWindow::showDiagnosticsDialog);
    connect(quitAction, &QAction::triggered, this, &PinloomMainWindow::quitRequested);
}

} // namespace Pinloom
