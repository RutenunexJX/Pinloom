#include "pinloom/core/AnchorCapture.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace Pinloom {

namespace {

QString effectiveTargetApp(const PdfCaptureRequest &request)
{
    const QString targetApp = request.targetApp.trimmed();
    return targetApp.isEmpty() ? QStringLiteral("SumatraPDF") : targetApp;
}

QString effectiveLocatorType(const PdfCaptureRequest &request)
{
    const QString locatorType = request.locatorType.trimmed().toLower();
    if (!locatorType.isEmpty()) {
        if (locatorType == QLatin1String("sumatrapdf.rect")) {
            return QStringLiteral("sumatrapdf.rect");
        }
        if (locatorType == QLatin1String("sumatrapdf.page")) {
            return QStringLiteral("sumatrapdf.page");
        }
        if (locatorType == QLatin1String("sumatrapdf.search")) {
            return QStringLiteral("sumatrapdf.search");
        }
        return locatorType;
    }
    if (request.rect.isValid()) {
        return QStringLiteral("sumatrapdf.rect");
    }
    return QStringLiteral("sumatrapdf.page");
}

QString effectiveUnit(const PdfCaptureRequest &request)
{
    const QString unit = request.unit.trimmed().toLower();
    return unit.isEmpty() ? QStringLiteral("pt") : unit;
}

QString effectiveSource(const PdfCaptureRequest &request, const QString &fallback)
{
    const QString source = request.source.trimmed().toLower();
    return source.isEmpty() ? fallback : source;
}

QString defaultAnchorName(const PdfCaptureRequest &request)
{
    const QString fileName = QFileInfo(request.targetFile.trimmed()).fileName();
    const QString target = fileName.isEmpty() ? request.targetFile.trimmed() : fileName;
    if (target.isEmpty()) {
        return QStringLiteral("PDF page %1").arg(request.page);
    }
    return QStringLiteral("%1 page %2").arg(target, QString::number(request.page));
}

AnchorCaptureResult resultForRequest(const PdfCaptureRequest &request,
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

QString ManualPdfRectCaptureProvider::source() const
{
    return QStringLiteral("manual");
}

AnchorCaptureResult ManualPdfRectCaptureProvider::capture(const PdfCaptureRequest &request) const
{
    PdfCaptureRequest rectRequest = request;
    rectRequest.locatorType = QStringLiteral("sumatrapdf.rect");
    AnchorCaptureResult result = resultForRequest(rectRequest, source());

    if (result.targetFile.isEmpty()) {
        result.error = QStringLiteral("SumatraPDF capture target file is missing");
        return result;
    }
    if (result.locatorType != QLatin1String("sumatrapdf.rect")) {
        result.error = QStringLiteral("SumatraPDF capture locator type must be sumatrapdf.rect");
        return result;
    }
    if (result.page <= 0) {
        result.error = QStringLiteral("SumatraPDF capture page is missing");
        return result;
    }
    if (!result.rect.isValid()) {
        result.error = QStringLiteral("SumatraPDF capture rectangle is missing");
        return result;
    }
    if (result.zoom > 0.0 && !std::isfinite(result.zoom)) {
        result.error = QStringLiteral("SumatraPDF capture zoom is invalid");
        return result;
    }

    Anchor anchor;
    anchor.name = rectRequest.anchorName.trimmed();
    if (anchor.name.isEmpty()) {
        anchor.name = defaultAnchorName(rectRequest);
    }
    anchor.targetApp = result.targetApp;
    anchor.targetFile = result.targetFile;
    anchor.locatorType = result.locatorType;
    anchor.locatorJson = pdfRectLocatorJson(rectRequest);
    result.anchor = anchor;
    return result;
}

QString pdfRectLocatorJson(const PdfCaptureRequest &request)
{
    PdfCaptureRequest rectRequest = request;
    rectRequest.locatorType = QStringLiteral("sumatrapdf.rect");
    return pdfLocatorJson(rectRequest);
}

QString pdfLocatorJson(const PdfCaptureRequest &request)
{
    QJsonObject locator;
    locator.insert(QStringLiteral("type"), effectiveLocatorType(request));
    locator.insert(QStringLiteral("page"), request.page);
    const QString locatorType = effectiveLocatorType(request);
    if (locatorType == QLatin1String("sumatrapdf.rect")) {
        locator.insert(QStringLiteral("rect"), rectArray(request.rect));
        locator.insert(QStringLiteral("unit"), effectiveUnit(request));
        locator.insert(QStringLiteral("version"), 2);
        locator.insert(QStringLiteral("coordinateSpace"),
                       QStringLiteral("page-top-left"));
    } else if (locatorType == QLatin1String("sumatrapdf.search")) {
        locator.insert(QStringLiteral("text"), request.searchText.simplified());
        if (!request.contextBefore.trimmed().isEmpty()) {
            locator.insert(QStringLiteral("contextBefore"),
                           request.contextBefore.simplified());
        }
        if (!request.contextAfter.trimmed().isEmpty()) {
            locator.insert(QStringLiteral("contextAfter"),
                           request.contextAfter.simplified());
        }
        if (request.occurrence >= 0) {
            locator.insert(QStringLiteral("occurrence"), request.occurrence);
        }
        if (request.fallbackRect.isValid()) {
            locator.insert(QStringLiteral("fallbackRect"),
                           rectArray(request.fallbackRect));
            locator.insert(QStringLiteral("unit"), effectiveUnit(request));
        }
    }
    locator.insert(QStringLiteral("source"), effectiveSource(request, QStringLiteral("manual")));
    if (request.zoom > 0.0) {
        locator.insert(QStringLiteral("zoom"), request.zoom);
    }

    return QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));
}

AnchorCaptureResult captureManualPdfAnchor(const PdfCaptureRequest &request)
{
    AnchorCaptureResult result = resultForRequest(request, QStringLiteral("manual"));

    if (result.targetFile.isEmpty()) {
        result.error = QStringLiteral("SumatraPDF capture target file is missing");
        return result;
    }
    if (result.locatorType != QLatin1String("sumatrapdf.rect")
        && result.locatorType != QLatin1String("sumatrapdf.page")
        && result.locatorType != QLatin1String("sumatrapdf.search")) {
        result.error = QStringLiteral("SumatraPDF capture locator type is unsupported");
        return result;
    }
    if (result.page <= 0) {
        result.error = QStringLiteral("SumatraPDF capture page is missing");
        return result;
    }
    if (result.locatorType == QLatin1String("sumatrapdf.rect") && !result.rect.isValid()) {
        result.error = QStringLiteral("SumatraPDF capture rectangle is missing");
        return result;
    }
    if (result.locatorType == QLatin1String("sumatrapdf.search")
        && request.searchText.simplified().isEmpty()) {
        result.error = QStringLiteral("SumatraPDF capture search text is missing");
        return result;
    }
    if (result.zoom > 0.0 && !std::isfinite(result.zoom)) {
        result.error = QStringLiteral("SumatraPDF capture zoom is invalid");
        return result;
    }

    Anchor anchor;
    anchor.name = request.anchorName.trimmed();
    if (anchor.name.isEmpty()) {
        anchor.name = defaultAnchorName(request);
    }
    anchor.targetApp = result.targetApp;
    anchor.targetFile = result.targetFile;
    anchor.locatorType = result.locatorType;
    anchor.locatorJson = pdfLocatorJson(request);
    result.anchor = anchor;
    return result;
}

AnchorCaptureResult captureManualPdfRectAnchor(const PdfCaptureRequest &request)
{
    return ManualPdfRectCaptureProvider{}.capture(request);
}

} // namespace Pinloom
