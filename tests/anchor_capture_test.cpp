#include "pinloom/core/AnchorCapture.h"
#include "pinloom/core/PdfXChangeCommand.h"

#include <QTest>

using namespace Pinloom;

class AnchorCaptureTest : public QObject {
    Q_OBJECT

private slots:
    void buildsManualPdfXChangeRectAnchor();
    void reportsMissingPdfXChangeCaptureInputs();
    void keepsPdfXChangeLocatorJsonStable();
    void buildsAnchorCompatibleWithPdfXChangeExecutor();
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

QTEST_MAIN(AnchorCaptureTest)

#include "anchor_capture_test.moc"
