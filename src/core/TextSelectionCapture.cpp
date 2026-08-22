#include "pinloom/core/TextSelectionCapture.h"

#include <QByteArray>
#include <QStringList>
#include <cmath>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <UIAutomation.h>
#endif

namespace Pinloom {

bool ForegroundTextTarget::isValid() const
{
    return windowHandle != 0;
}

bool ForegroundTextTarget::isExpired(qint64 maximumAgeSeconds, const QDateTime &now) const
{
    if (!capturedAt.isValid() || maximumAgeSeconds < 0) {
        return false;
    }
    const QDateTime comparedAt = now.isValid() ? now.toUTC() : QDateTime::currentDateTimeUtc();
    return capturedAt.toUTC().secsTo(comparedAt) > maximumAgeSeconds;
}

bool TextSelectionCaptureResult::hasSelectedText() const
{
    return state == TextSelectionState::TextSelected && !text.trimmed().isEmpty();
}

TextSelectionCaptureService::TextSelectionCaptureService(CaptureProvider provider)
    : provider_(std::move(provider))
{
}

TextSelectionCaptureResult TextSelectionCaptureService::capture() const
{
    return provider_ ? provider_() : TextSelectionCaptureResult{};
}

QString textSelectionStateText(TextSelectionState state)
{
    switch (state) {
    case TextSelectionState::TextSelected:
        return QStringLiteral("text-selected");
    case TextSelectionState::CaretOnly:
        return QStringLiteral("caret-only");
    case TextSelectionState::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

namespace {

#ifdef Q_OS_WIN

QString stringFromBstr(BSTR value)
{
    if (!value) {
        return {};
    }
    const QString text = QString::fromWCharArray(value, static_cast<qsizetype>(SysStringLen(value)));
    SysFreeString(value);
    return text;
}

ForegroundTextTarget currentTextTarget(HWND foregroundWindow)
{
    ForegroundTextTarget target;
    if (!foregroundWindow) {
        return target;
    }

    DWORD processId = 0;
    const DWORD threadId = GetWindowThreadProcessId(foregroundWindow, &processId);
    target.windowHandle = reinterpret_cast<quintptr>(foregroundWindow);
    target.processId = processId;
    target.threadId = threadId;
    target.capturedAt = QDateTime::currentDateTimeUtc();

    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (threadId != 0 && GetGUIThreadInfo(threadId, &info) && info.hwndFocus) {
        target.focusHandle = reinterpret_cast<quintptr>(info.hwndFocus);
        POINT caretPoint{info.rcCaret.left, info.rcCaret.bottom};
        if (info.hwndCaret && ClientToScreen(info.hwndCaret, &caretPoint)) {
            target.insertionPoint = QPoint(caretPoint.x, caretPoint.y);
            target.hasInsertionPoint = true;
        }
    } else {
        target.focusHandle = target.windowHandle;
    }
    return target;
}

bool screenPointFromTextRange(IUIAutomationTextRange *range, QPoint *point)
{
    if (!range || !point) {
        return false;
    }

    const auto readRectangles = [point](IUIAutomationTextRange *candidate, bool useRightEdge) {
        SAFEARRAY *rectangles = nullptr;
        if (FAILED(candidate->GetBoundingRectangles(&rectangles)) || !rectangles) {
            return false;
        }

        LONG lower = 0;
        LONG upper = -1;
        bool found = false;
        if (SUCCEEDED(SafeArrayGetLBound(rectangles, 1, &lower))
            && SUCCEEDED(SafeArrayGetUBound(rectangles, 1, &upper))
            && upper - lower + 1 >= 4) {
            double *values = nullptr;
            if (SUCCEEDED(SafeArrayAccessData(rectangles, reinterpret_cast<void **>(&values)))) {
                if (values) {
                    const LONG count = upper - lower + 1;
                    const double left = values[count - 4];
                    const double top = values[count - 3];
                    const double width = values[count - 2];
                    const double height = values[count - 1];
                    if (std::isfinite(left)
                        && std::isfinite(top)
                        && std::isfinite(width)
                        && std::isfinite(height)) {
                        *point = QPoint(qRound(left + (useRightEdge ? width : 0.0)),
                                        qRound(top + height));
                        found = true;
                    }
                }
                SafeArrayUnaccessData(rectangles);
            }
        }
        SafeArrayDestroy(rectangles);
        return found;
    };

    if (readRectangles(range, true)) {
        return true;
    }

    IUIAutomationTextRange *expanded = nullptr;
    if (FAILED(range->Clone(&expanded)) || !expanded) {
        return false;
    }
    const bool found = SUCCEEDED(expanded->ExpandToEnclosingUnit(TextUnit_Character))
        && readRectangles(expanded, false);
    expanded->Release();
    return found;
}

enum class TextPatternSelectionStatus {
    Unavailable,
    Observed,
    Selected
};

TextPatternSelectionStatus selectedTextFromTextPattern(IUIAutomationElement *element,
                                                        TextSelectionCaptureResult &result)
{
    if (!element) {
        return TextPatternSelectionStatus::Unavailable;
    }

    IUIAutomationTextPattern *pattern = nullptr;
    if (FAILED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&pattern))) || !pattern) {
        return TextPatternSelectionStatus::Unavailable;
    }

    IUIAutomationTextRangeArray *selection = nullptr;
    const HRESULT selectionResult = pattern->GetSelection(&selection);
    pattern->Release();
    if (FAILED(selectionResult) || !selection) {
        return TextPatternSelectionStatus::Observed;
    }

    int length = 0;
    selection->get_Length(&length);
    QStringList ranges;
    bool observedRange = false;
    const int maxRanges = qMin(length, 64);
    for (int index = 0; index < maxRanges; ++index) {
        IUIAutomationTextRange *range = nullptr;
        if (FAILED(selection->GetElement(index, &range)) || !range) {
            continue;
        }
        observedRange = true;
        QPoint insertionPoint;
        if (index == 0 && screenPointFromTextRange(range, &insertionPoint)) {
            result.target.insertionPoint = insertionPoint;
            result.target.hasInsertionPoint = true;
        }
        BSTR selected = nullptr;
        if (SUCCEEDED(range->GetText(-1, &selected))) {
            const QString text = stringFromBstr(selected);
            if (!text.trimmed().isEmpty()) {
                ranges.append(text);
            }
        }
        range->Release();
    }
    selection->Release();

    if (!ranges.isEmpty()) {
        result.state = TextSelectionState::TextSelected;
        result.text = ranges.join(QLatin1Char('\n'));
    } else if (observedRange) {
        result.state = TextSelectionState::CaretOnly;
    } else {
        result.state = TextSelectionState::Unknown;
    }
    result.source = QStringLiteral("uia-text-pattern");
    return ranges.isEmpty()
        ? TextPatternSelectionStatus::Observed
        : TextPatternSelectionStatus::Selected;
}

bool captureUiAutomationSelection(TextSelectionCaptureResult &result)
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        result.diagnostics = QStringLiteral("CoInitializeEx failed: 0x%1")
                                 .arg(static_cast<quint32>(initialized), 8, 16, QLatin1Char('0'));
        return false;
    }

    IUIAutomation *automation = nullptr;
    const HRESULT created = CoCreateInstance(CLSID_CUIAutomation,
                                             nullptr,
                                             CLSCTX_INPROC_SERVER,
                                             IID_PPV_ARGS(&automation));
    if (FAILED(created) || !automation) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        result.diagnostics = QStringLiteral("Unable to create UI Automation client: 0x%1")
                                 .arg(static_cast<quint32>(created), 8, 16, QLatin1Char('0'));
        return false;
    }

    IUIAutomationElement *element = nullptr;
    bool captured = false;
    bool observedTextPattern = false;
    if (SUCCEEDED(automation->GetFocusedElement(&element)) && element) {
        BOOL isPassword = FALSE;
        if (SUCCEEDED(element->get_CurrentIsPassword(&isPassword)) && isPassword != FALSE) {
            result.source = QStringLiteral("uia-password");
            result.diagnostics = QStringLiteral("Focused text control is password-protected");
            captured = true;
        } else {
            const TextPatternSelectionStatus status = selectedTextFromTextPattern(element, result);
            observedTextPattern = status != TextPatternSelectionStatus::Unavailable;
            captured = status == TextPatternSelectionStatus::Selected;
        }
        if (!captured) {
            IUIAutomationTreeWalker *walker = nullptr;
            if (SUCCEEDED(automation->get_ControlViewWalker(&walker)) && walker) {
                IUIAutomationElement *current = element;
                current->AddRef();
                for (int depth = 0; depth < 12 && current && !captured; ++depth) {
                    IUIAutomationElement *parent = nullptr;
                    if (FAILED(walker->GetParentElement(current, &parent)) || !parent) {
                        current->Release();
                        current = nullptr;
                        break;
                    }
                    current->Release();
                    current = parent;
                    const TextPatternSelectionStatus status = selectedTextFromTextPattern(current, result);
                    observedTextPattern = observedTextPattern
                        || status != TextPatternSelectionStatus::Unavailable;
                    captured = status == TextPatternSelectionStatus::Selected;
                }
                if (current) {
                    current->Release();
                }
                walker->Release();
            }
        }
        element->Release();
    }
    automation->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }
    return captured || observedTextPattern;
}

