#include "pinloom/widgets/MainPanelHotkey.h"

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

MainPanelHotkeyController::MainPanelHotkeyController(ClipHotkeyService &service,
                                                     ShowHandler showHandler,
                                                     QObject *parent)
    : QObject(parent)
    , service_(service)
    , showHandler_(std::move(showHandler))
{
    connect(&service_, &ClipHotkeyService::activated, this, &MainPanelHotkeyController::handleHotkeyActivated);
}

void MainPanelHotkeyController::handleHotkeyActivated()
{
    if (showHandler_) {
        showHandler_();
    }
}

} // namespace Pinloom
