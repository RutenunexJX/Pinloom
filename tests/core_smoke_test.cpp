#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/core/SqliteLibraryRepository.h"
#include "pinloom/core/AnchorLibraryArchive.h"
#include "pinloom/core/AnchorLibraryManagement.h"
#include "pinloom/core/AnchorLibraryPolicy.h"
#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/AnchorTarget.h"
#include "pinloom/core/ApplicationDataBackup.h"
#include "pinloom/core/ApplicationLaunchSettings.h"
#include "pinloom/core/ExcelCommand.h"
#include "pinloom/core/ExplorerFileSelection.h"
#include "pinloom/core/InboxFileCapture.h"
#include "pinloom/core/LibraryRoot.h"
#include "pinloom/core/SumatraPdfCommand.h"
#include "pinloom/core/SumatraPdfForegroundCapture.h"
#include "pinloom/core/PowerPointCommand.h"
#include "pinloom/core/Schema.h"
#include "pinloom/core/VisioCommand.h"
#include "pinloom/core/WordCommand.h"
#include "pinloom/core/Version.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <optional>

using namespace Pinloom;

class CoreSmokeTest : public QObject {
    Q_OBJECT

private slots:
    void exposesApplicationVersion();
    void searchesAliasesAndTags();
    void persistsAndSearchesAnchorLocatorFields();
    void resolvesCanonicalAnchorTargetsAndDetectsConflicts();
    void createsPairedApplicationDataBackupsAtomically();
    void buildsSumatraPdfRectCommand();
    void buildsSumatraPdfViewRectCommand();
    void buildsSumatraPdfTextCommand();
    void reportsMissingSumatraPdfTargetPath();
    void resolvesSumatraPdfExecutableFromEnvironment();
    void defaultsApplicationLaunchSettings();
    void appliesExplicitApplicationLaunchSettings();
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
    void configuresDefaultLibraryRootWithoutFixedDrive();
    void archivesInboxFilesAndRegistersLibraryRoots();
    void softDeletesAndRestoresAnchorsInSearch();
    void managesAnchorLibraryMetadataTagsPathsAndDuplicates();
    void managesAnchorLibraryLifecycleIntegrityHistoryAndAutoRelink();
    void clearsOnlyAnchorlessFileMetadata_data();
    void clearsOnlyAnchorlessFileMetadata();
    void cleansUnmarkedAnchorShellsWithoutDeletingFilesOrRoots();
    void archivesAnchorLibraryJsonAndPublishesAtomicChanges();
    void softDeletesAndRestoresInboxResourcesWithoutDeletingOriginalFile();
    void recognizesExplorerForegroundWindows();
    void ranksAnchorBeforePathMatches();
    void ranksExactMatchesWithinMatchType();
    void ranksPinnedAndOpenedResourcesWithinMatchType();
    void filtersByRequiredLocationPrefixes();
    void filtersByRequiredResourceKinds();
    void ranksContextResourcesWithinMatchType();
    void ranksOpenedAnchorsWithinAnchorMatches();
    void searchesExtractedContent();
    void exposesSqliteFts5SchemaDraft();
};

void CoreSmokeTest::exposesApplicationVersion()
{
    QVERIFY(!pinloomVersion().trimmed().isEmpty());
    QCOMPARE(pinloomVersionLabel(), QStringLiteral("v%1").arg(pinloomVersion()));
}

static Anchor testAnchor(const QString &name,
                         const QString &locatorType = QStringLiteral("manual"),
                         int line = -1)
{
    Anchor anchor;
    anchor.name = name;
    anchor.locatorType = locatorType;
    QJsonObject locator{{QStringLiteral("type"), locatorType}};
    if (line > 0) {
        locator.insert(QStringLiteral("line"), line);
    }
    anchor.locatorJson =
        QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));
    return anchor;
}

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

void CoreSmokeTest::resolvesCanonicalAnchorTargetsAndDetectsConflicts()
{
    Anchor anchor;
    const ResolvedAnchorTarget resourceTarget =
        resolveAnchorTarget(anchor, {}, QStringLiteral("E:/docs/spec.pdf"));
    QCOMPARE(resourceTarget.value, QStringLiteral("E:/docs/spec.pdf"));
    QVERIFY(resourceTarget.source == AnchorTargetSource::Resource);
    QVERIFY(!resourceTarget.isOverride());

    anchor.targetUri = QStringLiteral("file:///E:/docs/override.pdf");
    const ResolvedAnchorTarget uriTarget =
        resolveAnchorTarget(anchor, {}, QStringLiteral("E:/docs/spec.pdf"));
    QCOMPARE(QDir::fromNativeSeparators(uriTarget.value), QStringLiteral("E:/docs/override.pdf"));
    QVERIFY(uriTarget.source == AnchorTargetSource::AnchorUri);
    QVERIFY(uriTarget.isOverride());

    anchor.targetFile = QStringLiteral("E:/docs/different.pdf");
    const ResolvedAnchorTarget conflict =
        resolveAnchorTarget(anchor, {}, QStringLiteral("E:/docs/spec.pdf"));
    QVERIFY(conflict.conflictingExplicitTargets);
    QCOMPARE(conflict.value, anchor.targetFile);
}

void CoreSmokeTest::createsPairedApplicationDataBackupsAtomically()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir(dir.path()).filePath(QStringLiteral("backups"));
    const auto writer = [](const QByteArray &contents) {
        return [contents](const QString &destination) {
            QFile file(destination);
            return file.open(QIODevice::WriteOnly)
                && file.write(contents) == contents.size();
        };
    };
    const QList<ApplicationDataBackupItem> items = {
        {QStringLiteral("pinloom.sqlite3"), writer("anchor"), {}},
        {QStringLiteral("pinloom_clip.sqlite3"), writer("clip"), {}}
    };

    for (int day = 1; day <= 3; ++day) {
        const ApplicationDataBackupResult backup = createAutomaticApplicationDataBackup(
            root,
            items,
            2,
            QDateTime(QDate(2026, 1, day), QTime(12, 0), QTimeZone::UTC));
        QVERIFY2(backup.success, qPrintable(backup.error));
        QVERIFY(QFileInfo::exists(QDir(backup.directoryPath).filePath(QStringLiteral("pinloom.sqlite3"))));
        QVERIFY(QFileInfo::exists(QDir(backup.directoryPath).filePath(QStringLiteral("pinloom_clip.sqlite3"))));
        QVERIFY(QFileInfo::exists(QDir(backup.directoryPath).filePath(QStringLiteral("manifest.txt"))));
    }
    QCOMPARE(QDir(root).entryList({QStringLiteral("snapshot-*")}, QDir::Dirs | QDir::NoDotAndDotDot).size(), 2);

    const QList<ApplicationDataBackupItem> failingItems = {
        {QStringLiteral("pinloom.sqlite3"), writer("anchor"), {}},
        {QStringLiteral("pinloom_clip.sqlite3"), [](const QString &) { return false; },
         []() { return QStringLiteral("forced backup failure"); }}
    };
    const ApplicationDataBackupResult failed = createAutomaticApplicationDataBackup(
        root,
        failingItems,
        2,
        QDateTime(QDate(2026, 1, 4), QTime(12, 0), QTimeZone::UTC));
    QVERIFY(!failed.success);
    QCOMPARE(failed.error, QStringLiteral("forced backup failure"));
    QCOMPARE(QDir(root).entryList({QStringLiteral("snapshot-*")}, QDir::Dirs | QDir::NoDotAndDotDot).size(), 2);
    QVERIFY(QDir(root).entryList({QStringLiteral(".*-partial-*")}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());

    const ApplicationDataBackupResult reservedName = createAutomaticApplicationDataBackup(
        root,
        {{QStringLiteral("manifest.txt"), writer("collision"), {}}},
        2,
        QDateTime(QDate(2026, 1, 5), QTime(12, 0), QTimeZone::UTC));
    QVERIFY(!reservedName.success);
}

void CoreSmokeTest::persistsAndSearchesAnchorLocatorFields()
{
    InMemoryLibraryRepository repository;

    Anchor anchor;
    anchor.id = QStringLiteral("anchor:clock-domain");
    anchor.name = QStringLiteral("Clock domain window");
    anchor.targetApp = QStringLiteral("SumatraPDF");
    anchor.targetFile = QStringLiteral("E:/specs/clocking.pdf");
    anchor.locatorType = QStringLiteral("sumatrapdf.rect");
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

    const QList<SearchResult> metadataResults = repository.search(SearchQuery{QStringLiteral("sumatrapdf.rect")});
    QCOMPARE(metadataResults.size(), 1);
    QCOMPARE(metadataResults.first().matchedField, QStringLiteral("anchor_metadata"));
}

