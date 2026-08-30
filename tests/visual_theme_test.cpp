#include "pinloom/widgets/AnchorLibraryWindow.h"
#include "pinloom/widgets/ClipLibraryWindow.h"
#include "pinloom/widgets/LibraryRootWindow.h"
#include "pinloom/widgets/ManualPdfAnchorDialog.h"
#include "pinloom/widgets/PinloomCommandPanel.h"
#include "pinloom/widgets/PinloomSettingsDialog.h"
#include "pinloom/widgets/PinloomVisualTheme.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QTableWidget>
#include <QtTest>

using namespace Pinloom;

namespace {

class ApplicationThemeGuard final {
public:
    explicit ApplicationThemeGuard(QApplication &application)
        : application_(application)
        , palette_(application.palette())
        , styleSheet_(application.styleSheet())
        , scheme_(application.property("pinloomVisualScheme"))
        , reducedMotion_(application.property("pinloomReducedMotion"))
        , baseSpacing_(application.property("pinloomBaseSpacing"))
    {
    }

    ~ApplicationThemeGuard()
    {
        application_.setPalette(palette_);
        application_.setStyleSheet(styleSheet_);
        application_.setProperty("pinloomVisualScheme", scheme_);
        application_.setProperty("pinloomReducedMotion", reducedMotion_);
        application_.setProperty("pinloomBaseSpacing", baseSpacing_);
    }

private:
    QApplication &application_;
    QPalette palette_;
    QString styleSheet_;
    QVariant scheme_;
    QVariant reducedMotion_;
    QVariant baseSpacing_;
};

class EnvironmentGuard final {
public:
    explicit EnvironmentGuard(const char *name)
        : name_(name)
        , wasSet_(qEnvironmentVariableIsSet(name))
        , value_(qgetenv(name))
    {
    }

    ~EnvironmentGuard()
    {
        if (wasSet_) qputenv(name_, value_);
        else qunsetenv(name_);
    }

private:
    const char *name_;
    bool wasSet_ = false;
    QByteArray value_;
};

} // namespace

class VisualThemeTest final : public QObject {
    Q_OBJECT

private slots:
    void semanticTokensMeetContrastRequirements();
    void metricsScaleFrom100To200Percent();
    void styleSheetCoversSemanticAndInteractionStates();
    void applicationThemePublishesPaletteContract();
    void reducedMotionHonorsExplicitOverride();
    void commandNamespacesKeepDistinctAccessibleAccents();
    void majorSurfacesExposeRolesAndAccessibleFocusTargets();
    void themeSwitchPreservesLogicalGeometry();
};

void VisualThemeTest::semanticTokensMeetContrastRequirements()
{
    for (const PinloomVisualScheme scheme : {PinloomVisualScheme::Light,
                                              PinloomVisualScheme::Dark}) {
        const PinloomVisualTokens tokens = pinloomVisualTokens(scheme);
        QString error;
        QVERIFY2(tokens.isValid(&error), qPrintable(error));
        QVERIFY(pinloomContrastRatio(tokens.text, tokens.canvas) >= 4.5);
        QVERIFY(pinloomContrastRatio(tokens.text, tokens.panel) >= 4.5);
        QVERIFY(pinloomContrastRatio(tokens.mutedText, tokens.panel) >= 4.5);
        QVERIFY(pinloomContrastRatio(tokens.selectionText, tokens.selection) >= 4.5);
        QVERIFY(pinloomContrastRatio(tokens.focus, tokens.canvas) >= 3.0);
        QVERIFY(tokens.canvas != tokens.panel);
        QVERIFY(tokens.text != tokens.mutedText);
        QVERIFY(tokens.success != tokens.warning);
        QVERIFY(tokens.warning != tokens.error);
    }
}

