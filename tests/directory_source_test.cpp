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
    void extractsStructuredPlainTextLineAnchors();
    void extractsManifestDependencyLineAnchors();
    void extractsMarkdownHeadingAndBlockAnchors();
    void extractsObsidianAliasesTagsAndWikilinks();
    void extractsMarkdownBodyContent();
    void extractsLocalMarkdownLinkAnchors();
    void extractsMarkdownTaskLineAnchors();
    void extractsMarkdownLinkResources();
    void extractsMarkdownReferenceLinkResources();
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
    void extractsCodeSymbolAnchors();
    void extractsAdditionalLanguageSymbolAnchors();
    void extractsCodeTestCaseAnchors();
    void extractsCodeDependencyLineAnchors();
    void extractsCodeCommentLineAnchors();
    void extractsWebShortcutResources();
    void extractsPlainTextUrlListResources();
    void extractsJsonUrlResources();
    void extractsStructuredTextUrlResources();
    void extractsTabularUrlResources();
    void extractsHtmlPageContent();
    void extractsBookmarkExportLinks();
    void extractsBrowserBookmarkJsonLinks();
    void extractsBrowserHistorySqliteLinks();
    void extractsOpmlLinks();
    void extractsFeedXmlLinks();
    void extractsSitemapXmlLinks();
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
        return resource.kind == ResourceKind::Markdown && resource.title == QLatin1String("notes.md");
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
    writeFile(dir.filePath(QStringLiteral("library/ops.log")),
              QByteArray("ZeroSlack relay reconnect sequence\n"
                         "NOTE: Pinloom host handoff status\n"));

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
    QVERIFY(std::any_of(logIt->anchors.cbegin(), logIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("NOTE: Pinloom host handoff status")
            && anchor.line == 2;
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
            && result.matchedAnchor->line == 2;
    }));

    const QList<SearchResult> binaryResults = repository.search(SearchQuery{QStringLiteral("unsearchable-nul-token")});
    QVERIFY(binaryResults.isEmpty());
}

void DirectorySourceTest::extractsStructuredPlainTextLineAnchors()
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

void DirectorySourceTest::extractsManifestDependencyLineAnchors()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library")));
    writeFile(dir.filePath(QStringLiteral("library/package.json")),
              QByteArray("{\n"
                         "  \"dependencies\": {\n"
                         "    \"react\": \"^18.2.0\",\n"
                         "    \"@zeroslack/dock\": \"workspace:*\"\n"
                         "  },\n"
                         "  \"devDependencies\": {\n"
                         "    \"vite\": \"^5.0.0\"\n"
                         "  }\n"
                         "}\n"));
    writeFile(dir.filePath(QStringLiteral("library/Cargo.toml")),
              QByteArray("[dependencies]\n"
                         "serde = \"1\"\n"
                         "tokio = { version = \"1\", features = [\"rt\"] }\n"));
    writeFile(dir.filePath(QStringLiteral("library/go.mod")),
              QByteArray("module example.com/pinloom\n"
                         "require (\n"
                         "    github.com/pkg/errors v0.9.1\n"
                         "    golang.org/x/net v0.22.0\n"
                         ")\n"
                         "require github.com/stretchr/testify v1.8.4\n"));
    writeFile(dir.filePath(QStringLiteral("library/requirements-dev.txt")),
              QByteArray("pinloom-sdk>=1.2\n"
                         "PySide6==6.10.2\n"
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
    auto hasDependency = [](const Resource &resource, const QString &dependency, int line) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::FileLine
                && anchor.target == QStringLiteral("dependency: %1").arg(dependency)
                && anchor.line == line;
        });
    };

    const auto packageIt = findFile(QStringLiteral("package.json"));
    QVERIFY(packageIt != resources.cend());
    QVERIFY(hasDependency(*packageIt, QStringLiteral("react"), 3));
    QVERIFY(hasDependency(*packageIt, QStringLiteral("@zeroslack/dock"), 4));
    QVERIFY(hasDependency(*packageIt, QStringLiteral("vite"), 7));

    const auto cargoIt = findFile(QStringLiteral("Cargo.toml"));
    QVERIFY(cargoIt != resources.cend());
    QVERIFY(hasDependency(*cargoIt, QStringLiteral("serde"), 2));
    QVERIFY(hasDependency(*cargoIt, QStringLiteral("tokio"), 3));

    const auto goIt = findFile(QStringLiteral("go.mod"));
    QVERIFY(goIt != resources.cend());
    QVERIFY(hasDependency(*goIt, QStringLiteral("github.com/pkg/errors"), 3));
    QVERIFY(hasDependency(*goIt, QStringLiteral("golang.org/x/net"), 4));
    QVERIFY(hasDependency(*goIt, QStringLiteral("github.com/stretchr/testify"), 6));

    const auto requirementsIt = findFile(QStringLiteral("requirements-dev.txt"));
    QVERIFY(requirementsIt != resources.cend());
    QVERIFY(hasDependency(*requirementsIt, QStringLiteral("pinloom-sdk"), 1));
    QVERIFY(hasDependency(*requirementsIt, QStringLiteral("PySide6"), 2));
    QVERIFY(!hasDependency(*requirementsIt, QStringLiteral("base.txt"), 3));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> reactResults = repository.search(SearchQuery{QStringLiteral("react")});
    QVERIFY(std::any_of(reactResults.cbegin(), reactResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("package.json")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("dependency: react");
    }));

    const QList<SearchResult> tokioResults = repository.search(SearchQuery{QStringLiteral("tokio")});
    QVERIFY(std::any_of(tokioResults.cbegin(), tokioResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("Cargo.toml")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("dependency: tokio");
    }));

    const QList<SearchResult> goResults = repository.search(SearchQuery{QStringLiteral("testify")});
    QVERIFY(std::any_of(goResults.cbegin(), goResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("go.mod")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("dependency: github.com/stretchr/testify");
    }));
}

