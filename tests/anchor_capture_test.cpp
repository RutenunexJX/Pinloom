#include "pinloom/core/AnchorCapture.h"
#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/core/ManualPdfAnchorCreation.h"
#include "pinloom/core/PdfXChangeCommand.h"
#include "pinloom/core/SqliteLibraryRepository.h"

#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <optional>

using namespace Pinloom;

class AnchorCaptureTest : public QObject {
    Q_OBJECT

private slots:
    void buildsManualPdfXChangeRectAnchor();
    void reportsMissingPdfXChangeCaptureInputs();
    void keepsPdfXChangeLocatorJsonStable();
    void buildsAnchorCompatibleWithPdfXChangeExecutor();
    void savesManualPdfRectAnchorInRepository();
    void searchesCreatedManualPdfRectAnchorByNameAliasAndTag();
    void createsManualPdfRectAnchorCompatibleWithPdfXChangeExecutor();
    void rejectsInvalidManualPdfRectAnchorInputsWithoutSaving();
    void reportsManualPdfRectAnchorRepositorySaveFailure();
    void readsCreatedManualPdfRectAnchorAfterSqliteReopen();
};

class RejectingRepository final : public ILibraryRepository {
public:
    bool upsertResource(const Resource &resource) override
    {
        lastResource = resource;
        ++upsertCount;
        return false;
    }

    std::optional<Resource> findResource(const QString &) const override { return std::nullopt; }
    QList<SearchResult> search(const SearchQuery &) const override { return {}; }
    bool clearResources() override { return true; }
    bool upsertResourceRelation(const ResourceRelation &) override { return false; }
    QList<ResourceRelation> resourceRelations(const QString &) const override { return {}; }
    QList<ResourceRelation> allResourceRelations() const override { return {}; }
    bool removeResourceRelation(const QString &, const QString &, const QString &) override { return false; }
    bool recordResourceOpen(const QString &) override { return false; }
    bool setResourcePinned(const QString &, bool) override { return false; }
    std::optional<ResourceUsage> resourceUsage(const QString &) const override { return std::nullopt; }
    bool recordAnchorOpen(const QString &, const Anchor &) override { return false; }
    std::optional<AnchorUsage> anchorUsage(const QString &, const Anchor &) const override { return std::nullopt; }
    bool upsertLibraryRoot(const LibraryRoot &) override { return false; }
    QList<LibraryRoot> libraryRoots() const override { return {}; }
    std::optional<LibraryRoot> findLibraryRoot(const QString &) const override { return std::nullopt; }
    bool removeLibraryRoot(const QString &) override { return false; }
    bool setLibraryRootEnabled(const QString &, bool) override { return false; }
    bool setLibraryRootPinned(const QString &, bool) override { return false; }
    bool updateLibraryRootLastIndexedAt(const QString &, const QDateTime &) override { return false; }

    int upsertCount = 0;
    Resource lastResource;
};

static PdfXChangeCaptureRequest validRectRequest()
{
    PdfXChangeCaptureRequest request;
    request.anchorName = QStringLiteral("Clock domain window");
    request.targetFile = QStringLiteral("E:/docs/clock.pdf");
    request.page = 12;
    request.rect = {420.0, 860.0, 780.0, 920.0};
    request.zoom = 250.0;
    return request;
}

static ManualPdfAnchorCreationRequest validCreationRequest()
{
    ManualPdfAnchorCreationRequest request;
    request.name = QStringLiteral("Clock domain window");
    request.file = QStringLiteral("E:/docs/clock.pdf");
    request.page = 12;
    request.rect = {420.0, 860.0, 780.0, 920.0};
    request.zoom = 250.0;
    request.aliases = {QStringLiteral("cdc zoom"), QStringLiteral("CDC Zoom")};
    request.tags = {QStringLiteral("#reviewpoint"), QStringLiteral("reviewpoint")};
    request.pinned = true;
    return request;
}

