#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/core/AnchorHealthCheck.h"
#include "pinloom/core/ApplicationLaunchSettings.h"
#include "pinloom/core/ExcelCommand.h"
#include "pinloom/core/ExplorerFileSelection.h"
#include "pinloom/core/InboxFileCapture.h"
#include "pinloom/core/PdfXChangeCommand.h"
#include "pinloom/core/PowerPointCommand.h"
#include "pinloom/core/Schema.h"
#include "pinloom/core/VisioCommand.h"
#include "pinloom/core/WordCommand.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <optional>

using namespace Pinloom;

class CoreSmokeTest : public QObject {
    Q_OBJECT

private slots:
    void searchesAliasesAndTags();
    void persistsAndSearchesAnchorLocatorFields();
    void buildsPdfXChangeRectCommand();
    void buildsPdfXChangeViewRectCommand();
    void buildsPdfXChangePageCommandFromLegacyAnchor();
    void doesNotTreatLegacyPdfManualLineAsPdfXChangeAnchor();
    void reportsMissingPdfXChangeTargetPath();
    void resolvesPdfXChangeExecutableFromEnvironment();
    void defaultsApplicationLaunchSettings();
    void appliesExplicitApplicationLaunchSettings();
    void checksExistingAnchorTargetHealth();
    void reportsMissingAnchorTargetHealth();
    void reportsMissingPdfXChangeLauncherHealth();
    void reportsMissingExplicitPowerShellLauncherHealth();
    void reportsUnsupportedAnchorHealthInputs();
    void buildsExcelRangeCommand();
    void buildsExcelNamedRangeCommand();
    void reportsExcelCommandInputErrors();
    void recognizesExcelTargetAppAliases();
    void buildsVisioShapeCommand();
    void buildsVisioShapeCommandWithAliasAndFallbacks();
    void reportsVisioCommandInputErrors();
    void recognizesVisioTargetAppAliases();
    void buildsWordBookmarkCommand();
    void buildsWordBookmarkCommandWithAliasAndFallbacks();
    void reportsWordCommandInputErrors();
    void recognizesWordTargetAppAliases();
    void buildsPowerPointShapeCommandWithId();
    void buildsPowerPointShapeCommandWithName();
    void buildsPowerPointShapeCommandWithAliasesAndFallbacks();
    void reportsPowerPointCommandInputErrors();
    void recognizesPowerPointTargetAppAliases();
    void ranksAnchorLocatorMatchesByNameAliasTagAndMetadata();
    void validatesInboxFileRequests();
    void savesInboxFilesByStablePathAndSearchesMetadata();
    void recognizesExplorerForegroundWindows();
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
    void filtersLegacyPdfManualLineAnchorsFromSearch();
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
    QCOMPARE(result.command.action, QStringLiteral("page=12;zoom=250;highlight=420,780,860,920;usept=yes"));
    QCOMPARE(result.command.arguments,
             QStringList({QStringLiteral("/A"),
                          QStringLiteral("page=12;zoom=250;highlight=420,780,860,920;usept=yes"),
                          anchor.targetFile}));
}

