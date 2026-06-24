#pragma once

#include "pinloom/core/LibraryRoot.h"
#include "pinloom/core/Resource.h"
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

    virtual bool upsertLibraryRoot(const LibraryRoot &root) = 0;
    virtual QList<LibraryRoot> libraryRoots() const = 0;
    virtual std::optional<LibraryRoot> findLibraryRoot(const QString &id) const = 0;
    virtual bool removeLibraryRoot(const QString &id) = 0;
    virtual bool setLibraryRootEnabled(const QString &id, bool enabled) = 0;
    virtual bool updateLibraryRootLastIndexedAt(const QString &id, const QDateTime &indexedAt) = 0;
};

} // namespace Pinloom
