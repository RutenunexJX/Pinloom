#pragma once

#include "pinloom/core/LibraryRepository.h"
#include "pinloom/core/ManualPdfAnchorCreation.h"

#include <QtGlobal>

namespace Pinloom {

struct ForegroundAppWindowContext {
    QString windowTitle;
    QString processName;
    QString processPath;
    quintptr windowHandle = 0;
    quint32 processId = 0;

    bool isValid() const;
};

struct PdfXChangeForegroundCaptureResult {
    ManualPdfAnchorCreationRequest request;
    QString status;
    QString documentTitle;
    QString matchedResourceId;
    bool recognizedPdfXChange = false;
    bool matchedResource = false;

    bool success() const;
};

class PdfXChangeForegroundCaptureProvider {
public:
    explicit PdfXChangeForegroundCaptureProvider(const ILibraryRepository &repository);

    PdfXChangeForegroundCaptureResult capture(const ForegroundAppWindowContext &context) const;
    PdfXChangeForegroundCaptureResult captureCurrentForeground() const;

private:
    const ILibraryRepository &repository_;
};

ForegroundAppWindowContext currentForegroundAppWindowContext();
bool isPdfXChangeForegroundWindow(const ForegroundAppWindowContext &context);
QString pdfXChangeDocumentTitleFromWindowTitle(const QString &windowTitle);
QString pdfXChangeDocumentPathFromWindowTitle(const QString &windowTitle);
PdfXChangeForegroundCaptureResult capturePdfXChangeForegroundContext(
    const ILibraryRepository &repository,
    const ForegroundAppWindowContext &context);

} // namespace Pinloom
