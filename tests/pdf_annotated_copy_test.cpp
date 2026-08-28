#include "pinloom/core/PdfAnnotatedCopy.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace Pinloom;

namespace {

struct FixturePage {
    QRectF mediaBox = QRectF(0, 0, 600, 800);
    QRectF cropBox = mediaBox;
    int rotation = 0;
    double userUnit = 1.0;
};

QByteArray pdfNumber(double value)
{
    QByteArray number = QByteArray::number(value, 'f', 3);
    while (number.contains('.') && number.endsWith('0')) number.chop(1);
    if (number.endsWith('.')) number.chop(1);
    return number;
}

QByteArray pdfBox(const QRectF &box)
{
    return '[' + pdfNumber(box.left()) + ' ' + pdfNumber(box.top()) + ' '
        + pdfNumber(box.right()) + ' ' + pdfNumber(box.bottom()) + ']';
}

QByteArray fixturePdf(const QList<FixturePage> &pages, bool encrypted = false)
{
    QVector<QByteArray> objects;
    objects.append("<< /Type /Catalog /Pages 2 0 R >>");
    QByteArray kids("[");
    for (int index = 0; index < pages.size(); ++index) {
        if (index > 0) kids.append(' ');
        kids.append(QByteArray::number(index + 3) + " 0 R");
    }
    kids.append(']');
    objects.append("<< /Type /Pages /Count " + QByteArray::number(pages.size())
                   + " /Kids " + kids + " >>");
    for (const FixturePage &page : pages) {
        QByteArray object = "<< /Type /Page /Parent 2 0 R /MediaBox "
            + pdfBox(page.mediaBox) + " /CropBox " + pdfBox(page.cropBox);
        if (page.rotation != 0) {
            object.append(" /Rotate " + QByteArray::number(page.rotation));
        }
        if (page.userUnit != 1.0) {
            object.append(" /UserUnit " + pdfNumber(page.userUnit));
        }
        object.append(" >>");
        objects.append(object);
    }
    int encryptObject = -1;
    if (encrypted) {
        encryptObject = objects.size() + 1;
        objects.append("<< /Filter /Standard /V 1 /R 2 /Length 40 "
                       "/O <0011> /U <2233> /P -4 >>");
    }

    QByteArray pdf("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
    QVector<qint64> offsets(objects.size() + 1, 0);
    for (int index = 0; index < objects.size(); ++index) {
        offsets[index + 1] = pdf.size();
        pdf.append(QByteArray::number(index + 1) + " 0 obj\n");
        pdf.append(objects.at(index));
        pdf.append("\nendobj\n");
    }
    const qint64 xrefOffset = pdf.size();
    pdf.append("xref\n0 " + QByteArray::number(objects.size() + 1) + "\n");
    pdf.append("0000000000 65535 f \n");
    for (int index = 1; index < offsets.size(); ++index) {
        pdf.append(QByteArray::number(offsets.at(index)).rightJustified(10, '0'));
        pdf.append(" 00000 n \n");
    }
    pdf.append("trailer\n<< /Size " + QByteArray::number(objects.size() + 1)
               + " /Root 1 0 R");
    if (encrypted) {
        pdf.append(" /Encrypt " + QByteArray::number(encryptObject) + " 0 R");
    }
    pdf.append(" >>\nstartxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n");
    return pdf;
}

void appendBigEndian(QByteArray *bytes, quint64 value, int width)
{
    for (int shift = width - 1; shift >= 0; --shift) {
        bytes->append(static_cast<char>((value >> (shift * 8)) & 0xff));
    }
}

QByteArray flateBytes(const QByteArray &plain)
{
    const QByteArray compressed = qCompress(plain, 9);
    return compressed.size() > 4 ? compressed.mid(4) : QByteArray{};
}

QByteArray compressedObjectStreamFixturePdf()
{
    const QVector<QByteArray> compressedObjects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] "
        "/CropBox [25 50 575 750] /Rotate 90 >>",
    };
    QByteArray objectStreamHeader;
    QByteArray objectStreamBody;
    for (int index = 0; index < compressedObjects.size(); ++index) {
        objectStreamHeader.append(QByteArray::number(index + 1) + ' '
                                  + QByteArray::number(objectStreamBody.size()) + ' ');
        objectStreamBody.append(compressedObjects.at(index));
        objectStreamBody.append('\n');
    }
    const QByteArray objectStreamPlain = objectStreamHeader + objectStreamBody;
    const QByteArray objectStreamCompressed = flateBytes(objectStreamPlain);