QString windowClassName(HWND window)
{
    wchar_t className[256]{};
    const int length = GetClassNameW(window, className, static_cast<int>(std::size(className)));
    return length > 0 ? QString::fromWCharArray(className, length) : QString();
}

bool isStandardEditableClass(const QString &className)
{
    return className.compare(QStringLiteral("Edit"), Qt::CaseInsensitive) == 0
        || className.startsWith(QStringLiteral("RichEdit"), Qt::CaseInsensitive);
}

constexpr UINT SciGetCurrentPos = 2008;
constexpr UINT SciGetCodePage = 2137;
constexpr UINT SciGetSelectionStart = 2143;
constexpr UINT SciGetSelectionEnd = 2145;
constexpr UINT SciGetSelectedText = 2161;
constexpr UINT SciPointXFromPosition = 2164;
constexpr UINT SciPointYFromPosition = 2165;
constexpr DWORD SciUtf8CodePage = 65001;

bool sendMessageWithTimeout(HWND window,
                            UINT message,
                            WPARAM wordParameter,
                            LPARAM longParameter,
                            DWORD_PTR *result)
{
    DWORD_PTR value = 0;
    const LRESULT sent = SendMessageTimeoutW(window,
                                             message,
                                             wordParameter,
                                             longParameter,
                                             SMTO_ABORTIFHUNG | SMTO_BLOCK,
                                             200,
                                             &value);
    if (sent == 0) {
        return false;
    }
    if (result) {
        *result = value;
    }
    return true;
}

