#include "pinloom/core/AnchorHealthCheck.h"

#include "pinloom/core/ExcelCommand.h"
#include "pinloom/core/PdfXChangeCommand.h"
#include "pinloom/core/PowerPointCommand.h"
#include "pinloom/core/VisioCommand.h"
#include "pinloom/core/WordCommand.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>
#include <optional>

namespace Pinloom {

namespace {

enum class AnchorLauncherFamily {
    Generic,
    PdfXChange,
    Excel,
    Word,
    PowerPoint,
    Visio,
};

struct ParsedLocator {
    QJsonObject object;
    QString error;
};

struct TargetPath {
    QString path;
    bool nonLocalUri = false;
};

QString normalizedToken(QString value)
{
    value = value.trimmed().toLower();
    value.remove(QLatin1Char(' '));
    value.remove(QLatin1Char('-'));
    value.remove(QLatin1Char('_'));
    return value;
}

ParsedLocator parseLocatorJson(const QString &locatorJson)
{
    const QString trimmed = locatorJson.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {{}, QStringLiteral("Anchor locator JSON is invalid")};
    }
    return {document.object(), {}};
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

bool isWindowsDrivePath(const QString &value)
{
    return value.size() >= 2
        && value.at(0).isLetter()
        && value.at(1) == QLatin1Char(':');
}

TargetPath targetPathFromValue(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QUrl url(trimmed);
    if (url.isValid() && !url.scheme().isEmpty() && !isWindowsDrivePath(trimmed)) {
        if (url.isLocalFile()) {
            return {url.toLocalFile(), false};
        }
        return {trimmed, true};
    }

    return {trimmed, false};
}

QString firstLocatorPathValue(const QJsonObject &locator, const QStringList &keys)
{
    for (const QString &key : keys) {
        const QString value = locator.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

TargetPath targetPathForAnchor(const Anchor &anchor,
                               const QJsonObject &locator,
                               AnchorLauncherFamily family,
                               const QString &fallbackFilePath)
{
    TargetPath path = targetPathFromValue(anchor.targetFile);
    if (!path.path.isEmpty() || path.nonLocalUri) {
        return path;
    }

    path = targetPathFromValue(anchor.targetUri);
    if (!path.path.isEmpty() || path.nonLocalUri) {
        return path;
    }

    QStringList locatorPathKeys;
    switch (family) {
    case AnchorLauncherFamily::Excel:
        locatorPathKeys = {QStringLiteral("workbook"), QStringLiteral("target_file")};
        break;
    case AnchorLauncherFamily::Word:
    case AnchorLauncherFamily::Visio:
        locatorPathKeys = {QStringLiteral("document"), QStringLiteral("target_file")};
        break;
    case AnchorLauncherFamily::PowerPoint:
        locatorPathKeys = {QStringLiteral("presentation"), QStringLiteral("target_file")};
        break;
    case AnchorLauncherFamily::Generic:
    case AnchorLauncherFamily::PdfXChange:
        break;
    }

    path = targetPathFromValue(firstLocatorPathValue(locator, locatorPathKeys));
    if (!path.path.isEmpty() || path.nonLocalUri) {
        return path;
    }

    return targetPathFromValue(fallbackFilePath);
}

QString familyLabel(AnchorLauncherFamily family, const Anchor &anchor)
{
    switch (family) {
    case AnchorLauncherFamily::PdfXChange:
        return externalApplicationLabel(ExternalApplicationTarget::PdfXChange);
    case AnchorLauncherFamily::Excel:
        return externalApplicationLabel(ExternalApplicationTarget::Excel);
    case AnchorLauncherFamily::Word:
        return externalApplicationLabel(ExternalApplicationTarget::Word);
    case AnchorLauncherFamily::PowerPoint:
        return externalApplicationLabel(ExternalApplicationTarget::PowerPoint);
    case AnchorLauncherFamily::Visio:
        return externalApplicationLabel(ExternalApplicationTarget::Visio);
    case AnchorLauncherFamily::Generic:
        return anchor.targetApp.trimmed();
    }
    return {};
}

AnchorLauncherFamily launcherFamilyForAnchor(const Anchor &anchor, const QString &locatorType)
{
    if (isPdfXChangeLocatorType(locatorType)) {
        return AnchorLauncherFamily::PdfXChange;
    }
    if (isExcelLocatorType(locatorType)) {
        return AnchorLauncherFamily::Excel;
    }
    if (isWordLocatorType(locatorType)) {
        return AnchorLauncherFamily::Word;
    }
    if (isPowerPointLocatorType(locatorType)) {
        return AnchorLauncherFamily::PowerPoint;
    }
    if (isVisioLocatorType(locatorType)) {
        return AnchorLauncherFamily::Visio;
    }

    const QString app = normalizedToken(anchor.targetApp);
    if (app == QLatin1String("pdf")
        || app == QLatin1String("pdfxchange")
        || app == QLatin1String("pdfxchangeeditor")) {
        return AnchorLauncherFamily::PdfXChange;
    }
    if (app == QLatin1String("excel")
        || app == QLatin1String("msexcel")
        || app == QLatin1String("microsoftexcel")) {
        return AnchorLauncherFamily::Excel;
    }
    if (app == QLatin1String("word")
        || app == QLatin1String("msword")
        || app == QLatin1String("microsoftword")) {
        return AnchorLauncherFamily::Word;
    }
    if (app == QLatin1String("powerpoint")
        || app == QLatin1String("mspowerpoint")
        || app == QLatin1String("microsoftpowerpoint")
        || app == QLatin1String("ppt")) {
        return AnchorLauncherFamily::PowerPoint;
    }
    if (app == QLatin1String("visio")
        || app == QLatin1String("msvisio")
        || app == QLatin1String("microsoftvisio")) {
        return AnchorLauncherFamily::Visio;
    }

    return AnchorLauncherFamily::Generic;
}

bool supportedLocator(AnchorLauncherFamily family, const QString &locatorType)
{
    switch (family) {
    case AnchorLauncherFamily::PdfXChange:
        return isPdfXChangeLocatorType(locatorType);
    case AnchorLauncherFamily::Excel:
        return isExcelLocatorType(locatorType);
    case AnchorLauncherFamily::Word:
        return isWordLocatorType(locatorType);
    case AnchorLauncherFamily::PowerPoint:
        return isPowerPointLocatorType(locatorType);
    case AnchorLauncherFamily::Visio:
        return isVisioLocatorType(locatorType);
    case AnchorLauncherFamily::Generic:
        return locatorType.isEmpty();
    }
    return false;
}

AnchorHealthCheckResult resultFor(AnchorHealthStatus status,
                                  const QString &message,
                                  const QString &reason,
                                  const QString &path,
                                  const QString &launcherPath,
                                  const QString &app,
                                  const QString &locatorType)
{
    AnchorHealthCheckResult result;
    result.status = status;
    result.message = message;
    result.reason = reason;
    result.path = path;
    result.launcherPath = launcherPath;
    result.app = app;
    result.locatorType = locatorType;
    return result;
}

std::optional<AnchorHealthCheckResult> launcherProblemForAnchor(
    AnchorLauncherFamily family,
    const ApplicationLaunchSettings &settings,
    const QString &app,
    const QString &locatorType,
    const QString &targetPath)
{
    if (family == AnchorLauncherFamily::PdfXChange) {
        const QString launcherPath = resolvePdfXChangeExecutablePath(settings).trimmed();
        if (launcherPath.isEmpty() || !QFileInfo::exists(launcherPath)) {
            return resultFor(AnchorHealthStatus::MissingLauncher,
                             QStringLiteral("Anchor launcher is missing"),
                             QStringLiteral("PDF-XChange executable is not configured/found"),
                             targetPath,
                             launcherPath,
                             app,
                             locatorType);
        }
    }

    if (family == AnchorLauncherFamily::Excel
        || family == AnchorLauncherFamily::Word
        || family == AnchorLauncherFamily::PowerPoint
        || family == AnchorLauncherFamily::Visio) {
        const QString configuredPowerShell = settings.powerShellExecutablePath.trimmed();
        if (!configuredPowerShell.isEmpty() && !QFileInfo::exists(configuredPowerShell)) {
            return resultFor(AnchorHealthStatus::MissingLauncher,
                             QStringLiteral("Anchor launcher is missing"),
                             QStringLiteral("Configured PowerShell executable does not exist"),
                             targetPath,
                             configuredPowerShell,
                             app,
                             locatorType);
        }
    }

    return std::nullopt;
}

} // namespace

bool AnchorHealthCheckResult::healthy() const
{
    return status == AnchorHealthStatus::Ok;
}

AnchorHealthCheckResult checkAnchorHealth(const Anchor &anchor,
                                          const QString &fallbackFilePath,
                                          const ApplicationLaunchSettings &settings)
{
    const ParsedLocator locator = parseLocatorJson(anchor.locatorJson);
    const QString locatorType = effectiveLocatorType(anchor, locator.object);
    const AnchorLauncherFamily family = launcherFamilyForAnchor(anchor, locatorType);
    const QString app = familyLabel(family, anchor);

    if (!locator.error.isEmpty()) {
        return resultFor(AnchorHealthStatus::InvalidLocator,
                         QStringLiteral("Anchor locator is invalid"),
                         locator.error,
                         {},
                         {},
                         app,
                         locatorType);
    }

    const TargetPath targetPath = targetPathForAnchor(anchor, locator.object, family, fallbackFilePath);
    if (targetPath.nonLocalUri) {
        return resultFor(AnchorHealthStatus::UnsupportedTarget,
                         QStringLiteral("Anchor target is not a local file"),
                         QStringLiteral("Non-local targets are not checked by the static health checker"),
                         targetPath.path,
                         {},
                         app,
                         locatorType);
    }

    if (targetPath.path.isEmpty()) {
        return resultFor(AnchorHealthStatus::MissingTarget,
                         QStringLiteral("Anchor target file is missing"),
                         QStringLiteral("No target file or fallback file is available"),
                         {},
                         {},
                         app,
                         locatorType);
    }

    if (!QFileInfo::exists(targetPath.path)) {
        return resultFor(AnchorHealthStatus::MissingTarget,
                         QStringLiteral("Anchor target file is missing"),
                         QStringLiteral("Target file does not exist"),
                         targetPath.path,
                         {},
                         app,
                         locatorType);
    }

    if (!supportedLocator(family, locatorType)) {
        return resultFor(AnchorHealthStatus::UnsupportedLocator,
                         QStringLiteral("Anchor locator is unsupported"),
                         QStringLiteral("Static health checks do not support this locator type yet"),
                         targetPath.path,
                         {},
                         app,
                         locatorType);
    }

    const std::optional<AnchorHealthCheckResult> launcherProblem =
        launcherProblemForAnchor(family, settings, app, locatorType, targetPath.path);
    if (launcherProblem.has_value()) {
        return launcherProblem.value();
    }

    return resultFor(AnchorHealthStatus::Ok,
                     QStringLiteral("Anchor target is statically reachable"),
                     QStringLiteral("Static checks found an existing local target and no missing configured launcher"),
                     targetPath.path,
                     {},
                     app,
                     locatorType);
}

} // namespace Pinloom
