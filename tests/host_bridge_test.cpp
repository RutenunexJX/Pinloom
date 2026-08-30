#include "pinloom/clip/ClipRepository.h"
#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/widgets/PinloomHostBridge.h"
#include "pinloom/widgets/PinloomOpenService.h"
#include "pinloom/widgets/SumatraPdfViewerAdapter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace Pinloom;

namespace {
QJsonObject exchange(const QString &serverName, QJsonObject request)
{
    QLocalSocket socket;
    socket.connectToServer(serverName, QIODevice::ReadWrite);
    if (!socket.waitForConnected(500)) return {};
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
    socket.write("\n");
    socket.flush();

    QElapsedTimer timer;
    timer.start();
    QByteArray response;
    while (timer.elapsed() < 1500) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        response.append(socket.readAll());
        const qsizetype newline = response.indexOf('\n');
        if (newline >= 0) {
            const QJsonDocument document =
                QJsonDocument::fromJson(response.left(newline));
            return document.isObject() ? document.object() : QJsonObject{};
        }
        socket.waitForReadyRead(20);
    }
    return {};
}

QJsonObject request(const QString &method,
                    const QJsonObject &params = {})
{
    return {
        {QStringLiteral("protocol"), QString::fromLatin1(kPinloomHostProtocol)},
        {QStringLiteral("requestId"), QStringLiteral("request-1")},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params},
    };
}

class RecordingPdfPresenter final : public PdfAnchorPresenter {
public:
    using PdfAnchorPresenter::PdfAnchorPresenter;

    PdfAnchorPresentationStartResult present(
        const PdfAnchorPresentationRequest &requestValue,
        PdfAnchorPresentationCallbacks callbacks) override
    {
        lastRequest = requestValue;
        ++presentCount;
        active = 1;
        PdfAnchorPresentationStartResult start;
        start.requestId = ++lastId;
        QTimer::singleShot(0, this, [this, callbacks, id = start.requestId, requestValue]() {
            active = 0;
            PdfAnchorPresentationResult result;
            result.requestId = id;
            result.sourceFilePath = requestValue.sourceCommand.filePath;
            result.previewFilePath = QStringLiteral("E:/cache/host - Pinloom Preview.pdf");
            result.previewCommand = requestValue.sourceCommand;
            result.previewCommand.filePath = result.previewFilePath;
            if (callbacks.completed) callbacks.completed(result);
        });
        return start;
    }

    void cancelPending() override { active = 0; }
    int activeRequestCount() const override { return active; }

    PdfAnchorPresentationRequest lastRequest;
    int presentCount = 0;
    int active = 0;
    quint64 lastId = 0;
};
} // namespace

class HostBridgeTest final : public QObject {
    Q_OBJECT

private slots:
    void stableDeepLinksRoundTripAndRejectMalformedIds();
    void rejectsDeletedIdentityExplicitly();
    void servesVersionedSearchResolveAndOpen();
    void resolvesPdfRectangleWithBoundedLocalPreview();
    void hostOpenUsesSharedPdfAnchorPresenter();
    void createsValidatedSourceAnchor();
    void hostSourceAnchorWriteReturnsGlobalIdentityConflict();
};

