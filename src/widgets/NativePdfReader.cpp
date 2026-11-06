#include "pinloom/widgets/NativePdfReader.h"
#include "pinloom/core/PinloomStartup.h"
#include "pinloom/core/SumatraPdfCommand.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
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

QString initializeReaderConnection(const QString &profile, const QString &executable,
                                   const QString &library, bool explicitLibrary)
{
    // Use the reader's existing per-key fallback and Unicode INI format.
    const auto user = QDir::toNativeSeparators(QDir(profile).filePath("PinloomIntegration.user.ini")).toStdWString();
    const auto legacy = QDir::toNativeSeparators(QFileInfo(executable).dir().filePath("PinloomIntegration.ini")).toStdWString();
    wchar_t value[32768]{};
    for (const auto &path : {user, legacy}) {
        GetPrivateProfileStringW(L"Pinloom", L"LibraryPath", L"{missing}", value, 32768, path.c_str());
        if (wcscmp(value, L"{missing}") != 0) return {};
    }
    if (GetFileAttributesW(user.c_str()) == INVALID_FILE_ATTRIBUTES) {
        HANDLE file = CreateFileW(user.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return QStringLiteral("Cannot create the reader connection settings.");
        const WORD bom = 0xfeff;
        DWORD written = 0;
        const bool ok = WriteFile(file, &bom, sizeof(bom), &written, nullptr) && written == sizeof(bom);
        CloseHandle(file);
        if (!ok) return QStringLiteral("Cannot initialize the reader connection settings.");
    }
    const auto binding = (explicitLibrary ? QDir::toNativeSeparators(library) : QString()).toStdWString();
    if (!WritePrivateProfileStringW(L"Pinloom", L"LibraryPath", binding.c_str(), user.c_str()))
        return QStringLiteral("Cannot save the reader library connection.");
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, user.c_str());
    GetPrivateProfileStringW(L"Pinloom", L"LibraryPath", L"{missing}", value, 32768, user.c_str());
    return binding == value ? QString() : QStringLiteral("Cannot verify the reader library connection.");
}
#endif
}

QString nativePdfReaderProfilePath(const QString &libraryDirectory)
{
    return QDir(libraryDirectory).filePath(QStringLiteral("reader-profile"));
}

QString validateNativePdfFiles(const QStringList &files)
{
    if (files.isEmpty()) return QStringLiteral("Specify at least one PDF file.");
    for (const auto &path : files) {
        if (!pinloomReaderFileExtensions().contains(QFileInfo(path).suffix(), Qt::CaseInsensitive))
            return QStringLiteral("Unsupported file format: %1\nPinloom file opening currently supports PDF (.pdf).").arg(path);
        if (!QFileInfo(path).isFile()) return QStringLiteral("The PDF file does not exist: %1").arg(path);
        QFile source(path);
        if (!source.open(QIODevice::ReadOnly)) return QStringLiteral("The PDF file cannot be read: %1").arg(path);
    }
    return {};
}

NativePdfReaderResult openNativePdfFiles(const QStringList &files, const QString &libraryDirectory, bool explicitLibrary)
{
    const QString validation = validateNativePdfFiles(files);
    if (!validation.isEmpty()) return {false, validation};
    const QString executable = pairedSumatraPdfExecutablePath();
    if (!QFileInfo(executable).isFile())
        return {false, QStringLiteral("The paired reader is missing: %1\nRestore the complete Pinloom package.").arg(executable)};
    if (libraryDirectory.isEmpty()) return {false, QStringLiteral("Cannot resolve the Pinloom library directory.")};
#ifdef Q_OS_WIN
    const QString profile = nativePdfReaderProfilePath(libraryDirectory);
    if (!QDir().mkpath(profile)) return {false, QStringLiteral("Cannot create the reader profile directory: %1").arg(profile)};
    const QString connectionError = initializeReaderConnection(profile, executable, libraryDirectory, explicitLibrary);
    if (!connectionError.isEmpty()) return {false, connectionError};
    const QByteArray probe = QJsonDocument(QJsonObject{{"protocol", "pinloom-reader/v1"},
        {"method", "capabilities"}}).toJson(QJsonDocument::Compact);
    for (const auto &file : files) {
        // Absolute paths cannot be interpreted as reader switches. Let Sumatra reuse its
        // executable + profile instance; -reuse-instance would target unrelated readers.
        if (!QProcess::startDetached(executable,
                {"-appdata", QDir::toNativeSeparators(profile), QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath())},
                QFileInfo(executable).absolutePath()))
            return {false, QStringLiteral("Cannot start the paired reader: %1").arg(executable)};
        const QByteArray request = QJsonDocument(QJsonObject{{"protocol", "pinloom-reader/v1"},
            {"method", "page-state"}, {"file", QDir::toNativeSeparators(file)}}).toJson(QJsonDocument::Compact);
        QElapsedTimer timer;
        timer.start();
        bool displayed = false;
        while (timer.elapsed() < 15000 && !displayed) {
            ReaderWindows readers{canonicalPath(executable), {}};
            EnumWindows(collectReader, reinterpret_cast<LPARAM>(&readers));
            for (HWND reader : readers.windows) {
                DWORD_PTR answer = 0;
                if (!IsWindowVisible(reader) || !send(reader, probe, &answer, 300) || answer != 0x504c0001) continue;
                if (send(reader, request, &answer, 500) && answer >= 1 && answer <= 1000000) {
                    displayed = true;
                    break;
                }
            }
            // Sumatra's successful existing-instance forwarder can exit with 1.
            // Only the reader's acknowledgement establishes that the file opened.
            if (!displayed) QThread::msleep(50);
        }
        if (!displayed)
            return {false, QStringLiteral("The paired reader did not display this PDF: %1\nCheck the reader for a password or file error, then retry.").arg(file)};
    }
    return {true, {}};
#else
    Q_UNUSED(explicitLibrary)
    return {false, QStringLiteral("Pinloom file opening requires Windows.")};
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
    const QString bundled = pairedSumatraPdfExecutablePath();
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
