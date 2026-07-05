#include "pinloom/core/PdfXChangeForegroundCapture.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <algorithm>
#include <optional>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#endif

namespace Pinloom {

namespace {

constexpr const char *ForegroundPdfXChangeFallbackSource = "foreground-pdfxchange-fallback";

QString stripOuterDocumentDecorations(QString value)
{
    value = value.trimmed();
    while (value.startsWith(QLatin1Char('*'))) {
        value.remove(0, 1);
        value = value.trimmed();
    }
    if (value.size() >= 2
        && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))) {
        value = value.mid(1, value.size() - 2).trimmed();
    }
    return value;
}

QString withoutPdfExtension(QString value)
{
    value = stripOuterDocumentDecorations(value);
    if (value.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        value.chop(4);
    }
    return value.trimmed();
}

QString normalizedNameKey(const QString &value)
{
    return withoutPdfExtension(value).simplified().toCaseFolded();
}

QString normalizedPathKey(const QString &value)
{
    const QString trimmed = stripOuterDocumentDecorations(value);
    if (trimmed.isEmpty()) {
        return {};
    }
    QString normalized = QDir::cleanPath(trimmed);
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return normalized.toCaseFolded();
}

void appendUniqueKey(QStringList &keys, const QString &key)
{
    const QString trimmed = key.trimmed();
    if (!trimmed.isEmpty() && !keys.contains(trimmed, Qt::CaseInsensitive)) {
        keys.append(trimmed);
    }
}

void appendNameKey(QStringList &keys, const QString &value)
{
    const QString decorated = stripOuterDocumentDecorations(value);
    appendUniqueKey(keys, normalizedNameKey(decorated));
    const QFileInfo info(decorated);
    appendUniqueKey(keys, normalizedNameKey(info.fileName()));
    appendUniqueKey(keys, normalizedNameKey(info.completeBaseName()));
}

QStringList documentNameKeys(const QString &documentTitle)
{
    QStringList keys;
    appendNameKey(keys, documentTitle);
    return keys;
}

QStringList resourceNameKeys(const Resource &resource)
{
    QStringList keys;
    appendNameKey(keys, resource.title);
    appendNameKey(keys, QFileInfo(resource.location).fileName());
    appendNameKey(keys, QFileInfo(resource.location).completeBaseName());
    return keys;
}

bool hasPathShape(const QString &value)
{
    return value.contains(QLatin1Char('/'))
        || value.contains(QLatin1Char('\\'))
        || value.contains(QLatin1Char(':'));
}

bool documentMatchesResource(const QString &documentTitle, const Resource &resource)
{
    const QStringList documentKeys = documentNameKeys(documentTitle);
    const QStringList resourceKeys = resourceNameKeys(resource);
    for (const QString &documentKey : documentKeys) {
        if (resourceKeys.contains(documentKey, Qt::CaseInsensitive)) {
            return true;
        }
    }

    if (hasPathShape(documentTitle)) {
        const QString documentPath = normalizedPathKey(documentTitle);
        const QString resourcePath = normalizedPathKey(resource.location);
        return !documentPath.isEmpty() && documentPath == resourcePath;
    }

    return false;
}

QString defaultAnchorNameForDocument(const QString &documentTitle, const Resource &resource)
{
    const QString title = stripOuterDocumentDecorations(documentTitle);
    if (!title.isEmpty()) {
        return title;
    }
    const QString fileBaseName = QFileInfo(resource.location).completeBaseName().trimmed();
    if (!fileBaseName.isEmpty()) {
        return fileBaseName;
    }
    return resource.title.trimmed();
}

QList<Resource> uniquePdfResources(const ILibraryRepository &repository)
{
    SearchQuery query;
    query.requiredKinds = {ResourceKind::Pdf};
    query.limit = 0;

    QList<Resource> resources;
    QStringList seenIds;
    for (const SearchResult &result : repository.search(query)) {
        if (result.resource.kind != ResourceKind::Pdf) {
            continue;
        }
        if (!result.resource.id.isEmpty() && seenIds.contains(result.resource.id)) {
            continue;
        }
        seenIds.append(result.resource.id);
        resources.append(result.resource);
    }
    return resources;
}

PdfXChangeForegroundCaptureResult resultForMatchedResource(const QString &documentTitle, const Resource &resource)
{
    PdfXChangeForegroundCaptureResult result;
    result.recognizedPdfXChange = true;
    result.matchedResource = true;
    result.documentTitle = stripOuterDocumentDecorations(documentTitle);
    result.matchedResourceId = resource.id;

    result.request.name = defaultAnchorNameForDocument(documentTitle, resource);
    result.request.file = resource.location.trimmed();
    result.request.page = 1;
    result.request.rect = {0.0, 0.0, 612.0, 792.0};
    result.request.zoom = -1.0;
    result.request.unit = QStringLiteral("pt");
    result.request.source = QString::fromLatin1(ForegroundPdfXChangeFallbackSource);
    result.request.targetApp = QStringLiteral("PDF-XChange");
    return result;
}

#ifdef Q_OS_WIN
QString windowTitleForHandle(HWND window)
{
    if (!window) {
        return {};
    }
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }

    std::wstring buffer(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, buffer.data(), static_cast<int>(buffer.size()));
    if (copied <= 0) {
        return {};
    }
    return QString::fromWCharArray(buffer.c_str(), copied);
}

