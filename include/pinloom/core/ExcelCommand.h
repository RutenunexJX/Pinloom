#pragma once

#include "pinloom/core/Anchor.h"

#include <QString>
#include <QStringList>

namespace Pinloom {

struct ApplicationLaunchSettings;

struct ExcelJumpCommand {
    QString executablePath;
    QString workbookPath;
    QString locatorType;
    QString sheetName;
    QString rangeAddress;
    QString namedRange;
    QString powerShellScript;
    QStringList arguments;
};

struct ExcelJumpCommandResult {
    ExcelJumpCommand command;
    QString error;

    bool success() const;
};

bool isExcelLocatorType(const QString &locatorType);
bool isExcelAnchor(const Anchor &anchor);
ExcelJumpCommandResult buildExcelJumpCommand(const Anchor &anchor, const QString &fallbackFilePath);
ExcelJumpCommandResult buildExcelJumpCommand(const Anchor &anchor,
                                             const QString &fallbackFilePath,
                                             const ApplicationLaunchSettings &settings);

} // namespace Pinloom