    QByteArray pdf("%PDF-1.7\n% compressed object fixture\n");
    const qint64 objectStreamOffset = pdf.size();
    pdf.append("4 0 obj\n<< /Type /ObjStm /N 3 /First "
               + QByteArray::number(objectStreamHeader.size())
               + " /Filter /FlateDecode /Length "
               + QByteArray::number(objectStreamCompressed.size())
               + " >>\nstream\n");
    pdf.append(objectStreamCompressed);
    pdf.append("\nendstream\nendobj\n");

    const qint64 xrefOffset = pdf.size();
    QByteArray xrefPlain;
    appendBigEndian(&xrefPlain, 0, 1);
    appendBigEndian(&xrefPlain, 0, 4);
    appendBigEndian(&xrefPlain, 65535, 2);
    for (int index = 0; index < 3; ++index) {
        appendBigEndian(&xrefPlain, 2, 1);
        appendBigEndian(&xrefPlain, 4, 4);
        appendBigEndian(&xrefPlain, index, 2);
    }
    appendBigEndian(&xrefPlain, 1, 1);
    appendBigEndian(&xrefPlain, objectStreamOffset, 4);
    appendBigEndian(&xrefPlain, 0, 2);
    appendBigEndian(&xrefPlain, 1, 1);
    appendBigEndian(&xrefPlain, xrefOffset, 4);
    appendBigEndian(&xrefPlain, 0, 2);
    const QByteArray xrefCompressed = flateBytes(xrefPlain);
    pdf.append("5 0 obj\n<< /Type /XRef /Size 6 /Root 1 0 R /W [1 4 2] "
               "/Filter /FlateDecode /Length "
               + QByteArray::number(xrefCompressed.size())
               + " >>\nstream\n");
    pdf.append(xrefCompressed);
    pdf.append("\nendstream\nendobj\nstartxref\n"
               + QByteArray::number(xrefOffset) + "\n%%EOF\n");
    return pdf;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write(data) == data.size();
}

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

QByteArray sha256(const QString &path)
{
    return QCryptographicHash::hash(fileBytes(path), QCryptographicHash::Sha256);
}

PdfAnnotatedCopyRequest requestFor(const QString &source,
                                   const QString &cache,
                                   int page,
                                   const QRectF &rect)
{
    PdfAnnotatedCopyRequest request;
    request.sourceFilePath = source;
    request.cacheDirectory = cache;
    request.anchorId = QStringLiteral("anchor-%1").arg(page);
    request.locatorJson = QStringLiteral(
        R"({"type":"sumatrapdf.rect","version":2,"page":%1,"rect":[%2,%3,%4,%5],"coordinateSpace":"page-top-left"})")
                              .arg(page)
                              .arg(rect.left())
                              .arg(rect.top())
                              .arg(rect.right())
                              .arg(rect.bottom());
    request.pageNumber = page;
    request.anchorRect = rect;
    return request;
}

void compareRect(const QRectF &actual, const QRectF &expected)
{
    QVERIFY2(std::abs(actual.left() - expected.left()) < 0.001,
             qPrintable(QStringLiteral("left %1 != %2").arg(actual.left()).arg(expected.left())));
    QVERIFY(std::abs(actual.top() - expected.top()) < 0.001);
    QVERIFY(std::abs(actual.width() - expected.width()) < 0.001);
    QVERIFY(std::abs(actual.height() - expected.height()) < 0.001);
}

} // namespace

class PdfAnnotatedCopyTest final : public QObject {
    Q_OBJECT

private slots:
    void preservesSourceAndWritesExplicitAppearanceAndDestination();
    void mapsCropBoxAndRotatedPageCoordinates_data();
    void mapsCropBoxAndRotatedPageCoordinates();
    void cachesAndInvalidatesBySourceAnchorAndLocator();
    void handlesUnicodeReadOnlyCorruptAndEncryptedSources();
    void readsFlateXrefAndCompressedPageObjects();
    void atomicGenerationFailureLeavesNoPartialPdf();
    void rendersExpectedPageSpaceRectsWithSumatraTool();
};

