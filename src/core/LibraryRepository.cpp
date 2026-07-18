#include "pinloom/core/LibraryRepository.h"

namespace Pinloom {

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
