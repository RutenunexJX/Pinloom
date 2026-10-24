#include "pinloom/widgets/AnchorLibraryWindow.h"
#include "pinloom/widgets/AnchorLocatorPreviewWidget.h"
#include "pinloom/widgets/PdfLocatorPreviewRenderer.h"
#include "pinloom/widgets/PinloomHostBridge.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLockFile>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <memory>

using namespace Pinloom;

namespace {

bool writeFile(const QString &path, const QByteArray &contents = {})
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

// The test executable also acts as a deterministic renderer. Files control when
// a page may finish, so scheduling assertions do not depend on rendering speed.
int rendererHelper(const QStringList &arguments)
{
    const int outputIndex = arguments.indexOf(QStringLiteral("-o"));
    const int optionIndex = arguments.indexOf(QStringLiteral("-O"));
    if (outputIndex < 0 || optionIndex < 0 || arguments.size() < 4) return 90;
    const QString source = arguments.at(arguments.size() - 2);
    const int page = arguments.last().toInt();
    const int dpi = arguments.at(optionIndex + 1).section(QLatin1Char('='), 1).toInt();
    const QDir directory = QFileInfo(source).dir();
    const QString identity = QStringLiteral("%1-%2-%3")
                                 .arg(page).arg(dpi).arg(QCoreApplication::applicationPid());
    QLockFile active(directory.filePath(QStringLiteral("renderer.lock")));
    if (!active.tryLock(0)) {
        writeFile(directory.filePath(QStringLiteral("overlap-%1").arg(identity)));
        return 91;
    }
    if (!writeFile(directory.filePath(QStringLiteral("started-%1").arg(identity)))) return 92;
    if (page == 98) return 7;
    const QString release = directory.filePath(QStringLiteral("release-%1-%2").arg(page).arg(dpi));
    QElapsedTimer safetyDeadline;
    safetyDeadline.start();
    while (!QFileInfo::exists(release)
           && !QFileInfo::exists(directory.filePath(QStringLiteral("release-all")))) {
        if (safetyDeadline.elapsed() > 30000) return 93;
        QThread::msleep(5);
    }
    QImage image(dpi, 40 + page * 10, QImage::Format_RGB32);
    image.fill(QColor::fromHsv((page * 60) % 360, 200, 220));
    if (!image.save(arguments.at(outputIndex + 1))) return 94;
    if (!writeFile(directory.filePath(QStringLiteral("finished-%1").arg(identity)))) return 95;
    return 0;
}

struct PreviewFixture {
    QTemporaryDir directory;
    Resource resource;
    PdfLocatorPreviewRenderOptions renderOptions;

    ~PreviewFixture()
    {
        // A failing assertion must not leave the helper waiting for its gate.
        if (directory.isValid()) writeFile(directory.filePath(QStringLiteral("release-all")));
    }

    bool initialize(int pages = 3)
    {
        if (!directory.isValid()) return false;
        resource.id = QStringLiteral("preview-file");
        resource.title = QStringLiteral("Preview fixture");
        resource.kind = ResourceKind::Pdf;
        resource.location = directory.filePath(QStringLiteral("fixture.pdf"));
        if (!writeFile(resource.location, "%PDF-1.4\npreview fixture\n")) return false;
        for (int page = 1; page <= pages; ++page) {
            Anchor anchor;
            anchor.id = QStringLiteral("preview-anchor-%1").arg(page);
            anchor.name = QStringLiteral("Page %1").arg(page);
            anchor.targetFile = resource.location;
            anchor.locatorType = QStringLiteral("sumatrapdf.page");
            anchor.locatorJson = QStringLiteral("{\"page\":%1}").arg(page);
            resource.anchors.append(anchor);
        }
        renderOptions.rendererExecutablePath = QCoreApplication::applicationFilePath();
        renderOptions.cacheDirectory = directory.filePath(QStringLiteral("cache"));
        renderOptions.timeoutMilliseconds = 15000;
        return true;
    }

