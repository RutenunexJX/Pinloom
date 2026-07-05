#include "pinloom/core/AnchorArchive.h"
#include "pinloom/core/InMemoryLibraryRepository.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRectF>
#include <QTemporaryDir>
#include <QTest>
#include <optional>

using namespace Pinloom;

class AnchorArchiveTest : public QObject {
    Q_OBJECT

private slots:
    void exportsOnlyResourcesWithAnchorsAndAnchorFields();
    void importsExportedAnchorsIntoEmptyRepository();
    void skipsConflictingResourceIdsWithoutOverwritingLocal();
    void rejectsInvalidJsonAndUnsupportedSchemaOrVersion();
};

namespace {

Anchor makeArchiveAnchor()
{
    Anchor anchor;
    anchor.type = AnchorType::PdfRegion;
    anchor.id = QStringLiteral("anchor:clock-window");
    anchor.name = QStringLiteral("Clock domain window");
    anchor.target = QStringLiteral("Legacy Clock Target");
    anchor.targetApp = QStringLiteral("PDF-XChange");
    anchor.targetFile = QStringLiteral("E:/specs/clocking.pdf");
    anchor.targetUri = QStringLiteral("pinloom://clock-window");
    anchor.locatorType = QStringLiteral("pdfxchange.rect");
    anchor.locatorJson = QStringLiteral("{\"page\":12,\"rect\":[420,860,780,920],\"zoom\":250}");
    anchor.aliases = {QStringLiteral("cdc zoom")};
    anchor.tags = {QStringLiteral("handoff")};
    anchor.pinned = true;
    anchor.createdAt = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    anchor.updatedAt = QDateTime::fromString(QStringLiteral("2026-01-02T00:00:00Z"), Qt::ISODate);
    anchor.usedAt = QDateTime::fromString(QStringLiteral("2026-01-03T00:00:00Z"), Qt::ISODate);
    anchor.line = 42;
    anchor.page = 12;
    anchor.region = QRectF(420.0, 860.0, 360.0, 60.0);
    return anchor;
}

Resource makeAnchoredResource()
{
    Resource resource;
    resource.id = QStringLiteral("clocking-pdf");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Clocking PDF");
    resource.location = QStringLiteral("E:/specs/clocking.pdf");
    resource.aliases = {QStringLiteral("resource alias not archived")};
    resource.tags = {QStringLiteral("resource-tag-not-archived")};
    resource.content = QStringLiteral("content must not be archived");
    resource.updatedAt = QDateTime::fromString(QStringLiteral("2026-01-04T00:00:00Z"), Qt::ISODate);
    resource.anchors = {makeArchiveAnchor()};
    return resource;
}

bool writeTestFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    return file.write(content) == content.size();
}

} // namespace

