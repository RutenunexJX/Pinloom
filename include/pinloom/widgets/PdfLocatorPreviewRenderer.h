#pragma once

#include "pinloom/core/Resource.h"

#include <QImage>
#include <QRectF>
#include <QString>

namespace Pinloom {

struct PdfLocatorPreviewRenderOptions {
    QString rendererExecutablePath;
    QString cacheDirectory;
    int resolutionDpi = 144;
    qreal contextPaddingPoints = 18.0;
    int timeoutMilliseconds = 20000;
    qint64 maximumCacheBytes = 512LL * 1024LL * 1024LL;
    bool drawSelectionBorder = true;
    bool usePersistentCache = true;
};

struct PdfLocatorPreviewRenderResult {
    QImage image;
    QString error;
    int page = -1;
    QRectF locatorRectangle;
    bool cropped = false;
    bool fromCache = false;

    bool success() const;
};

QString resolvePdfLocatorPreviewRendererPath(const QString &sumatraPdfExecutablePath = {});

QImage cropPdfLocatorPreviewImage(const QImage &pageImage,
                                  const QRectF &locatorRectanglePoints,
                                  int resolutionDpi,
                                  qreal contextPaddingPoints = 18.0,
                                  bool drawSelectionBorder = true);

PdfLocatorPreviewRenderResult renderPdfLocatorPreview(
    const Resource &resource,
    const Anchor &anchor,
    const PdfLocatorPreviewRenderOptions &options = {});

} // namespace Pinloom
