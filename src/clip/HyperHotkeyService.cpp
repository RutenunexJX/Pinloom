#include "pinloom/clip/HyperHotkeyService.h"

#include <QMetaObject>
#include <utility>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Pinloom {

HyperHotkeyMatchResult HyperHotkeyStateMachine::process(HyperKeyRole role, bool pressed)
{
    HyperHotkeyMatchResult result;
    const auto processTrigger = [this, pressed, &result](bool &triggerPressed,
                                                         HyperHotkeyAction action) {
        if (pressed) {
            if (armed() || triggerPressed) {
                result.consume = true;
            }
            if (armed() && !triggerPressed) {
                triggerPressed = true;
                if (!activatedInChord_) {
                    activatedInChord_ = true;
                    activatedWithF24_ = f24Pressed_;
                    activatedWithLayerChord_ = !f24Pressed_
                        && controlPressed_
                        && altPressed_
                        && shiftPressed_
                        && layerPressed_;
                    result.activated = true;
                    result.action = action;
                }
            }
        } else if (!pressed && triggerPressed) {
            result.consume = true;
            triggerPressed = false;
        }
    };

    switch (role) {
    case HyperKeyRole::Control:
        controlPressed_ = pressed;
        break;
    case HyperKeyRole::Alt:
        altPressed_ = pressed;
        break;
    case HyperKeyRole::Shift:
        shiftPressed_ = pressed;
        break;
    case HyperKeyRole::Layer:
        layerPressed_ = pressed;
        break;
    case HyperKeyRole::F24:
        f24Pressed_ = pressed;
        break;
    case HyperKeyRole::SaveTrigger:
        processTrigger(saveTriggerPressed_, HyperHotkeyAction::Save);
        break;
    case HyperKeyRole::InsertTrigger:
        processTrigger(insertTriggerPressed_, HyperHotkeyAction::Insert);
        break;
    case HyperKeyRole::Other:
        break;
    }

    const bool triggersReleased = !saveTriggerPressed_ && !insertTriggerPressed_;
    const bool f24CarrierReleased = !activatedWithF24_ || !f24Pressed_;
    const bool layerCarrierReleased = !activatedWithLayerChord_
        || (!controlPressed_ && !altPressed_ && !shiftPressed_ && !layerPressed_);
    if (activatedInChord_
        && triggersReleased
        && f24CarrierReleased
        && layerCarrierReleased) {
        activatedInChord_ = false;
        activatedWithF24_ = false;
        activatedWithLayerChord_ = false;
        result.chordReleased = true;
    }

    return result;
}

void HyperHotkeyStateMachine::reset()
{
    controlPressed_ = false;
    altPressed_ = false;
    shiftPressed_ = false;
    layerPressed_ = false;
    f24Pressed_ = false;
    saveTriggerPressed_ = false;
    insertTriggerPressed_ = false;
    activatedInChord_ = false;
    activatedWithF24_ = false;
    activatedWithLayerChord_ = false;
}

bool HyperHotkeyStateMachine::armed() const
{
    return f24Pressed_
        || (controlPressed_ && altPressed_ && shiftPressed_ && layerPressed_);
}

HyperHotkeyBackend::HyperHotkeyBackend(QObject *parent)
    : QObject(parent)
{
}

bool HyperHotkeyBackend::isAvailable() const
{
    return true;
}

namespace {

#ifdef Q_OS_WIN

QString windowsErrorText(const QString &action)
{
    return QStringLiteral("%1 (Win32 error %2)").arg(action).arg(GetLastError());
}

HyperKeyRole roleForVirtualKey(DWORD virtualKey)
{
    switch (virtualKey) {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        return HyperKeyRole::Control;
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        return HyperKeyRole::Alt;
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        return HyperKeyRole::Shift;
    case VK_OEM_3:
        return HyperKeyRole::Layer;
    case VK_F24:
        return HyperKeyRole::F24;
    case 'S':
        return HyperKeyRole::SaveTrigger;
    case 'V':
        return HyperKeyRole::InsertTrigger;
    default:
        return HyperKeyRole::Other;
    }
}

class WindowsHyperHotkeyBackend final : public HyperHotkeyBackend {
public:
    explicit WindowsHyperHotkeyBackend(QObject *parent = nullptr)
        : HyperHotkeyBackend(parent)
    {
    }

    ~WindowsHyperHotkeyBackend() override
    {
        stop();
    }

    bool start(QString *error) override
    {
        if (hook_) {
            if (error) {
                error->clear();
            }
            return true;
        }
        if (activeBackend_ && activeBackend_ != this) {
            if (error) {
                *error = QStringLiteral("Another Hyper hotkey backend is already active");
            }
            return false;
        }

        activeBackend_ = this;
        hook_ = SetWindowsHookExW(WH_KEYBOARD_LL,
                                  &WindowsHyperHotkeyBackend::keyboardHook,
                                  GetModuleHandleW(nullptr),
                                  0);
        if (!hook_) {
            activeBackend_ = nullptr;
            if (error) {
                *error = windowsErrorText(QStringLiteral("Unable to install Hyper keyboard hook"));
            }
            return false;
        }
        if (error) {
            error->clear();
        }
        return true;
    }

