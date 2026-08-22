#include "pinloom/widgets/PinloomSettingsDialog.h"

#include "pinloom/clip/ObsidianClipStore.h"
#include "pinloom/core/LibraryRoot.h"
#include "pinloom/core/SumatraPdfCommand.h"
#include "pinloom/widgets/PdfLocatorPreviewRenderer.h"

#include <QCheckBox>
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

QString resolvedSumatraPdfPath(const QString &configuredPath)
{
    const QString configured = configuredPath.trimmed();
    return configured.isEmpty() ? resolveSumatraPdfExecutablePath() : configured;
}

QString sumatraPdfStatusText(const QString &configuredPath)
{
    const QString resolved = resolvedSumatraPdfPath(configuredPath);
    if (resolved.trimmed().isEmpty()) {
        return QStringLiteral("Missing. Configure SumatraPDF.exe before using PDF anchors.");
    }

    const QFileInfo executable(resolved);
    if (executable.exists() && executable.isFile()) {
        const QString previewRenderer = resolvePdfLocatorPreviewRendererPath(executable.filePath());
        return previewRenderer.isEmpty()
            ? QStringLiteral("Reader ready; rectangle preview unavailable because sumatrapdf-tool.exe is missing: %1")
                  .arg(QDir::toNativeSeparators(executable.filePath()))
            : QStringLiteral("Reader and rectangle preview ready: %1")
                  .arg(QDir::toNativeSeparators(executable.filePath()));
    }

    return QStringLiteral("Missing configured executable: %1").arg(QDir::toNativeSeparators(resolved));
}

QString obsidianStatusText(const QString &vaultPath, const QString &archiveDirectory)
{
    const QString configuredVault = vaultPath.trimmed();
    if (configuredVault.isEmpty()) {
        return QStringLiteral("Disabled. Configure an Obsidian Vault to archive Saved Clips as Markdown.");
    }

    const QFileInfo vault(configuredVault);
    if (!vault.exists() || !vault.isDir()) {
        return QStringLiteral("Missing Vault directory: %1")
            .arg(QDir::toNativeSeparators(vault.absoluteFilePath()));
    }

    ObsidianClipStoreConfig config;
    config.vaultPath = configuredVault;
    config.archiveDirectory = archiveDirectory;
    const ObsidianClipStore store(config);
    if (store.archivePath().isEmpty()) {
        return QStringLiteral("Invalid archive directory. It must stay inside the Vault.");
    }

    const bool hasObsidianConfig = QDir(vault.absoluteFilePath()).exists(QStringLiteral(".obsidian"));
    return hasObsidianConfig
        ? QStringLiteral("Ready: %1").arg(QDir::toNativeSeparators(store.archivePath()))
        : QStringLiteral("Directory is accessible but does not contain .obsidian: %1")
              .arg(QDir::toNativeSeparators(vault.absoluteFilePath()));
}

QString defaultLibraryRootStatusText(const QString &path)
{
    const QString normalized = normalizedLibraryRootPath(path);
    if (normalized.isEmpty()) {
        return QStringLiteral("Not configured. Register ordinary roots from Root Library as needed.");
    }

    const QFileInfo directory(normalized);
    if (!directory.exists() || !directory.isDir()) {
        return QStringLiteral("Directory not found: %1")
            .arg(QDir::toNativeSeparators(normalized));
    }

    return QStringLiteral("Ready: %1. _PinloomData is excluded from browsing.")
        .arg(QDir::toNativeSeparators(normalized));
}

QString clipPrivacyStatusText(bool automaticCapture,
                              bool sensitiveFilter,
                              const QStringList &excludedApps,
                              const QStringList &customMarkers)
{
    return QStringLiteral("Automatic clipboard history: %1; sensitive-pattern filter: %2; excluded apps: %3; custom markers: %4")
        .arg(automaticCapture ? QStringLiteral("on") : QStringLiteral("off"),
             sensitiveFilter ? QStringLiteral("on") : QStringLiteral("off"),
             QString::number(cleanedValues(excludedApps).size()),
             QString::number(cleanedValues(customMarkers).size()));
}

} // namespace

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

