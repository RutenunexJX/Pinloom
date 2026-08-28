#include "pinloom/widgets/PdfAnchorPresenter.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QSharedPointer>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace Pinloom {

namespace {

QString normalizedPdfPath(QString path)
{
    path = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}

QString sourceDisplayName(const PdfAnchorPresentationRequest &request)
{
    QString title = request.sourceTitle.trimmed();
    if (title.isEmpty()) {
        title = QFileInfo(request.sourceCommand.filePath).fileName();
    }
    return title.isEmpty() ? QStringLiteral("PDF") : title;
}

PdfAnchorCoordinateSpace coordinateSpaceForAnchor(const Anchor &anchor)
{
    const QJsonDocument locator = QJsonDocument::fromJson(anchor.locatorJson.toUtf8());
    if (!locator.isObject()) return PdfAnchorCoordinateSpace::PageTopLeft;
    QString coordinateSpace = locator.object()
                                  .value(QStringLiteral("coordinateSpace"))
                                  .toString()
                                  .trimmed()
                                  .toLower();
    if (coordinateSpace.isEmpty()) {
        coordinateSpace = locator.object()
                              .value(QStringLiteral("coordinate_space"))
                              .toString()
                              .trimmed()
                              .toLower();
    }
    return coordinateSpace == QLatin1String("pdf-user-space")
            || coordinateSpace == QLatin1String("pdf")
            || coordinateSpace == QLatin1String("pdf-user")
        ? PdfAnchorCoordinateSpace::PdfUserSpace
        : PdfAnchorCoordinateSpace::PageTopLeft;
}

SumatraPdfCommand previewCommandFor(const SumatraPdfCommand &source,
                                    const QString &previewFilePath)
{
    SumatraPdfCommand command;
    command.executablePath = source.executablePath;
    command.filePath = previewFilePath;
    command.page = source.page;
    command.arguments = {
        QStringLiteral("-reuse-instance"),
        QStringLiteral("-page"),
        QString::number(command.page),
        command.filePath,
    };
    return command;
}

} // namespace

bool PdfAnchorPresentationResult::success() const
{
    return !superseded && error.isEmpty()
        && !previewFilePath.trimmed().isEmpty();
}

bool PdfAnchorPresentationStartResult::accepted() const
{
    return requestId != 0 && error.isEmpty();
}

PdfAnchorPresenter::PdfAnchorPresenter(QObject *parent)
    : QObject(parent)
{
}

PdfAnchorPresenter::~PdfAnchorPresenter() = default;

class SumatraAnnotatedCopyPresenter::Private {
public:
    struct RequestContext {
        quint64 id = 0;
        PdfAnchorPresentationRequest request;
        PdfAnchorPresentationCallbacks callbacks;
        PdfAnnotatedCopyResult copyResult;
        QElapsedTimer verificationTimer;
        QString lastDiagnostics;
        bool copyGenerationFinished = false;
        bool finished = false;
    };

    Private(SumatraAnnotatedCopyPresenter *owner,
            SumatraAnnotatedCopyPresenterOptions presenterOptions)
        : q(owner)
        , options(std::move(presenterOptions))
    {
    }

    PdfAnchorPresentationStartResult present(
        const PdfAnchorPresentationRequest &request,
        PdfAnchorPresentationCallbacks callbacks)
    {
        PdfAnchorPresentationStartResult start;
        if (request.sourceCommand.filePath.trimmed().isEmpty()) {
            start.error = QStringLiteral("PDF anchor source file is missing");
            return start;
        }
        if (request.sourceCommand.executablePath.trimmed().isEmpty()) {
            start.error = QStringLiteral("SumatraPDF executable is not configured/found");
            return start;
        }
        if (request.sourceCommand.page <= 0
            || !request.sourceCommand.highlightRect.isValid()) {
            start.error = QStringLiteral("PDF rectangle anchor is invalid");
            return start;
        }

        supersedePending();
        const quint64 id = ++lastRequestId;
        auto context = QSharedPointer<RequestContext>::create();
        context->id = id;
        context->request = request;
        context->callbacks = std::move(callbacks);
        contexts.insert(id, context);
        start.requestId = id;

        const int generationTimeout = std::clamp(
            options.generationTimeoutMilliseconds, 1000, 120000);
        const QPointer<SumatraAnnotatedCopyPresenter> guard(q);
        QTimer::singleShot(generationTimeout, q, [guard, id]() {
            if (!guard) return;
            const auto context = guard->d_->contexts.value(id);
            if (!context || context->finished || context->copyGenerationFinished) return;
            guard->d_->finishFailure(
                id,
                QStringLiteral("Pinloom Preview generation timed out"));
        });
        QTimer::singleShot(0, q, [guard, id]() {
            if (!guard) return;
            const auto context = guard->d_->contexts.value(id);
            if (!context || context->finished) return;
            guard->d_->reportStatus(
                context,
                QStringLiteral("Preparing %1 — Pinloom Preview")
                    .arg(sourceDisplayName(context->request)));
        });

        PdfAnnotatedCopyRequest copyRequest;
        copyRequest.sourceFilePath = request.sourceCommand.filePath;
        copyRequest.cacheDirectory = request.cacheDirectory;
        copyRequest.anchorId = request.anchor.id;
        copyRequest.locatorJson = request.anchor.locatorJson;
        copyRequest.pageNumber = request.sourceCommand.page;
        copyRequest.anchorRect = request.sourceCommand.highlightRect;
        copyRequest.coordinateSpace = coordinateSpaceForAnchor(request.anchor);

        auto *watcher = new QFutureWatcher<PdfAnnotatedCopyResult>(q);
        QObject::connect(watcher,
                         &QFutureWatcher<PdfAnnotatedCopyResult>::finished,
                         watcher,
                         [guard, watcher, id]() {
            const PdfAnnotatedCopyResult copyResult = watcher->result();
            watcher->deleteLater();
            if (!guard) return;
            guard->d_->handleGeneratedCopy(id, copyResult);
        });
        watcher->setFuture(QtConcurrent::run([copyRequest]() {
            return preparePdfAnnotatedCopy(copyRequest);
        }));
        return start;
    }

