#pragma once

#include "pinloom/core/Resource.h"

#include <QList>

namespace Pinloom {

Resource normalizedResource(Resource resource);
bool resourceKindMatchesFilter(ResourceKind kind, const QList<ResourceKind> &requiredKinds);

} // namespace Pinloom
