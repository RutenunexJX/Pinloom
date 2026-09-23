#include "pinloom/widgets/PinloomUiControls.h"
#include "pinloom/widgets/PinloomVisualTheme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QDynamicPropertyChangeEvent>
#include <QFormLayout>
#include <QFrame>
#include <QKeyEvent>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QScroller>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#ifdef PINLOOM_ENABLE_ELA
#include "ElaApplication.h"
#include "ElaAppBar.h"
#include "ElaCheckBox.h"
#include "ElaComboBox.h"
#include "ElaContentDialog.h"
#include "ElaDoubleSpinBox.h"
#include "ElaLineEdit.h"
#include "ElaMenu.h"
#include "ElaMenuBar.h"
#include "ElaMessageBar.h"
#include "ElaNavigationBar.h"
#include "ElaPlainTextEdit.h"
#include "ElaPushButton.h"
#include "ElaScrollArea.h"
#include "ElaScrollBar.h"
#include "ElaSpinBox.h"
#include "ElaStatusBar.h"
#include "ElaText.h"
#include "ElaTreeView.h"
#include "ElaTheme.h"
#include "ElaToolButton.h"
#endif

namespace Pinloom::Ui {
bool usesEla()
{
#ifdef PINLOOM_ENABLE_ELA
    static const bool enabled = [] {
        const auto value = qgetenv("PINLOOM_UI_STYLE").trimmed().toLower();
        if (value.isEmpty() || value == "ela") return true;
        qFatal("This build supports PINLOOM_UI_STYLE=ela only");
        return false;
    }();
    return enabled;
#else
    return false;
#endif
}

#ifdef PINLOOM_ENABLE_ELA
namespace {
void initializeEla()
{
    if (qApp->property("pinloomElaInitialized").toBool()) return;
    const QFont font = qApp->font();
    const bool nativeSiblings = QApplication::testAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    eApp->init();
    QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings, nativeSiblings);
    qApp->setFont(font);
    qApp->setProperty("pinloomElaInitialized", true);
}

void refreshRole(QWidget *widget)
{
    if (auto *view = qobject_cast<QAbstractItemView *>(widget)) {
        const auto t = pinloomVisualTokens(activePinloomVisualScheme());
        auto palette = view->palette();
        palette.setColor(QPalette::Base, t.panel);
        palette.setColor(QPalette::AlternateBase, t.alternateSurface);
        palette.setColor(QPalette::Text, t.text);
        palette.setColor(QPalette::Disabled, QPalette::Text, t.disabledText);
        palette.setColor(QPalette::Highlight, view->window()->property("trashMode").toBool() ? t.error : t.selection);
        palette.setColor(QPalette::HighlightedText, t.selectionText);
        view->setPalette(palette);
    }
    if (auto *button = qobject_cast<ElaPushButton *>(widget)) {
        const auto t = pinloomVisualTokens(activePinloomVisualScheme());
        const bool primary = button->property("pinloomControl") == QStringLiteral("primary");
        const QString accent = button->property("accent").toString();
        const QColor text = primary ? t.selectionText : accent == "destructive" ? t.error
            : accent == "positive" ? t.success : t.text;
        button->setBorderRadius(6);
        button->setLightDefaultColor(primary ? t.selection : t.panel);
        button->setDarkDefaultColor(primary ? t.selection : t.panel);
        button->setLightHoverColor(primary ? t.selection.lighter(110) : t.hoverSurface);
        button->setDarkHoverColor(primary ? t.selection.lighter(110) : t.hoverSurface);
        button->setLightPressColor(primary ? t.selection.darker(110) : t.pressedSurface);
        button->setDarkPressColor(primary ? t.selection.darker(110) : t.pressedSurface);
        button->setLightTextColor(text);
        button->setDarkTextColor(text);
    }
    widget->update();
}

void refreshTextRole(QWidget *widget)
{
    if (widget->property("pinloomTextRole") != QStringLiteral("technical")) return;
    auto font = qApp->font();
    font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"), QStringLiteral("monospace")});
    font.setStyleHint(QFont::Monospace);
    widget->setFont(font);
}

template<class Base> class Control : public Base {
public:
    using Base::Base;
    QSize sizeHint() const override {
        auto hint = Base::sizeHint();
        const auto metrics = pinloomVisualMetrics();
        const int roleHeight = this->property("pinloomControl") == QStringLiteral("primary")
            ? metrics.primaryControlHeight : std::is_base_of_v<QToolButton, Base>
            ? metrics.compactControlHeight : metrics.regularControlHeight;
        hint.setHeight(qMax(roleHeight, qMax(hint.height(), this->fontMetrics().height() + 14)));
        if constexpr (std::is_base_of_v<QPushButton, Base>)
            hint.setWidth(qMax(hint.width(), this->fontMetrics().size(Qt::TextShowMnemonic,
                this->text()).width() + (this->icon().isNull() ? 0 : this->iconSize().width() + 6) + 32));
        if constexpr (std::is_base_of_v<QAbstractSpinBox, Base>) {
            QString minimum, maximum;
            if constexpr (std::is_base_of_v<QDoubleSpinBox, Base>) {
                minimum = this->locale().toString(this->minimum(), 'f', this->decimals());
                maximum = this->locale().toString(this->maximum(), 'f', this->decimals());
            } else {
                minimum = this->locale().toString(this->minimum());
                maximum = this->locale().toString(this->maximum());
            }
            const int textWidth = qMax(this->fontMetrics().horizontalAdvance(this->prefix() + minimum + this->suffix()),
                                      this->fontMetrics().horizontalAdvance(this->prefix() + maximum + this->suffix()));
            // Ela's inline step buttons occupy two full control heights.
            hint.setWidth(qMax(hint.width(), textWidth + 2 * hint.height() + 16));
        }
        return hint;
    }
    QSize minimumSizeHint() const override { return sizeHint(); }
protected:
    bool event(QEvent *event) override {
        const bool handled = Base::event(event);
        if (event->type() == QEvent::DynamicPropertyChange) {
            const auto *change = static_cast<QDynamicPropertyChangeEvent *>(event);
            if (change->propertyName() == "pinloomTextRole") refreshTextRole(this);
            this->updateGeometry();
        }
        if (event->type() == QEvent::DynamicPropertyChange
            || event->type() == QEvent::ApplicationPaletteChange
            || event->type() == QEvent::Polish) refreshRole(this);
        return handled;
    }
    void paintEvent(QPaintEvent *event) override {
        Base::paintEvent(event);
        if (this->hasFocus() && this->isEnabled()) {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(pinloomVisualTokens(activePinloomVisualScheme()).focus, 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(this->rect().adjusted(2, 2, -2, -2), 6, 6);
        }
        if constexpr (std::is_base_of_v<QToolButton, Base>) {
            if (this->isChecked() && this->property("accent") == QStringLiteral("destructive")) {
                QPainter painter(this);
                painter.setPen(QPen(pinloomVisualTokens(activePinloomVisualScheme()).error, 2));
                painter.drawLine(5, this->height() - 3, this->width() - 5, this->height() - 3);
            }
        }
    }
};

class Choice final : public Control<ElaComboBox> {
public:
    using Control::Control;
};
class Input final : public Control<ElaLineEdit> {
public:
    using Control::Control;
protected:
    void contextMenuEvent(QContextMenuEvent *event) override {
        std::unique_ptr<QMenu> actions(createStandardContextMenu());
        std::unique_ptr<QMenu> popup(Ui::menu(this));
        popup->addActions(actions->actions());
        popup->exec(event->globalPos());
    }
};
class Menu final : public ElaMenu {
public:
    using ElaMenu::ElaMenu;
};
class ScrollBar final : public ElaScrollBar {
public:
    ScrollBar(Qt::Orientation orientation, QAbstractScrollArea *area)
        : ElaScrollBar(orientation, area), textUnits_(qobject_cast<QPlainTextEdit *>(area)) {}
protected:
    // Keep Qt's localized actions; Ela owns wheel and hover transitions.
    void contextMenuEvent(QContextMenuEvent *event) override { QScrollBar::contextMenuEvent(event); }
    void wheelEvent(QWheelEvent *event) override {
        if (textUnits_ && !event->pixelDelta().isNull()) {
            stopSmoothWheel();
            QScrollBar::wheelEvent(event);
        } else ElaScrollBar::wheelEvent(event);
    }
private:
    bool textUnits_;
};
class PlainText final : public ElaPlainTextEdit {
public:
    using ElaPlainTextEdit::ElaPlainTextEdit;
protected:
    bool event(QEvent *event) override {
        const bool handled = ElaPlainTextEdit::event(event);
        if (event->type() == QEvent::DynamicPropertyChange
            && static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName() == "pinloomTextRole")
            refreshTextRole(this);
        return handled;
    }
    void contextMenuEvent(QContextMenuEvent *event) override {
        auto *actions = createStandardContextMenu();
        auto *popup = Ui::menu(this);
        popup->setAttribute(Qt::WA_DeleteOnClose);
        // Pair lifetimes without making Qt's action container inherit Ela's menu style.
        connect(popup, &QObject::destroyed, actions, [actions] { delete actions; });
        popup->addActions(actions->actions());
        popup->popup(event->globalPos());
    }
    void paintEvent(QPaintEvent *event) override {
        ElaPlainTextEdit::paintEvent(event);
        if (hasFocus() && isEnabled()) {
            QPainter painter(viewport());
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(pinloomVisualTokens(activePinloomVisualScheme()).focus, 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(viewport()->rect().adjusted(1, 1, -1, -1), 5, 5);
        }
    }
};
class StatusBar final : public ElaStatusBar {
public:
    using ElaStatusBar::ElaStatusBar;
    QSize sizeHint() const override {
        auto hint = ElaStatusBar::sizeHint();
        hint.setHeight(qMax(hint.height(), qMax(pinloomVisualMetrics().compactControlHeight,
                                               fontMetrics().height() + 10)));
        return hint;
    }
protected:
    bool event(QEvent *event) override {
        const bool handled = ElaStatusBar::event(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange
            || event->type() == QEvent::Polish || event->type() == QEvent::LayoutRequest)
            setMinimumHeight(qMax(pinloomVisualMetrics().compactControlHeight, fontMetrics().height() + 10));
        return handled;
    }
};
class Text final : public ElaText {
public:
    using ElaText::ElaText;
};
template<class Widget> Widget *prepare(Widget *widget)
{
    widget->setProperty("pinloomElaControl", true);
    widget->setFont(qApp->font());
    widget->setMinimumSize(0, 0);
    widget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, widget,
                     [widget] { refreshRole(widget); });
    refreshRole(widget);
    return widget;
}
class PrecisionWheelRouter final : public QObject {
public:
    explicit PrecisionWheelRouter(QAbstractScrollArea *area) : QObject(area), area_(area) {
        area->installEventFilter(this);
        area->viewport()->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress) {
            if (auto *tree = qobject_cast<QTreeView *>(area_)) ElaTreeView::finishExpansion(tree);
            for (auto *scroll : {area_->horizontalScrollBar(), area_->verticalScrollBar()})
                if (auto *bar = qobject_cast<ElaScrollBar *>(scroll)) bar->stopSmoothWheel();
        }
        if (event->type() != QEvent::Wheel) return false;
        auto *wheel = static_cast<QWheelEvent *>(event);
        const QPoint pixels = wheel->pixelDelta();
        if (pixels.isNull()) return false;
        // Text-editor scrollbar units are lines, not viewport pixels.
        if (qobject_cast<QPlainTextEdit *>(area_)) {
            for (auto *scroll : {area_->horizontalScrollBar(), area_->verticalScrollBar()})
                if (auto *bar = qobject_cast<ElaScrollBar *>(scroll)) bar->stopSmoothWheel();
            return false;
        }
        auto *bar = qAbs(pixels.x()) > qAbs(pixels.y()) || wheel->modifiers().testFlag(Qt::ShiftModifier)
            ? area_->horizontalScrollBar() : area_->verticalScrollBar();
        wheel->ignore();
        QApplication::sendEvent(bar, wheel);
        return wheel->isAccepted();
    }
private:
    QAbstractScrollArea *area_;
};
void installScrollBars(QAbstractScrollArea *area)
{
    area->setHorizontalScrollBar(prepare(new ScrollBar(Qt::Horizontal, area)));
    area->setVerticalScrollBar(prepare(new ScrollBar(Qt::Vertical, area)));
    for (auto *scroll : {area->horizontalScrollBar(), area->verticalScrollBar()}) {
        auto *bar = static_cast<ElaScrollBar *>(scroll);
        bar->setIsAnimation(false);
        bar->setSmoothWheelEnabled(true);
        bar->setWheelAnimationDuration(160);
    }
    if (auto *view = qobject_cast<QAbstractItemView *>(area)) {
        view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    }
    new PrecisionWheelRouter(area);
}
} // namespace
#endif

