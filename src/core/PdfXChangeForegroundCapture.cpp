#include "pinloom/core/PdfXChangeForegroundCapture.h"

#include "pinloom/core/LegacyCompatibility.h"

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
#include <ole2.h>
#include <oleauto.h>
#include <uiautomation.h>
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
    return normalizedPdfXChangeDocumentTitleKey(value);
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

QString cleanFullPdfPath(QString value)
{
    const QString cleaned = cleanPdfPathCandidate(std::move(value));
    if (cleaned.isEmpty()) {
        return {};
    }
    if (!cleaned.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        return {};
    }
    return cleaned;
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

bool isPdfResourceCandidate(const Resource &resource)
{
    return normalizedResourceKind(resource.kind) == ResourceKind::Pdf
        || resource.location.trimmed().endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive);
}

QList<Resource> uniquePdfResources(const ILibraryRepository &repository)
{
    SearchQuery query;
    query.limit = 0;

    QList<Resource> resources;
    QStringList seenIds;
    for (const SearchResult &result : repository.search(query)) {
        if (!isPdfResourceCandidate(result.resource)) {
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

QStringList resourceIds(const QList<Resource> &resources)
{
    QStringList ids;
    for (const Resource &resource : resources) {
        if (!resource.id.trimmed().isEmpty()) {
            ids.append(resource.id);
        }
    }
    ids.removeDuplicates();
    return ids;
}

PdfXChangeForegroundCaptureResult resultForMatchedResource(const QString &documentTitle, const Resource &resource)
{
    PdfXChangeForegroundCaptureResult result;
    result.recognizedPdfXChange = true;
    result.matchedResource = true;
    result.documentTitle = stripOuterDocumentDecorations(documentTitle);
    result.matchedResourceId = resource.id;
    result.matchedResourceIds = {resource.id};

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

PdfXChangeForegroundCaptureResult resultNeedsFileConfirmation(
    const QString &documentTitle,
    const PdfXChangeViewState &viewState,
    const QString &status,
    const QList<Resource> &matches = {})
{
    PdfXChangeForegroundCaptureResult result;
    result.recognizedPdfXChange = true;
    result.documentTitle = stripOuterDocumentDecorations(documentTitle);
    result.viewState = viewState;
    result.status = status;
    result.needsFileConfirmation = true;
    result.matchedResourceIds = resourceIds(matches);
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
QString stringFromBstr(BSTR value)
{
    if (!value) {
        return {};
    }
    const QString text = QString::fromWCharArray(value, static_cast<int>(SysStringLen(value)));
    SysFreeString(value);
    return text;
}

void appendUniqueFragment(QStringList &fragments, const QString &value)
{
    const QString text = value.simplified();
    if (!text.isEmpty() && !fragments.contains(text)) {
        fragments.append(text);
    }
}

bool containsViewStateKeyword(const QString &value)
{
    return value.contains(QStringLiteral("page"), Qt::CaseInsensitive)
        || value.contains(QStringLiteral("zoom"), Qt::CaseInsensitive)
        || value.contains(QStringLiteral("\u9875"))
        || value.contains(QStringLiteral("\u9801"))
        || value.contains(QStringLiteral("\u7f29\u653e"));
}

bool isCompactPageOrZoomValue(const QString &value)
{
    const QString text = value.simplified();
    if (text.isEmpty() || text.size() > 32) {
        return false;
    }

    static const QRegularExpression compactValueExpression(
        QStringLiteral("^(?:\\d{1,6}|\\d{1,6}\\s*/\\s*\\d{1,6}|\\d{1,4}(?:[\\.,]\\d+)?\\s*%)$"));
    return compactValueExpression.match(text).hasMatch();
}

bool shouldAppendTargetedAutomationText(const QString &name,
                                        const QString &value,
                                        CONTROLTYPEID controlType)
{
    const QString combined = QStringLiteral("%1 %2").arg(name, value).simplified();
    if (containsViewStateKeyword(combined)) {
        return true;
    }

    if (controlType == UIA_EditControlTypeId || controlType == UIA_ComboBoxControlTypeId) {
        return isCompactPageOrZoomValue(value);
    }

    return false;
}

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

QString crossProcessWindowTextForHandle(HWND window)
{
    if (!window) {
        return {};
    }

    constexpr UINT TextReadTimeoutMs = 30;

    DWORD_PTR lengthResult = 0;
    const LRESULT lengthRead = SendMessageTimeoutW(window,
                                                  WM_GETTEXTLENGTH,
                                                  0,
                                                  0,
                                                  SMTO_ABORTIFHUNG,
                                                  TextReadTimeoutMs,
                                                  &lengthResult);
    const DWORD_PTR requestedLength = lengthRead != 0 && lengthResult > 0
                                          ? std::min<DWORD_PTR>(lengthResult + 1, 4096)
                                          : 1024;

    std::wstring buffer(static_cast<size_t>(requestedLength), L'\0');
    DWORD_PTR copiedResult = 0;
    const LRESULT textRead = SendMessageTimeoutW(window,
                                                WM_GETTEXT,
                                                static_cast<WPARAM>(buffer.size()),
                                                reinterpret_cast<LPARAM>(buffer.data()),
                                                SMTO_ABORTIFHUNG,
                                                TextReadTimeoutMs,
                                                &copiedResult);
    if (textRead == 0 || copiedResult == 0) {
        return {};
    }

    const DWORD_PTR maxCopied = static_cast<DWORD_PTR>(buffer.size() - 1);
    const int copied = static_cast<int>(std::min<DWORD_PTR>(copiedResult, maxCopied));
    if (copied <= 0) {
        return {};
    }
    return QString::fromWCharArray(buffer.data(), copied);
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
    appendUniqueFragment(*fragments, text);
    const QString controlText = crossProcessWindowTextForHandle(window).trimmed();
    appendUniqueFragment(*fragments, controlText);
    return TRUE;
}

void appendTargetedAutomationElementText(IUIAutomationElement *element, QStringList &fragments)
{
    if (!element) {
        return;
    }

    CONTROLTYPEID controlType = 0;
    element->get_CurrentControlType(&controlType);

    QString nameText;
    BSTR text = nullptr;
    if (SUCCEEDED(element->get_CurrentName(&text))) {
        nameText = stringFromBstr(text);
    }

    QString valueText;
    IUIAutomationValuePattern *valuePattern = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId,
                                               IID_PPV_ARGS(&valuePattern)))
        && valuePattern) {
        BSTR value = nullptr;
        if (SUCCEEDED(valuePattern->get_CurrentValue(&value))) {
            valueText = stringFromBstr(value);
        }
        valuePattern->Release();
    }

    if (shouldAppendTargetedAutomationText(nameText, valueText, controlType)) {
        if (!nameText.trimmed().isEmpty() && !valueText.trimmed().isEmpty()) {
            appendUniqueFragment(fragments, QStringLiteral("%1 %2").arg(nameText, valueText));
        }
        appendUniqueFragment(fragments, nameText);
        appendUniqueFragment(fragments, valueText);
    }

    IUIAutomationLegacyIAccessiblePattern *legacyPattern = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId,
                                               IID_PPV_ARGS(&legacyPattern)))
        && legacyPattern) {
        QString legacyName;
        QString legacyValue;
        BSTR legacyText = nullptr;
        if (SUCCEEDED(legacyPattern->get_CurrentName(&legacyText))) {
            legacyName = stringFromBstr(legacyText);
        }
        legacyText = nullptr;
        if (SUCCEEDED(legacyPattern->get_CurrentValue(&legacyText))) {
            legacyValue = stringFromBstr(legacyText);
        }
        if (shouldAppendTargetedAutomationText(legacyName, legacyValue, controlType)) {
            if (!legacyName.trimmed().isEmpty() && !legacyValue.trimmed().isEmpty()) {
                appendUniqueFragment(fragments, QStringLiteral("%1 %2").arg(legacyName, legacyValue));
            }
            appendUniqueFragment(fragments, legacyName);
            appendUniqueFragment(fragments, legacyValue);
        }
        legacyPattern->Release();
    }
}

