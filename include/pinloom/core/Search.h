#pragma once

#include "pinloom/core/Resource.h"

#include <QString>
#include <QStringList>
#include <optional>

namespace Pinloom {

struct SearchQuery {
    QString text;
    QStringList requiredTags;
    QStringList requiredLocationPrefixes;
    QList<ResourceKind> requiredKinds;
    QStringList contextTags;
    QStringList contextLocationPrefixes;
    int limit = 50;
};

struct SearchResult {
    Resource resource;
    double score = 0.0;
    QString matchedField;
    std::optional<Anchor> matchedAnchor;
};

} // namespace Pinloom
