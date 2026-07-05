#include "pinloom/clip/ClipSearch.h"

#include <algorithm>
#include <utility>

namespace Pinloom {

namespace {

struct FieldMatch {
    QString field;
    QString value;
    int priority = 0;
    bool matched = false;
};

struct Candidate {
    ClipSearchResult result;
    int priority = 0;
};

QString normalizedSearchText(const QString &value)
{
    return value.simplified().toCaseFolded();
}

QString displayNameForClip(const Clip &clip)
{
    const QString trimmedName = clip.name.trimmed();
    return trimmedName.isEmpty() ? clip.preview : trimmedName;
}

bool isIncludedState(ClipState state, const ClipSearchOptions &options)
{
    switch (state) {
    case ClipState::Saved:
        return options.includeSaved;
    case ClipState::Temporary:
        return options.includeTemporary;
    }
    return false;
}

bool isIncludedEmptyQueryState(ClipState state, const ClipSearchOptions &options)
{
    if (options.includeTemporary) {
        return state == ClipState::Temporary;
    }
    return isIncludedState(state, options);
}

FieldMatch matchValue(const QString &field,
                      const QString &value,
                      const QString &query,
                      int exactPriority,
                      int prefixPriority,
                      int containsPriority)
{
    const QString normalizedValue = normalizedSearchText(value);
    if (normalizedValue.isEmpty() || query.isEmpty()) {
        return {};
    }

    if (normalizedValue == query) {
        return {field, value.trimmed(), exactPriority, true};
    }

    if (normalizedValue.startsWith(query)) {
        return {field, value.trimmed(), prefixPriority, true};
    }

    if (normalizedValue.contains(query)) {
        return {field, value.trimmed(), containsPriority, true};
    }

    return {};
}

void considerMatch(FieldMatch &best, const FieldMatch &match)
{
    if (!match.matched) {
        return;
    }

    if (!best.matched || match.priority > best.priority) {
        best = match;
    }
}

void considerListMatch(FieldMatch &best,
                       const QString &field,
                       const QStringList &values,
                       const QString &query,
                       int exactPriority,
                       int prefixPriority,
                       int containsPriority)
{
    for (const QString &value : values) {
        considerMatch(best, matchValue(field, value, query, exactPriority, prefixPriority, containsPriority));
    }
}

FieldMatch bestMatchForClip(const Clip &clip, const QString &query, bool tagQuery)
{
    FieldMatch best;

    if (tagQuery) {
        considerListMatch(best,
                          QStringLiteral("tag"),
                          clip.tags,
                          query,
                          1100,
                          1000,
                          900);
        return best;
    }

    considerMatch(best, matchValue(QStringLiteral("name"), clip.name, query, 1000, 700, 600));
    considerListMatch(best, QStringLiteral("alias"), clip.aliases, query, 900, 690, 590);
    considerListMatch(best, QStringLiteral("tag"), clip.tags, query, 800, 680, 580);
    considerMatch(best, matchValue(QStringLiteral("preview"), clip.preview, query, 660, 660, 560));
    considerMatch(best, matchValue(QStringLiteral("text"), clip.text, query, 650, 650, 550));

    return best;
}

ClipSearchResult resultForClip(const Clip &clip, const FieldMatch &match, int priority)
{
    ClipSearchResult result;
    result.clipId = clip.id;
    result.displayName = displayNameForClip(clip);
    result.preview = clip.preview;
    result.matchedField = match.field;
    result.matchedValue = match.value;
    result.score = static_cast<double>(priority) + (clip.pinned ? 10.0 : 0.0);
    result.state = clip.state;
    result.tags = clip.tags;
    result.aliases = clip.aliases;
    result.pinned = clip.pinned;
    result.createdAt = clip.createdAt;
    result.updatedAt = clip.updatedAt;
    result.usedAt = clip.usedAt;
    return result;
}

bool dateTimeMoreRecent(const QDateTime &left, const QDateTime &right)
{
    if (left.isValid() != right.isValid()) {
        return left.isValid();
    }

    if (left.isValid() && right.isValid() && left != right) {
        return left > right;
    }

    return false;
}

bool candidatesLessThan(const Candidate &left, const Candidate &right)
{
    if (left.priority != right.priority) {
        return left.priority > right.priority;
    }

    if (left.result.pinned != right.result.pinned) {
        return left.result.pinned;
    }

    if (left.result.usedAt != right.result.usedAt) {
        return dateTimeMoreRecent(left.result.usedAt, right.result.usedAt);
    }

    if (left.result.updatedAt != right.result.updatedAt) {
        return dateTimeMoreRecent(left.result.updatedAt, right.result.updatedAt);
    }

    if (left.result.createdAt != right.result.createdAt) {
        return dateTimeMoreRecent(left.result.createdAt, right.result.createdAt);
    }

    return left.result.clipId < right.result.clipId;
}

} // namespace

QList<ClipSearchResult> searchClips(const QList<Clip> &clips,
                                    const QString &query,
                                    const ClipSearchOptions &options)
{
    const QString normalizedQuery = normalizedSearchText(query);
    const bool tagQuery = normalizedQuery.startsWith(QLatin1Char('#'));
    const QString effectiveQuery = tagQuery ? normalizedSearchText(normalizedQuery.mid(1)) : normalizedQuery;
    const bool emptyQuery = !tagQuery && effectiveQuery.isEmpty();

    if ((emptyQuery && !options.emptyQueryReturnsPinnedAndRecent) || (tagQuery && effectiveQuery.isEmpty())
        || (!options.includeSaved && !options.includeTemporary) || options.limit == 0) {
        return {};
    }

    QList<Candidate> candidates;
    for (const Clip &clip : clips) {
        if (clip.kind != ClipKind::Text) {
            continue;
        }

        if (emptyQuery) {
            if (!isIncludedEmptyQueryState(clip.state, options)) {
                continue;
            }

            FieldMatch emptyMatch;
            emptyMatch.field = QStringLiteral("empty");
            emptyMatch.priority = 100;
            emptyMatch.matched = true;
            candidates.append({resultForClip(clip, emptyMatch, emptyMatch.priority), emptyMatch.priority});
            continue;
        }

        if (!isIncludedState(clip.state, options)) {
            continue;
        }

        const FieldMatch match = bestMatchForClip(clip, effectiveQuery, tagQuery);
        if (match.matched) {
            candidates.append({resultForClip(clip, match, match.priority), match.priority});
        }
    }

    std::sort(candidates.begin(), candidates.end(), candidatesLessThan);

    QList<ClipSearchResult> results;
    const int candidateCount = static_cast<int>(candidates.size());
    const int resultLimit = options.limit > 0 ? std::min(options.limit, candidateCount) : candidateCount;
    for (int index = 0; index < resultLimit; ++index) {
        ClipSearchResult result = candidates.at(index).result;
        result.rank = index + 1;
        results.append(result);
    }

    return results;
}

ClipSearchService::ClipSearchService(InMemoryClipRepository &repository)
    : ClipSearchService([&repository]() {
                            return repository.clips();
                        },
                        [&repository]() {
                            return repository.savedClips();
                        },
                        [&repository](const QString &clipId) {
                            return repository.findClip(clipId);
                        },
                        [&repository](const QString &clipId,
                                      const QString &name,
                                      const QStringList &aliases,
                                      const QStringList &tags,
                                      bool pinned,
                                      const QDateTime &now) {
                            return repository.saveClip(clipId, name, aliases, tags, pinned, now);
                        })
{
}

ClipSearchService::ClipSearchService(SqliteClipRepository &repository)
    : ClipSearchService([&repository]() {
                            return repository.clips();
                        },
                        [&repository]() {
                            return repository.savedClips();
                        },
                        [&repository](const QString &clipId) {
                            return repository.findClip(clipId);
                        },
                        [&repository](const QString &clipId,
                                      const QString &name,
                                      const QStringList &aliases,
                                      const QStringList &tags,
                                      bool pinned,
                                      const QDateTime &now) {
                            return repository.saveClip(clipId, name, aliases, tags, pinned, now);
                        },
                        [&repository]() {
                            return repository.lastError();
                        })
{
}

ClipSearchService::ClipSearchService(ListClipsCallback listClips,
                                     ListClipsCallback listSavedClips,
                                     FindClipCallback findClip,
                                     SaveClipCallback saveClip,
                                     LastErrorCallback lastError)
    : listClips_(std::move(listClips))
    , listSavedClips_(std::move(listSavedClips))
    , findClip_(std::move(findClip))
    , saveClip_(std::move(saveClip))
    , lastError_(std::move(lastError))
{
}

QList<ClipSearchResult> ClipSearchService::search(const QString &query, const ClipSearchOptions &options) const
{
    if (options.includeSaved && !options.includeTemporary && listSavedClips_) {
        return searchClips(listSavedClips_(), query, options);
    }

    if (listClips_) {
        return searchClips(listClips_(), query, options);
    }

    if (listSavedClips_) {
        return searchClips(listSavedClips_(), query, options);
    }

    return {};
}

std::optional<Clip> ClipSearchService::findClip(const QString &clipId) const
{
    if (!findClip_) {
        return std::nullopt;
    }

    return findClip_(clipId);
}

bool ClipSearchService::saveClip(const QString &clipId,
                                 const QString &name,
                                 const QStringList &aliases,
                                 const QStringList &tags,
                                 bool pinned,
                                 const QDateTime &now) const
{
    if (!saveClip_) {
        return false;
    }

    return saveClip_(clipId, name, aliases, tags, pinned, now);
}

QString ClipSearchService::lastError() const
{
    return lastError_ ? lastError_() : QString();
}

} // namespace Pinloom