void VisualThemeTest::metricsScaleFrom100To200Percent()
{
    const PinloomVisualMetrics base = pinloomVisualMetrics(100);
    QCOMPARE(base.scalePercent, 100);
    QCOMPARE(base.baseSpacing, 4);
    QCOMPARE(base.compactControlHeight, 28);
    QCOMPARE(base.regularControlHeight, 32);
    QCOMPARE(base.primaryControlHeight, 36);
    QCOMPARE(base.smallRadius, 6);
    QCOMPARE(base.largeRadius, 8);

    const PinloomVisualMetrics medium = pinloomVisualMetrics(150);
    QCOMPARE(medium.baseSpacing, 6);
    QCOMPARE(medium.compactControlHeight, 42);
    QCOMPARE(medium.regularControlHeight, 48);
    QCOMPARE(medium.primaryControlHeight, 54);
    QCOMPARE(medium.smallRadius, 9);
    QCOMPARE(medium.largeRadius, 12);

    const PinloomVisualMetrics large = pinloomVisualMetrics(200);
    QCOMPARE(large.baseSpacing, 8);
    QCOMPARE(large.compactControlHeight, 56);
    QCOMPARE(large.regularControlHeight, 64);
    QCOMPARE(large.primaryControlHeight, 72);
    QCOMPARE(large.smallRadius, 12);
    QCOMPARE(large.largeRadius, 16);
    QCOMPARE(pinloomVisualMetrics(75).scalePercent, 100);
    QCOMPARE(pinloomVisualMetrics(250).scalePercent, 200);
}

void VisualThemeTest::styleSheetCoversSemanticAndInteractionStates()
{
    for (const PinloomVisualScheme scheme : {PinloomVisualScheme::Light,
                                              PinloomVisualScheme::Dark}) {
        const QString styleSheet = pinloomVisualThemeStyleSheet(scheme);
        for (const QString &fragment : {
                 QStringLiteral("pinloomRole=\"canvas\""),
                 QStringLiteral("pinloomRole=\"panel\""),
                 QStringLiteral("pinloomTextRole=\"title\""),
                 QStringLiteral("pinloomTextRole=\"metadata\""),
                 QStringLiteral("pinloomTextRole=\"technical\""),
                 QStringLiteral("pinloomState=\"loading\""),
                 QStringLiteral("pinloomState=\"stale\""),
                 QStringLiteral("pinloomState=\"empty\""),
                 QStringLiteral("pinloomState=\"warning\""),
                 QStringLiteral("pinloomState=\"error\""),
                 QStringLiteral("pinloomState=\"success\""),
                 QStringLiteral(":hover"),
                 QStringLiteral(":pressed"),
                 QStringLiteral(":checked"),
                 QStringLiteral(":disabled"),
                 QStringLiteral(":focus"),
                 QStringLiteral("::item:selected"),
                 QStringLiteral("trashMode=\"true\"")}) {
            QVERIFY2(styleSheet.contains(fragment), qPrintable(fragment));
        }
        QVERIFY(!styleSheet.contains(QStringLiteral("gradient"),
                                     Qt::CaseInsensitive));
    }
}

void VisualThemeTest::applicationThemePublishesPaletteContract()
{
    ApplicationThemeGuard guard(*qApp);
    applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Dark);
    const PinloomVisualTokens dark = pinloomVisualTokens(PinloomVisualScheme::Dark);
    QCOMPARE(qApp->palette().color(QPalette::Window), dark.canvas);
    QCOMPARE(qApp->palette().color(QPalette::Base), dark.panel);
    QCOMPARE(qApp->palette().color(QPalette::Text), dark.text);
    QCOMPARE(qApp->palette().color(QPalette::Highlight), dark.selection);
    QCOMPARE(qApp->property("pinloomVisualScheme").toString(),
             QStringLiteral("dark"));
    QCOMPARE(qApp->property("pinloomBaseSpacing").toInt(), 4);
    QVERIFY(qApp->property("pinloomReducedMotion").isValid());

    applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Light);
    const PinloomVisualTokens light = pinloomVisualTokens(PinloomVisualScheme::Light);
    QCOMPARE(qApp->palette().color(QPalette::Window), light.canvas);
    QCOMPARE(qApp->property("pinloomVisualScheme").toString(),
             QStringLiteral("light"));
}

void VisualThemeTest::reducedMotionHonorsExplicitOverride()
{
    EnvironmentGuard guard("PINLOOM_REDUCED_MOTION");
    QVERIFY(qputenv("PINLOOM_REDUCED_MOTION", "true"));
    QVERIFY(pinloomReducedMotionEnabled());
    QVERIFY(qputenv("PINLOOM_REDUCED_MOTION", "0"));
    QVERIFY(!pinloomReducedMotionEnabled());
}

