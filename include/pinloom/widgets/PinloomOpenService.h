#pragma once

#include "pinloom/core/ApplicationLaunchSettings.h"
#include "pinloom/core/ExcelCommand.h"
#include "pinloom/core/LibraryRepository.h"
#include "pinloom/core/PowerPointCommand.h"
#include "pinloom/core/SumatraPdfCommand.h"
#include "pinloom/core/SumatraPdfDdeClient.h"
#include "pinloom/core/VisioCommand.h"
#include "pinloom/core/WordCommand.h"
#include "pinloom/widgets/PdfAnchorPresenter.h"
#include "pinloom/widgets/PinloomEntry.h"

#include <QElapsedTimer>
#include <QObject>
#include <functional>
#include <memory>
#include <optional>

class QWidget;

namespace Pinloom {

struct PinloomOpenServiceOptions {
    std::function<bool(const PinloomOpenTarget &)> hostOpenHandler;
    std::function<bool(const QString &clipId, QString *error)> clipInsertionHandler;
    ApplicationLaunchSettings applicationLaunchSettings;
    std::function<bool(const ExcelJumpCommand &, QString *error)> excelLaunchHandler;
    std::function<bool(const VisioJumpCommand &, QString *error)> visioLaunchHandler;
    std::function<bool(const WordJumpCommand &, QString *error)> wordLaunchHandler;
    std::function<bool(const PowerPointJumpCommand &, QString *error)> powerPointLaunchHandler;
    std::function<QString()> sumatraPdfExecutablePathProvider;
    std::function<bool(const SumatraPdfCommand &, QString *error)> sumatraPdfLaunchHandler;
    std::function<SumatraPdfDdeFileState(int timeoutMilliseconds)>
        sumatraPdfStateProvider;
    bool sumatraPdfStateProviderRunsInWorker = false;
    int sumatraPdfVerificationTimeoutMilliseconds = 4200;
    int sumatraPdfVerificationPollMilliseconds = 180;
    int pdfPresentationGenerationTimeoutMilliseconds = 15000;
    QString pdfPresentationCacheDirectory;
    PdfAnchorPresenter *pdfAnchorPresenter = nullptr;
    std::function<bool(const QString &sourceFilePath,
                       int page,
                       const QString &reason)> pdfOriginalFallbackPrompt;
};

bool sumatraPdfJumpMatches(const SumatraPdfDdeFileState &state,
                           const SumatraPdfCommand &command,
                           QString *diagnostics = nullptr);
QString sumatraPdfRetryDdeCommand(const SumatraPdfCommand &command,
                                  const SumatraPdfDdeFileState &lastState = {});

class PinloomOpenService final : public QObject {
    Q_OBJECT

public:
    PinloomOpenService(ILibraryRepository &repository,
                       PinloomOpenServiceOptions options,
                       QObject *parent = nullptr);

    bool open(const PinloomOpenTarget &target, QWidget *dialogParent = nullptr);
    QString statusText() const;
    bool hasPdfOriginalFallback() const;
    bool openPdfOriginalFallback();

signals:
    void statusChanged(const QString &status);
    void pdfOriginalFallbackAvailable(const QString &sourceFilePath,
                                      int page,
                                      const QString &reason);

private:
    bool openExcel(const PinloomOpenTarget &target);
    bool openVisio(const PinloomOpenTarget &target);
    bool openWord(const PinloomOpenTarget &target);
    bool openPowerPoint(const PinloomOpenTarget &target);
    bool openSumatraPdf(const PinloomOpenTarget &target,
                        QWidget *dialogParent);
    void verifySumatraPdfJump(const SumatraPdfCommand &command,
                              quint64 generation,
                              int elapsedMilliseconds,
                              int consecutiveMatches,
                              const QString &lastDiagnostics = {});
    void handleSumatraPdfVerificationState(
        const SumatraPdfCommand &command,
        quint64 generation,
        int elapsedMilliseconds,
        int consecutiveMatches,
        const QString &lastDiagnostics,
        const SumatraPdfDdeFileState &state);
    void handlePdfAnchorPresentation(
        const PinloomOpenTarget &target,
        const SumatraPdfCommand &sourceCommand,
        const PdfAnchorPresentationResult &result);
    void setStatus(const QString &status);
    void recordOpen(const PinloomOpenTarget &target);

    ILibraryRepository &repository_;
    PinloomOpenServiceOptions options_;
    QString statusText_;
    quint64 sumatraPdfVerificationGeneration_ = 0;
    QElapsedTimer sumatraPdfVerificationTimer_;
    std::unique_ptr<PdfAnchorPresenter> ownedPdfAnchorPresenter_;
    PdfAnchorPresenter *pdfAnchorPresenter_ = nullptr;
    quint64 activePdfPresentationRequestId_ = 0;
    std::optional<SumatraPdfCommand> pdfOriginalFallbackCommand_;
    std::optional<PinloomOpenTarget> pdfOriginalFallbackTarget_;
    QString pdfOriginalFallbackReason_;
};

} // namespace Pinloom
