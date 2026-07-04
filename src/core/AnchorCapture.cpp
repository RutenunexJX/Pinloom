#include "pinloom/core/AnchorCapture.h"

#include "pinloom/core/ExcelCommand.h"
#include "pinloom/core/VisioCommand.h"
#include "pinloom/core/WordCommand.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace Pinloom {

namespace {

QString effectiveTargetApp(const PdfXChangeCaptureRequest &request)
{
    const QString targetApp = request.targetApp.trimmed();
    return targetApp.isEmpty() ? QStringLiteral("PDF-XChange") : targetApp;
}

QString effectiveLocatorType(const PdfXChangeCaptureRequest &request)
{
    const QString locatorType = request.locatorType.trimmed().toLower();
    return locatorType.isEmpty() ? QStringLiteral("pdfxchange.rect") : locatorType;
}

QString effectiveUnit(const PdfXChangeCaptureRequest &request)
{
    const QString unit = request.unit.trimmed().toLower();
    return unit.isEmpty() ? QStringLiteral("pt") : unit;
}

QString effectiveSource(const PdfXChangeCaptureRequest &request, const QString &fallback)
{
    const QString source = request.source.trimmed().toLower();
    return source.isEmpty() ? fallback : source;
}

QString effectiveSource(const ExcelCaptureRequest &request, const QString &fallback)
{
    const QString source = request.source.trimmed().toLower();
    return source.isEmpty() ? fallback : source;
}

QString effectiveSource(const VisioCaptureRequest &request, const QString &fallback)
{
    const QString source = request.source.trimmed().toLower();
    return source.isEmpty() ? fallback : source;
}

QString effectiveSource(const WordCaptureRequest &request, const QString &fallback)
{
    const QString source = request.source.trimmed().toLower();
    return source.isEmpty() ? fallback : source;
}

QString effectiveExcelTargetApp(const ExcelCaptureRequest &request)
{
    const QString targetApp = request.targetApp.trimmed();
    return targetApp.isEmpty() ? QStringLiteral("Microsoft Excel") : targetApp;
}

QString effectiveExcelLocatorType(const ExcelCaptureRequest &request)
{
    const QString locatorType = request.locatorType.trimmed().toLower();
    if (!locatorType.isEmpty()) {
        return locatorType;
    }

    if (!request.rangeAddress.trimmed().isEmpty()) {
        return QStringLiteral("excel.range");
    }
    return QStringLiteral("excel.name");
}

QString effectiveVisioTargetApp(const VisioCaptureRequest &request)
{
    const QString targetApp = request.targetApp.trimmed();
    return targetApp.isEmpty() ? QStringLiteral("Microsoft Visio") : targetApp;
}

QString effectiveVisioLocatorType(const VisioCaptureRequest &request)
{
    const QString locatorType = request.locatorType.trimmed().toLower();
    return locatorType.isEmpty() ? QStringLiteral("visio.shape") : locatorType;
}

QString effectiveWordTargetApp(const WordCaptureRequest &request)
{
    const QString targetApp = request.targetApp.trimmed();
    return targetApp.isEmpty() ? QStringLiteral("Microsoft Word") : targetApp;
}

QString effectiveWordLocatorType(const WordCaptureRequest &request)
{
    const QString locatorType = request.locatorType.trimmed().toLower();
    return locatorType.isEmpty() ? QStringLiteral("word.bookmark") : locatorType;
}

QString defaultAnchorName(const PdfXChangeCaptureRequest &request)
{
    const QString fileName = QFileInfo(request.targetFile.trimmed()).fileName();
    const QString target = fileName.isEmpty() ? request.targetFile.trimmed() : fileName;
    if (target.isEmpty()) {
        return QStringLiteral("PDF page %1 rect").arg(request.page);
    }
    return QStringLiteral("%1 page %2 rect").arg(target, QString::number(request.page));
}

AnchorCaptureResult resultForRequest(const PdfXChangeCaptureRequest &request,
                                     const QString &source)
{
    AnchorCaptureResult result;
    result.targetApp = effectiveTargetApp(request);
    result.targetFile = request.targetFile.trimmed();
    result.locatorType = effectiveLocatorType(request);
    result.page = request.page;
    result.rect = request.rect;
    result.zoom = request.zoom;
    result.unit = effectiveUnit(request);
    result.source = effectiveSource(request, source);
    return result;
}

ExcelCaptureResult resultForRequest(const ExcelCaptureRequest &request,
                                    const QString &source)
{
    ExcelCaptureResult result;
    result.targetApp = effectiveExcelTargetApp(request);
    result.targetFile = request.targetFile.trimmed();
    result.locatorType = effectiveExcelLocatorType(request);
    result.sheet = request.sheet.trimmed();
    result.rangeAddress = request.rangeAddress.trimmed();
    result.namedRange = request.namedRange.trimmed();
    result.source = effectiveSource(request, source);
    return result;
}

VisioCaptureResult resultForRequest(const VisioCaptureRequest &request,
                                    const QString &source)
{
    VisioCaptureResult result;
    result.targetApp = effectiveVisioTargetApp(request);
    result.targetFile = request.targetFile.trimmed();
    result.locatorType = effectiveVisioLocatorType(request);
    result.page = request.page.trimmed();
    result.shapeUniqueId = request.shapeUniqueId.trimmed();
    result.source = effectiveSource(request, source);
    return result;
}

WordCaptureResult resultForRequest(const WordCaptureRequest &request,
                                   const QString &source)
{
    WordCaptureResult result;
    result.targetApp = effectiveWordTargetApp(request);
    result.targetFile = request.targetFile.trimmed();
    result.locatorType = effectiveWordLocatorType(request);
    result.bookmark = request.bookmark.trimmed();
    result.source = effectiveSource(request, source);
    return result;
}

QJsonArray rectArray(const PdfCaptureRect &rect)
{
    QJsonArray values;
    values.append(rect.left);
    values.append(rect.top);
    values.append(rect.right);
    values.append(rect.bottom);
    return values;
}

} // namespace

