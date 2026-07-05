#pragma once

#include "pinloom/core/Resource.h"

#include <QList>

namespace Pinloom {

ResourceKind normalizedResourceKind(ResourceKind kind);
AnchorType normalizedAnchorType(AnchorType type);
Anchor normalizedAnchor(Anchor anchor);
Resource normalizedResource(Resource resource);
bool isDeprecatedPdfManualLineAnchor(const Resource &resource, const Anchor &anchor);
bool resourceKindMatchesFilter(ResourceKind kind, const QList<ResourceKind> &requiredKinds);

} // namespace Pinloom
