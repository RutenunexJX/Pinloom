#pragma once

#include <QString>
#include <QVector>
#include <functional>

namespace Pinloom {

enum class PasteKey {
    Control,
    V
};

enum class PasteKeyAction {
    Press,
    Release
};

struct PasteKeyEvent {
    PasteKey key = PasteKey::Control;
    PasteKeyAction action = PasteKeyAction::Press;

    bool operator==(const PasteKeyEvent &other) const;
};

using PasteKeySequence = QVector<PasteKeyEvent>;

enum class PlatformPasteStatus {
    Invoked,
    Unavailable,
    SendFailed
};

struct PlatformPasteResult {
    PlatformPasteStatus status = PlatformPasteStatus::Unavailable;
    QString error;

    bool pasted() const;
};

class PasteKeySender {
public:
    virtual ~PasteKeySender() = default;

    virtual bool isAvailable() const;
    virtual bool sendKeys(const PasteKeySequence &sequence) = 0;
};

class PlatformPasteInvoker {
public:
    explicit PlatformPasteInvoker(PasteKeySender *sender = nullptr);

    PlatformPasteResult invoke() const;
    bool invokePaste() const;

    static PasteKeySequence ctrlVPasteSequence();

private:
    PasteKeySender *sender_ = nullptr;
};

PasteKeySender *defaultPlatformPasteKeySender();
std::function<bool()> createPlatformPasteInvoker(PasteKeySender *sender = nullptr);

} // namespace Pinloom
