#include "pinloom/clip/ClipRepositoryBackup.h"

#include "pinloom/clip/ClipRepository.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfoList>

namespace Pinloom {

ClipRepositoryBackupResult createAutomaticClipRepositoryBackup(
    SqliteClipRepository &repository,
    const QString &directoryPath,
    int retainedBackupCount)
{
    ClipRepositoryBackupResult result;
    const QString directory = QDir::cleanPath(directoryPath.trimmed());
    if (directory.isEmpty() || retainedBackupCount < 1) {
        result.error = QStringLiteral("A backup directory and positive retention count are required");
        return result;
    }
    if (!QDir().mkpath(directory)) {
        result.error = QStringLiteral("Unable to create Clip backup directory: %1")
                           .arg(QDir::toNativeSeparators(directory));
        return result;
    }

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    result.filePath = QDir(directory).filePath(
        QStringLiteral("pinloom-clip-auto-%1.sqlite3").arg(timestamp));
    if (!repository.backupDatabase(result.filePath)) {
        result.error = repository.lastError().trimmed().isEmpty()
            ? QStringLiteral("Unable to create Clip database backup")
            : repository.lastError().trimmed();
        result.filePath.clear();
        return result;
    }

    const QFileInfoList backups = QDir(directory).entryInfoList(
        {QStringLiteral("pinloom-clip-auto-*.sqlite3")},
        QDir::Files,
        QDir::Time);
    for (int index = retainedBackupCount; index < backups.size(); ++index) {
        if (!QFile::remove(backups.at(index).absoluteFilePath())) {
            result.error = QStringLiteral("Backup created, but an expired backup could not be removed: %1")
                               .arg(QDir::toNativeSeparators(backups.at(index).absoluteFilePath()));
            return result;
        }
    }

    result.success = true;
    return result;
}

} // namespace Pinloom