void CoreSmokeTest::buildsSumatraPdfRectCommand()
{
    Anchor anchor;
    anchor.targetApp = QStringLiteral("SumatraPDF");
    anchor.targetFile = QStringLiteral("E:/docs/clock.pdf");
    anchor.locatorType = QStringLiteral("sumatrapdf.rect");
    anchor.locatorJson = QStringLiteral("{\"type\":\"sumatrapdf.rect\",\"page\":12,\"rect\":[420.4,859.6,780,920],\"zoom\":250,\"unit\":\"pt\"}");

    QVERIFY(isSumatraPdfAnchor(anchor));
    const SumatraPdfCommandResult result =
        buildSumatraPdfCommand(anchor, QString(), QStringLiteral("C:/Tools/SumatraPDF.exe"));

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.executablePath, QStringLiteral("C:/Tools/SumatraPDF.exe"));
    QCOMPARE(result.command.filePath, anchor.targetFile);
    QCOMPARE(result.command.arguments,
             QStringList({QStringLiteral("-reuse-instance"),
                          QStringLiteral("-page"),
                          QStringLiteral("12"),
                          QStringLiteral("-zoom"),
                          QStringLiteral("250"),
                          QStringLiteral("-scroll"),
                          QStringLiteral("420,860"),
                          anchor.targetFile}));
}

void CoreSmokeTest::buildsSumatraPdfViewRectCommand()
{
    Anchor anchor;
    anchor.targetApp = QStringLiteral("SumatraPDF");
    anchor.targetFile = QStringLiteral("E:/docs/clock.pdf");
    anchor.locatorType = QStringLiteral("sumatrapdf.rect");
    anchor.locatorJson = QStringLiteral(
        "{\"type\":\"sumatrapdf.rect\",\"page\":12,\"rect\":[420,860,780,920],\"zoom\":250,\"unit\":\"pt\",\"mode\":\"viewrect\"}");

    const SumatraPdfCommandResult result =
        buildSumatraPdfCommand(anchor, QString(), QStringLiteral("C:/Tools/SumatraPDF.exe"));

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.arguments,
             QStringList({QStringLiteral("-reuse-instance"),
                          QStringLiteral("-page"),
                          QStringLiteral("12"),
                          QStringLiteral("-zoom"),
                          QStringLiteral("250"),
                          QStringLiteral("-scroll"),
                          QStringLiteral("420,860"),
                          anchor.targetFile}));
}

void CoreSmokeTest::buildsSumatraPdfTextCommand()
{
    Anchor anchor;
    anchor.targetApp = QStringLiteral("SumatraPDF");
    anchor.targetFile = QStringLiteral("E:/docs/clock.pdf");
    anchor.locatorType = QStringLiteral("sumatrapdf.search");
    anchor.locatorJson = QStringLiteral("{\"type\":\"sumatrapdf.search\",\"page\":12,\"text\":\"clock; domain \\\"crossing\\\"\",\"zoom\":250}");

    QVERIFY(isSumatraPdfAnchor(anchor));
    const SumatraPdfCommandResult result =
        buildSumatraPdfCommand(anchor, QString(), QStringLiteral("C:/Tools/SumatraPDF.exe"));

    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.command.arguments,
             QStringList({QStringLiteral("-reuse-instance"),
                          QStringLiteral("-page"),
                          QStringLiteral("12"),
                          QStringLiteral("-zoom"),
                          QStringLiteral("250"),
                          QStringLiteral("-search"),
                          QStringLiteral("clock; domain crossing"),
                          anchor.targetFile}));
}

void CoreSmokeTest::reportsMissingSumatraPdfTargetPath()
{
    Anchor anchor;
    anchor.targetApp = QStringLiteral("sumatrapdf");
    anchor.locatorType = QStringLiteral("sumatrapdf.page");
    anchor.locatorJson = QStringLiteral("{\"page\":4}");

    const SumatraPdfCommandResult result =
        buildSumatraPdfCommand(anchor, QString(), QStringLiteral("C:/Tools/SumatraPDF.exe"));

    QVERIFY(!result.success());
    QCOMPARE(result.error, QStringLiteral("SumatraPDF target file is missing"));
}

void CoreSmokeTest::resolvesSumatraPdfExecutableFromEnvironment()
{
    const bool hadValue = qEnvironmentVariableIsSet("PINLOOM_SUMATRAPDF_PATH");
    const QByteArray previous = qgetenv("PINLOOM_SUMATRAPDF_PATH");

    QVERIFY(qputenv("PINLOOM_SUMATRAPDF_PATH", "C:/Portable PDF/SumatraPDF.exe"));
    const QString resolved = resolveSumatraPdfExecutablePath();

    if (hadValue) {
        QVERIFY(qputenv("PINLOOM_SUMATRAPDF_PATH", previous));
    } else {
        qunsetenv("PINLOOM_SUMATRAPDF_PATH");
    }

    QCOMPARE(resolved, QStringLiteral("C:/Portable PDF/SumatraPDF.exe"));
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
    QVERIFY(!usesPowerShellLauncher(ExternalApplicationTarget::SumatraPDF));
    QCOMPARE(externalApplicationLabel(ExternalApplicationTarget::SumatraPDF),
             QStringLiteral("SumatraPDF"));

    const bool hadValue = qEnvironmentVariableIsSet("PINLOOM_SUMATRAPDF_PATH");
    const QByteArray previous = qgetenv("PINLOOM_SUMATRAPDF_PATH");

    QVERIFY(qputenv("PINLOOM_SUMATRAPDF_PATH", "C:/Portable PDF/SumatraPDF.exe"));
    QCOMPARE(resolveSumatraPdfExecutablePath(settings),
             QStringLiteral("C:/Portable PDF/SumatraPDF.exe"));

    Anchor anchor;
    anchor.targetFile = QStringLiteral("E:/docs/spec.pdf");
    anchor.locatorType = QStringLiteral("sumatrapdf.page");
    anchor.locatorJson = QStringLiteral("{\"page\":4}");
    const SumatraPdfCommandResult command = buildSumatraPdfCommand(anchor, QString(), settings);

    if (hadValue) {
        QVERIFY(qputenv("PINLOOM_SUMATRAPDF_PATH", previous));
    } else {
        qunsetenv("PINLOOM_SUMATRAPDF_PATH");
    }

    QVERIFY2(command.success(), qPrintable(command.error));
    QCOMPARE(command.command.executablePath, QStringLiteral("C:/Portable PDF/SumatraPDF.exe"));
}