void CoreSmokeTest::buildsPdfXChangeViewRectCommand()
{
    Anchor anchor;
    anchor.targetApp = QStringLiteral("PDF-XChange");
    anchor.targetFile = QStringLiteral("E:/docs/clock.pdf");
    anchor.locatorType = QStringLiteral("pdfxchange.rect");
    anchor.locatorJson = QStringLiteral(
        "{\"type\":\"pdfxchange.rect\",\"page\":12,\"rect\":[420,860,780,920],\"zoom\":250,\"unit\":\"pt\",\"mode\":\"viewrect\"}");

    const PdfXChangeCommandResult result =
        buildPdfXChangeCommand(anchor, QString(), QStringLiteral("C:/Tools/PDFXEdit.exe"));

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.action, QStringLiteral("page=12;zoom=250;viewrect=420,860,360,60;usept=yes"));
    QCOMPARE(result.command.arguments,
             QStringList({QStringLiteral("/A"),
                          QStringLiteral("page=12;zoom=250;viewrect=420,860,360,60;usept=yes"),
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

void CoreSmokeTest::doesNotTreatLegacyPdfManualLineAsPdfXChangeAnchor()
{
    Anchor anchor;
    anchor.type = AnchorType::Manual;
    anchor.targetApp = QStringLiteral("pdf");
    anchor.targetFile = QStringLiteral("E:/test_dir/ISO 11898-1.pdf");
    anchor.locatorType = QStringLiteral("manual");
    anchor.locatorJson = QStringLiteral("{\"line\":12}");
    anchor.line = 12;

    QVERIFY(!isPdfXChangeAnchor(anchor));

    const PdfXChangeCommandResult result =
        buildPdfXChangeCommand(anchor, QString(), QStringLiteral("C:/Tools/PDFXEdit.exe"));

    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("PDF-XChange locator type is unsupported"));
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

void CoreSmokeTest::defaultsApplicationLaunchSettings()
{
    ApplicationLaunchSettings settings;

    QCOMPARE(defaultPowerShellExecutablePath(), QStringLiteral("powershell.exe"));
    QCOMPARE(effectivePowerShellExecutablePath(settings), QStringLiteral("powershell.exe"));

    settings.powerShellExecutablePath = QStringLiteral("   ");
    QCOMPARE(effectivePowerShellExecutablePath(settings), QStringLiteral("powershell.exe"));
    QVERIFY(usesPowerShellLauncher(ExternalApplicationTarget::Excel));
    QVERIFY(usesPowerShellLauncher(ExternalApplicationTarget::Word));
    QVERIFY(usesPowerShellLauncher(ExternalApplicationTarget::PowerPoint));
    QVERIFY(usesPowerShellLauncher(ExternalApplicationTarget::Visio));
    QVERIFY(!usesPowerShellLauncher(ExternalApplicationTarget::PdfXChange));
    QCOMPARE(externalApplicationLabel(ExternalApplicationTarget::PdfXChange),
             QStringLiteral("PDF-XChange Editor"));

    const bool hadValue = qEnvironmentVariableIsSet("PINLOOM_PDFXCHANGE_PATH");
    const QByteArray previous = qgetenv("PINLOOM_PDFXCHANGE_PATH");

    QVERIFY(qputenv("PINLOOM_PDFXCHANGE_PATH", "C:/Portable PDF/PDFXEdit.exe"));
    QCOMPARE(resolvePdfXChangeExecutablePath(settings),
             QStringLiteral("C:/Portable PDF/PDFXEdit.exe"));

    Anchor anchor;
    anchor.targetFile = QStringLiteral("E:/docs/spec.pdf");
    anchor.locatorType = QStringLiteral("pdfxchange.page");
    anchor.locatorJson = QStringLiteral("{\"page\":4}");
    const PdfXChangeCommandResult command = buildPdfXChangeCommand(anchor, QString(), settings);

    if (hadValue) {
        QVERIFY(qputenv("PINLOOM_PDFXCHANGE_PATH", previous));
    } else {
        qunsetenv("PINLOOM_PDFXCHANGE_PATH");
    }

    QVERIFY2(command.success(), qPrintable(command.error));
    QCOMPARE(command.command.executablePath, QStringLiteral("C:/Portable PDF/PDFXEdit.exe"));
}

void CoreSmokeTest::appliesExplicitApplicationLaunchSettings()
{
    ApplicationLaunchSettings settings;
    settings.pdfXChangeExecutablePath = QStringLiteral(" C:/Pinned/PDFXEdit.exe ");
    settings.powerShellExecutablePath = QStringLiteral(" C:/Tools/PowerShell/powershell.exe ");

    QCOMPARE(resolvePdfXChangeExecutablePath(settings), QStringLiteral("C:/Pinned/PDFXEdit.exe"));
    QCOMPARE(effectivePowerShellExecutablePath(settings),
             QStringLiteral("C:/Tools/PowerShell/powershell.exe"));

    Anchor pdfAnchor;
    pdfAnchor.targetFile = QStringLiteral("E:/docs/spec.pdf");
    pdfAnchor.locatorType = QStringLiteral("pdfxchange.page");
    pdfAnchor.locatorJson = QStringLiteral("{\"page\":4}");
    const PdfXChangeCommandResult pdfCommand =
        buildPdfXChangeCommand(pdfAnchor, QString(), settings);
    QVERIFY2(pdfCommand.success(), qPrintable(pdfCommand.error));
    QCOMPARE(pdfCommand.command.executablePath, QStringLiteral("C:/Pinned/PDFXEdit.exe"));

    Anchor excelAnchor;
    excelAnchor.targetFile = QStringLiteral("E:/books/budget.xlsx");
    excelAnchor.locatorType = QStringLiteral("excel.range");
    excelAnchor.locatorJson =
        QStringLiteral("{\"sheet\":\"Sheet1\",\"range\":\"B12:D18\"}");
    const ExcelJumpCommandResult excelCommand =
        buildExcelJumpCommand(excelAnchor, QString(), settings);
    QVERIFY2(excelCommand.success(), qPrintable(excelCommand.error));
    QCOMPARE(excelCommand.command.executablePath,
             QStringLiteral("C:/Tools/PowerShell/powershell.exe"));

    Anchor wordAnchor;
    wordAnchor.targetFile = QStringLiteral("E:/docs/spec.docx");
    wordAnchor.locatorType = QStringLiteral("word.bookmark");
    wordAnchor.locatorJson = QStringLiteral("{\"bookmark\":\"Requirement_12\"}");
    const WordJumpCommandResult wordCommand =
        buildWordJumpCommand(wordAnchor, QString(), settings);
    QVERIFY2(wordCommand.success(), qPrintable(wordCommand.error));
    QCOMPARE(wordCommand.command.executablePath,
             QStringLiteral("C:/Tools/PowerShell/powershell.exe"));

    Anchor powerPointAnchor;
    powerPointAnchor.targetFile = QStringLiteral("E:/slides/process.pptx");
    powerPointAnchor.locatorType = QStringLiteral("powerpoint.shape");
    powerPointAnchor.locatorJson = QStringLiteral("{\"slide\":12,\"shape_id\":42}");
    const PowerPointJumpCommandResult powerPointCommand =
        buildPowerPointJumpCommand(powerPointAnchor, QString(), settings);
    QVERIFY2(powerPointCommand.success(), qPrintable(powerPointCommand.error));
    QCOMPARE(powerPointCommand.command.executablePath,
             QStringLiteral("C:/Tools/PowerShell/powershell.exe"));

    Anchor visioAnchor;
    visioAnchor.targetFile = QStringLiteral("E:/drawings/power.vsdx");
    visioAnchor.locatorType = QStringLiteral("visio.shape");
    visioAnchor.locatorJson =
        QStringLiteral("{\"page\":\"Page-1\","
                       "\"shape_unique_id\":\"{00000000-0000-0000-0000-000000000000}\"}");
    const VisioJumpCommandResult visioCommand =
        buildVisioJumpCommand(visioAnchor, QString(), settings);
    QVERIFY2(visioCommand.success(), qPrintable(visioCommand.error));
    QCOMPARE(visioCommand.command.executablePath,
             QStringLiteral("C:/Tools/PowerShell/powershell.exe"));
}

void CoreSmokeTest::checksExistingAnchorTargetHealth()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString targetPath = tempDir.filePath(QStringLiteral("target.txt"));
    QFile targetFile(targetPath);
    QVERIFY(targetFile.open(QIODevice::WriteOnly));
    QVERIFY(targetFile.write("anchor") > 0);
    targetFile.close();

    Anchor anchor;
    anchor.targetFile = targetPath;
    AnchorHealthCheckResult result = checkAnchorHealth(anchor);

    QCOMPARE(static_cast<int>(result.status), static_cast<int>(AnchorHealthStatus::Ok));
    QVERIFY2(result.healthy(), qPrintable(result.message));
    QCOMPARE(result.path, targetPath);

    Anchor fallbackAnchor;
    result = checkAnchorHealth(fallbackAnchor, targetPath);
    QCOMPARE(static_cast<int>(result.status), static_cast<int>(AnchorHealthStatus::Ok));
    QCOMPARE(result.path, targetPath);
}

void CoreSmokeTest::reportsMissingAnchorTargetHealth()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Anchor anchor;
    anchor.targetFile = tempDir.filePath(QStringLiteral("missing-target.txt"));
    AnchorHealthCheckResult result = checkAnchorHealth(anchor);

    QCOMPARE(static_cast<int>(result.status), static_cast<int>(AnchorHealthStatus::MissingTarget));
    QVERIFY(!result.healthy());
    QCOMPARE(result.path, anchor.targetFile);

    const QString missingFallback = tempDir.filePath(QStringLiteral("missing-fallback.txt"));
    Anchor fallbackAnchor;
    result = checkAnchorHealth(fallbackAnchor, missingFallback);
    QCOMPARE(static_cast<int>(result.status), static_cast<int>(AnchorHealthStatus::MissingTarget));
    QCOMPARE(result.path, missingFallback);
}

void CoreSmokeTest::reportsMissingPdfXChangeLauncherHealth()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString targetPath = tempDir.filePath(QStringLiteral("target.pdf"));
    QFile targetFile(targetPath);
    QVERIFY(targetFile.open(QIODevice::WriteOnly));
    QVERIFY(targetFile.write("%PDF-1.7") > 0);
    targetFile.close();

    ApplicationLaunchSettings settings;
    settings.pdfXChangeExecutablePath = tempDir.filePath(QStringLiteral("missing-PDFXEdit.exe"));

    Anchor anchor;
    anchor.targetFile = targetPath;
    anchor.locatorType = QStringLiteral("pdfxchange.page");
    anchor.locatorJson = QStringLiteral("{\"page\":1}");

    const AnchorHealthCheckResult result = checkAnchorHealth(anchor, QString(), settings);
    QCOMPARE(static_cast<int>(result.status), static_cast<int>(AnchorHealthStatus::MissingLauncher));
    QCOMPARE(result.path, targetPath);
    QCOMPARE(result.launcherPath, settings.pdfXChangeExecutablePath);
    QCOMPARE(result.app, QStringLiteral("PDF-XChange Editor"));
    QCOMPARE(result.locatorType, QStringLiteral("pdfxchange.page"));
}

