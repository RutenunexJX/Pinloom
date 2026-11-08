#pragma once

#include "pinloom/clip/ClipRepository.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>
#include <optional>

class QClipboard;

namespace Pinloom {

enum class ClipInsertionStatus {
    Inserted,
    MissingClip,
    UnsupportedKind,
    EmptyText,
    ClipboardUnavailable,
    ClipboardWriteFailed,
    PasteFailed,
    ClipboardRestoreFailed
};

struct ClipInsertionOptions {
    bool restoreOriginalClipboardOnSuccess = true;
    bool restoreOriginalClipboardOnFailure = true;
    bool markClipUsedOnSuccess = true;
    int clipboardRestoreDelayMs = 200;
};

struct ClipInsertionResult {
    ClipInsertionStatus status = ClipInsertionStatus::MissingClip;
    QString clipId;
    QString error;

    bool inserted() const;
};

struct ClipboardTextWriteResult {
    enum Status { Written, Changed, Failed };
    Status status = Failed;
    quint64 revision = 0;
};

class ClipboardTextAccessor : public QObject {
    Q_OBJECT

public:
    explicit ClipboardTextAccessor(QObject *parent = nullptr);
    ~ClipboardTextAccessor() override = default;

    virtual QString text() const = 0;
    // Changes even when another writer replaces text with the same value.
    virtual quint64 revision() const = 0;
    virtual bool setText(const QString &text) = 0;
    virtual ClipboardTextWriteResult setTextIfUnchanged(const QString &text, quint64 expectedRevision);
    virtual bool isAvailable() const;
};

class QtClipboardTextAccessor : public ClipboardTextAccessor {
    Q_OBJECT

public:
    explicit QtClipboardTextAccessor(QObject *parent = nullptr);
    explicit QtClipboardTextAccessor(QClipboard *clipboard, QObject *parent = nullptr);
    ~QtClipboardTextAccessor() override;

    QString text() const override;
    quint64 revision() const override;
    bool setText(const QString &text) override;
    ClipboardTextWriteResult setTextIfUnchanged(const QString &text, quint64 expectedRevision) override;
    bool isAvailable() const override;

private:
    QPointer<QClipboard> clipboard_;
    quint64 revision_ = 0;
#ifdef Q_OS_WIN
    quintptr nativeClipboardOwner_ = 0;
#endif
};

class ClipInsertionService : public QObject {
    Q_OBJECT

public:
    using FindClipCallback = std::function<std::optional<Clip>(const QString &clipId)>;
    using MarkClipUsedCallback = std::function<bool(const QString &clipId, const QDateTime &now)>;
    using RepositoryErrorCallback = std::function<QString()>;
    using PasteInvoker = std::function<bool()>;
    using SuppressClipboardCaptureCallback = std::function<void()>;

    explicit ClipInsertionService(InMemoryClipRepository &repository, QObject *parent = nullptr);
    explicit ClipInsertionService(SqliteClipRepository &repository, QObject *parent = nullptr);
    ~ClipInsertionService() override;
    ClipInsertionService(ClipboardTextAccessor *clipboard,
                         InMemoryClipRepository &repository,
                         PasteInvoker pasteInvoker,
                         QObject *parent = nullptr);
    ClipInsertionService(ClipboardTextAccessor *clipboard,
                         SqliteClipRepository &repository,
                         PasteInvoker pasteInvoker,
                         QObject *parent = nullptr);
    ClipInsertionService(ClipboardTextAccessor *clipboard,
                         FindClipCallback findClip,
                         PasteInvoker pasteInvoker,
                         QObject *parent = nullptr);
    ClipInsertionService(ClipboardTextAccessor *clipboard,
                         FindClipCallback findClip,
                         MarkClipUsedCallback markClipUsed,
                         RepositoryErrorCallback repositoryError,
                         PasteInvoker pasteInvoker,
                         QObject *parent = nullptr);

    void setOptions(const ClipInsertionOptions &options);
    ClipInsertionOptions options() const;

    void setPasteInvoker(PasteInvoker pasteInvoker);
    void setSuppressClipboardCaptureCallback(SuppressClipboardCaptureCallback suppressClipboardCapture);

    ClipInsertionResult insertClip(const QString &clipId);
    ClipInsertionResult insertClip(const Clip &clip);

    QString lastInsertedId() const;
    std::optional<Clip> lastInsertedClip() const;
    ClipInsertionStatus lastStatus() const;
    QString lastError() const;
    bool ownsCurrentClipboardWrite() const;

signals:
    void inserted(const Pinloom::Clip &clip);
    void insertionFailed(Pinloom::ClipInsertionStatus status, const QString &error);
    void statusChanged(Pinloom::ClipInsertionStatus status);
    void errorChanged(const QString &error);

private:
    ClipInsertionResult fail(ClipInsertionStatus status, const QString &clipId, const QString &error);
    ClipInsertionResult succeed(const Clip &clip);
    void setLastStatus(ClipInsertionStatus status);
    void setLastError(const QString &error);
    void suppressNextClipboardCapture();
    bool restoreClipboardIfUnchanged(quint64 generation);

    struct PendingClipboardRestore {
        QString originalText;
        quint64 revision;
        quint64 generation;
    };

    QPointer<ClipboardTextAccessor> clipboard_;
    FindClipCallback findClip_;
    MarkClipUsedCallback markClipUsed_;
    RepositoryErrorCallback repositoryError_;
    PasteInvoker pasteInvoker_;
    SuppressClipboardCaptureCallback suppressClipboardCapture_;
    ClipInsertionOptions options_;
    QString lastInsertedId_;
    std::optional<Clip> lastInsertedClip_;
    ClipInsertionStatus lastStatus_ = ClipInsertionStatus::MissingClip;
    QString lastError_;
    quint64 clipboardWriteGeneration_ = 0;
    std::optional<PendingClipboardRestore> pendingClipboardRestore_;
    std::optional<quint64> lastOwnClipboardRevision_;
};

} // namespace Pinloom
