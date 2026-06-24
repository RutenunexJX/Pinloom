#include "pinloom/core/Schema.h"

namespace Pinloom {

int Schema::currentVersion()
{
    return 1;
}

QStringList Schema::sqliteFts5Draft()
{
    return {
        QStringLiteral("CREATE TABLE IF NOT EXISTS schema_migrations ("
                       "version INTEGER PRIMARY KEY,"
                       "name TEXT NOT NULL,"
                       "applied_at TEXT NOT NULL"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS resources ("
                       "id TEXT PRIMARY KEY,"
                       "kind TEXT NOT NULL,"
                       "title TEXT NOT NULL,"
                       "location TEXT NOT NULL,"
                       "updated_at TEXT"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS resource_tags ("
                       "resource_id TEXT NOT NULL,"
                       "tag TEXT NOT NULL,"
                       "PRIMARY KEY (resource_id, tag),"
                       "FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS resource_aliases ("
                       "resource_id TEXT NOT NULL,"
                       "alias TEXT NOT NULL,"
                       "PRIMARY KEY (resource_id, alias),"
                       "FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS anchors ("
                       "resource_id TEXT NOT NULL,"
                       "anchor_order INTEGER NOT NULL,"
                       "type TEXT NOT NULL,"
                       "target TEXT,"
                       "line INTEGER,"
                       "page INTEGER,"
                       "x REAL,"
                       "y REAL,"
                       "width REAL,"
                       "height REAL,"
                       "PRIMARY KEY (resource_id, anchor_order),"
                       "FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS resource_fts "
                       "USING fts5(resource_id UNINDEXED, title, aliases, tags, location, content);")
    };
}

} // namespace Pinloom
