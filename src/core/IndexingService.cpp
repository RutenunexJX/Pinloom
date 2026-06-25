#include "pinloom/core/IndexingService.h"

#include "pinloom/core/DirectoryLibrarySource.h"

#include <QDateTime>
#include <utility>

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
    QList<ResourceRelation> discoveredRelations;
    for (const Resource &resource : resources) {
        if (!repository_.upsertResource(resource)) {
            lastError_ = QStringLiteral("Unable to index resource: %1").arg(resource.location);
            lastIndexedCount_ = indexedCount;
            return false;
        }
        discoveredRelations.append(resource.relations);
        ++indexedCount;
    }

    for (const ResourceRelation &relation : discoveredRelations) {
        if (!repository_.findResource(relation.sourceResourceId).has_value()
            || !repository_.findResource(relation.targetResourceId).has_value()) {
            continue;
        }
        if (!repository_.upsertResourceRelation(relation)) {
            lastError_ = QStringLiteral("Unable to index relation: %1 -> %2")
                             .arg(relation.sourceResourceId, relation.targetResourceId);
            lastIndexedCount_ = indexedCount;
            return false;
        }
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
    configureDirectorySource(source);
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

void IndexingService::setRemoteWebFetchingEnabled(bool enabled)
{
    remoteWebFetchingEnabled_ = enabled;
}

bool IndexingService::remoteWebFetchingEnabled() const
{
    return remoteWebFetchingEnabled_;
}

void IndexingService::setWebPageFetcher(DirectoryLibrarySource::WebPageFetcher fetcher)
{
    webPageFetcher_ = std::move(fetcher);
}

void IndexingService::configureDirectorySource(DirectoryLibrarySource &source) const
{
    source.setRemoteWebFetchingEnabled(remoteWebFetchingEnabled_);
    if (webPageFetcher_) {
        source.setWebPageFetcher(webPageFetcher_);
    }
}

} // namespace Pinloom
