#pragma once

#include "pinloom/clip/ClipRepository.h"

#include <QObject>
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
    bool markClipUsedOnSuccess = true;
};

struct ClipInsertionResult {
    ClipInsertionStatus status = ClipInsertionStatus::MissingClip;
    QString clipId;
    QString error;

    bool inserted() const;
};

class ClipboardTextAccessor : public QObject {
    Q_OBJECT

public:
    explicit ClipboardTextAccessor(QObject *parent = nullptr);
    ~ClipboardTextAccessor() override = default;

    virtual QString text() const = 0;
    virtual bool setText(const QString &text) = 0;
    virtual bool isAvailable() const;
};

class QtClipboardTextAccessor : public ClipboardTextAccessor {
    Q_OBJECT

public:
    explicit QtClipboardTextAccessor(QObject *parent = nullptr);
    explicit QtClipboardTextAccessor(QClipboard *clipboard, QObject *parent = nullptr);

    QString text() const override;
    bool setText(const QString &text) override;
    bool isAvailable() const override;

private:
    QClipboard *clipboard_ = nullptr;
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

    ClipboardTextAccessor *clipboard_ = nullptr;
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
};

} // namespace Pinloom
