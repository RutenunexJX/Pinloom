#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/core/ExcelCommand.h"
#include "pinloom/core/PdfXChangeCommand.h"
#include "pinloom/core/Schema.h"

#include <QByteArray>
#include <QDateTime>
#include <QTest>
#include <optional>

using namespace Pinloom;

class CoreSmokeTest : public QObject {
    Q_OBJECT

private slots:
    void searchesAliasesAndTags();
    void persistsAndSearchesAnchorLocatorFields();
    void buildsPdfXChangeRectCommand();
    void buildsPdfXChangePageCommandFromLegacyAnchor();
    void reportsMissingPdfXChangeTargetPath();
    void resolvesPdfXChangeExecutableFromEnvironment();
    void buildsExcelRangeCommand();
    void buildsExcelNamedRangeCommand();
    void reportsExcelCommandInputErrors();
    void recognizesExcelTargetAppAliases();
    void ranksAnchorLocatorMatchesByNameAliasTagAndMetadata();
    void normalizesLegacyTextResourceInputs();
    void ranksAnchorBeforePathMatches();
    void ranksExactMatchesWithinMatchType();
    void ranksPinnedAndOpenedResourcesWithinMatchType();
    void filtersByRequiredLocationPrefixes();
    void filtersByRequiredResourceKinds();
    void ranksContextResourcesWithinMatchType();
    void ranksRelatedContextResourcesWithinMatchType();
    void ranksOpenedAnchorsWithinAnchorMatches();
    void searchesExtractedContent();
    void exposesSqliteFts5SchemaDraft();
};

void CoreSmokeTest::searchesAliasesAndTags()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("pcie-notes");
    resource.kind = ResourceKind::File;
    resource.title = QStringLiteral("PCIe UART Bringup Notes");
    resource.location = QStringLiteral("docs/pcie.md");
    resource.tags = {QStringLiteral("fpga"), QStringLiteral("uart")};
    resource.aliases = {QStringLiteral("serial debug")};

    QVERIFY(repository.upsertResource(resource));

    const QList<SearchResult> aliasResults = repository.search(SearchQuery{QStringLiteral("serial")});
    QCOMPARE(aliasResults.size(), 1);
    QCOMPARE(aliasResults.first().matchedField, QStringLiteral("alias"));

    SearchQuery taggedQuery;
    taggedQuery.text = QStringLiteral("bringup");
    taggedQuery.requiredTags = {QStringLiteral("fpga")};
    const QList<SearchResult> tagResults = repository.search(taggedQuery);
    QCOMPARE(tagResults.size(), 1);
}

void CoreSmokeTest::persistsAndSearchesAnchorLocatorFields()
{
    InMemoryLibraryRepository repository;

    Anchor anchor;
    anchor.type = AnchorType::Manual;
    anchor.id = QStringLiteral("anchor:clock-domain");
    anchor.name = QStringLiteral("Clock domain window");
    anchor.targetApp = QStringLiteral("PDF-XChange");
    anchor.targetFile = QStringLiteral("E:/specs/clocking.pdf");
    anchor.locatorType = QStringLiteral("pdfxchange.rect");
    anchor.locatorJson = QStringLiteral("{\"page\":12,\"rect\":[420,860,780,920],\"zoom\":250}");
    anchor.aliases = {QStringLiteral("cdc zoom")};
    anchor.tags = {QStringLiteral("review-point")};
    anchor.pinned = true;
    anchor.createdAt = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    anchor.updatedAt = QDateTime::fromString(QStringLiteral("2026-01-02T00:00:00Z"), Qt::ISODate);
    anchor.usedAt = QDateTime::currentDateTimeUtc();

    Resource resource;
    resource.id = QStringLiteral("clocking-pdf");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Clocking PDF");
    resource.location = QStringLiteral("E:/specs/clocking.pdf");
    resource.anchors = {anchor};

    QVERIFY(repository.upsertResource(resource));

    const std::optional<Resource> stored = repository.findResource(resource.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->anchors.size(), 1);
    QCOMPARE(stored->anchors.first().id, anchor.id);
    QCOMPARE(stored->anchors.first().name, anchor.name);
    QCOMPARE(stored->anchors.first().target, anchor.name);
    QCOMPARE(stored->anchors.first().targetApp, anchor.targetApp);
    QCOMPARE(stored->anchors.first().targetFile, anchor.targetFile);
    QCOMPARE(stored->anchors.first().locatorType, anchor.locatorType);
    QCOMPARE(stored->anchors.first().locatorJson, anchor.locatorJson);
    QCOMPARE(stored->anchors.first().aliases, anchor.aliases);
    QCOMPARE(stored->anchors.first().tags, anchor.tags);
    QVERIFY(stored->anchors.first().pinned);

    const QList<SearchResult> nameResults = repository.search(SearchQuery{QStringLiteral("Clock domain window")});
    QCOMPARE(nameResults.size(), 1);
    QCOMPARE(nameResults.first().matchedField, QStringLiteral("anchor_name"));

    const QList<SearchResult> aliasResults = repository.search(SearchQuery{QStringLiteral("cdc zoom")});
    QCOMPARE(aliasResults.size(), 1);
    QCOMPARE(aliasResults.first().matchedField, QStringLiteral("anchor_alias"));

    const QList<SearchResult> tagResults = repository.search(SearchQuery{QStringLiteral("review-point")});
    QCOMPARE(tagResults.size(), 1);
    QCOMPARE(tagResults.first().matchedField, QStringLiteral("anchor_tag"));

    const QList<SearchResult> metadataResults = repository.search(SearchQuery{QStringLiteral("pdfxchange.rect")});
    QCOMPARE(metadataResults.size(), 1);
    QCOMPARE(metadataResults.first().matchedField, QStringLiteral("anchor_metadata"));
}

