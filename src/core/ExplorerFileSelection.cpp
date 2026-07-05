#include "pinloom/core/ExplorerFileSelection.h"

#include <QFileInfo>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <exdisp.h>
#include <shldisp.h>
#include <objbase.h>
#endif

namespace Pinloom {

namespace {

#ifdef Q_OS_WIN
class ComScope {
public:
    ComScope()
    {
        result_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    }

    ~ComScope()
    {
        if (result_ == S_OK || result_ == S_FALSE) {
            CoUninitialize();
        }
    }

    bool usable() const
    {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_ = E_FAIL;
};

template <typename T>
void releaseIfPresent(T *value)
{
    if (value) {
        value->Release();
    }
}

QString bstrToString(BSTR value)
{
    if (!value) {
        return {};
    }
    const QString converted = QString::fromWCharArray(value);
    SysFreeString(value);
    return converted;
}

QStringList selectedFilesForBrowser(IWebBrowserApp *browser)
{
    QStringList files;
    if (!browser) {
        return files;
    }

    IDispatch *documentDispatch = nullptr;
    if (FAILED(browser->get_Document(&documentDispatch)) || !documentDispatch) {
        return files;
    }

    IShellFolderViewDual *view = nullptr;
    if (SUCCEEDED(documentDispatch->QueryInterface(IID_PPV_ARGS(&view))) && view) {
        FolderItems *selectedItems = nullptr;
        if (SUCCEEDED(view->SelectedItems(&selectedItems)) && selectedItems) {
            long count = 0;
            selectedItems->get_Count(&count);
            for (long index = 0; index < count; ++index) {
                VARIANT itemIndex;
                VariantInit(&itemIndex);
                itemIndex.vt = VT_I4;
                itemIndex.lVal = index;

                FolderItem *item = nullptr;
                if (SUCCEEDED(selectedItems->Item(itemIndex, &item)) && item) {
                    BSTR path = nullptr;
                    if (SUCCEEDED(item->get_Path(&path))) {
                        const QString filePath = bstrToString(path).trimmed();
                        const QFileInfo info(filePath);
                        if (!filePath.isEmpty() && (!info.exists() || info.isFile())) {
                            files.append(filePath);
                        }
                    }
                    item->Release();
                }
                VariantClear(&itemIndex);
            }
            selectedItems->Release();
        }
        view->Release();
    }

    documentDispatch->Release();
    files.removeDuplicates();
    return files;
}
#endif

} // namespace

bool ExplorerFileSelectionResult::success() const
{
    return recognizedExplorer && !filePaths.isEmpty();
}

bool isExplorerForegroundWindow(const ForegroundAppWindowContext &context)
{
    const QString processName = QFileInfo(context.processName.trimmed()).fileName();
    const QString processPath = context.processPath.trimmed();
    return processName.compare(QStringLiteral("explorer.exe"), Qt::CaseInsensitive) == 0
        || processPath.endsWith(QStringLiteral("/explorer.exe"), Qt::CaseInsensitive)
        || processPath.endsWith(QStringLiteral("\\explorer.exe"), Qt::CaseInsensitive);
}

ExplorerFileSelectionResult captureExplorerFileSelection(const ForegroundAppWindowContext &context)
{
    ExplorerFileSelectionResult result;
    if (!context.isValid() || !isExplorerForegroundWindow(context)) {
        result.status = QStringLiteral("No pending Inbox file; drop a file or select one in Explorer");
        return result;
    }

    result.recognizedExplorer = true;

#ifndef Q_OS_WIN
    result.status = QStringLiteral("Explorer selection capture is available on Windows only");
    return result;
#else
    const ComScope com;
    if (!com.usable()) {
        result.status = QStringLiteral("Explorer selection capture could not initialize COM");
        return result;
    }

    IShellWindows *shellWindows = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows,
                                nullptr,
                                CLSCTX_ALL,
                                IID_PPV_ARGS(&shellWindows)))
        || !shellWindows) {
        result.status = QStringLiteral("Explorer selection is not available");
        return result;
    }

    long windowCount = 0;
    shellWindows->get_Count(&windowCount);
    const auto targetWindow = static_cast<SHANDLE_PTR>(context.windowHandle);

    for (long index = 0; index < windowCount; ++index) {
        VARIANT itemIndex;
        VariantInit(&itemIndex);
        itemIndex.vt = VT_I4;
        itemIndex.lVal = index;

        IDispatch *dispatch = nullptr;
        if (SUCCEEDED(shellWindows->Item(itemIndex, &dispatch)) && dispatch) {
            IWebBrowserApp *browser = nullptr;
            if (SUCCEEDED(dispatch->QueryInterface(IID_PPV_ARGS(&browser))) && browser) {
                SHANDLE_PTR hwnd = 0;
                if (SUCCEEDED(browser->get_HWND(&hwnd)) && hwnd == targetWindow) {
                    result.filePaths = selectedFilesForBrowser(browser);
                    browser->Release();
                    dispatch->Release();
                    VariantClear(&itemIndex);
                    break;
                }
                browser->Release();
            }
            dispatch->Release();
        }
        VariantClear(&itemIndex);
    }

    shellWindows->Release();

    if (result.filePaths.isEmpty()) {
        result.status = QStringLiteral("Explorer selection did not contain files");
    } else {
        result.status = QStringLiteral("Captured %1 Explorer file(s)").arg(result.filePaths.size());
    }
    return result;
#endif
}

ExplorerFileSelectionResult captureCurrentExplorerFileSelection()
{
    return captureExplorerFileSelection(currentForegroundAppWindowContext());
}

} // namespace Pinloom
