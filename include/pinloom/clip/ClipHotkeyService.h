#pragma once

#include <QObject>
#include <QString>
#include <Qt>
#include <functional>

namespace Pinloom {

struct ClipHotkeyConfig {
    Qt::Key key = Qt::Key_V;
    Qt::KeyboardModifiers modifiers = Qt::ControlModifier | Qt::ShiftModifier;

    bool isValid() const;
    QString displayText() const;
    bool operator==(const ClipHotkeyConfig &other) const;
    bool operator!=(const ClipHotkeyConfig &other) const;
};

ClipHotkeyConfig defaultClipHotkeyConfig();

class ClipHotkeyBackend : public QObject {
    Q_OBJECT

public:
    explicit ClipHotkeyBackend(QObject *parent = nullptr);
    ~ClipHotkeyBackend() override = default;

    virtual bool isAvailable() const;
    virtual bool registerHotkey(const ClipHotkeyConfig &config, QString *error) = 0;
    virtual void unregisterHotkey() = 0;

signals:
    void hotkeyActivated();
};

class ClipHotkeyService : public QObject {
    Q_OBJECT

public:
    using ActivationHandler = std::function<void()>;

    explicit ClipHotkeyService(QObject *parent = nullptr);
    explicit ClipHotkeyService(ClipHotkeyBackend *backend, QObject *parent = nullptr);
    ClipHotkeyService(ClipHotkeyConfig config, ClipHotkeyBackend *backend, QObject *parent = nullptr);
    ~ClipHotkeyService() override;

    void setConfig(const ClipHotkeyConfig &config);
    ClipHotkeyConfig config() const;
    QString displayText() const;

    void setActivationHandler(ActivationHandler handler);

    bool start();
    void stop();
    bool isRegistered() const;
    QString lastError() const;

signals:
    void activated();
    void registeredChanged(bool registered);
    void errorChanged(const QString &error);

private:
    void handleBackendActivated();
    void setLastError(const QString &error);

    ClipHotkeyConfig config_;
    ClipHotkeyBackend *backend_ = nullptr;
    ActivationHandler activationHandler_;
    QMetaObject::Connection backendConnection_;
    QString lastError_;
    bool registered_ = false;
};

class ClipPickerHotkeyController : public QObject {
    Q_OBJECT

public:
    using ShowHandler = std::function<void()>;

    explicit ClipPickerHotkeyController(ClipHotkeyService &service, QObject *parent = nullptr);
    ClipPickerHotkeyController(ClipHotkeyService &service, ShowHandler showHandler, QObject *parent = nullptr);

    void setShowHandler(ShowHandler showHandler);

signals:
    void showRequested();

private:
    void handleHotkeyActivated();

    ClipHotkeyService &service_;
    ShowHandler showHandler_;
};

ClipHotkeyBackend *defaultClipHotkeyBackend();

} // namespace Pinloom
