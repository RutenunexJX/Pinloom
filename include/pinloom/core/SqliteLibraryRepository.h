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

    bool upsertLibraryRoot(const LibraryRoot &root) override;
    QList<LibraryRoot> libraryRoots() const override;
    std::optional<LibraryRoot> findLibraryRoot(const QString &id) const override;
    bool removeLibraryRoot(const QString &id) override;
    bool setLibraryRootEnabled(const QString &id, bool enabled) override;
    bool updateLibraryRootLastIndexedAt(const QString &id, const QDateTime &indexedAt) override;

private:
    bool execute(const QString &sql);
    bool recordMigration(int version, const QString &name);
    bool beginTransaction();
    bool commitTransaction();
    void rollbackTransaction();
    Resource hydrateResource(const QString &id) const;
    LibraryRoot hydrateLibraryRoot(QSqlQuery &query) const;
    QStringList readStrings(const QString &table, const QString &column, const QString &resourceId) const;
    QList<Anchor> readAnchors(const QString &resourceId) const;
    void setLastError(const QString &message) const;

    QString connectionName_;
    QSqlDatabase database_;
    mutable QString lastError_;
};

} // namespace Pinloom
