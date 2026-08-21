#include "pinloom/widgets/PinloomEntry.h"

#include "pinloom/core/InboxFileCapture.h"

#include <QFileInfo>
#include <algorithm>

namespace Pinloom {

QString pinloomEntryTypeLabel(PinloomEntryType type)
{
    switch (type) {
    case PinloomEntryType::Anchor:
        return QStringLiteral("Anchor");
    case PinloomEntryType::SavedClip:
        return QStringLiteral("Clip");
    case PinloomEntryType::Inbox:
        return QStringLiteral("Inbox");
    case PinloomEntryType::FileResource:
        return QStringLiteral("File");
    }
    return QStringLiteral("File");
}

int pinloomEntryMatchPriority(const PinloomEntry &entry)
{
    const QString field = entry.matchedField;
    if (field == QLatin1String("anchor")
        || field == QLatin1String("anchor_name")
        || field == QLatin1String("title")
        || field == QLatin1String("name")) {
        return 0;
    }
    if (field == QLatin1String("filename")) return 5;
    if (field == QLatin1String("anchor_alias") || field == QLatin1String("alias")) return 20;
    if (field == QLatin1String("anchor_tag") || field == QLatin1String("tag")) return 30;
    if (field == QLatin1String("anchor_metadata")) return 70;
    if (field == QLatin1String("content")
        || field == QLatin1String("preview")
        || field == QLatin1String("text")
        || field == QLatin1String("path")) {
        return 80;
    }
    return 90;
}

bool pinloomEntryLessThan(const PinloomEntry &left, const PinloomEntry &right)
{
    const int leftPriority = pinloomEntryMatchPriority(left);
    const int rightPriority = pinloomEntryMatchPriority(right);
    if (leftPriority != rightPriority) return leftPriority < rightPriority;
    if (left.pinned != right.pinned) return left.pinned;
    if (left.usedAt.isValid() != right.usedAt.isValid()) return left.usedAt.isValid();
    if (left.usedAt.isValid() && left.usedAt != right.usedAt) return left.usedAt > right.usedAt;
    if (left.frequency != right.frequency) return left.frequency > right.frequency;
    if (left.score != right.score) return left.score < right.score;
    if (left.name != right.name) return left.name < right.name;
    return left.id < right.id;
}

QList<PinloomEntry> sortedPinloomEntries(QList<PinloomEntry> entries)
{
    std::sort(entries.begin(), entries.end(), pinloomEntryLessThan);
    return entries;
}

PinloomEntry entryFromOpenTarget(const PinloomOpenTarget &target)
{
    PinloomEntry entry;
    entry.resourceId = target.resourceId;
    entry.clipId = target.clipId;
    entry.resourceKind = target.resourceKind;
    entry.location = target.location;
    entry.deleted = target.deleted;
    entry.matchedField = target.matchedField;
    entry.matchSummary = target.matchSummary;
    entry.resultRow = target.resultRow;
    entry.score = target.score;
    entry.anchor = target.anchor;

    if (!target.clipId.trimmed().isEmpty()) {
        entry.type = PinloomEntryType::SavedClip;
        entry.id = QStringLiteral("clip:%1").arg(target.clipId);
        entry.name = target.title.trimmed().isEmpty() ? target.location : target.title;
        entry.targetSummary = target.location;
    } else if (target.anchor.has_value()) {
        entry.type = PinloomEntryType::Anchor;
        entry.id = target.anchor->id.trimmed().isEmpty()
            ? QStringLiteral("anchor:%1").arg(target.resourceId)
            : QStringLiteral("anchor:%1").arg(target.anchor->id);
        entry.name = target.anchor->name.trimmed().isEmpty() ? target.title : target.anchor->name;
        entry.aliases = target.anchor->aliases;
        entry.tags = target.anchor->tags;
        entry.pinned = target.anchor->pinned;
        entry.deleted = target.anchor->deleted || target.deleted;
        entry.usedAt = target.anchor->usedAt;
        entry.targetSummary = target.anchor->targetFile.trimmed().isEmpty()
            ? target.location
            : target.anchor->targetFile;
    } else {
        entry.type = isInboxResourceId(target.resourceId)
            ? PinloomEntryType::Inbox
            : PinloomEntryType::FileResource;
        entry.id = target.resourceId.trimmed().isEmpty()
            ? QStringLiteral("file:%1").arg(target.location)
            : QStringLiteral("resource:%1").arg(target.resourceId);
        entry.name = target.title.trimmed().isEmpty()
            ? QFileInfo(target.location).fileName()
            : target.title;
        entry.targetSummary = target.location;
    }

    if (entry.name.trimmed().isEmpty()) entry.name = entry.id;
    entry.metadata.insert(QStringLiteral("resourceId"), entry.resourceId);
    entry.metadata.insert(QStringLiteral("clipId"), entry.clipId);
    entry.metadata.insert(QStringLiteral("location"), entry.location);
    entry.metadata.insert(QStringLiteral("matchedField"), entry.matchedField);
    entry.metadata.insert(QStringLiteral("deleted"), entry.deleted);
    return entry;
}

PinloomOpenTarget openTargetFromEntry(const PinloomEntry &entry)
{
    PinloomOpenTarget target;
    target.resourceId = entry.resourceId;
    target.clipId = entry.clipId;
    target.resourceKind = entry.resourceKind;
    target.title = entry.name;
    target.location = entry.location;
    target.deleted = entry.deleted;
    target.matchedField = entry.matchedField;
    target.matchSummary = entry.matchSummary;
    target.resultRow = entry.resultRow;
    target.score = entry.score;
    target.anchor = entry.anchor;
    if (target.anchor.has_value() && target.anchor->deleted) target.deleted = true;
    return target;
}

QList<PinloomEntry> entriesFromOpenTargets(const QList<PinloomOpenTarget> &targets)
{
    QList<PinloomEntry> entries;
    entries.reserve(targets.size());
    for (const PinloomOpenTarget &target : targets) entries.append(entryFromOpenTarget(target));
    return entries;
}

} // namespace Pinloom
