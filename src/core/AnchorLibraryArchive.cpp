#include "pinloom/core/AnchorLibraryArchive.h"

#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/GlobalIdentity.h"
#include "pinloom/core/SqliteLibraryRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <utility>

namespace Pinloom {

namespace {

AnchorLibraryOperationResult failedArchiveResult(const QString &message)
{
    return {false, 0, 0, message};
}

QString resourceKindName(ResourceKind kind)
{
    switch (kind) {
    case ResourceKind::File:
        return QStringLiteral("file");
    case ResourceKind::Folder:
        return QStringLiteral("folder");
    case ResourceKind::Pdf:
        return QStringLiteral("pdf");
    case ResourceKind::TextSnippet:
        return QStringLiteral("text_snippet");
    case ResourceKind::Url:
        return QStringLiteral("url");
    case ResourceKind::Note:
        return QStringLiteral("note");
    case ResourceKind::ManualAnchor:
        return QStringLiteral("manual_anchor");
    case ResourceKind::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

ResourceKind resourceKindFromName(const QString &name)
{
    const QString value = name.trimmed().toLower();
    if (value == QLatin1String("file")) return ResourceKind::File;
    if (value == QLatin1String("folder")) return ResourceKind::Folder;
    if (value == QLatin1String("pdf")) return ResourceKind::Pdf;
    if (value == QLatin1String("text_snippet")) return ResourceKind::TextSnippet;
    if (value == QLatin1String("url")) return ResourceKind::Url;
    if (value == QLatin1String("note")) return ResourceKind::Note;
    if (value == QLatin1String("manual_anchor")) return ResourceKind::ManualAnchor;
    return ResourceKind::Unknown;
}

QJsonArray stringsToJson(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QStringList stringsFromJson(const QJsonValue &value, bool identityValues = false)
{
    QStringList values;
    if (!value.isArray()) {
        return values;
    }
    for (const QJsonValue &item : value.toArray()) {
        const QString text = item.toString();
        if (!text.trimmed().isEmpty()
            && (identityValues || !values.contains(text.trimmed(), Qt::CaseInsensitive))) {
            values.append(identityValues ? text : text.trimmed());
        }
    }
    return values;
}

QJsonObject anchorToJson(const Anchor &anchor)
{
    return {{QStringLiteral("id"), anchor.id},
            {QStringLiteral("name"), anchor.name},
            {QStringLiteral("targetApp"), anchor.targetApp},
            {QStringLiteral("targetFile"), anchor.targetFile},
            {QStringLiteral("targetUri"), anchor.targetUri},
            {QStringLiteral("locatorType"), anchor.locatorType},
            {QStringLiteral("locatorJson"), anchor.locatorJson},
            {QStringLiteral("aliases"), stringsToJson(anchor.aliases)},
            {QStringLiteral("tags"), stringsToJson(anchor.tags)},
            {QStringLiteral("pinned"), anchor.pinned},
            {QStringLiteral("deleted"), anchor.deleted},
            {QStringLiteral("createdAt"), anchor.createdAt.toUTC().toString(Qt::ISODateWithMs)},
            {QStringLiteral("updatedAt"), anchor.updatedAt.toUTC().toString(Qt::ISODateWithMs)},
            {QStringLiteral("usedAt"), anchor.usedAt.toUTC().toString(Qt::ISODateWithMs)}};
}

Anchor anchorFromJson(const QJsonObject &object)
{
    Anchor anchor;
    anchor.id = object.value(QStringLiteral("id")).toString();
    anchor.name = object.value(QStringLiteral("name")).toString();
    anchor.targetApp = object.value(QStringLiteral("targetApp")).toString();
    anchor.targetFile = object.value(QStringLiteral("targetFile")).toString();
    anchor.targetUri = object.value(QStringLiteral("targetUri")).toString();
    anchor.locatorType = object.value(QStringLiteral("locatorType")).toString();
    anchor.locatorJson = object.value(QStringLiteral("locatorJson")).toString();
    anchor.aliases = stringsFromJson(object.value(QStringLiteral("aliases")), true);
    anchor.tags = stringsFromJson(object.value(QStringLiteral("tags")));
    anchor.pinned = object.value(QStringLiteral("pinned")).toBool();
    anchor.deleted = object.value(QStringLiteral("deleted")).toBool();
    anchor.createdAt = QDateTime::fromString(object.value(QStringLiteral("createdAt")).toString(),
                                             Qt::ISODate);
    anchor.updatedAt = QDateTime::fromString(object.value(QStringLiteral("updatedAt")).toString(),
                                             Qt::ISODate);
    anchor.usedAt = QDateTime::fromString(object.value(QStringLiteral("usedAt")).toString(),
                                          Qt::ISODate);
    return anchor;
}

QJsonObject resourceToJson(const Resource &resource)
{
    QJsonArray anchors;
    for (const Anchor &anchor : resource.anchors) {
        anchors.append(anchorToJson(anchor));
    }
    return {{QStringLiteral("id"), resource.id},
            {QStringLiteral("kind"), resourceKindName(resource.kind)},
            {QStringLiteral("title"), resource.title},
            {QStringLiteral("location"), resource.location},
            {QStringLiteral("tags"), stringsToJson(resource.tags)},
            {QStringLiteral("aliases"), stringsToJson(resource.aliases)},
            {QStringLiteral("anchors"), anchors},
            {QStringLiteral("content"), resource.content},
            {QStringLiteral("explicitlyRetained"), resource.explicitlyRetained},
            {QStringLiteral("deleted"), resource.deleted},
            {QStringLiteral("updatedAt"), resource.updatedAt.toUTC().toString(Qt::ISODateWithMs)}};
}

std::optional<Resource> resourceFromJson(const QJsonValue &value, QString *error)
{
    if (!value.isObject()) {
        *error = QStringLiteral("Archive contains a non-object resource");
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    Resource resource;
    resource.id = object.value(QStringLiteral("id")).toString().trimmed();
    if (resource.id.isEmpty()) {
        *error = QStringLiteral("Archive resource id is missing");
        return std::nullopt;
    }
    resource.kind = resourceKindFromName(object.value(QStringLiteral("kind")).toString());
    resource.title = object.value(QStringLiteral("title")).toString();
    resource.location = object.value(QStringLiteral("location")).toString();
    resource.tags = stringsFromJson(object.value(QStringLiteral("tags")));
    resource.aliases = stringsFromJson(object.value(QStringLiteral("aliases")), true);
    resource.content = object.value(QStringLiteral("content")).toString();
    resource.explicitlyRetained = object.value(QStringLiteral("explicitlyRetained")).toBool();
    resource.deleted = object.value(QStringLiteral("deleted")).toBool();
    resource.updatedAt = QDateTime::fromString(object.value(QStringLiteral("updatedAt")).toString(),
                                               Qt::ISODate);
    const QJsonValue anchorsValue = object.value(QStringLiteral("anchors"));
    if (!anchorsValue.isArray()) {
        *error = QStringLiteral("Archive resource anchors must be an array");
        return std::nullopt;
    }
    QSet<QString> anchorKeys;
    for (const QJsonValue &anchorValue : anchorsValue.toArray()) {
        if (!anchorValue.isObject()) {
            *error = QStringLiteral("Archive contains a non-object anchor");
            return std::nullopt;
        }
        Anchor anchor = anchorFromJson(anchorValue.toObject());
        if (!hasAnchorIdentity(anchor)) {
            *error = QStringLiteral("Archive anchor identity is missing");
            return std::nullopt;
        }
        const QString key = anchorIdentityKey(anchor);
        if (anchorKeys.contains(key)) {
            *error = QStringLiteral("Archive contains duplicate anchor identities");
            return std::nullopt;
        }
        anchorKeys.insert(key);
        resource.anchors.append(anchor);
    }
    return resource;
}

void mergeStringLists(QStringList &target,
                      const QStringList &source,
                      bool identityValues = false)
{
    QSet<QString> preexistingIdentityKeys;
    if (identityValues) {
        for (const QString &value : std::as_const(target)) {
            preexistingIdentityKeys.insert(normalizedGlobalIdentity(value));
        }
    }
    for (const QString &value : source) {
        if (value.trimmed().isEmpty()) {
            continue;
        }
        if (identityValues) {
            if (!preexistingIdentityKeys.contains(normalizedGlobalIdentity(value))) {
                target.append(value);
            }
        } else if (!target.contains(value.trimmed(), Qt::CaseInsensitive)) {
            target.append(value.trimmed());
        }
    }
}

Resource mergeImportedResource(const Resource &current, const Resource &imported)
{
    Resource merged = current;
    if (!imported.title.trimmed().isEmpty()) merged.title = imported.title;
    if (imported.kind != ResourceKind::Unknown) merged.kind = imported.kind;
    if (!imported.location.trimmed().isEmpty()) merged.location = imported.location;
    if (!imported.content.isEmpty()) merged.content = imported.content;
    merged.explicitlyRetained = current.explicitlyRetained || imported.explicitlyRetained;
    merged.deleted = imported.deleted;
    mergeStringLists(merged.tags, imported.tags);
    mergeStringLists(merged.aliases, imported.aliases, true);
    for (const Anchor &importedAnchor : imported.anchors) {
        auto existing = std::find_if(merged.anchors.begin(), merged.anchors.end(),
                                     [&importedAnchor](const Anchor &anchor) {
                                         return sameAnchorIdentity(anchor, importedAnchor);
                                     });
        if (existing == merged.anchors.end()) {
            merged.anchors.append(importedAnchor);
        } else {
            *existing = importedAnchor;
        }
    }
    merged.updatedAt = QDateTime::currentDateTimeUtc();
    return merged;
}

} // namespace

AnchorLibraryArchiveService::AnchorLibraryArchiveService(ILibraryRepository &repository,
                                                         QString safetyBackupDirectory)
    : repository_(repository)
    , safetyBackupDirectory_(std::move(safetyBackupDirectory))
{
}

AnchorLibraryOperationResult AnchorLibraryArchiveService::exportJson(const QString &filePath) const
{
    if (filePath.trimmed().isEmpty()) {
        return failedArchiveResult(QStringLiteral("Archive file path is required"));
    }
    const QString path = QFileInfo(filePath).absoluteFilePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return failedArchiveResult(QStringLiteral("Unable to create the archive directory"));
    }
    QJsonArray resources;
    const QList<Resource> library = allResources();
    for (const Resource &resource : library) {
        resources.append(resourceToJson(resource));
    }
    const QJsonObject root{{QStringLiteral("format"), QStringLiteral("pinloom-anchor-library")},
                           {QStringLiteral("version"), 1},
                           {QStringLiteral("exportedAt"),
                            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                           {QStringLiteral("resources"), resources}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        return failedArchiveResult(QStringLiteral("Unable to write the Anchor Library JSON archive"));
    }
    return {true,
            static_cast<int>(library.size()),
            0,
            QStringLiteral("Exported %1 resource(s)").arg(library.size())};
}

AnchorLibraryOperationResult AnchorLibraryArchiveService::importJson(
    const QString &filePath,
    AnchorLibraryImportMode mode)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return failedArchiveResult(QStringLiteral("Unable to open the Anchor Library JSON archive"));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return failedArchiveResult(QStringLiteral("Anchor Library archive JSON is invalid"));
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("pinloom-anchor-library")
        || root.value(QStringLiteral("version")).toInt() != 1
        || !root.value(QStringLiteral("resources")).isArray()) {
        return failedArchiveResult(QStringLiteral("Anchor Library archive format is unsupported"));
    }

    QList<Resource> imported;
    QSet<QString> resourceIds;
    QString validationError;
    for (const QJsonValue &value : root.value(QStringLiteral("resources")).toArray()) {
        const std::optional<Resource> resource = resourceFromJson(value, &validationError);
        if (!resource.has_value()) {
            return failedArchiveResult(validationError);
        }
        if (resourceIds.contains(resource->id)) {
            return failedArchiveResult(QStringLiteral("Archive contains duplicate resource ids"));
        }
        resourceIds.insert(resource->id);
        imported.append(resource.value());
    }

    if (SqliteLibraryRepository *sqlite = dynamic_cast<SqliteLibraryRepository *>(&repository_)) {
        const QString backupDirectory = safetyBackupDirectory(*sqlite);
        const AnchorLibraryOperationResult backup = createAutomaticBackup(backupDirectory, 10);
        if (!backup.success) {
            return failedArchiveResult(QStringLiteral("Import canceled because the safety backup failed: %1")
                                           .arg(backup.message));
        }
    }

    LibraryBatchMutation mutation;
    mutation.clearExistingResources = mode == AnchorLibraryImportMode::Replace;
    if (mode == AnchorLibraryImportMode::Replace) {
        mutation.upserts = imported;
    } else {
        for (const Resource &resource : imported) {
            const std::optional<Resource> current = repository_.findResource(resource.id);
            mutation.upserts.append(current.has_value()
                                        ? mergeImportedResource(current.value(), resource)
                                        : resource);
        }
    }
    if (!repository_.applyBatch(mutation)) {
        return failedArchiveResult(repository_.lastError().trimmed().isEmpty()
                                       ? QStringLiteral("Unable to import the archive atomically")
                                       : repository_.lastError());
    }
    return {true,
            static_cast<int>(imported.size()),
            0,
            QStringLiteral("Imported %1 resource(s)").arg(imported.size())};
}

AnchorLibraryOperationResult AnchorLibraryArchiveService::backupDatabase(const QString &filePath) const
{
    if (filePath.trimmed().isEmpty()) {
        return failedArchiveResult(QStringLiteral("Database backup path is required"));
    }
    SqliteLibraryRepository *sqlite = dynamic_cast<SqliteLibraryRepository *>(&repository_);
    if (!sqlite) {
        return failedArchiveResult(QStringLiteral("Database backup requires the SQLite repository"));
    }
    if (!sqlite->backupDatabase(filePath)) {
        return failedArchiveResult(sqlite->lastError());
    }
    return {true, 1, 0, QStringLiteral("Created SQLite backup")};
}

AnchorLibraryOperationResult AnchorLibraryArchiveService::restoreDatabase(const QString &filePath)
{
    if (filePath.trimmed().isEmpty()) {
        return failedArchiveResult(QStringLiteral("Database restore path is required"));
    }
    SqliteLibraryRepository *sqlite = dynamic_cast<SqliteLibraryRepository *>(&repository_);
    if (!sqlite) {
        return failedArchiveResult(QStringLiteral("Database restore requires the SQLite repository"));
    }
    const QString backupDirectory = safetyBackupDirectory(*sqlite);
    const AnchorLibraryOperationResult safetyBackup = createAutomaticBackup(backupDirectory, 10);
    if (!safetyBackup.success) {
        return failedArchiveResult(QStringLiteral("Restore canceled because the safety backup failed: %1")
                                       .arg(safetyBackup.message));
    }
    if (!sqlite->restoreDatabase(filePath)) {
        return failedArchiveResult(sqlite->lastError());
    }
    return {true, 1, 0, QStringLiteral("Restored the SQLite database")};
}

AnchorLibraryOperationResult AnchorLibraryArchiveService::createAutomaticBackup(
    const QString &directoryPath,
    int retainedBackupCount) const
{
    if (directoryPath.trimmed().isEmpty()) {
        return failedArchiveResult(QStringLiteral("Automatic backup directory is required"));
    }
    if (!QDir().mkpath(directoryPath)) {
        return failedArchiveResult(QStringLiteral("Unable to create the automatic backup directory"));
    }
    const QString backupPath = automaticBackupPath(directoryPath, QStringLiteral("pinloom-auto"));
    const AnchorLibraryOperationResult result = backupDatabase(backupPath);
    if (!result.success) {
        return result;
    }

    QDir directory(directoryPath);
    QFileInfoList backups = directory.entryInfoList({QStringLiteral("pinloom-auto-*.sqlite3")},
                                                    QDir::Files,
                                                    QDir::Time);
    const int retained = std::max(1, retainedBackupCount);
    for (int index = retained; index < backups.size(); ++index) {
        QFile::remove(backups.at(index).absoluteFilePath());
    }
    return {true, 1, 0, QStringLiteral("Created automatic SQLite backup")};
}

QList<Resource> AnchorLibraryArchiveService::allResources() const
{
    SearchQuery query;
    query.limit = 0;
    query.includeDeleted = true;
    QList<Resource> resources;
    QSet<QString> seen;
    for (const SearchResult &result : repository_.search(query)) {
        if (!seen.contains(result.resource.id)) {
            seen.insert(result.resource.id);
            resources.append(result.resource);
        }
    }
    std::sort(resources.begin(), resources.end(), [](const Resource &left, const Resource &right) {
        return left.id < right.id;
    });
    return resources;
}

QString AnchorLibraryArchiveService::automaticBackupPath(const QString &directoryPath,
                                                          const QString &prefix) const
{
    QDir directory(directoryPath);
    return directory.filePath(
        QStringLiteral("%1-%2.sqlite3")
            .arg(prefix,
                 QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))));
}

QString AnchorLibraryArchiveService::safetyBackupDirectory(
    const SqliteLibraryRepository &repository) const
{
    if (!safetyBackupDirectory_.trimmed().isEmpty()) {
        return QFileInfo(safetyBackupDirectory_).absoluteFilePath();
    }
    return QDir(QFileInfo(repository.databasePath()).absolutePath())
        .filePath(QStringLiteral("backups"));
}

} // namespace Pinloom
