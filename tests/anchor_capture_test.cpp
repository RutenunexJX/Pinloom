#include "pinloom/core/AnchorCapture.h"
#include "pinloom/core/AnchorCaptureDraft.h"
#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/core/ManualPdfAnchorCreation.h"
#include "pinloom/core/NativeAnchorCapture.h"
#include "pinloom/core/SumatraPdfCommand.h"
#include "pinloom/core/SumatraPdfDdeClient.h"
#include "pinloom/core/SumatraPdfForegroundCapture.h"
#include "pinloom/core/SqliteLibraryRepository.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <optional>

using namespace Pinloom;

class AnchorCaptureTest : public QObject {
    Q_OBJECT

private slots:
    void buildsManualSumatraPdfRectAnchor();
    void buildsManualSumatraPdfPageAnchor();
    void reportsMissingSumatraPdfCaptureInputs();
    void keepsSumatraPdfLocatorJsonStable();
    void buildsAnchorCompatibleWithSumatraPdfExecutor();
    void savesManualPdfRectAnchorInRepository();
    void searchesCreatedManualPdfRectAnchorByNameAliasAndTag();
    void createsManualPdfRectAnchorCompatibleWithSumatraPdfExecutor();
    void rejectsInvalidManualPdfRectAnchorInputsWithoutSaving();
    void reportsManualPdfRectAnchorRepositorySaveFailure();
    void summarizesManualPdfCaptureLocatorForUsers();
    void normalizesSumatraPdfDocumentTitlesForFallback();
    void capturesForegroundSumatraPdfPathFromWindowTitleWithoutIndexedPdf();
    void matchesForegroundSumatraPdfTitleToUniqueIndexedPdf();
    void matchesForegroundSumatraPdfTitleToFileKindPdfLocation();
    void preservesViewStateWhenTitleFallbackBuildsRequest();
    void parsesSumatraPdfDdeFileStateAndMousePosition();
    void buildsSumatraPdfDdeRegionFromMousePositions();
    void prefersDdeDocumentPathOverTitleConfirmation();
    void parsesSumatraPdfViewStateFromStatusText();
    void reportsUnparseableSumatraPdfViewStateText();
    void injectsForegroundSumatraPdfViewStateIntoCaptureRequest();
    void reportsForegroundSumatraPdfTitleWithoutFilePath();
    void rejectsForegroundSumatraPdfTitleWithMultipleIndexedPdfMatches();
    void buildsAndStoresSumatraPdfSearchAnchor();
    void validatesAndCommitsSharedAnchorDraft();
    void capturesAndFinalizesNativeOfficeAnchors();
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
    bool softDeleteResource(const QString &) override { return false; }
    bool restoreResource(const QString &) override { return false; }
    bool softDeleteAnchor(const QString &, const Anchor &) override { return false; }
    bool restoreAnchor(const QString &, const Anchor &) override { return false; }
    bool clearResources() override { return true; }
    bool upsertLibraryRoot(const LibraryRoot &) override { return false; }
    QList<LibraryRoot> libraryRoots() const override { return {}; }
    std::optional<LibraryRoot> findLibraryRoot(const QString &) const override { return std::nullopt; }
    bool removeLibraryRoot(const QString &) override { return false; }
    bool applyBatch(const LibraryBatchMutation &) override { return false; }
    bool recordResourceOpen(const QString &) override { return false; }
    bool setResourcePinned(const QString &, bool) override { return false; }
    std::optional<ResourceUsage> resourceUsage(const QString &) const override { return std::nullopt; }
    bool recordAnchorOpen(const QString &, const Anchor &) override { return false; }
    std::optional<AnchorUsage> anchorUsage(const QString &, const Anchor &) const override { return std::nullopt; }

    int upsertCount = 0;
    Resource lastResource;
};

static PdfCaptureRequest validRectRequest()
{
    PdfCaptureRequest request;
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

void AnchorCaptureTest::buildsManualSumatraPdfRectAnchor()
{
    const AnchorCaptureResult result = captureManualPdfRectAnchor(validRectRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.targetApp, QStringLiteral("SumatraPDF"));
    QCOMPARE(result.targetFile, QStringLiteral("E:/docs/clock.pdf"));
    QCOMPARE(result.locatorType, QStringLiteral("sumatrapdf.rect"));
    QCOMPARE(result.page, 12);
    QCOMPARE(result.rect.left, 420.0);
    QCOMPARE(result.rect.top, 860.0);
    QCOMPARE(result.rect.right, 780.0);
    QCOMPARE(result.rect.bottom, 920.0);
    QCOMPARE(result.zoom, 250.0);
    QCOMPARE(result.unit, QStringLiteral("pt"));
    QCOMPARE(result.source, QStringLiteral("manual"));

    QCOMPARE(result.anchor.name, QStringLiteral("Clock domain window"));
    QCOMPARE(result.anchor.targetApp, QStringLiteral("SumatraPDF"));
    QCOMPARE(result.anchor.targetFile, QStringLiteral("E:/docs/clock.pdf"));
    QCOMPARE(result.anchor.locatorType, QStringLiteral("sumatrapdf.rect"));
    QCOMPARE(anchorLocatorPage(result.anchor), 12);
    const std::optional<QRectF> region = anchorLocatorRegion(result.anchor);
    QVERIFY(region.has_value());
    QCOMPARE(region->x(), 420.0);
    QCOMPARE(region->y(), 860.0);
    QCOMPARE(region->width(), 360.0);
    QCOMPARE(region->height(), 60.0);
}

void AnchorCaptureTest::buildsManualSumatraPdfPageAnchor()
{
    PdfCaptureRequest request;
    request.anchorName = QStringLiteral("Clock domain page");
    request.targetFile = QStringLiteral("E:/docs/clock.pdf");
    request.locatorType = QStringLiteral("sumatrapdf.page");
    request.page = 12;
    request.zoom = 250.0;
    request.source = QStringLiteral("foreground-sumatrapdf-viewstate");

    const AnchorCaptureResult result = captureManualPdfAnchor(request);

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.locatorType, QStringLiteral("sumatrapdf.page"));
    QCOMPARE(result.page, 12);
    QVERIFY(!result.rect.isValid());
    QCOMPARE(result.anchor.locatorType, QStringLiteral("sumatrapdf.page"));
    QVERIFY(!anchorLocatorRegion(result.anchor).has_value());
    QCOMPARE(result.anchor.locatorJson,
             QStringLiteral("{\"page\":12,\"source\":\"foreground-sumatrapdf-viewstate\",\"type\":\"sumatrapdf.page\",\"zoom\":250}"));
}