void CoreSmokeTest::appliesExplicitApplicationLaunchSettings()
{
    ApplicationLaunchSettings settings;
    settings.sumatraPdfExecutablePath = QStringLiteral(" C:/Pinned/SumatraPDF.exe ");
    settings.powerShellExecutablePath = QStringLiteral(" C:/Tools/PowerShell/powershell.exe ");

    QCOMPARE(resolveSumatraPdfExecutablePath(settings), QStringLiteral("C:/Pinned/SumatraPDF.exe"));
    QCOMPARE(effectivePowerShellExecutablePath(settings),
             QStringLiteral("C:/Tools/PowerShell/powershell.exe"));

    Anchor pdfAnchor;
    pdfAnchor.targetFile = QStringLiteral("E:/docs/spec.pdf");
    pdfAnchor.locatorType = QStringLiteral("sumatrapdf.page");
    pdfAnchor.locatorJson = QStringLiteral("{\"page\":4}");
    const SumatraPdfCommandResult pdfCommand =
        buildSumatraPdfCommand(pdfAnchor, QString(), settings);
    QVERIFY2(pdfCommand.success(), qPrintable(pdfCommand.error));
    QCOMPARE(pdfCommand.command.executablePath, QStringLiteral("C:/Pinned/SumatraPDF.exe"));

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
    nameAnchor.locatorType = QStringLiteral("manual");
    nameAnchor.name = QStringLiteral("shared name");
    nameResource.anchors = {nameAnchor};
    QVERIFY(repository.upsertResource(nameResource));

    Resource aliasResource;
    aliasResource.id = QStringLiteral("alias-anchor");
    aliasResource.kind = ResourceKind::ManualAnchor;
    aliasResource.title = QStringLiteral("Alpha");
    aliasResource.location = QStringLiteral("alias.pinloom");
    Anchor aliasAnchor;
    aliasAnchor.locatorType = QStringLiteral("manual");
    aliasAnchor.name = QStringLiteral("alias carrier");
    aliasAnchor.aliases = {QStringLiteral("shared alias")};
    aliasResource.anchors = {aliasAnchor};
    QVERIFY(repository.upsertResource(aliasResource));

    Resource tagResource;
    tagResource.id = QStringLiteral("tag-anchor");
    tagResource.kind = ResourceKind::ManualAnchor;
    tagResource.title = QStringLiteral("Beta");
    tagResource.location = QStringLiteral("tag.pinloom");
    Anchor tagAnchor;
    tagAnchor.locatorType = QStringLiteral("manual");
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
    hotMetadataAnchor.locatorType = QStringLiteral("manual");
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
    coldMetadataAnchor.locatorType = QStringLiteral("manual");
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

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString filePath = dir.filePath(QStringLiteral("spec.pdf"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("pdf") > 0);
    file.close();

    InboxFileSaveRequest copyRequest;
    copyRequest.filePath = filePath;
    copyRequest.mode = InboxFileArchiveMode::Copy;
    QCOMPARE(inboxFileSaveRequestError(copyRequest),
             QStringLiteral("A managed Pinloom library directory is required"));
    copyRequest.managedLibraryDirectory = dir.filePath(QStringLiteral("managed"));
    QVERIFY(inboxFileSaveRequestError(copyRequest).isEmpty());

    InboxFileSaveRequest folderRequest;
    folderRequest.filePath = dir.path();
    QVERIFY(inboxFileSaveRequestError(folderRequest).isEmpty());
    folderRequest.mode = InboxFileArchiveMode::Copy;
    QCOMPARE(inboxFileSaveRequestError(folderRequest),
             QStringLiteral("Folders can only remain in their original location"));

    InboxFileSaveRequest fileRootRequest;
    fileRootRequest.filePath = filePath;
    fileRootRequest.registerAsLibraryRoot = true;
    QCOMPARE(inboxFileSaveRequestError(fileRootRequest),
             QStringLiteral("Only a folder can be registered as a library root"));
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
    QVERIFY(stored->explicitlyRetained);
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

void CoreSmokeTest::archivesInboxFilesAndRegistersLibraryRoots()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString rootPath = dir.filePath(QStringLiteral("PinloomRoot"));
    const QString dataPath = QDir(rootPath).filePath(QStringLiteral("_PinloomData"));
    const QString documentsPath = QDir(rootPath).filePath(QStringLiteral("Documents"));
    QVERIFY(QDir().mkpath(dataPath));
    QVERIFY(QDir().mkpath(documentsPath));

    const QString sourcePath = QDir(documentsPath).filePath(QStringLiteral("board.pdf"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write("board-pdf"), qint64(9));
    source.close();

    QCOMPARE(defaultPinloomSyncRootPath(dataPath), normalizedLibraryRootPath(rootPath));
    LibraryRoot syncRoot = makeLibraryRootForPath(rootPath, true);
    QVERIFY(syncRoot.syncRoot);
    QVERIFY(syncRoot.ignoredDirectoryNames.contains(QStringLiteral("_PinloomData")));
    QVERIFY(libraryRootContainsPath(syncRoot, sourcePath));
    QVERIFY(libraryRootIgnoresPath(syncRoot,
                                   QDir(dataPath).filePath(QStringLiteral("pinloom.sqlite3"))));
    QVERIFY(!libraryRootIgnoresPath(syncRoot, sourcePath));

    InMemoryLibraryRepository repository;
    QVERIFY(repository.upsertLibraryRoot(syncRoot));

    InboxFileSaveRequest rootRequest;
    rootRequest.filePath = rootPath;
    rootRequest.name = QStringLiteral("Synchronized material");
    rootRequest.tags = {QStringLiteral("#source")};
    rootRequest.registerAsLibraryRoot = true;
    rootRequest.ignoredDirectoryNames = {QStringLiteral("Cache")};
    const InboxFileSaveResult rootResult = saveInboxFile(repository, rootRequest);
    QVERIFY2(rootResult.success(), qPrintable(rootResult.status));
    const std::optional<Resource> rootResource = repository.findResource(rootResult.resourceId);
    QVERIFY(rootResource.has_value());
    QCOMPARE(rootResource->kind, ResourceKind::Folder);
    QCOMPARE(rootResource->tags, QStringList{QStringLiteral("source")});
    QVERIFY(!rootResource->explicitlyRetained);

    const QList<LibraryRoot> roots = repository.libraryRoots();
    QCOMPARE(roots.size(), 1);
    QCOMPARE(roots.first().id, libraryRootIdForPath(rootPath));
    QCOMPARE(roots.first().displayName, QStringLiteral("Synchronized material"));
    QVERIFY(roots.first().syncRoot);
    QVERIFY(roots.first().ignoredDirectoryNames.contains(QStringLiteral("_PinloomData")));
    QVERIFY(roots.first().ignoredDirectoryNames.contains(QStringLiteral("Cache")));
    QVERIFY(!repository.removeLibraryRoot(roots.first().id));

    InboxFileSaveRequest copyRequest;
    copyRequest.filePath = sourcePath;
    copyRequest.name = QStringLiteral("Managed board PDF");
    copyRequest.tags = {QStringLiteral("managed")};
    copyRequest.mode = InboxFileArchiveMode::Copy;
    copyRequest.managedLibraryDirectory = QDir(dataPath).filePath(QStringLiteral("managed-library"));
    const InboxFileSaveResult copyResult = saveInboxFile(repository, copyRequest);
    QVERIFY2(copyResult.success(), qPrintable(copyResult.status));
    QVERIFY(QFileInfo::exists(sourcePath));
    QVERIFY(QFileInfo::exists(copyResult.filePath));
    QVERIFY(copyResult.filePath != normalizedInboxFilePath(sourcePath));
    QCOMPARE(copyResult.filePath,
             managedInboxFilePath(sourcePath, copyRequest.managedLibraryDirectory));
    const std::optional<Resource> copiedResource = repository.findResource(copyResult.resourceId);
    QVERIFY(copiedResource.has_value());
    QCOMPARE(copiedResource->kind, ResourceKind::Pdf);
    QCOMPARE(copiedResource->location, copyResult.filePath);
    QCOMPARE(copiedResource->tags, QStringList{QStringLiteral("managed")});
}

void CoreSmokeTest::configuresDefaultLibraryRootWithoutFixedDrive()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString ordinaryDataPath = dir.filePath(QStringLiteral("PinloomData"));
    QVERIFY(QDir().mkpath(ordinaryDataPath));
    QVERIFY(defaultPinloomSyncRootPath(ordinaryDataPath).isEmpty());

    const QString inferredRootPath = dir.filePath(QStringLiteral("PortableRoot"));
    const QString inferredDataPath =
        QDir(inferredRootPath).filePath(QStringLiteral("_PinloomData"));
    QVERIFY(QDir().mkpath(inferredDataPath));
    QCOMPARE(defaultPinloomSyncRootPath(inferredDataPath),
             normalizedLibraryRootPath(inferredRootPath));

    const QString previousRootPath = dir.filePath(QStringLiteral("PreviousRoot"));
    const QString selectedRootPath = dir.filePath(QStringLiteral("SelectedRoot"));
    QVERIFY(QDir().mkpath(previousRootPath));
    QVERIFY(QDir().mkpath(selectedRootPath));

    InMemoryLibraryRepository repository;
    LibraryRoot previousRoot = makeLibraryRootForPath(previousRootPath, true);
    previousRoot.displayName = QStringLiteral("Previous default");
    QVERIFY(repository.upsertLibraryRoot(previousRoot));

    LibraryRoot selectedRoot = makeLibraryRootForPath(selectedRootPath);
    selectedRoot.displayName = QStringLiteral("Cloud library");
    selectedRoot.ignoredDirectoryNames = {QStringLiteral("Cache")};
    QVERIFY(repository.upsertLibraryRoot(selectedRoot));

    QString error;
    QVERIFY2(configureDefaultLibraryRoot(repository, selectedRootPath, &error),
             qPrintable(error));
    const std::optional<LibraryRoot> configured =
        repository.findLibraryRoot(libraryRootIdForPath(selectedRootPath));
    QVERIFY(configured.has_value());
    QVERIFY(configured->syncRoot);
    QVERIFY(configured->enabled);
    QCOMPARE(configured->displayName, QStringLiteral("Cloud library"));
    QVERIFY(configured->ignoredDirectoryNames.contains(QStringLiteral("Cache")));
    QVERIFY(configured->ignoredDirectoryNames.contains(QStringLiteral("_PinloomData")));

    const std::optional<LibraryRoot> demoted =
        repository.findLibraryRoot(libraryRootIdForPath(previousRootPath));
    QVERIFY(demoted.has_value());
    QVERIFY(!demoted->syncRoot);
    QVERIFY(repository.removeLibraryRoot(demoted->id));

    QVERIFY2(configureDefaultLibraryRoot(repository, {}, &error), qPrintable(error));
    const std::optional<LibraryRoot> cleared =
        repository.findLibraryRoot(libraryRootIdForPath(selectedRootPath));
    QVERIFY(cleared.has_value());
    QVERIFY(!cleared->syncRoot);

    const QString missingRootPath = dir.filePath(QStringLiteral("MissingRoot"));
    QVERIFY(!configureDefaultLibraryRoot(repository, missingRootPath, &error));
    QVERIFY(error.contains(QStringLiteral("does not exist")));
    QCOMPARE(repository.libraryRoots().size(), 1);
    QVERIFY(!repository.libraryRoots().first().syncRoot);
}

void CoreSmokeTest::softDeletesAndRestoresAnchorsInSearch()
{
    InMemoryLibraryRepository repository;

    Anchor anchor;
    anchor.id = QStringLiteral("clock#anchor");
    anchor.name = QStringLiteral("Clock anchor");
    anchor.locatorType = QStringLiteral("manual");
    anchor.aliases = {QStringLiteral("clock alias")};
    anchor.tags = {QStringLiteral("review")};

    Resource resource;
    resource.id = QStringLiteral("clock");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Clock PDF");
    resource.location = QStringLiteral("E:/docs/clock.pdf");
    resource.anchors = {anchor};
    QVERIFY(repository.upsertResource(resource));

    QCOMPARE(repository.search(SearchQuery{QStringLiteral("Clock anchor")}).size(), 1);
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("clock alias")}).size(), 1);
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("review")}).size(), 1);
    QVERIFY(repository.softDeleteAnchor(resource.id, anchor));
    QVERIFY(repository.search(SearchQuery{QStringLiteral("Clock anchor")}).isEmpty());
    QVERIFY(repository.search(SearchQuery{QStringLiteral("clock alias")}).isEmpty());
    QVERIFY(repository.search(SearchQuery{QStringLiteral("review")}).isEmpty());

    SearchQuery deletedQuery{QStringLiteral("clock alias")};
    deletedQuery.includeDeleted = true;
    const QList<SearchResult> deletedResults = repository.search(deletedQuery);
    QCOMPARE(deletedResults.size(), 1);
    QVERIFY(deletedResults.first().matchedAnchor.has_value());
    QVERIFY(deletedResults.first().matchedAnchor->deleted);

    SearchQuery deletedOnlyQuery{QStringLiteral("clock alias")};
    deletedOnlyQuery.deletedOnly = true;
    const QList<SearchResult> deletedOnlyResults = repository.search(deletedOnlyQuery);
    QCOMPARE(deletedOnlyResults.size(), 1);
    QVERIFY(deletedOnlyResults.first().matchedAnchor.has_value());
    QVERIFY(deletedOnlyResults.first().matchedAnchor->deleted);

    QVERIFY(repository.restoreAnchor(resource.id, anchor));
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("Clock anchor")}).size(), 1);
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("clock alias")}).size(), 1);
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("review")}).size(), 1);
}

