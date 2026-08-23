#include "pinloom/widgets/PinloomHostBridge.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTest>

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
} // namespace

class HostBridgeTest final : public QObject {
    Q_OBJECT

private slots:
    void servesVersionedSearchResolveAndOpen();
};

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
    QVERIFY(serialized.value(QStringLiteral("uri")).toString().startsWith(
        QStringLiteral("pinloom://entry/anchor:anchor-a")));

    const QJsonObject identity = serialized.value(QStringLiteral("identity")).toObject();
    response = exchange(
        options.serverName,
        request(QStringLiteral("resolve"),
                QJsonObject{{QStringLiteral("identity"), identity}}));
    QVERIFY(response.value(QStringLiteral("ok")).toBool());
    QCOMPARE(response.value(QStringLiteral("result")).toObject()
                 .value(QStringLiteral("content")).toString(),
             QStringLiteral("Reset crosses the clock domain here."));

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

QTEST_MAIN(HostBridgeTest)
#include "host_bridge_test.moc"
