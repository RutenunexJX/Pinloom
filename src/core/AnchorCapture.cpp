#include "pinloom/core/AnchorCapture.h"

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

AnchorCaptureResult captureManualPdfXChangeRectAnchor(const PdfXChangeCaptureRequest &request)
{
    return ManualPdfXChangeRectCaptureProvider{}.capture(request);
}

} // namespace Pinloom