void DirectorySourceTest::extractsMarkdownHeadingAndBlockAnchors()
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
        return resource.kind == ResourceKind::Markdown;
    });
    QVERIFY(markdownIt != resources.cend());
    QCOMPARE(markdownIt->anchors.size(), 4);
    QCOMPARE(markdownIt->anchors.at(0).type, AnchorType::MarkdownHeading);
    QCOMPARE(markdownIt->anchors.at(0).target, QStringLiteral("Top"));
    QCOMPARE(markdownIt->anchors.at(0).line, 1);
    QCOMPARE(markdownIt->anchors.at(1).target, QStringLiteral("Power sequencing"));
    QCOMPARE(markdownIt->anchors.at(1).line, 3);
    QCOMPARE(markdownIt->anchors.at(2).type, AnchorType::MarkdownBlock);
    QCOMPARE(markdownIt->anchors.at(2).target, QStringLiteral("power-block"));
    QCOMPARE(markdownIt->anchors.at(2).line, 4);
    QCOMPARE(markdownIt->anchors.at(3).target, QStringLiteral("standalone"));
    QCOMPARE(markdownIt->anchors.at(3).line, 5);
}

void DirectorySourceTest::extractsObsidianAliasesTagsAndWikilinks()
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
        return resource.kind == ResourceKind::Markdown && resource.title == QLatin1String("notes.md");
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
            && anchor.target == QLatin1String("wikilink: Link Target -> Link Target.md")
            && anchor.line == 8;
    }));
    QVERIFY(std::any_of(markdownIt->anchors.cbegin(), markdownIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("wikilink: display -> deep/note.md#power-block")
            && anchor.line == 8;
    }));
    QCOMPARE(markdownIt->relations.size(), 2);
    QVERIFY(std::any_of(markdownIt->relations.cbegin(), markdownIt->relations.cend(), [](const ResourceRelation &relation) {
        return relation.label == QLatin1String("links-to")
            && relation.targetResourceId.endsWith(QStringLiteral("Link Target.md"))
            && relation.note == QLatin1String("wikilink: Link Target -> Link Target.md");
    }));
    QVERIFY(std::any_of(markdownIt->relations.cbegin(), markdownIt->relations.cend(), [](const ResourceRelation &relation) {
        return relation.label == QLatin1String("links-to")
            && relation.targetResourceId.endsWith(QStringLiteral("deep/note.md"))
            && relation.note == QLatin1String("wikilink: display -> deep/note.md#power-block");
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
            && result.matchedAnchor->target == QLatin1String("wikilink: Link Target -> Link Target.md");
    }));

    const QList<ResourceRelation> relations = repository.resourceRelations(markdownIt->id);
    QCOMPARE(relations.size(), 2);
    QVERIFY(std::any_of(relations.cbegin(), relations.cend(), [](const ResourceRelation &relation) {
        return relation.targetResourceId.endsWith(QStringLiteral("Link Target.md"));
    }));
    QVERIFY(std::any_of(relations.cbegin(), relations.cend(), [](const ResourceRelation &relation) {
        return relation.targetResourceId.endsWith(QStringLiteral("deep/note.md"));
    }));

    const QList<SearchResult> wikilinkResults = repository.search(SearchQuery{QStringLiteral("power-block")});
    QVERIFY(std::any_of(wikilinkResults.cbegin(), wikilinkResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("notes.md")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("wikilink: display -> deep/note.md#power-block");
    }));
}

