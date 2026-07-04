#include "pinloom/clip/ClipInsertionService.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <utility>

namespace Pinloom {

namespace {

ClipInsertionService::FindClipCallback findClipCallback(InMemoryClipRepository &repository)
{
    return [&repository](const QString &clipId) {
        return repository.findClip(clipId);
    };
}

ClipInsertionService::FindClipCallback findClipCallback(SqliteClipRepository &repository)
{
    return [&repository](const QString &clipId) {
        return repository.findClip(clipId);
    };
}

ClipInsertionService::MarkClipUsedCallback markClipUsedCallback(InMemoryClipRepository &repository)
{
    return [&repository](const QString &clipId, const QDateTime &now) {
        return repository.markClipUsed(clipId, now);
    };
}

ClipInsertionService::MarkClipUsedCallback markClipUsedCallback(SqliteClipRepository &repository)
{
    return [&repository](const QString &clipId, const QDateTime &now) {
        return repository.markClipUsed(clipId, now);
    };
}

ClipInsertionService::RepositoryErrorCallback repositoryErrorCallback(SqliteClipRepository &repository)
{
    return [&repository]() {
        return repository.lastError();
    };
}

QString repositoryError(const ClipInsertionService::RepositoryErrorCallback &repositoryError)
{
    return repositoryError ? repositoryError() : QString();
}

} // namespace

bool ClipInsertionResult::inserted() const
{
    return status == ClipInsertionStatus::Inserted;
}

ClipboardTextAccessor::ClipboardTextAccessor(QObject *parent)
    : QObject(parent)
{
}

bool ClipboardTextAccessor::isAvailable() const
{
    return true;
}

QtClipboardTextAccessor::QtClipboardTextAccessor(QObject *parent)
    : ClipboardTextAccessor(parent)
{
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        clipboard_ = QGuiApplication::clipboard();
    }
}

QtClipboardTextAccessor::QtClipboardTextAccessor(QClipboard *clipboard, QObject *parent)
    : ClipboardTextAccessor(parent)
    , clipboard_(clipboard)
{
}

QString QtClipboardTextAccessor::text() const
{
    return clipboard_ ? clipboard_->text(QClipboard::Clipboard) : QString();
}

bool QtClipboardTextAccessor::setText(const QString &text)
{
    if (!clipboard_) {
        return false;
    }

    clipboard_->setText(text, QClipboard::Clipboard);
    return true;
}

bool QtClipboardTextAccessor::isAvailable() const
{
    return clipboard_ != nullptr;
}

ClipInsertionService::ClipInsertionService(InMemoryClipRepository &repository, QObject *parent)
    : QObject(parent)
    , clipboard_(new QtClipboardTextAccessor(this))
    , findClip_(findClipCallback(repository))
    , markClipUsed_(markClipUsedCallback(repository))
{
}

ClipInsertionService::ClipInsertionService(SqliteClipRepository &repository, QObject *parent)
    : QObject(parent)
    , clipboard_(new QtClipboardTextAccessor(this))
    , findClip_(findClipCallback(repository))
    , markClipUsed_(markClipUsedCallback(repository))
    , repositoryError_(repositoryErrorCallback(repository))
{
}

ClipInsertionService::ClipInsertionService(ClipboardTextAccessor *clipboard,
                                           InMemoryClipRepository &repository,
                                           PasteInvoker pasteInvoker,
                                           QObject *parent)
    : ClipInsertionService(clipboard,
                           findClipCallback(repository),
                           markClipUsedCallback(repository),
                           {},
                           std::move(pasteInvoker),
                           parent)
{
}

ClipInsertionService::ClipInsertionService(ClipboardTextAccessor *clipboard,
                                           SqliteClipRepository &repository,
                                           PasteInvoker pasteInvoker,
                                           QObject *parent)
    : ClipInsertionService(clipboard,
                           findClipCallback(repository),
                           markClipUsedCallback(repository),
                           repositoryErrorCallback(repository),
                           std::move(pasteInvoker),
                           parent)
{
}

ClipInsertionService::ClipInsertionService(ClipboardTextAccessor *clipboard,
                                           FindClipCallback findClip,
                                           PasteInvoker pasteInvoker,
                                           QObject *parent)
    : ClipInsertionService(clipboard, std::move(findClip), {}, {}, std::move(pasteInvoker), parent)
{
}

ClipInsertionService::ClipInsertionService(ClipboardTextAccessor *clipboard,
                                           FindClipCallback findClip,
                                           MarkClipUsedCallback markClipUsed,
                                           RepositoryErrorCallback repositoryError,
                                           PasteInvoker pasteInvoker,
                                           QObject *parent)
    : QObject(parent)
    , clipboard_(clipboard)
    , findClip_(std::move(findClip))
    , markClipUsed_(std::move(markClipUsed))
    , repositoryError_(std::move(repositoryError))
    , pasteInvoker_(std::move(pasteInvoker))
{
}

