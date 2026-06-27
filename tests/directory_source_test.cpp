#include "pinloom/core/DirectoryLibrarySource.h"
#include "pinloom/core/IndexingService.h"
#include "pinloom/core/SqliteLibraryRepository.h"

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <optional>

using namespace Pinloom;

class DirectorySourceTest : public QObject {
    Q_OBJECT

private slots:
    void scansOnlyExplicitRoot();
    void indexesPlainTextFileContent();
    void keepsPackageContainersAsPermanentPathOnlyFiles();
    void extractsTextStructureLineBeacons();
    void extractsNeutralWorkflowConfigLineBeacons();
    void extractsNeutralPipelineConfigLineBeacons();
    void extractsTextNamedEntryLineBeacons();
    void extractsTextHeadingAndBlockAnchors();
    void extractsTextConventionAliasTagLinkBeacons();
    void extractsTextConventionBodyContent();
    void extractsLocalTextLinkAnchors();
    void extractsTextTaskLineAnchors();
    void extractsInlineTextLinkUrlResources();
    void extractsReferenceStyleTextLinkUrlResources();
    void extractsPdfTitleAndPageAnchors();
    void extractsPdfContentText();
    void extractsUtf16PdfContentText();
    void extractsToUnicodePdfContentText();
    void extractsFlateEncodedPdfContentText();
    void extractsAsciiHexEncodedPdfContentText();
    void extractsAscii85EncodedPdfContentText();
    void extractsRunLengthEncodedPdfContentText();
    void extractsLzwEncodedPdfContentText();
    void extractsChainedFilterPdfContentText();
    void extractsPdfRegionAnchors();
    void extractsUnifiedTextBeaconAnchors();
    void extractsNeutralDirectiveTextBeaconAnchors();
    void extractsRuleAndContainerTextBeaconAnchors();
    void extractsFileReferenceManifestBeacons();
    void extractsTextActionLineAnchors();
    void extractsWebShortcutResources();
    void extractsContentSniffedTextUrlResources();
    void extractsEmailMessageUrlResources();
    void extractsIcalendarEventUrlResources();
    void extractsJsonUrlResources();
    void extractsHarEntryLinks();
    void extractsWarcResponseLinks();
    void extractsConfigTextUrlBeacons();
    void extractsTabularUrlResources();
    void extractsHtmlPageContent();
    void extractsMhtmlPageContent();
    void extractsBookmarkExportLinks();
    void extractsBrowserBookmarkJsonLinks();
    void extractsXbelBookmarkLinks();
    void extractsBrowserHistorySqliteLinks();
    void extractsFirefoxPlacesSqliteLinks();
    void extractsGenericSqliteBeaconMetadata();
    void extractsOpmlLinks();
    void extractsFeedXmlLinks();
    void extractsSitemapXmlLinks();
    void extractsRobotsTxtSitemapLinks();
    void fetchesRemoteWebShortcutContent();
    void indexRootFetchesRemoteWebShortcutContent();
    void indexesDirectoryResourcesIdempotently();
    void indexesSavedEnabledRoots();
    void rebuildClearsExistingResources();
};

static void writeFile(const QString &path, const QByteArray &content = QByteArray("test"))
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

static void writeChromiumHistoryDatabase(const QString &path)
{
    const QString connectionName =
        QStringLiteral("pinloom_test_history_%1").arg(qHash(path));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        QVERIFY2(database.open(), qPrintable(database.lastError().text()));

        QSqlQuery query(database);
        QVERIFY2(query.exec(QStringLiteral(
                     "CREATE TABLE urls ("
                     "id INTEGER PRIMARY KEY, "
                     "url TEXT NOT NULL, "
                     "title TEXT, "
                     "visit_count INTEGER, "
                     "last_visit_time INTEGER"
                     ")")),
                 qPrintable(query.lastError().text()));

        QVERIFY2(query.prepare(QStringLiteral(
                     "INSERT INTO urls(url, title, visit_count, last_visit_time) VALUES (?, ?, ?, ?)")),
                 qPrintable(query.lastError().text()));
        query.addBindValue(QStringLiteral("https://docs.example.com/pinloom/history#jump"));
        query.addBindValue(QStringLiteral("Pinloom History Entry"));
        query.addBindValue(7);
        query.addBindValue(QVariant::fromValue<qlonglong>(13253760000000000LL));
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));

        QVERIFY2(query.prepare(QStringLiteral(
                     "INSERT INTO urls(url, title, visit_count, last_visit_time) VALUES (?, ?, ?, ?)")),
                 qPrintable(query.lastError().text()));
        query.addBindValue(QStringLiteral("chrome://settings"));
        query.addBindValue(QStringLiteral("Ignored Settings"));
        query.addBindValue(2);
        query.addBindValue(QVariant::fromValue<qlonglong>(13253760000001000LL));
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));

        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

static void writeFirefoxPlacesDatabase(const QString &path)
{
    const QString connectionName =
        QStringLiteral("pinloom_test_places_%1").arg(qHash(path));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        QVERIFY2(database.open(), qPrintable(database.lastError().text()));

        QSqlQuery query(database);
        QVERIFY2(query.exec(QStringLiteral(
                     "CREATE TABLE moz_places ("
                     "id INTEGER PRIMARY KEY, "
                     "url TEXT NOT NULL, "
                     "title TEXT, "
                     "visit_count INTEGER, "
                     "last_visit_date INTEGER"
                     ")")),
                 qPrintable(query.lastError().text()));

        QVERIFY2(query.prepare(QStringLiteral(
                     "INSERT INTO moz_places(id, url, title, visit_count, last_visit_date) VALUES (?, ?, ?, ?, ?)")),
                 qPrintable(query.lastError().text()));
        query.addBindValue(1);
        query.addBindValue(QStringLiteral("https://docs.example.com/pinloom/firefox#places"));
        query.addBindValue(QStringLiteral("Pinloom Firefox Place"));
        query.addBindValue(5);
        query.addBindValue(QVariant::fromValue<qlonglong>(1710000000000000LL));
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));

        QVERIFY2(query.prepare(QStringLiteral(
                     "INSERT INTO moz_places(id, url, title, visit_count, last_visit_date) VALUES (?, ?, ?, ?, ?)")),
                 qPrintable(query.lastError().text()));
        query.addBindValue(2);
        query.addBindValue(QStringLiteral("about:config"));
        query.addBindValue(QStringLiteral("Ignored About Config"));
        query.addBindValue(3);
        query.addBindValue(QVariant::fromValue<qlonglong>(1710000000001000LL));
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));

        QVERIFY2(query.exec(QStringLiteral(
                     "CREATE TABLE moz_bookmarks ("
                     "id INTEGER PRIMARY KEY, "
                     "type INTEGER, "
                     "fk INTEGER, "
                     "parent INTEGER, "
                     "title TEXT, "
                     "dateAdded INTEGER"
                     ")")),
                 qPrintable(query.lastError().text()));

        QVERIFY2(query.prepare(QStringLiteral(
                     "INSERT INTO moz_bookmarks(id, type, fk, parent, title, dateAdded) "
                     "VALUES (?, ?, ?, ?, ?, ?)")),
                 qPrintable(query.lastError().text()));
        query.addBindValue(1);
        query.addBindValue(2);
        query.addBindValue(QVariant());
        query.addBindValue(0);
        query.addBindValue(QString());
        query.addBindValue(QVariant::fromValue<qlonglong>(1709999997000000LL));
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));

        QVERIFY2(query.prepare(QStringLiteral(
                     "INSERT INTO moz_bookmarks(id, type, fk, parent, title, dateAdded) "
                     "VALUES (?, ?, ?, ?, ?, ?)")),
                 qPrintable(query.lastError().text()));
        query.addBindValue(2);
        query.addBindValue(2);
        query.addBindValue(QVariant());
        query.addBindValue(1);
        query.addBindValue(QStringLiteral("Research"));
        query.addBindValue(QVariant::fromValue<qlonglong>(1709999998000000LL));
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));

        QVERIFY2(query.prepare(QStringLiteral(
                     "INSERT INTO moz_bookmarks(id, type, fk, parent, title, dateAdded) "
                     "VALUES (?, ?, ?, ?, ?, ?)")),
                 qPrintable(query.lastError().text()));
        query.addBindValue(3);
        query.addBindValue(2);
        query.addBindValue(QVariant());
        query.addBindValue(2);
        query.addBindValue(QStringLiteral("Pinloom"));
        query.addBindValue(QVariant::fromValue<qlonglong>(1709999999000000LL));
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));

        QVERIFY2(query.prepare(QStringLiteral(
                     "INSERT INTO moz_bookmarks(id, type, fk, parent, title, dateAdded) "
                     "VALUES (?, ?, ?, ?, ?, ?)")),
                 qPrintable(query.lastError().text()));
        query.addBindValue(4);
        query.addBindValue(1);
        query.addBindValue(1);
        query.addBindValue(3);
        query.addBindValue(QStringLiteral("Pinned Firefox Guide"));
        query.addBindValue(QVariant::fromValue<qlonglong>(1710000000000000LL));
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));

        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

static void writeGenericSqliteDatabase(const QString &path)
{
    const QString connectionName =
        QStringLiteral("pinloom_test_generic_sqlite_%1").arg(qHash(path));
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(path);
        QVERIFY2(database.open(), qPrintable(database.lastError().text()));

        QSqlQuery query(database);
        QVERIFY2(query.exec(QStringLiteral(
                     "CREATE TABLE board_notes ("
                     "id INTEGER PRIMARY KEY, "
                     "title TEXT, "
                     "url TEXT, "
                     "status TEXT"
                     ")")),
                 qPrintable(query.lastError().text()));
        QVERIFY2(query.exec(QStringLiteral(
                     "CREATE TABLE parts ("
                     "part_no TEXT, "
                     "description TEXT, "
                     "doc_url TEXT"
                     ")")),
                 qPrintable(query.lastError().text()));

        QVERIFY2(query.prepare(QStringLiteral(
                     "INSERT INTO board_notes(title, url, status) VALUES (?, ?, ?)")),
                 qPrintable(query.lastError().text()));
        query.addBindValue(QStringLiteral("Timing Closure"));
        query.addBindValue(QStringLiteral("https://docs.example.com/pinloom/sqlite#row"));
        query.addBindValue(QStringLiteral("warning review"));
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));

        QVERIFY2(query.prepare(QStringLiteral(
                     "INSERT INTO parts(part_no, description, doc_url) VALUES (?, ?, ?)")),
                 qPrintable(query.lastError().text()));
        query.addBindValue(QStringLiteral("XC7A35T"));
        query.addBindValue(QStringLiteral("PCIe endpoint guide"));
        query.addBindValue(QStringLiteral("https://fpga.example.com/parts/xc7a35t"));
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));

        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

static QByteArray ascii85Encode(const QByteArray &content)
{
    QByteArray encoded;
    for (int i = 0; i < content.size(); i += 4) {
        const int chunkSize = std::min<int>(4, static_cast<int>(content.size() - i));
        quint32 value = 0;
        for (int j = 0; j < 4; ++j) {
            value <<= 8;
            if (j < chunkSize) {
                value |= static_cast<unsigned char>(content.at(i + j));
            }
        }

        if (chunkSize == 4 && value == 0) {
            encoded.append('z');
            continue;
        }

        char tuple[5];
        for (int j = 4; j >= 0; --j) {
            tuple[j] = static_cast<char>(value % 85 + 33);
            value /= 85;
        }
        encoded.append(tuple, chunkSize + 1);
    }
    encoded.append("~>");
    return encoded;
}

static QByteArray runLengthEncode(const QByteArray &content)
{
    QByteArray encoded;
    int cursor = 0;
    while (cursor < content.size()) {
        int repeatCount = 1;
        while (cursor + repeatCount < content.size()
               && repeatCount < 128
               && content.at(cursor + repeatCount) == content.at(cursor)) {
            ++repeatCount;
        }

        if (repeatCount >= 3) {
            encoded.append(static_cast<char>(257 - repeatCount));
            encoded.append(content.at(cursor));
            cursor += repeatCount;
            continue;
        }

        const int literalStart = cursor;
        cursor += repeatCount;
        while (cursor < content.size() && cursor - literalStart < 128) {
            int nextRepeatCount = 1;
            while (cursor + nextRepeatCount < content.size()
                   && nextRepeatCount < 128
                   && content.at(cursor + nextRepeatCount) == content.at(cursor)) {
                ++nextRepeatCount;
            }
            if (nextRepeatCount >= 3) {
                break;
            }
            cursor += nextRepeatCount;
        }

        const int literalCount = cursor - literalStart;
        encoded.append(static_cast<char>(literalCount - 1));
        encoded.append(content.constData() + literalStart, literalCount);
    }
    encoded.append(static_cast<char>(128));
    return encoded;
}

void DirectorySourceTest::scansOnlyExplicitRoot()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/docs")));
    writeFile(dir.filePath(QStringLiteral("library/notes.md")), QByteArray("# Top\n"));
    writeFile(dir.filePath(QStringLiteral("library/docs/design.pdf")));
    writeFile(dir.filePath(QStringLiteral("outside.pdf")));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(resources.size(), 4);

    for (const Resource &resource : resources) {
        QVERIFY2(resource.location.startsWith(source.rootPath()), qPrintable(resource.location));
        QVERIFY(!resource.location.endsWith(QStringLiteral("outside.pdf")));
    }

    QVERIFY(std::any_of(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File && resource.title == QLatin1String("notes.md");
    }));
    QVERIFY(std::none_of(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Markdown;
    }));
    QVERIFY(std::any_of(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Pdf && resource.title == QLatin1String("design.pdf");
    }));
}

void DirectorySourceTest::indexesPlainTextFileContent()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    QVERIFY(dir.mkpath(QStringLiteral("library/docs")));
    writeFile(dir.filePath(QStringLiteral("library/ops.log")),
              QByteArray("---\n"
                         "aliases:\n"
                         "  - ops handoff alias\n"
                         "tags: [plain-text, relay]\n"
                         "---\n"
                         "# Operations\n"
                         "ZeroSlack relay reconnect sequence\n"
                         "NOTE: Pinloom host handoff status\n"
                         "- [ ] Check [[Runbook|handoff note]] ^handoff-block\n"
                         "Open [Spec PDF](docs/spec.pdf#page=2)\n"));

    QByteArray binaryLike;
    binaryLike.append("visible ");
    binaryLike.append('\0');
    binaryLike.append("unsearchable-nul-token");
    writeFile(dir.filePath(QStringLiteral("library/binary.txt")), binaryLike);

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto logIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File && resource.title == QLatin1String("ops.log");
    });
    QVERIFY(logIt != resources.cend());
    QVERIFY(logIt->content.contains(QStringLiteral("relay reconnect sequence")));
    QVERIFY(!logIt->content.contains(QStringLiteral("ops handoff alias")));
    QVERIFY(logIt->aliases.contains(QStringLiteral("ops handoff alias")));
    QVERIFY(logIt->aliases.contains(QStringLiteral("handoff note")));
    QVERIFY(logIt->aliases.contains(QStringLiteral("Runbook.md")));
    QVERIFY(logIt->aliases.contains(QStringLiteral("Spec PDF")));
    QVERIFY(logIt->aliases.contains(QStringLiteral("docs/spec.pdf")));
    QVERIFY(logIt->tags.contains(QStringLiteral("plain-text")));
    QVERIFY(logIt->tags.contains(QStringLiteral("relay")));
    QVERIFY(std::any_of(logIt->anchors.cbegin(), logIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::TextHeading
            && anchor.target == QLatin1String("Operations")
            && anchor.line == 6;
    }));
    QVERIFY(std::any_of(logIt->anchors.cbegin(), logIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("NOTE: Pinloom host handoff status")
            && anchor.line == 8;
    }));
    QVERIFY(std::any_of(logIt->anchors.cbegin(), logIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::TextBlock
            && anchor.target == QLatin1String("handoff-block")
            && anchor.line == 9;
    }));
    QVERIFY(std::any_of(logIt->anchors.cbegin(), logIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("bracket link: handoff note -> Runbook.md")
            && anchor.line == 9;
    }));
    QVERIFY(std::any_of(logIt->anchors.cbegin(), logIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("Check handoff note")
            && anchor.line == 9;
    }));
    QVERIFY(std::any_of(logIt->anchors.cbegin(), logIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("link: Spec PDF -> docs/spec.pdf")
            && anchor.line == 10;
    }));
    QVERIFY(std::any_of(logIt->relations.cbegin(), logIt->relations.cend(), [](const ResourceRelation &relation) {
        return relation.label == QLatin1String("links-to")
            && relation.targetResourceId.endsWith(QStringLiteral("Runbook.md"))
            && relation.note == QLatin1String("bracket link: handoff note -> Runbook.md");
    }));
    QVERIFY(std::any_of(logIt->relations.cbegin(), logIt->relations.cend(), [](const ResourceRelation &relation) {
        return relation.label == QLatin1String("links-to")
            && relation.targetResourceId.endsWith(QStringLiteral("docs/spec.pdf"))
            && relation.note == QLatin1String("link: Spec PDF -> docs/spec.pdf");
    }));

    auto binaryIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File && resource.title == QLatin1String("binary.txt");
    });
    QVERIFY(binaryIt != resources.cend());
    QVERIFY(binaryIt->content.isEmpty());

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("relay reconnect")});
    QCOMPARE(contentResults.size(), 1);
    QCOMPARE(contentResults.first().resource.title, QStringLiteral("ops.log"));
    QCOMPARE(contentResults.first().matchedField, QStringLiteral("content"));

    const QList<SearchResult> anchorResults = repository.search(SearchQuery{QStringLiteral("host handoff status")});
    QVERIFY(std::any_of(anchorResults.cbegin(), anchorResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("ops.log")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 8;
    }));

    const QList<SearchResult> binaryResults = repository.search(SearchQuery{QStringLiteral("unsearchable-nul-token")});
    QVERIFY(binaryResults.isEmpty());
}

void DirectorySourceTest::keepsPackageContainersAsPermanentPathOnlyFiles()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/artifacts")));
    const QByteArray packageLikeText("#!/bin/sh\n"
                                     "hidden-entry.md\n"
                                     "Hidden Package Guide\n"
                                     "https://docs.example.com/package/inside\n");
    const QStringList compressedPackageFileNames{
        QStringLiteral("package.zip"),
        QStringLiteral("package.zipx"),
        QStringLiteral("bundle.tar"),
        QStringLiteral("bundle.tar.br"),
        QStringLiteral("bundle.tar.gz"),
        QStringLiteral("bundle.tar.bz2"),
        QStringLiteral("bundle.tar.lz"),
        QStringLiteral("bundle.tar.lz4"),
        QStringLiteral("bundle.tar.lzma"),
        QStringLiteral("bundle.tar.lzo"),
        QStringLiteral("bundle.tar.z"),
        QStringLiteral("bundle.tar.xz"),
        QStringLiteral("bundle.tar.zst"),
        QStringLiteral("bundle.taz"),
        QStringLiteral("bundle.tgz"),
        QStringLiteral("bundle.tbz"),
        QStringLiteral("bundle.tbz2"),
        QStringLiteral("bundle.tlz"),
        QStringLiteral("bundle.tlzma"),
        QStringLiteral("bundle.txz"),
        QStringLiteral("legacy.Z"),
        QStringLiteral("legacy.ace"),
        QStringLiteral("legacy.alz"),
        QStringLiteral("legacy.ar"),
        QStringLiteral("legacy.arc"),
        QStringLiteral("legacy.arj"),
        QStringLiteral("single.br"),
        QStringLiteral("single.bz2"),
        QStringLiteral("single.gz"),
        QStringLiteral("single.lz"),
        QStringLiteral("single.lz4"),
        QStringLiteral("single.lzma"),
        QStringLiteral("single.lha"),
        QStringLiteral("single.lzh"),
        QStringLiteral("single.lzo"),
        QStringLiteral("single.xz"),
        QStringLiteral("payload.7z"),
        QStringLiteral("payload.cpio"),
        QStringLiteral("dump.rar"),
        QStringLiteral("dump.zoo"),
        QStringLiteral("snapshot.zst"),
        QStringLiteral("mobile.apk"),
        QStringLiteral("desktop.appx"),
        QStringLiteral("desktop.appxbundle"),
        QStringLiteral("installer.cab"),
        QStringLiteral("installer.deb"),
        QStringLiteral("installer.dmg"),
        QStringLiteral("installer.msix"),
        QStringLiteral("installer.msixbundle"),
        QStringLiteral("installer.rpm"),
        QStringLiteral("installer.xar"),
        QStringLiteral("disk.iso"),
        QStringLiteral("comic.cb7"),
        QStringLiteral("comic.cbr"),
        QStringLiteral("comic.cbz"),
        QStringLiteral("extension.crx"),
        QStringLiteral("extension.vsix"),
        QStringLiteral("library.nupkg"),
        QStringLiteral("library.whl"),
        QStringLiteral("library.gem"),
        QStringLiteral("library.crate"),
        QStringLiteral("enterprise.ear"),
        QStringLiteral("phone.ipa"),
        QStringLiteral("module.jar"),
        QStringLiteral("site.war")
    };
    const QStringList officeVisioPackageFileNames{
        QStringLiteral("document.docm"),
        QStringLiteral("document.docx"),
        QStringLiteral("template.dotm"),
        QStringLiteral("template.dotx"),
        QStringLiteral("slides.potm"),
        QStringLiteral("slides.potx"),
        QStringLiteral("show.ppsm"),
        QStringLiteral("show.ppsx"),
        QStringLiteral("slides.pptm"),
        QStringLiteral("slides.pptx"),
        QStringLiteral("diagram.vsdm"),
        QStringLiteral("diagram.vsdx"),
        QStringLiteral("stencil.vssm"),
        QStringLiteral("stencil.vssx"),
        QStringLiteral("template.vstm"),
        QStringLiteral("template.vstx"),
        QStringLiteral("workbook.xlsm"),
        QStringLiteral("workbook.xlsx"),
        QStringLiteral("workbook.xltm"),
        QStringLiteral("workbook.xltx")
    };
    QStringList packageFileNames = compressedPackageFileNames;
    packageFileNames += officeVisioPackageFileNames;
    for (const QString &fileName : packageFileNames) {
        writeFile(dir.filePath(QStringLiteral("library/artifacts/%1").arg(fileName)), packageLikeText);
    }

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(resources.size(), packageFileNames.size() + 2);

    for (const QString &fileName : packageFileNames) {
        auto packageIt = std::find_if(resources.cbegin(), resources.cend(), [&](const Resource &resource) {
            return resource.kind == ResourceKind::File
                && resource.title == fileName;
        });
        QVERIFY(packageIt != resources.cend());
        QVERIFY(packageIt->tags.contains(QStringLiteral("path-only")));
        QVERIFY(packageIt->tags.contains(QStringLiteral("package-container")));
        QVERIFY(!packageIt->tags.contains(QStringLiteral("archive")));
        QVERIFY(!packageIt->tags.contains(QStringLiteral("zip-archive")));
        QVERIFY(!packageIt->tags.contains(QStringLiteral("special-reader")));
        QVERIFY(!packageIt->tags.contains(QStringLiteral("archive-preview-limited")));
        QVERIFY(packageIt->anchors.isEmpty());
        QVERIFY(packageIt->relations.isEmpty());
        QVERIFY(packageIt->content.isEmpty());
    }

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> packageResults = repository.search(SearchQuery{QStringLiteral("package.zip")});
    auto packageResultIt = std::find_if(packageResults.cbegin(), packageResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("package.zip")
            && !result.matchedAnchor.has_value();
    });
    QVERIFY(packageResultIt != packageResults.cend());
    QVERIFY(repository.resourceRelations(packageResultIt->resource.id).isEmpty());

    const QList<SearchResult> internalResults = repository.search(SearchQuery{QStringLiteral("hidden-entry")});
    QVERIFY(internalResults.isEmpty());
    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("Hidden Package Guide")});
    QVERIFY(contentResults.isEmpty());
    const QList<SearchResult> urlResults = repository.search(SearchQuery{QStringLiteral("docs.example.com/package/inside")});
    QVERIFY(urlResults.isEmpty());
}

