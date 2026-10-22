#include "pinloom/core/AnchorCaptureDraft.h"

#include <QDateTime>
#include <QDir>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUuid>
#include <QSet>
#include <algorithm>

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

QList<Resource> captureResources(ILibraryRepository &repository)
{
    SearchQuery query; query.includeDeleted = true; query.limit = 0;
    QSet<QString> seen;
    QList<Resource> resources;
    for (const auto &hit : repository.search(query)) {
        if (seen.contains(hit.resource.id)) continue;
        seen.insert(hit.resource.id);
        if (const auto resource = repository.findResource(hit.resource.id)) resources.append(*resource);
    }
    std::sort(resources.begin(), resources.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return resources;
}

QString fileIdentity(const QString &path)
{
    const QFileInfo file(path);
    const auto canonical = file.canonicalFilePath();
    auto result = QDir::cleanPath(canonical.isEmpty() ? file.absoluteFilePath() : canonical);
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
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

AnchorCaptureCommitResult AnchorCaptureCommitService::findCapture(const QString &captureId) const
{
    AnchorCaptureCommitResult result;
    const QUuid uuid(captureId);
    if (uuid.isNull()) {
        result.error = QStringLiteral("Capture ID must be a UUID");
        result.errorCode = QStringLiteral("invalid_capture");
        return result;
    }
    const auto id = uuid.toString(QUuid::WithoutBraces);
    for (const auto &resource : captureResources(repository_)) {
        for (const auto &anchor : resource.anchors) {
            const auto receipt = QJsonDocument::fromJson(anchor.locatorJson.toUtf8()).object()["captureReceipt"].toObject();
            if (anchor.id == "native-capture:" + id || receipt["id"].toString() == id) {
                result.resource = resource; result.anchor = anchor; result.replayed = true;
                return result;
            }
        }
    }
    result.error = QStringLiteral("Capture ID was not found");
    result.errorCode = QStringLiteral("not_found");
    return result;
}

AnchorCaptureCommitResult AnchorCaptureCommitService::commit(
    const AnchorCaptureDraft &draft, const QString &captureId)
{
    AnchorCaptureCommitResult result;
    QString validationError;
    if (!draft.isValid(&validationError)) {
        result.error = validationError;
        result.errorCode = QStringLiteral("invalid_capture");
        return result;
    }

    const auto requestUuid = QUuid(captureId);
    if (!captureId.isEmpty() && requestUuid.isNull()) {
        result.error = QStringLiteral("Capture ID must be a UUID");
        result.errorCode = QStringLiteral("invalid_capture");
        return result;
    }
    const QString stableId = captureId.isEmpty() ? QString()
        : QStringLiteral("native-capture:%1").arg(requestUuid.toString(QUuid::WithoutBraces));
    auto locator = QJsonDocument::fromJson(draft.locatorJson.toUtf8()).object();
    locator.remove(QStringLiteral("captureReceipt"));
    const QJsonObject request{{"targetApp", draft.targetApp.trimmed()},
        {"targetFile", draft.targetFile.trimmed()}, {"targetUri", draft.targetUri.trimmed()},
        {"locatorType", draft.locatorType.trimmed().toLower()}, {"locator", locator},
        {"name", draft.suggestedName}, {"aliases", QJsonArray::fromStringList(cleanedValues(draft.aliases, false))},
        {"tags", QJsonArray::fromStringList(cleanedValues(draft.tags, true))}, {"pinned", draft.pinned}};
    const QString requestHash = QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(request).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    if (!stableId.isEmpty()) {
        auto existing = findCapture(captureId);
        if (existing.success()) {
            const auto receipt = QJsonDocument::fromJson(existing.anchor.locatorJson.toUtf8()).object()["captureReceipt"].toObject();
            if (receipt["requestHash"].toString() == requestHash) return existing;
            result.error = QStringLiteral("Capture ID already belongs to a different saved request");
            result.errorCode = QStringLiteral("capture_conflict");
            return result;
        }
        locator.insert(QStringLiteral("captureReceipt"), QJsonObject{
            {"id", requestUuid.toString(QUuid::WithoutBraces)}, {"requestHash", requestHash}});
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
    if (!stableId.isEmpty()) {
        for (const auto &candidate : captureResources(repository_)) {
            if (!candidate.deleted && fileIdentity(candidate.location) == fileIdentity(draft.targetFile)) {
                resource = candidate;
                break;
            }
        }
    }
    resource.updatedAt = now;

    Anchor anchor;
    anchor.id = stableId.isEmpty() ? QStringLiteral("%1#%2")
                    .arg(resource.id,
                         QUuid::createUuid().toString(QUuid::WithoutBraces)) : stableId;
    anchor.name = draft.suggestedName;
    anchor.targetApp = draft.targetApp.trimmed();
    anchor.targetFile = draft.targetFile.trimmed();
    anchor.targetUri = draft.targetUri.trimmed();
    anchor.locatorType = draft.locatorType.trimmed().toLower();
    anchor.locatorJson = stableId.isEmpty() ? draft.locatorJson.trimmed()
        : QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));
    anchor.aliases = cleanedValues(draft.aliases, false);
    anchor.tags = cleanedValues(draft.tags, true);
    anchor.pinned = draft.pinned;
    anchor.createdAt = now;
    anchor.updatedAt = now;
    resource.anchors.append(anchor);

    if (!repository_.upsertResource(resource)) {
        result.resource = resource;
        result.anchor = anchor;
        result.error = repository_.lastError().trimmed().isEmpty()
            ? QStringLiteral("Unable to save captured Anchor")
            : repository_.lastError();
        result.errorCode = QStringLiteral("storage_error");
        return result;
    }

    // Repositories normalize metadata on write. Return the persisted representation
    // so an immediate retry/get has the same version and identifiers.
    result.resource = repository_.findResource(resource.id).value_or(resource);
    result.anchor = anchor;
    for (const auto &saved : result.resource.anchors) {
        if (saved.id == anchor.id) { result.anchor = saved; break; }
    }
    return result;
}

} // namespace Pinloom
