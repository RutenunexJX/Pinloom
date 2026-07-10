#pragma once

#include "pinloom/core/Anchor.h"

#include <QRectF>
#include <QString>
#include <QStringList>

namespace Pinloom {

struct ApplicationLaunchSettings;

struct SumatraPdfCommand {
    QString executablePath;
    QString filePath;
    QStringList arguments;
    int page = -1;
    double zoom = -1.0;
    QRectF highlightRect;
};

struct SumatraPdfCommandResult {
    SumatraPdfCommand command;
    QString error;

    bool success() const;
};

bool isSumatraPdfLocatorType(const QString &locatorType);
bool isSumatraPdfAnchor(const Anchor &anchor);
QString resolveSumatraPdfExecutablePath();
QString resolveSumatraPdfExecutablePath(const ApplicationLaunchSettings &settings);
SumatraPdfCommandResult buildSumatraPdfCommand(const Anchor &anchor,
                                               const QString &fallbackFilePath,
                                               const ApplicationLaunchSettings &settings);
SumatraPdfCommandResult buildSumatraPdfCommand(const Anchor &anchor,
                                               const QString &fallbackFilePath,
                                               const QString &executablePath);

} // namespace Pinloom
