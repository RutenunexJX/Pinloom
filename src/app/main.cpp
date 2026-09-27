#include "pinloom/widgets/PinloomUiControls.h"
#include "pinloom/core/SqliteLibraryRepository.h"
#include "pinloom/core/AppDataDirectory.h"
#include "pinloom/core/AnchorCapture.h"
#include "pinloom/core/AnchorCaptureDraft.h"
#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/AnchorLibraryArchive.h"
#include "pinloom/core/AnchorLibraryPolicy.h"
#include "pinloom/core/ApplicationDataBackup.h"
#include "pinloom/clip/ClipboardCaptureService.h"
#include "pinloom/clip/ClipAction.h"
#include "pinloom/clip/HyperHotkeyService.h"
#include "pinloom/clip/ObsidianClipStore.h"
#include "pinloom/clip/PersistentClipService.h"
#include "pinloom/clip/ClipTrayController.h"
#include "pinloom/core/ExplorerFileSelection.h"
#include "pinloom/core/InboxFileCapture.h"
#include "pinloom/core/LibraryRoot.h"
#include "pinloom/core/NativeAnchorCapture.h"
#include "pinloom/core/SumatraPdfCommand.h"
#include "pinloom/core/SumatraPdfDdeClient.h"
#include "pinloom/core/TextSelectionCapture.h"
#include "pinloom/core/Version.h"
#include "pinloom/widgets/ClipResidentHost.h"
#include "pinloom/widgets/ClipResidentRuntime.h"
#include "pinloom/widgets/ClipCaptureDialog.h"
#include "pinloom/widgets/ClipLibraryWindow.h"
#include "pinloom/widgets/ClipQuickPicker.h"
#include "pinloom/widgets/AnchorCaptureDialog.h"
#include "pinloom/widgets/AnchorLibraryWindow.h"
#include "pinloom/widgets/LibraryRootWindow.h"
#include "pinloom/widgets/MainPanelHotkey.h"
#include "pinloom/widgets/PinloomCommandPanel.h"
#include "pinloom/widgets/CommandFloatingController.h"
#include "pinloom/widgets/PinloomMainWindow.h"
#include "pinloom/widgets/PinloomEntrySearchService.h"
#include "pinloom/widgets/PinloomHostBridge.h"
#ifdef PINLOOM_HAS_SUITEAPP
#include "pinloom/widgets/PinloomSuiteIntegration.h"
#endif
#include "pinloom/widgets/PinloomOpenService.h"
#include "pinloom/widgets/PinloomSettingsDialog.h"
#include "pinloom/widgets/PinloomSingleInstance.h"
#include "pinloom/widgets/PinloomVisualTheme.h"
#include "pinloom/widgets/PdfLocatorPreviewRenderer.h"
#include "pinloom/widgets/SumatraPdfViewerAdapter.h"

