#include "pinloom/widgets/PdfLocatorPreviewRenderer.h"

#include "pinloom/core/SumatraPdfCommand.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace Pinloom {

namespace {

struct PdfLocatorGeometry {
    int page = -1;
    QRectF rectangle;
    QString error;
};

PdfLocatorGeometry parsePdfLocatorGeometry(const Anchor &anchor)
{
    PdfLocatorGeometry geometry;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(anchor.locatorJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        geometry.error = QStringLiteral("PDF locator JSON is invalid");
        return geometry;
    }

    const QJsonObject object = document.object();
    geometry.page = object.value(QStringLiteral("page")).toInt(-1);
    if (geometry.page <= 0) {
        geometry.error = QStringLiteral("PDF locator page is missing");
        return geometry;
    }

    QJsonArray values = object.value(QStringLiteral("rect")).toArray();
    if (values.size() != 4) values = object.value(QStringLiteral("region")).toArray();
    if (values.size() == 4) {
        const double left = values.at(0).toDouble();
        const double top = values.at(1).toDouble();
        const double right = values.at(2).toDouble();
        const double bottom = values.at(3).toDouble();
        if (std::isfinite(left) && std::isfinite(top)
            && std::isfinite(right) && std::isfinite(bottom)
            && right > left && bottom > top) {
            geometry.rectangle = QRectF(QPointF(left, top), QPointF(right, bottom));
        }
    }
    return geometry;
}

QString localPdfPath(const Resource &resource, const Anchor &anchor)
{
    QStringList candidates{anchor.targetFile, anchor.targetUri, resource.location};
    for (QString candidate : candidates) {
        candidate = candidate.trimmed();
        if (candidate.isEmpty()) continue;
        const QUrl url(candidate);
        if (url.isLocalFile()) candidate = url.toLocalFile();
        if (candidate.contains(QStringLiteral("://"))) continue;
        const QString path = QDir::cleanPath(QDir::fromNativeSeparators(candidate));
        if (QFileInfo::exists(path)) return path;
    }
    return {};
}

QString processFailureDetail(QProcess &process)
{
    QString detail = QString::fromLocal8Bit(process.readAllStandardError()).simplified();
    if (detail.isEmpty()) detail = QString::fromLocal8Bit(process.readAllStandardOutput()).simplified();
    if (detail.size() > 240) detail = detail.left(237) + QStringLiteral("...");
    return detail;
}

QString previewCacheDirectory(const PdfLocatorPreviewRenderOptions &options)
{
    if (!options.cacheDirectory.trimmed().isEmpty()) {
        return QDir::cleanPath(options.cacheDirectory.trimmed());
    }
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return cacheRoot.trimmed().isEmpty()
        ? QString()
        : QDir(cacheRoot).filePath(QStringLiteral("pdf-previews"));
}

QString previewCachePath(const QString &cacheDirectory,
                         const QString &pdfPath,
                         const Anchor &anchor,
                         const PdfLocatorPreviewRenderOptions &options)
{
    if (cacheDirectory.isEmpty()) return {};
    const QFileInfo file(pdfPath);
    QJsonObject key;
    key.insert(QStringLiteral("version"), 1);
    key.insert(QStringLiteral("path"), QDir::fromNativeSeparators(file.canonicalFilePath().isEmpty()
                                                                      ? file.absoluteFilePath()
                                                                      : file.canonicalFilePath()));
    key.insert(QStringLiteral("size"), QString::number(file.size()));
    key.insert(QStringLiteral("modified"), QString::number(file.lastModified().toMSecsSinceEpoch()));
    key.insert(QStringLiteral("locatorType"), anchor.locatorType.trimmed().toLower());
    key.insert(QStringLiteral("locator"), anchor.locatorJson.trimmed());
    key.insert(QStringLiteral("resolution"), std::clamp(options.resolutionDpi, 72, 300));
    key.insert(QStringLiteral("padding"), std::max<qreal>(0.0, options.contextPaddingPoints));
    key.insert(QStringLiteral("border"), options.drawSelectionBorder);
    const QByteArray digest = QCryptographicHash::hash(
        QJsonDocument(key).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
    return QDir(cacheDirectory).filePath(QString::fromLatin1(digest) + QStringLiteral(".png"));
}

QImage readPreviewCache(const QString &cachePath)
{
    if (cachePath.isEmpty()) return {};
    QImage image(cachePath);
    if (image.isNull()) {
        QFile::remove(cachePath);
        return {};
    }
    QFile file(cachePath);
    if (file.open(QIODevice::ReadWrite)) {
        file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    }
    return image;
}

void prunePreviewCache(const QString &cacheDirectory, qint64 maximumBytes)
{
    if (maximumBytes <= 0) return;
    const QFileInfoList files = QDir(cacheDirectory).entryInfoList(
        {QStringLiteral("*.png")}, QDir::Files, QDir::Time);
    qint64 totalBytes = 0;
    for (const QFileInfo &file : files) totalBytes += file.size();
    for (auto iterator = files.crbegin(); iterator != files.crend() && totalBytes > maximumBytes;
         ++iterator) {
        const qint64 size = iterator->size();
        if (QFile::remove(iterator->absoluteFilePath())) totalBytes -= size;
    }
}

bool writePreviewCache(const QString &cachePath,
                       const QImage &image,
                       qint64 maximumCacheBytes)
{
    if (cachePath.isEmpty() || image.isNull()) return false;
    const QString directory = QFileInfo(cachePath).absolutePath();
    if (!QDir().mkpath(directory)) return false;
    QSaveFile file(cachePath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    QImageWriter writer(&file, "png");
    if (!writer.write(image) || !file.commit()) return false;
    prunePreviewCache(directory, maximumCacheBytes);
    return QFileInfo::exists(cachePath);
}

} // namespace

bool PdfLocatorPreviewRenderResult::success() const
{
    return !image.isNull() && error.isEmpty();
}

QString resolvePdfLocatorPreviewRendererPath(const QString &sumatraPdfExecutablePath)
{
    QStringList candidates;
    const QString configured = qEnvironmentVariable("PINLOOM_SUMATRAPDF_TOOL_PATH").trimmed();
    if (!configured.isEmpty()) candidates.append(configured);

    QString sumatraPath = sumatraPdfExecutablePath.trimmed();
    if (sumatraPath.isEmpty()) sumatraPath = resolveSumatraPdfExecutablePath();
    if (!sumatraPath.isEmpty()) {
        candidates.append(QFileInfo(sumatraPath).dir().filePath(QStringLiteral("sumatrapdf-tool.exe")));
    }

    const QString discovered = QStandardPaths::findExecutable(QStringLiteral("sumatrapdf-tool.exe"));
    if (!discovered.isEmpty()) candidates.append(discovered);
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile()) return QDir::toNativeSeparators(info.absoluteFilePath());
    }
    return {};
}

