#include "pinloom/core/SqliteLibraryRepository.h"
#include "pinloom/core/Version.h"
#include "pinloom/widgets/PinloomHostBridge.h"
#include "pinloom/widgets/PinloomOpenService.h"
#include "pinloom/widgets/PinloomSuiteIntegration.h"
#include "pinloom/widgets/PinloomVisualTheme.h"

#include <suiteapp/client.h>
#include <suiteapp/runtime.h>

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>

using namespace Pinloom;

namespace {

bool writeJson(const QString &path, const QJsonObject &object)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write(QJsonDocument(object).toJson()) > 0 && file.commit();
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString uniqueEndpoint()
{
    return QStringLiteral("pinloom.ipc.test.%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

class OwnedProcess final : public QProcess {
public:
    ~OwnedProcess() override
    {
        if (state() == NotRunning) return;
        terminate();
        if (!waitForFinished(1000)) {
            kill();
            waitForFinished(1000);
        }
    }
};

// Runs the production adapter, storage, source-anchor writer and preview UI in
// a separate process, without the resident app's global hooks or user settings.
int runProvider(QApplication &app, const QStringList &arguments)
{
    const QString endpoint = arguments.at(2);
    const QDir data(arguments.at(3));
    const bool missingRuntime = arguments.at(4) == QLatin1String("missing");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, data.path());
    QApplication::setOrganizationName(QStringLiteral("PinloomIpcTest"));
    QApplication::setApplicationName(QStringLiteral("PinloomIpcTest"));
    QApplication::setApplicationVersion(pinloomVersion());
    app.setQuitOnLastWindowClosed(false);
    applySystemPinloomVisualTheme(app);

    SqliteLibraryRepository repository;
    if (!repository.open(data.filePath(QStringLiteral("library.sqlite3")),
                         data.filePath(QStringLiteral("identity.sqlite3")))
        || !repository.initialize()) return 10;
    const QString sourcePath = data.filePath(QStringLiteral("fixture.sv"));
    QFile source(sourcePath);
    if (!source.open(QIODevice::WriteOnly)) return 11;
    source.write("assign one = 1;\nassign two = 2;\nassign three = 3;\n");
    source.close();
    PinloomSourceAnchorRequest sourceRequest;
    sourceRequest.title = QStringLiteral("IPC anchor");
    sourceRequest.content = QStringLiteral("assign one = 1;");
    sourceRequest.workspaceRoot = data.path();
    sourceRequest.relativeFilePath = QStringLiteral("fixture.sv");
    sourceRequest.absoluteFilePath = sourcePath;
    sourceRequest.startLine = 1;
    sourceRequest.startColumn = 1;
    sourceRequest.endLine = 1;
    sourceRequest.endColumn = 16;
    const auto entry = createPinloomSourceAnchor(repository, sourceRequest);
    sourceRequest.title = QStringLiteral("Deleted IPC anchor");
    sourceRequest.startLine = sourceRequest.endLine = 2;
    const auto deleted = createPinloomSourceAnchor(repository, sourceRequest);
    if (!entry || !deleted || !repository.softDeleteResource(deleted->resourceId)) return 12;

    PinloomHostBridgeCallbacks callbacks;
    callbacks.resolve = [&repository](const PinloomHostIdentity &identity)
        -> std::optional<PinloomHostDocument> {
        SearchQuery query;
        query.includeDeleted = true;
        query.limit = 0;
        for (const auto &item : repository.search(query)) {
            if (!identity.resourceId.isEmpty() && identity.resourceId != item.resource.id)
                continue;
            const auto resource = repository.findResource(item.resource.id);
            if (!resource) continue;
            for (const auto &anchor : resource->anchors) {
                if (anchor.id == identity.anchorId)
                    return pinloomHostDocumentForResource(*resource, anchor);
            }
        }
        return std::nullopt;
    };
    callbacks.createSourceAnchor = [&repository](const PinloomSourceAnchorRequest &request,
                                                 QString *status) {
        return createPinloomSourceAnchor(repository, request, status);
    };
    PinloomOpenService openService(repository, {});
    int openCount = 0;
    callbacks.open = [&](const PinloomHostIdentity &identity, QString *status) {
        const auto document = callbacks.resolve(identity);
        if (!document || document->entry.deleted) return false;
        QFile::remove(data.filePath(QStringLiteral("close-preview.json")));
        QTimer closePreview;
        closePreview.setInterval(20);
        QElapsedTimer timeout;
        timeout.start();
        bool sawPreview = false;
        QObject::connect(&closePreview, &QTimer::timeout, &app, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != QLatin1String("textPreviewDialog")) return;
            if (!sawPreview) {
                sawPreview = true;
                writeJson(data.filePath(QStringLiteral("modal.json")), {{"visible", true}});
            }
            if (QFile::exists(data.filePath(QStringLiteral("close-preview.json")))
                || timeout.elapsed() > 8000) dialog->reject();
        });
        closePreview.start();
        const bool opened = openService.open(openTargetFromEntry(document->entry));
        if (status) *status = openService.statusText();
        QFile::remove(data.filePath(QStringLiteral("modal.json")));
        writeJson(data.filePath(QStringLiteral("opened.json")),
                  {{"count", ++openCount}, {"opened", opened}, {"preview", sawPreview}});
        return opened;
    };
    PinloomHostBridgeOptions hostOptions;
    hostOptions.serverName = endpoint + QStringLiteral(".host");
    PinloomHostBridgeServer host(hostOptions, callbacks);
    if (!host.start()) return 13;
    PinloomSuiteIntegration integration(callbacks);
    SuiteApp::RuntimeStartOptions options;
    options.endpoint = endpoint;
    options.startIfMissing = missingRuntime;
    options.probeTimeoutMs = 100;
    const QByteArray savedPath = qgetenv("PATH");
    if (missingRuntime) {
        qputenv("PATH", QByteArray());
        qputenv("SUITEAPP_RUNTIME_EXECUTABLE", data.filePath(QStringLiteral("absent.exe")).toUtf8());
    }
    QString reason;
    const bool registered = integration.start(options, &reason);
    qputenv("PATH", savedPath);
    if (!writeJson(data.filePath(QStringLiteral("ready.json")),
                   {{"registered", registered}, {"reason", reason},
                    {"entry", pinloomHostEntryToJson(*entry)},
                    {"deleted", pinloomHostEntryToJson(*deleted)}})) return 14;
    QTimer shutdown;
    shutdown.setInterval(25);
    QObject::connect(&shutdown, &QTimer::timeout, &app, [&] {
        if (QFile::exists(data.filePath(QStringLiteral("quit.json")))) app.quit();
    });
    shutdown.start();
    QTimer::singleShot(45000, &app, &QApplication::quit);
    return app.exec();
}

