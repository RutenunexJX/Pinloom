#include "pinloom/core/ManualWordAnchorCreation.h"

#include <QDateTime>
#include <QFileInfo>
#include <QUuid>

namespace Pinloom {

namespace {

bool containsValueCaseInsensitive(const QStringList &values, const QString &needle)
{
    return values.contains(needle, Qt::CaseInsensitive);
}

void appendUniqueCaseInsensitive(QStringList &values, const QString &value)
{
    const QString trimmed = value.trimmed();
    if (!trimmed.isEmpty() && !containsValueCaseInsensitive(values, trimmed)) {
        values.append(trimmed);
    }
}

QString cleanTag(QString tag)
{
    tag = tag.trimmed();
    while (tag.startsWith(QLatin1Char('#'))) {
        tag.remove(0, 1);
        tag = tag.trimmed();
    }
    return tag;
}

QStringList cleanedValues(const QStringList &values, bool tags = false)
{
    QStringList cleaned;
    for (const QString &value : values) {
        appendUniqueCaseInsensitive(cleaned, tags ? cleanTag(value) : value);
    }
    return cleaned;
}

QString manualWordAnchorResourceId()
{
    return QStringLiteral("manual-word-anchor:%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QString resourceTitleForFile(const QString &file, const QString &name)
{
    const QString fileName = QFileInfo(file).fileName().trimmed();
    return fileName.isEmpty() ? name : fileName;
}

WordCaptureRequest captureRequestFromManualRequest(
    const ManualWordAnchorCreationRequest &request,
    const QString &name,
    const QString &file)
{
    WordCaptureRequest captureRequest;
    captureRequest.anchorName = name;
    captureRequest.targetApp = request.targetApp;
    captureRequest.targetFile = file;
    captureRequest.locatorType = request.locatorType;
    captureRequest.bookmark = request.bookmark;
    captureRequest.source = request.source;
    return captureRequest;
}

} // namespace

bool ManualWordAnchorCreationResult::success() const
{
    return error.isEmpty();
}

ManualWordAnchorCreationService::ManualWordAnchorCreationService(ILibraryRepository &repository)
    : repository_(repository)
{
}

ManualWordAnchorCreationResult ManualWordAnchorCreationService::createManualWordBookmarkAnchor(
    const ManualWordAnchorCreationRequest &request)
{
    ManualWordAnchorCreationResult result;

    const QString name = request.name.trimmed();
    if (name.isEmpty()) {
        result.error = QStringLiteral("Manual Word anchor name is missing");
        return result;
    }

    const QString file = request.file.trimmed();
    if (file.isEmpty()) {
        result.error = QStringLiteral("Manual Word anchor file is missing");
        return result;
    }

    const WordCaptureResult capture = captureManualWordBookmarkAnchor(
        captureRequestFromManualRequest(request, name, file));
    if (!capture.success()) {
        result.error = capture.error;
        return result;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    Resource resource;
    resource.id = manualWordAnchorResourceId();
    resource.kind = ResourceKind::File;
    resource.title = resourceTitleForFile(file, name);
    resource.location = file;
    resource.updatedAt = now;

    Anchor anchor = capture.anchor;
    anchor.id = QStringLiteral("%1#word-bookmark").arg(resource.id);
    anchor.name = name;
    anchor.target = name;
    anchor.aliases = cleanedValues(request.aliases);
    anchor.tags = cleanedValues(request.tags, true);
    anchor.pinned = request.pinned;
    anchor.createdAt = now;
    anchor.updatedAt = now;

    resource.anchors = {anchor};

    if (!repository_.upsertResource(resource)) {
        result.resource = resource;
        result.anchor = anchor;
        result.error = QStringLiteral("Unable to save manual Word anchor");
        return result;
    }

    result.resource = resource;
    result.anchor = anchor;
    return result;
}

} // namespace Pinloom