void CoreSmokeTest::buildsPdfXChangeRectCommand()
{
    Anchor anchor;
    anchor.targetApp = QStringLiteral("PDF-XChange");
    anchor.targetFile = QStringLiteral("E:/docs/clock.pdf");
    anchor.locatorType = QStringLiteral("pdfxchange.rect");
    anchor.locatorJson = QStringLiteral("{\"type\":\"pdfxchange.rect\",\"page\":12,\"rect\":[420,860,780,920],\"zoom\":250,\"unit\":\"pt\"}");

    QVERIFY(isPdfXChangeAnchor(anchor));
    const PdfXChangeCommandResult result =
        buildPdfXChangeCommand(anchor, QString(), QStringLiteral("C:/Tools/PDFXEdit.exe"));

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.executablePath, QStringLiteral("C:/Tools/PDFXEdit.exe"));
    QCOMPARE(result.command.filePath, anchor.targetFile);
    QCOMPARE(result.command.action, QStringLiteral("page=12;zoom=250;highlight=420,860,780,920;usept=yes"));
    QCOMPARE(result.command.arguments,
             QStringList({QStringLiteral("/A"),
                          QStringLiteral("page=12;zoom=250;highlight=420,860,780,920;usept=yes"),
                          anchor.targetFile}));
}

void CoreSmokeTest::buildsPdfXChangePageCommandFromLegacyAnchor()
{
    Anchor anchor;
    anchor.type = AnchorType::PdfPage;
    anchor.targetApp = QStringLiteral("pdf");
    anchor.page = 3;
    anchor.locatorJson = QStringLiteral("{\"zoom\":175}");

    QVERIFY(isPdfXChangeAnchor(anchor));
    const PdfXChangeCommandResult result =
        buildPdfXChangeCommand(anchor,
                               QStringLiteral("E:/docs/spec.pdf"),
                               QStringLiteral("C:/Tools/PDFXEdit.exe"));

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.filePath, QStringLiteral("E:/docs/spec.pdf"));
    QCOMPARE(result.command.action, QStringLiteral("page=3;zoom=175"));
    QCOMPARE(result.command.arguments,
             QStringList({QStringLiteral("/A"),
                          QStringLiteral("page=3;zoom=175"),
                          QStringLiteral("E:/docs/spec.pdf")}));
}

void CoreSmokeTest::reportsMissingPdfXChangeTargetPath()
{
    Anchor anchor;
    anchor.targetApp = QStringLiteral("pdfxchange");
    anchor.locatorType = QStringLiteral("pdfxchange.page");
    anchor.locatorJson = QStringLiteral("{\"page\":4}");

    const PdfXChangeCommandResult result =
        buildPdfXChangeCommand(anchor, QString(), QStringLiteral("C:/Tools/PDFXEdit.exe"));

    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("PDF-XChange target file is missing"));
}

