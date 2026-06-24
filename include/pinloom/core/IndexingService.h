#pragma once

#include "pinloom/core/LibraryRepository.h"
#include "pinloom/core/LibrarySource.h"

namespace Pinloom {

class IndexingService final {
public:
    explicit IndexingService(ILibraryRepository &repository);

    bool index(const ILibrarySource &source);
    bool indexRoot(const LibraryRoot &root);
    bool indexEnabledRoots();
    bool rebuildEnabledRoots();
    QString lastError() const;
    int lastIndexedCount() const;

private:
    ILibraryRepository &repository_;
    QString lastError_;
    int lastIndexedCount_ = 0;
};

} // namespace Pinloom
