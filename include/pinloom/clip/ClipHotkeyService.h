#pragma once

#include <QObject>
#include <QString>
#include <Qt>
#include <memory>

namespace Pinloom {

struct ClipHotkeyConfig {
    Qt::Key key = Qt::Key_unknown;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;

    bool isValid() const;
    QString displayText() const;
    bool operator==(const ClipHotkeyConfig &other) const;
    bool operator!=(const ClipHotkeyConfig &other) const;
};

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
    ClipHotkeyService(ClipHotkeyConfig config, ClipHotkeyBackend *backend, QObject *parent = nullptr);
    ~ClipHotkeyService() override;

    void setConfig(const ClipHotkeyConfig &config);
    ClipHotkeyConfig config() const;
    QString displayText() const;

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
    QMetaObject::Connection backendConnection_;
    QString lastError_;
    bool registered_ = false;
};

std::unique_ptr<ClipHotkeyBackend> createClipHotkeyBackend(int hotkeyId);

} // namespace Pinloom
