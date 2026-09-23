#include "pinloom/widgets/PinloomUiControls.h"
#include "pinloom/widgets/AnchorCaptureDialog.h"
#include "pinloom/widgets/AnchorLocatorPreviewWidget.h"
#include "pinloom/widgets/ClipCaptureDialog.h"
#include "pinloom/widgets/PinloomMainWindow.h"
#include "pinloom/widgets/PinloomSettingsDialog.h"
#include "pinloom/widgets/ManualPdfAnchorDialog.h"
#include "pinloom/widgets/TextPreviewDialog.h"
#include "pinloom/widgets/PinloomVisualTheme.h"
#ifdef PINLOOM_ENABLE_SUITEUI
#include "PinloomSuiteUi.h"
#endif

#include <QApplication>
#include <QAccessible>
#include <QCheckBox>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLabel>
#include "pinloom/widgets/PinloomItemViews.h"
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProxyStyle>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QToolButton>
#include <QTemporaryFile>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>
#include <QtTest>

using namespace Pinloom;

class Contracts final : public QObject {
    Q_OBJECT
    QString initialStyle_;
private slots:
    void initTestCase() { initialStyle_ = qApp->style()->objectName(); }
    void groupedSettingsAndInterruptibleDrawers()
    {
        applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Light);
        PinloomAppSettings values;
        values.clipMaxTemporaryClips = 137;
        values.clipMaxTextBytes = 8192;
        values.clipTemporaryTtlSeconds = 321;
        values.clipExcludedSourceApps = {QStringLiteral("private-app.exe")};
        values.clipSensitiveTextMarkers = {QStringLiteral("private-marker")};
        PinloomSettingsDialog settings(values);
        settings.resize(480, 420);
        settings.show();
        settings.activateWindow();
        auto *scroll = settings.findChild<QScrollArea *>("settingsScrollArea");
        auto *toggle = settings.findChild<QPushButton *>("settingsAdvancedToggle");
        auto *content = settings.findChild<QWidget *>("settingsAdvancedContent");
        auto *drawer = settings.findChild<QWidget *>("settingsAdvancedSection");
        auto *limit = settings.findChild<QSpinBox *>("clipMaxTemporaryClipsSpin");
        QVERIFY(scroll && toggle && content && drawer && limit);
        QCOMPARE(drawer->inherits("ElaDrawerArea"), Ui::usesEla());
        for (const auto *name : {"settingsApplicationsSection", "settingsStorageSection", "settingsClipboardSection"}) {
            auto *card = settings.findChild<QWidget *>(name);
            QVERIFY(card);
            QCOMPARE(card->inherits("ElaScrollPageArea"), Ui::usesEla());
            QVERIFY(!card->accessibleName().isEmpty());
        }
        for (const auto &entry : QList<QPair<QString, QString>>{
                 {"settingsApplicationsSection", "sumatraPdfPathEdit"},
                 {"settingsStorageSection", "defaultLibraryRootPathEdit"},
                 {"settingsClipboardSection", "clipAutomaticCaptureCheck"}}) {
            auto *card = settings.findChild<QWidget *>(entry.first);
            auto *field = settings.findChild<QWidget *>(entry.second);
            QVERIFY(field && card->isAncestorOf(field));
            QCoreApplication::processEvents();
            QVERIFY(card->rect().contains(QRect(field->mapTo(card, QPoint()), field->size())));
        }
        QVERIFY(!content->isVisible());
        QVERIFY(!toggle->autoDefault());
        scroll->ensureWidgetVisible(toggle);
        toggle->setFocus();
        QTRY_VERIFY(toggle->hasFocus());
        auto *accessible = QAccessible::queryAccessibleInterface(toggle);
        QVERIFY(accessible && accessible->state().checkable);
        QSignalSpy toggled(toggle, &QPushButton::toggled);
        QTest::keyClick(toggle, Qt::Key_Space);
        QCOMPARE(toggled.count(), 1);
        QTRY_VERIFY(content->isVisible());
        QVERIFY(accessible->state().checked);
        QCOMPARE(limit->value(), 137);
        limit->setValue(138);
        limit->setFocus();
        QTRY_VERIFY(limit->hasFocus());
        toggle->setChecked(false);
        QVERIFY(!content->isVisible());
        QTRY_VERIFY(toggle->hasFocus());
        for (int repeat = 0; repeat < 12; ++repeat) {
            toggle->setChecked(true);
            QTRY_VERIFY(content->isVisible());
            toggle->setChecked(false);
            QTRY_VERIFY(!content->isVisible());
        }
        QCOMPARE(settings.settings().clipMaxTemporaryClips, 138);
        QCOMPARE(settings.settings().clipMaxTextBytes, values.clipMaxTextBytes);
        QCOMPARE(settings.settings().clipTemporaryTtlSeconds, values.clipTemporaryTtlSeconds);
        QCOMPARE(settings.settings().clipExcludedSourceApps, values.clipExcludedSourceApps);
        QCOMPARE(settings.settings().clipSensitiveTextMarkers, values.clipSensitiveTextMarkers);
        QTRY_COMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Dark);
        toggle->setChecked(true);
        QTRY_VERIFY(content->isVisible());
        QCoreApplication::processEvents();
        scroll->ensureWidgetVisible(limit);
        QVERIFY(limit->isVisible());
        QVERIFY(scroll->viewport()->rect().intersects(QRect(limit->mapTo(scroll->viewport(), QPoint()), limit->size())));
        QTRY_COMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        ManualPdfAnchorDialog manual;
        manual.show();
        auto *manualToggle = manual.findChild<QPushButton *>("manualPdfAnchorAdvancedToggleButton");
        auto *manualContent = manual.findChild<QWidget *>("manualPdfAnchorAdvancedWidget");
        QVERIFY(manualToggle && manualContent);
        manualToggle->click();
        QTRY_VERIFY(manualContent->isVisible());
        manualToggle->click();
        QVERIFY(!manualContent->isVisible());
        applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Light);
    }
    void sharedTagPopupKeyboardBoundsAndLifetime()
    {
        applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Light);
        for (int repeat = 0; repeat < 8; ++repeat) {
            auto host = std::make_unique<QWidget>();
            host->show();
            auto *popup = Ui::popupFrame(host.get());
            QPointer<QFrame> guard(popup);
            auto *layout = new QVBoxLayout(popup);
            auto *query = Ui::lineEdit(popup);
            auto *tags = new Ui::List(popup);
            auto *tag = new Ui::ListItem(QStringLiteral("Review"), tags);
            tag->setCheckState(Qt::Unchecked);
            layout->addWidget(query);
            layout->addWidget(tags);
            const auto bounds = popup->screen()->availableGeometry();
            popup->resize(320, 240);
            popup->move(bounds.bottomRight());
            popup->show();
            query->setFocus();
            QTRY_VERIFY(query->hasFocus());
            QVERIFY(bounds.contains(popup->geometry()));
            QCOMPARE(popup->property("pinloomPopupSurface").toBool(), Ui::usesEla());
            QCOMPARE(popup->testAttribute(Qt::WA_TranslucentBackground), Ui::usesEla());
            QTest::keyClick(query, Qt::Key_Tab);
            QTRY_VERIFY(tags->hasFocus());
            tags->setCurrentRow(0);
            QTest::keyClick(tags, Qt::Key_Space);
            QCOMPARE(tag->checkState(), Qt::Checked);
            applyPinloomVisualTheme(*qApp, repeat % 2 ? PinloomVisualScheme::Light : PinloomVisualScheme::Dark);
            if (repeat % 2) {
                QTest::keyClick(tags, Qt::Key_Escape);
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                QVERIFY(guard.isNull());
            }
            host.reset();
            QVERIFY(guard.isNull());
        }
        applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Light);
    }
    void itemModelsKeepRolesEditingKeyboardAndAccessibility()
    {
        applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Light);
        QWidget host;
        auto *layout = new QVBoxLayout(&host);
        auto *table = new Ui::Table(2, 2, &host);
        table->setAccessibleName(QStringLiteral("Library entries"));
        table->setHorizontalHeaderLabels({"Name", "Alias"});
        auto *beta = new Ui::TableItem("Beta");
        beta->setData(Qt::UserRole, "stable-beta");
        table->setItem(0, 0, beta);
        table->setItem(0, 1, new Ui::TableItem("second"));
        table->setItem(1, 0, new Ui::TableItem("Alpha"));
        table->setItem(1, 1, new Ui::TableItem("first"));
        layout->addWidget(table);
        auto *list = new Ui::List(&host);
        list->setAccessibleName(QStringLiteral("Tags"));
        auto *tag = new Ui::ListItem("First tag", list);
        tag->setCheckState(Qt::Unchecked);
        tag->setData(Qt::UserRole, "stable-tag");
        auto *hidden = new Ui::ListItem("Filtered tag", list);
        hidden->setHidden(true);
        auto *multi = new Ui::ListItem("Two-line command\nDescription", list);
        list->setUniformItemSizes(false);
        layout->addWidget(list);
        host.resize(400, 360);
        host.show();
        host.activateWindow();
        QVERIFY(qobject_cast<QStandardItemModel *>(table->model()));
        QVERIFY(qobject_cast<QStandardItemModel *>(list->model()));
        QCOMPARE(table->property("pinloomNativeView").toBool(), !Ui::usesEla());
        QCOMPARE(list->property("pinloomNativeView").toBool(), !Ui::usesEla());
        if (Ui::usesEla()) {
            QVERIFY(table->inherits("ElaTableView"));
            QVERIFY(list->inherits("ElaListView"));
        }
        table->setSortingEnabled(true);
        table->sortByColumn(0, Qt::AscendingOrder);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("Alpha"));
        QCOMPARE(table->item(1, 0), beta);
        QCOMPARE(beta->data(Qt::UserRole).toString(), QStringLiteral("stable-beta"));
        table->setCurrentItem(beta);
        QSignalSpy edited(table, &Ui::Table::itemChanged);
        QVERIFY(table->model()->setData(beta->index(), "Updated Beta", Qt::EditRole));
        QCOMPARE(edited.size(), 1);
        QCOMPARE(beta->text(), QStringLiteral("Updated Beta"));
        QCOMPARE(table->currentItem(), beta);
        const auto index = beta->index();
        auto *accessible = QAccessible::queryAccessibleInterface(table);
        QVERIFY(accessible && accessible->tableInterface());
        QCOMPARE(accessible->tableInterface()->rowCount(), 2);
        QCOMPARE(accessible->tableInterface()->cellAt(index.row(), 0)->text(QAccessible::Name), beta->text());
        QSignalSpy checks(list, &Ui::List::itemChanged);
        list->setCurrentRow(0);
        list->setFocus();
        QTRY_VERIFY(list->hasFocus());
        QTest::keyClick(list, Qt::Key_Space);
        QCOMPARE(tag->checkState(), Qt::Checked);
        QCOMPARE(checks.size(), 1);
        QTest::keyClick(list, Qt::Key_Down);
        QCOMPARE(list->currentItem(), multi);
        QVERIFY(hidden->isHidden());
        QCOMPARE(list->findItems("First tag", Qt::MatchExactly).size(), 1);
        QVERIFY(list->visualItemRect(multi).height() >= list->fontMetrics().height() * 2);
        auto *listAccess = QAccessible::queryAccessibleInterface(list);
        QVERIFY(listAccess);
        QCOMPARE(listAccess->text(QAccessible::Name), QStringLiteral("Tags"));
        for (const auto mode : {PinloomVisualScheme::Dark, PinloomVisualScheme::Light}) {
            host.setProperty("trashMode", true);
            applyPinloomVisualTheme(*qApp, mode);
            if (Ui::usesEla()) QCOMPARE(table->palette().color(QPalette::Highlight), pinloomVisualTokens(mode).error);
            QCOMPARE(beta->data(Qt::UserRole).toString(), QStringLiteral("stable-beta"));
            QCOMPARE(tag->checkState(), Qt::Checked);
        }
        table->clearContents();
        QCOMPARE(table->rowCount(), 2);
        QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("Name"));
        QVERIFY(!table->item(0, 0));
        list->clear();
        QCOMPARE(list->count(), 0);
    }
    void chromeLabelsAndNonModalNotices()
    {
        applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Light);
        Ui::Dialog dialog;
        dialog.setWindowTitle("Metadata");
        auto *form = new Ui::FormLayout(&dialog);
        auto *edit = Ui::lineEdit(&dialog);
        form->addRow(QStringLiteral("&Name"), edit);
        auto *caption = qobject_cast<QLabel *>(form->labelForField(edit));
        QVERIFY(caption);
        QCOMPARE(caption->inherits("ElaText"), Ui::usesEla());
        QCOMPARE(caption->buddy(), edit);
        auto *status = Ui::label(&dialog);
        status->setWordWrap(true);
        form->addRow(status);
        dialog.resize(380, 230);
        dialog.show();
        dialog.activateWindow();
        edit->setFocus();
        QTRY_VERIFY(edit->hasFocus());
        const QString message = QStringLiteral("Saved metadata; full diagnostics remain available. ").repeated(12);
        Ui::setStatusText(status, message);
        QCOMPARE(status->text(), message);
        QVERIFY(edit->hasFocus());
        auto *chrome = dialog.findChild<QWidget *>(QStringLiteral("ElaAppBar"));
        QCOMPARE(chrome != nullptr, Ui::usesEla());
        if (Ui::usesEla()) {
            QVERIFY(chrome->inherits("ElaAppBar"));
            QVERIFY(dialog.contentsMargins().top() >= chrome->height());
            QVERIFY(dialog.contentsRect().contains(edit->geometry()));
            auto *notice = dialog.findChild<QWidget *>(QStringLiteral("pinloomNoticeBar"));
            QVERIFY(notice && notice->inherits("ElaMessageBar"));
            QCOMPARE(notice->accessibleDescription(), message);
            QCOMPARE(Ui::showNotice(&dialog, message), notice);
            QPointer<QWidget> old = notice;
            notice = Ui::showNotice(&dialog, QStringLiteral("Updated"));
            QVERIFY(old.isNull());
            dialog.resize(280, 260);
            QCoreApplication::processEvents();
            QVERIFY(dialog.rect().contains(notice->geometry()));
            QVERIFY(edit->hasFocus());
            QSignalSpy rejected(&dialog, &QDialog::rejected);
            auto *close = chrome->findChild<QAbstractButton *>(QStringLiteral("pinloomWindowCloseButton"));
            QVERIFY(close && !close->accessibleName().isEmpty());
            close->click();
            QCOMPARE(rejected.size(), 1);
            QVERIFY(!dialog.isVisible());
        } else {
            QVERIFY(!Ui::showNotice(&dialog, message));
            QTest::keyClick(&dialog, Qt::Key_Escape);
            QVERIFY(!dialog.isVisible());
        }
        PinloomMainWindow resident;
        resident.setLauncherMode(true);
        resident.resize(400, 220);
        resident.show();
        QSignalSpy hiddenToTray(&resident, &PinloomMainWindow::hiddenToTray);
        if (Ui::usesEla()) {
            auto *close = resident.findChild<QAbstractButton *>(QStringLiteral("pinloomWindowCloseButton"));
            QVERIFY(close);
            close->click();
        } else resident.close();
        QCOMPARE(hiddenToTray.size(), 1);
        QVERIFY(!resident.isVisible());
        resident.show();
        QVERIFY(resident.windowHandle());
        resident.windowHandle()->close();
        QCOMPARE(hiddenToTray.size(), 2);
        QVERIFY(!resident.isVisible());
    }
    void treeModelsKeepExpansionKeyboardAndRoles()
    {
        QStandardItemModel model;
        model.setHorizontalHeaderLabels({"Name"});
        auto *folder = new QStandardItem("Folder");
        auto *file = new QStandardItem("Document.txt");
        file->setData("stable-file", Qt::UserRole);
        folder->appendRow(file);
        model.appendRow(folder);
        QWidget host;
        auto *layout = new QVBoxLayout(&host);
        auto *tree = Ui::treeView(&host);
        tree->setAccessibleName("Files");
        tree->setModel(&model);
        tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
        layout->addWidget(tree);
        host.resize(280, 200);
        host.show();
        host.activateWindow();
        tree->setCurrentIndex(folder->index());
        tree->setFocus();
        QTRY_VERIFY(tree->hasFocus());
        QCOMPARE(tree->inherits("ElaTreeView"), Ui::usesEla());
        QVERIFY(tree->isAnimated());
        QTest::keyClick(tree, Qt::Key_Right);
        QVERIFY(tree->isExpanded(folder->index()));
        QTest::keyClick(tree, Qt::Key_Down);
        QCOMPARE(tree->currentIndex().data(Qt::UserRole).toString(), QStringLiteral("stable-file"));
        auto *accessible = QAccessible::queryAccessibleInterface(tree);
        QVERIFY(accessible && accessible->tableInterface());
        QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Files"));
        QVERIFY(tree->viewport()->rect().contains(tree->visualRect(file->index())));
        QTest::keyClick(tree, Qt::Key_Left);
        QCOMPARE(tree->currentIndex(), folder->index());
        QTest::keyClick(tree, Qt::Key_Left);
        QVERIFY(!tree->isExpanded(folder->index()));
    }
    void plainTextPreviewContracts()
    {
        applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Light);
        QWidget host;
        auto *layout = new QVBoxLayout(&host);
        const QString content = QStringLiteral("Exact Unicode: \u03a9 \u6587\u672c\n")
            + QStringLiteral("long line ").repeated(160) + QStringLiteral("\nlast line");
        auto *edit = Ui::plainTextEdit(content, &host);
        QPlainTextEdit native;
        QCOMPARE(edit->tabChangesFocus(), native.tabChangesFocus());
        edit->setAccessibleName(QStringLiteral("Complete text preview"));
        edit->setReadOnly(true);
        edit->setTabChangesFocus(true);
        edit->setLineWrapMode(QPlainTextEdit::NoWrap);
        layout->addWidget(edit);
        auto *next = Ui::pushButton(QStringLiteral("Next"), &host);
        layout->addWidget(next);
        host.resize(340, 220);
        host.show();
        host.activateWindow();
        edit->setFocus();
        QTRY_VERIFY(edit->hasFocus());
        QCOMPARE(edit->inherits("ElaPlainTextEdit"), Ui::usesEla());
        QCOMPARE(edit->toPlainText(), content);
        auto *accessible = QAccessible::queryAccessibleInterface(edit);
        QVERIFY(accessible && accessible->textInterface());
        QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Complete text preview"));
        QVERIFY(accessible->state().readOnly);
        QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(edit, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(qApp->clipboard()->text(), content);
        QTest::keyClicks(edit, "must not replace selected text");
        QTest::keyClick(edit, Qt::Key_Backspace);
        QTest::keyClick(edit, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(edit->toPlainText(), content);
        QVERIFY(edit->horizontalScrollBar()->maximum() > 0);
        const auto geometry = host.geometry();
        const auto selection = edit->textCursor().selectedText();
        applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Dark);
        QCoreApplication::processEvents();
        QCOMPARE(host.geometry(), geometry);
        QCOMPARE(edit->textCursor().selectedText(), selection);
        QCOMPARE(edit->palette().color(QPalette::Text), pinloomVisualTokens(PinloomVisualScheme::Dark).text);
        QCOMPARE(edit->palette().color(QPalette::Highlight), pinloomVisualTokens(PinloomVisualScheme::Dark).selection);

        std::unique_ptr<QMenu> reference(edit->createStandardContextMenu());
        QStringList expected;
        for (auto *action : reference->actions()) expected << action->text();
        bool inspected = false;
        QTimer::singleShot(1000, &host, [] {
            if (auto *popup = QApplication::activePopupWidget()) popup->close();
        });
        QTimer::singleShot(0, &host, [&] {
            auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(popup);
            QCOMPARE(popup->inherits("ElaMenu"), Ui::usesEla());
            QStringList actual;
            for (auto *action : popup->actions()) actual << action->text();
            QCOMPARE(actual, expected);
            QTest::keyClick(popup, Qt::Key_Escape);
            inspected = true;
        });
        QContextMenuEvent context(QContextMenuEvent::Keyboard, QPoint(8, 8), edit->mapToGlobal(QPoint(8, 8)));
        QApplication::sendEvent(edit, &context);
        QTRY_VERIFY(inspected);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!QApplication::activePopupWidget());
        host.activateWindow();
        edit->setFocus();
        QTRY_VERIFY(edit->hasFocus());
        QTest::keyClick(edit, Qt::Key_Tab);
        QTRY_VERIFY(next->hasFocus());

        edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        QTRY_COMPARE(edit->horizontalScrollBar()->maximum(), 0);
        edit->setReadOnly(false);
        edit->selectAll();
        QTest::keyClicks(edit, "editable contract");
        QCOMPARE(edit->toPlainText(), QStringLiteral("editable contract"));
        edit->undo();
        QCOMPARE(edit->toPlainText(), content);
        edit->redo();
        QCOMPARE(edit->toPlainText(), QStringLiteral("editable contract"));
    }

    void textFileTargetLineAndMonospace()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        const QByteArray content = "first line\nsecond line\nthird line\n";
        QCOMPARE(file.write(content), qint64(content.size()));
        file.flush();
        TextPreviewDialog preview(file.fileName(), 2);
        QVERIFY(preview.load());
        preview.show();
        auto *edit = preview.findChild<QPlainTextEdit *>();
        QVERIFY(edit);
        QCOMPARE(edit->inherits("ElaPlainTextEdit"), Ui::usesEla());
        QCOMPARE(edit->toPlainText(), QString::fromUtf8(content));
        QCOMPARE(edit->textCursor().blockNumber(), 1);
        QCOMPARE(edit->extraSelections().size(), 1);
        QCOMPARE(edit->extraSelections().first().cursor.blockNumber(), 1);
        QCOMPARE(edit->lineWrapMode(), QPlainTextEdit::NoWrap);
        QVERIFY(edit->isReadOnly());
        QVERIFY(edit->font().families().contains(QStringLiteral("Consolas")));
    }

    void scrollAreaKeyboardWheelAndResize()
    {
        std::unique_ptr<QScrollArea> area(Ui::scrollArea());
        area->setWidgetResizable(true);
        auto *contents = new QWidget;
        auto *layout = new QVBoxLayout(contents);
        QLineEdit *last = nullptr;
        for (int index = 0; index < 24; ++index) {
            last = Ui::lineEdit(QString::number(index), contents);
            layout->addWidget(last);
        }
        area->setWidget(contents);
        area->resize(320, 230);
        area->show();
        QCoreApplication::processEvents();
        QCOMPARE(area->inherits("ElaScrollArea"), Ui::usesEla());
        auto *bar = area->verticalScrollBar();
        QCOMPARE(bar->inherits("ElaScrollBar"), Ui::usesEla());
        QVERIFY(bar->isVisible());
        QVERIFY(bar->maximum() > 0);
        QCOMPARE(area->horizontalScrollBar()->maximum(), 0);
        bar->setValue(0);
        QTest::keyClick(area.get(), Qt::Key_PageDown);
        QVERIFY(bar->value() > 0);
        QTest::keyClick(bar, Qt::Key_End);
        QCOMPARE(bar->value(), bar->maximum());
        QTest::keyClick(bar, Qt::Key_Home);
        QCOMPARE(bar->value(), bar->minimum());

        QScrollBar reference;
        reference.setRange(bar->minimum(), bar->maximum());
        reference.setSingleStep(bar->singleStep());
        reference.setPageStep(bar->pageStep());
        for (auto modifiers : {Qt::NoModifier, Qt::ControlModifier, Qt::ShiftModifier}) {
            const auto wheel = [&](QScrollBar *target) {
                QWheelEvent event(QPointF(3, 3), QPointF(3, 3), QPoint(), QPoint(0, -120),
                                  Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
                QApplication::sendEvent(target, &event);
            };
            wheel(&reference);
            wheel(bar);
            QTRY_COMPARE(bar->value(), reference.value());
        }
        area->ensureWidgetVisible(last);
        QCoreApplication::processEvents();
        QVERIFY(area->viewport()->rect().contains(QRect(last->mapTo(area->viewport(), QPoint()), last->size())));
        area->resize(280, 180);
        QCoreApplication::processEvents();
        area->ensureWidgetVisible(last);
        QCOMPARE(area->horizontalScrollBar()->maximum(), 0);
        QVERIFY(area->viewport()->rect().intersects(QRect(last->mapTo(area->viewport(), QPoint()), last->size())));
    }

    void menuAndStatusBarContracts()
    {
        PinloomMainWindow window;
        window.resize(520, 300);
        window.show();
        window.activateWindow();
        auto *bar = window.menuBar();
        auto *status = window.statusBar();
        QCOMPARE(bar->inherits("ElaMenuBar"), Ui::usesEla());
        QCOMPARE(status->inherits("ElaStatusBar"), Ui::usesEla());
        QSignalSpy changed(status, &QStatusBar::messageChanged);
        status->showMessage(QStringLiteral("Complete resident status"));
        QCOMPARE(status->currentMessage(), QStringLiteral("Complete resident status"));
        QCOMPARE(changed.size(), 1);
        auto *accessible = QAccessible::queryAccessibleInterface(status);
        QVERIFY(accessible);
        QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Pinloom resident status"));
        auto *menu = bar->actions().first()->menu();
        QVERIFY(menu);
        auto *settings = window.findChild<QAction *>("settingsAction");
        QSignalSpy requested(&window, &PinloomMainWindow::settingsRequested);
        bar->setFocus();
        bar->setActiveAction(menu->menuAction());
        QTest::keyClick(bar, Qt::Key_Down);
        QTRY_VERIFY(menu->isVisible());
        menu->setActiveAction(settings);
        QTest::keyClick(menu, Qt::Key_Return);
        QCOMPARE(requested.size(), 1);
        QVERIFY(!menu->isVisible());
        auto largeFont = qApp->font();
        largeFont.setPointSize(24);
        bar->setFont(largeFont);
        status->setFont(largeFont);
        QCoreApplication::processEvents();
        QTRY_VERIFY(bar->height() >= bar->fontMetrics().height());
        QTRY_VERIFY(status->height() >= status->fontMetrics().height());
        QVERIFY(window.rect().contains(bar->geometry()));
        QVERIFY(window.rect().contains(status->geometry()));
        for (int index = 0; index < 8; ++index) {
            auto *extra = Ui::menu(QStringLiteral("Menu %1").arg(index), bar);
            extra->addAction(QStringLiteral("Inspect"));
            bar->addMenu(extra);
        }
        window.resize(280, 300);
        auto *overflow = bar->findChild<QToolButton *>(QStringLiteral("qt_menubar_ext_button"));
        QVERIFY(overflow);
        QTRY_VERIFY(overflow->isVisible());
        bool overflowInspected = false;
        QTimer::singleShot(1000, &window, [] {
            if (auto *popup = QApplication::activePopupWidget()) popup->close();
        });
        QTimer::singleShot(0, &window, [&] {
            auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(popup && !popup->actions().isEmpty());
            QCOMPARE(popup->inherits("ElaMenu"), Ui::usesEla());
            QTest::keyClick(popup, Qt::Key_Escape);
            overflowInspected = true;
        });
        overflow->showMenu();
        QTRY_VERIFY(overflowInspected);
        window.setLauncherMode(true);
        QVERIFY(bar->isHidden() && status->isHidden());
        window.setLauncherMode(false);
        QVERIFY(bar->isVisible() && status->isVisible());
        QSignalSpy hidden(&window, &PinloomMainWindow::hiddenToTray);
        window.close();
        QCOMPARE(hidden.size(), 1);
    }

    void actualControlsAndBoundaries()
    {
        const QString themeName = qEnvironmentVariable("PINLOOM_TEST_THEME", "light");
        QVERIFY2(themeName == "light" || themeName == "dark", "Unsupported test theme");
        const auto theme = themeName == "dark" ? PinloomVisualScheme::Dark : PinloomVisualScheme::Light;
        const auto originalFont = qApp->font();
        const auto originalStyle = initialStyle_;
        applyPinloomVisualTheme(*qApp, theme);
        QStyle *backend = nullptr;
#ifdef PINLOOM_ENABLE_SUITEUI
        backend = SuiteUiAdapter::installedStyle();
        if (SuiteUiAdapter::enabled()) {
            QVERIFY(backend);
            QCOMPARE(backend->objectName(), QStringLiteral("PinloomSuiteUi"));
            QCOMPARE(qApp->property("pinloomControlBackend").toString(), backend->objectName());
            auto *proxy = qobject_cast<QProxyStyle *>(backend);
            QVERIFY(proxy);
            QCOMPARE(proxy->baseStyle()->objectName(), originalStyle);
            QCOMPARE(backend->property("pinloomAnimationsEnabled").toBool(),
                     !pinloomReducedMotionEnabled());
        } else {
            QVERIFY(!backend);
            QCOMPARE(qApp->property("pinloomControlBackend").toString(), QStringLiteral("classic"));
        }
#else
        if (Ui::usesEla()) {
            QCOMPARE(qApp->property("pinloomControlBackend").toString(), QStringLiteral("ela"));
        } else {
            QVERIFY(!qApp->property("pinloomControlBackend").isValid()
                    || qApp->property("pinloomControlBackend") == QStringLiteral("classic"));
        }
#endif
        QCOMPARE(qApp->font(), originalFont);
        const QString renderer = Ui::usesEla() ? QStringLiteral("ela")
            : backend ? backend->objectName() : QStringLiteral("classic");
        const auto available = qApp->primaryScreen()->availableGeometry();
        const auto metrics = pinloomVisualMetrics();
        const int margin = metrics.baseSpacing * 2;
        const QRect bounds = available.adjusted(margin, margin, -margin, -margin);
        QVERIFY(bounds.isValid());
        AnchorCaptureDraft draft;
        draft.targetApp = QStringLiteral("PDF");
        draft.targetUri = QStringLiteral("fixture.pdf");
        draft.locatorType = QStringLiteral("pdf.page");
        draft.locatorJson = QStringLiteral("{\"page\":1}");
        draft.suggestedName = QStringLiteral("Review anchor");
        draft.provenance = QStringLiteral("isolated UI fixture");
        QScrollArea host;
        host.setObjectName(QStringLiteral("suiteUiCaptureViewport"));
        auto *dialog = new AnchorCaptureDialog(draft);
        dialog->setModal(false);
        dialog->setWindowFlags(Qt::Widget);
        dialog->resize(dialog->sizeHint());
        host.setWidget(dialog);
        host.setGeometry(bounds);
        host.show();
        QTest::qWait(40);
        QCOMPARE(host.size(), bounds.size());
        QVERIFY(host.rect().contains(host.viewport()->geometry()));
        const auto stableSize = host.size();
        for (const auto next : {theme == PinloomVisualScheme::Dark ? PinloomVisualScheme::Light
                                                                 : PinloomVisualScheme::Dark, theme}) {
            applyPinloomVisualTheme(*qApp, next);
            QCoreApplication::processEvents();
            QCOMPARE(host.size(), stableSize);
            QCOMPARE(qApp->font(), originalFont);
#ifdef PINLOOM_ENABLE_SUITEUI
            QCOMPARE(SuiteUiAdapter::installedStyle(), backend);
#endif
        }
        auto *name = dialog->findChild<QLineEdit *>("anchorCaptureNameEdit");
        auto *pinned = dialog->findChild<QCheckBox *>("anchorCapturePinnedCheck");
        auto *buttons = dialog->findChild<Pinloom::Ui::DialogButtonBox *>("anchorCaptureButtons");
        QVERIFY(name && pinned && buttons);
        auto *save = buttons->button(QDialogButtonBox::Save);
        auto *cancel = buttons->button(QDialogButtonBox::Cancel);
        QVERIFY(save && cancel);
        QCOMPARE(save->property("pinloomControl").toString(), QStringLiteral("primary"));
        QVERIFY(save->isEnabled());
        const auto tokens = pinloomVisualTokens(theme);
        const auto cancelPixels = cancel->grab().toImage();
        QCOMPARE(cancelPixels.pixelColor(cancelPixels.width() / 2,
                                         qRound(metrics.baseSpacing * cancelPixels.devicePixelRatio())), tokens.panel);
        const auto savePixels = save->grab().toImage();
        int primaryTextPixels = 0;
        for (int y = 0; y < savePixels.height(); ++y)
            for (int x = 0; x < savePixels.width(); ++x)
                primaryTextPixels += savePixels.pixelColor(x, y) == tokens.selectionText;
        QVERIFY2(primaryTextPixels > 0, "Primary action must retain its on-accent text color");
        QVERIFY(pinloomContrastRatio(tokens.selectionText, tokens.selection) >= 4.5);
        const QString output = qEnvironmentVariable("PINLOOM_MATRIX_DIR");
        if (!output.isEmpty()) {
            QVERIFY(QDir().mkpath(output));
            QVERIFY(host.grab().save(output + "/capture.png"));
        }
        QSignalSpy pinnedChanges(pinned, &QCheckBox::toggled);
        host.ensureWidgetVisible(pinned, margin, margin);
        pinned->setFocus();
        QTest::keyClick(pinned, Qt::Key_Space);
        QVERIFY(pinned->isChecked());
        QStyleOptionButton checkOption;
        checkOption.initFrom(pinned);
        const auto indicator = pinned->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &checkOption, pinned);
        QTest::mouseClick(pinned, Qt::LeftButton, Qt::NoModifier, indicator.center());
        QVERIFY(!pinned->isChecked());
        QCOMPARE(pinnedChanges.count(), 2);

        host.ensureWidgetVisible(name, margin, margin);
        name->setFocus();
        QTest::keyClick(name, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(name, Qt::Key_Backspace);
        QVERIFY(name->text().isEmpty());
        QVERIFY(!save->isEnabled());
        QSignalSpy accepted(dialog, &QDialog::accepted);
        QTest::mouseClick(save, Qt::LeftButton);
        QCOMPARE(accepted.count(), 0);
        QTest::keyClicks(name, "Verified anchor");
        QVERIFY(save->isEnabled());
        host.ensureWidgetVisible(save, margin, margin);
        QVERIFY(host.viewport()->rect().intersects(QRect(save->mapTo(host.viewport(), QPoint()), save->size())));
        const auto normal = save->grab().toImage();
        QTest::mousePress(save, Qt::LeftButton);
        QTest::qWait(230);
        const auto pressed = save->grab().toImage();
        QVERIFY(normal != pressed);
        QTest::mouseRelease(save, Qt::LeftButton);
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(dialog->draft().suggestedName, QStringLiteral("Verified anchor"));
        QVERIFY(!dialog->draft().mutationAuthorized);
        QVERIFY(!dialog->draft().pinned);

        dialog->show();
        QSignalSpy rejected(dialog, &QDialog::rejected);
        host.ensureWidgetVisible(cancel, margin, margin);
        QTest::mouseClick(cancel, Qt::LeftButton);
        QCOMPARE(rejected.count(), 1);

        ClipCaptureDialog clip(QStringLiteral("fixture text"), QStringLiteral("Clip"), {"alpha", "beta"});
        clip.show();
        auto *tags = clip.findChild<QToolButton *>("clipCaptureTagsButton");
        QVERIFY(tags);
        QTest::mouseClick(tags, Qt::LeftButton);
        auto *popup = clip.findChild<QWidget *>("clipCaptureTagPicker");
        QVERIFY(popup && popup->isVisible());
        auto *filter = popup->findChild<QLineEdit *>("clipCaptureTagFilter");
        auto *create = popup->findChild<QToolButton *>("clipCaptureCreateTagButton");
        QVERIFY(filter && create);
        QVERIFY(!create->isEnabled());
        QTest::keyClicks(filter, "gamma");
        QVERIFY(create->isEnabled());
        QTest::mouseClick(create, Qt::LeftButton);
        QVERIFY(clip.metadata().tags.contains(QStringLiteral("gamma")));
        QTest::keyClick(popup, Qt::Key_Escape);
        QCoreApplication::processEvents();
        QVERIFY(!clip.findChild<QWidget *>("clipCaptureTagPicker")
                || !clip.findChild<QWidget *>("clipCaptureTagPicker")->isVisible());
        clip.close();

        AnchorLocatorPreviewWidget preview;
        QPixmap page(metrics.regularControlHeight * 4, metrics.regularControlHeight * 3);
        page.fill(pinloomVisualTokens(theme).panel);
        preview.setScreenshot(page);
        preview.ensurePolished();
        QVERIFY(preview.hasScreenshot());
        auto *viewButton = new QPushButton(QStringLiteral("boundary probe"), &preview);
        viewButton->ensurePolished();
        if (backend) {
            QVERIFY(preview.property("pinloomSuiteUiClassic").toBool());
            QVERIFY(viewButton->property("pinloomSuiteUiClassic").toBool());
            viewButton->setParent(&host);
            QVERIFY(!viewButton->property("pinloomSuiteUiClassic").toBool());
        }
        QJsonObject evidence{{"renderer", renderer}, {"theme", themeName},
            {"scale", qEnvironmentVariable("QT_SCALE_FACTOR", "1")},
            {"reducedMotion", pinloomReducedMotionEnabled()},
            {"hostWidth", host.width()}, {"hostHeight", host.height()},
            {"screenWidth", available.width()}, {"screenHeight", available.height()},
            {"pinnedSignals", pinnedChanges.count()}, {"accepted", accepted.count()},
            {"rejected", rejected.count()}, {"tagCreated", true},
            {"nativeFramePacingMeasured", false},
            {"fixture", QStringLiteral("real dialogs in scroll viewport; no document commit")},
            {"baseStyle", originalStyle}};
        if (!output.isEmpty()) {
            QFile file(output + "/result.json");
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write(QJsonDocument(evidence).toJson()) > 0);
        }
        qInfo().noquote() << QJsonDocument(evidence).toJson(QJsonDocument::Compact);
    }
};

QTEST_MAIN(Contracts)
#include "suiteui_controls_test.moc"