void appendTargetedAutomationText(IUIAutomation *automation,
                                  IUIAutomationElement *root,
                                  QStringList &fragments)
{
    if (!automation || !root) {
        return;
    }

    IUIAutomationCondition *condition = nullptr;
    if (FAILED(automation->CreateTrueCondition(&condition)) || !condition) {
        return;
    }

    IUIAutomationElementArray *elements = nullptr;
    const HRESULT hr = root->FindAll(TreeScope_Descendants, condition, &elements);
    condition->Release();
    if (FAILED(hr) || !elements) {
        return;
    }

    int length = 0;
    elements->get_Length(&length);
    const int maxElements = std::min(length, 4096);
    for (int index = 0; index < maxElements; ++index) {
        IUIAutomationElement *element = nullptr;
        if (SUCCEEDED(elements->GetElement(index, &element)) && element) {
            appendTargetedAutomationElementText(element, fragments);
            element->Release();
        }
    }
    elements->Release();
}

void appendAutomationElementText(IUIAutomationElement *element, QStringList &fragments)
{
    if (!element) {
        return;
    }

    QString nameText;
    QString valueText;
    QString className;

    BSTR text = nullptr;
    if (SUCCEEDED(element->get_CurrentName(&text))) {
        nameText = stringFromBstr(text);
        appendUniqueFragment(fragments, nameText);
    }

    text = nullptr;
    if (SUCCEEDED(element->get_CurrentClassName(&text))) {
        className = stringFromBstr(text);
    }

    IUIAutomationValuePattern *valuePattern = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId,
                                               IID_PPV_ARGS(&valuePattern)))
        && valuePattern) {
        BSTR value = nullptr;
        if (SUCCEEDED(valuePattern->get_CurrentValue(&value))) {
            valueText = stringFromBstr(value);
            if (!nameText.trimmed().isEmpty() && !valueText.trimmed().isEmpty()) {
                appendUniqueFragment(fragments, QStringLiteral("%1 %2").arg(nameText, valueText));
            }
            appendUniqueFragment(fragments, valueText);
        }
        valuePattern->Release();
    }
    appendUniqueFragment(fragments, className);

    IUIAutomationLegacyIAccessiblePattern *legacyPattern = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId,
                                               IID_PPV_ARGS(&legacyPattern)))
        && legacyPattern) {
        QString legacyName;
        QString legacyValue;
        BSTR legacyText = nullptr;
        if (SUCCEEDED(legacyPattern->get_CurrentName(&legacyText))) {
            legacyName = stringFromBstr(legacyText);
            appendUniqueFragment(fragments, legacyName);
        }
        legacyText = nullptr;
        if (SUCCEEDED(legacyPattern->get_CurrentValue(&legacyText))) {
            legacyValue = stringFromBstr(legacyText);
            if (!legacyName.trimmed().isEmpty() && !legacyValue.trimmed().isEmpty()) {
                appendUniqueFragment(fragments, QStringLiteral("%1 %2").arg(legacyName, legacyValue));
            }
            appendUniqueFragment(fragments, legacyValue);
        }
        legacyText = nullptr;
        if (SUCCEEDED(legacyPattern->get_CurrentDescription(&legacyText))) {
            appendUniqueFragment(fragments, stringFromBstr(legacyText));
        }
        legacyPattern->Release();
    }
}