#ifdef PINLOOM_ENABLE_ELA
namespace {
class Chrome final : public ElaAppBar {
public:
    Chrome(QWidget *host, bool dialog) : ElaAppBar(host) {
        setProperty("pinloomElaControl", true);
        setIsDefaultClosed(false);
        setWindowButtonFlags(dialog ? ElaAppBarType::CloseButtonHint
            : ElaAppBarType::MinimizeButtonHint | ElaAppBarType::MaximizeButtonHint | ElaAppBarType::CloseButtonHint);
        setAccessibleName(tr("Window title bar"));
        refreshFont();
        resize(host->width(), height());
        connect(this, &ElaAppBar::closeButtonClicked, host, [host] { host->close(); });
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == window() && event->type() == QEvent::FontChange) refreshFont();
        // Let Qt handle native/Alt-F4 closes directly; avoid a nested close request.
        if (event->type() == QEvent::Close) return false;
        // The offscreen platform has no HWND to modify.
        if (event->type() == QEvent::Show && QGuiApplication::platformName() != "windows") return false;
        return ElaAppBar::eventFilter(object, event);
    }
private:
    void refreshFont() {
        for (auto *text : findChildren<ElaText *>()) text->setFont(window()->font());
        setAppBarHeight(qMax(36, window()->fontMetrics().height() + 16));
    }
};

