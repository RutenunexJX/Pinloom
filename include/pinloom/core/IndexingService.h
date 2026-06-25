#pragma once

#include "pinloom/core/LibraryRepository.h"
#include "pinloom/core/LibrarySource.h"
#include "pinloom/core/DirectoryLibrarySource.h"

namespace Pinloom {

class IndexingService final {
public:
    explicit IndexingService(ILibraryRepository &repository);

    bool index(const ILibrarySource &source);
    bool indexRoot(const LibraryRoot &root);
    bool indexEnabledRoots();
    bool rebuildEnabledRoots();
    void setRemoteWebFetchingEnabled(bool enabled);
    bool remoteWebFetchingEnabled() const;
    void setWebPageFetcher(DirectoryLibrarySource::WebPageFetcher fetcher);
    QString lastError() const;
    int lastIndexedCount() const;

private:
    void configureDirectorySource(DirectoryLibrarySource &source) const;

    ILibraryRepository &repository_;
    QString lastError_;
    int lastIndexedCount_ = 0;
    bool remoteWebFetchingEnabled_ = false;
    DirectoryLibrarySource::WebPageFetcher webPageFetcher_;
};

} // namespace Pinloom
