#include "pinloom/application/CommandTransport.h"
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTimer>

namespace Pinloom {

LibrarySessionLock::LibrarySessionLock(const QString &path)
    : lock_(QDir(path).filePath(QStringLiteral(".pinloom-session.lock")))
{
    lock_.setStaleLockTime(0);
}
bool LibrarySessionLock::tryLock() { return lock_.tryLock(0); }

CommandServer::CommandServer(CommandService &service, QObject *parent)
    : QObject(parent), service_(service)
{
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    server_.setMaxPendingConnections(8);
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = server_.nextPendingConnection()) {
            socket->setParent(this);
            socket->setReadBufferSize(CommandMaxRequestBytes + 1);
            auto buffer = std::make_shared<QByteArray>();
            auto done = std::make_shared<bool>(false);
            auto *timer = new QTimer(socket);
            timer->setSingleShot(true);
            connect(timer, &QTimer::timeout, socket, [socket] { socket->abort(); socket->deleteLater(); });
            timer->start(10000);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer, done] {
                if (*done) return;
                *buffer += socket->readAll();
                QJsonObject response;
                const auto end = buffer->indexOf('\n');
                if (buffer->size() > CommandMaxRequestBytes) {
                    response = commandError("input_limit", "Request exceeds 1 MiB", 2);
                } else if (end < 0) {
                    return;
                } else {
                    QJsonParseError error;
                    const auto document = QJsonDocument::fromJson(buffer->left(end), &error);
                    if (error.error != QJsonParseError::NoError || !document.isObject()
                        || !buffer->mid(end + 1).trimmed().isEmpty()) {
                        response = commandError("invalid_json", "Expected one JSON request object", 2);
                    } else {
                        *done = true; // Nested GUI event loops must not execute this request twice.
                        response = service_.execute(document.object());
                    }
                }
                *done = true;
                auto bytes = QJsonDocument(response).toJson(QJsonDocument::Compact);
                if (bytes.size() > CommandMaxResponseBytes) {
                    bytes = QJsonDocument(commandError("output_limit", "Use a smaller limit or archive export", 2))
                                .toJson(QJsonDocument::Compact);
                }
                socket->write(bytes + '\n');
                socket->disconnectFromServer();
            });
        }
    });
}
bool CommandServer::listen(const QString &path)
{
    // The caller must already own LibrarySessionLock, so a stale endpoint is safe to remove.
    const auto name = libraryCommandEndpoint(path);
    QLocalServer::removeServer(name);
    return server_.listen(name);
}
QString CommandServer::errorString() const { return server_.errorString(); }

CommandClientResult sendLibraryCommand(const QString &path, const QJsonObject &request, int timeoutMs)
{
    QLocalSocket socket;
    socket.setReadBufferSize(CommandMaxResponseBytes + 1);
    socket.connectToServer(libraryCommandEndpoint(path));
    if (!socket.waitForConnected(qMin(timeoutMs, 500))) return {};
    CommandClientResult result;
    result.connected = true;
    QElapsedTimer timer;
    timer.start();
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    QByteArray buffer;
    while (timer.elapsed() < timeoutMs) {
        buffer += socket.readAll();
        if (buffer.size() > CommandMaxResponseBytes) {
            result.response = commandError("output_limit", "Response exceeds 8 MiB", 5);
            return result;
        }
        if (buffer.contains('\n')) {
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(buffer.trimmed(), &error);
            if (error.error == QJsonParseError::NoError && document.isObject()) {
                result.response = document.object();
            } else result.response = commandError("protocol_error", "Malformed server response", 5);
            return result;
        }
        if (socket.state() == QLocalSocket::UnconnectedState) break;
        socket.waitForReadyRead(qMax(1, timeoutMs - int(timer.elapsed())));
    }
    result.response = commandError("outcome_unknown",
        "Session reply was lost or timed out; inspect object/version before retrying a write", 5);
    return result;
}

} // namespace Pinloom