void PdfAnnotatedCopyTest::preservesSourceAndWritesExplicitAppearanceAndDestination()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.pdf"));
    const QByteArray original = fixturePdf({FixturePage{}});
    QVERIFY(writeFile(source, original));
    const QByteArray beforeHash = sha256(source);

    PdfAnnotatedCopyRequest request = requestFor(
        source, directory.filePath(QStringLiteral("cache")), 1,
        QRectF(100, 150, 100, 100));
    const PdfAnnotatedCopyResult result = preparePdfAnnotatedCopy(request);
    QVERIFY2(result.success(), qPrintable(result.error));
    QVERIFY(!result.cacheHit);
    QCOMPARE(sha256(source), beforeHash);
    QCOMPARE(fileBytes(source), original);
    QVERIFY(result.outputFilePath.contains(QStringLiteral("Pinloom Preview")));
    compareRect(result.annotationRect, QRectF(100, 550, 100, 100));

    const QByteArray preview = fileBytes(result.outputFilePath);
    QVERIFY(preview.startsWith(original));
    QVERIFY(preview.contains("/Subtype /Square"));
    QVERIFY(preview.contains("/Subtype /Form"));
    QVERIFY(preview.contains("/AP"));
    QVERIFY(preview.contains("/ExtGState"));
    QVERIFY(preview.contains("/ca 0.22"));
    QVERIFY(preview.contains(" re B"));
    QVERIFY(preview.contains("/Rect [100 550 200 650]"));
    QVERIFY(preview.contains("/OpenAction [3 0 R /XYZ 100 650 null]"));

    const QString artifactDirectory =
        qEnvironmentVariable("PINLOOM_PDF_TEST_ARTIFACT_DIR").trimmed();
    if (!artifactDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(artifactDirectory));
        const QString artifact = QDir(artifactDirectory).filePath(
            QStringLiteral("annotated-preview.pdf"));
        QFile::remove(artifact);
        QVERIFY(QFile::copy(result.outputFilePath, artifact));
    }
}

void PdfAnnotatedCopyTest::mapsCropBoxAndRotatedPageCoordinates_data()
{
    QTest::addColumn<int>("page");
    QTest::addColumn<QRectF>("expected");
    QTest::newRow("crop-0") << 2 << QRectF(70, 520, 100, 50);
    QTest::newRow("crop-90") << 3 << QRectF(80, 120, 50, 100);
    QTest::newRow("crop-180") << 4 << QRectF(330, 130, 100, 50);
    QTest::newRow("crop-270") << 5 << QRectF(370, 480, 50, 100);
    QTest::newRow("crop-user-unit-2") << 6 << QRectF(60, 560, 50, 25);
}

void PdfAnnotatedCopyTest::mapsCropBoxAndRotatedPageCoordinates()
{
    QFETCH(int, page);
    QFETCH(QRectF, expected);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QRectF media(0, 0, 600, 800);
    const QRectF crop(50, 100, 400, 500);
    const QList<FixturePage> pages = {
        {media, media, 0},
        {media, crop, 0},
        {media, crop, 90},
        {media, crop, 180},
        {media, crop, 270},
        {media, crop, 0, 2.0},
    };
    const QString source = directory.filePath(QStringLiteral("rotations.pdf"));
    QVERIFY(writeFile(source, fixturePdf(pages)));
    const PdfAnnotatedCopyResult result = preparePdfAnnotatedCopy(
        requestFor(source, directory.filePath(QStringLiteral("cache")), page,
                   QRectF(20, 30, 100, 50)));
    QVERIFY2(result.success(), qPrintable(result.error));
    compareRect(result.annotationRect, expected);
    QCOMPARE(result.pageGeometry.cropBox, crop);
    QCOMPARE(result.pageGeometry.rotation, pages.at(page - 1).rotation);
    QCOMPARE(result.pageGeometry.userUnit, pages.at(page - 1).userUnit);
    const QByteArray preview = fileBytes(result.outputFilePath);
    QVERIFY(preview.contains("/AP"));
    QVERIFY(preview.contains("/Rect " + pdfBox(expected)));
    QVERIFY(preview.contains("/BBox [0 0 " + pdfNumber(expected.width()) + ' '
                             + pdfNumber(expected.height()) + ']'));
    QVERIFY(preview.contains("/OpenAction [" + QByteArray::number(page + 2)
                             + " 0 R /XYZ " + pdfNumber(expected.left()) + ' '
                             + pdfNumber(expected.bottom()) + " null]"));
}