class NavigationPopup final : public QFrame {
public:
    NavigationPopup(QToolButton *button, QMenu *routes) : QFrame(button, Qt::Popup), button_(button) {
        setAttribute(Qt::WA_DeleteOnClose);
        setObjectName(QStringLiteral("commandNavigationPopup"));
        setProperty("pinloomRole", QStringLiteral("panel"));
        setAccessibleName(tr("Navigate Pinloom"));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(4, 4, 4, 4);
        auto *bar = new ElaNavigationBar(this);
        bar->setObjectName(QStringLiteral("pinloomNavigationBar"));
        bar->setProperty("pinloomActionNavigation", true);
        bar->setUserInfoCardVisible(false);
        bar->setIsAllowPageOpenInNewWindow(false);
        bar->setNavigationBarWidth(qMax(210, fontMetrics().horizontalAdvance(tr("All items")) + 90));
        bar->setDisplayMode(ElaNavigationType::Maximal, false);
        layout->addWidget(bar);
        for (auto *action : routes->actions()) {
            auto *page = new QWidget(bar);
            page->hide();
            bar->addPageNode(action->text(), page, ElaIconType::MagnifyingGlass);
            routes_.insert(page->property("ElaPageKey").toString(), action);
        }
        view_ = bar->findChild<QTreeView *>();
        Q_ASSERT(view_);
        view_->setAccessibleName(tr("Pinloom destinations"));
        view_->setProperty("pinloomActionNavigation", true);
        view_->setAccessibleDescription(tr("Use Up and Down to select, Enter to navigate, Escape to cancel."));
        view_->setAnimated(true);
        view_->setFocusPolicy(Qt::StrongFocus);
        view_->setSelectionMode(QAbstractItemView::SingleSelection);
        view_->installEventFilter(this);
        QScroller::ungrabGesture(view_->viewport());
        // ElaNavigationView owns an overlay mapped to its existing scrollbar.
        // Replacing that bar would invalidate the overlay's source pointer.
        for (auto *scroll : view_->findChildren<ElaScrollBar *>()) {
            scroll->setIsAnimation(false);
            scroll->setSmoothWheelEnabled(true);
            scroll->setWheelAnimationDuration(160);
        }
        new PrecisionWheelRouter(view_);
        connect(bar, &ElaNavigationBar::navigationNodeClicked, this,
            [this](ElaNavigationType::NavigationNodeType, const QString &key, bool) { activate(key); });
        resize(bar->width() + 8, qMax(260, routes->actions().size() * qMax(40, view_->fontMetrics().height() + 12) + 16));
    }
    void open() {
        const auto available = button_->screen()->availableGeometry();
        auto position = button_->mapToGlobal(QPoint(0, button_->height()));
        position.setX(qBound(available.left(), position.x(), qMax(available.left(), available.right() - width() + 1)));
        position.setY(qBound(available.top(), position.y(), qMax(available.top(), available.bottom() - height() + 1)));
        move(position);
        show();
        view_->setCurrentIndex(view_->model()->index(0, 0));
        view_->setFocus(Qt::PopupFocusReason);
    }
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == view_ && event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Space) {
                activate(view_->currentIndex().data(Qt::UserRole).toString());
                return true;
            }
            if (key->key() == Qt::Key_Escape) { close(); button_->setFocus(); return true; }
        }
        return QFrame::eventFilter(object, event);
    }
