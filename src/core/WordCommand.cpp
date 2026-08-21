#include "pinloom/core/WordCommand.h"

#include "pinloom/core/ApplicationLaunchSettings.h"
#include "pinloom/core/AnchorTarget.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

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
            *error = QStringLiteral("Word locator JSON is invalid");
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
    return resolveAnchorTarget(anchor, {locatorDocumentPath(locator)}, fallbackFilePath).value;
}

QString bookmarkName(const QJsonObject &locator)
{
    const QString bookmark = locator.value(QStringLiteral("bookmark")).toString().trimmed();
    if (!bookmark.isEmpty()) {
        return bookmark;
    }
    return locator.value(QStringLiteral("name")).toString().trimmed();
}

QString powerShellQuoted(QString value)
{
    value.replace(QLatin1String("'"), QLatin1String("''"));
    return QStringLiteral("'%1'").arg(value);
}

QString powerShellScriptForCommand(const WordJumpCommand &command)
{
    QStringList statements{
        QStringLiteral("$ErrorActionPreference = 'Stop'"),
        QStringLiteral("$word = New-Object -ComObject Word.Application"),
        QStringLiteral("$word.Visible = $true"),
        QStringLiteral("$document = $word.Documents.Open(%1)").arg(powerShellQuoted(command.documentPath)),
        QStringLiteral("$document.Activate()"),
        QStringLiteral("$bookmark = $document.Bookmarks.Item(%1)").arg(powerShellQuoted(command.bookmarkName)),
        QStringLiteral("$range = $bookmark.Range"),
        QStringLiteral("$range.Select()"),
        QStringLiteral("$word.Selection.GoTo(-1, 1, $null, %1) | Out-Null")
            .arg(powerShellQuoted(command.bookmarkName)),
        QStringLiteral("$word.Activate()"),
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

bool WordJumpCommandResult::success() const
{
    return error.isEmpty();
}

bool isWordLocatorType(const QString &locatorType)
{
    const QString type = locatorType.trimmed().toLower();
    return type == QLatin1String("word.bookmark");
}

bool isWordAnchor(const Anchor &anchor)
{
    QString parseError;
    const QJsonObject locator = parseLocatorJson(anchor.locatorJson, &parseError);
    if (isWordLocatorType(effectiveLocatorType(anchor, locator))) {
        return true;
    }

    const QString app = normalizedToken(anchor.targetApp);
    return app == QLatin1String("word")
        || app == QLatin1String("msword")
        || app == QLatin1String("microsoftword");
}

WordJumpCommandResult buildWordJumpCommand(const Anchor &anchor, const QString &fallbackFilePath)
{
    return buildWordJumpCommand(anchor, fallbackFilePath, ApplicationLaunchSettings{});
}

WordJumpCommandResult buildWordJumpCommand(const Anchor &anchor,
                                           const QString &fallbackFilePath,
                                           const ApplicationLaunchSettings &settings)
{
    WordJumpCommandResult result;

    QString parseError;
    const QJsonObject locator = parseLocatorJson(anchor.locatorJson, &parseError);
    if (!parseError.isEmpty()) {
        result.error = parseError;
        return result;
    }

    result.command.documentPath = targetDocumentPath(anchor, locator, fallbackFilePath);
    if (result.command.documentPath.isEmpty()) {
        result.error = QStringLiteral("Word document path is missing");
        return result;
    }

    result.command.locatorType = effectiveLocatorType(anchor, locator);
    if (!isWordLocatorType(result.command.locatorType)) {
        result.error = QStringLiteral("Word locator type is unsupported");
        return result;
    }

    result.command.bookmarkName = bookmarkName(locator);
    if (result.command.bookmarkName.isEmpty()) {
        result.error = QStringLiteral("Word bookmark is missing");
        return result;
    }

    result.command.executablePath = effectivePowerShellExecutablePath(settings);
    result.command.powerShellScript = powerShellScriptForCommand(result.command);
    result.command.arguments = powerShellArguments(result.command.powerShellScript);
    return result;
}

} // namespace Pinloom
