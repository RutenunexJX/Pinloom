#include "pinloom/widgets/SumatraPdfRegionCaptureOverlay.h"

#include "pinloom/core/SumatraPdfForegroundCapture.h"

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QToolTip>
#include <QWindow>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Pinloom {

namespace {

QRect targetClientGeometry(quintptr windowHandle)
{
    if (windowHandle != 0) {
        QWindow *foreignWindow = QWindow::fromWinId(static_cast<WId>(windowHandle));
        if (foreignWindow) {
            const QRect geometry = foreignWindow->geometry();
            delete foreignWindow;
            if (geometry.isValid()) {
                return geometry;
            }
        }
    }

#ifdef Q_OS_WIN
    if (windowHandle != 0) {
        HWND window = reinterpret_cast<HWND>(windowHandle);
        RECT clientRect{};
        POINT topLeft{};
        if (IsWindow(window)
            && GetClientRect(window, &clientRect)
            && ClientToScreen(window, &topLeft)) {
            return QRect(topLeft.x,
                         topLeft.y,
                         clientRect.right - clientRect.left,
                         clientRect.bottom - clientRect.top);
        }
    }
#else
    Q_UNUSED(windowHandle);
#endif

    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    return screen ? screen->geometry() : QRect();
}

void activateTargetWindow(quintptr windowHandle)
{
#ifdef Q_OS_WIN
    if (windowHandle == 0) {
        return;
    }
    HWND window = reinterpret_cast<HWND>(windowHandle);
    if (!IsWindow(window)) {
        return;
    }
    if (IsIconic(window)) {
        ShowWindow(window, SW_RESTORE);
    }
    BringWindowToTop(window);
    SetForegroundWindow(window);
#else
    Q_UNUSED(windowHandle);
#endif
}

class SumatraPdfRectHighlightOverlay final : public QWidget {
public:
    explicit SumatraPdfRectHighlightOverlay(const QRect &screenRect)
        : QWidget(nullptr,
                  Qt::Tool
                      | Qt::FramelessWindowHint
                      | Qt::WindowStaysOnTopHint
                      | Qt::WindowTransparentForInput
                      | Qt::WindowDoesNotAcceptFocus)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_DeleteOnClose);
        setGeometry(screenRect.adjusted(-5, -5, 5, 5));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF highlight = QRectF(rect()).adjusted(3.0, 3.0, -3.0, -3.0);
        painter.setBrush(QColor(255, 222, 0, 46));
        painter.setPen(QPen(QColor(245, 184, 0), 3.0));
        painter.drawRect(highlight);
    }
};

bool intersectsAvailableScreen(const QRect &rect)
{
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen && screen->geometry().intersects(rect)) {
            return true;
        }
    }
    return false;
}

} // namespace

bool SumatraPdfRegionCaptureResult::success() const
{
    return !canceled && region.success();
}

SumatraPdfRegionCaptureOverlay::SumatraPdfRegionCaptureOverlay(
    quintptr targetWindowHandle,
    MousePositionProvider mousePositionProvider,
    QWidget *parent)
    : QDialog(parent,
              Qt::Window
                  | Qt::FramelessWindowHint
                  | Qt::WindowStaysOnTopHint)
    , targetWindowHandle_(targetWindowHandle)
    , mousePositionProvider_(std::move(mousePositionProvider))
{
    if (!mousePositionProvider_) {
        mousePositionProvider_ = []() {
            return requestSumatraPdfDdeMousePosition(800);
        };
    }

    setWindowTitle(QStringLiteral("Pinloom PDF Region Capture"));
    setObjectName(QStringLiteral("sumatraPdfRegionCaptureOverlay"));
    setModal(true);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    setGeometry(targetClientGeometry(targetWindowHandle_));
}

SumatraPdfRegionCaptureResult SumatraPdfRegionCaptureOverlay::captureResult() const
{
    return result_;
}

void SumatraPdfRegionCaptureOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event && event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    QDialog::keyPressEvent(event);
}

void SumatraPdfRegionCaptureOverlay::mousePressEvent(QMouseEvent *event)
{
    if (!event) {
        return;
    }
    if (event->button() == Qt::RightButton) {
        reject();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }

    result_.region = {};
    setAccessibleDescription({});
    setWindowTitle(QStringLiteral("Pinloom PDF Region Capture"));

    dragStart_ = event->position().toPoint();
    dragCurrent_ = dragStart_;
    dragging_ = true;
    update();
}

void SumatraPdfRegionCaptureOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (!event || !dragging_) {
        return;
    }
    dragCurrent_ = event->position().toPoint();
    update();
}

void SumatraPdfRegionCaptureOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (!event || event->button() != Qt::LeftButton || !dragging_) {
        return;
    }

    dragCurrent_ = event->position().toPoint();
    dragging_ = false;
    if ((dragCurrent_ - dragStart_).manhattanLength() < 6) {
        showCaptureError(tr("PDF region is too small"));
        update();
        return;
    }

    const auto positions = sampleRegionPositions(mapToGlobal(dragStart_),
                                                 mapToGlobal(dragCurrent_));
    result_.region = sumatraPdfDdeRegionFromMousePositions(positions.first,
                                                           positions.second);
    if (!result_.region.success()) {
        showCaptureError(result_.region.error);
        update();
        return;
    }

    result_.canceled = false;
    accept();
}

void SumatraPdfRegionCaptureOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 18));
    if (!dragging_) {
        return;
    }

    const QRect selection = QRect(dragStart_, dragCurrent_).normalized();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(255, 222, 0, 52));
    painter.setPen(QPen(QColor(245, 184, 0), 2.0));
    painter.drawRect(selection);
}

void SumatraPdfRegionCaptureOverlay::reject()
{
    result_.canceled = true;
    QDialog::reject();
}

QPair<SumatraPdfDdeMousePosition, SumatraPdfDdeMousePosition>
SumatraPdfRegionCaptureOverlay::sampleRegionPositions(const QPoint &globalStart,
                                                      const QPoint &globalEnd)
{
    if (targetWindowHandle_ == 0) {
        return {mousePositionProvider_(), mousePositionProvider_()};
    }

#ifdef Q_OS_WIN
    HWND overlayWindow = reinterpret_cast<HWND>(winId());
    const bool restoreEnabled = overlayWindow && IsWindowEnabled(overlayWindow);
    if (restoreEnabled) {
        EnableWindow(overlayWindow, FALSE);
    }
    activateTargetWindow(targetWindowHandle_);
    const QPoint originalCursorPosition = QCursor::pos();
    QCursor::setPos(globalStart);
    const SumatraPdfDdeMousePosition start = mousePositionProvider_();
    QCursor::setPos(globalEnd);
    const SumatraPdfDdeMousePosition end = mousePositionProvider_();
    QCursor::setPos(originalCursorPosition);
    if (restoreEnabled) {
        EnableWindow(overlayWindow, TRUE);
        BringWindowToTop(overlayWindow);
        SetForegroundWindow(overlayWindow);
    }
    return {start, end};
#else
    Q_UNUSED(globalStart);
    Q_UNUSED(globalEnd);
    return {mousePositionProvider_(), mousePositionProvider_()};
#endif
}

void SumatraPdfRegionCaptureOverlay::showCaptureError(const QString &message)
{
    const QString text = message.trimmed().isEmpty()
        ? tr("Select a region inside one PDF page")
        : message.trimmed();
    setAccessibleDescription(text);
    setWindowTitle(QStringLiteral("Pinloom PDF Region Capture | %1").arg(text));
    QToolTip::showText(QCursor::pos(), text, this, rect(), 1800);
}

SumatraPdfRegionCaptureResult captureSumatraPdfRegion(quintptr targetWindowHandle)
{
    SumatraPdfRegionCaptureResult result;
    if (targetWindowHandle == 0) {
        result.region.error = QStringLiteral("SumatraPDF window is unavailable");
        return result;
    }

    activateTargetWindow(targetWindowHandle);
    SumatraPdfRegionCaptureOverlay overlay(targetWindowHandle);
    if (overlay.geometry().isEmpty()) {
        result.region.error = QStringLiteral("SumatraPDF window geometry is unavailable");
        return result;
    }
    overlay.exec();
    return overlay.captureResult();
}

bool showSumatraPdfRectHighlight(const QRectF &pdfRect,
                                 int page,
                                 double zoom,
                                 int durationMilliseconds)
{
    if (!pdfRect.isValid() || page <= 0 || !std::isfinite(zoom) || zoom <= 0.0) {
        return false;
    }

    const ForegroundAppWindowContext context = currentForegroundAppWindowContext();
    if (!isSumatraPdfForegroundWindow(context)) {
        return false;
    }

    const SumatraPdfDdeMousePosition mouse = requestSumatraPdfDdeMousePosition(600);
    if (!mouse.success() || mouse.page != page) {
        return false;
    }

    constexpr double LogicalPixelsPerPdfPointAt100Percent = 96.0 / 72.0;
    const double scale = LogicalPixelsPerPdfPointAt100Percent * zoom / 100.0;
    const QPoint cursor = QCursor::pos();
    const QPointF pageOrigin(cursor.x() - mouse.x * scale,
                             cursor.y() - mouse.y * scale);
    const QRect screenRect = QRectF(pageOrigin.x() + pdfRect.left() * scale,
                                    pageOrigin.y() + pdfRect.top() * scale,
                                    std::max(2.0, pdfRect.width() * scale),
                                    std::max(2.0, pdfRect.height() * scale))
                                 .toAlignedRect();
    if (!intersectsAvailableScreen(screenRect)) {
        return false;
    }

    auto *overlay = new SumatraPdfRectHighlightOverlay(screenRect);
    overlay->show();
    QTimer::singleShot(std::max(250, durationMilliseconds), overlay, &QWidget::close);
    return true;
}

} // namespace Pinloom
