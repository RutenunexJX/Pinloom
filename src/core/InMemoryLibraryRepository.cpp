#include "pinloom/core/InMemoryLibraryRepository.h"

#include <QFileInfo>
#include <algorithm>

namespace Pinloom {

namespace {

bool containsNeedle(const QString &text, const QString &needle)
{
    return text.contains(needle, Qt::CaseInsensitive);
}

SearchResult resourceResult(const Resource &resource, double score, const QString &field)
{
    return SearchResult{resource, score, field, std::nullopt};
}

SearchResult anchorResult(const Resource &resource, const Anchor &anchor)
{
    return SearchResult{resource, 0.0, QStringLiteral("anchor"), anchor};
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

        if (needle.isEmpty()) {
            results.append(resourceResult(resource, 100.0, QStringLiteral("all")));
        } else if (resource.title.contains(needle, caseMode)) {
            results.append(resourceResult(resource, 10.0, QStringLiteral("title")));
        } else if (QFileInfo(resource.location).fileName().contains(needle, caseMode)) {
            results.append(resourceResult(resource, 15.0, QStringLiteral("filename")));
        } else if (resource.aliases.join(QLatin1Char('\n')).contains(needle, caseMode)) {
            results.append(resourceResult(resource, 20.0, QStringLiteral("alias")));
        } else if (resource.tags.join(QLatin1Char('\n')).contains(needle, caseMode)) {
            results.append(resourceResult(resource, 30.0, QStringLiteral("tag")));
        } else if (resource.location.contains(needle, caseMode)) {
            results.append(resourceResult(resource, 90.0, QStringLiteral("path")));
        }

        if (!needle.isEmpty()) {
            for (const Anchor &anchor : resource.anchors) {
                if (containsNeedle(anchor.target, needle)) {
                    results.append(anchorResult(resource, anchor));
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
    return true;
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
