#include "pinloom/widgets/CommandFloatingController.h"
#include "pinloom/widgets/PinloomCommandPanel.h"
#include "pinloom/widgets/PinloomUiControls.h"
#include "pinloom/widgets/PinloomVisualTheme.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QIconEngine>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QToolButton>
#include <QWidget>
#include <algorithm>

namespace Pinloom {

namespace {
class CaptureIconEngine final : public QIconEngine {
public:
    explicit CaptureIconEngine(int action) : action_(action) {}
    QIconEngine *clone() const override { return new CaptureIconEngine(action_); }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap image(size);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return image;
    }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.topLeft());
        painter->scale(rect.width() / 24.0, rect.height() / 24.0);
        const auto tokens = pinloomVisualTokens(activePinloomVisualScheme());
        painter->setPen(QPen(mode == QIcon::Disabled ? tokens.disabledText : tokens.text,
                            1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        if (action_ == 0) {
            painter->drawRoundedRect(QRectF(3, 3, 14, 12), 1, 1);
        } else if (action_ == 1) {
            painter->drawLine(QPointF(4, 3), QPointF(17, 3));
            painter->drawLine(QPointF(10.5, 3), QPointF(10.5, 16));
            painter->drawLine(QPointF(7, 16), QPointF(14, 16));
        } else {
            painter->drawRoundedRect(QRectF(5, 5, 14, 17), 1.5, 1.5);
            painter->drawRoundedRect(QRectF(8, 2, 8, 5), 1, 1);
            for (int y : {11, 15, 18}) painter->drawLine(QPointF(8, y), QPointF(16, y));
        }
        if (action_ < 2) {
            painter->drawEllipse(QRectF(16, 14, 4, 4));
            painter->drawLine(QPointF(18, 18), QPointF(18, 22));
        }
        painter->restore();
    }
private:
    int action_;
};
}

CommandFloatingController::CommandFloatingController(QWidget &window, PinloomCommandPanel &panel)
    : QObject(&window), window_(window), panel_(panel),
      floating_(Ui::floatingPanel())
{
    setObjectName(QStringLiteral("commandFloatingController"));
    floating_->setObjectName(QStringLiteral("commandCaptureFloat"));
    floating_->setWindowTitle(tr("Pinloom Capture"));
    floating_->setAccessibleName(tr("Pinloom floating capture toolbar"));
    floating_->setAccessibleDescription(tr("Drag to move. Press Shift+Space to open the full command bar."));
    floating_->setToolTip(floating_->accessibleDescription());
    floating_->setCursor(Qt::OpenHandCursor);
    floating_->installEventFilter(this);
    auto *layout = new QHBoxLayout(floating_.get());
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    const QStringList names{tr("PDF Rectangle Anchor"), tr("PDF Text Anchor"), tr("PDF Text Clip")};
    const QStringList sourceNames{QStringLiteral("commandRectangleAnchorButton"),
                                  QStringLiteral("commandTextAnchorButton"),
                                  QStringLiteral("commandPdfTextClipButton")};
    const int side = pinloomVisualMetrics().primaryControlHeight;
    for (int i = 0; i < names.size(); ++i) {
        auto *button = Ui::toolButton(floating_.get());
        button->setObjectName(QStringLiteral("floatingCapture%1").arg(i));
        button->setIcon(QIcon(new CaptureIconEngine(i)));
        button->setIconSize(QSize(22, 22));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setText(names[i]);
        button->setAccessibleName(names[i]);
        button->setAccessibleDescription(tr("Click to capture; drag to move the toolbar."));
        button->setToolTip(names[i] + QLatin1Char('\n') + button->accessibleDescription());
        button->setCursor(Qt::PointingHandCursor);
        button->installEventFilter(this);
        button->setFixedSize(side, side);
        const auto *source = panel.findChild<QToolButton *>(sourceNames[i]);
        button->setEnabled(source && source->isEnabled());
        layout->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, i] { capture(i); });
    }
    connect(qApp, &QGuiApplication::applicationStateChanged,
            this, [this](Qt::ApplicationState state) {
                if (QGuiApplication::applicationState() == state) handleApplicationStateChanged(state);
            }, Qt::QueuedConnection);
}

CommandFloatingController::~CommandFloatingController() { floating_.reset(); }
bool CommandFloatingController::isFloating() const { return collapsed_; }
bool CommandFloatingController::isCapturing() const { return capturing_; }
QWidget *CommandFloatingController::floatingWindow() const { return floating_.get(); }

