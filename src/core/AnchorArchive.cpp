#include "pinloom/core/AnchorArchive.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

namespace Pinloom {

namespace {

constexpr int kArchiveVersion = 1;

QString archiveSchemaName()
{
    return QStringLiteral("pinloom.anchors");
}

QString resourceKindToString(ResourceKind kind)
{
    switch (kind) {
    case ResourceKind::File:
        return QStringLiteral("file");
    case ResourceKind::Folder:
        return QStringLiteral("folder");
    case ResourceKind::Pdf:
        return QStringLiteral("pdf");
    case ResourceKind::Markdown:
        return QStringLiteral("markdown");
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

ResourceKind resourceKindFromString(const QString &kind)
{
    if (kind == QLatin1String("file")) {
        return ResourceKind::File;
    }
    if (kind == QLatin1String("folder")) {
        return ResourceKind::Folder;
    }
    if (kind == QLatin1String("pdf")) {
        return ResourceKind::Pdf;
    }
    if (kind == QLatin1String("markdown")) {
        return ResourceKind::Markdown;
    }
    if (kind == QLatin1String("text_snippet")) {
        return ResourceKind::TextSnippet;
    }
    if (kind == QLatin1String("url")) {
        return ResourceKind::Url;
    }
    if (kind == QLatin1String("note")) {
        return ResourceKind::Note;
    }
    if (kind == QLatin1String("manual_anchor")) {
        return ResourceKind::ManualAnchor;
    }
    return ResourceKind::Unknown;
}

QString anchorTypeToString(AnchorType type)
{
    switch (type) {
    case AnchorType::FileLine:
        return QStringLiteral("file_line");
    case AnchorType::MarkdownHeading:
        return QStringLiteral("markdown_heading");
    case AnchorType::MarkdownBlock:
        return QStringLiteral("markdown_block");
    case AnchorType::Marker:
        return QStringLiteral("marker");
    case AnchorType::PdfPage:
        return QStringLiteral("pdf_page");
    case AnchorType::PdfRegion:
        return QStringLiteral("pdf_region");
    case AnchorType::UrlFragment:
        return QStringLiteral("url_fragment");
    case AnchorType::Manual:
        return QStringLiteral("manual");
    case AnchorType::TextHeading:
        return QStringLiteral("text_heading");
    case AnchorType::TextBlock:
        return QStringLiteral("text_block");
    case AnchorType::None:
        break;
    }
    return QStringLiteral("none");
}

AnchorType anchorTypeFromString(const QString &type)
{
    if (type == QLatin1String("file_line")) {
        return AnchorType::FileLine;
    }
    if (type == QLatin1String("markdown_heading")) {
        return AnchorType::MarkdownHeading;
    }
    if (type == QLatin1String("markdown_block")) {
        return AnchorType::MarkdownBlock;
    }
    if (type == QLatin1String("text_heading")) {
        return AnchorType::TextHeading;
    }
    if (type == QLatin1String("text_block")) {
        return AnchorType::TextBlock;
    }
    if (type == QLatin1String("marker")
        || type == QLatin1String("symbol_like")
        || type == QLatin1String("code_symbol")) {
        return AnchorType::Marker;
    }
    if (type == QLatin1String("pdf_page")) {
        return AnchorType::PdfPage;
    }
    if (type == QLatin1String("pdf_region")) {
        return AnchorType::PdfRegion;
    }
    if (type == QLatin1String("url_fragment")) {
        return AnchorType::UrlFragment;
    }
    if (type == QLatin1String("manual")) {
        return AnchorType::Manual;
    }
    return AnchorType::None;
}

QString dateTimeToString(const QDateTime &dateTime)
{
    return dateTime.isValid() ? dateTime.toUTC().toString(Qt::ISODate) : QString();
}

QDateTime dateTimeFromJson(const QJsonValue &value)
{
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        return {};
    }
    return QDateTime::fromString(value.toString(), Qt::ISODate);
}

QJsonArray stringListToJson(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QStringList stringListFromJson(const QJsonValue &value)
{
    QStringList values;
    if (!value.isArray()) {
        return values;
    }

    for (const QJsonValue &item : value.toArray()) {
        if (item.isString()) {
            values.append(item.toString());
        }
    }
    return values;
}

QJsonObject regionToJson(const QRectF &region)
{
    QJsonObject object;
    object.insert(QStringLiteral("x"), region.x());
    object.insert(QStringLiteral("y"), region.y());
    object.insert(QStringLiteral("width"), region.width());
    object.insert(QStringLiteral("height"), region.height());
    return object;
}

QRectF regionFromJson(const QJsonValue &value)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        return QRectF(object.value(QStringLiteral("x")).toDouble(),
                      object.value(QStringLiteral("y")).toDouble(),
                      object.value(QStringLiteral("width")).toDouble(),
                      object.value(QStringLiteral("height")).toDouble());
    }

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (array.size() == 4) {
            return QRectF(array.at(0).toDouble(),
                          array.at(1).toDouble(),
                          array.at(2).toDouble(),
                          array.at(3).toDouble());
        }
    }

