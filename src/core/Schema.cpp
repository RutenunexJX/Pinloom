#include "pinloom/core/Schema.h"

namespace Pinloom {

int Schema::currentVersion()
{
    return 16;
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
                       "explicitly_retained INTEGER NOT NULL DEFAULT 0,"
                       "deleted INTEGER NOT NULL DEFAULT 0,"
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
        QStringLiteral("CREATE TABLE IF NOT EXISTS library_roots ("
                       "id TEXT PRIMARY KEY,"
                       "path TEXT NOT NULL UNIQUE,"
                       "display_name TEXT NOT NULL,"
                       "enabled INTEGER NOT NULL DEFAULT 1,"
                       "sync_root INTEGER NOT NULL DEFAULT 0,"
                       "ignored_directory_names TEXT,"
                       "updated_at TEXT"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS anchors ("
                       "resource_id TEXT NOT NULL,"
                       "anchor_order INTEGER NOT NULL,"
                       "id TEXT NOT NULL,"
                       "name TEXT,"
                       "target_app TEXT,"
                       "target_file TEXT,"
                       "target_uri TEXT,"
                       "locator_type TEXT,"
                       "locator_json TEXT,"
                       "aliases TEXT,"
                       "tags TEXT,"
                       "pinned INTEGER NOT NULL DEFAULT 0,"
                       "deleted INTEGER NOT NULL DEFAULT 0,"
                       "created_at TEXT,"
                       "updated_at TEXT,"
                       "used_at TEXT,"
                       "PRIMARY KEY (resource_id, id),"
                       "UNIQUE (resource_id, anchor_order),"
                       "FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE"
                       ");"),
        QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS resource_fts "
                       "USING fts5(resource_id UNINDEXED, title, aliases, tags, location, content);"),
        QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS anchor_fts "
                       "USING fts5(resource_id UNINDEXED, anchor_id UNINDEXED, locator_type, text);"),
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
                       "open_count INTEGER NOT NULL DEFAULT 0,"
                       "last_opened_at TEXT,"
                       "PRIMARY KEY (resource_id, anchor_key),"
                       "FOREIGN KEY (resource_id) REFERENCES resources(id) ON DELETE CASCADE"
                       ");")
    };
}

} // namespace Pinloom