void CoreSmokeTest::managesAnchorLibraryMetadataTagsPathsAndDuplicates()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString originalPath = directory.filePath(QStringLiteral("original.pdf"));
    const QString replacementPath = directory.filePath(QStringLiteral("replacement.pdf"));
    for (const QString &path : {originalPath, replacementPath}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("pdf") > 0);
    }

    Anchor overview;
    overview.id = QStringLiteral("primary#overview");
    overview.name = QStringLiteral("Overview");
    overview.targetFile = originalPath;
    overview.locatorType = QStringLiteral("sumatrapdf.page");
    overview.locatorJson = QStringLiteral("{\"page\":2}");
    Resource primary;
    primary.id = QStringLiteral("primary");
    primary.kind = ResourceKind::Pdf;
    primary.title = QStringLiteral("Primary PDF");
    primary.location = originalPath;
    primary.tags = {QStringLiteral("hardware")};
    primary.anchors = {overview};

    Anchor details;
    details.id = QStringLiteral("duplicate#details");
    details.name = QStringLiteral("Details");
    details.targetFile = originalPath;
    details.locatorType = QStringLiteral("sumatrapdf.page");
    details.locatorJson = QStringLiteral("{\"page\":5}");
    Anchor duplicateOverview = overview;
    duplicateOverview.name = QStringLiteral("Overview from duplicate");
    duplicateOverview.aliases = {QStringLiteral("source overview")};
    duplicateOverview.tags = {QStringLiteral("source-tag")};
    duplicateOverview.deleted = true;
    Resource duplicate;
    duplicate.id = QStringLiteral("duplicate");
    duplicate.kind = ResourceKind::Pdf;
    duplicate.title = QStringLiteral("Duplicate PDF");
    duplicate.location = originalPath;
    duplicate.aliases = {QStringLiteral("secondary")};
    duplicate.tags = {QStringLiteral("review")};
    duplicate.anchors = {details, duplicateOverview};

    InMemoryLibraryRepository repository;
    QVERIFY(repository.upsertResource(primary));
    QVERIFY(repository.upsertResource(duplicate));
    AnchorLibraryManagementService service(repository);

    AnchorMetadataUpdate anchorUpdate;
    anchorUpdate.name = QStringLiteral("System overview");
    anchorUpdate.aliases = {QStringLiteral("architecture"), QStringLiteral("architecture")};
    anchorUpdate.tags = {QStringLiteral("core")};
    anchorUpdate.pinned = true;
    AnchorLibraryOperationResult result =
        service.updateAnchorMetadata({primary.id, overview}, anchorUpdate);
    QVERIFY(!result.success);
    QVERIFY(result.message.contains(QStringLiteral("conflicts with"), Qt::CaseInsensitive));
    QCOMPARE(repository.findResource(primary.id)->anchors.first().name,
             QStringLiteral("Overview"));
    anchorUpdate.aliases = {QStringLiteral("architecture")};
    result = service.updateAnchorMetadata({primary.id, overview}, anchorUpdate);
    QVERIFY2(result.success, qPrintable(result.message));
    std::optional<Resource> storedPrimary = repository.findResource(primary.id);
    QVERIFY(storedPrimary.has_value());
    QCOMPARE(storedPrimary->anchors.first().name, QStringLiteral("System overview"));
    QCOMPARE(storedPrimary->anchors.first().aliases, QStringList{QStringLiteral("architecture")});
    QVERIFY(storedPrimary->anchors.first().pinned);

    AnchorLocatorUpdate uriLocator;
    uriLocator.targetApp = QStringLiteral("Browser");
    uriLocator.targetUri = QStringLiteral("https://example.com/spec#clock");
    uriLocator.locatorType = QStringLiteral("url.fragment");
    uriLocator.locatorJson = QStringLiteral("{\"fragment\":\"clock\"}");
    result = service.updateAnchorLocator({primary.id, storedPrimary->anchors.first()}, uriLocator);
    QVERIFY2(result.success, qPrintable(result.message));
    storedPrimary = repository.findResource(primary.id);
    QVERIFY(storedPrimary->anchors.first().targetFile.isEmpty());
    QCOMPARE(storedPrimary->anchors.first().targetUri, uriLocator.targetUri);

    AnchorLocatorUpdate fileLocator;
    fileLocator.targetApp = QStringLiteral("SumatraPDF");
    fileLocator.targetFile = originalPath;
    fileLocator.locatorType = QStringLiteral("sumatrapdf.page");
    fileLocator.locatorJson = QStringLiteral("{\"page\":2}");
    result = service.updateAnchorLocator({primary.id, storedPrimary->anchors.first()}, fileLocator);
    QVERIFY2(result.success, qPrintable(result.message));
    storedPrimary = repository.findResource(primary.id);
    QCOMPARE(storedPrimary->anchors.first().targetFile,
             QDir::cleanPath(QFileInfo(originalPath).absoluteFilePath()));
    QVERIFY(storedPrimary->anchors.first().targetUri.isEmpty());

    result = service.updateAnchorTags({{primary.id, storedPrimary->anchors.first()},
                                       {duplicate.id, details}},
                                      {QStringLiteral("batch"), QStringLiteral("review")},
                                      false);
    QVERIFY2(result.success, qPrintable(result.message));
    QCOMPARE(result.affectedCount, 2);
    QVERIFY(repository.findResource(primary.id)->anchors.first().tags.contains(QStringLiteral("batch")));
    QVERIFY(repository.findResource(duplicate.id)->anchors.first().tags.contains(QStringLiteral("batch")));

    storedPrimary = repository.findResource(primary.id);
    result = service.setAnchorsDeleted({{primary.id, storedPrimary->anchors.first()}}, true);
    QVERIFY2(result.success, qPrintable(result.message));
    QVERIFY(repository.findResource(primary.id)->anchors.first().deleted);
    result = service.setAnchorsDeleted({{primary.id, repository.findResource(primary.id)->anchors.first()}}, false);
    QVERIFY2(result.success, qPrintable(result.message));
    QVERIFY(!repository.findResource(primary.id)->anchors.first().deleted);

    ResourceMetadataUpdate conflictingFileUpdate;
    conflictingFileUpdate.title = QStringLiteral("Managed PDF");
    conflictingFileUpdate.aliases = {QStringLiteral("managed")};
    conflictingFileUpdate.tags = {QStringLiteral("library")};
    result = service.updateResourceMetadata(
        {primary.id, duplicate.id}, conflictingFileUpdate);
    QVERIFY(!result.success);
    QVERIFY(result.message.contains(QStringLiteral("conflicts with"), Qt::CaseInsensitive));
    QCOMPARE(repository.findResource(primary.id)->title, QStringLiteral("Primary PDF"));
    QCOMPARE(repository.findResource(duplicate.id)->title, QStringLiteral("Duplicate PDF"));

    ResourceMetadataUpdate primaryFileUpdate;
    primaryFileUpdate.title = QStringLiteral("Managed Primary PDF");
    primaryFileUpdate.aliases = {QStringLiteral("managed")};
    primaryFileUpdate.tags = {QStringLiteral("library")};
    result = service.updateResourceMetadata({primary.id}, primaryFileUpdate);
    QVERIFY2(result.success, qPrintable(result.message));
    ResourceMetadataUpdate duplicateFileUpdate;
    duplicateFileUpdate.title = QStringLiteral("Managed Duplicate PDF");
    duplicateFileUpdate.aliases = {QStringLiteral("managed duplicate")};
    duplicateFileUpdate.tags = {QStringLiteral("library")};
    result = service.updateResourceMetadata({duplicate.id}, duplicateFileUpdate);
    QVERIFY2(result.success, qPrintable(result.message));
    QCOMPARE(repository.findResource(primary.id)->title, QStringLiteral("Managed Primary PDF"));
    QCOMPARE(repository.findResource(duplicate.id)->tags, QStringList{QStringLiteral("library")});

    result = service.relinkResources({primary.id, duplicate.id}, replacementPath);
    QVERIFY2(result.success, qPrintable(result.message));
    const QString normalizedReplacement = QDir::cleanPath(QFileInfo(replacementPath).absoluteFilePath());
    QCOMPARE(repository.findResource(primary.id)->location, normalizedReplacement);
    QCOMPARE(repository.findResource(duplicate.id)->anchors.first().targetFile, normalizedReplacement);

    Resource relinkedDuplicate = repository.findResource(duplicate.id).value();
    relinkedDuplicate.title = QStringLiteral("Alternate PDF title");
    relinkedDuplicate.aliases = {QStringLiteral("secondary")};
    QVERIFY(repository.upsertResource(relinkedDuplicate));

    result = service.mergeResources(primary.id, {duplicate.id});
    QVERIFY2(result.success, qPrintable(result.message));
    storedPrimary = repository.findResource(primary.id);
    QVERIFY(storedPrimary.has_value());
    QCOMPARE(storedPrimary->anchors.size(), 2);
    QVERIFY(storedPrimary->aliases.contains(QStringLiteral("managed")));
    QVERIFY(storedPrimary->aliases.contains(QStringLiteral("secondary")));
    QVERIFY(storedPrimary->aliases.contains(QStringLiteral("Alternate PDF title")));
    const Anchor *mergedOverview = nullptr;
    for (const Anchor &anchor : storedPrimary->anchors) {
        if (anchor.id == overview.id) {
            mergedOverview = &anchor;
            break;
        }
    }
    QVERIFY(mergedOverview != nullptr);
    QVERIFY(mergedOverview->aliases.contains(QStringLiteral("Overview from duplicate")));
    QVERIFY(mergedOverview->aliases.contains(QStringLiteral("source overview")));
    QVERIFY(mergedOverview->tags.contains(QStringLiteral("source-tag")));
    QVERIFY(mergedOverview->pinned);
    QVERIFY(!mergedOverview->deleted);
    const std::optional<Resource> storedDuplicate = repository.findResource(duplicate.id);
    QVERIFY(storedDuplicate.has_value());
    QVERIFY(storedDuplicate->deleted);
}

