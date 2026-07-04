#pragma once

#include "pinloom/core/AnchorCapture.h"
#include "pinloom/core/LibraryRepository.h"

#include <QString>
#include <QStringList>

namespace Pinloom {

struct ManualExcelAnchorCreationRequest {
    QString name;
    QString file;
    QString sheet;
    QString rangeAddress;
    QString namedRange;
    QString locatorType;
    QString source = QStringLiteral("manual");
    QString targetApp = QStringLiteral("Microsoft Excel");
    QStringList aliases;
    QStringList tags;
    bool pinned = false;
};

struct ManualExcelAnchorCreationResult {
    Resource resource;
    Anchor anchor;
    QString error;

    bool success() const;
};

class ManualExcelAnchorCreationService {
public:
    explicit ManualExcelAnchorCreationService(ILibraryRepository &repository);

    ManualExcelAnchorCreationResult createManualExcelAnchor(
        const ManualExcelAnchorCreationRequest &request);

private:
    ILibraryRepository &repository_;
};

} // namespace Pinloom