private:
    void activate(const QString &key) {
        auto action = routes_.value(key);
        if (!action || !action->isEnabled()) return;
        close();
        action->trigger();
    }
    QToolButton *button_;
    QTreeView *view_ = nullptr;
    QHash<QString, QPointer<QAction>> routes_;
};
} // namespace
#endif

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) { initializeEla(); chrome_ = new Chrome(this, false); }
#endif
}
Dialog::Dialog(QWidget *parent, Qt::WindowFlags flags) : QDialog(parent, flags) {
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) { initializeEla(); chrome_ = new Chrome(this, true); }
#endif
}
bool MainWindow::nativeEvent(const QByteArray &type, void *message, qintptr *result) {
#if defined(PINLOOM_ENABLE_ELA) && defined(Q_OS_WIN)
    if (chrome_ && QGuiApplication::platformName() == "windows") {
        const int handled = static_cast<ElaAppBar *>(chrome_)->takeOverNativeEvent(type, message, result);
        if (handled != -1) return handled != 0;
    }
#endif
    return QMainWindow::nativeEvent(type, message, result);
}
bool Dialog::nativeEvent(const QByteArray &type, void *message, qintptr *result) {
#if defined(PINLOOM_ENABLE_ELA) && defined(Q_OS_WIN)
    if (chrome_ && QGuiApplication::platformName() == "windows") {
        const int handled = static_cast<ElaAppBar *>(chrome_)->takeOverNativeEvent(type, message, result);
        if (handled != -1) return handled != 0;
    }
#endif
    return QDialog::nativeEvent(type, message, result);
}
void installNavigation(QToolButton *button, QMenu *routes) {
    button->setMenu(routes);
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) {
        button->setMenu(nullptr);
        button->setPopupMode(QToolButton::DelayedPopup);
        QObject::connect(button, &QToolButton::clicked, button, [button, routes] {
            (new NavigationPopup(button, routes))->open();
        });
    }
#endif
}
void FormLayout::addRow(const QString &text, QWidget *field) {
    auto *caption = label(text, parentWidget());
    caption->setBuddy(field);
    QFormLayout::addRow(caption, field);
}
void FormLayout::addRow(const QString &text, QLayout *field) {
    QFormLayout::addRow(label(text, parentWidget()), field);
}