void CoreSmokeTest::managesAnchorLibraryLifecycleIntegrityHistoryAndAutoRelink()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString missingPath = directory.filePath(QStringLiteral("old/design.pdf"));
    const QString foundDirectory = directory.filePath(QStringLiteral("found/reference"));
    QVERIFY(QDir().mkpath(foundDirectory));
    const QString foundPath = QDir(foundDirectory).filePath(QStringLiteral("design.pdf"));
    QFile foundFile(foundPath);
    QVERIFY(foundFile.open(QIODevice::WriteOnly));
    QVERIFY(foundFile.write("pdf") > 0);
    foundFile.close();

    Anchor region;
    region.id = QStringLiteral("design#region");
    region.name = QStringLiteral("Power stage");
    region.targetApp = QStringLiteral("SumatraPDF");
    region.targetFile = missingPath;
    region.locatorType = QStringLiteral("sumatrapdf.rect");
    region.locatorJson = QStringLiteral("{\"page\":3,\"rect\":[10,20,110,80],\"unit\":\"pt\"}");
    region.tags = {QStringLiteral("legacy")};
    Anchor duplicateRegion = region;
    duplicateRegion.id = QStringLiteral("design#region-copy");
    duplicateRegion.name = QStringLiteral("Power stage duplicate");

    Resource design;
    design.id = QStringLiteral("design");
    design.kind = ResourceKind::Pdf;
    design.title = QStringLiteral("Design PDF");
    design.location = missingPath;
    design.tags = {QStringLiteral("legacy")};
    design.anchors = {region, duplicateRegion};
    Resource duplicateDesign = design;
    duplicateDesign.id = QStringLiteral("design-copy");
    duplicateDesign.title = QStringLiteral("Design PDF copy");
    Anchor duplicateDesignRegion = region;
    duplicateDesignRegion.id = QStringLiteral("design-copy#region");
    duplicateDesignRegion.name = QStringLiteral("Power stage mirror");
    duplicateDesign.anchors = {duplicateDesignRegion};

    InMemoryLibraryRepository repository;
    QVERIFY(repository.upsertResource(design));
    QVERIFY(repository.upsertResource(duplicateDesign));
    AnchorLibraryManagementService service(repository);

    AnchorLibraryIntegrityReport report = service.inspectIntegrity();
    QCOMPARE(report.missingTargetCount, 2);
    QCOMPARE(report.duplicateResourceCount, 1);
    QCOMPARE(report.duplicateAnchorCount, 0);
    QVERIFY(report.invalidLocatorCount >= 2);

    AnchorLibraryOperationResult result =
        service.autoRelinkMissingResources({design.id, duplicateDesign.id}, directory.path());
    QVERIFY2(result.success, qPrintable(result.message));
    QCOMPARE(result.affectedCount, 2);
    QCOMPARE(result.unresolvedCount, 0);
    const QString normalizedFoundPath = QDir::cleanPath(QFileInfo(foundPath).absoluteFilePath());
    QCOMPARE(repository.findResource(design.id)->location, normalizedFoundPath);
    QCOMPARE(repository.findResource(design.id)->anchors.first().targetFile, normalizedFoundPath);
    QVERIFY(service.validateAnchor(repository.findResource(design.id).value(),
                                   repository.findResource(design.id)->anchors.first()).valid);

    result = service.deduplicateAnchors({design.id});
    QVERIFY2(result.success, qPrintable(result.message));
    QCOMPARE(result.affectedCount, 0);
    QCOMPARE(repository.findResource(design.id)->anchors.size(), 2);

    Anchor storedAnchor = repository.findResource(design.id)->anchors.first();
    result = service.setAnchorsPinned({{design.id, storedAnchor}}, true);
    QVERIFY(result.success);
    result = service.setResourcesPinned({design.id, duplicateDesign.id}, true);
    QVERIFY(result.success);
    QVERIFY(repository.resourceUsage(design.id)->pinned);

    result = service.renameTag(QStringLiteral("legacy"), QStringLiteral("reviewed"));
    QVERIFY(result.success);
    QVERIFY(repository.findResource(design.id)->tags.contains(QStringLiteral("reviewed")));
    QVERIFY(repository.findResource(design.id)->anchors.first().tags.contains(QStringLiteral("reviewed")));
    result = service.deleteTag(QStringLiteral("reviewed"));
    QVERIFY(result.success);
    QVERIFY(repository.findResource(design.id)->tags.isEmpty());
    QVERIFY(repository.findResource(design.id)->anchors.first().tags.isEmpty());

    result = service.updateResourceTags({design.id}, {QStringLiteral("usage-safe-undo")}, false);
    QVERIFY(result.success);
    QVERIFY(repository.recordResourceOpen(design.id));
    QVERIFY(service.undoLast().success);
    QVERIFY(!repository.findResource(design.id)->tags.contains(QStringLiteral("usage-safe-undo")));
    result = service.updateResourceTags({design.id}, {QStringLiteral("stale-undo")}, false);
    QVERIFY(result.success);
    Resource externalChange = repository.findResource(design.id).value();
    externalChange.aliases.append(QStringLiteral("external change"));
    QVERIFY(repository.upsertResource(externalChange));
    QVERIFY(!service.undoLast().success);
    QVERIFY(!service.canUndo());

    storedAnchor = repository.findResource(design.id)->anchors.first();
    result = service.setAnchorsDeleted({{design.id, storedAnchor}}, true);
    QVERIFY(result.success);
    QVERIFY(service.canUndo());
    result = service.undoLast();
    QVERIFY(result.success);
    QVERIFY(!repository.findResource(design.id)->anchors.first().deleted);

    storedAnchor = repository.findResource(design.id)->anchors.first();
    QVERIFY(service.setAnchorsDeleted({{design.id, storedAnchor}}, true).success);
    storedAnchor = repository.findResource(design.id)->anchors.first();
    result = service.permanentlyDeleteAnchors({{design.id, storedAnchor}});
    QVERIFY2(result.success, qPrintable(result.message));
    QCOMPARE(repository.findResource(design.id)->anchors.size(), 1);
    QVERIFY(!service.canUndo());

    Resource metadataTrash = repository.findResource(duplicateDesign.id).value();
    metadataTrash.aliases = {QStringLiteral("archived alias")};
    metadataTrash.tags = {QStringLiteral("archived tag")};
    QVERIFY(repository.upsertResource(metadataTrash));
    QVERIFY(service.setResourcesDeleted({duplicateDesign.id}, true).success);
    result = service.permanentlyClearResourceMetadata({duplicateDesign.id});
    QVERIFY2(result.success, qPrintable(result.message));
    const std::optional<Resource> clearedMetadata = repository.findResource(duplicateDesign.id);
    QVERIFY(clearedMetadata.has_value());
    QVERIFY(!clearedMetadata->deleted);
    QVERIFY(clearedMetadata->aliases.isEmpty());
    QVERIFY(clearedMetadata->tags.isEmpty());
    QVERIFY(!clearedMetadata->anchors.isEmpty());
    QVERIFY(!service.canUndo());

    QVERIFY(service.setResourcesDeleted({duplicateDesign.id}, true).success);
    result = service.permanentlyDeleteResources({duplicateDesign.id});
    QVERIFY(result.success);
    QVERIFY(!repository.findResource(duplicateDesign.id).has_value());
    QVERIFY(!service.canUndo());
}

