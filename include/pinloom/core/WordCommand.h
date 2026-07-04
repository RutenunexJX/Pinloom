#pragma once

#include "pinloom/core/Anchor.h"

#include <QString>
#include <QStringList>

namespace Pinloom {

struct ApplicationLaunchSettings;

struct WordJumpCommand {
    QString executablePath;
    QString documentPath;
    QString locatorType;
    QString bookmarkName;
    QString powerShellScript;
    QStringList arguments;
};

struct WordJumpCommandResult {
    WordJumpCommand command;
    QString error;

    bool success() const;
};

bool isWordLocatorType(const QString &locatorType);
bool isWordAnchor(const Anchor &anchor);
WordJumpCommandResult buildWordJumpCommand(const Anchor &anchor, const QString &fallbackFilePath);
WordJumpCommandResult buildWordJumpCommand(const Anchor &anchor,
                                           const QString &fallbackFilePath,
                                           const ApplicationLaunchSettings &settings);

} // namespace Pinloom
