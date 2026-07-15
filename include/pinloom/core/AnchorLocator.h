#pragma once

#include "pinloom/core/Anchor.h"

#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <optional>

namespace Pinloom {

QJsonObject anchorLocatorObject(const Anchor &anchor, QString *error = nullptr);
QString anchorLocatorType(const Anchor &anchor);
int anchorLocatorLine(const Anchor &anchor);
int anchorLocatorPage(const Anchor &anchor);
std::optional<QRectF> anchorLocatorRegion(const Anchor &anchor);
QString anchorLocatorFragment(const Anchor &anchor);
QString anchorIdentityKey(const Anchor &anchor);
bool sameAnchorIdentity(const Anchor &left, const Anchor &right);
bool hasAnchorIdentity(const Anchor &anchor);

} // namespace Pinloom
