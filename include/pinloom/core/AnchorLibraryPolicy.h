#pragma once

#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/LibraryRoot.h"
#include "pinloom/core/Resource.h"
#include "pinloom/core/ResourceUsage.h"

#include <algorithm>

namespace Pinloom {

inline bool isValidAnchorLibraryAnchor(const Anchor &anchor)
{
    return hasAnchorIdentity(anchor);
}

inline bool hasValidAnchorLibraryAnchor(const Resource &resource, bool includeDeleted)
{
    return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(),
                       [includeDeleted](const Anchor &anchor) {
                           return isValidAnchorLibraryAnchor(anchor)
                               && (includeDeleted || !anchor.deleted);
                       });
}

inline bool hasAnchorLibraryUserMarker(const Resource &resource,
                                       const ResourceUsage &usage)
{
    return !resource.aliases.isEmpty()
        || !resource.tags.isEmpty()
        || usage.pinned
        || resource.explicitlyRetained;
}

inline bool shouldProvideAnchorLibraryResource(const Resource &resource,
                                               const ResourceUsage &usage)
{
    return resource.deleted
        || hasValidAnchorLibraryAnchor(resource, true)
        || hasAnchorLibraryUserMarker(resource, usage);
}

inline bool resourceBacksLibraryRoot(const Resource &resource,
                                     const QList<LibraryRoot> &roots)
{
    const QString resourcePath = normalizedLibraryRootPath(resource.location);
    if (resourcePath.isEmpty()) {
        return false;
    }
    return std::any_of(roots.cbegin(), roots.cend(),
                       [&resourcePath](const LibraryRoot &root) {
                           return normalizedLibraryRootPath(root.path)
                                      .compare(resourcePath, Qt::CaseInsensitive) == 0;
                       });
}

inline bool shouldCleanupAnchorLibraryResource(const Resource &resource,
                                               const ResourceUsage &usage,
                                               const QList<LibraryRoot> &roots)
{
    return resource.anchors.isEmpty()
        && resource.aliases.isEmpty()
        && resource.tags.isEmpty()
        && !usage.pinned
        && !resource.explicitlyRetained
        && !resourceBacksLibraryRoot(resource, roots);
}

} // namespace Pinloom
