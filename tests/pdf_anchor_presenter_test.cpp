#include "pinloom/core/InMemoryLibraryRepository.h"
#include "pinloom/widgets/PdfAnchorPresenter.h"
#include "pinloom/widgets/PinloomOpenService.h"
#include "pinloom/widgets/SumatraPdfViewerAdapter.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

using namespace Pinloom;

namespace {

QByteArray onePagePdf()
{
    const QVector<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] >>",
    };
    QByteArray pdf("%PDF-1.7\n");
    QVector<qint64> offsets(objects.size() + 1, 0);
    for (int index = 0; index < objects.size(); ++index) {
        offsets[index + 1] = pdf.size();
        pdf.append(QByteArray::number(index + 1) + " 0 obj\n"
                   + objects.at(index) + "\nendobj\n");
    }
    const qint64 xref = pdf.size();
    pdf.append("xref\n0 4\n0000000000 65535 f \n");
    for (int index = 1; index <= 3; ++index) {
        pdf.append(QByteArray::number(offsets.at(index)).rightJustified(10, '0')
                   + " 00000 n \n");
    }
    pdf.append("trailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n"
               + QByteArray::number(xref) + "\n%%EOF\n");
    return pdf;
}

bool writePdf(const QString &path)
{
    QFile file(path);
    const QByteArray data = onePagePdf();
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

PdfAnchorPresentationRequest presentationRequest(const QString &source,
                                                 const QString &cache,
                                                 const QString &anchorId,
                                                 const QRectF &rect)
{
    PdfAnchorPresentationRequest request;
    request.anchor.id = anchorId;
    request.anchor.name = QStringLiteral("Region %1").arg(anchorId);
    request.anchor.targetApp = QStringLiteral("SumatraPDF");
    request.anchor.targetFile = source;
    request.anchor.locatorType = QStringLiteral("sumatrapdf.rect");
    request.anchor.locatorJson = QStringLiteral(
        R"({"type":"sumatrapdf.rect","version":2,"page":1,"rect":[%1,%2,%3,%4],"coordinateSpace":"page-top-left"})")
                                     .arg(rect.left())
                                     .arg(rect.top())
                                     .arg(rect.right())
                                     .arg(rect.bottom());
    request.sourceTitle = QFileInfo(source).fileName();
    request.cacheDirectory = cache;
    request.sourceCommand.executablePath = QStringLiteral("C:/Tools/SumatraPDF.exe");
    request.sourceCommand.filePath = source;
    request.sourceCommand.arguments = {
        QStringLiteral("-reuse-instance"), QStringLiteral("-page"),
        QStringLiteral("1"), QStringLiteral("-zoom"), QStringLiteral("250"),
        QStringLiteral("-scroll"), QStringLiteral("10,20"), source,
    };
    request.sourceCommand.page = 1;
    request.sourceCommand.zoom = 250;
    request.sourceCommand.highlightRect = rect;
    return request;
}

class FailingPresenter final : public PdfAnchorPresenter {
public:
    using PdfAnchorPresenter::PdfAnchorPresenter;

    PdfAnchorPresentationStartResult present(
        const PdfAnchorPresentationRequest &request,
        PdfAnchorPresentationCallbacks callbacks) override
    {
        lastRequest = request;
        active = 1;
        PdfAnchorPresentationStartResult start;
        start.requestId = ++lastId;
        active = 0;
        PdfAnchorPresentationResult result;
        result.requestId = start.requestId;
        result.sourceFilePath = request.sourceCommand.filePath;
        result.error = QStringLiteral("simulated annotation failure");
        if (callbacks.completed) callbacks.completed(result);
        return start;
    }

    void cancelPending() override { active = 0; }
    int activeRequestCount() const override { return active; }

    PdfAnchorPresentationRequest lastRequest;
    quint64 lastId = 0;
    int active = 0;
};

} // namespace

