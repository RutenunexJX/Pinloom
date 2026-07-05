#include "pinloom/core/SqliteLibraryRepository.h"
#include "pinloom/widgets/ClipResidentHost.h"
#include "pinloom/widgets/MainPanelHotkey.h"
#include "pinloom/widgets/PinloomCommandPanel.h"
#include "pinloom/widgets/PinloomMainWindow.h"
#include "pinloom/widgets/PinloomPanel.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QMainWindow>
#include <QMessageBox>
#include <QStandardPaths>
#include <memory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Pinloom"));
    QApplication::setOrganizationName(QStringLiteral("Pinloom"));
    app.setQuitOnLastWindowClosed(false);

    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        appDataPath = QDir::home().filePath(QStringLiteral(".pinloom"));
    }
    if (!QDir().mkpath(appDataPath)) {
        QMessageBox::critical(nullptr, QStringLiteral("Pinloom"), QStringLiteral("Unable to create app data directory."));
        return 1;
    }

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
    clipOptions.runtimeOptions.pickerSearchOptions.includeTemporary = true;
    clipOptions.runtimeOptions.registerHotkeyOnStart = false;

    Pinloom::ClipResidentHostResult clipHostResult = clipFactory.createDefaultPlatformHost(clipOptions);
    std::unique_ptr<Pinloom::ClipResidentHost> clipHost;
    if (clipHostResult.succeeded()) {
        clipHost = std::move(clipHostResult.host);
        QObject::connect(clipHost.get(), &Pinloom::ClipResidentHost::quitRequested, &app, &QApplication::quit);
    } else {
        QMessageBox::warning(nullptr,
                             QStringLiteral("Pinloom Clip"),
                             QStringLiteral("Pinloom Clip could not initialize:\n%1").arg(clipHostResult.error));
    }

    Pinloom::PinloomMainWindow window;
    window.setWindowTitle(QStringLiteral("Pinloom"));
    window.setMinimumWidth(560);
    window.resize(760, 72);

    Pinloom::PinloomPanelOptions panelOptions;
    panelOptions.clipSearchHandler =
        [&clipHost](const QString &query, const Pinloom::ClipSearchOptions &options) -> QList<Pinloom::ClipSearchResult> {
        if (!clipHost || !clipHost->runtime()) {
            return {};
        }
        return clipHost->runtime()->searchService().search(query, options);
    };
    panelOptions.clipInsertionHandler = [&clipHost](const QString &clipId, QString *error) {
        if (!clipHost || !clipHost->runtime()) {
            if (error) {
                *error = QStringLiteral("Pinloom Clip is not running");
            }
            return false;
        }
        const Pinloom::ClipInsertionResult result = clipHost->runtime()->insertionService().insertClip(clipId);
        if (!result.inserted() && error) {
            *error = result.error;
        }
        return result.inserted();
    };
    auto clipSaveHandler = [&clipHost](const Pinloom::PinloomClipSaveRequest &request, QString *error) {
        if (!clipHost || !clipHost->runtime()) {
            if (error) {
                *error = QStringLiteral("Pinloom Clip is not running");
            }
            return false;
        }
        if (!clipHost->runtime()->searchService().saveClip(request.clipId,
                                                           request.name,
                                                           request.aliases,
                                                           request.tags,
                                                           request.pinned)) {
            if (error) {
                const QString repositoryError = clipHost->runtime()->searchService().lastError().trimmed();
                *error = repositoryError.isEmpty()
                    ? QStringLiteral("Unable to save clip")
                    : repositoryError;
            }
            return false;
        }
        if (error) {
            error->clear();
        }
        return true;
    };

    auto *panel = new Pinloom::PinloomPanel(repository, panelOptions, &window);
    window.setCentralWidget(panel);

    QMainWindow commandWindow;
    commandWindow.setWindowTitle(QStringLiteral("Pinloom Command"));
    commandWindow.setMinimumWidth(560);
    commandWindow.resize(760, 300);

    Pinloom::PinloomCommandPanelOptions commandOptions;
    commandOptions.clipSearchHandler = panelOptions.clipSearchHandler;
    commandOptions.clipInsertionHandler = panelOptions.clipInsertionHandler;
    commandOptions.clipSaveHandler = clipSaveHandler;
    commandOptions.anchorCaptureHandler = [panel](QString *status) {
        const bool captured = panel->captureCurrentAppPosition();
        if (status) {
            *status = panel->statusText();
        }
        return captured;
    };
    commandOptions.searchWindowHandler = [&window, panel](const QString &query) {
        Pinloom::showMainPanelForHotkey(window, *panel);
        const QString trimmedQuery = query.trimmed();
        if (!trimmedQuery.isEmpty()) {
            panel->setSearchText(trimmedQuery);
        }
    };

    auto *commandPanel = new Pinloom::PinloomCommandPanel(commandOptions, &commandWindow);
    commandWindow.setCentralWidget(commandPanel);

    if (clipHost && clipHost->runtime()) {
        clipHost->runtime()->trayController().setShowPickerHandler([&commandWindow, commandPanel]() {
            Pinloom::showCommandPanelForHotkey(commandWindow, *commandPanel);
            commandPanel->openClipSearch();
        });
        if (!clipHost->start()) {
            QMessageBox::warning(&window,
                                 QStringLiteral("Pinloom Clip"),
                                 QStringLiteral("Pinloom Clip could not start:\n%1").arg(clipHost->lastError()));
        }
    }

    std::unique_ptr<Pinloom::ClipHotkeyBackend> mainPanelHotkeyBackend = Pinloom::createMainPanelHotkeyBackend();
    Pinloom::ClipHotkeyService mainPanelHotkeyService(Pinloom::defaultMainPanelHotkeyConfig(),
                                                      mainPanelHotkeyBackend.get(),
                                                      &app);
    Pinloom::MainPanelHotkeyController mainPanelHotkeyController(mainPanelHotkeyService,
                                                                 [&commandWindow, commandPanel]() {
                                                                     Pinloom::showCommandPanelForHotkey(commandWindow,
                                                                                                        *commandPanel);
                                                                 },
                                                                 &app);
    window.show();

    if (!mainPanelHotkeyService.start()) {
        const QString error = QStringLiteral("Pinloom main hotkey %1 could not be registered:\n%2\n\n"
                                             "Ctrl+Space may be reserved by an IME or another application.")
                                  .arg(mainPanelHotkeyService.displayText(), mainPanelHotkeyService.lastError());
        qWarning().noquote() << error;
        QMessageBox::warning(&window, QStringLiteral("Pinloom"), error);
    }

    return app.exec();
}
