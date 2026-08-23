#pragma once

#include "pinloom/widgets/PinloomEntry.h"

#include <QJsonObject>
#include <QObject>
#include <QUrl>

#include <functional>
#include <optional>

class QLocalServer;
class QLocalSocket;

namespace Pinloom {

inline constexpr const char *kPinloomHostProtocol = "pinloom-host/v1";

struct PinloomHostIdentity {
    QString entryId;
    QString resourceId;
    QString anchorId;
    QString clipId;

    bool isValid() const;
};

struct PinloomHostDocument {
    PinloomEntry entry;
    QString content;
    QString contentType = QStringLiteral("text/plain");
    QVariantMap details;
};

struct PinloomSourceAnchorRequest {
    QString title;
    QString content;
    QString workspaceRoot;
    QString relativeFilePath;
    QString absoluteFilePath;
    QString moduleName;
    int startLine = 0;
    int startColumn = 0;
    int endLine = 0;
    int endColumn = 0;
    QString selectedTextHash;
    QString prefixContext;
    QString suffixContext;

    bool isValid() const;
};

struct PinloomHostBridgeOptions {
    QString serverName;
    int requestTimeoutMs = 1200;
    qsizetype maxRequestBytes = 256 * 1024;
    int maxSearchResults = 100;
};

struct PinloomHostBridgeCallbacks {
    using SearchHandler =
        std::function<QList<PinloomEntry>(const QString &, int)>;
    using ResolveHandler =
        std::function<std::optional<PinloomHostDocument>(
            const PinloomHostIdentity &)>;
    using OpenHandler =
        std::function<bool(const PinloomHostIdentity &, QString *)>;
    using CreateSourceAnchorHandler =
        std::function<std::optional<PinloomEntry>(
            const PinloomSourceAnchorRequest &, QString *)>;

    SearchHandler search;
    ResolveHandler resolve;
    OpenHandler open;
    CreateSourceAnchorHandler createSourceAnchor;
};

QString defaultPinloomHostBridgeServerName();
PinloomHostIdentity pinloomHostIdentityForEntry(const PinloomEntry &entry);
QUrl pinloomHostUri(const PinloomHostIdentity &identity);
QJsonObject pinloomHostIdentityToJson(const PinloomHostIdentity &identity);
std::optional<PinloomHostIdentity> pinloomHostIdentityFromJson(
    const QJsonObject &object);
QJsonObject pinloomHostEntryToJson(const PinloomEntry &entry);
QJsonObject pinloomHostDocumentToJson(const PinloomHostDocument &document);

class PinloomHostBridgeServer final : public QObject {
    Q_OBJECT

public:
    explicit PinloomHostBridgeServer(
        PinloomHostBridgeOptions options,
        PinloomHostBridgeCallbacks callbacks,
        QObject *parent = nullptr);
    ~PinloomHostBridgeServer() override;

    bool start();
    bool isListening() const;
    QString serverName() const;
    QString lastError() const;

private:
    void handlePendingConnections();
    void consumeSocket(QLocalSocket *socket, bool allowIncompletePayload);
    QJsonObject processRequest(const QJsonObject &request) const;
    void finishSocket(QLocalSocket *socket, const QJsonObject &response);

    PinloomHostBridgeOptions options_;
    PinloomHostBridgeCallbacks callbacks_;
    QLocalServer *server_ = nullptr;
    QString lastError_;
};

} // namespace Pinloom