    AnchorLibraryWindowOptions windowOptions()
    {
        AnchorLibraryWindowOptions options;
        options.filesProvider = [this] {
            AnchorLibraryFile file;
            file.resource = resource;
            for (const Anchor &anchor : resource.anchors) file.anchors.append({resource.id, anchor});
            return QList<AnchorLibraryFile>{file};
        };
        options.pdfPreviewOptionsProvider = [this] { return renderOptions; };
        return options;
    }

    int count(const QString &pattern) const
    {
        return QDir(directory.path()).entryList({pattern}, QDir::Files).size();
    }

    int started(int page = 0, int dpi = 0) const
    {
        return count(QStringLiteral("started-%1-%2-*")
                         .arg(page ? QString::number(page) : QStringLiteral("*"),
                              dpi ? QString::number(dpi) : QStringLiteral("*")));
    }

    bool release(int page, int dpi = 144) const
    {
        return writeFile(directory.filePath(QStringLiteral("release-%1-%2").arg(page).arg(dpi)));
    }

    bool rendererIdle() const
    {
        QLockFile lock(directory.filePath(QStringLiteral("renderer.lock")));
        return lock.tryLock(0);
    }
};

} // namespace

class PdfLocatorPreviewTest final : public QObject {
    Q_OBJECT

private slots:
    void duplicateRequestReusesRenderAndExpands();
    void selectionKeepsOnlyLatestPendingPreview();
    void changedInputDoesNotReuseActiveRender_data();
    void changedInputDoesNotReuseActiveRender();
    void hiddenAndDestroyedWindowsCancelRendering();
    void rendererCancellationStopsWorkWithoutPublishingCache();
    void rendererReportsTimeoutAndProcessFailure_data();
    void rendererReportsTimeoutAndProcessFailure();
    void hostCoalescesCancelsAndStaysResponsive();
    void hostRejectsChangedAnchorAndBoundsQueue();
    void pairedRuntimeGeometryAndFreshness();
};

static void connectPreview(QLocalSocket &socket, const QString &server, int page)
{
    socket.connectToServer(server);
    socket.waitForConnected(500);
    const QJsonObject request{{"protocol", "pinloom-host/v1"}, {"requestId", QString::number(page)},
        {"method", "resolve"}, {"params", QJsonObject{{"identity", QJsonObject{{"resourceId", "preview-file"},
        {"anchorId", QStringLiteral("preview-anchor-%1").arg(page)}}}}}};
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();
}

static PinloomHostBridgeCallbacks previewCallbacks(PreviewFixture &fixture)
{
    for (auto &anchor : fixture.resource.anchors) {
        anchor.locatorType = QStringLiteral("sumatrapdf.rect");
        auto json = QJsonDocument::fromJson(anchor.locatorJson.toUtf8()).object();
        json.insert(QStringLiteral("rect"), QJsonArray{0,0,20,20});
        anchor.locatorJson = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
    }
    PinloomHostBridgeCallbacks callbacks;
    callbacks.resolve = [&fixture](const PinloomHostIdentity &identity) -> std::optional<PinloomHostDocument> {
        for (const auto &anchor : fixture.resource.anchors)
            if (anchor.id == identity.anchorId)
                return pinloomHostDocumentForResource(fixture.resource, anchor, {}, fixture.renderOptions, true);
        return {};
    };
    return callbacks;
}

