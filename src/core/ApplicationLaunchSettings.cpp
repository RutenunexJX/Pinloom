#include "pinloom/core/ApplicationLaunchSettings.h"

namespace Pinloom {

QString defaultPowerShellExecutablePath()
{
    return QStringLiteral("powershell.exe");
}

QString effectivePowerShellExecutablePath(const ApplicationLaunchSettings &settings)
{
    const QString configured = settings.powerShellExecutablePath.trimmed();
    return configured.isEmpty() ? defaultPowerShellExecutablePath() : configured;
}

QString externalApplicationLabel(ExternalApplicationTarget target)
{
    switch (target) {
    case ExternalApplicationTarget::SumatraPDF:
        return QStringLiteral("SumatraPDF");
    case ExternalApplicationTarget::Excel:
        return QStringLiteral("Microsoft Excel");
    case ExternalApplicationTarget::Word:
        return QStringLiteral("Microsoft Word");
    case ExternalApplicationTarget::PowerPoint:
        return QStringLiteral("Microsoft PowerPoint");
    case ExternalApplicationTarget::Visio:
        return QStringLiteral("Microsoft Visio");
    }
    return {};
}

bool usesPowerShellLauncher(ExternalApplicationTarget target)
{
    return target == ExternalApplicationTarget::Excel
        || target == ExternalApplicationTarget::Word
        || target == ExternalApplicationTarget::PowerPoint
        || target == ExternalApplicationTarget::Visio;
}

} // namespace Pinloom
