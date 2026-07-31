#include "pinloom/clip/ClipboardCaptureService.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <utility>

namespace Pinloom {

namespace {

ClipboardCaptureService::CaptureTextCallback captureCallback(InMemoryClipRepository &repository)
{
    return [&repository](const QString &text,
                         const ClipCapturePolicy &policy,
                         const QString &sourceApp,
                         const QDateTime &now) {
        return repository.captureText(text, policy, sourceApp, now);
    };
}

ClipboardCaptureService::CaptureTextCallback captureCallback(SqliteClipRepository &repository)
{
    return [&repository](const QString &text,
                         const ClipCapturePolicy &policy,
                         const QString &sourceApp,
                         const QDateTime &now) {
        return repository.captureText(text, policy, sourceApp, now);
    };
}

ClipboardCaptureService::RepositoryErrorCallback repositoryErrorCallback(SqliteClipRepository &repository)
{
    return [&repository]() {
        return repository.lastError();
    };
}

} // namespace

ClipboardTextSource::ClipboardTextSource(QObject *parent)
    : QObject(parent)
{
}

bool ClipboardTextSource::isAvailable() const
{
    return true;
}

QtClipboardTextSource::QtClipboardTextSource(QObject *parent)
    : ClipboardTextSource(parent)
{
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        clipboard_ = QGuiApplication::clipboard();
    }

    if (clipboard_) {
        connect(clipboard_, &QClipboard::dataChanged, this, &ClipboardTextSource::textChanged);
    }
}

QtClipboardTextSource::QtClipboardTextSource(QClipboard *clipboard, QObject *parent)
    : ClipboardTextSource(parent)
    , clipboard_(clipboard)
{
    if (clipboard_) {
        connect(clipboard_, &QClipboard::dataChanged, this, &ClipboardTextSource::textChanged);
    }
}

QString QtClipboardTextSource::text() const
{
    return clipboard_ ? clipboard_->text(QClipboard::Clipboard) : QString();
}

bool QtClipboardTextSource::isAvailable() const
{
    return clipboard_ != nullptr;
}

ClipboardCaptureService::ClipboardCaptureService(InMemoryClipRepository &repository, QObject *parent)
    : QObject(parent)
    , clipboard_(new QtClipboardTextSource(this))
    , captureText_(captureCallback(repository))
{
}

ClipboardCaptureService::ClipboardCaptureService(SqliteClipRepository &repository, QObject *parent)
    : QObject(parent)
    , clipboard_(new QtClipboardTextSource(this))
    , captureText_(captureCallback(repository))
    , repositoryError_(repositoryErrorCallback(repository))
{
}

ClipboardCaptureService::ClipboardCaptureService(ClipboardTextSource *clipboard,
                                                 InMemoryClipRepository &repository,
                                                 QObject *parent)
    : ClipboardCaptureService(clipboard, captureCallback(repository), parent)
{
}

ClipboardCaptureService::ClipboardCaptureService(ClipboardTextSource *clipboard,
                                                 SqliteClipRepository &repository,
                                                 QObject *parent)
    : ClipboardCaptureService(clipboard, captureCallback(repository), repositoryErrorCallback(repository), parent)
{
}

ClipboardCaptureService::ClipboardCaptureService(ClipboardTextSource *clipboard,
                                                 CaptureTextCallback captureText,
                                                 QObject *parent)
    : ClipboardCaptureService(clipboard, std::move(captureText), {}, parent)
{
}

ClipboardCaptureService::ClipboardCaptureService(ClipboardTextSource *clipboard,
                                                 CaptureTextCallback captureText,
                                                 RepositoryErrorCallback repositoryError,
                                                 QObject *parent)
    : QObject(parent)
    , clipboard_(clipboard)
    , captureText_(std::move(captureText))
    , repositoryError_(std::move(repositoryError))
{
}

