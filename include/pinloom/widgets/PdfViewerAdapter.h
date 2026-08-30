#pragma once

#include "pinloom/core/ManualPdfAnchorCreation.h"
#include "pinloom/core/TextSelectionCapture.h"

#include <QObject>

#include <functional>

class QWidget;

namespace Pinloom {

enum class PdfViewerOperationState {
    Succeeded,
    Failed,
    Canceled,
    TimedOut
};

struct PdfViewerObservation {
    ForegroundAppWindowContext context;
    QString adapterId;
    QString documentIdentity;
    QString documentPath;
    QString documentTitle;
    int page = -1;
    int pageCount = -1;
    double zoom = -1.0;
    quintptr windowHandle = 0;
    PdfCaptureRect mediaBox;
    PdfCaptureRect cropBox;
    int rotation = 0;
    double userUnit = 1.0;
    QString provenance;

    bool isValid() const;
};

struct PdfViewerCaptureRequest {
    ForegroundAppWindowContext context;
    ForegroundTextTarget textTarget;
    QWidget *parent = nullptr;
    int timeoutMilliseconds = 30000;
    bool requirePageGeometry = true;
};

struct PdfViewerCaptureResult {
    PdfViewerOperationState state = PdfViewerOperationState::Failed;
    PdfViewerObservation observation;
    ManualPdfAnchorCreationRequest anchorRequest;
    TextSelectionCaptureResult textSelection;
    QString message;
    QString diagnostics;

    bool succeeded() const;
    bool canceled() const;
    bool timedOut() const;
};

struct PdfViewerOpenRequest {
    Anchor anchor;
    QString fallbackFilePath;
    QString sourceTitle;
    QString cacheDirectory;
};

struct PdfViewerOpenResult {
    quint64 requestId = 0;
    PdfViewerOperationState state = PdfViewerOperationState::Failed;
    QString sourceFilePath;
    int page = -1;
    QString message;
    QString diagnostics;
    bool originalFallbackAvailable = false;

    bool succeeded() const;
    bool canceled() const;
    bool timedOut() const;
};

struct PdfViewerOpenCallbacks {
    std::function<void(quint64 requestId, const QString &status)> statusChanged;
    std::function<void(const PdfViewerOpenResult &result)> completed;
};

struct PdfViewerOpenStartResult {
    quint64 requestId = 0;
    QString error;

    bool accepted() const;
};

class PdfViewerAdapter : public QObject {
public:
    explicit PdfViewerAdapter(QObject *parent = nullptr);
    ~PdfViewerAdapter() override;

    virtual QString adapterId() const = 0;
    virtual bool supportsContext(const ForegroundAppWindowContext &context) const = 0;
    virtual bool supportsAnchor(const Anchor &anchor) const = 0;
    virtual PdfViewerCaptureResult captureRectangle(
        const PdfViewerCaptureRequest &request) = 0;
    virtual PdfViewerCaptureResult captureText(
        const PdfViewerCaptureRequest &request) = 0;
    virtual PdfViewerOpenStartResult open(
        const PdfViewerOpenRequest &request,
        PdfViewerOpenCallbacks callbacks = {}) = 0;
    virtual void cancelPending() = 0;
    virtual int activeOperationCount() const = 0;
    virtual bool hasOriginalFallback() const = 0;
    virtual bool openOriginalFallback(QString *error = nullptr) = 0;
};

} // namespace Pinloom
