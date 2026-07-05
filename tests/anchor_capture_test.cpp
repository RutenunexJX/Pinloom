#include "pinloom/core/AnchorCapture.h"
#include "pinloom/core/ExcelCommand.h"
#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/core/ManualExcelAnchorCreation.h"
#include "pinloom/core/ManualPdfAnchorCreation.h"
#include "pinloom/core/ManualPowerPointAnchorCreation.h"
#include "pinloom/core/ManualVisioAnchorCreation.h"
#include "pinloom/core/ManualWordAnchorCreation.h"
#include "pinloom/core/PdfXChangeCommand.h"
#include "pinloom/core/PdfXChangeForegroundCapture.h"
#include "pinloom/core/PowerPointCommand.h"
#include "pinloom/core/SqliteLibraryRepository.h"
#include "pinloom/core/VisioCommand.h"
#include "pinloom/core/WordCommand.h"

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
    void buildsManualPdfXChangeRectAnchor();
    void reportsMissingPdfXChangeCaptureInputs();
    void keepsPdfXChangeLocatorJsonStable();
    void buildsAnchorCompatibleWithPdfXChangeExecutor();
    void buildsManualExcelRangeAnchor();
    void buildsManualExcelNamedRangeAnchor();
    void savesManualExcelRangeAnchorInRepository();
    void rejectsInvalidManualExcelAnchorInputsWithoutSaving();
    void createsManualExcelAnchorCompatibleWithExcelExecutor();
    void buildsManualVisioShapeAnchor();
    void savesManualVisioShapeAnchorInRepository();
    void rejectsInvalidManualVisioAnchorInputsWithoutSaving();
    void createsManualVisioAnchorCompatibleWithVisioExecutor();
    void buildsManualWordBookmarkAnchor();
    void savesManualWordBookmarkAnchorInRepository();
    void rejectsInvalidManualWordBookmarkAnchorInputsWithoutSaving();
    void createsManualWordBookmarkAnchorCompatibleWithWordExecutor();
    void buildsManualPowerPointShapeIdAnchor();
    void buildsManualPowerPointShapeNameAnchor();
    void savesManualPowerPointShapeAnchorInRepository();
    void rejectsInvalidManualPowerPointAnchorInputsWithoutSaving();
    void createsManualPowerPointShapeAnchorCompatibleWithPowerPointExecutor();
    void savesManualPdfRectAnchorInRepository();
    void searchesCreatedManualPdfRectAnchorByNameAliasAndTag();
    void createsManualPdfRectAnchorCompatibleWithPdfXChangeExecutor();
    void rejectsInvalidManualPdfRectAnchorInputsWithoutSaving();
    void reportsManualPdfRectAnchorRepositorySaveFailure();
    void matchesForegroundPdfXChangeTitleToUniqueIndexedPdf();
    void rejectsForegroundPdfXChangeTitleWithoutIndexedPdfMatch();
    void rejectsForegroundPdfXChangeTitleWithMultipleIndexedPdfMatches();
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

static ExcelCaptureRequest validExcelRangeCaptureRequest()
{
    ExcelCaptureRequest request;
    request.anchorName = QStringLiteral("Q3 budget table");
    request.targetFile = QStringLiteral("E:/books/budget.xlsx");
    request.sheet = QStringLiteral(" Sheet1 ");
    request.rangeAddress = QStringLiteral(" B12:D18 ");
    request.namedRange = QStringLiteral("BudgetTable");
    return request;
}

static ManualExcelAnchorCreationRequest validExcelRangeCreationRequest()
{
    ManualExcelAnchorCreationRequest request;
    request.name = QStringLiteral("Q3 budget table");
    request.file = QStringLiteral("E:/books/budget.xlsx");
    request.sheet = QStringLiteral("Sheet1");
    request.rangeAddress = QStringLiteral("B12:D18");
    request.aliases = {QStringLiteral("worksheet slice"), QStringLiteral("Worksheet Slice")};
    request.tags = {QStringLiteral("#finance"), QStringLiteral("finance")};
    request.pinned = true;
    return request;
}

static VisioCaptureRequest validVisioShapeCaptureRequest()
{
    VisioCaptureRequest request;
    request.anchorName = QStringLiteral("Power gate symbol");
    request.targetFile = QStringLiteral("E:/drawings/power.vsdx");
    request.page = QStringLiteral(" Page-1 ");
    request.shapeUniqueId = QStringLiteral(" {00000000-0000-0000-0000-000000000000} ");
    return request;
}

static ManualVisioAnchorCreationRequest validVisioShapeCreationRequest()
{
    ManualVisioAnchorCreationRequest request;
    request.name = QStringLiteral("Power gate symbol");
    request.file = QStringLiteral("E:/drawings/power.vsdx");
    request.page = QStringLiteral("Page-1");
    request.shapeUniqueId = QStringLiteral("{00000000-0000-0000-0000-000000000000}");
    request.aliases = {QStringLiteral("gate mark"), QStringLiteral("Gate Mark")};
    request.tags = {QStringLiteral("#phase5"), QStringLiteral("phase5")};
    request.pinned = true;
    return request;
}