bool ClipboardCaptureService::start()
{
    if (running_) {
        return true;
    }

    if (!clipboard_) {
        setLastError(QStringLiteral("Clipboard text source is required"));
        return false;
    }

    if (!clipboard_->isAvailable()) {
        setLastError(QStringLiteral("Clipboard text source is unavailable"));
        return false;
    }

    if (!captureText_) {
        setLastError(QStringLiteral("Clip capture callback is required"));
        return false;
    }

    clipboardConnection_ = connect(clipboard_,
                                   &ClipboardTextSource::textChanged,
                                   this,
                                   &ClipboardCaptureService::handleClipboardTextChanged);
    running_ = true;
    suppressNextChange_ = false;
    setLastError({});
    emit runningChanged(running_);
    return true;
}

void ClipboardCaptureService::stop()
{
    if (!running_) {
        return;
    }

    disconnect(clipboardConnection_);
    clipboardConnection_ = {};
    running_ = false;
    suppressNextChange_ = false;
    emit runningChanged(running_);
}

bool ClipboardCaptureService::isRunning() const
{
    return running_;
}

void ClipboardCaptureService::pauseCapture()
{
    setCapturePaused(true);
}

void ClipboardCaptureService::resumeCapture()
{
    setCapturePaused(false);
}

void ClipboardCaptureService::setCapturePaused(bool paused)
{
    if (policy_.capturePaused == paused) {
        return;
    }

    policy_.capturePaused = paused;
    emit capturePausedChanged(paused);
}

bool ClipboardCaptureService::capturePaused() const
{
    return policy_.capturePaused;
}

void ClipboardCaptureService::setPolicy(const ClipCapturePolicy &policy)
{
    const bool pausedChanged = policy_.capturePaused != policy.capturePaused;
    policy_ = policy;
    if (pausedChanged) {
        emit capturePausedChanged(policy_.capturePaused);
    }
}

ClipCapturePolicy ClipboardCaptureService::policy() const
{
    return policy_;
}

void ClipboardCaptureService::setSourceApp(const QString &sourceApp)
{
    sourceApp_ = sourceApp.trimmed();
}

QString ClipboardCaptureService::sourceApp() const
{
    return sourceApp_;
}

void ClipboardCaptureService::setSourceAppProvider(SourceAppProvider provider)
{
    sourceAppProvider_ = std::move(provider);
}

void ClipboardCaptureService::suppressNextChange()
{
    suppressNextChange_ = true;
}

bool ClipboardCaptureService::suppressingNextChange() const
{
    return suppressNextChange_;
}

QString ClipboardCaptureService::lastCapturedId() const
{
    return lastCapturedId_;
}

std::optional<Clip> ClipboardCaptureService::lastCapturedClip() const
{
    return lastCapturedClip_;
}

ClipCaptureStatus ClipboardCaptureService::lastStatus() const
{
    return lastStatus_;
}

QString ClipboardCaptureService::lastError() const
{
    return lastError_;
}

void ClipboardCaptureService::captureCurrentText()
{
    if (!running_) {
        return;
    }

    if (!clipboard_ || !clipboard_->isAvailable()) {
        setLastError(QStringLiteral("Clipboard text source is unavailable"));
        return;
    }

    recordCaptureText(clipboard_->text());
}

void ClipboardCaptureService::handleClipboardTextChanged()
{
    if (!running_) {
        return;
    }

    if (suppressNextChange_) {
        suppressNextChange_ = false;
        setLastError({});
        return;
    }

    captureCurrentText();
}

void ClipboardCaptureService::recordCaptureText(const QString &text)
{
    const QString sourceApp = sourceAppProvider_
        ? sourceAppProvider_().trimmed()
        : sourceApp_;
    const ClipCaptureResult result = captureText_(text, policy_, sourceApp, {});
    lastStatus_ = result.status;

    const QString repositoryError = repositoryError_ ? repositoryError_() : QString();
    if (!repositoryError.isEmpty()) {
        setLastError(repositoryError);
    } else {
        setLastError({});
    }

    if (result.captured()) {
        lastCapturedClip_ = result.clip;
        lastCapturedId_ = result.clip->id;
        emit captured(result.clip.value());
        return;
    }

    emit captureIgnored(result.status);
}

void ClipboardCaptureService::setLastError(const QString &error)
{
    if (lastError_ == error) {
        return;
    }

    lastError_ = error;
    emit errorChanged(lastError_);
}

} // namespace Pinloom
