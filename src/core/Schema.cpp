#include "pinloom/core/Schema.h"

namespace Pinloom {

QStringList Schema::sqliteFts5Draft()
{
    return {
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
                       "PRIMARY KEY (resource_id, tag)"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS resource_aliases ("
                       "resource_id TEXT NOT NULL,"
                       "alias TEXT NOT NULL,"
                       "PRIMARY KEY (resource_id, alias)"
                       ");"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS anchors ("
                       "id TEXT PRIMARY KEY,"
                       "resource_id TEXT NOT NULL,"
                       "type TEXT NOT NULL,"
                       "target TEXT,"
                       "line INTEGER,"
                       "page INTEGER,"
                       "x REAL,"
                       "y REAL,"
                       "width REAL,"
                       "height REAL"
                       ");"),
        QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS resource_fts "
                       "USING fts5(title, aliases, tags, location, content, "
                       "content='resources', content_rowid='rowid');")
    };
}

} // namespace Pinloom
