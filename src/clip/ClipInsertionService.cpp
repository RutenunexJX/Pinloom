#include "pinloom/clip/ClipInsertionService.h"
#include "pinloom/clip/PlatformPasteInvoker.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QTimer>
#include <cstring>
#include <utility>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

ClipboardTextWriteResult ClipboardTextAccessor::setTextIfUnchanged(const QString &text,
                                                                  quint64 expectedRevision)
{
    if (revision() != expectedRevision) return {ClipboardTextWriteResult::Changed};
    if (!setText(text)) return {};
    return {ClipboardTextWriteResult::Written, revision()};
}

QtClipboardTextAccessor::QtClipboardTextAccessor(QObject *parent)
    : QtClipboardTextAccessor(qobject_cast<QGuiApplication *>(QCoreApplication::instance())
                                  ? QGuiApplication::clipboard() : nullptr,
                              parent)
{}

QtClipboardTextAccessor::QtClipboardTextAccessor(QClipboard *clipboard, QObject *parent)
    : ClipboardTextAccessor(parent)
    , clipboard_(clipboard)
{
    if (clipboard_) {
        connect(clipboard_, &QClipboard::dataChanged, this, [this]() { ++revision_; });
    }
}

QtClipboardTextAccessor::~QtClipboardTextAccessor()
{
#ifdef Q_OS_WIN
    if (nativeClipboardOwner_) DestroyWindow(reinterpret_cast<HWND>(nativeClipboardOwner_));
#endif
}

QString QtClipboardTextAccessor::text() const
{
    return clipboard_ ? clipboard_->text(QClipboard::Clipboard) : QString();
}

quint64 QtClipboardTextAccessor::revision() const
{
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == QLatin1String("windows")) {
        return GetClipboardSequenceNumber();
    }
#endif
    return revision_;
}

bool QtClipboardTextAccessor::setText(const QString &text)
{
    if (!clipboard_) {
        return false;
    }

    clipboard_->setText(text, QClipboard::Clipboard);
    return true;
}

ClipboardTextWriteResult QtClipboardTextAccessor::setTextIfUnchanged(const QString &text,
                                                                    quint64 expectedRevision)
{
    if (!clipboard_) return {};
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == QLatin1String("windows")) {
        // Hold the Windows clipboard lock across the revision check and text write.
        // A text comparison alone misses same-text copies and changes away and back.
        const SIZE_T bytes = (static_cast<SIZE_T>(text.size()) + 1) * sizeof(wchar_t);
        HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!data) return {};
        void *buffer = GlobalLock(data);
        if (!buffer) {
            GlobalFree(data);
            return {};
        }
        std::memcpy(buffer, text.utf16(), bytes);
        GlobalUnlock(data);
        if (!nativeClipboardOwner_) {
            nativeClipboardOwner_ = reinterpret_cast<quintptr>(CreateWindowExW(
                0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                GetModuleHandleW(nullptr), nullptr));
        }
        HWND owner = reinterpret_cast<HWND>(nativeClipboardOwner_);
        ClipboardTextWriteResult result;
        if (owner && OpenClipboard(owner)) {
            if (GetClipboardSequenceNumber() != expectedRevision) {
                result.status = ClipboardTextWriteResult::Changed;
            } else if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, data)) {
                data = nullptr; // The system owns the allocation after SetClipboardData.
                result.status = ClipboardTextWriteResult::Written;
            }
            CloseClipboard();
            if (result.status == ClipboardTextWriteResult::Written) {
                // Closing finalizes synthesized text formats and advances the sequence.
                // Keep the owner window alive; destroying it also changes clipboard state.
                result.revision = GetClipboardSequenceNumber();
                if (GetClipboardOwner() != owner) result.status = ClipboardTextWriteResult::Changed;
            }
        }
        if (data) GlobalFree(data);
        return result;
    }
#endif
    return ClipboardTextAccessor::setTextIfUnchanged(text, expectedRevision);
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
    , pasteInvoker_(createPlatformPasteInvoker())
{
}

ClipInsertionService::ClipInsertionService(SqliteClipRepository &repository, QObject *parent)
    : QObject(parent)
    , clipboard_(new QtClipboardTextAccessor(this))
    , findClip_(findClipCallback(repository))
    , markClipUsed_(markClipUsedCallback(repository))
    , repositoryError_(repositoryErrorCallback(repository))
    , pasteInvoker_(createPlatformPasteInvoker())
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

