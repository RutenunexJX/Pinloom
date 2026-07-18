#include "pinloom/widgets/PinloomSingleInstance.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>
#include <QVariant>
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
    QString error;
    if (!sendPinloomSingleInstanceMessage(options_.serverName,
                                          options_.activationMessage,
                                          options_.activationTimeoutMs,
                                          &error)) {
        return result(PinloomSingleInstanceRole::Error, error);
    }
    return result(PinloomSingleInstanceRole::Secondary, {}, true);
}

void PinloomSingleInstanceGuard::handleIncomingActivation()
{
    while (server_->hasPendingConnections()) {
        QLocalSocket *socket = server_->nextPendingConnection();
        if (!socket) {
            continue;
        }
        socket->setParent(this);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            finishIncomingActivation(socket, false);
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            finishIncomingActivation(socket, true);
        });
        QTimer::singleShot(options_.activationTimeoutMs, socket, [this, socket]() {
            finishIncomingActivation(socket, true);
        });
        finishIncomingActivation(socket, false);
    }
}

void PinloomSingleInstanceGuard::finishIncomingActivation(QLocalSocket *socket,
                                                           bool allowIncompletePayload)
{
    if (!socket || socket->property("pinloomActivationHandled").toBool()) {
        return;
    }

    QByteArray payload = socket->property("pinloomActivationPayload").toByteArray();
    payload.append(socket->readAll());
    socket->setProperty("pinloomActivationPayload", payload);
    if (!allowIncompletePayload && !payload.contains('\n')) {
        return;
    }

    socket->setProperty("pinloomActivationHandled", true);
    const QString message = QString::fromUtf8(payload).trimmed();
    emit activationRequested(message.isEmpty() ? QStringLiteral("activate") : message);
    socket->write("ok\n");
    socket->flush();
    socket->disconnectFromServer();
    socket->deleteLater();
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

bool sendPinloomSingleInstanceMessage(const QString &serverName,
                                      const QString &message,
                                      int timeoutMs,
                                      QString *error)
{
    QLocalSocket socket;
    socket.connectToServer(normalizedServerName(serverName), QIODevice::ReadWrite);
    if (!socket.waitForConnected(timeoutMs)) {
        if (error) {
            const QString socketError = socket.errorString().trimmed();
            *error = socketError.isEmpty() || socketError == QLatin1String("Unknown error")
                ? QStringLiteral("Unable to connect to Pinloom single-instance listener")
                : socketError;
        }
        return false;
    }

    QByteArray payload = message.trimmed().isEmpty()
        ? QByteArray("activate")
        : message.trimmed().toUtf8();
    if (!payload.endsWith('\n')) {
        payload.append('\n');
    }

    const qint64 written = socket.write(payload);
    if (written != payload.size()) {
        if (error) {
            *error = socket.errorString().trimmed().isEmpty()
                || socket.errorString() == QLatin1String("Unknown error")
                ? QStringLiteral("Unable to write Pinloom single-instance message")
                : socket.errorString();
        }
        socket.disconnectFromServer();
        return false;
    }

    socket.flush();
    if (QCoreApplication::instance()) {
        QEventLoop deliveryLoop;
        QTimer deliveryTimer;
        deliveryTimer.setSingleShot(true);
        QObject::connect(&socket, &QLocalSocket::readyRead, &deliveryLoop, &QEventLoop::quit);
        QObject::connect(&socket, &QLocalSocket::disconnected, &deliveryLoop, &QEventLoop::quit);
        QObject::connect(&deliveryTimer, &QTimer::timeout, &deliveryLoop, &QEventLoop::quit);
        deliveryTimer.start(timeoutMs);
        deliveryLoop.exec();
    } else if (socket.bytesToWrite() > 0) {
        socket.waitForBytesWritten(timeoutMs);
    }
    if (socket.bytesToWrite() > 0) {
        if (error) {
            const QString socketError = socket.errorString().trimmed();
            *error = socketError.isEmpty() || socketError == QLatin1String("Unknown error")
                ? QStringLiteral("Unable to finish writing Pinloom single-instance message")
                : socketError;
        }
        socket.disconnectFromServer();
        return false;
    }
    socket.readAll();

    socket.disconnectFromServer();
    if (error) {
        error->clear();
    }
    return true;
}

} // namespace Pinloom
