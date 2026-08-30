#pragma once

#include "pinloom/core/ForegroundApplicationContext.h"
#include "pinloom/core/LibraryRepository.h"
#include "pinloom/core/ManualPdfAnchorCreation.h"

#include <optional>
#include <QtGlobal>
#include <functional>
#include <QStringList>

namespace Pinloom {

struct SumatraPdfViewState {
    QString documentPath;
    int currentPage = -1;
    int totalPages = -1;
    double zoom = -1.0;
    QString sumatraVersion;
    QString source;
    QString diagnostics;

    bool hasDocumentPath() const;
    bool hasCurrentPage() const;
    bool hasZoom() const;
    bool hasAnyViewState() const;
};

struct SumatraPdfForegroundCaptureResult {
    ManualPdfAnchorCreationRequest request;
    SumatraPdfViewState viewState;
    QString status;
    QString documentTitle;
    bool recognizedSumatraPdf = false;
    bool needsFileConfirmation = false;

    bool success() const;
};

class SumatraPdfForegroundCaptureProvider {
public:
    using ViewStateProvider = std::function<SumatraPdfViewState(const ForegroundAppWindowContext &context)>;
    explicit SumatraPdfForegroundCaptureProvider(const ILibraryRepository &repository);
    SumatraPdfForegroundCaptureProvider(const ILibraryRepository &repository,
                                        ViewStateProvider viewStateProvider);

    SumatraPdfForegroundCaptureResult capture(const ForegroundAppWindowContext &context) const;
    SumatraPdfForegroundCaptureResult captureCurrentForeground() const;

private:
    const ILibraryRepository &repository_;
    ViewStateProvider viewStateProvider_;
};

bool isSumatraPdfForegroundWindow(const ForegroundAppWindowContext &context);
QString normalizedSumatraPdfDocumentTitleKey(const QString &documentTitle);
bool isSumatraPdfFullPdfPath(const QString &filePath);
QString sumatraPdfDocumentTitleFromWindowTitle(const QString &windowTitle);
QString sumatraPdfDocumentPathFromWindowTitle(const QString &windowTitle);
SumatraPdfViewState parseSumatraPdfViewStateText(const QString &text,
                                                 const QString &source = QStringLiteral("text"));
SumatraPdfViewState mergeSumatraPdfViewStates(
    const SumatraPdfViewState &primary,
    const SumatraPdfViewState &fallback);
SumatraPdfViewState captureSumatraPdfViewState(const ForegroundAppWindowContext &context);
QList<Resource> sumatraPdfTitleMatchedPdfResources(
    const ILibraryRepository &repository,
    const QString &documentTitle);
std::optional<Resource> uniqueSumatraPdfTitleMatchedPdfResource(
    const ILibraryRepository &repository,
    const QString &documentTitle);
SumatraPdfForegroundCaptureResult sumatraPdfForegroundCaptureResultForConfirmedPdfFile(
    const QString &documentTitle,
    const QString &filePath,
    const SumatraPdfViewState &viewState = {});
SumatraPdfForegroundCaptureResult captureSumatraPdfForegroundContext(
    const ILibraryRepository &repository,
    const ForegroundAppWindowContext &context);
SumatraPdfForegroundCaptureResult captureSumatraPdfForegroundContext(
    const ILibraryRepository &repository,
    const ForegroundAppWindowContext &context,
    const SumatraPdfViewState &viewState);
} // namespace Pinloom
