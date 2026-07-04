#pragma once

#include <QString>

namespace Pinloom {

struct ApplicationLaunchSettings {
    QString pdfXChangeExecutablePath;
    QString powerShellExecutablePath;
};

enum class ExternalApplicationTarget {
    PdfXChange,
    Excel,
    Word,
    PowerPoint,
    Visio,
};

QString defaultPowerShellExecutablePath();
QString effectivePowerShellExecutablePath(const ApplicationLaunchSettings &settings);
QString externalApplicationLabel(ExternalApplicationTarget target);
bool usesPowerShellLauncher(ExternalApplicationTarget target);

} // namespace Pinloom
