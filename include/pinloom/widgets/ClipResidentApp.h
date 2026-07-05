#pragma once

#include "pinloom/widgets/ClipResidentHost.h"

#include <QObject>
#include <QString>
#include <functional>
#include <memory>

namespace Pinloom {

enum class ClipResidentAppStatus {
    Ready,
    Running,
    Stopped,
    QuitRequested,
    Error
};

struct ClipResidentAppConfig {
    ClipResidentRepositoryKind repositoryKind = ClipResidentRepositoryKind::InMemory;
    QString sqliteDatabasePath;
    bool initializeSqlite = true;
    ClipHotkeyConfig hotkeyConfig = defaultClipHotkeyConfig();
    ClipSearchOptions pickerSearchOptions;
    ClipInsertionOptions insertionOptions;
    bool closePickerOnActivationSuccess = true;
    bool showTrayOnStart = true;
    bool hideTrayOnStop = true;
    bool hidePickerOnStop = true;
    bool stopOnQuitRequested = true;

    ClipResidentRuntimeOptions runtimeOptions() const;
    ClipResidentRuntimeFactoryOptions factoryOptions() const;
};

using ClipResidentAppHostBuilder =
    std::function<ClipResidentHostResult(ClipResidentRuntimeDependencies dependencies,
                                         const ClipResidentRuntimeFactoryOptions &options)>;

QString clipResidentAppStatusText(ClipResidentAppStatus status);
QString validateClipResidentAppConfig(const ClipResidentAppConfig &config);
QString clipResidentDatabasePathForAppDataLocation(const QString &appDataLocation);
ClipResidentAppConfig clipResidentSqliteAppConfig(const QString &databasePath);

class ClipResidentApp final : public QObject {
    Q_OBJECT

public:
    explicit ClipResidentApp(ClipResidentRuntimeDependencies dependencies, QObject *parent = nullptr);
    ClipResidentApp(ClipResidentRuntimeDependencies dependencies,
                    ClipResidentAppHostBuilder hostBuilder,
                    QObject *parent = nullptr);
    ~ClipResidentApp() override;

    bool configure(const ClipResidentAppConfig &config);
    ClipResidentAppConfig config() const;

    bool start();
    void stop();
    bool isRunning() const;
    void requestQuit();

    ClipResidentAppStatus status() const;
    QString statusText() const;
    QString lastError() const;

    ClipResidentHost *host();
    const ClipResidentHost *host() const;

signals:
    void statusChanged(Pinloom::ClipResidentAppStatus status);
    void runningChanged(bool running);
    void errorChanged(const QString &error);
    void quitRequested();

private:
    bool ensureHost();
    void connectHostSignals();
    void setStatus(ClipResidentAppStatus status);
    void setLastError(const QString &error);

    ClipResidentRuntimeDependencies dependencies_;
    ClipResidentAppHostBuilder hostBuilder_;
    ClipResidentAppConfig config_;
    std::unique_ptr<ClipResidentHost> host_;
    ClipResidentAppStatus status_ = ClipResidentAppStatus::Ready;
    QString lastError_;
};

} // namespace Pinloom
