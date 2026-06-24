#pragma once

#include "pinloom/core/Resource.h"
#include "pinloom/core/Search.h"

#include <QList>

namespace Pinloom {

class ILibraryRepository {
public:
    virtual ~ILibraryRepository() = default;

    virtual bool upsertResource(const Resource &resource) = 0;
    virtual QList<SearchResult> search(const SearchQuery &query) const = 0;
};

} // namespace Pinloom