void AnchorCaptureTest::reportsMissingSumatraPdfCaptureInputs()
{
    PdfCaptureRequest missingFile = validRectRequest();
    missingFile.targetFile.clear();
    AnchorCaptureResult result = captureManualPdfRectAnchor(missingFile);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("SumatraPDF capture target file is missing"));

    PdfCaptureRequest missingPage = validRectRequest();
    missingPage.page = -1;
    result = captureManualPdfRectAnchor(missingPage);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("SumatraPDF capture page is missing"));

    PdfCaptureRequest missingRect = validRectRequest();
    missingRect.rect = {};
    result = captureManualPdfRectAnchor(missingRect);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("SumatraPDF capture rectangle is missing"));
}

void AnchorCaptureTest::keepsSumatraPdfLocatorJsonStable()
{
    const AnchorCaptureResult result = captureManualPdfRectAnchor(validRectRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.anchor.locatorJson,
             QStringLiteral("{\"page\":12,\"rect\":[420,860,780,920],\"source\":\"manual\",\"type\":\"sumatrapdf.rect\",\"unit\":\"pt\",\"zoom\":250}"));
}

void AnchorCaptureTest::buildsAnchorCompatibleWithSumatraPdfExecutor()
{
    const AnchorCaptureResult capture = captureManualPdfRectAnchor(validRectRequest());

    QVERIFY2(capture.success(), qPrintable(capture.error));
    QVERIFY(isSumatraPdfAnchor(capture.anchor));

    const SumatraPdfCommandResult command =
        buildSumatraPdfCommand(capture.anchor, QString(), QStringLiteral("C:/Tools/SumatraPDF.exe"));

    QVERIFY2(command.success(), qPrintable(command.error));
    QCOMPARE(command.command.filePath, QStringLiteral("E:/docs/clock.pdf"));
    QCOMPARE(command.command.arguments,
             QStringList({QStringLiteral("-reuse-instance"),
                          QStringLiteral("-page"),
                          QStringLiteral("12"),
                          QStringLiteral("-zoom"),
                          QStringLiteral("250"),
                          QStringLiteral("-scroll"),
                          QStringLiteral("420,860"),
                          QStringLiteral("E:/docs/clock.pdf")}));
}