    void stop() override
    {
        if (hook_) {
            UnhookWindowsHookEx(hook_);
            hook_ = nullptr;
        }
        if (activeBackend_ == this) {
            activeBackend_ = nullptr;
        }
        matcher_.reset();
    }

private:
    static LRESULT CALLBACK keyboardHook(int code, WPARAM message, LPARAM data)
    {
        if (code < 0 || !activeBackend_ || !data) {
            return CallNextHookEx(nullptr, code, message, data);
        }

        const auto *event = reinterpret_cast<const KBDLLHOOKSTRUCT *>(data);
        const bool pressed = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const bool released = message == WM_KEYUP || message == WM_SYSKEYUP;
        if (!pressed && !released) {
            return CallNextHookEx(nullptr, code, message, data);
        }

        const HyperHotkeyMatchResult result =
            activeBackend_->matcher_.process(roleForVirtualKey(event->vkCode), pressed);
        if (result.activated) {
            QMetaObject::invokeMethod(activeBackend_, [backend = activeBackend_, action = result.action]() {
                if (backend == activeBackend_) {
                    emit backend->activated(action);
                }
            }, Qt::QueuedConnection);
        }
        if (result.chordReleased) {
            QMetaObject::invokeMethod(activeBackend_, [backend = activeBackend_]() {
                if (backend == activeBackend_) {
                    emit backend->chordReleased();
                }
            }, Qt::QueuedConnection);
        }
        return result.consume ? 1 : CallNextHookEx(nullptr, code, message, data);
    }

    inline static WindowsHyperHotkeyBackend *activeBackend_ = nullptr;
    HHOOK hook_ = nullptr;
    HyperHotkeyStateMachine matcher_;
};

#else

class UnavailableHyperHotkeyBackend final : public HyperHotkeyBackend {
public:
    explicit UnavailableHyperHotkeyBackend(QObject *parent = nullptr)
        : HyperHotkeyBackend(parent)
    {
    }

    bool isAvailable() const override
    {
        return false;
    }

    bool start(QString *error) override
    {
        if (error) {
            *error = QStringLiteral("Hyper hotkeys are unavailable on this platform");
        }
        return false;
    }

    void stop() override
    {
    }
};

#endif

} // namespace

HyperHotkeyService::HyperHotkeyService(HyperHotkeyBackend *backend, QObject *parent)
    : QObject(parent)
    , backend_(backend)
{
}

HyperHotkeyService::~HyperHotkeyService()
{
    stop();
}

void HyperHotkeyService::setActivationHandler(ActivationHandler handler)
{
    activationHandler_ = std::move(handler);
}

bool HyperHotkeyService::start()
{
    if (registered_) {
        return true;
    }
    if (!backend_ || !backend_->isAvailable()) {
        setLastError(QStringLiteral("Hyper hotkey backend is unavailable"));
        return false;
    }

    QString error;
    if (!backend_->start(&error)) {
        setLastError(error.trimmed().isEmpty()
                         ? QStringLiteral("Unable to start Hyper hotkey")
                         : error.trimmed());
        return false;
    }
    backendConnection_ = connect(backend_,
                                 &HyperHotkeyBackend::activated,
                                 this,
                                 &HyperHotkeyService::handleActivated);
    backendReleaseConnection_ = connect(backend_,
                                        &HyperHotkeyBackend::chordReleased,
                                        this,
                                        &HyperHotkeyService::chordReleased);
    registered_ = true;
    setLastError({});
    emit registeredChanged(true);
    return true;
}

void HyperHotkeyService::stop()
{
    if (!registered_) {
        return;
    }
    disconnect(backendConnection_);
    backendConnection_ = {};
    disconnect(backendReleaseConnection_);
    backendReleaseConnection_ = {};
    if (backend_) {
        backend_->stop();
    }
    registered_ = false;
    emit registeredChanged(false);
}

bool HyperHotkeyService::isRegistered() const
{
    return registered_;
}

QString HyperHotkeyService::displayText() const
{
    return QStringLiteral("F24+S / F24+V");
}

QString HyperHotkeyService::lastError() const
{
    return lastError_;
}

void HyperHotkeyService::handleActivated(HyperHotkeyAction action)
{
    if (!registered_) {
        return;
    }
    if (activationHandler_) {
        activationHandler_(action);
    }
    emit activated(action);
}

void HyperHotkeyService::setLastError(const QString &error)
{
    if (lastError_ == error) {
        return;
    }
    lastError_ = error;
    emit errorChanged(lastError_);
}

std::unique_ptr<HyperHotkeyBackend> createHyperHotkeyBackend()
{
#ifdef Q_OS_WIN
    return std::make_unique<WindowsHyperHotkeyBackend>();
#else
    return std::make_unique<UnavailableHyperHotkeyBackend>();
#endif
}

bool dismissHyperModifierUiState()
{
#ifdef Q_OS_WIN
    INPUT inputs[2]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_MENU;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_MENU;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(2, inputs, sizeof(INPUT)) == 2;
#else
    return false;
#endif
}

} // namespace Pinloom