static bool hasAnchorSearchResult(const QList<SearchResult> &results,
                                  const QString &field,
                                  const QString &anchorName)
{
    return std::any_of(results.cbegin(), results.cend(), [&](const SearchResult &result) {
        return result.matchedAnchor.has_value()
            && result.matchedField == field
            && result.matchedAnchor->name == anchorName;
    });
}

void AnchorCaptureTest::buildsManualPdfXChangeRectAnchor()
{
    const AnchorCaptureResult result = captureManualPdfXChangeRectAnchor(validRectRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.targetApp, QStringLiteral("PDF-XChange"));
    QCOMPARE(result.targetFile, QStringLiteral("E:/docs/clock.pdf"));
    QCOMPARE(result.locatorType, QStringLiteral("pdfxchange.rect"));
    QCOMPARE(result.page, 12);
    QCOMPARE(result.rect.left, 420.0);
    QCOMPARE(result.rect.top, 860.0);
    QCOMPARE(result.rect.right, 780.0);
    QCOMPARE(result.rect.bottom, 920.0);
    QCOMPARE(result.zoom, 250.0);
    QCOMPARE(result.unit, QStringLiteral("pt"));
    QCOMPARE(result.source, QStringLiteral("manual"));

    QCOMPARE(result.anchor.type, AnchorType::PdfRegion);
    QCOMPARE(result.anchor.name, QStringLiteral("Clock domain window"));
    QCOMPARE(result.anchor.target, QStringLiteral("Clock domain window"));
    QCOMPARE(result.anchor.targetApp, QStringLiteral("PDF-XChange"));
    QCOMPARE(result.anchor.targetFile, QStringLiteral("E:/docs/clock.pdf"));
    QCOMPARE(result.anchor.locatorType, QStringLiteral("pdfxchange.rect"));
    QCOMPARE(result.anchor.page, 12);
    QCOMPARE(result.anchor.region.x(), 420.0);
    QCOMPARE(result.anchor.region.y(), 860.0);
    QCOMPARE(result.anchor.region.width(), 360.0);
    QCOMPARE(result.anchor.region.height(), 60.0);
}

void AnchorCaptureTest::reportsMissingPdfXChangeCaptureInputs()
{
    PdfXChangeCaptureRequest missingFile = validRectRequest();
    missingFile.targetFile.clear();
    AnchorCaptureResult result = captureManualPdfXChangeRectAnchor(missingFile);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("PDF-XChange capture target file is missing"));

    PdfXChangeCaptureRequest missingPage = validRectRequest();
    missingPage.page = -1;
    result = captureManualPdfXChangeRectAnchor(missingPage);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("PDF-XChange capture page is missing"));

    PdfXChangeCaptureRequest missingRect = validRectRequest();
    missingRect.rect = {};
    result = captureManualPdfXChangeRectAnchor(missingRect);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("PDF-XChange capture rectangle is missing"));
}

void AnchorCaptureTest::keepsPdfXChangeLocatorJsonStable()
{
    const AnchorCaptureResult result = captureManualPdfXChangeRectAnchor(validRectRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.anchor.locatorJson,
             QStringLiteral("{\"page\":12,\"rect\":[420,860,780,920],\"source\":\"manual\",\"type\":\"pdfxchange.rect\",\"unit\":\"pt\",\"zoom\":250}"));
}

void AnchorCaptureTest::buildsAnchorCompatibleWithPdfXChangeExecutor()
{
    const AnchorCaptureResult capture = captureManualPdfXChangeRectAnchor(validRectRequest());

    QVERIFY2(capture.success(), qPrintable(capture.error));
    QVERIFY(isPdfXChangeAnchor(capture.anchor));

    const PdfXChangeCommandResult command =
        buildPdfXChangeCommand(capture.anchor, QString(), QStringLiteral("C:/Tools/PDFXEdit.exe"));

    QVERIFY2(command.success(), qPrintable(command.error));
    QCOMPARE(command.command.filePath, QStringLiteral("E:/docs/clock.pdf"));
    QCOMPARE(command.command.action, QStringLiteral("page=12;zoom=250;highlight=420,860,780,920;usept=yes"));
}

