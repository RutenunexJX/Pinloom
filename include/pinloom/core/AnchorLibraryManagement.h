#pragma once

#include "pinloom/core/LibraryRepository.h"

#include <QDateTime>
#include <QList>
#include <QStringList>
#include <optional>

namespace Pinloom {

struct AnchorReference {
    QString resourceId;
    Anchor anchor;
};

struct AnchorMetadataUpdate {
    QString name;
    QStringList aliases;
    QStringList tags;
    bool pinned = false;
};

struct AnchorLocatorUpdate {
    QString targetApp;
    QString targetFile;
    QString targetUri;
    QString locatorType;
    QString locatorJson;
};

struct ResourceMetadataUpdate {
    QString title;
    QStringList aliases;
    QStringList tags;
    std::optional<bool> pinned;
};

struct AnchorLibraryOperationResult {
    bool success = false;
    int affectedCount = 0;
    int unresolvedCount = 0;
    QString message;
};

struct AnchorValidationResult {
    bool valid = false;
    QStringList issues;
};

enum class AnchorLibraryIssueKind {
    MissingTarget,
    DuplicateResource,
    DuplicateAnchor,
    InvalidLocator
};

struct AnchorLibraryIntegrityIssue {
    AnchorLibraryIssueKind kind = AnchorLibraryIssueKind::InvalidLocator;
    QStringList resourceIds;
    QString anchorId;
    QString title;
    QString detail;
};

struct AnchorLibraryIntegrityReport {
    QList<AnchorLibraryIntegrityIssue> issues;
    int missingTargetCount = 0;
    int duplicateResourceCount = 0;
    int duplicateAnchorCount = 0;
    int invalidLocatorCount = 0;

    bool healthy() const;
};

struct AnchorLibraryTagSummary {
    QString tag;
    int resourceCount = 0;
    int anchorCount = 0;
};

struct AnchorLibraryHistoryItem {
    int id = 0;
    QString action;
    int affectedCount = 0;
    QDateTime timestamp;
    bool undone = false;
};

class AnchorLibraryManagementService {
public:
    explicit AnchorLibraryManagementService(ILibraryRepository &repository);

    AnchorLibraryOperationResult updateAnchorMetadata(const AnchorReference &reference,
                                                       const AnchorMetadataUpdate &update);
    AnchorLibraryOperationResult updateAnchorLocator(const AnchorReference &reference,
                                                      const AnchorLocatorUpdate &update);
    AnchorLibraryOperationResult updateResourceMetadata(const QStringList &resourceIds,
                                                         const ResourceMetadataUpdate &update);
    AnchorLibraryOperationResult setAnchorsDeleted(const QList<AnchorReference> &references,
                                                    bool deleted);
    AnchorLibraryOperationResult setResourcesDeleted(const QStringList &resourceIds,
                                                      bool deleted);
    AnchorLibraryOperationResult permanentlyDeleteAnchors(
        const QList<AnchorReference> &references);
    AnchorLibraryOperationResult permanentlyDeleteResources(const QStringList &resourceIds);
    AnchorLibraryOperationResult setAnchorsPinned(const QList<AnchorReference> &references,
                                                   bool pinned);
    AnchorLibraryOperationResult setResourcesPinned(const QStringList &resourceIds,
                                                     bool pinned);
    AnchorLibraryOperationResult updateAnchorTags(const QList<AnchorReference> &references,
                                                   const QStringList &tags,
                                                   bool remove);
    AnchorLibraryOperationResult updateResourceTags(const QStringList &resourceIds,
                                                     const QStringList &tags,
                                                     bool remove);
    AnchorLibraryOperationResult relinkResources(const QStringList &resourceIds,
                                                  const QString &newLocation);
    AnchorLibraryOperationResult autoRelinkMissingResources(const QStringList &resourceIds,
                                                             const QString &searchRoot);
    AnchorLibraryOperationResult mergeResources(const QString &targetResourceId,
                                                 const QStringList &sourceResourceIds);
    AnchorLibraryOperationResult deduplicateAnchors(const QStringList &resourceIds);
    AnchorLibraryOperationResult renameTag(const QString &oldTag, const QString &newTag);
    AnchorLibraryOperationResult deleteTag(const QString &tag);

    AnchorValidationResult validateAnchor(const Resource &resource, const Anchor &anchor) const;
    AnchorLibraryIntegrityReport inspectIntegrity() const;
    QList<AnchorLibraryTagSummary> tagSummary() const;

    QList<AnchorLibraryHistoryItem> history() const;
    bool canUndo() const;
    AnchorLibraryOperationResult undoLast();
    void clearHistory();

private:
    struct HistoryRecord {
        AnchorLibraryHistoryItem item;
        LibraryBatchMutation undoMutation;
    };

    AnchorLibraryOperationResult applyManagedMutation(const LibraryBatchMutation &mutation,
                                                       const LibraryBatchMutation &undoMutation,
                                                       int affectedCount,
                                                       const QString &message,
                                                       bool recordHistory = true);
    QList<Resource> allResources(bool includeDeleted = true) const;
    void appendHistory(const QString &action,
                       int affectedCount,
                       const LibraryBatchMutation &undoMutation);

    ILibraryRepository &repository_;
    QList<HistoryRecord> history_;
    int nextHistoryId_ = 1;
    quint64 observedContentRevision_ = 0;
};

} // namespace Pinloom
