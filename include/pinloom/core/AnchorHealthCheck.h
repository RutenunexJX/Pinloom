#pragma once

#include "pinloom/core/Anchor.h"
#include "pinloom/core/ApplicationLaunchSettings.h"

#include <QString>

namespace Pinloom {

enum class AnchorHealthStatus {
    Ok,
    MissingTarget,
    MissingLauncher,
    UnsupportedTarget,
    UnsupportedLocator,
    InvalidLocator,
};

struct AnchorHealthCheckResult {
    AnchorHealthStatus status = AnchorHealthStatus::UnsupportedLocator;
    QString message;
    QString reason;
    QString path;
    QString launcherPath;
    QString app;
    QString locatorType;

    bool healthy() const;
};

AnchorHealthCheckResult checkAnchorHealth(
    const Anchor &anchor,
    const QString &fallbackFilePath = QString(),
    const ApplicationLaunchSettings &settings = ApplicationLaunchSettings{});

} // namespace Pinloom