void DirectorySourceTest::extractsTextStructureLineBeacons()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    writeFile(dir.filePath(QStringLiteral("library/settings.toml")),
              QByteArray("[zeroslack]\n"
                         "remote_fetch = true\n"
                         "dock_mode = \"global\"\n"));
    writeFile(dir.filePath(QStringLiteral("library/routes.json")),
              QByteArray("{\n"
                         "  \"pinloomDock\": true,\n"
                         "  \"pinloom\": {\n"
                         "    \"dock\": {\n"
                         "      \"mode\": \"global\"\n"
                         "    }\n"
                         "  },\n"
                         "  \"jumpTarget\": \"handoff\"\n"
                         "}\n"));
    writeFile(dir.filePath(QStringLiteral("library/workspace.yml")),
              QByteArray("pinloom:\n"
                         "  dock:\n"
                         "    mode: global\n"
                         "  sources:\n"
                         "    - name: docs\n"));
    writeFile(dir.filePath(QStringLiteral("library/metrics.csv")),
              QByteArray("\"signal name\",baud_rate,handoff_status\n"
                         "uart0,115200,ready\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto hasLineAnchor = [](const Resource &resource, const QString &target, int line) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::FileLine
                && anchor.target == target
                && anchor.line == line;
        });
    };

    auto settingsIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File && resource.title == QLatin1String("settings.toml");
    });
    QVERIFY(settingsIt != resources.cend());
    QVERIFY(hasLineAnchor(*settingsIt, QStringLiteral("section: zeroslack"), 1));
    QVERIFY(hasLineAnchor(*settingsIt, QStringLiteral("path: zeroslack"), 1));
    QVERIFY(hasLineAnchor(*settingsIt, QStringLiteral("key: remote_fetch"), 2));
    QVERIFY(hasLineAnchor(*settingsIt, QStringLiteral("path: zeroslack.remote_fetch"), 2));

    auto routesIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File && resource.title == QLatin1String("routes.json");
    });
    QVERIFY(routesIt != resources.cend());
    QVERIFY(hasLineAnchor(*routesIt, QStringLiteral("key: pinloomDock"), 2));
    QVERIFY(hasLineAnchor(*routesIt, QStringLiteral("path: pinloom"), 3));
    QVERIFY(hasLineAnchor(*routesIt, QStringLiteral("path: pinloom.dock"), 4));
    QVERIFY(hasLineAnchor(*routesIt, QStringLiteral("path: pinloom.dock.mode"), 5));
    QVERIFY(hasLineAnchor(*routesIt, QStringLiteral("path: jumpTarget"), 8));

    auto workspaceIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File && resource.title == QLatin1String("workspace.yml");
    });
    QVERIFY(workspaceIt != resources.cend());
    QVERIFY(hasLineAnchor(*workspaceIt, QStringLiteral("path: pinloom"), 1));
    QVERIFY(hasLineAnchor(*workspaceIt, QStringLiteral("path: pinloom.dock"), 2));
    QVERIFY(hasLineAnchor(*workspaceIt, QStringLiteral("path: pinloom.dock.mode"), 3));
    QVERIFY(hasLineAnchor(*workspaceIt, QStringLiteral("path: pinloom.sources"), 4));
    QVERIFY(hasLineAnchor(*workspaceIt, QStringLiteral("path: pinloom.sources.name"), 5));

    auto metricsIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File && resource.title == QLatin1String("metrics.csv");
    });
    QVERIFY(metricsIt != resources.cend());
    QVERIFY(std::any_of(metricsIt->anchors.cbegin(), metricsIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("column: signal name")
            && anchor.line == 1;
    }));
    QVERIFY(std::any_of(metricsIt->anchors.cbegin(), metricsIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("column: baud_rate")
            && anchor.line == 1;
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> keyResults = repository.search(SearchQuery{QStringLiteral("remote_fetch")});
    QVERIFY(std::any_of(keyResults.cbegin(), keyResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("settings.toml")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 2;
    }));

    const QList<SearchResult> jsonResults = repository.search(SearchQuery{QStringLiteral("pinloomDock")});
    QVERIFY(std::any_of(jsonResults.cbegin(), jsonResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("routes.json")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 2;
    }));

    const QList<SearchResult> jsonPathResults = repository.search(SearchQuery{QStringLiteral("pinloom dock mode")});
    QVERIFY(std::any_of(jsonPathResults.cbegin(), jsonPathResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("routes.json")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->target == QLatin1String("path: pinloom.dock.mode")
            && result.matchedAnchor->line == 5;
    }));

    const QList<SearchResult> yamlPathResults = repository.search(SearchQuery{QStringLiteral("sources name")});
    QVERIFY(std::any_of(yamlPathResults.cbegin(), yamlPathResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("workspace.yml")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->target == QLatin1String("path: pinloom.sources.name")
            && result.matchedAnchor->line == 5;
    }));

    const QList<SearchResult> columnResults = repository.search(SearchQuery{QStringLiteral("baud_rate")});
    QVERIFY(std::any_of(columnResults.cbegin(), columnResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("metrics.csv")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 1;
    }));
}

void DirectorySourceTest::extractsNeutralWorkflowConfigLineBeacons()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/.github/workflows")));
    QVERIFY(dir.mkpath(QStringLiteral("library/config")));
    writeFile(dir.filePath(QStringLiteral("library/.github/workflows/ci.yml")),
              QByteArray("name: Pinloom CI\n"
                         "on: [push]\n"
                         "jobs:\n"
                         "  build:\n"
                         "    name: Build and Test\n"
                         "    runs-on: ubuntu-latest\n"
                         "    steps:\n"
                         "      - name: Checkout\n"
                         "        uses: actions/checkout@v4\n"
                         "      - name: Configure\n"
                         "        run: cmake -S . -B build\n"
                         "      - uses: actions/upload-artifact@v4\n"
                         "  docs:\n"
                         "    steps:\n"
                         "      - run: ./scripts/build-docs.sh\n"));
    writeFile(dir.filePath(QStringLiteral("library/config/ci.yml")),
              QByteArray("name: Not A Workflow\n"
                         "jobs:\n"
                         "  ignored:\n"
                         "    steps:\n"
                         "      - uses: actions/cache@v4\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto workflowIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File && resource.title == QLatin1String("ci.yml")
            && resource.location.contains(QStringLiteral(".github/workflows"));
    });
    QVERIFY(workflowIt != resources.cend());

    auto hasBeacon = [](const Resource &resource, const QString &target, int line) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::FileLine
                && anchor.target == target
                && anchor.line == line;
        });
    };
    auto hasLineAnchor = [](const Resource &resource, const QString &target, int line) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::FileLine
                && anchor.target == target
                && anchor.line == line;
        });
    };

    QVERIFY(hasBeacon(*workflowIt, QStringLiteral("ci workflow: Pinloom CI"), 1));
    QVERIFY(hasBeacon(*workflowIt, QStringLiteral("ci block: build"), 4));
    QVERIFY(hasLineAnchor(*workflowIt, QStringLiteral("ci label: Build and Test"), 5));
    QVERIFY(hasBeacon(*workflowIt, QStringLiteral("ci step: Checkout"), 8));
    QVERIFY(hasLineAnchor(*workflowIt, QStringLiteral("ci uses: actions/checkout@v4"), 9));
    QVERIFY(hasBeacon(*workflowIt, QStringLiteral("ci step: Configure"), 10));
    QVERIFY(hasLineAnchor(*workflowIt, QStringLiteral("ci run: cmake -S . -B build"), 11));
    QVERIFY(hasLineAnchor(*workflowIt, QStringLiteral("ci uses: actions/upload-artifact@v4"), 12));
    QVERIFY(hasBeacon(*workflowIt, QStringLiteral("ci block: docs"), 13));
    QVERIFY(hasLineAnchor(*workflowIt, QStringLiteral("ci run: ./scripts/build-docs.sh"), 15));

    auto configIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File && resource.title == QLatin1String("ci.yml")
            && resource.location.contains(QStringLiteral("/config/"));
    });
    QVERIFY(configIt != resources.cend());
    QVERIFY(std::none_of(configIt->anchors.cbegin(), configIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.target.startsWith(QStringLiteral("ci "));
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> workflowResults = repository.search(SearchQuery{QStringLiteral("Pinloom CI")});
    QVERIFY(std::any_of(workflowResults.cbegin(), workflowResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("ci.yml")
            && result.resource.location.contains(QStringLiteral(".github/workflows"))
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("ci workflow: Pinloom CI");
    }));

    const QList<SearchResult> actionResults = repository.search(SearchQuery{QStringLiteral("upload-artifact")});
    QVERIFY(std::any_of(actionResults.cbegin(), actionResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("ci.yml")
            && result.resource.location.contains(QStringLiteral(".github/workflows"))
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("ci uses: actions/upload-artifact@v4");
    }));

    const QList<SearchResult> runResults = repository.search(SearchQuery{QStringLiteral("build-docs")});
    QVERIFY(std::any_of(runResults.cbegin(), runResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("ci.yml")
            && result.resource.location.contains(QStringLiteral(".github/workflows"))
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("ci run: ./scripts/build-docs.sh");
    }));
}

void DirectorySourceTest::extractsNeutralPipelineConfigLineBeacons()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/config")));
    writeFile(dir.filePath(QStringLiteral("library/.gitlab-ci.yml")),
              QByteArray("stages:\n"
                         "  - build\n"
                         "  - test\n"
                         "default:\n"
                         "  image: alpine:3.20\n"
                         "build_app:\n"
                         "  stage: build\n"
                         "  image: gcc:13\n"
                         "  script:\n"
                         "    - cmake -S . -B build\n"
                         "    - cmake --build build\n"
                         "test_app:\n"
                         "  stage: test\n"
                         "  needs:\n"
                         "    - build_app\n"
                         "  script:\n"
                         "    - ctest --test-dir build --output-on-failure\n"));
    writeFile(dir.filePath(QStringLiteral("library/config/gitlab-ci.yml")),
              QByteArray("stages:\n"
                         "  - ignored\n"
                         "build_app:\n"
                         "  script:\n"
                         "    - echo ignored\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto pipelineIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File && resource.title == QLatin1String(".gitlab-ci.yml");
    });
    QVERIFY(pipelineIt != resources.cend());

    auto hasBeacon = [](const Resource &resource, const QString &target, int line) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::FileLine
                && anchor.target == target
                && anchor.line == line;
        });
    };
    auto hasLineAnchor = [](const Resource &resource, const QString &target, int line) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::FileLine
                && anchor.target == target
                && anchor.line == line;
        });
    };

    QVERIFY(hasLineAnchor(*pipelineIt, QStringLiteral("ci stage: build"), 2));
    QVERIFY(hasLineAnchor(*pipelineIt, QStringLiteral("ci stage: test"), 3));
    QVERIFY(hasBeacon(*pipelineIt, QStringLiteral("ci block: build_app"), 6));
    QVERIFY(hasLineAnchor(*pipelineIt, QStringLiteral("ci stage: build"), 7));
    QVERIFY(hasLineAnchor(*pipelineIt, QStringLiteral("ci image: gcc:13"), 8));
    QVERIFY(hasLineAnchor(*pipelineIt, QStringLiteral("ci script: cmake -S . -B build"), 10));
    QVERIFY(hasLineAnchor(*pipelineIt, QStringLiteral("ci script: cmake --build build"), 11));
    QVERIFY(hasBeacon(*pipelineIt, QStringLiteral("ci block: test_app"), 12));
    QVERIFY(hasLineAnchor(*pipelineIt, QStringLiteral("ci stage: test"), 13));
    QVERIFY(hasLineAnchor(*pipelineIt, QStringLiteral("ci needs: build_app"), 15));
    QVERIFY(hasLineAnchor(*pipelineIt,
                          QStringLiteral("ci script: ctest --test-dir build --output-on-failure"),
                          17));

    auto configIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File && resource.title == QLatin1String("gitlab-ci.yml")
            && resource.location.contains(QStringLiteral("/config/"));
    });
    QVERIFY(configIt != resources.cend());
    QVERIFY(std::none_of(configIt->anchors.cbegin(), configIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.target.startsWith(QStringLiteral("ci "));
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> jobResults = repository.search(SearchQuery{QStringLiteral("build_app")});
    QVERIFY(std::any_of(jobResults.cbegin(), jobResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String(".gitlab-ci.yml")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("ci block: build_app");
    }));

    const QList<SearchResult> scriptResults = repository.search(SearchQuery{QStringLiteral("output-on-failure")});
    QVERIFY(std::any_of(scriptResults.cbegin(), scriptResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String(".gitlab-ci.yml")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target
                == QLatin1String("ci script: ctest --test-dir build --output-on-failure");
    }));
}

void DirectorySourceTest::extractsTextNamedEntryLineBeacons()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    writeFile(dir.filePath(QStringLiteral("library/named-entries.json")),
              QByteArray("{\n"
                         "  \"entries\": {\n"
                         "    \"search-panel\": \"dock\",\n"
                         "    \"locator-dock\": \"panel\"\n"
                         "  },\n"
                         "  \"beacons\": {\n"
                         "    \"preview-runner\": \"smoke\"\n"
                         "  },\n"
                         "  \"dependencies\": {\n"
                         "    \"language-package\": \"ignored\"\n"
                         "  }\n"
                         "}\n"));
    writeFile(dir.filePath(QStringLiteral("library/named-entries.toml")),
              QByteArray("[entries]\n"
                         "index-core = \"1\"\n"
                         "locator-ui = { version = \"1\", features = [\"panel\"] }\n"));
    writeFile(dir.filePath(QStringLiteral("library/requirements-dev.txt")),
              QByteArray("pinloom-sdk>=1.2\n"
                         "desktop-shell==6.10.2\n"
                         "-r base.txt\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto findFile = [&](const QString &title) {
        return std::find_if(resources.cbegin(), resources.cend(), [&](const Resource &resource) {
            return resource.kind == ResourceKind::File && resource.title == title;
        });
    };
    auto hasNamedEntry = [](const Resource &resource, const QString &entry, int line) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::FileLine
                && anchor.target == QStringLiteral("named entry: %1").arg(entry)
                && anchor.line == line;
        });
    };

    const auto jsonNamedEntriesIt = findFile(QStringLiteral("named-entries.json"));
    QVERIFY(jsonNamedEntriesIt != resources.cend());
    QVERIFY(hasNamedEntry(*jsonNamedEntriesIt, QStringLiteral("search-panel"), 3));
    QVERIFY(hasNamedEntry(*jsonNamedEntriesIt, QStringLiteral("locator-dock"), 4));
    QVERIFY(hasNamedEntry(*jsonNamedEntriesIt, QStringLiteral("preview-runner"), 7));
    QVERIFY(!hasNamedEntry(*jsonNamedEntriesIt, QStringLiteral("language-package"), 10));

    const auto tomlNamedEntriesIt = findFile(QStringLiteral("named-entries.toml"));
    QVERIFY(tomlNamedEntriesIt != resources.cend());
    QVERIFY(hasNamedEntry(*tomlNamedEntriesIt, QStringLiteral("index-core"), 2));
    QVERIFY(hasNamedEntry(*tomlNamedEntriesIt, QStringLiteral("locator-ui"), 3));

    const auto requirementsIt = findFile(QStringLiteral("requirements-dev.txt"));
    QVERIFY(requirementsIt != resources.cend());
    QVERIFY(!hasNamedEntry(*requirementsIt, QStringLiteral("pinloom-sdk"), 1));
    QVERIFY(!hasNamedEntry(*requirementsIt, QStringLiteral("desktop-shell"), 2));
    QVERIFY(!hasNamedEntry(*requirementsIt, QStringLiteral("base.txt"), 3));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> panelResults = repository.search(SearchQuery{QStringLiteral("search-panel")});
    QVERIFY(std::any_of(panelResults.cbegin(), panelResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("named-entries.json")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("named entry: search-panel");
    }));

    const QList<SearchResult> locatorResults = repository.search(SearchQuery{QStringLiteral("locator-ui")});
    QVERIFY(std::any_of(locatorResults.cbegin(), locatorResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("named-entries.toml")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("named entry: locator-ui");
    }));
}

void DirectorySourceTest::extractsTextHeadingAndBlockAnchors()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    writeFile(dir.filePath(QStringLiteral("library/notes.md")),
              QByteArray("# Top\n\n## Power sequencing\nDetails ^power-block\n^standalone\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto markdownIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File;
    });
    QVERIFY(markdownIt != resources.cend());
    QCOMPARE(markdownIt->anchors.size(), 4);
    QCOMPARE(markdownIt->anchors.at(0).type, AnchorType::TextHeading);
    QCOMPARE(markdownIt->anchors.at(0).target, QStringLiteral("Top"));
    QCOMPARE(markdownIt->anchors.at(0).line, 1);
    QCOMPARE(markdownIt->anchors.at(1).target, QStringLiteral("Power sequencing"));
    QCOMPARE(markdownIt->anchors.at(1).line, 3);
    QCOMPARE(markdownIt->anchors.at(2).type, AnchorType::TextBlock);
    QCOMPARE(markdownIt->anchors.at(2).target, QStringLiteral("power-block"));
    QCOMPARE(markdownIt->anchors.at(2).line, 4);
    QCOMPARE(markdownIt->anchors.at(3).target, QStringLiteral("standalone"));
    QCOMPARE(markdownIt->anchors.at(3).line, 5);

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> headingResults = repository.search(SearchQuery{QStringLiteral("Power sequencing")});
    QVERIFY(std::any_of(headingResults.cbegin(), headingResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("notes.md")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::TextHeading
            && result.matchedAnchor->target == QLatin1String("Power sequencing");
    }));

    const QList<SearchResult> blockResults = repository.search(SearchQuery{QStringLiteral("power-block")});
    QVERIFY(std::any_of(blockResults.cbegin(), blockResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("notes.md")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::TextBlock
            && result.matchedAnchor->target == QLatin1String("power-block");
    }));
}

void DirectorySourceTest::extractsTextConventionAliasTagLinkBeacons()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    QVERIFY(dir.mkpath(QStringLiteral("library/deep")));
    writeFile(dir.filePath(QStringLiteral("library/notes.md")),
              QByteArray("---\n"
                         "aliases:\n"
                         "  - serial debug\n"
                         "  - board diary\n"
                         "tags: [fpga, uart]\n"
                         "---\n"
                         "# Bringup\n"
                         "Body #bringup and #lab/debug with [[Link Target]] and [[deep/note#^power-block|display]].\n"));
    writeFile(dir.filePath(QStringLiteral("library/Link Target.md")),
              QByteArray("# Link Target\n"));
    writeFile(dir.filePath(QStringLiteral("library/deep/note.md")),
              QByteArray("# Deep Note\n^power-block\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto markdownIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File && resource.title == QLatin1String("notes.md");
    });
    QVERIFY(markdownIt != resources.cend());

    QVERIFY(markdownIt->aliases.contains(QStringLiteral("serial debug")));
    QVERIFY(markdownIt->aliases.contains(QStringLiteral("board diary")));
    QVERIFY(markdownIt->aliases.contains(QStringLiteral("Link Target")));
    QVERIFY(markdownIt->aliases.contains(QStringLiteral("deep/note")));
    QVERIFY(markdownIt->aliases.contains(QStringLiteral("display")));
    QVERIFY(markdownIt->aliases.contains(QStringLiteral("note.md")));
    QVERIFY(markdownIt->aliases.contains(QStringLiteral("power-block")));
    QVERIFY(markdownIt->tags.contains(QStringLiteral("fpga")));
    QVERIFY(markdownIt->tags.contains(QStringLiteral("uart")));
    QVERIFY(markdownIt->tags.contains(QStringLiteral("bringup")));
    QVERIFY(markdownIt->tags.contains(QStringLiteral("lab/debug")));
    QVERIFY(std::any_of(markdownIt->anchors.cbegin(), markdownIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("bracket link: Link Target -> Link Target.md")
            && anchor.line == 8;
    }));
    QVERIFY(std::any_of(markdownIt->anchors.cbegin(), markdownIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("bracket link: display -> deep/note.md#power-block")
            && anchor.line == 8;
    }));
    QCOMPARE(markdownIt->relations.size(), 2);
    QVERIFY(std::any_of(markdownIt->relations.cbegin(), markdownIt->relations.cend(), [](const ResourceRelation &relation) {
        return relation.label == QLatin1String("links-to")
            && relation.targetResourceId.endsWith(QStringLiteral("Link Target.md"))
            && relation.note == QLatin1String("bracket link: Link Target -> Link Target.md");
    }));
    QVERIFY(std::any_of(markdownIt->relations.cbegin(), markdownIt->relations.cend(), [](const ResourceRelation &relation) {
        return relation.label == QLatin1String("links-to")
            && relation.targetResourceId.endsWith(QStringLiteral("deep/note.md"))
            && relation.note == QLatin1String("bracket link: display -> deep/note.md#power-block");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("serial")}).size(), 1);
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("lab/debug")}).size(), 1);
    const QList<SearchResult> linkAliasResults = repository.search(SearchQuery{QStringLiteral("Link")});
    QVERIFY(std::any_of(linkAliasResults.cbegin(), linkAliasResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("notes.md")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("bracket link: Link Target -> Link Target.md");
    }));

    const QList<ResourceRelation> relations = repository.resourceRelations(markdownIt->id);
    QCOMPARE(relations.size(), 2);
    QVERIFY(std::any_of(relations.cbegin(), relations.cend(), [](const ResourceRelation &relation) {
        return relation.targetResourceId.endsWith(QStringLiteral("Link Target.md"));
    }));
    QVERIFY(std::any_of(relations.cbegin(), relations.cend(), [](const ResourceRelation &relation) {
        return relation.targetResourceId.endsWith(QStringLiteral("deep/note.md"));
    }));

    const QList<SearchResult> bracketedTextLinkResults = repository.search(SearchQuery{QStringLiteral("power-block")});
    QVERIFY(std::any_of(bracketedTextLinkResults.cbegin(), bracketedTextLinkResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("notes.md")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("bracket link: display -> deep/note.md#power-block");
    }));
}

void DirectorySourceTest::extractsTextConventionBodyContent()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    writeFile(dir.filePath(QStringLiteral("library/notes.md")),
              QByteArray("---\n"
                         "aliases: [secret calibration]\n"
                         "tags: [private]\n"
                         "---\n"
                         "# Bringup Notes\n"
                         "The calibration envelope lives in [[Deep Note|display note]].\n"
                         "Use #lab/debug before release. ^body-block\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto markdownIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File;
    });
    QVERIFY(markdownIt != resources.cend());
    QVERIFY(markdownIt->content.contains(QStringLiteral("calibration envelope")));
    QVERIFY(markdownIt->content.contains(QStringLiteral("display note")));
    QVERIFY(markdownIt->content.contains(QStringLiteral("lab/debug")));
    QVERIFY(!markdownIt->content.contains(QStringLiteral("secret calibration")));
    QVERIFY(!markdownIt->content.contains(QStringLiteral("body-block")));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("calibration envelope")});
    QCOMPARE(contentResults.size(), 1);
    QCOMPARE(contentResults.first().resource.kind, ResourceKind::File);
    QCOMPARE(contentResults.first().matchedField, QStringLiteral("content"));
}

void DirectorySourceTest::extractsLocalTextLinkAnchors()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    QVERIFY(dir.mkpath(QStringLiteral("library/docs")));
    writeFile(dir.filePath(QStringLiteral("library/runbook.md")),
              QByteArray("# Runbook\n"
                         "Open [Spec PDF](docs/spec.pdf#page=2) before bringup.\n"
                         "Read [External](https://docs.example.com/spec).\n"
                         "![Diagram](images/diagram.png)\n"));
    writeFile(dir.filePath(QStringLiteral("library/docs/spec.pdf")),
              QByteArray("%PDF-1.4\n"
                         "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
                         "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n"
                         "3 0 obj << /Type /Page /Parent 2 0 R >> endobj\n"
                         "%%EOF\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto markdownIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File && resource.title == QLatin1String("runbook.md");
    });
    QVERIFY(markdownIt != resources.cend());
    QVERIFY(markdownIt->aliases.contains(QStringLiteral("Spec PDF")));
    QVERIFY(markdownIt->aliases.contains(QStringLiteral("spec.pdf")));
    QVERIFY(markdownIt->aliases.contains(QStringLiteral("docs/spec.pdf")));
    QVERIFY(std::any_of(markdownIt->anchors.cbegin(), markdownIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("link: Spec PDF -> docs/spec.pdf")
            && anchor.line == 2;
    }));
    QVERIFY(std::any_of(markdownIt->anchors.cbegin(), markdownIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: External -> https://docs.example.com/spec")
            && anchor.line == 3;
    }));
    QVERIFY(std::none_of(markdownIt->anchors.cbegin(), markdownIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target.contains(QStringLiteral("Diagram"));
    }));
    auto externalIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/spec");
    });
    QVERIFY(externalIt != resources.cend());
    QCOMPARE(markdownIt->relations.size(), 2);
    QVERIFY(std::any_of(markdownIt->relations.cbegin(), markdownIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == markdownIt->id
            && relation.label == QLatin1String("links-to")
            && relation.targetResourceId.endsWith(QStringLiteral("docs/spec.pdf"));
    }));
    QVERIFY(std::any_of(markdownIt->relations.cbegin(), markdownIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == markdownIt->id
            && relation.targetResourceId == externalIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("text line 3: url: External -> https://docs.example.com/spec");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<ResourceRelation> relations = repository.resourceRelations(markdownIt->id);
    QCOMPARE(relations.size(), 2);
    QVERIFY(std::any_of(relations.cbegin(), relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == markdownIt->id
            && relation.label == QLatin1String("links-to")
            && relation.targetResourceId.endsWith(QStringLiteral("docs/spec.pdf"))
            && relation.note == QLatin1String("link: Spec PDF -> docs/spec.pdf");
    }));
    QVERIFY(std::any_of(relations.cbegin(), relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == markdownIt->id
            && relation.targetResourceId == externalIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("text line 3: url: External -> https://docs.example.com/spec");
    }));

    const QList<SearchResult> linkResults = repository.search(SearchQuery{QStringLiteral("Spec PDF")});
    QVERIFY(std::any_of(linkResults.cbegin(), linkResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("runbook.md")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 2;
    }));
}

