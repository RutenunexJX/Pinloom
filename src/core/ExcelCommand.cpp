#include "pinloom/core/ExcelCommand.h"

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
            *error = QStringLiteral("Excel locator JSON is invalid");
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

QString locatorWorkbookPath(const QJsonObject &locator)
{
    const QString workbook = locator.value(QStringLiteral("workbook")).toString().trimmed();
    if (!workbook.isEmpty()) {
        return workbook;
    }
    return locator.value(QStringLiteral("target_file")).toString().trimmed();
}

QString targetWorkbookPath(const Anchor &anchor, const QJsonObject &locator, const QString &fallbackFilePath)
{
    const QString targetFile = anchor.targetFile.trimmed();
    if (!targetFile.isEmpty()) {
        return targetFile;
    }

    const QString targetUri = anchor.targetUri.trimmed();
    if (!targetUri.isEmpty()) {
        return filePathFromUri(targetUri);
    }

    const QString locatorWorkbook = locatorWorkbookPath(locator);
    if (!locatorWorkbook.isEmpty()) {
        return locatorWorkbook;
    }

    return fallbackFilePath.trimmed();
}

QString powerShellQuoted(QString value)
{
    value.replace(QLatin1String("'"), QLatin1String("''"));
    return QStringLiteral("'%1'").arg(value);
}

QString powerShellScriptForCommand(const ExcelJumpCommand &command)
{
    QStringList statements{
        QStringLiteral("$ErrorActionPreference = 'Stop'"),
        QStringLiteral("$excel = New-Object -ComObject Excel.Application"),
        QStringLiteral("$excel.Visible = $true"),
        QStringLiteral("$workbook = $excel.Workbooks.Open(%1)").arg(powerShellQuoted(command.workbookPath)),
    };

    if (command.locatorType == QLatin1String("excel.range")) {
        statements.append(QStringLiteral("$worksheet = $workbook.Worksheets.Item(%1)")
                              .arg(powerShellQuoted(command.sheetName)));
        statements.append(QStringLiteral("$worksheet.Activate()"));
        statements.append(QStringLiteral("$target = $worksheet.Range(%1)")
                              .arg(powerShellQuoted(command.rangeAddress)));
    } else {
        statements.append(QStringLiteral("$target = $workbook.Names.Item(%1).RefersToRange")
                              .arg(powerShellQuoted(command.namedRange)));
        statements.append(QStringLiteral("$target.Worksheet.Activate()"));
    }

    statements.append(QStringLiteral("$target.Select()"));
    statements.append(QStringLiteral("$excel.Application.Goto($target, $true) | Out-Null"));
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

bool ExcelJumpCommandResult::success() const
{
    return error.isEmpty();
}

bool isExcelLocatorType(const QString &locatorType)
{
    const QString type = locatorType.trimmed().toLower();
    return type == QLatin1String("excel.range")
        || type == QLatin1String("excel.name");
}

bool isExcelAnchor(const Anchor &anchor)
{
    QString parseError;
    const QJsonObject locator = parseLocatorJson(anchor.locatorJson, &parseError);
    if (isExcelLocatorType(effectiveLocatorType(anchor, locator))) {
        return true;
    }

    const QString app = normalizedToken(anchor.targetApp);
    return app == QLatin1String("excel")
        || app == QLatin1String("msexcel")
        || app == QLatin1String("microsoftexcel");
}

ExcelJumpCommandResult buildExcelJumpCommand(const Anchor &anchor, const QString &fallbackFilePath)
{
    ExcelJumpCommandResult result;

    QString parseError;
    const QJsonObject locator = parseLocatorJson(anchor.locatorJson, &parseError);
    if (!parseError.isEmpty()) {
        result.error = parseError;
        return result;
    }

    result.command.workbookPath = targetWorkbookPath(anchor, locator, fallbackFilePath);
    if (result.command.workbookPath.isEmpty()) {
        result.error = QStringLiteral("Excel workbook path is missing");
        return result;
    }

    result.command.locatorType = effectiveLocatorType(anchor, locator);
    if (!isExcelLocatorType(result.command.locatorType)) {
        result.error = QStringLiteral("Excel locator type is unsupported");
        return result;
    }

    if (result.command.locatorType == QLatin1String("excel.range")) {
        result.command.sheetName = locator.value(QStringLiteral("sheet")).toString().trimmed();
        result.command.rangeAddress = locator.value(QStringLiteral("range")).toString().trimmed();
        if (result.command.sheetName.isEmpty()) {
            result.error = QStringLiteral("Excel range sheet is missing");
            return result;
        }
        if (result.command.rangeAddress.isEmpty()) {
            result.error = QStringLiteral("Excel range address is missing");
            return result;
        }
    } else {
        result.command.namedRange = locator.value(QStringLiteral("name")).toString().trimmed();
        if (result.command.namedRange.isEmpty()) {
            result.error = QStringLiteral("Excel named range is missing");
            return result;
        }
    }

    result.command.executablePath = QStringLiteral("powershell.exe");
    result.command.powerShellScript = powerShellScriptForCommand(result.command);
    result.command.arguments = powerShellArguments(result.command.powerShellScript);
    return result;
}

} // namespace Pinloom