void PdfAnnotatedCopyTest::cachesAndInvalidatesBySourceAnchorAndLocator()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("cache-source.pdf"));
    QVERIFY(writeFile(source, fixturePdf({FixturePage{}})));
    PdfAnnotatedCopyRequest request = requestFor(
        source, directory.filePath(QStringLiteral("cache")), 1,
        QRectF(10, 20, 80, 40));

    const PdfAnnotatedCopyResult first = preparePdfAnnotatedCopy(request);
    QVERIFY2(first.success(), qPrintable(first.error));
    QVERIFY(!first.cacheHit);
    const PdfAnnotatedCopyResult second = preparePdfAnnotatedCopy(request);
    QVERIFY2(second.success(), qPrintable(second.error));
    QVERIFY(second.cacheHit);
    QCOMPARE(second.outputFilePath, first.outputFilePath);

    QVERIFY(writeFile(first.outputFilePath,
                      fileBytes(source)
                          + "\n% Pinloom Preview wrong-cache-key\n%%EOF\n"));
    const PdfAnnotatedCopyResult rebuilt = preparePdfAnnotatedCopy(request);
    QVERIFY2(rebuilt.success(), qPrintable(rebuilt.error));
    QVERIFY(!rebuilt.cacheHit);
    QCOMPARE(rebuilt.outputFilePath, first.outputFilePath);

    request.anchorId = QStringLiteral("different-anchor");
    const PdfAnnotatedCopyResult differentAnchor = preparePdfAnnotatedCopy(request);
    QVERIFY2(differentAnchor.success(), qPrintable(differentAnchor.error));
    QVERIFY(!differentAnchor.cacheHit);
    QVERIFY(differentAnchor.outputFilePath != first.outputFilePath);

    request.anchorId = QStringLiteral("anchor-1");
    request.locatorJson.replace(QStringLiteral("[10,20,90,60]"),
                                QStringLiteral("[11,20,91,60]"));
    request.anchorRect.translate(1, 0);
    const PdfAnnotatedCopyResult differentLocator = preparePdfAnnotatedCopy(request);
    QVERIFY2(differentLocator.success(), qPrintable(differentLocator.error));
    QVERIFY(differentLocator.outputFilePath != first.outputFilePath);

    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::Append));
    QVERIFY(sourceFile.write("% source fingerprint changed\n") > 0);
    sourceFile.close();
    const PdfAnnotatedCopyResult changedSource = preparePdfAnnotatedCopy(request);
    QVERIFY2(changedSource.success(), qPrintable(changedSource.error));
    QVERIFY(changedSource.outputFilePath != differentLocator.outputFilePath);
}

void PdfAnnotatedCopyTest::handlesUnicodeReadOnlyCorruptAndEncryptedSources()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("空 格 只读.pdf"));
    QVERIFY(writeFile(source, fixturePdf({FixturePage{}})));
    const QByteArray before = sha256(source);
    QVERIFY(QFile::setPermissions(source,
                                  QFileDevice::ReadOwner
                                      | QFileDevice::ReadUser
                                      | QFileDevice::ReadGroup
                                      | QFileDevice::ReadOther));
    const PdfAnnotatedCopyResult readOnly = preparePdfAnnotatedCopy(
        requestFor(source, directory.filePath(QStringLiteral("预览 缓存")), 1,
                   QRectF(12, 24, 72, 36)));
    QVERIFY2(readOnly.success(), qPrintable(readOnly.error));
    QCOMPARE(sha256(source), before);
    QVERIFY(QFileInfo::exists(readOnly.outputFilePath));
    QFile::setPermissions(source, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    const QString corruptPath = directory.filePath(QStringLiteral("corrupt.pdf"));
    QVERIFY(writeFile(corruptPath, "%PDF-1.7\ncorrupt\n"));
    const PdfAnnotatedCopyResult corrupt = preparePdfAnnotatedCopy(
        requestFor(corruptPath, directory.filePath(QStringLiteral("cache")), 1,
                   QRectF(1, 1, 10, 10)));
    QVERIFY(!corrupt.success());
    QVERIFY(corrupt.error.contains(QStringLiteral("startxref"), Qt::CaseInsensitive));

    const QString encryptedPath = directory.filePath(QStringLiteral("encrypted.pdf"));
    QVERIFY(writeFile(encryptedPath, fixturePdf({FixturePage{}}, true)));
    const PdfAnnotatedCopyResult encrypted = preparePdfAnnotatedCopy(
        requestFor(encryptedPath, directory.filePath(QStringLiteral("cache")), 1,
                   QRectF(1, 1, 10, 10)));
    QVERIFY(!encrypted.success());
    QVERIFY(encrypted.error.contains(QStringLiteral("Encrypted"), Qt::CaseInsensitive));
}

void PdfAnnotatedCopyTest::readsFlateXrefAndCompressedPageObjects()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("object-stream.pdf"));
    QVERIFY(writeFile(source, compressedObjectStreamFixturePdf()));
    const PdfAnnotatedCopyResult result = preparePdfAnnotatedCopy(
        requestFor(source, directory.filePath(QStringLiteral("cache")), 1,
                   QRectF(20, 30, 100, 50)));
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.pageGeometry.cropBox, QRectF(25, 50, 550, 700));
    QCOMPARE(result.pageGeometry.rotation, 90);
    compareRect(result.annotationRect, QRectF(55, 70, 50, 100));
    QVERIFY(fileBytes(result.outputFilePath).contains("/Subtype /Square"));
}