void PdfLocatorPreviewTest::hostCoalescesCancelsAndStaysResponsive()
{
    PreviewFixture fixture;
    QVERIFY(fixture.initialize(3));
    auto callbacks = previewCallbacks(fixture);
    PinloomHostBridgeOptions options;
    options.serverName = QStringLiteral("pinloom-preview-async-%1").arg(QCoreApplication::applicationPid());
    auto server = std::make_unique<PinloomHostBridgeServer>(options, callbacks);
    QVERIFY(server->start());
    QLocalSocket a, b;
    connectPreview(a, options.serverName, 1);
    connectPreview(b, options.serverName, 1);
    QTRY_COMPARE(fixture.started(1), 1);
    QTest::qWait(100);
    QCOMPARE(fixture.started(), 1);
    QLocalSocket ping;
    ping.connectToServer(options.serverName); QVERIFY(ping.waitForConnected(500));
    ping.write("{\"protocol\":\"pinloom-host/v1\",\"method\":\"capabilities\"}\n"); ping.flush();
    QTRY_VERIFY(ping.bytesAvailable() > 0);
    QVERIFY(QJsonDocument::fromJson(ping.readAll()).object()["ok"].toBool());
    a.abort();
    QTest::qWait(100);
    QCOMPARE(fixture.started(), 1);
    QVERIFY(fixture.release(1));
    QTRY_VERIFY(b.bytesAvailable() > 0);
    const auto ready = QJsonDocument::fromJson(b.readAll()).object()["result"].toObject()["preview"].toObject();
    QCOMPARE(ready["state"].toString(), QStringLiteral("ready"));
    QVERIFY(!ready["sourceVersion"].toString().isEmpty());
    QLocalSocket c;
    connectPreview(c, options.serverName, 2);
    QTRY_COMPARE(fixture.started(2), 1);
    c.abort();
    QTRY_VERIFY(fixture.rendererIdle());
    QCOMPARE(fixture.count(QStringLiteral("finished-2-*")), 0);
    QLocalSocket d;
    connectPreview(d, options.serverName, 3);
    QTRY_COMPARE(fixture.started(3), 1);
    server.reset();
    QTRY_VERIFY(fixture.rendererIdle());
    QCOMPARE(fixture.count(QStringLiteral("finished-3-*")), 0);
}

void PdfLocatorPreviewTest::hostRejectsChangedAnchorAndBoundsQueue()
{
    PreviewFixture fixture;
    QVERIFY(fixture.initialize(6));
    auto callbacks = previewCallbacks(fixture);
    PinloomHostBridgeOptions options;
    options.serverName = QStringLiteral("pinloom-preview-bounds-%1").arg(QCoreApplication::applicationPid());
    PinloomHostBridgeServer server(options, callbacks);
    QVERIFY(server.start());
    QLocalSocket sockets[6];
    connectPreview(sockets[0], options.serverName, 1);
    QTRY_COMPARE(fixture.started(1), 1);
    for (int i = 1; i < 6; ++i) connectPreview(sockets[i], options.serverName, i + 1);
    QTRY_VERIFY(sockets[5].bytesAvailable() > 0);
    const auto busy = QJsonDocument::fromJson(sockets[5].readAll()).object()["result"].toObject()["preview"].toObject();
    QCOMPARE(busy["state"].toString(), QStringLiteral("unavailable"));
    QVERIFY(busy["error"].toString().contains(QStringLiteral("busy")));
    fixture.resource.anchors[0].locatorJson = QStringLiteral("{\"page\":1,\"rect\":[2,2,22,22]}");
    for (int i = 1; i < 5; ++i) sockets[i].abort();
    QVERIFY(fixture.release(1));
    QTRY_VERIFY(sockets[0].bytesAvailable() > 0);
    const auto changed = QJsonDocument::fromJson(sockets[0].readAll()).object()["result"].toObject()["preview"].toObject();
    QCOMPARE(changed["state"].toString(), QStringLiteral("unavailable"));
    QVERIFY(changed["filePath"].toString().isEmpty());
    QTRY_VERIFY(fixture.rendererIdle());
    QCOMPARE(fixture.started(), 1);
}