void AnchorCaptureTest::savesManualPdfRectAnchorInRepository()
{
    InMemoryLibraryRepository repository;
    ManualPdfAnchorCreationService service(repository);

    const ManualPdfAnchorCreationResult result =
        service.createManualPdfXChangeRectAnchor(validCreationRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QVERIFY(!result.resource.id.isEmpty());
    QCOMPARE(result.resource.kind, ResourceKind::Pdf);
    QCOMPARE(result.resource.title, QStringLiteral("clock.pdf"));
    QCOMPARE(result.resource.location, QStringLiteral("E:/docs/clock.pdf"));
    QCOMPARE(result.anchor.name, QStringLiteral("Clock domain window"));
    QCOMPARE(result.anchor.targetFile, QStringLiteral("E:/docs/clock.pdf"));
    QCOMPARE(result.anchor.locatorType, QStringLiteral("pdfxchange.rect"));
    QCOMPARE(result.anchor.aliases, QStringList{QStringLiteral("cdc zoom")});
    QCOMPARE(result.anchor.tags, QStringList{QStringLiteral("reviewpoint")});
    QVERIFY(result.anchor.pinned);
    QVERIFY(result.anchor.createdAt.isValid());
    QVERIFY(result.anchor.updatedAt.isValid());

    const std::optional<Resource> stored = repository.findResource(result.resource.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->anchors.size(), 1);
    QCOMPARE(stored->anchors.first().id, result.anchor.id);
    QCOMPARE(stored->anchors.first().locatorJson, result.anchor.locatorJson);
}

void AnchorCaptureTest::searchesCreatedManualPdfRectAnchorByNameAliasAndTag()
{
    InMemoryLibraryRepository repository;
    ManualPdfAnchorCreationService service(repository);
    const ManualPdfAnchorCreationResult result =
        service.createManualPdfXChangeRectAnchor(validCreationRequest());
    QVERIFY2(result.success(), qPrintable(result.error));

    const QList<SearchResult> nameResults =
        repository.search(SearchQuery{QStringLiteral("Clock domain window")});
    QVERIFY(hasAnchorSearchResult(nameResults,
                                  QStringLiteral("anchor_name"),
                                  QStringLiteral("Clock domain window")));

    const QList<SearchResult> aliasResults =
        repository.search(SearchQuery{QStringLiteral("cdc zoom")});
    QVERIFY(hasAnchorSearchResult(aliasResults,
                                  QStringLiteral("anchor_alias"),
                                  QStringLiteral("Clock domain window")));

    const QList<SearchResult> tagResults =
        repository.search(SearchQuery{QStringLiteral("reviewpoint")});
    QVERIFY(hasAnchorSearchResult(tagResults,
                                  QStringLiteral("anchor_tag"),
                                  QStringLiteral("Clock domain window")));
}

void AnchorCaptureTest::createsManualPdfRectAnchorCompatibleWithPdfXChangeExecutor()
{
    InMemoryLibraryRepository repository;
    ManualPdfAnchorCreationService service(repository);
    const ManualPdfAnchorCreationResult result =
        service.createManualPdfXChangeRectAnchor(validCreationRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.anchor.locatorJson,
             QStringLiteral("{\"page\":12,\"rect\":[420,860,780,920],\"source\":\"manual\",\"type\":\"pdfxchange.rect\",\"unit\":\"pt\",\"zoom\":250}"));
    QVERIFY(isPdfXChangeAnchor(result.anchor));

    const PdfXChangeCommandResult command =
        buildPdfXChangeCommand(result.anchor, QString(), QStringLiteral("C:/Tools/PDFXEdit.exe"));

    QVERIFY2(command.success(), qPrintable(command.error));
    QCOMPARE(command.command.filePath, QStringLiteral("E:/docs/clock.pdf"));
    QCOMPARE(command.command.action, QStringLiteral("page=12;zoom=250;highlight=420,860,780,920;usept=yes"));
}

void AnchorCaptureTest::rejectsInvalidManualPdfRectAnchorInputsWithoutSaving()
{
    InMemoryLibraryRepository repository;
    ManualPdfAnchorCreationService service(repository);

    ManualPdfAnchorCreationRequest request = validCreationRequest();
    request.name = QStringLiteral(" ");
    ManualPdfAnchorCreationResult result = service.createManualPdfXChangeRectAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Manual PDF anchor name is missing"));

    request = validCreationRequest();
    request.file.clear();
    result = service.createManualPdfXChangeRectAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Manual PDF anchor file is missing"));

    request = validCreationRequest();
    request.page = 0;
    result = service.createManualPdfXChangeRectAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("PDF-XChange capture page is missing"));

    request = validCreationRequest();
    request.rect.right = request.rect.left;
    result = service.createManualPdfXChangeRectAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("PDF-XChange capture rectangle is missing"));

    QVERIFY(repository.search(SearchQuery{}).isEmpty());
}

