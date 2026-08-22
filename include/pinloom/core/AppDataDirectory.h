#pragma once

#include <QString>

class QSettings;

namespace Pinloom {

struct AppDataDirectoryResult {
    QString directory;
    bool migrated = false;
    bool adoptedExisting = false;
    QString error;

    bool succeeded() const;
};

QString configuredAppDataDirectory(QSettings &settings, const QString &defaultDirectory);
AppDataDirectoryResult prepareAppDataDirectory(QSettings &settings,
                                                const QString &defaultDirectory);
bool stageAppDataDirectoryChange(QSettings &settings,
                                 const QString &activeDirectory,
                                 const QString &requestedDirectory,
                                 QString *error = nullptr);

} // namespace Pinloom