class PdfAnchorPresenterTest final : public QObject {
    Q_OBJECT

private slots:
    void generatesAndLaunchesPageOnlyPreviewWithoutBlocking();
    void timeoutReleasesHungVerification();
    void newerRequestSupersedesOldResultWithoutLaunchingIt();
    void cancellationClosesPendingPresentationWithoutLaunching();
    void isolatesSequentialAnchorsAndCacheKeys();
    void openServiceOffersExplicitOriginalPdfFallback();
};

void PdfAnchorPresenterTest::generatesAndLaunchesPageOnlyPreviewWithoutBlocking()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source with 空格.pdf"));
    QVERIFY(writePdf(source));
    const QByteArray sourceHash = QCryptographicHash::hash(
        onePagePdf(), QCryptographicHash::Sha256);

    SumatraPdfCommand launched;
    SumatraAnnotatedCopyPresenterOptions options;
    options.launchHandler = [&launched](const SumatraPdfCommand &command, QString *) {
        launched = command;
        return true;
    };
    SumatraAnnotatedCopyPresenter presenter(options);
    bool completed = false;
    PdfAnchorPresentationResult result;
    qint64 timerElapsed = -1;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer::singleShot(20, &presenter, [&]() { timerElapsed = elapsed.elapsed(); });
    PdfAnchorPresentationCallbacks callbacks;
    callbacks.completed = [&](const PdfAnchorPresentationResult &value) {
        completed = true;
        result = value;
    };
    const PdfAnchorPresentationStartResult start = presenter.present(
        presentationRequest(source, directory.filePath(QStringLiteral("cache")),
                            QStringLiteral("a"), QRectF(10, 20, 80, 40)),
        callbacks);
    QVERIFY2(start.accepted(), qPrintable(start.error));
    QVERIFY(elapsed.elapsed() < 100);
    QTRY_VERIFY_WITH_TIMEOUT(timerElapsed >= 0, 1000);
    QVERIFY(timerElapsed < 250);
    QTRY_VERIFY_WITH_TIMEOUT(completed, 3000);
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(presenter.activeRequestCount(), 0);
    QVERIFY(launched.filePath != source);
    QVERIFY(launched.filePath.contains(QStringLiteral("Pinloom Preview")));
    QCOMPARE(launched.arguments,
             QStringList({QStringLiteral("-reuse-instance"),
                          QStringLiteral("-page"),
                          QStringLiteral("1"),
                          launched.filePath}));
    QVERIFY(!launched.arguments.contains(QStringLiteral("-scroll")));
    QVERIFY(!launched.arguments.contains(QStringLiteral("-zoom")));
    QVERIFY(!launched.highlightRect.isValid());
    QCOMPARE(launched.zoom, -1.0);
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(sourceFile.readAll(), QCryptographicHash::Sha256),
             sourceHash);
}

void PdfAnchorPresenterTest::timeoutReleasesHungVerification()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("timeout.pdf"));
    QVERIFY(writePdf(source));
    SumatraAnnotatedCopyPresenterOptions options;
    options.launchHandler = [](const SumatraPdfCommand &, QString *) { return true; };
    options.stateProvider = [](int) {
        QThread::msleep(1200);
        SumatraPdfDdeFileState state;
        state.error = QStringLiteral("simulated blocked DDE request");
        return state;
    };
    options.stateProviderRunsInWorker = true;
    options.verificationTimeoutMilliseconds = 400;
    options.verificationPollMilliseconds = 50;
    options.generationTimeoutMilliseconds = 5000;
    SumatraAnnotatedCopyPresenter presenter(options);
    PdfAnchorPresentationResult result;
    bool completed = false;
    PdfAnchorPresentationCallbacks callbacks;
    callbacks.completed = [&](const PdfAnchorPresentationResult &value) {
        result = value;
        completed = true;
    };
    QElapsedTimer elapsed;
    elapsed.start();
    QVERIFY(presenter.present(
        presentationRequest(source, directory.filePath(QStringLiteral("cache")),
                            QStringLiteral("timeout"), QRectF(10, 20, 80, 40)),
        callbacks).accepted());
    QTRY_VERIFY_WITH_TIMEOUT(completed, 1200);
    QVERIFY(elapsed.elapsed() < 1000);
    QVERIFY(!result.success());
    QVERIFY(result.error.contains(QStringLiteral("navigation failed"), Qt::CaseInsensitive));
    QCOMPARE(presenter.activeRequestCount(), 0);
}

