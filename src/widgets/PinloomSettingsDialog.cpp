#include "pinloom/widgets/PinloomSettingsDialog.h"

#include "pinloom/core/PdfXChangeCommand.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
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

QString siblingExecutablePath(const QString &baseName)
{
    QString executableName = baseName;
#ifdef Q_OS_WIN
    if (!executableName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        executableName.append(QStringLiteral(".exe"));
    }
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(executableName);
}

QString defaultPinloomPdfProxyExecutablePath()
{
    return QDir::toNativeSeparators(siblingExecutablePath(QStringLiteral("pinloom_pdf_proxy")));
}

QString pdfProxyStatusText(const QString &proxyPath)
{
    const QFileInfo proxy(proxyPath.trimmed());
    if (proxy.exists() && proxy.isFile()) {
        return QStringLiteral("Ready. Use this executable as the Windows PDF default app.");
    }
    return QStringLiteral("Missing. Build pinloom_pdf_proxy before enabling enhanced PDF mode.");
}

QString resolvedPdfXChangePath(const QString &configuredPath)
{
    const QString configured = configuredPath.trimmed();
    return configured.isEmpty() ? resolvePdfXChangeExecutablePath() : configured;
}

QString pdfXChangeStatusText(const QString &configuredPath)
{
    const QString resolved = resolvedPdfXChangePath(configuredPath);
    if (resolved.trimmed().isEmpty()) {
        return QStringLiteral("Missing. Configure PDFXEdit.exe before using the PDF proxy.");
    }

    const QFileInfo executable(resolved);
    if (executable.exists() && executable.isFile()) {
        return QStringLiteral("Ready: %1").arg(QDir::toNativeSeparators(executable.filePath()));
    }

    return QStringLiteral("Missing configured executable: %1").arg(QDir::toNativeSeparators(resolved));
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

    pdfProxyPathEdit_ = new QLineEdit(defaultPinloomPdfProxyExecutablePath(), this);
    pdfProxyPathEdit_->setObjectName(QStringLiteral("pdfProxyPathEdit"));
    pdfProxyPathEdit_->setReadOnly(true);

    pdfProxyStatusLabel_ = new QLabel(pdfProxyStatusText(pdfProxyPathEdit_->text()), this);
    pdfProxyStatusLabel_->setObjectName(QStringLiteral("pdfProxyStatusLabel"));
    pdfProxyStatusLabel_->setWordWrap(true);

    pdfXChangeStatusLabel_ = new QLabel(pdfXChangeStatusText(settings.pdfXChangeExecutablePath), this);
    pdfXChangeStatusLabel_->setObjectName(QStringLiteral("pdfXChangeStatusLabel"));
    pdfXChangeStatusLabel_->setWordWrap(true);

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
    form->addRow(tr("PDF-XChange status"), pdfXChangeStatusLabel_);
    form->addRow(tr("PDF proxy"), pdfProxyPathEdit_);
    form->addRow(tr("PDF proxy status"), pdfProxyStatusLabel_);
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
    connect(pdfXChangePathEdit_, &QLineEdit::textChanged, this, [this](const QString &text) {
        pdfXChangeStatusLabel_->setText(pdfXChangeStatusText(text));
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
