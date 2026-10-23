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
QJsonObject textCapture(const QString &path)
{
    auto request = capture(path);
    auto locator = request["locator"].toObject();
    locator["type"] = "sumatrapdf.text";
    locator["text"] = QString::fromUtf8("  重复文字\nsecond line  \n跨页😀");
    locator["selection"] = QJsonObject{{"startPage", 2}, {"endPage", 3},
        {"startGlyph", 12}, {"endGlyph", 8}};
    request["locator"] = locator;
    return request;
}
}
class NativePdfCaptureTest : public QObject {
    Q_OBJECT
private slots:
    void preservesNativeTextAndDistinctOccurrences()
    {
        QTemporaryDir temp; const auto path = temp.filePath("中文 PDF.pdf"); QVERIFY(fixture(path));
        auto params = textCapture(path);
        const auto prepared = nativePdfCaptureDraft(params);
        QVERIFY2(prepared.error.isEmpty(), qPrintable(prepared.error));
        const auto locator = params["locator"].toObject();
        const auto stored = QJsonDocument::fromJson(prepared.draft.locatorJson.toUtf8()).object();
        QCOMPARE(prepared.draft.locatorType, "sumatrapdf.text");
        QCOMPARE(stored["text"], locator["text"]);
        QCOMPARE(stored["selection"], locator["selection"]);
        QCOMPARE(stored["document"].toObject()["size"], params["documentSize"]);
        InMemoryLibraryRepository repository;
        AnchorCaptureCommitService service(repository);
        const auto id = params["captureId"].toString();
        const auto saved = service.commit(prepared.draft, id); QVERIFY(saved.success());
        auto otherLocator = locator;
        otherLocator["selection"] = QJsonObject{{"startPage", 2}, {"endPage", 3},
            {"startGlyph", 40}, {"endGlyph", 8}};
        params["locator"] = otherLocator;
        const auto other = nativePdfCaptureDraft(params); QVERIFY(other.error.isEmpty());
        QCOMPARE(service.commit(other.draft, id).errorCode, "capture_conflict");
        QCOMPARE(service.findCapture(id).anchor.locatorJson, saved.anchor.locatorJson);
    }
    void rejectsInvalidNativeTextWithoutTruncating()
    {
        QTemporaryDir temp; const auto path = temp.filePath("fixture.pdf"); QVERIFY(fixture(path));
        const auto original = textCapture(path);
        for (const auto range : {
                 QJsonObject{{"startPage", 3}, {"endPage", 2}, {"startGlyph", 1}, {"endGlyph", 4}},
                 QJsonObject{{"startPage", 2}, {"endPage", 2}, {"startGlyph", 4}, {"endGlyph", 4}},
                 QJsonObject{{"startPage", 2}, {"endPage", 1002}, {"startGlyph", 1}, {"endGlyph", 4}},
                 QJsonObject{{"startPage", 2}, {"endPage", 3}, {"startGlyph", -1}, {"endGlyph", 4}},
                 QJsonObject{{"startPage", 2}, {"endPage", 3}, {"startGlyph", 1.5}, {"endGlyph", 4}},
                 QJsonObject{{"startPage", 2}, {"endPage", 3}, {"startGlyph", 1}}}) {
            auto request = original; auto locator = request["locator"].toObject();
            locator["selection"] = range; request["locator"] = locator;
            QCOMPARE(nativePdfCaptureDraft(request).errorCode, "invalid_capture");
        }
        for (const auto &text : {QString(" \n\t"), QString(65537, 'a'), QString("bad") + QChar::Null}) {
            auto request = original; auto locator = request["locator"].toObject();
            locator["text"] = text; request["locator"] = locator;
            QCOMPARE(nativePdfCaptureDraft(request).errorCode, "invalid_capture");
        }
        auto request = original; auto locator = request["locator"].toObject();
        locator["text"] = QString(65536, 'a'); request["locator"] = locator;
        const auto maximum = nativePdfCaptureDraft(request); QVERIFY(maximum.error.isEmpty());
        QCOMPARE(QJsonDocument::fromJson(maximum.draft.locatorJson.toUtf8()).object()["text"], locator["text"]);
    }
    void nativeTextMetadataUpdatePreservesReopenFields()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("中文 fixture.pdf"); QVERIFY(fixture(path));
        const auto params = textCapture(path);
        const auto renamed = QString::fromUtf8("修改后的文本锚点");
        const QJsonArray tags{QString::fromUtf8("跨页"), "reviewed"};
        QJsonObject expectedLocator;
        QString resourceId, anchorId;
        {
            SqliteLibraryRepository library; SqliteClipRepository clips;
            const auto identity = temp.filePath("identity.sqlite3");
            QVERIFY(library.open(temp.filePath("pinloom.sqlite3"), identity)); QVERIFY(library.initialize());
            QVERIFY(clips.open(temp.filePath("clips.sqlite3"), identity)); QVERIFY(clips.initialize());
            ObsidianClipStore store; PersistentClipService persistent(clips, store);
            QString openedResourceId, openedAnchorId;
            QJsonObject openedLocator;
            CommandSessionCallbacks callbacks;
            callbacks.writeBlockReason = [] { return QString(); };
            callbacks.open = [&](const QString &resource, const QString &anchor) {
                openedResourceId = resource; openedAnchorId = anchor;
                const auto target = library.findResource(resource);
                if (target) for (const auto &item : target->anchors)
                    if (item.id == anchor) openedLocator = QJsonDocument::fromJson(item.locatorJson.toUtf8()).object();
                return QJsonObject{{"state", "open_requested"}, {"positionVerified", false}};
            };
            CommandService service(library, clips, persistent, temp.path(), callbacks);
            const auto invoke = [&](const QString &method, const QJsonObject &p) {
                return service.execute({{"protocol", "pinloom-cli/v1"}, {"library", temp.path()},
                    {"baseDirectory", temp.path()}, {"method", method}, {"params", p}});
            };
            const auto captured = invoke("capture.pdf", params);
            QVERIFY2(captured["ok"].toBool(), QJsonDocument(captured).toJson().constData());
            const auto saved = captured["result"].toObject();
            resourceId = saved["resourceId"].toString(); anchorId = saved["id"].toString();
            expectedLocator = saved["locator"].toObject();
            QCOMPARE(expectedLocator["text"], params["locator"].toObject()["text"]);
            QCOMPARE(expectedLocator["selection"], params["locator"].toObject()["selection"]);
            const QJsonObject key{{"resourceId", resourceId}, {"id", anchorId}};
            auto update = key;
            update["ifVersion"] = saved["version"]; update["name"] = renamed;
            update["aliases"] = QJsonArray{"renamed passage"}; update["tags"] = tags;
            const auto edited = invoke("anchor.update", update);
            QVERIFY2(edited["ok"].toBool(), QJsonDocument(edited).toJson().constData());
            QCOMPARE(edited["result"].toObject()["locator"].toObject(), expectedLocator);
            const auto queried = invoke("anchor.get", key);
            QVERIFY(queried["ok"].toBool());
            const auto current = queried["result"].toObject();
            QCOMPARE(current["name"].toString(), renamed); QCOMPARE(current["tags"].toArray(), tags);
            QCOMPARE(current["locator"].toObject(), expectedLocator);
            QCOMPARE(current["targetFile"], saved["targetFile"]);
            const QJsonObject target{{"resourceId", resourceId}, {"anchorId", anchorId}};
            const auto resolved = invoke("entry.resolve", target);
            QVERIFY(resolved["ok"].toBool());
            QVERIFY(resolved["result"].toObject()["validation"].toObject()["valid"].toBool());
            const auto opened = invoke("entry.open", target);
            QVERIFY(opened["ok"].toBool());
            QTRY_COMPARE(openedAnchorId, anchorId);
            QCOMPARE(openedResourceId, resourceId); QCOMPARE(openedLocator, expectedLocator);
            const auto operation = invoke("operation.get", {{"id", opened["result"].toObject()["id"]}});
            QCOMPARE(operation["result"].toObject()["state"].toString(), "open_requested");
            QVERIFY(!operation["result"].toObject()["positionVerified"].toBool());
        }
        SqliteLibraryRepository reopened;
        QVERIFY(reopened.open(temp.filePath("pinloom.sqlite3"), temp.filePath("identity.sqlite3"))); QVERIFY(reopened.initialize());
        const auto restored = reopened.findResource(resourceId);
        QVERIFY(restored.has_value()); QCOMPARE(restored->anchors.size(), 1);
        const auto &anchor = restored->anchors.first();
        QCOMPARE(anchor.id, anchorId); QCOMPARE(anchor.name, renamed);
        QCOMPARE(anchor.aliases, QStringList{"renamed passage"});
        QCOMPARE(anchor.tags, QStringList({QString::fromUtf8("跨页"), "reviewed"}));
        QCOMPARE(QJsonDocument::fromJson(anchor.locatorJson.toUtf8()).object(), expectedLocator);
    }
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
    void durableIdempotencyConflictAndWriteGuard_data()
    {
        QTest::addColumn<bool>("textAnchor");
        QTest::newRow("rectangle") << false;
        QTest::newRow("native-text") << true;
    }
    void durableIdempotencyConflictAndWriteGuard()
    {
        QFETCH(bool, textAnchor);
        QTemporaryDir temp; const auto path = temp.filePath("fixture.pdf"); QVERIFY(fixture(path));
        auto params = textAnchor ? textCapture(path) : capture(path);
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