bool PdfCaptureRect::isValid() const
{
    return std::isfinite(left)
        && std::isfinite(top)
        && std::isfinite(right)
        && std::isfinite(bottom)
        && right > left
        && bottom > top;
}

bool AnchorCaptureResult::success() const
{
    return error.isEmpty();
}

bool ExcelCaptureResult::success() const
{
    return error.isEmpty();
}

bool VisioCaptureResult::success() const
{
    return error.isEmpty();
}

bool WordCaptureResult::success() const
{
    return error.isEmpty();
}

QString ManualPdfXChangeRectCaptureProvider::source() const
{
    return QStringLiteral("manual");
}

AnchorCaptureResult ManualPdfXChangeRectCaptureProvider::capture(const PdfXChangeCaptureRequest &request) const
{
    AnchorCaptureResult result = resultForRequest(request, source());

    if (result.targetFile.isEmpty()) {
        result.error = QStringLiteral("PDF-XChange capture target file is missing");
        return result;
    }
    if (result.locatorType != QLatin1String("pdfxchange.rect")) {
        result.error = QStringLiteral("PDF-XChange capture locator type must be pdfxchange.rect");
        return result;
    }
    if (result.page <= 0) {
        result.error = QStringLiteral("PDF-XChange capture page is missing");
        return result;
    }
    if (!result.rect.isValid()) {
        result.error = QStringLiteral("PDF-XChange capture rectangle is missing");
        return result;
    }
    if (result.zoom > 0.0 && !std::isfinite(result.zoom)) {
        result.error = QStringLiteral("PDF-XChange capture zoom is invalid");
        return result;
    }

    Anchor anchor;
    anchor.type = AnchorType::PdfRegion;
    anchor.name = request.anchorName.trimmed();
    anchor.target = anchor.name.isEmpty() ? defaultAnchorName(request) : anchor.name;
    anchor.targetApp = result.targetApp;
    anchor.targetFile = result.targetFile;
    anchor.locatorType = result.locatorType;
    anchor.locatorJson = pdfXChangeRectLocatorJson(request);
    anchor.page = result.page;
    anchor.region = QRectF(result.rect.left,
                           result.rect.top,
                           result.rect.right - result.rect.left,
                           result.rect.bottom - result.rect.top);
    result.anchor = anchor;
    return result;
}

