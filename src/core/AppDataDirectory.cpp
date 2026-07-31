#include "pinloom/core/AppDataDirectory.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QUuid>

namespace Pinloom {

namespace {

constexpr auto DataDirectoryKey = "storage/dataDirectory";
constexpr auto PendingDataDirectoryKey = "storage/pendingDataDirectory";

QString absoluteDirectoryPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    return QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
}

bool samePath(const QString &left, const QString &right)
{
    return QDir::cleanPath(left).compare(QDir::cleanPath(right),
#ifdef Q_OS_WIN
                                         Qt::CaseInsensitive
#else
                                         Qt::CaseSensitive
#endif
                                         ) == 0;
}

bool isChildPath(const QString &candidate, const QString &parent)
{
    QString normalizedParent = QDir::fromNativeSeparators(QDir::cleanPath(parent));
    QString normalizedCandidate = QDir::fromNativeSeparators(QDir::cleanPath(candidate));
    if (!normalizedParent.endsWith(QLatin1Char('/'))) {
        normalizedParent.append(QLatin1Char('/'));
    }
    return normalizedCandidate.startsWith(normalizedParent,
#ifdef Q_OS_WIN
                                           Qt::CaseInsensitive
#else
                                           Qt::CaseSensitive
#endif
                                           );
}

bool copyDirectoryTree(const QString &sourcePath, const QString &targetPath, QString *error)
{
    if (!QDir().mkpath(targetPath)) {
        if (error) {
            *error = QStringLiteral("Unable to create migration staging directory: %1")
                         .arg(QDir::toNativeSeparators(targetPath));
        }
        return false;
    }

    QDirIterator iterator(sourcePath,
                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    const QDir source(sourcePath);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo entry = iterator.fileInfo();
        const QString relative = source.relativeFilePath(entry.absoluteFilePath());
        const QString destination = QDir(targetPath).filePath(relative);
        if (entry.isDir()) {
            if (!QDir().mkpath(destination)) {
                if (error) {
                    *error = QStringLiteral("Unable to copy data directory: %1")
                                 .arg(QDir::toNativeSeparators(destination));
                }
                return false;
            }
            continue;
        }
        if (!entry.isFile()) {
            continue;
        }
        if (!QDir().mkpath(QFileInfo(destination).absolutePath())
            || !QFile::copy(entry.absoluteFilePath(), destination)) {
            if (error) {
                *error = QStringLiteral("Unable to copy data file: %1")
                             .arg(QDir::toNativeSeparators(entry.absoluteFilePath()));
            }
            return false;
        }
    }
    return true;
}

bool directoryIsEmpty(const QString &path)
{
    const QDir directory(path);
    return !directory.exists()
        || directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)
               .isEmpty();
}

} // namespace

bool AppDataDirectoryResult::succeeded() const
{
    return error.isEmpty() && !directory.trimmed().isEmpty();
}

QString configuredAppDataDirectory(QSettings &settings, const QString &defaultDirectory)
{
    const QString fallback = absoluteDirectoryPath(defaultDirectory);
    const QString configured =
        absoluteDirectoryPath(settings.value(QString::fromLatin1(DataDirectoryKey), fallback).toString());
    return configured.isEmpty() ? fallback : configured;
}

AppDataDirectoryResult prepareAppDataDirectory(QSettings &settings,
                                                const QString &defaultDirectory)
{
    AppDataDirectoryResult result;
    result.directory = configuredAppDataDirectory(settings, defaultDirectory);
    if (result.directory.isEmpty()) {
        result.error = QStringLiteral("Pinloom data directory is empty");
        return result;
    }

    const QString pending = absoluteDirectoryPath(
        settings.value(QString::fromLatin1(PendingDataDirectoryKey)).toString());
    if (pending.isEmpty() || samePath(pending, result.directory)) {
        if (!QDir().mkpath(result.directory)) {
            result.error = QStringLiteral("Unable to create Pinloom data directory: %1")
                               .arg(QDir::toNativeSeparators(result.directory));
            return result;
        }
        settings.setValue(QString::fromLatin1(DataDirectoryKey), result.directory);
        settings.remove(QString::fromLatin1(PendingDataDirectoryKey));
        settings.sync();
        return result;
    }

    if (isChildPath(pending, result.directory) || isChildPath(result.directory, pending)) {
        result.error = QStringLiteral("The current and requested data directories cannot contain one another");
        return result;
    }
    if (!directoryIsEmpty(pending)) {
        result.error = QStringLiteral("Requested data directory is not empty; existing files were not overwritten: %1")
                           .arg(QDir::toNativeSeparators(pending));
        return result;
    }

    const QFileInfo sourceInfo(result.directory);
    if (!sourceInfo.exists()) {
        if (!QDir().mkpath(pending)) {
            result.error = QStringLiteral("Unable to create requested data directory: %1")
                               .arg(QDir::toNativeSeparators(pending));
            return result;
        }
    } else {
        const QFileInfo targetInfo(pending);
        const QString targetParent = targetInfo.absolutePath();
        if (!QDir().mkpath(targetParent)) {
            result.error = QStringLiteral("Unable to create requested data directory parent: %1")
                               .arg(QDir::toNativeSeparators(targetParent));
            return result;
        }
        if (targetInfo.exists() && !QDir(targetParent).rmdir(targetInfo.fileName())) {
            result.error = QStringLiteral("Unable to prepare empty requested data directory: %1")
                               .arg(QDir::toNativeSeparators(pending));
            return result;
        }

        const QString stagingName = QStringLiteral(".%1.pinloom-migrating-%2")
                                        .arg(targetInfo.fileName(),
                                             QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString stagingPath = QDir(targetParent).filePath(stagingName);
        QString copyError;
        if (!copyDirectoryTree(result.directory, stagingPath, &copyError)) {
            QDir(stagingPath).removeRecursively();
            result.error = copyError;
            return result;
        }
        if (!QDir(targetParent).rename(stagingName, targetInfo.fileName())) {
            QDir(stagingPath).removeRecursively();
            result.error = QStringLiteral("Unable to activate migrated data directory: %1")
                               .arg(QDir::toNativeSeparators(pending));
            return result;
        }
    }

    result.directory = pending;
    result.migrated = true;
    settings.setValue(QString::fromLatin1(DataDirectoryKey), result.directory);
    settings.remove(QString::fromLatin1(PendingDataDirectoryKey));
    settings.sync();
    return result;
}

bool stageAppDataDirectoryChange(QSettings &settings,
                                 const QString &activeDirectory,
                                 const QString &requestedDirectory,
                                 QString *error)
{
    const QString active = absoluteDirectoryPath(activeDirectory);
    const QString requested = absoluteDirectoryPath(requestedDirectory);
    if (active.isEmpty() || requested.isEmpty()) {
        if (error) {
            *error = QStringLiteral("A valid data directory is required");
        }
        return false;
    }
    if (isChildPath(requested, active) || isChildPath(active, requested)) {
        if (error) {
            *error = QStringLiteral("The current and requested data directories cannot contain one another");
        }
        return false;
    }

    settings.setValue(QString::fromLatin1(DataDirectoryKey), active);
    if (samePath(active, requested)) {
        settings.remove(QString::fromLatin1(PendingDataDirectoryKey));
    } else {
        settings.setValue(QString::fromLatin1(PendingDataDirectoryKey), requested);
    }
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error) {
            *error = QStringLiteral("Unable to save the data directory setting");
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

} // namespace Pinloom