ClipInsertionService::~ClipInsertionService()
{
    restoreClipboardIfUnchanged(clipboardWriteGeneration_);
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

    quint64 beforeRead = 0;
    quint64 currentRevision = 0;
    QString currentClipboardText;
    // Reading a delayed-rendered Windows format can itself advance the revision.
    for (int attempt = 0; attempt < 3; ++attempt) {
        beforeRead = clipboard_->revision();
        currentClipboardText = clipboard_->text();
        currentRevision = clipboard_->revision();
        if (beforeRead == currentRevision) break;
    }
    if (beforeRead != currentRevision) {
        return fail(ClipInsertionStatus::ClipboardWriteFailed, clipId,
                    QStringLiteral("Clipboard changed while preparing the insertion; retry"));
    }
    const QString originalClipboardText = pendingClipboardRestore_
            && pendingClipboardRestore_->revision == currentRevision
        ? pendingClipboardRestore_->originalText : currentClipboardText;
    suppressNextClipboardCapture();
    const ClipboardTextWriteResult write = clipboard_
        ? clipboard_->setTextIfUnchanged(clip.text, currentRevision) : ClipboardTextWriteResult{};
    if (write.status != ClipboardTextWriteResult::Written) {
        return fail(ClipInsertionStatus::ClipboardWriteFailed,
                    clipId,
                    QStringLiteral("Unable to write clip text to the clipboard"));
    }

    const quint64 clipboardWriteGeneration = ++clipboardWriteGeneration_;
    lastOwnClipboardRevision_ = write.revision;
    pendingClipboardRestore_ = PendingClipboardRestore{
        originalClipboardText, write.revision, clipboardWriteGeneration};

    if (!pasteInvoker_()) {
        if (options_.restoreOriginalClipboardOnFailure
            && !restoreClipboardIfUnchanged(clipboardWriteGeneration)) {
            return fail(ClipInsertionStatus::ClipboardRestoreFailed,
                        clipId,
                        QStringLiteral("Paste invocation failed and the original clipboard could not be restored"));
        }
        if (!options_.restoreOriginalClipboardOnFailure
            && pendingClipboardRestore_
            && pendingClipboardRestore_->generation == clipboardWriteGeneration) {
            pendingClipboardRestore_.reset();
        }
        return fail(ClipInsertionStatus::PasteFailed, clipId, QStringLiteral("Paste invocation failed"));
    }

    if (options_.markClipUsedOnSuccess && markClipUsed_ && !clipId.isEmpty()) {
        markClipUsed_(clipId, {});
    }

    if (options_.restoreOriginalClipboardOnSuccess) {
        const int restoreDelayMs = qMax(0, options_.clipboardRestoreDelayMs);
        if (restoreDelayMs == 0
            && !restoreClipboardIfUnchanged(clipboardWriteGeneration)) {
            return fail(ClipInsertionStatus::ClipboardRestoreFailed,
                        clipId,
                        QStringLiteral("Unable to restore the original clipboard text"));
        }

        if (restoreDelayMs > 0) {
            QTimer::singleShot(restoreDelayMs,
                               this,
                               [this, clipId, clipboardWriteGeneration]() {
                                   if (!restoreClipboardIfUnchanged(clipboardWriteGeneration)) {
                                       fail(ClipInsertionStatus::ClipboardRestoreFailed, clipId,
                                            QStringLiteral("Unable to restore the original clipboard text"));
                                   }
                               });
        }
    } else if (pendingClipboardRestore_
               && pendingClipboardRestore_->generation == clipboardWriteGeneration) {
        pendingClipboardRestore_.reset();
    }

    return succeed(clip);
}

bool ClipInsertionService::ownsCurrentClipboardWrite() const
{
    return clipboard_ && lastOwnClipboardRevision_
        && clipboard_->revision() == *lastOwnClipboardRevision_;
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

bool ClipInsertionService::restoreClipboardIfUnchanged(quint64 generation)
{
    if (!pendingClipboardRestore_ || pendingClipboardRestore_->generation != generation) {
        return true;
    }
    const PendingClipboardRestore pending = *pendingClipboardRestore_;
    if (!clipboard_ || clipboard_->revision() != pending.revision) {
        pendingClipboardRestore_.reset();
        return true;
    }
    suppressNextClipboardCapture();
    if (!pendingClipboardRestore_ || pendingClipboardRestore_->generation != generation) return true;
    const ClipboardTextWriteResult result = clipboard_
        ? clipboard_->setTextIfUnchanged(pending.originalText, pending.revision)
        : ClipboardTextWriteResult{ClipboardTextWriteResult::Changed};
    if (result.status == ClipboardTextWriteResult::Failed) return false;
    if (result.status == ClipboardTextWriteResult::Written) lastOwnClipboardRevision_ = result.revision;
    pendingClipboardRestore_.reset();
    return true;
}

} // namespace Pinloom