void CoreSmokeTest::clearsOnlyAnchorlessFileMetadata_data()
{
    QTest::addColumn<bool>("useSqlite");
    QTest::newRow("in-memory") << false;
    QTest::newRow("sqlite") << true;
}

void CoreSmokeTest::clearsOnlyAnchorlessFileMetadata()
{
    QFETCH(bool, useSqlite);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("preserved.txt"));
    QFile source(path);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("preserved source");
    source.close();
    const QString databasePath = directory.filePath(QStringLiteral("metadata.sqlite3"));
    InMemoryLibraryRepository memory;
    SqliteLibraryRepository sqlite;
    if (useSqlite) {
        QVERIFY(sqlite.open(databasePath));
        QVERIFY(sqlite.initialize());
    }
    ILibraryRepository &repository = useSqlite ? static_cast<ILibraryRepository &>(sqlite) : memory;
    Resource first;
    first.id = QStringLiteral("clear-first");
    first.title = QStringLiteral("First unchanged title");
    first.location = path;
    first.aliases = {QStringLiteral("first alias")};
    first.tags = {QStringLiteral("first-tag")};
    first.explicitlyRetained = true;
    Anchor trashed = testAnchor(QStringLiteral("Trashed reference"));
    trashed.id = QStringLiteral("trashed-reference");
    trashed.aliases = {QStringLiteral("trashed alias")};
    trashed.tags = {QStringLiteral("trashed-tag")};
    trashed.deleted = true;
    first.anchors = {trashed};
    Resource second = first;
    second.id = QStringLiteral("clear-second");
    second.title = QStringLiteral("Second unchanged title");
    second.aliases = {QStringLiteral("second alias")};
    second.tags = {QStringLiteral("second-tag")};
    second.anchors.clear();
    Resource active = second;
    active.id = QStringLiteral("keep-active");
    active.title = QStringLiteral("Active file");
    active.aliases = {QStringLiteral("active alias")};
    active.anchors = {testAnchor(QStringLiteral("Active reference"))};
    Resource archived = second;
    archived.id = QStringLiteral("keep-archived");
    archived.title = QStringLiteral("Archived file");
    archived.deleted = true;
    for (const auto &resource : {first, second, active, archived})
        QVERIFY2(repository.upsertResource(resource), qPrintable(repository.lastError()));
    QVERIFY(repository.setResourcePinned(first.id, true));
    AnchorLibraryManagementService service(repository);
    int notifications = 0;
    const int listener = repository.addChangeListener([&](const LibraryChange &change) {
        if (change.kind == LibraryChangeKind::Content) ++notifications;
    });
    const quint64 initialRevision = repository.contentRevision();
    QVERIFY(!service.clearAnchorlessResourceMetadata({}).success);
    QVERIFY(!service.clearAnchorlessResourceMetadata({first.id, QStringLiteral("missing")}).success);
    QVERIFY(!service.clearAnchorlessResourceMetadata({first.id, active.id, second.id}).success);
    QVERIFY(!service.clearAnchorlessResourceMetadata({first.id, archived.id}).success);
    QCOMPARE(repository.findResource(first.id)->aliases, first.aliases);
    QCOMPARE(repository.findResource(second.id)->tags, second.tags);
    QCOMPARE(repository.contentRevision(), initialRevision);
    QCOMPARE(notifications, 0);
    QVERIFY(!service.canUndo());

    auto result = service.clearAnchorlessResourceMetadata({first.id, second.id, first.id});
    QVERIFY2(result.success, qPrintable(result.message));
    QCOMPARE(result.affectedCount, 2);
    QCOMPARE(notifications, 1);
    for (const auto &original : {first, second}) {
        const Resource stored = repository.findResource(original.id).value();
        QVERIFY(stored.aliases.isEmpty());
        QVERIFY(stored.tags.isEmpty());
        QVERIFY(!stored.deleted);
        QCOMPARE(stored.title, original.title);
        QCOMPARE(stored.location, original.location);
        QCOMPARE(stored.explicitlyRetained, original.explicitlyRetained);
        QCOMPARE(stored.anchors.size(), original.anchors.size());
    }
    const Anchor kept = repository.findResource(first.id)->anchors.first();
    QVERIFY(kept.deleted);
    QCOMPARE(kept.id, trashed.id);
    QCOMPARE(kept.aliases, trashed.aliases);
    QCOMPARE(kept.tags, trashed.tags);
    QVERIFY(repository.resourceUsage(first.id)->pinned);
    QVERIFY(service.canUndo());
    QVERIFY(service.undoLast().success);
    QCOMPARE(repository.findResource(first.id)->aliases, first.aliases);
    QCOMPARE(repository.findResource(second.id)->tags, second.tags);
    QVERIFY(service.clearAnchorlessResourceMetadata({first.id, second.id}).success);
    const quint64 clearedRevision = repository.contentRevision();
    result = service.clearAnchorlessResourceMetadata({first.id, second.id});
    QVERIFY(result.success);
    QCOMPARE(result.affectedCount, 0);
    QCOMPARE(repository.contentRevision(), clearedRevision);
    QCOMPARE(repository.findResource(active.id)->aliases, active.aliases);
    QVERIFY(repository.findResource(archived.id)->deleted);
    if (useSqlite) {
        QVERIFY(sqlite.integrityCheck());
        SqliteLibraryRepository reopened;
        QVERIFY(reopened.open(databasePath));
        QVERIFY(reopened.initialize());
        QVERIFY(reopened.findResource(first.id)->aliases.isEmpty());
        QVERIFY(reopened.findResource(second.id)->tags.isEmpty());
        QVERIFY(reopened.findResource(first.id)->anchors.first().deleted);
    }
    Resource reuse;
    reuse.id = QStringLiteral("alias-reuse");
    reuse.title = QStringLiteral("Reused alias owner");
    reuse.location = path;
    reuse.aliases = first.aliases;
    QVERIFY2(repository.upsertResource(reuse), qPrintable(repository.lastError()));
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), QByteArray("preserved source"));
    repository.removeChangeListener(listener);
}

