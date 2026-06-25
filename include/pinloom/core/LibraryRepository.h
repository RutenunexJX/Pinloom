#pragma once

#include "pinloom/core/LibraryRoot.h"
#include "pinloom/core/Resource.h"
#include "pinloom/core/ResourceRelation.h"
#include "pinloom/core/ResourceUsage.h"
#include "pinloom/core/Search.h"

#include <QList>
#include <optional>

namespace Pinloom {

class ILibraryRepository {
public:
    virtual ~ILibraryRepository() = default;

    virtual bool upsertResource(const Resource &resource) = 0;
    virtual std::optional<Resource> findResource(const QString &id) const = 0;
    virtual QList<SearchResult> search(const SearchQuery &query) const = 0;
    virtual bool clearResources() = 0;

    virtual bool upsertResourceRelation(const ResourceRelation &relation) = 0;
    virtual QList<ResourceRelation> resourceRelations(const QString &resourceId) const = 0;
    virtual QList<ResourceRelation> allResourceRelations() const = 0;
    virtual bool removeResourceRelation(const QString &sourceResourceId, const QString &targetResourceId, const QString &label) = 0;

    virtual bool recordResourceOpen(const QString &resourceId) = 0;
    virtual bool setResourcePinned(const QString &resourceId, bool pinned) = 0;
    virtual std::optional<ResourceUsage> resourceUsage(const QString &resourceId) const = 0;
    virtual bool recordAnchorOpen(const QString &resourceId, const Anchor &anchor) = 0;
    virtual std::optional<AnchorUsage> anchorUsage(const QString &resourceId, const Anchor &anchor) const = 0;

    virtual bool upsertLibraryRoot(const LibraryRoot &root) = 0;
    virtual QList<LibraryRoot> libraryRoots() const = 0;
    virtual std::optional<LibraryRoot> findLibraryRoot(const QString &id) const = 0;
    virtual bool removeLibraryRoot(const QString &id) = 0;
    virtual bool setLibraryRootEnabled(const QString &id, bool enabled) = 0;
    virtual bool setLibraryRootPinned(const QString &id, bool pinned) = 0;
    virtual bool updateLibraryRootLastIndexedAt(const QString &id, const QDateTime &indexedAt) = 0;
};

} // namespace Pinloom