void DirectorySourceTest::extractsMarkdownBodyContent()
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
        return resource.kind == ResourceKind::Markdown;
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
    QCOMPARE(contentResults.first().resource.kind, ResourceKind::Markdown);
    QCOMPARE(contentResults.first().matchedField, QStringLiteral("content"));
}

void DirectorySourceTest::extractsLocalMarkdownLinkAnchors()
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
        return resource.kind == ResourceKind::Markdown && resource.title == QLatin1String("runbook.md");
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
            && relation.note == QLatin1String("markdown line 3: url: External -> https://docs.example.com/spec");
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
            && relation.note == QLatin1String("markdown line 3: url: External -> https://docs.example.com/spec");
    }));

    const QList<SearchResult> linkResults = repository.search(SearchQuery{QStringLiteral("Spec PDF")});
    QVERIFY(std::any_of(linkResults.cbegin(), linkResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("runbook.md")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 2;
    }));
}

void DirectorySourceTest::extractsMarkdownTaskLineAnchors()
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
        return resource.kind == ResourceKind::Markdown && resource.title == QLatin1String("tasks.md");
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

void DirectorySourceTest::extractsMarkdownLinkResources()
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
                         "```text\n"
                         "[Code Link](https://ignored.example.com/code)\n"
                         "```\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto markdownIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Markdown
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
    QVERIFY(guideIt->tags.contains(QStringLiteral("markdown-link")));
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
    QCOMPARE(markdownIt->relations.size(), 2);
    QVERIFY(std::any_of(markdownIt->relations.cbegin(), markdownIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == markdownIt->id
            && relation.targetResourceId == guideIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("markdown line 5: url: ZeroSlack Dock Guide -> https://docs.example.com/zeroslack/dock#handoff");
    }));
    QVERIFY(std::any_of(markdownIt->relations.cbegin(), markdownIt->relations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == markdownIt->id
            && relation.targetResourceId == statusIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("markdown line 7: url: status.example.com -> https://status.example.com/system");
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
    QCOMPARE(markdownRelations.size(), 2);
    QVERIFY(std::any_of(markdownRelations.cbegin(), markdownRelations.cend(), [&](const ResourceRelation &relation) {
        return relation.sourceResourceId == markdownIt->id
            && relation.targetResourceId == guideIt->id
            && relation.label == QLatin1String("links-to")
            && relation.note == QLatin1String("markdown line 5: url: ZeroSlack Dock Guide -> https://docs.example.com/zeroslack/dock#handoff");
    }));

    const QList<ResourceRelation> guideRelations = repository.resourceRelations(guideIt->id);
    QCOMPARE(guideRelations.size(), 1);
    QCOMPARE(guideRelations.first().sourceResourceId, markdownIt->id);
    QCOMPARE(guideRelations.first().targetResourceId, guideIt->id);

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("ZeroSlack Dock Guide")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::Markdown
            && result.resource.title == QLatin1String("runbook.md")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 5
            && result.matchedAnchor->target == QLatin1String("url: ZeroSlack Dock Guide -> https://docs.example.com/zeroslack/dock#handoff");
    }));
}

