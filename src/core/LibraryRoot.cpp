#include "pinloom/core/LibraryRoot.h"

#include <QDir>
#include <QFileInfo>

namespace Pinloom {

QString normalizedLibraryRootPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString libraryRootIdForPath(const QString &path)
{
    return QStringLiteral("dir:%1").arg(normalizedLibraryRootPath(path).toCaseFolded());
}

LibraryRoot makeLibraryRootForPath(const QString &path)
{
    const QString normalizedPath = normalizedLibraryRootPath(path);
    const QFileInfo info(normalizedPath);

    LibraryRoot root;
    root.id = libraryRootIdForPath(normalizedPath);
    root.path = normalizedPath;
    root.displayName = info.fileName().isEmpty() ? normalizedPath : info.fileName();
    root.enabled = true;
    return root;
}

} // namespace Pinloom
