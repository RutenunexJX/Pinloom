#pragma once

#include "pinloom/core/LibraryRepository.h"

#include <QSqlDatabase>

class QSqlQuery;

namespace Pinloom {

class SqliteLibraryRepository final : public ILibraryRepository {
public:
    SqliteLibraryRepository();
    ~SqliteLibraryRepository() override;

    bool open(const QString &path);
    bool initialize();
    bool isOpen() const;
    QString lastError() const;

    bool upsertResource(const Resource &resource) override;
    std::optional<Resource> findResource(const QString &id) const override;
    QList<SearchResult> search(const SearchQuery &query) const override;
    bool clearResources() override;

    bool upsertResourceRelation(const ResourceRelation &relation) override;
    QList<ResourceRelation> resourceRelations(const QString &resourceId) const override;
    QList<ResourceRelation> allResourceRelations() const override;
    bool removeResourceRelation(const QString &sourceResourceId, const QString &targetResourceId, const QString &label) override;

    bool recordResourceOpen(const QString &resourceId) override;
    bool setResourcePinned(const QString &resourceId, bool pinned) override;
    std::optional<ResourceUsage> resourceUsage(const QString &resourceId) const override;
    bool recordAnchorOpen(const QString &resourceId, const Anchor &anchor) override;
    std::optional<AnchorUsage> anchorUsage(const QString &resourceId, const Anchor &anchor) const override;

    bool upsertLibraryRoot(const LibraryRoot &root) override;
    QList<LibraryRoot> libraryRoots() const override;
    std::optional<LibraryRoot> findLibraryRoot(const QString &id) const override;
    bool removeLibraryRoot(const QString &id) override;
    bool setLibraryRootEnabled(const QString &id, bool enabled) override;
    bool setLibraryRootPinned(const QString &id, bool pinned) override;
    bool updateLibraryRootLastIndexedAt(const QString &id, const QDateTime &indexedAt) override;

private:
    bool execute(const QString &sql);
    bool ensureLibraryRootPinnedColumn();
    bool ensureAnchorLocatorColumns();
    bool recordMigration(int version, const QString &name);
    bool beginTransaction();
    bool commitTransaction();
    void rollbackTransaction();
    Resource hydrateResource(const QString &id) const;
    LibraryRoot hydrateLibraryRoot(QSqlQuery &query) const;
    ResourceRelation hydrateResourceRelation(QSqlQuery &query) const;
    ResourceUsage hydrateResourceUsage(QSqlQuery &query) const;
    AnchorUsage hydrateAnchorUsage(QSqlQuery &query) const;
    void applyRankingSignals(SearchResult &result, const SearchQuery &query) const;
    QStringList readStrings(const QString &table, const QString &column, const QString &resourceId) const;
    QList<Anchor> readAnchors(const QString &resourceId) const;
    void setLastError(const QString &message) const;

    QString connectionName_;
    QSqlDatabase database_;
    mutable QString lastError_;
};

} // namespace Pinloom
