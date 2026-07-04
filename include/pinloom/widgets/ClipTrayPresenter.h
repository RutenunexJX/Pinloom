#pragma once

#include "pinloom/clip/ClipTrayController.h"

#include <QIcon>
#include <QList>
#include <QObject>
#include <QString>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace Pinloom {

struct ClipTrayPresentedAction {
    QString id;
    QString title;
    bool enabled = true;
    bool checked = false;
    bool checkable = false;
};

class ClipTrayBackend : public QObject {
    Q_OBJECT

public:
    explicit ClipTrayBackend(QObject *parent = nullptr);
    ~ClipTrayBackend() override = default;

    virtual void setToolTip(const QString &toolTip) = 0;
    virtual void setActions(const QList<ClipTrayPresentedAction> &actions) = 0;
    virtual void setVisible(bool visible) = 0;

signals:
    void actionTriggered(const QString &actionId);
    void primaryActivated();
};

class ClipTrayPresenter final : public QObject {
    Q_OBJECT

public:
    explicit ClipTrayPresenter(ClipTrayController &controller, ClipTrayBackend &backend, QObject *parent = nullptr);

    void show();
    void hide();
    QString toolTipText() const;
    QList<ClipTrayPresentedAction> presentedActions() const;

private:
    void syncStatus();
    void syncActions();
    void syncAll();
    QString buildToolTip() const;

    ClipTrayController &controller_;
    ClipTrayBackend &backend_;
    QString toolTipText_;
    QList<ClipTrayPresentedAction> presentedActions_;
};

class QtSystemTrayIconBackend final : public ClipTrayBackend {
    Q_OBJECT

public:
    explicit QtSystemTrayIconBackend(QObject *parent = nullptr);
    explicit QtSystemTrayIconBackend(const QIcon &icon, QObject *parent = nullptr);
    ~QtSystemTrayIconBackend() override;

    void setToolTip(const QString &toolTip) override;
    void setActions(const QList<ClipTrayPresentedAction> &actions) override;
    void setVisible(bool visible) override;

private:
    QIcon resolveIcon(const QIcon &icon) const;

    QSystemTrayIcon *trayIcon_ = nullptr;
    QMenu *menu_ = nullptr;
};

} // namespace Pinloom
