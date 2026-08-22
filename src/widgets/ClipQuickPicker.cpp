#include "pinloom/widgets/ClipQuickPicker.h"

#include "pinloom/core/TextSelectionCapture.h"

#include <QEvent>
#include <QGuiApplication>
#include <QHideEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
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

constexpr int ScreenMargin = 8;

bool isInteractionEvent(QEvent::Type type)
{
    return type == QEvent::KeyPress
        || type == QEvent::MouseButtonPress
        || type == QEvent::MouseButtonDblClick
        || type == QEvent::Wheel
        || type == QEvent::TouchBegin;
}

QRect targetWindowGeometry(const ForegroundTextTarget &target)
{
#ifdef Q_OS_WIN
    HWND window = reinterpret_cast<HWND>(target.windowHandle);
    RECT bounds{};
    if (window && IsWindow(window) && GetWindowRect(window, &bounds)) {
        return QRect(bounds.left,
                     bounds.top,
                     bounds.right - bounds.left,
                     bounds.bottom - bounds.top);
    }
#else
    Q_UNUSED(target);
#endif
    return {};
}

} // namespace

QRect clipQuickPickerGeometry(const QPoint &anchorPoint,
                              const QSize &popupSize,
                              const QRect &availableGeometry,
                              int gap)
{
    if (!availableGeometry.isValid() || popupSize.isEmpty()) {
        return QRect(anchorPoint, popupSize);
    }

    QRect bounds = availableGeometry.adjusted(ScreenMargin,
                                               ScreenMargin,
                                               -ScreenMargin,
                                               -ScreenMargin);
    if (!bounds.isValid()) {
        bounds = availableGeometry;
    }
    const QSize fittedSize(std::min(popupSize.width(), bounds.width()),
                           std::min(popupSize.height(), bounds.height()));
    const int maximumX = bounds.right() - fittedSize.width() + 1;
    const int maximumY = bounds.bottom() - fittedSize.height() + 1;

    int x = anchorPoint.x() - fittedSize.width() / 2;
    int y = anchorPoint.y() + std::max(0, gap);
    if (y > maximumY) {
        y = anchorPoint.y() - fittedSize.height() - std::max(0, gap);
    }
    x = std::clamp(x, bounds.left(), maximumX);
    y = std::clamp(y, bounds.top(), maximumY);
    return QRect(QPoint(x, y), fittedSize);
}

ClipQuickPicker::ClipQuickPicker(ClipQuickPickerOptions options, QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
    , options_(std::move(options))
{
    setObjectName(QStringLiteral("clipQuickPicker"));
    setWindowFlag(Qt::WindowStaysOnTopHint, false);
    setAttribute(Qt::WA_DeleteOnClose, false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    panel_ = new PinloomCommandPanel(options_.panelOptions, this);
    panel_->setObjectName(QStringLiteral("clipQuickPickerPanel"));
    layout->addWidget(panel_);

    dismissTimer_ = new QTimer(this);
    dismissTimer_->setSingleShot(true);
    connect(dismissTimer_, &QTimer::timeout, this, &ClipQuickPicker::dismiss);
    connect(panel_, &PinloomCommandPanel::presentationChanged,
            this, [this](bool, int) {
                resizeAndPosition();
            });

    if (auto *commandEdit = panel_->findChild<QLineEdit *>(QStringLiteral("commandSearchEdit"))) {
        commandEdit->installEventFilter(this);
        connect(commandEdit, &QLineEdit::textEdited, this, [this]() {
            markInteraction();
        });
    }
    if (auto *results = panel_->findChild<QListWidget *>(QStringLiteral("commandResultList"))) {
        results->installEventFilter(this);
        results->viewport()->installEventFilter(this);
    }
    if (auto *libraryButton = panel_->findChild<QToolButton *>(
            QStringLiteral("commandClipLibraryButton"))) {
        libraryButton->installEventFilter(this);
    }
}

void ClipQuickPicker::openAt(const QPoint &anchorPoint, const QString &query)
{
    ++sessionGeneration_;
    anchorPoint_ = anchorPoint;
    panel_->openClipSearch(query);
    sessionState_ = SessionState::Untouched;
    resizeAndPosition();
    show();
    raise();
    activateWindow();
    panel_->focusCommand();

    const quint64 generation = sessionGeneration_;
    QTimer::singleShot(0, this, [this, generation]() {
        if (generation == sessionGeneration_
            && sessionState_ == SessionState::Untouched
            && isVisible()) {
            startDismissTimer(options_.untouchedDismissMilliseconds);
        }
    });
}

void ClipQuickPicker::openForTarget(const ForegroundTextTarget &target,
                                    const QString &query)
{
    const QPoint anchor = target.hasInsertionPoint
        && QGuiApplication::screenAt(target.insertionPoint)
        ? target.insertionPoint
        : fallbackAnchorForTarget(target);
    openAt(anchor, query);
}

void ClipQuickPicker::dismiss()
{
    if (isVisible()) {
        hide();
        return;
    }
    dismissTimer_->stop();
    sessionState_ = SessionState::Closed;
}

PinloomCommandPanel *ClipQuickPicker::panel() const
{
    return panel_;
}

bool ClipQuickPicker::event(QEvent *event)
{
    const bool handled = QWidget::event(event);
    if (event && event->type() == QEvent::WindowDeactivate && isVisible()) {
        hide();
    }
    return handled;
}

bool ClipQuickPicker::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    if (event && isInteractionEvent(event->type())) {
        markInteraction();
    }
    return QWidget::eventFilter(watched, event);
}

void ClipQuickPicker::hideEvent(QHideEvent *event)
{
    const bool wasOpen = sessionState_ != SessionState::Closed;
    ++sessionGeneration_;
    dismissTimer_->stop();
    sessionState_ = SessionState::Closed;
    QWidget::hideEvent(event);
    if (wasOpen) {
        emit dismissed();
    }
}

void ClipQuickPicker::markInteraction()
{
    if (sessionState_ == SessionState::Closed) {
        return;
    }
    sessionState_ = SessionState::Active;
    startDismissTimer(options_.idleDismissMilliseconds);
}

void ClipQuickPicker::startDismissTimer(int milliseconds)
{
    if (milliseconds <= 0) {
        dismissTimer_->stop();
        return;
    }
    dismissTimer_->start(milliseconds);
}

void ClipQuickPicker::resizeAndPosition()
{
    QScreen *screen = QGuiApplication::screenAt(anchorPoint_);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    const QRect available = screen->availableGeometry();
    const int preferredWidth = std::clamp(options_.preferredWidth, 320, 520);
    const QSize requestedSize(preferredWidth, panel_->preferredWindowHeight());
    setGeometry(clipQuickPickerGeometry(anchorPoint_, requestedSize, available));
}

QPoint ClipQuickPicker::fallbackAnchorForTarget(const ForegroundTextTarget &target) const
{
    const QRect foregroundGeometry = targetWindowGeometry(target);
    if (foregroundGeometry.isValid()) {
        const int verticalOffset = std::clamp(foregroundGeometry.height() / 5, 48, 140);
        return QPoint(foregroundGeometry.center().x(),
                      foregroundGeometry.top() + verticalOffset);
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return {};
    }
    const QRect available = screen->availableGeometry();
    return QPoint(available.center().x(),
                  available.top() + available.height() / 4);
}

} // namespace Pinloom
