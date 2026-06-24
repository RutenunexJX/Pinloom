#pragma once

#include "pinloom/core/LibraryRepository.h"

#include <QHash>

namespace Pinloom {

class InMemoryLibraryRepository final : public ILibraryRepository {
public:
    bool upsertResource(const Resource &resource) override;
    std::optional<Resource> findResource(const QString &id) const override;
    QList<SearchResult> search(const SearchQuery &query) const override;

private:
    QHash<QString, Resource> resources_;
};

} // namespace Pinloom
