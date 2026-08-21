#include "pinloom/core/InMemoryLibraryRepository.h"

#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/ResourceNormalization.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <utility>

namespace Pinloom {

namespace {

bool containsNeedle(const QString &text, const QString &needle)
{
    return text.contains(needle, Qt::CaseInsensitive);
}

bool equalsNeedle(const QString &text, const QString &needle)
{
    return text.trimmed().compare(needle.trimmed(), Qt::CaseInsensitive) == 0;
}

bool anyEqualsNeedle(const QStringList &values, const QString &needle)
{
    return std::any_of(values.cbegin(), values.cend(), [&](const QString &value) {
        return equalsNeedle(value, needle);
    });
}

double exactMatchScoreAdjustment(const QStringList &values, const QString &needle)
{
    return anyEqualsNeedle(values, needle) ? -0.75 : 0.0;
}

double exactMatchScoreAdjustment(const QString &value, const QString &needle)
{
    return exactMatchScoreAdjustment(QStringList{value}, needle);
}

SearchResult resourceResult(const Resource &resource, double score, const QString &field)
{
    return SearchResult{resource, score, field, std::nullopt};
}

QStringList nonEmptyValues(const QStringList &values)
{
    QStringList filtered;
    for (const QString &value : values) {
        if (!value.trimmed().isEmpty()) {
            filtered.append(value);
        }
    }
    return filtered;
}

bool anyContainsNeedle(const QStringList &values, const QString &needle)
{
    return std::any_of(values.cbegin(), values.cend(), [&](const QString &value) {
        return containsNeedle(value, needle);
    });
}

QString tagNeedleFromQuery(const QString &needle)
{
    const QString trimmed = needle.trimmed();
    if (trimmed.startsWith(QLatin1Char('#')) && trimmed.size() > 1) {
        return trimmed.mid(1);
    }
    return trimmed;
}

QString effectiveAnchorName(const Anchor &anchor)
{
    return anchor.name;
}

QStringList anchorMetadataValues(const Anchor &anchor)
{
    QStringList values;
    values = nonEmptyValues({anchor.targetApp,
                             anchor.targetFile,
                             anchor.targetUri,
                             anchor.locatorType,
                             anchor.locatorJson});
    return values;
}

struct AnchorMatch {
    bool matched = false;
    double score = 100.0;
    QString field;
};

AnchorMatch classifyAnchorMatch(const Anchor &anchor, const QString &needle)
{
    const QString name = effectiveAnchorName(anchor);
    if (!name.trimmed().isEmpty() && containsNeedle(name, needle)) {
        return {true,
                0.0 + exactMatchScoreAdjustment(name, needle),
                anchor.name.trimmed().isEmpty() ? QStringLiteral("anchor") : QStringLiteral("anchor_name")};
    }

    if (anyContainsNeedle(anchor.aliases, needle)) {
        return {true, 5.0 + exactMatchScoreAdjustment(anchor.aliases, needle), QStringLiteral("anchor_alias")};
    }

    const QString tagNeedle = tagNeedleFromQuery(needle);
    if (!tagNeedle.isEmpty() && anyContainsNeedle(anchor.tags, tagNeedle)) {
        return {true, 8.0 + exactMatchScoreAdjustment(anchor.tags, tagNeedle), QStringLiteral("anchor_tag")};
    }

    const QStringList metadata = anchorMetadataValues(anchor);
    if (anyContainsNeedle(metadata, needle)) {
        return {true, 40.0 + exactMatchScoreAdjustment(metadata, needle), QStringLiteral("anchor_metadata")};
    }

    return {};
}

SearchResult anchorResult(const Resource &resource, const Anchor &anchor, const AnchorMatch &match)
{
    return SearchResult{resource, match.score, match.field, anchor};
}

QStringList anchorUsageKeys(const QString &resourceId, const Anchor &anchor)
{
    return {QStringLiteral("%1|%2").arg(resourceId, anchorIdentityKey(anchor))};
}

double usageScoreAdjustment(const ResourceUsage &usage)
{
    double adjustment = 0.0;
    if (usage.pinned) {
        adjustment -= 0.6;
    }
    adjustment -= std::min(usage.openCount, 10) * 0.03;
    if (usage.lastOpenedAt.isValid()) {
        const qint64 secondsAgo = usage.lastOpenedAt.toUTC().secsTo(QDateTime::currentDateTimeUtc());
        if (secondsAgo >= 0 && secondsAgo <= 7 * 24 * 60 * 60) {
            adjustment -= 0.2;
        } else if (secondsAgo > 0 && secondsAgo <= 30 * 24 * 60 * 60) {
            adjustment -= 0.1;
        }
    }
    return adjustment;
}

double anchorUsageScoreAdjustment(const AnchorUsage &usage)
{
    double adjustment = 0.0;
    adjustment -= std::min(usage.openCount, 10) * 0.04;
    if (usage.lastOpenedAt.isValid()) {
        const qint64 secondsAgo = usage.lastOpenedAt.toUTC().secsTo(QDateTime::currentDateTimeUtc());
        if (secondsAgo >= 0 && secondsAgo <= 7 * 24 * 60 * 60) {
            adjustment -= 0.25;
        } else if (secondsAgo > 0 && secondsAgo <= 30 * 24 * 60 * 60) {
            adjustment -= 0.12;
        }
    }
    return adjustment;
}

double anchorScoreAdjustment(const Anchor &anchor)
{
    double adjustment = 0.0;
    if (anchor.pinned) {
        adjustment -= 0.6;
    }
    if (anchor.usedAt.isValid()) {
        const qint64 secondsAgo = anchor.usedAt.toUTC().secsTo(QDateTime::currentDateTimeUtc());
        if (secondsAgo >= 0 && secondsAgo <= 7 * 24 * 60 * 60) {
            adjustment -= 0.25;
        } else if (secondsAgo > 0 && secondsAgo <= 30 * 24 * 60 * 60) {
            adjustment -= 0.12;
        }
    }
    return adjustment;
}

bool hasContextTag(const Resource &resource, const QStringList &contextTags)
{
    for (const QString &tag : contextTags) {
        if (resource.tags.contains(tag, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QString normalizedLocation(const QString &location)
{
    const QString trimmed = location.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QString normalized = QDir::cleanPath(trimmed);
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return normalized.toLower();
}

bool matchesContextLocationPrefix(const QString &location, const QStringList &prefixes)
{
    const QString normalizedResourceLocation = normalizedLocation(location);
    if (normalizedResourceLocation.isEmpty()) {
        return false;
    }

    for (const QString &prefix : prefixes) {
        const QString normalizedPrefix = normalizedLocation(prefix);
        if (normalizedPrefix.isEmpty()) {
            continue;
        }
        if (normalizedResourceLocation == normalizedPrefix
            || normalizedResourceLocation.startsWith(normalizedPrefix + QLatin1Char('/'))) {
            return true;
        }
    }
    return false;
}

bool matchesRequiredLocationPrefixes(const QString &location, const QStringList &prefixes)
{
    return prefixes.isEmpty() || matchesContextLocationPrefix(location, prefixes);
}

bool matchesRequiredKinds(ResourceKind kind, const QList<ResourceKind> &requiredKinds)
{
    return resourceKindMatchesFilter(kind, requiredKinds);
}

double contextScoreAdjustment(const Resource &resource, const SearchQuery &query)
{
    double adjustment = 0.0;
    if (hasContextTag(resource, query.contextTags)) {
        adjustment -= 0.35;
    }
    if (matchesContextLocationPrefix(resource.location, query.contextLocationPrefixes)) {
        adjustment -= 0.35;
    }
    return adjustment;
}

void applyRankingSignals(SearchResult &result,
                         const SearchQuery &query,
                         const QHash<QString, ResourceUsage> &usage,
                         const QHash<QString, AnchorUsage> &anchorUsage)
{
    result.score += contextScoreAdjustment(result.resource, query);

    const auto usageIt = usage.constFind(result.resource.id);
    if (usageIt != usage.constEnd()) {
        result.score += usageScoreAdjustment(usageIt.value());
    }

    if (result.matchedAnchor.has_value()) {
        result.score += anchorScoreAdjustment(result.matchedAnchor.value());
        for (const QString &key : anchorUsageKeys(result.resource.id, result.matchedAnchor.value())) {
            const auto anchorUsageIt = anchorUsage.constFind(key);
            if (anchorUsageIt != anchorUsage.constEnd()) {
                result.score += anchorUsageScoreAdjustment(anchorUsageIt.value());
                break;
            }
        }
    }
}

} // namespace

bool InMemoryLibraryRepository::upsertResource(const Resource &resource)
{
    if (resource.id.trimmed().isEmpty()) {
        return false;
    }

    resources_.insert(resource.id, normalizedResource(resource));
    notifyChange(LibraryChangeKind::Content, {resource.id});
    return true;
}

std::optional<Resource> InMemoryLibraryRepository::findResource(const QString &id) const
{
    const auto it = resources_.constFind(id);
    if (it == resources_.constEnd()) {
        return std::nullopt;
    }

    return it.value();
}

QList<SearchResult> InMemoryLibraryRepository::search(const SearchQuery &query) const
{
    QList<SearchResult> results;
    const QString needle = query.text.trimmed();
    const QString tagNeedle = tagNeedleFromQuery(needle);
    const Qt::CaseSensitivity caseMode = Qt::CaseInsensitive;

    for (const Resource &resource : resources_) {
        if (!query.deletedOnly && !query.includeDeleted && resource.deleted) {
            continue;
        }
        const bool includeResourceResult = !query.deletedOnly || resource.deleted;

        bool tagMatched = true;
        for (const QString &requiredTag : query.requiredTags) {
            if (!resource.tags.contains(requiredTag, caseMode)) {
                tagMatched = false;
                break;
            }
        }
        if (!tagMatched) {
            continue;
        }
        if (!matchesRequiredLocationPrefixes(resource.location, query.requiredLocationPrefixes)) {
            continue;
        }
        if (!matchesRequiredKinds(resource.kind, query.requiredKinds)) {
            continue;
        }

        if (includeResourceResult && needle.isEmpty()) {
            SearchResult result = resourceResult(resource, 100.0, QStringLiteral("all"));
            applyRankingSignals(result, query, usage_, anchorUsage_);
            results.append(result);
        } else if (includeResourceResult && resource.title.contains(needle, caseMode)) {
            SearchResult result = resourceResult(resource,
                                                 10.0 + exactMatchScoreAdjustment(resource.title, needle),
                                                 QStringLiteral("title"));
            applyRankingSignals(result, query, usage_, anchorUsage_);
            results.append(result);
        } else if (includeResourceResult && QFileInfo(resource.location).fileName().contains(needle, caseMode)) {
            SearchResult result = resourceResult(resource,
                                                 15.0 + exactMatchScoreAdjustment(QFileInfo(resource.location).fileName(), needle),
                                                 QStringLiteral("filename"));
            applyRankingSignals(result, query, usage_, anchorUsage_);
            results.append(result);
        } else if (includeResourceResult && resource.aliases.join(QLatin1Char('\n')).contains(needle, caseMode)) {
            SearchResult result = resourceResult(resource,
                                                 20.0 + exactMatchScoreAdjustment(resource.aliases, needle),
                                                 QStringLiteral("alias"));
            applyRankingSignals(result, query, usage_, anchorUsage_);
            results.append(result);
        } else if (includeResourceResult && !tagNeedle.isEmpty()
                   && resource.tags.join(QLatin1Char('\n')).contains(tagNeedle, caseMode)) {
            SearchResult result = resourceResult(resource,
                                                 30.0 + exactMatchScoreAdjustment(resource.tags, tagNeedle),
                                                 QStringLiteral("tag"));
            applyRankingSignals(result, query, usage_, anchorUsage_);
            results.append(result);
        } else if (includeResourceResult && resource.content.contains(needle, caseMode)) {
            SearchResult result = resourceResult(resource,
                                                 80.0 + exactMatchScoreAdjustment(resource.content, needle),
                                                 QStringLiteral("content"));
            applyRankingSignals(result, query, usage_, anchorUsage_);
            results.append(result);
        } else if (includeResourceResult && resource.location.contains(needle, caseMode)) {
            SearchResult result = resourceResult(resource,
                                                 90.0 + exactMatchScoreAdjustment(resource.location, needle),
                                                 QStringLiteral("path"));
            applyRankingSignals(result, query, usage_, anchorUsage_);
            results.append(result);
        }

        if (!needle.isEmpty() || query.deletedOnly) {
            for (const Anchor &anchor : resource.anchors) {
                if (query.deletedOnly) {
                    if (resource.deleted || !anchor.deleted) continue;
                } else if (!query.includeDeleted && anchor.deleted) {
                    continue;
                }
                if (needle.isEmpty()) {
                    SearchResult result{resource, 100.0, QStringLiteral("anchor"), anchor};
                    applyRankingSignals(result, query, usage_, anchorUsage_);
                    results.append(result);
                    continue;
                }
                const AnchorMatch match = classifyAnchorMatch(anchor, needle);
                if (match.matched) {
                    SearchResult result = anchorResult(resource, anchor, match);
                    applyRankingSignals(result, query, usage_, anchorUsage_);
                    results.append(result);
                }
            }
        }
    }

    std::sort(results.begin(), results.end(), [](const SearchResult &left, const SearchResult &right) {
        if (left.score == right.score) {
            return left.resource.title < right.resource.title;
        }
        return left.score < right.score;
    });

    if (query.limit > 0 && results.size() > query.limit) {
        results.erase(results.begin() + query.limit, results.end());
    }

    return results;
}

bool InMemoryLibraryRepository::softDeleteResource(const QString &resourceId)
{
    auto it = resources_.find(resourceId);
    if (it == resources_.end()) {
        return false;
    }

    it->deleted = true;
    it->updatedAt = QDateTime::currentDateTimeUtc();
    notifyChange(LibraryChangeKind::Content, {resourceId});
    return true;
}

bool InMemoryLibraryRepository::restoreResource(const QString &resourceId)
{
    auto it = resources_.find(resourceId);
    if (it == resources_.end()) {
        return false;
    }

    it->deleted = false;
    it->updatedAt = QDateTime::currentDateTimeUtc();
    notifyChange(LibraryChangeKind::Content, {resourceId});
    return true;
}

bool InMemoryLibraryRepository::softDeleteAnchor(const QString &resourceId, const Anchor &anchor)
{
    auto it = resources_.find(resourceId);
    if (it == resources_.end()) {
        return false;
    }

    const Anchor normalized = anchor;
    for (Anchor &storedAnchor : it->anchors) {
        if (!sameAnchorIdentity(storedAnchor, normalized)) {
            continue;
        }
        storedAnchor.deleted = true;
        storedAnchor.updatedAt = QDateTime::currentDateTimeUtc();
        it->updatedAt = storedAnchor.updatedAt;
        notifyChange(LibraryChangeKind::Content, {resourceId});
        return true;
    }
    return false;
}

bool InMemoryLibraryRepository::restoreAnchor(const QString &resourceId, const Anchor &anchor)
{
    auto it = resources_.find(resourceId);
    if (it == resources_.end()) {
        return false;
    }

    const Anchor normalized = anchor;
    for (Anchor &storedAnchor : it->anchors) {
        if (!sameAnchorIdentity(storedAnchor, normalized)) {
            continue;
        }
        storedAnchor.deleted = false;
        storedAnchor.updatedAt = QDateTime::currentDateTimeUtc();
        it->updatedAt = storedAnchor.updatedAt;
        notifyChange(LibraryChangeKind::Content, {resourceId});
        return true;
    }
    return false;
}

bool InMemoryLibraryRepository::clearResources()
{
    resources_.clear();
    usage_.clear();
    anchorUsage_.clear();
    notifyChange(LibraryChangeKind::Reset);
    return true;
}

bool InMemoryLibraryRepository::applyBatch(const LibraryBatchMutation &mutation)
{
    QHash<QString, Resource> resources = mutation.clearExistingResources
        ? QHash<QString, Resource>{}
        : resources_;
    QHash<QString, ResourceUsage> usage = mutation.clearExistingResources
        ? QHash<QString, ResourceUsage>{}
        : usage_;
    QHash<QString, AnchorUsage> anchorUsage = mutation.clearExistingResources
        ? QHash<QString, AnchorUsage>{}
        : anchorUsage_;
    QStringList changedIds;

    for (const QString &resourceIdValue : mutation.permanentlyDeleteResourceIds) {
        const QString resourceId = resourceIdValue.trimmed();
        if (resourceId.isEmpty() || !resources.contains(resourceId)) {
            return false;
        }
        resources.remove(resourceId);
        usage.remove(resourceId);
        const QString prefix = resourceId + QLatin1Char('|');
        for (auto it = anchorUsage.begin(); it != anchorUsage.end();) {
            if (it.key().startsWith(prefix)) {
                it = anchorUsage.erase(it);
            } else {
                ++it;
            }
        }
        if (!changedIds.contains(resourceId)) {
            changedIds.append(resourceId);
        }
    }

    for (const Resource &resource : mutation.upserts) {
        if (resource.id.trimmed().isEmpty()) {
            return false;
        }
        resources.insert(resource.id, normalizedResource(resource));
        if (!changedIds.contains(resource.id)) {
            changedIds.append(resource.id);
        }
    }

    for (const ResourcePinUpdate &update : mutation.resourcePinUpdates) {
        if (!resources.contains(update.resourceId)) {
            return false;
        }
        ResourceUsage value = usage.value(update.resourceId);
        value.resourceId = update.resourceId;
        value.pinned = update.pinned;
        usage.insert(update.resourceId, value);
        if (!changedIds.contains(update.resourceId)) {
            changedIds.append(update.resourceId);
        }
    }

    resources_ = std::move(resources);
    usage_ = std::move(usage);
    anchorUsage_ = std::move(anchorUsage);
    const bool changesContent = mutation.clearExistingResources
        || !mutation.permanentlyDeleteResourceIds.isEmpty()
        || !mutation.upserts.isEmpty();
    notifyChange(mutation.clearExistingResources
                     ? LibraryChangeKind::Reset
                     : (changesContent ? LibraryChangeKind::Content : LibraryChangeKind::Usage),
                 changedIds);
    return true;
}

bool InMemoryLibraryRepository::upsertLibraryRoot(const LibraryRoot &root)
{
    LibraryRoot stored = root;
    stored.path = normalizedLibraryRootPath(root.path);
    if (stored.id.trimmed().isEmpty()) {
        stored.id = libraryRootIdForPath(stored.path);
    }
    if (stored.id.isEmpty() || stored.path.isEmpty()) {
        return false;
    }
    if (stored.displayName.trimmed().isEmpty()) {
        stored.displayName = QFileInfo(stored.path).fileName();
    }
    if (!stored.updatedAt.isValid()) {
        stored.updatedAt = QDateTime::currentDateTimeUtc();
    }
    for (auto it = libraryRoots_.begin(); it != libraryRoots_.end();) {
        const bool sameStoredPath = normalizedLibraryRootPath(it->path).compare(
                                        stored.path,
#ifdef Q_OS_WIN
                                        Qt::CaseInsensitive
#else
                                        Qt::CaseSensitive
#endif
                                        ) == 0;
        if (it.key() != stored.id && sameStoredPath) {
            it = libraryRoots_.erase(it);
        } else {
            ++it;
        }
    }
    libraryRoots_.insert(stored.id, stored);
    notifyChange(LibraryChangeKind::Content);
    return true;
}

QList<LibraryRoot> InMemoryLibraryRepository::libraryRoots() const
{
    QList<LibraryRoot> roots = libraryRoots_.values();
    std::sort(roots.begin(), roots.end(), [](const LibraryRoot &left, const LibraryRoot &right) {
        if (left.syncRoot != right.syncRoot) {
            return left.syncRoot;
        }
        const int byName = left.displayName.compare(right.displayName, Qt::CaseInsensitive);
        return byName == 0
            ? left.path.compare(right.path, Qt::CaseInsensitive) < 0
            : byName < 0;
    });
    return roots;
}

std::optional<LibraryRoot> InMemoryLibraryRepository::findLibraryRoot(const QString &id) const
{
    const auto it = libraryRoots_.constFind(id.trimmed());
    return it == libraryRoots_.constEnd() ? std::nullopt : std::optional<LibraryRoot>(it.value());
}

bool InMemoryLibraryRepository::removeLibraryRoot(const QString &id)
{
    const auto it = libraryRoots_.find(id.trimmed());
    if (it == libraryRoots_.end() || it->syncRoot) {
        return false;
    }
    libraryRoots_.erase(it);
    notifyChange(LibraryChangeKind::Content);
    return true;
}

bool InMemoryLibraryRepository::recordResourceOpen(const QString &resourceId)
{
    if (!resources_.contains(resourceId)) {
        return false;
    }

    ResourceUsage usage = usage_.value(resourceId);
    usage.resourceId = resourceId;
    usage.openCount += 1;
    usage.lastOpenedAt = QDateTime::currentDateTimeUtc();
    usage_.insert(resourceId, usage);
    notifyChange(LibraryChangeKind::Usage, {resourceId});
    return true;
}

bool InMemoryLibraryRepository::setResourcePinned(const QString &resourceId, bool pinned)
{
    if (!resources_.contains(resourceId)) {
        return false;
    }

    ResourceUsage usage = usage_.value(resourceId);
    usage.resourceId = resourceId;
    usage.pinned = pinned;
    usage_.insert(resourceId, usage);
    notifyChange(LibraryChangeKind::Usage, {resourceId});
    return true;
}

std::optional<ResourceUsage> InMemoryLibraryRepository::resourceUsage(const QString &resourceId) const
{
    const auto it = usage_.constFind(resourceId);
    if (it == usage_.constEnd()) {
        return std::nullopt;
    }
    return it.value();
}

bool InMemoryLibraryRepository::recordAnchorOpen(const QString &resourceId, const Anchor &anchor)
{
    if (!resources_.contains(resourceId) || !hasAnchorIdentity(anchor)) {
        return false;
    }

    Resource &resource = resources_[resourceId];
    Anchor effective = anchor;
    for (const Anchor &storedAnchor : std::as_const(resource.anchors)) {
        if (sameAnchorIdentity(storedAnchor, anchor)) {
            effective = storedAnchor;
            break;
        }
    }

    const QString key = anchorUsageKeys(resourceId, effective).first();
    AnchorUsage usage = anchorUsage_.value(key);
    usage.resourceId = resourceId;
    usage.openCount += 1;
    usage.lastOpenedAt = QDateTime::currentDateTimeUtc();
    anchorUsage_.insert(key, usage);

    for (Anchor &storedAnchor : resource.anchors) {
        if (sameAnchorIdentity(storedAnchor, effective)) {
            storedAnchor.usedAt = usage.lastOpenedAt;
            break;
        }
    }
    notifyChange(LibraryChangeKind::Usage, {resourceId});
    return true;
}

std::optional<AnchorUsage> InMemoryLibraryRepository::anchorUsage(const QString &resourceId, const Anchor &anchor) const
{
    Anchor effective = anchor;
    const auto resourceIt = resources_.constFind(resourceId);
    if (resourceIt != resources_.constEnd()) {
        for (const Anchor &storedAnchor : resourceIt->anchors) {
            if (sameAnchorIdentity(storedAnchor, anchor)) {
                effective = storedAnchor;
                break;
            }
        }
    }

    for (const QString &key : anchorUsageKeys(resourceId, effective)) {
        const auto it = anchorUsage_.constFind(key);
        if (it != anchorUsage_.constEnd()) {
            return it.value();
        }
    }
    return std::nullopt;
}

quint64 InMemoryLibraryRepository::changeRevision() const
{
    return revision_;
}

quint64 InMemoryLibraryRepository::contentRevision() const
{
    return contentRevision_;
}

int InMemoryLibraryRepository::addChangeListener(LibraryChangeListener listener)
{
    if (!listener) {
        return 0;
    }
    const int listenerId = nextListenerId_++;
    listeners_.insert(listenerId, std::move(listener));
    return listenerId;
}

void InMemoryLibraryRepository::removeChangeListener(int listenerId)
{
    listeners_.remove(listenerId);
}

void InMemoryLibraryRepository::notifyChange(LibraryChangeKind kind,
                                             const QStringList &resourceIds)
{
    ++revision_;
    if (kind != LibraryChangeKind::Usage) {
        ++contentRevision_;
    }
    LibraryChange change{kind, resourceIds, revision_};
    const QList<LibraryChangeListener> listeners = listeners_.values();
    for (const LibraryChangeListener &listener : listeners) {
        if (listener) {
            listener(change);
        }
    }
}


} // namespace Pinloom
