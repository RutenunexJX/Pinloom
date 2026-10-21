#include "pinloom/application/CommandService.h"
#include "pinloom/core/AnchorLibraryArchive.h"
#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/AnchorTarget.h"
#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/core/InboxFileCapture.h"
#include "pinloom/core/Version.h"
#include "pinloom/clip/ClipSearch.h"
#include "pinloom/clip/ClipAction.h"
#include "pinloom/clip/ObsidianClipStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <stdexcept>

namespace Pinloom {
namespace {
struct ApiFailure { QJsonObject response; };
void fail(const QString &code, const QString &message, int exitCode = 2,
          const QJsonObject &details = {})
{
    throw ApiFailure{commandError(code, message, exitCode, details)};
}
QString digest(const QJsonValue &value)
{
    const QByteArray data = value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                                          : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QStringList strings(const QJsonValue &value)
{
    QStringList list;
    for (const auto &item : value.toArray()) list.append(item.toString());
    return list;
}
QJsonArray array(const QStringList &values) { return QJsonArray::fromStringList(values); }
QJsonObject field(const char *type, const QString &description = {})
{
    QJsonObject result{{"type", type}};
    if (!description.isEmpty()) result.insert("description", description);
    if (QString::fromLatin1(type) == "string") result.insert("maxLength", 262144);
    if (QString::fromLatin1(type) == "array") {
        result.insert("items", QJsonObject{{"type", "string"}, {"maxLength", 1024}});
        result.insert("maxItems", 256);
    }
    return result;
}
QJsonObject choice(const QStringList &values)
{
    auto result = field("string");
    result.insert("enum", array(values));
    return result;
}
struct Spec { bool write = false; bool gui = false; QJsonObject properties; QStringList required; };
const QMap<QString, Spec> &specs()
{
    static const auto value = [] {
        QMap<QString, Spec> result;
        const auto s = field("string"), b = field("boolean"), a = field("array");
        const auto id = field("string", "Stable ID from a previous response");
        const auto version = field("string", "Exact version returned by get/status; 'absent' only for new objects");
        const QJsonObject metadata{{"name", s}, {"aliases", a}, {"tags", a}, {"pinned", b}};
        QJsonObject locatorSchema = field("object", "Structured locator, including type; existing adapter semantics apply");
        QJsonObject anchor = metadata;
        anchor.insert("targetApp", s); anchor.insert("targetFile", s); anchor.insert("targetUri", s);
        anchor.insert("locator", locatorSchema);
        auto add = [&](const QString &name, QJsonObject properties = {}, QStringList required = {},
                       bool write = false, bool gui = false) {
            if (write && name != "session.close" && name != "operation.cancel") {
                properties.insert("ifVersion", version); required.append("ifVersion");
                properties.insert("dryRun", b);
            }
            result.insert(name, {write, gui, properties, required});
        };
        add("capabilities"); add("schema"); add("session.status"); add("library.init");
        QJsonObject search{{"query", s}, {"limit", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}},
                           {"scope", choice({"active", "trash", "all"})},
                           {"kind", choice({"all", "anchors", "files", "clips"})}, {"tags", a}};
        add("search", search);
        for (const auto &method : {"resource.get", "resource.delete", "resource.restore", "resource.purge"})
            add(method, {{"id", id}}, {"id"}, QString::fromLatin1(method) != "resource.get");
        auto resourceMetadata = metadata;
        resourceMetadata.remove("name"); resourceMetadata.insert("title", s); resourceMetadata.insert("id", id);
        add("resource.update", resourceMetadata, {"id"}, true);
        auto create = anchor;
        create.insert("resourceId", id); create.insert("id", id);
        create.insert("location", s); create.insert("resourceTitle", s);
        create.insert("resourceKind", choice({"file", "folder", "pdf", "url", "manual_anchor"}));
        add("anchor.create", create, {"name", "locator"}, true);
        anchor.insert("id", id); anchor.insert("resourceId", id);
        add("anchor.update", anchor, {"id", "resourceId"}, true);
        for (const auto &method : {"anchor.get", "anchor.delete", "anchor.restore", "anchor.purge", "anchor.validate"})
            add(method, {{"id", id}, {"resourceId", id}}, {"id", "resourceId"},
                QString::fromLatin1(method) != "anchor.get" && QString::fromLatin1(method) != "anchor.validate");
        for (const auto &method : {"entry.resolve", "entry.open"})
            add(method, {{"resourceId", id}, {"anchorId", id}}, {"resourceId"}, false,
                QString::fromLatin1(method) == "entry.open");
        auto clip = metadata;
        clip.insert("text", s); clip.insert("id", id);
        clip.insert("action", choice({"insert_text", "open_web_url"}));
        add("clip.create", clip, {"name", "text"}, true);
        add("clip.update", clip, {"id"}, true);
        add("history.promote", clip, {"id", "name"}, true);
        for (const auto &method : {"clip.get", "clip.delete", "clip.restore", "clip.purge"})
            add(method, {{"id", id}}, {"id"}, QString::fromLatin1(method) != "clip.get");
        add("history.list", {{"limit", search["limit"]}});
        add("history.prune", {{"keep", QJsonObject{{"type", "integer"}, {"minimum", 0}, {"maximum", 1000}}},
                              {"ttlSeconds", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 31536000}}}},
                              {"keep", "ttlSeconds"}, true);
        auto inbox = metadata;
        inbox.insert("path", s); inbox.insert("mode", choice({"link", "copy", "move"})); inbox.insert("destination", s);
        add("inbox.add", inbox, {"path"}, true);
        add("root.list");
        QJsonObject root{{"path", s}, {"displayName", s}, {"enabled", b}, {"ignoredDirectoryNames", a}};
        add("root.register", root, {"path"}, true);
        root.remove("path"); root.insert("id", id);
        add("root.update", root, {"id"}, true);
        add("root.remove", {{"id", id}}, {"id"}, true);
        auto rootQuery = search;
        rootQuery.remove("kind"); rootQuery.insert("id", id);
        add("root.query", rootQuery, {"id"});
        add("root.files", {{"id", id}, {"relativePath", s}, {"limit", search["limit"]}}, {"id"});
        add("library.export", {{"path", s}, {"overwrite", b}, {"dryRun", b}}, {"path"});
        add("library.import", {{"path", s}, {"mode", choice({"merge", "replace"})}}, {"path", "mode"}, true);
        const QJsonObject view{{"view", choice({"anchors", "anchors_trash", "clips", "history", "trash", "roots", "command"})},
                               {"query", s}, {"resourceId", id}, {"anchorId", id}, {"clipId", id}};
        add("view.open", view, {"view"}, false, true);
        add("view.inspect", {}, {}, false, true);
        add("session.close", {}, {}, true, true);
        add("operation.get", {{"id", id}}, {"id"}, false, true);
        add("operation.cancel", {{"id", id}}, {"id"}, true, true);
        return result;
    }();
    return value;
}
void validateParams(const Spec &spec, const QJsonObject &params)
{
    for (const auto &name : spec.required)
        if (!params.contains(name)) fail("invalid_argument", "Missing parameter: " + name);
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (!spec.properties.contains(it.key())) fail("invalid_argument", "Unknown parameter: " + it.key());
        const auto property = spec.properties[it.key()].toObject();
        const auto type = property["type"].toString();
        const auto v = it.value();
        bool valid = type == "string" ? v.isString() : type == "boolean" ? v.isBool()
            : type == "array" ? v.isArray() : type == "object" ? v.isObject()
            : v.isDouble() && v.toDouble() == v.toInt(-2147483647);
        if (!valid) fail("invalid_argument", "Wrong type for parameter: " + it.key());
        if (type == "string" && v.toString().size() > 262144) fail("input_limit", "String is too long");
        if (property.contains("enum") && !property["enum"].toArray().contains(v))
            fail("invalid_argument", "Unsupported value for parameter: " + it.key());
        if (type == "integer" && (v.toInt() < property["minimum"].toInt() || v.toInt() > property["maximum"].toInt()))
            fail("invalid_argument", "Out of range: " + it.key());
        if (type == "array") {
            if (v.toArray().size() > 256) fail("input_limit", "Too many array items");
            for (const auto &item : v.toArray())
                if (!item.isString() || item.toString().size() > 1024) fail("invalid_argument", "Expected short strings: " + it.key());
        }
    }
}
void precondition(const QJsonObject &params, const QString &current)
{
    if (params["ifVersion"].toString() != current)
        fail("version_conflict", "Object changed or the supplied version is incorrect", 4,
             {{"currentVersion", current}});
}
QString absolutePath(const QString &value, const QString &base)
{
    if (value.trimmed().isEmpty()) fail("invalid_argument", "Path must not be empty");
    return QDir::cleanPath(QFileInfo(QDir(base), value).absoluteFilePath());
}
QList<Resource> resources(ILibraryRepository &repository)
{
    SearchQuery query; query.includeDeleted = true; query.limit = 0;
    QSet<QString> seen;
    QList<Resource> result;
    for (const auto &hit : repository.search(query)) {
        if (seen.contains(hit.resource.id)) continue;
        seen.insert(hit.resource.id);
        const auto resource = repository.findResource(hit.resource.id);
        if (resource) result.append(*resource);
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return result;
}
QJsonObject rootJson(const LibraryRoot &root)
{
    return {{"id", root.id}, {"path", root.path}, {"displayName", root.displayName}, {"enabled", root.enabled},
            {"syncRoot", root.syncRoot}, {"ignoredDirectoryNames", array(root.ignoredDirectoryNames)},
            {"updatedAt", root.updatedAt.toUTC().toString(Qt::ISODateWithMs)}};
}
QJsonObject versioned(QJsonObject object)
{
    object.insert("version", digest(object)); return object;
}
struct PreviewLibrary {
    SharedInMemoryGlobalIdentityRegistry identities = createInMemoryGlobalIdentityRegistry();
    InMemoryLibraryRepository library{identities};
    InMemoryClipRepository clips{identities};
    ObsidianClipStore store;
    PersistentClipService persistent{clips, store};
    PreviewLibrary(ILibraryRepository &source, const QList<Clip> &saved)
    {
        const auto snapshot = resources(source);
        if (!library.applyBatch({snapshot})) fail("storage_error", library.lastError(), 6);
        for (const auto &root : source.libraryRoots()) library.upsertLibraryRoot(root);
        for (const auto &item : snapshot)
            if (const auto usage = source.resourceUsage(item.id)) library.setResourcePinned(item.id, usage->pinned);
        for (auto clip : saved) {
            if (clip.state == ClipState::Temporary) continue;
            // Only the preview repository is local; no archive files are written.
            clip.storageBackend = ClipStorageBackend::Local;
            if (!clips.upsertPersistentClip(clip)) fail("identity_conflict", clips.lastError(), 4);
        }
    }
};
void checkResult(const AnchorLibraryOperationResult &result)
{
    if (!result.success) fail("operation_failed", result.message, 6);
}
void applyAnchorPatch(Anchor &anchor, const QJsonObject &params, const QString &base)
{
    if (params.contains("name")) anchor.name = params["name"].toString();
    if (params.contains("aliases")) anchor.aliases = strings(params["aliases"]);
    if (params.contains("tags")) anchor.tags = strings(params["tags"]);
    if (params.contains("pinned")) anchor.pinned = params["pinned"].toBool();
    if (params.contains("targetApp")) anchor.targetApp = params["targetApp"].toString();
    if (params.contains("targetFile")) {
        const auto file = params["targetFile"].toString();
        anchor.targetFile = file.isEmpty() ? QString() : absolutePath(file, base);
    }
    if (params.contains("targetUri")) {
        anchor.targetUri = params["targetUri"].toString();
        const QUrl url(anchor.targetUri, QUrl::StrictMode);
        if (!anchor.targetUri.isEmpty() && (!url.isValid()
            || (!url.isLocalFile() && url.scheme() != "http" && url.scheme() != "https")))
            fail("invalid_argument", "targetUri must be an absolute file, HTTP or HTTPS URI");
    }
    if (params.contains("locator")) {
        auto locator = params["locator"].toObject();
        if (!locator["type"].isString() || locator["type"].toString().trimmed().isEmpty())
            fail("invalid_locator", "locator.type is required");
        anchor.locatorType = locator["type"].toString();
        anchor.locatorJson = QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));
    }
    if (anchor.name.trimmed().isEmpty()) fail("invalid_argument", "Anchor name is required");
}
Anchor findAnchor(const Resource &resource, const QString &id)
{
    for (const auto &anchor : resource.anchors) if (anchor.id == id) return anchor;
    fail("not_found", "Anchor ID was not found in the specified resource", 3);
    return {};
}
} // namespace

