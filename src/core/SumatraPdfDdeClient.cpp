#include "pinloom/core/SumatraPdfDdeClient.h"

#include <QDir>
#include <QHash>
#include <QStringList>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ddeml.h>
#endif

namespace Pinloom {

namespace {

QString normalizedDdeCommand(QString command)
{
    command = command.trimmed();
    if (command.isEmpty()) {
        return {};
    }
    if (!command.startsWith(QLatin1Char('['))) {
        command.prepend(QLatin1Char('['));
    }
    if (!command.endsWith(QLatin1Char(']'))) {
        command.append(QLatin1Char(']'));
    }
    return command;
}

std::optional<double> toDoubleValue(const QString &value)
{
    bool ok = false;
    const double number = value.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(number)) {
        return std::nullopt;
    }
    return number;
}

std::optional<int> toIntValue(const QString &value)
{
    const std::optional<double> number = toDoubleValue(value);
    if (!number.has_value()) {
        return std::nullopt;
    }
    return static_cast<int>(std::round(number.value()));
}

QString cleanDdePdfPath(QString path)
{
    path = path.trimmed();
    if (path.isEmpty()) {
        return {};
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

QHash<QString, QString> keyValueLines(const QString &text)
{
    QHash<QString, QString> values;
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const qsizetype separator = line.indexOf(QLatin1Char(':'));
        if (separator <= 0) {
            continue;
        }
        const QString key = line.left(separator).trimmed().toLower();
        const QString value = line.mid(separator + 1).trimmed();
        if (!key.isEmpty()) {
            values.insert(key, value);
        }
    }
    return values;
}

#ifdef Q_OS_WIN
HDDEDATA CALLBACK ddeCallback(UINT, UINT, HCONV, HSZ, HSZ, HDDEDATA, ULONG_PTR, ULONG_PTR)
{
    return nullptr;
}

QString ddeErrorText(DWORD errorCode)
{
    switch (errorCode) {
    case DMLERR_NO_ERROR:
        return QStringLiteral("no error");
    case DMLERR_ADVACKTIMEOUT:
        return QStringLiteral("DDE advise acknowledgement timeout");
    case DMLERR_BUSY:
        return QStringLiteral("DDE server is busy");
    case DMLERR_DATAACKTIMEOUT:
        return QStringLiteral("DDE data acknowledgement timeout");
    case DMLERR_DLL_NOT_INITIALIZED:
        return QStringLiteral("DDE was not initialized");
    case DMLERR_DLL_USAGE:
        return QStringLiteral("DDE usage error");
    case DMLERR_EXECACKTIMEOUT:
        return QStringLiteral("DDE execute acknowledgement timeout");
    case DMLERR_INVALIDPARAMETER:
        return QStringLiteral("DDE invalid parameter");
    case DMLERR_LOW_MEMORY:
        return QStringLiteral("DDE low memory");
    case DMLERR_MEMORY_ERROR:
        return QStringLiteral("DDE memory error");
    case DMLERR_NO_CONV_ESTABLISHED:
        return QStringLiteral("DDE conversation was not established");
    case DMLERR_NOTPROCESSED:
        return QStringLiteral("DDE request was not processed");
    case DMLERR_POKEACKTIMEOUT:
        return QStringLiteral("DDE poke acknowledgement timeout");
    case DMLERR_POSTMSG_FAILED:
        return QStringLiteral("DDE post message failed");
    case DMLERR_REENTRANCY:
        return QStringLiteral("DDE reentrancy error");
    case DMLERR_SERVER_DIED:
        return QStringLiteral("DDE server died");
    case DMLERR_SYS_ERROR:
        return QStringLiteral("DDE system error");
    case DMLERR_UNADVACKTIMEOUT:
        return QStringLiteral("DDE unadvise acknowledgement timeout");
    case DMLERR_UNFOUND_QUEUE_ID:
        return QStringLiteral("DDE queue id not found");
    default:
        return QStringLiteral("DDE error %1").arg(errorCode);
    }
}

QString stringFromDdeData(HDDEDATA data, UINT format)
{
    DWORD byteCount = 0;
    BYTE *bytes = DdeAccessData(data, &byteCount);
    if (!bytes || byteCount == 0) {
        if (bytes) {
            DdeUnaccessData(data);
        }
        return {};
    }

    QString text;
    if (format == CF_UNICODETEXT) {
        int charCount = static_cast<int>(byteCount / sizeof(wchar_t));
        if (charCount > 0 && reinterpret_cast<const wchar_t *>(bytes)[charCount - 1] == L'\0') {
            --charCount;
        }
        text = QString::fromWCharArray(reinterpret_cast<const wchar_t *>(bytes), charCount);
    } else {
        int charCount = static_cast<int>(byteCount);
        if (charCount > 0 && reinterpret_cast<const char *>(bytes)[charCount - 1] == '\0') {
            --charCount;
        }
        text = QString::fromLocal8Bit(reinterpret_cast<const char *>(bytes), charCount);
    }

    DdeUnaccessData(data);
    return text;
}
#endif

} // namespace

bool SumatraPdfDdeRequestResult::success() const
{
    return error.isEmpty();
}

bool SumatraPdfDdeFileState::success() const
{
    return error.isEmpty() && !path.trimmed().isEmpty() && page > 0;
}

bool SumatraPdfDdeMousePosition::success() const
{
    return error.isEmpty()
        && page > 0
        && std::isfinite(x)
        && std::isfinite(y);
}

SumatraPdfDdeRequestResult requestSumatraPdfDdeCommand(
    const QString &command,
    int timeoutMilliseconds)
{
    SumatraPdfDdeRequestResult result;
    const QString itemText = normalizedDdeCommand(command);
    if (itemText.isEmpty()) {
        result.error = QStringLiteral("SumatraPDF DDE command is empty");
        return result;
    }

#ifndef Q_OS_WIN
    Q_UNUSED(timeoutMilliseconds);
    result.error = QStringLiteral("SumatraPDF DDE is only available on Windows");
    return result;
#else
    DWORD instance = 0;
    const UINT initializeResult = DdeInitializeW(&instance, ddeCallback, APPCLASS_STANDARD, 0);
    if (initializeResult != DMLERR_NO_ERROR) {
        result.error = ddeErrorText(initializeResult);
        return result;
    }

    HSZ service = nullptr;
    HSZ topic = nullptr;
    HSZ item = nullptr;
    HCONV conversation = nullptr;
    HDDEDATA data = nullptr;

    auto cleanup = [&]() {
        if (data) {
            DdeFreeDataHandle(data);
        }
        if (conversation) {
            DdeDisconnect(conversation);
        }
        if (item) {
            DdeFreeStringHandle(instance, item);
        }
        if (topic) {
            DdeFreeStringHandle(instance, topic);
        }
        if (service) {
            DdeFreeStringHandle(instance, service);
        }
        DdeUninitialize(instance);
    };

    service = DdeCreateStringHandleW(instance, L"SUMATRA", CP_WINUNICODE);
    topic = DdeCreateStringHandleW(instance, L"control", CP_WINUNICODE);
    if (!service || !topic) {
        result.error = QStringLiteral("Unable to create SumatraPDF DDE string handles");
        cleanup();
        return result;
    }

    conversation = DdeConnect(instance, service, topic, nullptr);
    if (!conversation) {
        result.error = ddeErrorText(DdeGetLastError(instance));
        cleanup();
        return result;
    }

    const std::wstring itemWide = itemText.toStdWString();
    item = DdeCreateStringHandleW(instance, itemWide.c_str(), CP_WINUNICODE);
    if (!item) {
        result.error = QStringLiteral("Unable to create SumatraPDF DDE item handle");
        cleanup();
        return result;
    }

    DWORD transactionResult = 0;
    UINT format = CF_UNICODETEXT;
    data = DdeClientTransaction(nullptr,
                                0,
                                conversation,
                                item,
                                format,
                                XTYP_REQUEST,
                                static_cast<DWORD>(std::max(1, timeoutMilliseconds)),
                                &transactionResult);
    if (!data) {
        format = CF_TEXT;
        data = DdeClientTransaction(nullptr,
                                    0,
                                    conversation,
                                    item,
                                    format,
                                    XTYP_REQUEST,
                                    static_cast<DWORD>(std::max(1, timeoutMilliseconds)),
                                    &transactionResult);
    }

    if (!data) {
        result.error = ddeErrorText(DdeGetLastError(instance));
        cleanup();
        return result;
    }

    result.text = stringFromDdeData(data, format).trimmed();
    if (result.text.isEmpty()) {
        result.error = QStringLiteral("SumatraPDF DDE returned an empty response");
    }
    cleanup();
    return result;
#endif
}

SumatraPdfDdeFileState parseSumatraPdfDdeFileState(const QString &text)
{
    SumatraPdfDdeFileState state;
    state.rawText = text.trimmed();

    const QHash<QString, QString> values = keyValueLines(text);
    const QString error = values.value(QStringLiteral("error")).trimmed();
    if (!error.isEmpty()) {
        state.error = error;
        return state;
    }

    state.path = cleanDdePdfPath(values.value(QStringLiteral("path")));
    state.view = values.value(QStringLiteral("view")).trimmed();
    state.version = values.value(QStringLiteral("sumver")).trimmed();

    const std::optional<int> page = toIntValue(values.value(QStringLiteral("page")));
    const std::optional<int> pageCount = toIntValue(values.value(QStringLiteral("pagecount")));
    const std::optional<double> zoom = toDoubleValue(values.value(QStringLiteral("zoom")));
    if (page.has_value()) {
        state.page = page.value();
    }
    if (pageCount.has_value()) {
        state.pageCount = pageCount.value();
    }
    if (zoom.has_value()) {
        state.zoom = zoom.value();
    }

    if (state.path.trimmed().isEmpty()) {
        state.error = QStringLiteral("SumatraPDF DDE file state did not include a path");
    } else if (state.page <= 0) {
        state.error = QStringLiteral("SumatraPDF DDE file state did not include a valid page");
    }
    return state;
}

SumatraPdfDdeMousePosition parseSumatraPdfDdeMousePosition(const QString &text)
{
    SumatraPdfDdeMousePosition position;
    position.rawText = text.trimmed();

    const QHash<QString, QString> values = keyValueLines(text);
    const QString error = values.value(QStringLiteral("error")).trimmed();
    if (!error.isEmpty()) {
        position.error = error;
        return position;
    }

    const std::optional<int> page = toIntValue(values.value(QStringLiteral("page")));
    const std::optional<double> x = toDoubleValue(values.value(QStringLiteral("x")));
    const std::optional<double> y = toDoubleValue(values.value(QStringLiteral("y")));
    const std::optional<double> yPdf = toDoubleValue(values.value(QStringLiteral("ypdf")));
    if (page.has_value()) {
        position.page = page.value();
    }
    if (x.has_value()) {
        position.x = x.value();
    }
    if (y.has_value()) {
        position.y = y.value();
    }
    if (yPdf.has_value()) {
        position.yPdf = yPdf.value();
        position.hasYPdf = true;
    }

    if (position.page <= 0) {
        position.error = QStringLiteral("SumatraPDF mouse cursor is not over a page");
    } else if (!x.has_value() || !y.has_value()) {
        position.error = QStringLiteral("SumatraPDF DDE mouse position did not include valid coordinates");
    }
    return position;
}

SumatraPdfDdeFileState requestSumatraPdfDdeFileState(int timeoutMilliseconds)
{
    const SumatraPdfDdeRequestResult response =
        requestSumatraPdfDdeCommand(QStringLiteral("GetFileState()"), timeoutMilliseconds);
    if (!response.success()) {
        SumatraPdfDdeFileState state;
        state.error = response.error;
        return state;
    }
    return parseSumatraPdfDdeFileState(response.text);
}

SumatraPdfDdeMousePosition requestSumatraPdfDdeMousePosition(int timeoutMilliseconds)
{
    const SumatraPdfDdeRequestResult response =
        requestSumatraPdfDdeCommand(QStringLiteral("GetMousePos()"), timeoutMilliseconds);
    if (!response.success()) {
        SumatraPdfDdeMousePosition position;
        position.error = response.error;
        return position;
    }
    return parseSumatraPdfDdeMousePosition(response.text);
}

} // namespace Pinloom
