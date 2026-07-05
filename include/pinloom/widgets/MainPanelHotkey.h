#pragma once

#include "pinloom/clip/ClipHotkeyService.h"

#include <QObject>
#include <functional>
#include <memory>

class QWidget;

namespace Pinloom {

class PinloomPanel;

ClipHotkeyConfig defaultMainPanelHotkeyConfig();
std::unique_ptr<ClipHotkeyBackend> createMainPanelHotkeyBackend();

void showMainPanelForHotkey(QWidget &mainWindow, PinloomPanel &panel);

class MainPanelHotkeyController final : public QObject {
    Q_OBJECT

public:
    using ShowHandler = std::function<void()>;

    explicit MainPanelHotkeyController(ClipHotkeyService &service, QObject *parent = nullptr);
    MainPanelHotkeyController(ClipHotkeyService &service,
                              QWidget &mainWindow,
                              PinloomPanel &panel,
                              QObject *parent = nullptr);
    MainPanelHotkeyController(ClipHotkeyService &service,
                              ShowHandler showHandler,
                              QObject *parent = nullptr);

    void setShowHandler(ShowHandler showHandler);

signals:
    void showRequested();

private:
    void handleHotkeyActivated();

    ClipHotkeyService &service_;
    ShowHandler showHandler_;
};

} // namespace Pinloom