void PdfAnchorPresenterTest::newerRequestSupersedesOldResultWithoutLaunchingIt()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("stale.pdf"));
    QVERIFY(writePdf(source));
    QList<SumatraPdfCommand> launches;
    SumatraAnnotatedCopyPresenterOptions options;
    options.launchHandler = [&launches](const SumatraPdfCommand &command, QString *) {
        launches.append(command);
        return true;
    };
    SumatraAnnotatedCopyPresenter presenter(options);
    QList<PdfAnchorPresentationResult> firstResults;
    QList<PdfAnchorPresentationResult> secondResults;
    PdfAnchorPresentationCallbacks firstCallbacks;
    firstCallbacks.completed = [&](const PdfAnchorPresentationResult &value) {
        firstResults.append(value);
    };
    PdfAnchorPresentationCallbacks secondCallbacks;
    secondCallbacks.completed = [&](const PdfAnchorPresentationResult &value) {
        secondResults.append(value);
    };
    QVERIFY(presenter.present(
        presentationRequest(source, directory.filePath(QStringLiteral("cache")),
                            QStringLiteral("old"), QRectF(10, 20, 80, 40)),
        firstCallbacks).accepted());
    QVERIFY(presenter.present(
        presentationRequest(source, directory.filePath(QStringLiteral("cache")),
                            QStringLiteral("new"), QRectF(110, 120, 80, 40)),
        secondCallbacks).accepted());
    QCOMPARE(firstResults.size(), 1);
    QVERIFY(firstResults.first().superseded);
    QTRY_COMPARE_WITH_TIMEOUT(secondResults.size(), 1, 3000);
    QVERIFY(secondResults.first().success());
    QCOMPARE(launches.size(), 1);
    QCOMPARE(launches.first().filePath, secondResults.first().previewFilePath);
}

void PdfAnchorPresenterTest::cancellationClosesPendingPresentationWithoutLaunching()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("cancel.pdf"));
    QVERIFY(writePdf(source));
    QList<SumatraPdfCommand> launches;
    SumatraAnnotatedCopyPresenterOptions options;
    options.launchHandler = [&launches](const SumatraPdfCommand &command, QString *) {
        launches.append(command);
        return true;
    };
    SumatraAnnotatedCopyPresenter presenter(options);
    QList<PdfAnchorPresentationResult> results;
    PdfAnchorPresentationCallbacks callbacks;
    callbacks.completed = [&results](const PdfAnchorPresentationResult &result) {
        results.append(result);
    };
    QVERIFY(presenter.present(
        presentationRequest(source, directory.filePath(QStringLiteral("cache")),
                            QStringLiteral("cancel"), QRectF(10, 20, 80, 40)),
        callbacks).accepted());
    presenter.cancelPending();
    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().superseded);
    QCOMPARE(presenter.activeRequestCount(), 0);
    QTest::qWait(100);
    QVERIFY(launches.isEmpty());
}

