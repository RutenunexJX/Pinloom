#include "pinloom/widgets/PinloomUiControls.h"
#include "pinloom/widgets/PinloomVisualTheme.h"

#include <QApplication>
#include <QFrame>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTextLayout>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#ifdef PINLOOM_ENABLE_ELA
#include "ElaDrawerArea.h"
#include "ElaScrollPageArea.h"
#include "ElaTheme.h"
#include "ElaToolTip.h"
#endif

namespace Pinloom::Ui {
namespace {
#ifdef PINLOOM_ENABLE_ELA
bool managedToolTip(QWidget *widget)
{
    for (auto *ancestor = widget; ancestor; ancestor = ancestor->parentWidget()) {
        if (ancestor->property("pinloomElaControl").toBool()) return true;
        if (ancestor->isWindow()) break;
    }
    return false;
}

QString wrappedToolTip(const QString &text, const QFont &font, int width, int maxLines)
{
    QStringList lines;
    const auto paragraphs = text.split(QLatin1Char('\n'));
    for (int paragraph = 0; paragraph < paragraphs.size(); ++paragraph) {
        QTextLayout layout(paragraphs.at(paragraph), font);
        QTextOption options;
        options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(options);
        layout.beginLayout();
        if (paragraphs.at(paragraph).isEmpty()) lines.append(QString());
        while (lines.size() < maxLines) {
            auto line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(width);
            lines.append(paragraphs.at(paragraph).mid(line.textStart(), line.textLength()));
            if (lines.size() == maxLines
                && (line.textStart() + line.textLength() < paragraphs.at(paragraph).size()
                    || paragraph + 1 < paragraphs.size())) {
                const QFontMetrics metrics(font);
                lines.last() = metrics.elidedText(lines.last(), Qt::ElideRight,
                    qMax(1, width - metrics.horizontalAdvance(QChar(0x2026)))) + QChar(0x2026);
            }
        }
        layout.endLayout();
        if (lines.size() >= maxLines) break;
    }
    return lines.join(QLatin1Char('\n'));
}

class ToolTips final : public QObject {
public:
    explicit ToolTips(QApplication &application) : QObject(&application) {
        setObjectName(QStringLiteral("pinloomToolTipController"));
        application.installEventFilter(this);
        expiry_.setSingleShot(true);
        connect(&expiry_, &QTimer::timeout, this, [this] { dismiss(); });
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        auto *widget = qobject_cast<QWidget *>(object);
        if (event->type() == QEvent::ToolTip && widget && managedToolTip(widget)) {
            dismiss();
            if (!widget->isVisible()) return false;
            source_ = widget;
            position_ = static_cast<QHelpEvent *>(event)->globalPos();
            if (auto *view = qobject_cast<QAbstractItemView *>(widget->parentWidget());
                view && widget == view->viewport()) {
                itemView_ = view;
                index_ = view->indexAt(static_cast<QHelpEvent *>(event)->pos());
                if (view->model()) {
                    connections_.append(connect(view->model(), &QAbstractItemModel::dataChanged,
                        this, [this] { display(); }));
                    connections_.append(connect(view->model(), &QAbstractItemModel::modelReset,
                        this, [this] { dismiss(); }));
                    connections_.append(connect(view->model(), &QAbstractItemModel::rowsRemoved,
                        this, [this] { dismiss(); }));
                    connections_.append(connect(view->model(), &QAbstractItemModel::layoutChanged,
                        this, [this] { dismiss(); }));
                }
                for (auto *bar : {view->horizontalScrollBar(), view->verticalScrollBar()})
                    connections_.append(connect(bar, &QScrollBar::valueChanged, this, [this] { dismiss(); }));
            }
            connections_.append(connect(widget, &QObject::destroyed, this, [this] { dismiss(); }));
            display();
            event->accept();
            return true;
        }
        if (!source_) return false;
        if (object == source_ && (event->type() == QEvent::ToolTipChange || event->type() == QEvent::FontChange)) display();
        if (object == source_ && itemView_ && event->type() == QEvent::MouseMove
            && itemView_->indexAt(static_cast<QMouseEvent *>(event)->position().toPoint()) != index_) dismiss();
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::KeyPress
            || event->type() == QEvent::Wheel
            || (object == source_ && (event->type() == QEvent::Leave || event->type() == QEvent::FocusOut))
            || (widget && (widget == source_ || widget->isAncestorOf(source_))
                && (event->type() == QEvent::Hide || event->type() == QEvent::Close
                    || event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Move
                    || event->type() == QEvent::Resize))) dismiss();
        return false;
    }
private:
    void dismiss() {
        source_.clear();
        itemView_.clear();
        index_ = QPersistentModelIndex();
        expiry_.stop();
        for (const auto &connection : connections_) disconnect(connection);
        connections_.clear();
        if (tip_) tip_->hide();
    }
    void display() {
        if (!source_) return;
        // Never fall back to DisplayRole/UserRole: a Clip's body is preview-only.
        const QString text = itemView_ ? index_.data(Qt::ToolTipRole).toString() : source_->toolTip();
        if (text.isEmpty()) { dismiss(); return; }
        auto *host = source_->window();
        if (tip_ && tip_->parentWidget() != host) delete tip_;
        if (!tip_) {
            // Construct without a parent to avoid Ela's Enter/mouse/animation filter.
            tip_ = new ElaToolTip;
            tip_->setParent(host, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
            tip_->setObjectName(QStringLiteral("pinloomToolTip"));
            tip_->setAttribute(Qt::WA_ShowWithoutActivating);
            tip_->setFocusPolicy(Qt::NoFocus);
            text_ = Ui::label(tip_);
            text_->setTextFormat(Qt::PlainText);
            tip_->setCustomWidget(text_);
        }
        QToolTip::hideText();
        tip_->setFont(source_->font());
        text_->setFont(source_->font());
        tip_->setToolTip(text);
        tip_->setAccessibleName(tr("Help"));
        tip_->setAccessibleDescription(text);
        auto *screen = QGuiApplication::screenAt(position_);
        const QRect bounds = (screen ? screen : source_->screen())->availableGeometry().adjusted(4, 4, -4, -4);
        const int maxWidth = qMax(1, qMin(520, bounds.width() - 24));
        const int maxLines = qMax(1, qMin(12, (bounds.height() - 24) / text_->fontMetrics().lineSpacing()));
        text_->setText(wrappedToolTip(text, text_->font(), maxWidth, maxLines));
        text_->setFixedSize(text_->sizeHint().boundedTo(QSize(maxWidth, qMax(1, bounds.height() - 24))));
        tip_->layout()->activate();
        tip_->adjustSize();
        QPoint point = position_ + QPoint(12, 18);
        if (point.y() + tip_->height() > bounds.bottom() + 1) point.setY(position_.y() - tip_->height() - 4);
        point.setX(qBound(bounds.left(), point.x(), qMax(bounds.left(), bounds.right() - tip_->width() + 1)));
        point.setY(qBound(bounds.top(), point.y(), qMax(bounds.top(), bounds.bottom() - tip_->height() + 1)));
        tip_->move(point);
        tip_->show();
        expiry_.start(source_->toolTipDuration() >= 0 ? source_->toolTipDuration() : 10000);
    }
    QPointer<QWidget> source_;
    QPointer<QAbstractItemView> itemView_;
    QPersistentModelIndex index_;
    QPointer<ElaToolTip> tip_;
    QLabel *text_ = nullptr;
    QPoint position_;
    QTimer expiry_;
    QList<QMetaObject::Connection> connections_;
};

class Drawer final : public ElaDrawerArea {
public:
    Drawer(QPushButton *toggle, QWidget *content, QWidget *parent) : ElaDrawerArea(parent), toggle_(toggle) {
        setProperty("pinloomElaControl", true);
        setDrawerHeader(toggle);
        addDrawer(content);
        resizeHeader();
        toggle->installEventFilter(this);
        connect(toggle, &QPushButton::toggled, this, [this, toggle, content](bool expanded) {
            if (!expanded && content->isAncestorOf(QApplication::focusWidget())) toggle->setFocus();
            if (expanded != getIsExpand()) { if (expanded) expand(); else collapse(); }
        });
        connect(this, &ElaDrawerArea::expandStateChanged, toggle, [toggle, content](bool expanded) {
            if (!expanded && content->isAncestorOf(QApplication::focusWidget())) toggle->setFocus();
            const QSignalBlocker blocker(toggle);
            toggle->setChecked(expanded);
        });
        if (toggle->isChecked()) expand();
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) resizeHeader();
        return false;
    }
private:
    void resizeHeader() { setHeaderHeight(qMax(40, toggle_->sizeHint().height() + 8)); }
    QPushButton *toggle_;
};
#endif

class ClassicFloatingPanel final : public QWidget {
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto tokens = pinloomVisualTokens(activePinloomVisualScheme());
        painter.setPen(tokens.border);
        painter.setBrush(tokens.panel);
        painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 10, 10);
    }
};