void CoreSmokeTest::resolvesPdfXChangeExecutableFromEnvironment()
{
    const bool hadValue = qEnvironmentVariableIsSet("PINLOOM_PDFXCHANGE_PATH");
    const QByteArray previous = qgetenv("PINLOOM_PDFXCHANGE_PATH");

    QVERIFY(qputenv("PINLOOM_PDFXCHANGE_PATH", "C:/Portable PDF/PDFXEdit.exe"));
    const QString resolved = resolvePdfXChangeExecutablePath();

    if (hadValue) {
        QVERIFY(qputenv("PINLOOM_PDFXCHANGE_PATH", previous));
    } else {
        qunsetenv("PINLOOM_PDFXCHANGE_PATH");
    }

    QCOMPARE(resolved, QStringLiteral("C:/Portable PDF/PDFXEdit.exe"));
}

void CoreSmokeTest::buildsExcelRangeCommand()
{
    Anchor anchor;
    anchor.targetApp = QStringLiteral("Microsoft Excel");
    anchor.targetFile = QStringLiteral("E:/books/budget.xlsx");
    anchor.locatorType = QStringLiteral("excel.range");
    anchor.locatorJson = QStringLiteral("{\"type\":\"excel.range\",\"sheet\":\"Sheet1\",\"range\":\"B12:D18\"}");

    QVERIFY(isExcelAnchor(anchor));
    const ExcelJumpCommandResult result = buildExcelJumpCommand(anchor, QString());

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.executablePath, QStringLiteral("powershell.exe"));
    QCOMPARE(result.command.workbookPath, anchor.targetFile);
    QCOMPARE(result.command.locatorType, QStringLiteral("excel.range"));
    QCOMPARE(result.command.sheetName, QStringLiteral("Sheet1"));
    QCOMPARE(result.command.rangeAddress, QStringLiteral("B12:D18"));
    QVERIFY(result.command.namedRange.isEmpty());
    QCOMPARE(result.command.arguments.at(0), QStringLiteral("-NoProfile"));
    QVERIFY(result.command.arguments.contains(QStringLiteral("-Command")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("Workbooks.Open('E:/books/budget.xlsx')")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("Worksheets.Item('Sheet1')")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("Range('B12:D18')")));
}

void CoreSmokeTest::buildsExcelNamedRangeCommand()
{
    Anchor anchor;
    anchor.targetApp = QStringLiteral("Excel");
    anchor.locatorJson = QStringLiteral("{\"type\":\"excel.name\",\"name\":\"RevenueTable\",\"workbook\":\"E:/books/revenue.xlsx\"}");

    QVERIFY(isExcelAnchor(anchor));
    const ExcelJumpCommandResult result = buildExcelJumpCommand(anchor, QStringLiteral("E:/books/fallback.xlsx"));

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.workbookPath, QStringLiteral("E:/books/revenue.xlsx"));
    QCOMPARE(result.command.locatorType, QStringLiteral("excel.name"));
    QVERIFY(result.command.sheetName.isEmpty());
    QVERIFY(result.command.rangeAddress.isEmpty());
    QCOMPARE(result.command.namedRange, QStringLiteral("RevenueTable"));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("Names.Item('RevenueTable').RefersToRange")));

    Anchor targetFileFallback = anchor;
    targetFileFallback.locatorJson = QStringLiteral("{\"type\":\"excel.name\",\"name\":\"RevenueTable\",\"target_file\":\"E:/books/target-file.xlsx\"}");
    const ExcelJumpCommandResult targetFileResult = buildExcelJumpCommand(targetFileFallback, QString());
    QVERIFY2(targetFileResult.success(), qPrintable(targetFileResult.error));
    QCOMPARE(targetFileResult.command.workbookPath, QStringLiteral("E:/books/target-file.xlsx"));

    Anchor anchorFieldWins = anchor;
    anchorFieldWins.targetFile = QStringLiteral("E:/books/anchor-field.xlsx");
    const ExcelJumpCommandResult anchorFieldResult = buildExcelJumpCommand(anchorFieldWins, QString());
    QVERIFY2(anchorFieldResult.success(), qPrintable(anchorFieldResult.error));
    QCOMPARE(anchorFieldResult.command.workbookPath, QStringLiteral("E:/books/anchor-field.xlsx"));
}