void CoreSmokeTest::reportsMissingExplicitPowerShellLauncherHealth()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString targetPath = tempDir.filePath(QStringLiteral("target.xlsx"));
    QFile targetFile(targetPath);
    QVERIFY(targetFile.open(QIODevice::WriteOnly));
    QVERIFY(targetFile.write("workbook") > 0);
    targetFile.close();

    Anchor anchor;
    anchor.targetFile = targetPath;
    anchor.locatorType = QStringLiteral("excel.range");
    anchor.locatorJson = QStringLiteral("{\"sheet\":\"Sheet1\",\"range\":\"A1\"}");

    AnchorHealthCheckResult result = checkAnchorHealth(anchor);
    QCOMPARE(static_cast<int>(result.status), static_cast<int>(AnchorHealthStatus::Ok));

    ApplicationLaunchSettings settings;
    settings.powerShellExecutablePath = tempDir.filePath(QStringLiteral("missing-powershell.exe"));
    result = checkAnchorHealth(anchor, QString(), settings);

    QCOMPARE(static_cast<int>(result.status), static_cast<int>(AnchorHealthStatus::MissingLauncher));
    QCOMPARE(result.path, targetPath);
    QCOMPARE(result.launcherPath, settings.powerShellExecutablePath);
    QCOMPARE(result.app, QStringLiteral("Microsoft Excel"));
    QCOMPARE(result.locatorType, QStringLiteral("excel.range"));
}

