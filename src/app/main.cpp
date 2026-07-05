#include "pinloom/core/SqliteLibraryRepository.h"
#include "pinloom/widgets/ClipResidentHost.h"
#include "pinloom/widgets/PinloomPanel.h"

#include <QApplication>
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

    Pinloom::ClipResidentHostResult clipHostResult = clipFactory.createDefaultPlatformHost(clipOptions);
    std::unique_ptr<Pinloom::ClipResidentHost> clipHost;
    if (clipHostResult.succeeded()) {
        clipHost = std::move(clipHostResult.host);
        QObject::connect(clipHost.get(), &Pinloom::ClipResidentHost::quitRequested, &app, &QApplication::quit);
        if (!clipHost->start()) {
            QMessageBox::warning(nullptr,
                                 QStringLiteral("Pinloom Clip"),
                                 QStringLiteral("Pinloom Clip could not start:\n%1").arg(clipHost->lastError()));
        }
    } else {
        QMessageBox::warning(nullptr,
                             QStringLiteral("Pinloom Clip"),
                             QStringLiteral("Pinloom Clip could not initialize:\n%1").arg(clipHostResult.error));
    }

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Pinloom"));
    window.resize(900, 560);
    window.setCentralWidget(new Pinloom::PinloomPanel(repository, &window));
    window.show();

    return app.exec();
}
