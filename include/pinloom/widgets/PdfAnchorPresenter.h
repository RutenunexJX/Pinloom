#pragma once

#include "pinloom/core/Anchor.h"
#include "pinloom/core/PdfAnnotatedCopy.h"
#include "pinloom/core/SumatraPdfCommand.h"
#include "pinloom/core/SumatraPdfDdeClient.h"

#include <QObject>
#include <functional>
#include <memory>

namespace Pinloom {

struct PdfAnchorPresentationRequest {
    Anchor anchor;
    QString sourceTitle;
    QString cacheDirectory;
    SumatraPdfCommand sourceCommand;
};

struct PdfAnchorPresentationResult {
    quint64 requestId = 0;
    QString sourceFilePath;
    QString previewFilePath;
    SumatraPdfCommand previewCommand;
    bool cacheHit = false;
    bool superseded = false;
    QString error;

    bool success() const;
};

struct PdfAnchorPresentationCallbacks {
    std::function<void(quint64 requestId, const QString &status)> statusChanged;
    std::function<void(const PdfAnchorPresentationResult &result)> completed;
};

struct PdfAnchorPresentationStartResult {
    quint64 requestId = 0;
    QString error;

    bool accepted() const;
};

class PdfAnchorPresenter : public QObject {
public:
    explicit PdfAnchorPresenter(QObject *parent = nullptr);
    ~PdfAnchorPresenter() override;

    virtual PdfAnchorPresentationStartResult present(
        const PdfAnchorPresentationRequest &request,
        PdfAnchorPresentationCallbacks callbacks) = 0;
    virtual void cancelPending() = 0;
    virtual int activeRequestCount() const = 0;
};

struct SumatraAnnotatedCopyPresenterOptions {
    std::function<bool(const SumatraPdfCommand &, QString *error)> launchHandler;
    std::function<SumatraPdfDdeFileState(int timeoutMilliseconds)> stateProvider;
    bool stateProviderRunsInWorker = false;
    int verificationTimeoutMilliseconds = 4200;
    int verificationPollMilliseconds = 180;
    int generationTimeoutMilliseconds = 15000;
};

class SumatraAnnotatedCopyPresenter final : public PdfAnchorPresenter {
public:
    explicit SumatraAnnotatedCopyPresenter(
        SumatraAnnotatedCopyPresenterOptions options = {},
        QObject *parent = nullptr);
    ~SumatraAnnotatedCopyPresenter() override;

    PdfAnchorPresentationStartResult present(
        const PdfAnchorPresentationRequest &request,
        PdfAnchorPresentationCallbacks callbacks) override;
    void cancelPending() override;
    int activeRequestCount() const override;

private:
    class Private;
    std::unique_ptr<Private> d_;
};

} // namespace Pinloom