QString pdfLocatorPreviewCacheFilePath(
    const Resource &resource,
    const Anchor &anchor,
    const PdfLocatorPreviewRenderOptions &options)
{
    if (!options.usePersistentCache) return {};
    const QString pdfPath = localPdfPath(resource, anchor);
    if (pdfPath.isEmpty()) return {};
    return previewCachePath(previewCacheDirectory(options), pdfPath, anchor, options);
}

QImage cropPdfLocatorPreviewImage(const QImage &pageImage,
                                  const QRectF &locatorRectanglePoints,
                                  int resolutionDpi,
                                  qreal contextPaddingPoints,
                                  bool drawSelectionBorder)
{
    if (pageImage.isNull() || !locatorRectanglePoints.isValid() || resolutionDpi <= 0) {
        return pageImage;
    }
    const qreal scale = static_cast<qreal>(resolutionDpi) / 72.0;
    const QRectF pagePixels(QPointF(0.0, 0.0), pageImage.size());
    const QRectF selectionPixels(locatorRectanglePoints.left() * scale,
                                 locatorRectanglePoints.top() * scale,
                                 locatorRectanglePoints.width() * scale,
                                 locatorRectanglePoints.height() * scale);
    const qreal padding = std::max<qreal>(0.0, contextPaddingPoints) * scale;
    const QRect crop = selectionPixels.adjusted(-padding, -padding, padding, padding)
                           .intersected(pagePixels)
                           .toAlignedRect()
                           .intersected(pageImage.rect());
    if (crop.isEmpty()) return {};

    QImage cropped = pageImage.copy(crop);
    if (drawSelectionBorder) {
        QPainter painter(&cropped);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const qreal penWidth = std::max<qreal>(2.0, scale * 1.5);
        painter.setPen(QPen(QColor(QStringLiteral("#d19a00")), penWidth));
        const QRectF highlighted = selectionPixels.translated(-crop.left(), -crop.top())
                                       .intersected(QRectF(cropped.rect()))
                                       .adjusted(penWidth / 2.0,
                                                 penWidth / 2.0,
                                                 -penWidth / 2.0,
                                                 -penWidth / 2.0);
        if (highlighted.isValid()) painter.drawRect(highlighted);
    }
    return cropped;
}

