#include "pinloom/core/PdfXChangeCommand.h"

#include "pinloom/core/ApplicationLaunchSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <optional>

namespace Pinloom {

namespace {

struct RectValues {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    bool valid = false;
};

enum class RectAction {
    Highlight,
    ViewRect,
};

struct LocatedRect {
    RectValues rect;
    RectAction action = RectAction::Highlight;
};

QString normalizedToken(QString value)
{
    value = value.trimmed().toLower();
    value.remove(QLatin1Char(' '));
    value.remove(QLatin1Char('-'));
    value.remove(QLatin1Char('_'));
    return value;
}

QString effectiveLocatorType(const Anchor &anchor, const QJsonObject &locator)
{
    QString locatorType = anchor.locatorType.trimmed();
    if (locatorType.isEmpty()) {
        locatorType = locator.value(QStringLiteral("type")).toString().trimmed();
    }
    if (locatorType.isEmpty()) {
        if (anchor.type == AnchorType::PdfRegion) {
            locatorType = QStringLiteral("pdf.region");
        } else if (anchor.type == AnchorType::PdfPage) {
            locatorType = QStringLiteral("pdf.page");
        }
    }
    return locatorType.toLower();
}

QJsonObject parseLocatorJson(const QString &locatorJson, QString *error)
{
    const QString trimmed = locatorJson.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("PDF-XChange locator JSON is invalid");
        }
        return {};
    }
    return document.object();
}

std::optional<double> numberValue(const QJsonValue &value)
{
    if (value.isDouble()) {
        return value.toDouble();
    }
    if (value.isString()) {
        bool ok = false;
        const double number = value.toString().trimmed().toDouble(&ok);
        if (ok) {
            return number;
        }
    }
    return std::nullopt;
}

int locatorPage(const Anchor &anchor, const QJsonObject &locator)
{
    const std::optional<double> page = numberValue(locator.value(QStringLiteral("page")));
    if (page.has_value()) {
        return static_cast<int>(std::round(page.value()));
    }
    if (anchor.page > 0) {
        return anchor.page;
    }
    return -1;
}

std::optional<double> locatorZoom(const QJsonObject &locator)
{
    return numberValue(locator.value(QStringLiteral("zoom")));
}

std::optional<QList<double>> fourNumbersFromArray(const QJsonArray &array)
{
    if (array.size() != 4) {
        return std::nullopt;
    }

    QList<double> values;
    for (const QJsonValue &value : array) {
        const std::optional<double> number = numberValue(value);
        if (!number.has_value()) {
            return std::nullopt;
        }
        values.append(number.value());
    }
    return values;
}

RectValues boundedRect(double left, double top, double right, double bottom)
{
    RectValues rect;
    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
    rect.valid = true;
    return rect;
}

RectValues boundsFromRectArray(const QJsonArray &array)
{
    const std::optional<QList<double>> values = fourNumbersFromArray(array);
    if (!values.has_value()) {
        return {};
    }
    return boundedRect(values->at(0), values->at(1), values->at(2), values->at(3));
}

RectValues boundsFromHighlightArray(const QJsonArray &array)
{
    const std::optional<QList<double>> values = fourNumbersFromArray(array);
    if (!values.has_value()) {
        return {};
    }
    return boundedRect(values->at(0), values->at(2), values->at(1), values->at(3));
}

RectValues boundsFromViewRectArray(const QJsonArray &array)
{
    const std::optional<QList<double>> values = fourNumbersFromArray(array);
    if (!values.has_value()) {
        return {};
    }
    return boundedRect(values->at(0),
                       values->at(1),
                       values->at(0) + values->at(2),
                       values->at(1) + values->at(3));
}

RectValues rectFromAnchorRegion(const Anchor &anchor)
{
    RectValues rect;
    if (!anchor.region.isValid()) {
        return rect;
    }

    return boundedRect(anchor.region.x(),
                       anchor.region.y(),
                       anchor.region.x() + anchor.region.width(),
                       anchor.region.y() + anchor.region.height());
}

bool wantsViewRect(const QJsonObject &locator)
{
    const QString mode = locator.value(QStringLiteral("mode")).toString().trimmed().toLower();
    const QString action = locator.value(QStringLiteral("action")).toString().trimmed().toLower();
    return mode == QLatin1String("viewrect")
        || action == QLatin1String("viewrect");
}

