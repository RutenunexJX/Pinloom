#include "pinloom/widgets/PinloomOpenService.h"
#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/core/SqliteLibraryRepository.h"
#include "pinloom/core/NativeAnchorCapture.h"
#include "pinloom/core/AnchorCaptureDraft.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <cstdio>
#include <memory>

using namespace Pinloom;

static PinloomOpenTarget seedTarget(InMemoryLibraryRepository &repository, const QString &scenario)
{
    Resource resource;
    resource.id = scenario;
    resource.title = scenario;
    resource.location = "E:/office-case-" + scenario + ".xlsx";
    resource.kind = ResourceKind::File;
    Anchor anchor;
    anchor.id = "anchor-" + scenario;
    anchor.name = "Anchor " + scenario;
    anchor.targetFile = resource.location;
    anchor.locatorType = "excel.range";
    anchor.locatorJson = R"({"sheet":"Data","range":"B2"})";
    resource.anchors = {anchor};
    repository.upsertResource(resource);
    PinloomOpenTarget target;
    target.resourceId = resource.id;
    target.location = resource.location;
    target.anchor = repository.findResource(resource.id)->anchors.first();
    return target;
}

static PinloomOpenServiceOptions childOptions()
{
    PinloomOpenServiceOptions options;
    options.applicationLaunchSettings.powerShellExecutablePath = QCoreApplication::applicationFilePath();
    options.officeAvailabilityHandler = [](const QString &) { return true; };
    options.officeTimeoutMilliseconds = 5000;
    return options;
}

class OfficeOpenServiceTest final : public QObject {
    Q_OBJECT
private slots:
    void processCompletionIsAuthoritative_data();
    void processCompletionIsAuthoritative();
    void cancellationAndSupersession();
    void destructionDropsPendingCallbacks();
    void synchronousStatusObserverCanDestroyCaller_data();
    void synchronousStatusObserverCanDestroyCaller();
    void installedOfficeTemporaryDocuments();
};

void OfficeOpenServiceTest::processCompletionIsAuthoritative_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::newRow("verified-selection") << QString("success");
    QTest::newRow("started-but-location-failed") << QString("failed");
    QTest::newRow("zero-exit-without-confirmation") << QString("unconfirmed");
    QTest::newRow("timeout") << QString("timeout");
    QTest::newRow("launch-failed") << QString("missing-executable");
    QTest::newRow("office-unavailable") << QString("unavailable");
}

void OfficeOpenServiceTest::processCompletionIsAuthoritative()
{
    QFETCH(QString, scenario);
    InMemoryLibraryRepository repository;
    const auto target = seedTarget(repository, scenario);
    auto options = childOptions();
    if (scenario == "timeout") options.officeTimeoutMilliseconds = 120;
    if (scenario == "missing-executable") options.applicationLaunchSettings.powerShellExecutablePath = "E:/nonexistent-pinloom-fixture.exe";
    if (scenario == "unavailable") options.officeAvailabilityHandler = [](const QString &) { return false; };
    PinloomOpenService service(repository, options);
    QSignalSpy completed(&service, &PinloomOpenService::navigationCompleted);
    QTimer heartbeat;
    int ticks = 0;
    connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
    heartbeat.start(10);
    QElapsedTimer elapsed;
    elapsed.start();
    QCOMPARE(service.open(target), scenario != "unavailable");
    QVERIFY(elapsed.elapsed() < 500);
    QVERIFY(!repository.resourceUsage(target.resourceId));
    if (scenario != "unavailable") {
        QVERIFY(service.hasPendingOpen());
        QCOMPARE(completed.size(), 0);
        QVERIFY(service.statusText().startsWith("Opening"));
    }
    QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 10000);
    QVERIFY(!service.hasPendingOpen());
    const bool success = scenario == "success";
    QCOMPARE(completed.first()[2].toString(), success ? QString("located") : QString("failed"));
    QCOMPARE(completed.first()[3].toBool(), success);
    const auto usage = repository.resourceUsage(target.resourceId);
    QCOMPARE(usage.has_value(), success);
    if (success) QCOMPARE(usage->openCount, 1);
    if (scenario == "timeout") {
        QVERIFY(ticks > 2);
        QVERIFY(service.statusText().contains("timed out"));
    }
}

