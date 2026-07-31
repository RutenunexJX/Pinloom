#pragma once

#include <QString>

namespace Pinloom {

class SqliteClipRepository;

struct ClipRepositoryBackupResult {
    bool success = false;
    QString filePath;
    QString error;
};

ClipRepositoryBackupResult createAutomaticClipRepositoryBackup(
    SqliteClipRepository &repository,
    const QString &directoryPath,
    int retainedBackupCount = 10);

} // namespace Pinloom
