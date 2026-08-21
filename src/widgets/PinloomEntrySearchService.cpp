#include "pinloom/widgets/PinloomEntrySearchService.h"

#include "pinloom/core/AnchorLocator.h"

#include <algorithm>

namespace Pinloom {

namespace {

QString anchorName(const Anchor &anchor, const Resource &resource)
{
    if (!anchor.name.trimmed().isEmpty()) return anchor.name.trimmed();
    if (!resource.title.trimmed().isEmpty()) return resource.title.trimmed();
    return resource.location;
}

QString resultSummary(const SearchResult &result, const SearchQuery &query)
{
    QStringList lines;
    if (!result.matchedField.trimmed().isEmpty()) {
        lines.append(QStringLiteral("Match: %1").arg(result.matchedField));
    }
    if (result.matchedAnchor.has_value()) {
        const QString type = anchorLocatorType(result.matchedAnchor.value());
        if (!type.isEmpty()) lines.append(QStringLiteral("Locator: %1").arg(type));
    }
    for (const QString &contextTag : query.contextTags) {
        if (result.resource.tags.contains(contextTag, Qt::CaseInsensitive)) {
            lines.append(QStringLiteral("Context tag: %1").arg(contextTag));
            break;
        }
    }
    for (const QString &prefix : query.contextLocationPrefixes) {
        if (!prefix.isEmpty()
            && result.resource.location.startsWith(prefix, Qt::CaseInsensitive)) {
            lines.append(QStringLiteral("Context location: %1").arg(prefix));
            break;
        }
    }
    return lines.join(QLatin1Char('\n'));
}

int clipPriority(const ClipSearchResult &result)
{
    if (result.matchedField == QLatin1String("name")) return 0;
    if (result.matchedField == QLatin1String("alias")) return 20;
    if (result.matchedField == QLatin1String("tag")) return 30;
    if (result.matchedField == QLatin1String("preview")
        || result.matchedField == QLatin1String("text")) {
        return 80;
    }
    return 90;
}

QString clipSummary(const ClipSearchResult &result)
{
    if (result.matchedField == QLatin1String("empty")) {
        return result.state == ClipState::Deleted
            ? QStringLiteral("Deleted Clip")
            : QStringLiteral("Saved Clip");
    }
    if (!result.matchedField.trimmed().isEmpty()) {
        return result.matchedValue.trimmed().isEmpty()
            ? QStringLiteral("match: %1").arg(result.matchedField)
            : QStringLiteral("match: %1=%2").arg(result.matchedField,
                                                   result.matchedValue.simplified().left(48));
    }
    return QStringLiteral("Saved Clip");
}

} // namespace

PinloomEntrySearchService::PinloomEntrySearchService(ILibraryRepository &repository,
                                                     ClipSearchHandler clipSearchHandler)
    : repository_(repository)
    , clipSearchHandler_(std::move(clipSearchHandler))
{
}

QList<PinloomEntry> PinloomEntrySearchService::search(
    const QString &text,
    const PinloomEntrySearchOptions &options) const
{
    QList<PinloomEntry> entries;
    SearchQuery query;
    query.text = text;
    query.requiredTags = options.requiredTags;
    query.requiredLocationPrefixes = options.requiredLocationPrefixes;
    query.requiredKinds = options.requiredKinds;
    query.contextTags = options.contextTags;
    query.contextLocationPrefixes = options.contextLocationPrefixes;
    query.deletedOnly = options.deletedOnly;
    query.includeDeleted = options.includeDeleted || options.deletedOnly;
    query.limit = std::max(0, options.limit);

    for (const SearchResult &result : repository_.search(query)) {
        PinloomOpenTarget target;
        target.resourceId = result.resource.id;
        target.resourceKind = result.resource.kind;
        target.title = result.matchedAnchor.has_value()
            ? anchorName(result.matchedAnchor.value(), result.resource)
            : result.resource.title;
        target.location = result.resource.location;
        target.deleted = result.matchedAnchor.has_value()
            ? (result.resource.deleted || result.matchedAnchor->deleted)
            : result.resource.deleted;
        target.matchedField = result.matchedField;
        target.matchSummary = resultSummary(result, query);
        target.score = result.score;
        target.anchor = result.matchedAnchor;

        PinloomEntry entry = entryFromOpenTarget(target);
        if (!result.matchedAnchor.has_value()) {
            entry.aliases = result.resource.aliases;
            entry.tags = result.resource.tags;
            const std::optional<ResourceUsage> usage = repository_.resourceUsage(result.resource.id);
            if (usage.has_value()) {
                entry.pinned = usage->pinned;
                entry.usedAt = usage->lastOpenedAt;
                entry.frequency = usage->openCount;
            }
        }
        entries.append(entry);
    }

    if (clipSearchHandler_) {
        ClipSearchOptions clipOptions;
        clipOptions.includeSaved = !options.deletedOnly;
        clipOptions.includeTemporary = false;
        clipOptions.includeDeleted = options.includeDeleted || options.deletedOnly;
        clipOptions.emptyQueryReturnsPinnedAndRecent = true;
        clipOptions.limit = query.limit;
        for (const ClipSearchResult &result : clipSearchHandler_(text, clipOptions)) {
            PinloomEntry entry;
            entry.id = QStringLiteral("clip:%1").arg(result.clipId);
            entry.type = PinloomEntryType::SavedClip;
            entry.name = result.displayName.trimmed().isEmpty() ? result.preview : result.displayName;
            entry.aliases = result.aliases;
            entry.tags = result.tags;
            entry.pinned = result.pinned;
            entry.deleted = result.state == ClipState::Deleted;
            entry.usedAt = result.usedAt;
            entry.targetSummary = result.preview;
            entry.clipId = result.clipId;
            entry.location = result.preview;
            entry.matchedField = result.matchedField;
            entry.matchSummary = clipSummary(result);
            entry.score = static_cast<double>(clipPriority(result)) - (result.score / 10000.0);
            entry.metadata.insert(QStringLiteral("clipId"), entry.clipId);
            entry.metadata.insert(QStringLiteral("deleted"), entry.deleted);
            entries.append(entry);
        }
    }

    entries = sortedPinloomEntries(std::move(entries));
    if (query.limit > 0 && entries.size() > query.limit) entries.resize(query.limit);
    for (int row = 0; row < entries.size(); ++row) entries[row].resultRow = row;
    return entries;
}

} // namespace Pinloom
