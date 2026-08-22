#include "pinloom/widgets/ClipQuickPicker.h"

#include "pinloom/core/TextSelectionCapture.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMoveEvent>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

#ifdef Q_OS_WIN
#include <QtGui/qscreen_platform.h>

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

constexpr int ScreenMargin = 8;
constexpr int PositionSaveDelayMilliseconds = 150;

QString positionSettingsKey()
{
    return QStringLiteral("ui/clipQuickPickerPosition");
}

QRect constrainedScreenBounds(const QRect &availableGeometry)
{
    const QRect inset = availableGeometry.adjusted(ScreenMargin,
                                                    ScreenMargin,
                                                    -ScreenMargin,
                                                    -ScreenMargin);
    return inset.isValid() ? inset : availableGeometry;
}

QSize fittedWindowSize(const QSize &windowSize, const QRect &bounds)
{
    return QSize(std::min(windowSize.width(), bounds.width()),
                 std::min(windowSize.height(), bounds.height()));
}

} // namespace

QRect clipQuickPickerCenteredGeometry(const QSize &windowSize,
                                      const QRect &availableGeometry)
{
    if (!availableGeometry.isValid() || windowSize.isEmpty()) {
        return QRect(availableGeometry.center(), windowSize);
    }

    const QRect bounds = constrainedScreenBounds(availableGeometry);
    const QSize fittedSize = fittedWindowSize(windowSize, bounds);
    return QRect(QPoint(bounds.left() + (bounds.width() - fittedSize.width()) / 2,
                        bounds.top() + (bounds.height() - fittedSize.height()) / 2),
                 fittedSize);
}

QRect clipQuickPickerPositionedGeometry(const QPoint &topLeft,
                                        const QSize &windowSize,
                                        const QRect &availableGeometry)
{
    if (!availableGeometry.isValid() || windowSize.isEmpty()) {
        return QRect(topLeft, windowSize);
    }

    const QRect bounds = constrainedScreenBounds(availableGeometry);
    const QSize fittedSize = fittedWindowSize(windowSize, bounds);
    const int maximumX = bounds.right() - fittedSize.width() + 1;
    const int maximumY = bounds.bottom() - fittedSize.height() + 1;
    return QRect(QPoint(std::clamp(topLeft.x(), bounds.left(), maximumX),
                        std::clamp(topLeft.y(), bounds.top(), maximumY)),
                 fittedSize);
}

ClipQuickPicker::ClipQuickPicker(ClipQuickPickerOptions options, QWidget *parent)
    : QWidget(parent,
              Qt::Tool
                  | Qt::CustomizeWindowHint
                  | Qt::WindowTitleHint
                  | Qt::WindowStaysOnTopHint)
    , options_(std::move(options))
{
    setObjectName(QStringLiteral("clipQuickPicker"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowModality(Qt::NonModal);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    panel_ = new PinloomCommandPanel(options_.panelOptions, this);
    panel_->setObjectName(QStringLiteral("clipQuickPickerPanel"));
    layout->addWidget(panel_);

    positionSaveTimer_ = new QTimer(this);
    positionSaveTimer_->setSingleShot(true);
    positionSaveTimer_->setInterval(PositionSaveDelayMilliseconds);
    connect(positionSaveTimer_, &QTimer::timeout,
            this, &ClipQuickPicker::persistPosition);
    connect(panel_, &PinloomCommandPanel::presentationChanged,
            this, [this](bool, int) {
                resizeAndPosition();
            });
}

void ClipQuickPicker::openForTarget(const ForegroundTextTarget &target,
                                    const QString &query)
{
    openingScreen_ = screenForTarget(target);
    panel_->openClipSearch(query);
    resizeAndPosition();

    if (isMinimized()) {
        showNormal();
    } else {
        show();
    }
    raise();
    activateWindow();
    panel_->focusCommand();
}

void ClipQuickPicker::dismiss()
{
    if (isVisible()) {
        hide();
    }
}

PinloomCommandPanel *ClipQuickPicker::panel() const
{
    return panel_;
}

void ClipQuickPicker::closeEvent(QCloseEvent *event)
{
    event->ignore();
}

void ClipQuickPicker::hideEvent(QHideEvent *event)
{
    positionSaveTimer_->stop();
    persistPosition();
    QWidget::hideEvent(event);
    emit dismissed();
}

void ClipQuickPicker::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        event->accept();
        dismiss();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ClipQuickPicker::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    if (positionInitialized_ && !applyingGeometry_ && isVisible()) {
        positionSaveTimer_->start();
    }
}

QScreen *ClipQuickPicker::screenForTarget(const ForegroundTextTarget &target) const
{
#ifdef Q_OS_WIN
    HWND window = reinterpret_cast<HWND>(target.windowHandle);
    if (window && IsWindow(window)) {
        HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        if (monitor) {
            for (QScreen *screen : QGuiApplication::screens()) {
                auto *nativeScreen = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
                if (nativeScreen && nativeScreen->handle() == monitor) {
                    return screen;
                }
            }
        }
    }
#else
    Q_UNUSED(target);
#endif
    return QGuiApplication::primaryScreen();
}

void ClipQuickPicker::resizeAndPosition()
{
    QScreen *screen = nullptr;
    QPoint requestedPosition;
    bool hasRequestedPosition = false;

    if (!positionInitialized_ && options_.settings
        && options_.settings->contains(positionSettingsKey())) {
        requestedPosition = options_.settings->value(positionSettingsKey()).toPoint();
        screen = QGuiApplication::screenAt(requestedPosition);
        hasRequestedPosition = screen != nullptr;
    }

    if (positionInitialized_) {
        requestedPosition = pos();
        screen = QGuiApplication::screenAt(requestedPosition);
        hasRequestedPosition = screen != nullptr;
    }

    if (!screen) {
        screen = openingScreen_ ? openingScreen_ : QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    const int preferredWidth = std::clamp(options_.preferredWidth, 320, 520);
    const QSize requestedSize(preferredWidth, panel_->preferredWindowHeight());
    const QRect nextGeometry = hasRequestedPosition
        ? clipQuickPickerPositionedGeometry(requestedPosition,
                                            requestedSize,
                                            screen->availableGeometry())
        : clipQuickPickerCenteredGeometry(requestedSize,
                                          screen->availableGeometry());

    applyingGeometry_ = true;
    resize(nextGeometry.size());
    move(nextGeometry.topLeft());
    applyingGeometry_ = false;
    positionInitialized_ = true;
}

void ClipQuickPicker::persistPosition()
{
    if (!positionInitialized_ || !options_.settings) {
        return;
    }
    options_.settings->setValue(positionSettingsKey(), pos());
}

} // namespace Pinloom