namespace {
class MessageDialog final : public ElaContentDialog {
public:
    using ElaContentDialog::ElaContentDialog;
protected:
    void keyPressEvent(QKeyEvent *event) override { QDialog::keyPressEvent(event); }
};
QMessageBox::StandardButton message(QWidget *parent, const QString &title, const QString &text,
                                   QMessageBox::StandardButtons buttons,
                                   QMessageBox::StandardButton defaultButton,
                                   const QString &acceptText = {}) {
    initializeEla();
    MessageDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("pinloomMessageDialog"));
    dialog.setWindowTitle(title);
    dialog.setAccessibleName(title);
    dialog.setStandardButtonsVisible(false);
    auto *content = new QWidget(&dialog);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 20, 24, 20);
    auto *heading = Ui::label(title, content);
    heading->setProperty("pinloomTextRole", QStringLiteral("panelTitle"));
    layout->addWidget(heading);
    auto *body = Ui::plainTextEdit(text, content);
    body->setReadOnly(true);
    body->setAccessibleName(text);
    layout->addWidget(body, 1);
    auto *box = new Ui::DialogButtonBox(static_cast<QDialogButtonBox::StandardButtons>(int(buttons)), content);
    box->setObjectName(QStringLiteral("pinloomMessageButtons"));
    if (!acceptText.isEmpty()) box->button(QDialogButtonBox::Ok)->setText(acceptText);
    layout->addWidget(box);
    if (defaultButton == QMessageBox::NoButton)
        defaultButton = buttons.testFlag(QMessageBox::Cancel) ? QMessageBox::Cancel
            : buttons.testFlag(QMessageBox::No) ? QMessageBox::No : QMessageBox::Ok;
    QMessageBox::StandardButton result = buttons.testFlag(QMessageBox::Cancel) ? QMessageBox::Cancel
        : buttons.testFlag(QMessageBox::No) ? QMessageBox::No
        : buttons == QMessageBox::Ok ? QMessageBox::Ok : QMessageBox::NoButton;
    for (auto *button : box->buttons()) {
        auto *push = qobject_cast<QPushButton *>(button);
        if (!push) continue;
        const bool isDefault = int(box->standardButton(push)) == int(defaultButton);
        push->setAutoDefault(false);
        push->setDefault(isDefault);
        if (isDefault) push->setFocus();
    }
    QObject::connect(box, &QDialogButtonBox::clicked, &dialog, [&](QAbstractButton *button) {
        result = static_cast<QMessageBox::StandardButton>(box->standardButton(button));
        dialog.accept();
    });
    dialog.setCentralWidget(content);
    dialog.resize(520, 300);
    dialog.exec();
    return result;
}
}
QMessageBox::StandardButton question(QWidget *parent, const QString &title, const QString &text,
                                     QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton) {
    return message(parent, title, text, buttons, defaultButton);
}
QMessageBox::StandardButton warning(QWidget *parent, const QString &title, const QString &text,
                                    QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton) {
    return message(parent, title, text, buttons, defaultButton);
}
QMessageBox::StandardButton critical(QWidget *parent, const QString &title, const QString &text,
                                     QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton) {
    return message(parent, title, text, buttons, defaultButton);
}
bool confirm(QWidget *parent, const QString &title, const QString &text, const QString &acceptText) {
    return message(parent, title, text, QMessageBox::Ok | QMessageBox::Cancel,
                   QMessageBox::Cancel, acceptText) == QMessageBox::Ok;
}

void rememberSplitter(QSplitter *splitter, QSettings *settings, const QString &key) {
    if (!splitter || !settings) return;
    const QByteArray fallback = splitter->saveState();
    const auto saved = settings->value(key).toByteArray();
    if (!saved.isEmpty() && !splitter->restoreState(saved)) splitter->restoreState(fallback);
    const QPointer<QSettings> store(settings);
    QObject::connect(splitter, &QSplitter::splitterMoved, splitter, [splitter, store, key] {
        if (store) store->setValue(key, splitter->saveState());
    });
}

QWidget *showNotice(QWidget *host, const QString &text) {
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla() && host && host->isVisible() && host->width() >= 180 && host->height() >= 100 && !text.trimmed().isEmpty()) {
        // One non-modal notice per surface; full text stays in the status control.
        for (auto *old : host->findChildren<QWidget *>(QStringLiteral("pinloomNoticeBar"), Qt::FindDirectChildrenOnly)) {
            if (old->accessibleDescription() == text) return old;
            delete old;
        }
        host->setProperty("pinloomInstantNotices", true);
        ElaMessageBar::information(ElaMessageBarType::Bottom, {}, text, 4500, host);
        auto *notice = host->findChild<ElaMessageBar *>(QStringLiteral("ElaMessageBar"), Qt::FindDirectChildrenOnly);
        if (notice) {
            notice->setObjectName(QStringLiteral("pinloomNoticeBar"));
            notice->setProperty("pinloomElaControl", true);
            notice->setAccessibleName(QObject::tr("Pinloom notification"));
            notice->setAccessibleDescription(text);
            notice->setToolTip(text);
        }
        return notice;
    }
#else
    Q_UNUSED(host);
    Q_UNUSED(text);
#endif
    return nullptr;
}
void setStatusText(QLabel *target, const QString &text, bool notify) {
    const bool changed = target->text() != text;
    target->setText(text);
    if (changed && notify) showNotice(target->window(), text);
}

