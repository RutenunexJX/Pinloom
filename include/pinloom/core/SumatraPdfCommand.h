#pragma once

#include "pinloom/core/Anchor.h"

#include <QString>
#include <QStringList>

namespace Pinloom {

struct ApplicationLaunchSettings;

struct SumatraPdfCommand {
    QString executablePath;
    QString filePath;
    QStringList arguments;
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