void DirectorySourceTest::extractsTextTaskLineAnchors()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    writeFile(dir.filePath(QStringLiteral("library/tasks.md")),
              QByteArray("# Bringup Tasks\n"
                         "- [ ] Verify timing closure #fpga\n"
                         "- [x] Update [[Runbook|handoff runbook]]\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto markdownIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File && resource.title == QLatin1String("tasks.md");
    });
    QVERIFY(markdownIt != resources.cend());
    QVERIFY(markdownIt->content.contains(QStringLiteral("Verify timing closure fpga")));
    QVERIFY(std::any_of(markdownIt->anchors.cbegin(), markdownIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("Verify timing closure fpga")
            && anchor.line == 2;
    }));
    QVERIFY(std::any_of(markdownIt->anchors.cbegin(), markdownIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("Update handoff runbook")
            && anchor.line == 3;
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> taskResults = repository.search(SearchQuery{QStringLiteral("timing closure")});
    QVERIFY(std::any_of(taskResults.cbegin(), taskResults.cend(), [](const SearchResult &result) {
        return result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 2;
    }));
}

void DirectorySourceTest::extractsInlineTextLinkUrlResources()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    writeFile(dir.filePath(QStringLiteral("library/runbook.md")),
              QByteArray("---\n"
                         "source: https://ignored.example.com/frontmatter\n"
                         "---\n"
                         "# Runbook\n"
                         "Read [ZeroSlack Dock Guide](https://docs.example.com/zeroslack/dock#handoff).\n"
                         "![Logo](https://cdn.example.com/logo.png)\n"
                         "<https://status.example.com/system>\n"
                         "Raw status dashboard https://raw.example.com/zeroslack#health\n"
                         "```text\n"
                         "[Ignored Link](https://ignored.example.com/fenced)\n"
                         "```\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto markdownIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("runbook.md");
    });
    QVERIFY(markdownIt != resources.cend());
    QVERIFY(std::any_of(markdownIt->anchors.cbegin(), markdownIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: ZeroSlack Dock Guide -> https://docs.example.com/zeroslack/dock#handoff")
            && anchor.line == 5;
    }));
    QVERIFY(std::any_of(markdownIt->anchors.cbegin(), markdownIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: status.example.com -> https://status.example.com/system")
            && anchor.line == 7;
    }));

    auto guideIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("ZeroSlack Dock Guide");
    });
    QVERIFY(guideIt != resources.cend());
    QCOMPARE(guideIt->location, QStringLiteral("https://docs.example.com/zeroslack/dock#handoff"));
    QVERIFY(guideIt->tags.contains(QStringLiteral("web")));
    QVERIFY(guideIt->tags.contains(QStringLiteral("text-link")));
    QVERIFY(guideIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(std::any_of(guideIt->anchors.cbegin(), guideIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("handoff");
    }));

    auto statusIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("status.example.com");
    });
    QVERIFY(statusIt != resources.cend());
    QCOMPARE(statusIt->location, QStringLiteral("https://status.example.com/system"));
    auto rawIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://raw.example.com/zeroslack#health");
    });
    QVERIFY(rawIt != resources.cend());
    QCOMPARE(rawIt->title, QStringLiteral("Raw status dashboard"));
    QVERIFY(rawIt->tags.contains(QStringLiteral("web-link")));
    QVERIFY(!rawIt->tags.contains(QStringLiteral("text-link")));
    QCOMPARE(markdownIt->relations.size(), 3);
    QVERIFY(std::any_of(markdownIt->relations.cbegin(), markdownIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == markdownIt->id
            && relation.targetResourceId == guideIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("text line 5: url: ZeroSlack Dock Guide -> https://docs.example.com/zeroslack/dock#handoff");
    }));
    QVERIFY(std::any_of(markdownIt->relations.cbegin(), markdownIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == markdownIt->id
            && relation.targetResourceId == statusIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("text line 7: url: status.example.com -> https://status.example.com/system");
    }));
    QVERIFY(std::any_of(markdownIt->relations.cbegin(), markdownIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == markdownIt->id
            && relation.targetResourceId == rawIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("text line 8: url: Raw status dashboard -> https://raw.example.com/zeroslack#health");
    }));

    QVERIFY(std::none_of(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location.contains(QStringLiteral("cdn.example.com"));
    }));
    QVERIFY(std::none_of(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location.contains(QStringLiteral("ignored.example.com"));
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("handoff")});
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("ZeroSlack Dock Guide")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment;
    }));

    const QList<ResourceRelation> markdownRelations = repository.resourceRelations(markdownIt->id);
    QCOMPARE(markdownRelations.size(), 3);
    QVERIFY(std::any_of(markdownRelations.cbegin(), markdownRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == markdownIt->id
            && relation.targetResourceId == guideIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("text line 5: url: ZeroSlack Dock Guide -> https://docs.example.com/zeroslack/dock#handoff");
    }));
    QVERIFY(std::any_of(markdownRelations.cbegin(), markdownRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == markdownIt->id
            && relation.targetResourceId == rawIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("text line 8: url: Raw status dashboard -> https://raw.example.com/zeroslack#health");
    }));

    const QList<ResourceRelation> guideRelations = repository.resourceRelations(guideIt->id);
    QCOMPARE(guideRelations.size(), 1);
    QCOMPARE(guideRelations.first().sourceResourceId, markdownIt->id);
    QCOMPARE(guideRelations.first().targetResourceId, guideIt->id);

    const QList<ResourceRelation> rawRelations = repository.resourceRelations(rawIt->id);
    QCOMPARE(rawRelations.size(), 1);
    QCOMPARE(rawRelations.first().sourceResourceId, markdownIt->id);
    QCOMPARE(rawRelations.first().targetResourceId, rawIt->id);

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("ZeroSlack Dock Guide")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("runbook.md")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 5
            && result.matchedAnchor->target == QLatin1String("url: ZeroSlack Dock Guide -> https://docs.example.com/zeroslack/dock#handoff");
    }));
}

void DirectorySourceTest::extractsReferenceStyleTextLinkUrlResources()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    writeFile(dir.filePath(QStringLiteral("library/runbook.md")),
              QByteArray("# Runbook\n"
                         "Read [Host API][pinloom-host] before wiring.\n"
                         "Skip image references like ![Badge][badge].\n"
                         "[pinloom-host]: https://docs.example.com/pinloom/host#context \"Host API\"\n"
                         "[badge]: https://cdn.example.com/badge.png\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto markdownIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("runbook.md");
    });
    QVERIFY(markdownIt != resources.cend());
    QVERIFY(std::any_of(markdownIt->anchors.cbegin(), markdownIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: Host API -> https://docs.example.com/pinloom/host#context")
            && anchor.line == 2;
    }));

    auto hostIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Host API");
    });
    QVERIFY(hostIt != resources.cend());
    QCOMPARE(hostIt->location, QStringLiteral("https://docs.example.com/pinloom/host#context"));
    QVERIFY(hostIt->tags.contains(QStringLiteral("text-link")));
    QVERIFY(hostIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(std::any_of(hostIt->anchors.cbegin(), hostIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("context");
    }));

    QVERIFY(std::none_of(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location.contains(QStringLiteral("cdn.example.com"));
    }));

    QCOMPARE(markdownIt->relations.size(), 1);
    QCOMPARE(markdownIt->relations.first().sourceResourceId, markdownIt->id);
    QCOMPARE(markdownIt->relations.first().targetResourceId, hostIt->id);
    QCOMPARE(markdownIt->relations.first().label, QStringLiteral("links-to"));
    QCOMPARE(markdownIt->relations.first().note,
             QStringLiteral("text line 2: url: Host API -> https://docs.example.com/pinloom/host#context"));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("context")});
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Host API")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment;
    }));

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("Host API")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("runbook.md")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 2
            && result.matchedAnchor->target == QLatin1String("url: Host API -> https://docs.example.com/pinloom/host#context");
    }));
}

void DirectorySourceTest::extractsPdfTitleAndPageAnchors()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    writeFile(dir.filePath(QStringLiteral("library/spec.pdf")),
              QByteArray("%PDF-1.4\n"
                         "1 0 obj << /Type /Catalog /Pages 2 0 R /Outlines 6 0 R /Dests 8 0 R >> endobj\n"
                         "2 0 obj << /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >> endobj\n"
                         "3 0 obj << /Type /Page /Parent 2 0 R >> endobj\n"
                         "4 0 obj << /Type /Page /Parent 2 0 R >> endobj\n"
                         "5 0 obj << /Title (PCIe Debug Spec) >> endobj\n"
                         "6 0 obj << /Type /Outlines /First 7 0 R /Last 11 0 R /Count 3 >> endobj\n"
                         "7 0 obj << /Title (Timing Closure Bookmark) /Dest [4 0 R /XYZ null null null] >> endobj\n"
                         "8 0 obj << /Dests << /overview [3 0 R /XYZ null null null] /remote 10 0 R >> >> endobj\n"
                         "9 0 obj << /Title (Overview Bookmark) /Dest /overview >> endobj\n"
                         "10 0 obj << /D [4 0 R /XYZ null null null] >> endobj\n"
                         "11 0 obj << /Title (Remote Bookmark) /Dest /remote >> endobj\n"
                         "trailer << /Root 1 0 R /Info 5 0 R >>\n"
                         "%%EOF\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto pdfIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Pdf;
    });
    QVERIFY(pdfIt != resources.cend());
    QVERIFY(pdfIt->aliases.contains(QStringLiteral("PCIe Debug Spec")));
    QCOMPARE(pdfIt->anchors.size(), 5);
    QCOMPARE(pdfIt->anchors.at(0).type, AnchorType::PdfPage);
    QCOMPARE(pdfIt->anchors.at(0).target, QStringLiteral("Page 1"));
    QCOMPARE(pdfIt->anchors.at(0).page, 1);
    QCOMPARE(pdfIt->anchors.at(1).target, QStringLiteral("Page 2"));
    QCOMPARE(pdfIt->anchors.at(1).page, 2);
    QCOMPARE(pdfIt->anchors.at(2).type, AnchorType::PdfPage);
    QCOMPARE(pdfIt->anchors.at(2).target, QStringLiteral("Timing Closure Bookmark"));
    QCOMPARE(pdfIt->anchors.at(2).page, 2);
    QCOMPARE(pdfIt->anchors.at(3).type, AnchorType::PdfPage);
    QCOMPARE(pdfIt->anchors.at(3).target, QStringLiteral("Overview Bookmark"));
    QCOMPARE(pdfIt->anchors.at(3).page, 1);
    QCOMPARE(pdfIt->anchors.at(4).type, AnchorType::PdfPage);
    QCOMPARE(pdfIt->anchors.at(4).target, QStringLiteral("Remote Bookmark"));
    QCOMPARE(pdfIt->anchors.at(4).page, 2);

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> titleResults = repository.search(SearchQuery{QStringLiteral("Debug")});
    QCOMPARE(titleResults.size(), 1);
    QCOMPARE(titleResults.first().matchedField, QStringLiteral("alias"));

    const QList<SearchResult> pageResults = repository.search(SearchQuery{QStringLiteral("Page 2")});
    QCOMPARE(pageResults.size(), 1);
    QVERIFY(pageResults.first().matchedAnchor.has_value());
    QCOMPARE(pageResults.first().matchedAnchor->type, AnchorType::PdfPage);
    QCOMPARE(pageResults.first().matchedAnchor->page, 2);

    const QList<SearchResult> outlineResults = repository.search(SearchQuery{QStringLiteral("Timing Closure")});
    QCOMPARE(outlineResults.size(), 1);
    QVERIFY(outlineResults.first().matchedAnchor.has_value());
    QCOMPARE(outlineResults.first().matchedAnchor->type, AnchorType::PdfPage);
    QCOMPARE(outlineResults.first().matchedAnchor->target, QStringLiteral("Timing Closure Bookmark"));
    QCOMPARE(outlineResults.first().matchedAnchor->page, 2);

    const QList<SearchResult> namedOutlineResults = repository.search(SearchQuery{QStringLiteral("Overview Bookmark")});
    QCOMPARE(namedOutlineResults.size(), 1);
    QVERIFY(namedOutlineResults.first().matchedAnchor.has_value());
    QCOMPARE(namedOutlineResults.first().matchedAnchor->type, AnchorType::PdfPage);
    QCOMPARE(namedOutlineResults.first().matchedAnchor->target, QStringLiteral("Overview Bookmark"));
    QCOMPARE(namedOutlineResults.first().matchedAnchor->page, 1);

    const QList<SearchResult> indirectOutlineResults = repository.search(SearchQuery{QStringLiteral("Remote Bookmark")});
    QCOMPARE(indirectOutlineResults.size(), 1);
    QVERIFY(indirectOutlineResults.first().matchedAnchor.has_value());
    QCOMPARE(indirectOutlineResults.first().matchedAnchor->type, AnchorType::PdfPage);
    QCOMPARE(indirectOutlineResults.first().matchedAnchor->target, QStringLiteral("Remote Bookmark"));
    QCOMPARE(indirectOutlineResults.first().matchedAnchor->page, 2);
}

void DirectorySourceTest::extractsPdfContentText()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    writeFile(dir.filePath(QStringLiteral("library/text.pdf")),
              QByteArray("%PDF-1.4\n"
                         "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
                         "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n"
                         "3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n"
                         "4 0 obj << /Length 116 >>\n"
                         "stream\n"
                         "BT\n"
                         "/F1 12 Tf\n"
                         "72 720 Td\n"
                         "(Pinloom launch matrix) Tj\n"
                         "[(ZeroSlack ) 120 (dock handoff)] TJ\n"
                         "[(Pin) -80 (loom ) 60 (array kerning)] TJ\n"
                         "<5043496520636f6e74656e74> Tj\n"
                         "ET\n"
                         "endstream\n"
                         "endobj\n"
                         "%%EOF\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto pdfIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Pdf && resource.title == QLatin1String("text.pdf");
    });
    QVERIFY(pdfIt != resources.cend());
    QVERIFY(pdfIt->content.contains(QStringLiteral("Pinloom launch matrix")));
    QVERIFY(pdfIt->content.contains(QStringLiteral("ZeroSlack dock handoff")));
    QVERIFY(pdfIt->content.contains(QStringLiteral("Pinloom array kerning")));
    QVERIFY(pdfIt->content.contains(QStringLiteral("PCIe content")));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("dock handoff")});
    QCOMPARE(contentResults.size(), 1);
    QCOMPARE(contentResults.first().resource.kind, ResourceKind::Pdf);
    QCOMPARE(contentResults.first().matchedField, QStringLiteral("content"));

    const QList<SearchResult> kerningResults = repository.search(SearchQuery{QStringLiteral("Pinloom array kerning")});
    QCOMPARE(kerningResults.size(), 1);
    QCOMPARE(kerningResults.first().resource.kind, ResourceKind::Pdf);
    QCOMPARE(kerningResults.first().matchedField, QStringLiteral("content"));
}

void DirectorySourceTest::extractsUtf16PdfContentText()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    writeFile(dir.filePath(QStringLiteral("library/utf16.pdf")),
              QByteArray("%PDF-1.4\n"
                         "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
                         "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n"
                         "3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n"
                         "4 0 obj << /Length 190 >>\n"
                         "stream\n"
                         "BT\n"
                         "/F1 12 Tf\n"
                         "72 720 Td\n"
                         "<FEFF00500069006E006C006F006F006D002000550054004600310036> Tj\n"
                         "(\\376\\377\\000Z\\000e\\000r\\000o\\000S\\000l\\000a\\000c\\000k\\000 \\000U\\000n\\000i\\000c\\000o\\000d\\000e) Tj\n"
                         "ET\n"
                         "endstream\n"
                         "endobj\n"
                         "5 0 obj << /Title <FEFF00500044004600200055006E00690063006F00640065> >> endobj\n"
                         "trailer << /Root 1 0 R /Info 5 0 R >>\n"
                         "%%EOF\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto pdfIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Pdf && resource.title == QLatin1String("utf16.pdf");
    });
    QVERIFY(pdfIt != resources.cend());
    QVERIFY(pdfIt->aliases.contains(QStringLiteral("PDF Unicode")));
    QVERIFY(pdfIt->content.contains(QStringLiteral("Pinloom UTF16")));
    QVERIFY(pdfIt->content.contains(QStringLiteral("ZeroSlack Unicode")));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("ZeroSlack Unicode")});
    QCOMPARE(contentResults.size(), 1);
    QCOMPARE(contentResults.first().resource.kind, ResourceKind::Pdf);
    QCOMPARE(contentResults.first().matchedField, QStringLiteral("content"));

    const QList<SearchResult> aliasResults = repository.search(SearchQuery{QStringLiteral("PDF Unicode")});
    QCOMPARE(aliasResults.size(), 1);
    QCOMPARE(aliasResults.first().matchedField, QStringLiteral("alias"));
}

void DirectorySourceTest::extractsToUnicodePdfContentText()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    writeFile(dir.filePath(QStringLiteral("library/cmap.pdf")),
              QByteArray("%PDF-1.4\n"
                         "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
                         "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n"
                         "3 0 obj << /Type /Page /Parent 2 0 R /Resources << /Font << /F1 6 0 R >> >> /Contents 4 0 R >> endobj\n"
                         "4 0 obj << /Length 82 >>\n"
                         "stream\n"
                         "BT\n"
                         "/F1 12 Tf\n"
                         "72 720 Td\n"
                         "<01020304050506> Tj\n"
                         "[<10111213> 120 <202122>] TJ\n"
                         "ET\n"
                         "endstream\n"
                         "endobj\n"
                         "5 0 obj << /Length 260 >>\n"
                         "stream\n"
                         "/CIDInit /ProcSet findresource begin\n"
                         "begincmap\n"
                         "7 beginbfchar\n"
                         "<01> <0050>\n"
                         "<02> <0069>\n"
                         "<03> <006E>\n"
                         "<04> <006C>\n"
                         "<05> <006F>\n"
                         "<06> <006D>\n"
                         "<13> <006F>\n"
                         "endbfchar\n"
                         "1 beginbfrange\n"
                         "<10> <12> [<005A> <0065> <0072>]\n"
                         "endbfrange\n"
                         "1 beginbfrange\n"
                         "<20> <22> <0061>\n"
                         "endbfrange\n"
                         "endcmap\n"
                         "end\n"
                         "endstream\n"
                         "endobj\n"
                         "6 0 obj << /Type /Font /Subtype /Type0 /BaseFont /F1 /ToUnicode 5 0 R >> endobj\n"
                         "%%EOF\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto pdfIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Pdf && resource.title == QLatin1String("cmap.pdf");
    });
    QVERIFY(pdfIt != resources.cend());
    QVERIFY(pdfIt->content.contains(QStringLiteral("Pinloom")));
    QVERIFY(pdfIt->content.contains(QStringLiteral("Zero abc")));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> pinloomResults = repository.search(SearchQuery{QStringLiteral("Pinloom")});
    QVERIFY(std::any_of(pinloomResults.cbegin(), pinloomResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Pdf
            && result.matchedField == QLatin1String("content");
    }));

    const QList<SearchResult> rangeResults = repository.search(SearchQuery{QStringLiteral("Zero abc")});
    QVERIFY(std::any_of(rangeResults.cbegin(), rangeResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Pdf
            && result.matchedField == QLatin1String("content");
    }));
}

void DirectorySourceTest::extractsFlateEncodedPdfContentText()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));

    const QByteArray contentStream("BT\n"
                                   "/F1 12 Tf\n"
                                   "72 720 Td\n"
                                   "(Compressed Pinloom matrix) Tj\n"
                                   "[(ZeroSlack ) 120 (dock handoff)] TJ\n"
                                   "ET\n");
    const QByteArray compressedStream = qCompress(contentStream, 9).mid(4);
    QByteArray pdf;
    pdf.append("%PDF-1.4\n");
    pdf.append("1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n");
    pdf.append("2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n");
    pdf.append("3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n");
    pdf.append("4 0 obj << /Length ");
    pdf.append(QByteArray::number(compressedStream.size()));
    pdf.append(" /Filter /FlateDecode /DL ");
    pdf.append(QByteArray::number(contentStream.size()));
    pdf.append(" >>\nstream\n");
    pdf.append(compressedStream);
    pdf.append("\nendstream\nendobj\n%%EOF\n");
    writeFile(dir.filePath(QStringLiteral("library/compressed.pdf")), pdf);

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto pdfIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Pdf && resource.title == QLatin1String("compressed.pdf");
    });
    QVERIFY(pdfIt != resources.cend());
    QVERIFY(pdfIt->content.contains(QStringLiteral("Compressed Pinloom matrix")));
    QVERIFY(pdfIt->content.contains(QStringLiteral("ZeroSlack dock handoff")));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("Compressed Pinloom")});
    QCOMPARE(contentResults.size(), 1);
    QCOMPARE(contentResults.first().resource.kind, ResourceKind::Pdf);
    QCOMPARE(contentResults.first().matchedField, QStringLiteral("content"));
}

void DirectorySourceTest::extractsAsciiHexEncodedPdfContentText()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));

    const QByteArray contentStream("BT\n"
                                   "/F1 12 Tf\n"
                                   "72 720 Td\n"
                                   "(ASCIIHex Pinloom matrix) Tj\n"
                                   "(Octal \\132eroSlack handoff) Tj\n"
                                   "ET\n");
    const QByteArray encodedStream = contentStream.toHex() + QByteArray(">");
    QByteArray pdf;
    pdf.append("%PDF-1.4\n");
    pdf.append("1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n");
    pdf.append("2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n");
    pdf.append("3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n");
    pdf.append("4 0 obj << /Length ");
    pdf.append(QByteArray::number(encodedStream.size()));
    pdf.append(" /Filter /ASCIIHexDecode >>\nstream\n");
    pdf.append(encodedStream);
    pdf.append("\nendstream\nendobj\n%%EOF\n");
    writeFile(dir.filePath(QStringLiteral("library/asciihex.pdf")), pdf);

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto pdfIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Pdf && resource.title == QLatin1String("asciihex.pdf");
    });
    QVERIFY(pdfIt != resources.cend());
    QVERIFY(pdfIt->content.contains(QStringLiteral("ASCIIHex Pinloom matrix")));
    QVERIFY(pdfIt->content.contains(QStringLiteral("Octal ZeroSlack handoff")));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("Octal ZeroSlack")});
    QCOMPARE(contentResults.size(), 1);
    QCOMPARE(contentResults.first().resource.kind, ResourceKind::Pdf);
    QCOMPARE(contentResults.first().matchedField, QStringLiteral("content"));
}

void DirectorySourceTest::extractsAscii85EncodedPdfContentText()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));

    const QByteArray contentStream("BT\n"
                                   "/F1 12 Tf\n"
                                   "72 720 Td\n"
                                   "(ASCII85 Pinloom matrix) Tj\n"
                                   "[(Encoded ) 80 (ZeroSlack handoff)] TJ\n"
                                   "ET\n");
    const QByteArray encodedStream = ascii85Encode(contentStream);
    QByteArray pdf;
    pdf.append("%PDF-1.4\n");
    pdf.append("1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n");
    pdf.append("2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n");
    pdf.append("3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n");
    pdf.append("4 0 obj << /Length ");
    pdf.append(QByteArray::number(encodedStream.size()));
    pdf.append(" /Filter /ASCII85Decode >>\nstream\n");
    pdf.append(encodedStream);
    pdf.append("\nendstream\nendobj\n%%EOF\n");
    writeFile(dir.filePath(QStringLiteral("library/ascii85.pdf")), pdf);

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto pdfIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Pdf && resource.title == QLatin1String("ascii85.pdf");
    });
    QVERIFY(pdfIt != resources.cend());
    QVERIFY(pdfIt->content.contains(QStringLiteral("ASCII85 Pinloom matrix")));
    QVERIFY(pdfIt->content.contains(QStringLiteral("Encoded ZeroSlack handoff")));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("ASCII85 Pinloom")});
    QCOMPARE(contentResults.size(), 1);
    QCOMPARE(contentResults.first().resource.kind, ResourceKind::Pdf);
    QCOMPARE(contentResults.first().matchedField, QStringLiteral("content"));
}

