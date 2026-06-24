#include "pinloom/core/IndexingService.h"

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

QString IndexingService::lastError() const
{
    return lastError_;
}

int IndexingService::lastIndexedCount() const
{
    return lastIndexedCount_;
}

} // namespace Pinloom
