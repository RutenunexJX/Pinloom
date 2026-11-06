#include "pinloom/core/PinloomStartup.h"

#include <QDir>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace Pinloom {

QStringList pinloomProcessArguments(int argc, char **argv)
{
    QStringList result;
#ifdef Q_OS_WIN
    int count = 0;
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments) {
        for (int i = 0; i < count; ++i) result.append(QString::fromWCharArray(arguments[i]));
        LocalFree(arguments);
        return result;
    }
#endif
    for (int i = 0; i < argc; ++i) result.append(QString::fromLocal8Bit(argv[i]));
    return result;
}

QStringList pinloomReaderFileExtensions()
{
    return {QStringLiteral("pdf")};
}

PinloomStartup parsePinloomStartup(const QStringList &arguments, const QString &workingDirectory)
{
    PinloomStartup result;
    bool filesOnly = false;
    bool specialAction = false;
    for (int i = 1; i < arguments.size(); ++i) {
        const QString argument = arguments[i];
        if (!filesOnly && argument == QLatin1String("--")) {
            filesOnly = true;
            continue;
        }
        if (!filesOnly && argument.compare(QLatin1String("--hidden"), Qt::CaseInsensitive) == 0) {
            result.hidden = true;
            continue;
        }
        if (!filesOnly && argument == QLatin1String("--no-global-hooks")) {
            result.noGlobalHooks = true;
            continue;
        }
        if (!filesOnly && argument == QLatin1String("--library")) {
            if (!result.library.isEmpty() || ++i >= arguments.size() || arguments[i].trimmed().isEmpty()
                || arguments[i].startsWith(QLatin1String("--"))) {
                result.error = QStringLiteral("--library requires one data directory.");
                return result;
            }
            result.library = QDir::cleanPath(QDir(workingDirectory).absoluteFilePath(arguments[i]));
            continue;
        }
        if (!filesOnly && (argument == QLatin1String("--register-file-associations")
            || argument == QLatin1String("--unregister-file-associations")
            || argument == QLatin1String("--package-check"))) {
            if (specialAction) {
                result.error = QStringLiteral("Specify only one startup action.");
                return result;
            }
            specialAction = true;
            result.action = argument == QLatin1String("--register-file-associations")
                ? PinloomStartupAction::RegisterFiles
                : argument == QLatin1String("--unregister-file-associations")
                    ? PinloomStartupAction::UnregisterFiles : PinloomStartupAction::PackageCheck;
            continue;
        }
        if (!filesOnly && argument.startsWith(QLatin1String("pinloom:"), Qt::CaseInsensitive)) {
            if (!result.deepLink.isEmpty()) {
                result.error = QStringLiteral("Specify only one Pinloom link.");
                return result;
            }
            result.deepLink = argument;
            continue;
        }
        if (!filesOnly && argument.startsWith(QLatin1Char('-'))) {
            result.error = QStringLiteral("Unknown option: %1\nUse -- before file names that start with a hyphen.").arg(argument);
            return result;
        }
        if (argument.isEmpty() || argument.contains(QLatin1String("://"))) {
            result.error = QStringLiteral("Specify a local PDF file path.");
            return result;
        }
        const QFileInfo file(QDir(workingDirectory).absoluteFilePath(argument));
        result.files.append(QDir::cleanPath(file.exists() ? file.canonicalFilePath() : file.absoluteFilePath()));
    }
    if ((specialAction && (!result.files.isEmpty() || !result.deepLink.isEmpty()
            || !result.library.isEmpty() || result.hidden || result.noGlobalHooks))
        || (!result.files.isEmpty() && !result.deepLink.isEmpty())) {
        result.error = QStringLiteral("File opening, Pinloom links and maintenance actions must be requested separately.");
        return result;
    }
    if (!result.files.isEmpty()) result.action = PinloomStartupAction::OpenFiles;
    else if (filesOnly) result.error = QStringLiteral("Specify at least one PDF file after --.");
    return result;
}

} // namespace Pinloom