void applyElaTheme(QApplication &application, PinloomVisualScheme scheme)
{
#ifdef PINLOOM_ENABLE_ELA
    application.setProperty("pinloomControlBackend", usesEla() ? "ela" : "classic");
    if (!usesEla()) return;
    initializeEla();
    installToolTips(application);
    for (const auto mode : {ElaThemeType::Light, ElaThemeType::Dark}) {
        const auto t = pinloomVisualTokens(mode == ElaThemeType::Light
            ? PinloomVisualScheme::Light : PinloomVisualScheme::Dark);
        const auto set = [mode](ElaThemeType::ThemeColor key, QColor value) {
            eTheme->setThemeColor(mode, key, value);
        };
        using namespace ElaThemeType;
        for (auto key : {WindowBase}) set(key, t.canvas);
        for (auto key : {WindowCentralStackBase, PopupBase, DialogBase, BasicBase, BasicBaseAlpha}) set(key, t.panel);
        for (auto key : {DialogLayoutArea, BasicBaseDeep, BasicBaseDeepAlpha, BasicChute}) set(key, t.raisedSurface);
        for (auto key : {PrimaryNormal, PrimaryHover, PrimaryPress, BasicIndicator}) set(key, t.accent);
        for (auto key : {PopupBorder, BasicBorder, BasicBorderDeep, BasicBaseLine, BasicHemline, Win10BorderInactive}) set(key, t.border);
        for (auto key : {PopupBorderHover, BasicBorderHover, Win10BorderActive}) set(key, t.focus);
        for (auto key : {BasicText, BasicTextPress}) set(key, t.text);
        for (auto key : {BasicDetailsText, BasicTextNoFocus, BasicTextCategory, ScrollBarHandle, ToggleSwitchNoToggledCenter}) set(key, t.mutedText);
        set(BasicTextInvert, t.selectionText);
        set(BasicTextDisable, t.disabledText);
        set(BasicDisable, t.disabledSurface);
        for (auto key : {PopupHover, BasicHover, BasicHoverAlpha, BasicSelectedHover, BasicSelectedHoverAlpha}) set(key, t.hoverSurface);
        for (auto key : {BasicPress, BasicPressAlpha, BasicSelectedAlpha}) set(key, t.pressedSurface);
        set(BasicAlternating, t.alternateSurface);
        set(StatusDanger, t.error);
    }
    eTheme->setThemeMode(scheme == PinloomVisualScheme::Dark ? ElaThemeType::Dark : ElaThemeType::Light);
#else
    Q_UNUSED(scheme);
#ifdef PINLOOM_ENABLE_SUITEUI
    Q_UNUSED(application);
#else
    const auto style = qgetenv("PINLOOM_UI_STYLE").trimmed().toLower();
    if (!style.isEmpty() && style != "classic")
        qFatal("This build supports PINLOOM_UI_STYLE=classic only");
    application.setProperty("pinloomControlBackend", QStringLiteral("classic"));
#endif
#endif
}

QString scopedStyleSheet(QString sheet)
{
    if (!usesEla()) return sheet;
    // Only retained Qt views and explicitly opted-in native controls receive QSS.
    sheet.replace(QRegularExpression(QStringLiteral("\\b(QPushButton|QToolButton|QLineEdit|QPlainTextEdit|QComboBox|QSpinBox|QDoubleSpinBox|QCheckBox|QMenu|QMenuBar|QStatusBar|QScrollArea|QScrollBar)\\b")),
                  QStringLiteral("\\1[pinloomNativeStyle=\"true\"]"));
    sheet.replace(QRegularExpression(QStringLiteral("\\b(QAbstractItemView|QListView|QTreeView|QTableView|QListWidget|QTreeWidget|QTableWidget|QHeaderView)\\b")),
                  QStringLiteral("\\1[pinloomNativeView=\"true\"]"));
    return sheet;
}

void refreshViewPalettes(QApplication &application)
{
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) {
        for (auto *widget : application.allWidgets())
            if (widget->property("pinloomNativeItemContract").toBool()) refreshRole(widget);
    }
#else
    Q_UNUSED(application);
#endif
}

QPushButton *pushButton(QWidget *parent) { return pushButton(QString(), parent); }
QPushButton *pushButton(const QString &text, QWidget *parent)
{
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) { initializeEla(); return prepare(new Control<ElaPushButton>(text, parent)); }
#endif
    return new QPushButton(text, parent);
}
QPushButton *pushButton(const QIcon &icon, const QString &text, QWidget *parent)
{
    auto *button = pushButton(text, parent);
    button->setIcon(icon);
    return button;
}
QToolButton *toolButton(QWidget *parent)
{
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) {
        initializeEla();
        auto *button = prepare(new Control<ElaToolButton>(parent));
        button->setPopupMode(QToolButton::DelayedPopup);
        button->setIconSize(QSize(16, 16));
        button->setBorderRadius(6);
        QObject::connect(button, &QToolButton::toggled, button, &ElaToolButton::setIsSelected);
        return button;
    }