void CoreSmokeTest::reportsUnsupportedAnchorHealthInputs()
{
    Anchor remoteTarget;
    remoteTarget.targetUri = QStringLiteral("https://example.com/spec.pdf");
    AnchorHealthCheckResult result = checkAnchorHealth(remoteTarget);

    QCOMPARE(static_cast<int>(result.status), static_cast<int>(AnchorHealthStatus::UnsupportedTarget));
    QCOMPARE(result.path, remoteTarget.targetUri);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString targetPath = tempDir.filePath(QStringLiteral("target.bin"));
    QFile targetFile(targetPath);
    QVERIFY(targetFile.open(QIODevice::WriteOnly));
    QVERIFY(targetFile.write("target") > 0);
    targetFile.close();

    Anchor unsupportedLocator;
    unsupportedLocator.targetFile = targetPath;
    unsupportedLocator.locatorType = QStringLiteral("cad.shape");
    unsupportedLocator.locatorJson = QStringLiteral("{\"type\":\"cad.shape\"}");
    result = checkAnchorHealth(unsupportedLocator);

    QCOMPARE(static_cast<int>(result.status), static_cast<int>(AnchorHealthStatus::UnsupportedLocator));
    QCOMPARE(result.path, targetPath);
    QCOMPARE(result.locatorType, QStringLiteral("cad.shape"));
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

void CoreSmokeTest::buildsVisioShapeCommand()
{
    const QString shapeId = QStringLiteral("{00000000-0000-0000-0000-000000000000}");

    Anchor anchor;
    anchor.targetApp = QStringLiteral("Microsoft Visio");
    anchor.targetFile = QStringLiteral("E:/drawings/power.vsdx");
    anchor.locatorType = QStringLiteral("visio.shape");
    anchor.locatorJson = QStringLiteral("{\"type\":\"visio.shape\",\"page\":\"Page-1\",\"shape_unique_id\":\"%1\"}")
                             .arg(shapeId);

    QVERIFY(isVisioAnchor(anchor));
    const VisioJumpCommandResult result = buildVisioJumpCommand(anchor, QString());

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.executablePath, QStringLiteral("powershell.exe"));
    QCOMPARE(result.command.documentPath, anchor.targetFile);
    QCOMPARE(result.command.locatorType, QStringLiteral("visio.shape"));
    QCOMPARE(result.command.pageName, QStringLiteral("Page-1"));
    QCOMPARE(result.command.shapeUniqueId, shapeId);
    QCOMPARE(result.command.arguments.at(0), QStringLiteral("-NoProfile"));
    QVERIFY(result.command.arguments.contains(QStringLiteral("-Command")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("Documents.Open('E:/drawings/power.vsdx')")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("Pages.ItemU('Page-1')")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("ItemFromUniqueID('%1')").arg(shapeId)));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("$window.Select($shape, 2)")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("$window.Activate()")));
}

void CoreSmokeTest::buildsVisioShapeCommandWithAliasAndFallbacks()
{
    const QString shapeId = QStringLiteral("{11111111-2222-3333-4444-555555555555}");

    Anchor anchor;
    anchor.targetApp = QStringLiteral("Visio");
    anchor.targetFile = QStringLiteral("E:/drawings/anchor-field.vsdx");
    anchor.locatorJson =
        QStringLiteral("{\"type\":\"visio.shape\",\"page\":\"Page-2\",\"shapeUniqueId\":\"%1\","
                       "\"document\":\"E:/drawings/document-fallback.vsdx\","
                       "\"target_file\":\"E:/drawings/target-file-fallback.vsdx\"}")
            .arg(shapeId);

    QVERIFY(isVisioAnchor(anchor));
    VisioJumpCommandResult result = buildVisioJumpCommand(anchor, QString());
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.documentPath, QStringLiteral("E:/drawings/anchor-field.vsdx"));
    QCOMPARE(result.command.shapeUniqueId, shapeId);

    Anchor targetUriWins = anchor;
    targetUriWins.targetFile.clear();
    targetUriWins.targetUri = QStringLiteral("E:/drawings/target-uri.vsdx");
    result = buildVisioJumpCommand(targetUriWins, QString());
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.documentPath, QStringLiteral("E:/drawings/target-uri.vsdx"));

    Anchor locatorDocumentFallback = anchor;
    locatorDocumentFallback.targetFile.clear();
    result = buildVisioJumpCommand(locatorDocumentFallback, QString());
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.documentPath, QStringLiteral("E:/drawings/document-fallback.vsdx"));

    Anchor locationFallback = anchor;
    locationFallback.targetFile.clear();
    locationFallback.locatorJson =
        QStringLiteral("{\"type\":\"visio.shape\",\"page\":\"Page-2\",\"shapeUniqueId\":\"%1\"}")
            .arg(shapeId);
    result = buildVisioJumpCommand(locationFallback, QStringLiteral("E:/drawings/location-fallback.vsdx"));
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.documentPath, QStringLiteral("E:/drawings/location-fallback.vsdx"));
}

