#include "pinloom/core/Version.h"

namespace Pinloom {

QString pinloomVersion()
{
    return QStringLiteral(PINLOOM_VERSION);
}

QString pinloomVersionLabel()
{
    return QStringLiteral("v%1").arg(pinloomVersion());
}

} // namespace Pinloom
