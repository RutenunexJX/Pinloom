#pragma once

#include "pinloom/core/LibraryRepository.h"

#include <QHash>

namespace Pinloom {

class InMemoryLibraryRepository final : public ILibraryRepository {
public:
    bool upsertResource(const Resource &resource) override;
    std::optional<Resource> findResource(const QString &id) const override;
    QList<SearchResult> search(const SearchQuery &query) const override;
    bool softDeleteResource(const QString &resourceId) override;
    bool restoreResource(const QString &resourceId) override;
    bool softDeleteAnchor(const QString &resourceId, const Anchor &anchor) override;
    bool restoreAnchor(const QString &resourceId, const Anchor &anchor) override;
    bool clearResources() override;

    bool recordResourceOpen(const QString &resourceId) override;
    bool setResourcePinned(const QString &resourceId, bool pinned) override;
    std::optional<ResourceUsage> resourceUsage(const QString &resourceId) const override;
    bool recordAnchorOpen(const QString &resourceId, const Anchor &anchor) override;
    std::optional<AnchorUsage> anchorUsage(const QString &resourceId, const Anchor &anchor) const override;

private:
    QHash<QString, Resource> resources_;
    QHash<QString, ResourceUsage> usage_;
    QHash<QString, AnchorUsage> anchorUsage_;
};

} // namespace Pinloom