PinloomSettingsDialog::PinloomSettingsDialog(const PinloomAppSettings &settings, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Pinloom Settings"));

    auto *form = new QFormLayout(this);

    auto *pdfPathRow = new QWidget(this);
    auto *pdfPathLayout = new QHBoxLayout(pdfPathRow);
    pdfPathLayout->setContentsMargins(0, 0, 0, 0);
    sumatraPdfPathEdit_ = new QLineEdit(settings.sumatraPdfExecutablePath, pdfPathRow);
    sumatraPdfPathEdit_->setObjectName(QStringLiteral("sumatraPdfPathEdit"));
    auto *browsePdfButton = new QPushButton(tr("Browse"), pdfPathRow);
    browsePdfButton->setObjectName(QStringLiteral("browseSumatraPdfButton"));
    pdfPathLayout->addWidget(sumatraPdfPathEdit_, 1);
    pdfPathLayout->addWidget(browsePdfButton);

    sumatraPdfStatusLabel_ = new QLabel(sumatraPdfStatusText(settings.sumatraPdfExecutablePath), this);
    sumatraPdfStatusLabel_->setObjectName(QStringLiteral("sumatraPdfStatusLabel"));
    sumatraPdfStatusLabel_->setWordWrap(true);

    auto *obsidianVaultRow = new QWidget(this);
    auto *obsidianVaultLayout = new QHBoxLayout(obsidianVaultRow);
    obsidianVaultLayout->setContentsMargins(0, 0, 0, 0);
    obsidianVaultPathEdit_ = new QLineEdit(settings.obsidianVaultPath, obsidianVaultRow);
    obsidianVaultPathEdit_->setObjectName(QStringLiteral("obsidianVaultPathEdit"));
    auto *browseObsidianButton = new QPushButton(tr("Browse"), obsidianVaultRow);
    browseObsidianButton->setObjectName(QStringLiteral("browseObsidianVaultButton"));
    obsidianVaultLayout->addWidget(obsidianVaultPathEdit_, 1);
    obsidianVaultLayout->addWidget(browseObsidianButton);

    obsidianArchiveDirectoryEdit_ = new QLineEdit(settings.obsidianArchiveDirectory, this);
    obsidianArchiveDirectoryEdit_->setObjectName(QStringLiteral("obsidianArchiveDirectoryEdit"));

    obsidianStatusLabel_ = new QLabel(obsidianStatusText(settings.obsidianVaultPath,
                                                         settings.obsidianArchiveDirectory),
                                      this);
    obsidianStatusLabel_->setObjectName(QStringLiteral("obsidianStatusLabel"));
    obsidianStatusLabel_->setWordWrap(true);

    auto *dataDirectoryRow = new QWidget(this);
    auto *dataDirectoryLayout = new QHBoxLayout(dataDirectoryRow);
    dataDirectoryLayout->setContentsMargins(0, 0, 0, 0);
    dataDirectoryEdit_ = new QLineEdit(settings.dataDirectory, dataDirectoryRow);
    dataDirectoryEdit_->setObjectName(QStringLiteral("dataDirectoryEdit"));
    auto *browseDataDirectoryButton = new QPushButton(tr("Browse"), dataDirectoryRow);
    browseDataDirectoryButton->setObjectName(QStringLiteral("browseDataDirectoryButton"));
    dataDirectoryLayout->addWidget(dataDirectoryEdit_, 1);
    dataDirectoryLayout->addWidget(browseDataDirectoryButton);
    dataDirectoryStatusLabel_ = new QLabel(
        tr("An empty directory receives a copy of the current data on the next Pinloom start. "
           "An existing Pinloom data directory is adopted without overwriting it. The old copy is retained. "
           "This is a local database directory. Do not let multiple computers open a live-synchronized copy."),
        this);
    dataDirectoryStatusLabel_->setObjectName(QStringLiteral("dataDirectoryStatusLabel"));
    dataDirectoryStatusLabel_->setWordWrap(true);

    auto *defaultLibraryRootRow = new QWidget(this);
    auto *defaultLibraryRootLayout = new QHBoxLayout(defaultLibraryRootRow);
    defaultLibraryRootLayout->setContentsMargins(0, 0, 0, 0);
    defaultLibraryRootPathEdit_ =
        new QLineEdit(settings.defaultLibraryRootPath, defaultLibraryRootRow);
    defaultLibraryRootPathEdit_->setObjectName(QStringLiteral("defaultLibraryRootPathEdit"));
    defaultLibraryRootPathEdit_->setClearButtonEnabled(true);
    defaultLibraryRootPathEdit_->setPlaceholderText(tr("No default root"));
    auto *browseDefaultLibraryRootButton =
        new QPushButton(tr("Browse"), defaultLibraryRootRow);
    browseDefaultLibraryRootButton->setObjectName(
        QStringLiteral("browseDefaultLibraryRootButton"));
    defaultLibraryRootLayout->addWidget(defaultLibraryRootPathEdit_, 1);
    defaultLibraryRootLayout->addWidget(browseDefaultLibraryRootButton);
    defaultLibraryRootStatusLabel_ =
        new QLabel(defaultLibraryRootStatusText(settings.defaultLibraryRootPath), this);
    defaultLibraryRootStatusLabel_->setObjectName(
        QStringLiteral("defaultLibraryRootStatusLabel"));
    defaultLibraryRootStatusLabel_->setWordWrap(true);

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

    clipAutomaticCaptureCheck_ = new QCheckBox(
        tr("Automatically keep temporary clipboard history"), this);
    clipAutomaticCaptureCheck_->setObjectName(QStringLiteral("clipAutomaticCaptureCheck"));
    clipAutomaticCaptureCheck_->setChecked(settings.clipAutomaticCaptureEnabled);

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
    clipPrivacyStatusLabel_ = new QLabel(
        clipPrivacyStatusText(settings.clipAutomaticCaptureEnabled,
                              settings.clipExcludeSensitiveText,
                              settings.clipExcludedSourceApps,
                              settings.clipSensitiveTextMarkers),
        this);
    clipPrivacyStatusLabel_->setObjectName(QStringLiteral("clipPrivacyStatusLabel"));
    clipPrivacyStatusLabel_->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("settingsButtons"));

    form->addRow(tr("SumatraPDF"), pdfPathRow);
    form->addRow(tr("SumatraPDF status"), sumatraPdfStatusLabel_);
    form->addRow(tr("Obsidian Vault"), obsidianVaultRow);
    form->addRow(tr("Obsidian archive directory"), obsidianArchiveDirectoryEdit_);
    form->addRow(tr("Obsidian status"), obsidianStatusLabel_);
    form->addRow(tr("Data directory"), dataDirectoryRow);
    form->addRow(tr("Data migration"), dataDirectoryStatusLabel_);
    form->addRow(tr("Default root directory"), defaultLibraryRootRow);
    form->addRow(tr("Default root status"), defaultLibraryRootStatusLabel_);
    form->addRow(tr("Clip history limit"), clipMaxTemporaryClipsSpin_);
    form->addRow(tr("Clip size limit (bytes)"), clipMaxTextBytesSpin_);
    form->addRow(tr("Clip history TTL (seconds)"), clipTemporaryTtlSecondsSpin_);
    form->addRow(QString(), clipAutomaticCaptureCheck_);
    form->addRow(QString(), clipExcludeSensitiveTextCheck_);
    form->addRow(QString(), clipRestoreOriginalClipboardCheck_);
    form->addRow(tr("Clip app blacklist"), clipExcludedSourceAppsEdit_);
    form->addRow(tr("Sensitive markers"), clipSensitiveTextMarkersEdit_);
    form->addRow(tr("Clip privacy"), clipPrivacyStatusLabel_);
    form->addWidget(buttons);

    connect(browsePdfButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this,
                                                          tr("SumatraPDF Executable"),
                                                          sumatraPdfPathEdit_->text());
        if (!path.trimmed().isEmpty()) {
            sumatraPdfPathEdit_->setText(path.trimmed());
        }
    });
    connect(sumatraPdfPathEdit_, &QLineEdit::textChanged, this, [this](const QString &text) {
        sumatraPdfStatusLabel_->setText(sumatraPdfStatusText(text));
    });
    connect(browseObsidianButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(this,
                                                               tr("Obsidian Vault"),
                                                               obsidianVaultPathEdit_->text());
        if (!path.trimmed().isEmpty()) {
            obsidianVaultPathEdit_->setText(path.trimmed());
        }
    });
    const auto refreshObsidianStatus = [this]() {
        obsidianStatusLabel_->setText(obsidianStatusText(obsidianVaultPathEdit_->text(),
                                                         obsidianArchiveDirectoryEdit_->text()));
    };
    connect(obsidianVaultPathEdit_, &QLineEdit::textChanged, this, refreshObsidianStatus);
    connect(obsidianArchiveDirectoryEdit_, &QLineEdit::textChanged, this, refreshObsidianStatus);
    connect(browseDataDirectoryButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(this,
                                                               tr("Pinloom Data Directory"),
                                                               dataDirectoryEdit_->text());
        if (!path.trimmed().isEmpty()) {
            dataDirectoryEdit_->setText(path.trimmed());
        }
    });
    connect(browseDefaultLibraryRootButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(
            this,
            tr("Pinloom Default Root Directory"),
            defaultLibraryRootPathEdit_->text());
        if (!path.trimmed().isEmpty()) {
            defaultLibraryRootPathEdit_->setText(path.trimmed());
        }
    });
    connect(defaultLibraryRootPathEdit_, &QLineEdit::textChanged, this,
            [this](const QString &text) {
                defaultLibraryRootStatusLabel_->setText(defaultLibraryRootStatusText(text));
            });
    const auto refreshClipPrivacyStatus = [this]() {
        clipPrivacyStatusLabel_->setText(
            clipPrivacyStatusText(clipAutomaticCaptureCheck_->isChecked(),
                                  clipExcludeSensitiveTextCheck_->isChecked(),
                                  commaSeparatedValues(clipExcludedSourceAppsEdit_->text()),
                                  commaSeparatedValues(clipSensitiveTextMarkersEdit_->text())));
    };
    connect(clipAutomaticCaptureCheck_, &QCheckBox::toggled, this, refreshClipPrivacyStatus);
    connect(clipExcludeSensitiveTextCheck_, &QCheckBox::toggled, this, refreshClipPrivacyStatus);
    connect(clipExcludedSourceAppsEdit_, &QLineEdit::textChanged, this, refreshClipPrivacyStatus);
    connect(clipSensitiveTextMarkersEdit_, &QLineEdit::textChanged, this, refreshClipPrivacyStatus);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

PinloomAppSettings PinloomSettingsDialog::settings() const
{
    PinloomAppSettings settings;
    settings.sumatraPdfExecutablePath = sumatraPdfPathEdit_->text().trimmed();
    settings.obsidianVaultPath = obsidianVaultPathEdit_->text().trimmed();
    settings.obsidianArchiveDirectory = obsidianArchiveDirectoryEdit_->text().trimmed();
    settings.dataDirectory = dataDirectoryEdit_->text().trimmed();
    settings.defaultLibraryRootPath =
        normalizedLibraryRootPath(defaultLibraryRootPathEdit_->text());
    settings.clipMaxTemporaryClips = clipMaxTemporaryClipsSpin_->value();
    settings.clipMaxTextBytes = clipMaxTextBytesSpin_->value();
    settings.clipTemporaryTtlSeconds = clipTemporaryTtlSecondsSpin_->value();
    settings.clipAutomaticCaptureEnabled = clipAutomaticCaptureCheck_->isChecked();
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