QJsonObject commandError(const QString &code, const QString &message, int exitCode, const QJsonObject &details)
{
    return {{"protocol", "pinloom-cli/v1"}, {"ok", false}, {"exitCode", exitCode},
            {"error", QJsonObject{{"code", code}, {"message", message}, {"details", details}}}};
}
QString canonicalLibraryPath(const QString &path)
{
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    return QDir::fromNativeSeparators(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}
QString libraryCommandEndpoint(const QString &path)
{
    auto canonical = canonicalLibraryPath(path);
#ifdef Q_OS_WIN
    canonical = canonical.toCaseFolded();
#endif
    return "pinloom-cli-v1-" + QString::fromLatin1(
        QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256).toHex().left(40));
}
QJsonObject commandCapabilities()
{
    QJsonArray methods;
    for (auto it = specs().begin(); it != specs().end(); ++it)
        methods.append(QJsonObject{{"method", it.key()},
            {"writes", it->write || it.key() == "library.init" || it.key() == "library.export"}, {"requiresGui", it->gui},
            {"params", QJsonObject{{"type", "object"}, {"properties", it->properties},
                                    {"required", array(it->required)}, {"additionalProperties", false}}}});
    return {{"protocol", "pinloom-cli/v1"}, {"productVersion", pinloomVersion()}, {"methods", methods},
            {"limits", QJsonObject{{"requestBytes", CommandMaxRequestBytes}, {"responseBytes", CommandMaxResponseBytes},
                                     {"archiveBytes", CommandMaxArchiveBytes}, {"listItems", 200}, {"clipTextBytes", 262144}}},
            {"exitCodes", QJsonObject{{"success", 0}, {"invalidInput", 2}, {"notFound", 3}, {"conflict", 4},
                                      {"sessionOrTransport", 5}, {"storageOrAdapter", 6}}},
            {"classification", "tags are the existing classification model"},
            {"pathBase", "Caller working directory; library path is always explicit"},
            {"dryRun", "No mutation. Versions still checked. External file permissions/adapters are not guaranteed by preview."},
            {"openStates", array({"queued", "running", "open_requested", "located", "failed", "cancelled"})},
            {"locatorTypes", array({"manual", "file.line", "zeroslack.source", "url.fragment", "text.heading", "text.block",
                                      "pdf.page", "pdf.region", "sumatrapdf.page", "sumatrapdf.rect", "sumatrapdf.search",
                                      "excel.range", "excel.name", "word.bookmark", "powerpoint.shape", "visio.shape"})}};
}
QJsonObject clipCommandJson(const Clip &clip)
{
    return {{"id", clip.id}, {"name", clip.name}, {"text", clip.text}, {"preview", clip.preview},
            {"aliases", array(clip.aliases)}, {"tags", array(clip.tags)}, {"pinned", clip.pinned},
            {"state", clip.state == ClipState::Temporary ? "history" : clip.state == ClipState::Saved ? "saved" : "trash"},
            {"action", clip.actionType == ClipActionType::InsertText ? "insert_text" : "open_web_url"},
            {"storage", clip.storageBackend == ClipStorageBackend::Local ? "local" : "obsidian"},
            {"sourceUri", clip.sourceUri}, {"createdAt", clip.createdAt.toUTC().toString(Qt::ISODateWithMs)},
            {"updatedAt", clip.updatedAt.toUTC().toString(Qt::ISODateWithMs)},
            {"expiresAt", clip.expiresAt.toUTC().toString(Qt::ISODateWithMs)}};
}
CommandService::CommandService(ILibraryRepository &library, SqliteClipRepository &clipRepository,
    PersistentClipService &clips, QString libraryPath, CommandSessionCallbacks callbacks, QObject *parent)
    : QObject(parent), library_(library), clipRepository_(clipRepository), clips_(clips),
      libraryPath_(canonicalLibraryPath(libraryPath)), sessionId_(newId()), callbacks_(std::move(callbacks)) {}

