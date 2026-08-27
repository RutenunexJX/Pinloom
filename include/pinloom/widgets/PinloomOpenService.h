#pragma once

#include "pinloom/core/ApplicationLaunchSettings.h"
#include "pinloom/core/ExcelCommand.h"
#include "pinloom/core/LibraryRepository.h"
#include "pinloom/core/PowerPointCommand.h"
#include "pinloom/core/SumatraPdfCommand.h"
#include "pinloom/core/SumatraPdfDdeClient.h"
#include "pinloom/core/VisioCommand.h"
#include "pinloom/core/WordCommand.h"
#include "pinloom/widgets/PinloomEntry.h"
#include "pinloom/widgets/SumatraPdfRegionCaptureOverlay.h"

#include <QElapsedTimer>
#include <QObject>
#include <functional>

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
    std::function<bool(const SumatraPdfCommand &,
                       const SumatraPdfDdeFileState &lastState,
                       QString *error)> sumatraPdfRetryHandler;
    std::function<bool(const SumatraPdfPersistentHighlight &)>
        sumatraPdfHighlightHandler;
    int sumatraPdfVerificationTimeoutMilliseconds = 4200;
    int sumatraPdfVerificationPollMilliseconds = 180;
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

signals:
    void statusChanged(const QString &status);

private:
    bool openExcel(const PinloomOpenTarget &target);
    bool openVisio(const PinloomOpenTarget &target);
    bool openWord(const PinloomOpenTarget &target);
    bool openPowerPoint(const PinloomOpenTarget &target);
    bool openSumatraPdf(const PinloomOpenTarget &target);
    void verifySumatraPdfJump(const PinloomOpenTarget &target,
                              const SumatraPdfCommand &command,
                              quint64 generation,
                              int elapsedMilliseconds,
                              bool positioningIssued,
                              int consecutiveMatches,
                              const QString &lastDiagnostics = {});
    void handleSumatraPdfVerificationState(
        const PinloomOpenTarget &target,
        const SumatraPdfCommand &command,
        quint64 generation,
        int elapsedMilliseconds,
        bool positioningIssued,
        int consecutiveMatches,
        const QString &lastDiagnostics,
        const SumatraPdfDdeFileState &state);
    void registerSumatraPdfHighlight(const PinloomOpenTarget &target,
                                     const SumatraPdfCommand &command,
                                     const SumatraPdfDdeFileState &state = {});
    void setStatus(const QString &status);
    void recordOpen(const PinloomOpenTarget &target);

    ILibraryRepository &repository_;
    PinloomOpenServiceOptions options_;
    QString statusText_;
    quint64 sumatraPdfVerificationGeneration_ = 0;
    QElapsedTimer sumatraPdfVerificationTimer_;
};

} // namespace Pinloom
