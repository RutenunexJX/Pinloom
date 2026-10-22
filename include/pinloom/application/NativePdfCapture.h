#pragma once

#include "pinloom/core/AnchorCaptureDraft.h"
#include <QJsonObject>

namespace Pinloom {
struct NativePdfCaptureDraftResult {
    AnchorCaptureDraft draft;
    QString error;
    QString errorCode;
};
NativePdfCaptureDraftResult nativePdfCaptureDraft(const QJsonObject &parameters);
}
