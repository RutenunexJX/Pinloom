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
    bool softDeleteResource(const QString &resourceId) override;
    bool restoreResource(const QString &resourceId) override;
    bool softDeleteAnchor(const QString &resourceId, const Anchor &anchor) override;
    bool restoreAnchor(const QString &resourceId, const Anchor &anchor) override;
    bool clearResources() override;

    bool recordResourceOpen(const QString &resourceId) override;
    bool setResourcePinned(const QString &resourceId, bool pinned) override;
    std::optional<ResourceUsage> resourceUsage(const QString &resourceId) const override;
    bool recordAnchorOpen(const QString &resourceId, const Anchor &anchor) override;
    std::optional<AnchorUsage> anchorUsage(const QString &resourceId, const Anchor &anchor) const override;

private:
    bool execute(const QString &sql);
    bool ensureAnchorLocatorColumns();
    bool ensureSoftDeleteColumns();
    bool migrateCanonicalAnchorSchema();
    bool recordMigration(int version, const QString &name);
    bool beginTransaction();
    bool commitTransaction();
    void rollbackTransaction();
    Resource hydrateResource(const QString &id) const;
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