void CoreSmokeTest::reportsExcelCommandInputErrors()
{
    Anchor missingFile;
    missingFile.targetApp = QStringLiteral("Excel");
    missingFile.locatorType = QStringLiteral("excel.range");
    missingFile.locatorJson = QStringLiteral("{\"sheet\":\"Sheet1\",\"range\":\"B12\"}");
    ExcelJumpCommandResult result = buildExcelJumpCommand(missingFile, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Excel workbook path is missing"));

    Anchor invalidJson = missingFile;
    invalidJson.targetFile = QStringLiteral("E:/books/budget.xlsx");
    invalidJson.locatorJson = QStringLiteral("{\"sheet\":");
    result = buildExcelJumpCommand(invalidJson, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Excel locator JSON is invalid"));

    Anchor unsupported = invalidJson;
    unsupported.locatorType = QStringLiteral("excel.cell");
    unsupported.locatorJson = QStringLiteral("{\"sheet\":\"Sheet1\",\"range\":\"B12\"}");
    result = buildExcelJumpCommand(unsupported, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Excel locator type is unsupported"));

    Anchor missingSheet = invalidJson;
    missingSheet.locatorJson = QStringLiteral("{\"range\":\"B12\"}");
    result = buildExcelJumpCommand(missingSheet, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Excel range sheet is missing"));

    Anchor missingRange = invalidJson;
    missingRange.locatorJson = QStringLiteral("{\"sheet\":\"Sheet1\"}");
    result = buildExcelJumpCommand(missingRange, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Excel range address is missing"));

    Anchor missingName = invalidJson;
    missingName.locatorType = QStringLiteral("excel.name");
    missingName.locatorJson = QStringLiteral("{\"type\":\"excel.name\"}");
    result = buildExcelJumpCommand(missingName, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Excel named range is missing"));
}

void CoreSmokeTest::recognizesExcelTargetAppAliases()
{
    Anchor excel;
    excel.targetApp = QStringLiteral("eXcEl");
    QVERIFY(isExcelAnchor(excel));

    excel.targetApp = QStringLiteral("Microsoft Excel");
    QVERIFY(isExcelAnchor(excel));

    excel.targetApp = QStringLiteral("MS Excel");
    QVERIFY(isExcelAnchor(excel));

    excel.targetApp = QStringLiteral("Word");
    QVERIFY(!isExcelAnchor(excel));

    excel.targetApp.clear();
    excel.locatorType = QStringLiteral("EXCEL.RANGE");
    QVERIFY(isExcelAnchor(excel));
}

void CoreSmokeTest::ranksAnchorLocatorMatchesByNameAliasTagAndMetadata()
{
    InMemoryLibraryRepository repository;

    Resource nameResource;
    nameResource.id = QStringLiteral("name-anchor");
    nameResource.kind = ResourceKind::ManualAnchor;
    nameResource.title = QStringLiteral("Zulu");
    nameResource.location = QStringLiteral("name.pinloom");
    Anchor nameAnchor;
    nameAnchor.type = AnchorType::Manual;
    nameAnchor.name = QStringLiteral("shared");
    nameResource.anchors = {nameAnchor};
    QVERIFY(repository.upsertResource(nameResource));

    Resource aliasResource;
    aliasResource.id = QStringLiteral("alias-anchor");
    aliasResource.kind = ResourceKind::ManualAnchor;
    aliasResource.title = QStringLiteral("Alpha");
    aliasResource.location = QStringLiteral("alias.pinloom");
    Anchor aliasAnchor;
    aliasAnchor.type = AnchorType::Manual;
    aliasAnchor.name = QStringLiteral("alias carrier");
    aliasAnchor.aliases = {QStringLiteral("shared")};
    aliasResource.anchors = {aliasAnchor};
    QVERIFY(repository.upsertResource(aliasResource));

    Resource tagResource;
    tagResource.id = QStringLiteral("tag-anchor");
    tagResource.kind = ResourceKind::ManualAnchor;
    tagResource.title = QStringLiteral("Beta");
    tagResource.location = QStringLiteral("tag.pinloom");
    Anchor tagAnchor;
    tagAnchor.type = AnchorType::Manual;
    tagAnchor.name = QStringLiteral("tag carrier");
    tagAnchor.tags = {QStringLiteral("shared")};
    tagResource.anchors = {tagAnchor};
    QVERIFY(repository.upsertResource(tagResource));

    Resource hotMetadataResource;
    hotMetadataResource.id = QStringLiteral("hot-metadata-anchor");
    hotMetadataResource.kind = ResourceKind::ManualAnchor;
    hotMetadataResource.title = QStringLiteral("Gamma");
    hotMetadataResource.location = QStringLiteral("hot-meta.pinloom");
    Anchor hotMetadataAnchor;
    hotMetadataAnchor.type = AnchorType::Manual;
    hotMetadataAnchor.name = QStringLiteral("hot metadata carrier");
    hotMetadataAnchor.targetFile = QStringLiteral("E:/targets/shared-target.pdf");
    hotMetadataAnchor.pinned = true;
    hotMetadataResource.anchors = {hotMetadataAnchor};
    QVERIFY(repository.upsertResource(hotMetadataResource));

    Resource coldMetadataResource;
    coldMetadataResource.id = QStringLiteral("cold-metadata-anchor");
    coldMetadataResource.kind = ResourceKind::ManualAnchor;
    coldMetadataResource.title = QStringLiteral("Delta");
    coldMetadataResource.location = QStringLiteral("cold-meta.pinloom");
    Anchor coldMetadataAnchor;
    coldMetadataAnchor.type = AnchorType::Manual;
    coldMetadataAnchor.name = QStringLiteral("cold metadata carrier");
    coldMetadataAnchor.targetFile = QStringLiteral("E:/targets/shared-target.pdf");
    coldMetadataResource.anchors = {coldMetadataAnchor};
    QVERIFY(repository.upsertResource(coldMetadataResource));

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("shared")});
    QCOMPARE(results.size(), 5);
    QCOMPARE(results.at(0).resource.id, nameResource.id);
    QCOMPARE(results.at(0).matchedField, QStringLiteral("anchor_name"));
    QCOMPARE(results.at(1).resource.id, aliasResource.id);
    QCOMPARE(results.at(1).matchedField, QStringLiteral("anchor_alias"));
    QCOMPARE(results.at(2).resource.id, tagResource.id);
    QCOMPARE(results.at(2).matchedField, QStringLiteral("anchor_tag"));
    QCOMPARE(results.at(3).resource.id, hotMetadataResource.id);
    QCOMPARE(results.at(3).matchedField, QStringLiteral("anchor_metadata"));
    QCOMPARE(results.at(4).resource.id, coldMetadataResource.id);
    QCOMPARE(results.at(4).matchedField, QStringLiteral("anchor_metadata"));
}

void CoreSmokeTest::normalizesLegacyTextResourceInputs()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("legacy-text-input");
    resource.kind = ResourceKind::Markdown;
    resource.title = QStringLiteral("Legacy Text Input");
    resource.location = QStringLiteral("docs/legacy.md");
    resource.anchors = {
        Anchor{AnchorType::MarkdownHeading, QStringLiteral("Legacy Heading"), 3},
        Anchor{AnchorType::MarkdownBlock, QStringLiteral("legacy-block"), 9}
    };
    QVERIFY(repository.upsertResource(resource));

    const std::optional<Resource> stored = repository.findResource(resource.id);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->kind, ResourceKind::File);
    QCOMPARE(stored->anchors.size(), 2);
    QCOMPARE(stored->anchors.at(0).type, AnchorType::TextHeading);
    QCOMPARE(stored->anchors.at(1).type, AnchorType::TextBlock);

    SearchQuery query;
    query.text = QStringLiteral("Legacy Heading");
    query.requiredKinds = {ResourceKind::Markdown};
    const QList<SearchResult> results = repository.search(query);
    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(results.first().matchedAnchor->type, AnchorType::TextHeading);

    QVERIFY(repository.recordAnchorOpen(resource.id, resource.anchors.first()));
    Anchor normalizedHeading = resource.anchors.first();
    normalizedHeading.type = AnchorType::TextHeading;
    const std::optional<AnchorUsage> usage = repository.anchorUsage(resource.id, normalizedHeading);
    QVERIFY(usage.has_value());
    QCOMPARE(usage->anchor.type, AnchorType::TextHeading);
}

void CoreSmokeTest::ranksAnchorBeforePathMatches()
{
    InMemoryLibraryRepository repository;

    Resource anchored;
    anchored.id = QStringLiteral("anchored");
    anchored.kind = ResourceKind::File;
    anchored.title = QStringLiteral("note.md");
    anchored.location = QStringLiteral("E:/test_dir/note.md");
    anchored.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("test"), 12}};
    QVERIFY(repository.upsertResource(anchored));

    Resource pathOnly;
    pathOnly.id = QStringLiteral("path-only");
    pathOnly.kind = ResourceKind::Pdf;
    pathOnly.title = QStringLiteral("ISO.pdf");
    pathOnly.location = QStringLiteral("E:/test_dir/ISO.pdf");
    QVERIFY(repository.upsertResource(pathOnly));

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("test")});
    QVERIFY(results.size() >= 2);
    QCOMPARE(results.first().matchedField, QStringLiteral("anchor"));
    QVERIFY(results.first().matchedAnchor.has_value());
}

