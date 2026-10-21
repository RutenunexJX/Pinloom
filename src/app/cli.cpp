#include "pinloom/application/AppSettings.h"
#include "pinloom/application/CommandService.h"
#include "pinloom/application/CommandTransport.h"
#include "pinloom/core/AppDataDirectory.h"
#include "pinloom/core/SqliteLibraryRepository.h"
#include "pinloom/clip/ObsidianClipStore.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <cstdio>

namespace {
int output(QJsonObject response)
{
    auto bytes = QJsonDocument(response).toJson(QJsonDocument::Compact);
    if (bytes.size() > Pinloom::CommandMaxResponseBytes) {
        response = Pinloom::commandError("output_limit", "Response exceeds 8 MiB; reduce the limit or use export", 2);
        bytes = QJsonDocument(response).toJson(QJsonDocument::Compact);
    }
    bytes.append('\n');
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    std::fflush(stdout);
    return response["exitCode"].toInt();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setOrganizationName("Pinloom"); app.setApplicationName("Pinloom");
    QCommandLineParser parser;
    parser.setApplicationDescription("Pinloom public command interface. JSON on stdout; diagnostics on stderr.");
    parser.addOption({{"h", "help"}, "Show invocation help and capabilities as JSON"});
    parser.addOption({"library", "Explicit library data directory", "directory"});
    parser.addOption({"input", "UTF-8 JSON parameters file, or '-' for stdin", "file"});
    parser.addOption({"if-version", "Required version precondition for writes", "version"});
    parser.addOption({"dry-run", "Validate and preview without writing"});
    parser.addOption({"require-gui", "Fail if the selected GUI session is not running"});
    parser.addOption({"timeout", "GUI response timeout in milliseconds (100..60000)", "milliseconds", "5000"});
    parser.addPositionalArgument("method", "Use capabilities/schema to discover methods and parameters");
    if (!parser.parse(app.arguments())) return output(Pinloom::commandError("invalid_argument", parser.errorText(), 2));
    const auto arguments = parser.positionalArguments();
    if (parser.isSet("help") || (arguments.size() == 1 && (arguments[0] == "capabilities" || arguments[0] == "schema"))) {
        auto result = Pinloom::commandCapabilities();
        result.insert("usage", parser.helpText());
        return output({{"protocol", "pinloom-cli/v1"}, {"ok", true}, {"exitCode", 0}, {"result", result}});
    }
    if (arguments.size() != 1 || parser.value("library").trimmed().isEmpty())
        return output(Pinloom::commandError("invalid_argument", "Exactly one method and --library are required; use --help", 2));
    bool timeoutOk = false;
    const int timeout = parser.value("timeout").toInt(&timeoutOk);
    if (!timeoutOk || timeout < 100 || timeout > 60000)
        return output(Pinloom::commandError("invalid_argument", "--timeout must be 100..60000 milliseconds", 2));
    QJsonObject params;
    if (parser.isSet("input")) {
        QFile input;
        bool opened = false;
        if (parser.value("input") == "-") opened = input.open(stdin, QIODevice::ReadOnly);
        else { input.setFileName(parser.value("input")); opened = input.open(QIODevice::ReadOnly); }
        if (!opened) return output(Pinloom::commandError("input_error", input.errorString(), 2));
        QByteArray bytes = input.read(Pinloom::CommandMaxRequestBytes + 1);
        if (bytes.size() > Pinloom::CommandMaxRequestBytes)
            return output(Pinloom::commandError("input_limit", "Input exceeds 1 MiB", 2));
        if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
            return output(Pinloom::commandError("invalid_json", "Expected a UTF-8 JSON object: " + error.errorString(), 2));
        params = document.object();
    }
    if (parser.isSet("if-version")) params.insert("ifVersion", parser.value("if-version"));
    if (parser.isSet("dry-run")) params.insert("dryRun", true);
    auto path = Pinloom::canonicalLibraryPath(parser.value("library"));
    const auto requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject request{{"protocol", "pinloom-cli/v1"}, {"requestId", requestId}, {"library", path},
                        {"baseDirectory", QDir::currentPath()}, {"method", arguments[0]}, {"params", params}};
    if (QJsonDocument(request).toJson(QJsonDocument::Compact).size() > Pinloom::CommandMaxRequestBytes)
        return output(Pinloom::commandError("input_limit", "Request envelope exceeds 1 MiB", 2));
    const auto remote = Pinloom::sendLibraryCommand(path, request, timeout);
    if (remote.connected) return output(remote.response);
    if (parser.isSet("require-gui")) return output(Pinloom::commandError("gui_required", "No GUI session for the selected library", 5));
    const bool init = arguments[0] == "library.init";
    if (!init && (!QFileInfo(QDir(path).filePath("pinloom.sqlite3")).isFile()
                 || !QFileInfo(QDir(path).filePath("pinloom_clip.sqlite3")).isFile()))
        return output(Pinloom::commandError("not_found", "Library is not initialized; use library.init explicitly", 3));
    if (init && (!params.isEmpty())) return output(Pinloom::commandError("invalid_argument", "library.init takes no parameters", 2));
    if (init && !QDir().mkpath(path)) return output(Pinloom::commandError("storage_error", "Cannot create library directory", 6));
    path = Pinloom::canonicalLibraryPath(path);
    request.insert("library", path);
    Pinloom::LibrarySessionLock lock(path);
    if (!lock.tryLock()) {
        // A GUI may have completed startup between the first connect and lock attempt.
        const auto retry = Pinloom::sendLibraryCommand(path, request, timeout);
        if (retry.connected) return output(retry.response);
        return output(Pinloom::commandError("session_busy", "Library is owned by another process; no offline fallback was attempted", 5));
    }
    Pinloom::SqliteLibraryRepository library;
    Pinloom::SqliteClipRepository clipRepository;
    const auto identity = QDir(path).filePath("pinloom_identity.sqlite3");
    if (!library.open(QDir(path).filePath("pinloom.sqlite3"), identity) || !library.initialize())
        return output(Pinloom::commandError("storage_error", library.lastError(), 6));
    if (!clipRepository.open(QDir(path).filePath("pinloom_clip.sqlite3"), identity) || !clipRepository.initialize())
        return output(Pinloom::commandError("storage_error", clipRepository.lastError(), 6));
    QSettings profile(QDir(path).filePath("profile.ini"), QSettings::IniFormat);
    QSettings defaultSettings;
    const auto defaultData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const auto configuredDefault = Pinloom::configuredAppDataDirectory(defaultSettings, defaultData);
    QSettings &settings = QFileInfo::exists(profile.fileName()) ||
        Pinloom::libraryCommandEndpoint(configuredDefault) != Pinloom::libraryCommandEndpoint(path)
        ? profile : defaultSettings;
    const auto config = Pinloom::loadPinloomAppSettings(settings, path);
    Pinloom::ObsidianClipStore store({config.obsidianVaultPath, config.obsidianArchiveDirectory});
    Pinloom::PersistentClipService clips(clipRepository, store);
    Pinloom::CommandService service(library, clipRepository, clips, path);
    return output(service.execute(request));
}