void PdfAnchorPresenterTest::isolatesSequentialAnchorsAndCacheKeys()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("sessions.pdf"));
    QVERIFY(writePdf(source));
    QList<SumatraPdfCommand> launches;
    SumatraAnnotatedCopyPresenterOptions options;
    options.launchHandler = [&launches](const SumatraPdfCommand &command, QString *) {
        launches.append(command);
        return true;
    };
    SumatraAnnotatedCopyPresenter presenter(options);
    PdfAnchorPresentationResult first;
    PdfAnchorPresentationCallbacks firstCallbacks;
    firstCallbacks.completed = [&](const PdfAnchorPresentationResult &value) { first = value; };
    QVERIFY(presenter.present(
        presentationRequest(source, directory.filePath(QStringLiteral("cache")),
                            QStringLiteral("first"), QRectF(10, 20, 80, 40)),
        firstCallbacks).accepted());
    QTRY_VERIFY_WITH_TIMEOUT(first.success(), 3000);
    PdfAnchorPresentationResult second;
    PdfAnchorPresentationCallbacks secondCallbacks;
    secondCallbacks.completed = [&](const PdfAnchorPresentationResult &value) { second = value; };
    QVERIFY(presenter.present(
        presentationRequest(source, directory.filePath(QStringLiteral("cache")),
                            QStringLiteral("second"), QRectF(110, 120, 80, 40)),
        secondCallbacks).accepted());
    QTRY_VERIFY_WITH_TIMEOUT(second.success(), 3000);
    QCOMPARE(launches.size(), 2);
    QVERIFY(first.previewFilePath != second.previewFilePath);
    QCOMPARE(presenter.activeRequestCount(), 0);
}

void PdfAnchorPresenterTest::openServiceOffersExplicitOriginalPdfFallback()
{
    InMemoryLibraryRepository repository;
    Resource resource;
    resource.id = QStringLiteral("pdf-resource");
    resource.kind = ResourceKind::Pdf;
    resource.title = QStringLiteral("Original report");
    resource.location = QStringLiteral("E:/docs/original report.pdf");
    Anchor anchor;
    anchor.id = QStringLiteral("rect-anchor");
    anchor.name = QStringLiteral("Target rectangle");
    anchor.targetApp = QStringLiteral("SumatraPDF");
    anchor.targetFile = resource.location;
    anchor.locatorType = QStringLiteral("sumatrapdf.rect");
    anchor.locatorJson = QStringLiteral(
        R"({"type":"sumatrapdf.rect","page":4,"rect":[10,20,90,60]})");
    resource.anchors = {anchor};
    QVERIFY(repository.upsertResource(resource));

    FailingPresenter presenter;
    QList<SumatraPdfCommand> launches;
    SumatraPdfViewerAdapterOptions adapterOptions;
    adapterOptions.executablePathProvider = []() {
        return QStringLiteral("C:/Tools/SumatraPDF.exe");
    };
    adapterOptions.pdfAnchorPresenter = &presenter;
    adapterOptions.launchHandler = [&launches](const SumatraPdfCommand &command,
                                               QString *) {
        launches.append(command);
        return true;
    };
    SumatraPdfViewerAdapter adapter(repository, adapterOptions);
    PinloomOpenServiceOptions options;
    options.pdfViewerAdapter = &adapter;
    options.pdfOriginalFallbackPrompt = [](const QString &, int, const QString &) {
        return true;
    };
    PinloomOpenService service(repository, options);
    PinloomOpenTarget target;
    target.resourceId = resource.id;
    target.resourceKind = resource.kind;
    target.title = resource.title;
    target.location = resource.location;
    target.anchor = anchor;
    QVERIFY(service.open(target));
    QTRY_COMPARE_WITH_TIMEOUT(launches.size(), 1, 1000);
    QCOMPARE(launches.first().filePath, resource.location);
    QCOMPARE(launches.first().arguments,
             QStringList({QStringLiteral("-reuse-instance"),
                          QStringLiteral("-page"),
                          QStringLiteral("4"),
                          resource.location}));
    QVERIFY(service.statusText().contains(QStringLiteral("original PDF fallback"),
                                          Qt::CaseInsensitive));
    QVERIFY(!service.hasPdfOriginalFallback());
}

QTEST_GUILESS_MAIN(PdfAnchorPresenterTest)

#include "pdf_anchor_presenter_test.moc"
