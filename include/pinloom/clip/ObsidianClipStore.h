#pragma once

#include "pinloom/clip/ClipRepository.h"

#include <QObject>
#include <QUrl>
#include <functional>
#include <memory>
#include <optional>

class QFileSystemWatcher;
class QTimer;

namespace Pinloom {

struct ObsidianClipStoreConfig {
    QString vaultPath;
    QString archiveDirectory = QStringLiteral("Pinloom Clips");

    bool isEnabled() const;
};

struct ObsidianClipDocument {
    Clip clip;
    QString filePath;
    QString relativePath;
};

struct ObsidianClipWriteResult {
    bool success = false;
    QString filePath;
    QString relativePath;
    QString error;

    bool succeeded() const;
};

struct ObsidianClipScanResult {
    QList<ObsidianClipDocument> documents;
    int skippedUnmanagedFiles = 0;
    QStringList errors;
    QString fatalError;

    bool succeeded() const;
};

struct ObsidianClipSyncResult {
    int discovered = 0;
    int imported = 0;
    int updated = 0;
    int unchanged = 0;
    int deleted = 0;
    int skipped = 0;
    QStringList errors;
    QString fatalError;

    bool succeeded() const;
};

class ObsidianClipStore {
public:
    explicit ObsidianClipStore(ObsidianClipStoreConfig config = {});

    void setConfig(const ObsidianClipStoreConfig &config);
    ObsidianClipStoreConfig config() const;

    QString vaultPath() const;
    QString archivePath() const;
    bool ensureArchiveDirectory(QString *error = nullptr) const;

    ObsidianClipWriteResult writeClip(const Clip &clip) const;
    std::optional<ObsidianClipDocument> readClipFile(const QString &filePath,
                                                     QString *error = nullptr) const;
    ObsidianClipScanResult scan() const;
    std::optional<ObsidianClipDocument> findClip(const QString &clipId,
                                                 QString *error = nullptr) const;
    QUrl openUrlForClip(const QString &clipId, QString *error = nullptr) const;

    ObsidianClipSyncResult synchronize(InMemoryClipRepository &repository) const;
    ObsidianClipSyncResult synchronize(SqliteClipRepository &repository) const;

private:
    ObsidianClipStoreConfig config_;
};

class ObsidianClipSyncService final : public QObject {
    Q_OBJECT

public:
    explicit ObsidianClipSyncService(InMemoryClipRepository &repository,
                                     ObsidianClipStoreConfig config = {},
                                     QObject *parent = nullptr);
    explicit ObsidianClipSyncService(SqliteClipRepository &repository,
                                     ObsidianClipStoreConfig config = {},
                                     QObject *parent = nullptr);
    ~ObsidianClipSyncService() override;

    void setConfig(const ObsidianClipStoreConfig &config);
    ObsidianClipStoreConfig config() const;
    const ObsidianClipStore &store() const;

    bool start();
    void stop();
    bool isRunning() const;
    ObsidianClipSyncResult synchronizeNow();
    QString lastError() const;

signals:
    void synchronized(int imported, int updated, int deleted);
    void errorChanged(const QString &error);

private:
    using SynchronizeCallback = std::function<ObsidianClipSyncResult(const ObsidianClipStore &)>;

    ObsidianClipSyncService(SynchronizeCallback synchronize,
                            ObsidianClipStoreConfig config,
                            QObject *parent);
    void scheduleSynchronize();
    void refreshWatchPaths();
    void setLastError(const QString &error);

    ObsidianClipStore store_;
    SynchronizeCallback synchronize_;
    std::unique_ptr<QFileSystemWatcher> watcher_;
    std::unique_ptr<QTimer> debounceTimer_;
    QString lastError_;
    bool running_ = false;
};

QString obsidianClipSourceApp();

} // namespace Pinloom