void HostBridgeTest::stableDeepLinksRoundTripAndRejectMalformedIds()
{
    PinloomHostIdentity anchor;
    anchor.entryId = QStringLiteral("anchor:anchor-a");
    anchor.resourceId = QStringLiteral("resource-a");
    anchor.anchorId = QStringLiteral("anchor-a");
    QCOMPARE(pinloomHostUri(anchor).toString(QUrl::FullyEncoded),
             QStringLiteral("pinloom://anchor/anchor-a"));
    const std::optional<PinloomHostIdentity> parsedAnchor =
        pinloomHostIdentityFromUri(QStringLiteral("pinloom://anchor/anchor-a"));
    QVERIFY(parsedAnchor.has_value());
    QCOMPARE(parsedAnchor->anchorId, QStringLiteral("anchor-a"));
    QVERIFY(parsedAnchor->resourceId.isEmpty());

    PinloomHostIdentity clip;
    clip.entryId = QStringLiteral("clip:clip-a");
    clip.clipId = QStringLiteral("clip-a");
    QCOMPARE(pinloomHostUri(clip).toString(QUrl::FullyEncoded),
             QStringLiteral("pinloom://clip/clip-a"));
    const std::optional<PinloomHostIdentity> parsedClip =
        pinloomHostIdentityFromUri(QStringLiteral("pinloom://clip/clip-a"));
    QVERIFY(parsedClip.has_value());
    QCOMPARE(parsedClip->clipId, QStringLiteral("clip-a"));

    const std::optional<PinloomHostIdentity> legacy =
        pinloomHostIdentityFromUri(QStringLiteral(
            "pinloom://entry/anchor:anchor-a?resource=resource-a&anchor=anchor-a"));
    QVERIFY(legacy.has_value());
    QCOMPARE(legacy->resourceId, QStringLiteral("resource-a"));
    QCOMPARE(legacy->anchorId, QStringLiteral("anchor-a"));

    QVERIFY(!pinloomHostIdentityFromUri(
        QStringLiteral("pinloom://anchor/")).has_value());
    QVERIFY(!pinloomHostIdentityFromUri(
        QStringLiteral("pinloom://anchor/a/b")).has_value());
    QVERIFY(!pinloomHostIdentityFromUri(
        QStringLiteral("https://anchor/anchor-a")).has_value());
}

void HostBridgeTest::rejectsDeletedIdentityExplicitly()
{
    PinloomHostBridgeCallbacks callbacks;
    callbacks.resolve = [](const PinloomHostIdentity &identity)
        -> std::optional<PinloomHostDocument> {
        PinloomHostDocument document;
        document.entry.id = QStringLiteral("clip:") + identity.clipId;
        document.entry.clipId = identity.clipId;
        document.entry.name = QStringLiteral("Deleted Clip");
        document.entry.deleted = true;
        return document;
    };
    PinloomHostBridgeOptions options;
    options.serverName = QStringLiteral("pinloom-host-deleted-test-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QDateTime::currentMSecsSinceEpoch());
    PinloomHostBridgeServer server(options, callbacks);
    QVERIFY2(server.start(), qPrintable(server.lastError()));

    PinloomHostIdentity identity;
    identity.clipId = QStringLiteral("deleted-clip");
    const QJsonObject response = exchange(
        options.serverName,
        request(QStringLiteral("resolve"),
                QJsonObject{{QStringLiteral("identity"),
                             pinloomHostIdentityToJson(identity)}}));
    QVERIFY(!response.value(QStringLiteral("ok")).toBool());
    QCOMPARE(response.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("code")).toString(),
             QStringLiteral("deleted"));
}

