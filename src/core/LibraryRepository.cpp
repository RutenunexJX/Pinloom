#include "pinloom/core/LibraryRepository.h"

namespace Pinloom {

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
