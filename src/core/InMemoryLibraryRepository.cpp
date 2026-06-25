#include "pinloom/core/InMemoryLibraryRepository.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

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

SearchResult anchorResult(const Resource &resource, const Anchor &anchor, const QString &needle)
{
    return SearchResult{resource,
                        exactMatchScoreAdjustment(anchor.target, needle),
                        QStringLiteral("anchor"),
                        anchor};
}

QString anchorTypeKey(AnchorType type)
{
    return QString::number(static_cast<int>(type));
}

QString anchorUsageKey(const QString &resourceId, const Anchor &anchor)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
        .arg(resourceId,
             anchorTypeKey(anchor.type),
             anchor.target,
             QString::number(anchor.line),
             QString::number(anchor.page),
             QString::number(anchor.region.x(), 'f', 2),
             QString::number(anchor.region.y(), 'f', 2),
             QString::number(anchor.region.width(), 'f', 2),
             QString::number(anchor.region.height(), 'f', 2));
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
    return requiredKinds.isEmpty() || requiredKinds.contains(kind);
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

bool resourceMatchesLibraryRoot(const Resource &resource, const LibraryRoot &root)
{
    if (!root.enabled || !root.pinned) {
        return false;
    }

    const QString normalizedResourceLocation = normalizedLocation(resource.location);
    const QString normalizedRootPath = normalizedLocation(root.path);
    return !normalizedResourceLocation.isEmpty()
        && !normalizedRootPath.isEmpty()
        && (normalizedResourceLocation == normalizedRootPath
            || normalizedResourceLocation.startsWith(normalizedRootPath + QLatin1Char('/')));
}

double pinnedRootScoreAdjustment(const Resource &resource, const QHash<QString, LibraryRoot> &roots)
{
    for (const LibraryRoot &root : roots) {
        if (resourceMatchesLibraryRoot(resource, root)) {
            return -0.45;
        }
    }
    return 0.0;
}

void applyRankingSignals(SearchResult &result,
                         const SearchQuery &query,
                         const QHash<QString, ResourceUsage> &usage,
                         const QHash<QString, AnchorUsage> &anchorUsage,
                         const QHash<QString, LibraryRoot> &roots)
{
    result.score += contextScoreAdjustment(result.resource, query);
    result.score += pinnedRootScoreAdjustment(result.resource, roots);

    const auto usageIt = usage.constFind(result.resource.id);
    if (usageIt != usage.constEnd()) {
        result.score += usageScoreAdjustment(usageIt.value());
    }

    if (result.matchedAnchor.has_value()) {
        const auto anchorUsageIt = anchorUsage.constFind(anchorUsageKey(result.resource.id, result.matchedAnchor.value()));
        if (anchorUsageIt != anchorUsage.constEnd()) {
            result.score += anchorUsageScoreAdjustment(anchorUsageIt.value());
        }
    }
}

} // namespace

