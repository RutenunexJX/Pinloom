#include "pinloom/widgets/PdfRegionSelectionOverlay.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace Pinloom {

bool PdfRegionSelectionResult::selected() const
{
    return state == PdfRegionSelectionState::Selected
        && screenRect.isValid();
}

bool PdfRegionSelectionResult::canceled() const
{
    return state == PdfRegionSelectionState::Canceled;
}

bool PdfRegionSelectionResult::timedOut() const
{
    return state == PdfRegionSelectionState::TimedOut;
}

PdfRegionSelectionOverlay::PdfRegionSelectionOverlay(
    const QRect &targetScreenGeometry,
    TargetStateProvider targetStateProvider,
    QWidget *parent,
    int timeoutMilliseconds,
    int minimumSelectionPixels)
    : QDialog(parent,
              Qt::Window
                  | Qt::FramelessWindowHint
                  | Qt::WindowStaysOnTopHint)
    , targetStateProvider_(std::move(targetStateProvider))
    , minimumSelectionPixels_(std::max(1, minimumSelectionPixels))
    , timeoutTimer_(new QTimer(this))
    , targetStateTimer_(new QTimer(this))
{
    setWindowTitle(QStringLiteral("Pinloom PDF Region Selection"));
    setObjectName(QStringLiteral("pdfRegionSelectionOverlay"));
    setAccessibleName(QStringLiteral("PDF page region selection"));
    setAccessibleDescription(
        QStringLiteral("Drag one rectangle. Press Escape or right-click to cancel."));
    setModal(true);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    setGeometry(targetScreenGeometry);

    timeoutTimer_->setSingleShot(true);
    timeoutTimer_->setInterval(std::max(1, timeoutMilliseconds));
    connect(timeoutTimer_, &QTimer::timeout, this,
            [this, timeoutMilliseconds]() {
                finish(PdfRegionSelectionState::TimedOut,
                       QStringLiteral("PDF region selection timed out after %1 ms")
                           .arg(std::max(1, timeoutMilliseconds)));
            });
    timeoutTimer_->start();

    targetStateTimer_->setInterval(100);
    targetCheck_ = new QFutureWatcher<QString>(this);
    connect(targetCheck_, &QFutureWatcher<QString>::finished, this, [this]() {
        checkInFlight_ = false;
        if (finished_) return;
        const QString diagnostics = targetCheck_->result().trimmed();
        if (!diagnostics.isEmpty()) finish(PdfRegionSelectionState::Canceled, diagnostics);
    });
    connect(targetStateTimer_, &QTimer::timeout, this, [this]() {
        if (!targetStateProvider_ || checkInFlight_ || finished_) return;
        checkInFlight_ = true;
        targetCheck_->setFuture(QtConcurrent::run(targetStateProvider_));
    });
    if (targetStateProvider_) targetStateTimer_->start();
}

PdfRegionSelectionResult PdfRegionSelectionOverlay::selectionResult() const
{
    return result_;
}

void PdfRegionSelectionOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event && event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    QDialog::keyPressEvent(event);
}

void PdfRegionSelectionOverlay::mousePressEvent(QMouseEvent *event)
{
    if (!event) return;
    if (event->button() == Qt::RightButton) {
        reject();
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    dragStart_ = event->position().toPoint();
    dragCurrent_ = dragStart_;
    dragging_ = true;
    update();
}

void PdfRegionSelectionOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (!event || !dragging_) return;
    dragCurrent_ = event->position().toPoint();
    update();
}

void PdfRegionSelectionOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (!event || event->button() != Qt::LeftButton || !dragging_) return;
    dragCurrent_ = event->position().toPoint();
    dragging_ = false;
    const QRect localSelection = QRect(dragStart_, dragCurrent_).normalized();
    if (localSelection.width() < minimumSelectionPixels_
        || localSelection.height() < minimumSelectionPixels_) {
        finish(PdfRegionSelectionState::Failed,
               QStringLiteral("PDF region selection is too small"));
        return;
    }
    result_.screenRect = QRect(mapToGlobal(localSelection.topLeft()),
                               mapToGlobal(localSelection.bottomRight()))
                             .normalized();
    finish(PdfRegionSelectionState::Selected);
}

void PdfRegionSelectionOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 18));
    if (!dragging_) return;
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(255, 222, 0, 52));
    painter.setPen(QPen(QColor(245, 184, 0), 2.0));
    painter.drawRect(QRect(dragStart_, dragCurrent_).normalized());
}

void PdfRegionSelectionOverlay::reject()
{
    finish(PdfRegionSelectionState::Canceled,
           QStringLiteral("PDF region selection canceled"));
}

void PdfRegionSelectionOverlay::finish(PdfRegionSelectionState state,
                                       const QString &diagnostics)
{
    if (finished_) return;
    finished_ = true;
    if (timeoutTimer_) timeoutTimer_->stop();
    if (targetStateTimer_) targetStateTimer_->stop();
    dragging_ = false;
    result_.state = state;
    result_.diagnostics = diagnostics.trimmed();
    if (state == PdfRegionSelectionState::Selected) {
        QDialog::accept();
    } else {
        QDialog::reject();
    }
}

} // namespace Pinloom