void ClipInsertionService::setOptions(const ClipInsertionOptions &options)
{
    options_ = options;
}

ClipInsertionOptions ClipInsertionService::options() const
{
    return options_;
}

void ClipInsertionService::setPasteInvoker(PasteInvoker pasteInvoker)
{
    pasteInvoker_ = std::move(pasteInvoker);
}

void ClipInsertionService::setSuppressClipboardCaptureCallback(SuppressClipboardCaptureCallback suppressClipboardCapture)
{
    suppressClipboardCapture_ = std::move(suppressClipboardCapture);
}

ClipInsertionResult ClipInsertionService::insertClip(const QString &clipId)
{
    const QString trimmedClipId = clipId.trimmed();
    if (trimmedClipId.isEmpty()) {
        return fail(ClipInsertionStatus::MissingClip, trimmedClipId, QStringLiteral("Clip id is required"));
    }

    if (!findClip_) {
        return fail(ClipInsertionStatus::MissingClip, trimmedClipId, QStringLiteral("Clip lookup callback is required"));
    }

    const std::optional<Clip> clip = findClip_(trimmedClipId);
    if (!clip.has_value()) {
        const QString error = repositoryError(repositoryError_);
        return fail(ClipInsertionStatus::MissingClip,
                    trimmedClipId,
                    error.isEmpty() ? QStringLiteral("Clip not found") : error);
    }

    return insertClip(clip.value());
}

ClipInsertionResult ClipInsertionService::insertClip(const Clip &clip)
{
    const QString clipId = clip.id.trimmed();
    if (clip.kind != ClipKind::Text) {
        return fail(ClipInsertionStatus::UnsupportedKind, clipId, QStringLiteral("Only text clips can be inserted"));
    }

    if (clip.text.trimmed().isEmpty()) {
        return fail(ClipInsertionStatus::EmptyText, clipId, QStringLiteral("Clip text is empty"));
    }

    if (!clipboard_ || !clipboard_->isAvailable()) {
        return fail(ClipInsertionStatus::ClipboardUnavailable, clipId, QStringLiteral("Clipboard is unavailable"));
    }

    if (!pasteInvoker_) {
        return fail(ClipInsertionStatus::PasteFailed, clipId, QStringLiteral("Paste invoker is required"));
    }

    const QString originalClipboardText = clipboard_->text();
    suppressNextClipboardCapture();
    if (!clipboard_->setText(clip.text)) {
        return fail(ClipInsertionStatus::ClipboardWriteFailed,
                    clipId,
                    QStringLiteral("Unable to write clip text to the clipboard"));
    }

    if (!pasteInvoker_()) {
        return fail(ClipInsertionStatus::PasteFailed, clipId, QStringLiteral("Paste invocation failed"));
    }

    if (options_.markClipUsedOnSuccess && markClipUsed_ && !clipId.isEmpty()) {
        markClipUsed_(clipId, {});
    }

    if (options_.restoreOriginalClipboardOnSuccess) {
        suppressNextClipboardCapture();
        if (!clipboard_->setText(originalClipboardText)) {
            return fail(ClipInsertionStatus::ClipboardRestoreFailed,
                        clipId,
                        QStringLiteral("Unable to restore the original clipboard text"));
        }
    }

    return succeed(clip);
}

QString ClipInsertionService::lastInsertedId() const
{
    return lastInsertedId_;
}

std::optional<Clip> ClipInsertionService::lastInsertedClip() const
{
    return lastInsertedClip_;
}

ClipInsertionStatus ClipInsertionService::lastStatus() const
{
    return lastStatus_;
}

QString ClipInsertionService::lastError() const
{
    return lastError_;
}

ClipInsertionResult ClipInsertionService::fail(ClipInsertionStatus status, const QString &clipId, const QString &error)
{
    setLastStatus(status);
    setLastError(error);
    emit insertionFailed(status, error);
    return {status, clipId, error};
}

ClipInsertionResult ClipInsertionService::succeed(const Clip &clip)
{
    lastInsertedId_ = clip.id;
    lastInsertedClip_ = clip;
    setLastStatus(ClipInsertionStatus::Inserted);
    setLastError({});
    emit inserted(clip);
    return {ClipInsertionStatus::Inserted, clip.id, {}};
}

void ClipInsertionService::setLastStatus(ClipInsertionStatus status)
{
    if (lastStatus_ == status) {
        return;
    }

    lastStatus_ = status;
    emit statusChanged(lastStatus_);
}

void ClipInsertionService::setLastError(const QString &error)
{
    if (lastError_ == error) {
        return;
    }

    lastError_ = error;
    emit errorChanged(lastError_);
}

void ClipInsertionService::suppressNextClipboardCapture()
{
    if (suppressClipboardCapture_) {
        suppressClipboardCapture_();
    }
}

} // namespace Pinloom
