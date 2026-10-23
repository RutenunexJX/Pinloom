#include "pinloom/application/CommandService.h"
#include "pinloom/application/NativePdfCapture.h"
#include "pinloom/core/SqliteLibraryRepository.h"
#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/clip/ObsidianClipStore.h"
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <memory>

using namespace Pinloom;
namespace {
QJsonObject capture(const QString &path)
{
    const QFileInfo file(path);
    return {{"captureProtocol", "pinloom-pdf-capture/v1"}, {"captureId", QUuid::createUuid().toString()},
        {"targetFile", path}, {"documentSize", QString::number(file.size())},
        {"documentModifiedMs", QString::number(file.lastModified().toMSecsSinceEpoch())},
        {"name", QString::fromUtf8("原生区域")}, {"tags", QJsonArray{"#review", "review", QString::fromUtf8("裁剪😀")}},
        {"ifVersion", "absent"}, {"locator", QJsonObject{{"type", "sumatrapdf.rect"}, {"version", 3},
            {"coordinateSpace", "page-top-left"}, {"unit", "pt"}, {"page", 2},
            {"rect", QJsonArray{20, 30, 120, 90}}, {"mediaBox", QJsonArray{-20, -30, 600, 800}},
            {"cropBox", QJsonArray{10, 20, 510, 720}}, {"rotation", 0}, {"userUnit", 1.0}}}};
}
bool fixture(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("%PDF-1.7\n% Synthetic metadata validation fixture\n") > 0;
}
}
class NativePdfCaptureTest : public QObject {
    Q_OBJECT
private slots:
    void projectedLookupPreservesLifecycle_data()
    {
        QTest::addColumn<bool>("sqlite");
        QTest::newRow("sqlite-projections") << true;
        QTest::newRow("default-repository-projections") << false;
    }
    void projectedLookupPreservesLifecycle()
    {
        QFETCH(bool, sqlite);
        QTemporaryDir temp;
        const auto path = temp.filePath("fixture.pdf"); QVERIFY(fixture(path));
        std::unique_ptr<ILibraryRepository> repository;
        if (sqlite) {
            auto database = std::make_unique<SqliteLibraryRepository>();
            QVERIFY(database->open(temp.filePath("library.sqlite3"), temp.filePath("identity.sqlite3")));
            QVERIFY(database->initialize());
            repository = std::move(database);
        } else repository = std::make_unique<InMemoryLibraryRepository>();
        auto params = capture(path);
        const auto prepared = nativePdfCaptureDraft(params); QVERIFY(prepared.error.isEmpty());
        const auto id = params["captureId"].toString();
        AnchorCaptureCommitService service(*repository);
        const auto created = service.commit(prepared.draft, id); QVERIFY2(created.success(), qPrintable(created.error));
        auto resource = created.resource;
        resource.content = "Searchable content must survive capture and receipt reads";
        resource.aliases = {"Existing metadata alias"};
        resource.tags = {"Existing resource tag"};
        resource.explicitlyRetained = true;
        resource.location = temp.path() + "/./fixture.pdf";
        QVERIFY(repository->upsertResource(resource));
        auto found = service.findCapture(id);
        QVERIFY(found.success()); QCOMPARE(found.resource.content, resource.content);
        QCOMPARE(found.resource.aliases, resource.aliases);

        // Imported receipts need not use native IDs; preserve the old resource-ID
        // tie break, anchor order, and JSON decoding (including escaped UUIDs).
        auto imported = resource;
        imported.id = "000-imported-receipt";
        imported.title = "Imported receipt";
        imported.aliases.clear();
        auto first = created.anchor;
        first.id = "imported-first"; first.name = "Imported first";
        const auto normalizedId = QUuid(id).toString(QUuid::WithoutBraces);
        QString escapedId = QString("\\u%1").arg(int(normalizedId[0].unicode()), 4, 16, QLatin1Char('0')) + normalizedId.mid(1);
        first.locatorJson.replace(normalizedId, escapedId);
        auto second = first; second.id = "imported-second"; second.name = "Imported second";
        imported.anchors = {first, second};
        QVERIFY2(repository->upsertResource(imported), qPrintable(repository->lastError()));
        found = service.findCapture(id);
        QVERIFY(found.success()); QCOMPARE(found.resource.id, imported.id); QCOMPARE(found.anchor.id, first.id);
        QVERIFY(repository->softDeleteResource(imported.id));
        found = service.findCapture(id);
        QVERIFY(found.success()); QVERIFY(found.resource.deleted); QCOMPARE(found.anchor.id, first.id);
        QVERIFY(repository->restoreResource(imported.id));
        LibraryBatchMutation purge; purge.permanentlyDeleteResourceIds = {imported.id};
        QVERIFY(repository->applyBatch(purge));
        QCOMPARE(service.findCapture(id).resource.id, resource.id);
        QVERIFY(repository->softDeleteAnchor(resource.id, created.anchor));
        found = service.findCapture(id); QVERIFY(found.success()); QVERIFY(found.anchor.deleted);
        QVERIFY(repository->restoreAnchor(resource.id, found.anchor));
        QVERIFY(!service.findCapture(id).anchor.deleted);

        auto nextDraft = prepared.draft; nextDraft.suggestedName = "Same file after metadata edits";
        const auto next = service.commit(nextDraft, QUuid::createUuid().toString());
        QVERIFY2(next.success(), qPrintable(next.error)); QCOMPARE(next.resource.id, resource.id);
        QCOMPARE(next.resource.content, resource.content); QCOMPARE(next.resource.tags, resource.tags);
        QCOMPARE(next.resource.aliases, resource.aliases); QCOMPARE(next.resource.anchors.size(), 2);
        QVERIFY(repository->softDeleteResource(resource.id));
        nextDraft.suggestedName = "Same file after resource deletion";
        const auto fresh = service.commit(nextDraft, QUuid::createUuid().toString());
        QVERIFY2(fresh.success(), qPrintable(fresh.error)); QVERIFY(fresh.resource.id != resource.id);
        QVERIFY(service.findCapture(id).resource.deleted);
        QVERIFY(repository->clearResources());
        QCOMPARE(service.findCapture(id).errorCode, "not_found");
        QVERIFY(repository->resourceLocations().isEmpty());
        QVERIFY(repository->anchorLocators().isEmpty());
    }
    void acceptsIntrinsicGeometry_data()
    {
        QTest::addColumn<int>("rotation");
        QTest::addColumn<double>("unit");
        for (int rotation : {0, 90, 180, 270})
            for (double unit : {1.0, 1.5, 2.0})
                QTest::newRow(qPrintable(QString("%1-%2").arg(rotation).arg(unit))) << rotation << unit;
    }
    void acceptsIntrinsicGeometry()
    {
        QFETCH(int, rotation); QFETCH(double, unit);
        QTemporaryDir temp; const auto path = temp.filePath("fixture.pdf"); QVERIFY(fixture(path));
        auto params = capture(path); auto locator = params["locator"].toObject();
        locator["rotation"] = rotation; locator["userUnit"] = unit;
        const bool swapped = rotation == 90 || rotation == 270;
        locator["rect"] = QJsonArray{0, 0, (swapped ? 700 : 500) * unit, (swapped ? 500 : 700) * unit};
        params["locator"] = locator;
        const auto prepared = nativePdfCaptureDraft(params);
        QVERIFY2(prepared.error.isEmpty(), qPrintable(prepared.error));
        const auto stored = QJsonDocument::fromJson(prepared.draft.locatorJson.toUtf8()).object();
        QCOMPARE(stored["rect"], locator["rect"]); QCOMPARE(stored["cropBox"], locator["cropBox"]);
        QCOMPARE(stored["rotation"].toInt(), rotation); QCOMPARE(stored["userUnit"].toDouble(), unit);
        QCOMPARE(stored["provenance"].toObject()["adapter"].toString(), "sumatra-pinloom/v1");
        QVERIFY(!stored.contains("zoom"));
        locator["rect"] = QJsonArray{0, 0, (swapped ? 700 : 500) * unit + 2, 20}; params["locator"] = locator;
        QVERIFY(!nativePdfCaptureDraft(params).error.isEmpty());
    }
    void rejectsStaleAndInvalidSelections()
    {
        QTemporaryDir temp; const auto path = temp.filePath("fixture.pdf"); QVERIFY(fixture(path));
        const auto original = capture(path);
        for (const auto region : {QJsonArray{0, 0, 0.49, 20}, QJsonArray{-1, 0, 20, 20}, QJsonArray{0, 0, 501, 20}}) {
            auto p = original; auto locator = p["locator"].toObject(); locator["rect"] = region; p["locator"] = locator;
            QCOMPARE(nativePdfCaptureDraft(p).errorCode, "invalid_capture");
        }
        auto p = original; auto locator = p["locator"].toObject(); locator["page"] = 1.5; p["locator"] = locator;
        QCOMPARE(nativePdfCaptureDraft(p).errorCode, "invalid_capture");
        p = original; p["captureProtocol"] = "unknown/v2";
        QCOMPARE(nativePdfCaptureDraft(p).errorCode, "invalid_capture");
        p = original; p["name"] = "  "; QCOMPARE(nativePdfCaptureDraft(p).errorCode, "invalid_capture");
        QFile file(path); QVERIFY(file.open(QIODevice::Append)); file.write("changed"); file.close();
        QCOMPARE(nativePdfCaptureDraft(original).errorCode, "target_changed");
    }
    void durableIdempotencyConflictAndWriteGuard()
    {
        QTemporaryDir temp; const auto path = temp.filePath("fixture.pdf"); QVERIFY(fixture(path));
        auto params = capture(path);
        QString resourceId, anchorId;
        {
            SqliteLibraryRepository library; SqliteClipRepository clips;
            const auto identity = temp.filePath("identity.sqlite3");
            QVERIFY(library.open(temp.filePath("pinloom.sqlite3"), identity)); QVERIFY(library.initialize());
            QVERIFY(clips.open(temp.filePath("clips.sqlite3"), identity)); QVERIFY(clips.initialize());
            ObsidianClipStore store; PersistentClipService persistent(clips, store);
            QString blocked; int refreshes = 0;
            CommandSessionCallbacks callbacks;
            callbacks.writeBlockReason = [&] { return blocked; };
            callbacks.refreshViews = [&] { ++refreshes; };
            CommandService service(library, clips, persistent, temp.path(), callbacks);
            const auto invoke = [&](const QString &method, const QJsonObject &p) {
                return service.execute({{"protocol", "pinloom-cli/v1"}, {"library", temp.path()},
                    {"baseDirectory", temp.path()}, {"method", method}, {"params", p}});
            };
            auto preview = params; preview["dryRun"] = true;
            const auto validated = invoke("capture.pdf", preview);
            QVERIFY(validated["ok"].toBool()); QCOMPARE(validated["result"].toObject()["state"].toString(), "validated");
            QVERIFY(!validated["persisted"].toBool()); QCOMPARE(refreshes, 0);
            blocked = "Pending synthetic edit";
            QCOMPARE(invoke("capture.pdf", params)["exitCode"].toInt(), 4); QCOMPARE(refreshes, 0);
            blocked.clear();
            const auto saved = invoke("capture.pdf", params);
            QVERIFY2(saved["ok"].toBool(), QJsonDocument(saved).toJson().constData());
            QVERIFY(saved["persisted"].toBool()); QVERIFY(saved["guiAdopted"].toBool()); QCOMPARE(refreshes, 1);
            const auto result = saved["result"].toObject(); resourceId = result["resourceId"].toString(); anchorId = result["id"].toString();
            QCOMPARE(result["tags"].toArray().size(), 2);
            const auto replay = invoke("capture.pdf", params)["result"].toObject();
            QVERIFY(replay["replayed"].toBool()); QCOMPARE(replay["id"], result["id"]); QCOMPARE(replay["version"], result["version"]);
            auto changed = params; changed["name"] = "Different request";
            QCOMPARE(invoke("capture.pdf", changed)["error"].toObject()["code"].toString(), "capture_conflict");
            changed = params; changed["captureId"] = QUuid::createUuid().toString();
            QVERIFY(!invoke("capture.pdf", changed)["ok"].toBool());
            changed["name"] = "Retried after name conflict";
            const auto recovered = invoke("capture.pdf", changed);
            QVERIFY2(recovered["ok"].toBool(), QJsonDocument(recovered).toJson().constData());
            QCOMPARE(library.findResource(resourceId)->anchors.size(), 2);
            const auto status = invoke("capture.status", {{"captureId", params["captureId"]}});
            QCOMPARE(status["result"].toObject()["id"].toString(), anchorId);
        }
        SqliteLibraryRepository reopened;
        QVERIFY(reopened.open(temp.filePath("pinloom.sqlite3"), temp.filePath("identity.sqlite3"))); QVERIFY(reopened.initialize());
        const auto draft = nativePdfCaptureDraft(params); QVERIFY(draft.error.isEmpty());
        const auto retry = AnchorCaptureCommitService(reopened).commit(draft.draft, params["captureId"].toString());
        QVERIFY2(retry.success(), qPrintable(retry.error)); QVERIFY(retry.replayed);
        QCOMPARE(retry.resource.id, resourceId); QCOMPARE(retry.anchor.id, anchorId);
        QCOMPARE(retry.anchor.tags, QStringList({"review", QString::fromUtf8("裁剪😀")}));
    }
};
QTEST_GUILESS_MAIN(NativePdfCaptureTest)
#include "native_pdf_capture_test.moc"
