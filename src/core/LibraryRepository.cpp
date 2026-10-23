#include "pinloom/core/LibraryRepository.h"
#include <QSet>

namespace Pinloom {

namespace {
QList<Resource> completeResources(const ILibraryRepository &repository)
{
    SearchQuery query;
    query.includeDeleted = true;
    query.limit = 0;
    QSet<QString> seen;
    QList<Resource> resources;
    for (const auto &hit : repository.search(query)) {
        if (seen.contains(hit.resource.id)) continue;
        seen.insert(hit.resource.id);
        if (const auto resource = repository.findResource(hit.resource.id)) resources.append(*resource);
    }
    return resources;
}
}

QList<LibraryResourceLocation> ILibraryRepository::resourceLocations() const
{
    QList<LibraryResourceLocation> result;
    for (const auto &resource : completeResources(*this))
        result.append({resource.id, resource.location, resource.deleted});
    return result;
}

QList<LibraryAnchorLocator> ILibraryRepository::anchorLocators() const
{
    QList<LibraryAnchorLocator> result;
    for (const auto &resource : completeResources(*this))
        for (const auto &anchor : resource.anchors)
            result.append({resource.id, anchor.id, anchor.locatorJson});
    return result;
}

QString ILibraryRepository::lastError() const
{
    return {};
}

std::optional<GlobalIdentityConflict> ILibraryRepository::lastIdentityConflict() const
{
    return std::nullopt;
}

QList<GlobalIdentityConflict> ILibraryRepository::identityConflicts() const
{
    return {};
}

quint64 ILibraryRepository::changeRevision() const
{
    return 0;
}

quint64 ILibraryRepository::contentRevision() const
{
    return changeRevision();
}

int ILibraryRepository::addChangeListener(LibraryChangeListener)
{
    return 0;
}

void ILibraryRepository::removeChangeListener(int)
{
}

} // namespace Pinloom
