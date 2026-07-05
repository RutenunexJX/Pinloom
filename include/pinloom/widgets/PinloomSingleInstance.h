#pragma once

#include <QObject>
#include <QString>

class QLocalServer;

namespace Pinloom {

enum class PinloomSingleInstanceRole {
    Primary,
    Secondary,
    Error
};

struct PinloomSingleInstanceStartResult {
    PinloomSingleInstanceRole role = PinloomSingleInstanceRole::Error;
    QString error;
    bool activationSent = false;

    bool isPrimary() const;
    bool isSecondary() const;
    bool succeeded() const;
};

struct PinloomSingleInstanceOptions {
    QString serverName;
    int activationTimeoutMs = 300;
};

class PinloomSingleInstanceGuard final : public QObject {
    Q_OBJECT

public:
    explicit PinloomSingleInstanceGuard(PinloomSingleInstanceOptions options, QObject *parent = nullptr);
    ~PinloomSingleInstanceGuard() override;

    PinloomSingleInstanceStartResult start();
    bool isPrimary() const;
    QString serverName() const;
    QString lastError() const;

signals:
    void activationRequested(const QString &message);

private:
    PinloomSingleInstanceStartResult listenAsPrimary();
    PinloomSingleInstanceStartResult notifyExistingInstance() const;
    void handleIncomingActivation();

    PinloomSingleInstanceOptions options_;
    QLocalServer *server_ = nullptr;
    QString lastError_;
    bool primary_ = false;
};

QString defaultPinloomSingleInstanceServerName();

} // namespace Pinloom
