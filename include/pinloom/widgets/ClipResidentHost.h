#pragma once

#include "pinloom/widgets/ClipResidentRuntime.h"

#include <QObject>
#include <QString>
#include <memory>

namespace Pinloom {

enum class ClipResidentRepositoryKind {
    InMemory,
    SQLite
};

struct ClipResidentRuntimeFactoryOptions {
    ClipResidentRepositoryKind repositoryKind = ClipResidentRepositoryKind::InMemory;
    QString sqliteDatabasePath;
    QString sqliteIdentityRegistryPath;
    bool initializeSqlite = true;
    ClipResidentRuntimeOptions runtimeOptions;
};

struct ClipResidentHostOwnedDependencies {
    ClipResidentHostOwnedDependencies() = default;
    ClipResidentHostOwnedDependencies(ClipResidentHostOwnedDependencies &&) noexcept = default;
    ClipResidentHostOwnedDependencies &operator=(ClipResidentHostOwnedDependencies &&) noexcept = default;
    ClipResidentHostOwnedDependencies(const ClipResidentHostOwnedDependencies &) = delete;
    ClipResidentHostOwnedDependencies &operator=(const ClipResidentHostOwnedDependencies &) = delete;

    std::unique_ptr<ClipboardTextSource> captureClipboard;
    std::unique_ptr<ClipboardTextAccessor> insertionClipboard;
    std::unique_ptr<ClipTrayBackend> trayBackend;
};

class ClipResidentHost;

struct ClipResidentHostResult {
    std::unique_ptr<ClipResidentHost> host;
    QString error;

    bool succeeded() const;
};

class ClipResidentHost final : public QObject {
    Q_OBJECT

public:
    ~ClipResidentHost() override;

    bool start();
    void stop();
    bool isRunning() const;

    void requestQuit();
    QString lastError() const;

    ClipResidentRuntime *runtime();
    const ClipResidentRuntime *runtime() const;
    InMemoryClipRepository *inMemoryRepository();
    const InMemoryClipRepository *inMemoryRepository() const;
    SqliteClipRepository *sqliteRepository();
    const SqliteClipRepository *sqliteRepository() const;

signals:
    void runningChanged(bool running);
    void errorChanged(const QString &error);
    void quitRequested();

private:
    friend class ClipResidentRuntimeFactory;

    ClipResidentHost(std::unique_ptr<InMemoryClipRepository> inMemoryRepository,
                     std::unique_ptr<SqliteClipRepository> sqliteRepository,
                     ClipResidentHostOwnedDependencies ownedDependencies,
                     std::unique_ptr<ClipResidentRuntime> runtime,
                     QObject *parent = nullptr);

    void setLastError(const QString &error);

    std::unique_ptr<InMemoryClipRepository> inMemoryRepository_;
    std::unique_ptr<SqliteClipRepository> sqliteRepository_;
    ClipResidentHostOwnedDependencies ownedDependencies_;
    std::unique_ptr<ClipResidentRuntime> runtime_;
    QString lastError_;
};

class ClipResidentRuntimeFactory {
public:
    ClipResidentHostResult createHost(
        ClipResidentRuntimeDependencies dependencies,
        const ClipResidentRuntimeFactoryOptions &options = ClipResidentRuntimeFactoryOptions{}) const;

    ClipResidentHostResult createInMemoryHost(
        std::unique_ptr<InMemoryClipRepository> repository,
        ClipResidentRuntimeDependencies dependencies,
        ClipResidentRuntimeOptions runtimeOptions = ClipResidentRuntimeOptions{}) const;

    ClipResidentHostResult createSqliteHost(
        std::unique_ptr<SqliteClipRepository> repository,
        ClipResidentRuntimeDependencies dependencies,
        ClipResidentRuntimeOptions runtimeOptions = ClipResidentRuntimeOptions{}) const;

    ClipResidentHostResult createDefaultPlatformHost(
        const ClipResidentRuntimeFactoryOptions &options = ClipResidentRuntimeFactoryOptions{}) const;

private:
    ClipResidentHostResult createInMemoryHostWithOwnedDependencies(
        std::unique_ptr<InMemoryClipRepository> repository,
        ClipResidentRuntimeDependencies dependencies,
        ClipResidentRuntimeOptions runtimeOptions,
        ClipResidentHostOwnedDependencies ownedDependencies) const;

    ClipResidentHostResult createSqliteHostWithOwnedDependencies(
        std::unique_ptr<SqliteClipRepository> repository,
        ClipResidentRuntimeDependencies dependencies,
        ClipResidentRuntimeOptions runtimeOptions,
        ClipResidentHostOwnedDependencies ownedDependencies) const;
};

} // namespace Pinloom