QString decodeScintillaText(const QByteArray &bytes, DWORD codePage)
{
    if (bytes.isEmpty()) {
        return {};
    }
    if (codePage == SciUtf8CodePage) {
        return QString::fromUtf8(bytes);
    }

    const UINT windowsCodePage = codePage == 0 ? CP_ACP : static_cast<UINT>(codePage);
    const int characterCount = MultiByteToWideChar(windowsCodePage,
                                                    0,
                                                    bytes.constData(),
                                                    bytes.size(),
                                                    nullptr,
                                                    0);
    if (characterCount <= 0) {
        return QString::fromUtf8(bytes);
    }

    std::vector<wchar_t> characters(static_cast<size_t>(characterCount));
    if (MultiByteToWideChar(windowsCodePage,
                            0,
                            bytes.constData(),
                            bytes.size(),
                            characters.data(),
                            characterCount) != characterCount) {
        return QString::fromUtf8(bytes);
    }
    return QString::fromWCharArray(characters.data(), characterCount);
}

void updateScintillaInsertionPoint(HWND focusWindow, ForegroundTextTarget &target)
{
    DWORD_PTR position = 0;
    DWORD_PTR x = 0;
    DWORD_PTR y = 0;
    if (!sendMessageWithTimeout(focusWindow, SciGetCurrentPos, 0, 0, &position)
        || !sendMessageWithTimeout(focusWindow,
                                   SciPointXFromPosition,
                                   0,
                                   static_cast<LPARAM>(position),
                                   &x)
        || !sendMessageWithTimeout(focusWindow,
                                   SciPointYFromPosition,
                                   0,
                                   static_cast<LPARAM>(position),
                                   &y)) {
        return;
    }

    POINT point{static_cast<LONG>(x), static_cast<LONG>(y)};
    if (ClientToScreen(focusWindow, &point)) {
        target.insertionPoint = QPoint(point.x, point.y);
        target.hasInsertionPoint = true;
    }
}

