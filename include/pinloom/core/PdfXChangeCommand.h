#pragma once

#include "pinloom/core/Anchor.h"

#include <QString>
#include <QStringList>

namespace Pinloom {

struct PdfXChangeCommand {
    QString executablePath;
    QString action;
    QString filePath;
    QStringList arguments;
};

struct PdfXChangeCommandResult {
    PdfXChangeCommand command;
    QString error;

    bool success() const;
};

bool isPdfXChangeLocatorType(const QString &locatorType);
bool isPdfXChangeAnchor(const Anchor &anchor);
QString resolvePdfXChangeExecutablePath();
PdfXChangeCommandResult buildPdfXChangeCommand(const Anchor &anchor,
                                               const QString &fallbackFilePath,
                                               const QString &executablePath);

} // namespace Pinloom
