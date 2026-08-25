#pragma once

#include "pinloom/core/AnchorCapture.h"
#include "pinloom/core/LibraryRepository.h"

#include <QString>
#include <QStringList>

namespace Pinloom {

struct ManualPdfAnchorCreationRequest {
    QString name;
    QString file;
    QString locatorType;
    int page = -1;
    PdfCaptureRect rect;
    double zoom = -1.0;
    QString unit = QStringLiteral("pt");
    QString source = QStringLiteral("manual");
    QString targetApp = QStringLiteral("SumatraPDF");
    QString searchText;
    QString contextBefore;
    QString contextAfter;
    int occurrence = -1;
    PdfCaptureRect fallbackRect;
    QStringList aliases;
    QStringList tags;
    bool pinned = false;
};

struct ManualPdfAnchorCreationResult {
    Resource resource;
    Anchor anchor;
    QString error;

    bool success() const;
};

QString manualPdfAnchorLocatorSummary(const ManualPdfAnchorCreationRequest &request);

class ManualPdfAnchorCreationService {
public:
    explicit ManualPdfAnchorCreationService(ILibraryRepository &repository);

    ManualPdfAnchorCreationResult createManualPdfAnchor(
        const ManualPdfAnchorCreationRequest &request);
    ManualPdfAnchorCreationResult createManualPdfRectAnchor(
        const ManualPdfAnchorCreationRequest &request);

private:
    ILibraryRepository &repository_;
};

} // namespace Pinloom