void PdfLocatorPreviewTest::pairedRuntimeGeometryAndFreshness()
{
    const QString fixturePath = qEnvironmentVariable("PINLOOM_GEOMETRY_FIXTURE");
    if (fixturePath.isEmpty()) QSKIP("Set PINLOOM_GEOMETRY_FIXTURE for real paired runtime validation");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString pdf = directory.filePath(QStringLiteral("geometry.pdf"));
    QVERIFY(QFile::copy(QDir(fixturePath).filePath(QStringLiteral("geometry.pdf")), pdf));
    QFile cases(QDir(fixturePath).filePath(QStringLiteral("cases.json")));
    QVERIFY(cases.open(QIODevice::ReadOnly));
    const auto geometries = QJsonDocument::fromJson(cases.readAll()).array();
    QVERIFY(geometries.size() >= 6);
    Resource resource;
    resource.kind = ResourceKind::Pdf; resource.location = pdf;
    Anchor anchor;
    anchor.targetFile = pdf; anchor.locatorType = QStringLiteral("sumatrapdf.rect");
    PdfLocatorPreviewRenderOptions options;
    options.rendererExecutablePath = qEnvironmentVariable("PINLOOM_REAL_PREVIEW_RUNTIME");
    options.cacheDirectory = directory.filePath(QStringLiteral("cache"));
    options.contextPaddingPoints = 0; options.drawSelectionBorder = false;
    const QFileInfo info(pdf);
    for (const auto &value : geometries) {
        auto locator = value.toObject();
        locator.insert("type", "sumatrapdf.rect"); locator.insert("version", 3);
        locator.insert("unit", "pt"); locator.insert("coordinateSpace", "page-top-left");
        locator.insert("document", QJsonObject{{"size", QString::number(info.size())},
            {"modifiedMilliseconds", QString::number(info.lastModified().toMSecsSinceEpoch())}});
        anchor.locatorJson = QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));
        QElapsedTimer elapsed; elapsed.start();
        const auto cold = renderPdfLocatorPreview(resource, anchor, options);
        const auto coldMs = elapsed.elapsed();
        QVERIFY2(cold.success(), qPrintable(cold.error)); QVERIFY(cold.cropped); QVERIFY(!cold.fromCache);
        const QColor color = cold.image.pixelColor(cold.image.width()/2, cold.image.height()/2);
        QVERIFY2(color.green() > 130 && color.green() > color.red() + 60 && color.green() > color.blue() + 40, qPrintable(color.name()));
        elapsed.restart();
        const auto hot = renderPdfLocatorPreview(resource, anchor, options);
        QVERIFY2(hot.success(), qPrintable(hot.error)); QVERIFY(hot.fromCache);
        qInfo() << "REAL_PREVIEW" << locator["page"].toInt() << cold.image.size() << "coldMs" << coldMs << "hotMs" << elapsed.elapsed();
        const QString evidence = qEnvironmentVariable("PINLOOM_PREVIEW_EVIDENCE");
        if (!evidence.isEmpty()) { QDir().mkpath(evidence); QVERIFY(cold.image.save(QDir(evidence).filePath(QStringLiteral("region-%1.png").arg(cold.page)))); }
        // A changed locator must not reuse an image, even if the PDF is unchanged.
        locator.insert("rotation", (locator["rotation"].toInt() + 90) % 360);
        Anchor bad = anchor; bad.locatorJson = QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));
        const auto invalid = renderPdfLocatorPreview(resource, bad, options);
        QVERIFY(!invalid.success()); QVERIFY(invalid.error.contains(QStringLiteral("geometry changed")));
    }
    const QString cache = pdfLocatorPreviewCacheFilePath(resource, anchor, options);
    QVERIFY(writeFile(cache, "broken png"));
    const auto repaired = renderPdfLocatorPreview(resource, anchor, options);
    QVERIFY(repaired.success()); QVERIFY(!repaired.fromCache);
    QFile changed(pdf); QVERIFY(changed.open(QIODevice::Append)); changed.write("\n% changed\n"); changed.close();
    const auto stale = renderPdfLocatorPreview(resource, anchor, options);
    QVERIFY(!stale.success()); QVERIFY(stale.image.isNull()); QVERIFY(stale.error.contains(QStringLiteral("changed since capture")));
    QVERIFY(QFile::remove(pdf));
    const auto missing = renderPdfLocatorPreview(resource, anchor, options);
    QVERIFY(!missing.success()); QVERIFY(missing.image.isNull()); QVERIFY(missing.error.contains(QStringLiteral("missing")));
}

void PdfLocatorPreviewTest::duplicateRequestReusesRenderAndExpands()
{
    PreviewFixture fixture;
    QVERIFY(fixture.initialize(1));
    AnchorLibraryWindow window(fixture.windowOptions());
    window.show();
    QTRY_COMPARE(fixture.started(1), 1);
    QVERIFY(window.previewSelectedAnchor());
    QVERIFY(window.previewSelectedAnchor());
    QVERIFY(fixture.release(1));
    auto *preview = window.findChild<AnchorLocatorPreviewWidget *>();
    QVERIFY(preview);
    QTRY_VERIFY(preview->hasScreenshot());
    auto *expanded = window.findChild<QDialog *>(QStringLiteral("anchorLocatorExpandedPreview"));
    QVERIFY(expanded);
    QVERIFY(expanded->isVisible());
    QCOMPARE(fixture.started(1), 1);
    QCOMPARE(fixture.count(QStringLiteral("overlap-*")), 0);
    QCOMPARE(fixture.count(QStringLiteral("finished-*")), 1);
}

