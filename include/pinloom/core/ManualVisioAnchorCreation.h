#pragma once

#include "pinloom/core/AnchorCapture.h"
#include "pinloom/core/LibraryRepository.h"

#include <QString>
#include <QStringList>

namespace Pinloom {

struct ManualVisioAnchorCreationRequest {
    QString name;
    QString file;
    QString page;
    QString shapeUniqueId;
    QString locatorType = QStringLiteral("visio.shape");
    QString source = QStringLiteral("manual");
    QString targetApp = QStringLiteral("Microsoft Visio");
    QStringList aliases;
    QStringList tags;
    bool pinned = false;
};

struct ManualVisioAnchorCreationResult {
    Resource resource;
    Anchor anchor;
    QString error;

    bool success() const;
};

class ManualVisioAnchorCreationService {
public:
    explicit ManualVisioAnchorCreationService(ILibraryRepository &repository);

    ManualVisioAnchorCreationResult createManualVisioAnchor(
        const ManualVisioAnchorCreationRequest &request);

private:
    ILibraryRepository &repository_;
};

} // namespace Pinloom
