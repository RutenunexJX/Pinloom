#include "pinloom/core/SqliteLibraryRepository.h"
#include "pinloom/clip/ClipboardCaptureService.h"
#include "pinloom/clip/HyperHotkeyService.h"
#include "pinloom/clip/ObsidianClipStore.h"
#include "pinloom/clip/ClipTrayController.h"
#include "pinloom/core/ExplorerFileSelection.h"
#include "pinloom/core/InboxFileCapture.h"
#include "pinloom/core/SumatraPdfForegroundCapture.h"
#include "pinloom/core/SumatraPdfCommand.h"
#include "pinloom/core/SumatraPdfOpenProxy.h"
#include "pinloom/core/TextSelectionCapture.h"
#include "pinloom/widgets/ClipResidentHost.h"
#include "pinloom/widgets/ClipResidentRuntime.h"
#include "pinloom/widgets/MainPanelHotkey.h"
#include "pinloom/widgets/PinloomCommandPanel.h"
#include "pinloom/widgets/PinloomMainWindow.h"
#include "pinloom/widgets/PinloomPanel.h"
#include "pinloom/widgets/PinloomSettingsDialog.h"
#include "pinloom/widgets/PinloomSingleInstance.h"
#include "pinloom/widgets/SumatraPdfRegionCaptureOverlay.h"