void PdfLocatorPreviewTest::selectionKeepsOnlyLatestPendingPreview()
{
    PreviewFixture fixture;
    QVERIFY(fixture.initialize());
    AnchorLibraryWindow window(fixture.windowOptions());
    window.show();
    QTRY_COMPARE(fixture.started(1), 1);
    // Stay in one GUI turn: B must be replaced before the active A completion
    // can dispatch pending work, even if A's worker has already noticed cancel.
    QVERIFY(window.selectAnchorAt(1));
    QVERIFY(window.previewSelectedAnchor());
    QVERIFY(window.selectAnchorAt(2));
    QVERIFY(window.previewSelectedAnchor());
    QTRY_COMPARE(fixture.started(3), 1);
    QCOMPARE(fixture.started(2), 0);
    QCOMPARE(fixture.count(QStringLiteral("overlap-*")), 0);
    QVERIFY(fixture.release(3));
    auto *preview = window.findChild<AnchorLocatorPreviewWidget *>();
    QVERIFY(preview);
    QTRY_VERIFY(preview->hasScreenshot());
    QVERIFY(window.statusText().contains(QStringLiteral("page 3")));
    QCOMPARE(fixture.count(QStringLiteral("finished-1-*")), 0);
    QVERIFY(!QFileInfo::exists(pdfLocatorPreviewCacheFilePath(
        fixture.resource, fixture.resource.anchors.first(), fixture.renderOptions)));
    QVERIFY(fixture.release(1));
    QCoreApplication::processEvents();
    QVERIFY(window.statusText().contains(QStringLiteral("page 3")));
    QCOMPARE(fixture.started(), 2);
}

void PdfLocatorPreviewTest::changedInputDoesNotReuseActiveRender_data()
{
    QTest::addColumn<bool>("changeSource");
    QTest::newRow("render-options") << false;
    QTest::newRow("source-version") << true;
}

void PdfLocatorPreviewTest::changedInputDoesNotReuseActiveRender()
{
    QFETCH(bool, changeSource);
    PreviewFixture fixture;
    QVERIFY(fixture.initialize(1));
    AnchorLibraryWindow window(fixture.windowOptions());
    window.show();
    QTRY_COMPARE(fixture.started(1), 1);
    if (changeSource) {
        QFile source(fixture.resource.location);
        QVERIFY(source.open(QIODevice::Append));
        QVERIFY(source.write("new version\n") > 0); // Size changes even on coarse timestamp filesystems.
        source.close();
    } else {
        fixture.renderOptions.resolutionDpi = 216;
    }
    QVERIFY(window.previewSelectedAnchor());
    QTRY_COMPARE(fixture.started(1), 2);
    if (!changeSource) QCOMPARE(fixture.started(1, 216), 1);
    QCOMPARE(fixture.count(QStringLiteral("overlap-*")), 0);
    QVERIFY(fixture.release(1, fixture.renderOptions.resolutionDpi));
    auto *preview = window.findChild<AnchorLocatorPreviewWidget *>();
    QVERIFY(preview);
    QTRY_VERIFY(preview->hasScreenshot());
    QCOMPARE(fixture.count(QStringLiteral("finished-*")), 1);
    const QImage cached(pdfLocatorPreviewCacheFilePath(
        fixture.resource, fixture.resource.anchors.first(), fixture.renderOptions));
    QCOMPARE(cached.width(), fixture.renderOptions.resolutionDpi);
}