void VisualThemeTest::commandNamespacesKeepDistinctAccessibleAccents()
{
    ApplicationThemeGuard guard(*qApp);
    applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Light);
    PinloomCommandPanel panel;

    panel.setCommandText(QStringLiteral("k"));
    QCOMPARE(panel.theme(), PinloomCommandTheme::Anchor);
    const QString anchorStyle = panel.styleSheet();
    QVERIFY(anchorStyle.contains(QStringLiteral("#2563eb"), Qt::CaseInsensitive));

    panel.setCommandText(QStringLiteral("c"));
    QCOMPARE(panel.theme(), PinloomCommandTheme::Clip);
    const QString clipStyle = panel.styleSheet();
    QVERIFY(clipStyle.contains(QStringLiteral("#0f766e"), Qt::CaseInsensitive));

    panel.setCommandText(QStringLiteral("i"));
    QCOMPARE(panel.theme(), PinloomCommandTheme::Inbox);
    const QString inboxStyle = panel.styleSheet();
    QVERIFY(inboxStyle.contains(QStringLiteral("#b45309"), Qt::CaseInsensitive));
    QVERIFY(anchorStyle != clipStyle);
    QVERIFY(clipStyle != inboxStyle);
    QVERIFY(anchorStyle != inboxStyle);
}

void VisualThemeTest::majorSurfacesExposeRolesAndAccessibleFocusTargets()
{
    ApplicationThemeGuard guard(*qApp);
    applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Light);

    PinloomCommandPanel commandPanel;
    ClipLibraryWindow clipLibrary;
    AnchorLibraryWindow anchorLibrary({});
    LibraryRootWindow rootLibrary({});
    ManualPdfAnchorDialog pdfDialog;
    PinloomSettingsDialog settingsDialog(PinloomAppSettings{});

    for (QWidget *surface : {static_cast<QWidget *>(&commandPanel),
                             static_cast<QWidget *>(&clipLibrary),
                             static_cast<QWidget *>(&anchorLibrary),
                             static_cast<QWidget *>(&rootLibrary),
                             static_cast<QWidget *>(&pdfDialog),
                             static_cast<QWidget *>(&settingsDialog)}) {
        QCOMPARE(surface->property("pinloomRole").toString(),
                 QStringLiteral("canvas"));
    }

    auto *commandEdit = commandPanel.findChild<QLineEdit *>(
        QStringLiteral("commandSearchEdit"));
    auto *commandResults = commandPanel.findChild<QListWidget *>(
        QStringLiteral("commandResultList"));
    auto *clipTable = clipLibrary.findChild<QTableWidget *>(
        QStringLiteral("clipLibraryTable"));
    auto *anchorTable = anchorLibrary.findChild<QTableWidget *>(
        QStringLiteral("anchorLibraryAnchorTable"));
    auto *rootTable = rootLibrary.findChild<QTableWidget *>(
        QStringLiteral("libraryRootTable"));
    QVERIFY(commandEdit);
    QVERIFY(commandResults);
    QVERIFY(clipTable);
    QVERIFY(anchorTable);
    QVERIFY(rootTable);
    QVERIFY(commandEdit->focusPolicy() != Qt::NoFocus);
    QVERIFY(commandResults->focusPolicy() != Qt::NoFocus);
    QVERIFY(!commandEdit->accessibleName().isEmpty());
    QVERIFY(!commandResults->accessibleName().isEmpty());
    QVERIFY(!clipTable->accessibleName().isEmpty());
    QVERIFY(!anchorTable->accessibleName().isEmpty());
    QVERIFY(!rootTable->accessibleName().isEmpty());

    auto *jumpButton = rootLibrary.findChild<QPushButton *>(
        QStringLiteral("libraryRootSaveButton"));
    QVERIFY(jumpButton);
    QCOMPARE(jumpButton->property("pinloomControl").toString(),
             QStringLiteral("primary"));
    auto *settingsButtons = settingsDialog.findChild<QDialogButtonBox *>(
        QStringLiteral("settingsButtons"));
    QVERIFY(settingsButtons);
    QCOMPARE(settingsButtons->button(QDialogButtonBox::Ok)
                 ->property("pinloomControl").toString(),
             QStringLiteral("primary"));
}

void VisualThemeTest::themeSwitchPreservesLogicalGeometry()
{
    ApplicationThemeGuard guard(*qApp);
    ClipLibraryWindow window;
    window.setGeometry(20, 24, 640, 500);
    const QRect original = window.geometry();

    applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Light);
    QApplication::processEvents();
    QCOMPARE(window.geometry(), original);

    applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Dark);
    QApplication::processEvents();
    QCOMPARE(window.geometry(), original);
}

QTEST_MAIN(VisualThemeTest)

#include "visual_theme_test.moc"