LocatedRect locatorRect(const Anchor &anchor, const QJsonObject &locator)
{
    const QJsonValue rectValue = locator.value(QStringLiteral("rect"));
    if (rectValue.isArray()) {
        const RectValues rect = boundsFromRectArray(rectValue.toArray());
        if (rect.valid) {
            return {rect, wantsViewRect(locator) ? RectAction::ViewRect : RectAction::Highlight};
        }
    }

    const QJsonValue highlightValue = locator.value(QStringLiteral("highlight"));
    if (highlightValue.isArray()) {
        const RectValues rect = boundsFromHighlightArray(highlightValue.toArray());
        if (rect.valid) {
            return {rect, RectAction::Highlight};
        }
    }

    const QJsonValue viewRectValue = locator.value(QStringLiteral("viewrect"));
    if (viewRectValue.isArray()) {
        const RectValues rect = boundsFromViewRectArray(viewRectValue.toArray());
        if (rect.valid) {
            return {rect, RectAction::ViewRect};
        }
    }

    const QJsonValue legacyRegion = locator.value(QStringLiteral("region"));
    if (legacyRegion.isArray()) {
        const RectValues rect = boundsFromViewRectArray(legacyRegion.toArray());
        if (rect.valid) {
            return {rect, wantsViewRect(locator) ? RectAction::ViewRect : RectAction::Highlight};
        }
    }

    const RectValues rect = rectFromAnchorRegion(anchor);
    return {rect, wantsViewRect(locator) ? RectAction::ViewRect : RectAction::Highlight};
}

QString decimalText(double value)
{
    QString text = QString::number(value, 'f', 4);
    while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0'))) {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }
    return text;
}

QString highlightText(const RectValues &rect)
{
    return QStringLiteral("%1,%2,%3,%4")
        .arg(decimalText(rect.left),
             decimalText(rect.right),
             decimalText(rect.top),
             decimalText(rect.bottom));
}

QString viewRectText(const RectValues &rect)
{
    return QStringLiteral("%1,%2,%3,%4")
        .arg(decimalText(rect.left),
             decimalText(rect.top),
             decimalText(rect.right - rect.left),
             decimalText(rect.bottom - rect.top));
}

bool locatorUsesPoints(const QJsonObject &locator)
{
    const QString unit = locator.value(QStringLiteral("unit")).toString().trimmed().toLower();
    return unit.isEmpty()
        || unit == QLatin1String("pt")
        || unit == QLatin1String("pts")
        || unit == QLatin1String("point")
        || unit == QLatin1String("points");
}

QString filePathFromUri(const QString &targetUri)
{
    const QUrl url(targetUri.trimmed());
    if (url.isValid() && url.isLocalFile()) {
        return url.toLocalFile();
    }
    return targetUri.trimmed();
}

QString targetFilePath(const Anchor &anchor, const QString &fallbackFilePath)
{
    const QString targetFile = anchor.targetFile.trimmed();
    if (!targetFile.isEmpty()) {
        return targetFile;
    }

    const QString targetUri = anchor.targetUri.trimmed();
    if (!targetUri.isEmpty()) {
        return filePathFromUri(targetUri);
    }

    return fallbackFilePath.trimmed();
}

QStringList defaultExecutablePaths()
{
    return {
        QStringLiteral("C:/Program Files/Tracker Software/PDF Editor/PDFXEdit.exe"),
        QStringLiteral("C:/Program Files (x86)/Tracker Software/PDF Editor/PDFXEdit.exe"),
        QStringLiteral("C:/Program Files/PDF-XChange Editor/PDFXEdit.exe"),
        QStringLiteral("C:/Program Files (x86)/PDF-XChange Editor/PDFXEdit.exe"),
    };
}

} // namespace

bool PdfXChangeCommandResult::success() const
{
    return error.isEmpty();
}

bool isPdfXChangeLocatorType(const QString &locatorType)
{
    const QString type = locatorType.trimmed().toLower();
    return type == QLatin1String("pdfxchange.rect")
        || type == QLatin1String("pdfxchange.page")
        || type == QLatin1String("pdf.region")
        || type == QLatin1String("pdf.page");
}