#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QScreen>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyleHints>
#include <QTimer>
#include <QToolTip>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Pinloom"));
    QApplication::setOrganizationName(QStringLiteral("Pinloom"));
    QApplication::setApplicationVersion(Pinloom::pinloomVersion());
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/pinloom/icon.png")));
    app.setQuitOnLastWindowClosed(false);
    Pinloom::applySystemPinloomVisualTheme(app);
    QObject::connect(QGuiApplication::styleHints(),
                     &QStyleHints::colorSchemeChanged,
                     &app,
                     [&app](Qt::ColorScheme) {
                         Pinloom::applySystemPinloomVisualTheme(app);
                     });

    const QStringList startupArguments = QCoreApplication::arguments();
    if (startupArguments.contains(QStringLiteral("--package-check"))) {
        // Must precede settings, single-instance IPC, user data and native hooks.
        bool databaseReady = false;
        const QString connectionName = QStringLiteral("package-check");
        {
            QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            probe.setDatabaseName(QStringLiteral(":memory:"));
            databaseReady = probe.open();
            if (databaseReady) {
                QSqlQuery query(probe);
                databaseReady = query.exec(QStringLiteral("SELECT sqlite_version()")) && query.next();
            }
            if (!databaseReady) qCritical().noquote() << probe.lastError().text();
        }
        QSqlDatabase::removeDatabase(connectionName);
        if (!databaseReady) return 2;
        Pinloom::Ui::Dialog probe;
        auto *layout = new Pinloom::Ui::FormLayout(&probe);
        layout->addRow(QStringLiteral("Version"), Pinloom::Ui::lineEdit(Pinloom::pinloomVersion(), &probe));
        probe.resize(320, 160);
        probe.ensurePolished();
        if (probe.grab().isNull()) return 3;
        const auto pdfProbe = Pinloom::requestSumatraPdfDdeCommand(QStringLiteral("ProbeHealth()"), 500);
        if (!pdfProbe.success() || pdfProbe.text != QLatin1String("ready")) {
            qCritical().noquote() << QStringLiteral("PDF helper check failed:") << pdfProbe.error;
            return 4;
        }
        qInfo().noquote() << QStringLiteral("Pinloom %1 package check passed (%2)")
            .arg(Pinloom::pinloomVersion(), Pinloom::Ui::usesEla() ? QStringLiteral("ELA") : QStringLiteral("CLASSIC"));
        return 0;
    }
    const bool startHidden = startupArguments.contains(QStringLiteral("--hidden"), Qt::CaseInsensitive);
    QString startupDeepLink;
    for (const QString &argument : startupArguments.mid(1)) {
        const QUrl candidate(argument, QUrl::StrictMode);
        if (candidate.scheme().compare(QStringLiteral("pinloom"),
                                       Qt::CaseInsensitive) == 0) {
            startupDeepLink = argument;
            break;
        }
    }

    Pinloom::PinloomSingleInstanceOptions instanceOptions;
    instanceOptions.serverName = Pinloom::defaultPinloomSingleInstanceServerName();
    instanceOptions.activationTimeoutMs = 300;
    instanceOptions.activationMessage = !startupDeepLink.isEmpty()
        ? startupDeepLink
        : (startHidden ? QStringLiteral("resident")
                       : QStringLiteral("activate"));
    Pinloom::PinloomSingleInstanceGuard instanceGuard(instanceOptions, &app);
    const Pinloom::PinloomSingleInstanceStartResult instanceStart = instanceGuard.start();
    if (instanceStart.isSecondary()) {
        return 0;
    }
    if (!instanceStart.succeeded()) {
        Pinloom::Ui::warning(nullptr,
                             QStringLiteral("Pinloom"),
                             QStringLiteral("Pinloom could not start its single-instance listener:\n%1")
                                 .arg(instanceStart.error));
    }

    QString defaultAppDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (defaultAppDataPath.isEmpty()) {
        defaultAppDataPath = QDir::home().filePath(QStringLiteral(".pinloom"));
    }
    QSettings appSettingsStore;
    const Pinloom::AppDataDirectoryResult dataDirectoryResult =
        Pinloom::prepareAppDataDirectory(appSettingsStore, defaultAppDataPath);
    const QString appDataPath = dataDirectoryResult.directory;
    if (appDataPath.trimmed().isEmpty()) {
        Pinloom::Ui::critical(nullptr,
                              QStringLiteral("Pinloom"),
                              dataDirectoryResult.error.trimmed().isEmpty()
                                  ? QStringLiteral("Unable to resolve the app data directory.")
                                  : dataDirectoryResult.error);
        return 1;
    }
    if (!QDir().mkpath(appDataPath)) {
        Pinloom::Ui::critical(nullptr, QStringLiteral("Pinloom"), QStringLiteral("Unable to create app data directory."));
        return 1;
    }
    if (!dataDirectoryResult.error.trimmed().isEmpty()) {
        Pinloom::Ui::warning(nullptr,
                             QStringLiteral("Pinloom Data Directory"),
                             dataDirectoryResult.error
                                 + QStringLiteral("\n\nPinloom will continue with the current directory:\n")
                                 + QDir::toNativeSeparators(appDataPath));
    }
    const bool hasAutomaticClipboardCaptureDecision =
        appSettingsStore.contains(QStringLiteral("clip/automaticCaptureEnabled"));
    const bool hasDefaultLibraryRootSetting =
        appSettingsStore.contains(QStringLiteral("library/defaultRootPath"));
    Pinloom::PinloomAppSettings runtimeSettings =
        Pinloom::loadPinloomAppSettings(appSettingsStore, appDataPath);
    if (!hasAutomaticClipboardCaptureDecision) {
        if (!startHidden) {
            const QMessageBox::StandardButton choice = Pinloom::Ui::question(
                nullptr,
                QStringLiteral("Pinloom Clipboard History"),
                QStringLiteral("Pinloom can automatically store copied text as temporary local history. "
                               "This may include sensitive clipboard content.\n\n"
                               "Enable automatic clipboard history? F24+S manual saving remains available when disabled."),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            runtimeSettings.clipAutomaticCaptureEnabled = choice == QMessageBox::Yes;
        }
        Pinloom::savePinloomAppSettings(appSettingsStore, runtimeSettings);
        appSettingsStore.sync();
    }

    Pinloom::SqliteLibraryRepository repository;
    const QString databasePath = QDir(appDataPath).filePath(QStringLiteral("pinloom.sqlite3"));
    const QString identityRegistryPath =
        QDir(appDataPath).filePath(QStringLiteral("pinloom_identity.sqlite3"));
    if (!repository.open(databasePath, identityRegistryPath)) {
        Pinloom::Ui::critical(nullptr,
                              QStringLiteral("Pinloom"),
                              QStringLiteral("Unable to open Pinloom database:\n%1").arg(repository.lastError()));
        return 1;
    }
    if (!repository.integrityCheck()) {
        Pinloom::Ui::critical(nullptr,
                              QStringLiteral("Pinloom Data Integrity"),
                              QStringLiteral("The Anchor Library database failed its integrity check:\n%1\n\n"
                                             "Pinloom will not modify this database.")
                                  .arg(repository.lastError()));
        return 1;
    }
    if (!repository.initialize() || !repository.integrityCheck()) {
        Pinloom::Ui::critical(nullptr,
                              QStringLiteral("Pinloom"),
                              QStringLiteral("Unable to initialize Pinloom database:\n%1").arg(repository.lastError()));
        return 1;
    }
    if (!hasDefaultLibraryRootSetting) {
        for (const Pinloom::LibraryRoot &root : repository.libraryRoots()) {
            if (root.syncRoot) {
                runtimeSettings.defaultLibraryRootPath = root.path;
                break;
            }
        }
    }
    const QString startupDefaultRoot =
        Pinloom::normalizedLibraryRootPath(runtimeSettings.defaultLibraryRootPath);
    if (startupDefaultRoot.isEmpty() || QFileInfo(startupDefaultRoot).isDir()) {
        QString defaultRootError;
        if (!Pinloom::configureDefaultLibraryRoot(repository,
                                                  startupDefaultRoot,
                                                  &defaultRootError)) {
            Pinloom::Ui::warning(nullptr,
                                 QStringLiteral("Pinloom Default Root"),
                                 defaultRootError);
        }
    }
    if (!hasDefaultLibraryRootSetting) {
        Pinloom::savePinloomAppSettings(appSettingsStore, runtimeSettings);
        appSettingsStore.sync();
    }
    const QString anchorLibraryBackupDirectory =
        QDir(appDataPath).filePath(QStringLiteral("backups/anchor-library"));
    Pinloom::AnchorLibraryArchiveService anchorLibraryArchive(repository,
                                                               anchorLibraryBackupDirectory);

    Pinloom::ClipResidentRuntimeFactory clipFactory;
    Pinloom::ClipResidentRuntimeFactoryOptions clipOptions;
    clipOptions.repositoryKind = Pinloom::ClipResidentRepositoryKind::SQLite;
    clipOptions.sqliteDatabasePath = QDir(appDataPath).filePath(QStringLiteral("pinloom_clip.sqlite3"));
    clipOptions.sqliteIdentityRegistryPath = identityRegistryPath;
    clipOptions.runtimeOptions.insertionOptions.restoreOriginalClipboardOnSuccess =
        runtimeSettings.clipRestoreOriginalClipboardOnInsert;

    Pinloom::ClipResidentHostResult clipHostResult = clipFactory.createDefaultPlatformHost(clipOptions);
    std::unique_ptr<Pinloom::ClipResidentHost> clipHost;
    if (clipHostResult.succeeded()) {
        clipHost = std::move(clipHostResult.host);
        QObject::connect(clipHost.get(), &Pinloom::ClipResidentHost::quitRequested, &app, &QApplication::quit);
        if (clipHost->runtime()) {
            clipHost->runtime()->captureService().setPolicy(runtimeSettings.clipCapturePolicy());
            clipHost->runtime()->trayController().setCapturePaused(
                !runtimeSettings.clipAutomaticCaptureEnabled);
            clipHost->runtime()->captureService().setSourceAppProvider([]() {
                return Pinloom::currentForegroundAppWindowContext().processName;
            });
        }
        if (clipHost->sqliteRepository()) {
            if (!clipHost->sqliteRepository()->integrityCheck()) {
                Pinloom::Ui::critical(
                    nullptr,
                    QStringLiteral("Pinloom Data Integrity"),
                    QStringLiteral("The Clip Library database failed its integrity check:\n%1\n\n"
                                   "Pinloom will not modify this database.")
                        .arg(clipHost->sqliteRepository()->lastError()));
                return 1;
            }
        }
    } else {
        Pinloom::Ui::warning(nullptr,
                             QStringLiteral("Pinloom Clip"),
                             QStringLiteral("Pinloom Clip could not initialize:\n%1").arg(clipHostResult.error));
    }

    QString startupIdentityWarning;
    const QList<Pinloom::GlobalIdentityConflict> startupIdentityConflicts =
        clipHost && clipHost->sqliteRepository()
        ? clipHost->sqliteRepository()->identityConflicts()
        : repository.identityConflicts();
    if (!startupIdentityConflicts.isEmpty()) {
        QStringList conflictLines;
        const qsizetype visibleConflictCount = std::min<qsizetype>(
            20, startupIdentityConflicts.size());
        for (int index = 0; index < visibleConflictCount; ++index) {
            conflictLines.append(startupIdentityConflicts.at(index).message());
        }
        if (startupIdentityConflicts.size() > visibleConflictCount) {
            conflictLines.append(QStringLiteral("%1 additional conflict(s) are not shown.")
                                     .arg(startupIdentityConflicts.size() - visibleConflictCount));
        }
        startupIdentityWarning = QStringLiteral(
            "Pinloom found historical File, Clip, or Anchor name/alias conflicts. "
            "Existing data remains readable, but conflicting identities cannot be saved or restored "
            "until each listed object is renamed.\n\n%1")
                                     .arg(conflictLines.join(QStringLiteral("\n\n")));
    }

    QList<Pinloom::ApplicationDataBackupItem> backupItems;
    backupItems.append({QStringLiteral("pinloom.sqlite3"),
                        [&repository](const QString &destination) {
                            return repository.backupDatabase(destination);
                        },
                        [&repository]() { return repository.lastError(); }});
    if (clipHost && clipHost->sqliteRepository()) {
        Pinloom::SqliteClipRepository *clipRepository = clipHost->sqliteRepository();
        backupItems.append({QStringLiteral("pinloom_clip.sqlite3"),
                            [clipRepository](const QString &destination) {
                                return clipRepository->backupDatabase(destination);
                            },
                            [clipRepository]() { return clipRepository->lastError(); }});
    }
    const Pinloom::ApplicationDataBackupResult startupBackup =
        Pinloom::createAutomaticApplicationDataBackup(
            QDir(appDataPath).filePath(QStringLiteral("backups/application-data")),
            backupItems,
            10);
    QString startupDataWarning;
    if (!startupBackup.success) {
        startupDataWarning = startupBackup.error;
        qWarning().noquote() << "Application data startup backup failed:" << startupBackup.error;
    }

    Pinloom::PinloomMainWindow window;
    window.setWindowTitle(QStringLiteral("Pinloom %1").arg(Pinloom::pinloomVersionLabel()));
    window.setMinimumWidth(560);
    window.resize(760, 72);
    window.setLauncherMode(true);
    if (!startupDataWarning.isEmpty()) {
        window.setRecentError(QStringLiteral("Application data backup failed"),
                              startupDataWarning);
    }
    if (!startupIdentityWarning.isEmpty()) {
        window.setRecentError(QStringLiteral("Global name/alias conflicts"),
                              startupIdentityWarning);
        if (!startHidden) {
            Pinloom::Ui::warning(&window,
                                 QStringLiteral("Pinloom Name/Alias Conflicts"),
                                 startupIdentityWarning);
        }
    }

    const auto obsidianConfigForSettings = [](const Pinloom::PinloomAppSettings &settings) {
        Pinloom::ObsidianClipStoreConfig config;
        config.vaultPath = settings.obsidianVaultPath;
        config.archiveDirectory = settings.obsidianArchiveDirectory;
        return config;
    };
    Pinloom::ObsidianClipStore obsidianClipStore(obsidianConfigForSettings(runtimeSettings));
    std::unique_ptr<Pinloom::ObsidianClipSyncService> obsidianSyncService;
    if (clipHost && clipHost->sqliteRepository()) {
        obsidianSyncService = std::make_unique<Pinloom::ObsidianClipSyncService>(
            *clipHost->sqliteRepository(),
            obsidianClipStore.config(),
            &app);
    } else if (clipHost && clipHost->inMemoryRepository()) {
        obsidianSyncService = std::make_unique<Pinloom::ObsidianClipSyncService>(
            *clipHost->inMemoryRepository(),
            obsidianClipStore.config(),
            &app);
    }
    if (obsidianSyncService) {
        QObject::connect(obsidianSyncService.get(),
                         &Pinloom::ObsidianClipSyncService::errorChanged,
                         &window,
                         [&window](const QString &error) {
                             if (!error.trimmed().isEmpty()) {
                                 window.setRecentError(QStringLiteral("Obsidian Clip synchronization failed"), error);
                             }
                         });
        if (obsidianClipStore.config().isEnabled() && !obsidianSyncService->start()) {
            window.setRecentError(QStringLiteral("Obsidian Clip synchronization failed"),
                                  obsidianSyncService->lastError());
        }
    }

    std::unique_ptr<Pinloom::PersistentClipService> persistentClipService;
    if (clipHost && clipHost->sqliteRepository()) {
        persistentClipService = std::make_unique<Pinloom::PersistentClipService>(
            *clipHost->sqliteRepository(), obsidianClipStore);
    } else if (clipHost && clipHost->inMemoryRepository()) {
        persistentClipService = std::make_unique<Pinloom::PersistentClipService>(
            *clipHost->inMemoryRepository(), obsidianClipStore);
    }

    const auto findPersistentClip = [&persistentClipService](const QString &clipId) -> std::optional<Pinloom::Clip> {
        return persistentClipService
            ? persistentClipService->findClip(clipId)
            : std::nullopt;
    };
    const auto persistSavedClip = [&persistentClipService](Pinloom::Clip clip, QString *error) {
        if (!persistentClipService) {
            if (error) *error = QStringLiteral("Pinloom Clip is not running");
            return false;
        }
        return persistentClipService->saveClip(std::move(clip), error);
    };
    const auto setPersistentClipState = [&persistentClipService](const QString &clipId,
                                                                 Pinloom::ClipState state,
                                                                 QString *error) {
        if (!persistentClipService) {
            if (error) *error = QStringLiteral("Pinloom Clip is not running");
            return false;
        }
        return persistentClipService->changeState(clipId, state, error);
    };
    const auto permanentlyRemovePersistentClip = [&persistentClipService](const QString &clipId,
                                                                          QString *error) {
        if (!persistentClipService) {
            if (error) *error = QStringLiteral("Pinloom Clip is not running");
            return false;
        }
        return persistentClipService->permanentlyRemove(clipId, error);
    };
    const auto automaticClipName = [](const QString &text) {
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            const QString name = line.simplified();
            if (!name.isEmpty()) {
                constexpr qsizetype MaxNameLength = 60;
                return name.size() <= MaxNameLength
                    ? name
                    : name.left(MaxNameLength - 3) + QStringLiteral("...");
            }
        }
        return QStringLiteral("Saved text");
    };
    const auto availableClipTags = [&clipHost]() {
        QStringList tags;
        const QList<Pinloom::Clip> clips = clipHost && clipHost->sqliteRepository()
            ? clipHost->sqliteRepository()->clips()
            : (clipHost && clipHost->inMemoryRepository()
                   ? clipHost->inMemoryRepository()->clips()
                   : QList<Pinloom::Clip>{});
        for (const Pinloom::Clip &clip : clips) {
            if (clip.state == Pinloom::ClipState::Deleted) {
                continue;
            }
            for (const QString &value : clip.tags) {
                const QString tag = value.trimmed();
                if (!tag.isEmpty() && !tags.contains(tag, Qt::CaseInsensitive)) {
                    tags.append(tag);
                }
            }
        }
        tags.sort(Qt::CaseInsensitive);
        return tags;
    };
    const auto archiveSelectedText = [&clipHost,
                                      &runtimeSettings,
                                      &persistSavedClip,
                                      &automaticClipName](const Pinloom::TextSelectionCaptureResult &selection,
                                                          const Pinloom::ClipCaptureMetadata &metadata,
                                                          QString *status) {
        if (!clipHost || !clipHost->runtime()) {
            if (status) {
                *status = QStringLiteral("Pinloom Clip is not running");
            }
            return false;
        }
        if (!selection.hasSelectedText()) {
            if (status) {
                *status = QStringLiteral("No selected text to archive");
            }
            return false;
        }

        Pinloom::Clip clip;
        Pinloom::ClipCaptureResult captured;
        if (clipHost->sqliteRepository()) {
            captured = clipHost->sqliteRepository()->captureText(selection.text,
                                                                 runtimeSettings.clipCapturePolicy(),
                                                                 selection.context.processName);
        } else {
            captured = clipHost->inMemoryRepository()->captureText(selection.text,
                                                                   runtimeSettings.clipCapturePolicy(),
                                                                   selection.context.processName);
        }
        if (captured.captured()) {
            clip = captured.clip.value();
        } else if (captured.status == Pinloom::ClipCaptureStatus::IgnoredDuplicate) {
            const QDateTime now = QDateTime::currentDateTimeUtc();
            clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            clip.kind = Pinloom::ClipKind::Text;
            clip.state = Pinloom::ClipState::Saved;
            clip.text = selection.text;
            clip.createdAt = now;
            clip.updatedAt = now;
            clip.usedAt = now;
        } else {
            if (status) {
                switch (captured.status) {
                case Pinloom::ClipCaptureStatus::IgnoredTooLarge:
                    *status = QStringLiteral("Selected text exceeds the configured Clip size limit");
                    break;
                case Pinloom::ClipCaptureStatus::IgnoredSensitiveContent:
                    *status = QStringLiteral("Selected text matched the sensitive-content filter");
                    break;
                case Pinloom::ClipCaptureStatus::IgnoredExcludedSource:
                    *status = QStringLiteral("The foreground app is excluded from Clip capture");
                    break;
                default:
                    *status = QStringLiteral("Unable to create a Clip from the selected text");
                    break;
                }
            }
            return false;
        }

        clip.name = metadata.name.trimmed().isEmpty()
            ? automaticClipName(selection.text)
            : metadata.name;
        clip.tags = metadata.tags;
        clip.sourceApp = selection.context.processName;
        clip.sourceWindowTitle = selection.context.windowTitle;
        QString error;
        if (!persistSavedClip(clip, &error)) {
            if (status) {
                *status = error.trimmed().isEmpty()
                    ? QStringLiteral("Unable to archive selected text")
                    : error.trimmed();
            }
            return false;
        }
        if (status) {
            *status = QStringLiteral("Archived: %1").arg(clip.name);
        }
        return true;
    };
    std::optional<Pinloom::ForegroundTextTarget> pendingClipInsertionTarget;
    QWidget *activeClipInsertionWindow = nullptr;

    Pinloom::ApplicationLaunchSettings applicationLaunchSettings;
    applicationLaunchSettings.sumatraPdfExecutablePath =
        runtimeSettings.sumatraPdfExecutablePath.trimmed();
    const auto sumatraPdfExecutablePathProvider = [&runtimeSettings]() {
        const QString configured = runtimeSettings.sumatraPdfExecutablePath.trimmed();
        return configured.isEmpty() ? Pinloom::resolveSumatraPdfExecutablePath() : configured;
    };
    const auto clipSearchHandler =
        [&clipHost](const QString &query, const Pinloom::ClipSearchOptions &options) -> QList<Pinloom::ClipSearchResult> {
        if (!clipHost || !clipHost->runtime()) {
            return {};
        }
        return clipHost->runtime()->searchService().search(query, options);
    };
    const auto clipInsertionHandler = [&clipHost,
                                       &pendingClipInsertionTarget,
                                       &activeClipInsertionWindow](const QString &clipId, QString *error) {
        if (!clipHost || !clipHost->runtime()) {
            if (error) {
                *error = QStringLiteral("Pinloom Clip is not running");
            }
            return false;
        }
        const std::optional<Pinloom::Clip> clip =
            clipHost->runtime()->searchService().findClip(clipId);
        if (!clip.has_value()) {
            if (error) {
                *error = QStringLiteral("Clip not found");
            }
            return false;
        }

        QWidget *const insertionWindow = activeClipInsertionWindow;
        if (Pinloom::actionForClip(clip.value()) == Pinloom::ClipActionType::OpenWebUrl) {
            const std::optional<QUrl> url = Pinloom::webUrlForClip(clip.value(), error);
            if (!url.has_value()) {
                return false;
            }
            const bool insertionWindowWasHidden = insertionWindow && insertionWindow->isVisible();
            if (insertionWindowWasHidden) {
                insertionWindow->hide();
            }
            if (!QDesktopServices::openUrl(url.value())) {
                if (insertionWindowWasHidden) {
                    activeClipInsertionWindow = insertionWindow;
                    insertionWindow->show();
                    insertionWindow->raise();
                    insertionWindow->activateWindow();
                }
                if (error) {
                    *error = QStringLiteral("Unable to open the wb Clip URL in the default browser");
                }
                return false;
            }
            pendingClipInsertionTarget.reset();
            if (clipHost->sqliteRepository()) {
                clipHost->sqliteRepository()->markClipUsed(clipId);
            } else if (clipHost->inMemoryRepository()) {
                clipHost->inMemoryRepository()->markClipUsed(clipId);
            }
            if (error) {
                *error = QStringLiteral("Opened wb Clip in the default browser");
            }
            return true;
        }

        const std::optional<Pinloom::ForegroundTextTarget> insertionTarget = pendingClipInsertionTarget;
        pendingClipInsertionTarget.reset();
        if (!insertionTarget.has_value()) {
            if (error) {
                *error = QStringLiteral("No valid foreground insertion target");
            }
            return false;
        }
        const bool insertionWindowWasHidden = insertionWindow && insertionWindow->isVisible();
        if (insertionWindowWasHidden) {
            insertionWindow->hide();
        }
        QString restoreError;
        if (!Pinloom::restoreForegroundTextTarget(insertionTarget.value(), &restoreError)) {
            if (insertionWindowWasHidden) {
                pendingClipInsertionTarget = insertionTarget;
                activeClipInsertionWindow = insertionWindow;
                insertionWindow->show();
                insertionWindow->raise();
                insertionWindow->activateWindow();
            }
            if (error) {
                *error = restoreError;
            }
            return false;
        }
        const Pinloom::ClipInsertionResult result =
            clipHost->runtime()->insertionService().insertClip(clip.value());
        if (!result.inserted() && error) {
            *error = result.error;
        }
        if (!result.inserted() && insertionWindowWasHidden) {
            pendingClipInsertionTarget = insertionTarget;
            activeClipInsertionWindow = insertionWindow;
            insertionWindow->show();
            insertionWindow->raise();
            insertionWindow->activateWindow();
        }
        return result.inserted();
    };
    auto clipSaveHandler = [&clipHost,
                            &persistSavedClip](const Pinloom::PinloomClipSaveRequest &request, QString *error) {
        if (!clipHost || !clipHost->runtime()) {
            if (error) {
                *error = QStringLiteral("Pinloom Clip is not running");
            }
            return false;
        }
        const std::optional<Pinloom::Clip> stored =
            clipHost->runtime()->searchService().findClip(request.clipId);
        if (!stored.has_value()) {
            if (error) {
                *error = QStringLiteral("Clip not found");
            }
            return false;
        }
        Pinloom::Clip saved = stored.value();
        saved.name = request.name;
        saved.aliases = request.aliases;
        saved.tags = request.tags;
        saved.pinned = request.pinned;
        if (!persistSavedClip(saved, error)) {
            return false;
        }
        if (error) {
            error->clear();
        }
        return true;
    };

    Pinloom::ForegroundAppWindowContext lastForegroundContext;
    bool floatingCaptureActive = false;
    Pinloom::ForegroundTextTarget lastForegroundTextTarget;
    const auto rememberForegroundTarget = [&lastForegroundContext, &lastForegroundTextTarget]() {
        const auto context = Pinloom::currentForegroundAppWindowContext();
        if (context.isValid() && context.processId != QCoreApplication::applicationPid()) {
            lastForegroundContext = context;
            lastForegroundTextTarget = Pinloom::captureForegroundTextTarget();
        }
    };
    QMainWindow *commandWindowForForegroundCapture = nullptr;
    Pinloom::SumatraPdfViewerAdapterOptions pdfViewerOptions;
    pdfViewerOptions.applicationLaunchSettings = applicationLaunchSettings;
    pdfViewerOptions.executablePathProvider = sumatraPdfExecutablePathProvider;
    pdfViewerOptions.fileConfirmationHandler =
        [](const QString &documentTitle, QWidget *parent) {
            return QFileDialog::getOpenFileName(
                parent,
                documentTitle.trimmed().isEmpty()
                    ? QStringLiteral("Confirm PDF File")
                    : QStringLiteral("Confirm PDF File: %1")
                          .arg(documentTitle),
                QString(),
                QStringLiteral("PDF files (*.pdf);;All files (*)"));
        };
    Pinloom::SumatraPdfViewerAdapter pdfViewerAdapter(
        repository, std::move(pdfViewerOptions), &app);
    Pinloom::NativeAnchorCaptureAdapter nativeAnchorCaptureAdapter;
    Pinloom::AnchorCaptureCommitService anchorCaptureCommitService(repository);

    const auto confirmAndCommitAnchorDraft =
        [&window,
         &nativeAnchorCaptureAdapter,
         &anchorCaptureCommitService](Pinloom::AnchorCaptureDraft draft,
                                      QString *status) {
        if (!window.isVisible()) {
            window.show();
        }
        window.raise();
        window.activateWindow();
        QApplication::processEvents();

        Pinloom::AnchorCaptureDialog dialog(draft, &window);
        dialog.setWindowFlag(Qt::WindowStaysOnTopHint, true);
        QTimer::singleShot(0, &dialog, [&dialog]() {
            dialog.show();
            dialog.raise();
            dialog.activateWindow();
        });
        if (dialog.exec() != QDialog::Accepted) {
            if (status) *status = QStringLiteral("Anchor capture canceled");
            return false;
        }

        draft = dialog.draft();
        const Pinloom::NativeAnchorCaptureResult finalized =
            nativeAnchorCaptureAdapter.finalize(draft);
        if (!finalized.success()) {
            if (status) *status = finalized.error;
            return false;
        }
        draft = finalized.draft;

        const Pinloom::AnchorCaptureCommitResult committed =
            anchorCaptureCommitService.commit(draft);
        if (!committed.success()) {
            if (status) *status = committed.error;
            return false;
        }
        if (status) {
            *status = QStringLiteral("Captured %1 Anchor \"%2\"")
                          .arg(committed.anchor.targetApp,
                               committed.anchor.name);
        }
        return true;
    };

    const auto pdfViewerCaptureRequest =
        [&lastForegroundContext,
         &lastForegroundTextTarget,
         &floatingCaptureActive,
         &commandWindowForForegroundCapture]() {
        Pinloom::PdfViewerCaptureRequest request;
        const bool useRememberedTarget = commandWindowForForegroundCapture
            && (commandWindowForForegroundCapture->isVisible() || floatingCaptureActive)
            && lastForegroundContext.isValid();
        if (useRememberedTarget) {
            request.context = lastForegroundContext;
            request.textTarget = lastForegroundTextTarget;
        }
        request.parent = commandWindowForForegroundCapture;
        return request;
    };
    const auto foregroundPdfAnchorCaptureRequestProvider =
        [&pdfViewerAdapter,
         &pdfViewerCaptureRequest,
         &commandWindowForForegroundCapture](QString *status)
        -> std::optional<Pinloom::ManualPdfAnchorCreationRequest> {
        const Pinloom::PdfViewerCaptureRequest captureRequest =
            pdfViewerCaptureRequest();
        const bool restoreCommandWindow = commandWindowForForegroundCapture
            && commandWindowForForegroundCapture->isVisible();
        if (restoreCommandWindow) {
            commandWindowForForegroundCapture->hide();
            QApplication::processEvents();
        }
        const Pinloom::PdfViewerCaptureResult result =
            pdfViewerAdapter.captureRectangle(captureRequest);
        if (restoreCommandWindow) {
            commandWindowForForegroundCapture->show();
            commandWindowForForegroundCapture->raise();
            commandWindowForForegroundCapture->activateWindow();
        }
        if (status) {
            *status = result.message;
            if (!result.diagnostics.trimmed().isEmpty()
                && result.diagnostics.trimmed() != result.message.trimmed()) {
                status->append(QStringLiteral(": ")
                                   + result.diagnostics.trimmed());
            }
        }
        return result.succeeded()
            ? std::optional<Pinloom::ManualPdfAnchorCreationRequest>(
                  result.anchorRequest)
            : std::nullopt;
    };
    Pinloom::PinloomEntrySearchService entrySearchService(repository, clipSearchHandler);
    Pinloom::PinloomOpenServiceOptions openServiceOptions;
    openServiceOptions.clipInsertionHandler = clipInsertionHandler;
    openServiceOptions.applicationLaunchSettings = applicationLaunchSettings;
    openServiceOptions.pdfViewerAdapter = &pdfViewerAdapter;
    openServiceOptions.pdfOriginalFallbackPrompt =
        [&window](const QString &sourceFilePath, int page, const QString &reason) {
        return Pinloom::Ui::confirm(&window, QStringLiteral("Pinloom Preview"),
            reason + QStringLiteral("\n\nThe original PDF was not modified.\n\n%1\nPage %2")
                .arg(QDir::toNativeSeparators(sourceFilePath))
                .arg(page), QStringLiteral("Open original PDF"));
    };
    Pinloom::PinloomOpenService openService(repository, std::move(openServiceOptions), &app);
    QObject::connect(&openService,
                     &Pinloom::PinloomOpenService::statusChanged,
                     &window,
                     [&window](const QString &status) {
                         window.statusBar()->showMessage(status, 6000);
                         Pinloom::Ui::showNotice(&window, status);
                         if (status.contains(QStringLiteral("verification failed"),
                                             Qt::CaseInsensitive)
                             || status.contains(QStringLiteral("Pinloom Preview failed"),
                                                Qt::CaseInsensitive)) {
                             window.setRecentError(
                                 QStringLiteral("SumatraPDF jump verification failed"),
                                 status);
                         }
                     });
    const auto captureForegroundPdfAnchor =
        [&foregroundPdfAnchorCaptureRequestProvider,
         &confirmAndCommitAnchorDraft](QString *status) {
        QString providerStatus;
        const std::optional<Pinloom::ManualPdfAnchorCreationRequest> request =
            foregroundPdfAnchorCaptureRequestProvider(&providerStatus);
        if (!request.has_value()) {
            if (status) {
                *status = providerStatus.trimmed().isEmpty()
                    ? QStringLiteral("Open or focus a SumatraPDF PDF before capturing an anchor")
                    : providerStatus.trimmed();
            }
            return false;
        }
        return confirmAndCommitAnchorDraft(
            Pinloom::anchorCaptureDraftFromPdfRequest(request.value()),
            status);
    };

    const auto captureForegroundPdfTextAnchor =
        [&pdfViewerAdapter,
         &pdfViewerCaptureRequest,
         &confirmAndCommitAnchorDraft](QString *status) {
        const Pinloom::PdfViewerCaptureResult captured =
            pdfViewerAdapter.captureText(pdfViewerCaptureRequest());
        if (!captured.succeeded()) {
            if (status) {
                *status = captured.message;
                if (!captured.diagnostics.trimmed().isEmpty()
                    && captured.diagnostics.trimmed()
                           != captured.message.trimmed()) {
                    status->append(QStringLiteral(": ")
                                       + captured.diagnostics.trimmed());
                }
            }
            return false;
        }
        return confirmAndCommitAnchorDraft(
            Pinloom::anchorCaptureDraftFromPdfRequest(
                captured.anchorRequest),
            status);
    };

    const auto captureForegroundPdfTextClip =
        [&pdfViewerAdapter,
         &pdfViewerCaptureRequest,
         &commandWindowForForegroundCapture,
         &availableClipTags,
         &automaticClipName,
         &archiveSelectedText](QString *status) {
        Pinloom::PdfViewerCaptureRequest request = pdfViewerCaptureRequest();
        request.requirePageGeometry = false;
        const Pinloom::PdfViewerCaptureResult captured =
            pdfViewerAdapter.captureText(request);
        if (!captured.succeeded()) {
            if (status) {
                *status = captured.message;
                if (!captured.diagnostics.trimmed().isEmpty()
                    && captured.diagnostics.trimmed()
                           != captured.message.trimmed()) {
                    status->append(QStringLiteral(": ")
                                       + captured.diagnostics.trimmed());
                }
            }
            return false;
        }

        Pinloom::ClipCaptureDialog dialog(
            captured.textSelection.text,
            automaticClipName(captured.textSelection.text),
            availableClipTags(),
            commandWindowForForegroundCapture);
        if (dialog.exec() != QDialog::Accepted) {
            if (status) {
                *status = QStringLiteral("PDF Text Clip capture canceled");
            }
            return false;
        }
        return archiveSelectedText(captured.textSelection,
                                   dialog.metadata(),
                                   status);
    };

    const auto captureRememberedApplicationAnchor =
        [&lastForegroundContext,
         &commandWindowForForegroundCapture,
         &nativeAnchorCaptureAdapter,
         &pdfViewerAdapter,
         &captureForegroundPdfAnchor,
         &confirmAndCommitAnchorDraft](QString *status) {
        const Pinloom::ForegroundAppWindowContext context =
            commandWindowForForegroundCapture
                && commandWindowForForegroundCapture->isVisible()
                && lastForegroundContext.isValid()
            ? lastForegroundContext
            : Pinloom::currentForegroundAppWindowContext();
        if (pdfViewerAdapter.supportsContext(context)) {
            return captureForegroundPdfAnchor(status);
        }
        const Pinloom::NativeAnchorCaptureResult captured =
            nativeAnchorCaptureAdapter.captureForProcess(context.processName);
        if (!captured.success()) {
            if (status) *status = captured.error;
            return false;
        }
        return confirmAndCommitAnchorDraft(captured.draft, status);
    };

    Pinloom::AnchorLibraryManagementService anchorLibraryManagement(repository);
    const auto availableFileTags = [&anchorLibraryManagement]() {
        QStringList tags;
        for (const Pinloom::AnchorLibraryTagSummary &summary : anchorLibraryManagement.tagSummary()) {
            if (summary.resourceCount > 0) {
                tags.append(summary.tag);
            }
        }
        return tags;
    };
    Pinloom::AnchorLibraryWindowOptions anchorLibraryOptions;
    anchorLibraryOptions.managementService = &anchorLibraryManagement;
    anchorLibraryOptions.archiveService = &anchorLibraryArchive;
    anchorLibraryOptions.repository = &repository;
    anchorLibraryOptions.settings = &appSettingsStore;
    anchorLibraryOptions.automaticBackupDirectory = anchorLibraryBackupDirectory;
    anchorLibraryOptions.filesProvider = [&repository]() {
        QList<Pinloom::AnchorLibraryFile> files;
        Pinloom::SearchQuery query;
        query.limit = 0;
        query.includeDeleted = true;
        for (const Pinloom::SearchResult &result : repository.search(query)) {
            Pinloom::AnchorLibraryFile file;
            file.resource = result.resource;
            file.usage = repository.resourceUsage(result.resource.id)
                             .value_or(Pinloom::ResourceUsage{result.resource.id});
            for (const Pinloom::Anchor &anchor : result.resource.anchors) {
                if (!Pinloom::isValidAnchorLibraryAnchor(anchor)) {
                    continue;
                }
                Pinloom::AnchorLibraryAnchor entry;
                entry.resourceId = result.resource.id;
                entry.anchor = anchor;
                entry.resourceDeleted = result.resource.deleted;
                entry.usage = repository.anchorUsage(result.resource.id, anchor)
                                  .value_or(Pinloom::AnchorUsage{result.resource.id});
                file.anchors.append(entry);
            }
            if (Pinloom::shouldProvideAnchorLibraryResource(file.resource, file.usage)) {
                files.append(file);
            }
        }
        return files;
    };
    anchorLibraryOptions.anchorJumpHandler = [&openService, &window](const Pinloom::AnchorLibraryFile &file,
                                                                     const Pinloom::AnchorLibraryAnchor &entry,
                                                                     QString *status) {
        const Pinloom::Anchor &anchor = entry.anchor;
        Pinloom::PinloomOpenTarget target;
        target.resourceId = entry.resourceId;
        target.resourceKind = file.resource.kind;
        target.title = anchor.name.trimmed().isEmpty() ? file.resource.title : anchor.name;
        target.location = file.resource.location;
        target.anchor = anchor;
        const bool activated = openService.open(target, &window);
        if (status) {
            *status = openService.statusText();
        }
        return activated;
    };
    anchorLibraryOptions.pdfPreviewOptionsProvider = [&runtimeSettings]() {
        Pinloom::PdfLocatorPreviewRenderOptions previewOptions;
        previewOptions.rendererExecutablePath = Pinloom::resolvePdfLocatorPreviewRendererPath(
            runtimeSettings.sumatraPdfExecutablePath.trimmed());
        return previewOptions;
    };
    anchorLibraryOptions.locatorPreviewHandler = [&openService, &window](const Pinloom::AnchorLibraryFile &file,
                                                                         const Pinloom::AnchorLibraryAnchor &entry,
                                                                         QString *status) {
        const Pinloom::Resource resource = file.resource;
        const Pinloom::Anchor anchor = entry.anchor;
        const QString resourceId = entry.resourceId;
        Pinloom::PinloomOpenTarget target;
        target.resourceId = resourceId;
        target.resourceKind = resource.kind;
        target.title = anchor.name.trimmed().isEmpty() ? resource.title : anchor.name;
        target.location = resource.location;
        target.anchor = anchor;
        if (!openService.open(target, &window)) {
            if (status) *status = openService.statusText();
            return QPixmap{};
        }
        QEventLoop previewDelay;
        QTimer::singleShot(120, &previewDelay, &QEventLoop::quit);
        previewDelay.exec(QEventLoop::ExcludeUserInputEvents);
        const Pinloom::ForegroundAppWindowContext context =
            Pinloom::currentForegroundAppWindowContext();
        QScreen *screen = QApplication::screenAt(QCursor::pos());
        if (!screen) screen = QApplication::primaryScreen();
        if (!screen || !context.isValid()) {
            if (status) *status = QStringLiteral("Opened anchor; application preview is unavailable");
            return QPixmap{};
        }
        const QPixmap screenshot = screen->grabWindow(static_cast<WId>(context.windowHandle));
        if (status) {
            *status = screenshot.isNull()
                ? QStringLiteral("Opened anchor; application preview is unavailable")
                : QStringLiteral("Captured application locator preview");
        }
        return screenshot;
    };
    anchorLibraryOptions.locatorRecaptureHandler =
        [&openService, &window, &pdfViewerAdapter](const Pinloom::AnchorLibraryFile &file,
                                                  const Pinloom::AnchorLibraryAnchor &entry,
                                                  QString *status)
        -> std::optional<Pinloom::AnchorLocatorUpdate> {
        const Pinloom::Resource resource = file.resource;
        const Pinloom::Anchor anchor = entry.anchor;
        const QString resourceId = entry.resourceId;
        Pinloom::PinloomOpenTarget target;
        target.resourceId = resourceId;
        target.resourceKind = resource.kind;
        target.title = anchor.name.trimmed().isEmpty() ? resource.title : anchor.name;
        target.location = resource.location;
        Pinloom::Anchor pageAnchor = anchor;
        pageAnchor.locatorType = QStringLiteral("sumatrapdf.page");
        pageAnchor.locatorJson = QString::fromUtf8(
            QJsonDocument(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("sumatrapdf.page")},
                {QStringLiteral("page"), Pinloom::anchorLocatorPage(anchor)}})
                .toJson(QJsonDocument::Compact));
        target.anchor = pageAnchor;
        if (!openService.open(target, &window)) {
            if (status) *status = openService.statusText();
            return std::nullopt;
        }
        bool navigationFinished = false;
        bool navigationSucceeded = false;
        bool waitTimedOut = false;
        QEventLoop navigationWait;
        const auto inspectNavigationStatus =
            [&](const QString &navigationStatus) {
            if (navigationStatus.startsWith(QStringLiteral("Opening PDF"))) {
                return;
            }
            navigationFinished = true;
            navigationSucceeded = navigationStatus.startsWith(
                QStringLiteral("Opened "));
            navigationWait.quit();
        };
        const QMetaObject::Connection statusConnection = QObject::connect(
            &openService,
            &Pinloom::PinloomOpenService::statusChanged,
            &navigationWait,
            inspectNavigationStatus);
        QTimer navigationTimeout;
        navigationTimeout.setSingleShot(true);
        QObject::connect(&navigationTimeout, &QTimer::timeout,
                         &navigationWait, [&]() {
            waitTimedOut = true;
            navigationWait.quit();
        });
        inspectNavigationStatus(openService.statusText());
        if (!navigationFinished) {
            navigationTimeout.start(6000);
            navigationWait.exec();
        }
        QObject::disconnect(statusConnection);
        if (waitTimedOut) {
            pdfViewerAdapter.cancelPending();
            if (status) {
                *status = QStringLiteral(
                    "PDF navigation timed out before locator recapture");
            }
            return std::nullopt;
        }
        if (!navigationSucceeded) {
            if (status) *status = openService.statusText();
            return std::nullopt;
        }
        Pinloom::PdfViewerCaptureRequest captureRequest;
        captureRequest.context = Pinloom::currentForegroundAppWindowContext();
        captureRequest.parent = &window;
        Pinloom::PdfViewerCaptureResult captured =
            pdfViewerAdapter.captureRectangle(captureRequest);
        if (!captured.succeeded()) {
            if (status) {
                *status = captured.message;
                if (!captured.diagnostics.trimmed().isEmpty()
                    && captured.diagnostics.trimmed()
                           != captured.message.trimmed()) {
                    status->append(QStringLiteral(": ")
                                       + captured.diagnostics.trimmed());
                }
            }
            return std::nullopt;
        }
        captured.anchorRequest.name = anchor.name;
        captured.anchorRequest.source =
            QStringLiteral("anchor-library-recapture");
        const Pinloom::AnchorCaptureDraft draft =
            Pinloom::anchorCaptureDraftFromPdfRequest(
                captured.anchorRequest);
        Pinloom::AnchorLocatorUpdate update;
        update.targetApp = draft.targetApp;
        update.targetFile = draft.targetFile;
        update.locatorType = draft.locatorType;
        update.locatorJson = draft.locatorJson;
        if (status) {
            *status = QStringLiteral("Captured PDF page %1 rectangle")
                          .arg(captured.anchorRequest.page);
        }
        return update;
    };
    std::unique_ptr<Pinloom::AnchorLibraryWindow> anchorLibraryWindow;
    Pinloom::LibraryRootWindowOptions libraryRootOptions;
    libraryRootOptions.settings = &appSettingsStore;
    libraryRootOptions.repository = &repository;
    libraryRootOptions.fileTagsProvider = availableFileTags;
    std::unique_ptr<Pinloom::LibraryRootWindow> libraryRootWindow;
    Pinloom::ClipLibraryWindowOptions clipLibraryOptions;
    clipLibraryOptions.settings = &appSettingsStore;
    clipLibraryOptions.clipsProvider = [&clipHost]() {
        if (clipHost && clipHost->sqliteRepository()) {
            return clipHost->sqliteRepository()->clips();
        }
        if (clipHost && clipHost->inMemoryRepository()) {
            return clipHost->inMemoryRepository()->clips();
        }
        return QList<Pinloom::Clip>{};
    };
    clipLibraryOptions.saveClipHandler = [&persistSavedClip](const Pinloom::Clip &clip, QString *error) {
        return persistSavedClip(clip, error);
    };
    clipLibraryOptions.deleteClipHandler = [&setPersistentClipState](const QString &clipId, QString *error) {
        return setPersistentClipState(clipId, Pinloom::ClipState::Deleted, error);
    };
    clipLibraryOptions.restoreClipHandler = [&setPersistentClipState](const QString &clipId, QString *error) {
        return setPersistentClipState(clipId, Pinloom::ClipState::Saved, error);
    };
    clipLibraryOptions.permanentlyDeleteClipHandler =
        [&permanentlyRemovePersistentClip](const QString &clipId, QString *error) {
            return permanentlyRemovePersistentClip(clipId, error);
    };
    clipLibraryOptions.openSourceHandler = [&obsidianClipStore](const QString &clipId, QString *error) {
        const QUrl url = obsidianClipStore.openUrlForClip(clipId, error);
        if (!url.isValid()) {
            return false;
        }
        if (!QDesktopServices::openUrl(url)) {
            if (error && error->trimmed().isEmpty()) {
                *error = QStringLiteral("Unable to open Obsidian Clip note");
            }
            return false;
        }
        return true;
    };
    std::unique_ptr<Pinloom::ClipLibraryWindow> clipLibraryWindow;

    auto showSettingsDialog = [&]() -> bool {
        Pinloom::PinloomSettingsDialog dialog(runtimeSettings, &window);
        if (dialog.exec() != QDialog::Accepted) {
            return false;
        }

        Pinloom::PinloomAppSettings editedSettings = dialog.settings();
        QString dataDirectoryError;
        if (!Pinloom::stageAppDataDirectoryChange(appSettingsStore,
                                                  appDataPath,
                                                  editedSettings.dataDirectory,
                                                  &dataDirectoryError)) {
            Pinloom::Ui::warning(&window,
                                 QStringLiteral("Pinloom Data Directory"),
                                 dataDirectoryError);
            editedSettings.dataDirectory = appDataPath;
        }
        QString defaultRootError;
        if (!Pinloom::configureDefaultLibraryRoot(repository,
                                                  editedSettings.defaultLibraryRootPath,
                                                  &defaultRootError)) {
            Pinloom::Ui::warning(&window,
                                 QStringLiteral("Pinloom Default Root"),
                                 defaultRootError);
            editedSettings.defaultLibraryRootPath = runtimeSettings.defaultLibraryRootPath;
        } else if (libraryRootWindow) {
            libraryRootWindow->refresh();
        }
        runtimeSettings = editedSettings;
        Pinloom::savePinloomAppSettings(appSettingsStore, runtimeSettings);
        appSettingsStore.sync();
        const Pinloom::ObsidianClipStoreConfig obsidianConfig = obsidianConfigForSettings(runtimeSettings);
        obsidianClipStore.setConfig(obsidianConfig);
        if (obsidianSyncService) {
            obsidianSyncService->setConfig(obsidianConfig);
            if (obsidianConfig.isEnabled()
                && !obsidianSyncService->isRunning()
                && !obsidianSyncService->start()) {
                window.setRecentError(QStringLiteral("Obsidian Clip synchronization failed"),
                                      obsidianSyncService->lastError());
            }
        }
        if (clipHost && clipHost->runtime()) {
            clipHost->runtime()->captureService().setPolicy(runtimeSettings.clipCapturePolicy());
            clipHost->runtime()->trayController().setCapturePaused(
                !runtimeSettings.clipAutomaticCaptureEnabled);
            Pinloom::ClipInsertionOptions insertionOptions =
                clipHost->runtime()->insertionService().options();
            insertionOptions.restoreOriginalClipboardOnSuccess =
                runtimeSettings.clipRestoreOriginalClipboardOnInsert;
            clipHost->runtime()->insertionService().setOptions(insertionOptions);
        }
        return true;
    };
    QObject::connect(&window, &Pinloom::PinloomMainWindow::quitRequested, &app, &QApplication::quit);

    commandWindowForForegroundCapture = &window;
    activeClipInsertionWindow = &window;

    Pinloom::PinloomCommandDispatcher commandDispatcher;
    Pinloom::PinloomCommandPanelOptions commandOptions;
    commandOptions.commandDispatcher = &commandDispatcher;
    commandOptions.statusChangedHandler = [&window](const QString &status) {
        const QString lower = status.toLower();
        if (lower.contains(QStringLiteral("unable"))
            || lower.contains(QStringLiteral("failed"))
            || lower.contains(QStringLiteral("error"))
            || lower.contains(QStringLiteral("missing"))
            || lower.contains(QStringLiteral("could not"))) {
            window.setRecentError(status);
        }
    };
    commandOptions.unifiedEntrySearchHandler = [&entrySearchService](const QString &query) {
        return entrySearchService.search(query);
    };
    commandOptions.deletedEntrySearchHandler = [&entrySearchService](const QString &query) {
        Pinloom::PinloomEntrySearchOptions options;
        options.deletedOnly = true;
        return entrySearchService.search(query, options);
    };
    const auto activateOpenTarget =
        [&openService, &window](const Pinloom::PinloomOpenTarget &target, QString *status) {
        const bool activated = openService.open(target, &window);
        if (status) {
            *status = openService.statusText();
        }
        return activated;
    };
    commandOptions.anchorJumpHandler = activateOpenTarget;
    commandOptions.resourceOpenHandler = activateOpenTarget;
    commandOptions.clipSearchHandler = clipSearchHandler;
    commandOptions.clipInsertionHandler = clipInsertionHandler;
    commandOptions.clipSaveHandler = clipSaveHandler;
    commandOptions.clipLibraryHandler = [&clipLibraryWindow,
                                         &clipLibraryOptions,
                                         &pendingClipInsertionTarget,
                                         &window](QString *status) {
        pendingClipInsertionTarget.reset();
        if (!clipLibraryWindow) {
            clipLibraryWindow = std::make_unique<Pinloom::ClipLibraryWindow>(clipLibraryOptions);
        }
        clipLibraryWindow->refresh();
        if (clipLibraryWindow->isMinimized()) {
            clipLibraryWindow->showNormal();
        } else {
            clipLibraryWindow->show();
        }
        clipLibraryWindow->raise();
        clipLibraryWindow->activateWindow();
        window.hide();
        if (status) {
            *status = QStringLiteral("Opened Clip Library: %1 Clip(s)")
                          .arg(clipLibraryWindow->visibleClipCount());
        }
        return true;
    };
    commandOptions.anchorCaptureHandler = captureRememberedApplicationAnchor;
    commandOptions.rectangleAnchorCaptureHandler = captureForegroundPdfAnchor;
    commandOptions.textAnchorCaptureHandler = captureForegroundPdfTextAnchor;
    commandOptions.pdfTextClipCaptureHandler = captureForegroundPdfTextClip;
    commandOptions.anchorLibraryHandler = [&anchorLibraryWindow,
                                           &anchorLibraryOptions,
                                           &window](QString *status) {
        if (!anchorLibraryWindow) {
            anchorLibraryWindow = std::make_unique<Pinloom::AnchorLibraryWindow>(anchorLibraryOptions);
        }
        anchorLibraryWindow->refreshLibrary();
        if (anchorLibraryWindow->isMinimized()) {
            anchorLibraryWindow->showNormal();
        } else {
            anchorLibraryWindow->show();
        }
        anchorLibraryWindow->raise();
        anchorLibraryWindow->activateWindow();
        window.hide();
        if (status) {
            *status = QStringLiteral("Opened Anchor Library: %1 marked file(s)")
                          .arg(anchorLibraryWindow->visibleFileCount());
        }
        return true;
    };
    commandOptions.libraryRootHandler = [&libraryRootWindow,
                                         &libraryRootOptions,
                                         &window](QString *status) {
        if (!libraryRootWindow) {
            libraryRootWindow = std::make_unique<Pinloom::LibraryRootWindow>(libraryRootOptions);
        }
        libraryRootWindow->refresh();
        if (libraryRootWindow->isMinimized()) {
            libraryRootWindow->showNormal();
        } else {
            libraryRootWindow->show();
        }
        libraryRootWindow->raise();
        libraryRootWindow->activateWindow();
        window.hide();
        if (status) {
            *status = QStringLiteral("Opened Root Library: %1 root(s)")
                          .arg(libraryRootWindow->rootCount());
        }
        return true;
    };
    commandOptions.inboxSelectionProvider =
        [&lastForegroundContext, &commandWindowForForegroundCapture](QString *status) -> QStringList {
        const bool useLastForegroundContext =
            commandWindowForForegroundCapture
            && commandWindowForForegroundCapture->isVisible()
            && lastForegroundContext.isValid();
        const Pinloom::ForegroundAppWindowContext context =
            useLastForegroundContext
                ? lastForegroundContext
                : Pinloom::currentForegroundAppWindowContext();
        const Pinloom::ExplorerFileSelectionResult result =
            Pinloom::captureExplorerFileSelection(context);
        if (status) {
            *status = result.status;
        }
        return result.filePaths;
    };
    commandOptions.inboxTagProvider = availableFileTags;
    const QString managedLibraryDirectory =
        QDir(appDataPath).filePath(QStringLiteral("managed-library"));
    commandOptions.inboxSaveHandler =
        [&repository, managedLibraryDirectory](Pinloom::InboxFileSaveRequest request) {
        request.managedLibraryDirectory = managedLibraryDirectory;
        return Pinloom::saveInboxFile(repository, request);
    };
    commandOptions.droppedTextSaveHandler =
        [&archiveSelectedText, &availableClipTags, &automaticClipName](QWidget *parent,
                                                                      const QString &text,
                                                                      QString *status) {
        Pinloom::TextSelectionCaptureResult selection;
        selection.state = Pinloom::TextSelectionState::TextSelected;
        selection.text = text;
        selection.source = QStringLiteral("drag-drop");
        Pinloom::ClipCaptureDialog dialog(text,
                                          automaticClipName(text),
                                          availableClipTags(),
                                          parent);
        if (dialog.exec() != QDialog::Accepted) {
            if (status) *status = QStringLiteral("Dropped text save canceled");
            return false;
        }
        return archiveSelectedText(selection, dialog.metadata(), status);
    };
    const auto registerBooleanCommand =
        [&commandDispatcher, &window](Pinloom::PinloomCommandId id,
                                      const std::function<bool(QString *)> &handler,
                                      const QString &successFallback,
                                      const QString &failureFallback) {
            QString registrationError;
            const bool registered = commandDispatcher.registerHandler(
                id,
                [handler, successFallback, failureFallback](
                    const Pinloom::PinloomCommandInvocation &) {
                    QString status;
                    const bool succeeded = handler && handler(&status);
                    return Pinloom::pinloomCommandResultFromBoolean(
                        succeeded, status, successFallback, failureFallback);
                },
                &registrationError);
            if (!registered) {
                window.setRecentError(QStringLiteral("Command registration failed"),
                                      registrationError);
            }
        };
    registerBooleanCommand(Pinloom::PinloomCommandId::ClipLibrary,
                           commandOptions.clipLibraryHandler,
                           QStringLiteral("Opened Clip Library"),
                           QStringLiteral("Unable to open Clip Library"));
    registerBooleanCommand(Pinloom::PinloomCommandId::ClipPdfText,
                           commandOptions.pdfTextClipCaptureHandler,
                           QStringLiteral("Captured PDF text Clip"),
                           QStringLiteral("PDF Text Clip is unavailable for the remembered target"));
    registerBooleanCommand(Pinloom::PinloomCommandId::AnchorNew,
                           commandOptions.anchorCaptureHandler,
                           QStringLiteral("Captured Anchor"),
                           QStringLiteral("No Anchor context is available"));
    registerBooleanCommand(Pinloom::PinloomCommandId::AnchorPdfRectangle,
                           commandOptions.rectangleAnchorCaptureHandler,
                           QStringLiteral("Captured PDF rectangle Anchor"),
                           QStringLiteral("PDF Rectangle Anchor is unavailable for the remembered target"));
    registerBooleanCommand(Pinloom::PinloomCommandId::AnchorPdfText,
                           commandOptions.textAnchorCaptureHandler,
                           QStringLiteral("Captured PDF text Anchor"),
                           QStringLiteral("PDF Text Anchor is unavailable for the remembered target"));
    registerBooleanCommand(Pinloom::PinloomCommandId::AnchorLibrary,
                           commandOptions.anchorLibraryHandler,
                           QStringLiteral("Opened Anchor Library"),
                           QStringLiteral("Unable to open Anchor Library"));
    registerBooleanCommand(Pinloom::PinloomCommandId::RootLibrary,
                           commandOptions.libraryRootHandler,
                           QStringLiteral("Opened Root Library"),
                           QStringLiteral("Unable to open Root Library"));
    commandDispatcher.registerHandler(
        Pinloom::PinloomCommandId::Settings,
        [&showSettingsDialog](const Pinloom::PinloomCommandInvocation &) {
            return showSettingsDialog()
                ? Pinloom::PinloomCommandDispatchResult::complete(
                      QStringLiteral("Settings saved"))
                : Pinloom::PinloomCommandDispatchResult::cancel(
                      QStringLiteral("Settings canceled"));
        });
    commandDispatcher.registerHandler(
        Pinloom::PinloomCommandId::Diagnostics,
        [&window](const Pinloom::PinloomCommandInvocation &) {
            window.showDiagnosticsDialog();
            return Pinloom::PinloomCommandDispatchResult::complete(
                QStringLiteral("Diagnostics closed"));
        });
    const auto dispatchWindowCommand =
        [&commandDispatcher, &window](Pinloom::PinloomCommandId id) {
            Pinloom::PinloomCommandInvocation invocation;
            invocation.parent = &window;
            const Pinloom::PinloomCommandDispatchResult result =
                commandDispatcher.dispatch(id, invocation);
            if (result.failed()) {
                const QString message = result.message.trimmed().isEmpty()
                    ? QStringLiteral("Pinloom command failed")
                    : result.message.trimmed();
                window.setRecentError(QStringLiteral("Command failed"), message);
                window.statusBar()->showMessage(message, 6000);
            }
            return result;
        };
    QObject::connect(&window,
                     &Pinloom::PinloomMainWindow::settingsRequested,
                     &window,
                     [&dispatchWindowCommand]() {
                         dispatchWindowCommand(Pinloom::PinloomCommandId::Settings);
                     });
    QObject::connect(&window,
                     &Pinloom::PinloomMainWindow::diagnosticsRequested,
                     &window,
                     [&dispatchWindowCommand]() {
                         dispatchWindowCommand(Pinloom::PinloomCommandId::Diagnostics);
                     });
    const auto targetTitle = [](const Pinloom::PinloomOpenTarget &target) {
        if (target.anchor.has_value()) {
            const QString anchorName = target.anchor->name.trimmed();
            if (!anchorName.isEmpty()) {
                return anchorName;
            }
        }
        if (!target.title.trimmed().isEmpty()) {
            return target.title.trimmed();
        }
        if (!target.location.trimmed().isEmpty()) {
            return target.location.trimmed();
        }
        return target.clipId.trimmed();
    };
    const auto cleanTag = [](QString tag) {
        tag = tag.trimmed();
        while (tag.startsWith(QLatin1Char('#'))) {
            tag.remove(0, 1);
            tag = tag.trimmed();
        }
        return tag;
    };
    const auto appendUniqueValue = [](QStringList &values, const QString &value) {
        if (!value.trimmed().isEmpty() && !values.contains(value, Qt::CaseInsensitive)) {
            values.append(value);
        }
    };
    const auto valuesFromCommaText = [&appendUniqueValue, &cleanTag](const QString &text, bool tags) {
        QStringList values;
        for (const QString &value : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            if (tags) {
                appendUniqueValue(values, cleanTag(value));
            } else if (!value.trimmed().isEmpty()) {
                values.append(value);
            }
        }
        return values;
    };
    struct MetadataEdit {
        QString name;
        QStringList aliases;
        QStringList tags;
        bool pinned = false;
    };
    const auto promptMetadataEdit =
        [&valuesFromCommaText](QWidget *parent,
                               const QString &windowTitle,
                               const QString &name,
                               const QStringList &aliases,
                               const QStringList &tags,
                               bool pinned) -> std::optional<MetadataEdit> {
        Pinloom::Ui::Dialog dialog(parent);
        dialog.setWindowTitle(windowTitle);
        auto *form = new Pinloom::Ui::FormLayout(&dialog);
        auto *nameEdit = Pinloom::Ui::lineEdit(name, &dialog);
        auto *aliasesEdit = Pinloom::Ui::lineEdit(aliases.join(QLatin1Char(',')), &dialog);
        auto *tagsEdit = Pinloom::Ui::lineEdit(tags.join(QStringLiteral(", ")), &dialog);
        auto *pinnedCheck = Pinloom::Ui::checkBox(QStringLiteral("Pinned"), &dialog);
        auto *buttons = new Pinloom::Ui::DialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        pinnedCheck->setChecked(pinned);
        form->addRow(QStringLiteral("Name"), nameEdit);
        form->addRow(QStringLiteral("Aliases"), aliasesEdit);
        form->addRow(QStringLiteral("Tags"), tagsEdit);
        form->addRow(QString(), pinnedCheck);
        form->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) {
            return std::nullopt;
        }
        MetadataEdit edit;
        edit.name = nameEdit->text();
        edit.aliases = valuesFromCommaText(aliasesEdit->text(), false);
        edit.tags = valuesFromCommaText(tagsEdit->text(), true);
        edit.pinned = pinnedCheck->isChecked();
        return edit;
    };
    const auto promptValueListEdit =
        [&valuesFromCommaText](QWidget *parent,
                               const QString &windowTitle,
                               const QString &label,
                               const QStringList &values,
                               bool tags) -> std::optional<QStringList> {
        bool accepted = false;
        const QString text = Pinloom::Ui::getText(parent,
                                                   windowTitle,
                                                   label,
                                                   QLineEdit::Normal,
                                                   values.join(tags ? QStringLiteral(", ")
                                                                    : QStringLiteral(",")),
                                                   &accepted);
        if (!accepted) {
            return std::nullopt;
        }
        return valuesFromCommaText(text, tags);
    };
    const auto findClip = [&findPersistentClip](const QString &clipId) -> std::optional<Pinloom::Clip> {
        return findPersistentClip(clipId);
    };
    const auto saveClipMetadata =
        [&clipHost, &persistSavedClip](const Pinloom::Clip &clip,
                                      const QString &name,
                                      const QStringList &aliases,
                                      const QStringList &tags,
                                      bool pinned,
                                      QString *status) {
        if (!clipHost || !clipHost->runtime()) {
            if (status) {
                *status = QStringLiteral("Pinloom Clip is not running");
            }
            return false;
        }
        Pinloom::Clip updated = clip;
        updated.name = name;
        updated.aliases = aliases;
        updated.tags = tags;
        updated.pinned = pinned;
        QString error;
        if (!persistSavedClip(updated, &error)) {
            if (status) {
                *status = error.trimmed().isEmpty() ? QStringLiteral("Unable to update clip") : error.trimmed();
            }
            return false;
        }
        return true;
    };
    const auto targetStillExists = [&repository](const Pinloom::PinloomOpenTarget &target) {
        if (target.resourceId.trimmed().isEmpty()) return false;
        const std::optional<Pinloom::Resource> resource = repository.findResource(target.resourceId);
        if (!resource.has_value()) return false;
        if (!target.anchor.has_value()) return true;
        return std::any_of(resource->anchors.cbegin(),
                           resource->anchors.cend(),
                           [&target](const Pinloom::Anchor &anchor) {
                               return Pinloom::sameAnchorIdentity(anchor, target.anchor.value());
                           });
    };
    const auto updateAnchorMetadata =
        [&anchorLibraryManagement](const Pinloom::PinloomOpenTarget &target,
                                   const QString &name,
                                   const QStringList &aliases,
                                   const QStringList &tags,
                                   bool pinned,
                                   QString *status) {
        if (!target.anchor.has_value()) {
            if (status) *status = QStringLiteral("Anchor is required");
            return false;
        }
        Pinloom::AnchorMetadataUpdate update;
        update.name = name;
        update.aliases = aliases;
        update.tags = tags;
        update.pinned = pinned;
        const Pinloom::AnchorLibraryOperationResult result =
            anchorLibraryManagement.updateAnchorMetadata(
                {target.resourceId, target.anchor.value()}, update);
        if (status) *status = result.message;
        return result.success;
    };
    const auto executeEntryAction =
        [&repository,
         &clipHost,
         &targetTitle,
         &cleanTag,
         &appendUniqueValue,
         &promptMetadataEdit,
         &promptValueListEdit,
         &findClip,
         &saveClipMetadata,
         &obsidianClipStore,
         &setPersistentClipState,
         &targetStillExists,
         &updateAnchorMetadata,
         &anchorLibraryManagement,
         &activateOpenTarget](QWidget *parent,
                                        const Pinloom::PinloomEntry &entry,
                                        const Pinloom::PinloomCommandResultAction &action,
                                        QString *status) {
        const Pinloom::PinloomOpenTarget target = Pinloom::openTargetFromEntry(entry);
        const QString actionId = action.id.trimmed();
        if (actionId == QLatin1String("open_folder")) {
            return Pinloom::openContainingFolderForPinloomEntry(entry, status);
        }
        if (actionId == QLatin1String("primary")) {
            return activateOpenTarget(target, status);
        }

        if (!target.clipId.trimmed().isEmpty()) {
            const std::optional<Pinloom::Clip> clip = findClip(target.clipId);
            if (actionId == QLatin1String("open_source")) {
                QString error;
                const QUrl url = obsidianClipStore.openUrlForClip(target.clipId, &error);
                if (!url.isValid() || !QDesktopServices::openUrl(url)) {
                    if (status) {
                        *status = error.trimmed().isEmpty()
                            ? QStringLiteral("Unable to open Obsidian Clip note")
                            : error.trimmed();
                    }
                    return false;
                }
                if (status) {
                    *status = QStringLiteral("Opened Saved Clip in Obsidian");
                }
                return true;
            }
            if (actionId == QLatin1String("restore")) {
                if (!clip.has_value() || clip->state != Pinloom::ClipState::Deleted) {
                    if (status) {
                        *status = QStringLiteral("Restore requires a deleted Saved Clip");
                    }
                    return false;
                }
                QString error;
                if (!setPersistentClipState(clip->id, Pinloom::ClipState::Saved, &error)) {
                    if (status) {
                        *status = error.trimmed().isEmpty()
                            ? QStringLiteral("Unable to restore Saved Clip")
                            : error.trimmed();
                    }
                    return false;
                }
                if (status) {
                    *status = QStringLiteral("Restored Saved Clip \"%1\"").arg(clip->name);
                }
                return true;
            }
            if (!clip.has_value() || clip->state != Pinloom::ClipState::Saved) {
                if (status) {
                    *status = QStringLiteral("Action requires a Saved Clip");
                }
                return false;
            }

            if (actionId == QLatin1String("remove")) {
                const QMessageBox::StandardButton choice = Pinloom::Ui::question(
                    parent,
                    QStringLiteral("Delete Saved Clip"),
                    QStringLiteral("Remove \"%1\" from ordinary Pinloom search?\n\n"
                                   "This archives the Saved Clip inside Pinloom and does not delete files or external clipboard data.")
                        .arg(targetTitle(target)));
                if (choice != QMessageBox::Yes) {
                    if (status) {
                        *status = QStringLiteral("Remove canceled");
                    }
                    return false;
                }
                QString error;
                if (!setPersistentClipState(clip->id, Pinloom::ClipState::Deleted, &error)) {
                    if (status) {
                        *status = error.trimmed().isEmpty()
                            ? QStringLiteral("Unable to remove Saved Clip")
                            : error.trimmed();
                    }
                    return false;
                }
                if (status) {
                    *status = QStringLiteral("Removed Saved Clip from Pinloom");
                }
                return true;
            }

            if (actionId == QLatin1String("rename")) {
                bool accepted = false;
                const QString name = Pinloom::Ui::getText(parent,
                                                           QStringLiteral("Rename Clip"),
                                                           QStringLiteral("Name"),
                                                           QLineEdit::Normal,
                                                           clip->name,
                                                           &accepted);
                if (!accepted) {
                    if (status) {
                        *status = QStringLiteral("Rename canceled");
                    }
                    return false;
                }
                if (!saveClipMetadata(clip.value(), name, clip->aliases, clip->tags, clip->pinned, status)) {
                    return false;
                }
                if (status) {
                    *status = QStringLiteral("Renamed clip \"%1\"").arg(name);
                }
                return true;
            }
            if (actionId == QLatin1String("edit_aliases")) {
                const std::optional<QStringList> aliases =
                    promptValueListEdit(parent,
                                        QStringLiteral("Edit Clip Aliases"),
                                        QStringLiteral("Aliases"),
                                        clip->aliases,
                                        false);
                if (!aliases.has_value()) {
                    if (status) {
                        *status = QStringLiteral("Edit aliases canceled");
                    }
                    return false;
                }
                if (!saveClipMetadata(clip.value(), clip->name, aliases.value(), clip->tags, clip->pinned, status)) {
                    return false;
                }
                if (status) {
                    *status = QStringLiteral("Updated clip aliases");
                }
                return true;
            }
            if (actionId == QLatin1String("edit_tags")) {
                const std::optional<QStringList> tags =
                    promptValueListEdit(parent,
                                        QStringLiteral("Edit Clip Tags"),
                                        QStringLiteral("Tags"),
                                        clip->tags,
                                        true);
                if (!tags.has_value()) {
                    if (status) {
                        *status = QStringLiteral("Edit tags canceled");
                    }
                    return false;
                }
                if (!saveClipMetadata(clip.value(), clip->name, clip->aliases, tags.value(), clip->pinned, status)) {
                    return false;
                }
                if (status) {
                    *status = QStringLiteral("Updated clip tags");
                }
                return true;
            }

            if (actionId == QLatin1String("pin") || actionId == QLatin1String("unpin")) {
                const bool pinned = actionId == QLatin1String("pin");
                if (!saveClipMetadata(clip.value(), clip->name, clip->aliases, clip->tags, pinned, status)) {
                    return false;
                }
                if (status) {
                    *status = pinned ? QStringLiteral("Pinned clip") : QStringLiteral("Unpinned clip");
                }
                return true;
            }
            if (actionId == QLatin1String("add_alias")) {
                bool accepted = false;
                const QString alias = Pinloom::Ui::getText(parent,
                                                            QStringLiteral("Add Alias"),
                                                            QStringLiteral("Alias"),
                                                            QLineEdit::Normal,
                                                            QString(),
                                                            &accepted);
                if (!accepted) {
                    if (status) {
                        *status = QStringLiteral("Add alias canceled");
                    }
                    return false;
                }
                QStringList aliases = clip->aliases;
                appendUniqueValue(aliases, alias);
                if (aliases == clip->aliases) {
                    if (status) {
                        *status = QStringLiteral("Clip alias already exists or is empty");
                    }
                    return false;
                }
                if (!saveClipMetadata(clip.value(), clip->name, aliases, clip->tags, clip->pinned, status)) {
                    return false;
                }
                if (status) {
                    *status = QStringLiteral("Added clip alias \"%1\"").arg(alias);
                }
                return true;
            }
            if (actionId == QLatin1String("add_tag")) {
                bool accepted = false;
                const QString tag = cleanTag(Pinloom::Ui::getText(parent,
                                                                   QStringLiteral("Add Tag"),
                                                                   QStringLiteral("Tag"),
                                                                   QLineEdit::Normal,
                                                                   QString(),
                                                                   &accepted));
                if (!accepted) {
                    if (status) {
                        *status = QStringLiteral("Add tag canceled");
                    }
                    return false;
                }
                QStringList tags = clip->tags;
                appendUniqueValue(tags, tag);
                if (tags == clip->tags) {
                    if (status) {
                        *status = QStringLiteral("Clip tag already exists or is empty");
                    }
                    return false;
                }
                if (!saveClipMetadata(clip.value(), clip->name, clip->aliases, tags, clip->pinned, status)) {
                    return false;
                }
                if (status) {
                    *status = QStringLiteral("Added clip tag \"%1\"").arg(tag);
                }
                return true;
            }
            if (actionId == QLatin1String("edit_metadata")) {
                const std::optional<MetadataEdit> edit =
                    promptMetadataEdit(parent,
                                       QStringLiteral("Edit Clip"),
                                       clip->name,
                                       clip->aliases,
                                       clip->tags,
                                       clip->pinned);
                if (!edit.has_value()) {
                    if (status) {
                        *status = QStringLiteral("Edit canceled");
                    }
                    return false;
                }
                if (!saveClipMetadata(clip.value(), edit->name, edit->aliases, edit->tags, edit->pinned, status)) {
                    return false;
                }
                if (status) {
                    *status = QStringLiteral("Updated clip \"%1\"").arg(edit->name);
                }
                return true;
            }
        }

        if (actionId == QLatin1String("restore")) {
            if (!target.deleted) {
                if (status) {
                    *status = QStringLiteral("Entry is not deleted");
                }
                return false;
            }
            const bool restored = target.anchor.has_value()
                ? repository.restoreAnchor(target.resourceId, target.anchor.value())
                : repository.restoreResource(target.resourceId);
            if (!restored) {
                if (status) {
                    *status = repository.lastError().trimmed().isEmpty()
                        ? (target.anchor.has_value()
                               ? QStringLiteral("Unable to restore anchor")
                               : QStringLiteral("Unable to restore resource"))
                        : repository.lastError();
                }
                return false;
            }
            if (status) {
                *status = target.anchor.has_value()
                    ? QStringLiteral("Restored anchor \"%1\"").arg(targetTitle(target))
                    : Pinloom::isInboxResourceId(target.resourceId)
                          ? QStringLiteral("Restored Inbox file \"%1\"").arg(targetTitle(target))
                          : QStringLiteral("Restored resource \"%1\"").arg(targetTitle(target));
            }
            return true;
        }

        if (!targetStillExists(target)) {
            if (status) {
                *status = QStringLiteral("Selected result is no longer available");
            }
            return false;
        }

        if (actionId == QLatin1String("remove")) {
            const QString title = target.anchor.has_value()
                ? QStringLiteral("Delete Anchor")
                : Pinloom::isInboxResourceId(target.resourceId)
                      ? QStringLiteral("Remove Inbox File")
                      : QStringLiteral("Remove Resource");
            const QString body = target.anchor.has_value()
                ? QStringLiteral("Delete \"%1\" from Pinloom?\n\nThis only removes the Pinloom anchor. It will not delete the target file.")
                      .arg(targetTitle(target))
                : QStringLiteral("Remove \"%1\" from Pinloom?\n\nThis hides Pinloom's record only. The original file is not deleted.")
                      .arg(targetTitle(target));
            const QMessageBox::StandardButton choice = Pinloom::Ui::question(parent, title, body);
            if (choice != QMessageBox::Yes) {
                if (status) {
                    *status = QStringLiteral("Remove canceled");
                }
                return false;
            }

            const bool removed = target.anchor.has_value()
                ? repository.softDeleteAnchor(target.resourceId, target.anchor.value())
                : repository.softDeleteResource(target.resourceId);
            if (!removed) {
                if (status) {
                    *status = target.anchor.has_value()
                        ? QStringLiteral("Unable to delete anchor")
                        : QStringLiteral("Unable to remove resource from Pinloom");
                }
                return false;
            }

            if (status) {
                *status = target.anchor.has_value()
                    ? QStringLiteral("Deleted anchor from Pinloom")
                    : Pinloom::isInboxResourceId(target.resourceId)
                          ? QStringLiteral("Removed Inbox file from Pinloom; original file was not deleted")
                          : QStringLiteral("Removed resource from Pinloom; original file was not deleted");
            }
            return true;
        }

        if (actionId == QLatin1String("rename")) {
            bool accepted = false;
            const QString name = Pinloom::Ui::getText(parent,
                                                       target.anchor.has_value()
                                                           ? QStringLiteral("Rename Anchor")
                                                           : QStringLiteral("Rename Resource"),
                                                       QStringLiteral("Name"),
                                                       QLineEdit::Normal,
                                                       targetTitle(target),
                                                       &accepted);
            if (!accepted) {
                if (status) {
                    *status = QStringLiteral("Rename canceled");
                }
                return false;
            }
            if (name.trimmed().isEmpty()) {
                if (status) {
                    *status = QStringLiteral("Name is required");
                }
                return false;
            }
            if (target.anchor.has_value()) {
                return updateAnchorMetadata(target,
                                            name,
                                            target.anchor->aliases,
                                            target.anchor->tags,
                                            target.anchor->pinned,
                                            status);
            }

            std::optional<Pinloom::Resource> resource = repository.findResource(target.resourceId);
            if (!resource.has_value()) {
                if (status) {
                    *status = QStringLiteral("Resource no longer exists");
                }
                return false;
            }
            resource->title = name;
            resource->updatedAt = QDateTime::currentDateTimeUtc();
            if (!repository.upsertResource(resource.value())) {
                if (status) {
                    *status = repository.lastError().trimmed().isEmpty()
                        ? QStringLiteral("Unable to rename resource")
                        : repository.lastError();
                }
                return false;
            }
            if (status) {
                *status = QStringLiteral("Renamed resource \"%1\"").arg(name);
            }
            return true;
        }

        if (actionId == QLatin1String("edit_aliases")) {
            const QStringList currentAliases = target.anchor.has_value()
                ? target.anchor->aliases
                : repository.findResource(target.resourceId).value_or(Pinloom::Resource{}).aliases;
            const std::optional<QStringList> aliases =
                promptValueListEdit(parent,
                                    target.anchor.has_value()
                                        ? QStringLiteral("Edit Anchor Aliases")
                                        : QStringLiteral("Edit Resource Aliases"),
                                    QStringLiteral("Aliases"),
                                    currentAliases,
                                    false);
            if (!aliases.has_value()) {
                if (status) {
                    *status = QStringLiteral("Edit aliases canceled");
                }
                return false;
            }
            if (target.anchor.has_value()) {
                return updateAnchorMetadata(target,
                                            target.anchor->name,
                                            aliases.value(),
                                            target.anchor->tags,
                                            target.anchor->pinned,
                                            status);
            }

            std::optional<Pinloom::Resource> resource = repository.findResource(target.resourceId);
            if (!resource.has_value()) {
                if (status) {
                    *status = QStringLiteral("Resource no longer exists");
                }
                return false;
            }
            resource->aliases = aliases.value();
            resource->updatedAt = QDateTime::currentDateTimeUtc();
            if (!repository.upsertResource(resource.value())) {
                if (status) {
                    *status = repository.lastError().trimmed().isEmpty()
                        ? QStringLiteral("Unable to update resource aliases")
                        : repository.lastError();
                }
                return false;
            }
            if (status) {
                *status = QStringLiteral("Updated resource aliases");
            }
            return true;
        }

        if (actionId == QLatin1String("edit_tags")) {
            const QStringList currentTags = target.anchor.has_value()
                ? target.anchor->tags
                : repository.findResource(target.resourceId).value_or(Pinloom::Resource{}).tags;
            const std::optional<QStringList> tags =
                promptValueListEdit(parent,
                                    target.anchor.has_value()
                                        ? QStringLiteral("Edit Anchor Tags")
                                        : QStringLiteral("Edit Resource Tags"),
                                    QStringLiteral("Tags"),
                                    currentTags,
                                    true);
            if (!tags.has_value()) {
                if (status) {
                    *status = QStringLiteral("Edit tags canceled");
                }
                return false;
            }
            if (target.anchor.has_value()) {
                return updateAnchorMetadata(target,
                                            target.anchor->name,
                                            target.anchor->aliases,
                                            tags.value(),
                                            target.anchor->pinned,
                                            status);
            }

            std::optional<Pinloom::Resource> resource = repository.findResource(target.resourceId);
            if (!resource.has_value()) {
                if (status) {
                    *status = QStringLiteral("Resource no longer exists");
                }
                return false;
            }
            resource->tags = tags.value();
            resource->updatedAt = QDateTime::currentDateTimeUtc();
            if (!repository.upsertResource(resource.value())) {
                if (status) {
                    *status = repository.lastError().trimmed().isEmpty()
                        ? QStringLiteral("Unable to update resource tags")
                        : repository.lastError();
                }
                return false;
            }
            if (status) {
                *status = QStringLiteral("Updated resource tags");
            }
            return true;
        }

        if (actionId == QLatin1String("pin") || actionId == QLatin1String("unpin")) {
            const bool pinned = actionId == QLatin1String("pin");
            const Pinloom::AnchorLibraryOperationResult result = target.anchor.has_value()
                ? anchorLibraryManagement.setAnchorsPinned(
                      {{target.resourceId, target.anchor.value()}}, pinned)
                : anchorLibraryManagement.setResourcesPinned({target.resourceId}, pinned);
            if (status) *status = result.message;
            return result.success;
        }
        if (actionId == QLatin1String("add_alias")) {
            bool accepted = false;
            const QString alias = Pinloom::Ui::getText(parent,
                                                        QStringLiteral("Add Alias"),
                                                        QStringLiteral("Alias"),
                                                        QLineEdit::Normal,
                                                        QString(),
                                                        &accepted);
            if (!accepted) {
                if (status) {
                    *status = QStringLiteral("Add alias canceled");
                }
                return false;
            }
            if (target.anchor.has_value()) {
                QStringList aliases = target.anchor->aliases;
                appendUniqueValue(aliases, alias);
                if (aliases == target.anchor->aliases) {
                    if (status) *status = QStringLiteral("Anchor alias already exists or is empty");
                    return false;
                }
                return updateAnchorMetadata(target,
                                            target.anchor->name,
                                            aliases,
                                            target.anchor->tags,
                                            target.anchor->pinned,
                                            status);
            }
            const std::optional<Pinloom::Resource> resource = repository.findResource(target.resourceId);
            if (!resource.has_value()) {
                if (status) *status = QStringLiteral("Resource no longer exists");
                return false;
            }
            QStringList aliases = resource->aliases;
            appendUniqueValue(aliases, alias);
            if (aliases == resource->aliases) {
                if (status) *status = QStringLiteral("Resource alias already exists or is empty");
                return false;
            }
            Pinloom::ResourceMetadataUpdate update;
            update.title = resource->title;
            update.aliases = aliases;
            update.tags = resource->tags;
            const Pinloom::AnchorLibraryOperationResult result =
                anchorLibraryManagement.updateResourceMetadata({resource->id}, update);
            if (status) *status = result.message;
            return result.success;
        }
        if (actionId == QLatin1String("add_tag")) {
            bool accepted = false;
            const QString tag = cleanTag(Pinloom::Ui::getText(parent,
                                                               QStringLiteral("Add Tag"),
                                                               QStringLiteral("Tag"),
                                                               QLineEdit::Normal,
                                                               QString(),
                                                               &accepted));
            if (!accepted) {
                if (status) {
                    *status = QStringLiteral("Add tag canceled");
                }
                return false;
            }
            if (target.anchor.has_value()) {
                QStringList tags = target.anchor->tags;
                appendUniqueValue(tags, tag);
                if (tags == target.anchor->tags) {
                    if (status) *status = QStringLiteral("Anchor tag already exists or is empty");
                    return false;
                }
                return updateAnchorMetadata(target,
                                            target.anchor->name,
                                            target.anchor->aliases,
                                            tags,
                                            target.anchor->pinned,
                                            status);
            }
            const std::optional<Pinloom::Resource> resource = repository.findResource(target.resourceId);
            if (!resource.has_value()) {
                if (status) *status = QStringLiteral("Resource no longer exists");
                return false;
            }
            QStringList tags = resource->tags;
            appendUniqueValue(tags, tag);
            if (tags == resource->tags) {
                if (status) *status = QStringLiteral("Resource tag already exists or is empty");
                return false;
            }
            Pinloom::ResourceMetadataUpdate update;
            update.title = resource->title;
            update.aliases = resource->aliases;
            update.tags = tags;
            const Pinloom::AnchorLibraryOperationResult result =
                anchorLibraryManagement.updateResourceMetadata({resource->id}, update);
            if (status) *status = result.message;
            return result.success;
        }
        if (actionId == QLatin1String("edit_metadata")) {
            if (target.anchor.has_value()) {
                const std::optional<MetadataEdit> edit =
                    promptMetadataEdit(parent,
                                       QStringLiteral("Edit Anchor"),
                                       targetTitle(target),
                                       target.anchor->aliases,
                                       target.anchor->tags,
                                       target.anchor->pinned);
                if (!edit.has_value()) {
                    if (status) {
                        *status = QStringLiteral("Edit canceled");
                    }
                    return false;
                }
                return updateAnchorMetadata(target,
                                            edit->name,
                                            edit->aliases,
                                            edit->tags,
                                            edit->pinned,
                                            status);
            }

            std::optional<Pinloom::Resource> resource = repository.findResource(target.resourceId);
            if (!resource.has_value()) {
                if (status) {
                    *status = QStringLiteral("Resource no longer exists");
                }
                return false;
            }
            const std::optional<Pinloom::ResourceUsage> usage = repository.resourceUsage(target.resourceId);
            const bool pinned = usage.has_value() && usage->pinned;
            const std::optional<MetadataEdit> edit =
                promptMetadataEdit(parent,
                                   QStringLiteral("Edit Resource"),
                                   resource->title,
                                   resource->aliases,
                                   resource->tags,
                                   pinned);
            if (!edit.has_value()) {
                if (status) {
                    *status = QStringLiteral("Edit canceled");
                }
                return false;
            }
            Pinloom::ResourceMetadataUpdate update;
            update.title = edit->name;
            update.aliases = edit->aliases;
            update.tags = edit->tags;
            update.pinned = edit->pinned;
            const Pinloom::AnchorLibraryOperationResult result =
                anchorLibraryManagement.updateResourceMetadata({resource->id}, update);
            if (status) *status = result.message;
            return result.success;
        }

        if (status) {
            *status = QStringLiteral("Action \"%1\" is not implemented").arg(action.label);
        }
        return false;
    };

    const auto enrichEntryForCommandAction =
        [&repository, &findClip](const Pinloom::PinloomEntry &entry) {
        const std::optional<Pinloom::Clip> clip = entry.clipId.trimmed().isEmpty()
            ? std::nullopt
            : findClip(entry.clipId);
        const std::optional<Pinloom::Resource> resource = entry.resourceId.trimmed().isEmpty()
            ? std::nullopt
            : repository.findResource(entry.resourceId);
        const std::optional<Pinloom::ResourceUsage> usage = entry.resourceId.trimmed().isEmpty()
            ? std::nullopt
            : repository.resourceUsage(entry.resourceId);
        return Pinloom::enrichedPinloomEntryForAction(entry, clip, resource, usage);
    };
    const auto entryTypeText = [](Pinloom::PinloomEntryType type) {
        switch (type) {
        case Pinloom::PinloomEntryType::Anchor:
            return QStringLiteral("Anchor");
        case Pinloom::PinloomEntryType::SavedClip:
            return QStringLiteral("SavedClip");
        case Pinloom::PinloomEntryType::Inbox:
            return QStringLiteral("Inbox");
        case Pinloom::PinloomEntryType::FileResource:
            return QStringLiteral("FileResource");
        }
        return QStringLiteral("Unknown");
    };
    const auto entryActionDiagnostics =
        [entryTypeText](const Pinloom::PinloomEntry &entry,
                        const Pinloom::PinloomCommandResultAction &action) {
        QStringList diagnostics;
        diagnostics.append(QStringLiteral("action=%1").arg(action.id));
        diagnostics.append(QStringLiteral("entry=%1").arg(entry.id));
        diagnostics.append(QStringLiteral("type=%1").arg(entryTypeText(entry.type)));
        if (!entry.resourceId.trimmed().isEmpty()) {
            diagnostics.append(QStringLiteral("resourceId=%1").arg(entry.resourceId));
        }
        if (!entry.clipId.trimmed().isEmpty()) {
            diagnostics.append(QStringLiteral("clipId=%1").arg(entry.clipId));
        }
        if (entry.anchor.has_value()) {
            diagnostics.append(QStringLiteral("anchorId=%1").arg(entry.anchor->id));
        }
        diagnostics.append(QStringLiteral("deleted=%1").arg(entry.deleted ? QStringLiteral("true") : QStringLiteral("false")));
        return diagnostics.join(QStringLiteral("; "));
    };
    commandOptions.unifiedEntryActionProvider =
        [enrichEntryForCommandAction](const Pinloom::PinloomEntry &entry) {
        return Pinloom::defaultActionsForPinloomEntry(enrichEntryForCommandAction(entry));
    };
    commandOptions.unifiedEntryCommandHandler =
        [enrichEntryForCommandAction,
         entryActionDiagnostics,
         executeEntryAction](QWidget *parent,
                             const Pinloom::PinloomEntry &entry,
                             const Pinloom::PinloomCommandResultAction &action) {
        const Pinloom::PinloomEntry enriched = enrichEntryForCommandAction(entry);
        QString status;
        const bool succeeded = executeEntryAction(parent, enriched, action, &status);
        Pinloom::PinloomCommandActionResult result =
            Pinloom::pinloomCommandResultFromBoolean(
                succeeded,
                status,
                QStringLiteral("Completed action \"%1\"").arg(action.label),
                QStringLiteral("Unable to run action \"%1\"").arg(action.label));
        if (!result.completed()) {
            result.diagnostics = entryActionDiagnostics(enriched, action);
            result.nextUiHint = QStringLiteral("keep actions open");
        } else if (action.id == QLatin1String("remove") || action.id == QLatin1String("restore")) {
            result.nextUiHint = QStringLiteral("refresh ordinary search");
        }
        return result;
    };

    const auto resolveHostDocument =
        [&repository, &findClip, &runtimeSettings](
            const Pinloom::PinloomHostIdentity &identity)
            -> std::optional<Pinloom::PinloomHostDocument> {
        if (!identity.clipId.trimmed().isEmpty()) {
            const std::optional<Pinloom::Clip> clip =
                findClip(identity.clipId);
            if (!clip.has_value()) return std::nullopt;

            Pinloom::PinloomEntry entry;
            entry.id = QStringLiteral("clip:%1").arg(clip->id);
            entry.type = Pinloom::PinloomEntryType::SavedClip;
            entry.name = clip->name.trimmed().isEmpty()
                ? clip->preview
                : clip->name;
            entry.aliases = clip->aliases;
            entry.tags = clip->tags;
            entry.pinned = clip->pinned;
            entry.deleted = clip->state == Pinloom::ClipState::Deleted;
            entry.usedAt = clip->usedAt;
            entry.targetSummary = clip->preview;
            entry.clipId = clip->id;
            entry.location = clip->sourceUri.trimmed().isEmpty()
                ? clip->preview
                : clip->sourceUri;
            entry.metadata.insert(QStringLiteral("sourceApp"), clip->sourceApp);
            entry.metadata.insert(QStringLiteral("sourceWindowTitle"),
                                  clip->sourceWindowTitle);

            Pinloom::PinloomHostDocument document;
            document.entry = entry;
            document.content = clip->text;
            document.details.insert(QStringLiteral("sourceApp"), clip->sourceApp);
            document.details.insert(QStringLiteral("sourceWindowTitle"),
                                    clip->sourceWindowTitle);
            document.details.insert(QStringLiteral("sourceUri"), clip->sourceUri);
            document.details.insert(QStringLiteral("tags"), clip->tags);
            return document;
        }

        QString resourceId = identity.resourceId.trimmed();
        if (resourceId.isEmpty() && !identity.anchorId.trimmed().isEmpty()) {
            Pinloom::SearchQuery allResources;
            allResources.includeDeleted = true;
            allResources.limit = 0;
            for (const Pinloom::SearchResult &result
                 : repository.search(allResources)) {
                const auto anchorMatch = std::find_if(
                    result.resource.anchors.cbegin(),
                    result.resource.anchors.cend(),
                    [&identity](const Pinloom::Anchor &candidate) {
                        return candidate.id == identity.anchorId;
                    });
                if (anchorMatch != result.resource.anchors.cend()) {
                    resourceId = result.resource.id;
                    break;
                }
            }
        }
        if (resourceId.isEmpty()) return std::nullopt;
        const std::optional<Pinloom::Resource> resource =
            repository.findResource(resourceId);
        if (!resource.has_value()) return std::nullopt;

        std::optional<Pinloom::Anchor> anchor;
        if (!identity.anchorId.trimmed().isEmpty()) {
            const auto match = std::find_if(
                resource->anchors.cbegin(),
                resource->anchors.cend(),
                [&identity](const Pinloom::Anchor &candidate) {
                    return candidate.id == identity.anchorId;
                });
            if (match == resource->anchors.cend()) return std::nullopt;
            anchor = *match;
        }

        Pinloom::PdfLocatorPreviewRenderOptions previewOptions;
        previewOptions.rendererExecutablePath =
            Pinloom::resolvePdfLocatorPreviewRendererPath(
                runtimeSettings.sumatraPdfExecutablePath.trimmed());
        return Pinloom::pinloomHostDocumentForResource(
            *resource,
            anchor,
            anchor.has_value()
                ? std::optional<Pinloom::ResourceUsage>{}
                : repository.resourceUsage(resource->id),
            previewOptions);
    };

    const auto resolveAndOpenHostIdentity =
        [resolveHostDocument, activateOpenTarget](
            const Pinloom::PinloomHostIdentity &identity,
            QString *status) {
        const std::optional<Pinloom::PinloomHostDocument> document =
            resolveHostDocument(identity);
        if (!document.has_value()) {
            if (status) {
                *status = !identity.anchorId.trimmed().isEmpty()
                    ? QStringLiteral("Pinloom Anchor %1 does not exist")
                          .arg(identity.anchorId)
                    : (!identity.clipId.trimmed().isEmpty()
                           ? QStringLiteral("Pinloom Clip %1 does not exist")
                                 .arg(identity.clipId)
                           : QStringLiteral("Pinloom entry no longer exists"));
            }
            return false;
        }
        if (document->entry.deleted) {
            if (status) {
                const QString kind = !identity.anchorId.trimmed().isEmpty()
                    ? QStringLiteral("Anchor")
                    : (!identity.clipId.trimmed().isEmpty()
                           ? QStringLiteral("Clip")
                           : QStringLiteral("entry"));
                *status = QStringLiteral(
                    "Pinloom %1 is deleted; restore it before opening")
                              .arg(kind);
            }
            return false;
        }
        return activateOpenTarget(
            Pinloom::openTargetFromEntry(document->entry), status);
    };

    commandDispatcher.registerHandler(
        Pinloom::PinloomCommandId::OpenIdentity,
        [resolveAndOpenHostIdentity](
            const Pinloom::PinloomCommandInvocation &invocation) {
            Pinloom::PinloomHostIdentity identity;
            identity.entryId = invocation.arguments
                                   .value(QStringLiteral("entryId"))
                                   .toString();
            identity.resourceId = invocation.arguments
                                      .value(QStringLiteral("resourceId"))
                                      .toString();
            identity.anchorId = invocation.arguments
                                    .value(QStringLiteral("anchorId"))
                                    .toString();
            identity.clipId = invocation.arguments
                                  .value(QStringLiteral("clipId"))
                                  .toString();
            if (!identity.isValid()) {
                return Pinloom::PinloomCommandDispatchResult::failure(
                    QStringLiteral("Invalid Pinloom identity"));
            }
            QString status;
            const bool opened = resolveAndOpenHostIdentity(identity, &status);
            return Pinloom::pinloomCommandResultFromBoolean(
                opened,
                status,
                QStringLiteral("Opened Pinloom identity"),
                QStringLiteral("Unable to open Pinloom identity"));
        });
    const auto openHostIdentity =
        [&commandDispatcher](const Pinloom::PinloomHostIdentity &identity,
                             QString *status) {
            Pinloom::PinloomCommandInvocation invocation;
            invocation.arguments.insert(QStringLiteral("entryId"),
                                        identity.entryId);
            invocation.arguments.insert(QStringLiteral("resourceId"),
                                        identity.resourceId);
            invocation.arguments.insert(QStringLiteral("anchorId"),
                                        identity.anchorId);
            invocation.arguments.insert(QStringLiteral("clipId"),
                                        identity.clipId);
            const Pinloom::PinloomCommandDispatchResult result =
                commandDispatcher.dispatch(
                    Pinloom::PinloomCommandId::OpenIdentity,
                    invocation);
            if (status) {
                *status = result.message.trimmed().isEmpty()
                    ? result.diagnostics.trimmed()
                    : result.message.trimmed();
            }
            return result.completed();
        };

    Pinloom::PinloomHostBridgeCallbacks hostCallbacks;
    hostCallbacks.search =
        [&entrySearchService](const QString &query, int limit) {
            Pinloom::PinloomEntrySearchOptions options;
            options.limit = limit;
            return entrySearchService.search(query, options);
        };
    hostCallbacks.resolve = resolveHostDocument;
    hostCallbacks.open = openHostIdentity;
    hostCallbacks.createSourceAnchor =
        [&repository](const Pinloom::PinloomSourceAnchorRequest &request,
                      QString *status)
            -> std::optional<Pinloom::PinloomEntry> {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const QString resourceId =
            QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString anchorId =
            QUuid::createUuid().toString(QUuid::WithoutBraces);
        QString title = request.title;
        if (title.trimmed().isEmpty()) {
            title = QFileInfo(request.absoluteFilePath).fileName()
                + QStringLiteral(":%1").arg(request.startLine);
        }
        const QString sourceLabel = request.relativeFilePath.trimmed().isEmpty()
            ? QFileInfo(request.absoluteFilePath).fileName()
            : request.relativeFilePath;
        const QString resourceTitle = QStringLiteral("Source %1:%2:%3")
                                          .arg(sourceLabel)
                                          .arg(request.startLine)
                                          .arg(request.startColumn);

        QJsonObject locator{
            {QStringLiteral("type"), QStringLiteral("zeroslack.source")},
            {QStringLiteral("workspaceRoot"), request.workspaceRoot},
            {QStringLiteral("relativeFilePath"), request.relativeFilePath},
            {QStringLiteral("line"), request.startLine},
            {QStringLiteral("column"), request.startColumn},
            {QStringLiteral("endLine"), request.endLine},
            {QStringLiteral("endColumn"), request.endColumn},
            {QStringLiteral("selectedTextHash"), request.selectedTextHash},
            {QStringLiteral("prefixContext"), request.prefixContext},
            {QStringLiteral("suffixContext"), request.suffixContext},
        };
        if (!request.moduleName.isEmpty()) {
            locator.insert(QStringLiteral("moduleName"), request.moduleName);
        }

        Pinloom::Anchor anchor;
        anchor.id = anchorId;
        anchor.name = title;
        anchor.targetApp = QStringLiteral("ZeroSlack");
        anchor.targetFile = request.absoluteFilePath;
        anchor.targetUri = QUrl::fromLocalFile(
            request.absoluteFilePath).toString(QUrl::FullyEncoded);
        anchor.locatorType = QStringLiteral("zeroslack.source");
        anchor.locatorJson = QString::fromUtf8(
            QJsonDocument(locator).toJson(QJsonDocument::Compact));
        anchor.tags = {QStringLiteral("zeroslack"),
                       QStringLiteral("source-anchor")};
        anchor.createdAt = now;
        anchor.updatedAt = now;

        Pinloom::Resource resource;
        resource.id = resourceId;
        resource.kind = Pinloom::ResourceKind::TextSnippet;
        resource.title = resourceTitle;
        resource.location = request.absoluteFilePath;
        resource.tags = anchor.tags;
        if (!request.moduleName.isEmpty())
            resource.tags.append(request.moduleName);
        resource.anchors = {anchor};
        resource.content = request.content;
        resource.updatedAt = now;
        if (!repository.upsertResource(resource)) {
            if (status) {
                *status = repository.lastError().trimmed().isEmpty()
                    ? QStringLiteral("Pinloom could not save the source anchor")
                    : repository.lastError();
            }
            return std::nullopt;
        }

        Pinloom::PinloomOpenTarget target;
        target.resourceId = resource.id;
        target.resourceKind = resource.kind;
        target.title = resource.title;
        target.location = resource.location;
        target.anchor = anchor;
        if (status) *status = QStringLiteral("Source anchor created");
        return Pinloom::entryFromOpenTarget(target);
    };
