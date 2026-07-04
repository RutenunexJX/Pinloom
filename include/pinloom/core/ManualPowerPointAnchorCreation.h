#pragma once

#include "pinloom/core/AnchorCapture.h"
#include "pinloom/core/LibraryRepository.h"

#include <QString>
#include <QStringList>

namespace Pinloom {

struct ManualPowerPointAnchorCreationRequest {
    QString name;
    QString file;
    int slide = -1;
    int shapeId = -1;
    QString shapeName;
    QString locatorType = QStringLiteral("powerpoint.shape");
    QString source = QStringLiteral("manual");
    QString targetApp = QStringLiteral("Microsoft PowerPoint");
    QStringList aliases;
    QStringList tags;
    bool pinned = false;
};

struct ManualPowerPointAnchorCreationResult {
    Resource resource;
    Anchor anchor;
    QString error;

    bool success() const;
};

class ManualPowerPointAnchorCreationService {
public:
    explicit ManualPowerPointAnchorCreationService(ILibraryRepository &repository);

    ManualPowerPointAnchorCreationResult createManualPowerPointShapeAnchor(
        const ManualPowerPointAnchorCreationRequest &request);

private:
    ILibraryRepository &repository_;
};

} // namespace Pinloom
