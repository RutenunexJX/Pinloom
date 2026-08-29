#pragma once

#include "pinloom/core/LibraryRepository.h"

#include <QHash>

namespace Pinloom {

class InMemoryLibraryRepository final : public ILibraryRepository {
public:
    explicit InMemoryLibraryRepository(
        SharedInMemoryGlobalIdentityRegistry identityRegistry = {});

    bool upsertResource(const Resource &resource) override;
    std::optional<Resource> findResource(const QString &id) const override;
    QList<SearchResult> search(const SearchQuery &query) const override;
    bool softDeleteResource(const QString &resourceId) override;
    bool restoreResource(const QString &resourceId) override;
    bool softDeleteAnchor(const QString &resourceId, const Anchor &anchor) override;
    bool restoreAnchor(const QString &resourceId, const Anchor &anchor) override;
    bool clearResources() override;
    bool applyBatch(const LibraryBatchMutation &mutation) override;

    bool upsertLibraryRoot(const LibraryRoot &root) override;
    QList<LibraryRoot> libraryRoots() const override;
    std::optional<LibraryRoot> findLibraryRoot(const QString &id) const override;
    bool removeLibraryRoot(const QString &id) override;

    bool recordResourceOpen(const QString &resourceId) override;
    bool setResourcePinned(const QString &resourceId, bool pinned) override;
    std::optional<ResourceUsage> resourceUsage(const QString &resourceId) const override;
    bool recordAnchorOpen(const QString &resourceId, const Anchor &anchor) override;
    std::optional<AnchorUsage> anchorUsage(const QString &resourceId, const Anchor &anchor) const override;

    QString lastError() const override;
    std::optional<GlobalIdentityConflict> lastIdentityConflict() const override;
    QList<GlobalIdentityConflict> identityConflicts() const override;

    quint64 changeRevision() const override;
    quint64 contentRevision() const override;
    int addChangeListener(LibraryChangeListener listener) override;
    void removeChangeListener(int listenerId) override;

private:
    void notifyChange(LibraryChangeKind kind, const QStringList &resourceIds = {});

    SharedInMemoryGlobalIdentityRegistry identityRegistry_;
    QString lastError_;
    std::optional<GlobalIdentityConflict> lastIdentityConflict_;

    QHash<QString, Resource> resources_;
    QHash<QString, LibraryRoot> libraryRoots_;
    QHash<QString, ResourceUsage> usage_;
    QHash<QString, AnchorUsage> anchorUsage_;
    QHash<int, LibraryChangeListener> listeners_;
    quint64 revision_ = 0;
    quint64 contentRevision_ = 0;
    int nextListenerId_ = 1;
};

} // namespace Pinloom