QString ManualExcelAnchorCaptureProvider::source() const
{
    return QStringLiteral("manual");
}

ExcelCaptureResult ManualExcelAnchorCaptureProvider::capture(const ExcelCaptureRequest &request) const
{
    ExcelCaptureResult result = resultForRequest(request, source());

    if (request.anchorName.trimmed().isEmpty()) {
        result.error = QStringLiteral("Excel capture anchor name is missing");
        return result;
    }
    if (result.targetFile.isEmpty()) {
        result.error = QStringLiteral("Excel capture target file is missing");
        return result;
    }
    if (!isExcelLocatorType(result.locatorType)) {
        result.error = QStringLiteral("Excel capture locator type is unsupported");
        return result;
    }
    if (result.rangeAddress.isEmpty() && result.namedRange.isEmpty()) {
        result.error = QStringLiteral("Excel capture range or named range is missing");
        return result;
    }

    if (result.locatorType == QLatin1String("excel.range")) {
        if (result.sheet.isEmpty()) {
            result.error = QStringLiteral("Excel capture range sheet is missing");
            return result;
        }
        if (result.rangeAddress.isEmpty()) {
            result.error = QStringLiteral("Excel capture range address is missing");
            return result;
        }
    } else if (result.namedRange.isEmpty()) {
        result.error = QStringLiteral("Excel capture named range is missing");
        return result;
    }

    Anchor anchor;
    anchor.type = AnchorType::Manual;
    anchor.name = request.anchorName.trimmed();
    anchor.target = anchor.name;
    anchor.targetApp = result.targetApp;
    anchor.targetFile = result.targetFile;
    anchor.locatorType = result.locatorType;
    anchor.locatorJson = excelLocatorJson(request);
    result.anchor = anchor;
    return result;
}

QString ManualVisioAnchorCaptureProvider::source() const
{
    return QStringLiteral("manual");
}

VisioCaptureResult ManualVisioAnchorCaptureProvider::capture(const VisioCaptureRequest &request) const
{
    VisioCaptureResult result = resultForRequest(request, source());

    if (request.anchorName.trimmed().isEmpty()) {
        result.error = QStringLiteral("Visio capture anchor name is missing");
        return result;
    }
    if (result.targetFile.isEmpty()) {
        result.error = QStringLiteral("Visio capture target file is missing");
        return result;
    }
    if (!isVisioLocatorType(result.locatorType)) {
        result.error = QStringLiteral("Visio capture locator type is unsupported");
        return result;
    }
    if (result.page.isEmpty()) {
        result.error = QStringLiteral("Visio capture page is missing");
        return result;
    }
    if (result.shapeUniqueId.isEmpty()) {
        result.error = QStringLiteral("Visio capture shape UniqueID is missing");
        return result;
    }

    Anchor anchor;
    anchor.type = AnchorType::Manual;
    anchor.name = request.anchorName.trimmed();
    anchor.target = anchor.name;
    anchor.targetApp = result.targetApp;
    anchor.targetFile = result.targetFile;
    anchor.locatorType = result.locatorType;
    anchor.locatorJson = visioLocatorJson(request);
    result.anchor = anchor;
    return result;
}

QString ManualWordBookmarkAnchorCaptureProvider::source() const
{
    return QStringLiteral("manual");
}

WordCaptureResult ManualWordBookmarkAnchorCaptureProvider::capture(const WordCaptureRequest &request) const
{
    WordCaptureResult result = resultForRequest(request, source());

    if (request.anchorName.trimmed().isEmpty()) {
        result.error = QStringLiteral("Word capture anchor name is missing");
        return result;
    }
    if (result.targetFile.isEmpty()) {
        result.error = QStringLiteral("Word capture target file is missing");
        return result;
    }
    if (!isWordLocatorType(result.locatorType)) {
        result.error = QStringLiteral("Word capture locator type is unsupported");
        return result;
    }
    if (result.bookmark.isEmpty()) {
        result.error = QStringLiteral("Word capture bookmark is missing");
        return result;
    }

    Anchor anchor;
    anchor.type = AnchorType::Manual;
    anchor.name = request.anchorName.trimmed();
    anchor.target = anchor.name;
    anchor.targetApp = result.targetApp;
    anchor.targetFile = result.targetFile;
    anchor.locatorType = result.locatorType;
    anchor.locatorJson = wordLocatorJson(request);
    result.anchor = anchor;
    return result;
}

