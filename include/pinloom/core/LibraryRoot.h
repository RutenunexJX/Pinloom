#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace Pinloom {

class ILibraryRepository;

struct LibraryRoot {
    QString id;
    QString path;
    QString displayName;
    bool enabled = true;
    bool syncRoot = false;
    QStringList ignoredDirectoryNames;
    QDateTime updatedAt;
};

QString normalizedLibraryRootPath(const QString &path);
QString libraryRootIdForPath(const QString &path);
LibraryRoot makeLibraryRootForPath(const QString &path, bool syncRoot = false);
QString defaultPinloomSyncRootPath(const QString &dataDirectory = {});
bool configureDefaultLibraryRoot(ILibraryRepository &repository,
                                 const QString &path,
                                 QString *error = nullptr);
bool libraryRootContainsPath(const LibraryRoot &root, const QString &path);
bool libraryRootIgnoresPath(const LibraryRoot &root, const QString &path);

} // namespace Pinloom