#endif
    return new QToolButton(parent);
}
QLineEdit *lineEdit(QWidget *parent) { return lineEdit(QString(), parent); }
QLineEdit *lineEdit(const QString &text, QWidget *parent)
{
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) {
        initializeEla();
        auto *edit = prepare(new Input(parent));
        edit->setIsClearButtonEnable(false);
        edit->setText(text);
        return edit;
    }
#endif
    return new QLineEdit(text, parent);
}
QComboBox *comboBox(QWidget *parent)
{
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) { initializeEla(); return prepare(new Choice(parent)); }
#endif
    return new QComboBox(parent);
}
QCheckBox *checkBox(QWidget *parent) { return checkBox(QString(), parent); }
QCheckBox *checkBox(const QString &text, QWidget *parent)
{
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) { initializeEla(); return prepare(new Control<ElaCheckBox>(text, parent)); }
#endif
    return new QCheckBox(text, parent);
}
QSpinBox *spinBox(QWidget *parent)
{
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) { initializeEla(); return prepare(new Control<ElaSpinBox>(parent)); }
#endif
    return new QSpinBox(parent);
}
QDoubleSpinBox *doubleSpinBox(QWidget *parent)
{
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) { initializeEla(); return prepare(new Control<ElaDoubleSpinBox>(parent)); }
#endif
    return new QDoubleSpinBox(parent);
}
QMenu *menu(QWidget *parent) { return menu(QString(), parent); }
QMenu *menu(const QString &title, QWidget *parent)
{
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) {
        initializeEla();
        auto *popup = prepare(new Menu(title, parent));
        popup->setNativeMenuBehavior(true);
        popup->setMenuItemHeight(qMax(28, popup->fontMetrics().height() + 12));
        return popup;
    }
#endif
    return new QMenu(title, parent);
}

QPlainTextEdit *plainTextEdit(QWidget *parent) { return plainTextEdit(QString(), parent); }
QPlainTextEdit *plainTextEdit(const QString &text, QWidget *parent)
{
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) {
        initializeEla();
        auto *edit = prepare(new PlainText(parent));
        edit->setNativeTextBehavior(true);
        installScrollBars(edit);
        edit->setPlainText(text);
        return edit;
    }
#endif
    return new QPlainTextEdit(text, parent);
}
QScrollArea *scrollArea(QWidget *parent)
{
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) {
        initializeEla();
        auto *area = prepare(new ElaScrollArea(parent));
        area->setIsAnimation(Qt::Horizontal, false);
        area->setIsAnimation(Qt::Vertical, false);
        area->setIsGrabGesture(false);
        installScrollBars(area);
        area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        return area;
    }
#endif
    return new QScrollArea(parent);
}
QMenuBar *menuBar(QWidget *parent)
{
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) {
        initializeEla();
        auto *bar = prepare(new ElaMenuBar(parent));
        if (auto *overflow = bar->findChild<QToolButton *>(QStringLiteral("qt_menubar_ext_button"))) {
            auto *previous = overflow->menu();
            overflow->setMenu(Ui::menu(bar));
            if (previous) previous->deleteLater();
        }
        return bar;
    }
#endif
    return new QMenuBar(parent);
}
QStatusBar *statusBar(QWidget *parent)
{
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) { initializeEla(); return prepare(new StatusBar(parent)); }
#endif
    return new QStatusBar(parent);
}

void initializeItemView(QAbstractItemView *view)
{
    if (usesEla()) {
#ifdef PINLOOM_ENABLE_ELA
        initializeEla();
        prepare(view);
        view->setProperty("pinloomNativeItemContract", true);
        class SpacedItemDelegate final : public QStyledItemDelegate {
        public:
            using QStyledItemDelegate::QStyledItemDelegate;
            QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
                auto size = QStyledItemDelegate::sizeHint(option, index);
                if (!index.data(Qt::SizeHintRole).isValid()) size.rheight() += 8;
                return size;
            }
        protected:
            void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
                QStyledItemDelegate::initStyleOption(option, index);
                if (index.data(Qt::ForegroundRole).isValid() || !(option->state & QStyle::State_Enabled)
                    || option->backgroundBrush.style() != Qt::SolidPattern) return;
                const QColor background = option->backgroundBrush.color();
                if (background.alpha() == 255
                    && pinloomContrastRatio(option->palette.color(QPalette::Text), background) < 4.5) {
                    const QColor foreground = pinloomContrastRatio(Qt::black, background)
                        >= pinloomContrastRatio(Qt::white, background) ? Qt::black : Qt::white;
                    option->palette.setColor(QPalette::Text, foreground);
                    option->palette.setColor(QPalette::Base, background);
                }
            }
        };
        view->setItemDelegate(new SpacedItemDelegate(view));
        installScrollBars(view);
        class ScopeObserver final : public QObject {
        public:
            explicit ScopeObserver(QAbstractItemView *view) : QObject(view), view_(view) { view->window()->installEventFilter(this); }
            bool eventFilter(QObject *, QEvent *event) override {
                if (event->type() == QEvent::DynamicPropertyChange
                    && static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName() == "trashMode") refreshRole(view_);
                return false;
            }
        private:
            QAbstractItemView *view_;
        };
        new ScopeObserver(view);
