#include "pinloom/core/LibraryRoot.h"

#include "pinloom/core/LibraryRepository.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

namespace Pinloom {

namespace {

Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString normalizedPathKey(const QString &path)
{
    QString key = normalizedLibraryRootPath(path);
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

} // namespace

QString normalizedLibraryRootPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    QString normalized = QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return normalized;
}

QString libraryRootIdForPath(const QString &path)
{
    const QString key = normalizedPathKey(path);
    if (key.isEmpty()) {
        return {};
    }
    const QByteArray digest =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("library-root:%1").arg(QString::fromLatin1(digest.left(32)));
}

LibraryRoot makeLibraryRootForPath(const QString &path, bool syncRoot)
{
    const QString normalizedPath = normalizedLibraryRootPath(path);
    const QFileInfo info(normalizedPath);

    LibraryRoot root;
    root.id = libraryRootIdForPath(normalizedPath);
    root.path = normalizedPath;
    root.displayName = info.fileName().trimmed().isEmpty() ? normalizedPath : info.fileName();
    root.enabled = info.isDir();
    root.syncRoot = syncRoot;
    if (syncRoot) {
        root.ignoredDirectoryNames = {QStringLiteral("_PinloomData")};
    }
    root.updatedAt = QDateTime::currentDateTimeUtc();
    return root;
}

QString defaultPinloomSyncRootPath(const QString &dataDirectory)
{
    const QString dataPath = normalizedLibraryRootPath(dataDirectory);
    if (!dataPath.isEmpty()) {
        const QFileInfo dataInfo(dataPath);
        if (dataInfo.fileName().compare(QStringLiteral("_PinloomData"), Qt::CaseInsensitive) == 0) {
            return normalizedLibraryRootPath(dataInfo.absolutePath());
        }
    }
    return {};
}

bool configureDefaultLibraryRoot(ILibraryRepository &repository,
                                 const QString &path,
                                 QString *error)
{
    const QString normalizedPath = normalizedLibraryRootPath(path);
    if (!normalizedPath.isEmpty() && !QFileInfo(normalizedPath).isDir()) {
        if (error) {
            *error = QStringLiteral("Default root directory does not exist: %1")
                         .arg(QDir::toNativeSeparators(normalizedPath));
        }
        return false;
    }

    const QList<LibraryRoot> existingRoots = repository.libraryRoots();
    if (!normalizedPath.isEmpty()) {
        LibraryRoot configured = makeLibraryRootForPath(normalizedPath, true);
        for (const LibraryRoot &existing : existingRoots) {
            if (normalizedPathKey(existing.path) != normalizedPathKey(normalizedPath)) {
                continue;
            }
            if (!existing.displayName.trimmed().isEmpty()) {
                configured.displayName = existing.displayName;
            }
            for (const QString &ignored : existing.ignoredDirectoryNames) {
                if (!configured.ignoredDirectoryNames.contains(ignored, Qt::CaseInsensitive)) {
                    configured.ignoredDirectoryNames.append(ignored);
                }
            }
            break;
        }
        configured.enabled = true;
        configured.syncRoot = true;
        if (!configured.ignoredDirectoryNames.contains(QStringLiteral("_PinloomData"),
                                                       Qt::CaseInsensitive)) {
            configured.ignoredDirectoryNames.append(QStringLiteral("_PinloomData"));
        }
        if (!repository.upsertLibraryRoot(configured)) {
            if (error) *error = QStringLiteral("Unable to register the default root directory");
            return false;
        }
    }

    for (LibraryRoot existing : existingRoots) {
        if (!existing.syncRoot
            || (!normalizedPath.isEmpty()
                && normalizedPathKey(existing.path) == normalizedPathKey(normalizedPath))) {
            continue;
        }
        existing.syncRoot = false;
        existing.updatedAt = QDateTime::currentDateTimeUtc();
        if (!repository.upsertLibraryRoot(existing)) {
            if (error) *error = QStringLiteral("Unable to update the previous default root directory");
            return false;
        }
    }

    if (error) error->clear();
    return true;
}

bool libraryRootContainsPath(const LibraryRoot &root, const QString &path)
{
    const QString rootPath = normalizedLibraryRootPath(root.path);
    const QString candidate = normalizedLibraryRootPath(path);
    if (rootPath.isEmpty() || candidate.isEmpty()) {
        return false;
    }
    if (candidate.compare(rootPath, pathCaseSensitivity()) == 0) {
        return true;
    }
    return candidate.startsWith(rootPath + QLatin1Char('/'), pathCaseSensitivity());
}

bool libraryRootIgnoresPath(const LibraryRoot &root, const QString &path)
{
    if (!libraryRootContainsPath(root, path)) {
        return false;
    }
    QString relative = QDir(root.path).relativeFilePath(normalizedLibraryRootPath(path));
    relative = QDir::fromNativeSeparators(relative);
    const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (root.ignoredDirectoryNames.contains(part, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

} // namespace Pinloom
