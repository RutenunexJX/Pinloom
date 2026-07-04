#include "pinloom/widgets/ClipResidentApp.h"

#include <QDir>
#include <utility>

namespace Pinloom {

namespace {

QString normalizedError(const QString &error, const QString &fallback)
{
    const QString trimmed = error.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

ClipResidentAppHostBuilder defaultHostBuilder()
{
    return [](ClipResidentRuntimeDependencies dependencies, const ClipResidentRuntimeFactoryOptions &options) {
        ClipResidentRuntimeFactory factory;
        return factory.createHost(std::move(dependencies), options);
    };
}

bool isRunningStatus(ClipResidentAppStatus status)
{
    return status == ClipResidentAppStatus::Running;
}

} // namespace

ClipResidentRuntimeOptions ClipResidentAppConfig::runtimeOptions() const
{
    ClipResidentRuntimeOptions options;
    options.pickerSearchOptions = pickerSearchOptions;
    options.insertionOptions = insertionOptions;
    options.closePickerOnActivationSuccess = closePickerOnActivationSuccess;
    options.showTrayOnStart = showTrayOnStart;
    options.hideTrayOnStop = hideTrayOnStop;
    options.hidePickerOnStop = hidePickerOnStop;
    options.stopOnQuitRequested = stopOnQuitRequested;
    return options;
}

ClipResidentRuntimeFactoryOptions ClipResidentAppConfig::factoryOptions() const
{
    ClipResidentRuntimeFactoryOptions options;
    options.repositoryKind = repositoryKind;
    options.sqliteDatabasePath = sqliteDatabasePath.trimmed();
    options.initializeSqlite = initializeSqlite;
    options.runtimeOptions = runtimeOptions();
    return options;
}

QString clipResidentAppStatusText(ClipResidentAppStatus status)
{
    switch (status) {
    case ClipResidentAppStatus::Ready:
        return QStringLiteral("Ready");
    case ClipResidentAppStatus::Running:
        return QStringLiteral("Running");
    case ClipResidentAppStatus::Stopped:
        return QStringLiteral("Stopped");
    case ClipResidentAppStatus::QuitRequested:
        return QStringLiteral("Quit requested");
    case ClipResidentAppStatus::Error:
        return QStringLiteral("Error");
    }

    return QStringLiteral("Error");
}

QString validateClipResidentAppConfig(const ClipResidentAppConfig &config)
{
    switch (config.repositoryKind) {
    case ClipResidentRepositoryKind::InMemory:
        break;
    case ClipResidentRepositoryKind::SQLite:
        if (config.sqliteDatabasePath.trimmed().isEmpty()) {
            return QStringLiteral("SQLite database path is required");
        }
        break;
    }

    if (!config.hotkeyConfig.isValid()) {
        return QStringLiteral("Hotkey key is required");
    }

    return {};
}

QString clipResidentDatabasePathForAppDataLocation(const QString &appDataLocation)
{
    const QString trimmedPath = appDataLocation.trimmed();
    if (trimmedPath.isEmpty()) {
        return {};
    }

    return QDir(trimmedPath).filePath(QStringLiteral("pinloom_clip.sqlite3"));
}

ClipResidentAppConfig clipResidentSqliteAppConfig(const QString &databasePath)
{
    ClipResidentAppConfig config;
    config.repositoryKind = ClipResidentRepositoryKind::SQLite;
    config.sqliteDatabasePath = databasePath;
    return config;
}

ClipResidentApp::ClipResidentApp(ClipResidentRuntimeDependencies dependencies, QObject *parent)
    : ClipResidentApp(std::move(dependencies), {}, parent)
{
}

ClipResidentApp::ClipResidentApp(ClipResidentRuntimeDependencies dependencies,
                                 ClipResidentAppHostBuilder hostBuilder,
                                 QObject *parent)
    : QObject(parent)
    , dependencies_(std::move(dependencies))
    , hostBuilder_(std::move(hostBuilder))
{
    if (!hostBuilder_) {
        hostBuilder_ = defaultHostBuilder();
    }
}

ClipResidentApp::~ClipResidentApp()
{
    stop();
}

bool ClipResidentApp::configure(const ClipResidentAppConfig &config)
{
    if (host_ && host_->isRunning()) {
        setLastError(QStringLiteral("Cannot configure clip resident app while running"));
        setStatus(ClipResidentAppStatus::Error);
        return false;
    }

    config_ = config;
    host_.reset();

    const QString configError = validateClipResidentAppConfig(config_);
    if (!configError.isEmpty()) {
        setLastError(configError);
        setStatus(ClipResidentAppStatus::Error);
        return false;
    }

    setLastError({});
    setStatus(ClipResidentAppStatus::Ready);
    return true;
}

ClipResidentAppConfig ClipResidentApp::config() const
{
    return config_;
}

bool ClipResidentApp::start()
{
    if (host_ && host_->isRunning()) {
        setStatus(ClipResidentAppStatus::Running);
        return true;
    }

    if (!ensureHost()) {
        return false;
    }

    if (!host_->start()) {
        setLastError(normalizedError(host_->lastError(), QStringLiteral("Unable to start clip resident host")));
        setStatus(ClipResidentAppStatus::Error);
        return false;
    }

    setLastError({});
    setStatus(ClipResidentAppStatus::Running);
    return true;
}

void ClipResidentApp::stop()
{
    if (host_) {
        host_->stop();
    }
    setStatus(ClipResidentAppStatus::Stopped);
}

bool ClipResidentApp::isRunning() const
{
    return host_ && host_->isRunning();
}

void ClipResidentApp::requestQuit()
{
    if (!host_) {
        setStatus(ClipResidentAppStatus::QuitRequested);
        return;
    }

    host_->requestQuit();
    if (host_->runtime() && host_->runtime()->quitWasRequested()) {
        setStatus(ClipResidentAppStatus::QuitRequested);
    }
}

ClipResidentAppStatus ClipResidentApp::status() const
{
    return status_;
}

QString ClipResidentApp::statusText() const
{
    return clipResidentAppStatusText(status_);
}

QString ClipResidentApp::lastError() const
{
    return lastError_;
}

ClipResidentHost *ClipResidentApp::host()
{
    return host_.get();
}

const ClipResidentHost *ClipResidentApp::host() const
{
    return host_.get();
}

bool ClipResidentApp::ensureHost()
{
    if (host_) {
        return true;
    }

    const QString configError = validateClipResidentAppConfig(config_);
    if (!configError.isEmpty()) {
        setLastError(configError);
        setStatus(ClipResidentAppStatus::Error);
        return false;
    }

    if (!hostBuilder_) {
        setLastError(QStringLiteral("Clip resident host builder is required"));
        setStatus(ClipResidentAppStatus::Error);
        return false;
    }

    ClipResidentHostResult result = hostBuilder_(dependencies_, config_.factoryOptions());
    if (!result.succeeded()) {
        setLastError(normalizedError(result.error, QStringLiteral("Unable to create clip resident host")));
        setStatus(ClipResidentAppStatus::Error);
        return false;
    }

    host_ = std::move(result.host);
    if (!host_ || !host_->runtime()) {
        host_.reset();
        setLastError(QStringLiteral("Clip resident host runtime is required"));
        setStatus(ClipResidentAppStatus::Error);
        return false;
    }

    host_->runtime()->hotkeyService().setConfig(config_.hotkeyConfig);
    connectHostSignals();
    setLastError({});
    return true;
}

void ClipResidentApp::connectHostSignals()
{
    connect(host_.get(), &ClipResidentHost::runningChanged, this, [this](bool running) {
        if (running) {
            setStatus(ClipResidentAppStatus::Running);
            return;
        }

        if (status_ != ClipResidentAppStatus::QuitRequested && status_ != ClipResidentAppStatus::Error) {
            setStatus(ClipResidentAppStatus::Stopped);
        }
    });
    connect(host_.get(), &ClipResidentHost::errorChanged, this, [this](const QString &error) {
        setLastError(error);
        if (!error.isEmpty()) {
            setStatus(ClipResidentAppStatus::Error);
        }
    });
    connect(host_.get(), &ClipResidentHost::quitRequested, this, [this]() {
        setStatus(ClipResidentAppStatus::QuitRequested);
        emit quitRequested();
    });
}

void ClipResidentApp::setStatus(ClipResidentAppStatus status)
{
    if (status_ == status) {
        return;
    }

    const bool wasRunning = isRunningStatus(status_);
    status_ = status;
    emit statusChanged(status_);

    const bool nowRunning = isRunningStatus(status_);
    if (wasRunning != nowRunning) {
        emit runningChanged(nowRunning);
    }
}

void ClipResidentApp::setLastError(const QString &error)
{
    if (lastError_ == error) {
        return;
    }

    lastError_ = error;
    emit errorChanged(lastError_);
}

} // namespace Pinloom
