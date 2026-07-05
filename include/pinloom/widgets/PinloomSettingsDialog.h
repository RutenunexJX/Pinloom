#pragma once

#include "pinloom/clip/ClipRepository.h"

#include <QDialog>
#include <QString>
#include <QStringList>

class QCheckBox;
class QLineEdit;
class QSettings;
class QSpinBox;

namespace Pinloom {

struct PinloomAppSettings {
    QString pdfXChangeExecutablePath;
    QString dataDirectory;
    int clipMaxTemporaryClips = 100;
    int clipMaxTextBytes = 256 * 1024;
    int clipTemporaryTtlSeconds = 24 * 60 * 60;
    bool clipExcludeSensitiveText = true;
    bool clipRestoreOriginalClipboardOnInsert = true;
    QStringList clipExcludedSourceApps;
    QStringList clipSensitiveTextMarkers;

    ClipCapturePolicy clipCapturePolicy() const;
};

PinloomAppSettings pinloomDefaultAppSettings(const QString &dataDirectory = {});
PinloomAppSettings loadPinloomAppSettings(QSettings &settings, const QString &dataDirectory = {});
void savePinloomAppSettings(QSettings &settings, const PinloomAppSettings &appSettings);

class PinloomSettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PinloomSettingsDialog(const PinloomAppSettings &settings, QWidget *parent = nullptr);

    PinloomAppSettings settings() const;

private:
    QStringList commaSeparatedValues(const QString &text) const;
    QString commaSeparatedText(const QStringList &values) const;

    QLineEdit *pdfXChangePathEdit_ = nullptr;
    QLineEdit *dataDirectoryEdit_ = nullptr;
    QSpinBox *clipMaxTemporaryClipsSpin_ = nullptr;
    QSpinBox *clipMaxTextBytesSpin_ = nullptr;
    QSpinBox *clipTemporaryTtlSecondsSpin_ = nullptr;
    QCheckBox *clipExcludeSensitiveTextCheck_ = nullptr;
    QCheckBox *clipRestoreOriginalClipboardCheck_ = nullptr;
    QLineEdit *clipExcludedSourceAppsEdit_ = nullptr;
    QLineEdit *clipSensitiveTextMarkersEdit_ = nullptr;
};

} // namespace Pinloom
