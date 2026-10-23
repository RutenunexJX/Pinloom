#include "pinloom/application/NativePdfCapture.h"
#include "pinloom/core/SqliteLibraryRepository.h"
#include "pinloom/clip/ClipRepository.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <stdexcept>

using namespace Pinloom;
namespace {
void require(bool ok, const QString &reason)
{
    if (!ok) throw std::runtime_error(reason.toStdString());
}
void writeJson(const QString &path, const QJsonObject &object)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), file.errorString());
    file.write(QJsonDocument(object).toJson());
}
QJsonObject parameters(const QString &pdf)
{
    QFileInfo file(pdf);
    return {{"captureProtocol", "pinloom-pdf-capture/v1"}, {"captureId", "f0000000-0000-4000-8000-000000000001"},
        {"ifVersion", "absent"}, {"name", "Native capture benchmark seed"}, {"tags", QJsonArray{"benchmark", "original"}},
        {"targetFile", file.canonicalFilePath()}, {"documentSize", QString::number(file.size())},
        {"documentModifiedMs", QString::number(file.lastModified().toMSecsSinceEpoch())},
        {"locator", QJsonObject{{"type", "sumatrapdf.rect"}, {"version", 3}, {"unit", "pt"},
            {"coordinateSpace", "page-top-left"}, {"page", 1}, {"rect", QJsonArray{110,170,270,265}},
            {"mediaBox", QJsonArray{0,0,600,800}}, {"cropBox", QJsonArray{0,0,600,800}}, {"rotation",0}, {"userUnit",1}}}};
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addOption({"library", "Isolated benchmark library", "path"});
    parser.addOption({"seed", "Create a new synthetic library"});
    parser.addOption({"count", "Synthetic resource count", "number", "1000"});
    parser.addOption({"samples", "Measured repetitions", "number", "9"});
    parser.addOption({"pdf", "Owned PDF fixture", "path"});
    parser.addOption({"output", "Result JSON", "path"});
    parser.process(app);
    try {
        const QString directory = QFileInfo(parser.value("library")).absoluteFilePath();
        require(!parser.value("library").isEmpty(), "Explicit library required");
        require(QDir().mkpath(directory), "Cannot create benchmark directory");
        QElapsedTimer openTimer; openTimer.start();
        SqliteLibraryRepository library;
        require(library.open(QDir(directory).filePath("pinloom.sqlite3"), QDir(directory).filePath("pinloom_identity.sqlite3")), library.lastError());
        require(library.initialize(), library.lastError());
        const double openMs = openTimer.nsecsElapsed() / 1e6;
        const QString templatePath = QDir(directory).filePath("capture-template.json");
        if (parser.isSet("seed")) {
            SearchQuery existing; existing.limit = 1; existing.includeDeleted = true;
            require(library.search(existing).isEmpty(), "Seed requires an empty library");
            const int count = parser.value("count").toInt();
            require(count >= 1 && QFileInfo(parser.value("pdf")).isFile(), "Invalid count or PDF fixture");
            LibraryBatchMutation batch;
            for (int i = 0; i < count; ++i) {
                Resource resource;
                resource.id = QString("benchmark-%1").arg(i, 6, 10, QLatin1Char('0'));
                resource.kind = ResourceKind::Pdf;
                resource.title = QString("Synthetic document %1").arg(i);
                resource.location = i == count-1 ? QFileInfo(parser.value("pdf")).canonicalFilePath()
                    : QDir(directory).filePath(QString("documents/%1.pdf").arg(i));
                resource.aliases = {QString("Document alias %1").arg(i)};
                resource.tags = {"benchmark", QString("group-%1").arg(i % 20)};
                resource.content = QString("Preserved searchable body %1 ").arg(i).repeated(12);
                resource.explicitlyRetained = true;
                resource.updatedAt = QDateTime::fromString("2026-10-22T00:00:00Z", Qt::ISODate);
                for (int j = 0; j < 3; ++j) {
                    Anchor anchor;
                    anchor.id = QString("%1-anchor-%2").arg(resource.id).arg(j);
                    anchor.name = QString("Synthetic point %1 %2").arg(i).arg(j);
                    anchor.targetApp = "SumatraPDF"; anchor.targetFile = resource.location;
                    anchor.locatorType = "pdf.page"; anchor.locatorJson = "{\"type\":\"pdf.page\",\"page\":1}";
                    anchor.tags = {"benchmark"}; anchor.createdAt = anchor.updatedAt = resource.updatedAt;
                    resource.anchors.append(anchor);
                }
                batch.upserts.append(resource);
            }
            require(library.applyBatch(batch), library.lastError());
            auto params = parameters(parser.value("pdf"));
            auto prepared = nativePdfCaptureDraft(params);
            require(prepared.error.isEmpty(), prepared.error);
            auto saved = AnchorCaptureCommitService(library).commit(prepared.draft, params["captureId"].toString());
            require(saved.success(), saved.error);
            params["resourceId"] = saved.resource.id;
            params["resourceCount"] = count;
            writeJson(templatePath, params);
            SqliteClipRepository clips;
            require(clips.open(QDir(directory).filePath("pinloom_clip.sqlite3"), QDir(directory).filePath("pinloom_identity.sqlite3")), clips.lastError());
            require(clips.initialize(), clips.lastError());
            std::puts("Synthetic library seeded");
            return 0;
        }
        QFile source(templatePath); require(source.open(QIODevice::ReadOnly), source.errorString());
        const auto params = QJsonDocument::fromJson(source.readAll()).object();
        auto prepared = nativePdfCaptureDraft(params); require(prepared.error.isEmpty(), prepared.error);
        AnchorCaptureCommitService service(library);
        const QString captureId = params["captureId"].toString(), resourceId = params["resourceId"].toString();
        const auto original = library.findResource(resourceId); require(original.has_value(), "Seed target missing");
        QJsonArray samples;
        const int count = parser.value("samples").toInt();
        for (int sample = -1; sample < count; ++sample) {
            const auto measure = [&](const QString &name, auto work) {
                QElapsedTimer timer; timer.start(); work();
                const double ms = timer.nsecsElapsed() / 1e6;
                if (sample >= 0) samples.append(QJsonObject{{"sample",sample},{"operation",name},{"milliseconds",ms}});
            };
            measure("capture_status_hit", [&] { auto r=service.findCapture(captureId); require(r.success() && r.resource.id==resourceId, "Receipt hit mismatch"); });
            measure("capture_status_miss", [&] { require(service.findCapture("f0000000-0000-4000-8000-000000000002").errorCode=="not_found", "Receipt miss mismatch"); });
            measure("capture_replay", [&] { auto r=service.commit(prepared.draft,captureId); require(r.success() && r.replayed, "Replay mismatch"); });
            measure("search_empty_50", [&] { SearchQuery q; q.limit=50; require(!library.search(q).isEmpty(), "Empty search failed"); });
            measure("search_text_50", [&] { SearchQuery q; q.limit=50; q.text="Synthetic"; require(!library.search(q).isEmpty(), "Text search failed"); });
            auto draft=prepared.draft; draft.suggestedName=QString("Measured save %1").arg(sample);
            const auto id=QString("e0000000-0000-4000-8000-%1").arg(sample+100,12,10,QLatin1Char('0'));
            measure("capture_new_same_file", [&] { auto r=service.commit(draft,id); require(r.success() && !r.replayed && r.resource.id==resourceId, r.error); });
            const auto saved=library.findResource(resourceId);
            require(saved && saved->content==original->content && saved->aliases==original->aliases && saved->tags==original->tags, "Capture changed existing metadata or indexed content");
        }
        SqliteSearchReadCounts reads; SearchQuery all; all.limit=0; all.includeDeleted=true;
        const auto allResults=library.search(all,&reads);
        QJsonObject result{{"resourceCount",params["resourceCount"]},{"anchorsPerResource",3},{"openInitializeMs",openMs},
            {"warmupRepetitions",1},{"samples",samples},{"emptyFullSearchHydrations",int(reads.resourceHydrations)},
            {"emptyFullSearchUsageReads",int(reads.resourceUsageReads)},{"fullSearchResults",allResults.size()},
            {"method","Synchronous domain/SQLite call wall time; fixture preparation, process startup and GUI repaint excluded"},
            {"passed",true}};
        require(!parser.value("output").isEmpty(), "Explicit output required");
        writeJson(parser.value("output"),result);
        std::puts("Capture benchmark completed with behavior checks");
    } catch (const std::exception &error) { std::fprintf(stderr,"%s\n",error.what()); return 1; }
    return 0;
}
