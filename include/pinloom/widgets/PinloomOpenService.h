#pragma once

#include "pinloom/core/ApplicationLaunchSettings.h"
#include "pinloom/core/ExcelCommand.h"
#include "pinloom/core/LibraryRepository.h"
#include "pinloom/core/PowerPointCommand.h"
#include "pinloom/core/VisioCommand.h"
#include "pinloom/core/WordCommand.h"
#include "pinloom/widgets/PdfViewerAdapter.h"
#include "pinloom/widgets/PinloomEntry.h"

#include <QObject>
#include <functional>
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
    PdfViewerAdapter *pdfViewerAdapter = nullptr;
    QString pdfPresentationCacheDirectory;
    std::function<bool(const QString &sourceFilePath,
                       int page,
                       const QString &reason)> pdfOriginalFallbackPrompt;
};

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
    bool openPdf(const PinloomOpenTarget &target,
                 QWidget *dialogParent);
    void handlePdfViewerOpen(const PinloomOpenTarget &target,
                             const PdfViewerOpenResult &result);
    void setStatus(const QString &status);
    void recordOpen(const PinloomOpenTarget &target);

    ILibraryRepository &repository_;
    PinloomOpenServiceOptions options_;
    QString statusText_;
    quint64 activePdfOpenRequestId_ = 0;
    std::optional<PinloomOpenTarget> activePdfOpenTarget_;
    std::optional<PinloomOpenTarget> pdfOriginalFallbackTarget_;
    QString pdfOriginalFallbackReason_;
};

} // namespace Pinloom
