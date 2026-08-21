#pragma once

#include "pinloom/core/Anchor.h"

#include <QStringList>

namespace Pinloom {

enum class AnchorTargetSource {
    None,
    AnchorFile,
    AnchorUri,
    Locator,
    Resource
};

struct ResolvedAnchorTarget {
    QString value;
    AnchorTargetSource source = AnchorTargetSource::None;
    bool conflictingExplicitTargets = false;

    bool hasValue() const;
    bool isOverride() const;
};

QString localPathFromAnchorTarget(const QString &target);
ResolvedAnchorTarget resolveAnchorTarget(const Anchor &anchor,
                                         const QStringList &locatorCandidates,
                                         const QString &resourceLocation);

} // namespace Pinloom
