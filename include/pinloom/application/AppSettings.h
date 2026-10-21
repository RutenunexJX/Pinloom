#pragma once

#include "pinloom/clip/ClipRepository.h"
class QSettings;

namespace Pinloom {

struct PinloomAppSettings {
    QString sumatraPdfExecutablePath;
    QString obsidianVaultPath;
    QString obsidianArchiveDirectory = QStringLiteral("Pinloom Clips");
    QString dataDirectory;
    QString defaultLibraryRootPath;
    int clipMaxTemporaryClips = 100;
    int clipMaxTextBytes = 256 * 1024;
    int clipTemporaryTtlSeconds = 24 * 60 * 60;
    bool clipAutomaticCaptureEnabled = false;
    bool clipExcludeSensitiveText = true;
    bool clipRestoreOriginalClipboardOnInsert = true;
    QStringList clipExcludedSourceApps;
    QStringList clipSensitiveTextMarkers;

    ClipCapturePolicy clipCapturePolicy() const;
};

PinloomAppSettings pinloomDefaultAppSettings(const QString &dataDirectory = {});
PinloomAppSettings loadPinloomAppSettings(QSettings &settings, const QString &dataDirectory = {});
void savePinloomAppSettings(QSettings &settings, const PinloomAppSettings &appSettings);

} // namespace Pinloom
