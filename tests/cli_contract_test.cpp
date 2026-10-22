#include "pinloom/clip/ClipRepository.h"
#include "pinloom/core/SqliteLibraryRepository.h"
#include "pinloom/application/CommandService.h"
#include "pinloom/application/CommandTransport.h"
#include "pinloom/clip/ObsidianClipStore.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace Pinloom;
class CliContractTest : public QObject {
    Q_OBJECT
    QJsonObject call(const QString &library, const QString &method, const QJsonObject &params = {}, int expected = 0)
    {
        QProcess process;
        process.setWorkingDirectory(QFileInfo(library).absolutePath());
        process.start(QDir(QCoreApplication::applicationDirPath()).filePath("pinloom_cli.exe"),
                      {method, "--library", library, "--input", "-"});
        if (!process.waitForStarted(5000)) { QTest::qFail("CLI failed to start", __FILE__, __LINE__); return {}; }
        process.write(QJsonDocument(params).toJson(QJsonDocument::Compact));
        process.closeWriteChannel();
        if (!process.waitForFinished(15000)) { QTest::qFail("CLI timed out", __FILE__, __LINE__); return {}; }
        const auto stdoutBytes = process.readAllStandardOutput();
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(stdoutBytes, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            QTest::qFail(qPrintable(QString::fromUtf8(stdoutBytes + process.readAllStandardError())), __FILE__, __LINE__); return {};
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != expected
            || doc.object()["exitCode"].toInt() != expected)
            QTest::qFail(qPrintable(QString::fromUtf8(stdoutBytes)), __FILE__, __LINE__);
        return doc.object()["result"].toObject();
    }
private slots:
    void discoversNativeCaptureAndResolvesExplicitLibrary()
    {
        QTemporaryDir temp;
        const auto path = temp.filePath("selected-library");
        const auto resolved = call(path, "session.resolve");
        QCOMPARE(resolved["captureProtocol"].toString(), "pinloom-pdf-capture/v1");
        QCOMPARE(resolved["library"].toString(), canonicalLibraryPath(path));
        QVERIFY(!QFileInfo::exists(path));
        const auto capabilities = call(path, "capabilities");
        bool found = false;
        for (const auto &value : capabilities["methods"].toArray()) {
            const auto method = value.toObject();
            if (method["method"] == "capture.pdf") {
                QVERIFY(method["writes"].toBool()); QVERIFY(method["requiresGui"].toBool()); found = true;
            }
        }
        QVERIFY(found);
        call(path, "library.init");
        call(path, "capture.status", {{"captureId", "00000000-0000-0000-0000-000000000001"}}, 3);
    }
    void configuredObsidianArchiveUsesSharedPersistence()
    {
        QTemporaryDir temp;
        const auto library = temp.filePath("library");
        const auto vault = temp.filePath("synthetic-vault");
        QVERIFY(QDir().mkpath(vault));
        call(library, "library.init");
        {
            QSettings settings(QDir(library).filePath("profile.ini"), QSettings::IniFormat);
            settings.setValue("obsidian/vaultPath", vault); settings.sync();
        }
        auto clip = call(library, "clip.create", {{"name", "Archived fixture"}, {"text", "Only synthetic archive text"}, {"ifVersion", "absent"}});
        QCOMPARE(clip["storage"].toString(), "obsidian");
        QVERIFY(!QDir(QDir(vault).filePath("Pinloom Clips")).entryList({"*.md"}, QDir::Files).isEmpty());
        const auto deleted = call(library, "clip.delete", {{"id", clip["id"]}, {"ifVersion", clip["version"]}});
        QCOMPARE(deleted["state"].toString(), "trash");
        const auto restored = call(library, "clip.restore", {{"id", clip["id"]}, {"ifVersion", deleted["version"]}});
        QCOMPARE(restored["state"].toString(), "saved");
        QCOMPARE(restored["storage"].toString(), "obsidian");
    }
    void refusesOfflineAccessWhileAnotherProcessOwnsLibrary()
    {
        QTemporaryDir temp;
        call(temp.path(), "library.init");
        {
            LibrarySessionLock lock(temp.path());
            QVERIFY(lock.tryLock());
            call(temp.path(), "clip.create", {{"name", "Must not be written"}, {"text", "fixture"}, {"ifVersion", "absent"}}, 5);
        }
        QCOMPARE(call(temp.path(), "search")["items"].toArray().size(), 0);
    }
    void exportDoesNotRefreshDirtyViews()
    {
        QTemporaryDir temp;
        SqliteLibraryRepository library;
        SqliteClipRepository clips;
        const auto identity = temp.filePath("identity.sqlite3");
        QVERIFY(library.open(temp.filePath("pinloom.sqlite3"), identity)); QVERIFY(library.initialize());
        QVERIFY(clips.open(temp.filePath("pinloom_clip.sqlite3"), identity)); QVERIFY(clips.initialize());
        ObsidianClipStore store;
        PersistentClipService persistent(clips, store);
        int refreshes = 0;
        CommandSessionCallbacks callbacks;
        callbacks.writeBlockReason = [] { return QStringLiteral("Pending user edit"); };
        callbacks.refreshViews = [&] { ++refreshes; };
        CommandService service(library, clips, persistent, temp.path(), callbacks);
        QJsonObject request{{"protocol", "pinloom-cli/v1"}, {"library", temp.path()}, {"baseDirectory", temp.path()},
                            {"method", "library.export"}, {"params", QJsonObject{{"path", "export.json"}}}};
        const auto response = service.execute(request);
        QVERIFY(response["ok"].toBool()); QVERIFY(response["persisted"].toBool());
        QVERIFY(!response["guiAdopted"].toBool()); QCOMPARE(refreshes, 0);
    }
    void existingLocatorValidation()
    {
        QTemporaryDir temp;
        const auto library = temp.filePath("library");
        call(library, "library.init");
        QFile file(temp.filePath("synthetic document.bin"));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("Synthetic locator validation fixture"); file.close();
        const QList<QJsonObject> locators{
            {{"type", "pdf.page"}, {"page", 1}},
            {{"type", "sumatrapdf.rect"}, {"page", 1}, {"rect", QJsonArray{10, 10, 80, 80}}},
            {{"type", "excel.range"}, {"sheet", "Sheet1"}, {"range", "B2"}},
            {{"type", "excel.name"}, {"name", "SampleRange"}},
            {{"type", "word.bookmark"}, {"bookmark", "SampleBookmark"}},
            {{"type", "powerpoint.shape"}, {"slide", 1}, {"shapeId", 1}},
            {{"type", "visio.shape"}, {"page", "Page-1"}, {"shapeUniqueId", "{00000000-0000-0000-0000-000000000001}"}}
        };
        QString parentId, parentVersion = "absent";
        for (int index = 0; index < locators.size(); ++index) {
            QJsonObject create{{"name", QString("locator %1").arg(index)}, {"targetFile", file.fileName()},
                               {"locator", locators[index]}, {"ifVersion", parentVersion}};
            if (!parentId.isEmpty()) create.insert("resourceId", parentId);
            const auto anchor = call(library, "anchor.create", create);
            parentId = anchor["resourceId"].toString(); parentVersion = anchor["version"].toString();
            const QJsonObject key{{"resourceId", anchor["resourceId"]}, {"id", anchor["id"]}};
            QVERIFY(call(library, "anchor.validate", key)["valid"].toBool());
            auto invalid = key; invalid.insert("ifVersion", anchor["version"]);
            invalid.insert("locator", QJsonObject{{"type", locators[index]["type"]}});
            call(library, "anchor.update", invalid, 2);
            QCOMPARE(call(library, "anchor.get", key)["version"], anchor["version"]);
        }
    }
    void explicitHistoryPromotionAndPurge()
    {
        QTemporaryDir temp;
        const auto path = temp.path();
        call(path, "library.init");
        QString temporaryId;
        {
            SqliteClipRepository repository;
            QVERIFY(repository.open(QDir(path).filePath("pinloom_clip.sqlite3"), QDir(path).filePath("pinloom_identity.sqlite3")));
            QVERIFY(repository.initialize());
            const auto captured = repository.captureText("Synthetic fixture text, never a clipboard read");
            QVERIFY(captured.captured()); temporaryId = captured.clip->id;
        }
        const auto history = call(path, "history.list", {{"limit", 1}})["items"].toArray();
        QCOMPARE(history.size(), 1);
        const auto item = history[0].toObject();
        auto promoted = call(path, "history.promote", {{"id", temporaryId}, {"ifVersion", item["version"]}, {"name", "Retained fixture"}});
        QCOMPARE(promoted["id"].toString(), temporaryId);
        QCOMPARE(promoted["state"].toString(), "saved");
        QCOMPARE(call(path, "history.list")["items"].toArray().size(), 0);
        call(path, "clip.purge", {{"id", temporaryId}, {"ifVersion", promoted["version"]}}, 4);
        const auto deleted = call(path, "clip.delete", {{"id", temporaryId}, {"ifVersion", promoted["version"]}});
        call(path, "clip.purge", {{"id", temporaryId}, {"ifVersion", deleted["version"]}, {"dryRun", true}});
        QCOMPARE(call(path, "clip.get", {{"id", temporaryId}})["state"].toString(), "trash");
        call(path, "clip.purge", {{"id", temporaryId}, {"ifVersion", deleted["version"]}});
        call(path, "clip.get", {{"id", temporaryId}}, 3);
    }
    void sharedIdentityAndImportAtomicity()
    {
        QTemporaryDir temp;
        const auto path = temp.path();
        call(path, "library.init");
        const auto clip = call(path, "clip.create", {{"name", "reserved"}, {"text", "body"}, {"ifVersion", "absent"}});
        auto state = call(path, "session.status");
        QJsonObject resource{{"id", "bad-import"}, {"kind", "url"}, {"title", "reserved"},
                             {"location", "https://example.invalid"}, {"explicitlyRetained", true}, {"anchors", QJsonArray{}}};
        QFile archive(QDir(path).filePath("conflict.json"));
        QVERIFY(archive.open(QIODevice::WriteOnly));
        archive.write(QJsonDocument(QJsonObject{{"format", "pinloom-anchor-library"}, {"version", 1},
                       {"resources", QJsonArray{resource}}}).toJson());
        archive.close();
        call(path, "library.import", {{"path", archive.fileName()}, {"mode", "replace"}, {"ifVersion", state["version"]}}, 6);
        QCOMPARE(call(path, "session.status")["version"], state["version"]);
        QCOMPARE(call(path, "clip.get", {{"id", clip["id"]}})["text"].toString(), "body");
        call(path, "resource.get", {{"id", "bad-import"}}, 3);
        call(path, "clip.update", {{"id", clip["id"]}, {"text", "changed"}}, 2);
        call(path, "clip.update", {{"id", clip["id"]}, {"ifVersion", clip["version"]}, {"unexpected", true}}, 2);
        QCOMPARE(call(path, "clip.get", {{"id", clip["id"]}})["version"], clip["version"]);
    }
    void inboxMoveCopyAndRelativePaths()
    {
        QTemporaryDir temp;
        const auto library = QDir(temp.path()).filePath(QString::fromUtf8("资料 库"));
        call(library, "library.init");
        QFile source(QDir(temp.path()).filePath(QString::fromUtf8("输入 空格.txt")));
        QVERIFY(source.open(QIODevice::WriteOnly)); source.write("explicit test bytes"); source.close();
        const auto destination = QDir(temp.path()).filePath("managed");
        const QJsonObject params{{"path", QFileInfo(source.fileName()).fileName()}, {"name", "Moved sample"},
                                {"mode", "move"}, {"destination", "managed"}, {"ifVersion", "absent"}};
        auto previewParams = params; previewParams.insert("dryRun", true);
        const auto preview = call(library, "inbox.add", previewParams);
        QVERIFY(QFileInfo::exists(source.fileName()));
        QVERIFY(!QFileInfo::exists(destination));
        const auto moved = call(library, "inbox.add", params);
        QCOMPARE(moved["id"], preview["resourceId"]);
        QVERIFY(!QFileInfo::exists(source.fileName()));
        QVERIFY(QFileInfo::exists(moved["location"].toString()));
        QFile second(QDir(temp.path()).filePath("second.txt"));
        QVERIFY(second.open(QIODevice::WriteOnly)); second.write("second bytes"); second.close();
        auto bad = params; bad.insert("path", "second.txt");
        call(library, "inbox.add", bad, 6);
        QVERIFY(QFileInfo::exists(second.fileName()));
        QVERIFY(!QFileInfo::exists(QDir(destination).filePath("second.txt")));
        QCOMPARE(call(library, "resource.get", {{"id", moved["id"]}})["version"], moved["version"]);
        const auto copied = call(library, "inbox.add", {{"path", "second.txt"}, {"mode", "copy"},
            {"destination", "managed"}, {"name", "Copied sample"}, {"ifVersion", "absent"}});
        QVERIFY(QFileInfo::exists(second.fileName()));
        QVERIFY(QFileInfo::exists(copied["location"].toString()));
        auto root = call(library, "root.register", {{"path", temp.path()}, {"ifVersion", "absent"},
            {"ignoredDirectoryNames", QJsonArray{"managed"}}});
        QCOMPARE(call(library, "root.query", {{"id", root["id"]}, {"query", "Moved sample"}})["items"].toArray().size(), 0);
        root = call(library, "root.update", {{"id", root["id"]}, {"ifVersion", root["version"]}, {"ignoredDirectoryNames", QJsonArray{}}});
        QCOMPARE(call(library, "root.query", {{"id", root["id"]}, {"query", "Moved sample"}})["items"].toArray().size(), 1);
        call(library, "root.remove", {{"id", root["id"]}, {"ifVersion", root["version"]}});
        QCOMPARE(call(library, "root.list")["items"].toArray().size(), 0);
    }
};
QTEST_GUILESS_MAIN(CliContractTest)
#include "cli_contract_test.moc"