void AnchorArchiveTest::exportsOnlyResourcesWithAnchorsAndAnchorFields()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Resource anchored = makeAnchoredResource();

    Resource plain;
    plain.id = QStringLiteral("plain-resource");
    plain.kind = ResourceKind::File;
    plain.title = QStringLiteral("Plain Resource");
    plain.location = QStringLiteral("E:/notes/plain.md");
    plain.content = QStringLiteral("plain content must not be archived");

    const QString archivePath = dir.filePath(QStringLiteral("anchors.json"));
    const AnchorArchiveExportResult result =
        exportAnchorsToJsonFile({anchored, plain}, archivePath);
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.exportedResources, 1);
    QCOMPARE(result.exportedAnchors, 1);

    QFile archiveFile(archivePath);
    QVERIFY(archiveFile.open(QIODevice::ReadOnly));
    const QJsonDocument document = QJsonDocument::fromJson(archiveFile.readAll());
    QVERIFY(document.isObject());

    const QJsonObject root = document.object();
    QCOMPARE(root.value(QStringLiteral("schema")).toString(), QStringLiteral("pinloom.anchors"));
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);

    const QJsonArray resources = root.value(QStringLiteral("resources")).toArray();
    QCOMPARE(resources.size(), 1);
    const QJsonObject resource = resources.first().toObject();
    QCOMPARE(resource.value(QStringLiteral("id")).toString(), anchored.id);
    QCOMPARE(resource.value(QStringLiteral("kind")).toString(), QStringLiteral("pdf"));
    QCOMPARE(resource.value(QStringLiteral("title")).toString(), anchored.title);
    QCOMPARE(resource.value(QStringLiteral("location")).toString(), anchored.location);
    QVERIFY(!resource.contains(QStringLiteral("content")));
    QVERIFY(!resource.contains(QStringLiteral("aliases")));
    QVERIFY(!resource.contains(QStringLiteral("tags")));
    QVERIFY(!resource.contains(QStringLiteral("relations")));

    const QJsonArray anchors = resource.value(QStringLiteral("anchors")).toArray();
    QCOMPARE(anchors.size(), 1);
    const QJsonObject anchor = anchors.first().toObject();
    QCOMPARE(anchor.value(QStringLiteral("id")).toString(), anchored.anchors.first().id);
    QCOMPARE(anchor.value(QStringLiteral("name")).toString(), anchored.anchors.first().name);
    QCOMPARE(anchor.value(QStringLiteral("target_app")).toString(), anchored.anchors.first().targetApp);
    QCOMPARE(anchor.value(QStringLiteral("target_file")).toString(), anchored.anchors.first().targetFile);
    QCOMPARE(anchor.value(QStringLiteral("target_uri")).toString(), anchored.anchors.first().targetUri);
    QCOMPARE(anchor.value(QStringLiteral("locator_type")).toString(), anchored.anchors.first().locatorType);
    QCOMPARE(anchor.value(QStringLiteral("locator_json")).toString(), anchored.anchors.first().locatorJson);
    QCOMPARE(anchor.value(QStringLiteral("aliases")).toArray().first().toString(), QStringLiteral("cdc zoom"));
    QCOMPARE(anchor.value(QStringLiteral("tags")).toArray().first().toString(), QStringLiteral("handoff"));
    QCOMPARE(anchor.value(QStringLiteral("pinned")).toBool(), true);
    QCOMPARE(anchor.value(QStringLiteral("type")).toString(), QStringLiteral("pdf_region"));
    QCOMPARE(anchor.value(QStringLiteral("target")).toString(), anchored.anchors.first().target);
    QCOMPARE(anchor.value(QStringLiteral("line")).toInt(), 42);
    QCOMPARE(anchor.value(QStringLiteral("page")).toInt(), 12);
    QCOMPARE(anchor.value(QStringLiteral("region")).toObject().value(QStringLiteral("x")).toDouble(), 420.0);
}

void AnchorArchiveTest::importsExportedAnchorsIntoEmptyRepository()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString archivePath = dir.filePath(QStringLiteral("anchors.json"));
    const AnchorArchiveExportResult exportResult =
        exportAnchorsToJsonFile({makeAnchoredResource()}, archivePath);
    QVERIFY2(exportResult.success(), qPrintable(exportResult.error));

    InMemoryLibraryRepository repository;
    const AnchorArchiveImportResult importResult =
        importAnchorsFromJsonFile(repository, archivePath);
    QVERIFY2(importResult.success(), qPrintable(importResult.error));
    QCOMPARE(importResult.importedResources, 1);
    QCOMPARE(importResult.importedAnchors, 1);
    QCOMPARE(importResult.skippedResources, 0);
    QCOMPARE(importResult.skippedAnchors, 0);

    const QList<SearchResult> nameResults =
        repository.search(SearchQuery{QStringLiteral("Clock domain window")});
    QCOMPARE(nameResults.size(), 1);
    QCOMPARE(nameResults.first().matchedField, QStringLiteral("anchor_name"));
    QVERIFY(nameResults.first().matchedAnchor.has_value());

    const QList<SearchResult> aliasResults =
        repository.search(SearchQuery{QStringLiteral("cdc zoom")});
    QCOMPARE(aliasResults.size(), 1);
    QCOMPARE(aliasResults.first().matchedField, QStringLiteral("anchor_alias"));

    const QList<SearchResult> tagResults =
        repository.search(SearchQuery{QStringLiteral("#handoff")});
    QCOMPARE(tagResults.size(), 1);
    QCOMPARE(tagResults.first().matchedField, QStringLiteral("anchor_tag"));
}

