#pragma once

#include "pinloom/core/LibraryRepository.h"
#include "pinloom/core/Resource.h"

#include <QList>
#include <QString>

namespace Pinloom {

struct AnchorArchiveExportResult {
    bool ok = false;
    QString error;
    int exportedResources = 0;
    int exportedAnchors = 0;

    bool success() const { return ok; }
};

struct AnchorArchiveImportResult {
    bool ok = false;
    QString error;
    int importedResources = 0;
    int skippedResources = 0;
    int importedAnchors = 0;
    int skippedAnchors = 0;

    bool success() const { return ok; }
};

AnchorArchiveExportResult exportAnchorsToJsonFile(const QList<Resource> &resources, const QString &filePath);
AnchorArchiveImportResult importAnchorsFromJsonFile(ILibraryRepository &repository, const QString &filePath);

} // namespace Pinloom