#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QToolTip>
#include <algorithm>
#include <memory>
#include <optional>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Pinloom"));
    QApplication::setOrganizationName(QStringLiteral("Pinloom"));
    app.setQuitOnLastWindowClosed(false);

    const QStringList startupArguments = QCoreApplication::arguments();
    const bool startHidden = startupArguments.contains(QStringLiteral("--hidden"), Qt::CaseInsensitive);

    Pinloom::PinloomSingleInstanceOptions instanceOptions;
    instanceOptions.serverName = Pinloom::defaultPinloomSingleInstanceServerName();
    instanceOptions.activationTimeoutMs = 300;
    instanceOptions.activationMessage = startHidden
        ? QStringLiteral("resident")
        : QStringLiteral("activate");
    Pinloom::PinloomSingleInstanceGuard instanceGuard(instanceOptions, &app);
    const Pinloom::PinloomSingleInstanceStartResult instanceStart = instanceGuard.start();
    if (instanceStart.isSecondary()) {
        return 0;
    }
    if (!instanceStart.succeeded()) {
        QMessageBox::warning(nullptr,
                             QStringLiteral("Pinloom"),
                             QStringLiteral("Pinloom could not start its single-instance listener:\n%1")
                                 .arg(instanceStart.error));
    }

    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        appDataPath = QDir::home().filePath(QStringLiteral(".pinloom"));
    }
    if (!QDir().mkpath(appDataPath)) {
        QMessageBox::critical(nullptr, QStringLiteral("Pinloom"), QStringLiteral("Unable to create app data directory."));
        return 1;
    }

    QSettings appSettingsStore;
    Pinloom::PinloomAppSettings runtimeSettings =
        Pinloom::loadPinloomAppSettings(appSettingsStore, appDataPath);

    Pinloom::SqliteLibraryRepository repository;
    const QString databasePath = QDir(appDataPath).filePath(QStringLiteral("pinloom.sqlite3"));
    if (!repository.open(databasePath) || !repository.initialize()) {
        QMessageBox::critical(nullptr,
                              QStringLiteral("Pinloom"),
                              QStringLiteral("Unable to initialize Pinloom database:\n%1").arg(repository.lastError()));
        return 1;
    }

    Pinloom::ClipResidentRuntimeFactory clipFactory;
    Pinloom::ClipResidentRuntimeFactoryOptions clipOptions;
    clipOptions.repositoryKind = Pinloom::ClipResidentRepositoryKind::SQLite;
    clipOptions.sqliteDatabasePath = QDir(appDataPath).filePath(QStringLiteral("pinloom_clip.sqlite3"));
    clipOptions.runtimeOptions.insertionOptions.restoreOriginalClipboardOnSuccess =
        runtimeSettings.clipRestoreOriginalClipboardOnInsert;

    Pinloom::ClipResidentHostResult clipHostResult = clipFactory.createDefaultPlatformHost(clipOptions);
    std::unique_ptr<Pinloom::ClipResidentHost> clipHost;
    if (clipHostResult.succeeded()) {
        clipHost = std::move(clipHostResult.host);
        QObject::connect(clipHost.get(), &Pinloom::ClipResidentHost::quitRequested, &app, &QApplication::quit);
        if (clipHost->runtime()) {
            clipHost->runtime()->captureService().setPolicy(runtimeSettings.clipCapturePolicy());
        }
    } else {
        QMessageBox::warning(nullptr,
                             QStringLiteral("Pinloom Clip"),
                             QStringLiteral("Pinloom Clip could not initialize:\n%1").arg(clipHostResult.error));
    }

    Pinloom::PinloomMainWindow window;
    window.setWindowTitle(QStringLiteral("Pinloom"));
    window.setMinimumWidth(560);
    window.resize(760, 72);

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

    const auto upsertSavedClip = [&clipHost](const Pinloom::Clip &clip, QString *error) {
        bool saved = false;
        QString repositoryError;
        if (clipHost && clipHost->sqliteRepository()) {
            saved = clipHost->sqliteRepository()->upsertSavedClip(clip);
            repositoryError = clipHost->sqliteRepository()->lastError();
        } else if (clipHost && clipHost->inMemoryRepository()) {
            saved = clipHost->inMemoryRepository()->upsertSavedClip(clip);
        }
        if (!saved && error) {
            *error = repositoryError.trimmed().isEmpty()
                ? QStringLiteral("Unable to update Saved Clip index")
                : repositoryError.trimmed();
        } else if (saved && error) {
            error->clear();
        }
        return saved;
    };
    const auto persistSavedClip = [&obsidianClipStore, &upsertSavedClip](Pinloom::Clip clip,
                                                                        QString *error) {
        clip.state = Pinloom::ClipState::Saved;
        clip.updatedAt = QDateTime::currentDateTimeUtc();
        clip.expiresAt = {};
        if (obsidianClipStore.config().isEnabled()) {
            clip.sourceApp = Pinloom::obsidianClipSourceApp();
            const Pinloom::ObsidianClipWriteResult written = obsidianClipStore.writeClip(clip);
            if (!written.succeeded()) {
                if (error) {
                    *error = written.error;
                }
                return false;
            }
        }
        return upsertSavedClip(clip, error);
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
    const auto archiveSelectedText = [&clipHost,
                                      &runtimeSettings,
                                      &obsidianClipStore,
                                      &persistSavedClip,
                                      &automaticClipName](const Pinloom::TextSelectionCaptureResult &selection,
                                                          QString *status) {
        if (!clipHost || !clipHost->runtime()) {
            if (status) {
                *status = QStringLiteral("Pinloom Clip is not running");
            }
            return false;
        }
        if (!obsidianClipStore.config().isEnabled()) {
            if (status) {
                *status = QStringLiteral("Configure an Obsidian Vault before archiving selected text");
            }
            return false;
        }
        if (!selection.hasSelectedText()) {
            if (status) {
                *status = QStringLiteral("No selected text to archive");
            }
            return false;
        }

        std::optional<Pinloom::Clip> matchingClip;
        const QList<Pinloom::Clip> existingClips = clipHost->sqliteRepository()
            ? clipHost->sqliteRepository()->clips()
            : clipHost->inMemoryRepository()->clips();
        for (const Pinloom::Clip &candidate : existingClips) {
            if (candidate.text != selection.text) {
                continue;
            }
            if (!matchingClip.has_value()
                || (candidate.state == Pinloom::ClipState::Saved
                    && matchingClip->state != Pinloom::ClipState::Saved)) {
                matchingClip = candidate;
            }
        }

        if (matchingClip.has_value()
            && matchingClip->state == Pinloom::ClipState::Saved
            && matchingClip->sourceApp == Pinloom::obsidianClipSourceApp()) {
            if (status) {
                *status = QStringLiteral("Already archived: %1")
                              .arg(matchingClip->name.trimmed().isEmpty()
                                       ? matchingClip->preview
                                       : matchingClip->name);
            }
            return true;
        }

        Pinloom::Clip clip;
        if (matchingClip.has_value()) {
            clip = matchingClip.value();
        } else {
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
            if (!captured.captured()) {
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
            clip = captured.clip.value();
        }

        clip.name = automaticClipName(selection.text);
        clip.aliases.clear();
        clip.tags.clear();
        clip.pinned = false;
        clip.sourceApp = selection.context.processName;
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
    const auto refreshClipFromObsidian = [&clipHost,
                                          &obsidianClipStore,
                                          &upsertSavedClip](const QString &clipId,
                                                            QString *error) {
        if (!obsidianClipStore.config().isEnabled()) {
            return true;
        }

        const std::optional<Pinloom::Clip> indexed = clipHost && clipHost->runtime()
            ? clipHost->runtime()->searchService().findClip(clipId)
            : std::nullopt;
        QString sourceError;
        const std::optional<Pinloom::ObsidianClipDocument> source =
            obsidianClipStore.findClip(clipId, &sourceError);
        if (!source.has_value()) {
            if (!sourceError.trimmed().isEmpty()
                || (indexed.has_value() && indexed->sourceApp == Pinloom::obsidianClipSourceApp())) {
                if (error) {
                    *error = sourceError.trimmed().isEmpty()
                        ? QStringLiteral("Obsidian Clip note was not found")
                        : sourceError.trimmed();
                }
                return false;
            }
            return true;
        }

        Pinloom::Clip current = source->clip;
        if (indexed.has_value()) {
            current.usedAt = indexed->usedAt;
        }
        return upsertSavedClip(current, error);
    };

    std::optional<Pinloom::ForegroundTextTarget> pendingClipInsertionTarget;
    QMainWindow *commandWindowForClipInsertion = nullptr;

    Pinloom::PinloomPanelOptions panelOptions;
    panelOptions.applicationLaunchSettings.sumatraPdfExecutablePath =
        runtimeSettings.sumatraPdfExecutablePath.trimmed();
    panelOptions.sumatraPdfExecutablePathProvider = [&runtimeSettings]() {
        const QString configured = runtimeSettings.sumatraPdfExecutablePath.trimmed();
        return configured.isEmpty() ? Pinloom::resolveSumatraPdfExecutablePath() : configured;
    };
    panelOptions.statusChangedHandler = [&window](const QString &status) {
        const QString lower = status.toLower();
        if (lower.contains(QStringLiteral("unable"))
            || lower.contains(QStringLiteral("failed"))
            || lower.contains(QStringLiteral("error"))
            || lower.contains(QStringLiteral("missing"))
            || lower.contains(QStringLiteral("could not"))) {
            window.setRecentError(status);
        }
    };
    panelOptions.clipSearchHandler =
        [&clipHost](const QString &query, const Pinloom::ClipSearchOptions &options) -> QList<Pinloom::ClipSearchResult> {
        if (!clipHost || !clipHost->runtime()) {
            return {};
        }
        return clipHost->runtime()->searchService().search(query, options);
    };
    panelOptions.clipInsertionHandler = [&app,
                                         &clipHost,
                                         &refreshClipFromObsidian,
                                         &pendingClipInsertionTarget,
                                         &commandWindowForClipInsertion](const QString &clipId, QString *error) {
        if (!clipHost || !clipHost->runtime()) {
            if (error) {
                *error = QStringLiteral("Pinloom Clip is not running");
            }
            return false;
        }
        if (!refreshClipFromObsidian(clipId, error)) {
            return false;
        }

        const std::optional<Pinloom::ForegroundTextTarget> insertionTarget = pendingClipInsertionTarget;
        pendingClipInsertionTarget.reset();
        bool commandWindowWasHidden = false;
        if (insertionTarget.has_value()) {
            if (commandWindowForClipInsertion && commandWindowForClipInsertion->isVisible()) {
                commandWindowForClipInsertion->hide();
                commandWindowWasHidden = true;
                app.processEvents();
            }
            QString restoreError;
            if (!Pinloom::restoreForegroundTextTarget(insertionTarget.value(), &restoreError)) {
                if (commandWindowWasHidden && commandWindowForClipInsertion) {
                    commandWindowForClipInsertion->show();
                    commandWindowForClipInsertion->raise();
                    commandWindowForClipInsertion->activateWindow();
                }
                if (error) {
                    *error = restoreError;
                }
                return false;
            }
            QThread::msleep(60);
        }
        const Pinloom::ClipInsertionResult result = clipHost->runtime()->insertionService().insertClip(clipId);
        if (!result.inserted() && error) {
            *error = result.error;
        }
        if (!result.inserted() && commandWindowWasHidden && commandWindowForClipInsertion) {
            commandWindowForClipInsertion->show();
            commandWindowForClipInsertion->raise();
            commandWindowForClipInsertion->activateWindow();
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
    QMainWindow *commandWindowForForegroundCapture = nullptr;
    const auto lookupSumatraPdfTitlePath =
        [&appSettingsStore](const QString &documentTitle, const QString &normalizedTitleKey)
            -> std::optional<QString> {
        return Pinloom::lookupRememberedSumatraPdfDocumentPath(appSettingsStore,
                                                               documentTitle,
                                                               normalizedTitleKey);
    };
    const auto rememberSumatraPdfTitlePath =
        [&appSettingsStore](const QString &documentTitle, const QString &filePath) {
        Pinloom::rememberSumatraPdfDocumentTitlePath(appSettingsStore, documentTitle, filePath);
    };
    Pinloom::SumatraPdfForegroundCaptureProvider foregroundPdfCaptureProvider(
        repository,
        Pinloom::captureSumatraPdfViewState,
        lookupSumatraPdfTitlePath);

    panelOptions.foregroundPdfAnchorCaptureRequestProvider =
        [&foregroundPdfCaptureProvider,
         &lastForegroundContext,
         &commandWindowForForegroundCapture,
         rememberSumatraPdfTitlePath](QString *status)
        -> std::optional<Pinloom::ManualPdfAnchorCreationRequest> {
        const bool useLastForegroundContext =
            commandWindowForForegroundCapture
            && commandWindowForForegroundCapture->isVisible()
            && lastForegroundContext.isValid();
        const Pinloom::ForegroundAppWindowContext context =
            useLastForegroundContext
                ? lastForegroundContext
                : Pinloom::currentForegroundAppWindowContext();
        const Pinloom::SumatraPdfForegroundCaptureResult result =
            foregroundPdfCaptureProvider.capture(context);
        if (status) {
            *status = result.status;
        }
        const auto captureRegion =
            [&](Pinloom::ManualPdfAnchorCreationRequest request)
                -> std::optional<Pinloom::ManualPdfAnchorCreationRequest> {
            const bool restoreCommandWindow = commandWindowForForegroundCapture
                && commandWindowForForegroundCapture->isVisible();
            if (restoreCommandWindow) {
                commandWindowForForegroundCapture->hide();
                QApplication::processEvents();
            }

            const Pinloom::SumatraPdfRegionCaptureResult region =
                Pinloom::captureSumatraPdfRegion(context.windowHandle);

            if (restoreCommandWindow) {
                commandWindowForForegroundCapture->show();
                commandWindowForForegroundCapture->raise();
                commandWindowForForegroundCapture->activateWindow();
            }

            if (!region.success()) {
                if (status) {
                    *status = region.canceled
                        ? QStringLiteral("PDF region capture canceled")
                        : (region.region.error.trimmed().isEmpty()
                               ? QStringLiteral("Unable to capture PDF region")
                               : region.region.error.trimmed());
                }
                return std::nullopt;
            }

            request.locatorType = QStringLiteral("sumatrapdf.rect");
            request.page = region.region.page;
            request.rect = region.region.rect;
            request.source = QStringLiteral("foreground-sumatrapdf-region");
            if (status) {
                *status = QStringLiteral("Captured SumatraPDF region on page %1")
                              .arg(request.page);
            }
            return request;
        };
        if (!result.success() && result.needsFileConfirmation) {
            QWidget *parent = commandWindowForForegroundCapture;
            const QString selectedFile = QFileDialog::getOpenFileName(
                parent,
                QStringLiteral("Confirm PDF File"),
                QString(),
                QStringLiteral("PDF files (*.pdf);;All files (*)"));
            if (selectedFile.trimmed().isEmpty()) {
                if (status) {
                    *status = QStringLiteral("PDF selection canceled for PDF document \"%1\"")
                                  .arg(result.documentTitle);
                }
                return std::nullopt;
            }

            const Pinloom::SumatraPdfForegroundCaptureResult confirmed =
                Pinloom::sumatraPdfForegroundCaptureResultForConfirmedPdfFile(
                    result.documentTitle,
                    selectedFile,
                    result.viewState);
            if (!confirmed.success()) {
                if (status) {
                    *status = confirmed.status;
                }
                return std::nullopt;
            }

            rememberSumatraPdfTitlePath(result.documentTitle, confirmed.request.file);
            return captureRegion(confirmed.request);
        }
        if (!result.success()) {
            return std::nullopt;
        }
        return captureRegion(result.request);
    };
    auto *panel = new Pinloom::PinloomPanel(repository, panelOptions, &window);
    window.setCentralWidget(panel);

    auto showSettingsDialog = [&]() {
        Pinloom::PinloomSettingsDialog dialog(runtimeSettings, &window);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        runtimeSettings = dialog.settings();
        runtimeSettings.dataDirectory = appDataPath;
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
            Pinloom::ClipInsertionOptions insertionOptions =
                clipHost->runtime()->insertionService().options();
            insertionOptions.restoreOriginalClipboardOnSuccess =
                runtimeSettings.clipRestoreOriginalClipboardOnInsert;
            clipHost->runtime()->insertionService().setOptions(insertionOptions);
        }
    };
    QObject::connect(&window, &Pinloom::PinloomMainWindow::settingsRequested, &window, showSettingsDialog);
    QObject::connect(&window, &Pinloom::PinloomMainWindow::quitRequested, &app, &QApplication::quit);

    QMainWindow commandWindow;
    commandWindowForForegroundCapture = &commandWindow;
    commandWindowForClipInsertion = &commandWindow;
    commandWindow.setWindowTitle(QStringLiteral("Pinloom Command"));
    commandWindow.setMinimumWidth(560);
    commandWindow.resize(760, 300);

    Pinloom::PinloomCommandPanelOptions commandOptions;
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
    commandOptions.unifiedEntrySearchHandler = [panel](const QString &query) {
        panel->setSearchText(query);
        return panel->currentEntries();
    };
    commandOptions.deletedEntrySearchHandler = [panel](const QString &query) {
        QList<Pinloom::PinloomEntry> deletedEntries;
        for (const Pinloom::PinloomEntry &entry : panel->searchEntries(query, true)) {
            if (entry.deleted) {
                deletedEntries.append(entry);
            }
        }
        return deletedEntries;
    };
    const auto activateOpenTargetFromPanel =
        [panel](const Pinloom::PinloomOpenTarget &target, QString *status) {
        const bool activated = panel->activateOpenTarget(target);
        if (status) {
            *status = panel->statusText();
        }
        return activated;
    };
    commandOptions.anchorJumpHandler = activateOpenTargetFromPanel;
    commandOptions.resourceOpenHandler = activateOpenTargetFromPanel;
    commandOptions.clipSearchHandler = panelOptions.clipSearchHandler;
    commandOptions.clipInsertionHandler = panelOptions.clipInsertionHandler;
    commandOptions.clipSaveHandler = clipSaveHandler;
    commandOptions.anchorCaptureHandler = [panel](QString *status) {
        const bool captured = panel->captureForegroundPdfAnchor();
        if (status) {
            *status = panel->statusText();
        }
        return captured;
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
    commandOptions.inboxSaveHandler = [&repository](const Pinloom::InboxFileSaveRequest &request, QString *status) {
        const Pinloom::InboxFileSaveResult result = Pinloom::saveInboxFile(repository, request);
        if (status) {
            *status = result.status;
        }
        return result.success();
    };
    commandOptions.searchWindowHandler = [&window, panel](const QString &query) {
        Pinloom::showMainPanelForHotkey(window, *panel);
        const QString trimmedQuery = query.trimmed();
        if (!trimmedQuery.isEmpty()) {
            panel->setSearchText(trimmedQuery);
        }
    };
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
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty() && !values.contains(trimmed, Qt::CaseInsensitive)) {
            values.append(trimmed);
        }
    };
    const auto valuesFromCommaText = [&appendUniqueValue, &cleanTag](const QString &text, bool tags) {
        QStringList values;
        for (const QString &value : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            appendUniqueValue(values, tags ? cleanTag(value) : value);
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
        QDialog dialog(parent);
        dialog.setWindowTitle(windowTitle);
        auto *form = new QFormLayout(&dialog);
        auto *nameEdit = new QLineEdit(name, &dialog);
        auto *aliasesEdit = new QLineEdit(aliases.join(QStringLiteral(", ")), &dialog);
        auto *tagsEdit = new QLineEdit(tags.join(QStringLiteral(", ")), &dialog);
        auto *pinnedCheck = new QCheckBox(QStringLiteral("Pinned"), &dialog);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
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
        edit.name = nameEdit->text().trimmed();
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
        const QString text = QInputDialog::getText(parent,
                                                   windowTitle,
                                                   label,
                                                   QLineEdit::Normal,
                                                   values.join(QStringLiteral(", ")),
                                                   &accepted);
        if (!accepted) {
            return std::nullopt;
        }
        return valuesFromCommaText(text, tags);
    };
    const auto findClip = [&clipHost](const QString &clipId) -> std::optional<Pinloom::Clip> {
        if (!clipHost || !clipHost->runtime()) {
            return std::nullopt;
        }
        return clipHost->runtime()->searchService().findClip(clipId);
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
    const auto selectPanelTarget = [panel](const Pinloom::PinloomOpenTarget &target) {
        if (target.resultRow >= 0 && panel->selectResultAt(target.resultRow)) {
            const Pinloom::PinloomOpenTarget current = panel->currentOpenTarget();
            if (!target.clipId.isEmpty()) {
                return current.clipId == target.clipId;
            }
            if (current.resourceId == target.resourceId) {
                const QString currentAnchorId = current.anchor.has_value() ? current.anchor->id : QString();
                const QString targetAnchorId = target.anchor.has_value() ? target.anchor->id : QString();
                return targetAnchorId.isEmpty() || currentAnchorId == targetAnchorId;
            }
        }
        return !target.resourceId.isEmpty() && panel->selectResultResource(target.resourceId);
    };
    const auto executeEntryAction =
        [&repository,
         &clipHost,
         panel,
         &targetTitle,
         &cleanTag,
         &appendUniqueValue,
         &promptMetadataEdit,
         &promptValueListEdit,
         &findClip,
         &saveClipMetadata,
         &obsidianClipStore,
         &selectPanelTarget,
         &activateOpenTargetFromPanel](QWidget *parent,
                                        const Pinloom::PinloomEntry &entry,
                                        const Pinloom::PinloomCommandResultAction &action,
                                        QString *status) {
        const Pinloom::PinloomOpenTarget target = Pinloom::openTargetFromEntry(entry);
        const QString actionId = action.id.trimmed();
        if (actionId == QLatin1String("primary")) {
            return activateOpenTargetFromPanel(target, status);
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
                if (!clipHost || !clipHost->runtime()
                    || !clipHost->runtime()->searchService().restoreClip(clip->id)) {
                    if (status) {
                        const QString error = clipHost && clipHost->runtime()
                            ? clipHost->runtime()->searchService().lastError().trimmed()
                            : QString();
                        *status = error.isEmpty() ? QStringLiteral("Unable to restore Saved Clip") : error;
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
                const QMessageBox::StandardButton choice = QMessageBox::question(
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
                if (!clipHost || !clipHost->runtime()
                    || !clipHost->runtime()->searchService().softDeleteClip(clip->id)) {
                    if (status) {
                        const QString error = clipHost && clipHost->runtime()
                            ? clipHost->runtime()->searchService().lastError().trimmed()
                            : QString();
                        *status = error.isEmpty() ? QStringLiteral("Unable to remove Saved Clip") : error;
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
                const QString name = QInputDialog::getText(parent,
                                                           QStringLiteral("Rename Clip"),
                                                           QStringLiteral("Name"),
                                                           QLineEdit::Normal,
                                                           clip->name,
                                                           &accepted).trimmed();
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
                const QString alias = QInputDialog::getText(parent,
                                                            QStringLiteral("Add Alias"),
                                                            QStringLiteral("Alias"),
                                                            QLineEdit::Normal,
                                                            QString(),
                                                            &accepted).trimmed();
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
                const QString tag = cleanTag(QInputDialog::getText(parent,
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
                    *status = target.anchor.has_value()
                        ? QStringLiteral("Unable to restore anchor")
                        : QStringLiteral("Unable to restore resource");
                }
                return false;
            }
            panel->setSearchText(targetTitle(target));
            if (status) {
                *status = target.anchor.has_value()
                    ? QStringLiteral("Restored anchor \"%1\"").arg(targetTitle(target))
                    : Pinloom::isInboxResourceId(target.resourceId)
                          ? QStringLiteral("Restored Inbox file \"%1\"").arg(targetTitle(target))
                          : QStringLiteral("Restored resource \"%1\"").arg(targetTitle(target));
            }
            return true;
        }

        if (!selectPanelTarget(target)) {
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
            const QMessageBox::StandardButton choice = QMessageBox::question(parent, title, body);
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

            panel->setSearchText(panel->searchText());
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
            const QString name = QInputDialog::getText(parent,
                                                       target.anchor.has_value()
                                                           ? QStringLiteral("Rename Anchor")
                                                           : QStringLiteral("Rename Resource"),
                                                       QStringLiteral("Name"),
                                                       QLineEdit::Normal,
                                                       targetTitle(target),
                                                       &accepted).trimmed();
            if (!accepted) {
                if (status) {
                    *status = QStringLiteral("Rename canceled");
                }
                return false;
            }
            if (name.isEmpty()) {
                if (status) {
                    *status = QStringLiteral("Name is required");
                }
                return false;
            }
            if (target.anchor.has_value()) {
                const bool updated = panel->editSelectedAnchor(name, target.anchor->aliases, target.anchor->tags);
                if (status) {
                    *status = panel->statusText();
                }
                return updated;
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
                    *status = QStringLiteral("Unable to rename resource");
                }
                return false;
            }
            panel->setSearchText(panel->searchText());
            panel->selectResultResource(resource->id);
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
                const bool updated = panel->editSelectedAnchor(targetTitle(target), aliases.value(), target.anchor->tags);
                if (status) {
                    *status = panel->statusText();
                }
                return updated;
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
                    *status = QStringLiteral("Unable to update resource aliases");
                }
                return false;
            }
            panel->setSearchText(panel->searchText());
            panel->selectResultResource(resource->id);
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
                const bool updated = panel->editSelectedAnchor(targetTitle(target), target.anchor->aliases, tags.value());
                if (status) {
                    *status = panel->statusText();
                }
                return updated;
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
                    *status = QStringLiteral("Unable to update resource tags");
                }
                return false;
            }
            panel->setSearchText(panel->searchText());
            panel->selectResultResource(resource->id);
            if (status) {
                *status = QStringLiteral("Updated resource tags");
            }
            return true;
        }

        if (actionId == QLatin1String("pin") || actionId == QLatin1String("unpin")) {
            const bool pinned = actionId == QLatin1String("pin");
            const bool updated = target.anchor.has_value()
                ? panel->setSelectedAnchorPinned(pinned)
                : panel->setResourcePinnedById(target.resourceId, pinned);
            if (status) {
                *status = panel->statusText();
            }
            return updated;
        }
        if (actionId == QLatin1String("add_alias")) {
            bool accepted = false;
            const QString alias = QInputDialog::getText(parent,
                                                        QStringLiteral("Add Alias"),
                                                        QStringLiteral("Alias"),
                                                        QLineEdit::Normal,
                                                        QString(),
                                                        &accepted).trimmed();
            if (!accepted) {
                if (status) {
                    *status = QStringLiteral("Add alias canceled");
                }
                return false;
            }
            const bool updated = panel->addAliasToSelectedTarget(alias);
            if (status) {
                *status = panel->statusText();
            }
            return updated;
        }
        if (actionId == QLatin1String("add_tag")) {
            bool accepted = false;
            const QString tag = cleanTag(QInputDialog::getText(parent,
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
            const bool updated = panel->addTagToSelectedTarget(tag);
            if (status) {
                *status = panel->statusText();
            }
            return updated;
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
                if (!panel->editSelectedAnchor(edit->name, edit->aliases, edit->tags)) {
                    if (status) {
                        *status = panel->statusText();
                    }
                    return false;
                }
                if (target.anchor->pinned != edit->pinned
                    && !panel->setSelectedAnchorPinned(edit->pinned)) {
                    if (status) {
                        *status = panel->statusText();
                    }
                    return false;
                }
                if (status) {
                    *status = panel->statusText();
                }
                return true;
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
            resource->title = edit->name;
            resource->aliases = edit->aliases;
            resource->tags = edit->tags;
            resource->updatedAt = QDateTime::currentDateTimeUtc();
            if (!repository.upsertResource(resource.value())) {
                if (status) {
                    *status = QStringLiteral("Unable to update resource");
                }
                return false;
            }
            if (pinned != edit->pinned && !repository.setResourcePinned(resource->id, edit->pinned)) {
                if (status) {
                    *status = QStringLiteral("Unable to update pinned resource");
                }
                return false;
            }
            panel->setSearchText(panel->searchText());
            panel->selectResultResource(resource->id);
            if (status) {
                *status = QStringLiteral("Updated resource \"%1\"").arg(resource->title);
            }
            return true;
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
        Pinloom::PinloomCommandActionResult result;
        QString status;
        result.success = executeEntryAction(parent, enriched, action, &status);
        result.message = status.trimmed().isEmpty()
            ? (result.success
                   ? QStringLiteral("Completed action \"%1\"").arg(action.label)
                   : QStringLiteral("Unable to run action \"%1\"").arg(action.label))
            : status.trimmed();
        if (!result.success) {
            result.diagnostics = entryActionDiagnostics(enriched, action);
            result.nextUiHint = QStringLiteral("keep actions open");
        } else if (action.id == QLatin1String("remove") || action.id == QLatin1String("restore")) {
            result.nextUiHint = QStringLiteral("refresh ordinary search");
        }
        return result;
    };

    auto *commandPanel = new Pinloom::PinloomCommandPanel(commandOptions, &commandWindow);
    commandWindow.setCentralWidget(commandPanel);

    QObject::connect(&instanceGuard,
                     &Pinloom::PinloomSingleInstanceGuard::activationRequested,
                     &app,
                     [&appSettingsStore, &commandWindow, commandPanel](const QString &message) {
                         const std::optional<QString> openedPdf =
                             Pinloom::sumatraPdfOpenFileFromPinloomMessage(message);
                         if (openedPdf.has_value()) {
                             Pinloom::rememberSumatraPdfOpenedFile(appSettingsStore, openedPdf.value());
                             return;
                         }

                         if (message.trimmed().compare(QStringLiteral("resident"), Qt::CaseInsensitive) == 0) {
                             return;
                         }

                         Pinloom::showCommandPanelForHotkey(commandWindow, *commandPanel);
                     });

    if (clipHost && clipHost->runtime()) {
        Pinloom::ClipTrayController &trayController = clipHost->runtime()->trayController();
        QObject::connect(&trayController,
                         &Pinloom::ClipTrayController::showClipboardRequested,
                         &commandWindow,
                         [&commandWindow, commandPanel]() {
                             Pinloom::showCommandPanelForHotkey(commandWindow, *commandPanel);
                             commandPanel->openClipSearch();
                         });
        QObject::connect(&trayController,
                         &Pinloom::ClipTrayController::settingsRequested,
                         &window,
                         [&window, &showSettingsDialog]() {
                             window.show();
                             window.raise();
                             window.activateWindow();
                             showSettingsDialog();
                         });
        QObject::connect(&trayController,
                         &Pinloom::ClipTrayController::diagnosticsRequested,
                         &window,
                         [&window]() {
                             window.show();
                             window.raise();
                             window.activateWindow();
                             window.showDiagnosticsDialog();
                         });
        if (!clipHost->start()) {
            window.setRecentError(QStringLiteral("Pinloom Clip could not start"), clipHost->lastError());
            QMessageBox::warning(&window,
                                 QStringLiteral("Pinloom Clip"),
                                 QStringLiteral("Pinloom Clip could not start:\n%1").arg(clipHost->lastError()));
        }
    }

    Pinloom::TextSelectionCaptureService textSelectionCaptureService;
    std::unique_ptr<Pinloom::HyperHotkeyBackend> hyperHotkeyBackend = Pinloom::createHyperHotkeyBackend();
    Pinloom::HyperHotkeyService hyperHotkeyService(hyperHotkeyBackend.get(), &app);
    bool hyperArchiveNeedsMenuCleanup = false;
    hyperHotkeyService.setActivationHandler([&]() {
        hyperArchiveNeedsMenuCleanup = false;
        const Pinloom::TextSelectionCaptureResult selection = textSelectionCaptureService.capture();
        lastForegroundContext = selection.context;
        if (Pinloom::contextualClipIntent(selection) == Pinloom::ContextualClipIntent::ArchiveSelection) {
            hyperArchiveNeedsMenuCleanup = true;
            QString status;
            const bool archived = archiveSelectedText(selection, &status);
            const QString message = status.trimmed().isEmpty()
                ? (archived ? QStringLiteral("Selected text archived")
                            : QStringLiteral("Unable to archive selected text"))
                : status.trimmed();
            QToolTip::showText(QCursor::pos(), message, nullptr, {}, 2500);
            if (!archived) {
                window.setRecentError(QStringLiteral("Hyper+S archive failed"), message);
            }
            return;
        }

        if (!selection.target.isValid()) {
            const QString error = selection.diagnostics.trimmed().isEmpty()
                ? QStringLiteral("No valid foreground insertion target")
                : selection.diagnostics.trimmed();
            QToolTip::showText(QCursor::pos(), error, nullptr, {}, 2500);
            window.setRecentError(QStringLiteral("Hyper+S insertion target unavailable"), error);
            return;
        }

        pendingClipInsertionTarget = selection.target;
        Pinloom::showCommandPanelForHotkey(commandWindow, *commandPanel);
        commandPanel->openClipSearch();
    });
    QObject::connect(&hyperHotkeyService,
                     &Pinloom::HyperHotkeyService::chordReleased,
                     &app,
                     [&]() {
                         if (hyperArchiveNeedsMenuCleanup) {
                             Pinloom::dismissHyperModifierUiState();
                             hyperArchiveNeedsMenuCleanup = false;
                         }
                     });

    std::unique_ptr<Pinloom::ClipHotkeyBackend> mainPanelHotkeyBackend = Pinloom::createMainPanelHotkeyBackend();
    Pinloom::ClipHotkeyService mainPanelHotkeyService(Pinloom::defaultMainPanelHotkeyConfig(),
                                                      mainPanelHotkeyBackend.get(),
                                                      &app);
    Pinloom::MainPanelHotkeyController mainPanelHotkeyController(mainPanelHotkeyService,
                                                                 [&commandWindow,
                                                                  commandPanel,
                                                                  &lastForegroundContext]() {
                                                                     lastForegroundContext =
                                                                         Pinloom::currentForegroundAppWindowContext();
                                                                     Pinloom::showCommandPanelForHotkey(commandWindow,
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
        window.show();
    }

    if (!mainPanelHotkeyService.start()) {
        const QString error = QStringLiteral("Pinloom main hotkey %1 could not be registered:\n%2\n\n"
                                             "Ctrl+Space may be reserved by an IME or another application.")
                                  .arg(mainPanelHotkeyService.displayText(), mainPanelHotkeyService.lastError());
        qWarning().noquote() << error;
        window.setRecentError(QStringLiteral("Pinloom main hotkey could not be registered"), error);
        refreshResidentStatus();
        QMessageBox::warning(&window, QStringLiteral("Pinloom"), error);
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
