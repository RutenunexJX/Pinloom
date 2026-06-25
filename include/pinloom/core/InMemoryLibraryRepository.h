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

    bool upsertResourceRelation(const ResourceRelation &relation) override;
    QList<ResourceRelation> resourceRelations(const QString &resourceId) const override;
    QList<ResourceRelation> allResourceRelations() const override;
    bool removeResourceRelation(const QString &sourceResourceId, const QString &targetResourceId, const QString &label) override;

    bool recordResourceOpen(const QString &resourceId) override;
    bool setResourcePinned(const QString &resourceId, bool pinned) override;
    std::optional<ResourceUsage> resourceUsage(const QString &resourceId) const override;
    bool recordAnchorOpen(const QString &resourceId, const Anchor &anchor) override;
    std::optional<AnchorUsage> anchorUsage(const QString &resourceId, const Anchor &anchor) const override;

    bool upsertLibraryRoot(const LibraryRoot &root) override;
    QList<LibraryRoot> libraryRoots() const override;
    std::optional<LibraryRoot> findLibraryRoot(const QString &id) const override;
    bool removeLibraryRoot(const QString &id) override;
    bool setLibraryRootEnabled(const QString &id, bool enabled) override;
    bool setLibraryRootPinned(const QString &id, bool pinned) override;
    bool updateLibraryRootLastIndexedAt(const QString &id, const QDateTime &indexedAt) override;

private:
    QHash<QString, Resource> resources_;
    QList<ResourceRelation> relations_;
    QHash<QString, ResourceUsage> usage_;
    QHash<QString, AnchorUsage> anchorUsage_;
    QHash<QString, LibraryRoot> libraryRoots_;
};

} // namespace Pinloom
