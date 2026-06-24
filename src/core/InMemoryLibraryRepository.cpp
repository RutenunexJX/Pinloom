#include "pinloom/core/InMemoryLibraryRepository.h"

#include <algorithm>

namespace Pinloom {

bool InMemoryLibraryRepository::upsertResource(const Resource &resource)
{
    if (resource.id.trimmed().isEmpty()) {
        return false;
    }

    resources_.insert(resource.id, resource);
    return true;
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

        QString matchedField;
        double score = 0.1;
        if (needle.isEmpty()) {
            matchedField = QStringLiteral("all");
        } else if (resource.title.contains(needle, caseMode)) {
            matchedField = QStringLiteral("title");
            score = 1.0;
        } else if (resource.aliases.join(QLatin1Char('\n')).contains(needle, caseMode)) {
            matchedField = QStringLiteral("alias");
            score = 0.8;
        } else if (resource.tags.join(QLatin1Char('\n')).contains(needle, caseMode)) {
            matchedField = QStringLiteral("tag");
            score = 0.6;
        } else if (resource.location.contains(needle, caseMode)) {
            matchedField = QStringLiteral("location");
            score = 0.4;
        }

        if (!matchedField.isEmpty()) {
            results.append(SearchResult{resource, score, matchedField});
        }
    }

    std::sort(results.begin(), results.end(), [](const SearchResult &left, const SearchResult &right) {
        return left.score > right.score;
    });

    if (query.limit > 0 && results.size() > query.limit) {
        results.erase(results.begin() + query.limit, results.end());
    }

    return results;
}

} // namespace Pinloom
