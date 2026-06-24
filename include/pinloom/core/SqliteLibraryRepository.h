#pragma once

#include "pinloom/core/LibraryRepository.h"

#include <QSqlDatabase>

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

private:
    bool execute(const QString &sql);
    bool beginTransaction();
    bool commitTransaction();
    void rollbackTransaction();
    Resource hydrateResource(const QString &id) const;
    QStringList readStrings(const QString &table, const QString &column, const QString &resourceId) const;
    QList<Anchor> readAnchors(const QString &resourceId) const;
    void setLastError(const QString &message) const;

    QString connectionName_;
    QSqlDatabase database_;
    mutable QString lastError_;
};

} // namespace Pinloom