void AnchorCaptureTest::reportsManualPdfRectAnchorRepositorySaveFailure()
{
    RejectingRepository repository;
    ManualPdfAnchorCreationService service(repository);

    const ManualPdfAnchorCreationResult result =
        service.createManualPdfXChangeRectAnchor(validCreationRequest());

    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Unable to save manual PDF anchor"));
    QCOMPARE(repository.upsertCount, 1);
    QCOMPARE(repository.lastResource.location, QStringLiteral("E:/docs/clock.pdf"));
}

void AnchorCaptureTest::readsCreatedManualPdfRectAnchorAfterSqliteReopen()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString databasePath = dir.filePath(QStringLiteral("pinloom.sqlite3"));
    QString resourceId;
    Anchor createdAnchor;
    {
        SqliteLibraryRepository repository;
        QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
        QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

        ManualPdfAnchorCreationService service(repository);
        const ManualPdfAnchorCreationResult result =
            service.createManualPdfXChangeRectAnchor(validCreationRequest());
        QVERIFY2(result.success(), qPrintable(result.error));
        resourceId = result.resource.id;
        createdAnchor = result.anchor;
    }

    {
        SqliteLibraryRepository repository;
        QVERIFY2(repository.open(databasePath), qPrintable(repository.lastError()));
        QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

        const std::optional<Resource> stored = repository.findResource(resourceId);
        QVERIFY(stored.has_value());
        QCOMPARE(stored->anchors.size(), 1);
        const Anchor storedAnchor = stored->anchors.first();
        QCOMPARE(storedAnchor.id, createdAnchor.id);
        QCOMPARE(storedAnchor.name, createdAnchor.name);
        QCOMPARE(storedAnchor.targetFile, createdAnchor.targetFile);
        QCOMPARE(storedAnchor.locatorType, createdAnchor.locatorType);
        QCOMPARE(storedAnchor.locatorJson, createdAnchor.locatorJson);
        QCOMPARE(storedAnchor.aliases, createdAnchor.aliases);
        QCOMPARE(storedAnchor.tags, createdAnchor.tags);
        QVERIFY(storedAnchor.pinned);

        const QList<SearchResult> aliasResults =
            repository.search(SearchQuery{QStringLiteral("cdc zoom")});
        QVERIFY(hasAnchorSearchResult(aliasResults,
                                      QStringLiteral("anchor_alias"),
                                      QStringLiteral("Clock domain window")));
    }
}

QTEST_MAIN(AnchorCaptureTest)

#include "anchor_capture_test.moc"
