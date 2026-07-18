#pragma once

#include "pinloom/clip/ClipHotkeyService.h"

#include <QObject>
#include <functional>
#include <memory>

namespace Pinloom {

ClipHotkeyConfig defaultMainPanelHotkeyConfig();
std::unique_ptr<ClipHotkeyBackend> createMainPanelHotkeyBackend();

class MainPanelHotkeyController final : public QObject {
    Q_OBJECT

public:
    using ShowHandler = std::function<void()>;

    MainPanelHotkeyController(ClipHotkeyService &service,
                              ShowHandler showHandler,
                              QObject *parent = nullptr);

private:
    void handleHotkeyActivated();

    ClipHotkeyService &service_;
    ShowHandler showHandler_;
};

} // namespace Pinloom
