#include "pinloom/core/PdfXChangeForegroundCapture.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

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
constexpr const char *ForegroundPdfXChangeViewStateSource = "foreground-pdfxchange-viewstate";

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

bool hasPathShape(const QString &value);

QString cleanPdfPathCandidate(QString value)
{
    value = stripOuterDocumentDecorations(value);
    if (value.isEmpty()) {
        return {};
    }

    const int pdfIndex = value.lastIndexOf(QStringLiteral(".pdf"), -1, Qt::CaseInsensitive);
    if (pdfIndex < 0) {
        return {};
    }
    value = value.left(pdfIndex + 4).trimmed();
    value = stripOuterDocumentDecorations(value);
    if (value.isEmpty()) {
        return {};
    }

    if (value.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl url(value);
        if (url.isLocalFile()) {
            value = url.toLocalFile();
        }
    }

    if (!hasPathShape(value)) {
        return {};
    }

    return QDir::cleanPath(QDir::fromNativeSeparators(value));
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

QString viewStateStatus(const PdfXChangeViewState &viewState)
{
    if (!viewState.hasAnyViewState()) {
        const QString diagnostics = viewState.diagnostics.trimmed();
        return diagnostics.isEmpty()
            ? QStringLiteral("PDF-XChange view state unavailable; page defaults to 1 and rectangle is full-page fallback")
            : QStringLiteral("%1; page defaults to 1 and rectangle is full-page fallback").arg(diagnostics);
    }

    QStringList parts;
    if (viewState.hasCurrentPage()) {
        QString pageText = QStringLiteral("page %1").arg(viewState.currentPage);
        if (viewState.totalPages > 0) {
            pageText += QStringLiteral(" of %1").arg(viewState.totalPages);
        }
        parts.append(pageText);
    } else {
        parts.append(QStringLiteral("page unavailable; defaulting to 1"));
    }

    parts.append(viewState.hasZoom()
                     ? QStringLiteral("zoom %1%").arg(decimalText(viewState.zoom))
                     : QStringLiteral("zoom unavailable"));
    parts.append(QStringLiteral("rectangle is full-page fallback"));
    return QStringLiteral("Captured PDF-XChange view state: %1").arg(parts.join(QStringLiteral("; ")));
}

void applyViewState(PdfXChangeForegroundCaptureResult &result, const PdfXChangeViewState &viewState)
{
    result.viewState = viewState;
    if (viewState.hasCurrentPage()) {
        result.request.page = viewState.currentPage;
    }
    if (viewState.hasZoom()) {
        result.request.zoom = viewState.zoom;
    }
    if (viewState.hasAnyViewState()) {
        result.request.source = QString::fromLatin1(ForegroundPdfXChangeViewStateSource);
    }
    result.status = viewStateStatus(viewState);
}

PdfXChangeForegroundCaptureResult resultForMatchedResource(
    const QString &documentTitle,
    const Resource &resource,
    const PdfXChangeViewState &viewState)
{
    PdfXChangeForegroundCaptureResult result = resultForMatchedResource(documentTitle, resource);
    applyViewState(result, viewState);
    return result;
}

PdfXChangeForegroundCaptureResult resultForResolvedFilePath(const QString &documentTitle, const QString &filePath)
{
    PdfXChangeForegroundCaptureResult result;
    result.recognizedPdfXChange = true;
    result.documentTitle = stripOuterDocumentDecorations(documentTitle);

    const QFileInfo fileInfo(filePath);
    const QString defaultName = fileInfo.completeBaseName().trimmed().isEmpty()
        ? stripOuterDocumentDecorations(documentTitle)
        : fileInfo.completeBaseName().trimmed();

    result.request.name = defaultName;
    result.request.file = QDir::cleanPath(QDir::fromNativeSeparators(filePath.trimmed()));
    result.request.page = 1;
    result.request.rect = {0.0, 0.0, 612.0, 792.0};
    result.request.zoom = -1.0;
    result.request.unit = QStringLiteral("pt");
    result.request.source = QString::fromLatin1(ForegroundPdfXChangeFallbackSource);
    result.request.targetApp = QStringLiteral("PDF-XChange");
    return result;
}

PdfXChangeForegroundCaptureResult resultForResolvedFilePath(
    const QString &documentTitle,
    const QString &filePath,
    const PdfXChangeViewState &viewState)
{
    PdfXChangeForegroundCaptureResult result = resultForResolvedFilePath(documentTitle, filePath);
    applyViewState(result, viewState);
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

BOOL CALLBACK collectChildWindowText(HWND window, LPARAM lParam)
{
    auto *fragments = reinterpret_cast<QStringList *>(lParam);
    if (!fragments) {
        return TRUE;
    }

    const QString text = windowTitleForHandle(window).trimmed();
    if (!text.isEmpty() && !fragments->contains(text)) {
        fragments->append(text);
    }
    return TRUE;
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

bool PdfXChangeViewState::hasCurrentPage() const
{
    return currentPage > 0;
}

bool PdfXChangeViewState::hasZoom() const
{
    return std::isfinite(zoom) && zoom > 0.0;
}

bool PdfXChangeViewState::hasAnyViewState() const
{
    return hasCurrentPage() || totalPages > 0 || hasZoom();
}

bool PdfXChangeForegroundCaptureResult::success() const
{
    return recognizedPdfXChange
        && !request.file.trimmed().isEmpty();
}

PdfXChangeForegroundCaptureProvider::PdfXChangeForegroundCaptureProvider(
    const ILibraryRepository &repository)
    : repository_(repository)
    , viewStateProvider_(capturePdfXChangeViewState)
{
}

PdfXChangeForegroundCaptureProvider::PdfXChangeForegroundCaptureProvider(
    const ILibraryRepository &repository,
    ViewStateProvider viewStateProvider)
    : repository_(repository)
    , viewStateProvider_(std::move(viewStateProvider))
{
}

PdfXChangeForegroundCaptureResult PdfXChangeForegroundCaptureProvider::capture(
    const ForegroundAppWindowContext &context) const
{
    const PdfXChangeViewState viewState = viewStateProvider_
        ? viewStateProvider_(context)
        : PdfXChangeViewState{};
    return capturePdfXChangeForegroundContext(repository_, context, viewState);
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

QString pdfXChangeDocumentPathFromWindowTitle(const QString &windowTitle)
{
    const QString documentTitle = pdfXChangeDocumentTitleFromWindowTitle(windowTitle);
    QString path = cleanPdfPathCandidate(documentTitle);
    if (!path.isEmpty()) {
        return path;
    }

    QString title = stripOuterDocumentDecorations(windowTitle);
    const QString prefix = QStringLiteral("PDF-XChange Editor - ");
    if (title.startsWith(prefix, Qt::CaseInsensitive)) {
        path = cleanPdfPathCandidate(title.mid(prefix.size()));
        if (!path.isEmpty()) {
            return path;
        }
    }

    path = cleanPdfPathCandidate(title);
    return path;
}

PdfXChangeViewState parsePdfXChangeViewStateText(const QString &text, const QString &source)
{
    PdfXChangeViewState state;
    state.source = source.trimmed().isEmpty() ? QStringLiteral("text") : source.trimmed();

    const QString normalized = text.simplified();
    if (normalized.isEmpty()) {
        state.diagnostics = QStringLiteral("PDF-XChange view state text is empty");
        return state;
    }

    const QList<QRegularExpression> pageExpressions = {
        QRegularExpression(QStringLiteral("\\bpage\\s*[:#]?\\s*(\\d{1,6})\\s*(?:/|of)\\s*(\\d{1,6})\\b"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("\\b(\\d{1,6})\\s*(?:/|of)\\s*(\\d{1,6})\\s*(?:pages?)?\\b"),
                           QRegularExpression::CaseInsensitiveOption),
    };
    for (const QRegularExpression &expression : pageExpressions) {
        const QRegularExpressionMatch match = expression.match(normalized);
        if (!match.hasMatch()) {
            continue;
        }

        const int currentPage = match.captured(1).toInt();
        const int totalPages = match.captured(2).toInt();
        if (currentPage > 0 && totalPages > 0 && totalPages >= currentPage) {
            state.currentPage = currentPage;
            state.totalPages = totalPages;
            break;
        }
    }

    if (!state.hasCurrentPage()) {
        const QRegularExpression pageOnly(
            QStringLiteral("\\bpage\\s*[:#]?\\s*(\\d{1,6})\\b"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = pageOnly.match(normalized);
        if (match.hasMatch()) {
            const int currentPage = match.captured(1).toInt();
            if (currentPage > 0) {
                state.currentPage = currentPage;
            }
        }
    }

    const QList<QRegularExpression> zoomExpressions = {
        QRegularExpression(QStringLiteral("\\bzoom\\s*[:=]?\\s*(\\d{1,4}(?:[\\.,]\\d+)?)\\s*%"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("\\b(\\d{1,4}(?:[\\.,]\\d+)?)\\s*%"),
                           QRegularExpression::CaseInsensitiveOption),
    };
    for (const QRegularExpression &expression : zoomExpressions) {
        const QRegularExpressionMatch match = expression.match(normalized);
        if (!match.hasMatch()) {
            continue;
        }

        QString zoomText = match.captured(1);
        zoomText.replace(QLatin1Char(','), QLatin1Char('.'));
        bool ok = false;
        const double parsedZoom = zoomText.toDouble(&ok);
        if (ok && std::isfinite(parsedZoom) && parsedZoom > 0.0) {
            state.zoom = parsedZoom;
            break;
        }
    }

    if (!state.hasAnyViewState()) {
        state.diagnostics = QStringLiteral("PDF-XChange view state text did not contain a current page or zoom");
        return state;
    }

    QStringList missing;
    if (!state.hasCurrentPage()) {
        missing.append(QStringLiteral("current page"));
    }
    if (state.hasCurrentPage() && state.totalPages <= 0) {
        missing.append(QStringLiteral("total pages"));
    }
    if (!state.hasZoom()) {
        missing.append(QStringLiteral("zoom"));
    }
    if (!missing.isEmpty()) {
        state.diagnostics = QStringLiteral("PDF-XChange view state parsed partially; missing %1")
                                .arg(missing.join(QStringLiteral(", ")));
    }
    return state;
}

PdfXChangeViewState capturePdfXChangeViewState(const ForegroundAppWindowContext &context)
{
    PdfXChangeViewState state;
    state.source = QStringLiteral("win32-window-text");

    if (!context.isValid() || !isPdfXChangeForegroundWindow(context)) {
        state.diagnostics = QStringLiteral("Foreground window is not PDF-XChange");
        return state;
    }

    QStringList fragments;
    if (!context.windowTitle.trimmed().isEmpty()) {
        fragments.append(context.windowTitle.trimmed());
    }

#ifdef Q_OS_WIN
    if (context.windowHandle != 0) {
        HWND window = reinterpret_cast<HWND>(context.windowHandle);
        const QString rootText = windowTitleForHandle(window).trimmed();
        if (!rootText.isEmpty() && !fragments.contains(rootText)) {
            fragments.append(rootText);
        }
        EnumChildWindows(window, collectChildWindowText, reinterpret_cast<LPARAM>(&fragments));
    }
#endif

    if (fragments.isEmpty()) {
        state.diagnostics = QStringLiteral("No PDF-XChange window text was readable");
        return state;
    }

    state = parsePdfXChangeViewStateText(fragments.join(QLatin1Char('\n')),
                                         QStringLiteral("win32-window-text"));
    return state;
}

PdfXChangeForegroundCaptureResult capturePdfXChangeForegroundContext(
    const ILibraryRepository &repository,
    const ForegroundAppWindowContext &context)
{
    return capturePdfXChangeForegroundContext(repository, context, capturePdfXChangeViewState(context));
}

PdfXChangeForegroundCaptureResult capturePdfXChangeForegroundContext(
    const ILibraryRepository &repository,
    const ForegroundAppWindowContext &context,
    const PdfXChangeViewState &viewState)
{
    PdfXChangeForegroundCaptureResult result;
    if (!context.isValid() || !isPdfXChangeForegroundWindow(context)) {
        result.status = QStringLiteral("Open or focus a PDF-XChange PDF before k n");
        return result;
    }

    result.recognizedPdfXChange = true;
    result.documentTitle = pdfXChangeDocumentTitleFromWindowTitle(context.windowTitle);
    const QString documentPath = pdfXChangeDocumentPathFromWindowTitle(context.windowTitle);
    if (!documentPath.trimmed().isEmpty()) {
        return resultForResolvedFilePath(result.documentTitle.trimmed().isEmpty()
                                             ? documentPath
                                             : result.documentTitle,
                                         documentPath,
                                         viewState);
    }

    if (result.documentTitle.trimmed().isEmpty()) {
        result.status = QStringLiteral("Foreground PDF-XChange window did not expose a PDF document title or file path");
        return result;
    }

    QList<Resource> matches;
    for (const Resource &resource : uniquePdfResources(repository)) {
        if (documentMatchesResource(result.documentTitle, resource)) {
            matches.append(resource);
        }
    }

    if (matches.isEmpty()) {
        result.status = QStringLiteral("Foreground PDF-XChange document \"%1\" did not expose a full PDF file path")
                            .arg(result.documentTitle);
        return result;
    }
    if (matches.size() > 1) {
        result.status = QStringLiteral("Foreground PDF-XChange document \"%1\" did not expose a full PDF file path and matches multiple indexed PDFs")
                            .arg(result.documentTitle);
        return result;
    }

    return resultForMatchedResource(result.documentTitle, matches.first(), viewState);
}

} // namespace Pinloom
