#include "pinloom/core/TextSelectionCapture.h"

#include <QStringList>
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

ContextualClipIntent contextualClipIntent(const TextSelectionCaptureResult &selection)
{
    return selection.hasSelectedText()
        ? ContextualClipIntent::ArchiveSelection
        : ContextualClipIntent::OpenInsertionPicker;
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

    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (threadId != 0 && GetGUIThreadInfo(threadId, &info) && info.hwndFocus) {
        target.focusHandle = reinterpret_cast<quintptr>(info.hwndFocus);
    } else {
        target.focusHandle = target.windowHandle;
    }
    return target;
}

bool selectedTextFromTextPattern(IUIAutomationElement *element,
                                 TextSelectionCaptureResult &result)
{
    if (!element) {
        return false;
    }

    IUIAutomationTextPattern *pattern = nullptr;
    if (FAILED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&pattern))) || !pattern) {
        return false;
    }

    IUIAutomationTextRangeArray *selection = nullptr;
    const HRESULT selectionResult = pattern->GetSelection(&selection);
    pattern->Release();
    if (FAILED(selectionResult) || !selection) {
        return false;
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
    return true;
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
    if (SUCCEEDED(automation->GetFocusedElement(&element)) && element) {
        BOOL isPassword = FALSE;
        if (SUCCEEDED(element->get_CurrentIsPassword(&isPassword)) && isPassword != FALSE) {
            result.source = QStringLiteral("uia-password");
            result.diagnostics = QStringLiteral("Focused text control is password-protected");
            captured = true;
        } else {
            captured = selectedTextFromTextPattern(element, result);
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
                    captured = selectedTextFromTextPattern(current, result);
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
    return captured;
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

TextSelectionCaptureResult captureForegroundTextSelection()
{
    TextSelectionCaptureResult result;
    result.context = currentForegroundAppWindowContext();
#ifdef Q_OS_WIN
    HWND foregroundWindow = reinterpret_cast<HWND>(result.context.windowHandle);
    if (!foregroundWindow) {
        result.diagnostics = QStringLiteral("No foreground window");
        return result;
    }
    result.target = currentTextTarget(foregroundWindow);

    if (captureUiAutomationSelection(result)) {
        return result;
    }

    HWND focusWindow = reinterpret_cast<HWND>(result.target.focusHandle);
    if (captureStandardEditSelection(focusWindow, result)) {
        return result;
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

bool restoreForegroundTextTarget(const ForegroundTextTarget &target, QString *error)
{
#ifdef Q_OS_WIN
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
    const bool attached = targetThread != 0
        && targetThread != currentThread
        && AttachThreadInput(currentThread, targetThread, TRUE);

    BringWindowToTop(window);
    const bool activated = SetForegroundWindow(window) != FALSE;
    if (focus && IsWindow(focus)) {
        SetFocus(focus);
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