void CoreSmokeTest::ranksExactMatchesWithinMatchType()
{
    InMemoryLibraryRepository repository;

    Resource partialTitle;
    partialTitle.id = QStringLiteral("partial-title");
    partialTitle.kind = ResourceKind::File;
    partialTitle.title = QStringLiteral("UART Bringup");
    partialTitle.location = QStringLiteral("partial.md");
    QVERIFY(repository.upsertResource(partialTitle));

    Resource exactTitle;
    exactTitle.id = QStringLiteral("exact-title");
    exactTitle.kind = ResourceKind::File;
    exactTitle.title = QStringLiteral("UART");
    exactTitle.location = QStringLiteral("exact.md");
    QVERIFY(repository.upsertResource(exactTitle));

    const QList<SearchResult> titleResults = repository.search(SearchQuery{QStringLiteral("UART")});
    QCOMPARE(titleResults.size(), 2);
    QCOMPARE(titleResults.first().resource.id, exactTitle.id);
    QCOMPARE(titleResults.first().matchedField, QStringLiteral("title"));
    QVERIFY(titleResults.first().score < titleResults.at(1).score);

    Resource partialAnchor;
    partialAnchor.id = QStringLiteral("partial-anchor");
    partialAnchor.kind = ResourceKind::File;
    partialAnchor.title = QStringLiteral("a.md");
    partialAnchor.location = QStringLiteral("a.md");
    partialAnchor.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Power sequencing"), 7}};
    QVERIFY(repository.upsertResource(partialAnchor));

    Resource exactAnchor;
    exactAnchor.id = QStringLiteral("exact-anchor");
    exactAnchor.kind = ResourceKind::File;
    exactAnchor.title = QStringLiteral("b.md");
    exactAnchor.location = QStringLiteral("b.md");
    exactAnchor.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Power"), 3}};
    QVERIFY(repository.upsertResource(exactAnchor));

    const QList<SearchResult> anchorResults = repository.search(SearchQuery{QStringLiteral("Power")});
    QVERIFY(anchorResults.size() >= 2);
    QCOMPARE(anchorResults.first().resource.id, exactAnchor.id);
    QCOMPARE(anchorResults.first().matchedField, QStringLiteral("anchor"));
    QVERIFY(anchorResults.first().matchedAnchor.has_value());
    QVERIFY(anchorResults.first().score < anchorResults.at(1).score);
}

