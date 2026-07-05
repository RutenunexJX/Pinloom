#include "pinloom/widgets/ClipTrayPresenter.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QStringList>

namespace Pinloom {

namespace {

ClipTrayPresentedAction presentedActionFromControllerAction(const ClipTrayAction &action)
{
    return {action.id, action.title, action.enabled, action.checked, action.checkable};
}

} // namespace

ClipTrayBackend::ClipTrayBackend(QObject *parent)
    : QObject(parent)
{
}

ClipTrayPresenter::ClipTrayPresenter(ClipTrayController &controller,
                                     ClipTrayBackend &backend,
                                     QObject *parent)
    : QObject(parent)
    , controller_(controller)
    , backend_(backend)
{
    connect(&controller_, &ClipTrayController::statusChanged, this, &ClipTrayPresenter::syncStatus);
    connect(&controller_, &ClipTrayController::errorChanged, this, &ClipTrayPresenter::syncStatus);
    connect(&controller_, &ClipTrayController::trayActionsChanged, this, &ClipTrayPresenter::syncActions);
    connect(&backend_, &ClipTrayBackend::actionTriggered, this, [this](const QString &actionId) {
        controller_.triggerAction(actionId);
    });
    connect(&backend_, &ClipTrayBackend::primaryActivated, &controller_, &ClipTrayController::requestShowPicker);

    syncAll();
}

void ClipTrayPresenter::show()
{
    backend_.setVisible(true);
}

void ClipTrayPresenter::hide()
{
    backend_.setVisible(false);
}

QString ClipTrayPresenter::toolTipText() const
{
    return toolTipText_;
}

QList<ClipTrayPresentedAction> ClipTrayPresenter::presentedActions() const
{
    return presentedActions_;
}

void ClipTrayPresenter::syncStatus()
{
    toolTipText_ = buildToolTip();
    backend_.setToolTip(toolTipText_);
}

void ClipTrayPresenter::syncActions()
{
    presentedActions_.clear();
    for (const ClipTrayAction &action : controller_.actions()) {
        presentedActions_.append(presentedActionFromControllerAction(action));
    }
    backend_.setActions(presentedActions_);
}

void ClipTrayPresenter::syncAll()
{
    syncStatus();
    syncActions();
}

QString ClipTrayPresenter::buildToolTip() const
{
    QStringList lines;
    lines.append(QStringLiteral("Pinloom"));
    lines.append(QStringLiteral("Clip: %1").arg(controller_.isRunning()
                                                    ? QStringLiteral("running")
                                                    : QStringLiteral("stopped")));

    if (controller_.hotkeyRegistrationEnabled()) {
        lines.append(QStringLiteral("Hotkey: %1 (%2)")
                         .arg(controller_.hotkeyRegistered()
                                  ? QStringLiteral("registered")
                                  : QStringLiteral("not registered"),
                              controller_.hotkeyDisplayText()));
    } else {
        lines.append(QStringLiteral("Hotkey: handled by Command Window"));
    }

    QString capture = QStringLiteral("stopped");
    if (controller_.isRunning()) {
        capture = controller_.capturePaused()
            ? QStringLiteral("paused")
            : QStringLiteral("active");
    }
    lines.append(QStringLiteral("Clip capture: %1").arg(capture));

    const QString error = controller_.lastError().trimmed();
    if (!error.isEmpty()) {
        lines.append(QStringLiteral("Last error: %1").arg(error));
    }
    return lines.join(QLatin1Char('\n'));
}

QtSystemTrayIconBackend::QtSystemTrayIconBackend(QObject *parent)
    : QtSystemTrayIconBackend({}, parent)
{
}

QtSystemTrayIconBackend::QtSystemTrayIconBackend(const QIcon &icon, QObject *parent)
    : ClipTrayBackend(parent)
    , trayIcon_(new QSystemTrayIcon(resolveIcon(icon), this))
    , menu_(new QMenu())
{
    menu_->setObjectName(QStringLiteral("clipTrayMenu"));
    trayIcon_->setContextMenu(menu_);
    connect(trayIcon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            emit primaryActivated();
        }
    });
}

QtSystemTrayIconBackend::~QtSystemTrayIconBackend()
{
    if (trayIcon_) {
        trayIcon_->hide();
        trayIcon_->setContextMenu(nullptr);
    }
    delete menu_;
}

void QtSystemTrayIconBackend::setToolTip(const QString &toolTip)
{
    trayIcon_->setToolTip(toolTip);
}

void QtSystemTrayIconBackend::setActions(const QList<ClipTrayPresentedAction> &actions)
{
    menu_->clear();
    for (const ClipTrayPresentedAction &presentedAction : actions) {
        QAction *action = menu_->addAction(presentedAction.title);
        action->setObjectName(QStringLiteral("clipTrayAction_%1").arg(presentedAction.id));
        action->setData(presentedAction.id);
        action->setEnabled(presentedAction.enabled);
        action->setCheckable(presentedAction.checkable);
        action->setChecked(presentedAction.checked);
        connect(action, &QAction::triggered, this, [this, presentedAction]() {
            emit actionTriggered(presentedAction.id);
        });
    }
}

void QtSystemTrayIconBackend::setVisible(bool visible)
{
    if (visible) {
        trayIcon_->show();
    } else {
        trayIcon_->hide();
    }
}

QIcon QtSystemTrayIconBackend::resolveIcon(const QIcon &icon) const
{
    if (!icon.isNull()) {
        return icon;
    }

    const QIcon themedIcon = QIcon::fromTheme(QStringLiteral("edit-paste"));
    if (!themedIcon.isNull()) {
        return themedIcon;
    }

    if (qobject_cast<QApplication *>(QCoreApplication::instance())) {
        const QIcon applicationIcon = QApplication::windowIcon();
        if (!applicationIcon.isNull()) {
            return applicationIcon;
        }
    }

    return {};
}

} // namespace Pinloom