bool captureScintillaSelection(HWND focusWindow, TextSelectionCaptureResult &result)
{
    if (!focusWindow
        || windowClassName(focusWindow).compare(QStringLiteral("Scintilla"),
                                                Qt::CaseInsensitive) != 0) {
        return false;
    }

    result.source = QStringLiteral("scintilla");
    updateScintillaInsertionPoint(focusWindow, result.target);

    DWORD_PTR start = 0;
    DWORD_PTR end = 0;
    if (!sendMessageWithTimeout(focusWindow, SciGetSelectionStart, 0, 0, &start)
        || !sendMessageWithTimeout(focusWindow, SciGetSelectionEnd, 0, 0, &end)) {
        result.diagnostics = QStringLiteral("Unable to query the Scintilla selection");
        return true;
    }
    if (end <= start) {
        result.state = TextSelectionState::CaretOnly;
        return true;
    }

    DWORD_PTR textLength = 0;
    if (!sendMessageWithTimeout(focusWindow, SciGetSelectedText, 0, 0, &textLength)) {
        result.diagnostics = QStringLiteral("Unable to query the Scintilla selection length");
        return true;
    }

    constexpr SIZE_T MaxSelectedTextBytes = 1024U * 1024U;
    const SIZE_T linearSelectionLength = static_cast<SIZE_T>(end - start);
    const SIZE_T selectedTextLength = std::max(static_cast<SIZE_T>(textLength),
                                               linearSelectionLength);
    if (selectedTextLength == 0 || selectedTextLength > MaxSelectedTextBytes) {
        result.diagnostics = selectedTextLength > MaxSelectedTextBytes
            ? QStringLiteral("The Scintilla selection exceeds the 1 MiB capture limit")
            : QStringLiteral("Unable to determine the Scintilla selection length");
        return true;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(focusWindow, &processId);
    HANDLE process = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ,
                                 FALSE,
                                 processId);
    if (!process) {
        result.diagnostics = QStringLiteral("Unable to open the Scintilla process for selection capture");
        return true;
    }

    const SIZE_T bufferSize = selectedTextLength + 1;
    void *remoteBuffer = VirtualAllocEx(process,
                                        nullptr,
                                        bufferSize,
                                        MEM_COMMIT | MEM_RESERVE,
                                        PAGE_READWRITE);
    if (!remoteBuffer) {
        CloseHandle(process);
        result.diagnostics = QStringLiteral("Unable to allocate a Scintilla selection buffer");
        return true;
    }

    DWORD_PTR copiedLength = 0;
    const bool copied = sendMessageWithTimeout(focusWindow,
                                               SciGetSelectedText,
                                               0,
                                               reinterpret_cast<LPARAM>(remoteBuffer),
                                               &copiedLength);
    QByteArray bytes(static_cast<qsizetype>(bufferSize), '\0');
    SIZE_T bytesRead = 0;
    const bool read = copied
        && ReadProcessMemory(process,
                             remoteBuffer,
                             bytes.data(),
                             bufferSize,
                             &bytesRead) != FALSE;
    VirtualFreeEx(process, remoteBuffer, 0, MEM_RELEASE);
    CloseHandle(process);

    if (!read || bytesRead == 0) {
        result.diagnostics = QStringLiteral("Unable to read the Scintilla selection text");
        return true;
    }

    bytes.truncate(static_cast<qsizetype>(bytesRead));
    const qsizetype terminator = bytes.indexOf('\0');
    if (terminator >= 0) {
        bytes.truncate(terminator);
    }

    DWORD_PTR codePage = 0;
    sendMessageWithTimeout(focusWindow, SciGetCodePage, 0, 0, &codePage);
    result.text = decodeScintillaText(bytes, static_cast<DWORD>(codePage));
    result.state = result.text.trimmed().isEmpty()
        ? TextSelectionState::CaretOnly
        : TextSelectionState::TextSelected;
    if (result.state != TextSelectionState::TextSelected) {
        result.diagnostics = QStringLiteral("The Scintilla selection contains no text");
    }
    return true;
}

bool captureStandardEditSelection(HWND focusWindow, TextSelectionCaptureResult &result)
{
    if (!focusWindow || !isStandardEditableClass(windowClassName(focusWindow))) {
        return false;
    }
    const LONG_PTR style = GetWindowLongPtrW(focusWindow, GWL_STYLE);
    if ((style & ES_PASSWORD) != 0) {
        result.diagnostics = QStringLiteral("Focused edit control is password-protected");
        return false;
    }

    DWORD start = 0;
    DWORD end = 0;
    DWORD_PTR ignored = 0;
    if (!SendMessageTimeoutW(focusWindow,
                             EM_GETSEL,
                             reinterpret_cast<WPARAM>(&start),
                             reinterpret_cast<LPARAM>(&end),
                             SMTO_ABORTIFHUNG,
                             150,
                             &ignored)) {
        return false;
    }
    result.source = QStringLiteral("win32-edit");
    if (end <= start) {
        result.state = TextSelectionState::CaretOnly;
        return true;
    }

    const LRESULT textLength = SendMessageW(focusWindow, WM_GETTEXTLENGTH, 0, 0);
    constexpr LRESULT MaxTextLength = 1024 * 1024;
    if (textLength <= 0 || textLength > MaxTextLength || end > static_cast<DWORD>(textLength)) {
        result.state = TextSelectionState::Unknown;
        result.diagnostics = QStringLiteral("Unable to read selected edit-control text");
        return true;
    }

    std::vector<wchar_t> buffer(static_cast<size_t>(textLength) + 1, L'\0');
    if (SendMessageW(focusWindow,
                     WM_GETTEXT,
                     static_cast<WPARAM>(buffer.size()),
                     reinterpret_cast<LPARAM>(buffer.data())) <= 0) {
        result.state = TextSelectionState::Unknown;
        result.diagnostics = QStringLiteral("Unable to read edit-control text");
        return true;
    }

    result.text = QString::fromWCharArray(buffer.data(), textLength).mid(start, end - start);
    result.state = result.text.trimmed().isEmpty()
        ? TextSelectionState::CaretOnly
        : TextSelectionState::TextSelected;
    return true;
}