void AnchorCaptureTest::savesManualPdfRectAnchorInRepository()
{
    InMemoryLibraryRepository repository;
    ManualPdfAnchorCreationService service(repository);

    const ManualPdfAnchorCreationResult result =
        service.createManualPdfRectAnchor(validCreationRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QVERIFY(!result.resource.id.isEmpty());
    QCOMPARE(result.resource.kind, ResourceKind::Pdf);
    QCOMPARE(result.resource.title, QStringLiteral("clock.pdf"));
    QCOMPARE(result.resource.location, QStringLiteral("E:/docs/clock.pdf"));
    QCOMPARE(result.anchor.name, QStringLiteral("Clock domain window"));
    QCOMPARE(result.anchor.targetFile, QStringLiteral("E:/docs/clock.pdf"));
    QCOMPARE(result.anchor.locatorType, QStringLiteral("sumatrapdf.rect"));
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
        service.createManualPdfRectAnchor(validCreationRequest());
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

void AnchorCaptureTest::createsManualPdfRectAnchorCompatibleWithSumatraPdfExecutor()
{
    InMemoryLibraryRepository repository;
    ManualPdfAnchorCreationService service(repository);
    const ManualPdfAnchorCreationResult result =
        service.createManualPdfRectAnchor(validCreationRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.anchor.locatorJson,
             QStringLiteral("{\"page\":12,\"rect\":[420,860,780,920],\"source\":\"manual\",\"type\":\"sumatrapdf.rect\",\"unit\":\"pt\",\"zoom\":250}"));
    QVERIFY(isSumatraPdfAnchor(result.anchor));

    const SumatraPdfCommandResult command =
        buildSumatraPdfCommand(result.anchor, QString(), QStringLiteral("C:/Tools/SumatraPDF.exe"));

    QVERIFY2(command.success(), qPrintable(command.error));
    QCOMPARE(command.command.filePath, QStringLiteral("E:/docs/clock.pdf"));
    QCOMPARE(command.command.arguments,
             QStringList({QStringLiteral("-reuse-instance"),
                          QStringLiteral("-page"),
                          QStringLiteral("12"),
                          QStringLiteral("-zoom"),
                          QStringLiteral("250"),
                          QStringLiteral("-scroll"),
                          QStringLiteral("420,860"),
                          QStringLiteral("E:/docs/clock.pdf")}));
    QCOMPARE(command.command.page, 12);
    QCOMPARE(command.command.zoom, 250.0);
    QCOMPARE(command.command.highlightRect, QRectF(420.0, 860.0, 360.0, 60.0));
}

void AnchorCaptureTest::rejectsInvalidManualPdfRectAnchorInputsWithoutSaving()
{
    InMemoryLibraryRepository repository;
    ManualPdfAnchorCreationService service(repository);

    ManualPdfAnchorCreationRequest request = validCreationRequest();
    request.name = QStringLiteral(" ");
    ManualPdfAnchorCreationResult result = service.createManualPdfRectAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Manual PDF anchor name is missing"));

    request = validCreationRequest();
    request.file.clear();
    result = service.createManualPdfRectAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Manual PDF anchor file is missing"));

    request = validCreationRequest();
    request.page = 0;
    result = service.createManualPdfRectAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("SumatraPDF capture page is missing"));

    request = validCreationRequest();
    request.rect.right = request.rect.left;
    result = service.createManualPdfRectAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("SumatraPDF capture rectangle is missing"));

    QVERIFY(repository.search(SearchQuery{}).isEmpty());
}

void AnchorCaptureTest::reportsManualPdfRectAnchorRepositorySaveFailure()
{
    RejectingRepository repository;
    ManualPdfAnchorCreationService service(repository);

    const ManualPdfAnchorCreationResult result =
        service.createManualPdfRectAnchor(validCreationRequest());

    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Unable to save manual PDF anchor"));
    QCOMPARE(repository.upsertCount, 1);
    QCOMPARE(repository.lastResource.location, QStringLiteral("E:/docs/clock.pdf"));
}

void AnchorCaptureTest::summarizesManualPdfCaptureLocatorForUsers()
{
    ManualPdfAnchorCreationRequest request = validCreationRequest();
    request.source = QStringLiteral("foreground-sumatrapdf-viewstate");
    request.zoom = 175.5;

    const QString summary = manualPdfAnchorLocatorSummary(request);
    QVERIFY(summary.contains(QStringLiteral("PDF file E:/docs/clock.pdf")));
    QVERIFY(summary.contains(QStringLiteral("page 12")));
    QVERIFY(summary.contains(QStringLiteral("rect 420,860,780,920 pt")));
    QVERIFY(summary.contains(QStringLiteral("zoom 175.5%")));
    QVERIFY(summary.contains(QStringLiteral("source foreground-sumatrapdf-viewstate")));
}

void AnchorCaptureTest::normalizesSumatraPdfDocumentTitlesForFallback()
{
    QCOMPARE(normalizedSumatraPdfDocumentTitleKey(QStringLiteral(" *\"HB0823_MIV_RV32IMA_L1_AXI.pdf\" ")),
             QStringLiteral("hb0823_miv_rv32ima_l1_axi"));
    QCOMPARE(normalizedSumatraPdfDocumentTitleKey(QStringLiteral("HB0823   MIV   RV32IMA")),
             QStringLiteral("hb0823 miv rv32ima"));
    QCOMPARE(normalizedSumatraPdfDocumentTitleKey(QStringLiteral("'Clock Domain Window.PDF'")),
             QStringLiteral("clock domain window"));
}

void AnchorCaptureTest::capturesForegroundSumatraPdfPathFromWindowTitleWithoutIndexedPdf()
{
    InMemoryLibraryRepository repository;

    ForegroundAppWindowContext context;
    context.windowTitle = QStringLiteral("*\"E:/docs/live foreground.pdf\" - SumatraPDF");
    context.processName = QStringLiteral("SumatraPDF.exe");

    const SumatraPdfForegroundCaptureResult result =
        captureSumatraPdfForegroundContext(repository, context, SumatraPdfViewState{});

    QVERIFY2(result.success(), qPrintable(result.status));
    QVERIFY(result.recognizedSumatraPdf);
    QCOMPARE(result.documentTitle, QStringLiteral("E:/docs/live foreground.pdf"));
    QCOMPARE(result.request.name, QStringLiteral("live foreground"));
    QCOMPARE(result.request.file, QStringLiteral("E:/docs/live foreground.pdf"));
    QCOMPARE(result.request.locatorType, QStringLiteral("sumatrapdf.page"));
    QCOMPARE(result.request.page, 1);
    QVERIFY(!result.request.rect.isValid());
    QCOMPARE(result.request.source, QStringLiteral("foreground-sumatrapdf-fallback"));
    QCOMPARE(result.request.targetApp, QStringLiteral("SumatraPDF"));

    QCOMPARE(sumatraPdfDocumentPathFromWindowTitle(context.windowTitle),
             QStringLiteral("E:/docs/live foreground.pdf"));
    QCOMPARE(sumatraPdfDocumentPathFromWindowTitle(
                 QStringLiteral("SumatraPDF - file:///E:/docs/live%20foreground.pdf")),
             QStringLiteral("E:/docs/live foreground.pdf"));
}

