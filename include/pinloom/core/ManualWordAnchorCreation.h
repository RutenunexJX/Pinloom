#pragma once

#include "pinloom/core/AnchorCapture.h"
#include "pinloom/core/LibraryRepository.h"

#include <QString>
#include <QStringList>

namespace Pinloom {

struct ManualWordAnchorCreationRequest {
    QString name;
    QString file;
    QString bookmark;
    QString locatorType = QStringLiteral("word.bookmark");
    QString source = QStringLiteral("manual");
    QString targetApp = QStringLiteral("Microsoft Word");
    QStringList aliases;
    QStringList tags;
    bool pinned = false;
};

struct ManualWordAnchorCreationResult {
    Resource resource;
    Anchor anchor;
    QString error;

    bool success() const;
};

class ManualWordAnchorCreationService {
public:
    explicit ManualWordAnchorCreationService(ILibraryRepository &repository);

    ManualWordAnchorCreationResult createManualWordBookmarkAnchor(
        const ManualWordAnchorCreationRequest &request);

private:
    ILibraryRepository &repository_;
};

} // namespace Pinloom
