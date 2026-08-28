#pragma once

#include <QByteArray>
#include <QRectF>
#include <QString>

namespace Pinloom {

enum class PdfAnchorCoordinateSpace {
    PageTopLeft,
    PdfUserSpace,
};

struct PdfSourceFingerprint {
    QString normalizedPath;
    qint64 size = -1;
    qint64 modifiedMilliseconds = -1;
    QByteArray sha256;
    QString error;

    bool success() const;
};

struct PdfPageGeometry {
    QRectF mediaBox;
    QRectF cropBox;
    int rotation = 0;
    double userUnit = 1.0;

    bool isValid() const;
};

struct PdfAnnotatedCopyRequest {
    QString sourceFilePath;
    QString cacheDirectory;
    QString anchorId;
    QString locatorJson;
    int pageNumber = -1;
    QRectF anchorRect;
    PdfAnchorCoordinateSpace coordinateSpace = PdfAnchorCoordinateSpace::PageTopLeft;
};

struct PdfAnnotatedCopyResult {
    QString sourceFilePath;
    QString outputFilePath;
    QByteArray cacheKey;
    PdfSourceFingerprint sourceFingerprint;
    PdfPageGeometry pageGeometry;
    QRectF annotationRect;
    bool cacheHit = false;
    QString error;

    bool success() const;
};

QString defaultPdfAnchorPresentationCacheDirectory();
PdfSourceFingerprint fingerprintPdfSource(const QString &sourceFilePath);
QByteArray pdfAnchorPresentationCacheKey(const PdfAnnotatedCopyRequest &request,
                                         const PdfSourceFingerprint &fingerprint);
QString pdfAnchorPresentationCacheFilePath(const PdfAnnotatedCopyRequest &request,
                                           const PdfSourceFingerprint &fingerprint);
PdfAnnotatedCopyResult preparePdfAnnotatedCopy(const PdfAnnotatedCopyRequest &request);

} // namespace Pinloom
