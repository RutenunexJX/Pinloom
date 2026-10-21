#pragma once

#include "pinloom/core/LibraryRepository.h"
#include "pinloom/clip/PersistentClipService.h"
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <functional>

namespace Pinloom {

inline constexpr int CommandMaxRequestBytes = 1024 * 1024;
inline constexpr int CommandMaxResponseBytes = 8 * 1024 * 1024;
inline constexpr int CommandMaxArchiveBytes = 16 * 1024 * 1024;

QJsonObject commandCapabilities();
QJsonObject commandError(const QString &code, const QString &message, int exitCode,
                         const QJsonObject &details = {});
QString canonicalLibraryPath(const QString &path);
QString libraryCommandEndpoint(const QString &path);
QJsonObject clipCommandJson(const Clip &clip);

struct CommandSessionCallbacks {
    // Empty means the session can adopt a write immediately.
    std::function<QString()> writeBlockReason;
    std::function<void()> refreshViews;
    std::function<QJsonObject(const QJsonObject &, bool)> view;
    std::function<QJsonObject(const QString &, const QString &)> open;
    std::function<bool(const QString &, const QString &)> cancelOpen;
    std::function<void()> close;
};

class CommandService final : public QObject {
public:
    CommandService(ILibraryRepository &library, SqliteClipRepository &clipRepository,
                   PersistentClipService &clips, QString libraryPath,
                   CommandSessionCallbacks callbacks = {}, QObject *parent = nullptr);
    QJsonObject execute(const QJsonObject &request);
    QString libraryVersion() const;
    QString resourceVersion(const Resource &resource) const;
    void reportOpenResult(const QString &resourceId, const QString &anchorId, const QJsonObject &result);

private:
    QJsonObject dispatch(const QString &method, const QJsonObject &params, const QString &base);
    ILibraryRepository &library_;
    SqliteClipRepository &clipRepository_;
    PersistentClipService &clips_;
    QString libraryPath_;
    QString sessionId_;
    CommandSessionCallbacks callbacks_;
    QMap<QString, QJsonObject> operations_;
};

} // namespace Pinloom