bool isPdfXChangeAnchor(const Anchor &anchor)
{
    QString parseError;
    const QJsonObject locator = parseLocatorJson(anchor.locatorJson, &parseError);
    const QString type = effectiveLocatorType(anchor, locator);
    if (isPdfXChangeLocatorType(type)) {
        return true;
    }

    if (anchor.type == AnchorType::Manual
        && (type.isEmpty() || type == QLatin1String("manual"))) {
        return false;
    }

    const QString app = normalizedToken(anchor.targetApp);
    return app == QLatin1String("pdf")
        || app == QLatin1String("pdfxchange")
        || app == QLatin1String("pdfxchangeeditor");
}

QString resolvePdfXChangeExecutablePath()
{
    const QString configured = qEnvironmentVariable("PINLOOM_PDFXCHANGE_PATH").trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    for (const QString &path : defaultExecutablePaths()) {
        if (QFileInfo::exists(path)) {
            return QDir::toNativeSeparators(path);
        }
    }
    return {};
}

QString resolvePdfXChangeExecutablePath(const ApplicationLaunchSettings &settings)
{
    const QString configured = settings.pdfXChangeExecutablePath.trimmed();
    return configured.isEmpty() ? resolvePdfXChangeExecutablePath() : configured;
}

PdfXChangeCommandResult buildPdfXChangeCommand(const Anchor &anchor,
                                               const QString &fallbackFilePath,
                                               const ApplicationLaunchSettings &settings)
{
    return buildPdfXChangeCommand(anchor,
                                  fallbackFilePath,
                                  resolvePdfXChangeExecutablePath(settings));
}

PdfXChangeCommandResult buildPdfXChangeCommand(const Anchor &anchor,
                                               const QString &fallbackFilePath,
                                               const QString &executablePath)
{
    PdfXChangeCommandResult result;
    result.command.executablePath = executablePath.trimmed();
    if (result.command.executablePath.isEmpty()) {
        result.error = QStringLiteral("PDF-XChange executable is not configured/found");
        return result;
    }

    result.command.filePath = targetFilePath(anchor, fallbackFilePath);
    if (result.command.filePath.isEmpty()) {
        result.error = QStringLiteral("PDF-XChange target file is missing");
        return result;
    }

    QString parseError;
    const QJsonObject locator = parseLocatorJson(anchor.locatorJson, &parseError);
    if (!parseError.isEmpty()) {
        result.error = parseError;
        return result;
    }

    const QString locatorType = effectiveLocatorType(anchor, locator);
    if (!isPdfXChangeLocatorType(locatorType)) {
        result.error = QStringLiteral("PDF-XChange locator type is unsupported");
        return result;
    }

    const int page = locatorPage(anchor, locator);
    if (page <= 0) {
        result.error = QStringLiteral("PDF-XChange locator page is missing");
        return result;
    }

    QStringList actions;
    actions.append(QStringLiteral("page=%1").arg(page));

    const std::optional<double> zoom = locatorZoom(locator);
    if (zoom.has_value() && zoom.value() > 0.0) {
        actions.append(QStringLiteral("zoom=%1").arg(decimalText(zoom.value())));
    }

    const bool isRectLocator = locatorType == QLatin1String("pdfxchange.rect")
        || locatorType == QLatin1String("pdf.region")
        || anchor.type == AnchorType::PdfRegion;
    if (isRectLocator) {
        const LocatedRect locatedRect = locatorRect(anchor, locator);
        if (!locatedRect.rect.valid) {
            result.error = QStringLiteral("PDF-XChange locator rectangle is missing");
            return result;
        }

        if (locatedRect.action == RectAction::ViewRect) {
            actions.append(QStringLiteral("viewrect=%1").arg(viewRectText(locatedRect.rect)));
        } else {
            actions.append(QStringLiteral("highlight=%1").arg(highlightText(locatedRect.rect)));
        }
        if (locatorUsesPoints(locator)) {
            actions.append(QStringLiteral("usept=yes"));
        }
    }

    result.command.action = actions.join(QLatin1Char(';'));
    result.command.arguments = {QStringLiteral("/A"), result.command.action, result.command.filePath};
    return result;
}

} // namespace Pinloom
