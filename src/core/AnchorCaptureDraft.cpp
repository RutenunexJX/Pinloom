#include "pinloom/core/AnchorCaptureDraft.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>

namespace Pinloom {

namespace {

QStringList cleanedValues(const QStringList &values, bool tags)
{
    QStringList result;
    for (QString value : values) {
        if (!tags) {
            if (!value.trimmed().isEmpty()) {
                result.append(value);
            }
            continue;
        }
        value = value.trimmed();
        while (value.startsWith(QLatin1Char('#'))) {
            value.remove(0, 1);
            value = value.trimmed();
        }
        if (!value.isEmpty() && !result.contains(value, Qt::CaseInsensitive)) {
            result.append(value);
        }
    }
    return result;
}

QString newIdentity(const QString &prefix)
{
    return QStringLiteral("%1:%2")
        .arg(prefix, QUuid::createUuid().toString(QUuid::WithoutBraces));
}

ResourceKind resourceKindForDraft(const AnchorCaptureDraft &draft)
{
    if (draft.targetApp.contains(QStringLiteral("sumatra"), Qt::CaseInsensitive)
        || QFileInfo(draft.targetFile).suffix().compare(
               QStringLiteral("pdf"), Qt::CaseInsensitive) == 0) {
        return ResourceKind::Pdf;
    }
    return ResourceKind::File;
}

} // namespace

bool AnchorCaptureDraft::isValid(QString *error) const
{
    if (error) {
        error->clear();
    }
    if (targetApp.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("Anchor target application is missing");
        return false;
    }
    if (targetFile.trimmed().isEmpty() && targetUri.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("Anchor target file or URI is missing");
        return false;
    }
    if (locatorType.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("Anchor locator type is missing");
        return false;
    }
    if (locatorJson.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("Anchor locator is missing");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument locator = QJsonDocument::fromJson(
        locatorJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !locator.isObject()) {
        if (error) *error = QStringLiteral("Anchor locator JSON is invalid");
        return false;
    }
    if (suggestedName.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("Anchor name is missing");
        return false;
    }
    if (mutationRequired && !mutationAuthorized) {
        if (error) *error = QStringLiteral("Document mutation authorization is required");
        return false;
    }
    return true;
}

bool AnchorCaptureCommitResult::success() const
{
    return error.isEmpty();
}

AnchorCaptureDraft anchorCaptureDraftFromPdfRequest(
    const ManualPdfAnchorCreationRequest &request)
{
    PdfCaptureRequest capture;
    capture.targetApp = request.targetApp;
    capture.targetFile = request.file;
    capture.documentIdentity = request.documentIdentity;
    capture.locatorType = request.locatorType;
    capture.page = request.page;
    capture.rect = request.rect;
    capture.mediaBox = request.mediaBox;
    capture.cropBox = request.cropBox;
    capture.rotation = request.rotation;
    capture.userUnit = request.userUnit;
    capture.zoom = request.zoom;
    capture.unit = request.unit;
    capture.source = request.source;
    capture.adapterId = request.adapterId;
    capture.searchText = request.searchText;
    capture.contextBefore = request.contextBefore;
    capture.contextAfter = request.contextAfter;
    capture.occurrence = request.occurrence;
    capture.fallbackRect = request.fallbackRect;

    AnchorCaptureDraft draft;
    draft.targetApp = request.targetApp.trimmed().isEmpty()
        ? QStringLiteral("SumatraPDF")
        : request.targetApp.trimmed();
    draft.targetFile = request.file.trimmed();
    draft.locatorType = request.locatorType.trimmed().toLower();
    if (draft.locatorType.isEmpty()) {
        draft.locatorType = request.rect.isValid()
            ? QStringLiteral("sumatrapdf.rect")
            : QStringLiteral("sumatrapdf.page");
    }
    draft.locatorJson = pdfLocatorJson(capture);
    draft.suggestedName = request.name;
    if (draft.suggestedName.trimmed().isEmpty()) {
        const QString fileName = QFileInfo(request.file).fileName();
        draft.suggestedName = draft.locatorType == QLatin1String("sumatrapdf.search")
            ? QStringLiteral("%1: %2").arg(fileName, request.searchText.simplified().left(48))
            : QStringLiteral("%1 page %2").arg(fileName).arg(request.page);
    }
    draft.aliases = request.aliases;
    draft.tags = request.tags;
    draft.pinned = request.pinned;
    draft.provenance = request.source;
    return draft;
}

AnchorCaptureCommitService::AnchorCaptureCommitService(
    ILibraryRepository &repository)
    : repository_(repository)
{
}

AnchorCaptureCommitResult AnchorCaptureCommitService::commit(
    const AnchorCaptureDraft &draft)
{
    AnchorCaptureCommitResult result;
    QString validationError;
    if (!draft.isValid(&validationError)) {
        result.error = validationError;
        return result;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    Resource resource;
    resource.id = newIdentity(QStringLiteral("captured-anchor"));
    resource.kind = resourceKindForDraft(draft);
    resource.location = draft.targetFile.trimmed().isEmpty()
        ? draft.targetUri.trimmed()
        : draft.targetFile.trimmed();
    const QString fileName = QFileInfo(resource.location).fileName();
    resource.title = fileName.isEmpty()
        ? QStringLiteral("Anchor target: %1").arg(draft.suggestedName)
        : fileName;
    resource.updatedAt = now;

    Anchor anchor;
    anchor.id = QStringLiteral("%1#%2")
                    .arg(resource.id,
                         QUuid::createUuid().toString(QUuid::WithoutBraces));
    anchor.name = draft.suggestedName;
    anchor.targetApp = draft.targetApp.trimmed();
    anchor.targetFile = draft.targetFile.trimmed();
    anchor.targetUri = draft.targetUri.trimmed();
    anchor.locatorType = draft.locatorType.trimmed().toLower();
    anchor.locatorJson = draft.locatorJson.trimmed();
    anchor.aliases = cleanedValues(draft.aliases, false);
    anchor.tags = cleanedValues(draft.tags, true);
    anchor.pinned = draft.pinned;
    anchor.createdAt = now;
    anchor.updatedAt = now;
    resource.anchors = {anchor};

    if (!repository_.upsertResource(resource)) {
        result.resource = resource;
        result.anchor = anchor;
        result.error = repository_.lastError().trimmed().isEmpty()
            ? QStringLiteral("Unable to save captured Anchor")
            : repository_.lastError();
        return result;
    }

    result.resource = resource;
    result.anchor = anchor;
    return result;
}

} // namespace Pinloom