void DirectorySourceTest::extractsRunLengthEncodedPdfContentText()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));

    const QByteArray contentStream("BT\n"
                                   "/F1 12 Tf\n"
                                   "72 720 Td\n"
                                   "(RunLength Pinloom matrix) Tj\n"
                                   "[(Repeated ) 80 (ZeroSlack handoff)] TJ\n"
                                   "ET\n");
    const QByteArray encodedStream = runLengthEncode(contentStream);
    QByteArray pdf;
    pdf.append("%PDF-1.4\n");
    pdf.append("1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n");
    pdf.append("2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n");
    pdf.append("3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n");
    pdf.append("4 0 obj << /Length ");
    pdf.append(QByteArray::number(encodedStream.size()));
    pdf.append(" /Filter /RunLengthDecode >>\nstream\n");
    pdf.append(encodedStream);
    pdf.append("\nendstream\nendobj\n%%EOF\n");
    writeFile(dir.filePath(QStringLiteral("library/runlength.pdf")), pdf);

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto pdfIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Pdf && resource.title == QLatin1String("runlength.pdf");
    });
    QVERIFY(pdfIt != resources.cend());
    QVERIFY(pdfIt->content.contains(QStringLiteral("RunLength Pinloom matrix")));
    QVERIFY(pdfIt->content.contains(QStringLiteral("Repeated ZeroSlack handoff")));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("RunLength Pinloom")});
    QCOMPARE(contentResults.size(), 1);
    QCOMPARE(contentResults.first().resource.kind, ResourceKind::Pdf);
    QCOMPARE(contentResults.first().matchedField, QStringLiteral("content"));
}

void DirectorySourceTest::extractsLzwEncodedPdfContentText()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));

    const QByteArray encodedStream = QByteArray::fromHex(
        "80108a80a179186220188c84054330286f0a878c21664050a0985a2b880a069371b0de"
        "6f36880da613a1c8d27814c2cd40a2d8a0b465391bca66c3098cd62094c262428321"
        "be6c2031498c867328a4bb0b250288b03808");
    QByteArray pdf;
    pdf.append("%PDF-1.4\n");
    pdf.append("1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n");
    pdf.append("2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n");
    pdf.append("3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n");
    pdf.append("4 0 obj << /Length ");
    pdf.append(QByteArray::number(encodedStream.size()));
    pdf.append(" /Filter /LZWDecode /DecodeParms << /EarlyChange 1 >> >>\nstream\n");
    pdf.append(encodedStream);
    pdf.append("\nendstream\nendobj\n%%EOF\n");
    writeFile(dir.filePath(QStringLiteral("library/lzw.pdf")), pdf);

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto pdfIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Pdf && resource.title == QLatin1String("lzw.pdf");
    });
    QVERIFY(pdfIt != resources.cend());
    QVERIFY(pdfIt->content.contains(QStringLiteral("LZW Pinloom matrix")));
    QVERIFY(pdfIt->content.contains(QStringLiteral("ZeroSlack dock bridge")));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("LZW Pinloom")});
    QCOMPARE(contentResults.size(), 1);
    QCOMPARE(contentResults.first().resource.kind, ResourceKind::Pdf);
    QCOMPARE(contentResults.first().matchedField, QStringLiteral("content"));
}

void DirectorySourceTest::extractsChainedFilterPdfContentText()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));

    const QByteArray contentStream("BT\n"
                                   "/F1 12 Tf\n"
                                   "72 720 Td\n"
                                   "(Chained filter Pinloom matrix) Tj\n"
                                   "[(ASCII85 ) 80 (plus Flate handoff)] TJ\n"
                                   "ET\n");
    const QByteArray compressedStream = qCompress(contentStream, 9).mid(4);
    const QByteArray encodedStream = ascii85Encode(compressedStream);
    QByteArray pdf;
    pdf.append("%PDF-1.4\n");
    pdf.append("1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n");
    pdf.append("2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n");
    pdf.append("3 0 obj << /Type /Page /Parent 2 0 R /Contents 4 0 R >> endobj\n");
    pdf.append("4 0 obj << /Length ");
    pdf.append(QByteArray::number(encodedStream.size()));
    pdf.append(" /Filter [/ASCII85Decode /FlateDecode] /DL ");
    pdf.append(QByteArray::number(contentStream.size()));
    pdf.append(" >>\nstream\n");
    pdf.append(encodedStream);
    pdf.append("\nendstream\nendobj\n%%EOF\n");
    writeFile(dir.filePath(QStringLiteral("library/chained.pdf")), pdf);

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto pdfIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Pdf && resource.title == QLatin1String("chained.pdf");
    });
    QVERIFY(pdfIt != resources.cend());
    QVERIFY(pdfIt->content.contains(QStringLiteral("Chained filter Pinloom matrix")));
    QVERIFY(pdfIt->content.contains(QStringLiteral("ASCII85 plus Flate handoff")));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("plus Flate")});
    QCOMPARE(contentResults.size(), 1);
    QCOMPARE(contentResults.first().resource.kind, ResourceKind::Pdf);
    QCOMPARE(contentResults.first().matchedField, QStringLiteral("content"));
}

void DirectorySourceTest::extractsPdfRegionAnchors()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    writeFile(dir.filePath(QStringLiteral("library/annotated.pdf")),
              QByteArray("%PDF-1.4\n"
                         "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
                         "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n"
                         "3 0 obj << /Type /Page /Parent 2 0 R /Annots [4 0 R 5 0 R] >> endobj\n"
                         "4 0 obj << /Type /Annot /Subtype /Highlight /Rect [10 20 110 60] /Contents (Clock domain note) >> endobj\n"
                         "5 0 obj << /Type /Annot /Subtype /Link /Rect [20 80 160 110] /Contents (PDF Link Guide) /A << /S /URI /URI (https://docs.example.com/pinloom/pdf#guide) >> >> endobj\n"
                         "trailer << /Root 1 0 R >>\n"
                         "%%EOF\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto pdfIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Pdf;
    });
    QVERIFY(pdfIt != resources.cend());
    QVERIFY(std::any_of(pdfIt->anchors.cbegin(), pdfIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::PdfRegion
            && anchor.target == QLatin1String("Clock domain note")
            && anchor.page == 1
            && anchor.region == QRectF(10.0, 20.0, 100.0, 40.0);
    }));
    auto linkIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("PDF Link Guide")
            && resource.location == QLatin1String("https://docs.example.com/pinloom/pdf#guide");
    });
    QVERIFY(linkIt != resources.cend());
    QVERIFY(linkIt->tags.contains(QStringLiteral("pdf-link")));
    QVERIFY(linkIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(linkIt->aliases.contains(QStringLiteral("annotated")));
    QVERIFY(linkIt->aliases.contains(QStringLiteral("page 1")));
    QVERIFY(std::any_of(linkIt->anchors.cbegin(), linkIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("guide");
    }));
    QVERIFY(std::any_of(pdfIt->relations.cbegin(), pdfIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == pdfIt->id
            && relation.targetResourceId == linkIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("pdf page 1 link: url: PDF Link Guide -> https://docs.example.com/pinloom/pdf#guide");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> regionResults = repository.search(SearchQuery{QStringLiteral("Clock")});
    QCOMPARE(regionResults.size(), 1);
    QVERIFY(regionResults.first().matchedAnchor.has_value());
    QCOMPARE(regionResults.first().matchedAnchor->type, AnchorType::PdfRegion);
    QCOMPARE(regionResults.first().matchedAnchor->page, 1);
    QCOMPARE(regionResults.first().matchedAnchor->region, QRectF(10.0, 20.0, 100.0, 40.0));

    const QList<ResourceRelation> relations = repository.resourceRelations(pdfIt->id);
    QVERIFY(std::any_of(relations.cbegin(), relations.cend(), [&](const ResourceRelation &relation) {
        return relation.targetResourceId == linkIt->id
            && relation.label == QLatin1String("links-to");
    }));

    const QList<SearchResult> linkResults = repository.search(SearchQuery{QStringLiteral("PDF Link Guide")});
    QVERIFY(std::any_of(linkResults.cbegin(), linkResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("PDF Link Guide")
            && result.matchedField == QLatin1String("title");
    }));
}

void DirectorySourceTest::extractsUnifiedTextBeaconAnchors()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/text")));
    QVERIFY(dir.mkpath(QStringLiteral("library/logs")));
    QVERIFY(dir.mkpath(QStringLiteral("library/config")));
    QVERIFY(dir.mkpath(QStringLiteral("library/scripts")));
    writeFile(dir.filePath(QStringLiteral("library/text/handoff.txt")),
              QByteArray("// ANCHOR: host dock handoff\n"
                         "MARKER: jump target\n"
                         "handoff = planned\n"
                         "[Build Handoff]\n"
                         "WARNING: route timing changed\n"
                         "https://docs.example.com/pinloom/jump\n"
                         "TODO: wire ZeroSlack jump\n"));
    writeFile(dir.filePath(QStringLiteral("library/logs/review.log")),
              QByteArray("BOOKMARK: route log\n"
                         "ERROR: missing reset\n"
                         "status: reviewed\n"));
    writeFile(dir.filePath(QStringLiteral("library/config/flow.cfg")),
              QByteArray("; MARKER: launch dock\n"
                         "status: ready\n"));
    writeFile(dir.filePath(QStringLiteral("library/scripts/handoff")),
              QByteArray("#!/usr/bin/env custom-runner\n"
                         "ANCHOR: handoff_entry\n"
                         "TODO: no-extension text without interpreter allowlist\n"));
    writeFile(dir.filePath(QStringLiteral("library/config/pins.txt")),
              QByteArray("NOTE: board pin review\n"
                         "set_property PACKAGE_PIN A1 [get_ports clk]\n"));
    writeFile(dir.filePath(QStringLiteral("library/notes.opaque")),
              QByteArray("MARKER: opaque_payload\n"
                         "TODO: index unknown suffix text\n"
                         "https://docs.example.com/pinloom/opaque\n"));
    writeFile(dir.filePath(QStringLiteral("library/raw.opaque")),
              QByteArray("TODO: hidden\0binary", 19));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto findFile = [&](const QString &title) {
        return std::find_if(resources.cbegin(), resources.cend(), [&](const Resource &resource) {
            return resource.kind == ResourceKind::File && resource.title == title;
        });
    };
    auto hasLineAnchor = [](const Resource &resource, const QString &target, int line) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::FileLine
                && anchor.target == target
                && anchor.line == line;
        });
    };

    const auto handoffTextIt = findFile(QStringLiteral("handoff.txt"));
    QVERIFY(handoffTextIt != resources.cend());
    QVERIFY(hasLineAnchor(*handoffTextIt, QStringLiteral("marker: host dock handoff"), 1));
    QVERIFY(hasLineAnchor(*handoffTextIt, QStringLiteral("marker: jump target"), 2));
    QVERIFY(hasLineAnchor(*handoffTextIt, QStringLiteral("section: Build Handoff"), 4));
    QVERIFY(hasLineAnchor(*handoffTextIt, QStringLiteral("warning: WARNING: route timing changed"), 5));
    QVERIFY(hasLineAnchor(*handoffTextIt, QStringLiteral("url: https://docs.example.com/pinloom/jump"), 6));
    QVERIFY(hasLineAnchor(*handoffTextIt, QStringLiteral("TODO: wire ZeroSlack jump"), 7));

    const auto logIt = findFile(QStringLiteral("review.log"));
    QVERIFY(logIt != resources.cend());
    QVERIFY(hasLineAnchor(*logIt, QStringLiteral("marker: route log"), 1));
    QVERIFY(hasLineAnchor(*logIt, QStringLiteral("error: ERROR: missing reset"), 2));

    const auto configIt = findFile(QStringLiteral("flow.cfg"));
    QVERIFY(configIt != resources.cend());
    QVERIFY(hasLineAnchor(*configIt, QStringLiteral("marker: launch dock"), 1));

    const auto handoffIt = findFile(QStringLiteral("handoff"));
    QVERIFY(handoffIt != resources.cend());
    QVERIFY(hasLineAnchor(*handoffIt, QStringLiteral("marker: handoff_entry"), 2));
    QVERIFY(hasLineAnchor(*handoffIt, QStringLiteral("TODO: no-extension text without interpreter allowlist"), 3));

    const auto pinsIt = findFile(QStringLiteral("pins.txt"));
    QVERIFY(pinsIt != resources.cend());
    QVERIFY(hasLineAnchor(*pinsIt, QStringLiteral("NOTE: board pin review"), 1));

    const auto opaqueIt = findFile(QStringLiteral("notes.opaque"));
    QVERIFY(opaqueIt != resources.cend());
    QVERIFY(hasLineAnchor(*opaqueIt, QStringLiteral("marker: opaque_payload"), 1));
    QVERIFY(hasLineAnchor(*opaqueIt, QStringLiteral("TODO: index unknown suffix text"), 2));
    QVERIFY(hasLineAnchor(*opaqueIt, QStringLiteral("url: https://docs.example.com/pinloom/opaque"), 3));

    const auto rawIt = findFile(QStringLiteral("raw.opaque"));
    QVERIFY(rawIt != resources.cend());
    QVERIFY(std::none_of(rawIt->anchors.cbegin(), rawIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.target.contains(QStringLiteral("hidden"));
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> beaconResults = repository.search(SearchQuery{QStringLiteral("jump target")});
    QVERIFY(std::any_of(beaconResults.cbegin(), beaconResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("handoff.txt")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("marker: jump target");
    }));

    const QList<SearchResult> warningResults = repository.search(SearchQuery{QStringLiteral("route timing")});
    QVERIFY(std::any_of(warningResults.cbegin(), warningResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("handoff.txt")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->target == QLatin1String("warning: WARNING: route timing changed");
    }));

    const QList<SearchResult> opaqueResults = repository.search(SearchQuery{QStringLiteral("unknown suffix")});
    QVERIFY(std::any_of(opaqueResults.cbegin(), opaqueResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("notes.opaque")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->target == QLatin1String("TODO: index unknown suffix text");
    }));

    const QList<SearchResult> handoffResults = repository.search(SearchQuery{QStringLiteral("interpreter allowlist")});
    QVERIFY(std::any_of(handoffResults.cbegin(), handoffResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("handoff")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->target == QLatin1String("TODO: no-extension text without interpreter allowlist");
    }));

    const QList<SearchResult> hiddenResults = repository.search(SearchQuery{QStringLiteral("hidden")});
    QVERIFY(std::none_of(hiddenResults.cbegin(), hiddenResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("raw.opaque");
    }));
}

void DirectorySourceTest::extractsNeutralDirectiveTextBeaconAnchors()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/build/cmake")));
    writeFile(dir.filePath(QStringLiteral("library/CMakeLists.txt")),
              QByteArray("cmake_minimum_required(VERSION 3.24)\n"
                         "project(PinloomHost)\n"
                         "find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)\n"
                         "option(PINLOOM_ENABLE_REMOTE_FETCH \"Fetch remote pages\" ON)\n"
                         "add_library(pinloom_core STATIC src/core.cpp)\n"
                         "add_executable(pinloom_app src/main.cpp)\n"
                         "add_custom_target(pinloom_docs)\n"
                         "add_test(NAME pinloom_core_smoke_test COMMAND pinloom_core_smoke_test)\n"));
    writeFile(dir.filePath(QStringLiteral("library/build/cmake/PinloomHelpers.cmake")),
              QByteArray("function(pinloom_add_widget_test name)\n"
                         "endfunction()\n"
                         "macro(pinloom_copy_runtime)\n"
                         "endmacro()\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto findFile = [&](const QString &title) {
        return std::find_if(resources.cbegin(), resources.cend(), [&](const Resource &resource) {
            return resource.kind == ResourceKind::File && resource.title == title;
        });
    };
    auto hasBeacon = [](const Resource &resource, const QString &target, int line) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::FileLine
                && anchor.target == target
                && anchor.line == line;
        });
    };
    auto hasLineAnchor = [](const Resource &resource, const QString &target, int line) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::FileLine
                && anchor.target == target
                && anchor.line == line;
        });
    };

    const auto directiveTextIt = findFile(QStringLiteral("CMakeLists.txt"));
    QVERIFY(directiveTextIt != resources.cend());
    QVERIFY(hasBeacon(*directiveTextIt, QStringLiteral("directive entry: PinloomHost"), 2));
    QVERIFY(hasLineAnchor(*directiveTextIt, QStringLiteral("directive reference: Qt6"), 3));
    QVERIFY(hasBeacon(*directiveTextIt, QStringLiteral("directive setting: PINLOOM_ENABLE_REMOTE_FETCH"), 4));
    QVERIFY(hasBeacon(*directiveTextIt, QStringLiteral("directive entry: pinloom_core"), 5));
    QVERIFY(hasBeacon(*directiveTextIt, QStringLiteral("directive entry: pinloom_app"), 6));
    QVERIFY(hasBeacon(*directiveTextIt, QStringLiteral("directive entry: pinloom_docs"), 7));
    QVERIFY(hasBeacon(*directiveTextIt, QStringLiteral("directive entry: pinloom_core_smoke_test"), 8));

    const auto helpersIt = findFile(QStringLiteral("PinloomHelpers.cmake"));
    QVERIFY(helpersIt != resources.cend());
    QVERIFY(hasBeacon(*helpersIt, QStringLiteral("directive block: pinloom_add_widget_test"), 1));
    QVERIFY(hasBeacon(*helpersIt, QStringLiteral("directive block: pinloom_copy_runtime"), 3));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> entryResults = repository.search(SearchQuery{QStringLiteral("pinloom_core")});
    QVERIFY(std::any_of(entryResults.cbegin(), entryResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("CMakeLists.txt")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("directive entry: pinloom_core");
    }));

    const QList<SearchResult> referenceResults = repository.search(SearchQuery{QStringLiteral("Qt6")});
    QVERIFY(std::any_of(referenceResults.cbegin(), referenceResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("CMakeLists.txt")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("directive reference: Qt6");
    }));
}

void DirectorySourceTest::extractsRuleAndContainerTextBeaconAnchors()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/build")));
    writeFile(dir.filePath(QStringLiteral("library/Makefile")),
              QByteArray(".PHONY: all clean\n"
                         "all build: app\n"
                         "\t@echo build\n"
                         "clean:\n"
                         "\t@rm -rf build\n"));
    writeFile(dir.filePath(QStringLiteral("library/build/rules.mk")),
              QByteArray("pinloom-docs:\n"
                         "\t@echo docs\n"));
    writeFile(dir.filePath(QStringLiteral("library/Dockerfile")),
              QByteArray("FROM qt:6.10 AS build\n"
                         "COPY src/ /app/src/\n"
                         "ADD assets.tar.gz /app/assets/\n"
                         "FROM build AS runtime\n"));
    writeFile(dir.filePath(QStringLiteral("library/Dockerfile.dev")),
              QByteArray("FROM ubuntu:24.04\n"));
    writeFile(dir.filePath(QStringLiteral("library/app.dockerfile")),
              QByteArray("FROM alpine:3.20 AS tools\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto findFile = [&](const QString &title) {
        return std::find_if(resources.cbegin(), resources.cend(), [&](const Resource &resource) {
            return resource.kind == ResourceKind::File && resource.title == title;
        });
    };
    auto hasBeacon = [](const Resource &resource, const QString &target, int line) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::FileLine
                && anchor.target == target
                && anchor.line == line;
        });
    };
    auto hasLineAnchor = [](const Resource &resource, const QString &target, int line) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::FileLine
                && anchor.target == target
                && anchor.line == line;
        });
    };

    const auto makefileIt = findFile(QStringLiteral("Makefile"));
    QVERIFY(makefileIt != resources.cend());
    QVERIFY(hasBeacon(*makefileIt, QStringLiteral("rule entry: all"), 2));
    QVERIFY(hasBeacon(*makefileIt, QStringLiteral("rule entry: build"), 2));
    QVERIFY(hasBeacon(*makefileIt, QStringLiteral("rule entry: clean"), 4));
    QVERIFY(!hasBeacon(*makefileIt, QStringLiteral("rule entry: .PHONY"), 1));

    const auto rulesIt = findFile(QStringLiteral("rules.mk"));
    QVERIFY(rulesIt != resources.cend());
    QVERIFY(hasBeacon(*rulesIt, QStringLiteral("rule entry: pinloom-docs"), 1));

    const auto containerIt = findFile(QStringLiteral("Dockerfile"));
    QVERIFY(containerIt != resources.cend());
    QVERIFY(hasBeacon(*containerIt, QStringLiteral("container block: build"), 1));
    QVERIFY(hasLineAnchor(*containerIt, QStringLiteral("container input: qt:6.10"), 1));
    QVERIFY(hasLineAnchor(*containerIt, QStringLiteral("container input: src/"), 2));
    QVERIFY(hasLineAnchor(*containerIt, QStringLiteral("container input: assets.tar.gz"), 3));
    QVERIFY(hasBeacon(*containerIt, QStringLiteral("container block: runtime"), 4));

    const auto containerDevIt = findFile(QStringLiteral("Dockerfile.dev"));
    QVERIFY(containerDevIt != resources.cend());
    QVERIFY(hasLineAnchor(*containerDevIt, QStringLiteral("container input: ubuntu:24.04"), 1));

    const auto containerSuffixIt = findFile(QStringLiteral("app.dockerfile"));
    QVERIFY(containerSuffixIt != resources.cend());
    QVERIFY(hasBeacon(*containerSuffixIt, QStringLiteral("container block: tools"), 1));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> makeResults = repository.search(SearchQuery{QStringLiteral("pinloom-docs")});
    QVERIFY(std::any_of(makeResults.cbegin(), makeResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("rules.mk")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("rule entry: pinloom-docs");
    }));

    const QList<SearchResult> containerBlockResults = repository.search(SearchQuery{QStringLiteral("runtime")});
    QVERIFY(std::any_of(containerBlockResults.cbegin(), containerBlockResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("Dockerfile")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("container block: runtime");
    }));

    const QList<SearchResult> containerInputResults = repository.search(SearchQuery{QStringLiteral("assets.tar.gz")});
    QVERIFY(std::any_of(containerInputResults.cbegin(), containerInputResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("Dockerfile")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("container input: assets.tar.gz");
    }));
}

void DirectorySourceTest::extractsFileReferenceManifestBeacons()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/build")));
    QVERIFY(dir.mkpath(QStringLiteral("library/inputs")));
    writeFile(dir.filePath(QStringLiteral("library/inputs/beacon-input.txt")),
              QByteArray("jump target referenced file\n"
                         "ready\n"));

    QString libraryPath = QDir::cleanPath(dir.filePath(QStringLiteral("library")));
    writeFile(dir.filePath(QStringLiteral("library/build/compile_commands.json")),
              QStringLiteral("[\n"
                             "  {\n"
                             "    \"directory\": \"%1\",\n"
                             "    \"command\": \"indexer --input inputs/beacon-input.txt --output build/beacon-output.loc\",\n"
                             "    \"file\": \"inputs/beacon-input.txt\",\n"
                             "    \"output\": \"build/beacon-output.loc\"\n"
                             "  }\n"
                             "]\n")
                  .arg(libraryPath.replace(QLatin1Char('\\'), QLatin1Char('/')))
                  .toUtf8());

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto referenceManifestIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("compile_commands.json");
    });
    QVERIFY(referenceManifestIt != resources.cend());

    auto inputFileIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("beacon-input.txt");
    });
    QVERIFY(inputFileIt != resources.cend());

    const QString anchorTarget =
        QStringLiteral("file reference: ../inputs/beacon-input.txt -> build/beacon-output.loc");
    QVERIFY(std::any_of(referenceManifestIt->anchors.cbegin(), referenceManifestIt->anchors.cend(), [&](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == anchorTarget
            && anchor.line == 5;
    }));

    QCOMPARE(referenceManifestIt->relations.size(), 1);
    QCOMPARE(referenceManifestIt->relations.first().sourceResourceId, referenceManifestIt->id);
    QCOMPARE(referenceManifestIt->relations.first().targetResourceId, inputFileIt->id);
    QCOMPARE(referenceManifestIt->relations.first().label, QStringLiteral("file-reference"));
    QCOMPARE(referenceManifestIt->relations.first().note,
             QStringLiteral("file reference line 5: %1").arg(anchorTarget));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> outputResults = repository.search(SearchQuery{QStringLiteral("beacon-output")});
    QVERIFY(std::any_of(outputResults.cbegin(), outputResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("compile_commands.json")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 5;
    }));

    SearchQuery query{QStringLiteral("jump target")};
    query.contextResourceIds = {referenceManifestIt->id};
    const QList<SearchResult> contextResults = repository.search(query);
    QVERIFY(std::any_of(contextResults.cbegin(), contextResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("beacon-input.txt")
            && result.matchedContextRelationLabel == QLatin1String("file-reference");
    }));

    const QList<ResourceRelation> inputFileRelations = repository.resourceRelations(inputFileIt->id);
    QCOMPARE(inputFileRelations.size(), 1);
    QCOMPARE(inputFileRelations.first().sourceResourceId, referenceManifestIt->id);
    QCOMPARE(inputFileRelations.first().targetResourceId, inputFileIt->id);
}