void AnchorCaptureTest::matchesForegroundSumatraPdfTitleToUniqueIndexedPdf()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("axi-spec");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("AMBA AXI Protocol Spec");
    resource.location = QStringLiteral("E:/docs/IHI0022K_amba_axi_protocol_spec[axi].pdf");
    QVERIFY(repository.upsertResource(resource));

    ForegroundAppWindowContext context;
    context.windowTitle = QStringLiteral("IHI0022K_amba_axi_protocol_spec[axi] - SumatraPDF");
    context.processName = QStringLiteral("SumatraPDF.exe");

    const SumatraPdfForegroundCaptureResult result =
        captureSumatraPdfForegroundContext(repository, context, SumatraPdfViewState{});

    QVERIFY2(result.success(), qPrintable(result.status));
    QVERIFY(result.recognizedSumatraPdf);
    QCOMPARE(result.documentTitle, QStringLiteral("IHI0022K_amba_axi_protocol_spec[axi]"));
    QCOMPARE(result.request.name, QStringLiteral("IHI0022K_amba_axi_protocol_spec[axi]"));
    QCOMPARE(result.request.file, resource.location);
    QCOMPARE(result.request.locatorType, QStringLiteral("sumatrapdf.page"));
    QCOMPARE(result.request.page, 1);
    QVERIFY(!result.request.rect.isValid());
    QCOMPARE(result.request.source, QStringLiteral("foreground-sumatrapdf-fallback"));
    QCOMPARE(result.request.targetApp, QStringLiteral("SumatraPDF"));

    QCOMPARE(sumatraPdfDocumentTitleFromWindowTitle(
                 QStringLiteral("*IHI0022K_amba_axi_protocol_spec[axi] - SumatraPDF")),
             QStringLiteral("IHI0022K_amba_axi_protocol_spec[axi]"));
}

void AnchorCaptureTest::matchesForegroundSumatraPdfTitleToFileKindPdfLocation()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("hb0823-file-resource");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("HB0823_MIV_RV32IMA_L1_AXI");
    resource.location = QStringLiteral("E:/docs/HB0823_MIV_RV32IMA_L1_AXI.pdf");
    QVERIFY(repository.upsertResource(resource));

    const QList<Resource> matches =
        sumatraPdfTitleMatchedPdfResources(repository, QStringLiteral("HB0823_MIV_RV32IMA_L1_AXI"));
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().id, resource.id);

    const std::optional<Resource> unique =
        uniqueSumatraPdfTitleMatchedPdfResource(repository, QStringLiteral("HB0823_MIV_RV32IMA_L1_AXI"));
    QVERIFY(unique.has_value());
    QCOMPARE(unique->location, resource.location);

    ForegroundAppWindowContext context;
    context.windowTitle = QStringLiteral("HB0823_MIV_RV32IMA_L1_AXI - SumatraPDF");
    context.processName = QStringLiteral("SumatraPDF.exe");

    const SumatraPdfForegroundCaptureResult result =
        captureSumatraPdfForegroundContext(repository, context, SumatraPdfViewState{});

    QVERIFY2(result.success(), qPrintable(result.status));
    QCOMPARE(result.request.file, resource.location);
}

void AnchorCaptureTest::preservesViewStateWhenTitleFallbackBuildsRequest()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("hb0823");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("HB0823_MIV_RV32IMA_L1_AXI");
    resource.location = QStringLiteral("E:/docs/HB0823_MIV_RV32IMA_L1_AXI.pdf");
    QVERIFY(repository.upsertResource(resource));

    ForegroundAppWindowContext context;
    context.windowTitle = QStringLiteral("HB0823_MIV_RV32IMA_L1_AXI - SumatraPDF");
    context.processName = QStringLiteral("SumatraPDF.exe");

    SumatraPdfViewState viewState;
    viewState.currentPage = 26;
    viewState.totalPages = 31;
    viewState.zoom = 300.0;
    viewState.source = QStringLiteral("test-toolbar");

    const SumatraPdfForegroundCaptureResult result =
        captureSumatraPdfForegroundContext(repository, context, viewState);

    QVERIFY2(result.success(), qPrintable(result.status));
    QCOMPARE(result.request.file, resource.location);
    QCOMPARE(result.request.name, QStringLiteral("HB0823_MIV_RV32IMA_L1_AXI"));
    QCOMPARE(result.request.page, 26);
    QCOMPARE(result.request.zoom, 300.0);
    QCOMPARE(result.request.source, QStringLiteral("foreground-sumatrapdf-viewstate"));
    QCOMPARE(result.viewState.currentPage, 26);
    QCOMPARE(result.viewState.totalPages, 31);
    QCOMPARE(result.viewState.zoom, 300.0);
}

