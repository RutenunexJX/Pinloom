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
    QStringList contextResourceIds;
    QStringList contextRelationLabels;
    int limit = 50;
};

struct SearchResult {
    Resource resource;
    double score = 0.0;
    QString matchedField;
    std::optional<Anchor> matchedAnchor;
    QString matchedContextResourceId;
    QString matchedContextRelationLabel;
    QString matchedContextRelationNote;
};

} // namespace Pinloom