void DirectorySourceTest::extractsMarkdownReferenceLinkResources()
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
        return resource.kind == ResourceKind::Markdown
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
    QVERIFY(hostIt->tags.contains(QStringLiteral("markdown-link")));
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
             QStringLiteral("markdown line 2: url: Host API -> https://docs.example.com/pinloom/host#context"));

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
        return result.resource.kind == ResourceKind::Markdown
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
                         "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n"
                         "2 0 obj << /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >> endobj\n"
                         "3 0 obj << /Type /Page /Parent 2 0 R >> endobj\n"
                         "4 0 obj << /Type /Page /Parent 2 0 R >> endobj\n"
                         "5 0 obj << /Title (PCIe Debug Spec) >> endobj\n"
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
    QCOMPARE(pdfIt->anchors.size(), 2);
    QCOMPARE(pdfIt->anchors.at(0).type, AnchorType::PdfPage);
    QCOMPARE(pdfIt->anchors.at(0).target, QStringLiteral("Page 1"));
    QCOMPARE(pdfIt->anchors.at(0).page, 1);
    QCOMPARE(pdfIt->anchors.at(1).target, QStringLiteral("Page 2"));
    QCOMPARE(pdfIt->anchors.at(1).page, 2);

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
                         "3 0 obj << /Type /Page /Parent 2 0 R /Annots [4 0 R] >> endobj\n"
                         "4 0 obj << /Type /Annot /Subtype /Highlight /Rect [10 20 110 60] /Contents (Clock domain note) >> endobj\n"
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
}

void DirectorySourceTest::extractsCodeSymbolAnchors()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/src")));
    writeFile(dir.filePath(QStringLiteral("library/src/pinloom.cpp")),
              QByteArray("class JumpController {\n"
                         "};\n"
                         "\n"
                         "void openTarget()\n"
                         "{\n"
                         "}\n"));
    writeFile(dir.filePath(QStringLiteral("library/src/tools.py")),
              QByteArray("def parse_note():\n"
                         "    pass\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto cppIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::CodeSnippet && resource.title == QLatin1String("pinloom.cpp");
    });
    QVERIFY(cppIt != resources.cend());
    QVERIFY(std::any_of(cppIt->anchors.cbegin(), cppIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::CodeSymbol
            && anchor.target == QLatin1String("JumpController")
            && anchor.line == 1;
    }));
    QVERIFY(std::any_of(cppIt->anchors.cbegin(), cppIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::CodeSymbol
            && anchor.target == QLatin1String("openTarget")
            && anchor.line == 4;
    }));

    auto pythonIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::CodeSnippet && resource.title == QLatin1String("tools.py");
    });
    QVERIFY(pythonIt != resources.cend());
    QVERIFY(std::any_of(pythonIt->anchors.cbegin(), pythonIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::CodeSymbol
            && anchor.target == QLatin1String("parse_note")
            && anchor.line == 1;
    }));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> symbolResults = repository.search(SearchQuery{QStringLiteral("JumpController")});
    QCOMPARE(symbolResults.size(), 1);
    QVERIFY(symbolResults.first().matchedAnchor.has_value());
    QCOMPARE(symbolResults.first().matchedAnchor->type, AnchorType::CodeSymbol);
    QCOMPARE(symbolResults.first().matchedAnchor->line, 1);

    const QList<SearchResult> pythonResults = repository.search(SearchQuery{QStringLiteral("parse_note")});
    QCOMPARE(pythonResults.size(), 1);
    QVERIFY(pythonResults.first().matchedAnchor.has_value());
    QCOMPARE(pythonResults.first().matchedAnchor->type, AnchorType::CodeSymbol);
}