void AnchorCaptureTest::parsesSumatraPdfDdeFileStateAndMousePosition()
{
    const SumatraPdfDdeFileState fileState = parseSumatraPdfDdeFileState(
        QStringLiteral("path: E:\\test_dir\\output.pdf\n"
                       "page: 28\n"
                       "pageCount: 149\n"
                       "zoom: -1\n"
                       "view: continuous\n"
                       "sumver: 3.7.20026\n"));

    QVERIFY2(fileState.success(), qPrintable(fileState.error));
    QCOMPARE(fileState.path, QStringLiteral("E:/test_dir/output.pdf"));
    QCOMPARE(fileState.page, 28);
    QCOMPARE(fileState.pageCount, 149);
    QCOMPARE(fileState.zoom, -1.0);
    QCOMPARE(fileState.view, QStringLiteral("continuous"));
    QCOMPARE(fileState.version, QStringLiteral("3.7.20026"));

    const SumatraPdfDdeMousePosition mousePosition = parseSumatraPdfDdeMousePosition(
        QStringLiteral("page: 28\n"
                       "x: 305.04\n"
                       "y: 395.58\n"
                       "ypdf: 396.42\n"));

    QVERIFY2(mousePosition.success(), qPrintable(mousePosition.error));
    QCOMPARE(mousePosition.page, 28);
    QCOMPARE(mousePosition.x, 305.04);
    QCOMPARE(mousePosition.y, 395.58);
    QCOMPARE(mousePosition.yPdf, 396.42);
    QVERIFY(mousePosition.hasYPdf);

    const SumatraPdfDdeMousePosition offPage =
        parseSumatraPdfDdeMousePosition(QStringLiteral("page: 0\nx: 0.00\ny: 0.00\n"));
    QVERIFY(!offPage.success());
    QVERIFY(offPage.error.contains(QStringLiteral("not over a page")));
}

void AnchorCaptureTest::buildsSumatraPdfDdeRegionFromMousePositions()
{
    SumatraPdfDdeMousePosition start;
    start.page = 28;
    start.x = 420.0;
    start.y = 920.0;

    SumatraPdfDdeMousePosition end;
    end.page = 28;
    end.x = 120.0;
    end.y = 640.0;

    const SumatraPdfDdeRegion region =
        sumatraPdfDdeRegionFromMousePositions(start, end);
    QVERIFY2(region.success(), qPrintable(region.error));
    QCOMPARE(region.page, 28);
    QCOMPARE(region.rect.left, 120.0);
    QCOMPARE(region.rect.top, 640.0);
    QCOMPARE(region.rect.right, 420.0);
    QCOMPARE(region.rect.bottom, 920.0);

    end.page = 29;
    const SumatraPdfDdeRegion crossPage =
        sumatraPdfDdeRegionFromMousePositions(start, end);
    QVERIFY(!crossPage.success());
    QCOMPARE(crossPage.error, QStringLiteral("PDF region must stay on one page"));

    end = start;
    const SumatraPdfDdeRegion empty =
        sumatraPdfDdeRegionFromMousePositions(start, end);
    QVERIFY(!empty.success());
    QCOMPARE(empty.error, QStringLiteral("PDF region is too small"));
}

void AnchorCaptureTest::prefersDdeDocumentPathOverTitleConfirmation()
{
    InMemoryLibraryRepository repository;

    ForegroundAppWindowContext context;
    context.windowTitle = QStringLiteral("missing-spec - SumatraPDF");
    context.processName = QStringLiteral("SumatraPDF.exe");

    SumatraPdfViewState viewState;
    viewState.documentPath = QStringLiteral("E:/docs/dde-active.pdf");
    viewState.currentPage = 43;
    viewState.totalPages = 149;
    viewState.zoom = 100.0;
    viewState.sumatraVersion = QStringLiteral("3.7.20026");
    viewState.source = QStringLiteral("sumatrapdf-dde");

    const SumatraPdfForegroundCaptureResult result =
        captureSumatraPdfForegroundContext(repository, context, viewState);

    QVERIFY2(result.success(), qPrintable(result.status));
    QVERIFY(!result.needsFileConfirmation);
    QCOMPARE(result.request.file, QStringLiteral("E:/docs/dde-active.pdf"));
    QCOMPARE(result.request.locatorType, QStringLiteral("sumatrapdf.page"));
    QCOMPARE(result.request.page, 43);
    QCOMPARE(result.request.zoom, 100.0);
    QCOMPARE(result.request.source, QStringLiteral("foreground-sumatrapdf-viewstate"));
    QCOMPARE(result.viewState.documentPath, QStringLiteral("E:/docs/dde-active.pdf"));
}