void DirectorySourceTest::extractsTextActionLineAnchors()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/logs")));
    QVERIFY(dir.mkpath(QStringLiteral("library/constraints")));
    writeFile(dir.filePath(QStringLiteral("library/logs/build.log")),
              QByteArray("TODO: wire ZeroSlack dock\n"
                         "INFO: ready\n"
                         "FIXME: remove blocking wait\n"));
    writeFile(dir.filePath(QStringLiteral("library/constraints/top.xdc")),
              QByteArray("NOTE: cache active project\n"
                         "set_property PACKAGE_PIN A1 [get_ports clk]\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto logIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File && resource.title == QLatin1String("build.log");
    });
    QVERIFY(logIt != resources.cend());
    QVERIFY(std::any_of(logIt->anchors.cbegin(), logIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("TODO: wire ZeroSlack dock")
            && anchor.line == 1;
    }));
    QVERIFY(std::any_of(logIt->anchors.cbegin(), logIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("FIXME: remove blocking wait")
            && anchor.line == 3;
    }));

    auto xdcIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File && resource.title == QLatin1String("top.xdc");
    });
    QVERIFY(xdcIt != resources.cend());
    QVERIFY(std::any_of(xdcIt->anchors.cbegin(), xdcIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("NOTE: cache active project")
            && anchor.line == 1;
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> todoResults = repository.search(SearchQuery{QStringLiteral("ZeroSlack dock")});
    QVERIFY(std::any_of(todoResults.cbegin(), todoResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("build.log")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 1;
    }));

    const QList<SearchResult> fixmeResults = repository.search(SearchQuery{QStringLiteral("blocking wait")});
    QVERIFY(std::any_of(fixmeResults.cbegin(), fixmeResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("build.log")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 3;
    }));
}

void DirectorySourceTest::extractsWebShortcutResources()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/links")));
    writeFile(dir.filePath(QStringLiteral("library/links/Pinloom Setup.url")),
              QByteArray("[InternetShortcut]\n"
                         "URL=https://docs.example.com/pinloom/setup#install\n"
                         "Name=Pinloom Setup Guide\n"));
    writeFile(dir.filePath(QStringLiteral("library/links/ZeroSlack Dock.desktop")),
              QByteArray("[Desktop Entry]\n"
                         "Type=Link\n"
                         "Name=ZeroSlack Dock Dashboard\n"
                         "URL=https://dash.example.org/zeroslack#dock\n"));
    writeFile(dir.filePath(QStringLiteral("library/links/Pinloom App.desktop")),
              QByteArray("[Desktop Entry]\n"
                         "Type=Application\n"
                         "Name=Pinloom App\n"
                         "Exec=pinloom\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto urlIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Pinloom Setup Guide");
    });
    QVERIFY(urlIt != resources.cend());
    QCOMPARE(urlIt->title, QStringLiteral("Pinloom Setup Guide"));
    QCOMPARE(urlIt->location, QStringLiteral("https://docs.example.com/pinloom/setup#install"));
    QVERIFY(urlIt->tags.contains(QStringLiteral("web")));
    QVERIFY(urlIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(urlIt->aliases.contains(QStringLiteral("https://docs.example.com/pinloom/setup#install")));
    QVERIFY(std::any_of(urlIt->anchors.cbegin(), urlIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("install");
    }));
    QVERIFY(std::any_of(urlIt->anchors.cbegin(), urlIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: Pinloom Setup Guide -> https://docs.example.com/pinloom/setup#install")
            && anchor.line == 2;
    }));
    QVERIFY(std::any_of(urlIt->anchors.cbegin(), urlIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("shortcut title: Pinloom Setup Guide")
            && anchor.line == 3;
    }));

    auto desktopIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("ZeroSlack Dock Dashboard");
    });
    QVERIFY(desktopIt != resources.cend());
    QCOMPARE(desktopIt->location, QStringLiteral("https://dash.example.org/zeroslack#dock"));
    QVERIFY(desktopIt->aliases.contains(QStringLiteral("dash.example.org")));
    QVERIFY(std::any_of(desktopIt->anchors.cbegin(), desktopIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("dock");
    }));
    QVERIFY(std::any_of(desktopIt->anchors.cbegin(), desktopIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: ZeroSlack Dock Dashboard -> https://dash.example.org/zeroslack#dock")
            && anchor.line == 4;
    }));
    QVERIFY(std::any_of(desktopIt->anchors.cbegin(), desktopIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("shortcut title: ZeroSlack Dock Dashboard")
            && anchor.line == 3;
    }));

    auto launcherIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.title == QLatin1String("Pinloom App.desktop");
    });
    QVERIFY(launcherIt != resources.cend());
    QCOMPARE(launcherIt->kind, ResourceKind::File);

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> hostResults = repository.search(SearchQuery{QStringLiteral("docs.example.com")});
    QVERIFY(std::any_of(hostResults.cbegin(), hostResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinloom Setup Guide");
    }));

    const QList<SearchResult> desktopHostResults = repository.search(SearchQuery{QStringLiteral("dash.example.org")});
    QVERIFY(std::any_of(desktopHostResults.cbegin(), desktopHostResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("ZeroSlack Dock Dashboard");
    }));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("install")});
    QVERIFY(!fragmentResults.isEmpty());
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("install");
    }));

    const QList<SearchResult> shortcutTitleResults = repository.search(SearchQuery{QStringLiteral("shortcut title")});
    QVERIFY(std::any_of(shortcutTitleResults.cbegin(), shortcutTitleResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinloom Setup Guide")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 3
            && result.matchedAnchor->target == QLatin1String("shortcut title: Pinloom Setup Guide");
    }));

    const QList<SearchResult> desktopFragmentResults = repository.search(SearchQuery{QStringLiteral("dock")});
    QVERIFY(std::any_of(desktopFragmentResults.cbegin(), desktopFragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("ZeroSlack Dock Dashboard")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("dock");
    }));

    const QList<SearchResult> shortcutUrlResults = repository.search(SearchQuery{QStringLiteral("dash.example.org")});
    QVERIFY(std::any_of(shortcutUrlResults.cbegin(), shortcutUrlResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("ZeroSlack Dock Dashboard")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 4
            && result.matchedAnchor->target == QLatin1String("url: ZeroSlack Dock Dashboard -> https://dash.example.org/zeroslack#dock");
    }));
}

void DirectorySourceTest::extractsContentSniffedTextUrlResources()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/links")));
    writeFile(dir.filePath(QStringLiteral("library/links/research.urls")),
              QByteArray("Pinloom Launch Notes - https://docs.example.com/pinloom/launch#overview\n"
                         "https://status.example.org/zeroslack\n"
                         "Duplicate: https://docs.example.com/pinloom/launch#overview\n"
                         "Ignore local file://not-web\n"
                         "Read [Pinloom Portal](https://portal.example.com/pinloom#home)\n"
                         "See [Host API][pinloom-host]\n"
                         "[pinloom-host]: https://api.example.com/pinloom#v1\n"));
    writeFile(dir.filePath(QStringLiteral("library/links/jump-targets.opaque")),
              QByteArray("Opaque Jump Target - https://jump.example.net/pinloom#opaque\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto fileIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("research.urls");
    });
    QVERIFY(fileIt != resources.cend());
    QVERIFY(std::any_of(fileIt->anchors.cbegin(), fileIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: Pinloom Launch Notes -> https://docs.example.com/pinloom/launch#overview")
            && anchor.line == 1;
    }));
    QVERIFY(std::any_of(fileIt->anchors.cbegin(), fileIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: status.example.org -> https://status.example.org/zeroslack")
            && anchor.line == 2;
    }));
    QVERIFY(std::none_of(fileIt->anchors.cbegin(), fileIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target.contains(QStringLiteral("not-web"));
    }));

    auto launchIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/pinloom/launch#overview");
    });
    QVERIFY(launchIt != resources.cend());
    QCOMPARE(launchIt->title, QStringLiteral("Pinloom Launch Notes"));
    QVERIFY(launchIt->tags.contains(QStringLiteral("web")));
    QVERIFY(launchIt->tags.contains(QStringLiteral("web-link")));
    QVERIFY(launchIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(launchIt->aliases.contains(QStringLiteral("research")));
    QCOMPARE(launchIt->anchors.size(), 1);
    QCOMPARE(launchIt->anchors.first().type, AnchorType::UrlFragment);
    QCOMPARE(launchIt->anchors.first().target, QStringLiteral("overview"));

    const int launchResourceCount = std::count_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/pinloom/launch#overview");
    });
    QCOMPARE(launchResourceCount, 1);

    auto statusIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://status.example.org/zeroslack");
    });
    QVERIFY(statusIt != resources.cend());
    QCOMPARE(statusIt->title, QStringLiteral("status.example.org"));

    auto portalIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://portal.example.com/pinloom#home");
    });
    QVERIFY(portalIt != resources.cend());
    QCOMPARE(portalIt->title, QStringLiteral("Pinloom Portal"));
    QVERIFY(portalIt->tags.contains(QStringLiteral("text-link")));
    QVERIFY(!portalIt->tags.contains(QStringLiteral("web-link")));

    auto hostApiIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://api.example.com/pinloom#v1");
    });
    QVERIFY(hostApiIt != resources.cend());
    QCOMPARE(hostApiIt->title, QStringLiteral("Host API"));
    QVERIFY(hostApiIt->tags.contains(QStringLiteral("text-link")));
    QVERIFY(!hostApiIt->tags.contains(QStringLiteral("web-link")));

    auto opaqueFileIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("jump-targets.opaque");
    });
    QVERIFY(opaqueFileIt != resources.cend());
    auto opaqueJumpIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://jump.example.net/pinloom#opaque");
    });
    QVERIFY(opaqueJumpIt != resources.cend());
    QCOMPARE(opaqueJumpIt->title, QStringLiteral("Opaque Jump Target"));
    QVERIFY(std::any_of(opaqueFileIt->relations.cbegin(), opaqueFileIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == opaqueFileIt->id
            && relation.targetResourceId == opaqueJumpIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("text line 1: url: Opaque Jump Target -> https://jump.example.net/pinloom#opaque");
    }));

    QCOMPARE(fileIt->relations.size(), 4);
    QVERIFY(std::any_of(fileIt->relations.cbegin(), fileIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == fileIt->id
            && relation.targetResourceId == launchIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("text line 1: url: Pinloom Launch Notes -> https://docs.example.com/pinloom/launch#overview");
    }));
    QVERIFY(std::any_of(fileIt->relations.cbegin(), fileIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == fileIt->id
            && relation.targetResourceId == statusIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("text line 2: url: status.example.org -> https://status.example.org/zeroslack");
    }));
    QVERIFY(std::any_of(fileIt->relations.cbegin(), fileIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == fileIt->id
            && relation.targetResourceId == portalIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("text line 5: url: Pinloom Portal -> https://portal.example.com/pinloom#home");
    }));
    QVERIFY(std::any_of(fileIt->relations.cbegin(), fileIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == fileIt->id
            && relation.targetResourceId == hostApiIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("text line 6: url: Host API -> https://api.example.com/pinloom#v1");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> titleResults = repository.search(SearchQuery{QStringLiteral("Launch Notes")});
    QVERIFY(std::any_of(titleResults.cbegin(), titleResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinloom Launch Notes");
    }));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("overview")});
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("overview");
    }));

    const QList<ResourceRelation> fileRelations = repository.resourceRelations(fileIt->id);
    QCOMPARE(fileRelations.size(), 4);
    QVERIFY(std::any_of(fileRelations.cbegin(), fileRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == fileIt->id
            && relation.targetResourceId == launchIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("text line 1: url: Pinloom Launch Notes -> https://docs.example.com/pinloom/launch#overview");
    }));

    const QList<ResourceRelation> launchRelations = repository.resourceRelations(launchIt->id);
    QCOMPARE(launchRelations.size(), 1);
    QCOMPARE(launchRelations.first().sourceResourceId, fileIt->id);
    QCOMPARE(launchRelations.first().targetResourceId, launchIt->id);

    const QList<ResourceRelation> hostApiRelations = repository.resourceRelations(hostApiIt->id);
    QCOMPARE(hostApiRelations.size(), 1);
    QCOMPARE(hostApiRelations.first().sourceResourceId, fileIt->id);
    QCOMPARE(hostApiRelations.first().targetResourceId, hostApiIt->id);

    const QList<ResourceRelation> opaqueJumpRelations = repository.resourceRelations(opaqueJumpIt->id);
    QCOMPARE(opaqueJumpRelations.size(), 1);
    QCOMPARE(opaqueJumpRelations.first().sourceResourceId, opaqueFileIt->id);
    QCOMPARE(opaqueJumpRelations.first().targetResourceId, opaqueJumpIt->id);

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("Launch Notes")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("research.urls")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 1
            && result.matchedAnchor->target == QLatin1String("url: Pinloom Launch Notes -> https://docs.example.com/pinloom/launch#overview");
    }));

    const QList<SearchResult> hostApiResults = repository.search(SearchQuery{QStringLiteral("Host API")});
    QVERIFY(std::any_of(hostApiResults.cbegin(), hostApiResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Host API");
    }));
    QVERIFY(std::any_of(hostApiResults.cbegin(), hostApiResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("research.urls")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 6
            && result.matchedAnchor->target == QLatin1String("url: Host API -> https://api.example.com/pinloom#v1");
    }));
}

void DirectorySourceTest::extractsEmailMessageUrlResources()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/mail")));
    writeFile(dir.filePath(QStringLiteral("library/mail/handoff.eml")),
              QByteArray("From: Alice Example <alice@example.com>\r\n"
                         "To: Pinloom Team <pinloom@example.com>\r\n"
                         "Subject: ZeroSlack Dock\r\n"
                         " Handoff\r\n"
                         "Date: Sat, 27 Jun 2026 10:00:00 +0800\r\n"
                         "\r\n"
                         "Please review https://docs.example.com/pinloom/email#handoff before standup.\r\n"
                         "Dashboard: https://dash.example.org/zeroslack#dock\r\n"
                         "Duplicate https://docs.example.com/pinloom/email#handoff\r\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto emailIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("handoff.eml");
    });
    QVERIFY(emailIt != resources.cend());
    QVERIFY(emailIt->tags.contains(QStringLiteral("email")));
    QVERIFY(emailIt->aliases.contains(QStringLiteral("ZeroSlack Dock Handoff")));
    QVERIFY(emailIt->aliases.contains(QStringLiteral("Alice Example <alice@example.com>")));
    QVERIFY(emailIt->aliases.contains(QStringLiteral("Pinloom Team <pinloom@example.com>")));
    QVERIFY(emailIt->content.contains(QStringLiteral("before standup")));
    QVERIFY(std::any_of(emailIt->anchors.cbegin(), emailIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("email subject: ZeroSlack Dock Handoff")
            && anchor.line == 3;
    }));
    QVERIFY(std::any_of(emailIt->anchors.cbegin(), emailIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("email from: Alice Example <alice@example.com>")
            && anchor.line == 1;
    }));
    QVERIFY(std::any_of(emailIt->anchors.cbegin(), emailIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("email to: Pinloom Team <pinloom@example.com>")
            && anchor.line == 2;
    }));
    QVERIFY(std::any_of(emailIt->anchors.cbegin(), emailIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("email date: Sat, 27 Jun 2026 10:00:00 +0800")
            && anchor.line == 5;
    }));
    QVERIFY(std::any_of(emailIt->anchors.cbegin(), emailIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: Please review -> https://docs.example.com/pinloom/email#handoff")
            && anchor.line == 7;
    }));
    QVERIFY(std::any_of(emailIt->anchors.cbegin(), emailIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: Dashboard -> https://dash.example.org/zeroslack#dock")
            && anchor.line == 8;
    }));

    auto reviewIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Please review")
            && resource.location == QLatin1String("https://docs.example.com/pinloom/email#handoff");
    });
    QVERIFY(reviewIt != resources.cend());
    QVERIFY(reviewIt->tags.contains(QStringLiteral("email-link")));
    QVERIFY(reviewIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(reviewIt->aliases.contains(QStringLiteral("ZeroSlack Dock Handoff")));
    QVERIFY(reviewIt->aliases.contains(QStringLiteral("Alice Example <alice@example.com>")));
    QVERIFY(std::any_of(reviewIt->anchors.cbegin(), reviewIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("handoff");
    }));

    auto dashboardIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Dashboard")
            && resource.location == QLatin1String("https://dash.example.org/zeroslack#dock");
    });
    QVERIFY(dashboardIt != resources.cend());
    QVERIFY(dashboardIt->tags.contains(QStringLiteral("email-link")));
    QVERIFY(std::any_of(dashboardIt->anchors.cbegin(), dashboardIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("dock");
    }));

    const int reviewResourceCount = std::count_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/pinloom/email#handoff");
    });
    QCOMPARE(reviewResourceCount, 1);

    QCOMPARE(emailIt->relations.size(), 2);
    QVERIFY(std::any_of(emailIt->relations.cbegin(), emailIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == emailIt->id
            && relation.targetResourceId == reviewIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("email line 7: url: Please review -> https://docs.example.com/pinloom/email#handoff");
    }));
    QVERIFY(std::any_of(emailIt->relations.cbegin(), emailIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == emailIt->id
            && relation.targetResourceId == dashboardIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("email line 8: url: Dashboard -> https://dash.example.org/zeroslack#dock");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> subjectResults = repository.search(SearchQuery{QStringLiteral("ZeroSlack Dock Handoff")});
    QVERIFY(std::any_of(subjectResults.cbegin(), subjectResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("handoff.eml")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("email subject: ZeroSlack Dock Handoff");
    }));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("standup")});
    QVERIFY(std::any_of(contentResults.cbegin(), contentResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("handoff.eml")
            && result.matchedField == QLatin1String("content");
    }));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("dock")});
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Dashboard")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("dock");
    }));

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("Dashboard")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("handoff.eml")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 8
            && result.matchedAnchor->target == QLatin1String("url: Dashboard -> https://dash.example.org/zeroslack#dock");
    }));

    const QList<ResourceRelation> emailRelations = repository.resourceRelations(emailIt->id);
    QCOMPARE(emailRelations.size(), 2);
    QVERIFY(std::any_of(emailRelations.cbegin(), emailRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == emailIt->id
            && relation.targetResourceId == reviewIt->id
            && relation.label == QLatin1String("links-to");
    }));
    QVERIFY(std::any_of(emailRelations.cbegin(), emailRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == emailIt->id
            && relation.targetResourceId == dashboardIt->id
            && relation.label == QLatin1String("links-to");
    }));
}

void DirectorySourceTest::extractsIcalendarEventUrlResources()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/calendar")));
    writeFile(dir.filePath(QStringLiteral("library/calendar/pinloom.ics")),
              QByteArray("BEGIN:VCALENDAR\r\n"
                         "VERSION:2.0\r\n"
                         "BEGIN:VEVENT\r\n"
                         "SUMMARY:ZeroSlack Dock Planning\r\n"
                         "DTSTART:20260701T090000Z\r\n"
                         "DTEND:20260701T100000Z\r\n"
                         "LOCATION:Planning Room\r\n"
                         "URL:https://meet.example.com/pinloom#agenda\r\n"
                         "DESCRIPTION:Read https://docs.example.com/pinloom/calendar#notes before the call\r\n"
                         "END:VEVENT\r\n"
                         "BEGIN:VEVENT\r\n"
                         "SUMMARY:Offline Review\r\n"
                         "DTSTART;VALUE=DATE:20260702\r\n"
                         "LOCATION:Desk\r\n"
                         "END:VEVENT\r\n"
                         "END:VCALENDAR\r\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto calendarIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("pinloom.ics");
    });
    QVERIFY(calendarIt != resources.cend());
    QVERIFY(calendarIt->tags.contains(QStringLiteral("calendar")));
    QVERIFY(calendarIt->aliases.contains(QStringLiteral("ZeroSlack Dock Planning")));
    QVERIFY(calendarIt->aliases.contains(QStringLiteral("Offline Review")));

    auto hasLineAnchor = [](const Resource &resource, const QString &target, int line) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::FileLine
                && anchor.target == target
                && anchor.line == line;
        });
    };

    QVERIFY(hasLineAnchor(*calendarIt, QStringLiteral("calendar event: ZeroSlack Dock Planning"), 4));
    QVERIFY(hasLineAnchor(*calendarIt, QStringLiteral("calendar start: 2026-07-01 09:00:00 UTC"), 5));
    QVERIFY(hasLineAnchor(*calendarIt, QStringLiteral("calendar end: 2026-07-01 10:00:00 UTC"), 6));
    QVERIFY(hasLineAnchor(*calendarIt, QStringLiteral("calendar location: Planning Room"), 7));
    QVERIFY(hasLineAnchor(*calendarIt,
                          QStringLiteral("url: ZeroSlack Dock Planning -> https://meet.example.com/pinloom#agenda"),
                          8));
    QVERIFY(hasLineAnchor(*calendarIt,
                          QStringLiteral("url: ZeroSlack Dock Planning -> https://docs.example.com/pinloom/calendar#notes"),
                          9));
    QVERIFY(hasLineAnchor(*calendarIt, QStringLiteral("calendar event: Offline Review"), 12));
    QVERIFY(hasLineAnchor(*calendarIt, QStringLiteral("calendar start: 2026-07-02"), 13));

    auto meetingIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://meet.example.com/pinloom#agenda");
    });
    QVERIFY(meetingIt != resources.cend());
    QCOMPARE(meetingIt->title, QStringLiteral("ZeroSlack Dock Planning"));
    QVERIFY(meetingIt->tags.contains(QStringLiteral("web")));
    QVERIFY(meetingIt->tags.contains(QStringLiteral("calendar-link")));
    QVERIFY(meetingIt->aliases.contains(QStringLiteral("pinloom")));
    QVERIFY(meetingIt->aliases.contains(QStringLiteral("Planning Room")));
    QVERIFY(meetingIt->aliases.contains(QStringLiteral("2026-07-01 09:00:00 UTC")));
    QVERIFY(std::any_of(meetingIt->anchors.cbegin(), meetingIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("agenda");
    }));

    auto docsIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/pinloom/calendar#notes");
    });
    QVERIFY(docsIt != resources.cend());

    QCOMPARE(calendarIt->relations.size(), 2);
    QVERIFY(std::any_of(calendarIt->relations.cbegin(), calendarIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == calendarIt->id
            && relation.targetResourceId == meetingIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("calendar line 8: url: ZeroSlack Dock Planning -> https://meet.example.com/pinloom#agenda");
    }));
    QVERIFY(std::any_of(calendarIt->relations.cbegin(), calendarIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == calendarIt->id
            && relation.targetResourceId == docsIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("calendar line 9: url: ZeroSlack Dock Planning -> https://docs.example.com/pinloom/calendar#notes");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("agenda")});
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("ZeroSlack Dock Planning")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("agenda");
    }));

    const QList<SearchResult> offlineResults = repository.search(SearchQuery{QStringLiteral("Offline Review")});
    QVERIFY(std::any_of(offlineResults.cbegin(), offlineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("pinloom.ics")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("calendar event: Offline Review");
    }));

    const QList<ResourceRelation> calendarRelations = repository.resourceRelations(calendarIt->id);
    QCOMPARE(calendarRelations.size(), 2);
    QVERIFY(std::any_of(calendarRelations.cbegin(), calendarRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.targetResourceId == meetingIt->id
            && relation.note == QLatin1String("calendar line 8: url: ZeroSlack Dock Planning -> https://meet.example.com/pinloom#agenda");
    }));
}