static WordCaptureRequest validWordBookmarkCaptureRequest()
{
    WordCaptureRequest request;
    request.anchorName = QStringLiteral("Requirement 12");
    request.targetApp = QStringLiteral(" MS Word ");
    request.targetFile = QStringLiteral("E:/docs/requirements.docx");
    request.bookmark = QStringLiteral(" Requirement_12 ");
    request.source = QStringLiteral(" Host ");
    return request;
}

static ManualWordAnchorCreationRequest validWordBookmarkCreationRequest()
{
    ManualWordAnchorCreationRequest request;
    request.name = QStringLiteral("Requirement 12");
    request.file = QStringLiteral("E:/docs/requirements.docx");
    request.bookmark = QStringLiteral("Requirement_12");
    request.targetApp = QStringLiteral("MS Word");
    request.source = QStringLiteral("Host");
    request.aliases = {QStringLiteral("word bookmark"), QStringLiteral("Word Bookmark")};
    request.tags = {QStringLiteral("#phase5"), QStringLiteral("phase5")};
    request.pinned = true;
    return request;
}

static PowerPointCaptureRequest validPowerPointShapeIdCaptureRequest()
{
    PowerPointCaptureRequest request;
    request.anchorName = QStringLiteral("Valve A callout");
    request.targetApp = QStringLiteral(" PPT ");
    request.targetFile = QStringLiteral("E:/slides/process.pptx");
    request.slide = 12;
    request.shapeId = 42;
    request.shapeName = QStringLiteral(" Valve A ");
    request.source = QStringLiteral(" Host ");
    return request;
}

static PowerPointCaptureRequest validPowerPointShapeNameCaptureRequest()
{
    PowerPointCaptureRequest request;
    request.anchorName = QStringLiteral("Pump curve note");
    request.targetFile = QStringLiteral("E:/slides/process.pptx");
    request.slide = 7;
    request.shapeName = QStringLiteral(" Pump Curve ");
    return request;
}

static ManualPowerPointAnchorCreationRequest validPowerPointShapeCreationRequest()
{
    ManualPowerPointAnchorCreationRequest request;
    request.name = QStringLiteral("Valve A callout");
    request.file = QStringLiteral("E:/slides/process.pptx");
    request.slide = 12;
    request.shapeId = 42;
    request.aliases = {QStringLiteral("slide callout"), QStringLiteral("Slide Callout")};
    request.tags = {QStringLiteral("#phase5"), QStringLiteral("phase5")};
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
    QCOMPARE(command.command.action, QStringLiteral("page=12;zoom=250;highlight=420,780,860,920;usept=yes"));
}

void AnchorCaptureTest::buildsManualExcelRangeAnchor()
{
    const ExcelCaptureResult result = captureManualExcelAnchor(validExcelRangeCaptureRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.targetApp, QStringLiteral("Microsoft Excel"));
    QCOMPARE(result.targetFile, QStringLiteral("E:/books/budget.xlsx"));
    QCOMPARE(result.locatorType, QStringLiteral("excel.range"));
    QCOMPARE(result.sheet, QStringLiteral("Sheet1"));
    QCOMPARE(result.rangeAddress, QStringLiteral("B12:D18"));
    QCOMPARE(result.namedRange, QStringLiteral("BudgetTable"));
    QCOMPARE(result.source, QStringLiteral("manual"));

    QCOMPARE(result.anchor.type, AnchorType::Manual);
    QCOMPARE(result.anchor.name, QStringLiteral("Q3 budget table"));
    QCOMPARE(result.anchor.target, QStringLiteral("Q3 budget table"));
    QCOMPARE(result.anchor.targetApp, QStringLiteral("Microsoft Excel"));
    QCOMPARE(result.anchor.targetFile, QStringLiteral("E:/books/budget.xlsx"));
    QCOMPARE(result.anchor.locatorType, QStringLiteral("excel.range"));
    QCOMPARE(result.anchor.locatorJson,
             QStringLiteral("{\"name\":\"BudgetTable\",\"range\":\"B12:D18\",\"sheet\":\"Sheet1\",\"source\":\"manual\",\"target_file\":\"E:/books/budget.xlsx\",\"type\":\"excel.range\"}"));
}

void AnchorCaptureTest::buildsManualExcelNamedRangeAnchor()
{
    ExcelCaptureRequest request;
    request.anchorName = QStringLiteral("Revenue table");
    request.targetFile = QStringLiteral("E:/books/revenue.xlsx");
    request.namedRange = QStringLiteral(" RevenueTable ");

    const ExcelCaptureResult result = captureManualExcelAnchor(request);

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.locatorType, QStringLiteral("excel.name"));
    QCOMPARE(result.namedRange, QStringLiteral("RevenueTable"));
    QCOMPARE(result.anchor.locatorType, QStringLiteral("excel.name"));
    QCOMPARE(result.anchor.locatorJson,
             QStringLiteral("{\"name\":\"RevenueTable\",\"source\":\"manual\",\"target_file\":\"E:/books/revenue.xlsx\",\"type\":\"excel.name\"}"));
    QVERIFY(isExcelAnchor(result.anchor));

    const ExcelJumpCommandResult command = buildExcelJumpCommand(result.anchor, QString());
    QVERIFY2(command.success(), qPrintable(command.error));
    QCOMPARE(command.command.workbookPath, QStringLiteral("E:/books/revenue.xlsx"));
    QCOMPARE(command.command.locatorType, QStringLiteral("excel.name"));
    QCOMPARE(command.command.namedRange, QStringLiteral("RevenueTable"));
}

