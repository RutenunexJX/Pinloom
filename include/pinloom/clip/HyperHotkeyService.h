#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <memory>

namespace Pinloom {

enum class HyperKeyRole {
    Control,
    Alt,
    Shift,
    Layer,
    F24,
    SaveTrigger,
    InsertTrigger,
    Other
};

enum class HyperHotkeyAction {
    None,
    Save,
    Insert
};

struct HyperHotkeyMatchResult {
    bool consume = false;
    bool activated = false;
    bool chordReleased = false;
    HyperHotkeyAction action = HyperHotkeyAction::None;
};

class HyperHotkeyStateMachine {
public:
    HyperHotkeyMatchResult process(HyperKeyRole role, bool pressed);
    void reset();
    bool armed() const;

private:
    bool controlPressed_ = false;
    bool altPressed_ = false;
    bool shiftPressed_ = false;
    bool layerPressed_ = false;
    bool f24Pressed_ = false;
    bool saveTriggerPressed_ = false;
    bool insertTriggerPressed_ = false;
    bool activatedInChord_ = false;
    bool activatedWithF24_ = false;
    bool activatedWithLayerChord_ = false;
};

class HyperHotkeyBackend : public QObject {
    Q_OBJECT

public:
    explicit HyperHotkeyBackend(QObject *parent = nullptr);
    ~HyperHotkeyBackend() override = default;

    virtual bool isAvailable() const;
    virtual bool start(QString *error = nullptr) = 0;
    virtual void stop() = 0;

signals:
    void activated(Pinloom::HyperHotkeyAction action);
    void chordReleased();
};

class HyperHotkeyService final : public QObject {
    Q_OBJECT

public:
    using ActivationHandler = std::function<void(HyperHotkeyAction action)>;

    explicit HyperHotkeyService(HyperHotkeyBackend *backend, QObject *parent = nullptr);
    ~HyperHotkeyService() override;

    void setActivationHandler(ActivationHandler handler);
    bool start();
    void stop();
    bool isRegistered() const;
    QString displayText() const;
    QString lastError() const;

signals:
    void activated(Pinloom::HyperHotkeyAction action);
    void chordReleased();
    void registeredChanged(bool registered);
    void errorChanged(const QString &error);

private:
    void handleActivated(HyperHotkeyAction action);
    void setLastError(const QString &error);

    HyperHotkeyBackend *backend_ = nullptr;
    ActivationHandler activationHandler_;
    QMetaObject::Connection backendConnection_;
    QMetaObject::Connection backendReleaseConnection_;
    QString lastError_;
    bool registered_ = false;
};

std::unique_ptr<HyperHotkeyBackend> createHyperHotkeyBackend();
bool dismissHyperModifierUiState();

} // namespace Pinloom