void AnchorArchiveTest::skipsConflictingResourceIdsWithoutOverwritingLocal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Resource archived = makeAnchoredResource();
    archived.id = QStringLiteral("shared-resource");
    archived.title = QStringLiteral("Archived Title");
    archived.anchors.first().name = QStringLiteral("Archived Anchor");

    const QString archivePath = dir.filePath(QStringLiteral("anchors.json"));
    const AnchorArchiveExportResult exportResult =
        exportAnchorsToJsonFile({archived}, archivePath);
    QVERIFY2(exportResult.success(), qPrintable(exportResult.error));

    Resource local;
    local.id = archived.id;
    local.kind = ResourceKind::File;
    local.title = QStringLiteral("Local Title");
    local.location = QStringLiteral("E:/local/local.md");
    Anchor localAnchor;
    localAnchor.type = AnchorType::Manual;
    localAnchor.name = QStringLiteral("Local Anchor");
    local.anchors = {localAnchor};

    InMemoryLibraryRepository repository;
    QVERIFY(repository.upsertResource(local));

    const AnchorArchiveImportResult importResult =
        importAnchorsFromJsonFile(repository, archivePath);
    QVERIFY2(importResult.success(), qPrintable(importResult.error));
    QCOMPARE(importResult.importedResources, 0);
    QCOMPARE(importResult.importedAnchors, 0);
    QCOMPARE(importResult.skippedResources, 1);
    QCOMPARE(importResult.skippedAnchors, 1);

    const std::optional<Resource> stored = repository.findResource(local.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->title, local.title);
    QCOMPARE(stored->location, local.location);
    QCOMPARE(stored->anchors.size(), 1);
    QCOMPARE(stored->anchors.first().name, localAnchor.name);
    QVERIFY(repository.search(SearchQuery{QStringLiteral("Archived Anchor")}).isEmpty());
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("Local Anchor")}).size(), 1);
}

void AnchorArchiveTest::rejectsInvalidJsonAndUnsupportedSchemaOrVersion()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    InMemoryLibraryRepository repository;

    const QString invalidPath = dir.filePath(QStringLiteral("invalid.json"));
    QVERIFY(writeTestFile(invalidPath, QByteArrayLiteral("{")));
    const AnchorArchiveImportResult invalidResult =
        importAnchorsFromJsonFile(repository, invalidPath);
    QVERIFY(!invalidResult.success());
    QVERIFY(invalidResult.error.contains(QStringLiteral("Invalid anchor archive JSON")));

    const QString schemaPath = dir.filePath(QStringLiteral("bad-schema.json"));
    QVERIFY(writeTestFile(schemaPath,
                          QByteArrayLiteral("{\"schema\":\"pinloom.resources\",\"version\":1,\"resources\":[]}")));
    const AnchorArchiveImportResult schemaResult =
        importAnchorsFromJsonFile(repository, schemaPath);
    QVERIFY(!schemaResult.success());
    QCOMPARE(schemaResult.error, QStringLiteral("Unsupported anchor archive schema"));

    const QString versionPath = dir.filePath(QStringLiteral("bad-version.json"));
    QVERIFY(writeTestFile(versionPath,
                          QByteArrayLiteral("{\"schema\":\"pinloom.anchors\",\"version\":2,\"resources\":[]}")));
    const AnchorArchiveImportResult versionResult =
        importAnchorsFromJsonFile(repository, versionPath);
    QVERIFY(!versionResult.success());
    QCOMPARE(versionResult.error, QStringLiteral("Unsupported anchor archive version"));
}

QTEST_MAIN(AnchorArchiveTest)

#include "anchor_archive_test.moc"
