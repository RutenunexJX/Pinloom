#pragma once

#include "pinloom/application/CommandService.h"
#include <QLocalServer>
#include <QLockFile>
#include <memory>

namespace Pinloom {

class LibrarySessionLock {
public:
    explicit LibrarySessionLock(const QString &libraryPath);
    bool tryLock();
private:
    QLockFile lock_;
};

class CommandServer final : public QObject {
public:
    explicit CommandServer(CommandService &service, QObject *parent = nullptr);
    bool listen(const QString &libraryPath);
    QString errorString() const;
private:
    CommandService &service_;
    QLocalServer server_;
};

// A connected request is never retried offline, even if its reply is lost.
struct CommandClientResult {
    bool connected = false;
    QJsonObject response;
};
CommandClientResult sendLibraryCommand(const QString &libraryPath, const QJsonObject &request,
                                       int timeoutMs = 5000);

} // namespace Pinloom
