#include "pinloom/core/AnchorLibraryManagement.h"

#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/ExcelCommand.h"
#include "pinloom/core/PowerPointCommand.h"
#include "pinloom/core/SumatraPdfCommand.h"
#include "pinloom/core/VisioCommand.h"
#include "pinloom/core/WordCommand.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QUrl>
#include <algorithm>

namespace Pinloom {

namespace {

AnchorLibraryOperationResult failedResult(const QString &message)
{
    return {false, 0, 0, message};
}

QStringList cleanedValues(const QStringList &values)
{
    QStringList result;
    for (const QString &value : values) {
        const QString cleaned = value.trimmed();
        if (!cleaned.isEmpty() && !result.contains(cleaned, Qt::CaseInsensitive)) {
            result.append(cleaned);
        }
    }
    return result;
}

QStringList cleanedResourceIds(const QStringList &resourceIds)
{
    return cleanedValues(resourceIds);
}

bool removeValue(QStringList &values, const QString &value)
{
    bool removed = false;
    for (auto it = values.begin(); it != values.end();) {
        if (it->compare(value, Qt::CaseInsensitive) == 0) {
            it = values.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    return removed;
}

bool appendValue(QStringList &values, const QString &value)
{
    const QString cleaned = value.trimmed();
    if (cleaned.isEmpty() || values.contains(cleaned, Qt::CaseInsensitive)) {
        return false;
    }
    values.append(cleaned);
    return true;
}

void mergeValues(QStringList &target, const QStringList &source)
{
    for (const QString &value : cleanedValues(source)) {
        appendValue(target, value);
    }
}

struct ResourceBatch {
    QList<Resource> originals;
    QList<Resource> updates;
    QHash<QString, int> indexes;
};

std::optional<ResourceBatch> loadResourceBatch(ILibraryRepository &repository,
                                               const QStringList &resourceIds)
{
    ResourceBatch batch;
    for (const QString &resourceId : cleanedResourceIds(resourceIds)) {
        const std::optional<Resource> resource = repository.findResource(resourceId);
        if (!resource.has_value()) {
            return std::nullopt;
        }
        batch.indexes.insert(resourceId, batch.updates.size());
        batch.originals.append(resource.value());
        batch.updates.append(resource.value());
    }
    return batch;
}

Anchor *findAnchor(Resource &resource, const Anchor &reference)
{
    for (Anchor &anchor : resource.anchors) {
        if (sameAnchorIdentity(anchor, reference)) {
            return &anchor;
        }
    }
    return nullptr;
}

QString normalizedPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool isRemoteLocation(const QString &location);

QString resourcePathKey(const Resource &resource)
{
    const QString location = resource.location.trimmed();
    if (location.isEmpty()) {
        return QStringLiteral("resource:%1").arg(resource.id);
    }
    if (isRemoteLocation(location)) {
        return QStringLiteral("url:%1").arg(location.toCaseFolded());
    }
    return QStringLiteral("path:%1").arg(normalizedPath(location).toCaseFolded());
}

bool equivalentMergedAnchor(const Anchor &left, const Anchor &right)
{
    if (sameAnchorIdentity(left, right)) {
        return true;
    }
    return left.name.trimmed().compare(right.name.trimmed(), Qt::CaseInsensitive) == 0
        && left.targetApp.trimmed().compare(right.targetApp.trimmed(), Qt::CaseInsensitive) == 0
        && left.targetFile.trimmed().compare(right.targetFile.trimmed(), Qt::CaseInsensitive) == 0
        && left.targetUri.trimmed().compare(right.targetUri.trimmed(), Qt::CaseInsensitive) == 0
        && anchorLocatorType(left) == anchorLocatorType(right)
        && left.locatorJson.trimmed() == right.locatorJson.trimmed();
}

void mergeAnchorMetadata(Anchor &target, const Anchor &source)
{
    if (target.name.trimmed().isEmpty()) {
        target.name = source.name;
    } else if (!source.name.trimmed().isEmpty()
               && target.name.trimmed().compare(source.name.trimmed(), Qt::CaseInsensitive) != 0) {
        appendValue(target.aliases, source.name);
    }
    mergeValues(target.aliases, source.aliases);
    mergeValues(target.tags, source.tags);
    target.pinned = target.pinned || source.pinned;
    target.deleted = target.deleted && source.deleted;

    if (target.targetApp.trimmed().isEmpty()) {
        target.targetApp = source.targetApp;
    }
    if (target.targetFile.trimmed().isEmpty()) {
        target.targetFile = source.targetFile;
    }
    if (target.targetUri.trimmed().isEmpty()) {
        target.targetUri = source.targetUri;
    }
    if (target.locatorType.trimmed().isEmpty()) {
        target.locatorType = source.locatorType;
    }
    if (target.locatorJson.trimmed().isEmpty()) {
        target.locatorJson = source.locatorJson;
    }
    if (source.createdAt.isValid()
        && (!target.createdAt.isValid() || source.createdAt < target.createdAt)) {
        target.createdAt = source.createdAt;
    }
    if (source.updatedAt.isValid()
        && (!target.updatedAt.isValid() || source.updatedAt > target.updatedAt)) {
        target.updatedAt = source.updatedAt;
    }
    if (source.usedAt.isValid()
        && (!target.usedAt.isValid() || source.usedAt > target.usedAt)) {
        target.usedAt = source.usedAt;
    }
}

void updateResourceLocation(Resource &resource, const QString &path, const QDateTime &updatedAt)
{
    resource.location = path;
    resource.updatedAt = updatedAt;
    for (Anchor &anchor : resource.anchors) {
        anchor.targetFile = path;
        const QUrl targetUri(anchor.targetUri);
        if (targetUri.isLocalFile()) {
            anchor.targetUri = QUrl::fromLocalFile(path).toString();
        }
        anchor.updatedAt = updatedAt;
    }
}

int sharedPathSuffixScore(const QString &originalPath, const QString &candidatePath)
{
    const QStringList originalParts = QDir::fromNativeSeparators(originalPath).toCaseFolded().split(
        QLatin1Char('/'), Qt::SkipEmptyParts);
    const QStringList candidateParts = QDir::fromNativeSeparators(candidatePath).toCaseFolded().split(
        QLatin1Char('/'), Qt::SkipEmptyParts);
    int score = 0;
    int left = originalParts.size() - 2;
    int right = candidateParts.size() - 2;
    while (left >= 0 && right >= 0 && originalParts.at(left) == candidateParts.at(right)) {
        ++score;
        --left;
        --right;
    }
    return score;
}

QString uniqueBestCandidate(const QString &originalPath, const QStringList &candidates)
{
    if (candidates.size() == 1) {
        return candidates.first();
    }
    QString best;
    int bestScore = -1;
    bool tied = false;
    for (const QString &candidate : candidates) {
        const int score = sharedPathSuffixScore(originalPath, candidate);
        if (score > bestScore) {
            best = candidate;
            bestScore = score;
            tied = false;
        } else if (score == bestScore) {
            tied = true;
        }
    }
    return tied ? QString() : best;
}

QJsonObject parsedLocator(const Anchor &anchor, QString *error)
{
    return anchorLocatorObject(anchor, error);
}

bool isRemoteLocation(const QString &location)
{
    const QString value = location.trimmed();
    if (value.isEmpty()
        || value.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)
        || (value.size() >= 3 && value.at(0).isLetter() && value.at(1) == QLatin1Char(':')
            && (value.at(2) == QLatin1Char('/') || value.at(2) == QLatin1Char('\\')))
        || value.startsWith(QStringLiteral("\\\\"))) {
        return false;
    }
    const QUrl url(value);
    return value.contains(QStringLiteral("://"))
        && url.isValid()
        && !url.scheme().isEmpty();
}

} // namespace

bool AnchorLibraryIntegrityReport::healthy() const
{
    return issues.isEmpty();
}

AnchorLibraryManagementService::AnchorLibraryManagementService(ILibraryRepository &repository)
    : repository_(repository)
    , observedContentRevision_(repository.contentRevision())
{
}

AnchorLibraryOperationResult AnchorLibraryManagementService::updateAnchorMetadata(
    const AnchorReference &reference,
    const AnchorMetadataUpdate &update)
{
    const QString name = update.name.trimmed();
    if (reference.resourceId.trimmed().isEmpty() || name.isEmpty()) {
        return failedResult(QStringLiteral("Anchor name is required"));
    }
    std::optional<ResourceBatch> batch = loadResourceBatch(repository_, {reference.resourceId});
    if (!batch.has_value()) {
        return failedResult(QStringLiteral("Anchor resource was not found"));
    }
    Anchor *anchor = findAnchor(batch->updates.first(), reference.anchor);
    if (!anchor) {
        return failedResult(QStringLiteral("Anchor was not found"));
    }

    anchor->name = name;
    anchor->aliases = cleanedValues(update.aliases);
    anchor->tags = cleanedValues(update.tags);
    anchor->pinned = update.pinned;
    anchor->updatedAt = QDateTime::currentDateTimeUtc();
    batch->updates.first().updatedAt = anchor->updatedAt;
    return applyManagedMutation({batch->updates},
                                {batch->originals},
                                1,
                                QStringLiteral("Updated anchor: %1").arg(name));
}

AnchorLibraryOperationResult AnchorLibraryManagementService::updateAnchorLocator(
    const AnchorReference &reference,
    const AnchorLocatorUpdate &update)
{
    const QString locatorType = update.locatorType.trimmed().toLower();
    const QString locatorJson = update.locatorJson.trimmed();
    if (reference.resourceId.trimmed().isEmpty() || locatorType.isEmpty() || locatorJson.isEmpty()) {
        return failedResult(QStringLiteral("Locator type and JSON are required"));
    }
    QJsonParseError parseError;
    const QJsonDocument locatorDocument = QJsonDocument::fromJson(locatorJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !locatorDocument.isObject()) {
        return failedResult(QStringLiteral("Locator JSON is invalid"));
    }

    std::optional<ResourceBatch> batch = loadResourceBatch(repository_, {reference.resourceId});
    if (!batch.has_value()) {
        return failedResult(QStringLiteral("Anchor resource was not found"));
    }
    Anchor *anchor = findAnchor(batch->updates.first(), reference.anchor);
    if (!anchor) {
        return failedResult(QStringLiteral("Anchor was not found"));
    }
    anchor->targetApp = update.targetApp.trimmed();
    anchor->targetFile = update.targetFile.trimmed().isEmpty()
        ? QString()
        : normalizedPath(update.targetFile);
    anchor->targetUri = update.targetUri.trimmed();
    anchor->locatorType = locatorType;
    anchor->locatorJson = QString::fromUtf8(locatorDocument.toJson(QJsonDocument::Compact));
    anchor->updatedAt = QDateTime::currentDateTimeUtc();
    batch->updates.first().updatedAt = anchor->updatedAt;
    return applyManagedMutation({batch->updates},
                                {batch->originals},
                                1,
                                QStringLiteral("Updated anchor locator"));
}

AnchorLibraryOperationResult AnchorLibraryManagementService::updateResourceMetadata(
    const QStringList &resourceIds,
    const ResourceMetadataUpdate &update)
{
    const QString title = update.title.trimmed();
    const QStringList ids = cleanedResourceIds(resourceIds);
    if (ids.isEmpty() || title.isEmpty()) {
        return failedResult(QStringLiteral("File title is required"));
    }
    std::optional<ResourceBatch> batch = loadResourceBatch(repository_, ids);
    if (!batch.has_value()) {
        return failedResult(QStringLiteral("File resource was not found"));
    }
    const QDateTime updatedAt = QDateTime::currentDateTimeUtc();
    for (Resource &resource : batch->updates) {
        resource.title = title;
        resource.aliases = cleanedValues(update.aliases);
        resource.tags = cleanedValues(update.tags);
        resource.updatedAt = updatedAt;
    }
    LibraryBatchMutation mutation;
    mutation.upserts = batch->updates;
    LibraryBatchMutation undoMutation;
    undoMutation.upserts = batch->originals;
    if (update.pinned.has_value()) {
        for (const QString &id : ids) {
            const bool originalPinned = repository_.resourceUsage(id).value_or(ResourceUsage{}).pinned;
            mutation.resourcePinUpdates.append({id, update.pinned.value()});
            undoMutation.resourcePinUpdates.append({id, originalPinned});
        }
    }
    return applyManagedMutation(mutation,
                                undoMutation,
                                ids.size(),
                                QStringLiteral("Updated file metadata"));
}

AnchorLibraryOperationResult AnchorLibraryManagementService::setAnchorsDeleted(
    const QList<AnchorReference> &references,
    bool deleted)
{
    if (references.isEmpty()) {
        return failedResult(QStringLiteral("Select at least one anchor"));
    }
    QStringList resourceIds;
    for (const AnchorReference &reference : references) {
        resourceIds.append(reference.resourceId);
    }
    std::optional<ResourceBatch> batch = loadResourceBatch(repository_, resourceIds);
    if (!batch.has_value()) {
        return failedResult(QStringLiteral("An anchor resource was not found"));
    }

    int affected = 0;
    const QDateTime updatedAt = QDateTime::currentDateTimeUtc();
    for (const AnchorReference &reference : references) {
        const auto index = batch->indexes.constFind(reference.resourceId);
        if (index == batch->indexes.constEnd()) {
            return failedResult(QStringLiteral("An anchor resource was not found"));
        }
        Resource &resource = batch->updates[index.value()];
        Anchor *anchor = findAnchor(resource, reference.anchor);
        if (!anchor) {
            return failedResult(QStringLiteral("An anchor was not found"));
        }
        if (anchor->deleted != deleted) {
            anchor->deleted = deleted;
            anchor->updatedAt = updatedAt;
            resource.updatedAt = updatedAt;
            ++affected;
        }
    }
    return applyManagedMutation({batch->updates},
                                {batch->originals},
                                affected,
                                deleted
                                    ? QStringLiteral("Moved %1 anchor(s) to trash").arg(affected)
                                    : QStringLiteral("Restored %1 anchor(s)").arg(affected));
}

AnchorLibraryOperationResult AnchorLibraryManagementService::setResourcesDeleted(
    const QStringList &resourceIds,
    bool deleted)
{
    const QStringList ids = cleanedResourceIds(resourceIds);
    if (ids.isEmpty()) {
        return failedResult(QStringLiteral("Select at least one file record"));
    }
    std::optional<ResourceBatch> batch = loadResourceBatch(repository_, ids);
    if (!batch.has_value()) {
        return failedResult(QStringLiteral("A file resource was not found"));
    }
    int affected = 0;
    const QDateTime updatedAt = QDateTime::currentDateTimeUtc();
    for (Resource &resource : batch->updates) {
        if (resource.deleted != deleted) {
            resource.deleted = deleted;
            resource.updatedAt = updatedAt;
            ++affected;
        }
    }
    return applyManagedMutation({batch->updates},
                                {batch->originals},
                                affected,
                                deleted
                                    ? QStringLiteral("Archived %1 file record(s)").arg(affected)
                                    : QStringLiteral("Restored %1 file record(s)").arg(affected));
}

AnchorLibraryOperationResult AnchorLibraryManagementService::permanentlyDeleteAnchors(
    const QList<AnchorReference> &references)
{
    if (references.isEmpty()) {
        return failedResult(QStringLiteral("Select trashed anchors to delete permanently"));
    }
    QStringList resourceIds;
    for (const AnchorReference &reference : references) {
        resourceIds.append(reference.resourceId);
    }
    std::optional<ResourceBatch> batch = loadResourceBatch(repository_, resourceIds);
    if (!batch.has_value()) {
        return failedResult(QStringLiteral("An anchor resource was not found"));
    }

    int affected = 0;
    for (const AnchorReference &reference : references) {
        Resource &resource = batch->updates[batch->indexes.value(reference.resourceId)];
        bool found = false;
        for (auto it = resource.anchors.begin(); it != resource.anchors.end(); ++it) {
            if (!sameAnchorIdentity(*it, reference.anchor)) {
                continue;
            }
            found = true;
            if (!it->deleted) {
                return failedResult(QStringLiteral("Only trashed anchors can be permanently deleted"));
            }
            resource.anchors.erase(it);
            resource.updatedAt = QDateTime::currentDateTimeUtc();
            ++affected;
            break;
        }
        if (!found) {
            return failedResult(QStringLiteral("A trashed anchor was not found"));
        }
    }
    return applyManagedMutation({batch->updates},
                                {},
                                affected,
                                QStringLiteral("Permanently deleted %1 anchor(s)").arg(affected),
                                false);
}

AnchorLibraryOperationResult AnchorLibraryManagementService::permanentlyDeleteResources(
    const QStringList &resourceIds)
{
    const QStringList ids = cleanedResourceIds(resourceIds);
    if (ids.isEmpty()) {
        return failedResult(QStringLiteral("Select archived file records to delete permanently"));
    }
    for (const QString &resourceId : ids) {
        const std::optional<Resource> resource = repository_.findResource(resourceId);
        if (!resource.has_value()) {
            return failedResult(QStringLiteral("A file resource was not found"));
        }
        if (!resource->deleted) {
            return failedResult(QStringLiteral("Only archived file records can be permanently deleted"));
        }
    }
    LibraryBatchMutation mutation;
    mutation.permanentlyDeleteResourceIds = ids;
    return applyManagedMutation(mutation,
                                {},
                                ids.size(),
                                QStringLiteral("Permanently deleted %1 file record(s)").arg(ids.size()),
                                false);
}

AnchorLibraryOperationResult AnchorLibraryManagementService::permanentlyClearResourceMetadata(
    const QStringList &resourceIds)
{
    const QStringList ids = cleanedResourceIds(resourceIds);
    if (ids.isEmpty()) {
        return failedResult(QStringLiteral("Select archived file metadata to delete permanently"));
    }
    std::optional<ResourceBatch> batch = loadResourceBatch(repository_, ids);
    if (!batch.has_value()) {
        return failedResult(QStringLiteral("A file resource was not found"));
    }
    const QDateTime updatedAt = QDateTime::currentDateTimeUtc();
    int affected = 0;
    for (Resource &resource : batch->updates) {
        if (!resource.deleted) {
            return failedResult(QStringLiteral("Only file metadata in Trash can be permanently deleted"));
        }
        if (!resource.aliases.isEmpty() || !resource.tags.isEmpty() || resource.deleted) {
            resource.aliases.clear();
            resource.tags.clear();
            resource.deleted = false;
            resource.updatedAt = updatedAt;
            ++affected;
        }
    }
    return applyManagedMutation({batch->updates},
                                {},
                                affected,
                                QStringLiteral("Permanently deleted Alias and Tag metadata for %1 file(s)")
                                    .arg(affected),
                                false);
}

AnchorLibraryOperationResult AnchorLibraryManagementService::setAnchorsPinned(
    const QList<AnchorReference> &references,
    bool pinned)
{
    if (references.isEmpty()) {
        return failedResult(QStringLiteral("Select anchors before updating Pinned"));
    }
    QStringList resourceIds;
    for (const AnchorReference &reference : references) {
        resourceIds.append(reference.resourceId);
    }
    std::optional<ResourceBatch> batch = loadResourceBatch(repository_, resourceIds);
    if (!batch.has_value()) {
        return failedResult(QStringLiteral("An anchor resource was not found"));
    }
    int affected = 0;
    const QDateTime updatedAt = QDateTime::currentDateTimeUtc();
    for (const AnchorReference &reference : references) {
        Resource &resource = batch->updates[batch->indexes.value(reference.resourceId)];
        Anchor *anchor = findAnchor(resource, reference.anchor);
        if (!anchor) {
            return failedResult(QStringLiteral("An anchor was not found"));
        }
        if (anchor->pinned != pinned) {
            anchor->pinned = pinned;
            anchor->updatedAt = updatedAt;
            resource.updatedAt = updatedAt;
            ++affected;
        }
    }
    return applyManagedMutation({batch->updates},
                                {batch->originals},
                                affected,
                                pinned
                                    ? QStringLiteral("Pinned %1 anchor(s)").arg(affected)
                                    : QStringLiteral("Unpinned %1 anchor(s)").arg(affected));
}

AnchorLibraryOperationResult AnchorLibraryManagementService::setResourcesPinned(
    const QStringList &resourceIds,
    bool pinned)
{
    const QStringList ids = cleanedResourceIds(resourceIds);
    if (ids.isEmpty()) {
        return failedResult(QStringLiteral("Select files before updating Pinned"));
    }
    LibraryBatchMutation mutation;
    LibraryBatchMutation undo;
    int affected = 0;
    for (const QString &resourceId : ids) {
        if (!repository_.findResource(resourceId).has_value()) {
            return failedResult(QStringLiteral("A file resource was not found"));
        }
        const std::optional<ResourceUsage> usage = repository_.resourceUsage(resourceId);
        const bool wasPinned = usage.has_value() && usage->pinned;
        if (wasPinned == pinned) {
            continue;
        }
        mutation.resourcePinUpdates.append({resourceId, pinned});
        undo.resourcePinUpdates.append({resourceId, wasPinned});
        ++affected;
    }
    return applyManagedMutation(mutation,
                                undo,
                                affected,
                                pinned
                                    ? QStringLiteral("Pinned %1 file(s)").arg(affected)
                                    : QStringLiteral("Unpinned %1 file(s)").arg(affected));
}

AnchorLibraryOperationResult AnchorLibraryManagementService::updateAnchorTags(
    const QList<AnchorReference> &references,
    const QStringList &tags,
    bool remove)
{
    const QStringList cleanedTags = cleanedValues(tags);
    if (references.isEmpty() || cleanedTags.isEmpty()) {
        return failedResult(QStringLiteral("Select anchors and provide at least one tag"));
    }
    QStringList resourceIds;
    for (const AnchorReference &reference : references) {
        resourceIds.append(reference.resourceId);
    }
    std::optional<ResourceBatch> batch = loadResourceBatch(repository_, resourceIds);
    if (!batch.has_value()) {
        return failedResult(QStringLiteral("An anchor resource was not found"));
    }

    int affected = 0;
    const QDateTime updatedAt = QDateTime::currentDateTimeUtc();
    for (const AnchorReference &reference : references) {
        const auto resourceIndex = batch->indexes.constFind(reference.resourceId);
        if (resourceIndex == batch->indexes.cend()) {
            return failedResult(QStringLiteral("An anchor resource was not found"));
        }
        Resource &resource = batch->updates[*resourceIndex];
        Anchor *anchor = findAnchor(resource, reference.anchor);
        if (!anchor) {
            return failedResult(QStringLiteral("An anchor was not found"));
        }
        bool changed = false;
        for (const QString &tag : cleanedTags) {
            changed = (remove ? removeValue(anchor->tags, tag) : appendValue(anchor->tags, tag))
                || changed;
        }
        if (changed) {
            anchor->updatedAt = updatedAt;
            resource.updatedAt = updatedAt;
            ++affected;
        }
    }
    return applyManagedMutation({batch->updates},
                                {batch->originals},
                                affected,
                                remove
                                    ? QStringLiteral("Removed tags from %1 anchor(s)").arg(affected)
                                    : QStringLiteral("Added tags to %1 anchor(s)").arg(affected));
}

AnchorLibraryOperationResult AnchorLibraryManagementService::updateResourceTags(
    const QStringList &resourceIds,
    const QStringList &tags,
    bool remove)
{
    const QStringList ids = cleanedResourceIds(resourceIds);
    const QStringList cleanedTags = cleanedValues(tags);
    if (ids.isEmpty() || cleanedTags.isEmpty()) {
        return failedResult(QStringLiteral("Select files and provide at least one tag"));
    }
    std::optional<ResourceBatch> batch = loadResourceBatch(repository_, ids);
    if (!batch.has_value()) {
        return failedResult(QStringLiteral("A file resource was not found"));
    }
    int affected = 0;
    const QDateTime updatedAt = QDateTime::currentDateTimeUtc();
    for (Resource &resource : batch->updates) {
        bool changed = false;
        for (const QString &tag : cleanedTags) {
            changed = (remove ? removeValue(resource.tags, tag) : appendValue(resource.tags, tag))
                || changed;
        }
        if (changed) {
            resource.updatedAt = updatedAt;
            ++affected;
        }
    }
    return applyManagedMutation({batch->updates},
                                {batch->originals},
                                affected,
                                remove
                                    ? QStringLiteral("Removed tags from %1 file(s)").arg(affected)
                                    : QStringLiteral("Added tags to %1 file(s)").arg(affected));
}

AnchorLibraryOperationResult AnchorLibraryManagementService::relinkResources(
    const QStringList &resourceIds,
    const QString &newLocation)
{
    const QStringList ids = cleanedResourceIds(resourceIds);
    const QString location = newLocation.trimmed();
    if (ids.isEmpty() || location.isEmpty()) {
        return failedResult(QStringLiteral("Select a file and provide its new location"));
    }
    if (!QFileInfo::exists(location)) {
        return failedResult(QStringLiteral("The replacement file does not exist"));
    }
    std::optional<ResourceBatch> batch = loadResourceBatch(repository_, ids);
    if (!batch.has_value()) {
        return failedResult(QStringLiteral("File resource was not found"));
    }

    const QString path = normalizedPath(location);
    const QDateTime updatedAt = QDateTime::currentDateTimeUtc();
    for (Resource &resource : batch->updates) {
        updateResourceLocation(resource, path, updatedAt);
    }
    return applyManagedMutation({batch->updates},
                                {batch->originals},
                                ids.size(),
                                QStringLiteral("Relinked file: %1").arg(QDir::toNativeSeparators(path)));
}

AnchorLibraryOperationResult AnchorLibraryManagementService::autoRelinkMissingResources(
    const QStringList &resourceIds,
    const QString &searchRoot)
{
    const QStringList ids = cleanedResourceIds(resourceIds);
    const QFileInfo rootInfo(searchRoot);
    if (ids.isEmpty() || !rootInfo.exists() || !rootInfo.isDir()) {
        return failedResult(QStringLiteral("Select missing files and provide a search directory"));
    }
    std::optional<ResourceBatch> batch = loadResourceBatch(repository_, ids);
    if (!batch.has_value()) {
        return failedResult(QStringLiteral("A file resource was not found"));
    }

    QMultiHash<QString, QString> fileCandidates;
    QMultiHash<QString, QString> directoryCandidates;
    QDirIterator iterator(rootInfo.absoluteFilePath(),
                          QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QFileInfo candidate(iterator.next());
        const QString key = candidate.fileName().toCaseFolded();
        if (candidate.isDir()) {
            directoryCandidates.insert(key, candidate.absoluteFilePath());
        } else {
            fileCandidates.insert(key, candidate.absoluteFilePath());
        }
    }

    int affected = 0;
    int unresolved = 0;
    const QDateTime updatedAt = QDateTime::currentDateTimeUtc();
    for (Resource &resource : batch->updates) {
        if (resource.location.trimmed().isEmpty() || QFileInfo::exists(resource.location)
            || isRemoteLocation(resource.location)) {
            continue;
        }
        const QFileInfo original(resource.location);
        const QStringList candidates = resource.kind == ResourceKind::Folder
            ? directoryCandidates.values(original.fileName().toCaseFolded())
            : fileCandidates.values(original.fileName().toCaseFolded());
        const QString replacement = uniqueBestCandidate(resource.location, candidates);
        if (replacement.isEmpty()) {
            ++unresolved;
            continue;
        }
        updateResourceLocation(resource, normalizedPath(replacement), updatedAt);
        ++affected;
    }

    AnchorLibraryOperationResult result = applyManagedMutation(
        {batch->updates},
        {batch->originals},
        affected,
        QStringLiteral("Relinked %1 missing file(s); %2 unresolved").arg(affected).arg(unresolved));
    result.unresolvedCount = unresolved;
    return result;
}

AnchorLibraryOperationResult AnchorLibraryManagementService::mergeResources(
    const QString &targetResourceId,
    const QStringList &sourceResourceIds)
{
    const QString targetId = targetResourceId.trimmed();
    QStringList sourceIds = cleanedResourceIds(sourceResourceIds);
    sourceIds.erase(std::remove(sourceIds.begin(), sourceIds.end(), targetId), sourceIds.end());
    if (targetId.isEmpty() || sourceIds.isEmpty()) {
        return failedResult(QStringLiteral("No duplicate resources are available to merge"));
    }

    QStringList allIds{targetId};
    allIds.append(sourceIds);
    std::optional<ResourceBatch> batch = loadResourceBatch(repository_, allIds);
    if (!batch.has_value()) {
        return failedResult(QStringLiteral("A duplicate resource was not found"));
    }

    Resource &target = batch->updates[batch->indexes.value(targetId)];
    const QDateTime updatedAt = QDateTime::currentDateTimeUtc();
    bool targetPinned = repository_.resourceUsage(targetId).value_or(ResourceUsage{}).pinned;
    for (const QString &sourceId : sourceIds) {
        Resource &source = batch->updates[batch->indexes.value(sourceId)];
        if (resourcePathKey(target) != resourcePathKey(source)) {
            return failedResult(QStringLiteral("Only records for the same path can be merged"));
        }
        if (target.title.trimmed().isEmpty()) {
            target.title = source.title;
        } else if (!source.title.trimmed().isEmpty()
                   && target.title.trimmed().compare(source.title.trimmed(), Qt::CaseInsensitive) != 0) {
            appendValue(target.aliases, source.title);
        }
        if (target.kind == ResourceKind::Unknown) {
            target.kind = source.kind;
        }
        if (target.location.trimmed().isEmpty()) {
            target.location = source.location;
        }
        if (target.content.trimmed().isEmpty()) {
            target.content = source.content;
        }
        mergeValues(target.aliases, source.aliases);
        mergeValues(target.tags, source.tags);
        for (const Anchor &anchor : source.anchors) {
            auto existing = std::find_if(target.anchors.begin(), target.anchors.end(),
                                         [&anchor](const Anchor &candidate) {
                                             return equivalentMergedAnchor(candidate, anchor);
                                         });
            if (existing == target.anchors.end()) {
                target.anchors.append(anchor);
            } else {
                mergeAnchorMetadata(*existing, anchor);
            }
        }
        targetPinned = targetPinned
            || repository_.resourceUsage(sourceId).value_or(ResourceUsage{}).pinned;
        source.deleted = true;
        source.updatedAt = updatedAt;
    }
    target.updatedAt = updatedAt;

    LibraryBatchMutation mutation{batch->updates};
    LibraryBatchMutation undo{batch->originals};
    const bool originalTargetPinned = repository_.resourceUsage(targetId).value_or(ResourceUsage{}).pinned;
    if (targetPinned != originalTargetPinned) {
        mutation.resourcePinUpdates.append({targetId, targetPinned});
        undo.resourcePinUpdates.append({targetId, originalTargetPinned});
    }
    return applyManagedMutation(mutation,
                                undo,
                                sourceIds.size(),
                                QStringLiteral("Merged %1 duplicate resource(s)").arg(sourceIds.size()));
}

AnchorLibraryOperationResult AnchorLibraryManagementService::deduplicateAnchors(
    const QStringList &resourceIds)
{
    const QStringList ids = cleanedResourceIds(resourceIds);
    if (ids.isEmpty()) {
        return failedResult(QStringLiteral("Select files before removing duplicate anchors"));
    }
    std::optional<ResourceBatch> batch = loadResourceBatch(repository_, ids);
    if (!batch.has_value()) {
        return failedResult(QStringLiteral("A file resource was not found"));
    }
    int affected = 0;
    const QDateTime updatedAt = QDateTime::currentDateTimeUtc();
    for (Resource &resource : batch->updates) {
        QList<Anchor> unique;
        for (const Anchor &anchor : resource.anchors) {
            auto existing = std::find_if(unique.begin(), unique.end(), [&anchor](const Anchor &candidate) {
                return equivalentMergedAnchor(candidate, anchor);
            });
            if (existing == unique.end()) {
                unique.append(anchor);
            } else {
                mergeAnchorMetadata(*existing, anchor);
                ++affected;
            }
        }
        if (unique.size() != resource.anchors.size()) {
            resource.anchors = unique;
            resource.updatedAt = updatedAt;
        }
    }
    return applyManagedMutation({batch->updates},
                                {batch->originals},
                                affected,
                                QStringLiteral("Removed %1 duplicate anchor(s)").arg(affected));
}

AnchorLibraryOperationResult AnchorLibraryManagementService::renameTag(const QString &oldTagValue,
                                                                        const QString &newTagValue)
{
    const QString oldTag = oldTagValue.trimmed();
    const QString newTag = newTagValue.trimmed();
    if (oldTag.isEmpty() || newTag.isEmpty()) {
        return failedResult(QStringLiteral("Old and new tag names are required"));
    }
    QList<Resource> originals = allResources(true);
    QList<Resource> updates = originals;
    int affected = 0;
    const QDateTime updatedAt = QDateTime::currentDateTimeUtc();
    for (Resource &resource : updates) {
        bool resourceChanged = false;
        if (removeValue(resource.tags, oldTag)) {
            appendValue(resource.tags, newTag);
            resourceChanged = true;
            ++affected;
        }
        for (Anchor &anchor : resource.anchors) {
            if (removeValue(anchor.tags, oldTag)) {
                appendValue(anchor.tags, newTag);
                anchor.updatedAt = updatedAt;
                resourceChanged = true;
                ++affected;
            }
        }
        if (resourceChanged) {
            resource.updatedAt = updatedAt;
        }
    }
    return applyManagedMutation({updates},
                                {originals},
                                affected,
                                QStringLiteral("Renamed tag \"%1\" to \"%2\"").arg(oldTag, newTag));
}

AnchorLibraryOperationResult AnchorLibraryManagementService::deleteTag(const QString &tagValue)
{
    const QString tag = tagValue.trimmed();
    if (tag.isEmpty()) {
        return failedResult(QStringLiteral("Tag name is required"));
    }
    QList<Resource> originals = allResources(true);
    QList<Resource> updates = originals;
    int affected = 0;
    const QDateTime updatedAt = QDateTime::currentDateTimeUtc();
    for (Resource &resource : updates) {
        bool resourceChanged = false;
        if (removeValue(resource.tags, tag)) {
            resourceChanged = true;
            ++affected;
        }
        for (Anchor &anchor : resource.anchors) {
            if (removeValue(anchor.tags, tag)) {
                anchor.updatedAt = updatedAt;
                resourceChanged = true;
                ++affected;
            }
        }
        if (resourceChanged) {
            resource.updatedAt = updatedAt;
        }
    }
    return applyManagedMutation({updates},
                                {originals},
                                affected,
                                QStringLiteral("Deleted tag \"%1\"").arg(tag));
}

AnchorValidationResult AnchorLibraryManagementService::validateAnchor(const Resource &resource,
                                                                       const Anchor &anchor) const
{
    AnchorValidationResult result;
    const QString targetFile = anchor.targetFile.trimmed().isEmpty()
        ? resource.location.trimmed()
        : anchor.targetFile.trimmed();
    if (!targetFile.isEmpty() && !isRemoteLocation(targetFile) && !QFileInfo::exists(targetFile)) {
        result.issues.append(QStringLiteral("Target file is missing"));
    }

    QString parseError;
    const QJsonObject locator = parsedLocator(anchor, &parseError);
    if (!parseError.isEmpty()) {
        result.issues.append(parseError);
        return result;
    }
    const QString type = anchorLocatorType(anchor);
    if (type.isEmpty()) {
        result.issues.append(QStringLiteral("Locator type is missing"));
        return result;
    }
    if (locator.isEmpty()) {
        result.issues.append(QStringLiteral("Locator JSON object is empty"));
        return result;
    }

    QString semanticError;
    if (isSumatraPdfLocatorType(type)) {
        const SumatraPdfCommandResult command = buildSumatraPdfCommand(
            anchor, resource.location, QStringLiteral("SumatraPDF.exe"));
        semanticError = command.error;
    } else if (isExcelLocatorType(type)) {
        semanticError = buildExcelJumpCommand(anchor, resource.location).error;
    } else if (isWordLocatorType(type)) {
        semanticError = buildWordJumpCommand(anchor, resource.location).error;
    } else if (isPowerPointLocatorType(type)) {
        semanticError = buildPowerPointJumpCommand(anchor, resource.location).error;
    } else if (isVisioLocatorType(type)) {
        semanticError = buildVisioJumpCommand(anchor, resource.location).error;
    } else if (type == QLatin1String("file.line")) {
        if (anchorLocatorLine(anchor) <= 0) {
            semanticError = QStringLiteral("File line is missing");
        }
    } else if (type == QLatin1String("url.fragment")) {
        if (anchor.targetUri.trimmed().isEmpty() && resource.location.trimmed().isEmpty()) {
            semanticError = QStringLiteral("URL target is missing");
        }
    } else if (type != QLatin1String("manual")
               && type != QLatin1String("text.heading")
               && type != QLatin1String("text.block")) {
        semanticError = QStringLiteral("Locator type is unsupported");
    }
    if (!semanticError.trimmed().isEmpty()) {
        result.issues.append(semanticError.trimmed());
    }
    result.valid = result.issues.isEmpty();
    return result;
}

AnchorLibraryIntegrityReport AnchorLibraryManagementService::inspectIntegrity() const
{
    AnchorLibraryIntegrityReport report;
    const QList<Resource> resources = allResources(false);
    QHash<QString, QList<Resource>> resourcesByPath;
    for (const Resource &resource : resources) {
        if (resource.deleted) {
            continue;
        }
        resourcesByPath[resourcePathKey(resource)].append(resource);
        if (!resource.location.trimmed().isEmpty()
            && !isRemoteLocation(resource.location)
            && !QFileInfo::exists(resource.location)) {
            report.issues.append({AnchorLibraryIssueKind::MissingTarget,
                                  {resource.id},
                                  {},
                                  resource.title,
                                  QStringLiteral("File is missing: %1").arg(resource.location)});
            ++report.missingTargetCount;
        }
        for (const Anchor &anchor : resource.anchors) {
            if (anchor.deleted) {
                continue;
            }
            const AnchorValidationResult validation = validateAnchor(resource, anchor);
            if (!validation.valid) {
                report.issues.append({AnchorLibraryIssueKind::InvalidLocator,
                                      {resource.id},
                                      anchor.id,
                                      anchor.name,
                                      validation.issues.join(QStringLiteral("; "))});
                ++report.invalidLocatorCount;
            }
        }
    }

    for (auto group = resourcesByPath.cbegin(); group != resourcesByPath.cend(); ++group) {
        const QList<Resource> groupedResources = group.value();
        if (groupedResources.size() > 1 && !group.key().startsWith(QStringLiteral("resource:"))) {
            QStringList ids;
            for (const Resource &resource : groupedResources) {
                ids.append(resource.id);
            }
            report.issues.append({AnchorLibraryIssueKind::DuplicateResource,
                                  ids,
                                  {},
                                  groupedResources.first().title,
                                  QStringLiteral("%1 records share one target").arg(ids.size())});
            ++report.duplicateResourceCount;
        }

        struct LocatedAnchor {
            QString resourceId;
            Anchor anchor;
        };
        QList<LocatedAnchor> uniqueAnchors;
        for (const Resource &resource : groupedResources) {
            for (const Anchor &anchor : resource.anchors) {
                if (anchor.deleted) {
                    continue;
                }
                const auto duplicate = std::find_if(
                    uniqueAnchors.cbegin(), uniqueAnchors.cend(), [&anchor](const LocatedAnchor &existing) {
                        return equivalentMergedAnchor(existing.anchor, anchor);
                    });
                if (duplicate == uniqueAnchors.cend()) {
                    uniqueAnchors.append({resource.id, anchor});
                    continue;
                }
                report.issues.append({AnchorLibraryIssueKind::DuplicateAnchor,
                                      {duplicate->resourceId, resource.id},
                                      anchor.id,
                                      anchor.name,
                                      QStringLiteral("Anchor duplicates another locator for the same target")});
                ++report.duplicateAnchorCount;
            }
        }
    }
    return report;
}

QList<AnchorLibraryTagSummary> AnchorLibraryManagementService::tagSummary() const
{
    QHash<QString, AnchorLibraryTagSummary> summaries;
    for (const Resource &resource : allResources(false)) {
        if (resource.deleted) {
            continue;
        }
        QSet<QString> resourceTags;
        for (const QString &tag : resource.tags) {
            const QString key = tag.trimmed().toCaseFolded();
            if (key.isEmpty()) {
                continue;
            }
            AnchorLibraryTagSummary &summary = summaries[key];
            if (summary.tag.isEmpty()) {
                summary.tag = tag.trimmed();
            }
            if (!resourceTags.contains(key)) {
                ++summary.resourceCount;
                resourceTags.insert(key);
            }
        }
        for (const Anchor &anchor : resource.anchors) {
            if (anchor.deleted) {
                continue;
            }
            for (const QString &tag : anchor.tags) {
                const QString key = tag.trimmed().toCaseFolded();
                if (key.isEmpty()) {
                    continue;
                }
                AnchorLibraryTagSummary &summary = summaries[key];
                if (summary.tag.isEmpty()) {
                    summary.tag = tag.trimmed();
                }
                ++summary.anchorCount;
            }
        }
    }
    QList<AnchorLibraryTagSummary> result = summaries.values();
    std::sort(result.begin(), result.end(), [](const AnchorLibraryTagSummary &left,
                                               const AnchorLibraryTagSummary &right) {
        return left.tag.compare(right.tag, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QList<AnchorLibraryHistoryItem> AnchorLibraryManagementService::history() const
{
    QList<AnchorLibraryHistoryItem> items;
    for (auto it = history_.crbegin(); it != history_.crend(); ++it) {
        items.append(it->item);
    }
    return items;
}

bool AnchorLibraryManagementService::canUndo() const
{
    if (repository_.contentRevision() != observedContentRevision_) {
        return false;
    }
    return std::any_of(history_.crbegin(), history_.crend(), [](const HistoryRecord &record) {
        return !record.item.undone;
    });
}

AnchorLibraryOperationResult AnchorLibraryManagementService::undoLast()
{
    if (repository_.contentRevision() != observedContentRevision_) {
        clearHistory();
        return failedResult(QStringLiteral("Undo history was cleared because the library changed externally"));
    }
    for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
        if (it->item.undone) {
            continue;
        }
        if (!repository_.applyBatch(it->undoMutation)) {
            return failedResult(QStringLiteral("Unable to undo the last Anchor Library operation"));
        }
        observedContentRevision_ = repository_.contentRevision();
        it->item.undone = true;
        return {true,
                it->item.affectedCount,
                0,
                QStringLiteral("Undid: %1").arg(it->item.action)};
    }
    return failedResult(QStringLiteral("No Anchor Library operation is available to undo"));
}

void AnchorLibraryManagementService::clearHistory()
{
    history_.clear();
    observedContentRevision_ = repository_.contentRevision();
}

AnchorLibraryOperationResult AnchorLibraryManagementService::applyManagedMutation(
    const LibraryBatchMutation &mutation,
    const LibraryBatchMutation &undoMutation,
    int affectedCount,
    const QString &message,
    bool recordHistory)
{
    if (affectedCount <= 0) {
        return {true, 0, 0, message};
    }
    if (repository_.contentRevision() != observedContentRevision_) {
        history_.clear();
        observedContentRevision_ = repository_.contentRevision();
    }
    if (!repository_.applyBatch(mutation)) {
        return failedResult(QStringLiteral("Unable to update the Anchor Library atomically"));
    }
    observedContentRevision_ = repository_.contentRevision();
    if (recordHistory) {
        appendHistory(message, affectedCount, undoMutation);
    } else {
        history_.clear();
    }
    return {true, affectedCount, 0, message};
}

QList<Resource> AnchorLibraryManagementService::allResources(bool includeDeleted) const
{
    SearchQuery query;
    query.limit = 0;
    query.includeDeleted = includeDeleted;
    QList<Resource> resources;
    QSet<QString> seen;
    for (const SearchResult &result : repository_.search(query)) {
        if (seen.contains(result.resource.id)) {
            continue;
        }
        seen.insert(result.resource.id);
        resources.append(result.resource);
    }
    return resources;
}

void AnchorLibraryManagementService::appendHistory(const QString &action,
                                                    int affectedCount,
                                                    const LibraryBatchMutation &undoMutation)
{
    HistoryRecord record;
    record.item.id = nextHistoryId_++;
    record.item.action = action;
    record.item.affectedCount = affectedCount;
    record.item.timestamp = QDateTime::currentDateTimeUtc();
    record.undoMutation = undoMutation;
    history_.append(record);
    constexpr int MaxHistoryItems = 100;
    if (history_.size() > MaxHistoryItems) {
        history_.removeFirst();
    }
}

} // namespace Pinloom