    return {};
}

QJsonObject anchorToJson(const Anchor &anchor)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), anchor.id);
    object.insert(QStringLiteral("name"), anchor.name);
    object.insert(QStringLiteral("target_app"), anchor.targetApp);
    object.insert(QStringLiteral("target_file"), anchor.targetFile);
    object.insert(QStringLiteral("target_uri"), anchor.targetUri);
    object.insert(QStringLiteral("locator_type"), anchor.locatorType);
    object.insert(QStringLiteral("locator_json"), anchor.locatorJson);
    object.insert(QStringLiteral("aliases"), stringListToJson(anchor.aliases));
    object.insert(QStringLiteral("tags"), stringListToJson(anchor.tags));
    object.insert(QStringLiteral("pinned"), anchor.pinned);
    object.insert(QStringLiteral("created_at"), dateTimeToString(anchor.createdAt));
    object.insert(QStringLiteral("updated_at"), dateTimeToString(anchor.updatedAt));
    object.insert(QStringLiteral("used_at"), dateTimeToString(anchor.usedAt));
    object.insert(QStringLiteral("type"), anchorTypeToString(anchor.type));
    object.insert(QStringLiteral("target"), anchor.target);
    object.insert(QStringLiteral("line"), anchor.line);
    object.insert(QStringLiteral("page"), anchor.page);
    object.insert(QStringLiteral("region"), regionToJson(anchor.region));
    return object;
}

Anchor anchorFromJson(const QJsonObject &object)
{
    Anchor anchor;
    anchor.id = object.value(QStringLiteral("id")).toString();
    anchor.name = object.value(QStringLiteral("name")).toString();
    anchor.targetApp = object.value(QStringLiteral("target_app")).toString();
    anchor.targetFile = object.value(QStringLiteral("target_file")).toString();
    anchor.targetUri = object.value(QStringLiteral("target_uri")).toString();
    anchor.locatorType = object.value(QStringLiteral("locator_type")).toString();
    anchor.locatorJson = object.value(QStringLiteral("locator_json")).toString();
    anchor.aliases = stringListFromJson(object.value(QStringLiteral("aliases")));
    anchor.tags = stringListFromJson(object.value(QStringLiteral("tags")));
    anchor.pinned = object.value(QStringLiteral("pinned")).toBool(false);
    anchor.createdAt = dateTimeFromJson(object.value(QStringLiteral("created_at")));
    anchor.updatedAt = dateTimeFromJson(object.value(QStringLiteral("updated_at")));
    anchor.usedAt = dateTimeFromJson(object.value(QStringLiteral("used_at")));
    anchor.type = anchorTypeFromString(object.value(QStringLiteral("type")).toString());
    anchor.target = object.value(QStringLiteral("target")).toString();
    anchor.line = object.value(QStringLiteral("line")).toInt(-1);
    anchor.page = object.value(QStringLiteral("page")).toInt(-1);
    anchor.region = regionFromJson(object.value(QStringLiteral("region")));
    return anchor;
}

QJsonObject resourceToJson(const Resource &resource)
{
    QJsonArray anchors;
    for (const Anchor &anchor : resource.anchors) {
        anchors.append(anchorToJson(anchor));
    }

    QJsonObject object;
    object.insert(QStringLiteral("id"), resource.id);
    object.insert(QStringLiteral("kind"), resourceKindToString(resource.kind));
    object.insert(QStringLiteral("title"), resource.title);
    object.insert(QStringLiteral("location"), resource.location);
    object.insert(QStringLiteral("updated_at"), dateTimeToString(resource.updatedAt));
    object.insert(QStringLiteral("anchors"), anchors);
    return object;
}

