#include "pinloom/core/ManualPdfAnchorCreation.h"

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

QString decimalText(double value)
{
    QString text = QString::number(value, 'f', 2);
    while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0'))) {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }
    return text;
}

QString manualPdfAnchorResourceId()
{
    return QStringLiteral("manual-pdf-anchor:%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QString resourceTitleForFile(const QString &file, const QString &name)
{
    const QString fileName = QFileInfo(file).fileName().trimmed();
    return fileName.isEmpty() ? name : fileName;
}

PdfXChangeCaptureRequest captureRequestFromManualRequest(
    const ManualPdfAnchorCreationRequest &request,
    const QString &name,
    const QString &file)
{
    PdfXChangeCaptureRequest captureRequest;
    captureRequest.anchorName = name;
    captureRequest.targetApp = request.targetApp;
    captureRequest.targetFile = file;
    captureRequest.page = request.page;
    captureRequest.rect = request.rect;
    captureRequest.zoom = request.zoom;
    captureRequest.unit = request.unit;
    captureRequest.source = request.source;
    return captureRequest;
}

} // namespace

bool ManualPdfAnchorCreationResult::success() const
{
    return error.isEmpty();
}

QString manualPdfAnchorLocatorSummary(const ManualPdfAnchorCreationRequest &request)
{
    QStringList parts;
    const QString file = request.file.trimmed();
    parts.append(file.isEmpty()
                     ? QStringLiteral("PDF file unknown")
                     : QStringLiteral("PDF file %1").arg(file));
    parts.append(request.page > 0
                     ? QStringLiteral("page %1").arg(request.page)
                     : QStringLiteral("page unknown"));
    if (request.rect.isValid()) {
        parts.append(QStringLiteral("rect %1,%2,%3,%4 %5")
                         .arg(decimalText(request.rect.left),
                              decimalText(request.rect.top),
                              decimalText(request.rect.right),
                              decimalText(request.rect.bottom),
                              request.unit.trimmed().isEmpty() ? QStringLiteral("pt") : request.unit.trimmed()));
    } else {
        parts.append(QStringLiteral("rect unknown"));
    }
    parts.append(request.zoom > 0.0
                     ? QStringLiteral("zoom %1%").arg(decimalText(request.zoom))
                     : QStringLiteral("zoom unknown"));

    const QString source = request.source.trimmed();
    if (!source.isEmpty()) {
        parts.append(QStringLiteral("source %1").arg(source));
    }
    return parts.join(QStringLiteral(" | "));
}

ManualPdfAnchorCreationService::ManualPdfAnchorCreationService(ILibraryRepository &repository)
    : repository_(repository)
{
}

ManualPdfAnchorCreationResult ManualPdfAnchorCreationService::createManualPdfXChangeRectAnchor(
    const ManualPdfAnchorCreationRequest &request)
{
    ManualPdfAnchorCreationResult result;

    const QString name = request.name.trimmed();
    if (name.isEmpty()) {
        result.error = QStringLiteral("Manual PDF anchor name is missing");
        return result;
    }

    const QString file = request.file.trimmed();
    if (file.isEmpty()) {
        result.error = QStringLiteral("Manual PDF anchor file is missing");
        return result;
    }

    const AnchorCaptureResult capture = captureManualPdfXChangeRectAnchor(
        captureRequestFromManualRequest(request, name, file));
    if (!capture.success()) {
        result.error = capture.error;
        return result;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    Resource resource;
    resource.id = manualPdfAnchorResourceId();
    resource.kind = ResourceKind::Pdf;
    resource.title = resourceTitleForFile(file, name);
    resource.location = file;
    resource.updatedAt = now;

    Anchor anchor = capture.anchor;
    anchor.id = QStringLiteral("%1#rect").arg(resource.id);
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
        result.error = QStringLiteral("Unable to save manual PDF anchor");
        return result;
    }

    result.resource = resource;
    result.anchor = anchor;
    return result;
}

} // namespace Pinloom
