#include "pinloom/widgets/MainPanelHotkey.h"

#include "pinloom/widgets/PinloomPanel.h"

#include <QWidget>
#include <utility>

namespace Pinloom {

namespace {

constexpr int MainPanelHotkeyId = 0x504d414e;

} // namespace

ClipHotkeyConfig defaultMainPanelHotkeyConfig()
{
    ClipHotkeyConfig config;
    config.key = Qt::Key_Space;
    config.modifiers = Qt::ControlModifier;
    return config;
}

std::unique_ptr<ClipHotkeyBackend> createMainPanelHotkeyBackend()
{
    return createClipHotkeyBackend(MainPanelHotkeyId);
}

void showMainPanelForHotkey(QWidget &mainWindow, PinloomPanel &panel)
{
    if (mainWindow.isMinimized()) {
        mainWindow.showNormal();
    } else {
        mainWindow.show();
    }

    mainWindow.raise();
    mainWindow.activateWindow();
    panel.focusSearch();
}

MainPanelHotkeyController::MainPanelHotkeyController(ClipHotkeyService &service, QObject *parent)
    : MainPanelHotkeyController(service, ShowHandler{}, parent)
{
}

MainPanelHotkeyController::MainPanelHotkeyController(ClipHotkeyService &service,
                                                     QWidget &mainWindow,
                                                     PinloomPanel &panel,
                                                     QObject *parent)
    : MainPanelHotkeyController(service,
                                [&mainWindow, &panel]() {
                                    showMainPanelForHotkey(mainWindow, panel);
                                },
                                parent)
{
}

MainPanelHotkeyController::MainPanelHotkeyController(ClipHotkeyService &service,
                                                     ShowHandler showHandler,
                                                     QObject *parent)
    : QObject(parent)
    , service_(service)
    , showHandler_(std::move(showHandler))
{
    connect(&service_, &ClipHotkeyService::activated, this, &MainPanelHotkeyController::handleHotkeyActivated);
}

void MainPanelHotkeyController::setShowHandler(ShowHandler showHandler)
{
    showHandler_ = std::move(showHandler);
}

void MainPanelHotkeyController::handleHotkeyActivated()
{
    if (showHandler_) {
        showHandler_();
    }
    emit showRequested();
}

} // namespace Pinloom
