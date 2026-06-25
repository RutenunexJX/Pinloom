#pragma once

#include <QDateTime>
#include <QString>

namespace Pinloom {

struct LibraryRoot {
    QString id;
    QString path;
    QString displayName;
    bool enabled = true;
    bool pinned = false;
    QDateTime lastIndexedAt;
};

QString normalizedLibraryRootPath(const QString &path);
QString libraryRootIdForPath(const QString &path);
LibraryRoot makeLibraryRootForPath(const QString &path);

} // namespace Pinloom
