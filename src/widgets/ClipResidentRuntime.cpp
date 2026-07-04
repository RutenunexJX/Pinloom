#include "pinloom/widgets/ClipResidentRuntime.h"

#include <utility>

namespace Pinloom {

namespace {

QString nonEmptyError(const QString &error, const QString &fallback)
{
    const QString trimmed = error.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

} // namespace

ClipResidentRuntime::ClipResidentRuntime(InMemoryClipRepository &repository,
                                         ClipResidentRuntimeDependencies dependencies,
                                         QObject *parent)
    : ClipResidentRuntime(repository, std::move(dependencies), {}, parent)
{
}

ClipResidentRuntime::ClipResidentRuntime(InMemoryClipRepository &repository,
                                         ClipResidentRuntimeDependencies dependencies,
                                         ClipResidentRuntimeOptions options,
                                         QObject *parent)
    : QObject(parent)
    , options_(options)
    , searchService_(repository)
    , captureService_(dependencies.captureClipboard, repository, this)
    , insertionService_(dependencies.insertionClipboard, repository, std::move(dependencies.pasteInvoker), this)
    , hotkeyService_(dependencies.hotkeyBackend, this)
    , trayController_(hotkeyService_,
                      ClipTrayControllerOptions{[this]() {
                                                    showPicker();
                                                },
                                                [this](bool paused) {
                                                    captureService_.setCapturePaused(paused);
                                                },
                                                false},
                      this)
{
    configure(dependencies.trayBackend);
}

ClipResidentRuntime::ClipResidentRuntime(SqliteClipRepository &repository,
                                         ClipResidentRuntimeDependencies dependencies,
                                         QObject *parent)
    : ClipResidentRuntime(repository, std::move(dependencies), {}, parent)
{
}

ClipResidentRuntime::ClipResidentRuntime(SqliteClipRepository &repository,
                                         ClipResidentRuntimeDependencies dependencies,
                                         ClipResidentRuntimeOptions options,
                                         QObject *parent)
    : QObject(parent)
    , options_(options)
    , searchService_(repository)
    , captureService_(dependencies.captureClipboard, repository, this)
    , insertionService_(dependencies.insertionClipboard, repository, std::move(dependencies.pasteInvoker), this)
    , hotkeyService_(dependencies.hotkeyBackend, this)
    , trayController_(hotkeyService_,
                      ClipTrayControllerOptions{[this]() {
                                                    showPicker();
                                                },
                                                [this](bool paused) {
                                                    captureService_.setCapturePaused(paused);
                                                },
                                                false},
                      this)
{
    configure(dependencies.trayBackend);
}

ClipResidentRuntime::~ClipResidentRuntime()
{
    stop();
}

bool ClipResidentRuntime::start()
{
    if (running_) {
        return true;
    }

    quitRequested_ = false;

    if (!trayPresenter_) {
        setLastError(QStringLiteral("Tray backend is required"));
        return false;
    }

    if (!captureService_.start()) {
        setLastError(nonEmptyError(captureService_.lastError(), QStringLiteral("Unable to start clipboard capture")));
        return false;
    }

    if (!trayController_.start()) {
        captureService_.stop();
        setLastError(nonEmptyError(trayController_.lastError(), QStringLiteral("Unable to start clip tray runtime")));
        return false;
    }

    if (options_.showTrayOnStart) {
        trayPresenter_->show();
    }

    setLastError({});
    setRunning(true);
    return true;
}

void ClipResidentRuntime::stop()
{
    const bool hadRuntimeWork = running_ || captureService_.isRunning() || trayController_.isRunning();
    if (!hadRuntimeWork) {
        return;
    }

    trayController_.stop();
    captureService_.stop();

    if (options_.hidePickerOnStop && pickerPanel_) {
        pickerPanel_->hide();
    }

    if (options_.hideTrayOnStop && trayPresenter_) {
        trayPresenter_->hide();
    }

    setRunning(false);
}

bool ClipResidentRuntime::isRunning() const
{
    return running_;
}

void ClipResidentRuntime::showPicker()
{
    if (!pickerPanel_) {
        setLastError(QStringLiteral("Clip picker panel is required"));
        return;
    }

    pickerPanel_->show();
    pickerPanel_->raise();
    pickerPanel_->activateWindow();
    pickerPanel_->focusSearch();
    ++pickerShownCount_;
    emit pickerShown();
}

int ClipResidentRuntime::pickerShownCount() const
{
    return pickerShownCount_;
}

void ClipResidentRuntime::requestQuit()
{
    handleTrayQuitRequested();
}

bool ClipResidentRuntime::quitWasRequested() const
{
    return quitRequested_;
}

QString ClipResidentRuntime::lastError() const
{
    return lastError_;
}

ClipboardCaptureService &ClipResidentRuntime::captureService()
{
    return captureService_;
}

const ClipboardCaptureService &ClipResidentRuntime::captureService() const
{
    return captureService_;
}

ClipSearchService &ClipResidentRuntime::searchService()
{
    return searchService_;
}

const ClipSearchService &ClipResidentRuntime::searchService() const
{
    return searchService_;
}

ClipInsertionService &ClipResidentRuntime::insertionService()
{
    return insertionService_;
}

const ClipInsertionService &ClipResidentRuntime::insertionService() const
{
    return insertionService_;
}

ClipHotkeyService &ClipResidentRuntime::hotkeyService()
{
    return hotkeyService_;
}

const ClipHotkeyService &ClipResidentRuntime::hotkeyService() const
{
    return hotkeyService_;
}

ClipTrayController &ClipResidentRuntime::trayController()
{
    return trayController_;
}

const ClipTrayController &ClipResidentRuntime::trayController() const
{
    return trayController_;
}

ClipTrayPresenter *ClipResidentRuntime::trayPresenter()
{
    return trayPresenter_.get();
}

const ClipTrayPresenter *ClipResidentRuntime::trayPresenter() const
{
    return trayPresenter_.get();
}

ClipPickerPanel &ClipResidentRuntime::pickerPanel()
{
    return *pickerPanel_;
}

const ClipPickerPanel &ClipResidentRuntime::pickerPanel() const
{
    return *pickerPanel_;
}

void ClipResidentRuntime::configure(ClipTrayBackend *trayBackend)
{
    insertionService_.setOptions(options_.insertionOptions);
    insertionService_.setSuppressClipboardCaptureCallback([this]() {
        captureService_.suppressNextChange();
    });

    ClipPickerOptions pickerOptions;
    pickerOptions.searchOptions = options_.pickerSearchOptions;
    pickerOptions.insertionHandler = makeClipPickerInsertionHandler(insertionService_);
    pickerOptions.closeOnActivationSuccess = options_.closePickerOnActivationSuccess;
    pickerPanel_ = std::make_unique<ClipPickerPanel>(searchService_, pickerOptions);
    pickerPanel_->setAttribute(Qt::WA_DeleteOnClose, false);

    if (trayBackend) {
        trayPresenter_ = std::make_unique<ClipTrayPresenter>(trayController_, *trayBackend, this);
    }

    connect(&trayController_, &ClipTrayController::quitRequested, this, &ClipResidentRuntime::handleTrayQuitRequested);
}

void ClipResidentRuntime::handleTrayQuitRequested()
{
    quitRequested_ = true;
    emit quitRequested();
    if (options_.stopOnQuitRequested) {
        stop();
    }
}

void ClipResidentRuntime::setRunning(bool running)
{
    if (running_ == running) {
        return;
    }

    running_ = running;
    emit runningChanged(running_);
}

void ClipResidentRuntime::setLastError(const QString &error)
{
    if (lastError_ == error) {
        return;
    }

    lastError_ = error;
    emit errorChanged(lastError_);
}

} // namespace Pinloom