void HostBridgeTest::servesVersionedSearchResolveAndOpen()
{
    PinloomEntry entry;
    entry.id = QStringLiteral("anchor:anchor-a");
    entry.type = PinloomEntryType::Anchor;
    entry.name = QStringLiteral("Clock reset notes");
    entry.resourceId = QStringLiteral("resource-a");
    entry.location = QStringLiteral("E:/docs/clock.md");
    Anchor anchor;
    anchor.id = QStringLiteral("anchor-a");
    anchor.name = entry.name;
    anchor.locatorType = QStringLiteral("text.line");
    entry.anchor = anchor;

    int openCount = 0;
    PinloomHostBridgeCallbacks callbacks;
    callbacks.search = [entry](const QString &query, int limit) {
        if (query != QLatin1String("clock") || limit != 7) return QList<PinloomEntry>{};
        return QList<PinloomEntry>{entry};
    };
    callbacks.resolve = [entry](const PinloomHostIdentity &identity)
        -> std::optional<PinloomHostDocument> {
        if (identity.resourceId != QLatin1String("resource-a")
            || identity.anchorId != QLatin1String("anchor-a")) {
            return std::nullopt;
        }
        PinloomHostDocument document;
        document.entry = entry;
        document.content = QStringLiteral("Reset crosses the clock domain here.");
        document.details.insert(QStringLiteral("line"), 42);
        return document;
    };
    callbacks.open = [&openCount](const PinloomHostIdentity &identity, QString *status) {
        if (identity.anchorId != QLatin1String("anchor-a")) return false;
        ++openCount;
        if (status) *status = QStringLiteral("Opened anchor");
        return true;
    };

    PinloomHostBridgeOptions options;
    options.serverName = QStringLiteral("pinloom-host-test-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QDateTime::currentMSecsSinceEpoch());
    PinloomHostBridgeServer server(options, callbacks);
    QVERIFY2(server.start(), qPrintable(server.lastError()));

    QJsonObject response = exchange(options.serverName, request(QStringLiteral("capabilities")));
    QVERIFY(response.value(QStringLiteral("ok")).toBool());
    QCOMPARE(response.value(QStringLiteral("protocol")).toString(),
             QString::fromLatin1(kPinloomHostProtocol));

    response = exchange(
        options.serverName,
        request(QStringLiteral("search"),
                QJsonObject{{QStringLiteral("query"), QStringLiteral("clock")},
                            {QStringLiteral("limit"), 7}}));
    QVERIFY(response.value(QStringLiteral("ok")).toBool());
    const QJsonArray entries = response.value(QStringLiteral("result"))
                                   .toObject().value(QStringLiteral("entries")).toArray();
    QCOMPARE(entries.size(), 1);
    const QJsonObject serialized = entries.first().toObject();
    QCOMPARE(serialized.value(QStringLiteral("title")).toString(), entry.name);
    QCOMPARE(serialized.value(QStringLiteral("uri")).toString(),
             QStringLiteral("pinloom://anchor/anchor-a"));

    const QJsonObject identity = serialized.value(QStringLiteral("identity")).toObject();
    response = exchange(
        options.serverName,
        request(QStringLiteral("resolve"),
                QJsonObject{{QStringLiteral("identity"), identity}}));
    QVERIFY(response.value(QStringLiteral("ok")).toBool());
    QCOMPARE(response.value(QStringLiteral("result")).toObject()
                 .value(QStringLiteral("content")).toString(),
             QStringLiteral("Reset crosses the clock domain here."));
    QVERIFY(!response.value(QStringLiteral("result")).toObject()
                 .contains(QStringLiteral("preview")));

    response = exchange(
        options.serverName,
        request(QStringLiteral("open"),
                QJsonObject{{QStringLiteral("identity"), identity}}));
    QVERIFY(response.value(QStringLiteral("ok")).toBool());
    QCOMPARE(openCount, 1);

    QJsonObject invalid = request(QStringLiteral("search"));
    invalid.insert(QStringLiteral("protocol"), QStringLiteral("pinloom-host/v99"));
    response = exchange(options.serverName, invalid);
    QVERIFY(!response.value(QStringLiteral("ok")).toBool());
    QCOMPARE(response.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("code")).toString(),
             QStringLiteral("unsupported_protocol"));
}

