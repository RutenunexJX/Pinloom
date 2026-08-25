#include "pinloom/widgets/PinloomHostBridge.h"

#include "pinloom/core/AnchorLocator.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>
#include <utility>

namespace Pinloom {

namespace {

constexpr const char *kBufferProperty = "pinloomHostBridgeBuffer";
constexpr const char *kHandledProperty = "pinloomHostBridgeHandled";

QString entryTypeName(PinloomEntryType type)
{
    switch (type) {
    case PinloomEntryType::Anchor:
        return QStringLiteral("anchor");
    case PinloomEntryType::SavedClip:
        return QStringLiteral("clip");
    case PinloomEntryType::Inbox:
        return QStringLiteral("inbox");
    case PinloomEntryType::FileResource:
        return QStringLiteral("resource");
    }
    return QStringLiteral("resource");
}

QJsonArray stringArray(const QStringList &values)
{
    QJsonArray result;
    for (const QString &value : values) result.append(value);
    return result;
}

QJsonObject baseResponse(const QJsonObject &request)
{
    return {
        {QStringLiteral("protocol"), QString::fromLatin1(kPinloomHostProtocol)},
        {QStringLiteral("requestId"), request.value(QStringLiteral("requestId"))},
    };
}

QJsonObject successResponse(const QJsonObject &request,
                            const QJsonObject &result)
{
    QJsonObject response = baseResponse(request);
    response.insert(QStringLiteral("ok"), true);
    response.insert(QStringLiteral("result"), result);
    return response;
}

QJsonObject errorResponse(const QJsonObject &request,
                          const QString &code,
                          const QString &message)
{
    QJsonObject response = baseResponse(request);
    response.insert(QStringLiteral("ok"), false);
    response.insert(
        QStringLiteral("error"),
        QJsonObject{{QStringLiteral("code"), code},
                    {QStringLiteral("message"), message}});
    return response;
}

PinloomHostBridgeOptions normalizedOptions(PinloomHostBridgeOptions options)
{
    options.serverName = options.serverName.trimmed();
    if (options.serverName.isEmpty())
        options.serverName = defaultPinloomHostBridgeServerName();
    options.requestTimeoutMs = std::max(100, options.requestTimeoutMs);
    options.maxRequestBytes = std::max<qsizetype>(1024, options.maxRequestBytes);
    options.maxSearchResults = std::clamp(options.maxSearchResults, 1, 500);
    return options;
}

std::optional<PinloomSourceAnchorRequest> sourceAnchorRequestFromJson(
    const QJsonObject &object)
{
    PinloomSourceAnchorRequest request;
    request.title = object.value(QStringLiteral("title")).toString().trimmed();
    request.content = object.value(QStringLiteral("content")).toString();
    request.workspaceRoot =
        object.value(QStringLiteral("workspaceRoot")).toString().trimmed();
    request.relativeFilePath =
        object.value(QStringLiteral("relativeFilePath")).toString().trimmed();
    request.absoluteFilePath =
        object.value(QStringLiteral("absoluteFilePath")).toString().trimmed();
    request.moduleName =
        object.value(QStringLiteral("moduleName")).toString().trimmed();
    request.startLine = object.value(QStringLiteral("startLine")).toInt();
    request.startColumn = object.value(QStringLiteral("startColumn")).toInt();
    request.endLine = object.value(QStringLiteral("endLine")).toInt();
    request.endColumn = object.value(QStringLiteral("endColumn")).toInt();
    request.selectedTextHash =
        object.value(QStringLiteral("selectedTextHash")).toString().trimmed();
    request.prefixContext =
        object.value(QStringLiteral("prefixContext")).toString();
    request.suffixContext =
        object.value(QStringLiteral("suffixContext")).toString();
    return request.isValid()
        ? std::optional<PinloomSourceAnchorRequest>(request)
        : std::nullopt;
}

QStringList mergedValues(const QStringList &primary,
                         const QStringList &secondary)
{
    QStringList result;
    for (const QStringList *values : {&primary, &secondary}) {
        for (const QString &value : *values) {
            const QString normalized = value.trimmed();
            if (normalized.isEmpty()
                || result.contains(normalized, Qt::CaseInsensitive)) {
                continue;
            }
            result.append(normalized);
        }
    }
    return result;
}

QString anchorFileName(const Resource &resource,
                       const std::optional<Anchor> &anchor)
{
    QStringList candidates;
    if (anchor.has_value()) {
        candidates.append(anchor->targetFile);
        candidates.append(anchor->targetUri);
    }
    candidates.append(resource.location);
    for (QString candidate : candidates) {
        candidate = candidate.trimmed();
        if (candidate.isEmpty()) continue;
        const QUrl url(candidate);
        if (url.isLocalFile()) candidate = url.toLocalFile();
        if (url.isValid() && !url.scheme().isEmpty() && !url.isLocalFile()) {
            const QString name = url.fileName().trimmed();
            if (!name.isEmpty()) return name;
            continue;
        }
        const QString name = QFileInfo(candidate).fileName().trimmed();
        if (!name.isEmpty()) return name;
    }
    return {};
}

QString previewAltText(const QString &title,
                       const QString &fileName,
                       int page)
{
    QStringList parts;
    if (!title.trimmed().isEmpty()) parts.append(title.trimmed());
    if (!fileName.trimmed().isEmpty()) parts.append(fileName.trimmed());
    if (page > 0) parts.append(QStringLiteral("Page %1").arg(page));
    return parts.join(QStringLiteral(" | "));
}

} // namespace

bool PinloomSourceAnchorRequest::isValid() const
{
    return !content.isEmpty()
        && !absoluteFilePath.trimmed().isEmpty()
        && startLine > 0
        && startColumn > 0
        && endLine >= startLine
        && endColumn > 0;
}

bool PinloomHostIdentity::isValid() const
{
    return !entryId.trimmed().isEmpty()
        || !resourceId.trimmed().isEmpty()
        || !clipId.trimmed().isEmpty();
}

bool PinloomHostPreviewDescriptor::isValid() const
{
    const QString normalizedKind = kind.trimmed().toLower();
    const QString normalizedState = state.trimmed().toLower();
    if (normalizedKind != QLatin1String("image")
        || (normalizedState != QLatin1String("ready")
            && normalizedState != QLatin1String("unavailable")
            && normalizedState != QLatin1String("error"))) {
        return false;
    }
    if (!uri.isEmpty() && !uri.isLocalFile()) return false;
    if (normalizedState == QLatin1String("ready")) {
        return uri.isLocalFile()
            && !filePath.trimmed().isEmpty()
            && mimeType.trimmed().compare(QStringLiteral("image/png"),
                                           Qt::CaseInsensitive) == 0
            && byteSize >= 0
            && pixelWidth > 0
            && pixelHeight > 0;
    }
    return uri.isEmpty()
        && filePath.trimmed().isEmpty()
        && !error.trimmed().isEmpty();
}

QString defaultPinloomHostBridgeServerName()
{
    return QStringLiteral("pinloom.Pinloom.Pinloom.host.v1");
}

PinloomHostIdentity pinloomHostIdentityForEntry(const PinloomEntry &entry)
{
    PinloomHostIdentity identity;
    identity.entryId = entry.id.trimmed();
    identity.resourceId = entry.resourceId.trimmed();
    identity.clipId = entry.clipId.trimmed();
    if (entry.anchor.has_value())
        identity.anchorId = entry.anchor->id.trimmed();
    return identity;
}

QUrl pinloomHostUri(const PinloomHostIdentity &identity)
{
    if (!identity.isValid()) return {};
    QUrl uri;
    uri.setScheme(QStringLiteral("pinloom"));
    uri.setHost(QStringLiteral("entry"));
    uri.setPath(QStringLiteral("/") + identity.entryId);
    QUrlQuery query;
    if (!identity.resourceId.isEmpty())
        query.addQueryItem(QStringLiteral("resource"), identity.resourceId);
    if (!identity.anchorId.isEmpty())
        query.addQueryItem(QStringLiteral("anchor"), identity.anchorId);
    if (!identity.clipId.isEmpty())
        query.addQueryItem(QStringLiteral("clip"), identity.clipId);
    uri.setQuery(query);
    return uri;
}

QJsonObject pinloomHostIdentityToJson(const PinloomHostIdentity &identity)
{
    return {
        {QStringLiteral("entryId"), identity.entryId},
        {QStringLiteral("resourceId"), identity.resourceId},
        {QStringLiteral("anchorId"), identity.anchorId},
        {QStringLiteral("clipId"), identity.clipId},
    };
}

std::optional<PinloomHostIdentity> pinloomHostIdentityFromJson(
    const QJsonObject &object)
{
    PinloomHostIdentity identity;
    identity.entryId = object.value(QStringLiteral("entryId")).toString().trimmed();
    identity.resourceId = object.value(QStringLiteral("resourceId")).toString().trimmed();
    identity.anchorId = object.value(QStringLiteral("anchorId")).toString().trimmed();
    identity.clipId = object.value(QStringLiteral("clipId")).toString().trimmed();
    if (!identity.isValid()) return std::nullopt;
    return identity;
}

QJsonObject pinloomHostEntryToJson(const PinloomEntry &entry)
{
    const PinloomHostIdentity identity = pinloomHostIdentityForEntry(entry);
    QJsonObject result{
        {QStringLiteral("identity"), pinloomHostIdentityToJson(identity)},
        {QStringLiteral("uri"), pinloomHostUri(identity).toString(QUrl::FullyEncoded)},
        {QStringLiteral("type"), entryTypeName(entry.type)},
        {QStringLiteral("title"), entry.name},
        {QStringLiteral("aliases"), stringArray(entry.aliases)},
        {QStringLiteral("tags"), stringArray(entry.tags)},
        {QStringLiteral("pinned"), entry.pinned},
        {QStringLiteral("deleted"), entry.deleted},
        {QStringLiteral("frequency"), entry.frequency},
        {QStringLiteral("summary"), entry.targetSummary},
        {QStringLiteral("location"), entry.location},
        {QStringLiteral("matchedField"), entry.matchedField},
        {QStringLiteral("matchSummary"), entry.matchSummary},
        {QStringLiteral("metadata"), QJsonObject::fromVariantMap(entry.metadata)},
    };
    if (entry.usedAt.isValid()) {
        result.insert(QStringLiteral("usedAt"),
                      entry.usedAt.toUTC().toString(Qt::ISODateWithMs));
    }
    if (entry.anchor.has_value()) {
        const Anchor &anchor = entry.anchor.value();
        result.insert(
            QStringLiteral("anchor"),
            QJsonObject{
                {QStringLiteral("id"), anchor.id},
                {QStringLiteral("name"), anchor.name},
                {QStringLiteral("targetApp"), anchor.targetApp},
                {QStringLiteral("targetFile"), anchor.targetFile},
                {QStringLiteral("targetUri"), anchor.targetUri},
                {QStringLiteral("locatorType"), anchor.locatorType},
                {QStringLiteral("locatorJson"), anchor.locatorJson},
            });
    }
    return result;
}

QJsonObject pinloomHostPreviewToJson(
    const PinloomHostPreviewDescriptor &preview)
{
    if (!preview.isValid()) return {};

    QJsonObject result{
        {QStringLiteral("kind"), preview.kind.trimmed().toLower()},
        {QStringLiteral("state"), preview.state.trimmed().toLower()},
        {QStringLiteral("page"), preview.page},
        {QStringLiteral("cropped"), preview.cropped},
    };
    if (!preview.mimeType.trimmed().isEmpty()) {
        result.insert(QStringLiteral("mimeType"), preview.mimeType.trimmed().toLower());
    }
    if (!preview.uri.isEmpty()) {
        result.insert(QStringLiteral("uri"),
                      preview.uri.toString(QUrl::FullyEncoded));
    }
    if (!preview.filePath.trimmed().isEmpty()) {
        result.insert(QStringLiteral("filePath"), preview.filePath.trimmed());
    }
    if (preview.byteSize >= 0) {
        result.insert(QStringLiteral("byteSize"), preview.byteSize);
    }
    if (preview.pixelWidth > 0) {
        result.insert(QStringLiteral("pixelWidth"), preview.pixelWidth);
    }
    if (preview.pixelHeight > 0) {
        result.insert(QStringLiteral("pixelHeight"), preview.pixelHeight);
    }
    if (!preview.altText.trimmed().isEmpty()) {
        result.insert(QStringLiteral("altText"), preview.altText.trimmed());
    }
    if (!preview.error.trimmed().isEmpty()) {
        result.insert(QStringLiteral("error"), preview.error.trimmed());
    }

    if (QJsonDocument(result).toJson(QJsonDocument::Compact).size()
        <= kPinloomHostPreviewDescriptorMaxBytes) {
        return result;
    }
    result.remove(QStringLiteral("altText"));
    if (QJsonDocument(result).toJson(QJsonDocument::Compact).size()
        <= kPinloomHostPreviewDescriptorMaxBytes) {
        return result;
    }
    return {
        {QStringLiteral("kind"), QStringLiteral("image")},
        {QStringLiteral("state"), QStringLiteral("error")},
        {QStringLiteral("page"), preview.page},
        {QStringLiteral("cropped"), false},
        {QStringLiteral("error"),
         QStringLiteral("Preview descriptor exceeds the host size limit")},
    };
}

QJsonObject pinloomHostDocumentToJson(const PinloomHostDocument &document)
{
    QJsonObject result{
        {QStringLiteral("entry"), pinloomHostEntryToJson(document.entry)},
        {QStringLiteral("content"), document.content},
        {QStringLiteral("contentType"), document.contentType},
        {QStringLiteral("details"), QJsonObject::fromVariantMap(document.details)},
    };
    if (document.preview.has_value()) {
        const QJsonObject preview = pinloomHostPreviewToJson(*document.preview);
        if (!preview.isEmpty()) result.insert(QStringLiteral("preview"), preview);
    }
    return result;
}

PinloomHostDocument pinloomHostDocumentForResource(
    const Resource &resource,
    const std::optional<Anchor> &anchor,
    const std::optional<ResourceUsage> &usage,
    const PdfLocatorPreviewRenderOptions &previewOptions)
{
    PinloomOpenTarget target;
    target.resourceId = resource.id;
    target.resourceKind = resource.kind;
    target.title = resource.title;
    target.location = resource.location;
    target.deleted = resource.deleted;
    target.anchor = anchor;
    if (anchor.has_value()) {
        target.title = anchor->name.trimmed().isEmpty()
            ? resource.title
            : anchor->name;
        target.deleted = target.deleted || anchor->deleted;
    }

    PinloomEntry entry = entryFromOpenTarget(target);
    entry.aliases = anchor.has_value()
        ? mergedValues(anchor->aliases, resource.aliases)
        : resource.aliases;
    entry.tags = anchor.has_value()
        ? mergedValues(anchor->tags, resource.tags)
        : resource.tags;
    if (!anchor.has_value() && usage.has_value()) {
        entry.pinned = usage->pinned;
        entry.usedAt = usage->lastOpenedAt;
        entry.frequency = usage->openCount;
    }

    const QString fileName = anchorFileName(resource, anchor);
    const int page = anchor.has_value() ? anchorLocatorPage(*anchor) : -1;
    entry.metadata.insert(QStringLiteral("title"), entry.name);
    entry.metadata.insert(QStringLiteral("fileName"), fileName);
    if (page > 0) entry.metadata.insert(QStringLiteral("page"), page);
    entry.metadata.insert(QStringLiteral("aliases"), entry.aliases);
    entry.metadata.insert(QStringLiteral("tags"), entry.tags);

    PinloomHostDocument document;
    document.entry = entry;
    document.content = resource.content;
    document.details.insert(QStringLiteral("resourceId"), resource.id);
    document.details.insert(QStringLiteral("title"), entry.name);
    document.details.insert(QStringLiteral("fileName"), fileName);
    document.details.insert(QStringLiteral("location"), resource.location);
    document.details.insert(QStringLiteral("aliases"), entry.aliases);
    document.details.insert(QStringLiteral("tags"), entry.tags);
    if (page > 0) document.details.insert(QStringLiteral("page"), page);

    if (!anchor.has_value()) return document;

    document.details.insert(QStringLiteral("anchorId"), anchor->id);
    document.details.insert(QStringLiteral("targetApp"), anchor->targetApp);
    document.details.insert(QStringLiteral("locatorType"), anchor->locatorType);
    document.details.insert(QStringLiteral("locatorJson"), anchor->locatorJson);
    document.details.insert(
        QStringLiteral("locator"),
        QVariantMap{
            {QStringLiteral("targetApp"), anchor->targetApp},
            {QStringLiteral("targetFile"), anchor->targetFile},
            {QStringLiteral("targetUri"), anchor->targetUri},
            {QStringLiteral("type"), anchor->locatorType},
            {QStringLiteral("json"), anchor->locatorJson},
        });

    if (anchorLocatorType(*anchor) != QLatin1String("sumatrapdf.rect")) {
        return document;
    }

    const PdfLocatorPreviewRenderResult rendered =
        renderPdfLocatorPreview(resource, *anchor, previewOptions);
    PinloomHostPreviewDescriptor preview;
    preview.page = rendered.page > 0 ? rendered.page : page;
    preview.cropped = rendered.cropped;
    preview.altText = previewAltText(entry.name, fileName, preview.page);
    const QFileInfo cacheFile(rendered.cacheFilePath);
    if (rendered.success()
        && !rendered.cacheFilePath.trimmed().isEmpty()
        && cacheFile.exists()
        && cacheFile.isFile()) {
        preview.state = QStringLiteral("ready");
        preview.mimeType = QStringLiteral("image/png");
        preview.filePath = cacheFile.absoluteFilePath();
        preview.uri = QUrl::fromLocalFile(preview.filePath);
        preview.byteSize = cacheFile.size();
        preview.pixelWidth = rendered.image.width();
        preview.pixelHeight = rendered.image.height();
    } else {
        preview.state = QStringLiteral("unavailable");
        preview.cropped = false;
        preview.error = rendered.error.trimmed().isEmpty()
            ? QStringLiteral("PDF preview cache file is unavailable")
            : rendered.error.trimmed();
    }
    document.preview = preview;
    return document;
}

PinloomHostBridgeServer::PinloomHostBridgeServer(
    PinloomHostBridgeOptions options,
    PinloomHostBridgeCallbacks callbacks,
    QObject *parent)
    : QObject(parent)
    , options_(normalizedOptions(std::move(options)))
    , callbacks_(std::move(callbacks))
    , server_(new QLocalServer(this))
{
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    connect(server_, &QLocalServer::newConnection,
            this, &PinloomHostBridgeServer::handlePendingConnections);
}

PinloomHostBridgeServer::~PinloomHostBridgeServer()
{
    if (server_->isListening()) server_->close();
}

bool PinloomHostBridgeServer::start()
{
    if (server_->isListening()) return true;
    if (server_->listen(options_.serverName)) {
        lastError_.clear();
        return true;
    }

    QLocalSocket probe;
    probe.connectToServer(options_.serverName, QIODevice::ReadWrite);
    if (probe.waitForConnected(80)) {
        probe.disconnectFromServer();
        lastError_ = QStringLiteral("Another Pinloom host bridge is already active");
        return false;
    }

    QLocalServer::removeServer(options_.serverName);
    if (server_->listen(options_.serverName)) {
        lastError_.clear();
        return true;
    }
    lastError_ = server_->errorString().trimmed();
    if (lastError_.isEmpty())
        lastError_ = QStringLiteral("Unable to start the Pinloom host bridge");
    return false;
}

bool PinloomHostBridgeServer::isListening() const
{
    return server_->isListening();
}

QString PinloomHostBridgeServer::serverName() const
{
    return options_.serverName;
}

QString PinloomHostBridgeServer::lastError() const
{
    return lastError_;
}

void PinloomHostBridgeServer::handlePendingConnections()
{
    while (server_->hasPendingConnections()) {
        QLocalSocket *socket = server_->nextPendingConnection();
        if (!socket) continue;
        socket->setParent(this);
        connect(socket, &QLocalSocket::readyRead, this,
                [this, socket]() { consumeSocket(socket, false); });
        connect(socket, &QLocalSocket::disconnected, this,
                [this, socket]() { consumeSocket(socket, true); });
        QTimer::singleShot(options_.requestTimeoutMs, socket,
                           [this, socket]() { consumeSocket(socket, true); });
    }
}

void PinloomHostBridgeServer::consumeSocket(QLocalSocket *socket,
                                             bool allowIncompletePayload)
{
    if (!socket || socket->property(kHandledProperty).toBool()) return;
    QByteArray buffer = socket->property(kBufferProperty).toByteArray();
    buffer.append(socket->readAll());
    socket->setProperty(kBufferProperty, buffer);

    if (buffer.size() > options_.maxRequestBytes) {
        socket->setProperty(kHandledProperty, true);
        finishSocket(socket,
                     errorResponse({}, QStringLiteral("request_too_large"),
                                   QStringLiteral("Request exceeds the bridge size limit")));
        return;
    }

    const qsizetype newline = buffer.indexOf('\n');
    if (newline < 0 && !allowIncompletePayload) return;
    const QByteArray payload = (newline >= 0 ? buffer.left(newline) : buffer).trimmed();
    socket->setProperty(kHandledProperty, true);
    if (payload.isEmpty()) {
        finishSocket(socket,
                     errorResponse({}, QStringLiteral("empty_request"),
                                   QStringLiteral("Request body is empty")));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        finishSocket(socket,
                     errorResponse({}, QStringLiteral("invalid_json"),
                                   QStringLiteral("Request is not a JSON object")));
        return;
    }
    finishSocket(socket, processRequest(document.object()));
}

QJsonObject PinloomHostBridgeServer::processRequest(
    const QJsonObject &request) const
{
    if (request.value(QStringLiteral("protocol")).toString()
        != QString::fromLatin1(kPinloomHostProtocol)) {
        return errorResponse(request, QStringLiteral("unsupported_protocol"),
                             QStringLiteral("Unsupported Pinloom host protocol"));
    }
    const QString method = request.value(QStringLiteral("method")).toString().trimmed();
    const QJsonObject params = request.value(QStringLiteral("params")).toObject();

    if (method == QLatin1String("capabilities")) {
        return successResponse(
            request,
            QJsonObject{
                {QStringLiteral("protocol"), QString::fromLatin1(kPinloomHostProtocol)},
                {QStringLiteral("applicationVersion"), QCoreApplication::applicationVersion()},
                {QStringLiteral("methods"),
                 QJsonArray{QStringLiteral("capabilities"),
                            QStringLiteral("search"),
                            QStringLiteral("resolve"),
                            QStringLiteral("open"),
                            QStringLiteral("createSourceAnchor")}},
            });
    }

    if (method == QLatin1String("search")) {
        if (!callbacks_.search) {
            return errorResponse(request, QStringLiteral("unavailable"),
                                 QStringLiteral("Pinloom search is unavailable"));
        }
        const int requestedLimit = params.value(QStringLiteral("limit")).toInt(30);
        const int limit = std::clamp(requestedLimit, 1, options_.maxSearchResults);
        QList<PinloomEntry> entries =
            callbacks_.search(params.value(QStringLiteral("query")).toString(), limit);
        if (entries.size() > limit) entries.resize(limit);
        QJsonArray serialized;
        for (const PinloomEntry &entry : entries)
            serialized.append(pinloomHostEntryToJson(entry));
        return successResponse(request,
                               QJsonObject{{QStringLiteral("entries"), serialized}});
    }

    if (method == QLatin1String("createSourceAnchor")) {
        if (!callbacks_.createSourceAnchor) {
            return errorResponse(
                request,
                QStringLiteral("unavailable"),
                QStringLiteral("Pinloom source-anchor creation is unavailable"));
        }
        const std::optional<PinloomSourceAnchorRequest> sourceRequest =
            sourceAnchorRequestFromJson(params);
        if (!sourceRequest.has_value()) {
            return errorResponse(
                request,
                QStringLiteral("invalid_source_anchor"),
                QStringLiteral("A non-empty code selection and valid source range are required"));
        }
        QString status;
        const std::optional<PinloomEntry> entry =
            callbacks_.createSourceAnchor(*sourceRequest, &status);
        if (!entry.has_value()) {
            return errorResponse(
                request,
                QStringLiteral("create_failed"),
                status.trimmed().isEmpty()
                    ? QStringLiteral("Pinloom could not create the source anchor")
                    : status.trimmed());
        }
        return successResponse(
            request,
            QJsonObject{{QStringLiteral("entry"),
                         pinloomHostEntryToJson(*entry)},
                        {QStringLiteral("message"), status.trimmed()}});
    }

    const std::optional<PinloomHostIdentity> identity =
        pinloomHostIdentityFromJson(params.value(QStringLiteral("identity")).toObject());
    if (!identity.has_value()) {
        return errorResponse(request, QStringLiteral("invalid_identity"),
                             QStringLiteral("A stable Pinloom identity is required"));
    }

    if (method == QLatin1String("resolve")) {
        if (!callbacks_.resolve) {
            return errorResponse(request, QStringLiteral("unavailable"),
                                 QStringLiteral("Pinloom resolve is unavailable"));
        }
        const std::optional<PinloomHostDocument> document = callbacks_.resolve(*identity);
        if (!document.has_value()) {
            return errorResponse(request, QStringLiteral("not_found"),
                                 QStringLiteral("Pinloom entry no longer exists"));
        }
        return successResponse(request, pinloomHostDocumentToJson(*document));
    }

    if (method == QLatin1String("open")) {
        if (!callbacks_.open) {
            return errorResponse(request, QStringLiteral("unavailable"),
                                 QStringLiteral("Pinloom open is unavailable"));
        }
        QString status;
        const bool opened = callbacks_.open(*identity, &status);
        if (!opened) {
            return errorResponse(request, QStringLiteral("open_failed"),
                                 status.trimmed().isEmpty()
                                     ? QStringLiteral("Pinloom could not open the entry")
                                     : status.trimmed());
        }
        return successResponse(
            request,
            QJsonObject{{QStringLiteral("message"),
                         status.trimmed().isEmpty()
                             ? QStringLiteral("Pinloom entry opened")
                             : status.trimmed()}});
    }

    return errorResponse(request, QStringLiteral("unknown_method"),
                         QStringLiteral("Unknown Pinloom host method"));
}

void PinloomHostBridgeServer::finishSocket(QLocalSocket *socket,
                                            const QJsonObject &response)
{
    if (!socket) return;
    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
    socket->flush();
    socket->disconnectFromServer();
    if (socket->state() == QLocalSocket::UnconnectedState)
        socket->deleteLater();
    else
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater,
                Qt::UniqueConnection);
}

} // namespace Pinloom