#ifdef PINLOOM_HAS_SUITEAPP
    Pinloom::PinloomHostBridgeCallbacks suiteCallbacks = hostCallbacks;
#endif
    auto hostBridge = std::make_unique<Pinloom::PinloomHostBridgeServer>(
        Pinloom::PinloomHostBridgeOptions{}, std::move(hostCallbacks), &app);
    if (!hostBridge->start()) {
        window.setRecentError(QStringLiteral("Pinloom host bridge unavailable"),
                              hostBridge->lastError());
    }
#ifdef PINLOOM_HAS_SUITEAPP
    Pinloom::PinloomSuiteIntegration suiteIntegration(
        std::move(suiteCallbacks), &app);
    QString suiteIntegrationError;
    if (!suiteIntegration.start(&suiteIntegrationError)) {
        const QString error = QStringLiteral("Suite App registration failed: %1")
                                  .arg(suiteIntegrationError);
        qWarning().noquote() << error;
        window.setRecentError(QStringLiteral("Suite App unavailable"), error);
    }
#endif

    Pinloom::ClipQuickPickerOptions quickPickerOptions;
    quickPickerOptions.panelOptions.commandDispatcher = &commandDispatcher;
    quickPickerOptions.panelOptions.clipSearchHandler = commandOptions.clipSearchHandler;
    quickPickerOptions.panelOptions.clipInsertionHandler = commandOptions.clipInsertionHandler;
    quickPickerOptions.panelOptions.clipLibraryHandler = commandOptions.clipLibraryHandler;
    quickPickerOptions.panelOptions.statusChangedHandler = commandOptions.statusChangedHandler;
    quickPickerOptions.settings = &appSettingsStore;
    Pinloom::ClipQuickPicker clipQuickPicker(std::move(quickPickerOptions));
    QObject::connect(&clipQuickPicker,
                     &Pinloom::ClipQuickPicker::dismissed,
                     &app,
                     [&clipQuickPicker,
                      &activeClipInsertionWindow,
                      &pendingClipInsertionTarget]() {
                         if (activeClipInsertionWindow == &clipQuickPicker) {
                             activeClipInsertionWindow = nullptr;
                         }
                         pendingClipInsertionTarget.reset();
                     });

    auto *commandPanel = new Pinloom::PinloomCommandPanel(commandOptions, &window);
    window.setCentralWidget(commandPanel);
    auto *floatingController = new Pinloom::CommandFloatingController(window, *commandPanel);
    QObject::connect(floatingController, &Pinloom::CommandFloatingController::captureStarted,
                     &window, [&floatingCaptureActive, rememberForegroundTarget]() {
                         rememberForegroundTarget();
                         floatingCaptureActive = true;
                     });
    QObject::connect(floatingController, &Pinloom::CommandFloatingController::captureFinished,
                     &window, [&floatingCaptureActive]() { floatingCaptureActive = false; });

    const auto handleInstanceActivation =
        [&window,
         commandPanel,
         &clipQuickPicker,
         &activeClipInsertionWindow,
         &pendingClipInsertionTarget,
         &openHostIdentity](const QString &message) {
        const QString trimmedMessage = message.trimmed();
        if (trimmedMessage.compare(QStringLiteral("resident"),
                                   Qt::CaseInsensitive) == 0) {
            return;
        }

        clipQuickPicker.dismiss();
        activeClipInsertionWindow = &window;
        pendingClipInsertionTarget.reset();

        const QUrl candidate(trimmedMessage, QUrl::StrictMode);
        if (candidate.scheme().compare(QStringLiteral("pinloom"),
                                       Qt::CaseInsensitive) == 0) {
            const std::optional<Pinloom::PinloomHostIdentity> identity =
                Pinloom::pinloomHostIdentityFromUri(candidate);
            QString status;
            const bool opened = identity.has_value()
                && openHostIdentity(identity.value(), &status);
            if (!identity.has_value()) {
                status = QStringLiteral("Invalid Pinloom deep link: %1")
                             .arg(trimmedMessage);
            }
            if (status.trimmed().isEmpty()) {
                status = opened
                    ? QStringLiteral("Opened Pinloom deep link")
                    : QStringLiteral("Unable to open Pinloom deep link");
            }
            window.statusBar()->showMessage(status, 8000);
            Pinloom::Ui::showNotice(&window, status);
            if (!opened) {
                window.setRecentError(QStringLiteral("Deep link failed"), status);
                commandPanel->openCommandSearch();
                Pinloom::showCommandPanelForHotkey(window, *commandPanel);
            }
            return;
        }

        commandPanel->openCommandSearch();
        Pinloom::showCommandPanelForHotkey(window, *commandPanel);
    };
    QObject::connect(&instanceGuard,
                     &Pinloom::PinloomSingleInstanceGuard::activationRequested,
                     &app,
                     handleInstanceActivation);
    if (!startupDeepLink.isEmpty()) {
        QTimer::singleShot(0, &app,
                           [handleInstanceActivation, startupDeepLink]() {
                               handleInstanceActivation(startupDeepLink);
                           });
    }

    if (clipHost && clipHost->runtime()) {
        Pinloom::ClipTrayController &trayController = clipHost->runtime()->trayController();
        QObject::connect(&trayController,
                         &Pinloom::ClipTrayController::showClipboardRequested,
                         &window,
                         [&window,
                          commandPanel,
                          &clipQuickPicker,
                          &activeClipInsertionWindow,
                          &pendingClipInsertionTarget]() {
                             clipQuickPicker.dismiss();
                             activeClipInsertionWindow = &window;
                             pendingClipInsertionTarget.reset();
                             commandPanel->openClipSearch();
                             Pinloom::showCommandPanelForHotkey(window, *commandPanel);
                         });
        QObject::connect(&trayController,
                         &Pinloom::ClipTrayController::settingsRequested,
                         &window,
                         [&dispatchWindowCommand]() {
                             dispatchWindowCommand(Pinloom::PinloomCommandId::Settings);
                         });
        QObject::connect(&trayController,
                         &Pinloom::ClipTrayController::diagnosticsRequested,
                         &window,
                         [&dispatchWindowCommand]() {
                             dispatchWindowCommand(Pinloom::PinloomCommandId::Diagnostics);
                         });
        if (!clipHost->start()) {
            window.setRecentError(QStringLiteral("Pinloom Clip could not start"), clipHost->lastError());
            Pinloom::Ui::warning(&window,
                                 QStringLiteral("Pinloom Clip"),
                                 QStringLiteral("Pinloom Clip could not start:\n%1").arg(clipHost->lastError()));
        }
    }

    Pinloom::TextSelectionCaptureService textSelectionCaptureService;
    std::unique_ptr<Pinloom::HyperHotkeyBackend> hyperHotkeyBackend = Pinloom::createHyperHotkeyBackend();
    Pinloom::HyperHotkeyService hyperHotkeyService(hyperHotkeyBackend.get(), &app);
    bool hyperArchiveNeedsMenuCleanup = false;
    std::optional<Pinloom::TextSelectionCaptureResult> pendingHyperArchiveSelection;
    const auto showHyperArchiveDialog = [&](const Pinloom::TextSelectionCaptureResult &selection) {
        Pinloom::ClipCaptureDialog dialog(selection.text,
                                          automaticClipName(selection.text),
                                          availableClipTags(),
                                          &window);
        if (selection.target.hasInsertionPoint) {
            dialog.adjustSize();
            QScreen *screen = QGuiApplication::screenAt(selection.target.insertionPoint);
            if (!screen) {
                screen = QGuiApplication::primaryScreen();
            }
            if (screen) {
                constexpr int Gap = 12;
                const QRect available = screen->availableGeometry();
                const QSize dialogSize = dialog.frameGeometry().size();
                const int maximumX = std::max(available.left(),
                                              available.right() - dialogSize.width() + 1);
                const int maximumY = std::max(available.top(),
                                              available.bottom() - dialogSize.height() + 1);
                const int x = std::clamp(selection.target.insertionPoint.x() + Gap,
                                         available.left(),
                                         maximumX);
                const int y = std::clamp(selection.target.insertionPoint.y() + Gap,
                                         available.top(),
                                         maximumY);
                dialog.move(x, y);
            }
        }
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        QString status;
        const bool archived = archiveSelectedText(selection, dialog.metadata(), &status);
        const QString message = status.trimmed().isEmpty()
            ? (archived ? QStringLiteral("Selected text archived")
                        : QStringLiteral("Unable to archive selected text"))
            : status.trimmed();
        QToolTip::showText(QCursor::pos(), message, nullptr, {}, 2500);
        if (!archived) {
            window.setRecentError(QStringLiteral("F24+S save failed"), message);
        }
    };
    hyperHotkeyService.setActivationHandler([&](Pinloom::HyperHotkeyAction action) {
        hyperArchiveNeedsMenuCleanup = false;
        if (action == Pinloom::HyperHotkeyAction::Save) {
            const Pinloom::TextSelectionCaptureResult selection = textSelectionCaptureService.capture();
            lastForegroundContext = selection.context;
            hyperArchiveNeedsMenuCleanup = true;
            if (!selection.hasSelectedText()) {
                const QString message = QStringLiteral("Select text before pressing F24+S");
                QToolTip::showText(QCursor::pos(), message, nullptr, {}, 2500);
                window.setRecentError(QStringLiteral("F24+S save unavailable"),
                                      selection.diagnostics.trimmed().isEmpty()
                                          ? message
                                          : selection.diagnostics.trimmed());
                return;
            }
            pendingHyperArchiveSelection = selection;
            return;
        }

        if (action != Pinloom::HyperHotkeyAction::Insert) {
            return;
        }

        const Pinloom::ForegroundTextTarget insertionTarget =
            Pinloom::captureForegroundTextTarget();
        if (clipQuickPicker.isVisible()
            && insertionTarget.windowHandle == clipQuickPicker.winId()) {
            clipQuickPicker.raise();
            clipQuickPicker.activateWindow();
            clipQuickPicker.panel()->focusCommand();
            return;
        }

        const QString currentQuery = clipQuickPicker.isVisible()
            ? clipQuickPicker.panel()->commandText()
            : QString();
        pendingClipInsertionTarget = insertionTarget.isValid()
            ? std::optional<Pinloom::ForegroundTextTarget>(insertionTarget)
            : std::nullopt;
        activeClipInsertionWindow = &clipQuickPicker;
        window.hide();
        clipQuickPicker.openForTarget(insertionTarget, currentQuery);
    });
    QObject::connect(&hyperHotkeyService,
                     &Pinloom::HyperHotkeyService::chordReleased,
                     &app,
                     [&]() {
                         if (hyperArchiveNeedsMenuCleanup) {
                             Pinloom::dismissHyperModifierUiState();
                             hyperArchiveNeedsMenuCleanup = false;
                         }
                         if (pendingHyperArchiveSelection.has_value()) {
                             const Pinloom::TextSelectionCaptureResult selection =
                                 pendingHyperArchiveSelection.value();
                             pendingHyperArchiveSelection.reset();
                             QTimer::singleShot(0, &app, [&, selection]() {
                                 showHyperArchiveDialog(selection);
                             });
                         }
                     });

    std::unique_ptr<Pinloom::ClipHotkeyBackend> mainPanelHotkeyBackend = Pinloom::createMainPanelHotkeyBackend();
    Pinloom::ClipHotkeyService mainPanelHotkeyService(Pinloom::defaultMainPanelHotkeyConfig(),
                                                      mainPanelHotkeyBackend.get(),
                                                      &app);
    Pinloom::MainPanelHotkeyController mainPanelHotkeyController(mainPanelHotkeyService,
                                                                 [&window,
                                                                  commandPanel,
                                                                  floatingController,
                                                                  &clipQuickPicker,
                                                                  &activeClipInsertionWindow,
                                                                  rememberForegroundTarget,
                                                                  &pendingClipInsertionTarget]() {
                                                                     if (floatingController->isCapturing()) {
                                                                         floatingController->requestExpansion();
                                                                         return;
                                                                     }
                                                                     rememberForegroundTarget();
                                                                     clipQuickPicker.dismiss();
                                                                     activeClipInsertionWindow = &window;
                                                                     pendingClipInsertionTarget.reset();
                                                                     commandPanel->openCommandSearch();
                                                                     Pinloom::showCommandPanelForHotkey(window,
                                                                                                        *commandPanel);
                                                                 },
                                                                 &app);
    auto refreshResidentStatus = [&]() {
        Pinloom::PinloomResidentStatus status;
        status.running = true;
        status.mainHotkeyRegistered = mainPanelHotkeyService.isRegistered();
        status.mainHotkeyText = mainPanelHotkeyService.displayText();
        status.hyperHotkeyRegistered = hyperHotkeyService.isRegistered();
        status.hyperHotkeyText = hyperHotkeyService.displayText();
        if (clipHost && clipHost->runtime()) {
            status.clipStatus = clipHost->runtime()->trayController().status();
            status.clipCaptureActive = clipHost->runtime()->captureService().isRunning();
            status.clipCapturePaused = clipHost->runtime()->captureService().capturePaused();
            if (!clipHost->runtime()->lastError().trimmed().isEmpty()) {
                status.lastError = clipHost->runtime()->lastError().trimmed();
            } else if (!clipHost->runtime()->trayController().lastError().trimmed().isEmpty()) {
                status.lastError = clipHost->runtime()->trayController().lastError().trimmed();
            }
        }
        if (!mainPanelHotkeyService.lastError().trimmed().isEmpty()) {
            status.lastError = mainPanelHotkeyService.lastError().trimmed();
        }
        if (!hyperHotkeyService.lastError().trimmed().isEmpty()) {
            status.lastError = hyperHotkeyService.lastError().trimmed();
        }
        if (!window.recentError().trimmed().isEmpty()) {
            status.lastError = window.recentError().trimmed();
        }
        window.setResidentStatus(status);
    };
    QObject::connect(&mainPanelHotkeyService,
                     &Pinloom::ClipHotkeyService::registeredChanged,
                     &window,
                     [&](bool) {
                         refreshResidentStatus();
                     });
    QObject::connect(&hyperHotkeyService,
                     &Pinloom::HyperHotkeyService::registeredChanged,
                     &window,
                     [&](bool) {
                         refreshResidentStatus();
                     });
    QObject::connect(&hyperHotkeyService,
                     &Pinloom::HyperHotkeyService::errorChanged,
                     &window,
                     [&](const QString &) {
                         refreshResidentStatus();
                     });
    QObject::connect(&mainPanelHotkeyService,
                     &Pinloom::ClipHotkeyService::errorChanged,
                     &window,
                     [&](const QString &) {
                         refreshResidentStatus();
                     });
    if (clipHost && clipHost->runtime()) {
        QObject::connect(clipHost->runtime(),
                         &Pinloom::ClipResidentRuntime::runningChanged,
                         &window,
                         [&](bool) {
                             refreshResidentStatus();
                         });
        QObject::connect(clipHost->runtime(),
                         &Pinloom::ClipResidentRuntime::errorChanged,
                         &window,
                         [&](const QString &error) {
                             if (!error.trimmed().isEmpty()) {
                                 window.setRecentError(error);
                             }
                             refreshResidentStatus();
                         });
        QObject::connect(&clipHost->runtime()->captureService(),
                         &Pinloom::ClipboardCaptureService::runningChanged,
                         &window,
                         [&](bool) {
                             refreshResidentStatus();
                         });
        QObject::connect(&clipHost->runtime()->captureService(),
                         &Pinloom::ClipboardCaptureService::capturePausedChanged,
                         &window,
                         [&](bool) {
                             refreshResidentStatus();
                         });
        QObject::connect(&clipHost->runtime()->trayController(),
                         &Pinloom::ClipTrayController::statusChanged,
                         &window,
                         [&](const QString &) {
                             refreshResidentStatus();
                         });
        QObject::connect(&clipHost->runtime()->trayController(),
                         &Pinloom::ClipTrayController::errorChanged,
                         &window,
                         [&](const QString &error) {
                             if (!error.trimmed().isEmpty()) {
                                 window.setRecentError(error);
                             }
                             refreshResidentStatus();
                         });
    }
    refreshResidentStatus();
    if (!startHidden) {
        Pinloom::showCommandPanelForHotkey(window, *commandPanel);
    }

    if (!mainPanelHotkeyService.start()) {
        const QString error = QStringLiteral("Pinloom main hotkey %1 could not be registered:\n%2\n\n"
                                             "Shift+Space may be reserved by another application.")
                                  .arg(mainPanelHotkeyService.displayText(), mainPanelHotkeyService.lastError());
        qWarning().noquote() << error;
        window.setRecentError(QStringLiteral("Pinloom main hotkey could not be registered"), error);
        refreshResidentStatus();
        Pinloom::Ui::warning(&window, QStringLiteral("Pinloom"), error);
    } else {
        refreshResidentStatus();
    }

    if (!hyperHotkeyService.start()) {
        const QString error = QStringLiteral("Pinloom %1 could not start:\n%2")
                                  .arg(hyperHotkeyService.displayText(), hyperHotkeyService.lastError());
        qWarning().noquote() << error;
        window.setRecentError(QStringLiteral("Pinloom Hyper hotkey could not start"), error);
    }
    refreshResidentStatus();

    return app.exec();
}
