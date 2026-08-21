#pragma once

#include "pinloom/core/Anchor.h"
#include "pinloom/core/Resource.h"

#include <QDateTime>
#include <QList>
#include <QStringList>
#include <QVariantMap>
#include <optional>

namespace Pinloom {

enum class PinloomEntryType {
    Anchor,
    SavedClip,
    Inbox,
    FileResource
};

struct PinloomEntry {
    QString id;
    PinloomEntryType type = PinloomEntryType::FileResource;
    QString name;
    QStringList aliases;
    QStringList tags;
    bool pinned = false;
    bool deleted = false;
    QDateTime usedAt;
    int frequency = 0;
    QString targetSummary;
    QVariantMap metadata;
    QString resourceId;
    QString clipId;
    ResourceKind resourceKind = ResourceKind::Unknown;
    QString location;
    QString matchedField;
    QString matchSummary;
    int resultRow = -1;
    double score = 0.0;
    std::optional<Anchor> anchor;
};

struct PinloomOpenTarget {
    QString resourceId;
    QString clipId;
    ResourceKind resourceKind = ResourceKind::Unknown;
    QString title;
    QString location;
    bool deleted = false;
    QString matchedField;
    QString matchedContextTag;
    QString matchedContextLocationPrefix;
    QString matchSummary;
    int resultRow = -1;
    double score = 0.0;
    std::optional<Anchor> anchor;
};

struct PinloomClipSaveRequest {
    QString clipId;
    QString name;
    QStringList aliases;
    QStringList tags;
    bool pinned = false;
};

QString pinloomEntryTypeLabel(PinloomEntryType type);
int pinloomEntryMatchPriority(const PinloomEntry &entry);
bool pinloomEntryLessThan(const PinloomEntry &left, const PinloomEntry &right);
QList<PinloomEntry> sortedPinloomEntries(QList<PinloomEntry> entries);
PinloomEntry entryFromOpenTarget(const PinloomOpenTarget &target);
PinloomOpenTarget openTargetFromEntry(const PinloomEntry &entry);
QList<PinloomEntry> entriesFromOpenTargets(const QList<PinloomOpenTarget> &targets);

} // namespace Pinloom
