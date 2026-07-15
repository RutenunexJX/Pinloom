#pragma once

#include "pinloom/core/Resource.h"

#include <QString>
#include <QStringList>
#include <utility>
#include <optional>

namespace Pinloom {

struct SearchQuery {
    SearchQuery() = default;
    explicit SearchQuery(QString queryText)
        : text(std::move(queryText))
    {
    }

    QString text;
    QStringList requiredTags;
    QStringList requiredLocationPrefixes;
    QList<ResourceKind> requiredKinds;
    QStringList contextTags;
    QStringList contextLocationPrefixes;
    int limit = 50;
    bool includeDeleted = false;
};

struct SearchResult {
    Resource resource;
    double score = 0.0;
    QString matchedField;
    std::optional<Anchor> matchedAnchor;
};

} // namespace Pinloom
