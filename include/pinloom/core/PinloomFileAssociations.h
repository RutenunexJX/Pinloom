#pragma once

#include <QString>

class QSettings;

namespace Pinloom {

// registry is rooted at HKCU in production, or a private test root.
QString registerPinloomFileAssociations(QSettings &registry, const QString &executable);
QString unregisterPinloomFileAssociations(QSettings &registry, const QString &executable);
void notifyPinloomFileAssociationsChanged();

} // namespace Pinloom