void DirectorySourceTest::extractsAdditionalLanguageSymbolAnchors()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/src")));
    writeFile(dir.filePath(QStringLiteral("library/src/bridge.rs")),
              QByteArray("pub struct BridgeController {}\n"
                         "pub async fn open_target() {}\n"));
    writeFile(dir.filePath(QStringLiteral("library/src/server.go")),
              QByteArray("type DockServer struct {}\n"
                         "func (s *DockServer) StartRelay() error { return nil }\n"));
    writeFile(dir.filePath(QStringLiteral("library/src/HostBridge.java")),
              QByteArray("public class HostBridge {\n"
                         "    public void attachDock() {}\n"
                         "}\n"));
    writeFile(dir.filePath(QStringLiteral("library/src/DockHost.cs")),
              QByteArray("public sealed class DockHost {\n"
                         "    public void AttachDock() {}\n"
                         "}\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto findCode = [&](const QString &title) {
        return std::find_if(resources.cbegin(), resources.cend(), [&](const Resource &resource) {
            return resource.kind == ResourceKind::CodeSnippet && resource.title == title;
        });
    };
    auto hasSymbol = [](const Resource &resource, const QString &symbol, int line) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::CodeSymbol
                && anchor.target == symbol
                && anchor.line == line;
        });
    };

    const auto rustIt = findCode(QStringLiteral("bridge.rs"));
    QVERIFY(rustIt != resources.cend());
    QVERIFY(hasSymbol(*rustIt, QStringLiteral("BridgeController"), 1));
    QVERIFY(hasSymbol(*rustIt, QStringLiteral("open_target"), 2));

    const auto goIt = findCode(QStringLiteral("server.go"));
    QVERIFY(goIt != resources.cend());
    QVERIFY(hasSymbol(*goIt, QStringLiteral("DockServer"), 1));
    QVERIFY(hasSymbol(*goIt, QStringLiteral("StartRelay"), 2));

    const auto javaIt = findCode(QStringLiteral("HostBridge.java"));
    QVERIFY(javaIt != resources.cend());
    QVERIFY(hasSymbol(*javaIt, QStringLiteral("HostBridge"), 1));
    QVERIFY(hasSymbol(*javaIt, QStringLiteral("attachDock"), 2));

    const auto csharpIt = findCode(QStringLiteral("DockHost.cs"));
    QVERIFY(csharpIt != resources.cend());
    QVERIFY(hasSymbol(*csharpIt, QStringLiteral("DockHost"), 1));
    QVERIFY(hasSymbol(*csharpIt, QStringLiteral("AttachDock"), 2));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> rustResults = repository.search(SearchQuery{QStringLiteral("BridgeController")});
    QCOMPARE(rustResults.size(), 1);
    QCOMPARE(rustResults.first().resource.kind, ResourceKind::CodeSnippet);
    QVERIFY(rustResults.first().matchedAnchor.has_value());
    QCOMPARE(rustResults.first().matchedAnchor->type, AnchorType::CodeSymbol);

    const QList<SearchResult> goResults = repository.search(SearchQuery{QStringLiteral("StartRelay")});
    QCOMPARE(goResults.size(), 1);
    QCOMPARE(goResults.first().resource.kind, ResourceKind::CodeSnippet);
    QVERIFY(goResults.first().matchedAnchor.has_value());
    QCOMPARE(goResults.first().matchedAnchor->type, AnchorType::CodeSymbol);
}

void DirectorySourceTest::extractsCodeTestCaseAnchors()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/tests")));
    writeFile(dir.filePath(QStringLiteral("library/tests/pinloom_test.cpp")),
              QByteArray("TEST(PinloomLocator, OpensSelectedAnchor)\n"
                         "{\n"
                         "}\n"
                         "TEST_F(PinloomPanelTest, RecordsActivation)\n"
                         "{\n"
                         "}\n"));
    writeFile(dir.filePath(QStringLiteral("library/tests/panel.test.ts")),
              QByteArray("describe(\"Pinloom panel\", () => {\n"
                         "  it('renders dock handoff state', () => {});\n"
                         "  test(`records activation metrics`, () => {});\n"
                         "});\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto findCode = [&](const QString &title) {
        return std::find_if(resources.cbegin(), resources.cend(), [&](const Resource &resource) {
            return resource.kind == ResourceKind::CodeSnippet && resource.title == title;
        });
    };
    auto hasSymbol = [](const Resource &resource, const QString &symbol, int line) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::CodeSymbol
                && anchor.target == symbol
                && anchor.line == line;
        });
    };

    const auto cppIt = findCode(QStringLiteral("pinloom_test.cpp"));
    QVERIFY(cppIt != resources.cend());
    QVERIFY(hasSymbol(*cppIt, QStringLiteral("PinloomLocator.OpensSelectedAnchor"), 1));
    QVERIFY(hasSymbol(*cppIt, QStringLiteral("PinloomPanelTest.RecordsActivation"), 4));

    const auto tsIt = findCode(QStringLiteral("panel.test.ts"));
    QVERIFY(tsIt != resources.cend());
    QVERIFY(hasSymbol(*tsIt, QStringLiteral("suite: Pinloom panel"), 1));
    QVERIFY(hasSymbol(*tsIt, QStringLiteral("test: renders dock handoff state"), 2));
    QVERIFY(hasSymbol(*tsIt, QStringLiteral("test: records activation metrics"), 3));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> cppResults = repository.search(SearchQuery{QStringLiteral("OpensSelectedAnchor")});
    QCOMPARE(cppResults.size(), 1);
    QCOMPARE(cppResults.first().resource.kind, ResourceKind::CodeSnippet);
    QVERIFY(cppResults.first().matchedAnchor.has_value());
    QCOMPARE(cppResults.first().matchedAnchor->type, AnchorType::CodeSymbol);
    QCOMPARE(cppResults.first().matchedAnchor->target, QStringLiteral("PinloomLocator.OpensSelectedAnchor"));

    const QList<SearchResult> jsResults = repository.search(SearchQuery{QStringLiteral("handoff")});
    QCOMPARE(jsResults.size(), 1);
    QCOMPARE(jsResults.first().resource.kind, ResourceKind::CodeSnippet);
    QVERIFY(jsResults.first().matchedAnchor.has_value());
    QCOMPARE(jsResults.first().matchedAnchor->type, AnchorType::CodeSymbol);
    QCOMPARE(jsResults.first().matchedAnchor->target, QStringLiteral("test: renders dock handoff state"));
}