void AnchorCaptureTest::savesManualExcelRangeAnchorInRepository()
{
    InMemoryLibraryRepository repository;
    ManualExcelAnchorCreationService service(repository);

    const ManualExcelAnchorCreationResult result =
        service.createManualExcelAnchor(validExcelRangeCreationRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QVERIFY(!result.resource.id.isEmpty());
    QCOMPARE(result.resource.kind, ResourceKind::File);
    QCOMPARE(result.resource.title, QStringLiteral("budget.xlsx"));
    QCOMPARE(result.resource.location, QStringLiteral("E:/books/budget.xlsx"));
    QCOMPARE(result.anchor.name, QStringLiteral("Q3 budget table"));
    QCOMPARE(result.anchor.targetApp, QStringLiteral("Microsoft Excel"));
    QCOMPARE(result.anchor.targetFile, QStringLiteral("E:/books/budget.xlsx"));
    QCOMPARE(result.anchor.locatorType, QStringLiteral("excel.range"));
    QCOMPARE(result.anchor.aliases, QStringList{QStringLiteral("worksheet slice")});
    QCOMPARE(result.anchor.tags, QStringList{QStringLiteral("finance")});
    QVERIFY(result.anchor.pinned);
    QVERIFY(result.anchor.createdAt.isValid());
    QVERIFY(result.anchor.updatedAt.isValid());

    const std::optional<Resource> stored = repository.findResource(result.resource.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->anchors.size(), 1);
    QCOMPARE(stored->anchors.first().id, result.anchor.id);
    QCOMPARE(stored->anchors.first().locatorJson, result.anchor.locatorJson);

    const QList<SearchResult> aliasResults = repository.search(SearchQuery{QStringLiteral("worksheet slice")});
    QVERIFY(hasAnchorSearchResult(aliasResults,
                                  QStringLiteral("anchor_alias"),
                                  QStringLiteral("Q3 budget table")));

    const QList<SearchResult> tagResults = repository.search(SearchQuery{QStringLiteral("finance")});
    QVERIFY(hasAnchorSearchResult(tagResults,
                                  QStringLiteral("anchor_tag"),
                                  QStringLiteral("Q3 budget table")));
}

void AnchorCaptureTest::rejectsInvalidManualExcelAnchorInputsWithoutSaving()
{
    InMemoryLibraryRepository repository;
    ManualExcelAnchorCreationService service(repository);

    ManualExcelAnchorCreationRequest request = validExcelRangeCreationRequest();
    request.name = QStringLiteral(" ");
    ManualExcelAnchorCreationResult result = service.createManualExcelAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Manual Excel anchor name is missing"));

    request = validExcelRangeCreationRequest();
    request.file.clear();
    result = service.createManualExcelAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Manual Excel anchor file is missing"));

    request = validExcelRangeCreationRequest();
    request.rangeAddress.clear();
    result = service.createManualExcelAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Excel capture range or named range is missing"));

    request = validExcelRangeCreationRequest();
    request.sheet.clear();
    result = service.createManualExcelAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Excel capture range sheet is missing"));

    request = validExcelRangeCreationRequest();
    request.locatorType = QStringLiteral("excel.cell");
    result = service.createManualExcelAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Excel capture locator type is unsupported"));

    QVERIFY(repository.search(SearchQuery{}).isEmpty());
}

void AnchorCaptureTest::createsManualExcelAnchorCompatibleWithExcelExecutor()
{
    InMemoryLibraryRepository repository;
    ManualExcelAnchorCreationService service(repository);
    const ManualExcelAnchorCreationResult result =
        service.createManualExcelAnchor(validExcelRangeCreationRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QVERIFY(isExcelAnchor(result.anchor));

    const ExcelJumpCommandResult command = buildExcelJumpCommand(result.anchor, QString());

    QVERIFY2(command.success(), qPrintable(command.error));
    QCOMPARE(command.command.workbookPath, QStringLiteral("E:/books/budget.xlsx"));
    QCOMPARE(command.command.locatorType, QStringLiteral("excel.range"));
    QCOMPARE(command.command.sheetName, QStringLiteral("Sheet1"));
    QCOMPARE(command.command.rangeAddress, QStringLiteral("B12:D18"));
    QVERIFY(command.command.powerShellScript.contains(QStringLiteral("Workbooks.Open('E:/books/budget.xlsx')")));
    QVERIFY(command.command.powerShellScript.contains(QStringLiteral("Worksheets.Item('Sheet1')")));
    QVERIFY(command.command.powerShellScript.contains(QStringLiteral("Range('B12:D18')")));
}

void AnchorCaptureTest::buildsManualVisioShapeAnchor()
{
    const VisioCaptureResult result = captureManualVisioAnchor(validVisioShapeCaptureRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.targetApp, QStringLiteral("Microsoft Visio"));
    QCOMPARE(result.targetFile, QStringLiteral("E:/drawings/power.vsdx"));
    QCOMPARE(result.locatorType, QStringLiteral("visio.shape"));
    QCOMPARE(result.page, QStringLiteral("Page-1"));
    QCOMPARE(result.shapeUniqueId, QStringLiteral("{00000000-0000-0000-0000-000000000000}"));
    QCOMPARE(result.source, QStringLiteral("manual"));

    QCOMPARE(result.anchor.type, AnchorType::Manual);
    QCOMPARE(result.anchor.name, QStringLiteral("Power gate symbol"));
    QCOMPARE(result.anchor.target, QStringLiteral("Power gate symbol"));
    QCOMPARE(result.anchor.targetApp, QStringLiteral("Microsoft Visio"));
    QCOMPARE(result.anchor.targetFile, QStringLiteral("E:/drawings/power.vsdx"));
    QCOMPARE(result.anchor.locatorType, QStringLiteral("visio.shape"));

    const QJsonDocument locatorDocument = QJsonDocument::fromJson(result.anchor.locatorJson.toUtf8());
    QVERIFY(locatorDocument.isObject());
    const QJsonObject locator = locatorDocument.object();
    QCOMPARE(locator.value(QStringLiteral("type")).toString(), QStringLiteral("visio.shape"));
    QCOMPARE(locator.value(QStringLiteral("page")).toString(), QStringLiteral("Page-1"));
    QCOMPARE(locator.value(QStringLiteral("shape_unique_id")).toString(),
             QStringLiteral("{00000000-0000-0000-0000-000000000000}"));
    QCOMPARE(locator.value(QStringLiteral("shapeUniqueID")).toString(),
             QStringLiteral("{00000000-0000-0000-0000-000000000000}"));
    QCOMPARE(locator.value(QStringLiteral("target_file")).toString(), QStringLiteral("E:/drawings/power.vsdx"));
    QCOMPARE(locator.value(QStringLiteral("source")).toString(), QStringLiteral("manual"));
}

void AnchorCaptureTest::savesManualVisioShapeAnchorInRepository()
{
    InMemoryLibraryRepository repository;
    ManualVisioAnchorCreationService service(repository);

    const ManualVisioAnchorCreationResult result =
        service.createManualVisioAnchor(validVisioShapeCreationRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QVERIFY(!result.resource.id.isEmpty());
    QCOMPARE(result.resource.kind, ResourceKind::File);
    QCOMPARE(result.resource.title, QStringLiteral("power.vsdx"));
    QCOMPARE(result.resource.location, QStringLiteral("E:/drawings/power.vsdx"));
    QCOMPARE(result.anchor.name, QStringLiteral("Power gate symbol"));
    QCOMPARE(result.anchor.targetApp, QStringLiteral("Microsoft Visio"));
    QCOMPARE(result.anchor.targetFile, QStringLiteral("E:/drawings/power.vsdx"));
    QCOMPARE(result.anchor.locatorType, QStringLiteral("visio.shape"));
    QCOMPARE(result.anchor.aliases, QStringList{QStringLiteral("gate mark")});
    QCOMPARE(result.anchor.tags, QStringList{QStringLiteral("phase5")});
    QVERIFY(result.anchor.pinned);
    QVERIFY(result.anchor.createdAt.isValid());
    QVERIFY(result.anchor.updatedAt.isValid());

    const std::optional<Resource> stored = repository.findResource(result.resource.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->anchors.size(), 1);
    QCOMPARE(stored->anchors.first().id, result.anchor.id);
    QCOMPARE(stored->anchors.first().locatorJson, result.anchor.locatorJson);

    const QList<SearchResult> nameResults =
        repository.search(SearchQuery{QStringLiteral("Power gate symbol")});
    QVERIFY(hasAnchorSearchResult(nameResults,
                                  QStringLiteral("anchor_name"),
                                  QStringLiteral("Power gate symbol")));

    const QList<SearchResult> aliasResults =
        repository.search(SearchQuery{QStringLiteral("gate mark")});
    QVERIFY(hasAnchorSearchResult(aliasResults,
                                  QStringLiteral("anchor_alias"),
                                  QStringLiteral("Power gate symbol")));

    const QList<SearchResult> tagResults =
        repository.search(SearchQuery{QStringLiteral("phase5")});
    QVERIFY(hasAnchorSearchResult(tagResults,
                                  QStringLiteral("anchor_tag"),
                                  QStringLiteral("Power gate symbol")));
}

void AnchorCaptureTest::rejectsInvalidManualVisioAnchorInputsWithoutSaving()
{
    InMemoryLibraryRepository repository;
    ManualVisioAnchorCreationService service(repository);

    ManualVisioAnchorCreationRequest request = validVisioShapeCreationRequest();
    request.name = QStringLiteral(" ");
    ManualVisioAnchorCreationResult result = service.createManualVisioAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Manual Visio anchor name is missing"));

    request = validVisioShapeCreationRequest();
    request.file.clear();
    result = service.createManualVisioAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Manual Visio anchor file is missing"));

    request = validVisioShapeCreationRequest();
    request.page.clear();
    result = service.createManualVisioAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Visio capture page is missing"));

    request = validVisioShapeCreationRequest();
    request.shapeUniqueId.clear();
    result = service.createManualVisioAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Visio capture shape UniqueID is missing"));

    request = validVisioShapeCreationRequest();
    request.locatorType = QStringLiteral("visio.page");
    result = service.createManualVisioAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Visio capture locator type is unsupported"));

    QVERIFY(repository.search(SearchQuery{}).isEmpty());
}

void AnchorCaptureTest::createsManualVisioAnchorCompatibleWithVisioExecutor()
{
    InMemoryLibraryRepository repository;
    ManualVisioAnchorCreationService service(repository);
    const ManualVisioAnchorCreationResult result =
        service.createManualVisioAnchor(validVisioShapeCreationRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QVERIFY(isVisioAnchor(result.anchor));

    const VisioJumpCommandResult command = buildVisioJumpCommand(result.anchor, QString());

    QVERIFY2(command.success(), qPrintable(command.error));
    QCOMPARE(command.command.documentPath, QStringLiteral("E:/drawings/power.vsdx"));
    QCOMPARE(command.command.locatorType, QStringLiteral("visio.shape"));
    QCOMPARE(command.command.pageName, QStringLiteral("Page-1"));
    QCOMPARE(command.command.shapeUniqueId, QStringLiteral("{00000000-0000-0000-0000-000000000000}"));
    QVERIFY(command.command.powerShellScript.contains(QStringLiteral("Documents.Open('E:/drawings/power.vsdx')")));
    QVERIFY(command.command.powerShellScript.contains(QStringLiteral("Pages.ItemU('Page-1')")));
    QVERIFY(command.command.powerShellScript.contains(QStringLiteral("ItemFromUniqueID('{00000000-0000-0000-0000-000000000000}')")));
}

void AnchorCaptureTest::buildsManualWordBookmarkAnchor()
{
    const WordCaptureResult result = captureManualWordBookmarkAnchor(validWordBookmarkCaptureRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.targetApp, QStringLiteral("MS Word"));
    QCOMPARE(result.targetFile, QStringLiteral("E:/docs/requirements.docx"));
    QCOMPARE(result.locatorType, QStringLiteral("word.bookmark"));
    QCOMPARE(result.bookmark, QStringLiteral("Requirement_12"));
    QCOMPARE(result.source, QStringLiteral("host"));

    QCOMPARE(result.anchor.type, AnchorType::Manual);
    QCOMPARE(result.anchor.name, QStringLiteral("Requirement 12"));
    QCOMPARE(result.anchor.target, QStringLiteral("Requirement 12"));
    QCOMPARE(result.anchor.targetApp, QStringLiteral("MS Word"));
    QCOMPARE(result.anchor.targetFile, QStringLiteral("E:/docs/requirements.docx"));
    QCOMPARE(result.anchor.locatorType, QStringLiteral("word.bookmark"));

    const QJsonDocument locatorDocument = QJsonDocument::fromJson(result.anchor.locatorJson.toUtf8());
    QVERIFY(locatorDocument.isObject());
    const QJsonObject locator = locatorDocument.object();
    QCOMPARE(locator.value(QStringLiteral("type")).toString(), QStringLiteral("word.bookmark"));
    QCOMPARE(locator.value(QStringLiteral("bookmark")).toString(), QStringLiteral("Requirement_12"));
    QCOMPARE(locator.value(QStringLiteral("target_file")).toString(), QStringLiteral("E:/docs/requirements.docx"));
    QCOMPARE(locator.value(QStringLiteral("source")).toString(), QStringLiteral("host"));
    QCOMPARE(locator.value(QStringLiteral("target_app")).toString(), QStringLiteral("MS Word"));
}

void AnchorCaptureTest::savesManualWordBookmarkAnchorInRepository()
{
    InMemoryLibraryRepository repository;
    ManualWordAnchorCreationService service(repository);

    const ManualWordAnchorCreationResult result =
        service.createManualWordBookmarkAnchor(validWordBookmarkCreationRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QVERIFY(!result.resource.id.isEmpty());
    QCOMPARE(result.resource.kind, ResourceKind::File);
    QCOMPARE(result.resource.title, QStringLiteral("requirements.docx"));
    QCOMPARE(result.resource.location, QStringLiteral("E:/docs/requirements.docx"));
    QCOMPARE(result.anchor.name, QStringLiteral("Requirement 12"));
    QCOMPARE(result.anchor.targetApp, QStringLiteral("MS Word"));
    QCOMPARE(result.anchor.targetFile, QStringLiteral("E:/docs/requirements.docx"));
    QCOMPARE(result.anchor.locatorType, QStringLiteral("word.bookmark"));
    QCOMPARE(result.anchor.aliases, QStringList{QStringLiteral("word bookmark")});
    QCOMPARE(result.anchor.tags, QStringList{QStringLiteral("phase5")});
    QVERIFY(result.anchor.pinned);
    QVERIFY(result.anchor.createdAt.isValid());
    QVERIFY(result.anchor.updatedAt.isValid());

    const std::optional<Resource> stored = repository.findResource(result.resource.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->anchors.size(), 1);
    QCOMPARE(stored->anchors.first().id, result.anchor.id);
    QCOMPARE(stored->anchors.first().locatorJson, result.anchor.locatorJson);

    const QList<SearchResult> nameResults =
        repository.search(SearchQuery{QStringLiteral("Requirement 12")});
    QVERIFY(hasAnchorSearchResult(nameResults,
                                  QStringLiteral("anchor_name"),
                                  QStringLiteral("Requirement 12")));

    const QList<SearchResult> aliasResults =
        repository.search(SearchQuery{QStringLiteral("word bookmark")});
    QVERIFY(hasAnchorSearchResult(aliasResults,
                                  QStringLiteral("anchor_alias"),
                                  QStringLiteral("Requirement 12")));

    const QList<SearchResult> tagResults =
        repository.search(SearchQuery{QStringLiteral("phase5")});
    QVERIFY(hasAnchorSearchResult(tagResults,
                                  QStringLiteral("anchor_tag"),
                                  QStringLiteral("Requirement 12")));
}

void AnchorCaptureTest::rejectsInvalidManualWordBookmarkAnchorInputsWithoutSaving()
{
    InMemoryLibraryRepository repository;
    ManualWordAnchorCreationService service(repository);

    ManualWordAnchorCreationRequest request = validWordBookmarkCreationRequest();
    request.name = QStringLiteral(" ");
    ManualWordAnchorCreationResult result = service.createManualWordBookmarkAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Manual Word anchor name is missing"));

    request = validWordBookmarkCreationRequest();
    request.file.clear();
    result = service.createManualWordBookmarkAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Manual Word anchor file is missing"));

    request = validWordBookmarkCreationRequest();
    request.bookmark.clear();
    result = service.createManualWordBookmarkAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Word capture bookmark is missing"));

    request = validWordBookmarkCreationRequest();
    request.locatorType = QStringLiteral("word.heading");
    result = service.createManualWordBookmarkAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Word capture locator type is unsupported"));

    QVERIFY(repository.search(SearchQuery{}).isEmpty());
}

void AnchorCaptureTest::createsManualWordBookmarkAnchorCompatibleWithWordExecutor()
{
    InMemoryLibraryRepository repository;
    ManualWordAnchorCreationService service(repository);
    const ManualWordAnchorCreationResult result =
        service.createManualWordBookmarkAnchor(validWordBookmarkCreationRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QVERIFY(isWordAnchor(result.anchor));

    const WordJumpCommandResult command = buildWordJumpCommand(result.anchor, QString());

    QVERIFY2(command.success(), qPrintable(command.error));
    QCOMPARE(command.command.documentPath, QStringLiteral("E:/docs/requirements.docx"));
    QCOMPARE(command.command.locatorType, QStringLiteral("word.bookmark"));
    QCOMPARE(command.command.bookmarkName, QStringLiteral("Requirement_12"));
    QVERIFY(command.command.powerShellScript.contains(QStringLiteral("Documents.Open('E:/docs/requirements.docx')")));
    QVERIFY(command.command.powerShellScript.contains(QStringLiteral("Bookmarks.Item('Requirement_12')")));
}

void AnchorCaptureTest::buildsManualPowerPointShapeIdAnchor()
{
    const PowerPointCaptureResult result =
        captureManualPowerPointShapeAnchor(validPowerPointShapeIdCaptureRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.targetApp, QStringLiteral("PPT"));
    QCOMPARE(result.targetFile, QStringLiteral("E:/slides/process.pptx"));
    QCOMPARE(result.locatorType, QStringLiteral("powerpoint.shape"));
    QCOMPARE(result.slide, 12);
    QCOMPARE(result.shapeId, 42);
    QCOMPARE(result.shapeName, QStringLiteral("Valve A"));
    QCOMPARE(result.source, QStringLiteral("host"));

    QCOMPARE(result.anchor.type, AnchorType::Manual);
    QCOMPARE(result.anchor.name, QStringLiteral("Valve A callout"));
    QCOMPARE(result.anchor.target, QStringLiteral("Valve A callout"));
    QCOMPARE(result.anchor.targetApp, QStringLiteral("PPT"));
    QCOMPARE(result.anchor.targetFile, QStringLiteral("E:/slides/process.pptx"));
    QCOMPARE(result.anchor.locatorType, QStringLiteral("powerpoint.shape"));

    const QJsonDocument locatorDocument = QJsonDocument::fromJson(result.anchor.locatorJson.toUtf8());
    QVERIFY(locatorDocument.isObject());
    const QJsonObject locator = locatorDocument.object();
    QCOMPARE(locator.value(QStringLiteral("type")).toString(), QStringLiteral("powerpoint.shape"));
    QCOMPARE(locator.value(QStringLiteral("slide")).toInt(), 12);
    QCOMPARE(locator.value(QStringLiteral("slide_index")).toInt(), 12);
    QCOMPARE(locator.value(QStringLiteral("shape_id")).toInt(), 42);
    QCOMPARE(locator.value(QStringLiteral("shapeId")).toInt(), 42);
    QCOMPARE(locator.value(QStringLiteral("shape_name")).toString(), QStringLiteral("Valve A"));
    QCOMPARE(locator.value(QStringLiteral("shapeName")).toString(), QStringLiteral("Valve A"));
    QCOMPARE(locator.value(QStringLiteral("target_file")).toString(), QStringLiteral("E:/slides/process.pptx"));
    QCOMPARE(locator.value(QStringLiteral("source")).toString(), QStringLiteral("host"));
    QCOMPARE(locator.value(QStringLiteral("target_app")).toString(), QStringLiteral("PPT"));
}

void AnchorCaptureTest::buildsManualPowerPointShapeNameAnchor()
{
    const PowerPointCaptureResult result =
        captureManualPowerPointShapeAnchor(validPowerPointShapeNameCaptureRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.targetApp, QStringLiteral("Microsoft PowerPoint"));
    QCOMPARE(result.targetFile, QStringLiteral("E:/slides/process.pptx"));
    QCOMPARE(result.locatorType, QStringLiteral("powerpoint.shape"));
    QCOMPARE(result.slide, 7);
    QCOMPARE(result.shapeId, -1);
    QCOMPARE(result.shapeName, QStringLiteral("Pump Curve"));
    QCOMPARE(result.source, QStringLiteral("manual"));
    QCOMPARE(result.anchor.locatorType, QStringLiteral("powerpoint.shape"));

    const QJsonDocument locatorDocument = QJsonDocument::fromJson(result.anchor.locatorJson.toUtf8());
    QVERIFY(locatorDocument.isObject());
    const QJsonObject locator = locatorDocument.object();
    QCOMPARE(locator.value(QStringLiteral("slide")).toInt(), 7);
    QCOMPARE(locator.value(QStringLiteral("slide_index")).toInt(), 7);
    QVERIFY(locator.value(QStringLiteral("shape_id")).isUndefined());
    QCOMPARE(locator.value(QStringLiteral("shape_name")).toString(), QStringLiteral("Pump Curve"));
    QVERIFY(isPowerPointAnchor(result.anchor));

    const PowerPointJumpCommandResult command = buildPowerPointJumpCommand(result.anchor, QString());
    QVERIFY2(command.success(), qPrintable(command.error));
    QCOMPARE(command.command.presentationPath, QStringLiteral("E:/slides/process.pptx"));
    QCOMPARE(command.command.locatorType, QStringLiteral("powerpoint.shape"));
    QCOMPARE(command.command.slideIndex, 7);
    QCOMPARE(command.command.shapeId, -1);
    QCOMPARE(command.command.shapeName, QStringLiteral("Pump Curve"));
}

void AnchorCaptureTest::savesManualPowerPointShapeAnchorInRepository()
{
    InMemoryLibraryRepository repository;
    ManualPowerPointAnchorCreationService service(repository);

    const ManualPowerPointAnchorCreationResult result =
        service.createManualPowerPointShapeAnchor(validPowerPointShapeCreationRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QVERIFY(!result.resource.id.isEmpty());
    QCOMPARE(result.resource.kind, ResourceKind::File);
    QCOMPARE(result.resource.title, QStringLiteral("process.pptx"));
    QCOMPARE(result.resource.location, QStringLiteral("E:/slides/process.pptx"));
    QCOMPARE(result.anchor.name, QStringLiteral("Valve A callout"));
    QCOMPARE(result.anchor.targetApp, QStringLiteral("Microsoft PowerPoint"));
    QCOMPARE(result.anchor.targetFile, QStringLiteral("E:/slides/process.pptx"));
    QCOMPARE(result.anchor.locatorType, QStringLiteral("powerpoint.shape"));
    QCOMPARE(result.anchor.aliases, QStringList{QStringLiteral("slide callout")});
    QCOMPARE(result.anchor.tags, QStringList{QStringLiteral("phase5")});
    QVERIFY(result.anchor.pinned);
    QVERIFY(result.anchor.createdAt.isValid());
    QVERIFY(result.anchor.updatedAt.isValid());

    const std::optional<Resource> stored = repository.findResource(result.resource.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->anchors.size(), 1);
    QCOMPARE(stored->anchors.first().id, result.anchor.id);
    QCOMPARE(stored->anchors.first().locatorJson, result.anchor.locatorJson);

    const QList<SearchResult> nameResults =
        repository.search(SearchQuery{QStringLiteral("Valve A callout")});
    QVERIFY(hasAnchorSearchResult(nameResults,
                                  QStringLiteral("anchor_name"),
                                  QStringLiteral("Valve A callout")));

    const QList<SearchResult> aliasResults =
        repository.search(SearchQuery{QStringLiteral("slide callout")});
    QVERIFY(hasAnchorSearchResult(aliasResults,
                                  QStringLiteral("anchor_alias"),
                                  QStringLiteral("Valve A callout")));

    const QList<SearchResult> tagResults =
        repository.search(SearchQuery{QStringLiteral("phase5")});
    QVERIFY(hasAnchorSearchResult(tagResults,
                                  QStringLiteral("anchor_tag"),
                                  QStringLiteral("Valve A callout")));
}

void AnchorCaptureTest::rejectsInvalidManualPowerPointAnchorInputsWithoutSaving()
{
    InMemoryLibraryRepository repository;
    ManualPowerPointAnchorCreationService service(repository);

    ManualPowerPointAnchorCreationRequest request = validPowerPointShapeCreationRequest();
    request.name = QStringLiteral(" ");
    ManualPowerPointAnchorCreationResult result = service.createManualPowerPointShapeAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Manual PowerPoint anchor name is missing"));

    request = validPowerPointShapeCreationRequest();
    request.file.clear();
    result = service.createManualPowerPointShapeAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Manual PowerPoint anchor file is missing"));

    request = validPowerPointShapeCreationRequest();
    request.slide = 0;
    result = service.createManualPowerPointShapeAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("PowerPoint capture slide is missing"));

    request = validPowerPointShapeCreationRequest();
    request.shapeId = -1;
    request.shapeName.clear();
    result = service.createManualPowerPointShapeAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("PowerPoint capture shape id or name is missing"));

    request = validPowerPointShapeCreationRequest();
    request.locatorType = QStringLiteral("powerpoint.slide");
    result = service.createManualPowerPointShapeAnchor(request);
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("PowerPoint capture locator type is unsupported"));

    QVERIFY(repository.search(SearchQuery{}).isEmpty());
}

void AnchorCaptureTest::createsManualPowerPointShapeAnchorCompatibleWithPowerPointExecutor()
{
    InMemoryLibraryRepository repository;
    ManualPowerPointAnchorCreationService service(repository);
    const ManualPowerPointAnchorCreationResult result =
        service.createManualPowerPointShapeAnchor(validPowerPointShapeCreationRequest());

    QVERIFY2(result.success(), qPrintable(result.error));
    QVERIFY(isPowerPointAnchor(result.anchor));

    const PowerPointJumpCommandResult command =
        buildPowerPointJumpCommand(result.anchor, QString());

    QVERIFY2(command.success(), qPrintable(command.error));
    QCOMPARE(command.command.presentationPath, QStringLiteral("E:/slides/process.pptx"));
    QCOMPARE(command.command.locatorType, QStringLiteral("powerpoint.shape"));
    QCOMPARE(command.command.slideIndex, 12);
    QCOMPARE(command.command.shapeId, 42);
    QVERIFY(command.command.shapeName.isEmpty());
    QVERIFY(command.command.powerShellScript.contains(QStringLiteral("Presentations.Open('E:/slides/process.pptx')")));
    QVERIFY(command.command.powerShellScript.contains(QStringLiteral("Slides.Item(12)")));
    QVERIFY(command.command.powerShellScript.contains(QStringLiteral("Shapes.FindById(42)")));
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
    QCOMPARE(command.command.action, QStringLiteral("page=12;zoom=250;highlight=420,780,860,920;usept=yes"));
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

void AnchorCaptureTest::matchesForegroundPdfXChangeTitleToUniqueIndexedPdf()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("axi-spec");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("AMBA AXI Protocol Spec");
    resource.location = QStringLiteral("E:/docs/IHI0022K_amba_axi_protocol_spec[axi].pdf");
    QVERIFY(repository.upsertResource(resource));

    ForegroundAppWindowContext context;
    context.windowTitle = QStringLiteral("IHI0022K_amba_axi_protocol_spec[axi] - PDF-XChange Editor");
    context.processName = QStringLiteral("PDFXEdit.exe");

    const PdfXChangeForegroundCaptureResult result =
        capturePdfXChangeForegroundContext(repository, context);

    QVERIFY2(result.success(), qPrintable(result.status));
    QVERIFY(result.recognizedPdfXChange);
    QVERIFY(result.matchedResource);
    QCOMPARE(result.documentTitle, QStringLiteral("IHI0022K_amba_axi_protocol_spec[axi]"));
    QCOMPARE(result.matchedResourceId, resource.id);
    QCOMPARE(result.request.name, QStringLiteral("IHI0022K_amba_axi_protocol_spec[axi]"));
    QCOMPARE(result.request.file, resource.location);
    QCOMPARE(result.request.page, 1);
    QCOMPARE(result.request.rect.left, 0.0);
    QCOMPARE(result.request.rect.top, 0.0);
    QCOMPARE(result.request.rect.right, 612.0);
    QCOMPARE(result.request.rect.bottom, 792.0);
    QCOMPARE(result.request.source, QStringLiteral("foreground-pdfxchange-fallback"));
    QCOMPARE(result.request.targetApp, QStringLiteral("PDF-XChange"));

    QCOMPARE(pdfXChangeDocumentTitleFromWindowTitle(
                 QStringLiteral("*IHI0022K_amba_axi_protocol_spec[axi] - PDF-XChange Editor")),
             QStringLiteral("IHI0022K_amba_axi_protocol_spec[axi]"));
}

void AnchorCaptureTest::rejectsForegroundPdfXChangeTitleWithoutIndexedPdfMatch()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("other-spec");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Other Spec");
    resource.location = QStringLiteral("E:/docs/other.pdf");
    QVERIFY(repository.upsertResource(resource));

    ForegroundAppWindowContext context;
    context.windowTitle = QStringLiteral("missing-spec - PDF-XChange Editor");
    context.processName = QStringLiteral("PXCEditor.exe");

    const PdfXChangeForegroundCaptureResult result =
        capturePdfXChangeForegroundContext(repository, context);

    QVERIFY(!result.success());
    QVERIFY(result.recognizedPdfXChange);
    QVERIFY(!result.matchedResource);
    QCOMPARE(result.documentTitle, QStringLiteral("missing-spec"));
    QVERIFY(result.status.contains(QStringLiteral("not matched to a unique indexed PDF")));
    QVERIFY(result.request.file.isEmpty());
}

void AnchorCaptureTest::rejectsForegroundPdfXChangeTitleWithMultipleIndexedPdfMatches()
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
    context.windowTitle = QStringLiteral("clock - PDF-XChange Editor");
    context.processName = QStringLiteral("PDFXEdit.exe");

    const PdfXChangeForegroundCaptureResult result =
        capturePdfXChangeForegroundContext(repository, context);

    QVERIFY(!result.success());
    QVERIFY(result.recognizedPdfXChange);
    QVERIFY(!result.matchedResource);
    QCOMPARE(result.documentTitle, QStringLiteral("clock"));
    QVERIFY(result.status.contains(QStringLiteral("matches multiple indexed PDFs")));
    QVERIFY(result.request.file.isEmpty());
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
