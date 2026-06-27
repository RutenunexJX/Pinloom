#include "pinloom/core/LegacyCompatibility.h"

#include <algorithm>

namespace Pinloom {

ResourceKind normalizedResourceKind(ResourceKind kind)
{
    if (kind == ResourceKind::Markdown) {
        return ResourceKind::File;
    }
    return kind;
}

AnchorType normalizedAnchorType(AnchorType type)
{
    switch (type) {
    case AnchorType::MarkdownHeading:
        return AnchorType::TextHeading;
    case AnchorType::MarkdownBlock:
        return AnchorType::TextBlock;
    default:
        return type;
    }
}

Anchor normalizedAnchor(Anchor anchor)
{
    anchor.type = normalizedAnchorType(anchor.type);
    return anchor;
}

Resource normalizedResource(Resource resource)
{
    resource.kind = normalizedResourceKind(resource.kind);
    for (Anchor &anchor : resource.anchors) {
        anchor = normalizedAnchor(anchor);
    }
    return resource;
}

bool resourceKindMatchesFilter(ResourceKind kind, const QList<ResourceKind> &requiredKinds)
{
    if (requiredKinds.isEmpty()) {
        return true;
    }

    const ResourceKind normalizedKind = normalizedResourceKind(kind);
    return std::any_of(requiredKinds.cbegin(), requiredKinds.cend(), [&](ResourceKind requiredKind) {
        return normalizedResourceKind(requiredKind) == normalizedKind;
    });
}

} // namespace Pinloom