QString pdfXChangeRectLocatorJson(const PdfXChangeCaptureRequest &request)
{
    QJsonObject locator;
    locator.insert(QStringLiteral("type"), effectiveLocatorType(request));
    locator.insert(QStringLiteral("page"), request.page);
    locator.insert(QStringLiteral("rect"), rectArray(request.rect));
    locator.insert(QStringLiteral("unit"), effectiveUnit(request));
    locator.insert(QStringLiteral("source"), effectiveSource(request, QStringLiteral("manual")));
    if (request.zoom > 0.0) {
        locator.insert(QStringLiteral("zoom"), request.zoom);
    }

    return QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));
}

QString excelLocatorJson(const ExcelCaptureRequest &request)
{
    const QString locatorType = effectiveExcelLocatorType(request);

    QJsonObject locator;
    locator.insert(QStringLiteral("type"), locatorType);
    locator.insert(QStringLiteral("target_file"), request.targetFile.trimmed());
    locator.insert(QStringLiteral("source"), effectiveSource(request, QStringLiteral("manual")));
    if (locatorType == QLatin1String("excel.range")) {
        locator.insert(QStringLiteral("sheet"), request.sheet.trimmed());
        locator.insert(QStringLiteral("range"), request.rangeAddress.trimmed());
        const QString namedRange = request.namedRange.trimmed();
        if (!namedRange.isEmpty()) {
            locator.insert(QStringLiteral("name"), namedRange);
        }
    } else {
        locator.insert(QStringLiteral("name"), request.namedRange.trimmed());
    }

    return QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));
}

QString visioLocatorJson(const VisioCaptureRequest &request)
{
    const QString shapeUniqueId = request.shapeUniqueId.trimmed();

    QJsonObject locator;
    locator.insert(QStringLiteral("type"), effectiveVisioLocatorType(request));
    locator.insert(QStringLiteral("page"), request.page.trimmed());
    locator.insert(QStringLiteral("shape_unique_id"), shapeUniqueId);
    locator.insert(QStringLiteral("shapeUniqueID"), shapeUniqueId);
    locator.insert(QStringLiteral("target_file"), request.targetFile.trimmed());
    locator.insert(QStringLiteral("source"), effectiveSource(request, QStringLiteral("manual")));

    return QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));
}

QString wordLocatorJson(const WordCaptureRequest &request)
{
    QJsonObject locator;
    locator.insert(QStringLiteral("type"), effectiveWordLocatorType(request));
    locator.insert(QStringLiteral("bookmark"), request.bookmark.trimmed());
    locator.insert(QStringLiteral("target_file"), request.targetFile.trimmed());
    locator.insert(QStringLiteral("source"), effectiveSource(request, QStringLiteral("manual")));
    locator.insert(QStringLiteral("target_app"), effectiveWordTargetApp(request));

    return QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));
}

AnchorCaptureResult captureManualPdfXChangeRectAnchor(const PdfXChangeCaptureRequest &request)
{
    return ManualPdfXChangeRectCaptureProvider{}.capture(request);
}

ExcelCaptureResult captureManualExcelAnchor(const ExcelCaptureRequest &request)
{
    return ManualExcelAnchorCaptureProvider{}.capture(request);
}

VisioCaptureResult captureManualVisioAnchor(const VisioCaptureRequest &request)
{
    return ManualVisioAnchorCaptureProvider{}.capture(request);
}

WordCaptureResult captureManualWordBookmarkAnchor(const WordCaptureRequest &request)
{
    return ManualWordBookmarkAnchorCaptureProvider{}.capture(request);
}

} // namespace Pinloom
