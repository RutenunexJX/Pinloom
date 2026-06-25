#pragma once

#include "pinloom/core/Anchor.h"

#include <QDateTime>
#include <QString>

namespace Pinloom {

struct ResourceUsage {
    QString resourceId;
    int openCount = 0;
    QDateTime lastOpenedAt;
    bool pinned = false;
};

struct AnchorUsage {
    QString resourceId;
    Anchor anchor;
    int openCount = 0;
    QDateTime lastOpenedAt;
};

} // namespace Pinloom