QString CommandService::resourceVersion(const Resource &resource) const
{
    auto object = resourceArchiveJson(resource);
    object.insert("pinned", library_.resourceUsage(resource.id).value_or(ResourceUsage{}).pinned);
    return digest(object);
}
QString CommandService::libraryVersion() const
{
    QJsonArray values;
    for (const auto &resource : resources(library_)) values.append(resourceVersion(resource));
    auto clips = clips_.clips();
    std::sort(clips.begin(), clips.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    for (const auto &clip : clips) values.append(clipCommandJson(clip));
    auto roots = library_.libraryRoots();
    std::sort(roots.begin(), roots.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    for (const auto &root : roots) values.append(rootJson(root));
    return digest(values);
}

void CommandService::reportOpenResult(const QString &resourceId, const QString &anchorId, const QJsonObject &result)
{
    for (auto &operation : operations_) {
        if (operation["resourceId"].toString() != resourceId || operation["anchorId"].toString() != anchorId
            || operation["state"].toString() != "running") continue;
        for (auto it = result.begin(); it != result.end(); ++it) operation.insert(it.key(), it.value());
    }
}

QJsonObject CommandService::execute(const QJsonObject &request)
{
    QJsonObject response;
    const auto method = request["method"].toString();
    try {
        if (request["protocol"].toString() != "pinloom-cli/v1") fail("protocol_error", "Unsupported protocol");
        if (libraryCommandEndpoint(request["library"].toString()) != libraryCommandEndpoint(libraryPath_))
            fail("session_mismatch", "Requested library does not match this session", 5);
        if (!specs().contains(method)) fail("unknown_method", "Use capabilities to discover commands");
        if (!request["params"].isObject()) fail("invalid_argument", "params must be an object");
        const auto spec = specs()[method];
        const auto params = request["params"].toObject();
        validateParams(spec, params);
        if (spec.gui && !callbacks_.writeBlockReason) fail("gui_required", "Open pinloom_app --library with this library first", 5);
        const auto block = callbacks_.writeBlockReason ? callbacks_.writeBlockReason() : QString();
        if (!block.isEmpty() && ((spec.write && method != "operation.cancel") || method == "view.open" || method == "entry.open"))
            fail("pending_edits", block, 4);
        const auto base = request["baseDirectory"].toString();
        if (!QDir::isAbsolutePath(base)) fail("invalid_argument", "baseDirectory must be absolute");
        auto result = dispatch(method, params, base);
        const bool libraryChanged = spec.write && method != "operation.cancel" && method != "session.close"
            && !params["dryRun"].toBool();
        const bool persisted = libraryChanged || (method == "library.export" && !params["dryRun"].toBool())
            || (method == "library.init" && !callbacks_.writeBlockReason);
        if (libraryChanged && callbacks_.refreshViews) callbacks_.refreshViews();
        response = {{"protocol", "pinloom-cli/v1"}, {"ok", true}, {"exitCode", 0}, {"result", result},
                    {"persisted", persisted}, {"guiAdopted", libraryChanged && bool(callbacks_.refreshViews)},
                    {"dryRun", params["dryRun"].toBool()}};
    } catch (const ApiFailure &failure) { response = failure.response; }
    response.insert("requestId", request["requestId"]);
    response.insert("library", libraryPath_);
    response.insert("session", QJsonObject{{"id", sessionId_}, {"mode", callbacks_.writeBlockReason ? "gui" : "headless"}});
    return response;
}

QJsonObject CommandService::dispatch(const QString &method, const QJsonObject &p, const QString &base)
{
    const bool dry = p["dryRun"].toBool();
    const auto id = p["id"].toString();
    const auto requireResource = [&](const QString &resourceId) {
        const auto resource = library_.findResource(resourceId);
        if (!resource) fail("not_found", "Resource ID was not found", 3);
        return *resource;
    };
    const auto resourceResult = [&](const Resource &resource) {
        auto object = resourceArchiveJson(resource);
        object.insert("pinned", library_.resourceUsage(resource.id).value_or(ResourceUsage{}).pinned);
        object.insert("version", resourceVersion(resource)); return object;
    };
    const auto anchorResult = [&](const Resource &resource, const Anchor &anchor) {
        auto object = anchorArchiveJson(anchor);
        object.insert("locator", anchorLocatorObject(anchor));
        object.insert("resourceId", resource.id); object.insert("version", resourceVersion(resource)); return object;
    };
    if (method == "capabilities" || method == "schema") return commandCapabilities();
    if (method == "session.status" || method == "library.init") {
        const auto block = callbacks_.writeBlockReason ? callbacks_.writeBlockReason() : QString();
        return {{"version", libraryVersion()}, {"writesBlocked", !block.isEmpty()}, {"blockReason", block},
                {"library", libraryPath_}, {"schema", 16}};
    }
    if (method == "view.open" || method == "view.inspect") return callbacks_.view(p, method == "view.open");
    if (method == "session.close") {
        QTimer::singleShot(100, this, [this] { if (callbacks_.close) callbacks_.close(); });
        return {{"state", "closing"}};
    }
    if (method.startsWith("operation.")) {
        if (!operations_.contains(id)) fail("not_found", "Operation does not exist in this session", 3);
        if (method == "operation.cancel") {
            if (operations_[id]["state"].toString() != "queued"
                && !(operations_[id]["state"].toString() == "running" && callbacks_.cancelOpen
                    && callbacks_.cancelOpen(operations_[id]["resourceId"].toString(), operations_[id]["anchorId"].toString())))
                fail("operation_conflict", "Only queued or active PDF operations can be cancelled; external launches cannot be recalled", 4);
            operations_[id].insert("state", "cancelled");
        }
        return operations_[id];
    }
    if (method.startsWith("entry.")) {
        const auto resource = requireResource(p["resourceId"].toString());
        const auto anchorId = p["anchorId"].toString();
        const auto anchor = anchorId.isEmpty() ? Anchor{} : findAnchor(resource, anchorId);
        if (resource.deleted || anchor.deleted) fail("invalid_state", "Restore the entry before opening it", 4);
        auto resolved = resourceResult(resource);
        if (!anchorId.isEmpty()) {
            const auto validation = AnchorLibraryManagementService(library_).validateAnchor(resource, anchor);
            resolved.insert("validation", QJsonObject{{"valid", validation.valid}, {"issues", array(validation.issues)}});
            if (method == "entry.open" && !validation.valid) fail("invalid_locator", validation.issues.join("; "));
        }
        resolved.insert("target", resolveAnchorTarget(anchor, {}, resource.location).value);
        resolved.insert("state", "resolved");
        if (method == "entry.resolve") return resolved;
        if (operations_.size() >= 100) {
            auto old = operations_.begin();
            while (old != operations_.end() && (old->value("state") == "queued" || old->value("state") == "running")) ++old;
            if (old == operations_.end()) fail("busy", "Too many pending open operations", 5);
            operations_.erase(old);
        }
        const auto operation = newId();
        const auto version = resourceVersion(resource);
        operations_.insert(operation, {{"id", operation}, {"state", "queued"}, {"resourceId", resource.id},
                                       {"anchorId", anchorId}, {"resolvedVersion", version}, {"positionVerified", false}});
        QTimer::singleShot(50, this, [this, operation, resourceId = resource.id, anchorId, version] {
            if (operations_[operation]["state"] != "queued") return;
            const auto current = library_.findResource(resourceId);
            const auto blocked = callbacks_.writeBlockReason();
            if (!current || resourceVersion(*current) != version || !blocked.isEmpty()) {
                operations_[operation].insert("state", "failed");
                operations_[operation].insert("error", blocked.isEmpty() ? "Resolved version changed before open" : blocked);
                return;
            }
            operations_[operation].insert("state", "running");
            auto result = callbacks_.open(resourceId, anchorId);
            if (operations_[operation]["state"].toString() == "running")
                for (auto it = result.begin(); it != result.end(); ++it) operations_[operation].insert(it.key(), it.value());
        });
        return operations_[operation];
    }
    if (method == "search" || method == "root.query") {
        SearchQuery query(p["query"].toString());
        const int limit = p["limit"].toInt(50);
        query.limit = limit + 1;
        query.includeDeleted = p["scope"].toString() == "all" || p["scope"].toString() == "trash";
        query.deletedOnly = p["scope"].toString() == "trash";
        query.requiredTags = strings(p["tags"]);
        if (method == "root.query") {
            const auto root = library_.findLibraryRoot(id);
            if (!root) fail("not_found", "Registered root was not found", 3);
            if (!root->enabled) fail("invalid_state", "Registered root is disabled", 4);
            query.requiredLocationPrefixes = {root->path};
            query.limit = 0;
        }
        QJsonArray items;
        const auto kind = p["kind"].toString("all");
        if (kind != "clips") {
            for (const auto &hit : library_.search(query)) {
                if (method == "root.query") {
                    const auto root = library_.findLibraryRoot(id);
                    const auto path = canonicalLibraryPath(hit.resource.location);
                    if (!libraryRootContainsPath(*root, path) || libraryRootIgnoresPath(*root, path)) continue;
                }
                if (kind == "anchors" && !hit.matchedAnchor) continue;
                if (kind == "files" && hit.matchedAnchor) continue;
                auto item = QJsonObject{{"kind", hit.matchedAnchor ? "anchor" : "resource"},
                                       {"resourceId", hit.resource.id}, {"title", hit.resource.title},
                                       {"location", hit.resource.location}, {"matchedField", hit.matchedField}};
                if (hit.matchedAnchor) {
                    item.insert("anchorId", hit.matchedAnchor->id); item.insert("name", hit.matchedAnchor->name);
                }
                items.append(item);
                if (items.size() > limit) break;
            }
        }
        if (method != "root.query" && (kind == "all" || kind == "clips") && items.size() <= limit) {
            ClipSearchOptions options;
            options.includeDeleted = query.includeDeleted;
            options.includeSaved = !query.deletedOnly;
            options.limit = limit + 1;
            options.emptyQueryReturnsPinnedAndRecent = false;
            for (const auto &hit : ClipSearchService(clipRepository_).search(query.text, options)) {
                bool matches = true;
                for (const auto &tag : query.requiredTags) if (!hit.tags.contains(tag, Qt::CaseInsensitive)) matches = false;
                if (!matches) continue;
                items.append(QJsonObject{{"kind", "clip"}, {"clipId", hit.clipId}, {"name", hit.displayName},
                                          {"preview", hit.preview}, {"matchedField", hit.matchedField}});
                if (items.size() > limit) break;
            }
        }
        const bool more = items.size() > limit;
        while (items.size() > limit) items.removeLast();
        return {{"items", items}, {"truncated", more}, {"limit", limit}};
    }
    if (method == "resource.get") return resourceResult(requireResource(id));
    if (method.startsWith("resource.")) {
        const auto resource = requireResource(id);
        precondition(p, resourceVersion(resource));
        if (method == "resource.purge" && !resource.deleted) fail("invalid_state", "Only trashed resources can be purged", 4);
        std::unique_ptr<PreviewLibrary> preview;
        if (dry) preview = std::make_unique<PreviewLibrary>(library_, clips_.clips());
        auto &target = dry ? static_cast<ILibraryRepository &>(preview->library) : library_;
        AnchorLibraryManagementService service(target);
        if (method == "resource.update") {
            ResourceMetadataUpdate update;
            update.title = p["title"].toString(resource.title);
            update.aliases = p.contains("aliases") ? strings(p["aliases"]) : resource.aliases;
            update.tags = p.contains("tags") ? strings(p["tags"]) : resource.tags;
            if (p.contains("pinned")) update.pinned = p["pinned"].toBool();
            checkResult(service.updateResourceMetadata({id}, update));
        } else if (method == "resource.purge") checkResult(service.permanentlyDeleteResources({id}));
        else checkResult(service.setResourcesDeleted({id}, method == "resource.delete"));
        const auto updated = target.findResource(id);
        if (!updated) return {{"id", id}, {"state", "purged"}};
        auto result = resourceArchiveJson(*updated);
        result.insert("pinned", target.resourceUsage(id).value_or(ResourceUsage{}).pinned);
        return versioned(result);
    }
    if (method.startsWith("anchor.")) {
        const bool create = method == "anchor.create";
        const auto resourceId = p["resourceId"].toString();
        const auto existing = resourceId.isEmpty() ? std::optional<Resource>() : library_.findResource(resourceId);
        if (!create && !existing) fail("not_found", "Anchor resource was not found", 3);
        if (create && !resourceId.isEmpty() && !existing) fail("not_found", "Specified parent resource was not found", 3);
        Resource resource = existing.value_or(Resource{});
        Anchor anchor = create ? Anchor{} : findAnchor(resource, id);
        if (method == "anchor.get") return anchorResult(resource, anchor);
        if (method == "anchor.validate") {
            const auto result = AnchorLibraryManagementService(library_).validateAnchor(resource, anchor);
            return {{"valid", result.valid}, {"issues", array(result.issues)}, {"version", resourceVersion(resource)}};
        }
        precondition(p, existing ? resourceVersion(resource) : "absent");
        if (create) {
            anchor.id = id.isEmpty() ? newId() : id;
            for (const auto &item : resources(library_))
                for (const auto &other : item.anchors) if (other.id == anchor.id) fail("identity_conflict", "Anchor ID already exists", 4);
            anchor.createdAt = QDateTime::currentDateTimeUtc();
            if (!existing) {
                resource.id = newId();
                resource.kind = ResourceKind::ManualAnchor;
                resource.title = p["resourceTitle"].toString();
                if (p.contains("location")) {
                    const auto location = p["location"].toString();
                    resource.location = location.startsWith("https://") || location.startsWith("http://")
                        ? location : absolutePath(location, base);
                }
                const auto kind = p["resourceKind"].toString();
                if (!kind.isEmpty()) {
                    QString error;
                    auto json = resourceArchiveJson(resource); json.insert("kind", kind);
                    resource = *resourceFromArchiveJson(json, &error);
                }
            }
        }
        if (method == "anchor.create" || method == "anchor.update") {
            if (resource.deleted || anchor.deleted) fail("invalid_state", "Restore the resource/anchor before editing", 4);
            applyAnchorPatch(anchor, p, base);
            if (resource.location.isEmpty()) resource.location = !anchor.targetFile.isEmpty() ? anchor.targetFile : anchor.targetUri;
            if (create && !existing && resource.title.trimmed().isEmpty())
                resource.title = QFileInfo(resource.location).fileName().isEmpty() ? resource.location : QFileInfo(resource.location).fileName();
            if (resolveAnchorTarget(anchor, {}, resource.location).value.isEmpty()) fail("invalid_locator", "A target is required");
            const auto validation = AnchorLibraryManagementService(library_).validateAnchor(resource, anchor);
            if (!validation.valid) fail("invalid_locator", validation.issues.join("; "));
        }
        if (method == "anchor.purge" && !anchor.deleted) fail("invalid_state", "Only trashed anchors can be purged", 4);
        std::unique_ptr<PreviewLibrary> preview;
        if (dry) preview = std::make_unique<PreviewLibrary>(library_, clips_.clips());
        auto &target = dry ? static_cast<ILibraryRepository &>(preview->library) : library_;
        AnchorLibraryManagementService service(target);
        if (create) {
            anchor.updatedAt = QDateTime::currentDateTimeUtc();
            resource.updatedAt = anchor.updatedAt;
            resource.anchors.append(anchor);
            if (!target.applyBatch({{resource}})) fail("identity_or_storage_conflict", target.lastError(), 4);
        } else if (method == "anchor.update") {
            checkResult(service.updateAnchor({resource.id, anchor},
                AnchorMetadataUpdate{anchor.name, anchor.aliases, anchor.tags, anchor.pinned},
                AnchorLocatorUpdate{anchor.targetApp, anchor.targetFile, anchor.targetUri, anchor.locatorType, anchor.locatorJson}));
        } else if (method == "anchor.purge") checkResult(service.permanentlyDeleteAnchors({{resource.id, anchor}}));
        else checkResult(service.setAnchorsDeleted({{resource.id, anchor}}, method == "anchor.delete"));
        if (method == "anchor.purge") return {{"id", anchor.id}, {"resourceId", resource.id}, {"state", "purged"}};
        const auto updated = target.findResource(resource.id);
        return anchorResult(*updated, findAnchor(*updated, anchor.id));
    }
    if (method == "history.list") {
        const auto history = clipRepository_.temporaryClips();
        QJsonArray items;
        const int limit = p["limit"].toInt(50);
        for (const auto &clip : history) {
            if (items.size() == limit) break;
            items.append(versioned(clipCommandJson(clip)));
        }
        return {{"items", items}, {"truncated", history.size() > limit}};
    }
    if (method == "history.prune") {
        precondition(p, libraryVersion());
        auto history = clipRepository_.temporaryClips();
        ClipCapturePolicy policy;
        policy.maxTemporaryClips = p["keep"].toInt(); policy.temporaryTtlSeconds = p["ttlSeconds"].toInt();
        if (dry) return {{"keep", policy.maxTemporaryClips}, {"ttlSeconds", policy.temporaryTtlSeconds}, {"existingCount", history.size()}};
        clipRepository_.pruneTemporaryHistory(policy);
        if (!clipRepository_.lastError().isEmpty()) fail("storage_error", clipRepository_.lastError(), 6);
        return {{"remaining", clipRepository_.temporaryClips().size()}};
    }
    if (method.startsWith("clip.") || method == "history.promote") {
        const bool create = method == "clip.create";
        const auto existing = id.isEmpty() ? std::optional<Clip>() : clips_.findClip(id);
        if (!create && !existing) fail("not_found", "Clip ID was not found", 3);
        if (create && existing) fail("identity_conflict", "Clip ID already exists", 4);
        if (method == "clip.get") return versioned(clipCommandJson(*existing));
        precondition(p, existing ? digest(clipCommandJson(*existing)) : "absent");
        Clip clip = existing.value_or(Clip{});
        if (method == "history.promote" && clip.state != ClipState::Temporary) fail("invalid_state", "Clip is not temporary history", 4);
        if (!create && method != "history.promote" && clip.state == ClipState::Temporary)
            fail("invalid_state", "Use history.promote before editing temporary history", 4);
        if (method == "clip.update" && clip.state != ClipState::Saved) fail("invalid_state", "Restore the Clip before editing", 4);
        if (method == "clip.purge" && clip.state != ClipState::Deleted) fail("invalid_state", "Only trashed Clips can be purged", 4);
        std::unique_ptr<PreviewLibrary> preview;
        if (dry) preview = std::make_unique<PreviewLibrary>(library_, clips_.clips());
        auto &target = dry ? preview->persistent : clips_;
        QString error;
        if (create || method == "clip.update" || method == "history.promote") {
            if (create) { clip.id = id.isEmpty() ? newId() : id; clip.createdAt = QDateTime::currentDateTimeUtc(); }
            if (p.contains("text")) clip.text = p["text"].toString();
            if (clip.text.trimmed().isEmpty() || clip.text.toUtf8().size() > 262144) fail("input_limit", "Clip text must be nonempty and at most 256 KiB UTF-8");
            if (p.contains("name")) clip.name = p["name"].toString();
            if (clip.name.trimmed().isEmpty()) fail("invalid_argument", "Clip name is required");
            if (p.contains("aliases")) clip.aliases = strings(p["aliases"]);
            if (p.contains("tags")) clip.tags = strings(p["tags"]);
            if (p.contains("pinned")) clip.pinned = p["pinned"].toBool();
            if (p.contains("action")) clip.actionType = p["action"].toString() == "open_web_url"
                ? ClipActionType::OpenWebUrl : ClipActionType::InsertText;
            if (p.contains("action") && (create || method == "history.promote")) {
                clip.tags.erase(std::remove_if(clip.tags.begin(), clip.tags.end(), [](const QString &tag) {
                    return tag.trimmed().compare(webBrowserClipTag(), Qt::CaseInsensitive) == 0;
                }), clip.tags.end());
                if (clip.actionType == ClipActionType::OpenWebUrl) clip.tags.append(webBrowserClipTag());
            }
            if (actionForClip(clip) == ClipActionType::OpenWebUrl && !webUrlForClip(clip, &error))
                fail("invalid_argument", error);
            clip.preview = clip.text.simplified().left(160);
            clip.contentHash = QString::fromLatin1(QCryptographicHash::hash(clip.text.toUtf8(), QCryptographicHash::Sha256).toHex());
            clip.sizeBytes = clip.text.toUtf8().size();
            if (dry) clip.storageBackend = ClipStorageBackend::Local;
            if (!target.saveClip(clip, &error)) fail("identity_or_storage_conflict", error, 4);
        } else if (method == "clip.purge") {
            if (!target.permanentlyRemove(id, &error)) fail("storage_error", error, 6);
        } else if (!target.changeState(id, method == "clip.delete" ? ClipState::Deleted : ClipState::Saved, &error))
            fail("storage_error", error, 6);
        if (method == "clip.purge") return {{"id", id}, {"state", "purged"}};
        return versioned(clipCommandJson(*target.findClip(clip.id)));
    }
    if (method == "inbox.add") {
        InboxFileSaveRequest request;
        request.filePath = absolutePath(p["path"].toString(), base);
        request.name = p["name"].toString(); request.aliases = strings(p["aliases"]); request.tags = strings(p["tags"]);
        request.pinned = p["pinned"].toBool();
        const auto mode = p["mode"].toString("link");
        request.mode = mode == "copy" ? InboxFileArchiveMode::Copy : mode == "move" ? InboxFileArchiveMode::Move : InboxFileArchiveMode::Link;
        if (p.contains("destination")) request.managedLibraryDirectory = absolutePath(p["destination"].toString(), base);
        const auto error = inboxFileSaveRequestError(request);
        if (!error.isEmpty()) fail("invalid_argument", error);
        const auto path = request.mode == InboxFileArchiveMode::Link ? request.filePath
            : managedInboxFilePath(request.filePath, request.managedLibraryDirectory);
        const auto resourceId = inboxResourceIdForPath(path);
        const auto existing = library_.findResource(resourceId);
        precondition(p, existing ? resourceVersion(*existing) : "absent");
        if (mode != "link" && path != request.filePath && QFileInfo::exists(path))
            fail("destination_exists", "Choose a destination without an existing file", 4);
        if (dry) {
            PreviewLibrary preview(library_, clips_.clips());
            LibraryBatchMutation planned;
            planned.upserts = {preparedInboxResource(existing, request, path)};
            if (request.pinned) planned.resourcePinUpdates = {{resourceId, true}};
            if (!preview.library.applyBatch(planned)) fail("identity_conflict", preview.library.lastError(), 4);
            return {{"resourceId", resourceId}, {"path", path}, {"mode", mode}, {"source", request.filePath}};
        }
        const auto saved = saveInboxFile(library_, request);
        if (!saved.success()) fail("storage_error", saved.status, 6);
        return resourceResult(requireResource(saved.resourceId));
    }
    if (method == "root.list") {
        QJsonArray items;
        for (const auto &root : library_.libraryRoots()) items.append(versioned(rootJson(root)));
        return {{"items", items}};
    }
    if (method == "root.files") {
        const auto root = library_.findLibraryRoot(id);
        if (!root) fail("not_found", "Registered root was not found", 3);
        if (!root->enabled) fail("invalid_state", "Registered root is disabled", 4);
        const auto path = canonicalLibraryPath(QDir(root->path).filePath(p["relativePath"].toString(".")));
        if (!QFileInfo(path).isDir() || !libraryRootContainsPath(*root, path) || libraryRootIgnoresPath(*root, path))
            fail("outside_root", "Directory must exist inside the enabled registered root and not be ignored", 4);
        QJsonArray items;
        const int limit = p["limit"].toInt(50);
        // Deliberately one directory, never recursive indexing.
        QDirIterator entries(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        while (entries.hasNext()) {
            entries.next();
            const auto entry = entries.fileInfo();
            if (libraryRootIgnoresPath(*root, entry.absoluteFilePath())) continue;
            items.append(QJsonObject{{"name", entry.fileName()}, {"path", entry.absoluteFilePath()}, {"directory", entry.isDir()}});
            if (items.size() > limit) break;
        }
        const bool more = items.size() > limit;
        if (more) items.removeLast();
        return {{"items", items}, {"truncated", more}, {"rootId", id}, {"path", path}};
    }
    if (method.startsWith("root.")) {
        LibraryRoot root;
        if (method == "root.register") {
            const auto path = canonicalLibraryPath(absolutePath(p["path"].toString(), base));
            if (!QFileInfo(path).isDir()) fail("invalid_argument", "Root must be an existing directory");
            root = makeLibraryRootForPath(path);
            if (library_.findLibraryRoot(root.id)) fail("identity_conflict", "Root already registered; use root.update", 4);
            precondition(p, "absent");
        } else {
            const auto existing = library_.findLibraryRoot(id);
            if (!existing) fail("not_found", "Registered root was not found", 3);
            root = *existing;
            precondition(p, digest(rootJson(root)));
        }
        if (p.contains("displayName")) root.displayName = p["displayName"].toString();
        if (p.contains("enabled")) root.enabled = p["enabled"].toBool();
        if (p.contains("ignoredDirectoryNames")) root.ignoredDirectoryNames = strings(p["ignoredDirectoryNames"]);
        if (root.syncRoot && !root.ignoredDirectoryNames.contains("_PinloomData")) root.ignoredDirectoryNames.append("_PinloomData");
        root.updatedAt = QDateTime::currentDateTimeUtc();
        if (!dry && !(method == "root.remove" ? library_.removeLibraryRoot(id) : library_.upsertLibraryRoot(root)))
            fail("storage_error", library_.lastError(), 6);
        if (method == "root.remove") return {{"id", id}, {"state", "unregistered"}};
        return versioned(rootJson(dry ? root : *library_.findLibraryRoot(root.id)));
    }
    if (method == "library.export" || method == "library.import") {
        const auto path = absolutePath(p["path"].toString(), base);
        if (method == "library.export") {
            if (QFileInfo::exists(path) && !p["overwrite"].toBool()) fail("destination_exists", "Archive exists; set overwrite explicitly", 4);
            if (!dry) checkResult(AnchorLibraryArchiveService(library_).exportJson(path));
            return {{"path", path}, {"version", libraryVersion()}, {"format", "pinloom-anchor-library"}};
        }
        precondition(p, libraryVersion());
        if (!QFileInfo(path).isFile()) fail("not_found", "Archive file was not found", 3);
        if (QFileInfo(path).size() > CommandMaxArchiveBytes) fail("input_limit", "Archive exceeds 16 MiB");
        PreviewLibrary preview(library_, clips_.clips());
        const auto mode = p["mode"].toString() == "replace" ? AnchorLibraryImportMode::Replace : AnchorLibraryImportMode::Merge;
        const auto planned = AnchorLibraryArchiveService(preview.library).importJson(path, mode);
        checkResult(planned);
        if (!dry) checkResult(AnchorLibraryArchiveService(library_).importJson(path, mode));
        return {{"affectedCount", planned.affectedCount}, {"version", libraryVersion()}, {"path", path}};
    }
    fail("unknown_method", "Command is not implemented");
    return {};
}
} // namespace Pinloom