void collectUiAutomationTextRecursive(IUIAutomationTreeWalker *walker,
                                      IUIAutomationElement *element,
                                      QStringList &fragments,
                                      int depth)
{
    constexpr int MaxAutomationDepth = 12;
    constexpr int MaxAutomationFragments = 2048;

    if (!walker || !element || depth > MaxAutomationDepth
        || fragments.size() > MaxAutomationFragments) {
        return;
    }

    appendAutomationElementText(element, fragments);

    IUIAutomationElement *child = nullptr;
    if (FAILED(walker->GetFirstChildElement(element, &child)) || !child) {
        return;
    }

    while (child && fragments.size() <= MaxAutomationFragments) {
        collectUiAutomationTextRecursive(walker, child, fragments, depth + 1);

        IUIAutomationElement *next = nullptr;
        walker->GetNextSiblingElement(child, &next);
        child->Release();
        child = next;
    }
}

QStringList uiAutomationTextFragmentsForWindow(HWND window, QString *diagnostics)
{
    QStringList fragments;
    if (!window) {
        if (diagnostics) {
            *diagnostics = QStringLiteral("PDF-XChange UI Automation capture has no window handle");
        }
        return fragments;
    }

    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        if (diagnostics) {
            *diagnostics = QStringLiteral("PDF-XChange UI Automation capture could not initialize COM");
        }
        return fragments;
    }

    IUIAutomation *automation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation,
                                  nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&automation));
    if (FAILED(hr) || !automation) {
        if (diagnostics) {
            *diagnostics = QStringLiteral("PDF-XChange UI Automation capture could not create client");
        }
        if (uninitialize) {
            CoUninitialize();
        }
        return fragments;
    }

    IUIAutomationElement *root = nullptr;
    hr = automation->ElementFromHandle(window, &root);
    if (FAILED(hr) || !root) {
        if (diagnostics) {
            *diagnostics = QStringLiteral("PDF-XChange UI Automation capture could not read the foreground window");
        }
        automation->Release();
        if (uninitialize) {
            CoUninitialize();
        }
        return fragments;
    }

    IUIAutomationTreeWalker *walker = nullptr;
    hr = automation->get_ControlViewWalker(&walker);
    appendTargetedAutomationText(automation, root, fragments);
    if (SUCCEEDED(hr) && walker) {
        collectUiAutomationTextRecursive(walker, root, fragments, 0);
        walker->Release();
    } else if (diagnostics) {
        *diagnostics = QStringLiteral("PDF-XChange UI Automation capture could not create a control walker");
    }

    root->Release();
    automation->Release();
    if (uninitialize) {
        CoUninitialize();
    }
    return fragments;
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