    void supersedePending()
    {
        const QList<quint64> ids = contexts.keys();
        for (quint64 id : ids) {
            const auto context = contexts.value(id);
            if (!context || context->finished) continue;
            PdfAnchorPresentationResult result;
            result.requestId = id;
            result.sourceFilePath = context->request.sourceCommand.filePath;
            result.superseded = true;
            finish(context, result, false);
        }
    }

    void abandonPending()
    {
        const auto pending = contexts;
        contexts.clear();
        for (const auto &context : pending) {
            if (context) context->finished = true;
        }
    }

    void handleGeneratedCopy(quint64 id,
                             const PdfAnnotatedCopyResult &copyResult)
    {
        const auto context = contexts.value(id);
        if (!context || context->finished) return;
        context->copyGenerationFinished = true;
        context->copyResult = copyResult;
        if (!copyResult.success()) {
            finishFailure(id,
                          copyResult.error.trimmed().isEmpty()
                              ? QStringLiteral("Unable to generate Pinloom Preview PDF")
                              : copyResult.error.trimmed());
            return;
        }

        const SumatraPdfCommand command = previewCommandFor(
            context->request.sourceCommand, copyResult.outputFilePath);
        QString launchError;
        const bool launched = options.launchHandler
            ? options.launchHandler(command, &launchError)
            : (QFileInfo(command.executablePath).isFile()
               && QProcess::startDetached(command.executablePath, command.arguments));
        if (!launched) {
            finishFailure(id,
                          launchError.trimmed().isEmpty()
                              ? QStringLiteral("Unable to launch SumatraPDF Pinloom Preview")
                              : launchError.trimmed());
            return;
        }

        context->verificationTimer.start();
        reportStatus(
            context,
            QStringLiteral("Opening %1 — Pinloom Preview; verifying page %2")
                .arg(sourceDisplayName(context->request))
                .arg(command.page));
        if (options.launchHandler && !options.stateProvider) {
            finishSuccess(context, command);
            return;
        }

        const int verificationTimeout = std::clamp(
            options.verificationTimeoutMilliseconds, 400, 30000);
        const QPointer<SumatraAnnotatedCopyPresenter> guard(q);
        QTimer::singleShot(verificationTimeout, q, [guard, id]() {
            if (!guard) return;
            const auto context = guard->d_->contexts.value(id);
            if (!context || context->finished) return;
            const QString detail = context->lastDiagnostics.trimmed().isEmpty()
                ? QStringLiteral("generated PDF and target page were not observed")
                : context->lastDiagnostics.trimmed();
            guard->d_->finishFailure(
                id,
                QStringLiteral("SumatraPDF Pinloom Preview navigation failed: %1")
                    .arg(detail));
        });
        pollVerification(id, command);
    }

    void pollVerification(quint64 id, const SumatraPdfCommand &command)
    {
        const auto context = contexts.value(id);
        if (!context || context->finished) return;
        if (options.stateProvider && !options.stateProviderRunsInWorker) {
            handleVerificationState(id, command, options.stateProvider(140));
            return;
        }
        const auto provider = options.stateProvider;
        auto *watcher = new QFutureWatcher<SumatraPdfDdeFileState>(q);
        const QPointer<SumatraAnnotatedCopyPresenter> guard(q);
        QObject::connect(watcher,
                         &QFutureWatcher<SumatraPdfDdeFileState>::finished,
                         watcher,
                         [guard, watcher, id, command]() {
            const SumatraPdfDdeFileState state = watcher->result();
            watcher->deleteLater();
            if (!guard) return;
            guard->d_->handleVerificationState(id, command, state);
        });
        watcher->setFuture(QtConcurrent::run([provider]() {
            return provider
                ? provider(140)
                : requestSumatraPdfDdeFileState(140);
        }));
    }