void DirectorySourceTest::extractsCodeDependencyLineAnchors()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/src")));
    writeFile(dir.filePath(QStringLiteral("library/src/pinloom.cpp")),
              QByteArray("#include \"pinloom/core/Resource.h\"\n"
                         "class JumpController {};\n"));
    writeFile(dir.filePath(QStringLiteral("library/src/tools.py")),
              QByteArray("from pinloom.core import Resource\n"
                         "import pathlib, json\n"));
    writeFile(dir.filePath(QStringLiteral("library/src/web.ts")),
              QByteArray("import type { PinloomOpenTarget } from \"./pinloom\";\n"
                         "const bridge = require(\"@zeroslack/dock\");\n"
                         "const panel = await import(\"./lazy-panel\");\n"));
    writeFile(dir.filePath(QStringLiteral("library/src/bridge.rs")),
              QByteArray("use crate::dock::HostBridge;\n"
                         "pub fn open_target() {}\n"));
    writeFile(dir.filePath(QStringLiteral("library/src/server.go")),
              QByteArray("import (\n"
                         "    \"context\"\n"
                         "    http \"net/http\"\n"
                         ")\n"
                         "func StartRelay() {}\n"));
    writeFile(dir.filePath(QStringLiteral("library/src/HostBridge.java")),
              QByteArray("import com.example.pinloom.Dock;\n"
                         "public class HostBridge {}\n"));
    writeFile(dir.filePath(QStringLiteral("library/src/DockHost.cs")),
              QByteArray("using ZeroSlack.Dock;\n"
                         "public sealed class DockHost {}\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto findCode = [&](const QString &title) {
        return std::find_if(resources.cbegin(), resources.cend(), [&](const Resource &resource) {
            return resource.kind == ResourceKind::CodeSnippet && resource.title == title;
        });
    };
    auto hasLineAnchor = [](const Resource &resource, const QString &target, int line) {
        return std::any_of(resource.anchors.cbegin(), resource.anchors.cend(), [&](const Anchor &anchor) {
            return anchor.type == AnchorType::FileLine
                && anchor.target == target
                && anchor.line == line;
        });
    };

    const auto cppIt = findCode(QStringLiteral("pinloom.cpp"));
    QVERIFY(cppIt != resources.cend());
    QVERIFY(hasLineAnchor(*cppIt, QStringLiteral("include: pinloom/core/Resource.h"), 1));

    const auto pythonIt = findCode(QStringLiteral("tools.py"));
    QVERIFY(pythonIt != resources.cend());
    QVERIFY(hasLineAnchor(*pythonIt, QStringLiteral("import: pinloom.core"), 1));
    QVERIFY(hasLineAnchor(*pythonIt, QStringLiteral("import: pathlib, json"), 2));

    const auto tsIt = findCode(QStringLiteral("web.ts"));
    QVERIFY(tsIt != resources.cend());
    QVERIFY(hasLineAnchor(*tsIt, QStringLiteral("import: ./pinloom"), 1));
    QVERIFY(hasLineAnchor(*tsIt, QStringLiteral("require: @zeroslack/dock"), 2));
    QVERIFY(hasLineAnchor(*tsIt, QStringLiteral("import: ./lazy-panel"), 3));

    const auto rustIt = findCode(QStringLiteral("bridge.rs"));
    QVERIFY(rustIt != resources.cend());
    QVERIFY(hasLineAnchor(*rustIt, QStringLiteral("use: crate::dock::HostBridge"), 1));

    const auto goIt = findCode(QStringLiteral("server.go"));
    QVERIFY(goIt != resources.cend());
    QVERIFY(hasLineAnchor(*goIt, QStringLiteral("import: context"), 2));
    QVERIFY(hasLineAnchor(*goIt, QStringLiteral("import: net/http"), 3));

    const auto javaIt = findCode(QStringLiteral("HostBridge.java"));
    QVERIFY(javaIt != resources.cend());
    QVERIFY(hasLineAnchor(*javaIt, QStringLiteral("import: com.example.pinloom.Dock"), 1));

    const auto csharpIt = findCode(QStringLiteral("DockHost.cs"));
    QVERIFY(csharpIt != resources.cend());
    QVERIFY(hasLineAnchor(*csharpIt, QStringLiteral("using: ZeroSlack.Dock"), 1));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> includeResults = repository.search(SearchQuery{QStringLiteral("Resource")});
    QVERIFY(std::any_of(includeResults.cbegin(), includeResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("pinloom.cpp")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("include: pinloom/core/Resource.h");
    }));

    const QList<SearchResult> requireResults = repository.search(SearchQuery{QStringLiteral("zeroslack")});
    QVERIFY(std::any_of(requireResults.cbegin(), requireResults.cend(), [](const SearchResult &result) {
        return result.resource.title == QLatin1String("web.ts")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->target == QLatin1String("require: @zeroslack/dock");
    }));
}

