#include "pinloom/core/ResourceNormalization.h"

#include "pinloom/core/AnchorLocator.h"

#include <algorithm>

namespace Pinloom {

Resource normalizedResource(Resource resource)
{
    for (int i = 0; i < resource.anchors.size(); ++i) {
        Anchor &anchor = resource.anchors[i];
        anchor.locatorType = anchorLocatorType(anchor);
        if (anchor.id.trimmed().isEmpty() && !resource.id.trimmed().isEmpty()) {
            anchor.id = QStringLiteral("%1#anchor-%2").arg(resource.id, QString::number(i));
        }
        if (anchor.name.trimmed().isEmpty()) {
            anchor.name = resource.title.trimmed();
        }
    }
    return resource;
}

bool resourceKindMatchesFilter(ResourceKind kind, const QList<ResourceKind> &requiredKinds)
{
    return requiredKinds.isEmpty() || requiredKinds.contains(kind);
}

} // namespace Pinloom
