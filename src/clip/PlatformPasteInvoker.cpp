#include "pinloom/clip/PlatformPasteInvoker.h"

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef Q_OS_WIN
#include <vector>
#endif

namespace Pinloom {

namespace {

#ifdef Q_OS_WIN

WORD virtualKeyFor(PasteKey key)
{
    switch (key) {
    case PasteKey::Control:
        return VK_CONTROL;
    case PasteKey::V:
        return 'V';
    }

    return 0;
}

class WindowsPasteKeySender final : public PasteKeySender {
public:
    bool sendKeys(const PasteKeySequence &sequence) override
    {
        if (sequence.isEmpty()) {
            return true;
        }

        std::vector<INPUT> inputs;
        inputs.reserve(static_cast<std::size_t>(sequence.size()));

        for (const PasteKeyEvent &event : sequence) {
            const WORD virtualKey = virtualKeyFor(event.key);
            if (virtualKey == 0) {
                return false;
            }

            INPUT input = {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = virtualKey;
            if (event.action == PasteKeyAction::Release) {
                input.ki.dwFlags = KEYEVENTF_KEYUP;
            }
            inputs.push_back(input);
        }

        const UINT expected = static_cast<UINT>(inputs.size());
        const UINT sent = SendInput(expected, inputs.data(), static_cast<int>(sizeof(INPUT)));
        return sent == expected;
    }
};

#else

class UnavailablePasteKeySender final : public PasteKeySender {
public:
    bool isAvailable() const override
    {
        return false;
    }

    bool sendKeys(const PasteKeySequence &) override
    {
        return false;
    }
};

#endif

} // namespace

bool PasteKeyEvent::operator==(const PasteKeyEvent &other) const
{
    return key == other.key && action == other.action;
}

bool PlatformPasteResult::pasted() const
{
    return status == PlatformPasteStatus::Invoked;
}

bool PasteKeySender::isAvailable() const
{
    return true;
}

PlatformPasteInvoker::PlatformPasteInvoker(PasteKeySender *sender)
    : sender_(sender ? sender : defaultPlatformPasteKeySender())
{
}

PlatformPasteResult PlatformPasteInvoker::invoke() const
{
    if (!sender_ || !sender_->isAvailable()) {
        return {PlatformPasteStatus::Unavailable, QStringLiteral("Platform paste invoker is unavailable")};
    }

    if (!sender_->sendKeys(ctrlVPasteSequence())) {
        return {PlatformPasteStatus::SendFailed, QStringLiteral("Unable to send Ctrl+V input")};
    }

    return {PlatformPasteStatus::Invoked, {}};
}

bool PlatformPasteInvoker::invokePaste() const
{
    return invoke().pasted();
}

PasteKeySequence PlatformPasteInvoker::ctrlVPasteSequence()
{
    return {{PasteKey::Control, PasteKeyAction::Press},
            {PasteKey::V, PasteKeyAction::Press},
            {PasteKey::V, PasteKeyAction::Release},
            {PasteKey::Control, PasteKeyAction::Release}};
}

PasteKeySender *defaultPlatformPasteKeySender()
{
#ifdef Q_OS_WIN
    static WindowsPasteKeySender sender;
#else
    static UnavailablePasteKeySender sender;
#endif
    return &sender;
}

std::function<bool()> createPlatformPasteInvoker(PasteKeySender *sender)
{
    return [sender]() {
        PlatformPasteInvoker invoker(sender);
        return invoker.invokePaste();
    };
}

} // namespace Pinloom
