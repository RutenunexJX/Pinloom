#include "pinloom/core/VisioCommand.h"

#include "pinloom/core/ApplicationLaunchSettings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

namespace Pinloom {

namespace {

QString normalizedToken(QString value)
{
    value = value.trimmed().toLower();
    value.remove(QLatin1Char(' '));
    value.remove(QLatin1Char('-'));
    value.remove(QLatin1Char('_'));
    return value;
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
            *error = QStringLiteral("Visio locator JSON is invalid");
        }
        return {};
    }
    return document.object();
}

QString effectiveLocatorType(const Anchor &anchor, const QJsonObject &locator)
{
    QString locatorType = anchor.locatorType.trimmed();
    if (locatorType.isEmpty()) {
        locatorType = locator.value(QStringLiteral("type")).toString().trimmed();
    }
    return locatorType.toLower();
}

QString filePathFromUri(const QString &targetUri)
{
    const QUrl url(targetUri.trimmed());
    if (url.isValid() && url.isLocalFile()) {
        return url.toLocalFile();
    }
    return targetUri.trimmed();
}

QString locatorDocumentPath(const QJsonObject &locator)
{
    const QString document = locator.value(QStringLiteral("document")).toString().trimmed();
    if (!document.isEmpty()) {
        return document;
    }
    return locator.value(QStringLiteral("target_file")).toString().trimmed();
}

QString targetDocumentPath(const Anchor &anchor, const QJsonObject &locator, const QString &fallbackFilePath)
{
    const QString targetFile = anchor.targetFile.trimmed();
    if (!targetFile.isEmpty()) {
        return targetFile;
    }

    const QString targetUri = anchor.targetUri.trimmed();
    if (!targetUri.isEmpty()) {
        return filePathFromUri(targetUri);
    }

    const QString locatorDocument = locatorDocumentPath(locator);
    if (!locatorDocument.isEmpty()) {
        return locatorDocument;
    }

    return fallbackFilePath.trimmed();
}

QString shapeUniqueId(const QJsonObject &locator)
{
    const QString snakeCase = locator.value(QStringLiteral("shape_unique_id")).toString().trimmed();
    if (!snakeCase.isEmpty()) {
        return snakeCase;
    }
    const QString camelCase = locator.value(QStringLiteral("shapeUniqueId")).toString().trimmed();
    if (!camelCase.isEmpty()) {
        return camelCase;
    }
    return locator.value(QStringLiteral("shapeUniqueID")).toString().trimmed();
}

QString powerShellQuoted(QString value)
{
    value.replace(QLatin1String("'"), QLatin1String("''"));
    return QStringLiteral("'%1'").arg(value);
}

QString powerShellScriptForCommand(const VisioJumpCommand &command)
{
    QStringList statements{
        QStringLiteral("$ErrorActionPreference = 'Stop'"),
        QStringLiteral("$visio = New-Object -ComObject Visio.Application"),
        QStringLiteral("$visio.Visible = $true"),
        QStringLiteral("$document = $visio.Documents.Open(%1)").arg(powerShellQuoted(command.documentPath)),
        QStringLiteral("$page = $document.Pages.ItemU(%1)").arg(powerShellQuoted(command.pageName)),
        QStringLiteral("$window = $visio.ActiveWindow"),
        QStringLiteral("$window.Page = $page"),
        QStringLiteral("$shape = $page.Shapes.ItemFromUniqueID(%1)").arg(powerShellQuoted(command.shapeUniqueId)),
        QStringLiteral("$window.DeselectAll()"),
        QStringLiteral("$window.Select($shape, 2) | Out-Null"),
        QStringLiteral("$window.Activate()"),
    };
    return statements.join(QStringLiteral("; "));
}

QStringList powerShellArguments(const QString &script)
{
    return {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-STA"),
        QStringLiteral("-ExecutionPolicy"),
        QStringLiteral("Bypass"),
        QStringLiteral("-Command"),
        script,
    };
}

} // namespace

bool VisioJumpCommandResult::success() const
{
    return error.isEmpty();
}

bool isVisioLocatorType(const QString &locatorType)
{
    const QString type = locatorType.trimmed().toLower();
    return type == QLatin1String("visio.shape");
}

bool isVisioAnchor(const Anchor &anchor)
{
    QString parseError;
    const QJsonObject locator = parseLocatorJson(anchor.locatorJson, &parseError);
    if (isVisioLocatorType(effectiveLocatorType(anchor, locator))) {
        return true;
    }

    const QString app = normalizedToken(anchor.targetApp);
    return app == QLatin1String("visio")
        || app == QLatin1String("msvisio")
        || app == QLatin1String("microsoftvisio");
}

VisioJumpCommandResult buildVisioJumpCommand(const Anchor &anchor, const QString &fallbackFilePath)
{
    return buildVisioJumpCommand(anchor, fallbackFilePath, ApplicationLaunchSettings{});
}

VisioJumpCommandResult buildVisioJumpCommand(const Anchor &anchor,
                                             const QString &fallbackFilePath,
                                             const ApplicationLaunchSettings &settings)
{
    VisioJumpCommandResult result;

    QString parseError;
    const QJsonObject locator = parseLocatorJson(anchor.locatorJson, &parseError);
    if (!parseError.isEmpty()) {
        result.error = parseError;
        return result;
    }

    result.command.documentPath = targetDocumentPath(anchor, locator, fallbackFilePath);
    if (result.command.documentPath.isEmpty()) {
        result.error = QStringLiteral("Visio document path is missing");
        return result;
    }

    result.command.locatorType = effectiveLocatorType(anchor, locator);
    if (!isVisioLocatorType(result.command.locatorType)) {
        result.error = QStringLiteral("Visio locator type is unsupported");
        return result;
    }

    result.command.pageName = locator.value(QStringLiteral("page")).toString().trimmed();
    if (result.command.pageName.isEmpty()) {
        result.error = QStringLiteral("Visio locator page is missing");
        return result;
    }

    result.command.shapeUniqueId = shapeUniqueId(locator);
    if (result.command.shapeUniqueId.isEmpty()) {
        result.error = QStringLiteral("Visio shape UniqueID is missing");
        return result;
    }

    result.command.executablePath = effectivePowerShellExecutablePath(settings);
    result.command.powerShellScript = powerShellScriptForCommand(result.command);
    result.command.arguments = powerShellArguments(result.command.powerShellScript);
    return result;
}

} // namespace Pinloom
