#include "pinloom/widgets/PinloomUiControls.h"
#include "pinloom/widgets/PinloomVisualTheme.h"
#include "ElaComboBox.h"
#include "ElaContentDialog.h"
#include "ElaDrawerArea.h"
#include "ElaListView.h"
#include "ElaMenu.h"
#include "ElaScrollBar.h"
#include "ElaTableView.h"
#include "ElaTreeView.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>

using namespace Pinloom;

class ElaInteractionsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        if (QFontDatabase::families().contains("Segoe UI")) qApp->setFont(QFont("Segoe UI", 9));
        applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Light);
    }
    void popupAnimationInputAndTeardown() {
        for (int repeat = 0; repeat < 6; ++repeat) {
            auto host = std::make_unique<QWidget>();
            auto *layout = new QVBoxLayout(host.get());
            auto *choice = Ui::comboBox(host.get());
            auto *ela = qobject_cast<ElaComboBox *>(choice);
            QVERIFY(ela);
            choice->addItems({"Saved", "History", "Trash"});
            layout->addWidget(choice);
            host->show();
            QCoreApplication::processEvents();
            QSignalSpy activated(choice, QOverload<int>::of(&QComboBox::activated));
            choice->showPopup();
            QVERIFY(ela->isPopupAnimating());
            QTest::keyClick(choice->view(), Qt::Key_Down);
            QVERIFY(!ela->isPopupAnimating());
            QTest::keyClick(choice->view(), Qt::Key_Return);
            QCOMPARE(choice->currentIndex(), 1);
            QCOMPARE(activated.size(), 1);
            choice->showPopup();
            QTRY_VERIFY(!ela->isPopupAnimating());
            QVERIFY(choice->view()->isVisible());
            QTest::keyClick(choice->view(), Qt::Key_Escape);
            QVERIFY(!choice->view()->isVisible());
            QCOMPARE(activated.size(), 1);
            choice->showPopup();
            host->resize(host->width() + 30, host->height());
            QVERIFY(!ela->isPopupAnimating());
            choice->hidePopup();
            choice->setEditable(true);
            choice->showPopup();
            host.reset();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(!QApplication::activePopupWidget());
        }
        std::unique_ptr<QComboBox> empty(Ui::comboBox());
        empty->showPopup();
        QVERIFY(!qobject_cast<ElaComboBox *>(empty.get())->isPopupAnimating());
    }
    void menuAnimationKeepsActionSemantics() {
        QWidget host;
        host.show();
        auto *menu = qobject_cast<ElaMenu *>(Ui::menu(&host));
        QVERIFY(menu);
        auto *disabled = menu->addAction("Disabled");
        disabled->setEnabled(false);
        auto *action = menu->addAction("Pinned");
        action->setCheckable(true);
        QSignalSpy invoked(action, &QAction::triggered);
        menu->popup(QPoint(10, 10));
        QVERIFY(menu->isPopupAnimating());
        QTest::keyClick(menu, Qt::Key_Down);
        QVERIFY(!menu->isPopupAnimating());
        QCOMPARE(menu->activeAction(), action);
        QTest::keyClick(menu, Qt::Key_Return);
        QCOMPARE(invoked.size(), 1);
        QVERIFY(action->isChecked());
        menu->popup(QPoint(10, 10));
        QTRY_VERIFY(!menu->isPopupAnimating());
        QVERIFY(menu->isVisible());
        QTest::keyClick(menu, Qt::Key_Escape);
        QCOMPARE(invoked.size(), 1);
        menu->popup(QPoint(10, 10));
        applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Dark);
        QVERIFY(!menu->isPopupAnimating());
        menu->hide();
        auto *submenu = Ui::menu("More", menu);
        auto *nested = submenu->addAction("Inspect");
        QSignalSpy nestedInvoked(nested, &QAction::triggered);
        menu->addMenu(submenu);
        menu->popup(QPoint(10, 10));
        menu->setActiveAction(submenu->menuAction());
        QTest::keyClick(menu, Qt::Key_Right);
        QTRY_VERIFY(submenu->isVisible());
        QTest::keyClick(submenu, Qt::Key_Down);
        QTest::keyClick(submenu, Qt::Key_Return);
        QCOMPARE(nestedInvoked.size(), 1);
        auto *embedded = new QWidgetAction(menu);
        auto *edit = Ui::lineEdit("live editor");
        embedded->setDefaultWidget(edit);
        menu->addAction(embedded);
        menu->popup(QPoint(10, 10));
        QVERIFY(!menu->isPopupAnimating());
        QTest::keyClicks(edit, "!");
        QVERIFY(edit->text().endsWith('!'));
        delete menu;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!QApplication::activePopupWidget());
        applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Light);
    }
    void drawerReversalInputHideResizeAndLifetime() {
        for (int repeat = 0; repeat < 6; ++repeat) {
            auto host = std::make_unique<QWidget>();
            auto *layout = new QVBoxLayout(host.get());
            auto *toggle = Ui::pushButton("Advanced");
            toggle->setCheckable(true);
            auto *content = new QWidget;
            auto *fields = new QVBoxLayout(content);
            auto *edit = Ui::lineEdit("preserved", content);
            fields->addWidget(edit);
            auto *drawer = qobject_cast<ElaDrawerArea *>(Ui::collapsibleSection(toggle, content, host.get()));
            QVERIFY(drawer);
            layout->addWidget(drawer);
            layout->addStretch();
            host->resize(500, 400);
            host->show();
            QCoreApplication::processEvents();
            toggle->click();
            QVERIFY(drawer->getIsExpand());
            QVERIFY(drawer->isDrawerAnimating());
            QVERIFY(drawer->drawerSnapshotBytes() > 0);
            QVERIFY(drawer->drawerSnapshotBytes() <= 32 * 1024 * 1024);
            QTest::qWait(25);
            toggle->click();
            toggle->click();
            QVERIFY(drawer->getIsExpand());
            QTest::keyClick(toggle, Qt::Key_Tab);
            QVERIFY(!drawer->isDrawerAnimating());
            QVERIFY(content->isVisible());
            QCOMPARE(drawer->drawerSnapshotBytes(), 0);
            QCOMPARE(edit->text(), QStringLiteral("preserved"));
            edit->setFocus();
            QTRY_VERIFY(edit->hasFocus());
            toggle->setChecked(false);
            QTRY_VERIFY(toggle->hasFocus());
            QVERIFY(!content->isVisible());
            toggle->setChecked(true);
            host->hide();
            QVERIFY(!drawer->isDrawerAnimating());
            QCOMPARE(drawer->drawerSnapshotBytes(), 0);
            host->show();
            QVERIFY(content->isVisible());
            toggle->setChecked(false);
            toggle->setChecked(true);
            host->resize(520, 410);
            QTRY_VERIFY(!drawer->isDrawerAnimating());
            QCOMPARE(drawer->drawerSnapshotBytes(), 0);
            toggle->setChecked(false);
            toggle->setChecked(true);
            QPointer<QWidget> lifetime(content);
            host.reset();
            QVERIFY(lifetime.isNull());
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    }
    void smoothWheelPrecisionAndInterruption() {
        std::unique_ptr<QScrollArea> area(Ui::scrollArea());
        auto *contents = new QWidget;
        contents->setMinimumSize(1000, 3000);
        area->setWidget(contents);
        area->resize(360, 240);
        area->show();
        QCoreApplication::processEvents();
        auto *bar = qobject_cast<ElaScrollBar *>(area->verticalScrollBar());
        QVERIFY(bar && bar->smoothWheelEnabled());
        bar->setValue(200);
        const auto wheel = [](QWidget *target, QPoint pixels, QPoint angles) {
            QWheelEvent event(QPointF(5, 5), QPointF(5, 5), pixels, angles,
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(target, &event);
        };
        wheel(bar, {}, QPoint(0, -120));
        QVERIFY(bar->findChild<QPropertyAnimation *>()->state() == QAbstractAnimation::Running);
        QTest::qWait(25);
        wheel(bar, {}, QPoint(0, 120));
        QTest::keyClick(area.get(), Qt::Key_Home);
        const int afterKey = bar->value();
        QTest::qWait(190);
        QCOMPARE(bar->value(), afterKey);
        bar->setValue(100);
        wheel(area->viewport(), QPoint(0, -7), {});
        QCOMPARE(bar->value(), 107);
        wheel(bar, {}, QPoint(0, -120));
        bar->setValue(300);
        QTest::qWait(190);
        QCOMPARE(bar->value(), 300);
        wheel(bar, {}, QPoint(0, -120));
        area->hide();
        const int hidden = bar->value();
        QTest::qWait(190);
        QCOMPARE(bar->value(), hidden);
    }
    void textPrecisionAndOwnedFocusAnimation() {
        QWidget host;
        auto *layout = new QVBoxLayout(&host);
        auto *line = Ui::lineEdit("unchanged", &host);
        const QString text = QStringLiteral("Unicode Ω text\n").repeated(200);
        auto *edit = Ui::plainTextEdit(text, &host);
        auto *native = new QPlainTextEdit(text, &host);
        edit->setFixedSize(300, 160);
        native->setFixedSize(300, 160);
        layout->addWidget(line);
        layout->addWidget(edit);
        layout->addWidget(native);
        host.show();
        host.activateWindow();
        QCoreApplication::processEvents();
        const auto markCount = [](QWidget *widget) {
            int count = 0;
            for (auto *animation : widget->findChildren<QPropertyAnimation *>())
                if (animation->propertyName() == "pExpandMarkWidth") ++count;
            return count;
        };
        QCOMPARE(markCount(line), 1);
        QCOMPARE(markCount(edit), 1);
        for (int repeat = 0; repeat < 12; ++repeat) {
            line->setFocus(Qt::TabFocusReason);
            QVERIFY(line->hasFocus());
            edit->setFocus(Qt::TabFocusReason);
            QVERIFY(edit->hasFocus());
        }
        QCOMPARE(markCount(line), 1);
        QCOMPARE(markCount(edit), 1);
        for (int angle : {0, -15, 15, -120, 120}) {
            for (auto *target : {edit, native}) target->verticalScrollBar()->setValue(0);
            for (int repeat = 0; repeat < 20; ++repeat) {
                for (auto *target : {edit, native}) {
                    QWheelEvent event(QPointF(5, 5), QPointF(5, 5), QPoint(0, angle > 0 ? 7 : -7), QPoint(0, angle),
                                      Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
                    QApplication::sendEvent(target->viewport(), &event);
                }
                QCOMPARE(edit->verticalScrollBar()->value(), native->verticalScrollBar()->value());
            }
        }
        QCOMPARE(edit->toPlainText(), text);
        QCOMPARE(line->text(), QStringLiteral("unchanged"));
    }
    void modalDecisionsMasksAndDefaultCancel() {
        QWidget host;
        host.resize(650, 450);
        host.show();
        for (int mode = 0; mode < 3; ++mode) {
            bool handled = false;
            QTimer::singleShot(0, [&] {
                auto *dialog = QApplication::activeModalWidget();
                QVERIFY(dialog && dialog->inherits("ElaContentDialog"));
                auto *buttons = dialog->findChild<Ui::DialogButtonBox *>("pinloomMessageButtons");
                QVERIFY(buttons);
                QVERIFY(buttons->button(QDialogButtonBox::Cancel)->isDefault());
                QCOMPARE(buttons->button(QDialogButtonBox::Ok)->text(), QStringLiteral("Open original PDF"));
                QCOMPARE(dialog->findChild<QPlainTextEdit *>()->toPlainText(), QStringLiteral("Original unchanged"));
                if (mode != 2) {
                    host.resize(670, 470);
                    auto *mask = host.findChild<QWidget *>("ElaMaskWidget");
                    QVERIFY(mask && mask->isVisible());
                    QCOMPARE(mask->geometry(), host.rect());
                }
                handled = true;
                if (mode == 0) QTest::keyClick(dialog, Qt::Key_Return);
                else if (mode == 1) buttons->button(QDialogButtonBox::Ok)->click();
                else QTest::keyClick(dialog, Qt::Key_Escape);
            });
            const bool accepted = Ui::confirm(mode == 2 ? nullptr : &host, "Preview",
                                               "Original unchanged", "Open original PDF");
            QVERIFY(handled);
            QCOMPARE(accepted, mode == 1);
            for (auto *mask : host.findChildren<QWidget *>("ElaMaskWidget")) QVERIFY(!mask->isVisible());
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        QTimer::singleShot(0, [] {
            QTest::keyClick(QApplication::activeModalWidget(), Qt::Key_Escape);
        });
        QCOMPARE(Ui::question(&host, "Delete", "Keep this item?"), QMessageBox::No);
        QTimer::singleShot(0, [] {
            QTest::keyClick(QApplication::activeModalWidget(), Qt::Key_Return);
        });
        QCOMPARE(Ui::warning(nullptr, "Warning", "Acknowledgement"), QMessageBox::Ok);
        auto parent = std::make_unique<QWidget>();
        QPointer<ElaContentDialog> dialog = new ElaContentDialog(parent.get());
        parent->show();
        dialog->show();
        parent.reset();
        QVERIFY(dialog.isNull());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void splitterPersistenceAndCorruptState() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings(directory.filePath("layout.ini"), QSettings::IniFormat);
        auto create = [] {
            auto splitter = std::make_unique<QSplitter>();
            splitter->setChildrenCollapsible(false);
            splitter->addWidget(new QWidget);
            splitter->addWidget(new QWidget);
            splitter->resize(600, 300);
            splitter->setSizes({200, 400});
            return splitter;
        };
        auto first = create();
        Ui::rememberSplitter(first.get(), &settings, "layout/test");
        first->setSizes({350, 250});
        QMetaObject::invokeMethod(first.get(), "splitterMoved", Qt::DirectConnection, Q_ARG(int, 350), Q_ARG(int, 1));
        const auto sizes = first->sizes();
        first.reset();
        auto restored = create();
        Ui::rememberSplitter(restored.get(), &settings, "layout/test");
        QCOMPARE(restored->sizes(), sizes);
        settings.setValue("layout/bad", QByteArray("invalid"));
        const auto state = restored->saveState();
        Ui::rememberSplitter(restored.get(), &settings, "layout/bad");
        QCOMPARE(restored->saveState(), state);
        QVERIFY(!restored->childrenCollapsible());
        auto *shortLived = new QSettings(directory.filePath("temporary.ini"), QSettings::IniFormat);
        Ui::rememberSplitter(restored.get(), shortLived, "layout/test");
        delete shortLived;
        QMetaObject::invokeMethod(restored.get(), "splitterMoved", Qt::DirectConnection, Q_ARG(int, 1), Q_ARG(int, 1));
    }
    void rawFocusedItemViewStyleLifetime() {
        for (int type = 0; type < 3; ++type) for (int repeat = 0; repeat < 4; ++repeat) {
            auto host = std::make_unique<QWidget>();
            auto *layout = new QVBoxLayout(host.get());
            QAbstractItemView *view = type == 0 ? static_cast<QAbstractItemView *>(new ElaListView(host.get()))
                : type == 1 ? static_cast<QAbstractItemView *>(new ElaTreeView(host.get()))
                            : static_cast<QAbstractItemView *>(new ElaTableView(host.get()));
            auto *model = new QStandardItemModel(host.get());
            model->appendRow(new QStandardItem("item"));
            view->setModel(model);
            layout->addWidget(view);
            host->show();
            host->activateWindow();
            view->setFocus();
            QTRY_VERIFY(view->hasFocus());
            view->setCurrentIndex(model->index(0, 0));
            delete view;
            host.reset();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    }
    void mappedScrollBarOriginReplacement() {
        for (int repeat = 0; repeat < 6; ++repeat) {
            auto area = std::make_unique<QScrollArea>();
            auto *contents = new QWidget;
            contents->setMinimumSize(800, 2400);
            area->setWidget(contents);
            auto *origin = new ElaScrollBar(Qt::Vertical, area.get());
            area->setVerticalScrollBar(origin);
            QPointer<QScrollBar> originLifetime(origin);
            QPointer<ElaScrollBar> overlay = new ElaScrollBar(origin, area.get());
            overlay->setSmoothWheelEnabled(true);
            area->resize(360, 240);
            area->show();
            QCoreApplication::processEvents();
            QWheelEvent wheel(QPointF(5, 5), QPointF(5, 5), {}, QPoint(0, -120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(overlay, &wheel);
            area->setVerticalScrollBar(new QScrollBar(Qt::Vertical, area.get()));
            QVERIFY(originLifetime.isNull());
            area->resize(400, 280);
            QCoreApplication::processEvents();
            QVERIFY(!area->grab().isNull());
            QVERIFY(overlay && !overlay->isVisible());
            overlay->setValue(10);
            area.reset();
            QVERIFY(overlay.isNull());
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    }
};
QTEST_MAIN(ElaInteractionsTest)
#include "ela_interactions_test.moc"