void PdfLocatorPreviewTest::hiddenAndDestroyedWindowsCancelRendering()
{
    PreviewFixture fixture;
    QVERIFY(fixture.initialize(1));
    auto window = std::make_unique<AnchorLibraryWindow>(fixture.windowOptions());
    window->show();
    QTRY_COMPARE(fixture.started(1), 1);
    window->hide();
    QTRY_VERIFY(fixture.rendererIdle());
    QCOMPARE(fixture.count(QStringLiteral("finished-*")), 0);
    window->refreshLibrary();
    // Let the documented 120 ms debounce run, if a hidden refresh scheduled it.
    QTest::qWait(180);
    QCOMPARE(fixture.started(1), 1);
    window->show();
    QTRY_COMPARE(fixture.started(1), 2);
    window.reset();
    QTRY_VERIFY(fixture.rendererIdle());
    QCOMPARE(fixture.count(QStringLiteral("finished-*")), 0);
    QCOMPARE(fixture.count(QStringLiteral("overlap-*")), 0);
}

void PdfLocatorPreviewTest::rendererCancellationStopsWorkWithoutPublishingCache()
{
    PreviewFixture fixture;
    QVERIFY(fixture.initialize(1));
    const auto cancellation = std::make_shared<std::atomic_bool>(true);
    const auto cancelledBeforeStart = renderPdfLocatorPreview(
        fixture.resource, fixture.resource.anchors.first(), fixture.renderOptions, cancellation);
    QVERIFY(cancelledBeforeStart.cancelled);
    QVERIFY(!cancelledBeforeStart.success());
    QCOMPARE(fixture.started(), 0);
    cancellation->store(false);
    auto future = QtConcurrent::run([&] {
        return renderPdfLocatorPreview(fixture.resource, fixture.resource.anchors.first(),
                                       fixture.renderOptions, cancellation);
    });
    const auto cleanup = qScopeGuard([&] {
        cancellation->store(true);
        future.waitForFinished();
    });
    QTRY_COMPARE(fixture.started(), 1);
    cancellation->store(true);
    QTRY_VERIFY(future.isFinished());
    const auto result = future.result();
    QVERIFY(result.cancelled);
    QVERIFY(!result.success());
    QVERIFY(result.image.isNull());
    QVERIFY(fixture.rendererIdle());
    QCOMPARE(fixture.count(QStringLiteral("finished-*")), 0);
    QVERIFY(!QFileInfo::exists(pdfLocatorPreviewCacheFilePath(
        fixture.resource, fixture.resource.anchors.first(), fixture.renderOptions)));
}

void PdfLocatorPreviewTest::rendererReportsTimeoutAndProcessFailure_data()
{
    QTest::addColumn<bool>("processFailure");
    QTest::newRow("timeout") << false;
    QTest::newRow("process-failure") << true;
}

void PdfLocatorPreviewTest::rendererReportsTimeoutAndProcessFailure()
{
    QFETCH(bool, processFailure);
    PreviewFixture fixture;
    QVERIFY(fixture.initialize(1));
    fixture.renderOptions.timeoutMilliseconds = 1000;
    Anchor anchor = fixture.resource.anchors.first();
    if (processFailure) anchor.locatorJson = QStringLiteral("{\"page\":98}");
    auto future = QtConcurrent::run([&] {
        return renderPdfLocatorPreview(fixture.resource, anchor, fixture.renderOptions);
    });
    const auto cleanup = qScopeGuard([&] {
        writeFile(fixture.directory.filePath(QStringLiteral("release-all")));
        future.waitForFinished();
    });
    QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 10000);
    const auto result = future.result();
    QVERIFY(!result.success());
    QVERIFY(!result.cancelled);
    QVERIFY(result.image.isNull());
    QVERIFY2(result.error.contains(processFailure ? QStringLiteral("exit code 7")
                                                 : QStringLiteral("timed out")),
             qPrintable(result.error));
    QVERIFY(fixture.rendererIdle());
    QCOMPARE(fixture.started(), 1);
    QCOMPARE(fixture.count(QStringLiteral("finished-*")), 0);
}

int main(int argc, char **argv)
{
    if (argc > 1 && QByteArray(argv[1]) == "convert") {
        QCoreApplication application(argc, argv);
        return rendererHelper(application.arguments());
    }
    QApplication application(argc, argv);
    PdfLocatorPreviewTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "pdf_locator_preview_test.moc"