void DirectorySourceTest::extractsJsonUrlResources()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/data")));
    writeFile(dir.filePath(QStringLiteral("library/data/references.json")),
              QByteArray("{\n"
                         "  \"references\": [\n"
                         "    {\n"
                         "      \"title\": \"Pinloom Host Guide\",\n"
                         "      \"url\": \"https://docs.example.com/pinloom/host#embed\"\n"
                         "    },\n"
                         "    {\n"
                         "      \"label\": \"Status Dashboard\",\n"
                         "      \"href\": \"https://status.example.com/zeroslack\"\n"
                         "    },\n"
                         "    {\n"
                         "      \"title\": \"Duplicate Host Guide\",\n"
                         "      \"url\": \"https://docs.example.com/pinloom/host#embed\"\n"
                         "    }\n"
                         "  ],\n"
                         "  \"local\": \"file://ignored\"\n"
                         "}\n"));
    writeFile(dir.filePath(QStringLiteral("library/data/events.jsonl")),
              QByteArray("{\"title\":\"Pinloom Release Feed\",\"link\":\"https://docs.example.com/pinloom/feed#latest\"}\n"
                         "{\"message\":\"raw https://raw.example.com/incident path\"}\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto referencesIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("references.json");
    });
    QVERIFY(referencesIt != resources.cend());
    QVERIFY(std::any_of(referencesIt->anchors.cbegin(), referencesIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: Pinloom Host Guide -> https://docs.example.com/pinloom/host#embed")
            && anchor.line == 5;
    }));
    QVERIFY(std::any_of(referencesIt->anchors.cbegin(), referencesIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: Status Dashboard -> https://status.example.com/zeroslack")
            && anchor.line == 9;
    }));
    QVERIFY(std::none_of(referencesIt->anchors.cbegin(), referencesIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target.contains(QStringLiteral("file://ignored"));
    }));

    auto hostIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/pinloom/host#embed");
    });
    QVERIFY(hostIt != resources.cend());
    QCOMPARE(hostIt->title, QStringLiteral("Pinloom Host Guide"));
    QVERIFY(hostIt->tags.contains(QStringLiteral("web")));
    QVERIFY(hostIt->tags.contains(QStringLiteral("json-link")));
    QVERIFY(hostIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(hostIt->aliases.contains(QStringLiteral("references")));
    QVERIFY(hostIt->aliases.contains(QStringLiteral("references[0].url")));
    QCOMPARE(hostIt->anchors.size(), 1);
    QCOMPARE(hostIt->anchors.first().type, AnchorType::UrlFragment);
    QCOMPARE(hostIt->anchors.first().target, QStringLiteral("embed"));

    const int hostResourceCount = std::count_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/pinloom/host#embed");
    });
    QCOMPARE(hostResourceCount, 1);

    auto feedIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/pinloom/feed#latest");
    });
    QVERIFY(feedIt != resources.cend());
    QCOMPARE(feedIt->title, QStringLiteral("Pinloom Release Feed"));
    QVERIFY(feedIt->aliases.contains(QStringLiteral("events")));
    QVERIFY(feedIt->aliases.contains(QStringLiteral("line1.link")));

    QCOMPARE(referencesIt->relations.size(), 2);
    QVERIFY(std::any_of(referencesIt->relations.cbegin(), referencesIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == referencesIt->id
            && relation.targetResourceId == hostIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("json line 5 path references[0].url: url: Pinloom Host Guide -> https://docs.example.com/pinloom/host#embed");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> titleResults = repository.search(SearchQuery{QStringLiteral("Host Guide")});
    QVERIFY(std::any_of(titleResults.cbegin(), titleResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinloom Host Guide");
    }));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("latest")});
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinloom Release Feed")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("latest");
    }));

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("Pinloom Host Guide")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("references.json")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 5
            && result.matchedAnchor->target == QLatin1String("url: Pinloom Host Guide -> https://docs.example.com/pinloom/host#embed");
    }));

    const QList<ResourceRelation> hostRelations = repository.resourceRelations(hostIt->id);
    QCOMPARE(hostRelations.size(), 1);
    QCOMPARE(hostRelations.first().sourceResourceId, referencesIt->id);
    QCOMPARE(hostRelations.first().targetResourceId, hostIt->id);
}

void DirectorySourceTest::extractsHarEntryLinks()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/captures")));
    writeFile(dir.filePath(QStringLiteral("library/captures/session.har")),
              QByteArray("{\n"
                         "  \"log\": {\n"
                         "    \"pages\": [\n"
                         "      {\n"
                         "        \"startedDateTime\": \"2026-06-25T10:15:00.000Z\",\n"
                         "        \"id\": \"page_1\",\n"
                         "        \"title\": \"Pinloom HAR Capture\"\n"
                         "      }\n"
                         "    ],\n"
                         "    \"entries\": [\n"
                         "      {\n"
                         "        \"pageref\": \"page_1\",\n"
                         "        \"startedDateTime\": \"2026-06-25T10:15:01.000Z\",\n"
                         "        \"request\": {\n"
                         "          \"method\": \"GET\",\n"
                         "          \"url\": \"https://docs.example.com/pinloom/har#entry\"\n"
                         "        },\n"
                         "        \"response\": {\n"
                         "          \"status\": 200,\n"
                         "          \"content\": {\n"
                         "            \"mimeType\": \"text/html\"\n"
                         "          }\n"
                         "        }\n"
                         "      },\n"
                         "      {\n"
                         "        \"startedDateTime\": \"2026-06-25T10:15:02.000Z\",\n"
                         "        \"request\": {\n"
                         "          \"method\": \"POST\",\n"
                         "          \"url\": \"https://api.example.com/pinloom/events\"\n"
                         "        },\n"
                         "        \"response\": {\n"
                         "          \"status\": 202\n"
                         "        }\n"
                         "      },\n"
                         "      {\n"
                         "        \"request\": {\n"
                         "          \"method\": \"GET\",\n"
                         "          \"url\": \"chrome://settings\"\n"
                         "        }\n"
                         "      }\n"
                         "    ]\n"
                         "  }\n"
                         "}\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto harIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("session.har");
    });
    QVERIFY(harIt != resources.cend());

    auto pageIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Pinloom HAR Capture")
            && resource.location == QLatin1String("https://docs.example.com/pinloom/har#entry");
    });
    QVERIFY(pageIt != resources.cend());
    QVERIFY(pageIt->tags.contains(QStringLiteral("web")));
    QVERIFY(pageIt->tags.contains(QStringLiteral("har")));
    QVERIFY(pageIt->tags.contains(QStringLiteral("web-archive")));
    QVERIFY(pageIt->tags.contains(QStringLiteral("http-get")));
    QVERIFY(pageIt->tags.contains(QStringLiteral("http-200")));
    QVERIFY(pageIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(pageIt->aliases.contains(QStringLiteral("session")));
    QVERIFY(pageIt->aliases.contains(QStringLiteral("page_1")));
    QVERIFY(pageIt->aliases.contains(QStringLiteral("status 200")));
    QVERIFY(pageIt->aliases.contains(QStringLiteral("text/html")));
    QVERIFY(std::any_of(pageIt->anchors.cbegin(), pageIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("entry");
    }));

    auto apiIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("POST /pinloom/events")
            && resource.location == QLatin1String("https://api.example.com/pinloom/events");
    });
    QVERIFY(apiIt != resources.cend());
    QVERIFY(apiIt->tags.contains(QStringLiteral("http-post")));
    QVERIFY(apiIt->tags.contains(QStringLiteral("http-202")));
    QVERIFY(apiIt->aliases.contains(QStringLiteral("status 202")));

    QVERIFY(std::none_of(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location.contains(QStringLiteral("chrome://settings"));
    }));

    QVERIFY(std::any_of(harIt->anchors.cbegin(), harIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.line == 16
            && anchor.target == QLatin1String("url: Pinloom HAR Capture -> https://docs.example.com/pinloom/har#entry");
    }));

    QCOMPARE(harIt->relations.size(), 2);
    QVERIFY(std::any_of(harIt->relations.cbegin(), harIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == harIt->id
            && relation.targetResourceId == pageIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("har line 16 GET status 200: url: Pinloom HAR Capture -> https://docs.example.com/pinloom/har#entry");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> titleResults = repository.search(SearchQuery{QStringLiteral("Pinloom HAR Capture")});
    QVERIFY(std::any_of(titleResults.cbegin(), titleResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinloom HAR Capture");
    }));

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("Pinloom HAR Capture")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("session.har")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 16;
    }));
}

void DirectorySourceTest::extractsWarcResponseLinks()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/captures")));
    writeFile(dir.filePath(QStringLiteral("library/captures/session.warc")),
              QByteArray("WARC/1.0\r\n"
                         "WARC-Type: response\r\n"
                         "WARC-Target-URI: https://docs.example.com/pinloom/warc#snapshot\r\n"
                         "Content-Type: application/http; msgtype=response\r\n"
                         "\r\n"
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/html; charset=UTF-8\r\n"
                         "\r\n"
                         "<!doctype html>\r\n"
                         "<html><head>\r\n"
                         "<title>Pinloom WARC Capture</title>\r\n"
                         "<link rel=\"canonical\" href=\"https://docs.example.com/pinloom/warc\">\r\n"
                         "</head><body>\r\n"
                         "<h1 id=\"snapshot\">Captured Snapshot</h1>\r\n"
                         "<p>Archived WARC response for source refinement.</p>\r\n"
                         "</body></html>\r\n"
                         "\r\n"
                         "WARC/1.0\r\n"
                         "WARC-Type: metadata\r\n"
                         "WARC-Target-URI: https://ignored.example.com/meta\r\n"
                         "Content-Type: text/plain\r\n"
                         "\r\n"
                         "metadata only\r\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto warcIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("session.warc");
    });
    QVERIFY(warcIt != resources.cend());
    QVERIFY(warcIt->tags.contains(QStringLiteral("warc")));
    QVERIFY(warcIt->tags.contains(QStringLiteral("web-archive")));

    auto pageIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Pinloom WARC Capture")
            && resource.location == QLatin1String("https://docs.example.com/pinloom/warc#snapshot");
    });
    QVERIFY(pageIt != resources.cend());
    QVERIFY(pageIt->tags.contains(QStringLiteral("web")));
    QVERIFY(pageIt->tags.contains(QStringLiteral("warc")));
    QVERIFY(pageIt->tags.contains(QStringLiteral("web-archive")));
    QVERIFY(pageIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(pageIt->aliases.contains(QStringLiteral("session")));
    QVERIFY(pageIt->aliases.contains(QStringLiteral("https://docs.example.com/pinloom/warc")));
    QVERIFY(pageIt->aliases.contains(QStringLiteral("Captured Snapshot")));
    QVERIFY(pageIt->content.contains(QStringLiteral("Archived WARC response")));
    QVERIFY(std::any_of(pageIt->anchors.cbegin(), pageIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("snapshot");
    }));
    QVERIFY(std::any_of(warcIt->anchors.cbegin(), warcIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.line == 1
            && anchor.target == QLatin1String("url: Pinloom WARC Capture -> https://docs.example.com/pinloom/warc#snapshot");
    }));

    QCOMPARE(warcIt->relations.size(), 1);
    QCOMPARE(warcIt->relations.first().sourceResourceId, warcIt->id);
    QCOMPARE(warcIt->relations.first().targetResourceId, pageIt->id);
    QCOMPARE(warcIt->relations.first().label, QStringLiteral("links-to"));
    QCOMPARE(warcIt->relations.first().note,
             QStringLiteral("warc line 1 record 1: url: Pinloom WARC Capture -> https://docs.example.com/pinloom/warc#snapshot"));

    QVERIFY(std::none_of(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location.contains(QStringLiteral("ignored.example.com"));
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<ResourceRelation> relations = repository.resourceRelations(warcIt->id);
    QCOMPARE(relations.size(), 1);
    QCOMPARE(relations.first().targetResourceId, pageIt->id);

    const QList<SearchResult> sourceAnchorResults = repository.search(SearchQuery{QStringLiteral("Pinloom WARC Capture")});
    QVERIFY(std::any_of(sourceAnchorResults.cbegin(), sourceAnchorResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("session.warc")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 1
            && result.matchedAnchor->target
                == QLatin1String("url: Pinloom WARC Capture -> https://docs.example.com/pinloom/warc#snapshot");
    }));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("Archived WARC response")});
    QVERIFY(std::any_of(contentResults.cbegin(), contentResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinloom WARC Capture")
            && result.matchedField == QLatin1String("content");
    }));

    const QList<SearchResult> anchorResults = repository.search(SearchQuery{QStringLiteral("snapshot")});
    QVERIFY(std::any_of(anchorResults.cbegin(), anchorResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinloom WARC Capture")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("snapshot");
    }));
}

void DirectorySourceTest::extractsConfigTextUrlBeacons()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/config")));
    writeFile(dir.filePath(QStringLiteral("library/config/workspace.yml")),
              QByteArray("pinloom:\n"
                         "  docs_url: https://docs.example.com/pinloom/workspace#roots\n"
                         "  status_links:\n"
                         "    - https://status.example.com/zeroslack\n"
                         "  local: file://ignored\n"));
    writeFile(dir.filePath(QStringLiteral("library/config/settings.toml")),
              QByteArray("[links]\n"
                         "handoff = \"https://docs.example.com/pinloom/handoff#dock\"\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto workspaceIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("workspace.yml");
    });
    QVERIFY(workspaceIt != resources.cend());
    QVERIFY(std::any_of(workspaceIt->anchors.cbegin(), workspaceIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: docs_url -> https://docs.example.com/pinloom/workspace#roots")
            && anchor.line == 2;
    }));
    QVERIFY(std::any_of(workspaceIt->anchors.cbegin(), workspaceIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: status.example.com -> https://status.example.com/zeroslack")
            && anchor.line == 4;
    }));
    QVERIFY(std::none_of(workspaceIt->anchors.cbegin(), workspaceIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target.contains(QStringLiteral("file://ignored"));
    }));

    auto workspaceDocsIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/pinloom/workspace#roots");
    });
    QVERIFY(workspaceDocsIt != resources.cend());
    QCOMPARE(workspaceDocsIt->title, QStringLiteral("docs_url"));
    QVERIFY(workspaceDocsIt->tags.contains(QStringLiteral("web")));
    QVERIFY(workspaceDocsIt->tags.contains(QStringLiteral("web-link")));
    QVERIFY(workspaceDocsIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(workspaceDocsIt->aliases.contains(QStringLiteral("workspace")));
    QCOMPARE(workspaceDocsIt->anchors.size(), 1);
    QCOMPARE(workspaceDocsIt->anchors.first().type, AnchorType::UrlFragment);
    QCOMPARE(workspaceDocsIt->anchors.first().target, QStringLiteral("roots"));

    auto settingsIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("settings.toml");
    });
    QVERIFY(settingsIt != resources.cend());
    QVERIFY(std::any_of(settingsIt->anchors.cbegin(), settingsIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: handoff -> https://docs.example.com/pinloom/handoff#dock")
            && anchor.line == 2;
    }));

    auto handoffIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/pinloom/handoff#dock");
    });
    QVERIFY(handoffIt != resources.cend());
    QCOMPARE(handoffIt->title, QStringLiteral("handoff"));
    QVERIFY(handoffIt->aliases.contains(QStringLiteral("settings")));
    QCOMPARE(handoffIt->anchors.size(), 1);
    QCOMPARE(handoffIt->anchors.first().target, QStringLiteral("dock"));

    QCOMPARE(workspaceIt->relations.size(), 2);
    QVERIFY(std::any_of(workspaceIt->relations.cbegin(), workspaceIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == workspaceIt->id
            && relation.targetResourceId == workspaceDocsIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("text line 2: url: docs_url -> https://docs.example.com/pinloom/workspace#roots");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("roots")});
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("docs_url")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("roots");
    }));

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("handoff")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("settings.toml")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 2
            && result.matchedAnchor->target == QLatin1String("url: handoff -> https://docs.example.com/pinloom/handoff#dock");
    }));

    const QList<ResourceRelation> handoffRelations = repository.resourceRelations(handoffIt->id);
    QCOMPARE(handoffRelations.size(), 1);
    QCOMPARE(handoffRelations.first().sourceResourceId, settingsIt->id);
    QCOMPARE(handoffRelations.first().targetResourceId, handoffIt->id);
}

void DirectorySourceTest::extractsTabularUrlResources()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/tables")));
    writeFile(dir.filePath(QStringLiteral("library/tables/research.csv")),
              QByteArray("Title,URL,Tags\n"
                         "\"Pinloom Host API\",https://docs.example.com/pinloom/host#dock,\"zeroslack;host\"\n"
                         "FPGA Notes,https://fpga.example.com/notes,fpga\n"
                         "Ignored,not-a-link,skip\n"
                         "Duplicate,https://docs.example.com/pinloom/host#dock,duplicate\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto fileIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("research.csv");
    });
    QVERIFY(fileIt != resources.cend());
    QVERIFY(std::any_of(fileIt->anchors.cbegin(), fileIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("column: URL")
            && anchor.line == 1;
    }));
    QVERIFY(std::any_of(fileIt->anchors.cbegin(), fileIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: Pinloom Host API -> https://docs.example.com/pinloom/host#dock")
            && anchor.line == 2;
    }));

    auto hostIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/pinloom/host#dock");
    });
    QVERIFY(hostIt != resources.cend());
    QCOMPARE(hostIt->title, QStringLiteral("Pinloom Host API"));
    QVERIFY(hostIt->tags.contains(QStringLiteral("web")));
    QVERIFY(hostIt->tags.contains(QStringLiteral("tabular-link")));
    QVERIFY(hostIt->tags.contains(QStringLiteral("zeroslack")));
    QVERIFY(hostIt->tags.contains(QStringLiteral("host")));
    QVERIFY(hostIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(hostIt->aliases.contains(QStringLiteral("research")));
    QVERIFY(hostIt->aliases.contains(QStringLiteral("zeroslack")));
    QVERIFY(hostIt->aliases.contains(QStringLiteral("host")));
    QCOMPARE(hostIt->anchors.size(), 1);
    QCOMPARE(hostIt->anchors.first().type, AnchorType::UrlFragment);
    QCOMPARE(hostIt->anchors.first().target, QStringLiteral("dock"));
    QCOMPARE(fileIt->relations.size(), 2);
    QVERIFY(std::any_of(fileIt->relations.cbegin(), fileIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == fileIt->id
            && relation.targetResourceId == hostIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("table row 2: url: Pinloom Host API -> https://docs.example.com/pinloom/host#dock");
    }));

    const int duplicateCount = std::count_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/pinloom/host#dock");
    });
    QCOMPARE(duplicateCount, 1);

    auto fpgaIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://fpga.example.com/notes");
    });
    QVERIFY(fpgaIt != resources.cend());
    QCOMPARE(fpgaIt->title, QStringLiteral("FPGA Notes"));
    QVERIFY(fpgaIt->tags.contains(QStringLiteral("fpga")));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> titleResults = repository.search(SearchQuery{QStringLiteral("Pinloom Host API")});
    QVERIFY(std::any_of(titleResults.cbegin(), titleResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinloom Host API");
    }));

    const QList<SearchResult> tagResults = repository.search(SearchQuery{QStringLiteral("zeroslack")});
    QVERIFY(std::any_of(tagResults.cbegin(), tagResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinloom Host API");
    }));

    const QList<SearchResult> hostResults = repository.search(SearchQuery{QStringLiteral("fpga.example.com")});
    QVERIFY(std::any_of(hostResults.cbegin(), hostResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("FPGA Notes");
    }));

    const QList<ResourceRelation> fileRelations = repository.resourceRelations(fileIt->id);
    QCOMPARE(fileRelations.size(), 2);
    QVERIFY(std::any_of(fileRelations.cbegin(), fileRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == fileIt->id
            && relation.targetResourceId == hostIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("table row 2: url: Pinloom Host API -> https://docs.example.com/pinloom/host#dock");
    }));

    const QList<ResourceRelation> urlRelations = repository.resourceRelations(hostIt->id);
    QCOMPARE(urlRelations.size(), 1);
    QCOMPARE(urlRelations.first().sourceResourceId, fileIt->id);
    QCOMPARE(urlRelations.first().targetResourceId, hostIt->id);

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("Pinloom Host API")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("research.csv")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 2
            && result.matchedAnchor->target == QLatin1String("url: Pinloom Host API -> https://docs.example.com/pinloom/host#dock");
    }));
}

void DirectorySourceTest::extractsHtmlPageContent()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/pages")));
    writeFile(dir.filePath(QStringLiteral("library/pages/guide.html")),
              QByteArray("<!doctype html>\n"
                         "<html><head>\n"
                         "<title>Pinloom Web Guide</title>\n"
                         "<link rel=\"canonical\" href=\"https://docs.example.com/pinloom/web-guide\">\n"
                         "<style>.hidden { display: none; }</style>\n"
                         "<script>const ignored = 'secret';</script>\n"
                         "</head><body>\n"
                         "<h1 id=\"install\">Install &amp; Launch</h1>\n"
                         "<p>This page explains browser launch routing and saved web references.</p>\n"
                         "<a href=\"https://docs.example.com/pinloom/launch#browser\">Browser Launch</a>\n"
                         "</body></html>\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto htmlIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url && resource.title == QLatin1String("Pinloom Web Guide");
    });
    QVERIFY(htmlIt != resources.cend());
    QVERIFY(htmlIt->location.endsWith(QStringLiteral("guide.html")));
    QVERIFY(htmlIt->tags.contains(QStringLiteral("web")));
    QVERIFY(htmlIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(htmlIt->aliases.contains(QStringLiteral("https://docs.example.com/pinloom/web-guide")));
    QVERIFY(htmlIt->aliases.contains(QStringLiteral("Install & Launch")));
    QVERIFY(htmlIt->content.contains(QStringLiteral("browser launch routing")));
    QVERIFY(!htmlIt->content.contains(QStringLiteral("secret")));
    QVERIFY(std::any_of(htmlIt->anchors.cbegin(), htmlIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("install");
    }));

    auto launchIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Browser Launch")
            && resource.location == QLatin1String("https://docs.example.com/pinloom/launch#browser");
    });
    QVERIFY(launchIt != resources.cend());
    QVERIFY(launchIt->tags.contains(QStringLiteral("html-link")));
    QVERIFY(std::any_of(launchIt->anchors.cbegin(), launchIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("browser");
    }));
    QVERIFY(std::any_of(htmlIt->anchors.cbegin(), htmlIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: Browser Launch -> https://docs.example.com/pinloom/launch#browser")
            && anchor.line == 10;
    }));
    QVERIFY(std::any_of(htmlIt->relations.cbegin(), htmlIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == htmlIt->id
            && relation.targetResourceId == launchIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("html link: url: Browser Launch -> https://docs.example.com/pinloom/launch#browser");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("browser launch")});
    QVERIFY(std::any_of(contentResults.cbegin(), contentResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinloom Web Guide")
            && result.matchedField == QLatin1String("content");
    }));

    const QList<SearchResult> anchorResults = repository.search(SearchQuery{QStringLiteral("install")});
    QVERIFY(!anchorResults.isEmpty());
    QVERIFY(std::any_of(anchorResults.cbegin(), anchorResults.cend(), [](const SearchResult &result) {
        return result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("install");
    }));

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("Browser Launch")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinloom Web Guide")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 10
            && result.matchedAnchor->target == QLatin1String("url: Browser Launch -> https://docs.example.com/pinloom/launch#browser");
    }));

    const QList<ResourceRelation> htmlRelations = repository.resourceRelations(htmlIt->id);
    QVERIFY(std::any_of(htmlRelations.cbegin(), htmlRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == htmlIt->id
            && relation.targetResourceId == launchIt->id
            && relation.label == QLatin1String("links-to");
    }));
}

void DirectorySourceTest::extractsMhtmlPageContent()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/pages")));
    writeFile(dir.filePath(QStringLiteral("library/pages/archive.mhtml")),
              QByteArray("MIME-Version: 1.0\r\n"
                         "Content-Type: multipart/related; boundary=\"----=_PinloomBoundary\"\r\n"
                         "\r\n"
                         "------=_PinloomBoundary\r\n"
                         "Content-Type: text/html; charset=\"utf-8\"\r\n"
                         "Content-Transfer-Encoding: quoted-printable\r\n"
                         "Content-Location: https://docs.example.com/archive\r\n"
                         "\r\n"
                         "<!doctype html>\r\n"
                         "<html><head>\r\n"
                         "<title>Pinloom Web Archive</title>\r\n"
                         "<link rel=3D\"canonical\" href=3D\"https://docs.example.com/pinloom/archive\">\r\n"
                         "</head><body>\r\n"
                         "<h2 id=3D\"snapshot\">Saved Snapshot</h2>\r\n"
                         "<p>Archived launch reference for ZeroSlack embedding.</p>\r\n"
                         "<a href=3D\"https://docs.example.com/pinloom/host#dock\">Host Playbook</a>\r\n"
                         "</body></html>\r\n"
                         "------=_PinloomBoundary--\r\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto archiveIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url && resource.title == QLatin1String("Pinloom Web Archive");
    });
    QVERIFY(archiveIt != resources.cend());
    QVERIFY(archiveIt->location.endsWith(QStringLiteral("archive.mhtml")));
    QVERIFY(archiveIt->tags.contains(QStringLiteral("web")));
    QVERIFY(archiveIt->tags.contains(QStringLiteral("web-archive")));
    QVERIFY(archiveIt->aliases.contains(QStringLiteral("archive")));
    QVERIFY(archiveIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(archiveIt->aliases.contains(QStringLiteral("https://docs.example.com/pinloom/archive")));
    QVERIFY(archiveIt->aliases.contains(QStringLiteral("Saved Snapshot")));
    QVERIFY(archiveIt->content.contains(QStringLiteral("Archived launch reference")));
    QVERIFY(std::any_of(archiveIt->anchors.cbegin(), archiveIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("snapshot");
    }));

    auto playbookIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Host Playbook")
            && resource.location == QLatin1String("https://docs.example.com/pinloom/host#dock");
    });
    QVERIFY(playbookIt != resources.cend());
    QVERIFY(playbookIt->tags.contains(QStringLiteral("html-link")));
    QVERIFY(playbookIt->tags.contains(QStringLiteral("web-archive-link")));
    QVERIFY(playbookIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(std::any_of(playbookIt->anchors.cbegin(), playbookIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("dock");
    }));
    QVERIFY(std::any_of(archiveIt->relations.cbegin(), archiveIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == archiveIt->id
            && relation.targetResourceId == playbookIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("html link: url: Host Playbook -> https://docs.example.com/pinloom/host#dock");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("Archived launch reference")});
    QVERIFY(std::any_of(contentResults.cbegin(), contentResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinloom Web Archive")
            && result.matchedField == QLatin1String("content");
    }));

    const QList<ResourceRelation> relations = repository.resourceRelations(archiveIt->id);
    QVERIFY(std::any_of(relations.cbegin(), relations.cend(), [&](const ResourceRelation &relation) {
        return relation.targetResourceId == playbookIt->id
            && relation.label == QLatin1String("links-to");
    }));

    const QList<SearchResult> linkResults = repository.search(SearchQuery{QStringLiteral("Host Playbook")});
    QVERIFY(std::any_of(linkResults.cbegin(), linkResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Host Playbook");
    }));
}

