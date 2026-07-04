#include "pinloom/core/PowerPointCommand.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QUrl>
#include <cmath>
#include <limits>

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
            *error = QStringLiteral("PowerPoint locator JSON is invalid");
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

QString locatorPresentationPath(const QJsonObject &locator)
{
    const QString presentation = locator.value(QStringLiteral("presentation")).toString().trimmed();
    if (!presentation.isEmpty()) {
        return presentation;
    }
    return locator.value(QStringLiteral("target_file")).toString().trimmed();
}

QString targetPresentationPath(const Anchor &anchor, const QJsonObject &locator, const QString &fallbackFilePath)
{
    const QString targetFile = anchor.targetFile.trimmed();
    if (!targetFile.isEmpty()) {
        return targetFile;
    }

    const QString targetUri = anchor.targetUri.trimmed();
    if (!targetUri.isEmpty()) {
        return filePathFromUri(targetUri);
    }

    const QString locatorPresentation = locatorPresentationPath(locator);
    if (!locatorPresentation.isEmpty()) {
        return locatorPresentation;
    }

    return fallbackFilePath.trimmed();
}

int positiveIntFromValue(const QJsonValue &value)
{
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (number > 0.0
            && number <= static_cast<double>(std::numeric_limits<int>::max())
            && std::floor(number) == number) {
            return static_cast<int>(number);
        }
    }

    if (value.isString()) {
        bool ok = false;
        const int parsed = value.toString().trimmed().toInt(&ok);
        if (ok && parsed > 0) {
            return parsed;
        }
    }

    return -1;
}

int slideIndex(const QJsonObject &locator)
{
    int slide = positiveIntFromValue(locator.value(QStringLiteral("slide")));
    if (slide > 0) {
        return slide;
    }
    return positiveIntFromValue(locator.value(QStringLiteral("slide_index")));
}

int shapeId(const QJsonObject &locator)
{
    int id = positiveIntFromValue(locator.value(QStringLiteral("shape_id")));
    if (id > 0) {
        return id;
    }
    return positiveIntFromValue(locator.value(QStringLiteral("shapeId")));
}

QString shapeName(const QJsonObject &locator)
{
    const QString snakeCase = locator.value(QStringLiteral("shape_name")).toString().trimmed();
    if (!snakeCase.isEmpty()) {
        return snakeCase;
    }
    return locator.value(QStringLiteral("shapeName")).toString().trimmed();
}

QString powerShellQuoted(QString value)
{
    value.replace(QLatin1String("'"), QLatin1String("''"));
    return QStringLiteral("'%1'").arg(value);
}

QString powerShellScriptForCommand(const PowerPointJumpCommand &command)
{
    QStringList statements{
        QStringLiteral("$ErrorActionPreference = 'Stop'"),
        QStringLiteral("$powerPoint = New-Object -ComObject PowerPoint.Application"),
        QStringLiteral("$powerPoint.Visible = $true"),
        QStringLiteral("$presentation = $powerPoint.Presentations.Open(%1)")
            .arg(powerShellQuoted(command.presentationPath)),
        QStringLiteral("$slide = $presentation.Slides.Item(%1)").arg(command.slideIndex),
        QStringLiteral("$window = $presentation.Windows.Item(1)"),
        QStringLiteral("$window.Activate()"),
        QStringLiteral("$window.View.GotoSlide(%1)").arg(command.slideIndex),
    };

    if (command.shapeId > 0) {
        statements.append(QStringLiteral("$shape = $slide.Shapes.FindById(%1)").arg(command.shapeId));
    } else {
        statements.append(QStringLiteral("$shape = $slide.Shapes.Item(%1)")
                              .arg(powerShellQuoted(command.shapeName)));
    }

    statements.append(QStringLiteral("$shape.Select($true)"));
    statements.append(QStringLiteral("$powerPoint.Activate()"));
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

bool PowerPointJumpCommandResult::success() const
{
    return error.isEmpty();
}

bool isPowerPointLocatorType(const QString &locatorType)
{
    const QString type = locatorType.trimmed().toLower();
    return type == QLatin1String("powerpoint.shape");
}

bool isPowerPointAnchor(const Anchor &anchor)
{
    QString parseError;
    const QJsonObject locator = parseLocatorJson(anchor.locatorJson, &parseError);
    if (isPowerPointLocatorType(effectiveLocatorType(anchor, locator))) {
        return true;
    }

    const QString app = normalizedToken(anchor.targetApp);
    return app == QLatin1String("powerpoint")
        || app == QLatin1String("mspowerpoint")
        || app == QLatin1String("microsoftpowerpoint")
        || app == QLatin1String("ppt");
}

PowerPointJumpCommandResult buildPowerPointJumpCommand(const Anchor &anchor, const QString &fallbackFilePath)
{
    PowerPointJumpCommandResult result;

    QString parseError;
    const QJsonObject locator = parseLocatorJson(anchor.locatorJson, &parseError);
    if (!parseError.isEmpty()) {
        result.error = parseError;
        return result;
    }

    result.command.presentationPath = targetPresentationPath(anchor, locator, fallbackFilePath);
    if (result.command.presentationPath.isEmpty()) {
        result.error = QStringLiteral("PowerPoint presentation path is missing");
        return result;
    }

    result.command.locatorType = effectiveLocatorType(anchor, locator);
    if (!isPowerPointLocatorType(result.command.locatorType)) {
        result.error = QStringLiteral("PowerPoint locator type is unsupported");
        return result;
    }

    result.command.slideIndex = slideIndex(locator);
    if (result.command.slideIndex <= 0) {
        result.error = QStringLiteral("PowerPoint slide is missing");
        return result;
    }

    result.command.shapeId = shapeId(locator);
    if (result.command.shapeId <= 0) {
        result.command.shapeName = shapeName(locator);
    }
    if (result.command.shapeId <= 0 && result.command.shapeName.isEmpty()) {
        result.error = QStringLiteral("PowerPoint shape id or name is missing");
        return result;
    }

    result.command.executablePath = QStringLiteral("powershell.exe");
    result.command.powerShellScript = powerShellScriptForCommand(result.command);
    result.command.arguments = powerShellArguments(result.command.powerShellScript);
    return result;
}

} // namespace Pinloom
