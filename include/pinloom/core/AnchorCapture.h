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

struct PdfXChangeCaptureRequest {
    QString targetApp = QStringLiteral("PDF-XChange");
    QString targetFile;
    QString locatorType = QStringLiteral("pdfxchange.rect");
    int page = -1;
    PdfCaptureRect rect;
    double zoom = -1.0;
    QString unit = QStringLiteral("pt");
    QString source = QStringLiteral("manual");
    QString anchorName;
};

struct ExcelCaptureRequest {
    QString targetApp = QStringLiteral("Microsoft Excel");
    QString targetFile;
    QString locatorType;
    QString sheet;
    QString rangeAddress;
    QString namedRange;
    QString source = QStringLiteral("manual");
    QString anchorName;
};

struct VisioCaptureRequest {
    QString targetApp = QStringLiteral("Microsoft Visio");
    QString targetFile;
    QString locatorType = QStringLiteral("visio.shape");
    QString page;
    QString shapeUniqueId;
    QString source = QStringLiteral("manual");
    QString anchorName;
};

struct WordCaptureRequest {
    QString targetApp = QStringLiteral("Microsoft Word");
    QString targetFile;
    QString locatorType = QStringLiteral("word.bookmark");
    QString bookmark;
    QString source = QStringLiteral("manual");
    QString anchorName;
};

struct AnchorCaptureResult {
    Anchor anchor;
    QString targetApp;
    QString targetFile;
    QString locatorType;
    int page = -1;
    PdfCaptureRect rect;
    double zoom = -1.0;
    QString unit;
    QString source;
    QString error;

    bool success() const;
};

struct ExcelCaptureResult {
    Anchor anchor;
    QString targetApp;
    QString targetFile;
    QString locatorType;
    QString sheet;
    QString rangeAddress;
    QString namedRange;
    QString source;
    QString error;

    bool success() const;
};

struct VisioCaptureResult {
    Anchor anchor;
    QString targetApp;
    QString targetFile;
    QString locatorType;
    QString page;
    QString shapeUniqueId;
    QString source;
    QString error;

    bool success() const;
};

struct WordCaptureResult {
    Anchor anchor;
    QString targetApp;
    QString targetFile;
    QString locatorType;
    QString bookmark;
    QString source;
    QString error;

    bool success() const;
};

class CaptureProvider {
public:
    virtual ~CaptureProvider() = default;

    virtual QString source() const = 0;
    virtual AnchorCaptureResult capture(const PdfXChangeCaptureRequest &request) const = 0;
};

class ManualPdfXChangeRectCaptureProvider final : public CaptureProvider {
public:
    QString source() const override;
    AnchorCaptureResult capture(const PdfXChangeCaptureRequest &request) const override;
};

class ExcelCaptureProvider {
public:
    virtual ~ExcelCaptureProvider() = default;

    virtual QString source() const = 0;
    virtual ExcelCaptureResult capture(const ExcelCaptureRequest &request) const = 0;
};

class ManualExcelAnchorCaptureProvider final : public ExcelCaptureProvider {
public:
    QString source() const override;
    ExcelCaptureResult capture(const ExcelCaptureRequest &request) const override;
};

class VisioCaptureProvider {
public:
    virtual ~VisioCaptureProvider() = default;

    virtual QString source() const = 0;
    virtual VisioCaptureResult capture(const VisioCaptureRequest &request) const = 0;
};

class ManualVisioAnchorCaptureProvider final : public VisioCaptureProvider {
public:
    QString source() const override;
    VisioCaptureResult capture(const VisioCaptureRequest &request) const override;
};

class WordCaptureProvider {
public:
    virtual ~WordCaptureProvider() = default;

    virtual QString source() const = 0;
    virtual WordCaptureResult capture(const WordCaptureRequest &request) const = 0;
};

class ManualWordBookmarkAnchorCaptureProvider final : public WordCaptureProvider {
public:
    QString source() const override;
    WordCaptureResult capture(const WordCaptureRequest &request) const override;
};

QString pdfXChangeRectLocatorJson(const PdfXChangeCaptureRequest &request);
AnchorCaptureResult captureManualPdfXChangeRectAnchor(const PdfXChangeCaptureRequest &request);
QString excelLocatorJson(const ExcelCaptureRequest &request);
ExcelCaptureResult captureManualExcelAnchor(const ExcelCaptureRequest &request);
QString visioLocatorJson(const VisioCaptureRequest &request);
VisioCaptureResult captureManualVisioAnchor(const VisioCaptureRequest &request);
QString wordLocatorJson(const WordCaptureRequest &request);
WordCaptureResult captureManualWordBookmarkAnchor(const WordCaptureRequest &request);

} // namespace Pinloom