QString processPathForId(DWORD processId)
{
    if (processId == 0) {
        return {};
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        return {};
    }

    std::wstring buffer(32768, L'\0');
    DWORD length = static_cast<DWORD>(buffer.size());
    QString path;
    if (QueryFullProcessImageNameW(process, 0, buffer.data(), &length) && length > 0) {
        path = QString::fromWCharArray(buffer.data(), static_cast<int>(length));
    }
    CloseHandle(process);
    return path;
}
#endif

} // namespace

bool ForegroundAppWindowContext::isValid() const
{
    return windowHandle != 0
        || !windowTitle.trimmed().isEmpty()
        || !processName.trimmed().isEmpty()
        || !processPath.trimmed().isEmpty()
        || processId != 0;
}

bool PdfXChangeForegroundCaptureResult::success() const
{
    return recognizedPdfXChange
        && matchedResource
        && !request.file.trimmed().isEmpty();
}

PdfXChangeForegroundCaptureProvider::PdfXChangeForegroundCaptureProvider(
    const ILibraryRepository &repository)
    : repository_(repository)
{
}

PdfXChangeForegroundCaptureResult PdfXChangeForegroundCaptureProvider::capture(
    const ForegroundAppWindowContext &context) const
{
    return capturePdfXChangeForegroundContext(repository_, context);
}

PdfXChangeForegroundCaptureResult PdfXChangeForegroundCaptureProvider::captureCurrentForeground() const
{
    return capture(currentForegroundAppWindowContext());
}

ForegroundAppWindowContext currentForegroundAppWindowContext()
{
    ForegroundAppWindowContext context;
#ifdef Q_OS_WIN
    HWND window = GetForegroundWindow();
    if (!window) {
        return context;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);

    context.windowHandle = reinterpret_cast<quintptr>(window);
    context.processId = static_cast<quint32>(processId);
    context.windowTitle = windowTitleForHandle(window);
    context.processPath = processPathForId(processId);
    context.processName = QFileInfo(context.processPath).fileName();
#endif
    return context;
}

bool isPdfXChangeForegroundWindow(const ForegroundAppWindowContext &context)
{
    const QString title = context.windowTitle.trimmed();
    const QString processName = QFileInfo(context.processName.trimmed()).fileName();
    const QString processPath = context.processPath.trimmed();

    return title.contains(QStringLiteral("PDF-XChange Editor"), Qt::CaseInsensitive)
        || processName.compare(QStringLiteral("PDFXEdit.exe"), Qt::CaseInsensitive) == 0
        || processName.compare(QStringLiteral("PXCEditor.exe"), Qt::CaseInsensitive) == 0
        || processName.contains(QStringLiteral("PDFXEdit"), Qt::CaseInsensitive)
        || processName.contains(QStringLiteral("PXCEditor"), Qt::CaseInsensitive)
        || processPath.contains(QStringLiteral("PDF-XChange Editor"), Qt::CaseInsensitive);
}

QString pdfXChangeDocumentTitleFromWindowTitle(const QString &windowTitle)
{
    QString title = stripOuterDocumentDecorations(windowTitle);
    const QString marker = QStringLiteral(" - PDF-XChange Editor");
    const int markerIndex = title.indexOf(marker, 0, Qt::CaseInsensitive);
    if (markerIndex >= 0) {
        return stripOuterDocumentDecorations(title.left(markerIndex));
    }

    if (title.compare(QStringLiteral("PDF-XChange Editor"), Qt::CaseInsensitive) == 0) {
        return {};
    }

    if (title.endsWith(QStringLiteral("PDF-XChange Editor"), Qt::CaseInsensitive)) {
        title.chop(QStringLiteral("PDF-XChange Editor").size());
        while (title.endsWith(QLatin1Char('-')) || title.endsWith(QLatin1Char(':'))) {
            title.chop(1);
            title = title.trimmed();
        }
        return stripOuterDocumentDecorations(title);
    }

    return {};
}

PdfXChangeForegroundCaptureResult capturePdfXChangeForegroundContext(
    const ILibraryRepository &repository,
    const ForegroundAppWindowContext &context)
{
    PdfXChangeForegroundCaptureResult result;
    if (!context.isValid() || !isPdfXChangeForegroundWindow(context)) {
        return result;
    }

    result.recognizedPdfXChange = true;
    result.documentTitle = pdfXChangeDocumentTitleFromWindowTitle(context.windowTitle);
    if (result.documentTitle.trimmed().isEmpty()) {
        result.status = QStringLiteral("Foreground PDF-XChange window did not expose a document title");
        return result;
    }

    QList<Resource> matches;
    for (const Resource &resource : uniquePdfResources(repository)) {
        if (documentMatchesResource(result.documentTitle, resource)) {
            matches.append(resource);
        }
    }

    if (matches.isEmpty()) {
        result.status = QStringLiteral("Foreground PDF-XChange document \"%1\" is not matched to a unique indexed PDF; select or index the PDF in Pinloom first")
                            .arg(result.documentTitle);
        return result;
    }
    if (matches.size() > 1) {
        result.status = QStringLiteral("Foreground PDF-XChange document \"%1\" matches multiple indexed PDFs; select the exact PDF in Pinloom first")
                            .arg(result.documentTitle);
        return result;
    }

    return resultForMatchedResource(result.documentTitle, matches.first());
}

} // namespace Pinloom