#endif

} // namespace

TextSelectionCaptureResult captureTextSelectionFromTarget(const ForegroundAppWindowContext &context,
                                                           const ForegroundTextTarget &target)
{
    TextSelectionCaptureResult result;
    result.context = context;
    result.target = target;
#ifdef Q_OS_WIN
    HWND targetWindow = reinterpret_cast<HWND>(result.target.windowHandle);
    if (!targetWindow) {
        result.diagnostics = QStringLiteral("No text target window");
        return result;
    }

    HWND focusWindow = reinterpret_cast<HWND>(result.target.focusHandle);
    if (!focusWindow) {
        focusWindow = targetWindow;
    }
    bool nativeControlCaptured = captureStandardEditSelection(focusWindow, result);
    if (!nativeControlCaptured) {
        nativeControlCaptured = captureScintillaSelection(focusWindow, result);
    }
    if (nativeControlCaptured && result.hasSelectedText()) {
        return result;
    }
    const TextSelectionCaptureResult nativeControlResult = result;

    if (captureUiAutomationSelection(result)) {
        return result;
    }
    if (nativeControlCaptured) {
        return nativeControlResult;
    }
    result.state = TextSelectionState::Unknown;
    result.source = QStringLiteral("unsupported");
    if (result.diagnostics.trimmed().isEmpty()) {
        result.diagnostics = QStringLiteral("Focused control does not expose a supported text selection interface");
    }
#else
    result.diagnostics = QStringLiteral("Text selection capture is unavailable on this platform");
#endif
    return result;
}

TextSelectionCaptureResult captureForegroundTextSelection()
{
    const ForegroundAppWindowContext context = currentForegroundAppWindowContext();
#ifdef Q_OS_WIN
    HWND foregroundWindow = reinterpret_cast<HWND>(context.windowHandle);
    if (!foregroundWindow) {
        TextSelectionCaptureResult result;
        result.context = context;
        result.diagnostics = QStringLiteral("No foreground window");
        return result;
    }
    return captureTextSelectionFromTarget(context, currentTextTarget(foregroundWindow));
#else
    return captureTextSelectionFromTarget(context, {});
#endif
}

bool restoreForegroundTextTarget(const ForegroundTextTarget &target, QString *error)
{
#ifdef Q_OS_WIN
    if (target.isExpired()) {
        if (error) {
            *error = QStringLiteral("Original insertion target expired; press F24+V again");
        }
        return false;
    }
    HWND window = reinterpret_cast<HWND>(target.windowHandle);
    HWND focus = reinterpret_cast<HWND>(target.focusHandle);
    if (!window || !IsWindow(window)) {
        if (error) {
            *error = QStringLiteral("Original insertion window is no longer available");
        }
        return false;
    }

    if (IsIconic(window)) {
        ShowWindow(window, SW_RESTORE);
    }
    const DWORD currentThread = GetCurrentThreadId();
    DWORD targetProcess = 0;
    const DWORD targetThread = GetWindowThreadProcessId(window, &targetProcess);
    if (target.processId != 0 && targetProcess != target.processId) {
        if (error) {
            *error = QStringLiteral("Original insertion window now belongs to a different process");
        }
        return false;
    }
    const bool attached = targetThread != 0
        && targetThread != currentThread
        && AttachThreadInput(currentThread, targetThread, TRUE);

    BringWindowToTop(window);
    const bool activated = SetForegroundWindow(window) != FALSE;
    if (focus && IsWindow(focus)) {
        DWORD focusProcess = 0;
        GetWindowThreadProcessId(focus, &focusProcess);
        if (focusProcess == targetProcess) {
            SetFocus(focus);
        }
    }
    if (attached) {
        AttachThreadInput(currentThread, targetThread, FALSE);
    }

    if (!activated && GetForegroundWindow() != window) {
        if (error) {
            *error = QStringLiteral("Windows did not restore the original insertion target");
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
#else
    Q_UNUSED(target);
    if (error) {
        *error = QStringLiteral("Foreground target restoration is unavailable on this platform");
    }
    return false;
#endif
}

} // namespace Pinloom