bool ok(const SuiteApp::TransportResult &result)
{
    return result.hasResponse() && result.response.value(QStringLiteral("ok")).toBool();
}

QJsonObject resultObject(const SuiteApp::TransportResult &result)
{
    return result.response.value(QStringLiteral("result")).toObject();
}

QJsonObject hostResolve(const QString &endpoint, const QJsonObject &identity)
{
    QLocalSocket socket;
    socket.connectToServer(endpoint);
    if (!socket.waitForConnected(1000)) return {};
    const QJsonObject request{{"protocol", kPinloomHostProtocol}, {"requestId", "fallback"},
                              {"method", "resolve"}, {"params", QJsonObject{{"identity", identity}}}};
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();
    QByteArray bytes;
    while (!bytes.contains('\n')) {
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(2000)) break;
        bytes += socket.readAll();
    }
    return QJsonDocument::fromJson(bytes).object();
}

} // namespace

class PinloomSuiteAppIpcTest final : public QObject {
    Q_OBJECT

private slots:
    void routesRealResourcesActionsAndSurfaces()
    {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString endpoint = uniqueEndpoint();
        OwnedProcess runtime;
        runtime.start(QStringLiteral(PINLOOM_TEST_RUNTIME), {"--endpoint", endpoint});
        QVERIFY2(runtime.waitForStarted(5000), qPrintable(runtime.errorString()));
        const SuiteApp::Client client(endpoint, 1500);
        QTRY_VERIFY_WITH_TIMEOUT(ok(client.request(QStringLiteral("health.ping"))), 5000);
        OwnedProcess provider;
        provider.start(QCoreApplication::applicationFilePath(),
                       {"--provider", endpoint, data.path(), "available"});
        QVERIFY(provider.waitForStarted(5000));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(data.filePath("ready.json")), 10000);
        const QJsonObject ready = readJson(data.filePath("ready.json"));
        QVERIFY2(ready.value("registered").toBool(), qPrintable(ready.value("reason").toString()));
        const auto registry = client.listProviders();
        QVERIFY(ok(registry));
        const QJsonArray providers = resultObject(registry).value("providers").toArray();
        QCOMPARE(providers.size(), 1);
        const QJsonObject descriptor = providers.first().toObject();
        QCOMPARE(descriptor.value("appId").toString(), QStringLiteral("pinloom"));
        QCOMPARE(descriptor.value("displayName").toString(), QStringLiteral("Pinloom"));
        QCOMPARE(descriptor.value("version").toString(), pinloomVersion());
        QCOMPARE(descriptor.value("processId").toInteger(), provider.processId());
        const QString providerEndpoint = descriptor.value("endpoint").toString();
        const QJsonObject entry = ready.value("entry").toObject();
        const QString uri = entry.value("uri").toString();
        QVERIFY(uri.startsWith(QStringLiteral("pinloom://anchor/")));
        const auto resolved = client.resolveResource(uri);
        QVERIFY(ok(resolved));
        QCOMPARE(resultObject(resolved).value("content").toString(), QStringLiteral("assign one = 1;"));
        const auto surface = client.describeSurface(QStringLiteral("pinloom.entry.preview"), uri);
        QVERIFY(ok(surface));
        QCOMPARE(resultObject(surface).value("mode").toString(), QStringLiteral("model"));
        QCOMPARE(resultObject(surface).value("model").toObject(), resultObject(resolved));