void CoreSmokeTest::reportsVisioCommandInputErrors()
{
    const QString shapeId = QStringLiteral("{00000000-0000-0000-0000-000000000000}");

    Anchor base;
    base.targetApp = QStringLiteral("Visio");
    base.targetFile = QStringLiteral("E:/drawings/power.vsdx");
    base.locatorType = QStringLiteral("visio.shape");
    base.locatorJson = QStringLiteral("{\"page\":\"Page-1\",\"shape_unique_id\":\"%1\"}").arg(shapeId);

    Anchor missingFile = base;
    missingFile.targetFile.clear();
    VisioJumpCommandResult result = buildVisioJumpCommand(missingFile, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Visio document path is missing"));

    Anchor invalidJson = base;
    invalidJson.locatorJson = QStringLiteral("{\"page\":");
    result = buildVisioJumpCommand(invalidJson, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Visio locator JSON is invalid"));

    Anchor unsupported = base;
    unsupported.locatorType = QStringLiteral("visio.page");
    result = buildVisioJumpCommand(unsupported, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Visio locator type is unsupported"));

    Anchor missingPage = base;
    missingPage.locatorJson = QStringLiteral("{\"shape_unique_id\":\"%1\"}").arg(shapeId);
    result = buildVisioJumpCommand(missingPage, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Visio locator page is missing"));

    Anchor missingShape = base;
    missingShape.locatorJson = QStringLiteral("{\"page\":\"Page-1\"}");
    result = buildVisioJumpCommand(missingShape, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Visio shape UniqueID is missing"));
}

void CoreSmokeTest::recognizesVisioTargetAppAliases()
{
    Anchor visio;
    visio.targetApp = QStringLiteral("vIsIo");
    QVERIFY(isVisioAnchor(visio));

    visio.targetApp = QStringLiteral("Microsoft Visio");
    QVERIFY(isVisioAnchor(visio));

    visio.targetApp = QStringLiteral("MS Visio");
    QVERIFY(isVisioAnchor(visio));

    visio.targetApp = QStringLiteral("Word");
    QVERIFY(!isVisioAnchor(visio));

    visio.targetApp.clear();
    visio.locatorType = QStringLiteral("VISIO.SHAPE");
    QVERIFY(isVisioAnchor(visio));

    visio.locatorType.clear();
    visio.locatorJson = QStringLiteral("{\"type\":\"visio.shape\"}");
    QVERIFY(isVisioAnchor(visio));
}

void CoreSmokeTest::buildsWordBookmarkCommand()
{
    Anchor anchor;
    anchor.targetApp = QStringLiteral("Microsoft Word");
    anchor.targetFile = QStringLiteral("E:/docs/spec.docx");
    anchor.locatorType = QStringLiteral("word.bookmark");
    anchor.locatorJson = QStringLiteral("{\"type\":\"word.bookmark\",\"bookmark\":\"Requirement_12\"}");

    QVERIFY(isWordAnchor(anchor));
    const WordJumpCommandResult result = buildWordJumpCommand(anchor, QString());

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.executablePath, QStringLiteral("powershell.exe"));
    QCOMPARE(result.command.documentPath, anchor.targetFile);
    QCOMPARE(result.command.locatorType, QStringLiteral("word.bookmark"));
    QCOMPARE(result.command.bookmarkName, QStringLiteral("Requirement_12"));
    QCOMPARE(result.command.arguments.at(0), QStringLiteral("-NoProfile"));
    QVERIFY(result.command.arguments.contains(QStringLiteral("-Command")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("Documents.Open('E:/docs/spec.docx')")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("Bookmarks.Item('Requirement_12')")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("$range.Select()")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("$word.Selection.GoTo(-1, 1, $null, 'Requirement_12')")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("$word.Activate()")));
}

void CoreSmokeTest::buildsWordBookmarkCommandWithAliasAndFallbacks()
{
    Anchor anchor;
    anchor.targetApp = QStringLiteral("Word");
    anchor.targetFile = QStringLiteral("E:/docs/anchor-field.docx");
    anchor.locatorJson =
        QStringLiteral("{\"type\":\"word.bookmark\",\"name\":\"Requirement_12\","
                       "\"document\":\"E:/docs/document-fallback.docx\","
                       "\"target_file\":\"E:/docs/target-file-fallback.docx\"}");

    QVERIFY(isWordAnchor(anchor));
    WordJumpCommandResult result = buildWordJumpCommand(anchor, QString());
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.documentPath, QStringLiteral("E:/docs/anchor-field.docx"));
    QCOMPARE(result.command.bookmarkName, QStringLiteral("Requirement_12"));

    Anchor targetUriWins = anchor;
    targetUriWins.targetFile.clear();
    targetUriWins.targetUri = QStringLiteral("E:/docs/target-uri.docx");
    result = buildWordJumpCommand(targetUriWins, QString());
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.documentPath, QStringLiteral("E:/docs/target-uri.docx"));

    Anchor locatorDocumentFallback = anchor;
    locatorDocumentFallback.targetFile.clear();
    result = buildWordJumpCommand(locatorDocumentFallback, QString());
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.documentPath, QStringLiteral("E:/docs/document-fallback.docx"));

    Anchor locatorTargetFileFallback = anchor;
    locatorTargetFileFallback.targetFile.clear();
    locatorTargetFileFallback.locatorJson =
        QStringLiteral("{\"type\":\"word.bookmark\",\"name\":\"Requirement_12\","
                       "\"target_file\":\"E:/docs/target-file-fallback.docx\"}");
    result = buildWordJumpCommand(locatorTargetFileFallback, QString());
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.documentPath, QStringLiteral("E:/docs/target-file-fallback.docx"));

    Anchor locationFallback = anchor;
    locationFallback.targetFile.clear();
    locationFallback.locatorJson =
        QStringLiteral("{\"type\":\"word.bookmark\",\"name\":\"Requirement_12\"}");
    result = buildWordJumpCommand(locationFallback, QStringLiteral("E:/docs/location-fallback.docx"));
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.documentPath, QStringLiteral("E:/docs/location-fallback.docx"));
}