void HostBridgeTest::resolvesPdfRectangleWithBoundedLocalPreview()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdfPath = directory.filePath(QStringLiteral("clock.pdf"));
    QFile pdf(pdfPath);
    QVERIFY(pdf.open(QIODevice::WriteOnly));
    QVERIFY(pdf.write("%PDF-1.4\n% host preview fixture\n") > 0);
    pdf.close();

    Resource resource;
    resource.id = QStringLiteral("resource-pdf");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Clock domain report");
    resource.location = pdfPath;
    resource.aliases = {QStringLiteral("CDC report")};
    resource.tags = {QStringLiteral("rtl")};

    Anchor anchor;
    anchor.id = QStringLiteral("anchor-rect");
    anchor.name = QStringLiteral("Reset crossing diagram");
    anchor.targetApp = QStringLiteral("SumatraPDF");
    anchor.targetFile = pdfPath;
    anchor.targetUri = QUrl::fromLocalFile(pdfPath).toString(QUrl::FullyEncoded);
    anchor.locatorType = QStringLiteral("sumatrapdf.rect");
    anchor.locatorJson = QStringLiteral(
        "{\"type\":\"sumatrapdf.rect\",\"page\":4,\"rect\":[72,144,252,252]}");
    anchor.aliases = {QStringLiteral("reset figure")};
    anchor.tags = {QStringLiteral("timing")};
    resource.anchors = {anchor};

    PdfLocatorPreviewRenderOptions previewOptions;
    previewOptions.cacheDirectory = directory.filePath(QStringLiteral("preview-cache"));
    previewOptions.rendererExecutablePath =
        directory.filePath(QStringLiteral("missing-renderer.exe"));
    const QString cachePath =
        pdfLocatorPreviewCacheFilePath(resource, anchor, previewOptions);
    QVERIFY(!cachePath.isEmpty());
    QVERIFY(QDir().mkpath(QFileInfo(cachePath).absolutePath()));
    QImage cachedImage(360, 216, QImage::Format_ARGB32_Premultiplied);
    cachedImage.fill(QColor(QStringLiteral("#c08a16")));
    QVERIFY(cachedImage.save(cachePath, "PNG"));

    const PinloomHostDocument resolved = pinloomHostDocumentForResource(
        resource, anchor, std::nullopt, previewOptions);
    QVERIFY(resolved.preview.has_value());
    QCOMPARE(resolved.preview->state, QStringLiteral("ready"));
    QCOMPARE(resolved.preview->filePath, QFileInfo(cachePath).absoluteFilePath());
    QCOMPARE(resolved.preview->pixelWidth, 360);
    QCOMPARE(resolved.preview->pixelHeight, 216);

    PinloomHostBridgeCallbacks callbacks;
    callbacks.resolve = [resolved](const PinloomHostIdentity &identity)
        -> std::optional<PinloomHostDocument> {
        if (identity.resourceId != QLatin1String("resource-pdf")
            || identity.anchorId != QLatin1String("anchor-rect")) {
            return std::nullopt;
        }
        return resolved;
    };

    PinloomHostBridgeOptions options;
    options.serverName = QStringLiteral("pinloom-pdf-host-test-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QDateTime::currentMSecsSinceEpoch());
    PinloomHostBridgeServer server(options, callbacks);
    QVERIFY2(server.start(), qPrintable(server.lastError()));

    const QJsonObject identity{
        {QStringLiteral("entryId"), QStringLiteral("anchor:anchor-rect")},
        {QStringLiteral("resourceId"), resource.id},
        {QStringLiteral("anchorId"), anchor.id},
    };
    const QJsonObject response = exchange(
        options.serverName,
        request(QStringLiteral("resolve"),
                QJsonObject{{QStringLiteral("identity"), identity}}));
    QVERIFY(response.value(QStringLiteral("ok")).toBool());

    const QJsonObject result = response.value(QStringLiteral("result")).toObject();
    const QJsonObject entry = result.value(QStringLiteral("entry")).toObject();
    const QJsonObject details = result.value(QStringLiteral("details")).toObject();
    const QJsonObject preview = result.value(QStringLiteral("preview")).toObject();
    QCOMPARE(entry.value(QStringLiteral("title")).toString(), anchor.name);
    QCOMPARE(details.value(QStringLiteral("title")).toString(), anchor.name);
    QCOMPARE(details.value(QStringLiteral("fileName")).toString(),
             QStringLiteral("clock.pdf"));
    QCOMPARE(details.value(QStringLiteral("page")).toInt(), 4);
    QVERIFY(details.value(QStringLiteral("aliases")).toArray().contains(
        QStringLiteral("reset figure")));
    QVERIFY(details.value(QStringLiteral("aliases")).toArray().contains(
        QStringLiteral("CDC report")));
    QVERIFY(details.value(QStringLiteral("tags")).toArray().contains(
        QStringLiteral("timing")));
    QVERIFY(details.value(QStringLiteral("tags")).toArray().contains(
        QStringLiteral("rtl")));
    QCOMPARE(details.value(QStringLiteral("locator")).toObject()
                 .value(QStringLiteral("json")).toString(),
             anchor.locatorJson);

    QCOMPARE(preview.value(QStringLiteral("kind")).toString(),
             QStringLiteral("image"));
    QCOMPARE(preview.value(QStringLiteral("state")).toString(),
             QStringLiteral("ready"));
    QCOMPARE(preview.value(QStringLiteral("mimeType")).toString(),
             QStringLiteral("image/png"));
    QCOMPARE(QUrl(preview.value(QStringLiteral("uri")).toString()).toLocalFile(),
             QFileInfo(cachePath).absoluteFilePath());
    QCOMPARE(preview.value(QStringLiteral("filePath")).toString(),
             QFileInfo(cachePath).absoluteFilePath());
    QCOMPARE(preview.value(QStringLiteral("pixelWidth")).toInt(), 360);
    QCOMPARE(preview.value(QStringLiteral("pixelHeight")).toInt(), 216);
    QCOMPARE(preview.value(QStringLiteral("page")).toInt(), 4);
    QVERIFY(preview.value(QStringLiteral("cropped")).toBool());
    QVERIFY(preview.value(QStringLiteral("byteSize")).toInteger() > 0);
    QVERIFY(!preview.contains(QStringLiteral("data")));

    const QByteArray wireResponse =
        QJsonDocument(response).toJson(QJsonDocument::Compact);
    QVERIFY(wireResponse.size() < 256 * 1024);
    QVERIFY(!wireResponse.contains("base64"));

    PinloomHostPreviewDescriptor oversized = *resolved.preview;
    oversized.altText = QString(100000, QLatin1Char('x'));
    const QJsonObject bounded = pinloomHostPreviewToJson(oversized);
    QVERIFY(!bounded.isEmpty());
    QVERIFY(QJsonDocument(bounded).toJson(QJsonDocument::Compact).size()
            <= kPinloomHostPreviewDescriptorMaxBytes);
    QVERIFY(!bounded.contains(QStringLiteral("altText")));

    PinloomHostPreviewDescriptor inlineImage = *resolved.preview;
    inlineImage.uri = QUrl(QStringLiteral("data:image/png;base64,AAAA"));
    QVERIFY(!inlineImage.isValid());
    QVERIFY(pinloomHostPreviewToJson(inlineImage).isEmpty());
}