        // Both routes must respond while the actual modal preview is still open.
        for (int count = 1; count <= 2; ++count) {
            const auto opened = count == 1
                ? client.invokeAction(QStringLiteral("pinloom.entry.open"), {}, uri)
                : client.openSurface(QStringLiteral("pinloom.entry.preview"), uri);
            QVERIFY(ok(opened));
            QVERIFY(resultObject(opened).value("accepted").toBool());
            QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(data.filePath("modal.json")), 3000);
            QVERIFY(ok(client.resolveResource(uri)));
            QVERIFY(writeJson(data.filePath("close-preview.json"), {}));
            QTRY_COMPARE_WITH_TIMEOUT(readJson(data.filePath("opened.json")).value("count").toInt(), count, 3000);
            QVERIFY(readJson(data.filePath("opened.json")).value("preview").toBool());
            QVERIFY(readJson(data.filePath("opened.json")).value("opened").toBool());
        }

        const auto missing = client.resolveResource(QStringLiteral("pinloom://anchor/missing"));
        QCOMPARE(missing.response.value("error").toObject().value("code").toString(), QStringLiteral("resource_not_found"));
        const auto deleted = client.resolveResource(ready.value("deleted").toObject().value("uri").toString());
        QCOMPARE(deleted.response.value("error").toObject().value("code").toString(), QStringLiteral("resource_deleted"));
        const auto invalid = client.invokeAction(QStringLiteral("pinloom.source-anchor.create"), {});
        QCOMPARE(invalid.response.value("error").toObject().value("code").toString(), QStringLiteral("invalid_source_anchor"));
        const QJsonObject source{{"title", "Created over IPC"}, {"content", "assign three = 3;"},
                                 {"workspaceRoot", data.path()}, {"relativeFilePath", "fixture.sv"},
                                 {"absoluteFilePath", data.filePath("fixture.sv")},
                                 {"startLine", 3}, {"startColumn", 1}, {"endLine", 3}, {"endColumn", 18}};
        const auto created = client.invokeAction(QStringLiteral("pinloom.source-anchor.create"), source);
        QVERIFY2(ok(created), QJsonDocument(created.response).toJson().constData());
        const QJsonObject createdEntry = resultObject(created).value("entry").toObject();
        const auto createdResource = client.resolveResource(createdEntry.value("uri").toString());
        QVERIFY(ok(createdResource));
        QCOMPARE(resultObject(createdResource).value("content").toString(), QStringLiteral("assign three = 3;"));
        const auto duplicate = client.invokeAction(QStringLiteral("pinloom.source-anchor.create"), source);
        QCOMPARE(duplicate.response.value("error").toObject().value("code").toString(), QStringLiteral("source_anchor_create_failed"));

        QVERIFY(writeJson(data.filePath("quit.json"), {}));
        QVERIFY(provider.waitForFinished(5000));
        QCOMPARE(provider.exitStatus(), QProcess::NormalExit);
        QCOMPARE(provider.exitCode(), 0);
        const auto afterExit = client.listProviders();
        QVERIFY(ok(afterExit));
        QVERIFY(resultObject(afterExit).value("providers").toArray().isEmpty());
        QVERIFY(!SuiteApp::sendRequest(providerEndpoint, SuiteApp::makeRequest("health.ping"), 100).hasResponse());
        QVERIFY(runtime.waitForFinished(5000));
        QCOMPARE(runtime.exitStatus(), QProcess::NormalExit);
        QCOMPARE(runtime.exitCode(), 0);

        SqliteLibraryRepository reopened;
        QVERIFY(reopened.open(data.filePath("library.sqlite3"), data.filePath("identity.sqlite3")));
        QVERIFY(reopened.integrityCheck());
        const auto saved = reopened.findResource(createdEntry.value("identity").toObject().value("resourceId").toString());
        QVERIFY(saved.has_value());
        QCOMPARE(saved->content, QStringLiteral("assign three = 3;"));
        QCOMPARE(saved->anchors.first().locatorType, QStringLiteral("zeroslack.source"));
        qInfo() << "Real Runtime, provider process, SQLite write/reopen, modal open, Surface and graceful unregister verified";
    }

    void missingRuntimePreservesHostBridge()
    {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        const QString endpoint = uniqueEndpoint();
        OwnedProcess provider;
        provider.start(QCoreApplication::applicationFilePath(),
                       {"--provider", endpoint, data.path(), "missing"});
        QVERIFY(provider.waitForStarted(5000));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(data.filePath("ready.json")), 10000);
        const auto ready = readJson(data.filePath("ready.json"));
        QVERIFY(!ready.value("registered").toBool());
        QVERIFY(ready.value("reason").toString().contains(QStringLiteral("could not be located")));
        const auto response = hostResolve(endpoint + QStringLiteral(".host"),
            ready.value("entry").toObject().value("identity").toObject());
        QVERIFY2(response.value("ok").toBool(), QJsonDocument(response).toJson().constData());
        QCOMPARE(response.value("result").toObject().value("content").toString(), QStringLiteral("assign one = 1;"));
        QVERIFY(!SuiteApp::Client(endpoint, 100).listProviders().hasResponse());
        QVERIFY(writeJson(data.filePath("quit.json"), {}));
        QVERIFY(provider.waitForFinished(5000));
        QCOMPARE(provider.exitStatus(), QProcess::NormalExit);
        QCOMPARE(provider.exitCode(), 0);
        qInfo() << "Missing Runtime reported; independent HostBridge and temporary SQLite remain usable";
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() == 5 && arguments.at(1) == QLatin1String("--provider"))
        return runProvider(app, arguments);
    PinloomSuiteAppIpcTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "suiteapp_ipc_test.moc"