bool resourceFromJson(const QJsonObject &object, Resource *resource, QString *error)
{
    resource->id = object.value(QStringLiteral("id")).toString().trimmed();
    if (resource->id.isEmpty()) {
        *error = QStringLiteral("Anchor archive resource id is required");
        return false;
    }

    const QJsonValue anchorsValue = object.value(QStringLiteral("anchors"));
    if (!anchorsValue.isArray()) {
        *error = QStringLiteral("Anchor archive resource anchors must be an array");
        return false;
    }

    resource->kind = resourceKindFromString(object.value(QStringLiteral("kind")).toString());
    resource->title = object.value(QStringLiteral("title")).toString();
    resource->location = object.value(QStringLiteral("location")).toString();
    resource->updatedAt = dateTimeFromJson(object.value(QStringLiteral("updated_at")));
    resource->content.clear();
    resource->aliases.clear();
    resource->tags.clear();
    resource->relations.clear();
    resource->anchors.clear();

    for (const QJsonValue &anchorValue : anchorsValue.toArray()) {
        if (!anchorValue.isObject()) {
            *error = QStringLiteral("Anchor archive anchor must be an object");
            return false;
        }
        resource->anchors.append(anchorFromJson(anchorValue.toObject()));
    }

    return true;
}

} // namespace

AnchorArchiveExportResult exportAnchorsToJsonFile(const QList<Resource> &resources, const QString &filePath)
{
    AnchorArchiveExportResult result;
    if (filePath.trimmed().isEmpty()) {
        result.error = QStringLiteral("Anchor archive path is required");
        return result;
    }

    QJsonArray resourceArray;
    for (const Resource &resource : resources) {
        if (resource.anchors.isEmpty()) {
            continue;
        }

        resourceArray.append(resourceToJson(resource));
        ++result.exportedResources;
        result.exportedAnchors += resource.anchors.size();
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), archiveSchemaName());
    root.insert(QStringLiteral("version"), kArchiveVersion);
    root.insert(QStringLiteral("resources"), resourceArray);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        result.error = QStringLiteral("Unable to open anchor archive for writing: %1").arg(file.errorString());
        return result;
    }

    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size()) {
        result.error = QStringLiteral("Unable to write anchor archive: %1").arg(file.errorString());
        return result;
    }

    if (!file.commit()) {
        result.error = QStringLiteral("Unable to save anchor archive: %1").arg(file.errorString());
        return result;
    }

    result.ok = true;
    return result;
}

AnchorArchiveImportResult importAnchorsFromJsonFile(ILibraryRepository &repository, const QString &filePath)
{
    AnchorArchiveImportResult result;
    if (filePath.trimmed().isEmpty()) {
        result.error = QStringLiteral("Anchor archive path is required");
        return result;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Unable to open anchor archive for reading: %1").arg(file.errorString());
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.error = QStringLiteral("Invalid anchor archive JSON: %1").arg(parseError.errorString());
        return result;
    }
    if (!document.isObject()) {
        result.error = QStringLiteral("Anchor archive must be a JSON object");
        return result;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toString() != archiveSchemaName()) {
        result.error = QStringLiteral("Unsupported anchor archive schema");
        return result;
    }
    if (root.value(QStringLiteral("version")).toInt(-1) != kArchiveVersion) {
        result.error = QStringLiteral("Unsupported anchor archive version");
        return result;
    }

    const QJsonValue resourcesValue = root.value(QStringLiteral("resources"));
    if (!resourcesValue.isArray()) {
        result.error = QStringLiteral("Anchor archive resources must be an array");
        return result;
    }

    for (const QJsonValue &resourceValue : resourcesValue.toArray()) {
        if (!resourceValue.isObject()) {
            result.error = QStringLiteral("Anchor archive resource must be an object");
            return result;
        }

        Resource resource;
        if (!resourceFromJson(resourceValue.toObject(), &resource, &result.error)) {
            return result;
        }

        if (resource.anchors.isEmpty()) {
            ++result.skippedResources;
            continue;
        }

        if (repository.findResource(resource.id).has_value()) {
            ++result.skippedResources;
            result.skippedAnchors += resource.anchors.size();
            continue;
        }

        if (!repository.upsertResource(resource)) {
            result.error = QStringLiteral("Unable to import anchor resource: %1").arg(resource.id);
            return result;
        }

        ++result.importedResources;
        result.importedAnchors += resource.anchors.size();
    }

    result.ok = true;
    return result;
}

} // namespace Pinloom