void DirectorySourceTest::extractsBookmarkExportLinks()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/pages")));
    writeFile(dir.filePath(QStringLiteral("library/pages/bookmarks.html")),
              QByteArray("<!DOCTYPE NETSCAPE-Bookmark-file-1>\n"
                         "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n"
                         "<TITLE>Bookmarks</TITLE>\n"
                         "<H1>Bookmarks</H1>\n"
                         "<DL><p>\n"
                         "<DT><A HREF=\"https://fpga.example.com/handbook#timing\">FPGA Handbook</A>\n"
                         "<DT><A HREF=\"https://docs.example.com/pinloom/setup#install\">Pinloom Setup</A>\n"
                         "</DL><p>\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto htmlIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Bookmarks")
            && resource.location.endsWith(QStringLiteral("bookmarks.html"));
    });
    QVERIFY(htmlIt != resources.cend());
    QVERIFY(htmlIt->tags.contains(QStringLiteral("bookmark")));
    QVERIFY(std::any_of(htmlIt->anchors.cbegin(), htmlIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: FPGA Handbook -> https://fpga.example.com/handbook#timing")
            && anchor.line == 6;
    }));
    QVERIFY(std::any_of(htmlIt->anchors.cbegin(), htmlIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: Pinloom Setup -> https://docs.example.com/pinloom/setup#install")
            && anchor.line == 7;
    }));

    auto fpgaIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("FPGA Handbook")
            && resource.location == QLatin1String("https://fpga.example.com/handbook#timing");
    });
    QVERIFY(fpgaIt != resources.cend());
    QVERIFY(fpgaIt->tags.contains(QStringLiteral("web")));
    QVERIFY(fpgaIt->tags.contains(QStringLiteral("bookmark")));
    QVERIFY(fpgaIt->aliases.contains(QStringLiteral("fpga.example.com")));
    QVERIFY(std::any_of(fpgaIt->anchors.cbegin(), fpgaIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("timing");
    }));

    auto setupIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Pinloom Setup")
            && resource.location == QLatin1String("https://docs.example.com/pinloom/setup#install");
    });
    QVERIFY(setupIt != resources.cend());

    QCOMPARE(htmlIt->relations.size(), 2);
    QVERIFY(std::any_of(htmlIt->relations.cbegin(), htmlIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == htmlIt->id
            && relation.targetResourceId == fpgaIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("bookmark line 6: url: FPGA Handbook -> https://fpga.example.com/handbook#timing");
    }));
    QVERIFY(std::any_of(htmlIt->relations.cbegin(), htmlIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == htmlIt->id
            && relation.targetResourceId == setupIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("bookmark line 7: url: Pinloom Setup -> https://docs.example.com/pinloom/setup#install");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> hostResults = repository.search(SearchQuery{QStringLiteral("fpga.example.com")});
    QVERIFY(std::any_of(hostResults.cbegin(), hostResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("FPGA Handbook");
    }));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("timing")});
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("FPGA Handbook")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("timing");
    }));

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("Pinloom Setup")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Bookmarks")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 7
            && result.matchedAnchor->target == QLatin1String("url: Pinloom Setup -> https://docs.example.com/pinloom/setup#install");
    }));

    const QList<ResourceRelation> bookmarkRelations = repository.resourceRelations(htmlIt->id);
    QCOMPARE(bookmarkRelations.size(), 2);
    QVERIFY(std::any_of(bookmarkRelations.cbegin(), bookmarkRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == htmlIt->id
            && relation.targetResourceId == fpgaIt->id
            && relation.label == QLatin1String("links-to");
    }));
    QVERIFY(std::any_of(bookmarkRelations.cbegin(), bookmarkRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == htmlIt->id
            && relation.targetResourceId == setupIt->id
            && relation.label == QLatin1String("links-to");
    }));
}

void DirectorySourceTest::extractsBrowserBookmarkJsonLinks()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/profile")));
    writeFile(dir.filePath(QStringLiteral("library/profile/Bookmarks")),
              QByteArray("{\n"
                         "  \"version\": 1,\n"
                         "  \"roots\": {\n"
                         "    \"bookmark_bar\": {\n"
                         "      \"type\": \"folder\",\n"
                         "      \"name\": \"Bookmarks Bar\",\n"
                         "      \"children\": [\n"
                         "        {\"type\": \"folder\", \"name\": \"FPGA\", \"children\": [\n"
                         "          {\"type\": \"url\", \"name\": \"Timing Closure\", \"url\": \"https://fpga.example.com/timing#slack\"}\n"
                         "        ]}\n"
                         "      ]\n"
                         "    },\n"
                         "    \"other\": {\n"
                         "      \"type\": \"folder\",\n"
                         "      \"name\": \"Other Bookmarks\",\n"
                         "      \"children\": [\n"
                         "        {\"type\": \"url\", \"name\": \"Pinloom Host API\", \"url\": \"https://docs.example.com/pinloom/host#dock\"}\n"
                         "      ]\n"
                         "    }\n"
                         "  }\n"
                         "}\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto bookmarksIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("Bookmarks");
    });
    QVERIFY(bookmarksIt != resources.cend());
    QVERIFY(bookmarksIt->tags.contains(QStringLiteral("bookmark")));
    QVERIFY(bookmarksIt->tags.contains(QStringLiteral("browser-bookmark")));
    QVERIFY(std::any_of(bookmarksIt->anchors.cbegin(), bookmarksIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: Timing Closure -> https://fpga.example.com/timing#slack")
            && anchor.line == 9;
    }));
    QVERIFY(std::any_of(bookmarksIt->anchors.cbegin(), bookmarksIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: Pinloom Host API -> https://docs.example.com/pinloom/host#dock")
            && anchor.line == 17;
    }));

    auto timingIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Timing Closure")
            && resource.location == QLatin1String("https://fpga.example.com/timing#slack");
    });
    QVERIFY(timingIt != resources.cend());
    QVERIFY(timingIt->tags.contains(QStringLiteral("bookmark")));
    QVERIFY(timingIt->tags.contains(QStringLiteral("browser-bookmark")));
    QVERIFY(timingIt->aliases.contains(QStringLiteral("fpga.example.com")));
    QVERIFY(timingIt->aliases.contains(QStringLiteral("FPGA")));
    QVERIFY(timingIt->aliases.contains(QStringLiteral("Bookmarks Bar / FPGA")));
    QVERIFY(std::any_of(timingIt->anchors.cbegin(), timingIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("slack");
    }));

    auto hostIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Pinloom Host API")
            && resource.location == QLatin1String("https://docs.example.com/pinloom/host#dock");
    });
    QVERIFY(hostIt != resources.cend());

    QCOMPARE(bookmarksIt->relations.size(), 2);
    QVERIFY(std::any_of(bookmarksIt->relations.cbegin(), bookmarksIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == bookmarksIt->id
            && relation.targetResourceId == timingIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("browser bookmark line 9: url: Timing Closure -> https://fpga.example.com/timing#slack");
    }));
    QVERIFY(std::any_of(bookmarksIt->relations.cbegin(), bookmarksIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == bookmarksIt->id
            && relation.targetResourceId == hostIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("browser bookmark line 17: url: Pinloom Host API -> https://docs.example.com/pinloom/host#dock");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> folderResults = repository.search(SearchQuery{QStringLiteral("FPGA")});
    QVERIFY(std::any_of(folderResults.cbegin(), folderResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Timing Closure");
    }));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("dock")});
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("Pinloom Host API")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("dock");
    }));

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("Pinloom Host API")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("Bookmarks")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 17
            && result.matchedAnchor->target == QLatin1String("url: Pinloom Host API -> https://docs.example.com/pinloom/host#dock");
    }));

    const QList<ResourceRelation> bookmarkRelations = repository.resourceRelations(bookmarksIt->id);
    QCOMPARE(bookmarkRelations.size(), 2);
    QVERIFY(std::any_of(bookmarkRelations.cbegin(), bookmarkRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == bookmarksIt->id
            && relation.targetResourceId == timingIt->id
            && relation.label == QLatin1String("links-to");
    }));
    QVERIFY(std::any_of(bookmarkRelations.cbegin(), bookmarkRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == bookmarksIt->id
            && relation.targetResourceId == hostIt->id
            && relation.label == QLatin1String("links-to");
    }));
}

void DirectorySourceTest::extractsXbelBookmarkLinks()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/bookmarks")));
    writeFile(dir.filePath(QStringLiteral("library/bookmarks/engineering.xbel")),
              QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                         "<xbel version=\"1.0\">\n"
                         "  <folder>\n"
                         "    <title>Engineering</title>\n"
                         "    <folder>\n"
                         "      <title>FPGA</title>\n"
                         "      <bookmark href=\"https://fpga.example.com/handbook#timing\">\n"
                         "        <title>FPGA Handbook</title>\n"
                         "      </bookmark>\n"
                         "      <bookmark href=\"https://docs.example.com/pinloom/host#dock\">\n"
                         "        <title>Pinloom Host API</title>\n"
                         "      </bookmark>\n"
                         "      <bookmark href=\"https://fpga.example.com/handbook#timing\">\n"
                         "        <title>Duplicate FPGA Handbook</title>\n"
                         "      </bookmark>\n"
                         "    </folder>\n"
                         "  </folder>\n"
                         "</xbel>\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto xbelIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("engineering.xbel");
    });
    QVERIFY(xbelIt != resources.cend());
    QVERIFY(xbelIt->tags.contains(QStringLiteral("bookmark")));
    QVERIFY(xbelIt->tags.contains(QStringLiteral("xbel")));
    QVERIFY(std::any_of(xbelIt->anchors.cbegin(), xbelIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: FPGA Handbook -> https://fpga.example.com/handbook#timing")
            && anchor.line == 7;
    }));
    QVERIFY(std::any_of(xbelIt->anchors.cbegin(), xbelIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: Pinloom Host API -> https://docs.example.com/pinloom/host#dock")
            && anchor.line == 10;
    }));

    auto handbookIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("FPGA Handbook")
            && resource.location == QLatin1String("https://fpga.example.com/handbook#timing");
    });
    QVERIFY(handbookIt != resources.cend());
    QVERIFY(handbookIt->tags.contains(QStringLiteral("bookmark")));
    QVERIFY(handbookIt->tags.contains(QStringLiteral("xbel")));
    QVERIFY(handbookIt->aliases.contains(QStringLiteral("fpga.example.com")));
    QVERIFY(handbookIt->aliases.contains(QStringLiteral("Engineering")));
    QVERIFY(handbookIt->aliases.contains(QStringLiteral("FPGA")));
    QVERIFY(handbookIt->aliases.contains(QStringLiteral("Engineering / FPGA")));
    QVERIFY(std::any_of(handbookIt->anchors.cbegin(), handbookIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("timing");
    }));

    auto hostIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Pinloom Host API")
            && resource.location == QLatin1String("https://docs.example.com/pinloom/host#dock");
    });
    QVERIFY(hostIt != resources.cend());
    QVERIFY(hostIt->tags.contains(QStringLiteral("xbel")));
    QVERIFY(std::any_of(hostIt->anchors.cbegin(), hostIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("dock");
    }));

    const int handbookResourceCount = std::count_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://fpga.example.com/handbook#timing");
    });
    QCOMPARE(handbookResourceCount, 1);

    QCOMPARE(xbelIt->relations.size(), 2);
    QVERIFY(std::any_of(xbelIt->relations.cbegin(), xbelIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == xbelIt->id
            && relation.targetResourceId == handbookIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("xbel line 7: url: FPGA Handbook -> https://fpga.example.com/handbook#timing");
    }));
    QVERIFY(std::any_of(xbelIt->relations.cbegin(), xbelIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == xbelIt->id
            && relation.targetResourceId == hostIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("xbel line 10: url: Pinloom Host API -> https://docs.example.com/pinloom/host#dock");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> folderResults = repository.search(SearchQuery{QStringLiteral("Engineering")});
    QVERIFY(std::any_of(folderResults.cbegin(), folderResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("FPGA Handbook");
    }));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("dock")});
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("Pinloom Host API")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("dock");
    }));

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("Pinloom Host API")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("engineering.xbel")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 10
            && result.matchedAnchor->target == QLatin1String("url: Pinloom Host API -> https://docs.example.com/pinloom/host#dock");
    }));

    const QList<ResourceRelation> bookmarkRelations = repository.resourceRelations(xbelIt->id);
    QCOMPARE(bookmarkRelations.size(), 2);
    QVERIFY(std::any_of(bookmarkRelations.cbegin(), bookmarkRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == xbelIt->id
            && relation.targetResourceId == handbookIt->id
            && relation.label == QLatin1String("links-to");
    }));
    QVERIFY(std::any_of(bookmarkRelations.cbegin(), bookmarkRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == xbelIt->id
            && relation.targetResourceId == hostIt->id
            && relation.label == QLatin1String("links-to");
    }));
}

void DirectorySourceTest::extractsBrowserHistorySqliteLinks()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/profile")));
    const QString historyPath = dir.filePath(QStringLiteral("library/profile/History"));
    writeChromiumHistoryDatabase(historyPath);

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto historyIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("History");
    });
    QVERIFY(historyIt != resources.cend());

    auto entryIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Pinloom History Entry")
            && resource.location == QLatin1String("https://docs.example.com/pinloom/history#jump");
    });
    QVERIFY(entryIt != resources.cend());
    QVERIFY(entryIt->tags.contains(QStringLiteral("web")));
    QVERIFY(entryIt->tags.contains(QStringLiteral("browser-history")));
    QVERIFY(entryIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(entryIt->aliases.contains(QStringLiteral("visited 7 times")));
    QVERIFY(std::any_of(entryIt->anchors.cbegin(), entryIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("jump");
    }));

    QVERIFY(std::none_of(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location.contains(QStringLiteral("chrome://settings"));
    }));

    QCOMPARE(historyIt->relations.size(), 1);
    QCOMPARE(historyIt->relations.first().sourceResourceId, historyIt->id);
    QCOMPARE(historyIt->relations.first().targetResourceId, entryIt->id);
    QCOMPARE(historyIt->relations.first().label, QStringLiteral("links-to"));
    QCOMPARE(historyIt->relations.first().note,
             QStringLiteral("browser history visits 7: url: Pinloom History Entry -> https://docs.example.com/pinloom/history#jump"));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> titleResults = repository.search(SearchQuery{QStringLiteral("History Entry")});
    QVERIFY(std::any_of(titleResults.cbegin(), titleResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinloom History Entry");
    }));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("jump")});
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinloom History Entry")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("jump");
    }));

    const QList<ResourceRelation> entryRelations = repository.resourceRelations(entryIt->id);
    QCOMPARE(entryRelations.size(), 1);
    QCOMPARE(entryRelations.first().sourceResourceId, historyIt->id);
    QCOMPARE(entryRelations.first().targetResourceId, entryIt->id);
}

void DirectorySourceTest::extractsFirefoxPlacesSqliteLinks()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/firefox")));
    const QString placesPath = dir.filePath(QStringLiteral("library/firefox/places.sqlite"));
    writeFirefoxPlacesDatabase(placesPath);

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto placesIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("places.sqlite");
    });
    QVERIFY(placesIt != resources.cend());

    auto entryIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Pinned Firefox Guide")
            && resource.location == QLatin1String("https://docs.example.com/pinloom/firefox#places");
    });
    QVERIFY(entryIt != resources.cend());
    QVERIFY(entryIt->tags.contains(QStringLiteral("web")));
    QVERIFY(entryIt->tags.contains(QStringLiteral("browser-history")));
    QVERIFY(entryIt->tags.contains(QStringLiteral("firefox-history")));
    QVERIFY(entryIt->tags.contains(QStringLiteral("firefox-bookmark")));
    QVERIFY(entryIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(entryIt->aliases.contains(QStringLiteral("Research")));
    QVERIFY(entryIt->aliases.contains(QStringLiteral("Pinloom")));
    QVERIFY(entryIt->aliases.contains(QStringLiteral("Research / Pinloom")));
    QVERIFY(entryIt->aliases.contains(QStringLiteral("visited 5 times")));
    QVERIFY(std::any_of(entryIt->anchors.cbegin(), entryIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("places");
    }));

    QVERIFY(std::none_of(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location.contains(QStringLiteral("about:config"));
    }));

    QCOMPARE(placesIt->relations.size(), 1);
    QCOMPARE(placesIt->relations.first().sourceResourceId, placesIt->id);
    QCOMPARE(placesIt->relations.first().targetResourceId, entryIt->id);
    QCOMPARE(placesIt->relations.first().label, QStringLiteral("links-to"));
    QCOMPARE(placesIt->relations.first().note,
             QStringLiteral("browser history visits 5: url: Pinned Firefox Guide -> https://docs.example.com/pinloom/firefox#places"));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> titleResults = repository.search(SearchQuery{QStringLiteral("Pinned Firefox Guide")});
    QVERIFY(std::any_of(titleResults.cbegin(), titleResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinned Firefox Guide");
    }));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("places")});
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Pinned Firefox Guide")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("places");
    }));

    const QList<ResourceRelation> entryRelations = repository.resourceRelations(entryIt->id);
    QCOMPARE(entryRelations.size(), 1);
    QCOMPARE(entryRelations.first().sourceResourceId, placesIt->id);
    QCOMPARE(entryRelations.first().targetResourceId, entryIt->id);
}

void DirectorySourceTest::extractsGenericSqliteBeaconMetadata()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/db")));
    const QString databasePath = dir.filePath(QStringLiteral("library/db/inventory.sqlite3"));
    writeGenericSqliteDatabase(databasePath);

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto databaseIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("inventory.sqlite3");
    });
    QVERIFY(databaseIt != resources.cend());
    QVERIFY(databaseIt->tags.contains(QStringLiteral("sqlite")));
    QVERIFY(databaseIt->tags.contains(QStringLiteral("sqlite-database")));
    QVERIFY(databaseIt->tags.contains(QStringLiteral("special-reader")));
    QVERIFY(databaseIt->aliases.contains(QStringLiteral("board_notes")));
    QVERIFY(databaseIt->aliases.contains(QStringLiteral("parts")));

    auto hasLineAnchor = [](const Resource &resource, const QString &target) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::FileLine
                && anchor.target == target
                && anchor.line == 1;
        });
    };

    QVERIFY(hasLineAnchor(*databaseIt, QStringLiteral("sqlite table: board_notes")));
    QVERIFY(hasLineAnchor(*databaseIt, QStringLiteral("sqlite table: parts")));
    QVERIFY(hasLineAnchor(*databaseIt, QStringLiteral("sqlite column: board_notes.title TEXT")));
    QVERIFY(hasLineAnchor(*databaseIt, QStringLiteral("sqlite column: parts.doc_url TEXT")));
    QVERIFY(hasLineAnchor(*databaseIt,
                          QStringLiteral("sqlite url: board_notes.url -> https://docs.example.com/pinloom/sqlite#row")));
    QVERIFY(hasLineAnchor(*databaseIt,
                          QStringLiteral("sqlite url: parts.doc_url -> https://fpga.example.com/parts/xc7a35t")));
    QVERIFY(hasLineAnchor(*databaseIt,
                          QStringLiteral("sqlite sample: board_notes.title = Timing Closure")));
    QVERIFY(hasLineAnchor(*databaseIt,
                          QStringLiteral("sqlite sample: parts.description = PCIe endpoint guide")));

    QVERIFY(std::none_of(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location.contains(QStringLiteral("pinloom/sqlite"));
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> titleResults = repository.search(SearchQuery{QStringLiteral("Timing Closure")});
    QVERIFY(std::any_of(titleResults.cbegin(), titleResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("inventory.sqlite3")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("sqlite sample: board_notes.title = Timing Closure");
    }));

    const QList<SearchResult> columnResults = repository.search(SearchQuery{QStringLiteral("parts.doc_url")});
    QVERIFY(std::any_of(columnResults.cbegin(), columnResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("inventory.sqlite3")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("sqlite column: parts.doc_url TEXT");
    }));

    const QList<SearchResult> urlResults = repository.search(SearchQuery{QStringLiteral("sqlite#row")});
    QVERIFY(std::any_of(urlResults.cbegin(), urlResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("inventory.sqlite3")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target
                == QLatin1String("sqlite url: board_notes.url -> https://docs.example.com/pinloom/sqlite#row");
    }));
}

void DirectorySourceTest::extractsOpmlLinks()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/feeds")));
    writeFile(dir.filePath(QStringLiteral("library/feeds/subscriptions.opml")),
              QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                         "<opml version=\"2.0\">\n"
                         "  <head><title>Pinloom feeds</title></head>\n"
                         "  <body>\n"
                         "    <outline text=\"Engineering\">\n"
                         "      <outline text=\"FPGA Daily\" htmlUrl=\"https://fpga.example.com/daily#timing\" xmlUrl=\"https://feeds.example.com/fpga.xml\" />\n"
                         "      <outline text=\"Pinloom Release Feed\" xmlUrl=\"https://docs.example.com/pinloom/feed.xml\" />\n"
                         "    </outline>\n"
                         "  </body>\n"
                         "</opml>\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto opmlIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("subscriptions.opml");
    });
    QVERIFY(opmlIt != resources.cend());
    QVERIFY(opmlIt->tags.contains(QStringLiteral("opml")));
    QVERIFY(std::any_of(opmlIt->anchors.cbegin(), opmlIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: FPGA Daily -> https://fpga.example.com/daily#timing")
            && anchor.line == 6;
    }));
    QVERIFY(std::any_of(opmlIt->anchors.cbegin(), opmlIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: Pinloom Release Feed -> https://docs.example.com/pinloom/feed.xml")
            && anchor.line == 7;
    }));

    auto fpgaIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("FPGA Daily")
            && resource.location == QLatin1String("https://fpga.example.com/daily#timing");
    });
    QVERIFY(fpgaIt != resources.cend());
    QVERIFY(fpgaIt->tags.contains(QStringLiteral("opml")));
    QVERIFY(fpgaIt->tags.contains(QStringLiteral("feed")));
    QVERIFY(fpgaIt->aliases.contains(QStringLiteral("fpga.example.com")));
    QVERIFY(fpgaIt->aliases.contains(QStringLiteral("feeds.example.com")));
    QVERIFY(fpgaIt->aliases.contains(QStringLiteral("Engineering")));
    QVERIFY(std::any_of(fpgaIt->anchors.cbegin(), fpgaIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("timing");
    }));

    auto feedOnlyIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Pinloom Release Feed")
            && resource.location == QLatin1String("https://docs.example.com/pinloom/feed.xml");
    });
    QVERIFY(feedOnlyIt != resources.cend());
    QVERIFY(feedOnlyIt->tags.contains(QStringLiteral("opml")));
    QVERIFY(feedOnlyIt->tags.contains(QStringLiteral("feed")));

    QCOMPARE(opmlIt->relations.size(), 2);
    QVERIFY(std::any_of(opmlIt->relations.cbegin(), opmlIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == opmlIt->id
            && relation.targetResourceId == fpgaIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("opml line 6: url: FPGA Daily -> https://fpga.example.com/daily#timing");
    }));
    QVERIFY(std::any_of(opmlIt->relations.cbegin(), opmlIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == opmlIt->id
            && relation.targetResourceId == feedOnlyIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("opml line 7: url: Pinloom Release Feed -> https://docs.example.com/pinloom/feed.xml");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> folderResults = repository.search(SearchQuery{QStringLiteral("Engineering")});
    QVERIFY(std::any_of(folderResults.cbegin(), folderResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("FPGA Daily");
    }));

    const QList<SearchResult> feedHostResults = repository.search(SearchQuery{QStringLiteral("feeds.example.com")});
    QVERIFY(std::any_of(feedHostResults.cbegin(), feedHostResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("FPGA Daily");
    }));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("timing")});
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("FPGA Daily")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("timing");
    }));

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("Pinloom Release Feed")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("subscriptions.opml")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 7
            && result.matchedAnchor->target == QLatin1String("url: Pinloom Release Feed -> https://docs.example.com/pinloom/feed.xml");
    }));

    const QList<ResourceRelation> opmlRelations = repository.resourceRelations(opmlIt->id);
    QCOMPARE(opmlRelations.size(), 2);
    QVERIFY(std::any_of(opmlRelations.cbegin(), opmlRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == opmlIt->id
            && relation.targetResourceId == fpgaIt->id
            && relation.label == QLatin1String("links-to");
    }));
    QVERIFY(std::any_of(opmlRelations.cbegin(), opmlRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == opmlIt->id
            && relation.targetResourceId == feedOnlyIt->id
            && relation.label == QLatin1String("links-to");
    }));
}

