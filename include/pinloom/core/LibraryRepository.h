#pragma once

#include "pinloom/core/Resource.h"
#include "pinloom/core/ResourceUsage.h"
#include "pinloom/core/Search.h"

#include <QList>
#include <QStringList>
#include <QtGlobal>
#include <functional>
#include <optional>

namespace Pinloom {

struct ResourcePinUpdate {
    QString resourceId;
    bool pinned = false;
};

struct LibraryBatchMutation {
    QList<Resource> upserts;
    QStringList permanentlyDeleteResourceIds;
    QList<ResourcePinUpdate> resourcePinUpdates;
    bool clearExistingResources = false;
};

enum class LibraryChangeKind {
    Content,
    Usage,
    Reset
};

struct LibraryChange {
    LibraryChangeKind kind = LibraryChangeKind::Content;
    QStringList resourceIds;
    quint64 revision = 0;
};

using LibraryChangeListener = std::function<void(const LibraryChange &change)>;

class ILibraryRepository {
public:
    virtual ~ILibraryRepository() = default;

    virtual bool upsertResource(const Resource &resource) = 0;
    virtual std::optional<Resource> findResource(const QString &id) const = 0;
    virtual QList<SearchResult> search(const SearchQuery &query) const = 0;
    virtual bool softDeleteResource(const QString &resourceId) = 0;
    virtual bool restoreResource(const QString &resourceId) = 0;
    virtual bool softDeleteAnchor(const QString &resourceId, const Anchor &anchor) = 0;
    virtual bool restoreAnchor(const QString &resourceId, const Anchor &anchor) = 0;
    virtual bool clearResources() = 0;
    virtual bool applyBatch(const LibraryBatchMutation &mutation) = 0;

    virtual bool recordResourceOpen(const QString &resourceId) = 0;
    virtual bool setResourcePinned(const QString &resourceId, bool pinned) = 0;
    virtual std::optional<ResourceUsage> resourceUsage(const QString &resourceId) const = 0;
    virtual bool recordAnchorOpen(const QString &resourceId, const Anchor &anchor) = 0;
    virtual std::optional<AnchorUsage> anchorUsage(const QString &resourceId, const Anchor &anchor) const = 0;

    virtual quint64 changeRevision() const;
    virtual quint64 contentRevision() const;
    virtual int addChangeListener(LibraryChangeListener listener);
    virtual void removeChangeListener(int listenerId);
};

} // namespace Pinloom
