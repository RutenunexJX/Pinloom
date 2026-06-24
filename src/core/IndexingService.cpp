#include "pinloom/core/IndexingService.h"

#include "pinloom/core/DirectoryLibrarySource.h"

#include <QDateTime>

namespace Pinloom {

IndexingService::IndexingService(ILibraryRepository &repository)
    : repository_(repository)
{
}

bool IndexingService::index(const ILibrarySource &source)
{
    QString scanError;
    const QList<Resource> resources = source.scan(&scanError);
    if (!scanError.isEmpty()) {
        lastError_ = scanError;
        lastIndexedCount_ = 0;
        return false;
    }

    int indexedCount = 0;
    for (const Resource &resource : resources) {
        if (!repository_.upsertResource(resource)) {
            lastError_ = QStringLiteral("Unable to index resource: %1").arg(resource.location);
            lastIndexedCount_ = indexedCount;
            return false;
        }
        ++indexedCount;
    }

    lastError_.clear();
    lastIndexedCount_ = indexedCount;
    return true;
}

bool IndexingService::indexRoot(const LibraryRoot &root)
{
    if (!root.enabled) {
        lastError_.clear();
        lastIndexedCount_ = 0;
        return true;
    }

    DirectoryLibrarySource source(root.path);
    if (!index(source)) {
        return false;
    }

    if (!repository_.updateLibraryRootLastIndexedAt(root.id, QDateTime::currentDateTimeUtc())) {
        lastError_ = QStringLiteral("Unable to update library root index timestamp: %1").arg(root.path);
        return false;
    }

    return true;
}

bool IndexingService::indexEnabledRoots()
{
    int totalIndexed = 0;
    for (const LibraryRoot &root : repository_.libraryRoots()) {
        if (!root.enabled) {
            continue;
        }
        if (!indexRoot(root)) {
            lastIndexedCount_ = totalIndexed;
            return false;
        }
        totalIndexed += lastIndexedCount_;
    }

    lastError_.clear();
    lastIndexedCount_ = totalIndexed;
    return true;
}

bool IndexingService::rebuildEnabledRoots()
{
    if (!repository_.clearResources()) {
        lastError_ = QStringLiteral("Unable to clear indexed resources");
        lastIndexedCount_ = 0;
        return false;
    }

    return indexEnabledRoots();
}

QString IndexingService::lastError() const
{
    return lastError_;
}

int IndexingService::lastIndexedCount() const
{
    return lastIndexedCount_;
}

} // namespace Pinloom