void HostBridgeTest::hostOpenUsesSharedPdfAnchorPresenter()
{
    InMemoryLibraryRepository repository;
    Resource resource;
    resource.id = QStringLiteral("host-open-pdf");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Host open PDF");
    resource.location = QStringLiteral("E:/docs/host-open.pdf");
    Anchor anchor;
    anchor.id = QStringLiteral("host-open-pdf#region");
    anchor.name = QStringLiteral("Host region without zoom");
    anchor.targetApp = QStringLiteral("SumatraPDF");
    anchor.targetFile = resource.location;
    anchor.locatorType = QStringLiteral("sumatrapdf.rect");
    anchor.locatorJson = QStringLiteral(
        R"({"page":5,"rect":[20,40,180,110],"unit":"pt"})");
    resource.anchors = {anchor};
    QVERIFY(repository.upsertResource(resource));

    RecordingPdfPresenter presenter;
    int directLaunchCount = 0;
    SumatraPdfViewerAdapterOptions adapterOptions;
    adapterOptions.executablePathProvider = []() {
        return QStringLiteral("C:/Tools/SumatraPDF.exe");
    };
    adapterOptions.launchHandler = [&directLaunchCount](
                                       const SumatraPdfCommand &, QString *) {
        ++directLaunchCount;
        return true;
    };
    adapterOptions.pdfAnchorPresenter = &presenter;
    SumatraPdfViewerAdapter adapter(repository, adapterOptions);
    PinloomOpenServiceOptions openOptions;
    openOptions.pdfViewerAdapter = &adapter;
    PinloomOpenService openService(repository, openOptions);

    PinloomHostBridgeCallbacks callbacks;
    callbacks.open = [&](const PinloomHostIdentity &identity, QString *status) {
        if (identity.resourceId != resource.id || identity.anchorId != anchor.id) {
            return false;
        }
        PinloomOpenTarget target;
        target.resourceId = resource.id;
        target.resourceKind = resource.kind;
        target.title = resource.title;
        target.location = resource.location;
        target.anchor = anchor;
        const bool opened = openService.open(target);
        if (status) *status = openService.statusText();
        return opened;
    };

    PinloomHostBridgeOptions options;
    options.serverName = QStringLiteral("pinloom-live-zoom-host-test-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QDateTime::currentMSecsSinceEpoch());
    PinloomHostBridgeServer server(options, callbacks);
    QVERIFY2(server.start(), qPrintable(server.lastError()));

    const QJsonObject identity{
        {QStringLiteral("entryId"), QStringLiteral("anchor:") + anchor.id},
        {QStringLiteral("resourceId"), resource.id},
        {QStringLiteral("anchorId"), anchor.id},
    };
    const QJsonObject response = exchange(
        options.serverName,
        request(QStringLiteral("open"),
                QJsonObject{{QStringLiteral("identity"), identity}}));
    QVERIFY(response.value(QStringLiteral("ok")).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(presenter.presentCount, 1, 1000);
    QCOMPARE(directLaunchCount, 0);
    QCOMPARE(presenter.lastRequest.anchor.id, anchor.id);
    QCOMPARE(presenter.lastRequest.sourceCommand.filePath, resource.location);
    QCOMPARE(presenter.lastRequest.sourceCommand.page, 5);
    QCOMPARE(presenter.lastRequest.sourceCommand.highlightRect,
             QRectF(20, 40, 160, 70));
    QTRY_VERIFY_WITH_TIMEOUT(
        openService.statusText().contains(QStringLiteral("Pinloom Preview")),
        1000);
}

void HostBridgeTest::createsValidatedSourceAnchor()
{
    PinloomSourceAnchorRequest captured;
    PinloomHostBridgeCallbacks callbacks;
    callbacks.createSourceAnchor =
        [&captured](const PinloomSourceAnchorRequest &requestValue,
                    QString *status) -> std::optional<PinloomEntry> {
        captured = requestValue;
        PinloomEntry entry;
        entry.id = QStringLiteral("anchor:source-a");
        entry.type = PinloomEntryType::Anchor;
        entry.name = requestValue.title;
        entry.resourceId = QStringLiteral("resource-source-a");
        Anchor anchor;
        anchor.id = QStringLiteral("source-a");
        anchor.name = entry.name;
        anchor.locatorType = QStringLiteral("zeroslack.source");
        entry.anchor = anchor;
        if (status) *status = QStringLiteral("Created source anchor");
        return entry;
    };

    PinloomHostBridgeOptions options;
    options.serverName = QStringLiteral("pinloom-source-host-test-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QDateTime::currentMSecsSinceEpoch());
    PinloomHostBridgeServer server(options, callbacks);
    QVERIFY2(server.start(), qPrintable(server.lastError()));

    const QJsonObject params{
        {QStringLiteral("title"), QStringLiteral("Reset assignment")},
        {QStringLiteral("content"), QStringLiteral("rst_n <= 1'b0;")},
        {QStringLiteral("workspaceRoot"), QStringLiteral("E:/rtl")},
        {QStringLiteral("relativeFilePath"), QStringLiteral("src/top.sv")},
        {QStringLiteral("absoluteFilePath"), QStringLiteral("E:/rtl/src/top.sv")},
        {QStringLiteral("moduleName"), QStringLiteral("top")},
        {QStringLiteral("startLine"), 12},
        {QStringLiteral("startColumn"), 5},
        {QStringLiteral("endLine"), 12},
        {QStringLiteral("endColumn"), 19},
        {QStringLiteral("selectedTextHash"), QStringLiteral("hash")},
        {QStringLiteral("prefixContext"), QStringLiteral("begin\n")},
        {QStringLiteral("suffixContext"), QStringLiteral("\nend")},
    };
    QJsonObject response = exchange(
        options.serverName,
        request(QStringLiteral("createSourceAnchor"), params));
    QVERIFY(response.value(QStringLiteral("ok")).toBool());
    QCOMPARE(captured.relativeFilePath, QStringLiteral("src/top.sv"));
    QCOMPARE(captured.startLine, 12);
    QCOMPARE(response.value(QStringLiteral("result")).toObject()
                 .value(QStringLiteral("entry")).toObject()
                 .value(QStringLiteral("title")).toString(),
             QStringLiteral("Reset assignment"));

    QJsonObject invalidParams = params;
    invalidParams.insert(QStringLiteral("content"), QString());
    response = exchange(
        options.serverName,
        request(QStringLiteral("createSourceAnchor"), invalidParams));
    QVERIFY(!response.value(QStringLiteral("ok")).toBool());
    QCOMPARE(response.value(QStringLiteral("error")).toObject()
                 .value(QStringLiteral("code")).toString(),
             QStringLiteral("invalid_source_anchor"));
}

void HostBridgeTest::hostSourceAnchorWriteReturnsGlobalIdentityConflict()
{
    const auto identityRegistry = createInMemoryGlobalIdentityRegistry();
    InMemoryLibraryRepository library(identityRegistry);
    InMemoryClipRepository clips(identityRegistry);
    Clip existing;
    existing.id = QStringLiteral("host-conflict-clip");
    existing.state = ClipState::Saved;
    existing.text = QStringLiteral("Existing clip body");
    existing.name = QStringLiteral("Host collision");
    existing.createdAt = QDateTime::currentDateTimeUtc();
    existing.updatedAt = existing.createdAt;
    QVERIFY(clips.upsertPersistentClip(existing));

    PinloomHostBridgeCallbacks callbacks;
    callbacks.createSourceAnchor =
        [&library](const PinloomSourceAnchorRequest &requestValue,
                   QString *status) -> std::optional<PinloomEntry> {
        Resource resource;
        resource.id = QStringLiteral("host-conflict-resource");
        resource.kind = ResourceKind::File;
        resource.title = QStringLiteral("Host source file");
        resource.location = requestValue.absoluteFilePath;
        resource.updatedAt = QDateTime::currentDateTimeUtc();
        Anchor anchor;
        anchor.id = QStringLiteral("host-conflict-anchor");
        anchor.name = requestValue.title;
        anchor.targetApp = QStringLiteral("host-bridge-test");
        anchor.targetFile = requestValue.absoluteFilePath;
        anchor.locatorType = QStringLiteral("zeroslack.source");
        anchor.locatorJson = QStringLiteral("{}");
        anchor.createdAt = resource.updatedAt;
        anchor.updatedAt = resource.updatedAt;
        resource.anchors = {anchor};
        if (!library.upsertResource(resource)) {
            if (status) *status = library.lastError();
            return std::nullopt;
        }
        return PinloomEntry{};
    };

    PinloomHostBridgeOptions options;
    options.serverName = QStringLiteral("pinloom-source-conflict-test-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QDateTime::currentMSecsSinceEpoch());
    PinloomHostBridgeServer server(options, callbacks);
    QVERIFY2(server.start(), qPrintable(server.lastError()));

    const QJsonObject params{
        {QStringLiteral("title"), QStringLiteral(" host collision ")},
        {QStringLiteral("content"), QStringLiteral("assign ready = valid;")},
        {QStringLiteral("workspaceRoot"), QStringLiteral("E:/rtl")},
        {QStringLiteral("relativeFilePath"), QStringLiteral("src/host.sv")},
        {QStringLiteral("absoluteFilePath"), QStringLiteral("E:/rtl/src/host.sv")},
        {QStringLiteral("startLine"), 4},
        {QStringLiteral("startColumn"), 1},
        {QStringLiteral("endLine"), 4},
        {QStringLiteral("endColumn"), 21},
    };
    const QJsonObject response = exchange(
        options.serverName,
        request(QStringLiteral("createSourceAnchor"), params));
    QVERIFY(!response.value(QStringLiteral("ok")).toBool());
    const QJsonObject error = response.value(QStringLiteral("error")).toObject();
    QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("create_failed"));
    const QString message = error.value(QStringLiteral("message")).toString();
    QVERIFY(message.contains(QStringLiteral("host-conflict-clip")));
    QVERIFY(message.contains(QStringLiteral("conflicts with"), Qt::CaseInsensitive));
    QVERIFY(!library.findResource(QStringLiteral("host-conflict-resource")).has_value());
}

QTEST_MAIN(HostBridgeTest)
#include "host_bridge_test.moc"