void AnchorCaptureTest::parsesSumatraPdfViewStateFromStatusText()
{
    const SumatraPdfViewState state = parseSumatraPdfViewStateText(
        QStringLiteral("Status: Page: 12 of 345   Zoom: 250%"),
        QStringLiteral("test-status"));

    QVERIFY(state.hasAnyViewState());
    QVERIFY(state.hasCurrentPage());
    QVERIFY(state.hasZoom());
    QCOMPARE(state.currentPage, 12);
    QCOMPARE(state.totalPages, 345);
    QCOMPARE(state.zoom, 250.0);
    QCOMPARE(state.source, QStringLiteral("test-status"));
    QVERIFY(state.diagnostics.isEmpty());

    const SumatraPdfViewState slashState = parseSumatraPdfViewStateText(
        QStringLiteral("SumatraPDF  7 / 91  175%"),
        QStringLiteral("test-toolbar"));
    QCOMPARE(slashState.currentPage, 7);
    QCOMPARE(slashState.totalPages, 91);
    QCOMPARE(slashState.zoom, 175.0);

    const SumatraPdfViewState pageBoxState = parseSumatraPdfViewStateText(
        QStringLiteral("28/149\n100%"),
        QStringLiteral("test-page-box"));
    QCOMPARE(pageBoxState.currentPage, 28);
    QCOMPARE(pageBoxState.totalPages, 149);
    QCOMPARE(pageBoxState.zoom, 100.0);

    const SumatraPdfViewState localizedUiaState = parseSumatraPdfViewStateText(
        QStringLiteral("\u9875:\n28\n\u7f29\u653e\n100%"),
        QStringLiteral("test-localized-uia"));
    QCOMPARE(localizedUiaState.currentPage, 28);
    QCOMPARE(localizedUiaState.totalPages, -1);
    QCOMPARE(localizedUiaState.zoom, 100.0);

    const SumatraPdfViewState localizedCombinedElementState = parseSumatraPdfViewStateText(
        QStringLiteral("\u9875: 28\nEdit\n100%"),
        QStringLiteral("test-localized-uia-combined"));
    QCOMPARE(localizedCombinedElementState.currentPage, 28);
    QCOMPARE(localizedCombinedElementState.totalPages, -1);
    QCOMPARE(localizedCombinedElementState.zoom, 100.0);

    const SumatraPdfViewState localizedLabelledZoomState = parseSumatraPdfViewStateText(
        QStringLiteral("221.58%\n\u9875: 28\n\u7f29\u653e 100%"),
        QStringLiteral("test-localized-labelled-zoom"));
    QCOMPARE(localizedLabelledZoomState.currentPage, 28);
    QCOMPARE(localizedLabelledZoomState.totalPages, -1);
    QCOMPARE(localizedLabelledZoomState.zoom, 100.0);

    const SumatraPdfViewState uiaState = parseSumatraPdfViewStateText(
        QStringLiteral("Page number\n26\nTotal pages\n31\nZoom\n300%"),
        QStringLiteral("test-uia"));
    QCOMPARE(uiaState.currentPage, 26);
    QCOMPARE(uiaState.totalPages, 31);
    QCOMPARE(uiaState.zoom, 300.0);
}

void AnchorCaptureTest::reportsUnparseableSumatraPdfViewStateText()
{
    const SumatraPdfViewState state = parseSumatraPdfViewStateText(
        QStringLiteral("Ready - no deterministic page or zoom here"),
        QStringLiteral("test-status"));

    QVERIFY(!state.hasAnyViewState());
    QVERIFY(!state.hasCurrentPage());
    QVERIFY(!state.hasZoom());
    QCOMPARE(state.currentPage, -1);
    QCOMPARE(state.totalPages, -1);
    QCOMPARE(state.zoom, -1.0);
    QVERIFY(state.diagnostics.contains(QStringLiteral("did not contain")));
}

void AnchorCaptureTest::injectsForegroundSumatraPdfViewStateIntoCaptureRequest()
{
    InMemoryLibraryRepository repository;

    ForegroundAppWindowContext context;
    context.windowTitle = QStringLiteral("*\"E:/docs/live foreground.pdf\" - SumatraPDF");
    context.processName = QStringLiteral("SumatraPDF.exe");

    SumatraPdfViewState viewState;
    viewState.currentPage = 37;
    viewState.totalPages = 220;
    viewState.zoom = 175.0;
    viewState.source = QStringLiteral("fake-status");

    const SumatraPdfForegroundCaptureResult result =
        captureSumatraPdfForegroundContext(repository, context, viewState);

    QVERIFY2(result.success(), qPrintable(result.status));
    QCOMPARE(result.request.file, QStringLiteral("E:/docs/live foreground.pdf"));
    QCOMPARE(result.request.locatorType, QStringLiteral("sumatrapdf.page"));
    QCOMPARE(result.request.page, 37);
    QCOMPARE(result.request.zoom, 175.0);
    QVERIFY(!result.request.rect.isValid());
    QCOMPARE(result.request.source, QStringLiteral("foreground-sumatrapdf-viewstate"));
    QCOMPARE(result.viewState.currentPage, 37);
    QCOMPARE(result.viewState.totalPages, 220);
    QCOMPARE(result.viewState.zoom, 175.0);
    QVERIFY(result.status.contains(QStringLiteral("page 37")));
    QVERIFY(result.status.contains(QStringLiteral("zoom 175%")));
    QVERIFY(!result.status.contains(QStringLiteral("rectangle")));
}

void AnchorCaptureTest::reportsForegroundSumatraPdfTitleWithoutFilePath()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("other-spec");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Other Spec");
    resource.location = QStringLiteral("E:/docs/other.pdf");
    QVERIFY(repository.upsertResource(resource));

    ForegroundAppWindowContext context;
    context.windowTitle = QStringLiteral("missing-spec - SumatraPDF");
    context.processName = QStringLiteral("SumatraPDF.exe");

    const SumatraPdfForegroundCaptureResult result =
        captureSumatraPdfForegroundContext(repository, context, SumatraPdfViewState{});

    QVERIFY(!result.success());
    QVERIFY(result.recognizedSumatraPdf);
    QVERIFY(result.needsFileConfirmation);
    QCOMPARE(result.documentTitle, QStringLiteral("missing-spec"));
    QVERIFY(result.status.contains(QStringLiteral("Confirm the PDF file")));
    QVERIFY(result.status.contains(QStringLiteral("no indexed PDF matched")));
    QVERIFY(result.request.file.isEmpty());
}

