#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <functional>

namespace Pinloom {

struct ApplicationDataBackupItem {
    QString fileName;
    std::function<bool(const QString &destinationPath)> createBackup;
    std::function<QString()> error;
};

struct ApplicationDataBackupResult {
    bool success = false;
    QString directoryPath;
    QString error;
};

ApplicationDataBackupResult createAutomaticApplicationDataBackup(
    const QString &rootDirectory,
    const QList<ApplicationDataBackupItem> &items,
    int retainedBackupCount = 10,
    const QDateTime &createdAt = {});

} // namespace Pinloom