void DirectorySourceTest::extractsFeedXmlLinks()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/feeds")));
    writeFile(dir.filePath(QStringLiteral("library/feeds/pinloom.rss")),
              QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                         "<rss version=\"2.0\">\n"
                         "  <channel>\n"
                         "    <title>Pinloom Release Feed</title>\n"
                         "    <item>\n"
                         "      <title>Dock API release</title>\n"
                         "      <link>https://docs.example.com/pinloom/releases/dock#api</link>\n"
                         "      <category>release</category>\n"
                         "    </item>\n"
                         "    <item>\n"
                         "      <title>Duplicate Dock API release</title>\n"
                         "      <link>https://docs.example.com/pinloom/releases/dock#api</link>\n"
                         "    </item>\n"
                         "  </channel>\n"
                         "</rss>\n"));
    writeFile(dir.filePath(QStringLiteral("library/feeds/updates.atom")),
              QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                         "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
                         "  <title>ZeroSlack Updates</title>\n"
                         "  <entry>\n"
                         "    <title>Host bridge update</title>\n"
                         "    <link rel=\"alternate\" href=\"https://zeroslack.example.com/updates/host#bridge\" />\n"
                         "    <category term=\"integration\" />\n"
                         "  </entry>\n"
                         "</feed>\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto rssFileIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("pinloom.rss");
    });
    QVERIFY(rssFileIt != resources.cend());
    QVERIFY(rssFileIt->tags.contains(QStringLiteral("feed")));
    QVERIFY(rssFileIt->aliases.contains(QStringLiteral("Pinloom Release Feed")));
    QVERIFY(std::any_of(rssFileIt->anchors.cbegin(), rssFileIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: Dock API release -> https://docs.example.com/pinloom/releases/dock#api")
            && anchor.line == 7;
    }));

    auto rssIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Dock API release")
            && resource.location == QLatin1String("https://docs.example.com/pinloom/releases/dock#api");
    });
    QVERIFY(rssIt != resources.cend());
    QVERIFY(rssIt->tags.contains(QStringLiteral("feed")));
    QVERIFY(rssIt->tags.contains(QStringLiteral("feed-entry")));
    QVERIFY(rssIt->tags.contains(QStringLiteral("release")));
    QVERIFY(rssIt->aliases.contains(QStringLiteral("Pinloom Release Feed")));
    QVERIFY(rssIt->aliases.contains(QStringLiteral("pinloom")));
    QVERIFY(rssIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(std::any_of(rssIt->anchors.cbegin(), rssIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("api");
    }));

    const int rssResourceCount = std::count_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/pinloom/releases/dock#api");
    });
    QCOMPARE(rssResourceCount, 1);

    QCOMPARE(rssFileIt->relations.size(), 1);
    QCOMPARE(rssFileIt->relations.first().sourceResourceId, rssFileIt->id);
    QCOMPARE(rssFileIt->relations.first().targetResourceId, rssIt->id);
    QCOMPARE(rssFileIt->relations.first().label, QStringLiteral("links-to"));
    QCOMPARE(rssFileIt->relations.first().note,
             QStringLiteral("feed line 7: url: Dock API release -> https://docs.example.com/pinloom/releases/dock#api"));

    auto atomFileIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("updates.atom");
    });
    QVERIFY(atomFileIt != resources.cend());
    QVERIFY(atomFileIt->tags.contains(QStringLiteral("feed")));
    QVERIFY(atomFileIt->aliases.contains(QStringLiteral("ZeroSlack Updates")));
    QVERIFY(std::any_of(atomFileIt->anchors.cbegin(), atomFileIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: Host bridge update -> https://zeroslack.example.com/updates/host#bridge")
            && anchor.line == 6;
    }));

    auto atomIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.title == QLatin1String("Host bridge update")
            && resource.location == QLatin1String("https://zeroslack.example.com/updates/host#bridge");
    });
    QVERIFY(atomIt != resources.cend());
    QVERIFY(atomIt->tags.contains(QStringLiteral("feed")));
    QVERIFY(atomIt->tags.contains(QStringLiteral("feed-entry")));
    QVERIFY(atomIt->tags.contains(QStringLiteral("integration")));
    QVERIFY(atomIt->aliases.contains(QStringLiteral("ZeroSlack Updates")));
    QVERIFY(atomIt->aliases.contains(QStringLiteral("updates")));

    QCOMPARE(atomFileIt->relations.size(), 1);
    QCOMPARE(atomFileIt->relations.first().sourceResourceId, atomFileIt->id);
    QCOMPARE(atomFileIt->relations.first().targetResourceId, atomIt->id);
    QCOMPARE(atomFileIt->relations.first().label, QStringLiteral("links-to"));
    QCOMPARE(atomFileIt->relations.first().note,
             QStringLiteral("feed line 6: url: Host bridge update -> https://zeroslack.example.com/updates/host#bridge"));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> feedTitleResults = repository.search(SearchQuery{QStringLiteral("Pinloom Release Feed")});
    QVERIFY(std::any_of(feedTitleResults.cbegin(), feedTitleResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Dock API release");
    }));

    const QList<SearchResult> categoryResults = repository.search(SearchQuery{QStringLiteral("integration")});
    QVERIFY(std::any_of(categoryResults.cbegin(), categoryResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.title == QLatin1String("Host bridge update")
            && result.matchedField == QLatin1String("tag");
    }));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("bridge")});
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("Host bridge update")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("bridge");
    }));

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("Dock API release")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("pinloom.rss")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 7
            && result.matchedAnchor->target == QLatin1String("url: Dock API release -> https://docs.example.com/pinloom/releases/dock#api");
    }));

    const QList<ResourceRelation> rssRelations = repository.resourceRelations(rssFileIt->id);
    QCOMPARE(rssRelations.size(), 1);
    QCOMPARE(rssRelations.first().sourceResourceId, rssFileIt->id);
    QCOMPARE(rssRelations.first().targetResourceId, rssIt->id);

    const QList<ResourceRelation> atomRelations = repository.resourceRelations(atomFileIt->id);
    QCOMPARE(atomRelations.size(), 1);
    QCOMPARE(atomRelations.first().sourceResourceId, atomFileIt->id);
    QCOMPARE(atomRelations.first().targetResourceId, atomIt->id);
}

void DirectorySourceTest::extractsSitemapXmlLinks()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/site")));
    writeFile(dir.filePath(QStringLiteral("library/site/sitemap.xml")),
              QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                         "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n"
                         "  <url><loc>https://docs.example.com/pinloom/guide#install</loc></url>\n"
                         "  <url><loc>https://docs.example.com/pinloom/guide#install</loc></url>\n"
                         "  <url><loc>https://docs.example.com/pinloom/reference</loc></url>\n"
                         "</urlset>\n"));
    writeFile(dir.filePath(QStringLiteral("library/site/sitemap-index.xml")),
              QByteArray("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                         "<sitemapindex xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n"
                         "  <sitemap><loc>https://docs.example.com/sitemaps/pinloom.xml</loc></sitemap>\n"
                         "</sitemapindex>\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto sitemapFileIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("sitemap.xml");
    });
    QVERIFY(sitemapFileIt != resources.cend());
    QVERIFY(sitemapFileIt->tags.contains(QStringLiteral("sitemap")));
    QVERIFY(std::any_of(sitemapFileIt->anchors.cbegin(), sitemapFileIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: guide -> https://docs.example.com/pinloom/guide#install")
            && anchor.line == 3;
    }));
    QVERIFY(std::any_of(sitemapFileIt->anchors.cbegin(), sitemapFileIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: reference -> https://docs.example.com/pinloom/reference")
            && anchor.line == 5;
    }));

    auto guideIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/pinloom/guide#install");
    });
    QVERIFY(guideIt != resources.cend());
    QCOMPARE(guideIt->title, QStringLiteral("guide"));
    QVERIFY(guideIt->tags.contains(QStringLiteral("web")));
    QVERIFY(guideIt->tags.contains(QStringLiteral("sitemap")));
    QVERIFY(guideIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(guideIt->aliases.contains(QStringLiteral("sitemap")));
    QVERIFY(std::any_of(guideIt->anchors.cbegin(), guideIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("install");
    }));

    const int guideResourceCount = std::count_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/pinloom/guide#install");
    });
    QCOMPARE(guideResourceCount, 1);

    auto indexIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/sitemaps/pinloom.xml");
    });
    QVERIFY(indexIt != resources.cend());
    QVERIFY(indexIt->tags.contains(QStringLiteral("sitemap")));
    QVERIFY(indexIt->tags.contains(QStringLiteral("sitemap-index")));

    auto referenceIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/pinloom/reference");
    });
    QVERIFY(referenceIt != resources.cend());

    QCOMPARE(sitemapFileIt->relations.size(), 2);
    QVERIFY(std::any_of(sitemapFileIt->relations.cbegin(), sitemapFileIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == sitemapFileIt->id
            && relation.targetResourceId == guideIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("sitemap line 3: url: guide -> https://docs.example.com/pinloom/guide#install");
    }));
    QVERIFY(std::any_of(sitemapFileIt->relations.cbegin(), sitemapFileIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == sitemapFileIt->id
            && relation.targetResourceId == referenceIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("sitemap line 5: url: reference -> https://docs.example.com/pinloom/reference");
    }));

    auto sitemapIndexFileIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("sitemap-index.xml");
    });
    QVERIFY(sitemapIndexFileIt != resources.cend());
    QVERIFY(sitemapIndexFileIt->tags.contains(QStringLiteral("sitemap")));
    QVERIFY(sitemapIndexFileIt->tags.contains(QStringLiteral("sitemap-index")));
    QVERIFY(std::any_of(sitemapIndexFileIt->anchors.cbegin(), sitemapIndexFileIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: pinloom.xml -> https://docs.example.com/sitemaps/pinloom.xml")
            && anchor.line == 3;
    }));
    QCOMPARE(sitemapIndexFileIt->relations.size(), 1);
    QCOMPARE(sitemapIndexFileIt->relations.first().sourceResourceId, sitemapIndexFileIt->id);
    QCOMPARE(sitemapIndexFileIt->relations.first().targetResourceId, indexIt->id);
    QCOMPARE(sitemapIndexFileIt->relations.first().label, QStringLiteral("links-to"));
    QCOMPARE(sitemapIndexFileIt->relations.first().note,
             QStringLiteral("sitemap-index line 3: url: pinloom.xml -> https://docs.example.com/sitemaps/pinloom.xml"));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> hostResults = repository.search(SearchQuery{QStringLiteral("docs.example.com")});
    QVERIFY(std::any_of(hostResults.cbegin(), hostResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.location == QLatin1String("https://docs.example.com/pinloom/guide#install");
    }));

    const QList<SearchResult> tagResults = repository.search(SearchQuery{QStringLiteral("sitemap-index")});
    QVERIFY(std::any_of(tagResults.cbegin(), tagResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.location == QLatin1String("https://docs.example.com/sitemaps/pinloom.xml")
            && result.resource.tags.contains(QStringLiteral("sitemap-index"));
    }));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("install")});
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.location == QLatin1String("https://docs.example.com/pinloom/guide#install")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("install");
    }));

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("pinloom.xml")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("sitemap-index.xml")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 3
            && result.matchedAnchor->target == QLatin1String("url: pinloom.xml -> https://docs.example.com/sitemaps/pinloom.xml");
    }));

    const QList<ResourceRelation> sitemapRelations = repository.resourceRelations(sitemapFileIt->id);
    QCOMPARE(sitemapRelations.size(), 2);
    QVERIFY(std::any_of(sitemapRelations.cbegin(), sitemapRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == sitemapFileIt->id
            && relation.targetResourceId == guideIt->id
            && relation.label == QLatin1String("links-to");
    }));

    const QList<ResourceRelation> sitemapIndexRelations = repository.resourceRelations(sitemapIndexFileIt->id);
    QCOMPARE(sitemapIndexRelations.size(), 1);
    QCOMPARE(sitemapIndexRelations.first().targetResourceId, indexIt->id);
}

void DirectorySourceTest::extractsRobotsTxtSitemapLinks()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/site")));
    writeFile(dir.filePath(QStringLiteral("library/site/robots.txt")),
              QByteArray("User-agent: *\n"
                         "Allow: /\n"
                         "Sitemap: https://docs.example.com/sitemaps/pinloom.xml\n"
                         "sitemap: https://docs.example.com/pinloom/guide#install\n"
                         "Sitemap: https://docs.example.com/sitemaps/pinloom.xml\n"
                         "Disallow: /private\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto robotsIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::File
            && resource.title == QLatin1String("robots.txt");
    });
    QVERIFY(robotsIt != resources.cend());
    QVERIFY(robotsIt->tags.contains(QStringLiteral("robots")));
    QVERIFY(robotsIt->tags.contains(QStringLiteral("sitemap")));
    QVERIFY(std::any_of(robotsIt->anchors.cbegin(), robotsIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("url: pinloom.xml -> https://docs.example.com/sitemaps/pinloom.xml")
            && anchor.line == 3;
    }));

    auto sitemapIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/sitemaps/pinloom.xml");
    });
    QVERIFY(sitemapIt != resources.cend());
    QCOMPARE(sitemapIt->title, QStringLiteral("pinloom.xml"));
    QVERIFY(sitemapIt->tags.contains(QStringLiteral("web")));
    QVERIFY(sitemapIt->tags.contains(QStringLiteral("robots")));
    QVERIFY(sitemapIt->tags.contains(QStringLiteral("sitemap")));
    QVERIFY(!sitemapIt->tags.contains(QStringLiteral("web-link")));
    QVERIFY(sitemapIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(sitemapIt->aliases.contains(QStringLiteral("robots")));
    QVERIFY(sitemapIt->aliases.contains(QStringLiteral("robots.txt")));

    auto guideIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/pinloom/guide#install");
    });
    QVERIFY(guideIt != resources.cend());
    QCOMPARE(guideIt->title, QStringLiteral("guide"));
    QVERIFY(guideIt->tags.contains(QStringLiteral("robots")));
    QVERIFY(guideIt->tags.contains(QStringLiteral("sitemap")));
    QVERIFY(std::any_of(guideIt->anchors.cbegin(), guideIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("install");
    }));

    const int sitemapResourceCount = std::count_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url
            && resource.location == QLatin1String("https://docs.example.com/sitemaps/pinloom.xml");
    });
    QCOMPARE(sitemapResourceCount, 1);

    QCOMPARE(robotsIt->relations.size(), 2);
    QVERIFY(std::any_of(robotsIt->relations.cbegin(), robotsIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == robotsIt->id
            && relation.targetResourceId == sitemapIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("robots line 3: url: pinloom.xml -> https://docs.example.com/sitemaps/pinloom.xml");
    }));
    QVERIFY(std::any_of(robotsIt->relations.cbegin(), robotsIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == robotsIt->id
            && relation.targetResourceId == guideIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("robots line 4: url: guide -> https://docs.example.com/pinloom/guide#install");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> sitemapResults = repository.search(SearchQuery{QStringLiteral("sitemap")});
    QVERIFY(std::any_of(sitemapResults.cbegin(), sitemapResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.location == QLatin1String("https://docs.example.com/sitemaps/pinloom.xml");
    }));

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("install")});
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Url
            && result.resource.location == QLatin1String("https://docs.example.com/pinloom/guide#install")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("install");
    }));

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("pinloom.xml")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("robots.txt")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 3
            && result.matchedAnchor->target == QLatin1String("url: pinloom.xml -> https://docs.example.com/sitemaps/pinloom.xml");
    }));

    const QList<ResourceRelation> robotsRelations = repository.resourceRelations(robotsIt->id);
    QCOMPARE(robotsRelations.size(), 2);
    QVERIFY(std::any_of(robotsRelations.cbegin(), robotsRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == robotsIt->id
            && relation.targetResourceId == sitemapIt->id
            && relation.label == QLatin1String("links-to");
    }));
}

void DirectorySourceTest::fetchesRemoteWebShortcutContent()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/links")));
    writeFile(dir.filePath(QStringLiteral("library/links/Remote Guide.url")),
              QByteArray("[InternetShortcut]\n"
                         "URL=https://docs.example.com/pinloom/live#routing\n"));

    bool fetchCalled = false;
    QUrl fetchedUrl;
    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    source.setRemoteWebFetchingEnabled(true);
    source.setWebPageFetcher([&](const QUrl &url, QString *errorMessage) {
        fetchCalled = true;
        fetchedUrl = url;
        if (errorMessage) {
            errorMessage->clear();
        }

        DirectoryLibrarySource::WebPageFetchResult result;
        result.finalUrl = QUrl(QStringLiteral("https://docs.example.com/pinloom/live"));
        result.contentType = QStringLiteral("text/html; charset=utf-8");
        result.body = QByteArray("<!doctype html>"
                                 "<html><head>"
                                 "<title>Remote Pinloom Guide</title>"
                                 "<link rel=\"canonical\" href=\"https://docs.example.com/pinloom/live\">"
                                 "</head><body>"
                                 "<h2 id=\"routing\">Browser Routing</h2>"
                                 "<p>Remote launch handoff content is searchable after fetching.</p>"
                                 "</body></html>");
        return std::optional<DirectoryLibrarySource::WebPageFetchResult>(result);
    });

    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(fetchCalled);
    QCOMPARE(fetchedUrl.toDisplayString(), QStringLiteral("https://docs.example.com/pinloom/live#routing"));

    auto remoteIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url && resource.title == QLatin1String("Remote Pinloom Guide");
    });
    QVERIFY(remoteIt != resources.cend());
    QCOMPARE(remoteIt->location, QStringLiteral("https://docs.example.com/pinloom/live#routing"));
    QVERIFY(remoteIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(remoteIt->aliases.contains(QStringLiteral("https://docs.example.com/pinloom/live")));
    QVERIFY(remoteIt->aliases.contains(QStringLiteral("Browser Routing")));
    QVERIFY(remoteIt->content.contains(QStringLiteral("Remote launch handoff content")));
    QVERIFY(std::any_of(remoteIt->anchors.cbegin(), remoteIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::UrlFragment && anchor.target == QLatin1String("routing");
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("launch handoff")});
    QCOMPARE(contentResults.size(), 1);
    QCOMPARE(contentResults.first().resource.kind, ResourceKind::Url);
    QCOMPARE(contentResults.first().matchedField, QStringLiteral("content"));

    const QList<SearchResult> anchorResults = repository.search(SearchQuery{QStringLiteral("routing")});
    QVERIFY(std::any_of(anchorResults.cbegin(), anchorResults.cend(), [](const SearchResult &result) {
        return result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("routing");
    }));
}

void DirectorySourceTest::indexRootFetchesRemoteWebShortcutContent()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/links")));
    writeFile(dir.filePath(QStringLiteral("library/links/Host Guide.url")),
              QByteArray("[InternetShortcut]\n"
                         "URL=https://docs.example.com/pinloom/host\n"));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    LibraryRoot root = makeLibraryRootForPath(dir.filePath(QStringLiteral("library")));
    QVERIFY(repository.upsertLibraryRoot(root));

    bool fetchCalled = false;
    QUrl fetchedUrl;
    IndexingService indexer(repository);
    indexer.setRemoteWebFetchingEnabled(true);
    indexer.setWebPageFetcher([&](const QUrl &url, QString *errorMessage) {
        fetchCalled = true;
        fetchedUrl = url;
        if (errorMessage) {
            errorMessage->clear();
        }

        DirectoryLibrarySource::WebPageFetchResult result;
        result.finalUrl = url;
        result.contentType = QStringLiteral("text/html");
        result.body = QByteArray("<html><head><title>Host Integration Guide</title></head>"
                                 "<body><h1 id=\"dock\">Dock Host</h1>"
                                 "<p>ZeroSlack dock handoff content is fetched.</p></body></html>");
        return std::optional<DirectoryLibrarySource::WebPageFetchResult>(result);
    });

    QVERIFY2(indexer.indexRoot(root), qPrintable(indexer.lastError()));
    QVERIFY(fetchCalled);
    QCOMPARE(fetchedUrl.toDisplayString(), QStringLiteral("https://docs.example.com/pinloom/host"));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("dock handoff")});
    QCOMPARE(contentResults.size(), 1);
    QCOMPARE(contentResults.first().resource.kind, ResourceKind::Url);
    QCOMPARE(contentResults.first().matchedField, QStringLiteral("content"));

    const QList<SearchResult> anchorResults = repository.search(SearchQuery{QStringLiteral("dock")});
    QVERIFY(std::any_of(anchorResults.cbegin(), anchorResults.cend(), [](const SearchResult &result) {
        return result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("dock");
    }));
}

void DirectorySourceTest::indexesDirectoryResourcesIdempotently()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/docs")));
    writeFile(dir.filePath(QStringLiteral("library/notes.md")));
    writeFile(dir.filePath(QStringLiteral("library/docs/design.pdf")));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));
    QCOMPARE(indexer.lastIndexedCount(), 4);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));
    QCOMPARE(indexer.lastIndexedCount(), 4);

    SearchQuery allQuery;
    allQuery.limit = 100;
    QCOMPARE(repository.search(allQuery).size(), 4);

    const QList<SearchResult> designResults = repository.search(SearchQuery{QStringLiteral("design")});
    QCOMPARE(designResults.size(), 1);
    QCOMPARE(designResults.first().resource.kind, ResourceKind::Pdf);
}

void DirectorySourceTest::indexesSavedEnabledRoots()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("enabled")));
    QVERIFY(dir.mkpath(QStringLiteral("disabled")));
    writeFile(dir.filePath(QStringLiteral("enabled/notes.md")));
    writeFile(dir.filePath(QStringLiteral("disabled/hidden.md")));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    LibraryRoot enabledRoot = makeLibraryRootForPath(dir.filePath(QStringLiteral("enabled")));
    LibraryRoot disabledRoot = makeLibraryRootForPath(dir.filePath(QStringLiteral("disabled")));
    disabledRoot.enabled = false;
    QVERIFY2(repository.upsertLibraryRoot(enabledRoot), qPrintable(repository.lastError()));
    QVERIFY2(repository.upsertLibraryRoot(disabledRoot), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.indexEnabledRoots(), qPrintable(indexer.lastError()));
    QCOMPARE(indexer.lastIndexedCount(), 2);
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("notes")}).size(), 1);
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("hidden")}).size(), 0);

    const std::optional<LibraryRoot> stored = repository.findLibraryRoot(enabledRoot.id);
    QVERIFY(stored.has_value());
    QVERIFY(stored->lastIndexedAt.isValid());
}

void DirectorySourceTest::rebuildClearsExistingResources()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    writeFile(dir.filePath(QStringLiteral("library/notes.md")));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    Resource stale;
    stale.id = QStringLiteral("stale");
    stale.kind = ResourceKind::File;
    stale.title = QStringLiteral("stale resource");
    stale.location = QStringLiteral("stale.txt");
    QVERIFY2(repository.upsertResource(stale), qPrintable(repository.lastError()));

    LibraryRoot root = makeLibraryRootForPath(dir.filePath(QStringLiteral("library")));
    QVERIFY2(repository.upsertLibraryRoot(root), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.rebuildEnabledRoots(), qPrintable(indexer.lastError()));

    SearchQuery allQuery;
    allQuery.limit = 100;
    QCOMPARE(repository.search(allQuery).size(), 2);
    QCOMPARE(repository.search(SearchQuery{QStringLiteral("stale")}).size(), 0);
}

QTEST_MAIN(DirectorySourceTest)

#include "directory_source_test.moc"