void AnchorCaptureTest::rejectsForegroundSumatraPdfTitleWithMultipleIndexedPdfMatches()
{
    InMemoryLibraryRepository repository;

    Resource first;
    first.id = QStringLiteral("clock-a");
    first.kind = ResourceKind::Pdf;
    first.title = QStringLiteral("clock");
    first.location = QStringLiteral("E:/docs/a/clock.pdf");
    QVERIFY(repository.upsertResource(first));

    Resource second;
    second.id = QStringLiteral("clock-b");
    second.kind = ResourceKind::Pdf;
    second.title = QStringLiteral("clock");
    second.location = QStringLiteral("E:/docs/b/clock.pdf");
    QVERIFY(repository.upsertResource(second));

    ForegroundAppWindowContext context;
    context.windowTitle = QStringLiteral("clock - SumatraPDF");
    context.processName = QStringLiteral("SumatraPDF.exe");

    const SumatraPdfForegroundCaptureResult result =
        captureSumatraPdfForegroundContext(repository, context, SumatraPdfViewState{});

    QVERIFY(!result.success());
    QVERIFY(result.recognizedSumatraPdf);
    QVERIFY(result.needsFileConfirmation);
    QCOMPARE(result.documentTitle, QStringLiteral("clock"));
    QVERIFY(result.status.contains(QStringLiteral("Confirm the PDF file")));
    QVERIFY(result.status.contains(QStringLiteral("2 indexed PDFs matched")));
    QVERIFY(result.request.file.isEmpty());
}

void AnchorCaptureTest::buildsAndStoresSumatraPdfSearchAnchor()
{
    PdfCaptureRequest request;
    request.targetFile = QStringLiteral("E:/docs/protocol.pdf");
    request.locatorType = QStringLiteral("sumatrapdf.search");
    request.page = 17;
    request.zoom = 140.0;
    request.searchText = QStringLiteral("  clock   domain crossing  ");
    request.contextBefore = QStringLiteral("the selected");
    request.contextAfter = QStringLiteral("must be synchronized");
    request.occurrence = 2;
    request.fallbackRect = {10.0, 20.0, 210.0, 64.0};
    request.anchorName = QStringLiteral("CDC requirement");

    const AnchorCaptureResult captured = captureManualPdfAnchor(request);
    QVERIFY2(captured.success(), qPrintable(captured.error));
    QCOMPARE(captured.locatorType, QStringLiteral("sumatrapdf.search"));
    const QJsonObject locator = QJsonDocument::fromJson(
        captured.anchor.locatorJson.toUtf8()).object();
    QCOMPARE(locator.value(QStringLiteral("text")).toString(),
             QStringLiteral("clock domain crossing"));
    QCOMPARE(locator.value(QStringLiteral("contextBefore")).toString(),
             QStringLiteral("the selected"));
    QCOMPARE(locator.value(QStringLiteral("contextAfter")).toString(),
             QStringLiteral("must be synchronized"));
    QCOMPARE(locator.value(QStringLiteral("occurrence")).toInt(), 2);
    QCOMPARE(locator.value(QStringLiteral("fallbackRect")).toArray().size(), 4);

    ManualPdfAnchorCreationRequest creation;
    creation.name = request.anchorName;
    creation.file = request.targetFile;
    creation.locatorType = request.locatorType;
    creation.page = request.page;
    creation.zoom = request.zoom;
    creation.searchText = request.searchText;
    creation.contextBefore = request.contextBefore;
    creation.contextAfter = request.contextAfter;
    creation.occurrence = request.occurrence;
    creation.fallbackRect = request.fallbackRect;
    creation.aliases = {QStringLiteral("CDC clause")};
    creation.tags = {QStringLiteral("#spec")};

    InMemoryLibraryRepository repository;
    AnchorCaptureCommitService service(repository);
    const AnchorCaptureCommitResult committed = service.commit(
        anchorCaptureDraftFromPdfRequest(creation));
    QVERIFY2(committed.success(), qPrintable(committed.error));
    QCOMPARE(committed.anchor.locatorType,
             QStringLiteral("sumatrapdf.search"));
    QCOMPARE(committed.anchor.aliases, QStringList{QStringLiteral("CDC clause")});
    QCOMPARE(committed.anchor.tags, QStringList{QStringLiteral("spec")});
    const std::optional<Resource> stored =
        repository.findResource(committed.resource.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->anchors.size(), 1);
    QCOMPARE(stored->anchors.first().locatorJson, committed.anchor.locatorJson);
}

