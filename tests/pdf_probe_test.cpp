#include "pinloom/core/SumatraPdfDdeClient.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTest>
#include <QThread>
#include <cstdio>

class PdfProbeTest : public QObject {
    Q_OBJECT
private slots:
    void boundedResponsesAndFailures()
    {
        const auto health = Pinloom::requestSumatraPdfDdeCommand(QStringLiteral("ProbeHealth()"), 500);
        QVERIFY2(health.success(), qPrintable(health.error));
        QCOMPARE(health.text, QStringLiteral("ready"));
        const QString program = QCoreApplication::applicationFilePath();
        const auto success = Pinloom::runSumatraPdfDdeProbe(program, QStringLiteral("ok"), 100);
        QVERIFY2(success.success(), qPrintable(success.error));
        QCOMPARE(success.text, QStringLiteral("fixture response"));
        QVERIFY(!Pinloom::runSumatraPdfDdeProbe(program, QStringLiteral("invalid"), 100).success());
        QVERIFY(!Pinloom::runSumatraPdfDdeProbe(program, QStringLiteral("crash"), 100).success());
        QVERIFY(!Pinloom::runSumatraPdfDdeProbe(program + QStringLiteral(".missing"), QStringLiteral("ok"), 100).success());
        QElapsedTimer elapsed;
        elapsed.start();
        const auto hung = Pinloom::runSumatraPdfDdeProbe(program, QStringLiteral("hang"), 50);
        QVERIFY(hung.error.contains(QStringLiteral("timed out")));
        QVERIFY2(elapsed.elapsed() < 2000, qPrintable(QString::number(elapsed.elapsed())));
        QVERIFY(Pinloom::runSumatraPdfDdeProbe(program, QStringLiteral("ok"), 100).success());
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 4 && args[1] == QLatin1String("--request")) {
        if (args[2] == QLatin1String("hang")) QThread::msleep(10000);
        if (args[2] == QLatin1String("crash")) return 7;
        QFile output;
        if (!output.open(stdout, QIODevice::WriteOnly)) return 2;
        output.write(args[2] == QLatin1String("invalid") ? "invalid"
                     : R"({"text":"fixture response","error":""})");
        output.flush();
        return 0;
    }
    PdfProbeTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "pdf_probe_test.moc"
