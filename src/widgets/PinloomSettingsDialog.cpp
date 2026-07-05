#include "pinloom/widgets/PinloomSettingsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>

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

} // namespace

ClipCapturePolicy PinloomAppSettings::clipCapturePolicy() const
{
    ClipCapturePolicy policy;
    policy.maxTemporaryClips = clipMaxTemporaryClips;
    policy.maxTextBytes = clipMaxTextBytes;
    policy.temporaryTtlSeconds = clipTemporaryTtlSeconds;
    policy.excludeSensitiveText = clipExcludeSensitiveText;
    policy.excludedSourceApps = cleanedValues(clipExcludedSourceApps);
    policy.sensitiveTextMarkers = cleanedValues(clipSensitiveTextMarkers);
    return policy;
}

PinloomAppSettings pinloomDefaultAppSettings(const QString &dataDirectory)
{
    PinloomAppSettings settings;
    settings.dataDirectory = dataDirectory.trimmed();
    return settings;
}

PinloomAppSettings loadPinloomAppSettings(QSettings &settings, const QString &dataDirectory)
{
    PinloomAppSettings loaded = pinloomDefaultAppSettings(dataDirectory);
    loaded.pdfXChangeExecutablePath =
        settings.value(QStringLiteral("applications/pdfXChangeExecutablePath")).toString().trimmed();
    loaded.clipMaxTemporaryClips =
        settingsInt(settings, QStringLiteral("clip/maxTemporaryClips"), loaded.clipMaxTemporaryClips);
    loaded.clipMaxTextBytes =
        settingsInt(settings, QStringLiteral("clip/maxTextBytes"), loaded.clipMaxTextBytes);
    loaded.clipTemporaryTtlSeconds =
        settingsInt(settings, QStringLiteral("clip/temporaryTtlSeconds"), loaded.clipTemporaryTtlSeconds);
    loaded.clipExcludeSensitiveText =
        settings.value(QStringLiteral("clip/excludeSensitiveText"), loaded.clipExcludeSensitiveText).toBool();
    loaded.clipRestoreOriginalClipboardOnInsert =
        settings.value(QStringLiteral("clip/restoreOriginalClipboardOnInsert"),
                       loaded.clipRestoreOriginalClipboardOnInsert).toBool();
    loaded.clipExcludedSourceApps =
        cleanedValues(settings.value(QStringLiteral("clip/excludedSourceApps")).toStringList());
    loaded.clipSensitiveTextMarkers =
        cleanedValues(settings.value(QStringLiteral("clip/sensitiveTextMarkers")).toStringList());
    return loaded;
}

void savePinloomAppSettings(QSettings &settings, const PinloomAppSettings &appSettings)
{
    settings.setValue(QStringLiteral("applications/pdfXChangeExecutablePath"),
                      appSettings.pdfXChangeExecutablePath.trimmed());
    settings.setValue(QStringLiteral("clip/maxTemporaryClips"), appSettings.clipMaxTemporaryClips);
    settings.setValue(QStringLiteral("clip/maxTextBytes"), appSettings.clipMaxTextBytes);
    settings.setValue(QStringLiteral("clip/temporaryTtlSeconds"), appSettings.clipTemporaryTtlSeconds);
    settings.setValue(QStringLiteral("clip/excludeSensitiveText"), appSettings.clipExcludeSensitiveText);
    settings.setValue(QStringLiteral("clip/restoreOriginalClipboardOnInsert"),
                      appSettings.clipRestoreOriginalClipboardOnInsert);
    settings.setValue(QStringLiteral("clip/excludedSourceApps"),
                      cleanedValues(appSettings.clipExcludedSourceApps));
    settings.setValue(QStringLiteral("clip/sensitiveTextMarkers"),
                      cleanedValues(appSettings.clipSensitiveTextMarkers));
}

