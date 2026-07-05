#pragma once

#include "pinloom/core/LibraryRepository.h"
#include "pinloom/core/ManualPdfAnchorCreation.h"

#include <QtGlobal>
#include <functional>

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
    bool recognizedPdfXChange = false;
    bool matchedResource = false;

    bool success() const;
};

class PdfXChangeForegroundCaptureProvider {
public:
    using ViewStateProvider = std::function<PdfXChangeViewState(const ForegroundAppWindowContext &context)>;

    explicit PdfXChangeForegroundCaptureProvider(const ILibraryRepository &repository);
    PdfXChangeForegroundCaptureProvider(const ILibraryRepository &repository,
                                        ViewStateProvider viewStateProvider);

    PdfXChangeForegroundCaptureResult capture(const ForegroundAppWindowContext &context) const;
    PdfXChangeForegroundCaptureResult captureCurrentForeground() const;

private:
    const ILibraryRepository &repository_;
    ViewStateProvider viewStateProvider_;
};

ForegroundAppWindowContext currentForegroundAppWindowContext();
bool isPdfXChangeForegroundWindow(const ForegroundAppWindowContext &context);
QString pdfXChangeDocumentTitleFromWindowTitle(const QString &windowTitle);
QString pdfXChangeDocumentPathFromWindowTitle(const QString &windowTitle);
PdfXChangeViewState parsePdfXChangeViewStateText(const QString &text,
                                                 const QString &source = QStringLiteral("text"));
PdfXChangeViewState capturePdfXChangeViewState(const ForegroundAppWindowContext &context);
PdfXChangeForegroundCaptureResult capturePdfXChangeForegroundContext(
    const ILibraryRepository &repository,
    const ForegroundAppWindowContext &context);
PdfXChangeForegroundCaptureResult capturePdfXChangeForegroundContext(
    const ILibraryRepository &repository,
    const ForegroundAppWindowContext &context,
    const PdfXChangeViewState &viewState);

} // namespace Pinloom