PdfXChangeForegroundCaptureProvider::PdfXChangeForegroundCaptureProvider(
    const ILibraryRepository &repository,
    ViewStateProvider viewStateProvider,
    TitlePathProvider titlePathProvider)
    : repository_(repository)
    , viewStateProvider_(std::move(viewStateProvider))
    , titlePathProvider_(std::move(titlePathProvider))
{
}

PdfXChangeForegroundCaptureResult PdfXChangeForegroundCaptureProvider::capture(
    const ForegroundAppWindowContext &context) const
{
    const PdfXChangeViewState viewState = viewStateProvider_
        ? viewStateProvider_(context)
        : PdfXChangeViewState{};
    std::optional<QString> savedDocumentPath;
    if (titlePathProvider_) {
        const QString documentTitle = pdfXChangeDocumentTitleFromWindowTitle(context.windowTitle);
        const QString key = normalizedPdfXChangeDocumentTitleKey(documentTitle);
        if (!documentTitle.trimmed().isEmpty() && !key.isEmpty()) {
            savedDocumentPath = titlePathProvider_(documentTitle, key);
        }
    }
    return capturePdfXChangeForegroundContext(repository_, context, viewState, savedDocumentPath);
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

QString normalizedPdfXChangeDocumentTitleKey(const QString &documentTitle)
{
    return withoutPdfExtension(documentTitle).simplified().toCaseFolded();
}

bool isPdfXChangeFullPdfPath(const QString &filePath)
{
    return !cleanFullPdfPath(filePath).isEmpty();
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
        QRegularExpression(QStringLiteral("\\bpage(?:\\s+number)?\\s*[:#]?\\s*(\\d{1,6})\\b.*?\\b(?:of|total\\s+pages?|pages?)\\s*[:#]?\\s*(\\d{1,6})\\b"),
                           QRegularExpression::CaseInsensitiveOption),
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
        const QList<QRegularExpression> pageOnlyExpressions = {
            QRegularExpression(QStringLiteral("\\bpage\\s*[:#]?\\s*(\\d{1,6})\\b"),
                               QRegularExpression::CaseInsensitiveOption),
            QRegularExpression(QStringLiteral("(?:^|\\s)(?:\u9875|\u9801)\\s*[:\uff1a]?\\s*(\\d{1,6})(?=\\s|$)")),
            QRegularExpression(QStringLiteral("(?:^|\\s)\u7b2c\\s*(\\d{1,6})\\s*(?:\u9875|\u9801)(?=\\s|$)")),
        };
        for (const QRegularExpression &expression : pageOnlyExpressions) {
            const QRegularExpressionMatch match = expression.match(normalized);
            if (match.hasMatch()) {
                const int currentPage = match.captured(1).toInt();
                if (currentPage > 0) {
                    state.currentPage = currentPage;
                    break;
                }
            }
        }
    }

    const QList<QRegularExpression> zoomExpressions = {
        QRegularExpression(QStringLiteral("\\bzoom\\s*[:=]?\\s*(\\d{1,4}(?:[\\.,]\\d+)?)\\s*%"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("(?:^|\\s)\u7f29\u653e\\s*[:\uff1a=]?\\s*(\\d{1,4}(?:[\\.,]\\d+)?)\\s*%")),
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
        appendUniqueFragment(fragments, rootText);
        EnumChildWindows(window, collectChildWindowText, reinterpret_cast<LPARAM>(&fragments));

        QString uiAutomationDiagnostics;
        const QStringList automationFragments =
            uiAutomationTextFragmentsForWindow(window, &uiAutomationDiagnostics);
        for (const QString &fragment : automationFragments) {
            appendUniqueFragment(fragments, fragment);
        }
        if (!uiAutomationDiagnostics.trimmed().isEmpty()) {
            appendUniqueFragment(fragments, uiAutomationDiagnostics);
        }
    }
#endif

    if (fragments.isEmpty()) {
        state.diagnostics = QStringLiteral("No PDF-XChange window text was readable");
        return state;
    }

    state = parsePdfXChangeViewStateText(fragments.join(QLatin1Char('\n')),
                                         QStringLiteral("win32-window-text+wm-gettext+uia"));
    return state;
}

QList<Resource> pdfXChangeTitleMatchedPdfResources(
    const ILibraryRepository &repository,
    const QString &documentTitle)
{
    QList<Resource> matches;
    const QString title = stripOuterDocumentDecorations(documentTitle);
    if (title.trimmed().isEmpty()) {
        return matches;
    }

    for (const Resource &resource : uniquePdfResources(repository)) {
        if (documentMatchesResource(title, resource)) {
            matches.append(resource);
        }
    }
    return matches;
}

std::optional<Resource> uniquePdfXChangeTitleMatchedPdfResource(
    const ILibraryRepository &repository,
    const QString &documentTitle)
{
    const QList<Resource> matches = pdfXChangeTitleMatchedPdfResources(repository, documentTitle);
    if (matches.size() != 1) {
        return std::nullopt;
    }
    return matches.first();
}

PdfXChangeForegroundCaptureResult pdfXChangeForegroundCaptureResultForConfirmedPdfFile(
    const QString &documentTitle,
    const QString &filePath,
    const PdfXChangeViewState &viewState)
{
    const QString cleanedPath = cleanFullPdfPath(filePath);
    if (cleanedPath.isEmpty()) {
        PdfXChangeForegroundCaptureResult result;
        result.recognizedPdfXChange = true;
        result.documentTitle = stripOuterDocumentDecorations(documentTitle);
        result.viewState = viewState;
        result.needsFileConfirmation = true;
        result.status = QStringLiteral("Selected file for PDF-XChange document \"%1\" is not a valid full PDF path")
                            .arg(result.documentTitle);
        return result;
    }

    PdfXChangeForegroundCaptureResult result =
        resultForResolvedFilePath(documentTitle, cleanedPath, viewState);
    result.confirmedFile = true;
    return result;
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
    return capturePdfXChangeForegroundContext(repository, context, viewState, std::nullopt);
}

PdfXChangeForegroundCaptureResult capturePdfXChangeForegroundContext(
    const ILibraryRepository &repository,
    const ForegroundAppWindowContext &context,
    const PdfXChangeViewState &viewState,
    const std::optional<QString> &savedDocumentPath)
{
    PdfXChangeForegroundCaptureResult result;
    if (!context.isValid() || !isPdfXChangeForegroundWindow(context)) {
        result.status = QStringLiteral("PDF-XChange was not detected in the foreground; open or focus a PDF-XChange PDF before k n");
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

    if (savedDocumentPath.has_value()) {
        const QString mappedPath = cleanFullPdfPath(savedDocumentPath.value());
        if (mappedPath.isEmpty()) {
            PdfXChangeForegroundCaptureResult mappedResult =
                resultNeedsFileConfirmation(
                    result.documentTitle,
                    viewState,
                    QStringLiteral("Saved PDF-XChange mapping for document \"%1\" is not a valid full PDF path; confirm the PDF file")
                        .arg(result.documentTitle));
            mappedResult.rejectedTitleMapping = true;
            return mappedResult;
        }

        PdfXChangeForegroundCaptureResult mappedResult =
            resultForResolvedFilePath(result.documentTitle, mappedPath, viewState);
        mappedResult.resolvedFromTitleMapping = true;
        return mappedResult;
    }

    const QList<Resource> matches = pdfXChangeTitleMatchedPdfResources(repository, result.documentTitle);
    if (matches.isEmpty()) {
        return resultNeedsFileConfirmation(
            result.documentTitle,
            viewState,
            QStringLiteral("Confirm the PDF file for PDF-XChange document \"%1\": PDF-XChange did not expose a full path and no indexed PDF matched the title")
                .arg(result.documentTitle));
    }
    if (matches.size() > 1) {
        return resultNeedsFileConfirmation(
            result.documentTitle,
            viewState,
            QStringLiteral("Confirm the PDF file for PDF-XChange document \"%1\": PDF-XChange did not expose a full path and %2 indexed PDFs matched the title")
                .arg(result.documentTitle)
                .arg(matches.size()),
            matches);
    }

    return resultForMatchedResource(result.documentTitle, matches.first(), viewState);
}

} // namespace Pinloom