void CoreSmokeTest::reportsWordCommandInputErrors()
{
    Anchor base;
    base.targetApp = QStringLiteral("Word");
    base.targetFile = QStringLiteral("E:/docs/spec.docx");
    base.locatorType = QStringLiteral("word.bookmark");
    base.locatorJson = QStringLiteral("{\"bookmark\":\"Requirement_12\"}");

    Anchor missingFile = base;
    missingFile.targetFile.clear();
    WordJumpCommandResult result = buildWordJumpCommand(missingFile, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Word document path is missing"));

    Anchor invalidJson = base;
    invalidJson.locatorJson = QStringLiteral("{\"bookmark\":");
    result = buildWordJumpCommand(invalidJson, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Word locator JSON is invalid"));

    Anchor unsupported = base;
    unsupported.locatorType = QStringLiteral("word.heading");
    result = buildWordJumpCommand(unsupported, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Word locator type is unsupported"));

    Anchor missingBookmark = base;
    missingBookmark.locatorJson = QStringLiteral("{}");
    result = buildWordJumpCommand(missingBookmark, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("Word bookmark is missing"));
}

void CoreSmokeTest::recognizesWordTargetAppAliases()
{
    Anchor word;
    word.targetApp = QStringLiteral("wOrD");
    QVERIFY(isWordAnchor(word));

    word.targetApp = QStringLiteral("Microsoft Word");
    QVERIFY(isWordAnchor(word));

    word.targetApp = QStringLiteral("MS Word");
    QVERIFY(isWordAnchor(word));

    word.targetApp = QStringLiteral("Excel");
    QVERIFY(!isWordAnchor(word));

    word.targetApp.clear();
    word.locatorType = QStringLiteral("WORD.BOOKMARK");
    QVERIFY(isWordAnchor(word));

    word.locatorType.clear();
    word.locatorJson = QStringLiteral("{\"type\":\"word.bookmark\"}");
    QVERIFY(isWordAnchor(word));
}

void CoreSmokeTest::buildsPowerPointShapeCommandWithId()
{
    Anchor anchor;
    anchor.targetApp = QStringLiteral("Microsoft PowerPoint");
    anchor.targetFile = QStringLiteral("E:/slides/process.pptx");
    anchor.locatorType = QStringLiteral("powerpoint.shape");
    anchor.locatorJson = QStringLiteral("{\"type\":\"powerpoint.shape\",\"slide\":12,\"shape_id\":42}");

    QVERIFY(isPowerPointAnchor(anchor));
    const PowerPointJumpCommandResult result = buildPowerPointJumpCommand(anchor, QString());

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.executablePath, QStringLiteral("powershell.exe"));
    QCOMPARE(result.command.presentationPath, anchor.targetFile);
    QCOMPARE(result.command.locatorType, QStringLiteral("powerpoint.shape"));
    QCOMPARE(result.command.slideIndex, 12);
    QCOMPARE(result.command.shapeId, 42);
    QVERIFY(result.command.shapeName.isEmpty());
    QCOMPARE(result.command.arguments.at(0), QStringLiteral("-NoProfile"));
    QVERIFY(result.command.arguments.contains(QStringLiteral("-Command")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("Presentations.Open('E:/slides/process.pptx')")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("Slides.Item(12)")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("View.GotoSlide(12)")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("Shapes.FindById(42)")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("$shape.Select($true)")));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("$powerPoint.Activate()")));
}

void CoreSmokeTest::buildsPowerPointShapeCommandWithName()
{
    Anchor anchor;
    anchor.targetApp = QStringLiteral("PPT");
    anchor.targetFile = QStringLiteral("E:/slides/process.pptx");
    anchor.locatorType = QStringLiteral("powerpoint.shape");
    anchor.locatorJson = QStringLiteral("{\"type\":\"powerpoint.shape\",\"slide\":12,\"shape_name\":\"Valve A\"}");

    QVERIFY(isPowerPointAnchor(anchor));
    const PowerPointJumpCommandResult result = buildPowerPointJumpCommand(anchor, QString());

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.presentationPath, anchor.targetFile);
    QCOMPARE(result.command.locatorType, QStringLiteral("powerpoint.shape"));
    QCOMPARE(result.command.slideIndex, 12);
    QCOMPARE(result.command.shapeId, -1);
    QCOMPARE(result.command.shapeName, QStringLiteral("Valve A"));
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("Shapes.Item('Valve A')")));
    QVERIFY(!result.command.powerShellScript.contains(QStringLiteral("FindById")));
}

void CoreSmokeTest::buildsPowerPointShapeCommandWithAliasesAndFallbacks()
{
    Anchor anchor;
    anchor.targetApp = QStringLiteral("PowerPoint");
    anchor.targetFile = QStringLiteral("E:/slides/anchor-field.pptx");
    anchor.locatorJson =
        QStringLiteral("{\"type\":\"powerpoint.shape\",\"slide_index\":7,\"shapeId\":99,"
                       "\"shapeName\":\"Ignored name\","
                       "\"presentation\":\"E:/slides/presentation-fallback.pptx\","
                       "\"target_file\":\"E:/slides/target-file-fallback.pptx\"}");

    QVERIFY(isPowerPointAnchor(anchor));
    PowerPointJumpCommandResult result = buildPowerPointJumpCommand(anchor, QString());
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.presentationPath, QStringLiteral("E:/slides/anchor-field.pptx"));
    QCOMPARE(result.command.slideIndex, 7);
    QCOMPARE(result.command.shapeId, 99);
    QVERIFY(result.command.shapeName.isEmpty());
    QVERIFY(result.command.powerShellScript.contains(QStringLiteral("Shapes.FindById(99)")));
    QVERIFY(!result.command.powerShellScript.contains(QStringLiteral("Ignored name")));

    Anchor targetUriWins = anchor;
    targetUriWins.targetFile.clear();
    targetUriWins.targetUri = QStringLiteral("E:/slides/target-uri.pptx");
    result = buildPowerPointJumpCommand(targetUriWins, QString());
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.presentationPath, QStringLiteral("E:/slides/target-uri.pptx"));

    Anchor locatorPresentationFallback = anchor;
    locatorPresentationFallback.targetFile.clear();
    result = buildPowerPointJumpCommand(locatorPresentationFallback, QString());
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.presentationPath, QStringLiteral("E:/slides/presentation-fallback.pptx"));

    Anchor locatorTargetFileFallback = anchor;
    locatorTargetFileFallback.targetFile.clear();
    locatorTargetFileFallback.locatorJson =
        QStringLiteral("{\"type\":\"powerpoint.shape\",\"slide_index\":7,\"shapeId\":99,"
                       "\"target_file\":\"E:/slides/target-file-fallback.pptx\"}");
    result = buildPowerPointJumpCommand(locatorTargetFileFallback, QString());
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.presentationPath, QStringLiteral("E:/slides/target-file-fallback.pptx"));

    Anchor locationFallback = anchor;
    locationFallback.targetFile.clear();
    locationFallback.locatorJson =
        QStringLiteral("{\"type\":\"powerpoint.shape\",\"slide_index\":7,\"shapeName\":\"Valve A\"}");
    result = buildPowerPointJumpCommand(locationFallback, QStringLiteral("E:/slides/location-fallback.pptx"));
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.presentationPath, QStringLiteral("E:/slides/location-fallback.pptx"));
    QCOMPARE(result.command.shapeName, QStringLiteral("Valve A"));
}