void PdfAnnotatedCopyTest::atomicGenerationFailureLeavesNoPartialPdf()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("source.pdf"));
    QVERIFY(writeFile(source, fixturePdf({FixturePage{}})));
    const QString blockedCache = directory.filePath(QStringLiteral("cache-is-a-file"));
    QVERIFY(writeFile(blockedCache, "not a directory"));
    const PdfAnnotatedCopyResult result = preparePdfAnnotatedCopy(
        requestFor(source, blockedCache, 1, QRectF(10, 10, 20, 20)));
    QVERIFY(!result.success());
    QVERIFY(result.error.contains(QStringLiteral("cache directory"), Qt::CaseInsensitive)
            || result.error.contains(QStringLiteral("create Pinloom Preview"), Qt::CaseInsensitive));
    const QStringList partials = QDir(directory.path()).entryList(
        {QStringLiteral("*.pinloom-preview.pdf"), QStringLiteral("*.tmp")},
        QDir::Files);
    QVERIFY(partials.isEmpty());
}

void PdfAnnotatedCopyTest::rendersExpectedPageSpaceRectsWithSumatraTool()
{
    const QString tool = qEnvironmentVariable("PINLOOM_SUMATRA_TOOL_PATH").trimmed();
    if (!QFileInfo(tool).isFile()) {
        QSKIP("PINLOOM_SUMATRA_TOOL_PATH does not name sumatrapdf-tool.exe");
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QRectF media(0, 0, 600, 800);
    const QRectF crop(50, 100, 400, 500);
    const QList<FixturePage> pages = {
        {media, crop, 0},
        {media, crop, 90},
        {media, crop, 180},
        {media, crop, 270},
        {media, crop, 0, 2.0},
    };
    const QString source = directory.filePath(QStringLiteral("Sumatra 坐标 fixture.pdf"));
    QVERIFY(writeFile(source, fixturePdf(pages)));
    const QRectF capturedRect(20, 30, 100, 50);

    for (int page = 1; page <= pages.size(); ++page) {
        const PdfAnnotatedCopyResult preview = preparePdfAnnotatedCopy(
            requestFor(source,
                       directory.filePath(QStringLiteral("preview cache")),
                       page,
                       capturedRect));
        QVERIFY2(preview.success(), qPrintable(preview.error));
        const QString rendered = directory.filePath(
            QStringLiteral("sumatra-render-%1.png").arg(page));
        QProcess process;
        process.start(tool,
                      {QStringLiteral("draw"),
                       QStringLiteral("-q"),
                       QStringLiteral("-r"),
                       QStringLiteral("72"),
                       QStringLiteral("-b"),
                       QStringLiteral("CropBox"),
                       QStringLiteral("-o"),
                       rendered,
                       preview.outputFilePath,
                       QString::number(page)});
        QVERIFY2(process.waitForStarted(3000), qPrintable(process.errorString()));
        QVERIFY2(process.waitForFinished(15000), qPrintable(process.errorString()));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());

        const QImage image(rendered);
        QVERIFY2(!image.isNull(), qPrintable(rendered));
        const int rotation = pages.at(page - 1).rotation;
        const int unit = qRound(pages.at(page - 1).userUnit);
        const QSize expectedSize = rotation == 90 || rotation == 270
            ? QSize(500 * unit, 400 * unit)
            : QSize(400 * unit, 500 * unit);
        QCOMPARE(image.size(), expectedSize);

        QRect inkBounds;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor color = image.pixelColor(x, y);
                const bool orangeBorder = color.red() > 220
                    && color.green() > 80 && color.green() < 225
                    && color.blue() < 100;
                if (orangeBorder) {
                    inkBounds = inkBounds.isNull()
                        ? QRect(x, y, 1, 1)
                        : inkBounds.united(QRect(x, y, 1, 1));
                }
            }
        }
        QVERIFY2(!inkBounds.isNull(), qPrintable(QStringLiteral("page %1").arg(page)));
        QVERIFY(std::abs(inkBounds.left() - qRound(capturedRect.left())) <= 3);
        QVERIFY(std::abs(inkBounds.top() - qRound(capturedRect.top())) <= 3);
        QVERIFY(std::abs(inkBounds.right() - qRound(capturedRect.right())) <= 3);
        QVERIFY(std::abs(inkBounds.bottom() - qRound(capturedRect.bottom())) <= 3);
    }
}

QTEST_GUILESS_MAIN(PdfAnnotatedCopyTest)

#include "pdf_annotated_copy_test.moc"
