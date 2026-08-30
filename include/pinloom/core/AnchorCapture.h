#pragma once

#include "pinloom/core/Anchor.h"

#include <QString>

namespace Pinloom {

struct PdfCaptureRect {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;

    bool isValid() const;
};

struct PdfCaptureRequest {
    QString targetApp = QStringLiteral("SumatraPDF");
    QString targetFile;
    QString documentIdentity;
    QString locatorType;
    int page = -1;
    PdfCaptureRect rect;
    PdfCaptureRect mediaBox;
    PdfCaptureRect cropBox;
    int rotation = 0;
    double userUnit = 1.0;
    // Runtime observation used while capturing. It is not authoritative and
    // must not be serialized into new locators.
    double zoom = -1.0;
    QString unit = QStringLiteral("pt");
    QString source = QStringLiteral("manual");
    QString adapterId = QStringLiteral("sumatrapdf");
    QString searchText;
    QString contextBefore;
    QString contextAfter;
    int occurrence = -1;
    PdfCaptureRect fallbackRect;
    QString anchorName;
};

struct AnchorCaptureResult {
    Anchor anchor;
    QString targetApp;
    QString targetFile;
    QString locatorType;
    int page = -1;
    PdfCaptureRect rect;
    PdfCaptureRect mediaBox;
    PdfCaptureRect cropBox;
    int rotation = 0;
    double userUnit = 1.0;
    double zoom = -1.0;
    QString unit;
    QString source;
    QString error;

    bool success() const;
};

class CaptureProvider {
public:
    virtual ~CaptureProvider() = default;

    virtual QString source() const = 0;
    virtual AnchorCaptureResult capture(const PdfCaptureRequest &request) const = 0;
};

class ManualPdfRectCaptureProvider final : public CaptureProvider {
public:
    QString source() const override;
    AnchorCaptureResult capture(const PdfCaptureRequest &request) const override;
};

QString pdfRectLocatorJson(const PdfCaptureRequest &request);
QString pdfLocatorJson(const PdfCaptureRequest &request);
AnchorCaptureResult captureManualPdfAnchor(const PdfCaptureRequest &request);
AnchorCaptureResult captureManualPdfRectAnchor(const PdfCaptureRequest &request);

} // namespace Pinloom
