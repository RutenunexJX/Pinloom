#include "pinloom/core/SumatraPdfCommand.h"

#include "pinloom/core/ApplicationLaunchSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStandardPaths>
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
            *error = QStringLiteral("SumatraPDF locator JSON is invalid");
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

int locatorPage(const QJsonObject &locator)
{
    const std::optional<double> page = numberValue(locator.value(QStringLiteral("page")));
    if (page.has_value()) {
        return static_cast<int>(std::round(page.value()));
    }
    return -1;
}

std::optional<double> locatorZoom(const QJsonObject &locator)
{
    return numberValue(locator.value(QStringLiteral("zoom")));
}

QString locatorText(const QJsonObject &locator)
{
    QString text = locator.value(QStringLiteral("text")).toString().trimmed();
    if (text.isEmpty()) {
        text = locator.value(QStringLiteral("selected_text")).toString().trimmed();
    }
    if (text.isEmpty()) {
        text = locator.value(QStringLiteral("selectedText")).toString().trimmed();
    }
    if (text.isEmpty()) {
        text = locator.value(QStringLiteral("search")).toString().trimmed();
    }
    return text.simplified();
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

RectValues locatorRect(const QJsonObject &locator)
{
    const QJsonValue rectValue = locator.value(QStringLiteral("rect"));
    if (rectValue.isArray()) {
        const RectValues rect = boundsFromRectArray(rectValue.toArray());
        if (rect.valid) {
            return rect;
        }
    }

    const QJsonValue highlightValue = locator.value(QStringLiteral("highlight"));
    if (highlightValue.isArray()) {
        const RectValues rect = boundsFromHighlightArray(highlightValue.toArray());
        if (rect.valid) {
            return rect;
        }
    }

    const QJsonValue viewRectValue = locator.value(QStringLiteral("viewrect"));
    if (viewRectValue.isArray()) {
        const RectValues rect = boundsFromViewRectArray(viewRectValue.toArray());
        if (rect.valid) {
            return rect;
        }
    }

    const QJsonValue legacyRegion = locator.value(QStringLiteral("region"));
    if (legacyRegion.isArray()) {
        const RectValues rect = boundsFromViewRectArray(legacyRegion.toArray());
        if (rect.valid) {
            return rect;
        }
    }

    return {};
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

QString openParameterText(QString value)
{
    value = value.simplified();
    value.replace(QLatin1Char('"'), QLatin1Char(' '));
    value.replace(QLatin1Char('\''), QLatin1Char(' '));
    value = value.simplified();
    constexpr qsizetype MaxSearchTextLength = 240;
    if (value.size() > MaxSearchTextLength) {
        value.truncate(MaxSearchTextLength);
        value = value.trimmed();
    }
    return value;
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
    QStringList paths = {
        QStringLiteral("D:/__software_install_dir/__SumatraPDF/SumatraPDF.exe"),
        QStringLiteral("C:/Program Files/SumatraPDF/SumatraPDF.exe"),
        QStringLiteral("C:/Program Files (x86)/SumatraPDF/SumatraPDF.exe"),
    };

    const QString localData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!localData.trimmed().isEmpty()) {
        QDir dir(localData);
        dir.cdUp();
        paths.append(dir.filePath(QStringLiteral("SumatraPDF/SumatraPDF.exe")));
    }
    return paths;
}

} // namespace

bool SumatraPdfCommandResult::success() const
{
    return error.isEmpty();
}

bool isSumatraPdfLocatorType(const QString &locatorType)
{
    const QString type = locatorType.trimmed().toLower();
    return type == QLatin1String("sumatrapdf.rect")
        || type == QLatin1String("sumatrapdf.page")
        || type == QLatin1String("sumatrapdf.search")
        || type == QLatin1String("pdf.region")
        || type == QLatin1String("pdf.page");
}

