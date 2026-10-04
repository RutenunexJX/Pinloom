#pragma once

#include "pinloom/core/Resource.h"

#include <QImage>
#include <QRectF>
#include <QString>
#include <atomic>
#include <memory>

namespace Pinloom {

using PdfLocatorPreviewCancellation = std::shared_ptr<std::atomic_bool>;

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
    QString cacheFilePath;
    QString error;
    int page = -1;
    QRectF locatorRectangle;
    bool cropped = false;
    bool fromCache = false;
    bool cancelled = false;

    bool success() const;
};

QString resolvePdfLocatorPreviewRendererPath(const QString &sumatraPdfExecutablePath = {});

QString pdfLocatorPreviewCacheFilePath(
    const Resource &resource,
    const Anchor &anchor,
    const PdfLocatorPreviewRenderOptions &options = {});

// Includes source version and render/cache options even when disk caching is disabled.
QString pdfLocatorPreviewRequestKey(
    const Resource &resource,
    const Anchor &anchor,
    const PdfLocatorPreviewRenderOptions &options = {});

QImage cropPdfLocatorPreviewImage(const QImage &pageImage,
                                  const QRectF &locatorRectanglePoints,
                                  int resolutionDpi,
                                  qreal contextPaddingPoints = 18.0,
                                  bool drawSelectionBorder = true);

PdfLocatorPreviewRenderResult renderPdfLocatorPreview(
    const Resource &resource,
    const Anchor &anchor,
    const PdfLocatorPreviewRenderOptions &options = {},
    const PdfLocatorPreviewCancellation &cancellation = {});

} // namespace Pinloom