void AnchorCaptureTest::validatesAndCommitsSharedAnchorDraft()
{
    AnchorCaptureDraft draft;
    draft.targetApp = QStringLiteral("Word");
    draft.targetFile = QStringLiteral("E:/docs/design.docx");
    draft.locatorType = QStringLiteral("word.bookmark");
    draft.locatorJson = QStringLiteral(
        R"({"type":"word.bookmark","bookmark":"_Pinloom_a1"})");
    draft.suggestedName = QStringLiteral("Reset sequence");
    draft.aliases = {QStringLiteral(" reset "), QStringLiteral("RESET")};
    draft.tags = {QStringLiteral("#review"), QStringLiteral("review")};
    draft.pinned = true;
    draft.mutationRequired = true;

    QString error;
    QVERIFY(!draft.isValid(&error));
    QVERIFY(error.contains(QStringLiteral("authorization"), Qt::CaseInsensitive));
    draft.mutationAuthorized = true;
    QVERIFY2(draft.isValid(&error), qPrintable(error));

    InMemoryLibraryRepository repository;
    AnchorCaptureCommitService service(repository);
    const AnchorCaptureCommitResult result = service.commit(draft);
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.resource.kind, ResourceKind::File);
    QCOMPARE(result.anchor.name, QStringLiteral("Reset sequence"));
    QCOMPARE(result.anchor.aliases, QStringList{QStringLiteral("reset")});
    QCOMPARE(result.anchor.tags, QStringList{QStringLiteral("review")});
    QVERIFY(result.anchor.pinned);
}

void AnchorCaptureTest::capturesAndFinalizesNativeOfficeAnchors()
{
    QStringList scripts;
    const NativeCaptureScriptRunner runner = [&scripts](const QString &script, int) {
        scripts.append(script);
        NativeCaptureScriptResult result;
        if (script.contains(QStringLiteral("Bookmarks.Add"))) {
            result.standardOutput = QStringLiteral(
                R"({"ok":true,"locatorType":"word.bookmark","locator":{"type":"word.bookmark","bookmark":"_Pinloom_abcd"}})");
        } else if (script.contains(QStringLiteral("Word.Application"))) {
            result.standardOutput = QStringLiteral(
                R"({"ok":true,"targetApp":"Word","targetFile":"E:/docs/design.docx","locatorType":"word.bookmark","locator":{"type":"word.bookmark","bookmark":"_Pinloom_abcd"},"suggestedName":"Reset sequence","provenance":"word-com","mutationRequired":true,"mutationOptional":false,"mutationKind":"word.bookmark","mutationLabel":"Create bookmark","mutationPayload":{"documentPath":"E:/docs/design.docx","start":20,"end":34,"bookmark":"_Pinloom_abcd"}})");
        } else if (script.contains(QStringLiteral("Excel.Application"))) {
            result.standardOutput = QStringLiteral(
                R"({"ok":true,"targetApp":"Excel","targetFile":"E:/docs/map.xlsx","locatorType":"excel.range","locator":{"type":"excel.range","sheet":"Map","range":"$B$2:$D$5"},"suggestedName":"Map $B$2:$D$5","provenance":"excel-com","mutationRequired":false,"mutationOptional":true,"mutationKind":"excel.definedName","mutationLabel":"Create name","mutationPayload":{"workbookPath":"E:/docs/map.xlsx","sheet":"Map","range":"$B$2:$D$5","definedName":"_Pinloom_range"}})");
        } else {
            result.standardOutput = QStringLiteral(
                R"({"ok":false,"error":"unsupported test script"})");
        }
        return result;
    };

    QCOMPARE(nativeAnchorApplicationForProcess(QStringLiteral("WINWORD.EXE")),
             NativeAnchorApplication::Word);
    QCOMPARE(nativeAnchorApplicationForProcess(QStringLiteral("VISIO.EXE")),
             NativeAnchorApplication::Visio);
    QCOMPARE(nativeAnchorApplicationForProcess(QStringLiteral("EXCEL.EXE")),
             NativeAnchorApplication::Excel);
    QCOMPARE(nativeAnchorApplicationForProcess(QStringLiteral("notepad.exe")),
             NativeAnchorApplication::Unknown);

    NativeAnchorCaptureAdapter adapter(runner);
    NativeAnchorCaptureResult word = adapter.captureForProcess(
        QStringLiteral("WINWORD.EXE"));
    QVERIFY2(word.success(), qPrintable(word.error));
    QVERIFY(word.draft.mutationRequired);
    QVERIFY(!word.draft.mutationAuthorized);
    word.draft.mutationAuthorized = true;
    const NativeAnchorCaptureResult finalized = adapter.finalize(word.draft);
    QVERIFY2(finalized.success(), qPrintable(finalized.error));
    QVERIFY(!finalized.draft.mutationRequired);
    QCOMPARE(finalized.draft.locatorType, QStringLiteral("word.bookmark"));
    QVERIFY(finalized.draft.locatorJson.contains(QStringLiteral("_Pinloom_abcd")));

    NativeAnchorCaptureResult excel = adapter.capture(
        NativeAnchorApplication::Excel);
    QVERIFY2(excel.success(), qPrintable(excel.error));
    QVERIFY(excel.draft.mutationOptional);
    const NativeAnchorCaptureResult unchanged = adapter.finalize(excel.draft);
    QVERIFY2(unchanged.success(), qPrintable(unchanged.error));
    QCOMPARE(unchanged.draft.locatorType, QStringLiteral("excel.range"));
    QVERIFY(!unchanged.draft.mutationOptional);
    QCOMPARE(scripts.size(), 3);
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
            service.createManualPdfRectAnchor(validCreationRequest());
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