void CoreSmokeTest::ranksPinnedAndOpenedResourcesWithinMatchType()
{
    InMemoryLibraryRepository repository;

    Resource cold;
    cold.id = QStringLiteral("cold");
    cold.kind = ResourceKind::File;
    cold.title = QStringLiteral("UART Alpha");
    cold.location = QStringLiteral("alpha.md");
    QVERIFY(repository.upsertResource(cold));

    Resource hot;
    hot.id = QStringLiteral("hot");
    hot.kind = ResourceKind::File;
    hot.title = QStringLiteral("UART Zulu");
    hot.location = QStringLiteral("zulu.md");
    QVERIFY(repository.upsertResource(hot));

    QVERIFY(repository.recordResourceOpen(hot.id));
    QVERIFY(repository.recordResourceOpen(hot.id));
    QVERIFY(repository.setResourcePinned(hot.id, true));

    const std::optional<ResourceUsage> usage = repository.resourceUsage(hot.id);
    QVERIFY(usage.has_value());
    QCOMPARE(usage->openCount, 2);
    QVERIFY(usage->lastOpenedAt.isValid());
    QVERIFY(usage->pinned);

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("UART")});
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, hot.id);
    QCOMPARE(results.first().matchedField, QStringLiteral("title"));
}

void CoreSmokeTest::filtersByRequiredLocationPrefixes()
{
    InMemoryLibraryRepository repository;

    Resource project;
    project.id = QStringLiteral("project");
    project.kind = ResourceKind::File;
    project.title = QStringLiteral("UART Project Note");
    project.location = QStringLiteral("E:/workspace/project/notes/uart.md");
    QVERIFY(repository.upsertResource(project));

    Resource other;
    other.id = QStringLiteral("other");
    other.kind = ResourceKind::File;
    other.title = QStringLiteral("UART Other Note");
    other.location = QStringLiteral("E:/workspace/other/uart.md");
    QVERIFY(repository.upsertResource(other));

    SearchQuery query;
    query.text = QStringLiteral("UART");
    query.requiredLocationPrefixes = {QStringLiteral("E:/workspace/project")};

    const QList<SearchResult> results = repository.search(query);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().resource.id, project.id);

    query.requiredLocationPrefixes = {QStringLiteral("E:/workspace/missing")};
    QVERIFY(repository.search(query).isEmpty());
}