bool InMemoryLibraryRepository::upsertResource(const Resource &resource)
{
    if (resource.id.trimmed().isEmpty()) {
        return false;
    }

    resources_.insert(resource.id, resource);
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
    const Qt::CaseSensitivity caseMode = Qt::CaseInsensitive;

    for (const Resource &resource : resources_) {
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

        if (needle.isEmpty()) {
            SearchResult result = resourceResult(resource, 100.0, QStringLiteral("all"));
            applyRankingSignals(result, query, usage_, anchorUsage_, libraryRoots_);
            results.append(result);
        } else if (resource.title.contains(needle, caseMode)) {
            SearchResult result = resourceResult(resource,
                                                 10.0 + exactMatchScoreAdjustment(resource.title, needle),
                                                 QStringLiteral("title"));
            applyRankingSignals(result, query, usage_, anchorUsage_, libraryRoots_);
            results.append(result);
        } else if (QFileInfo(resource.location).fileName().contains(needle, caseMode)) {
            SearchResult result = resourceResult(resource,
                                                 15.0 + exactMatchScoreAdjustment(QFileInfo(resource.location).fileName(), needle),
                                                 QStringLiteral("filename"));
            applyRankingSignals(result, query, usage_, anchorUsage_, libraryRoots_);
            results.append(result);
        } else if (resource.aliases.join(QLatin1Char('\n')).contains(needle, caseMode)) {
            SearchResult result = resourceResult(resource,
                                                 20.0 + exactMatchScoreAdjustment(resource.aliases, needle),
                                                 QStringLiteral("alias"));
            applyRankingSignals(result, query, usage_, anchorUsage_, libraryRoots_);
            results.append(result);
        } else if (resource.tags.join(QLatin1Char('\n')).contains(needle, caseMode)) {
            SearchResult result = resourceResult(resource,
                                                 30.0 + exactMatchScoreAdjustment(resource.tags, needle),
                                                 QStringLiteral("tag"));
            applyRankingSignals(result, query, usage_, anchorUsage_, libraryRoots_);
            results.append(result);
        } else if (resource.content.contains(needle, caseMode)) {
            SearchResult result = resourceResult(resource,
                                                 80.0 + exactMatchScoreAdjustment(resource.content, needle),
                                                 QStringLiteral("content"));
            applyRankingSignals(result, query, usage_, anchorUsage_, libraryRoots_);
            results.append(result);
        } else if (resource.location.contains(needle, caseMode)) {
            SearchResult result = resourceResult(resource,
                                                 90.0 + exactMatchScoreAdjustment(resource.location, needle),
                                                 QStringLiteral("path"));
            applyRankingSignals(result, query, usage_, anchorUsage_, libraryRoots_);
            results.append(result);
        }

        if (!needle.isEmpty()) {
            for (const Anchor &anchor : resource.anchors) {
                if (containsNeedle(anchor.target, needle)) {
                    SearchResult result = anchorResult(resource, anchor, needle);
                    applyRankingSignals(result, query, usage_, anchorUsage_, libraryRoots_);
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

bool InMemoryLibraryRepository::clearResources()
{
    resources_.clear();
    relations_.clear();
    usage_.clear();
    anchorUsage_.clear();
    return true;
}

bool InMemoryLibraryRepository::upsertResourceRelation(const ResourceRelation &relation)
{
    if (relation.sourceResourceId.trimmed().isEmpty()
        || relation.targetResourceId.trimmed().isEmpty()
        || relation.label.trimmed().isEmpty()) {
        return false;
    }

    for (ResourceRelation &stored : relations_) {
        if (stored.sourceResourceId == relation.sourceResourceId
            && stored.targetResourceId == relation.targetResourceId
            && stored.label == relation.label) {
            stored.note = relation.note;
            return true;
        }
    }

    relations_.append(relation);
    return true;
}

QList<ResourceRelation> InMemoryLibraryRepository::resourceRelations(const QString &resourceId) const
{
    QList<ResourceRelation> relations;
    for (const ResourceRelation &relation : relations_) {
        if (relation.sourceResourceId == resourceId || relation.targetResourceId == resourceId) {
            relations.append(relation);
        }
    }
    return relations;
}

QList<ResourceRelation> InMemoryLibraryRepository::allResourceRelations() const
{
    return relations_;
}

bool InMemoryLibraryRepository::removeResourceRelation(const QString &sourceResourceId,
                                                       const QString &targetResourceId,
                                                       const QString &label)
{
    for (int i = 0; i < relations_.size(); ++i) {
        const ResourceRelation &relation = relations_.at(i);
        if (relation.sourceResourceId == sourceResourceId
            && relation.targetResourceId == targetResourceId
            && relation.label == label) {
            relations_.removeAt(i);
            return true;
        }
    }
    return false;
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
    if (!resources_.contains(resourceId) || anchor.type == AnchorType::None) {
        return false;
    }

    const QString key = anchorUsageKey(resourceId, anchor);
    AnchorUsage usage = anchorUsage_.value(key);
    usage.resourceId = resourceId;
    usage.anchor = anchor;
    usage.openCount += 1;
    usage.lastOpenedAt = QDateTime::currentDateTimeUtc();
    anchorUsage_.insert(key, usage);
    return true;
}

std::optional<AnchorUsage> InMemoryLibraryRepository::anchorUsage(const QString &resourceId, const Anchor &anchor) const
{
    const auto it = anchorUsage_.constFind(anchorUsageKey(resourceId, anchor));
    if (it == anchorUsage_.constEnd()) {
        return std::nullopt;
    }
    return it.value();
}

bool InMemoryLibraryRepository::upsertLibraryRoot(const LibraryRoot &root)
{
    if (root.id.trimmed().isEmpty() || root.path.trimmed().isEmpty()) {
        return false;
    }

    libraryRoots_.insert(root.id, root);
    return true;
}

QList<LibraryRoot> InMemoryLibraryRepository::libraryRoots() const
{
    QList<LibraryRoot> roots = libraryRoots_.values();
    std::sort(roots.begin(), roots.end(), [](const LibraryRoot &left, const LibraryRoot &right) {
        if (left.pinned != right.pinned) {
            return left.pinned;
        }
        return left.path < right.path;
    });
    return roots;
}

std::optional<LibraryRoot> InMemoryLibraryRepository::findLibraryRoot(const QString &id) const
{
    const auto it = libraryRoots_.constFind(id);
    if (it == libraryRoots_.constEnd()) {
        return std::nullopt;
    }

    return it.value();
}

bool InMemoryLibraryRepository::removeLibraryRoot(const QString &id)
{
    return libraryRoots_.remove(id) > 0;
}

bool InMemoryLibraryRepository::setLibraryRootEnabled(const QString &id, bool enabled)
{
    auto it = libraryRoots_.find(id);
    if (it == libraryRoots_.end()) {
        return false;
    }
    it->enabled = enabled;
    return true;
}

bool InMemoryLibraryRepository::setLibraryRootPinned(const QString &id, bool pinned)
{
    auto it = libraryRoots_.find(id);
    if (it == libraryRoots_.end()) {
        return false;
    }
    it->pinned = pinned;
    return true;
}

bool InMemoryLibraryRepository::updateLibraryRootLastIndexedAt(const QString &id, const QDateTime &indexedAt)
{
    auto it = libraryRoots_.find(id);
    if (it == libraryRoots_.end()) {
        return false;
    }
    it->lastIndexedAt = indexedAt.toUTC();
    return true;
}

} // namespace Pinloom
