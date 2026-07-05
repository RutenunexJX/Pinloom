#include "pinloom/widgets/PinloomSingleInstance.h"

#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <utility>

namespace Pinloom {

namespace {

QString normalizedServerName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty()) {
        return trimmed;
    }
    return defaultPinloomSingleInstanceServerName();
}

PinloomSingleInstanceStartResult result(PinloomSingleInstanceRole role,
                                        const QString &error = {},
                                        bool activationSent = false)
{
    PinloomSingleInstanceStartResult result;
    result.role = role;
    result.error = error;
    result.activationSent = activationSent;
    return result;
}

} // namespace

bool PinloomSingleInstanceStartResult::isPrimary() const
{
    return role == PinloomSingleInstanceRole::Primary;
}

bool PinloomSingleInstanceStartResult::isSecondary() const
{
    return role == PinloomSingleInstanceRole::Secondary;
}

bool PinloomSingleInstanceStartResult::succeeded() const
{
    return role != PinloomSingleInstanceRole::Error;
}

PinloomSingleInstanceGuard::PinloomSingleInstanceGuard(PinloomSingleInstanceOptions options, QObject *parent)
    : QObject(parent)
    , options_(std::move(options))
    , server_(new QLocalServer(this))
{
    options_.serverName = normalizedServerName(options_.serverName);
    connect(server_, &QLocalServer::newConnection, this, &PinloomSingleInstanceGuard::handleIncomingActivation);
}

PinloomSingleInstanceGuard::~PinloomSingleInstanceGuard()
{
    if (server_->isListening()) {
        server_->close();
    }
}

PinloomSingleInstanceStartResult PinloomSingleInstanceGuard::start()
{
    if (primary_) {
        return result(PinloomSingleInstanceRole::Primary);
    }

    const PinloomSingleInstanceStartResult activation = notifyExistingInstance();
    if (activation.isSecondary()) {
        lastError_.clear();
        return activation;
    }

    return listenAsPrimary();
}

bool PinloomSingleInstanceGuard::isPrimary() const
{
    return primary_;
}

QString PinloomSingleInstanceGuard::serverName() const
{
    return options_.serverName;
}

QString PinloomSingleInstanceGuard::lastError() const
{
    return lastError_;
}

PinloomSingleInstanceStartResult PinloomSingleInstanceGuard::listenAsPrimary()
{
    if (server_->listen(options_.serverName)) {
        primary_ = true;
        lastError_.clear();
        return result(PinloomSingleInstanceRole::Primary);
    }

    QLocalServer::removeServer(options_.serverName);
    if (server_->listen(options_.serverName)) {
        primary_ = true;
        lastError_.clear();
        return result(PinloomSingleInstanceRole::Primary);
    }

    lastError_ = server_->errorString().trimmed();
    if (lastError_.isEmpty()) {
        lastError_ = QStringLiteral("Unable to create Pinloom single-instance listener");
    }
    return result(PinloomSingleInstanceRole::Error, lastError_);
}

PinloomSingleInstanceStartResult PinloomSingleInstanceGuard::notifyExistingInstance() const
{
    QLocalSocket socket;
    socket.connectToServer(options_.serverName, QIODevice::WriteOnly);
    if (!socket.waitForConnected(options_.activationTimeoutMs)) {
        return result(PinloomSingleInstanceRole::Error, socket.errorString());
    }

    const QByteArray message("activate\n");
    socket.write(message);
    socket.waitForBytesWritten(options_.activationTimeoutMs);
    socket.disconnectFromServer();
    return result(PinloomSingleInstanceRole::Secondary, {}, true);
}

void PinloomSingleInstanceGuard::handleIncomingActivation()
{
    while (server_->hasPendingConnections()) {
        QLocalSocket *socket = server_->nextPendingConnection();
        QString message = QStringLiteral("activate");
        if (socket) {
            if (socket->bytesAvailable() > 0) {
                const QString payload = QString::fromUtf8(socket->readAll()).trimmed();
                if (!payload.isEmpty()) {
                    message = payload;
                }
            }
            connect(socket, &QLocalSocket::readyRead, socket, [socket]() {
                socket->readAll();
            });
            socket->disconnectFromServer();
            socket->deleteLater();
        }
        emit activationRequested(message);
    }
}

QString defaultPinloomSingleInstanceServerName()
{
    QString suffix = QCoreApplication::organizationName().trimmed();
    const QString appName = QCoreApplication::applicationName().trimmed();
    if (!suffix.isEmpty() && !appName.isEmpty()) {
        suffix.append(QLatin1Char('.'));
    }
    suffix.append(appName.isEmpty() ? QStringLiteral("Pinloom") : appName);
    return QStringLiteral("pinloom.%1.single-instance").arg(suffix.toLower());
}

} // namespace Pinloom
