#pragma once

#include "pinloom/core/LibraryRepository.h"
#include "pinloom/core/ManualPdfAnchorCreation.h"

#include <optional>
#include <QtGlobal>
#include <functional>
#include <QStringList>

namespace Pinloom {

struct ForegroundAppWindowContext {
    QString windowTitle;
    QString processName;
    QString processPath;
    quintptr windowHandle = 0;
    quint32 processId = 0;

    bool isValid() const;
};

struct PdfXChangeViewState {
    int currentPage = -1;
    int totalPages = -1;
    double zoom = -1.0;
    QString source;
    QString diagnostics;

    bool hasCurrentPage() const;
    bool hasZoom() const;
    bool hasAnyViewState() const;
};

struct PdfXChangeForegroundCaptureResult {
    ManualPdfAnchorCreationRequest request;
    PdfXChangeViewState viewState;
    QString status;
    QString documentTitle;
    QString matchedResourceId;
    QStringList matchedResourceIds;
    bool recognizedPdfXChange = false;
    bool matchedResource = false;
    bool needsFileConfirmation = false;
    bool resolvedFromTitleMapping = false;
    bool rejectedTitleMapping = false;
    bool confirmedFile = false;

    bool success() const;
};

class PdfXChangeForegroundCaptureProvider {
public:
    using ViewStateProvider = std::function<PdfXChangeViewState(const ForegroundAppWindowContext &context)>;
    using TitlePathProvider = std::function<std::optional<QString>(
        const QString &documentTitle,
        const QString &normalizedTitleKey)>;

    explicit PdfXChangeForegroundCaptureProvider(const ILibraryRepository &repository);
    PdfXChangeForegroundCaptureProvider(const ILibraryRepository &repository,
                                        ViewStateProvider viewStateProvider);
    PdfXChangeForegroundCaptureProvider(const ILibraryRepository &repository,
                                        ViewStateProvider viewStateProvider,
                                        TitlePathProvider titlePathProvider);

    PdfXChangeForegroundCaptureResult capture(const ForegroundAppWindowContext &context) const;
    PdfXChangeForegroundCaptureResult captureCurrentForeground() const;

private:
    const ILibraryRepository &repository_;
    ViewStateProvider viewStateProvider_;
    TitlePathProvider titlePathProvider_;
};

ForegroundAppWindowContext currentForegroundAppWindowContext();
bool isPdfXChangeForegroundWindow(const ForegroundAppWindowContext &context);
QString normalizedPdfXChangeDocumentTitleKey(const QString &documentTitle);
bool isPdfXChangeFullPdfPath(const QString &filePath);
QString pdfXChangeDocumentTitleFromWindowTitle(const QString &windowTitle);
QString pdfXChangeDocumentPathFromWindowTitle(const QString &windowTitle);
PdfXChangeViewState parsePdfXChangeViewStateText(const QString &text,
                                                 const QString &source = QStringLiteral("text"));
PdfXChangeViewState capturePdfXChangeViewState(const ForegroundAppWindowContext &context);
QList<Resource> pdfXChangeTitleMatchedPdfResources(
    const ILibraryRepository &repository,
    const QString &documentTitle);
std::optional<Resource> uniquePdfXChangeTitleMatchedPdfResource(
    const ILibraryRepository &repository,
    const QString &documentTitle);
PdfXChangeForegroundCaptureResult pdfXChangeForegroundCaptureResultForConfirmedPdfFile(
    const QString &documentTitle,
    const QString &filePath,
    const PdfXChangeViewState &viewState = {});
PdfXChangeForegroundCaptureResult capturePdfXChangeForegroundContext(
    const ILibraryRepository &repository,
    const ForegroundAppWindowContext &context);
PdfXChangeForegroundCaptureResult capturePdfXChangeForegroundContext(
    const ILibraryRepository &repository,
    const ForegroundAppWindowContext &context,
    const PdfXChangeViewState &viewState);
PdfXChangeForegroundCaptureResult capturePdfXChangeForegroundContext(
    const ILibraryRepository &repository,
    const ForegroundAppWindowContext &context,
    const PdfXChangeViewState &viewState,
    const std::optional<QString> &savedDocumentPath);

} // namespace Pinloom
