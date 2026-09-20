#include "pinloom/widgets/AnchorCaptureDialog.h"
#include "pinloom/widgets/AnchorLocatorPreviewWidget.h"
#include "pinloom/widgets/ClipCaptureDialog.h"
#include "pinloom/widgets/PinloomVisualTheme.h"
#ifdef PINLOOM_ENABLE_SUITEUI
#include "PinloomSuiteUi.h"
#endif

#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QProxyStyle>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStyleFactory>
#include <QStyleOptionButton>
#include <QToolButton>
#include <QtTest>

using namespace Pinloom;

class Contracts final : public QObject {
    Q_OBJECT
private slots:
    void actualControlsAndBoundaries()
    {
        const QString themeName = qEnvironmentVariable("PINLOOM_TEST_THEME", "light");
        QVERIFY2(themeName == "light" || themeName == "dark", "Unsupported test theme");
        const auto theme = themeName == "dark" ? PinloomVisualScheme::Dark : PinloomVisualScheme::Light;
        const auto originalFont = qApp->font();
        const auto originalStyle = qApp->style()->objectName();
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
        QVERIFY(!qApp->property("pinloomControlBackend").isValid());
#endif
        QCOMPARE(qApp->font(), originalFont);
        const QString renderer = backend ? backend->objectName() : QStringLiteral("classic");
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
        auto *buttons = dialog->findChild<QDialogButtonBox *>("anchorCaptureButtons");
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
