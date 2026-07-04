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
    QList<double> values;
    bool valid = false;
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
    return anchor.page > 0 ? anchor.page : -1;
}

std::optional<double> locatorZoom(const QJsonObject &locator)
{
    return numberValue(locator.value(QStringLiteral("zoom")));
}

RectValues rectFromArray(const QJsonArray &array)
{
    RectValues rect;
    if (array.size() != 4) {
        return rect;
    }

    for (const QJsonValue &value : array) {
        const std::optional<double> number = numberValue(value);
        if (!number.has_value()) {
            rect.values.clear();
            return rect;
        }
        rect.values.append(number.value());
    }
    rect.valid = true;
    return rect;
}

RectValues exactRectFromLocator(const QJsonObject &locator, const QString &key)
{
    const QJsonValue value = locator.value(key);
    if (!value.isArray()) {
        return {};
    }
    return rectFromArray(value.toArray());
}

RectValues legacyRegionFromArray(const QJsonArray &array)
{
    RectValues region = rectFromArray(array);
    if (!region.valid) {
        return region;
    }

    region.values[2] = region.values.at(0) + region.values.at(2);
    region.values[3] = region.values.at(1) + region.values.at(3);
    return region;
}

RectValues rectFromAnchorRegion(const Anchor &anchor)
{
    RectValues rect;
    if (!anchor.region.isValid()) {
        return rect;
    }

    rect.values = {anchor.region.x(),
                   anchor.region.y(),
                   anchor.region.x() + anchor.region.width(),
                   anchor.region.y() + anchor.region.height()};
    rect.valid = true;
    return rect;
}

RectValues locatorRect(const Anchor &anchor, const QJsonObject &locator)
{
    RectValues rect = exactRectFromLocator(locator, QStringLiteral("rect"));
    if (rect.valid) {
        return rect;
    }

    rect = exactRectFromLocator(locator, QStringLiteral("highlight"));
    if (rect.valid) {
        return rect;
    }

    rect = exactRectFromLocator(locator, QStringLiteral("viewrect"));
    if (rect.valid) {
        return rect;
    }

    const QJsonValue legacyRegion = locator.value(QStringLiteral("region"));
    if (legacyRegion.isArray()) {
        rect = legacyRegionFromArray(legacyRegion.toArray());
        if (rect.valid) {
            return rect;
        }
    }

    return rectFromAnchorRegion(anchor);
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

QString rectText(const RectValues &rect)
{
    QStringList parts;
    for (double value : rect.values) {
        parts.append(decimalText(value));
    }
    return parts.join(QLatin1Char(','));
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

bool wantsViewRect(const QJsonObject &locator)
{
    const QString mode = locator.value(QStringLiteral("mode")).toString().trimmed().toLower();
    const QString action = locator.value(QStringLiteral("action")).toString().trimmed().toLower();
    return mode == QLatin1String("viewrect")
        || action == QLatin1String("viewrect");
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
        const RectValues rect = locatorRect(anchor, locator);
        if (!rect.valid) {
            result.error = QStringLiteral("PDF-XChange locator rectangle is missing");
            return result;
        }

        actions.append(QStringLiteral("%1=%2")
                           .arg(wantsViewRect(locator) ? QStringLiteral("viewrect") : QStringLiteral("highlight"),
                                rectText(rect)));
        if (locatorUsesPoints(locator)) {
            actions.append(QStringLiteral("usept=yes"));
        }
    }

    result.command.action = actions.join(QLatin1Char(';'));
    result.command.arguments = {QStringLiteral("/A"), result.command.action, result.command.filePath};
    return result;
}

} // namespace Pinloom
