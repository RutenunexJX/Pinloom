#pragma once

#include "pinloom/core/ResourceUsage.h"
#include "pinloom/widgets/PdfLocatorPreviewRenderer.h"
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
inline constexpr qsizetype kPinloomHostPreviewDescriptorMaxBytes = 16 * 1024;

struct PinloomHostIdentity {
    QString entryId;
    QString resourceId;
    QString anchorId;
    QString clipId;

    bool isValid() const;
};

struct PinloomHostPreviewDescriptor {
    QString kind = QStringLiteral("image");
    QString state;
    QString mimeType;
    QUrl uri;
    QString filePath;
    qint64 byteSize = -1;
    int pixelWidth = 0;
    int pixelHeight = 0;
    int page = -1;
    bool cropped = false;
    QString altText;
    QString error;

    bool isValid() const;
};

struct PinloomHostDocument {
    PinloomEntry entry;
    QString content;
    QString contentType = QStringLiteral("text/plain");
    QVariantMap details;
    std::optional<PinloomHostPreviewDescriptor> preview;
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

class ILibraryRepository;

std::optional<PinloomEntry> createPinloomSourceAnchor(
    ILibraryRepository &repository,
    const PinloomSourceAnchorRequest &request,
    QString *status = nullptr);

QString defaultPinloomHostBridgeServerName();
PinloomHostIdentity pinloomHostIdentityForEntry(const PinloomEntry &entry);
QUrl pinloomHostUri(const PinloomHostIdentity &identity);
std::optional<PinloomHostIdentity> pinloomHostIdentityFromUri(
    const QUrl &uri);
std::optional<PinloomHostIdentity> pinloomHostIdentityFromUri(
    const QString &uri);
QJsonObject pinloomHostIdentityToJson(const PinloomHostIdentity &identity);
std::optional<PinloomHostIdentity> pinloomHostIdentityFromJson(
    const QJsonObject &object);
QJsonObject pinloomHostEntryToJson(const PinloomEntry &entry);
QJsonObject pinloomHostPreviewToJson(
    const PinloomHostPreviewDescriptor &preview);
QJsonObject pinloomHostDocumentToJson(const PinloomHostDocument &document);
PinloomHostDocument pinloomHostDocumentForResource(
    const Resource &resource,
    const std::optional<Anchor> &anchor = std::nullopt,
    const std::optional<ResourceUsage> &usage = std::nullopt,
    const PdfLocatorPreviewRenderOptions &previewOptions = {});

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