    void handleVerificationState(quint64 id,
                                 const SumatraPdfCommand &command,
                                 const SumatraPdfDdeFileState &state)
    {
        const auto context = contexts.value(id);
        if (!context || context->finished) return;
        if (state.success()
            && normalizedPdfPath(state.path) == normalizedPdfPath(command.filePath)
            && state.page == command.page) {
            finishSuccess(context, command);
            return;
        }
        if (!state.success()) {
            context->lastDiagnostics = state.error.trimmed().isEmpty()
                ? QStringLiteral("SumatraPDF DDE state is unavailable")
                : state.error.trimmed();
        } else if (normalizedPdfPath(state.path) != normalizedPdfPath(command.filePath)) {
            context->lastDiagnostics = QStringLiteral("active file is %1").arg(state.path);
        } else {
            context->lastDiagnostics = QStringLiteral("active page is %1; expected %2")
                                           .arg(state.page)
                                           .arg(command.page);
        }
        const int timeout = std::clamp(
            options.verificationTimeoutMilliseconds, 400, 30000);
        if (context->verificationTimer.elapsed() >= timeout) {
            finishFailure(
                id,
                QStringLiteral("SumatraPDF Pinloom Preview navigation failed: %1")
                    .arg(context->lastDiagnostics));
            return;
        }
        const int poll = std::clamp(options.verificationPollMilliseconds, 50, 1000);
        const QPointer<SumatraAnnotatedCopyPresenter> guard(q);
        QTimer::singleShot(poll, q, [guard, id, command]() {
            if (!guard) return;
            guard->d_->pollVerification(id, command);
        });
    }

    void finishSuccess(const QSharedPointer<RequestContext> &context,
                       const SumatraPdfCommand &command)
    {
        PdfAnchorPresentationResult result;
        result.requestId = context->id;
        result.sourceFilePath = context->request.sourceCommand.filePath;
        result.previewFilePath = context->copyResult.outputFilePath;
        result.previewCommand = command;
        result.cacheHit = context->copyResult.cacheHit;
        reportStatus(
            context,
            QStringLiteral("Opened %1 — Pinloom Preview at page %2%3")
                .arg(sourceDisplayName(context->request))
                .arg(command.page)
                .arg(result.cacheHit ? QStringLiteral(" (cached)") : QString()));
        finish(context, result, true);
    }

    void finishFailure(quint64 id, const QString &error)
    {
        const auto context = contexts.value(id);
        if (!context || context->finished) return;
        PdfAnchorPresentationResult result;
        result.requestId = id;
        result.sourceFilePath = context->request.sourceCommand.filePath;
        result.previewFilePath = context->copyResult.outputFilePath;
        result.cacheHit = context->copyResult.cacheHit;
        result.error = error.trimmed().isEmpty()
            ? QStringLiteral("Pinloom Preview failed")
            : error.trimmed();
        finish(context, result, true);
    }

    void finish(const QSharedPointer<RequestContext> &context,
                const PdfAnchorPresentationResult &result,
                bool notify)
    {
        if (!context || context->finished) return;
        context->finished = true;
        contexts.remove(context->id);
        if (notify && context->callbacks.completed) {
            context->callbacks.completed(result);
        } else if (result.superseded && context->callbacks.completed) {
            context->callbacks.completed(result);
        }
    }

    void reportStatus(const QSharedPointer<RequestContext> &context,
                      const QString &status)
    {
        if (context && !context->finished && context->callbacks.statusChanged) {
            context->callbacks.statusChanged(context->id, status);
        }
    }

    SumatraAnnotatedCopyPresenter *q = nullptr;
    SumatraAnnotatedCopyPresenterOptions options;
    quint64 lastRequestId = 0;
    QHash<quint64, QSharedPointer<RequestContext>> contexts;
};

SumatraAnnotatedCopyPresenter::SumatraAnnotatedCopyPresenter(
    SumatraAnnotatedCopyPresenterOptions options,
    QObject *parent)
    : PdfAnchorPresenter(parent)
    , d_(std::make_unique<Private>(this, std::move(options)))
{
}

SumatraAnnotatedCopyPresenter::~SumatraAnnotatedCopyPresenter()
{
    d_->abandonPending();
}

PdfAnchorPresentationStartResult SumatraAnnotatedCopyPresenter::present(
    const PdfAnchorPresentationRequest &request,
    PdfAnchorPresentationCallbacks callbacks)
{
    return d_->present(request, std::move(callbacks));
}

void SumatraAnnotatedCopyPresenter::cancelPending()
{
    d_->supersedePending();
}

int SumatraAnnotatedCopyPresenter::activeRequestCount() const
{
    return d_->contexts.size();
}

} // namespace Pinloom
