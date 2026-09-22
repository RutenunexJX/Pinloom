#include "pinloom/widgets/PinloomUiControls.h"
#include "pinloom/widgets/PinloomVisualTheme.h"
#include "pinloom/widgets/AnchorCaptureDialog.h"
#include "pinloom/widgets/AnchorLibraryWindow.h"
#include "pinloom/widgets/ClipCaptureDialog.h"
#include "pinloom/widgets/ClipLibraryWindow.h"
#include "pinloom/widgets/ClipTrayPresenter.h"
#include "pinloom/widgets/LibraryRootWindow.h"
#include "pinloom/widgets/ManualPdfAnchorDialog.h"
#include "pinloom/widgets/PinloomCommandPanel.h"
#include "pinloom/widgets/PinloomMainWindow.h"
#include "pinloom/widgets/PinloomSettingsDialog.h"

#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFontInfo>
#include <QFontDatabase>
#include <QHelpEvent>
#include <QPlainTextEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QRawFont>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include "pinloom/widgets/PinloomItemViews.h"
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

using namespace Pinloom;

class ElaControlsTest : public QObject {
    Q_OBJECT
private:
    PinloomVisualScheme scheme_ = PinloomVisualScheme::Light;
    void snapshot(QWidget &widget, const QString &name) {
        const auto directory = qEnvironmentVariable("PINLOOM_PREVIEW_DIR");
        if (directory.isEmpty()) return;
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(widget.grab().save(directory + '/' + name + ".png"));
    }
private slots:
    void initTestCase() {
        QVERIFY(Ui::usesEla());
#ifdef Q_OS_WIN
        // Offscreen has no Windows system-font mapping for "Sans Serif".
        if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
            QVERIFY(QFontDatabase::families().contains(QStringLiteral("Segoe UI")));
            qApp->setFont(QFont(QStringLiteral("Segoe UI"), 9));
        }
#endif
        scheme_ = qEnvironmentVariable("PINLOOM_TEST_THEME") == "dark"
            ? PinloomVisualScheme::Dark : PinloomVisualScheme::Light;
        const auto originalFont = qApp->font();
        const auto resolvedFont = QFontInfo(originalFont).family();
        QVERIFY2(!resolvedFont.isEmpty(), "Offscreen previews require QT_QPA_FONTDIR with real text fonts");
        applyPinloomVisualTheme(*qApp, scheme_);
        QCOMPARE(QFontInfo(qApp->font()).family(), resolvedFont);
        QCOMPARE(qApp->font(), originalFont);
        QCOMPARE(qApp->property("pinloomControlBackend").toString(), QStringLiteral("ela"));
        QVERIFY(qApp->style()->objectName() != QStringLiteral("PinloomSuiteUi"));
    }
    void realControlTypesAndKeyboard() {
        QDialog dialog;
        auto *form = new QFormLayout(&dialog);
        auto *edit = Ui::lineEdit(QStringLiteral("Original"), &dialog);
        edit->setAccessibleName(QStringLiteral("Name"));
        auto *check = Ui::checkBox(QStringLiteral("&Pinned"), &dialog);
        auto *choice = Ui::comboBox(&dialog);
        choice->addItems({"Saved", "History", "Trash"});
        auto *spin = Ui::spinBox(&dialog);
        auto *decimal = Ui::doubleSpinBox(&dialog);
        auto *button = Ui::pushButton(QStringLiteral("&Run"), &dialog);
        auto *tool = Ui::toolButton(&dialog);
        tool->setText(QStringLiteral("Tools"));
        const QList<QPair<QWidget *, const char *>> controls = {
            {edit, "ElaLineEdit"}, {check, "ElaCheckBox"}, {choice, "ElaComboBox"},
            {spin, "ElaSpinBox"}, {decimal, "ElaDoubleSpinBox"},
            {button, "ElaPushButton"}, {tool, "ElaToolButton"}};
        for (const auto &entry : controls) {
            QVERIFY(entry.first->inherits(entry.second));
            QVERIFY(entry.first->property("pinloomElaControl").toBool());
            QCOMPARE(entry.first->font(), qApp->font());
            QVERIFY(entry.first->focusPolicy() != Qt::NoFocus);
            form->addRow(entry.first);
        }
        dialog.show();
        dialog.activateWindow();
        edit->setFocus();
        QTRY_VERIFY(edit->hasFocus());
        QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(edit, "Updated");
        QCOMPARE(edit->text(), QStringLiteral("Updated"));
        auto *accessible = QAccessible::queryAccessibleInterface(edit);
        QVERIFY(accessible);
        QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Name"));
        QTest::keyClick(edit, Qt::Key_Tab);
        QTRY_VERIFY(check->hasFocus());
        QSignalSpy toggles(check, &QCheckBox::toggled);
        QTest::keyClick(check, Qt::Key_Space);
        QVERIFY(check->isChecked());
        QCOMPARE(toggles.size(), 1);
        spin->setValue(4);
        QTest::keyClick(spin, Qt::Key_Up);
        QCOMPARE(spin->value(), 5);
        QSignalSpy clicks(button, &QPushButton::clicked);
        QTest::keyClick(button, Qt::Key_Space);
        QCOMPARE(clicks.size(), 1);
        snapshot(dialog, "controls");
    }
    void managedToolTipsStayBoundedDynamicAndPrivate() {
        for (int repeat = 0; repeat < 6; ++repeat) {
            auto host = std::make_unique<QWidget>();
            auto *layout = new QVBoxLayout(host.get());
            auto *button = Ui::pushButton(QStringLiteral("Inspect"), host.get());
            layout->addWidget(button);
            button->setToolTip(QStringLiteral("First hint"));
            button->setAccessibleName(QStringLiteral("Inspect metadata"));
            host->show();
            host->activateWindow();
            button->setFocus();
            QTRY_VERIFY(button->hasFocus());
            const auto bounds = button->screen()->availableGeometry();
            QHelpEvent help(QEvent::ToolTip, button->rect().center(), bounds.bottomRight());
            QApplication::sendEvent(button, &help);
            QPointer<QWidget> tip = host->findChild<QWidget *>("pinloomToolTip");
            QVERIFY(tip && tip->inherits("ElaToolTip") && tip->isVisible());
            QVERIFY(bounds.contains(tip->geometry()));
            QVERIFY(button->hasFocus());
            QCOMPARE(button->toolTip(), QStringLiteral("First hint"));
            QCOMPARE(tip->accessibleDescription(), button->toolTip());
            button->setToolTip(QStringLiteral("<literal> updated hint and long/path/").repeated(180));
            QVERIFY(tip->isVisible());
            QCOMPARE(tip->accessibleDescription(), button->toolTip());
            QVERIFY(bounds.contains(tip->geometry()));
            QVERIFY(tip->width() <= 544);
            auto *body = tip->findChild<QLabel *>(QString(), Qt::FindDirectChildrenOnly);
            QVERIFY(body);
            for (auto *text : tip->findChildren<QLabel *>())
                if (text->isVisible()) QCOMPARE(text->textFormat(), Qt::PlainText);
            applyPinloomVisualTheme(*qApp, repeat % 2 ? PinloomVisualScheme::Light : PinloomVisualScheme::Dark);
            QVERIFY(button->hasFocus());
            if (!repeat) {
                applyPinloomVisualTheme(*qApp, scheme_);
                snapshot(*tip, "tooltip");
            }
            button->setToolTip({});
            QVERIFY(!tip->isVisible());
            button->setToolTip(QStringLiteral("Brief hint"));
            button->setToolTipDuration(1);
            QApplication::sendEvent(button, &help);
            QTRY_VERIFY(!tip->isVisible());
            button->setToolTipDuration(-1);
            QApplication::sendEvent(button, &help);
            QVERIFY(tip->isVisible());
            QTest::keyClick(button, Qt::Key_Escape);
            QVERIFY(!tip->isVisible());
            QApplication::sendEvent(button, &help);
            host.reset();
            QVERIFY(tip.isNull());
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        applyPinloomVisualTheme(*qApp, scheme_);
        QWidget host;
        auto *layout = new QVBoxLayout(&host);
        auto *table = new Ui::Table(2, 1, &host);
        auto *item = new Ui::TableItem(QStringLiteral("Metadata only"));
        item->setToolTip(QStringLiteral("Explicit location"));
        table->setItem(0, 0, item);
        auto *nextItem = new Ui::TableItem(QStringLiteral("Next metadata"));
        table->setItem(1, 0, nextItem);
        layout->addWidget(table);
        host.show();
        QCoreApplication::processEvents();
        const auto point = table->visualItemRect(item).center();
        QHelpEvent help(QEvent::ToolTip, point, table->viewport()->mapToGlobal(point));
        QApplication::sendEvent(table->viewport(), &help);
        auto *tip = host.findChild<QWidget *>("pinloomToolTip");
        QVERIFY(tip && tip->isVisible());
        QCOMPARE(tip->accessibleDescription(), item->toolTip());
        item->setToolTip(QStringLiteral("Changed location"));
        QCOMPARE(tip->accessibleDescription(), item->toolTip());
        const auto nextPoint = table->visualItemRect(nextItem).center();
        QMouseEvent move(QEvent::MouseMove, nextPoint, table->viewport()->mapToGlobal(nextPoint),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(table->viewport(), &move);
        QVERIFY(!tip->isVisible());
        QApplication::sendEvent(table->viewport(), &help);
        QVERIFY(tip->isVisible());
        table->verticalScrollBar()->setRange(0, 10);
        table->verticalScrollBar()->setValue(1);
        QVERIFY(!tip->isVisible());
        QApplication::sendEvent(table->viewport(), &help);
        table->clearContents();
        QVERIFY(!tip->isVisible());

        QMessageBox safety(QMessageBox::Warning, QStringLiteral("Safety"),
                           QStringLiteral("Native confirmation"), QMessageBox::Cancel, &host);
        safety.show();
        auto *cancel = safety.button(QMessageBox::Cancel);
        cancel->setToolTip(QStringLiteral("Native safety hint"));
        QHelpEvent nativeHelp(QEvent::ToolTip, cancel->rect().center(), cancel->mapToGlobal(cancel->rect().center()));
        QApplication::sendEvent(cancel, &nativeHelp);
        QVERIFY(!safety.findChild<QWidget *>("pinloomToolTip"));
        QToolTip::hideText();
        safety.close();

        Clip clip;
        clip.id = "tooltip-privacy";
        clip.state = ClipState::Saved;
        clip.name = "Named clip";
        clip.text = QStringLiteral("Private body must stay on the right").repeated(50);
        ClipLibraryWindowOptions options;
        options.clipsProvider = [clip] { return QList<Clip>{clip}; };
        ClipLibraryWindow library(options);
        library.setScope(ClipLibraryScope::Saved);
        library.show();
        QVERIFY(library.selectClipAt(0));
        auto *clips = library.findChild<Ui::Table *>("clipLibraryTable");
        QVERIFY(clips);
        for (int column = 0; column < clips->columnCount(); ++column) {
            const auto index = clips->model()->index(0, column);
            const auto point = clips->visualRect(index).center();
            QHelpEvent event(QEvent::ToolTip, point, clips->viewport()->mapToGlobal(point));
            QApplication::sendEvent(clips->viewport(), &event);
            auto *clipTip = library.findChild<QWidget *>("pinloomToolTip");
            QVERIFY(!clipTip || !clipTip->isVisible()
                    || !clipTip->accessibleDescription().contains(QStringLiteral("Private body")));
        }
        QCOMPARE(library.findChild<QPlainTextEdit *>()->toPlainText(), clip.text);
    }
    void standardButtonsAndModalPrompts() {
        QDialog dialog;
        auto *layout = new QFormLayout(&dialog);
        auto *edit = Ui::lineEdit(&dialog);
        auto *buttons = new Ui::DialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        layout->addRow(edit);
        layout->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        auto *save = buttons->button(QDialogButtonBox::Save);
        QVERIFY(save->inherits("ElaPushButton"));
        QCOMPARE(buttons->standardButton(save), QDialogButtonBox::Save);
        QCOMPARE(buttons->standardButtons(), QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        QCOMPARE(buttons->buttonRole(save), QDialogButtonBox::AcceptRole);
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        dialog.show();
        dialog.activateWindow();
        edit->setFocus();
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(accepted.size(), 1);
        QSignalSpy rejected(&dialog, &QDialog::rejected);
        dialog.show();
        QTest::keyClick(&dialog, Qt::Key_Escape);
        QCOMPARE(rejected.size(), 1);
        auto *extra = buttons->addButton(QDialogButtonBox::Apply);
        QVERIFY(extra->inherits("ElaPushButton"));
        QCOMPARE(buttons->standardButton(extra), QDialogButtonBox::Apply);
        buttons->removeButton(extra);
        delete extra;
        QVERIFY(!buttons->button(QDialogButtonBox::Apply));

        QTimer::singleShot(0, [] {
            auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(modal);
            auto *input = modal->findChild<QLineEdit *>();
            QVERIFY(input && input->inherits("ElaLineEdit"));
            input->setText(QStringLiteral("Renamed"));
            QTest::keyClick(input, Qt::Key_Return);
        });
        bool ok = false;
        QCOMPARE(Ui::getText(nullptr, "Rename", "Name", QLineEdit::Normal, "Old", &ok), QStringLiteral("Renamed"));
        QVERIFY(ok);
        QTimer::singleShot(0, [] {
            QTest::keyClick(QApplication::activeModalWidget(), Qt::Key_Escape);
        });
        QCOMPARE(Ui::getInt(nullptr, "Line", "Line", 7, 1, 99, 1, &ok), 7);
        QVERIFY(!ok);
    }
    void popupSelectionEscapeAndTeardown() {
        for (int repeat = 0; repeat < 12; ++repeat) {
            auto host = std::make_unique<QWidget>();
            auto *layout = new QFormLayout(host.get());
            auto *choice = Ui::comboBox(host.get());
            choice->addItems({"Saved", "History", "Trash"});
            layout->addRow(choice);
            layout->addRow(Ui::lineEdit(host.get()));
            layout->addRow(Ui::checkBox("Pinned", host.get()));
            layout->addRow(Ui::spinBox(host.get()));
            layout->addRow(Ui::toolButton(host.get()));
            host->show();
            choice->showPopup();
            QTRY_VERIFY(choice->view()->isVisible());
            QTest::keyClick(choice->view(), Qt::Key_Down);
            QTest::keyClick(choice->view(), Qt::Key_Return);
            QCOMPARE(choice->currentIndex(), 1);
            QVERIFY(!choice->view()->isVisible());
            choice->showPopup();
            QTest::keyClick(choice->view(), Qt::Key_Escape);
            QVERIFY(!choice->view()->isVisible());
            auto *popup = Ui::menu(host.get());
            QVERIFY(popup->inherits("ElaMenu"));
            popup->addAction("Inspect");
            popup->popup(QPoint(10, 10));
            QTest::keyClick(popup, Qt::Key_Escape);
            QVERIFY(!popup->isVisible());
            choice->showPopup();
            applyPinloomVisualTheme(*qApp, repeat % 2 ? PinloomVisualScheme::Dark : PinloomVisualScheme::Light);
            // Also destroys a visible popup and all local styles.
            host.reset();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(!QApplication::activePopupWidget());
        }
        applyPinloomVisualTheme(*qApp, scheme_);
    }
    void captureDialogPreviewsAndStableGeometry() {
        AnchorCaptureDraft draft;
        draft.targetApp = QStringLiteral("PDF");
        draft.targetUri = QStringLiteral("isolated-fixture.pdf");
        draft.locatorType = QStringLiteral("pdf.page");
        draft.locatorJson = QStringLiteral("{\"page\":3}");
        draft.suggestedName = QStringLiteral("Review anchor");
        draft.provenance = QStringLiteral("isolated UI fixture");
        AnchorCaptureDialog anchor(draft);
        anchor.show();
        QCoreApplication::processEvents();
        auto *name = anchor.findChild<QLineEdit *>("anchorCaptureNameEdit");
        QVERIFY(name && name->inherits("ElaLineEdit"));
        const auto originalGeometry = anchor.geometry();
        for (auto mode : {PinloomVisualScheme::Light, PinloomVisualScheme::Dark, scheme_}) {
            applyPinloomVisualTheme(*qApp, mode);
            QCoreApplication::processEvents();
            QCOMPARE(anchor.geometry(), originalGeometry);
            QCOMPARE(anchor.findChild<QLineEdit *>("anchorCaptureNameEdit"), name);
        }
        snapshot(anchor, "anchor-capture");
        anchor.close();

        ClipCaptureDialog clip(QStringLiteral("Exact selected text\nSecond line"),
                               QStringLiteral("Saved selection"), {QStringLiteral("review")});
        clip.show();
        QCoreApplication::processEvents();
        QVERIFY(clip.findChild<QLineEdit *>("clipCaptureNameEdit")->inherits("ElaLineEdit"));
        QVERIFY(clip.findChild<QPlainTextEdit *>("clipCapturePreview")->inherits("ElaPlainTextEdit"));
        QCOMPARE(clip.findChild<QPlainTextEdit *>("clipCapturePreview")->toPlainText(),
                 QStringLiteral("Exact selected text\nSecond line"));
        snapshot(clip, "clip-capture");
        clip.findChild<QToolButton *>("clipCaptureTagsButton")->click();
        auto *picker = clip.findChild<QFrame *>("clipCaptureTagPicker");
        QVERIFY(picker);
        QVERIFY(picker->property("pinloomPopupSurface").toBool());
        QVERIFY(picker->testAttribute(Qt::WA_TranslucentBackground));
        auto *tags = picker->findChild<Ui::List *>("clipCaptureTagList");
        QVERIFY(tags && tags->inherits("ElaListView"));
        QVERIFY(qobject_cast<QStandardItemModel *>(tags->model()));
        tags->setCurrentRow(0);
        tags->setFocus();
        QTest::keyClick(tags, Qt::Key_Space);
        QVERIFY(clip.metadata().tags.contains(QStringLiteral("review")));
        const auto renderedTags = tags->viewport()->grab().toImage();
        const auto tagRect = tags->visualItemRect(tags->item(0));
        const qreal ratio = renderedTags.devicePixelRatio();
        const auto background = tags->item(0)->background().color();
        int readableTextPixels = 0;
        for (int y = (tagRect.top() + 4) * ratio; y < (tagRect.bottom() - 4) * ratio; ++y)
            for (int x = (tagRect.left() + 26) * ratio; x < (tagRect.left() + 60) * ratio; ++x)
                if (pinloomContrastRatio(renderedTags.pixelColor(x, y), background) >= 4.5) ++readableTextPixels;
        QVERIFY2(readableTextPixels > 5, "Tag text must contrast with its retained tag-color background in both themes");
        snapshot(*picker, "tag-picker");
        picker->close();
    }
    void extendedControlGeometryAndTeardown() {
        for (int repeat = 0; repeat < 8; ++repeat) {
            auto main = std::make_unique<PinloomMainWindow>();
            auto *edit = Ui::plainTextEdit(QStringLiteral("Complete preview\n").repeated(300), main.get());
            edit->setAccessibleName(QStringLiteral("Complete preview"));
            edit->setReadOnly(true);
            main->setCentralWidget(edit);
            main->resize(460, 300);
            auto *bar = main->menuBar();
            auto *status = main->statusBar();
            QVERIFY(bar->inherits("ElaMenuBar"));
            QVERIFY(status->inherits("ElaStatusBar"));
            auto font = qApp->font();
            font.setPointSize(9 + repeat * 2);
            bar->setFont(font);
            status->setFont(font);
            main->show();
            main->activateWindow();
            edit->setFocus();
            QCoreApplication::processEvents();
            QVERIFY(edit->hasFocus());
            const auto originalSize = main->size();
            const auto originalBarSize = bar->size();
            const auto originalStatusSize = status->size();
            for (auto mode : {PinloomVisualScheme::Light, PinloomVisualScheme::Dark, scheme_}) {
                applyPinloomVisualTheme(*qApp, mode);
                QCoreApplication::processEvents();
                QCOMPARE(main->size(), originalSize);
                QCOMPARE(bar->size(), originalBarSize);
                QCOMPARE(status->size(), originalStatusSize);
                QCOMPARE(edit->palette().color(QPalette::Text), pinloomVisualTokens(mode).text);
                QVERIFY(bar->height() >= bar->fontMetrics().height());
                QVERIFY(status->height() >= status->fontMetrics().height());
                QVERIFY(main->rect().contains(bar->geometry()));
                QVERIFY(main->rect().contains(status->geometry()));
            }
            QVERIFY(edit->verticalScrollBar()->inherits("ElaScrollBar"));
            QVERIFY(edit->verticalScrollBar()->maximum() > 0);
            QVERIFY(edit->verticalScrollBar()->isVisible());
            if (repeat == 0) snapshot(*main, "extended-controls");
            edit->selectAll();
            QContextMenuEvent context(QContextMenuEvent::Keyboard, QPoint(12, 12), edit->mapToGlobal(QPoint(12, 12)));
            QApplication::sendEvent(edit, &context);
            auto *popup = QApplication::activePopupWidget();
            QVERIFY(popup && popup->inherits("ElaMenu"));
            // Destroy the editor's standard actions, Ela popup, bars and styles together.
            main.reset();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(!QApplication::activePopupWidget());
        }
    }
    void realSurfacesAndNavigation() {
        QVERIFY(QRawFont::fromFont(QFont(QStringLiteral("Font Awesome 6 Free"), 12)).supportsCharacter(0xf002));
        PinloomCommandPanel command;
        command.setCommandText("clip ");
        command.show();
        auto *navigation = command.findChild<QToolButton *>("commandNavigationButton");
        QVERIFY(navigation && navigation->inherits("ElaToolButton"));
        navigation->click();
        auto *navigationPopup = QApplication::activePopupWidget();
        QVERIFY(navigationPopup);
        QVERIFY(navigationPopup->findChild<QWidget *>("pinloomNavigationBar")->inherits("ElaNavigationBar"));
        auto *destinations = navigationPopup->findChild<QTreeView *>();
        QVERIFY(destinations && destinations->model()->rowCount() == 6);
        QCOMPARE(destinations->model()->index(1, 0).data().toString(), QStringLiteral("Anchor"));
        snapshot(*navigationPopup, "navigation");
        QTest::keyClick(destinations, Qt::Key_Down);
        QTest::keyClick(destinations, Qt::Key_Return);
        QCOMPARE(command.commandText(), QStringLiteral("anchor "));
        QCOMPARE(command.theme(), PinloomCommandTheme::Anchor);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        navigation->click();
        auto *cancelPopup = QApplication::activePopupWidget();
        QVERIFY(cancelPopup);
        QTest::keyClick(cancelPopup->findChild<QTreeView *>(), Qt::Key_Escape);
        QCOMPARE(command.commandText(), QStringLiteral("anchor "));
        QVERIFY(!QApplication::activePopupWidget());
        const QStringList routes = {QString(), "anchor ", "clip ", "inbox ", "library ", "root "};
        for (int row = 0; row < routes.size(); ++row) {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            navigation->click();
            auto *popup = QApplication::activePopupWidget();
            QVERIFY(popup);
            auto *view = popup->findChild<QTreeView *>();
            view->setCurrentIndex(view->model()->index(row, 0));
            QTest::keyClick(view, Qt::Key_Return);
            QCOMPARE(command.commandText(), routes.at(row));
        }
        command.setCommandText(QStringLiteral("anchor "));
        snapshot(command, "command");

        Clip clip;
        clip.id = "fixture";
        clip.state = ClipState::Saved;
        clip.name = "Isolated preview clip";
        clip.text = QStringLiteral("Full content stays in the preview.\n").repeated(80);
        clip.pinned = true;
        clip.tags = {"preview"};
        ClipLibraryWindowOptions options;
        options.clipsProvider = [clip] { return QList<Clip>{clip}; };
        ClipLibraryWindow library(options);
        library.setScope(ClipLibraryScope::Saved);
        library.resize(880, 520);
        library.show();
        QVERIFY(library.selectClipAt(0));
        auto *preview = library.findChild<QPlainTextEdit *>();
        QVERIFY(preview);
        QVERIFY(preview->inherits("ElaPlainTextEdit"));
        QCOMPARE(preview->toPlainText(), clip.text);
        QCOMPARE(preview->lineWrapMode(), QPlainTextEdit::WidgetWidth);
        QTRY_COMPARE(preview->horizontalScrollBar()->maximum(), 0);
        auto *table = library.findChild<Pinloom::Ui::Table *>("clipLibraryTable");
        QVERIFY(table && table->inherits("ElaTableView"));
        QVERIFY(qobject_cast<QStandardItemModel *>(table->model()));
        for (int column = 0; column < table->columnCount(); ++column)
            QVERIFY(table->horizontalHeaderItem(column)->text() != QStringLiteral("Content"));
        snapshot(library, "clips");

        PinloomSettingsDialog settings(PinloomAppSettings{});
        auto *path = settings.findChild<QLineEdit *>("sumatraPdfPathEdit");
        QVERIFY(path && path->inherits("ElaLineEdit"));
        settings.resize(640, 440);
        settings.show();
        QCoreApplication::processEvents();
        auto *settingsButtons = settings.findChild<Ui::DialogButtonBox *>("settingsButtons");
        QVERIFY(settingsButtons);
        QVERIFY(settings.rect().contains(settingsButtons->geometry()));
        auto *settingsScroll = settings.findChild<QScrollArea *>("settingsScrollArea");
        QVERIFY(settingsScroll && settingsScroll->widgetResizable());
        QVERIFY(settingsScroll->inherits("ElaScrollArea"));
        QVERIFY(settingsScroll->verticalScrollBar()->inherits("ElaScrollBar"));
        settingsScroll->ensureWidgetVisible(path);
        QVERIFY(settingsScroll->viewport()->rect().intersects(QRect(path->mapTo(settingsScroll->viewport(), QPoint()), path->size())));
        snapshot(settings, "settings");
        for (auto *label : settings.findChildren<QLabel *>())
            if (label->wordWrap()) QVERIFY(label->height() >= label->heightForWidth(label->width()));
        for (auto *spin : settings.findChildren<QSpinBox *>()) {
            const auto *editor = spin->findChild<QLineEdit *>();
            QVERIFY(editor);
            QVERIFY(editor->width() >= editor->fontMetrics().horizontalAdvance(spin->text()) + 6);
        }
        settingsScroll->ensureWidgetVisible(settings.findChild<QCheckBox *>("clipRestoreOriginalClipboardCheck"));
        QCoreApplication::processEvents();
        snapshot(settings, "settings-clipboard");
        auto *advancedToggle = settings.findChild<QPushButton *>("settingsAdvancedToggle");
        QVERIFY(advancedToggle);
        advancedToggle->click();
        QCoreApplication::processEvents();
        settingsScroll->verticalScrollBar()->setValue(settingsScroll->verticalScrollBar()->maximum());
        QCoreApplication::processEvents();
        auto *advancedContent = settings.findChild<QWidget *>("settingsAdvancedContent");
        QVERIFY(advancedContent && advancedContent->isVisible());
        QVERIFY(settingsScroll->viewport()->rect().intersects(
            QRect(advancedContent->mapTo(settingsScroll->viewport(), QPoint()), advancedContent->size())));
        snapshot(settings, "settings-advanced");

        AnchorLibraryWindow anchors({});
        QVERIFY(anchors.findChild<QScrollArea *>("anchorLibraryInspectorScroll")->inherits("ElaScrollArea"));
        LibraryRootWindow roots({});
        ManualPdfAnchorDialog manual;
        manual.show();
        manual.findChild<QPushButton *>("manualPdfAnchorAdvancedToggleButton")->click();
        QCoreApplication::processEvents();
        auto *manualSection = manual.findChild<QWidget *>("manualPdfAnchorAdvancedSection");
        auto *zoom = manual.findChild<QDoubleSpinBox *>("manualPdfAnchorZoomSpin");
        auto *manualScroll = manual.findChild<QScrollArea *>("manualPdfAnchorScrollArea");
        QVERIFY(manualSection && zoom && manualScroll);
        QTRY_VERIFY(manualSection->rect().contains(QRect(zoom->mapTo(manualSection, QPoint()), zoom->size())));
        manualScroll->ensureWidgetVisible(zoom);
        QCoreApplication::processEvents();
        QVERIFY(manualScroll->viewport()->rect().contains(QRect(zoom->mapTo(manualScroll->viewport(), QPoint()), zoom->size())));
        snapshot(manual, "manual-advanced");
        manual.close();
        for (auto *surface : {static_cast<QWidget *>(&anchors), static_cast<QWidget *>(&roots), static_cast<QWidget *>(&manual)}) {
            QVERIFY(!surface->findChildren<QLineEdit *>().isEmpty());
            for (auto *input : surface->findChildren<QLineEdit *>()) {
                // Spin/combo embedded editors are owned by Ela's parent control.
                if (!qobject_cast<QAbstractSpinBox *>(input->parentWidget()))
                    QVERIFY(input->inherits("ElaLineEdit"));
            }
        }
        anchors.resize(1100, 620);
        anchors.show();
        snapshot(anchors, "anchors");
        roots.resize(950, 570);
        roots.show();
        auto *fileTree = roots.findChild<QTreeView *>();
        QVERIFY(fileTree && fileTree->inherits("ElaTreeView"));
        QVERIFY(!fileTree->isAnimated());
        snapshot(roots, "roots");
        QVERIFY(!roots.findChild<QWidget *>(QStringLiteral("pinloomNoticeBar")));
        auto *notice = Ui::showNotice(&roots, QStringLiteral("Metadata saved. Existing files and IDs are unchanged."));
        QVERIFY(notice && notice->inherits("ElaMessageBar"));
        QCoreApplication::processEvents();
        QVERIFY(roots.rect().contains(notice->geometry()));
        snapshot(roots, "notification");

        PinloomMainWindow main;
        QSignalSpy hidden(&main, &PinloomMainWindow::hiddenToTray);
        main.show();
        main.close();
        QVERIFY(!main.isVisible());
        QCOMPARE(hidden.size(), 1);
        QtSystemTrayIconBackend tray;
        tray.setActions({{"pause", "Pause", true, false, true}});
        // Never show a native tray icon in an isolated preview.
        QSignalSpy trayAction(&tray, &ClipTrayBackend::actionTriggered);
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->objectName() == "clipTrayMenu") {
                QVERIFY(widget->inherits("ElaMenu"));
                qobject_cast<QMenu *>(widget)->actions().first()->trigger();
            }
        QCOMPARE(trayAction.size(), 1);
        QCOMPARE(trayAction.first().first().toString(), QStringLiteral("pause"));
    }
    void navigationParentTeardownWithVisiblePopup() {
        for (int repeat = 0; repeat < 8; ++repeat) {
            auto command = std::make_unique<PinloomCommandPanel>();
            command->show();
            command->findChild<QToolButton *>("commandNavigationButton")->click();
            QVERIFY(QApplication::activePopupWidget());
            applyPinloomVisualTheme(*qApp, repeat % 2 ? PinloomVisualScheme::Dark : PinloomVisualScheme::Light);
            command.reset();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(!QApplication::activePopupWidget());
        }
        applyPinloomVisualTheme(*qApp, scheme_);
    }
};

QTEST_MAIN(ElaControlsTest)
#include "ela_controls_test.moc"