class PopupFrame final : public QFrame {
public:
    explicit PopupFrame(QWidget *parent) : QFrame(parent, Qt::Popup) {
        setAttribute(Qt::WA_DeleteOnClose);
        setAccessibleDescription(tr("Use Tab to move, Space to toggle a tag, Escape to close."));
#ifdef PINLOOM_ENABLE_ELA
        if (usesEla()) {
            setProperty("pinloomElaControl", true);
            setProperty("pinloomPopupSurface", true);
            setWindowFlag(Qt::FramelessWindowHint);
            setWindowFlag(Qt::NoDropShadowWindowHint);
            setAttribute(Qt::WA_TranslucentBackground);
            setContentsMargins(6, 6, 6, 6);
            connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { update(); });
            return;
        }
#endif
        setFrameShape(QFrame::StyledPanel);
        setProperty("pinloomRole", QStringLiteral("raised"));
    }
protected:
    void showEvent(QShowEvent *event) override {
        const auto bounds = screen()->availableGeometry();
        resize(size().boundedTo(bounds.size()));
        move(qBound(bounds.left(), x(), qMax(bounds.left(), bounds.right() - width() + 1)),
             qBound(bounds.top(), y(), qMax(bounds.top(), bounds.bottom() - height() + 1)));
        QFrame::showEvent(event);
    }
    void paintEvent(QPaintEvent *event) override {
#ifdef PINLOOM_ENABLE_ELA
        if (usesEla()) {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            eTheme->drawEffectShadow(&painter, rect(), 6, 8);
            painter.setPen(eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::PopupBorder));
            painter.setBrush(eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::PopupBase));
            painter.drawRoundedRect(rect().adjusted(6, 6, -6, -6), 8, 8);
            return;
        }
