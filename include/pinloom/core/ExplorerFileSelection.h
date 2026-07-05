#pragma once

#include "pinloom/core/PdfXChangeForegroundCapture.h"

#include <QString>
#include <QStringList>

namespace Pinloom {

struct ExplorerFileSelectionResult {
    QStringList filePaths;
    QString status;
    bool recognizedExplorer = false;

    bool success() const;
};

bool isExplorerForegroundWindow(const ForegroundAppWindowContext &context);
ExplorerFileSelectionResult captureExplorerFileSelection(const ForegroundAppWindowContext &context);
ExplorerFileSelectionResult captureCurrentExplorerFileSelection();

} // namespace Pinloom