void CoreSmokeTest::cleansUnmarkedAnchorShellsWithoutDeletingFilesOrRoots()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString originalPath = directory.filePath(QStringLiteral("original.pdf"));
    QFile original(originalPath);
    QVERIFY(original.open(QIODevice::WriteOnly));
    QVERIFY(original.write("pdf") > 0);
    original.close();

    const QString rootPath = directory.filePath(QStringLiteral("Reference"));
    QVERIFY(QDir().mkpath(rootPath));

    const auto resourceWithTrashedAnchor = [](const QString &id,
                                               const QString &location,
                                               ResourceKind kind = ResourceKind::File) {
        Resource resource;
        resource.id = id;
        resource.kind = kind;
        resource.title = id;
        resource.location = location;
        Anchor anchor = testAnchor(QStringLiteral("Trashed anchor"));
        anchor.id = id + QStringLiteral("#anchor");
        anchor.deleted = true;
        resource.anchors = {anchor};
        return resource;
    };

    Resource unmarked = resourceWithTrashedAnchor(QStringLiteral("unmarked"), originalPath);
    Resource aliasMarked = resourceWithTrashedAnchor(QStringLiteral("alias-marked"), originalPath);
    aliasMarked.aliases = {QStringLiteral("kept alias")};
    Resource tagMarked = resourceWithTrashedAnchor(QStringLiteral("tag-marked"), originalPath);
    tagMarked.tags = {QStringLiteral("kept-tag")};
    Resource pinned = resourceWithTrashedAnchor(QStringLiteral("pinned"), originalPath);
    Resource retained = resourceWithTrashedAnchor(QStringLiteral("retained"), originalPath);
    retained.explicitlyRetained = true;
    Resource rootBacked = resourceWithTrashedAnchor(QStringLiteral("root-backed"),
                                                     rootPath,
                                                     ResourceKind::Folder);

    InMemoryLibraryRepository repository;
    for (const Resource &resource : {unmarked, aliasMarked, tagMarked, pinned, retained, rootBacked}) {
        QVERIFY(repository.upsertResource(resource));
    }
    QVERIFY(repository.setResourcePinned(pinned.id, true));
    const LibraryRoot root = makeLibraryRootForPath(rootPath);
    QVERIFY(repository.upsertLibraryRoot(root));

    AnchorLibraryManagementService service(repository);
    QList<AnchorReference> references;
    for (const Resource &resource : {unmarked, aliasMarked, tagMarked, pinned, retained, rootBacked}) {
        references.append({resource.id, resource.anchors.first()});
    }
    const AnchorLibraryOperationResult result = service.permanentlyDeleteAnchors(references);
    QVERIFY2(result.success, qPrintable(result.message));
    QCOMPARE(result.affectedCount, 6);

    QVERIFY(!repository.findResource(unmarked.id).has_value());
    QVERIFY(QFileInfo::exists(originalPath));
    for (const QString &resourceId : {aliasMarked.id,
                                      tagMarked.id,
                                      pinned.id,
                                      retained.id,
                                      rootBacked.id}) {
        const std::optional<Resource> stored = repository.findResource(resourceId);
        QVERIFY(stored.has_value());
        QVERIFY(stored->anchors.isEmpty());
    }
    QCOMPARE(repository.libraryRoots().size(), 1);
    QCOMPARE(repository.libraryRoots().first().id, root.id);

    const ResourceUsage noUsage{QStringLiteral("inbox:file:internal-shell")};
    Resource internalShell;
    internalShell.id = noUsage.resourceId;
    internalShell.location = originalPath;
    QVERIFY(!shouldProvideAnchorLibraryResource(internalShell, noUsage));
    Resource retainedOnly = retained;
    retainedOnly.anchors.clear();
    QVERIFY(!shouldProvideAnchorLibraryResource(retainedOnly,
                                                ResourceUsage{retainedOnly.id}));
    QVERIFY(!shouldCleanupAnchorLibraryResource(retainedOnly,
                                                ResourceUsage{retainedOnly.id}, {}));
}

void CoreSmokeTest::archivesAnchorLibraryJsonAndPublishesAtomicChanges()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    Resource resource;
    resource.id = QStringLiteral("archive-note");
    resource.kind = ResourceKind::Note;
    resource.title = QStringLiteral("Archive note");
    resource.location = QStringLiteral("note://archive");
    resource.tags = {QStringLiteral("portable")};
    resource.explicitlyRetained = true;
    Anchor anchor = testAnchor(QStringLiteral("Section"), QStringLiteral("text.heading"));
    anchor.id = QStringLiteral("archive-note#section");
    anchor.aliases = {QStringLiteral("part")};
    anchor.pinned = true;
    resource.anchors = {anchor};

    InMemoryLibraryRepository repository;
    QVERIFY(repository.upsertResource(resource));
    int notificationCount = 0;
    LibraryChange lastChange;
    const int listenerId = repository.addChangeListener([&](const LibraryChange &change) {
        ++notificationCount;
        lastChange = change;
    });

    Resource changed = resource;
    changed.title = QStringLiteral("Should roll back");
    Resource invalid;
    LibraryBatchMutation invalidBatch;
    invalidBatch.upserts = {changed, invalid};
    QVERIFY(!repository.applyBatch(invalidBatch));
    QCOMPARE(repository.findResource(resource.id)->title, resource.title);
    QCOMPARE(notificationCount, 0);

    AnchorLibraryArchiveService archive(repository);
    QVERIFY(!archive.exportJson(QString()).success);
    const QString archivePath = directory.filePath(QStringLiteral("library.json"));
    AnchorLibraryOperationResult result = archive.exportJson(archivePath);
    QVERIFY2(result.success, qPrintable(result.message));
    QVERIFY(QFileInfo::exists(archivePath));

    QVERIFY(repository.clearResources());
    QCOMPARE(notificationCount, 1);
    QCOMPARE(lastChange.kind, LibraryChangeKind::Reset);
    notificationCount = 0;
    result = archive.importJson(archivePath, AnchorLibraryImportMode::Replace);
    QVERIFY2(result.success, qPrintable(result.message));
    QCOMPARE(notificationCount, 1);
    QCOMPARE(lastChange.kind, LibraryChangeKind::Reset);
    const std::optional<Resource> imported = repository.findResource(resource.id);
    QVERIFY(imported.has_value());
    QCOMPARE(imported->title, resource.title);
    QVERIFY(imported->explicitlyRetained);
    QCOMPARE(imported->anchors.first().aliases, anchor.aliases);
    QVERIFY(imported->anchors.first().pinned);

    const QString invalidPath = directory.filePath(QStringLiteral("invalid.json"));
    QFile invalidArchive(invalidPath);
    QVERIFY(invalidArchive.open(QIODevice::WriteOnly));
    invalidArchive.write("{not-json");
    invalidArchive.close();
    result = archive.importJson(invalidPath, AnchorLibraryImportMode::Replace);
    QVERIFY(!result.success);
    QVERIFY(repository.findResource(resource.id).has_value());
    repository.removeChangeListener(listenerId);
}

void CoreSmokeTest::softDeletesAndRestoresInboxResourcesWithoutDeletingOriginalFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString filePath = dir.filePath(QStringLiteral("inbox.txt"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("inbox") > 0);
    file.close();

    InMemoryLibraryRepository repository;
    InboxFileSaveRequest request;
    request.filePath = filePath;
    request.name = QStringLiteral("Inbox target");
    const InboxFileSaveResult result = saveInboxFile(repository, request);
    QVERIFY(result.success());
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("Inbox target")}).size(), 1);

    QVERIFY(repository.softDeleteResource(result.resourceId));
    QVERIFY(QFile::exists(filePath));
    QVERIFY(repository.search(SearchQuery{QStringLiteral("Inbox target")}).isEmpty());

    SearchQuery deletedQuery{QStringLiteral("Inbox target")};
    deletedQuery.includeDeleted = true;
    const QList<SearchResult> deletedResults = repository.search(deletedQuery);
    QCOMPARE(deletedResults.size(), 1);
    QVERIFY(deletedResults.first().resource.deleted);

    QVERIFY(repository.restoreResource(result.resourceId));
    QVERIFY(QFile::exists(filePath));
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("Inbox target")}).size(), 1);
}

