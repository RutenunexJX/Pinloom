#include "pinloom/core/DirectoryLibrarySource.h"

#include "pinloom/core/LibraryRoot.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <algorithm>

namespace Pinloom {

namespace {

QString normalizedPath(const QFileInfo &fileInfo)
{
    return QDir::cleanPath(fileInfo.absoluteFilePath());
}

ResourceKind kindForFileInfo(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir()) {
        return ResourceKind::Folder;
    }

    const QString suffix = fileInfo.suffix().toLower();
    if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown")) {
        return ResourceKind::Markdown;
    }
    if (suffix == QLatin1String("pdf")) {
        return ResourceKind::Pdf;
    }
    return ResourceKind::File;
}

} // namespace

DirectoryLibrarySource::DirectoryLibrarySource(QString rootPath)
    : rootPath_(normalizedLibraryRootPath(rootPath))
{
}

QString DirectoryLibrarySource::rootPath() const
{
    return rootPath_;
}

QString DirectoryLibrarySource::displayName() const
{
    return QStringLiteral("Directory: %1").arg(rootPath_);
}

QList<Resource> DirectoryLibrarySource::scan(QString *errorMessage) const
{
    if (errorMessage) {
        errorMessage->clear();
    }

    const QFileInfo rootInfo(rootPath_);
    if (!rootInfo.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Directory does not exist: %1").arg(rootPath_);
        }
        return {};
    }
    if (!rootInfo.isDir()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Library root is not a directory: %1").arg(rootPath_);
        }
        return {};
    }

    QList<Resource> resources;
    resources.append(resourceFromFileInfo(rootInfo));

    QDirIterator iterator(rootPath_,
                          QDir::AllEntries | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        resources.append(resourceFromFileInfo(iterator.fileInfo()));
    }

    std::sort(resources.begin(), resources.end(), [](const Resource &left, const Resource &right) {
        return left.location < right.location;
    });

    return resources;
}

Resource DirectoryLibrarySource::resourceFromFileInfo(const QFileInfo &fileInfo) const
{
    Resource resource;
    resource.location = normalizedPath(fileInfo);
    resource.id = QStringLiteral("file:%1").arg(resource.location);
    resource.kind = kindForFileInfo(fileInfo);
    resource.title = fileInfo.fileName();
    if (resource.title.isEmpty()) {
        resource.title = resource.location;
    }
    resource.updatedAt = fileInfo.lastModified().toUTC();
    return resource;
}

} // namespace Pinloom
