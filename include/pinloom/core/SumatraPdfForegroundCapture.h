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

struct SumatraPdfViewState {
    int currentPage = -1;
    int totalPages = -1;
    double zoom = -1.0;
    QString selectedText;
    QString source;
    QString diagnostics;

    bool hasCurrentPage() const;
    bool hasZoom() const;
    bool hasSelectedText() const;
    bool hasAnyViewState() const;
};

struct SumatraPdfForegroundCaptureResult {
    ManualPdfAnchorCreationRequest request;
    SumatraPdfViewState viewState;
    QString status;
    QString documentTitle;
    QString matchedResourceId;
    QStringList matchedResourceIds;
    bool recognizedSumatraPdf = false;
    bool matchedResource = false;
    bool needsFileConfirmation = false;
    bool resolvedFromTitleMapping = false;
    bool rejectedTitleMapping = false;
    bool confirmedFile = false;

    bool success() const;
};

class SumatraPdfForegroundCaptureProvider {
public:
    using ViewStateProvider = std::function<SumatraPdfViewState(const ForegroundAppWindowContext &context)>;
    using TitlePathProvider = std::function<std::optional<QString>(
        const QString &documentTitle,
        const QString &normalizedTitleKey)>;

    explicit SumatraPdfForegroundCaptureProvider(const ILibraryRepository &repository);
    SumatraPdfForegroundCaptureProvider(const ILibraryRepository &repository,
                                        ViewStateProvider viewStateProvider);
    SumatraPdfForegroundCaptureProvider(const ILibraryRepository &repository,
                                        ViewStateProvider viewStateProvider,
                                        TitlePathProvider titlePathProvider);

    SumatraPdfForegroundCaptureResult capture(const ForegroundAppWindowContext &context) const;
    SumatraPdfForegroundCaptureResult captureCurrentForeground() const;

private:
    const ILibraryRepository &repository_;
    ViewStateProvider viewStateProvider_;
    TitlePathProvider titlePathProvider_;
};

ForegroundAppWindowContext currentForegroundAppWindowContext();
bool isSumatraPdfForegroundWindow(const ForegroundAppWindowContext &context);
QString normalizedSumatraPdfDocumentTitleKey(const QString &documentTitle);
bool isSumatraPdfFullPdfPath(const QString &filePath);
QString sumatraPdfDocumentTitleFromWindowTitle(const QString &windowTitle);
QString sumatraPdfDocumentPathFromWindowTitle(const QString &windowTitle);
SumatraPdfViewState parseSumatraPdfViewStateText(const QString &text,
                                                 const QString &source = QStringLiteral("text"));
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
SumatraPdfForegroundCaptureResult captureSumatraPdfForegroundContext(
    const ILibraryRepository &repository,
    const ForegroundAppWindowContext &context,
    const SumatraPdfViewState &viewState,
    const std::optional<QString> &savedDocumentPath);

} // namespace Pinloom
