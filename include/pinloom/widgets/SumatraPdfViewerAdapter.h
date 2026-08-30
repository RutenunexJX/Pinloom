#pragma once

#include "pinloom/core/ApplicationLaunchSettings.h"
#include "pinloom/core/PdfAnnotatedCopy.h"
#include "pinloom/core/SumatraPdfDdeClient.h"
#include "pinloom/core/SumatraPdfForegroundCapture.h"
#include "pinloom/widgets/PdfAnchorPresenter.h"
#include "pinloom/widgets/PdfRegionSelectionOverlay.h"
#include "pinloom/widgets/PdfViewerAdapter.h"

#include <QRect>

#include <functional>
#include <memory>

namespace Pinloom {

struct SumatraPdfViewerAdapterOptions {
    ApplicationLaunchSettings applicationLaunchSettings;
    std::function<QString()> executablePathProvider;
    std::function<ForegroundAppWindowContext()> foregroundContextProvider;
    std::function<SumatraPdfViewState(const ForegroundAppWindowContext &)>
        viewStateProvider;
    std::function<QString(const QString &documentTitle, QWidget *parent)>
        fileConfirmationHandler;
    std::function<QRect(quintptr windowHandle)> windowGeometryProvider;
    std::function<bool(quintptr windowHandle)> windowExistsProvider;
    std::function<void(quintptr windowHandle)> activateWindowHandler;
    std::function<PdfRegionSelectionResult(
        const QRect &targetScreenGeometry,
        PdfRegionSelectionOverlay::TargetStateProvider targetStateProvider,
        QWidget *parent,
        int timeoutMilliseconds,
        int minimumSelectionPixels)> regionSelectionHandler;
    std::function<SumatraPdfDdeMousePosition(const QPoint &globalPosition,
                                             int timeoutMilliseconds)>
        mousePositionProvider;
    std::function<ForegroundTextTarget()> textTargetProvider;
    std::function<TextSelectionCaptureResult(
        const ForegroundAppWindowContext &,
        const ForegroundTextTarget &)> textSelectionProvider;
    std::function<PdfPageGeometryResult(const QString &sourceFilePath,
                                        int pageNumber)>
        pageGeometryProvider;
    std::function<bool(const SumatraPdfCommand &, QString *error)> launchHandler;
    std::function<SumatraPdfDdeFileState(int timeoutMilliseconds)>
        navigationStateProvider;
    bool navigationStateProviderRunsInWorker = true;
    int navigationTimeoutMilliseconds = 4200;
    int navigationPollMilliseconds = 180;
    int minimumSelectionPixels = 4;
    PdfAnchorPresenter *pdfAnchorPresenter = nullptr;
    int presentationGenerationTimeoutMilliseconds = 15000;
    QString presentationCacheDirectory;
};

class SumatraPdfViewerAdapter final : public PdfViewerAdapter {
public:
    SumatraPdfViewerAdapter(
        ILibraryRepository &repository,
        SumatraPdfViewerAdapterOptions options = {},
        QObject *parent = nullptr);
    ~SumatraPdfViewerAdapter() override;

    QString adapterId() const override;
    bool supportsContext(const ForegroundAppWindowContext &context) const override;
    bool supportsAnchor(const Anchor &anchor) const override;
    PdfViewerCaptureResult captureRectangle(
        const PdfViewerCaptureRequest &request) override;
    PdfViewerCaptureResult captureText(
        const PdfViewerCaptureRequest &request) override;
    PdfViewerOpenStartResult open(
        const PdfViewerOpenRequest &request,
        PdfViewerOpenCallbacks callbacks = {}) override;
    void cancelPending() override;
    int activeOperationCount() const override;
    bool hasOriginalFallback() const override;
    bool openOriginalFallback(QString *error = nullptr) override;

private:
    class Private;
    std::unique_ptr<Private> d_;
};

bool sumatraPdfJumpMatches(const SumatraPdfDdeFileState &state,
                           const SumatraPdfCommand &command,
                           QString *diagnostics = nullptr);

} // namespace Pinloom
