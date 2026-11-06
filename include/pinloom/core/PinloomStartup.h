#pragma once

#include <QString>
#include <QStringList>

namespace Pinloom {

enum class PinloomStartupAction { Manage, OpenFiles, RegisterFiles, UnregisterFiles, PackageCheck };

struct PinloomStartup {
    PinloomStartupAction action = PinloomStartupAction::Manage;
    QStringList files;
    QString library;
    QString deepLink;
    QString error;
    bool hidden = false;
    bool noGlobalHooks = false;
};

QStringList pinloomProcessArguments(int argc, char **argv);
PinloomStartup parsePinloomStartup(const QStringList &arguments, const QString &workingDirectory);
QStringList pinloomReaderFileExtensions();

} // namespace Pinloom
