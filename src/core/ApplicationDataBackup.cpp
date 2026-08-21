#include "pinloom/core/ApplicationDataBackup.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <QUuid>
#include <algorithm>

namespace Pinloom {
namespace {

bool isSimpleFileName(const QString &fileName)
{
    const QString normalized = fileName.trimmed();
    return !normalized.isEmpty()
        && normalized == QFileInfo(normalized).fileName()
        && normalized != QLatin1String(".")
        && normalized != QLatin1String("..");
}

bool writeManifest(const QString &directoryPath,
                   const QDateTime &createdAt,
                   const QList<ApplicationDataBackupItem> &items,
                   QString *error)
{
    QSaveFile manifest(QDir(directoryPath).filePath(QStringLiteral("manifest.txt")));
    if (!manifest.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = manifest.errorString();
        return false;
    }
    QTextStream stream(&manifest);
    stream << "format=pinloom-application-data-backup-v1\n";
    stream << "created_utc=" << createdAt.toUTC().toString(Qt::ISODateWithMs) << '\n';
    for (const ApplicationDataBackupItem &item : items) {
        stream << "file=" << item.fileName.trimmed() << '\n';
    }
    if (!manifest.commit()) {
        if (error) *error = manifest.errorString();
        return false;
    }
    return true;
}

} // namespace

ApplicationDataBackupResult createAutomaticApplicationDataBackup(
    const QString &rootDirectory,
    const QList<ApplicationDataBackupItem> &items,
    int retainedBackupCount,
    const QDateTime &createdAt)
{
    ApplicationDataBackupResult result;
    const QString rootPath = QFileInfo(rootDirectory).absoluteFilePath();
    if (rootDirectory.trimmed().isEmpty() || items.isEmpty() || retainedBackupCount < 1) {
        result.error = QStringLiteral("A backup directory, at least one database, and positive retention are required");
        return result;
    }
    QSet<QString> fileNames;
    for (const ApplicationDataBackupItem &item : items) {
        const QString fileName = item.fileName.trimmed().toCaseFolded();
        if (!isSimpleFileName(item.fileName)
            || fileName == QLatin1String("manifest.txt")
            || !item.createBackup
            || fileNames.contains(fileName)) {
            result.error = QStringLiteral("Each backup item requires a simple file name and backup callback");
            return result;
        }
        fileNames.insert(fileName);
    }

    QDir root(rootPath);
    if (!root.mkpath(QStringLiteral("."))) {
        result.error = QStringLiteral("Unable to create application data backup directory");
        return result;
    }

    const QDateTime timestamp = createdAt.isValid() ? createdAt.toUTC() : QDateTime::currentDateTimeUtc();
    const QString baseName = QStringLiteral("snapshot-%1")
                                 .arg(timestamp.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    QString finalName = baseName;
    int suffix = 2;
    while (root.exists(finalName)) {
        finalName = QStringLiteral("%1-%2").arg(baseName).arg(suffix++);
    }
    const QString stagingName = QStringLiteral(".%1-partial-%2")
                                    .arg(finalName, QUuid::createUuid().toString(QUuid::Id128));
    if (!root.mkpath(stagingName)) {
        result.error = QStringLiteral("Unable to create application data backup staging directory");
        return result;
    }
    const QString stagingPath = root.filePath(stagingName);

    for (const ApplicationDataBackupItem &item : items) {
        const QString destination = QDir(stagingPath).filePath(item.fileName.trimmed());
        if (!item.createBackup(destination) || !QFileInfo::exists(destination)) {
            const QString detail = item.error ? item.error().trimmed() : QString();
            QDir(stagingPath).removeRecursively();
            result.error = detail.isEmpty()
                ? QStringLiteral("Unable to back up %1").arg(item.fileName)
                : detail;
            return result;
        }
    }

    if (!writeManifest(stagingPath, timestamp, items, &result.error)) {
        QDir(stagingPath).removeRecursively();
        return result;
    }
    if (!root.rename(stagingName, finalName)) {
        QDir(stagingPath).removeRecursively();
        result.error = QStringLiteral("Unable to finalize application data backup");
        return result;
    }

    const QFileInfoList snapshots = root.entryInfoList(
        {QStringLiteral("snapshot-*")},
        QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Name | QDir::Reversed);
    for (int index = retainedBackupCount; index < snapshots.size(); ++index) {
        if (!QDir(snapshots.at(index).absoluteFilePath()).removeRecursively()) {
            result.directoryPath = root.filePath(finalName);
            result.error = QStringLiteral("Backup created, but an expired snapshot could not be removed: %1")
                               .arg(snapshots.at(index).absoluteFilePath());
            return result;
        }
    }

    result.success = true;
    result.directoryPath = root.filePath(finalName);
    result.error.clear();
    return result;
}

} // namespace Pinloom