void CoreSmokeTest::filtersByRequiredResourceKinds()
{
    InMemoryLibraryRepository repository;

    Resource note;
    note.id = QStringLiteral("note");
    note.kind = ResourceKind::File;
    note.title = QStringLiteral("UART Note");
    note.location = QStringLiteral("note.md");
    note.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Dock handoff"), 4}};
    QVERIFY(repository.upsertResource(note));

    Resource link;
    link.id = QStringLiteral("link");
    link.kind = ResourceKind::Url;
    link.title = QStringLiteral("UART Link");
    link.location = QStringLiteral("https://docs.example.com/uart");
    link.anchors = {Anchor{AnchorType::UrlFragment, QStringLiteral("Dock handoff")}};
    QVERIFY(repository.upsertResource(link));

    SearchQuery query;
    query.text = QStringLiteral("UART");
    query.requiredKinds = {ResourceKind::Url};

    const QList<SearchResult> results = repository.search(query);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().resource.id, link.id);

    SearchQuery anchorQuery;
    anchorQuery.text = QStringLiteral("Dock handoff");
    anchorQuery.requiredKinds = {ResourceKind::File};

    const QList<SearchResult> anchorResults = repository.search(anchorQuery);
    QCOMPARE(anchorResults.size(), 1);
    QCOMPARE(anchorResults.first().resource.id, note.id);
    QVERIFY(anchorResults.first().matchedAnchor.has_value());
}

void CoreSmokeTest::ranksContextResourcesWithinMatchType()
{
    InMemoryLibraryRepository repository;

    Resource generic;
    generic.id = QStringLiteral("generic");
    generic.kind = ResourceKind::File;
    generic.title = QStringLiteral("UART Alpha");
    generic.location = QStringLiteral("E:/workspace/other/alpha.md");
    generic.tags = {QStringLiteral("notes")};
    QVERIFY(repository.upsertResource(generic));

    Resource contextual;
    contextual.id = QStringLiteral("contextual");
    contextual.kind = ResourceKind::File;
    contextual.title = QStringLiteral("UART Zulu");
    contextual.location = QStringLiteral("E:/workspace/project/zulu.md");
    contextual.tags = {QStringLiteral("pcie")};
    QVERIFY(repository.upsertResource(contextual));

    SearchQuery query;
    query.text = QStringLiteral("UART");
    query.contextTags = {QStringLiteral("pcie")};
    query.contextLocationPrefixes = {QStringLiteral("E:/workspace/project")};

    const QList<SearchResult> results = repository.search(query);
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, contextual.id);
    QCOMPARE(results.first().matchedField, QStringLiteral("title"));
}

