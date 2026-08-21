#pragma once

#include "pinloom/clip/ClipSearch.h"
#include "pinloom/core/LibraryRepository.h"
#include "pinloom/widgets/PinloomEntry.h"

#include <functional>

namespace Pinloom {

struct PinloomEntrySearchOptions {
    QStringList requiredTags;
    QStringList requiredLocationPrefixes;
    QList<ResourceKind> requiredKinds;
    QStringList contextTags;
    QStringList contextLocationPrefixes;
    int limit = 100;
    bool includeDeleted = false;
    bool deletedOnly = false;
};

class PinloomEntrySearchService {
public:
    using ClipSearchHandler =
        std::function<QList<ClipSearchResult>(const QString &, const ClipSearchOptions &)>;

    explicit PinloomEntrySearchService(ILibraryRepository &repository,
                                       ClipSearchHandler clipSearchHandler = {});

    QList<PinloomEntry> search(const QString &text,
                               const PinloomEntrySearchOptions &options = {}) const;

private:
    ILibraryRepository &repository_;
    ClipSearchHandler clipSearchHandler_;
};

} // namespace Pinloom