PdfLocatorPreviewRenderResult renderPdfLocatorPreview(
    const Resource &resource,
    const Anchor &anchor,
    const PdfLocatorPreviewRenderOptions &options)
{
    PdfLocatorPreviewRenderResult result;
    const PdfLocatorGeometry geometry = parsePdfLocatorGeometry(anchor);
    result.page = geometry.page;
    result.locatorRectangle = geometry.rectangle;
    if (!geometry.error.isEmpty()) {
        result.error = geometry.error;
        return result;
    }

    const QString pdfPath = localPdfPath(resource, anchor);
    if (pdfPath.isEmpty()) {
        result.error = QStringLiteral("PDF preview file is missing");
        return result;
    }
    const QString cachePath = pdfLocatorPreviewCacheFilePath(resource, anchor, options);
    result.image = readPreviewCache(cachePath);
    if (!result.image.isNull()) {
        result.cacheFilePath = QFileInfo(cachePath).absoluteFilePath();
        result.cropped = geometry.rectangle.isValid();
        result.fromCache = true;
        return result;
    }
    const QString rendererPath = options.rendererExecutablePath.trimmed().isEmpty()
        ? resolvePdfLocatorPreviewRendererPath()
        : options.rendererExecutablePath.trimmed();
    if (rendererPath.isEmpty() || !QFileInfo::exists(rendererPath)) {
        result.error = QStringLiteral("SumatraPDF 3.7 preview renderer was not found");
        return result;
    }
    const int resolutionDpi = std::clamp(options.resolutionDpi, 72, 300);
    const int timeoutMilliseconds = std::max(1000, options.timeoutMilliseconds);
    QTemporaryDir temporaryDirectory(QDir::tempPath() + QStringLiteral("/pinloom-pdf-preview-XXXXXX"));
    if (!temporaryDirectory.isValid()) {
        result.error = QStringLiteral("Unable to create PDF preview temporary directory");
        return result;
    }

    const QString outputPath = temporaryDirectory.filePath(QStringLiteral("page.png"));
    QProcess process;
    process.setProgram(rendererPath);
    process.setArguments({QStringLiteral("convert"),
                          QStringLiteral("-o"),
                          outputPath,
                          QStringLiteral("-O"),
                          QStringLiteral("resolution=%1").arg(resolutionDpi),
                          pdfPath,
                          QString::number(geometry.page)});
    process.setWorkingDirectory(temporaryDirectory.path());
    process.start();
    if (!process.waitForStarted(5000)) {
        result.error = QStringLiteral("Unable to start the SumatraPDF preview renderer");
        return result;
    }
    if (!process.waitForFinished(timeoutMilliseconds)) {
        process.kill();
        process.waitForFinished();
        result.error = QStringLiteral("PDF preview rendering timed out");
        return result;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString detail = processFailureDetail(process);
        result.error = detail.isEmpty()
            ? QStringLiteral("PDF preview renderer failed with exit code %1").arg(process.exitCode())
            : QStringLiteral("PDF preview renderer failed: %1").arg(detail);
        return result;
    }

    const QFileInfoList images = QDir(temporaryDirectory.path()).entryInfoList(
        {QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    if (images.isEmpty()) {
        result.error = QStringLiteral("PDF preview renderer produced no image");
        return result;
    }
    const QImage pageImage(images.first().absoluteFilePath());
    if (pageImage.isNull()) {
        result.error = QStringLiteral("Unable to read the rendered PDF page");
        return result;
    }
    if (geometry.rectangle.isValid()) {
        result.image = cropPdfLocatorPreviewImage(pageImage,
                                                  geometry.rectangle,
                                                  resolutionDpi,
                                                  options.contextPaddingPoints,
                                                  options.drawSelectionBorder);
        result.cropped = !result.image.isNull();
        if (result.image.isNull()) {
            result.error = QStringLiteral("PDF locator rectangle is outside the rendered page");
        }
    } else {
        result.image = pageImage;
    }
    if (result.success()) {
        if (writePreviewCache(cachePath, result.image, options.maximumCacheBytes)) {
            result.cacheFilePath = QFileInfo(cachePath).absoluteFilePath();
        }
    }
    return result;
}

} // namespace Pinloom