bool isSumatraPdfAnchor(const Anchor &anchor)
{
    QString parseError;
    const QJsonObject locator = parseLocatorJson(anchor.locatorJson, &parseError);
    const QString type = effectiveLocatorType(anchor, locator);
    if (isSumatraPdfLocatorType(type)) {
        return true;
    }

    const QString app = normalizedToken(anchor.targetApp);
    return app == QLatin1String("pdf")
        || app == QLatin1String("sumatrapdf");
}

QString resolveSumatraPdfExecutablePath()
{
    const QString configured = qEnvironmentVariable("PINLOOM_SUMATRAPDF_PATH").trimmed();
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

QString resolveSumatraPdfExecutablePath(const ApplicationLaunchSettings &settings)
{
    const QString configured = settings.sumatraPdfExecutablePath.trimmed();
    return configured.isEmpty() ? resolveSumatraPdfExecutablePath() : configured;
}

SumatraPdfCommandResult buildSumatraPdfCommand(const Anchor &anchor,
                                               const QString &fallbackFilePath,
                                               const ApplicationLaunchSettings &settings)
{
    return buildSumatraPdfCommand(anchor,
                                  fallbackFilePath,
                                  resolveSumatraPdfExecutablePath(settings));
}

SumatraPdfCommandResult buildSumatraPdfCommand(const Anchor &anchor,
                                               const QString &fallbackFilePath,
                                               const QString &executablePath)
{
    SumatraPdfCommandResult result;
    result.command.executablePath = executablePath.trimmed();
    if (result.command.executablePath.isEmpty()) {
        result.error = QStringLiteral("SumatraPDF executable is not configured/found");
        return result;
    }

    result.command.filePath = targetFilePath(anchor, fallbackFilePath);
    if (result.command.filePath.isEmpty()) {
        result.error = QStringLiteral("SumatraPDF target file is missing");
        return result;
    }

    QString parseError;
    const QJsonObject locator = parseLocatorJson(anchor.locatorJson, &parseError);
    if (!parseError.isEmpty()) {
        result.error = parseError;
        return result;
    }

    const QString locatorType = effectiveLocatorType(anchor, locator);
    if (!isSumatraPdfLocatorType(locatorType)) {
        result.error = QStringLiteral("SumatraPDF locator type is unsupported");
        return result;
    }

    const int page = locatorPage(locator);
    if (page <= 0) {
        result.error = QStringLiteral("SumatraPDF locator page is missing");
        return result;
    }
    result.command.page = page;

    QStringList arguments;
    arguments.append(QStringLiteral("-reuse-instance"));
    arguments.append(QStringLiteral("-page"));
    arguments.append(QString::number(page));

    const std::optional<double> zoom = locatorZoom(locator);
    if (zoom.has_value() && zoom.value() > 0.0) {
        result.command.zoom = zoom.value();
        arguments.append(QStringLiteral("-zoom"));
        arguments.append(decimalText(zoom.value()));
    }

    const bool isSearchLocator = locatorType == QLatin1String("sumatrapdf.search");
    const bool isRectLocator = !isSearchLocator
        && (locatorType == QLatin1String("sumatrapdf.rect")
            || locatorType == QLatin1String("pdf.region"));

    if (isSearchLocator) {
        const QString text = openParameterText(locatorText(locator));
        if (text.isEmpty()) {
            result.error = QStringLiteral("SumatraPDF locator search text is missing");
            return result;
        }
        arguments.append(QStringLiteral("-search"));
        arguments.append(text);
    }

    if (isRectLocator) {
        const RectValues rect = locatorRect(locator);
        if (!rect.valid) {
            result.error = QStringLiteral("SumatraPDF locator rectangle is missing");
            return result;
        }
        arguments.append(QStringLiteral("-scroll"));
        arguments.append(QStringLiteral("%1,%2").arg(decimalText(rect.left), decimalText(rect.top)));
        result.command.highlightRect = QRectF(rect.left,
                                              rect.top,
                                              rect.right - rect.left,
                                              rect.bottom - rect.top);
    }

    arguments.append(result.command.filePath);
    result.command.arguments = arguments;
    return result;
}

} // namespace Pinloom
