#pragma once

#include "pinloom/clip/ClipboardCaptureService.h"
#include "pinloom/clip/ClipInsertionService.h"
#include "pinloom/clip/ClipRepository.h"
#include "pinloom/clip/ClipSearch.h"
#include "pinloom/clip/ClipTrayController.h"
#include "pinloom/widgets/ClipTrayPresenter.h"

#include <QObject>
#include <QString>
#include <memory>

namespace Pinloom {

struct ClipResidentRuntimeDependencies {
    ClipboardTextSource *captureClipboard = nullptr;
    ClipboardTextAccessor *insertionClipboard = nullptr;
    ClipTrayBackend *trayBackend = nullptr;
    ClipInsertionService::PasteInvoker pasteInvoker;
};

struct ClipResidentRuntimeOptions {
    ClipInsertionOptions insertionOptions;
    bool showTrayOnStart = true;
    bool hideTrayOnStop = true;
    bool stopOnQuitRequested = true;
};

class ClipResidentRuntime final : public QObject {
    Q_OBJECT

public:
    ClipResidentRuntime(InMemoryClipRepository &repository,
                        ClipResidentRuntimeDependencies dependencies,
                        QObject *parent = nullptr);
    ClipResidentRuntime(InMemoryClipRepository &repository,
                        ClipResidentRuntimeDependencies dependencies,
                        ClipResidentRuntimeOptions options,
                        QObject *parent = nullptr);
    ClipResidentRuntime(SqliteClipRepository &repository,
                        ClipResidentRuntimeDependencies dependencies,
                        QObject *parent = nullptr);
    ClipResidentRuntime(SqliteClipRepository &repository,
                        ClipResidentRuntimeDependencies dependencies,
                        ClipResidentRuntimeOptions options,
                        QObject *parent = nullptr);
    ~ClipResidentRuntime() override;

    bool start();
    void stop();
    bool isRunning() const;

    void requestQuit();
    bool quitWasRequested() const;

    QString lastError() const;

    ClipboardCaptureService &captureService();
    const ClipboardCaptureService &captureService() const;
    ClipSearchService &searchService();
    const ClipSearchService &searchService() const;
    ClipInsertionService &insertionService();
    const ClipInsertionService &insertionService() const;
    ClipTrayController &trayController();
    const ClipTrayController &trayController() const;
    ClipTrayPresenter *trayPresenter();
    const ClipTrayPresenter *trayPresenter() const;
signals:
    void runningChanged(bool running);
    void errorChanged(const QString &error);
    void quitRequested();

private:
    void configure(ClipTrayBackend *trayBackend);
    void handleTrayQuitRequested();
    void setRunning(bool running);
    void setLastError(const QString &error);

    ClipResidentRuntimeOptions options_;
    ClipSearchService searchService_;
    ClipboardCaptureService captureService_;
    ClipInsertionService insertionService_;
    ClipTrayController trayController_;
    std::unique_ptr<ClipTrayPresenter> trayPresenter_;
    QString lastError_;
    bool running_ = false;
    bool quitRequested_ = false;
};

} // namespace Pinloom
