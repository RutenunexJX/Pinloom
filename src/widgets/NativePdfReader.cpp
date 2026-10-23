#include "pinloom/widgets/NativePdfReader.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QThread>
#include <QVector>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Pinloom {
namespace {
QString canonicalPath(const QString &path)
{
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical).toCaseFolded();
}
#ifdef Q_OS_WIN
struct ReaderWindows {
    QString executable;
    QVector<HWND> windows;
};
BOOL CALLBACK collectReader(HWND window, LPARAM argument)
{
    wchar_t name[128]{};
    GetClassNameW(window, name, 128);
    if (wcscmp(name, L"SUMATRA_PDF_FRAME") != 0) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return TRUE;
    wchar_t path[32768]; DWORD size = 32768;
    const bool read = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    auto &state = *reinterpret_cast<ReaderWindows *>(argument);
    if (read && canonicalPath(QString::fromWCharArray(path, int(size))) == state.executable)
        state.windows.append(window);
    return TRUE;
}
bool send(HWND window, const QByteArray &json, DWORD_PTR *result, UINT timeout)
{
    COPYDATASTRUCT message{};
    message.dwData = 0x504c4f4d;
    message.cbData = DWORD(json.size());
    message.lpData = const_cast<char *>(json.constData());
    return SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&message),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, timeout, result) != 0;
}
#endif
}

SumatraPdfDdeFileState requestConfiguredPdfReaderState(
    const QString &executable, const QString &file, int expectedPage,
    int timeoutMilliseconds)
{
#ifdef Q_OS_WIN
    const auto probe = QJsonDocument(QJsonObject{{"protocol", "pinloom-reader/v1"},
        {"method", "capabilities"}}).toJson(QJsonDocument::Compact);
    const auto request = QJsonDocument(QJsonObject{{"protocol", "pinloom-reader/v1"},
        {"method", "page-state"}, {"file", QDir::toNativeSeparators(file)}}
    ).toJson(QJsonDocument::Compact);
    ReaderWindows readers{canonicalPath(executable), {}};
    EnumWindows(collectReader, reinterpret_cast<LPARAM>(&readers));
    bool supported = false;
    SumatraPdfDdeFileState state;
    state.error = QStringLiteral("The paired reader has not displayed the requested PDF");
    for (HWND reader : readers.windows) {
        DWORD_PTR answer = 0;
        if (!send(reader, probe, &answer, qBound(1, timeoutMilliseconds, 1000))
            || answer != 0x504c0001) continue;
        supported = true;
        if (!send(reader, request, &answer, qBound(1, timeoutMilliseconds, 1000))
            || answer < 1 || answer > 1000000) continue;
        state.path = file;
        state.page = int(answer);
        state.error.clear();
        if (state.page == expectedPage) return state;
    }
    if (supported) return state;
#else
    Q_UNUSED(executable)
    Q_UNUSED(file)
    Q_UNUSED(expectedPage)
#endif
    return requestSumatraPdfDdeFileState(timeoutMilliseconds);
}

NativePdfReaderResult openNativePdfReader(
    const QString &configuredExecutable, const QString &profileDirectory,
    const QString &file, const QJsonObject &locator,
    const std::shared_ptr<std::atomic_bool> &canceled)
{
    // Native locators require the paired protocol, even when a legacy external reader is configured.
    const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("reader/SumatraPDF.exe"));
    const QString executable = QFileInfo(bundled).isFile() ? bundled : configuredExecutable;
    const QFileInfo source(file);
    const auto document = locator["document"].toObject();
    if (!source.isFile() || !source.isReadable())
        return {false, QStringLiteral("The original PDF is missing or unreadable")};
    if (source.size() != document["size"].toString().toLongLong()
        || source.lastModified().toMSecsSinceEpoch() != document["modifiedMilliseconds"].toString().toLongLong())
        return {false, QStringLiteral("The original PDF changed; capture the selection again")};
    if (!QFileInfo(executable).isFile())
        return {false, QStringLiteral("Select the paired SumatraPDF in Pinloom Settings")};
#ifdef Q_OS_WIN
    const bool textAnchor = locator["type"] == "sumatrapdf.text";
    const auto probe = QJsonDocument(QJsonObject{{"protocol", "pinloom-reader/v1"},
        {"method", textAnchor ? "text-capabilities" : "capabilities"}}).toJson(QJsonDocument::Compact);
    const auto request = QJsonDocument(QJsonObject{{"protocol", "pinloom-reader/v1"},
        {"method", textAnchor ? "open-text" : "open"}, {"file", QDir::toNativeSeparators(source.canonicalFilePath())},
        {"page", locator["page"]}, {"rect", locator["rect"]},
        {"selection", locator["selection"]}, {"text", locator["text"]},
        {"documentSize", document["size"]}, {"documentModifiedMs", document["modifiedMilliseconds"]}}
    ).toJson(QJsonDocument::Compact);
    QElapsedTimer timer;
    timer.start();
    bool launched = false;
    while (!canceled->load() && timer.elapsed() < 15000) {
        ReaderWindows readers{canonicalPath(executable), {}};
        EnumWindows(collectReader, reinterpret_cast<LPARAM>(&readers));
        QString rejection;
        for (HWND reader : readers.windows) {
            DWORD_PTR answer = 0;
            if (canceled->load()) return {};
            if (!send(reader, probe, &answer, 500)) continue;
            if (answer != (textAnchor ? 0x504c0002 : 0x504c0001))
                return {false, QStringLiteral("This reader does not support the requested native selection; use the paired SumatraPDF")};
            DWORD pid = 0;
            GetWindowThreadProcessId(reader, &pid);
            AllowSetForegroundWindow(pid);
            if (canceled->load()) return {};
            if (!send(reader, request, &answer, 5000))
                return {false, QStringLiteral("The reader did not confirm PDF positioning; check the reader and retry")};
            if (answer == 1) return {true, {}};
            if (answer == 2) {
                rejection = QStringLiteral("The reader has an active capture; save or cancel it before opening another anchor");
                continue;
            }
            rejection = QStringLiteral("The reader rejected positioning: the document, page, text, or selection no longer matches");
        }
        if (!rejection.isEmpty()) return {false, rejection};
        if (!launched && readers.windows.isEmpty()) {
            if (profileDirectory.isEmpty() || !QDir().mkpath(profileDirectory))
                return {false, QStringLiteral("Cannot create the reader profile directory")};
            if (canceled->load()) return {};
            if (!QProcess::startDetached(executable, {"-appdata", profileDirectory, "-new-window", file}))
                return {false, QStringLiteral("Cannot start the paired SumatraPDF")};
            launched = true;
        }
        QThread::msleep(75);
    }
    return {false, canceled->load() ? QString() : QStringLiteral("Timed out waiting for the paired SumatraPDF")};
#else
    Q_UNUSED(profileDirectory)
    Q_UNUSED(canceled)
    return {false, QStringLiteral("Native SumatraPDF positioning requires Windows")};
#endif
}
}