void DirectorySourceTest::extractsCodeCommentLineAnchors()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/src")));
    writeFile(dir.filePath(QStringLiteral("library/src/pinloom.cpp")),
              QByteArray("// TODO: wire ZeroSlack dock\n"
                         "class JumpController {};\n"
                         "int main() { return 0; } // FIXME: remove blocking wait\n"));
    writeFile(dir.filePath(QStringLiteral("library/src/tools.py")),
              QByteArray("# NOTE: cache active project\n"
                         "def parse_note():\n"
                         "    pass\n"));

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto cppIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::CodeSnippet && resource.title == QLatin1String("pinloom.cpp");
    });
    QVERIFY(cppIt != resources.cend());
    QVERIFY(std::any_of(cppIt->anchors.cbegin(), cppIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("TODO: wire ZeroSlack dock")
            && anchor.line == 1;
    }));
    QVERIFY(std::any_of(cppIt->anchors.cbegin(), cppIt->anchors.cend(), [](const Anchor &anchor) {
        return anchor.type == AnchorType::FileLine
            && anchor.target == QLatin1String("FIXME: remove blocking wait")
            && anchor.line == 3;
    }));

    auto pythonIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::CodeSnippet && resource.title == QLatin1String("tools.py");
    });
    QVERIFY(pythonIt != resources.cend());
    QVERIFY(std::any_of(pythonIt->anchors.cbegin(), pythonIt->anchors.cend(), [](const Anchor &anchor) {
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
    QCOMPARE(todoResults.size(), 1);
    QVERIFY(todoResults.first().matchedAnchor.has_value());
    QCOMPARE(todoResults.first().matchedAnchor->type, AnchorType::FileLine);
    QCOMPARE(todoResults.first().matchedAnchor->line, 1);

    const QList<SearchResult> fixmeResults = repository.search(SearchQuery{QStringLiteral("blocking wait")});
    QCOMPARE(fixmeResults.size(), 1);
    QVERIFY(fixmeResults.first().matchedAnchor.has_value());
    QCOMPARE(fixmeResults.first().matchedAnchor->type, AnchorType::FileLine);
    QCOMPARE(fixmeResults.first().matchedAnchor->line, 3);
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

    DirectoryLibrarySource source(dir.filePath(QStringLiteral("library")));
    QString error;
    const QList<Resource> resources = source.scan(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    auto urlIt = std::find_if(resources.cbegin(), resources.cend(), [](const Resource &resource) {
        return resource.kind == ResourceKind::Url;
    });
    QVERIFY(urlIt != resources.cend());
    QCOMPARE(urlIt->title, QStringLiteral("Pinloom Setup Guide"));
    QCOMPARE(urlIt->location, QStringLiteral("https://docs.example.com/pinloom/setup#install"));
    QVERIFY(urlIt->tags.contains(QStringLiteral("web")));
    QVERIFY(urlIt->aliases.contains(QStringLiteral("docs.example.com")));
    QVERIFY(urlIt->aliases.contains(QStringLiteral("https://docs.example.com/pinloom/setup#install")));
    QCOMPARE(urlIt->anchors.size(), 1);
    QCOMPARE(urlIt->anchors.first().type, AnchorType::UrlFragment);
    QCOMPARE(urlIt->anchors.first().target, QStringLiteral("install"));

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> hostResults = repository.search(SearchQuery{QStringLiteral("docs.example.com")});
    QCOMPARE(hostResults.size(), 1);
    QCOMPARE(hostResults.first().resource.kind, ResourceKind::Url);

    const QList<SearchResult> fragmentResults = repository.search(SearchQuery{QStringLiteral("install")});
    QVERIFY(!fragmentResults.isEmpty());
    QVERIFY(std::any_of(fragmentResults.cbegin(), fragmentResults.cend(), [](const SearchResult &result) {
        return result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("install");
    }));
}

void DirectorySourceTest::extractsPlainTextUrlListResources()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/links")));
    writeFile(dir.filePath(QStringLiteral("library/links/research.urls")),
              QByteArray("Pinloom Launch Notes - https://docs.example.com/pinloom/launch#overview\n"
                         "https://status.example.org/zeroslack\n"
                         "Duplicate: https://docs.example.com/pinloom/launch#overview\n"
                         "Ignore local file://not-web\n"));

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
    QCOMPARE(fileIt->relations.size(), 2);
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
    QCOMPARE(fileRelations.size(), 2);
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

    const QList<SearchResult> sourceLineResults = repository.search(SearchQuery{QStringLiteral("Launch Notes")});
    QVERIFY(std::any_of(sourceLineResults.cbegin(), sourceLineResults.cend(), [](const SearchResult &result) {
        return result.resource.kind == ResourceKind::File
            && result.resource.title == QLatin1String("research.urls")
            && result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::FileLine
            && result.matchedAnchor->line == 1
            && result.matchedAnchor->target == QLatin1String("url: Pinloom Launch Notes -> https://docs.example.com/pinloom/launch#overview");
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

void DirectorySourceTest::extractsStructuredTextUrlResources()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QDir dir(temp.path());
    QVERIFY(dir.mkpath(QStringLiteral("library/config")));
    writeFile(dir.filePath(QStringLiteral("library/config/workspace.yml")),
              QByteArray("pinloom:\n"
                         "  docs_url: https://docs.example.com/pinloom/workspace#roots\n"
                         "  support:\n"
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

    SqliteLibraryRepository repository;
    QVERIFY2(repository.open(dir.filePath(QStringLiteral("pinloom.sqlite3"))),
             qPrintable(repository.lastError()));
    QVERIFY2(repository.initialize(), qPrintable(repository.lastError()));

    IndexingService indexer(repository);
    QVERIFY2(indexer.index(source), qPrintable(indexer.lastError()));

    const QList<SearchResult> contentResults = repository.search(SearchQuery{QStringLiteral("browser launch")});
    QCOMPARE(contentResults.size(), 1);
    QCOMPARE(contentResults.first().resource.kind, ResourceKind::Url);
    QCOMPARE(contentResults.first().matchedField, QStringLiteral("content"));

    const QList<SearchResult> anchorResults = repository.search(SearchQuery{QStringLiteral("install")});
    QVERIFY(!anchorResults.isEmpty());
    QVERIFY(std::any_of(anchorResults.cbegin(), anchorResults.cend(), [](const SearchResult &result) {
        return result.matchedAnchor.has_value()
            && result.matchedAnchor->type == AnchorType::UrlFragment
            && result.matchedAnchor->target == QLatin1String("install");
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