void CoreSmokeTest::reportsPowerPointCommandInputErrors()
{
    Anchor base;
    base.targetApp = QStringLiteral("PowerPoint");
    base.targetFile = QStringLiteral("E:/slides/process.pptx");
    base.locatorType = QStringLiteral("powerpoint.shape");
    base.locatorJson = QStringLiteral("{\"slide\":12,\"shape_id\":42}");

    Anchor missingFile = base;
    missingFile.targetFile.clear();
    PowerPointJumpCommandResult result = buildPowerPointJumpCommand(missingFile, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("PowerPoint presentation path is missing"));

    Anchor invalidJson = base;
    invalidJson.locatorJson = QStringLiteral("{\"slide\":");
    result = buildPowerPointJumpCommand(invalidJson, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("PowerPoint locator JSON is invalid"));

    Anchor unsupported = base;
    unsupported.locatorType = QStringLiteral("powerpoint.slide");
    result = buildPowerPointJumpCommand(unsupported, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("PowerPoint locator type is unsupported"));

    Anchor missingSlide = base;
    missingSlide.locatorJson = QStringLiteral("{\"shape_id\":42}");
    result = buildPowerPointJumpCommand(missingSlide, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("PowerPoint slide is missing"));

    Anchor missingShape = base;
    missingShape.locatorJson = QStringLiteral("{\"slide\":12}");
    result = buildPowerPointJumpCommand(missingShape, QString());
    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("PowerPoint shape id or name is missing"));
}

void CoreSmokeTest::recognizesPowerPointTargetAppAliases()
{
    Anchor powerPoint;
    powerPoint.targetApp = QStringLiteral("pOwErPoInT");
    QVERIFY(isPowerPointAnchor(powerPoint));

    powerPoint.targetApp = QStringLiteral("Microsoft PowerPoint");
    QVERIFY(isPowerPointAnchor(powerPoint));

    powerPoint.targetApp = QStringLiteral("MS PowerPoint");
    QVERIFY(isPowerPointAnchor(powerPoint));

    powerPoint.targetApp = QStringLiteral("PPT");
    QVERIFY(isPowerPointAnchor(powerPoint));

    powerPoint.targetApp = QStringLiteral("Excel");
    QVERIFY(!isPowerPointAnchor(powerPoint));

    powerPoint.targetApp.clear();
    powerPoint.locatorType = QStringLiteral("POWERPOINT.SHAPE");
    QVERIFY(isPowerPointAnchor(powerPoint));

    powerPoint.locatorType.clear();
    powerPoint.locatorJson = QStringLiteral("{\"type\":\"powerpoint.shape\"}");
    QVERIFY(isPowerPointAnchor(powerPoint));
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

void CoreSmokeTest::validatesInboxFileRequests()
{
    InboxFileSaveRequest emptyPath;
    QCOMPARE(inboxFileSaveRequestError(emptyPath), QStringLiteral("Inbox file path is required"));

    InboxFileSaveRequest copyRequest;
    copyRequest.filePath = QStringLiteral("E:/docs/spec.pdf");
    copyRequest.mode = InboxFileArchiveMode::Copy;
    QCOMPARE(inboxFileSaveRequestError(copyRequest), QStringLiteral("Inbox MVP supports Link mode only"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    InboxFileSaveRequest folderRequest;
    folderRequest.filePath = dir.path();
    QCOMPARE(inboxFileSaveRequestError(folderRequest), QStringLiteral("Inbox captures files only"));
}

void CoreSmokeTest::savesInboxFilesByStablePathAndSearchesMetadata()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString filePath = dir.filePath(QStringLiteral("Clock Plan.txt"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("clock plan");
    file.close();

    InMemoryLibraryRepository repository;

    InboxFileSaveRequest firstRequest;
    firstRequest.filePath = filePath;
    firstRequest.name = QStringLiteral("Clock Inbox Plan");
    firstRequest.aliases = {QStringLiteral("timing inbox")};
    firstRequest.tags = {QStringLiteral("#review")};
    firstRequest.pinned = true;

    const InboxFileSaveResult firstResult = saveInboxFile(repository, firstRequest);
    QVERIFY(firstResult.success());
    QCOMPARE(firstResult.saveStatus, InboxFileSaveStatus::Created);
    QVERIFY(isInboxResourceId(firstResult.resourceId));
    QCOMPARE(firstResult.filePath, normalizedInboxFilePath(filePath));

    const std::optional<Resource> stored = repository.findResource(firstResult.resourceId);
    QVERIFY(stored.has_value());
    QVERIFY(isInboxResource(stored.value()));
    QCOMPARE(stored->kind, ResourceKind::File);
    QCOMPARE(stored->title, QStringLiteral("Clock Inbox Plan"));
    QCOMPARE(stored->location, normalizedInboxFilePath(filePath));
    QCOMPARE(stored->aliases, QStringList{QStringLiteral("timing inbox")});
    QCOMPARE(stored->tags, QStringList{QStringLiteral("review")});
    const std::optional<ResourceUsage> usage = repository.resourceUsage(firstResult.resourceId);
    QVERIFY(usage.has_value());
    QVERIFY(usage->pinned);

    QCOMPARE(repository.search(SearchQuery{QStringLiteral("Clock Inbox Plan")}).size(), 1);
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("timing inbox")}).size(), 1);
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("review")}).size(), 1);

    InboxFileSaveRequest secondRequest;
    secondRequest.filePath = filePath;
    secondRequest.name = QStringLiteral("Clock Inbox Plan Updated");
    secondRequest.aliases = {QStringLiteral("handoff inbox")};
    secondRequest.tags = {QStringLiteral("urgent")};

    const InboxFileSaveResult secondResult = saveInboxFile(repository, secondRequest);
    QVERIFY(secondResult.success());
    QCOMPARE(secondResult.saveStatus, InboxFileSaveStatus::Updated);
    QCOMPARE(secondResult.resourceId, firstResult.resourceId);

    const std::optional<Resource> updated = repository.findResource(firstResult.resourceId);
    QVERIFY(updated.has_value());
    QCOMPARE(updated->title, QStringLiteral("Clock Inbox Plan Updated"));
    QCOMPARE(updated->aliases,
             (QStringList{QStringLiteral("timing inbox"), QStringLiteral("handoff inbox")}));
    QCOMPARE(updated->tags,
             (QStringList{QStringLiteral("review"), QStringLiteral("urgent")}));

    SearchQuery allInbox;
    allInbox.requiredKinds = {ResourceKind::File};
    allInbox.limit = 0;
    int matchingPathCount = 0;
    for (const SearchResult &result : repository.search(allInbox)) {
        if (result.resource.location == normalizedInboxFilePath(filePath)) {
            ++matchingPathCount;
        }
    }
    QCOMPARE(matchingPathCount, 1);
}

