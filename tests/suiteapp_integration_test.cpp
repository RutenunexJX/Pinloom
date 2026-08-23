#include "pinloom/widgets/PinloomSuiteIntegration.h"

#include <suiteapp/protocol.h>

#include <QtTest>

class PinloomSuiteAppIntegrationTest final : public QObject {
    Q_OBJECT

private slots:
    void publishesStableContract()
    {
        const QJsonObject descriptor =
            Pinloom::PinloomSuiteIntegration::appDescriptor(
                QStringLiteral("1.2.3"), QStringLiteral("test.pinloom"));
        QString reason;
        QVERIFY2(SuiteApp::validateAppDescriptor(descriptor, &reason),
                 qPrintable(reason));
        QVERIFY(SuiteApp::descriptorOwnsAction(
            descriptor, QStringLiteral("pinloom.entry.open")));
        QVERIFY(SuiteApp::descriptorOwnsAction(
            descriptor, QStringLiteral("pinloom.source-anchor.create")));
        QVERIFY(SuiteApp::descriptorOwnsSurface(
            descriptor, QStringLiteral("pinloom.entry.preview")));
    }

    void resolvesThroughAuthoritativeCallback()
    {
        Pinloom::PinloomHostBridgeCallbacks callbacks;
        callbacks.resolve = [](const Pinloom::PinloomHostIdentity& identity)
            -> std::optional<Pinloom::PinloomHostDocument> {
            Pinloom::PinloomHostDocument document;
            document.entry.id = identity.entryId;
            document.entry.resourceId = identity.resourceId;
            document.entry.name = QStringLiteral("Protocol entry");
            document.content = QStringLiteral("authoritative content");
            return document;
        };
        Pinloom::PinloomSuiteIntegration integration(std::move(callbacks));
        const QJsonObject response = integration.processRequestForTesting(
            SuiteApp::makeRequest(
                QStringLiteral("resource.resolve"),
                {{QStringLiteral("uri"),
                  QStringLiteral("pinloom://entry/e1?resource=r1")}}));
        QVERIFY(response.value(QStringLiteral("ok")).toBool());
        QCOMPARE(response.value(QStringLiteral("result")).toObject()
                     .value(QStringLiteral("content")).toString(),
                 QStringLiteral("authoritative content"));
    }

    void queuesUiOpenAfterResponding()
    {
        bool opened = false;
        Pinloom::PinloomHostBridgeCallbacks callbacks;
        callbacks.resolve = [](const Pinloom::PinloomHostIdentity& identity)
            -> std::optional<Pinloom::PinloomHostDocument> {
            Pinloom::PinloomHostDocument document;
            document.entry.id = identity.entryId;
            document.entry.resourceId = identity.resourceId;
            document.entry.name = QStringLiteral("Queued entry");
            return document;
        };
        callbacks.open = [&opened](const Pinloom::PinloomHostIdentity&,
                                   QString*) {
            opened = true;
            return true;
        };
        Pinloom::PinloomSuiteIntegration integration(std::move(callbacks));
        const QJsonObject response = integration.processRequestForTesting(
            SuiteApp::makeRequest(
                QStringLiteral("action.invoke"),
                {{QStringLiteral("actionId"),
                  QStringLiteral("pinloom.entry.open")},
                 {QStringLiteral("resourceUri"),
                  QStringLiteral("pinloom://entry/e1?resource=r1")}}));
        QVERIFY(response.value(QStringLiteral("ok")).toBool());
        QVERIFY(response.value(QStringLiteral("result")).toObject()
                    .value(QStringLiteral("accepted")).toBool());
        QVERIFY(!opened);
        QTRY_VERIFY(opened);
    }
};

QTEST_GUILESS_MAIN(PinloomSuiteAppIntegrationTest)

#include "suiteapp_integration_test.moc"
