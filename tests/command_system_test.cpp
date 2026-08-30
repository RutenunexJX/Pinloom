#include "pinloom/widgets/PinloomCommandSystem.h"

#include <QSet>
#include <QtTest>

using namespace Pinloom;

class CommandSystemTest final : public QObject {
    Q_OBJECT

private slots:
    void registryHasStableUniqueRowsAndCompatibleInputAliases();
    void dispatcherRejectsDuplicateHandlersAndPreservesOutcomes();
};

void CommandSystemTest::registryHasStableUniqueRowsAndCompatibleInputAliases()
{
    QString error;
    QVERIFY2(PinloomCommandRegistry::validate(&error), qPrintable(error));

    QSet<int> ids;
    QSet<QString> canonicalRows;
    for (const PinloomCommandDefinition &definition
         : PinloomCommandRegistry::definitions()) {
        QVERIFY(!ids.contains(static_cast<int>(definition.id)));
        QVERIFY(!canonicalRows.contains(definition.canonical.toCaseFolded()));
        ids.insert(static_cast<int>(definition.id));
        canonicalRows.insert(definition.canonical.toCaseFolded());
        for (const QString &alias : definition.aliases) {
            QVERIFY(!canonicalRows.contains(alias.toCaseFolded()));
        }
    }

    PinloomParsedCommand parsed = PinloomCommandRegistry::parse(
        QStringLiteral("anchor:rect"));
    QCOMPARE(parsed.action, PinloomCommandId::AnchorPdfRectangle);
    QCOMPARE(parsed.commandNamespace, PinloomCommandNamespace::Anchor);

    parsed = PinloomCommandRegistry::parse(QStringLiteral("k;te"));
    QCOMPARE(parsed.action, PinloomCommandId::AnchorPdfText);

    parsed = PinloomCommandRegistry::parse(QStringLiteral("clip;text"));
    QCOMPARE(parsed.action, PinloomCommandId::ClipPdfText);

    parsed = PinloomCommandRegistry::parse(QStringLiteral("search phase noise"));
    QCOMPARE(parsed.action, PinloomCommandId::OpenSearch);
    QCOMPARE(parsed.query, QStringLiteral("phase noise"));

    parsed = PinloomCommandRegistry::parse(QStringLiteral("status"));
    QCOMPARE(parsed.action, PinloomCommandId::Diagnostics);

    parsed = PinloomCommandRegistry::parse(QStringLiteral("c s"));
    QVERIFY(!parsed.recognized());
}

void CommandSystemTest::dispatcherRejectsDuplicateHandlersAndPreservesOutcomes()
{
    PinloomCommandDispatcher dispatcher;
    const PinloomCommandDispatchResult missing = dispatcher.dispatch(
        PinloomCommandId::Settings);
    QVERIFY(missing.failed());
    QVERIFY(missing.message.contains(QStringLiteral("settings")));

    int calls = 0;
    QString observedSource;
    QString error;
    QVERIFY(dispatcher.registerHandler(
        PinloomCommandId::Settings,
        [&calls, &observedSource](const PinloomCommandInvocation &invocation) {
            ++calls;
            observedSource = invocation.arguments.value(
                QStringLiteral("source")).toString();
            return PinloomCommandDispatchResult::complete(
                QStringLiteral("settings complete"));
        },
        &error));
    QVERIFY(error.isEmpty());
    QVERIFY(!dispatcher.registerHandler(
        PinloomCommandId::Settings,
        [](const PinloomCommandInvocation &) {
            return PinloomCommandDispatchResult::complete();
        },
        &error));
    QVERIFY(error.contains(QStringLiteral("already registered")));

    PinloomCommandInvocation invocation;
    invocation.arguments.insert(QStringLiteral("source"), QStringLiteral("test"));
    const PinloomCommandDispatchResult completed = dispatcher.dispatch(
        PinloomCommandId::Settings, invocation);
    QVERIFY(completed.completed());
    QCOMPARE(calls, 1);
    QCOMPARE(observedSource, QStringLiteral("test"));

    QVERIFY(dispatcher.registerHandler(
        PinloomCommandId::Diagnostics,
        [](const PinloomCommandInvocation &) {
            return PinloomCommandDispatchResult::cancel(
                QStringLiteral("diagnostics canceled"));
        }));
    const PinloomCommandDispatchResult cancelled = dispatcher.dispatch(
        PinloomCommandId::Diagnostics);
    QVERIFY(cancelled.cancelled());
    QVERIFY(!cancelled.completed());

    const PinloomCommandDispatchResult failure =
        pinloomCommandResultFromBoolean(
            false,
            QStringLiteral("capture failed"),
            QStringLiteral("captured"),
            QStringLiteral("capture failed"));
    QVERIFY(failure.failed());
    const PinloomCommandDispatchResult cancel =
        pinloomCommandResultFromBoolean(
            false,
            QStringLiteral("capture canceled"),
            QStringLiteral("captured"),
            QStringLiteral("capture failed"));
    QVERIFY(cancel.cancelled());
}

QTEST_GUILESS_MAIN(CommandSystemTest)

#include "command_system_test.moc"