void CoreSmokeTest::recognizesExplorerForegroundWindows()
{
    ForegroundAppWindowContext explorer;
    explorer.processName = QStringLiteral("explorer.exe");
    QVERIFY(isExplorerForegroundWindow(explorer));

    ForegroundAppWindowContext explorerPath;
    explorerPath.processPath = QStringLiteral("C:/Windows/explorer.exe");
    QVERIFY(isExplorerForegroundWindow(explorerPath));

    ForegroundAppWindowContext pdfXChange;
    pdfXChange.processName = QStringLiteral("PDFXEdit.exe");
    QVERIFY(!isExplorerForegroundWindow(pdfXChange));

    const ExplorerFileSelectionResult result = captureExplorerFileSelection(pdfXChange);
    QVERIFY(!result.success());
    QVERIFY(!result.recognizedExplorer);
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

void CoreSmokeTest::filtersLegacyPdfManualLineAnchorsFromSearch()
{
    InMemoryLibraryRepository repository;

    Resource pdf;
    pdf.id = QStringLiteral("iso-pdf");
    pdf.kind = ResourceKind::Pdf;
    pdf.title = QStringLiteral("ISO 11898-1");
    pdf.location = QStringLiteral("E:/test_dir/ISO 11898-1.pdf");
    Anchor legacyPdfLine;
    legacyPdfLine.type = AnchorType::Manual;
    legacyPdfLine.name = QStringLiteral("legacy PDF line anchor");
    legacyPdfLine.target = legacyPdfLine.name;
    legacyPdfLine.line = 12;
    legacyPdfLine.locatorType = QStringLiteral("manual");
    legacyPdfLine.locatorJson = QStringLiteral("{\"line\":12}");
    pdf.anchors = {legacyPdfLine};
    QVERIFY(repository.upsertResource(pdf));

    Resource text;
    text.id = QStringLiteral("bringup-note");
    text.kind = ResourceKind::File;
    text.title = QStringLiteral("Bringup Note");
    text.location = QStringLiteral("E:/test_dir/bringup.txt");
    Anchor textLine;
    textLine.type = AnchorType::Manual;
    textLine.name = QStringLiteral("legacy text line anchor");
    textLine.target = textLine.name;
    textLine.line = 12;
    text.anchors = {textLine};
    QVERIFY(repository.upsertResource(text));

    const QList<SearchResult> pdfResults =
        repository.search(SearchQuery{QStringLiteral("legacy PDF line anchor")});
    QCOMPARE(pdfResults.size(), 0);

    const QList<SearchResult> textResults =
        repository.search(SearchQuery{QStringLiteral("legacy text line anchor")});
    QCOMPARE(textResults.size(), 1);
    QCOMPARE(textResults.first().resource.id, text.id);
    QVERIFY(textResults.first().matchedAnchor.has_value());
    QCOMPARE(textResults.first().matchedAnchor->line, 12);
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