PinloomSettingsDialog::PinloomSettingsDialog(const PinloomAppSettings &settings, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Pinloom Settings"));

    auto *form = new QFormLayout(this);

    auto *pdfPathRow = new QWidget(this);
    auto *pdfPathLayout = new QHBoxLayout(pdfPathRow);
    pdfPathLayout->setContentsMargins(0, 0, 0, 0);
    pdfXChangePathEdit_ = new QLineEdit(settings.pdfXChangeExecutablePath, pdfPathRow);
    pdfXChangePathEdit_->setObjectName(QStringLiteral("pdfXChangePathEdit"));
    auto *browsePdfButton = new QPushButton(tr("Browse"), pdfPathRow);
    browsePdfButton->setObjectName(QStringLiteral("browsePdfXChangeButton"));
    pdfPathLayout->addWidget(pdfXChangePathEdit_, 1);
    pdfPathLayout->addWidget(browsePdfButton);

    dataDirectoryEdit_ = new QLineEdit(settings.dataDirectory, this);
    dataDirectoryEdit_->setObjectName(QStringLiteral("dataDirectoryEdit"));
    dataDirectoryEdit_->setReadOnly(true);

    clipMaxTemporaryClipsSpin_ = new QSpinBox(this);
    clipMaxTemporaryClipsSpin_->setObjectName(QStringLiteral("clipMaxTemporaryClipsSpin"));
    clipMaxTemporaryClipsSpin_->setRange(0, 1000000);
    clipMaxTemporaryClipsSpin_->setValue(settings.clipMaxTemporaryClips);

    clipMaxTextBytesSpin_ = new QSpinBox(this);
    clipMaxTextBytesSpin_->setObjectName(QStringLiteral("clipMaxTextBytesSpin"));
    clipMaxTextBytesSpin_->setRange(0, 100 * 1024 * 1024);
    clipMaxTextBytesSpin_->setValue(settings.clipMaxTextBytes);

    clipTemporaryTtlSecondsSpin_ = new QSpinBox(this);
    clipTemporaryTtlSecondsSpin_->setObjectName(QStringLiteral("clipTemporaryTtlSecondsSpin"));
    clipTemporaryTtlSecondsSpin_->setRange(0, 365 * 24 * 60 * 60);
    clipTemporaryTtlSecondsSpin_->setValue(settings.clipTemporaryTtlSeconds);

    clipExcludeSensitiveTextCheck_ = new QCheckBox(tr("Filter common secret patterns"), this);
    clipExcludeSensitiveTextCheck_->setObjectName(QStringLiteral("clipExcludeSensitiveTextCheck"));
    clipExcludeSensitiveTextCheck_->setChecked(settings.clipExcludeSensitiveText);

    clipRestoreOriginalClipboardCheck_ = new QCheckBox(tr("Restore original clipboard after inserting a Saved Clip"), this);
    clipRestoreOriginalClipboardCheck_->setObjectName(QStringLiteral("clipRestoreOriginalClipboardCheck"));
    clipRestoreOriginalClipboardCheck_->setChecked(settings.clipRestoreOriginalClipboardOnInsert);

    clipExcludedSourceAppsEdit_ = new QLineEdit(commaSeparatedText(settings.clipExcludedSourceApps), this);
    clipExcludedSourceAppsEdit_->setObjectName(QStringLiteral("clipExcludedSourceAppsEdit"));

    clipSensitiveTextMarkersEdit_ = new QLineEdit(commaSeparatedText(settings.clipSensitiveTextMarkers), this);
    clipSensitiveTextMarkersEdit_->setObjectName(QStringLiteral("clipSensitiveTextMarkersEdit"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("settingsButtons"));

    form->addRow(tr("PDF-XChange"), pdfPathRow);
    form->addRow(tr("Data directory"), dataDirectoryEdit_);
    form->addRow(tr("Clip history limit"), clipMaxTemporaryClipsSpin_);
    form->addRow(tr("Clip size limit (bytes)"), clipMaxTextBytesSpin_);
    form->addRow(tr("Clip history TTL (seconds)"), clipTemporaryTtlSecondsSpin_);
    form->addRow(QString(), clipExcludeSensitiveTextCheck_);
    form->addRow(QString(), clipRestoreOriginalClipboardCheck_);
    form->addRow(tr("Clip app blacklist"), clipExcludedSourceAppsEdit_);
    form->addRow(tr("Sensitive markers"), clipSensitiveTextMarkersEdit_);
    form->addWidget(buttons);

    connect(browsePdfButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this,
                                                          tr("PDF-XChange Executable"),
                                                          pdfXChangePathEdit_->text());
        if (!path.trimmed().isEmpty()) {
            pdfXChangePathEdit_->setText(path.trimmed());
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

PinloomAppSettings PinloomSettingsDialog::settings() const
{
    PinloomAppSettings settings;
    settings.pdfXChangeExecutablePath = pdfXChangePathEdit_->text().trimmed();
    settings.dataDirectory = dataDirectoryEdit_->text().trimmed();
    settings.clipMaxTemporaryClips = clipMaxTemporaryClipsSpin_->value();
    settings.clipMaxTextBytes = clipMaxTextBytesSpin_->value();
    settings.clipTemporaryTtlSeconds = clipTemporaryTtlSecondsSpin_->value();
    settings.clipExcludeSensitiveText = clipExcludeSensitiveTextCheck_->isChecked();
    settings.clipRestoreOriginalClipboardOnInsert = clipRestoreOriginalClipboardCheck_->isChecked();
    settings.clipExcludedSourceApps = commaSeparatedValues(clipExcludedSourceAppsEdit_->text());
    settings.clipSensitiveTextMarkers = commaSeparatedValues(clipSensitiveTextMarkersEdit_->text());
    return settings;
}

QStringList PinloomSettingsDialog::commaSeparatedValues(const QString &text) const
{
    return cleanedValues(text.split(QLatin1Char(','), Qt::SkipEmptyParts));
}

QString PinloomSettingsDialog::commaSeparatedText(const QStringList &values) const
{
    return cleanedValues(values).join(QStringLiteral(", "));
}

} // namespace Pinloom
