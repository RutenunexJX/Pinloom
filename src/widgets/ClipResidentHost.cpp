#include "pinloom/widgets/ClipResidentHost.h"

#include "pinloom/clip/PlatformPasteInvoker.h"

#include <utility>

namespace Pinloom {

namespace {

ClipResidentHostResult failedHostResult(const QString &error)
{
    ClipResidentHostResult result;
    result.error = error;
    return result;
}

ClipResidentHostResult successfulHostResult(std::unique_ptr<ClipResidentHost> host)
{
    ClipResidentHostResult result;
    result.host = std::move(host);
    return result;
}

QString validateRuntimeDependencies(const ClipResidentRuntimeDependencies &dependencies)
{
    if (!dependencies.captureClipboard) {
        return QStringLiteral("Clipboard text source is required");
    }
    if (!dependencies.insertionClipboard) {
        return QStringLiteral("Clipboard text accessor is required");
    }
    if (!dependencies.hotkeyBackend) {
        return QStringLiteral("Hotkey backend is required");
    }
    if (!dependencies.trayBackend) {
        return QStringLiteral("Tray backend is required");
    }
    if (!dependencies.pasteInvoker) {
        return QStringLiteral("Paste invoker is required");
    }
    return {};
}

QString normalizedError(const QString &error, const QString &fallback)
{
    const QString trimmed = error.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

} // namespace

bool ClipResidentHostResult::succeeded() const
{
    return host != nullptr;
}

ClipResidentHost::ClipResidentHost(std::unique_ptr<InMemoryClipRepository> inMemoryRepository,
                                   std::unique_ptr<SqliteClipRepository> sqliteRepository,
                                   ClipResidentHostOwnedDependencies ownedDependencies,
                                   std::unique_ptr<ClipResidentRuntime> runtime,
                                   QObject *parent)
    : QObject(parent)
    , inMemoryRepository_(std::move(inMemoryRepository))
    , sqliteRepository_(std::move(sqliteRepository))
    , ownedDependencies_(std::move(ownedDependencies))
    , runtime_(std::move(runtime))
{
    if (!runtime_) {
        lastError_ = QStringLiteral("Clip resident runtime is required");
        return;
    }

    connect(runtime_.get(), &ClipResidentRuntime::runningChanged, this, &ClipResidentHost::runningChanged);
    connect(runtime_.get(), &ClipResidentRuntime::errorChanged, this, &ClipResidentHost::setLastError);
    connect(runtime_.get(), &ClipResidentRuntime::quitRequested, this, &ClipResidentHost::quitRequested);
}

ClipResidentHost::~ClipResidentHost()
{
    stop();
}

bool ClipResidentHost::start()
{
    if (!runtime_) {
        setLastError(QStringLiteral("Clip resident runtime is required"));
        return false;
    }

    if (!runtime_->start()) {
        setLastError(normalizedError(runtime_->lastError(), QStringLiteral("Unable to start clip resident runtime")));
        return false;
    }

    setLastError({});
    return true;
}

void ClipResidentHost::stop()
{
    if (runtime_) {
        runtime_->stop();
    }
}

bool ClipResidentHost::isRunning() const
{
    return runtime_ && runtime_->isRunning();
}

void ClipResidentHost::requestQuit()
{
    if (runtime_) {
        runtime_->requestQuit();
    }
}

QString ClipResidentHost::lastError() const
{
    return lastError_;
}

ClipResidentRuntime *ClipResidentHost::runtime()
{
    return runtime_.get();
}

const ClipResidentRuntime *ClipResidentHost::runtime() const
{
    return runtime_.get();
}

InMemoryClipRepository *ClipResidentHost::inMemoryRepository()
{
    return inMemoryRepository_.get();
}

const InMemoryClipRepository *ClipResidentHost::inMemoryRepository() const
{
    return inMemoryRepository_.get();
}

SqliteClipRepository *ClipResidentHost::sqliteRepository()
{
    return sqliteRepository_.get();
}

const SqliteClipRepository *ClipResidentHost::sqliteRepository() const
{
    return sqliteRepository_.get();
}

void ClipResidentHost::setLastError(const QString &error)
{
    if (lastError_ == error) {
        return;
    }

    lastError_ = error;
    emit errorChanged(lastError_);
}

ClipResidentHostResult ClipResidentRuntimeFactory::createHost(
    ClipResidentRuntimeDependencies dependencies,
    const ClipResidentRuntimeFactoryOptions &options) const
{
    switch (options.repositoryKind) {
    case ClipResidentRepositoryKind::InMemory:
        return createInMemoryHostWithOwnedDependencies(std::make_unique<InMemoryClipRepository>(),
                                                       std::move(dependencies),
                                                       options.runtimeOptions,
                                                       {});
    case ClipResidentRepositoryKind::SQLite: {
        const QString databasePath = options.sqliteDatabasePath.trimmed();
        if (databasePath.isEmpty()) {
            return failedHostResult(QStringLiteral("SQLite database path is required"));
        }

        auto repository = std::make_unique<SqliteClipRepository>();
        if (!repository->open(databasePath)) {
            return failedHostResult(
                normalizedError(repository->lastError(), QStringLiteral("Unable to open clip SQLite database")));
        }
        if (options.initializeSqlite && !repository->initialize()) {
            return failedHostResult(
                normalizedError(repository->lastError(), QStringLiteral("Unable to initialize clip SQLite database")));
        }

        return createSqliteHostWithOwnedDependencies(std::move(repository),
                                                     std::move(dependencies),
                                                     options.runtimeOptions,
                                                     {});
    }
    }

    return failedHostResult(QStringLiteral("Unsupported clip repository kind"));
}

ClipResidentHostResult ClipResidentRuntimeFactory::createInMemoryHost(
    std::unique_ptr<InMemoryClipRepository> repository,
    ClipResidentRuntimeDependencies dependencies,
    ClipResidentRuntimeOptions runtimeOptions) const
{
    return createInMemoryHostWithOwnedDependencies(std::move(repository),
                                                   std::move(dependencies),
                                                   std::move(runtimeOptions),
                                                   {});
}

ClipResidentHostResult ClipResidentRuntimeFactory::createSqliteHost(
    std::unique_ptr<SqliteClipRepository> repository,
    ClipResidentRuntimeDependencies dependencies,
    ClipResidentRuntimeOptions runtimeOptions) const
{
    return createSqliteHostWithOwnedDependencies(std::move(repository),
                                                 std::move(dependencies),
                                                 std::move(runtimeOptions),
                                                 {});
}

ClipResidentHostResult ClipResidentRuntimeFactory::createDefaultPlatformHost(
    const ClipResidentRuntimeFactoryOptions &options) const
{
    ClipResidentHostOwnedDependencies ownedDependencies;
    ownedDependencies.captureClipboard = std::make_unique<QtClipboardTextSource>();
    ownedDependencies.insertionClipboard = std::make_unique<QtClipboardTextAccessor>();
    ownedDependencies.trayBackend = std::make_unique<QtSystemTrayIconBackend>();

    ClipResidentRuntimeDependencies dependencies;
    dependencies.captureClipboard = ownedDependencies.captureClipboard.get();
    dependencies.insertionClipboard = ownedDependencies.insertionClipboard.get();
    dependencies.hotkeyBackend = defaultClipHotkeyBackend();
    dependencies.trayBackend = ownedDependencies.trayBackend.get();
    dependencies.pasteInvoker = createPlatformPasteInvoker();

    switch (options.repositoryKind) {
    case ClipResidentRepositoryKind::InMemory:
        return createInMemoryHostWithOwnedDependencies(std::make_unique<InMemoryClipRepository>(),
                                                       std::move(dependencies),
                                                       options.runtimeOptions,
                                                       std::move(ownedDependencies));
    case ClipResidentRepositoryKind::SQLite: {
        const QString databasePath = options.sqliteDatabasePath.trimmed();
        if (databasePath.isEmpty()) {
            return failedHostResult(QStringLiteral("SQLite database path is required"));
        }

        auto repository = std::make_unique<SqliteClipRepository>();
        if (!repository->open(databasePath)) {
            return failedHostResult(
                normalizedError(repository->lastError(), QStringLiteral("Unable to open clip SQLite database")));
        }
        if (options.initializeSqlite && !repository->initialize()) {
            return failedHostResult(
                normalizedError(repository->lastError(), QStringLiteral("Unable to initialize clip SQLite database")));
        }

        return createSqliteHostWithOwnedDependencies(std::move(repository),
                                                     std::move(dependencies),
                                                     options.runtimeOptions,
                                                     std::move(ownedDependencies));
    }
    }

    return failedHostResult(QStringLiteral("Unsupported clip repository kind"));
}

ClipResidentHostResult ClipResidentRuntimeFactory::createInMemoryHostWithOwnedDependencies(
    std::unique_ptr<InMemoryClipRepository> repository,
    ClipResidentRuntimeDependencies dependencies,
    ClipResidentRuntimeOptions runtimeOptions,
    ClipResidentHostOwnedDependencies ownedDependencies) const
{
    if (!repository) {
        return failedHostResult(QStringLiteral("Clip repository is required"));
    }

    const QString dependencyError = validateRuntimeDependencies(dependencies);
    if (!dependencyError.isEmpty()) {
        return failedHostResult(dependencyError);
    }

    auto runtime =
        std::make_unique<ClipResidentRuntime>(*repository, std::move(dependencies), std::move(runtimeOptions));
    return successfulHostResult(std::unique_ptr<ClipResidentHost>(new ClipResidentHost(std::move(repository),
                                                                                       nullptr,
                                                                                       std::move(ownedDependencies),
                                                                                       std::move(runtime))));
}

ClipResidentHostResult ClipResidentRuntimeFactory::createSqliteHostWithOwnedDependencies(
    std::unique_ptr<SqliteClipRepository> repository,
    ClipResidentRuntimeDependencies dependencies,
    ClipResidentRuntimeOptions runtimeOptions,
    ClipResidentHostOwnedDependencies ownedDependencies) const
{
    if (!repository) {
        return failedHostResult(QStringLiteral("Clip repository is required"));
    }

    if (!repository->isOpen()) {
        return failedHostResult(normalizedError(repository->lastError(), QStringLiteral("SQLite database is not open")));
    }

    const QString dependencyError = validateRuntimeDependencies(dependencies);
    if (!dependencyError.isEmpty()) {
        return failedHostResult(dependencyError);
    }

    auto runtime =
        std::make_unique<ClipResidentRuntime>(*repository, std::move(dependencies), std::move(runtimeOptions));
    return successfulHostResult(std::unique_ptr<ClipResidentHost>(new ClipResidentHost(nullptr,
                                                                                       std::move(repository),
                                                                                       std::move(ownedDependencies),
                                                                                       std::move(runtime))));
}

} // namespace Pinloom
