#include "pinloom/core/PdfXChangeForegroundCapture.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
QString windowTitle(HWND window)
{
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }

    std::wstring buffer(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, buffer.data(), static_cast<int>(buffer.size()));
    if (copied <= 0) {
        return {};
    }
    return QString::fromWCharArray(buffer.data(), copied);
}

QString processPath(DWORD processId)
{
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

struct FindPdfXChangeWindowState {
    Pinloom::ForegroundAppWindowContext context;
};

BOOL CALLBACK findPdfXChangeWindow(HWND window, LPARAM lParam)
{
    auto *state = reinterpret_cast<FindPdfXChangeWindowState *>(lParam);
    if (!state || !IsWindowVisible(window)) {
        return TRUE;
    }

    const QString title = windowTitle(window);
    if (!title.contains(QStringLiteral("PDF-XChange Editor"), Qt::CaseInsensitive)) {
        return TRUE;
    }

    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);

    state->context.windowHandle = reinterpret_cast<quintptr>(window);
    state->context.processId = static_cast<quint32>(processId);
    state->context.windowTitle = title;
    state->context.processPath = processPath(processId);
    state->context.processName = QFileInfo(state->context.processPath).fileName();
    return FALSE;
}
#endif

Pinloom::ForegroundAppWindowContext findPdfXChangeWindowContext()
{
#ifdef Q_OS_WIN
    FindPdfXChangeWindowState state;
    EnumWindows(findPdfXChangeWindow, reinterpret_cast<LPARAM>(&state));
    return state.context;
#else
    return {};
#endif
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    const Pinloom::ForegroundAppWindowContext context = findPdfXChangeWindowContext();
    out << "windowTitle=" << context.windowTitle << '\n';
    out << "processName=" << context.processName << '\n';
    out << "processPath=" << context.processPath << '\n';
    out << "windowHandle=" << QString::number(context.windowHandle) << '\n';

    const Pinloom::PdfXChangeViewState state = Pinloom::capturePdfXChangeViewState(context);
    out << "source=" << state.source << '\n';
    out << "currentPage=" << state.currentPage << '\n';
    out << "totalPages=" << state.totalPages << '\n';
    out << "zoom=" << state.zoom << '\n';
    out << "diagnostics=" << state.diagnostics << '\n';
    out.flush();

    return state.hasAnyViewState() ? 0 : 2;
}
