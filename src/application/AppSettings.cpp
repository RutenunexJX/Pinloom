#include "pinloom/application/AppSettings.h"
#include "pinloom/core/LibraryRoot.h"
#include <QSettings>

namespace Pinloom {
namespace {
QStringList cleanedValues(const QStringList &values)
{
    QStringList cleaned;
    for (const QString &value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty() && !cleaned.contains(trimmed, Qt::CaseInsensitive)) {
            cleaned.append(trimmed);
        }
    }
    return cleaned;
}

int settingsInt(QSettings &settings, const QString &key, int fallback)
{
    bool ok = false;
    const int value = settings.value(key, fallback).toInt(&ok);
    return ok ? value : fallback;
}

}

ClipCapturePolicy PinloomAppSettings::clipCapturePolicy() const
{
    ClipCapturePolicy policy;
    policy.maxTemporaryClips = clipMaxTemporaryClips;
    policy.maxTextBytes = clipMaxTextBytes;
    policy.temporaryTtlSeconds = clipTemporaryTtlSeconds;
    policy.capturePaused = !clipAutomaticCaptureEnabled;
    policy.excludeSensitiveText = clipExcludeSensitiveText;
    policy.excludedSourceApps = cleanedValues(clipExcludedSourceApps);
    policy.sensitiveTextMarkers = cleanedValues(clipSensitiveTextMarkers);
    return policy;
}

PinloomAppSettings pinloomDefaultAppSettings(const QString &dataDirectory)
{
    PinloomAppSettings settings;
    settings.dataDirectory = dataDirectory.trimmed();
    settings.defaultLibraryRootPath = defaultPinloomSyncRootPath(dataDirectory);
    settings.clipExcludedSourceApps = {
        QStringLiteral("1Password.exe"),
        QStringLiteral("Bitwarden.exe"),
        QStringLiteral("KeePass.exe"),
        QStringLiteral("KeePassXC.exe")
    };
    return settings;
}

PinloomAppSettings loadPinloomAppSettings(QSettings &settings, const QString &dataDirectory)
{
    PinloomAppSettings loaded = pinloomDefaultAppSettings(dataDirectory);
    loaded.sumatraPdfExecutablePath =
        settings.value(QStringLiteral("applications/sumatraPdfExecutablePath")).toString().trimmed();
    loaded.obsidianVaultPath =
        settings.value(QStringLiteral("obsidian/vaultPath")).toString().trimmed();
    loaded.obsidianArchiveDirectory =
        settings.value(QStringLiteral("obsidian/archiveDirectory"), loaded.obsidianArchiveDirectory)
            .toString()
            .trimmed();
    loaded.defaultLibraryRootPath = normalizedLibraryRootPath(
        settings.value(QStringLiteral("library/defaultRootPath"),
                       loaded.defaultLibraryRootPath).toString());
    loaded.clipMaxTemporaryClips =
        settingsInt(settings, QStringLiteral("clip/maxTemporaryClips"), loaded.clipMaxTemporaryClips);
    loaded.clipMaxTextBytes =
        settingsInt(settings, QStringLiteral("clip/maxTextBytes"), loaded.clipMaxTextBytes);
    loaded.clipTemporaryTtlSeconds =
        settingsInt(settings, QStringLiteral("clip/temporaryTtlSeconds"), loaded.clipTemporaryTtlSeconds);
    loaded.clipAutomaticCaptureEnabled =
        settings.value(QStringLiteral("clip/automaticCaptureEnabled"),
                       loaded.clipAutomaticCaptureEnabled).toBool();
    loaded.clipExcludeSensitiveText =
        settings.value(QStringLiteral("clip/excludeSensitiveText"), loaded.clipExcludeSensitiveText).toBool();
    loaded.clipRestoreOriginalClipboardOnInsert =
        settings.value(QStringLiteral("clip/restoreOriginalClipboardOnInsert"),
                       loaded.clipRestoreOriginalClipboardOnInsert).toBool();
    loaded.clipExcludedSourceApps =
        cleanedValues(settings.value(QStringLiteral("clip/excludedSourceApps"),
                                     loaded.clipExcludedSourceApps).toStringList());
    loaded.clipSensitiveTextMarkers =
        cleanedValues(settings.value(QStringLiteral("clip/sensitiveTextMarkers")).toStringList());
    return loaded;
}

void savePinloomAppSettings(QSettings &settings, const PinloomAppSettings &appSettings)
{
    settings.setValue(QStringLiteral("applications/sumatraPdfExecutablePath"),
                      appSettings.sumatraPdfExecutablePath.trimmed());
    settings.setValue(QStringLiteral("obsidian/vaultPath"), appSettings.obsidianVaultPath.trimmed());
    settings.setValue(QStringLiteral("obsidian/archiveDirectory"),
                      appSettings.obsidianArchiveDirectory.trimmed());
    settings.setValue(QStringLiteral("library/defaultRootPath"),
                      normalizedLibraryRootPath(appSettings.defaultLibraryRootPath));
    settings.setValue(QStringLiteral("clip/maxTemporaryClips"), appSettings.clipMaxTemporaryClips);
    settings.setValue(QStringLiteral("clip/maxTextBytes"), appSettings.clipMaxTextBytes);
    settings.setValue(QStringLiteral("clip/temporaryTtlSeconds"), appSettings.clipTemporaryTtlSeconds);
    settings.setValue(QStringLiteral("clip/automaticCaptureEnabled"),
                      appSettings.clipAutomaticCaptureEnabled);
    settings.setValue(QStringLiteral("clip/excludeSensitiveText"), appSettings.clipExcludeSensitiveText);
    settings.setValue(QStringLiteral("clip/restoreOriginalClipboardOnInsert"),
                      appSettings.clipRestoreOriginalClipboardOnInsert);
    settings.setValue(QStringLiteral("clip/excludedSourceApps"),
                      cleanedValues(appSettings.clipExcludedSourceApps));
    settings.setValue(QStringLiteral("clip/sensitiveTextMarkers"),
                      cleanedValues(appSettings.clipSensitiveTextMarkers));
}

} // namespace Pinloom