#endif
    } else {
        view->setStyleSheet({});
        view->setStyle(nullptr);
        view->setHorizontalScrollBar(new QScrollBar(Qt::Horizontal, view));
        view->setVerticalScrollBar(new QScrollBar(Qt::Vertical, view));
        view->setProperty("pinloomNativeView", true);
        if (auto *table = qobject_cast<QTableView *>(view)) {
            table->horizontalHeader()->setProperty("pinloomNativeView", true);
            table->verticalHeader()->setProperty("pinloomNativeView", true);
        }
        if (auto *tree = qobject_cast<QTreeView *>(view))
            tree->header()->setProperty("pinloomNativeView", true);
    }
}
QTreeView *treeView(QWidget *parent)
{
    QTreeView *view = nullptr;
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) { initializeEla(); view = new ElaTreeView(parent); }
#endif
    if (!view) view = new QTreeView(parent);
    initializeItemView(view);
    view->setAnimated(true);
    return view;
}
QLabel *label(QWidget *parent) { return label(QString(), parent); }
QLabel *label(const QString &text, QWidget *parent)
{
#ifdef PINLOOM_ENABLE_ELA
    if (usesEla()) {
        initializeEla();
        auto *result = prepare(new Text(text, parent));
        result->setThemeColorEnabled(false);
        result->setWordWrap(false);
        return result;
    }
#endif
    return new QLabel(text, parent);
}

DialogButtonBox::DialogButtonBox(StandardButtons buttons, QWidget *parent)
    : QDialogButtonBox(parent)
{
    setStandardButtons(buttons);
}
void DialogButtonBox::setStandardButtons(StandardButtons buttons)
{
    const auto old = standard_.values();
    for (const auto &entry : old) {
        auto *button = entry.data();
        if (!button) continue;
        removeButton(button);
        delete button;
    }
    QDialogButtonBox reference(buttons);
    for (auto *native : reference.buttons()) {
        const auto standard = reference.standardButton(native);
        addButton(standard);
    }
}
QPushButton *DialogButtonBox::addButton(StandardButton standard)
{
    if (auto *existing = button(standard)) return existing;
    QDialogButtonBox reference(standard);
    auto *native = reference.button(standard);
    if (!native) return nullptr;
    const auto role = reference.buttonRole(native);
    auto *replacement = addButton(native->text(), role);
    standard_.insert(standard, replacement);
    if (role == AcceptRole) {
        replacement->setDefault(true);
        replacement->setProperty("pinloomControl", QStringLiteral("primary"));
    }
    return replacement;
}
QDialogButtonBox::StandardButtons DialogButtonBox::standardButtons() const
{
    StandardButtons result;
    for (auto it = standard_.cbegin(); it != standard_.cend(); ++it)
        if (it.value()) result |= it.key();
    return result;
}
void DialogButtonBox::removeButton(QAbstractButton *button)
{
    standard_.remove(standardButton(button));
    QDialogButtonBox::removeButton(button);
}
void DialogButtonBox::clear()
{
    QDialogButtonBox::clear();
    standard_.clear();
}
QPushButton *DialogButtonBox::button(StandardButton standard) const { return standard_.value(standard).data(); }
QDialogButtonBox::StandardButton DialogButtonBox::standardButton(QAbstractButton *button) const
{
    for (auto it = standard_.cbegin(); it != standard_.cend(); ++it)
        if (it.value().data() == button) return it.key();
    return NoButton;
}
QPushButton *DialogButtonBox::addButton(const QString &text, ButtonRole role)
{
    auto *button = pushButton(text, this);
    QDialogButtonBox::addButton(button, role);
    return button;
}

QString getText(QWidget *parent, const QString &title, const QString &label,
                QLineEdit::EchoMode mode, const QString &text, bool *ok)
{
    if (!usesEla()) return QInputDialog::getText(parent, title, label, mode, text, ok);
    Pinloom::Ui::Dialog dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = new Pinloom::Ui::FormLayout(&dialog);
    auto *edit = lineEdit(text, &dialog);
    edit->setEchoMode(mode);
    edit->setAccessibleName(label);
    auto *buttons = new DialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(label, edit);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    edit->selectAll();
    edit->setFocus();
    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (ok) *ok = accepted;
    return accepted ? edit->text() : QString();
}
int getInt(QWidget *parent, const QString &title, const QString &label,
           int value, int min, int max, int step, bool *ok)
{
    if (!usesEla()) return QInputDialog::getInt(parent, title, label, value, min, max, step, ok);
    Pinloom::Ui::Dialog dialog(parent);
    dialog.setWindowTitle(title);
    auto *form = new Pinloom::Ui::FormLayout(&dialog);
    auto *spin = spinBox(&dialog);
    spin->setRange(min, max);
    spin->setSingleStep(step);
    spin->setValue(value);
    spin->setAccessibleName(label);
    auto *buttons = new DialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(label, spin);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    spin->selectAll();
    spin->setFocus();
    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (ok) *ok = accepted;
    return accepted ? spin->value() : value;
}
} // namespace Pinloom::Ui
