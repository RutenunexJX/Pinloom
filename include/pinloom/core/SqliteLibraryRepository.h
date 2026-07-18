#pragma once

#include "pinloom/core/LibraryRepository.h"

#include <QHash>
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
    bool applyBatch(const LibraryBatchMutation &mutation) override;

    bool recordResourceOpen(const QString &resourceId) override;
    bool setResourcePinned(const QString &resourceId, bool pinned) override;
    std::optional<ResourceUsage> resourceUsage(const QString &resourceId) const override;
    bool recordAnchorOpen(const QString &resourceId, const Anchor &anchor) override;
    std::optional<AnchorUsage> anchorUsage(const QString &resourceId, const Anchor &anchor) const override;

    quint64 changeRevision() const override;
    quint64 contentRevision() const override;
    int addChangeListener(LibraryChangeListener listener) override;
    void removeChangeListener(int listenerId) override;

    QString databasePath() const;
    bool backupDatabase(const QString &destinationPath);
    bool restoreDatabase(const QString &sourcePath);

private:
    bool execute(const QString &sql);
    bool ensureAnchorLocatorColumns();
    bool ensureSoftDeleteColumns();
    bool migrateCanonicalAnchorSchema();
    bool recordMigration(int version, const QString &name);
    bool beginTransaction();
    bool commitTransaction();
    void rollbackTransaction();
    bool deleteResourcePermanently(const QString &resourceId);
    bool clearResourceTables();
    void notifyChange(LibraryChangeKind kind, const QStringList &resourceIds = {});
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
    QHash<int, LibraryChangeListener> listeners_;
    quint64 revision_ = 0;
    quint64 contentRevision_ = 0;
    int nextListenerId_ = 1;
    int transactionDepth_ = 0;
    int deferredChangeDepth_ = 0;
    QStringList deferredResourceIds_;
    LibraryChangeKind deferredChangeKind_ = LibraryChangeKind::Content;
};

} // namespace Pinloom