void CoreSmokeTest::ranksRelatedContextResourcesWithinMatchType()
{
    InMemoryLibraryRepository repository;

    Resource active;
    active.id = QStringLiteral("active");
    active.kind = ResourceKind::File;
    active.title = QStringLiteral("Current Note");
    active.location = QStringLiteral("E:/workspace/current.md");
    QVERIFY(repository.upsertResource(active));

    Resource generic;
    generic.id = QStringLiteral("generic");
    generic.kind = ResourceKind::File;
    generic.title = QStringLiteral("UART Alpha");
    generic.location = QStringLiteral("E:/workspace/other/alpha.md");
    QVERIFY(repository.upsertResource(generic));

    Resource related;
    related.id = QStringLiteral("related");
    related.kind = ResourceKind::File;
    related.title = QStringLiteral("UART Zulu");
    related.location = QStringLiteral("E:/workspace/project/zulu.md");
    QVERIFY(repository.upsertResource(related));

    ResourceRelation relation;
    relation.sourceResourceId = active.id;
    relation.targetResourceId = related.id;
    relation.label = QStringLiteral("related-to");
    relation.note = QStringLiteral("active relation edge");
    QVERIFY(repository.upsertResourceRelation(relation));

    SearchQuery query;
    query.text = QStringLiteral("UART");
    QList<SearchResult> results = repository.search(query);
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, generic.id);

    query.contextResourceIds = {active.id};
    results = repository.search(query);
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, related.id);
    QCOMPARE(results.first().matchedContextResourceId, active.id);
    QCOMPARE(results.first().matchedContextRelationLabel, QStringLiteral("related-to"));
    QCOMPARE(results.first().matchedContextRelationNote, QStringLiteral("active relation edge"));

    query.contextRelationLabels = {QStringLiteral("file-reference")};
    results = repository.search(query);
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, generic.id);
    QVERIFY(results.first().matchedContextRelationLabel.isEmpty());
    QVERIFY(results.first().matchedContextRelationNote.isEmpty());

    query.contextRelationLabels = {QStringLiteral("RELATED-TO")};
    results = repository.search(query);
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, related.id);
    QCOMPARE(results.first().matchedContextRelationLabel, QStringLiteral("related-to"));
    QCOMPARE(results.first().matchedContextRelationNote, QStringLiteral("active relation edge"));
}

void CoreSmokeTest::ranksOpenedAnchorsWithinAnchorMatches()
{
    InMemoryLibraryRepository repository;

    Resource cold;
    cold.id = QStringLiteral("cold-anchor");
    cold.kind = ResourceKind::File;
    cold.title = QStringLiteral("Alpha");
    cold.location = QStringLiteral("alpha.md");
    cold.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Power rail"), 1}};
    QVERIFY(repository.upsertResource(cold));

    Resource hot;
    hot.id = QStringLiteral("hot-anchor");
    hot.kind = ResourceKind::File;
    hot.title = QStringLiteral("Zulu");
    hot.location = QStringLiteral("zulu.md");
    hot.anchors = {Anchor{AnchorType::TextHeading, QStringLiteral("Power rail"), 2}};
    QVERIFY(repository.upsertResource(hot));

    QVERIFY(repository.recordAnchorOpen(hot.id, hot.anchors.first()));
    QVERIFY(repository.recordAnchorOpen(hot.id, hot.anchors.first()));

    const std::optional<AnchorUsage> usage = repository.anchorUsage(hot.id, hot.anchors.first());
    QVERIFY(usage.has_value());
    QCOMPARE(usage->openCount, 2);
    QVERIFY(usage->lastOpenedAt.isValid());

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("Power")});
    QCOMPARE(results.size(), 2);
    QCOMPARE(results.first().resource.id, hot.id);
    QVERIFY(results.first().matchedAnchor.has_value());
    QCOMPARE(results.first().matchedAnchor->line, 2);
}

void CoreSmokeTest::searchesExtractedContent()
{
    InMemoryLibraryRepository repository;

    Resource resource;
    resource.id = QStringLiteral("html");
    resource.kind = ResourceKind::Url;
    resource.title = QStringLiteral("Local Guide");
    resource.location = QStringLiteral("guide.html");
    resource.content = QStringLiteral("This page explains browser launch routing.");
    QVERIFY(repository.upsertResource(resource));

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("browser launch")});
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().resource.id, resource.id);
    QCOMPARE(results.first().matchedField, QStringLiteral("content"));
}

void CoreSmokeTest::exposesSqliteFts5SchemaDraft()
{
    const QString ddl = Schema::sqliteFts5Draft().join(QLatin1Char('\n')).toLower();
    QVERIFY(ddl.contains(QStringLiteral("create virtual table")));
    QVERIFY(ddl.contains(QStringLiteral("fts5")));
    QVERIFY(ddl.contains(QStringLiteral("anchors")));
}

QTEST_MAIN(CoreSmokeTest)

#include "core_smoke_test.moc"