#endif
        QFrame::paintEvent(event);
    }
};
} // namespace

void installToolTips(QApplication &application)
{
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla() && !application.findChild<QObject *>(QStringLiteral("pinloomToolTipController")))
        new ToolTips(application);
#else
    Q_UNUSED(application);
#endif
}

QWidget *section(const QString &title, QWidget *parent)
{
    QWidget *card = nullptr;
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) {
        card = new ElaScrollPageArea(parent);
        card->setMinimumHeight(0);
        card->setMaximumHeight(QWIDGETSIZE_MAX);
        card->setProperty("pinloomElaControl", true);
        QObject::connect(eTheme, &ElaTheme::themeModeChanged, card, [card] { card->update(); });
    }
#endif
    if (!card) { card = new QWidget(parent); card->setProperty("pinloomRole", QStringLiteral("raised")); }
    card->setAccessibleName(title);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 12, 12, 12);
    auto *heading = label(title, card);
    heading->setProperty("pinloomTextRole", QStringLiteral("panelTitle"));
    heading->setWordWrap(true);
    layout->addWidget(heading);
    return card;
}

QWidget *collapsibleSection(QPushButton *toggle, QWidget *content, QWidget *parent)
{
    toggle->setCheckable(true);
    toggle->setAutoDefault(false);
    toggle->setDefault(false);
    toggle->setAccessibleName(toggle->text());
    toggle->setAccessibleDescription(QObject::tr("Press Space to expand or collapse. Values are retained when collapsed."));
    QWidget *container = nullptr;
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) container = new Drawer(toggle, content, parent);
#endif
    if (!container) {
        container = new QWidget(parent);
        auto *layout = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(toggle);
        layout->addWidget(content);
        content->setVisible(toggle->isChecked());
        QObject::connect(toggle, &QPushButton::toggled, content, [toggle, content](bool expanded) {
            if (!expanded && content->isAncestorOf(QApplication::focusWidget())) toggle->setFocus();
            content->setVisible(expanded);
        });
    }
    container->setAccessibleName(toggle->text());
    return container;
}

QFrame *popupFrame(QWidget *parent) { return new PopupFrame(parent); }

QWidget *floatingPanel()
{
    QWidget *panel = nullptr;
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) {
        auto *area = new ElaScrollPageArea;
        area->setBorderRadius(10);
        area->setMinimumHeight(0);
        area->setMaximumHeight(QWIDGETSIZE_MAX);
        area->setProperty("pinloomElaControl", true);
        QObject::connect(eTheme, &ElaTheme::themeModeChanged, area, [area] { area->update(); });
        panel = area;
    }
#endif
    if (!panel) panel = new ClassicFloatingPanel;
    panel->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                          | Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint);
    panel->setAttribute(Qt::WA_TranslucentBackground);
    panel->setAttribute(Qt::WA_ShowWithoutActivating);
    panel->setAttribute(Qt::WA_QuitOnClose, false);
    panel->setAutoFillBackground(false);
    return panel;
}
} // namespace Pinloom::Ui
