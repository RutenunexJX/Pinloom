#pragma once

#include "pinloom/core/Anchor.h"

#include <QString>
#include <QStringList>

namespace Pinloom {

struct PowerPointJumpCommand {
    QString executablePath;
    QString presentationPath;
    QString locatorType;
    int slideIndex = -1;
    int shapeId = -1;
    QString shapeName;
    QString powerShellScript;
    QStringList arguments;
};

struct PowerPointJumpCommandResult {
    PowerPointJumpCommand command;
    QString error;

    bool success() const;
};

bool isPowerPointLocatorType(const QString &locatorType);
bool isPowerPointAnchor(const Anchor &anchor);
PowerPointJumpCommandResult buildPowerPointJumpCommand(const Anchor &anchor, const QString &fallbackFilePath);

} // namespace Pinloom