void OfficeOpenServiceTest::cancellationAndSupersession()
{
    InMemoryLibraryRepository repository;
    const auto delayed = seedTarget(repository, "delayed");
    const auto success = seedTarget(repository, "success");
    PinloomOpenService service(repository, childOptions());
    QSignalSpy completed(&service, &PinloomOpenService::navigationCompleted);
    QVERIFY(service.open(delayed));
    QVERIFY(!service.cancelOpen(delayed.resourceId, "wrong-anchor"));
    QVERIFY(service.cancelOpen(delayed.resourceId, delayed.anchor->id));
    QCOMPARE(completed.size(), 1);
    QCOMPARE(completed.first()[2].toString(), QString("cancelled"));
    QVERIFY(!service.hasPendingOpen());
    QVERIFY(service.open(delayed));
    QVERIFY(service.open(success));
    QCOMPARE(completed.size(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 3, 10000);
    QCOMPARE(completed.last()[0].toString(), success.resourceId);
    QCOMPARE(completed.last()[2].toString(), QString("located"));
    QTest::qWait(400);
    QCOMPARE(completed.size(), 3);
    QVERIFY(!repository.resourceUsage(delayed.resourceId));
}

void OfficeOpenServiceTest::destructionDropsPendingCallbacks()
{
    InMemoryLibraryRepository repository;
    const auto target = seedTarget(repository, "delayed");
    auto service = std::make_unique<PinloomOpenService>(repository, childOptions());
    int callbacks = 0;
    connect(service.get(), &PinloomOpenService::navigationCompleted, this, [&] { ++callbacks; });
    QVERIFY(service->open(target));
    QTest::qWait(30);
    QElapsedTimer elapsed;
    elapsed.start();
    service.reset();
    QVERIFY(elapsed.elapsed() < 500);
    QTest::qWait(400);
    QCOMPARE(callbacks, 0);
    QVERIFY(!repository.resourceUsage(target.resourceId));
}

void OfficeOpenServiceTest::synchronousStatusObserverCanDestroyCaller_data()
{
    QTest::addColumn<QString>("scenario");
    for (const auto *name : {"native", "unavailable", "Excel", "Visio", "Word", "PowerPoint"})
        QTest::newRow(name) << QString::fromLatin1(name);
}

void OfficeOpenServiceTest::synchronousStatusObserverCanDestroyCaller()
{
    QFETCH(QString, scenario);
    InMemoryLibraryRepository repository;
    auto target = seedTarget(repository, "success");
    auto options = childOptions();
    if (scenario == "unavailable") options.officeAvailabilityHandler = [](const QString &) { return false; };
    if (scenario == "Excel") options.excelLaunchHandler = [](const auto &, QString *) { return true; };
    if (scenario == "Visio") {
        target.anchor->locatorType = "visio.shape";
        target.anchor->locatorJson = R"({"pageNameU":"Page-1","shapeUniqueId":"{fixture}"})";
        options.visioLaunchHandler = [](const auto &, QString *) { return true; };
    }
    if (scenario == "Word") {
        target.anchor->locatorType = "word.bookmark";
        target.anchor->locatorJson = R"({"bookmark":"FixtureBookmark"})";
        options.wordLaunchHandler = [](const auto &, QString *) { return true; };
    }
    if (scenario == "PowerPoint") {
        target.anchor->locatorType = "powerpoint.shape";
        target.anchor->locatorJson = R"({"slide":1,"shape_id":3})";
        options.powerPointLaunchHandler = [](const auto &, QString *) { return true; };
    }
    QPointer<PinloomOpenService> service = new PinloomOpenService(repository, options);
    connect(service, &PinloomOpenService::statusChanged, this, [&] { delete service.data(); });
    QVERIFY(!service->open(target));
    QVERIFY(service.isNull());
    QTest::qWait(50);
    QVERIFY(!repository.resourceUsage(target.resourceId));
}

void OfficeOpenServiceTest::installedOfficeTemporaryDocuments()
{
    const QString fixturePath = qEnvironmentVariable("PINLOOM_OFFICE_FIXTURE");
    if (fixturePath.isEmpty()) QSKIP("Set PINLOOM_OFFICE_FIXTURE to an isolated temporary Office fixture manifest");
    QFile file(fixturePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto cases = QJsonDocument::fromJson(file.readAll()).array();
    QVERIFY(cases.size() >= 4);
    QTemporaryDir dir;
    SqliteLibraryRepository repository;
    QVERIFY(repository.open(dir.filePath("office.db")));
    QVERIFY(repository.initialize());
    for (const auto &item : cases) {
        const auto object = item.toObject();
        AnchorCaptureDraft draft;
        draft.targetApp = object["application"].toString();
        draft.targetFile = object["path"].toString();
        draft.locatorType = object["locator"].toObject()["type"].toString();
        draft.locatorJson = QString::fromUtf8(QJsonDocument(object["locator"].toObject()).toJson(QJsonDocument::Compact));
        draft.suggestedName = object["name"].toString();
        const auto saved = AnchorCaptureCommitService(repository).commit(draft, QUuid::createUuid().toString());
        QVERIFY2(saved.success(), qPrintable(saved.error));
        PinloomOpenTarget target;
        target.resourceId = saved.resource.id;
        target.location = saved.resource.location;
        target.anchor = saved.anchor;
        PinloomOpenService service(repository, {});
        QSignalSpy completed(&service, &PinloomOpenService::navigationCompleted);
        QVERIFY2(service.open(target), qPrintable(service.statusText()));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 40000);
        qInfo() << "OFFICE_COM" << draft.targetApp << draft.suggestedName << completed.first();
        QCOMPARE(completed.first()[2].toString(), QString("located"));
        QVERIFY(completed.first()[3].toBool());
        if (draft.targetApp != "PowerPoint") {
            auto captured = NativeAnchorCaptureAdapter().capture(
                draft.targetApp == "Excel" ? NativeAnchorApplication::Excel
                : draft.targetApp == "Visio" ? NativeAnchorApplication::Visio : NativeAnchorApplication::Word);
            QVERIFY2(captured.success(), qPrintable(captured.error));
            qInfo() << "OFFICE_NATIVE_CAPTURE" << draft.targetApp << captured.draft.locatorJson;
            QCOMPARE(captured.draft.locatorType, draft.locatorType);
            captured.draft.suggestedName = draft.suggestedName + " captured";
            const auto roundTrip = AnchorCaptureCommitService(repository).commit(captured.draft, QUuid::createUuid().toString());
            QVERIFY2(roundTrip.success(), qPrintable(roundTrip.error));
            PinloomOpenTarget capturedTarget = target;
            capturedTarget.resourceId = roundTrip.resource.id;
            capturedTarget.anchor = repository.findResource(roundTrip.resource.id)->anchors.last();
            completed.clear();
            QVERIFY(service.open(capturedTarget));
            QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 40000);
            qInfo() << "OFFICE_CAPTURE_REOPEN" << draft.targetApp << completed.first();
            QCOMPARE(completed.first()[2].toString(), QString("located"));
        }
        const auto beforeFailure = repository.resourceUsage(target.resourceId)->openCount;
        auto locator = object["locator"].toObject();
        if (draft.targetApp == "Excel") locator.insert(locator["type"] == "excel.name" ? "namedRange" : "range", "PINLOOM_MISSING_TARGET");
        else if (draft.targetApp == "Visio") locator.insert("pageNameU", "PINLOOM_MISSING_PAGE");
        else if (draft.targetApp == "Word") locator.insert("bookmark", "PINLOOM_MISSING_BOOKMARK");
        else { locator.insert("shape_id", 2147483647); locator.remove("shape_name"); }
        target.anchor->locatorJson = QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));
        completed.clear();
        QVERIFY(service.open(target));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 40000);
        qInfo() << "OFFICE_COM_MISSING" << draft.targetApp << completed.first();
        QCOMPARE(completed.first()[2].toString(), QString("failed"));
        QVERIFY(!completed.first()[3].toBool());
        QCOMPARE(repository.resourceUsage(target.resourceId)->openCount, beforeFailure);
    }
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (application.arguments().contains("-Command")) {
        const QString script = application.arguments().last();
        if (script.contains("office-case-timeout")) QThread::msleep(2000);
        if (script.contains("office-case-delayed")) QThread::msleep(300);
        if (script.contains("office-case-failed")) { std::fputs("COM location failed\n", stderr); return 1; }
        if (!script.contains("office-case-unconfirmed")) std::fputs("PinloomOfficeLocated\n", stdout);
        return 0;
    }
    OfficeOpenServiceTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "office_open_service_test.moc"
