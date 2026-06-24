#pragma once

#include "pinloom/core/LibraryRepository.h"

#include <QHash>

namespace Pinloom {

class InMemoryLibraryRepository final : public ILibraryRepository {
public:
    bool upsertResource(const Resource &resource) override;
    std::optional<Resource> findResource(const QString &id) const override;
    QList<SearchResult> search(const SearchQuery &query) const override;
    bool clearResources() override;

    bool upsertLibraryRoot(const LibraryRoot &root) override;
    QList<LibraryRoot> libraryRoots() const override;
    std::optional<LibraryRoot> findLibraryRoot(const QString &id) const override;
    bool removeLibraryRoot(const QString &id) override;
    bool setLibraryRootEnabled(const QString &id, bool enabled) override;
    bool updateLibraryRootLastIndexedAt(const QString &id, const QDateTime &indexedAt) override;

private:
    QHash<QString, Resource> resources_;
    QHash<QString, LibraryRoot> libraryRoots_;
};

} // namespace Pinloom
