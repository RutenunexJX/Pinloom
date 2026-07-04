#pragma once

#include "pinloom/core/Anchor.h"

#include <QString>
#include <QStringList>

namespace Pinloom {

struct ApplicationLaunchSettings;

struct VisioJumpCommand {
    QString executablePath;
    QString documentPath;
    QString locatorType;
    QString pageName;
    QString shapeUniqueId;
    QString powerShellScript;
    QStringList arguments;
};

struct VisioJumpCommandResult {
    VisioJumpCommand command;
    QString error;

    bool success() const;
};

bool isVisioLocatorType(const QString &locatorType);
bool isVisioAnchor(const Anchor &anchor);
VisioJumpCommandResult buildVisioJumpCommand(const Anchor &anchor, const QString &fallbackFilePath);
VisioJumpCommandResult buildVisioJumpCommand(const Anchor &anchor,
                                             const QString &fallbackFilePath,
                                             const ApplicationLaunchSettings &settings);

} // namespace Pinloom