bool CommandFloatingController::eventFilter(QObject *object, QEvent *event)
{
    if (!floating_) return false;
    if ((object == floating_.get() && event->type() == QEvent::Hide)
        || (object == pressedWidget_.data() && event->type() == QEvent::UngrabMouse)) {
        resetDrag();
        return false;
    }
    auto *widget = qobject_cast<QWidget *>(object);
    if (!widget || capturing_) return false;
    if (event->type() == QEvent::MouseButtonPress) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) return false;
        pressedWidget_ = widget;
        pressPosition_ = mouse->globalPosition().toPoint();
        dragOrigin_ = floating_->pos();
        dragging_ = false;
        return widget == floating_.get();
    }
    if (!pressedWidget_) return false;
    if (event->type() == QEvent::MouseMove) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (!(mouse->buttons() & Qt::LeftButton)) { resetDrag(); return false; }
        const QPoint delta = mouse->globalPosition().toPoint() - pressPosition_;
        if (!dragging_ && delta.manhattanLength() < qMax(1, QApplication::startDragDistance())) return false;
        dragging_ = true;
        userPositioned_ = true;
        if (auto *button = qobject_cast<QToolButton *>(pressedWidget_.data())) button->setDown(false);
        floating_->setCursor(Qt::ClosedHandCursor);
        pressedWidget_->setCursor(Qt::ClosedHandCursor);
        moveFloatingWindow(dragOrigin_ + delta, mouse->globalPosition().toPoint());
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        if (static_cast<QMouseEvent *>(event)->button() != Qt::LeftButton) return false;
        const bool consume = dragging_ || widget == floating_.get();
        resetDrag(consume);
        return consume;
    }
    return false;
}

void CommandFloatingController::resetDrag(bool cancelClick)
{
    if (auto *button = qobject_cast<QToolButton *>(pressedWidget_.data())) {
        if (cancelClick) button->setDown(false);
        button->setCursor(Qt::PointingHandCursor);
    }
    pressedWidget_.clear();
    dragging_ = false;
    floating_->setCursor(Qt::OpenHandCursor);
}

void CommandFloatingController::handleApplicationStateChanged(Qt::ApplicationState state)
{
    if (state != Qt::ApplicationInactive || capturing_ || collapsed_ || !window_.isVisible()
        || QApplication::activeModalWidget() || QApplication::activePopupWidget()) return;
    collapsed_ = true;
    if (!userPositioned_) floating_->move(window_.pos());
    window_.hide();
    showFloatingWindow();
}

void CommandFloatingController::showFloatingWindow()
{
    floating_->adjustSize();
    moveFloatingWindow(floating_->pos(), floating_->geometry().center());
    floating_->show();
}

void CommandFloatingController::moveFloatingWindow(const QPoint &position, const QPoint &screenPoint)
{
    QScreen *screen = QGuiApplication::screenAt(screenPoint);
    if (!screen) screen = floating_->screen();
    if (!screen) screen = window_.screen();
    QPoint point = position;
    if (screen) {
        const QRect available = screen->availableGeometry();
        point.setX(std::clamp(point.x(), available.left(),
                             std::max(available.left(), available.right() - floating_->width() + 1)));
        point.setY(std::clamp(point.y(), available.top(),
                             std::max(available.top(), available.bottom() - floating_->height() + 1)));
    }
    floating_->move(point);
}

bool CommandFloatingController::requestExpansion()
{
    if (capturing_) {
        expansionPending_ = true;
        return false;
    }
    collapsed_ = false;
    floating_->hide();
    return true;
}

void CommandFloatingController::capture(int action)
{
    if (capturing_) return;
    capturing_ = true;
    emit captureStarted();
    floating_->hide();
    switch (action) {
    case 0: panel_.triggerRectangleAnchorCapture(); break;
    case 1: panel_.triggerTextAnchorCapture(); break;
    case 2: panel_.triggerPdfTextClipCapture(); break;
    }
    emit captureFinished();
    capturing_ = false;
    if (expansionPending_) {
        expansionPending_ = false;
        panel_.openCommandSearch();
        showCommandPanelForHotkey(window_, panel_);
    } else if (collapsed_) {
        showFloatingWindow();
    }
}

}
