#include "pinloom/core/Schema.h"

namespace Pinloom {

int Schema::currentVersion()
{
    return 7;
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
                       "USING fts5(resource_id UNINDEXED, title, aliases, tags, location, content);"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS library_roots ("
                       "id TEXT PRIMARY KEY,"
                       "path TEXT NOT NULL UNIQUE,"
                       "display_name TEXT NOT NULL,"
                       "enabled INTEGER NOT NULL DEFAULT 1,"
                       "pinned INTEGER NOT NULL DEFAULT 0,"
                       "last_indexed_at TEXT"
                       ");"),
        QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS anchor_fts "
                       "USING fts5(resource_id UNINDEXED, anchor_order UNINDEXED, type, target);"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS resource_relations ("
                       "source_resource_id TEXT NOT NULL,"
                       "target_resource_id TEXT NOT NULL,"
                       "label TEXT NOT NULL,"
                       "note TEXT,"
                       "PRIMARY KEY (source_resource_id, target_resource_id, label),"
                       "FOREIGN KEY (source_resource_id) REFERENCES resources(id) ON DELETE CASCADE,"
                       "FOREIGN KEY (target_resource_id) REFERENCES resources(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS resource_usage ("
                       "resource_id TEXT PRIMARY KEY,"
                       "open_count INTEGER NOT NULL DEFAULT 0,"
                       "last_opened_at TEXT,"
                       "pinned INTEGER NOT NULL DEFAULT 0,"
                       "FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS anchor_usage ("
                       "resource_id TEXT NOT NULL,"
                       "anchor_key TEXT NOT NULL,"
                       "type TEXT NOT NULL,"
                       "target TEXT,"
                       "line INTEGER,"
                       "page INTEGER,"
                       "x REAL,"
                       "y REAL,"
                       "width REAL,"
                       "height REAL,"
                       "open_count INTEGER NOT NULL DEFAULT 0,"
                       "last_opened_at TEXT,"
                       "PRIMARY KEY (resource_id, anchor_key),"
                       "FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE"
                       ");")
    };
}

} // namespace Pinloom
