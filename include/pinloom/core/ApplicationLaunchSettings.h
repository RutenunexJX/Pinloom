#pragma once

#include <QString>

namespace Pinloom {

struct ApplicationLaunchSettings {
    QString sumatraPdfExecutablePath;
    QString powerShellExecutablePath;
};

enum class ExternalApplicationTarget {
    SumatraPDF,
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