void CoreSmokeTest::recognizesExplorerForegroundWindows()
{
    ForegroundAppWindowContext explorer;
    explorer.processName = QStringLiteral("explorer.exe");
    QVERIFY(isExplorerForegroundWindow(explorer));

    ForegroundAppWindowContext explorerPath;
    explorerPath.processPath = QStringLiteral("C:/Windows/explorer.exe");
    QVERIFY(isExplorerForegroundWindow(explorerPath));

    ForegroundAppWindowContext sumatraPdf;
    sumatraPdf.processName = QStringLiteral("SumatraPDF.exe");
    QVERIFY(!isExplorerForegroundWindow(sumatraPdf));

    const ExplorerFileSelectionResult result = captureExplorerFileSelection(sumatraPdf);
    QVERIFY(!result.success());
    QVERIFY(!result.recognizedExplorer);
}

void CoreSmokeTest::ranksAnchorBeforePathMatches()
{
    InMemoryLibraryRepository repository;

    Resource anchored;
    anchored.id = QStringLiteral("anchored");
    anchored.kind = ResourceKind::File;
    anchored.title = QStringLiteral("note.md");
    anchored.location = QStringLiteral("E:/test_dir/note.md");
    anchored.anchors = {testAnchor(QStringLiteral("test"), QStringLiteral("text.heading"), 12)};
    QVERIFY(repository.upsertResource(anchored));

    Resource pathOnly;
    pathOnly.id = QStringLiteral("path-only");
    pathOnly.kind = ResourceKind::Pdf;
    pathOnly.title = QStringLiteral("ISO.pdf");
    pathOnly.location = QStringLiteral("E:/test_dir/ISO.pdf");
    QVERIFY(repository.upsertResource(pathOnly));

    const QList<SearchResult> results = repository.search(SearchQuery{QStringLiteral("test")});
    QVERIFY(results.size() >= 2);
    QCOMPARE(results.first().matchedField, QStringLiteral("anchor_name"));
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
    partialAnchor.anchors = {
        testAnchor(QStringLiteral("Power sequencing"), QStringLiteral("text.heading"), 7)};
    QVERIFY(repository.upsertResource(partialAnchor));

    Resource exactAnchor;
    exactAnchor.id = QStringLiteral("exact-anchor");
    exactAnchor.kind = ResourceKind::File;
    exactAnchor.title = QStringLiteral("b.md");
    exactAnchor.location = QStringLiteral("b.md");
    exactAnchor.anchors = {
        testAnchor(QStringLiteral("Power"), QStringLiteral("text.heading"), 3)};
    QVERIFY(repository.upsertResource(exactAnchor));

    const QList<SearchResult> anchorResults = repository.search(SearchQuery{QStringLiteral("Power")});
    QVERIFY(anchorResults.size() >= 2);
    QCOMPARE(anchorResults.first().resource.id, exactAnchor.id);
    QCOMPARE(anchorResults.first().matchedField, QStringLiteral("anchor_name"));
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
    QVERIFY(results.first().resourceUsageLoaded);
    QVERIFY(results.first().resourceUsage.has_value());
    QCOMPARE(results.first().resourceUsage->resourceId, usage->resourceId);
    QCOMPARE(results.first().resourceUsage->openCount, usage->openCount);
    QCOMPARE(results.first().resourceUsage->lastOpenedAt, usage->lastOpenedAt);
    QCOMPARE(results.first().resourceUsage->pinned, usage->pinned);
    QCOMPARE(results.last().resource.id, cold.id);
    QVERIFY(results.last().resourceUsageLoaded);
    QVERIFY(!results.last().resourceUsage.has_value());

    QVERIFY(repository.setResourcePinned(hot.id, false));
    const auto unpinned = repository.search(SearchQuery{QStringLiteral("UART Zulu")});
    QCOMPARE(unpinned.size(), 1);
    QVERIFY(unpinned.first().resourceUsageLoaded);
    QVERIFY(unpinned.first().resourceUsage.has_value());
    QVERIFY(!unpinned.first().resourceUsage->pinned);
    QCOMPARE(unpinned.first().resourceUsage->openCount, 2);
    QCOMPARE(unpinned.first().resourceUsage->lastOpenedAt, usage->lastOpenedAt);

    QVERIFY(repository.recordResourceOpen(hot.id));
    const auto opened = repository.search(SearchQuery{QStringLiteral("UART Zulu")});
    QCOMPARE(opened.size(), 1);
    QVERIFY(opened.first().resourceUsageLoaded);
    QVERIFY(opened.first().resourceUsage.has_value());
    QCOMPARE(opened.first().resourceUsage->openCount, 3);
    QCOMPARE(opened.first().resourceUsage->lastOpenedAt,
             repository.resourceUsage(hot.id)->lastOpenedAt);
    QVERIFY(!opened.first().resourceUsage->pinned);

    QVERIFY(repository.setResourcePinned(hot.id, true));
    const auto repinned = repository.search(SearchQuery{QStringLiteral("UART Zulu")});
    QCOMPARE(repinned.size(), 1);
    QVERIFY(repinned.first().resourceUsageLoaded);
    QVERIFY(repinned.first().resourceUsage.has_value());
    QVERIFY(repinned.first().resourceUsage->pinned);
    QCOMPARE(repinned.first().resourceUsage->openCount, 3);

    QVERIFY(repository.recordResourceOpen(cold.id));
    const auto newlyOpened = repository.search(SearchQuery{QStringLiteral("UART Alpha")});
    QCOMPARE(newlyOpened.size(), 1);
    QVERIFY(newlyOpened.first().resourceUsageLoaded);
    QVERIFY(newlyOpened.first().resourceUsage.has_value());
    QCOMPARE(newlyOpened.first().resourceUsage->resourceId, cold.id);
    QCOMPARE(newlyOpened.first().resourceUsage->openCount, 1);
    QCOMPARE(newlyOpened.first().resourceUsage->lastOpenedAt,
             repository.resourceUsage(cold.id)->lastOpenedAt);
    QVERIFY(!newlyOpened.first().resourceUsage->pinned);

    // Positive and missing usage snapshots belong to their original search.
    QVERIFY(results.first().resourceUsage->pinned);
    QCOMPARE(results.first().resourceUsage->openCount, 2);
    QCOMPARE(results.first().resourceUsage->lastOpenedAt, usage->lastOpenedAt);
    QVERIFY(!results.last().resourceUsage.has_value());
    QVERIFY(!unpinned.first().resourceUsage->pinned);
    QCOMPARE(unpinned.first().resourceUsage->openCount, 2);
    QVERIFY(!opened.first().resourceUsage->pinned);
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
    note.anchors = {
        testAnchor(QStringLiteral("Dock handoff"), QStringLiteral("text.heading"), 4)};
    QVERIFY(repository.upsertResource(note));

    Resource link;
    link.id = QStringLiteral("link");
    link.kind = ResourceKind::Url;
    link.title = QStringLiteral("UART Link");
    link.location = QStringLiteral("https://docs.example.com/uart");
    link.anchors = {testAnchor(QStringLiteral("Dock handoff link"), QStringLiteral("url.fragment"))};
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

void CoreSmokeTest::ranksOpenedAnchorsWithinAnchorMatches()
{
    InMemoryLibraryRepository repository;

    Resource cold;
    cold.id = QStringLiteral("cold-anchor");
    cold.kind = ResourceKind::File;
    cold.title = QStringLiteral("Alpha");
    cold.location = QStringLiteral("alpha.md");
    cold.anchors = {
        testAnchor(QStringLiteral("Power rail cold"), QStringLiteral("text.heading"), 1)};
    QVERIFY(repository.upsertResource(cold));

    Resource hot;
    hot.id = QStringLiteral("hot-anchor");
    hot.kind = ResourceKind::File;
    hot.title = QStringLiteral("Zulu");
    hot.location = QStringLiteral("zulu.md");
    hot.anchors = {
        testAnchor(QStringLiteral("Power rail hot"), QStringLiteral("text.heading"), 2)};
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
    QCOMPARE(anchorLocatorLine(results.first().matchedAnchor.value()), 2);
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
