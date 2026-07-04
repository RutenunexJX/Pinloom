#pragma once

#include "pinloom/clip/ClipRepository.h"

#include <QObject>
#include <QString>
#include <functional>
#include <optional>

class QClipboard;

namespace Pinloom {

class ClipboardTextSource : public QObject {
    Q_OBJECT

public:
    explicit ClipboardTextSource(QObject *parent = nullptr);
    ~ClipboardTextSource() override = default;

    virtual QString text() const = 0;
    virtual bool isAvailable() const;

signals:
    void textChanged();
};

class QtClipboardTextSource : public ClipboardTextSource {
    Q_OBJECT

public:
    explicit QtClipboardTextSource(QObject *parent = nullptr);
    explicit QtClipboardTextSource(QClipboard *clipboard, QObject *parent = nullptr);

    QString text() const override;
    bool isAvailable() const override;

private:
    QClipboard *clipboard_ = nullptr;
};

class ClipboardCaptureService : public QObject {
    Q_OBJECT

public:
    using CaptureTextCallback = std::function<ClipCaptureResult(const QString &text,
                                                                const ClipCapturePolicy &policy,
                                                                const QString &sourceApp,
                                                                const QDateTime &now)>;
    using RepositoryErrorCallback = std::function<QString()>;

    explicit ClipboardCaptureService(InMemoryClipRepository &repository, QObject *parent = nullptr);
    explicit ClipboardCaptureService(SqliteClipRepository &repository, QObject *parent = nullptr);
    ClipboardCaptureService(ClipboardTextSource *clipboard,
                            InMemoryClipRepository &repository,
                            QObject *parent = nullptr);
    ClipboardCaptureService(ClipboardTextSource *clipboard,
                            SqliteClipRepository &repository,
                            QObject *parent = nullptr);
    ClipboardCaptureService(ClipboardTextSource *clipboard,
                            CaptureTextCallback captureText,
                            QObject *parent = nullptr);
    ClipboardCaptureService(ClipboardTextSource *clipboard,
                            CaptureTextCallback captureText,
                            RepositoryErrorCallback repositoryError,
                            QObject *parent = nullptr);

    bool start();
    void stop();
    bool isRunning() const;

    void pauseCapture();
    void resumeCapture();
    void setCapturePaused(bool paused);
    bool capturePaused() const;

    void setPolicy(const ClipCapturePolicy &policy);
    ClipCapturePolicy policy() const;

    void setSourceApp(const QString &sourceApp);
    QString sourceApp() const;

    void suppressNextChange();
    bool suppressingNextChange() const;

    QString lastCapturedId() const;
    std::optional<Clip> lastCapturedClip() const;
    ClipCaptureStatus lastStatus() const;
    QString lastError() const;

public slots:
    void captureCurrentText();

signals:
    void captured(const Pinloom::Clip &clip);
    void captureIgnored(Pinloom::ClipCaptureStatus status);
    void errorChanged(const QString &error);
    void runningChanged(bool running);
    void capturePausedChanged(bool paused);

private slots:
    void handleClipboardTextChanged();

private:
    void recordCaptureText(const QString &text);
    void setLastError(const QString &error);

    ClipboardTextSource *clipboard_ = nullptr;
    CaptureTextCallback captureText_;
    RepositoryErrorCallback repositoryError_;
    ClipCapturePolicy policy_;
    QString sourceApp_;
    QString lastCapturedId_;
    std::optional<Clip> lastCapturedClip_;
    ClipCaptureStatus lastStatus_ = ClipCaptureStatus::IgnoredBlank;
    QString lastError_;
    bool running_ = false;
    bool suppressNextChange_ = false;
    QMetaObject::Connection clipboardConnection_;
};

} // namespace Pinloom
