#pragma once

#include "pinloom/clip/ClipRepository.h"

#include "pinloom/widgets/PinloomUiControls.h"
#include <QString>
#include <QStringList>

class QCheckBox;
class QLabel;
class QLineEdit;
class QSettings;
class QSpinBox;

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

class PinloomSettingsDialog final : public Ui::Dialog {
    Q_OBJECT

public:
    explicit PinloomSettingsDialog(const PinloomAppSettings &settings, QWidget *parent = nullptr);

    PinloomAppSettings settings() const;

private:
    QStringList commaSeparatedValues(const QString &text) const;
    QString commaSeparatedText(const QStringList &values) const;

    QLabel *sumatraPdfStatusLabel_ = nullptr;
    QLineEdit *sumatraPdfPathEdit_ = nullptr;
    QLabel *obsidianStatusLabel_ = nullptr;
    QLineEdit *obsidianVaultPathEdit_ = nullptr;
    QLineEdit *obsidianArchiveDirectoryEdit_ = nullptr;
    QLineEdit *dataDirectoryEdit_ = nullptr;
    QLabel *dataDirectoryStatusLabel_ = nullptr;
    QLineEdit *defaultLibraryRootPathEdit_ = nullptr;
    QLabel *defaultLibraryRootStatusLabel_ = nullptr;
    QSpinBox *clipMaxTemporaryClipsSpin_ = nullptr;
    QSpinBox *clipMaxTextBytesSpin_ = nullptr;
    QSpinBox *clipTemporaryTtlSecondsSpin_ = nullptr;
    QCheckBox *clipAutomaticCaptureCheck_ = nullptr;
    QCheckBox *clipExcludeSensitiveTextCheck_ = nullptr;
    QCheckBox *clipRestoreOriginalClipboardCheck_ = nullptr;
    QLineEdit *clipExcludedSourceAppsEdit_ = nullptr;
    QLineEdit *clipSensitiveTextMarkersEdit_ = nullptr;
    QLabel *clipPrivacyStatusLabel_ = nullptr;
};

} // namespace Pinloom
