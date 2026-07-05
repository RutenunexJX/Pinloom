#include "pinloom/core/LegacyCompatibility.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace Pinloom {

ResourceKind normalizedResourceKind(ResourceKind kind)
{
    if (kind == ResourceKind::Markdown) {
        return ResourceKind::File;
    }
    return kind;
}

AnchorType normalizedAnchorType(AnchorType type)
{
    switch (type) {
    case AnchorType::MarkdownHeading:
        return AnchorType::TextHeading;
    case AnchorType::MarkdownBlock:
        return AnchorType::TextBlock;
    default:
        return type;
    }
}

namespace {

QString locatorTypeFromAnchorType(AnchorType type)
{
    switch (normalizedAnchorType(type)) {
    case AnchorType::FileLine:
        return QStringLiteral("file.line");
    case AnchorType::TextHeading:
        return QStringLiteral("text.heading");
    case AnchorType::TextBlock:
        return QStringLiteral("text.block");
    case AnchorType::Marker:
        return QStringLiteral("marker");
    case AnchorType::PdfPage:
        return QStringLiteral("pdf.page");
    case AnchorType::PdfRegion:
        return QStringLiteral("pdf.region");
    case AnchorType::UrlFragment:
        return QStringLiteral("url.fragment");
    case AnchorType::Manual:
        return QStringLiteral("manual");
    case AnchorType::MarkdownHeading:
    case AnchorType::MarkdownBlock:
    case AnchorType::None:
        break;
    }
    return {};
}

QString locatorJsonFromLegacyFields(const Anchor &anchor)
{
    QJsonObject locator;
    if (anchor.line >= 0) {
        locator.insert(QStringLiteral("line"), anchor.line);
    }
    if (anchor.page >= 0) {
        locator.insert(QStringLiteral("page"), anchor.page);
    }
    if (anchor.region.isValid()) {
        QJsonArray region;
        region.append(anchor.region.x());
        region.append(anchor.region.y());
        region.append(anchor.region.width());
        region.append(anchor.region.height());
        locator.insert(QStringLiteral("region"), region);
    }

    if (locator.isEmpty()) {
        return {};
    }

    return QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));
}

bool looksLikeUri(const QString &location)
{
    return location.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        || location.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)
        || location.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive);
}

bool isPositiveNumber(const QJsonValue &value)
{
    if (value.isDouble()) {
        return value.toDouble() > 0.0;
    }
    if (value.isString()) {
        bool ok = false;
        const double number = value.toString().trimmed().toDouble(&ok);
        return ok && number > 0.0;
    }
    return false;
}

QJsonObject locatorObject(const QString &locatorJson)
{
    const QString trimmed = locatorJson.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    return document.object();
}

bool hasOnlyLegacyManualLineFields(const QJsonObject &locator)
{
    for (const QString &key : locator.keys()) {
        const QString normalizedKey = key.trimmed().toLower();
        if (normalizedKey == QLatin1String("line")) {
            continue;
        }
        if (normalizedKey == QLatin1String("type")
            && locator.value(key).toString().trimmed().compare(QStringLiteral("manual"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        if (normalizedKey == QLatin1String("source")
            && locator.value(key).toString().trimmed().compare(QStringLiteral("manual"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        return false;
    }
    return true;
}

} // namespace

Anchor normalizedAnchor(Anchor anchor)
{
    anchor.type = normalizedAnchorType(anchor.type);
    if (anchor.target.trimmed().isEmpty() && !anchor.name.trimmed().isEmpty()) {
        anchor.target = anchor.name;
    }
    if (anchor.locatorType.trimmed().isEmpty()) {
        anchor.locatorType = locatorTypeFromAnchorType(anchor.type);
    }
    if (anchor.locatorJson.trimmed().isEmpty()) {
        anchor.locatorJson = locatorJsonFromLegacyFields(anchor);
    }
    return anchor;
}

Resource normalizedResource(Resource resource)
{
    resource.kind = normalizedResourceKind(resource.kind);
    for (int i = 0; i < resource.anchors.size(); ++i) {
        Anchor &anchor = resource.anchors[i];
        anchor = normalizedAnchor(anchor);
        if (anchor.id.trimmed().isEmpty() && !resource.id.trimmed().isEmpty()) {
            anchor.id = QStringLiteral("%1#anchor-%2").arg(resource.id, QString::number(i));
        }
        if (anchor.targetFile.trimmed().isEmpty()
            && anchor.targetUri.trimmed().isEmpty()
            && !resource.location.trimmed().isEmpty()) {
            if (resource.kind == ResourceKind::Url || looksLikeUri(resource.location)) {
                anchor.targetUri = resource.location;
            } else {
                anchor.targetFile = resource.location;
            }
        }
        if (anchor.targetApp.trimmed().isEmpty()) {
            if (resource.kind == ResourceKind::Pdf) {
                anchor.targetApp = QStringLiteral("pdf");
            } else if (resource.kind == ResourceKind::Url) {
                anchor.targetApp = QStringLiteral("browser");
            }
        }
    }
    return resource;
}

bool isDeprecatedPdfManualLineAnchor(const Resource &resource, const Anchor &anchor)
{
    if (normalizedResourceKind(resource.kind) != ResourceKind::Pdf
        || normalizedAnchorType(anchor.type) != AnchorType::Manual) {
        return false;
    }

    const QString locatorType = anchor.locatorType.trimmed().toLower();
    if (!locatorType.isEmpty() && locatorType != QLatin1String("manual")) {
        return false;
    }

    if (anchor.page > 0 || anchor.region.isValid()) {
        return false;
    }

    const QJsonObject locator = locatorObject(anchor.locatorJson);
    if (!hasOnlyLegacyManualLineFields(locator)) {
        return false;
    }

    return anchor.line > 0 || isPositiveNumber(locator.value(QStringLiteral("line")));
}

bool resourceKindMatchesFilter(ResourceKind kind, const QList<ResourceKind> &requiredKinds)
{
    if (requiredKinds.isEmpty()) {
        return true;
    }

    const ResourceKind normalizedKind = normalizedResourceKind(kind);
    return std::any_of(requiredKinds.cbegin(), requiredKinds.cend(), [&](ResourceKind requiredKind) {
        return normalizedResourceKind(requiredKind) == normalizedKind;
    });
}

} // namespace Pinloom
