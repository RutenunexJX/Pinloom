#include "pinloom/widgets/PinloomVisualTheme.h"
#include "pinloom/widgets/PinloomUiControls.h"
#ifdef PINLOOM_ENABLE_SUITEUI
#include "PinloomSuiteUi.h"
#endif

#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Pinloom {

namespace {

QString cssColor(const QColor &color)
{
    return color.name(QColor::HexRgb).toUpper();
}

QColor mixedColor(const QColor &foreground,
                  const QColor &background,
                  double foregroundWeight)
{
    const double weight = std::clamp(foregroundWeight, 0.0, 1.0);
    return QColor(qRound(foreground.red() * weight
                         + background.red() * (1.0 - weight)),
                  qRound(foreground.green() * weight
                         + background.green() * (1.0 - weight)),
                  qRound(foreground.blue() * weight
                         + background.blue() * (1.0 - weight)));
}

double linearChannel(int channel)
{
    const double value = static_cast<double>(channel) / 255.0;
    return value <= 0.04045
        ? value / 12.92
        : std::pow((value + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor &color)
{
    return 0.2126 * linearChannel(color.red())
        + 0.7152 * linearChannel(color.green())
        + 0.0722 * linearChannel(color.blue());
}

int scaledMetric(int logicalPixels, int scalePercent)
{
    return qRound(static_cast<double>(logicalPixels)
                  * static_cast<double>(scalePercent) / 100.0);
}

} // namespace

PinloomVisualScheme systemPinloomVisualScheme()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
        ? PinloomVisualScheme::Dark
        : PinloomVisualScheme::Light;
}

PinloomVisualScheme activePinloomVisualScheme()
{
    if (const auto *application = qobject_cast<QApplication *>(
            QCoreApplication::instance())) {
        const QString applied = application->property(
            "pinloomVisualScheme").toString();
        if (applied == QLatin1String("dark")) return PinloomVisualScheme::Dark;
        if (applied == QLatin1String("light")) return PinloomVisualScheme::Light;
    }
    return systemPinloomVisualScheme();
}

PinloomVisualTokens pinloomVisualTokens(PinloomVisualScheme scheme)
{
    if (scheme == PinloomVisualScheme::Dark) {
        return {
            QColor(QStringLiteral("#0B1120")),
            QColor(QStringLiteral("#111827")),
            QColor(QStringLiteral("#172033")),
            QColor(QStringLiteral("#1D2939")),
            QColor(QStringLiteral("#3E4B5D")),
            QColor(QStringLiteral("#F1F5F9")),
            QColor(QStringLiteral("#B8C4D2")),
            QColor(QStringLiteral("#2DD4BF")),
            QColor(QStringLiteral("#0F766E")),
            QColor(QStringLiteral("#FFFFFF")),
            QColor(QStringLiteral("#60A5FA")),
            QColor(QStringLiteral("#2DD4BF")),
            QColor(QStringLiteral("#FBBF24")),
            QColor(QStringLiteral("#F87171")),
            QColor(QStringLiteral("#223047")),
            QColor(QStringLiteral("#293A52")),
            QColor(QStringLiteral("#1B2432")),
            QColor(QStringLiteral("#8290A3")),
        };
    }

    return {
        QColor(QStringLiteral("#F4F7FB")),
        QColor(QStringLiteral("#FFFFFF")),
        QColor(QStringLiteral("#E8EEF5")),
        QColor(QStringLiteral("#F7F9FC")),
        QColor(QStringLiteral("#B7C3D2")),
        QColor(QStringLiteral("#172033")),
        QColor(QStringLiteral("#4A5A70")),
        QColor(QStringLiteral("#0F766E")),
        QColor(QStringLiteral("#0F766E")),
        QColor(QStringLiteral("#FFFFFF")),
        QColor(QStringLiteral("#2563EB")),
        QColor(QStringLiteral("#087A55")),
        QColor(QStringLiteral("#8A4B00")),
        QColor(QStringLiteral("#B42318")),
        QColor(QStringLiteral("#EAF1F8")),
        QColor(QStringLiteral("#DCE7F1")),
        QColor(QStringLiteral("#EEF1F5")),
        QColor(QStringLiteral("#7B8798")),
    };
}

double pinloomContrastRatio(const QColor &foreground,
                            const QColor &background)
{
    if (!foreground.isValid() || !background.isValid()) return 0.0;
    const double first = relativeLuminance(foreground);
    const double second = relativeLuminance(background);
    const double lighter = std::max(first, second);
    const double darker = std::min(first, second);
    return (lighter + 0.05) / (darker + 0.05);
}

bool PinloomVisualTokens::isValid(QString *error) const
{
    if (error) error->clear();
    const std::array<std::pair<const char *, QColor>, 18> roles{{
        {"canvas", canvas}, {"panel", panel},
        {"raisedSurface", raisedSurface}, {"alternateSurface", alternateSurface},
        {"border", border}, {"text", text}, {"mutedText", mutedText},
        {"accent", accent}, {"selection", selection},
        {"selectionText", selectionText}, {"focus", focus},
        {"success", success}, {"warning", warning}, {"error", this->error},
        {"hoverSurface", hoverSurface}, {"pressedSurface", pressedSurface},
        {"disabledSurface", disabledSurface}, {"disabledText", disabledText},
    }};
    for (const auto &role : roles) {
        if (!role.second.isValid()) {
            if (error) {
                *error = QStringLiteral("Invalid visual token: %1")
                             .arg(QString::fromLatin1(role.first));
            }
            return false;
        }
    }
    const std::array<std::tuple<const char *, QColor, QColor, double>, 8>
        contrastChecks{{
            {"text/canvas", text, canvas, 4.5},
            {"text/panel", text, panel, 4.5},
            {"mutedText/panel", mutedText, panel, 4.5},
            {"selectionText/selection", selectionText, selection, 4.5},
            {"focus/canvas", focus, canvas, 3.0},
            {"success/panel", success, panel, 4.5},
            {"warning/panel", warning, panel, 4.5},
            {"error/panel", this->error, panel, 4.5},
        }};
    for (const auto &check : contrastChecks) {
        if (pinloomContrastRatio(std::get<1>(check), std::get<2>(check))
            + 0.001 < std::get<3>(check)) {
            if (error) {
                *error = QStringLiteral("Insufficient contrast for %1")
                             .arg(QString::fromLatin1(std::get<0>(check)));
            }
            return false;
        }
    }
    return true;
}

PinloomVisualMetrics pinloomVisualMetrics(int scalePercent)
{
    PinloomVisualMetrics metrics;
    metrics.scalePercent = std::clamp(scalePercent, 100, 200);
    metrics.baseSpacing = scaledMetric(4, metrics.scalePercent);
    metrics.compactControlHeight = scaledMetric(28, metrics.scalePercent);
    metrics.regularControlHeight = scaledMetric(32, metrics.scalePercent);
    metrics.primaryControlHeight = scaledMetric(36, metrics.scalePercent);
    metrics.smallRadius = scaledMetric(6, metrics.scalePercent);
    metrics.largeRadius = scaledMetric(8, metrics.scalePercent);
    metrics.panelHeaderPadding = scaledMetric(8, metrics.scalePercent);
    return metrics;
}

bool pinloomReducedMotionEnabled()
{
    const QString override = qEnvironmentVariable(
        "PINLOOM_REDUCED_MOTION").trimmed().toCaseFolded();
    if (override == QLatin1String("1")
        || override == QLatin1String("true")
        || override == QLatin1String("yes")
        || override == QLatin1String("on")) {
        return true;
    }
    if (override == QLatin1String("0")
        || override == QLatin1String("false")
        || override == QLatin1String("no")
        || override == QLatin1String("off")) {
        return false;
    }
#ifdef Q_OS_WIN
    BOOL animationsEnabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,
                              0,
                              &animationsEnabled,
                              0)) {
        return animationsEnabled == FALSE;
    }
#endif
    return false;
}

static QString visualThemeStyleSheet(PinloomVisualScheme scheme, bool suiteUi)
{
    const PinloomVisualTokens tokens = pinloomVisualTokens(scheme);
    const PinloomVisualMetrics metrics = pinloomVisualMetrics();
    const QColor successSurface = mixedColor(tokens.success, tokens.panel, 0.12);
    const QColor warningSurface = mixedColor(tokens.warning, tokens.panel, 0.13);
    const QColor errorSurface = mixedColor(tokens.error, tokens.panel, 0.12);
    QString buttonRules = QStringLiteral(R"QSS(
QPushButton, QToolButton {
  min-height: %8px; background: %3; color: %2;
  border: 1px solid %5; border-radius: %9px; padding: 0 %16px;
}
QToolButton { min-height: %17px; }
QPushButton:hover, QToolButton:hover { background: %18; border-color: %7; }
QPushButton:pressed, QToolButton:pressed, QToolButton:checked {
  background: %19; border-color: %20;
}
QPushButton:focus, QToolButton:focus { border: 2px solid %13; }
QPushButton:disabled, QToolButton:disabled {
  color: %14; background: %15; border-color: %5;
}
QPushButton[pinloomControl="primary"] {
  min-height: %21px; background: %11; color: %12; border-color: %11;
  font-weight: 600;
}
QPushButton[pinloomControl="primary"]:hover { background: %20; }
QToolButton[pinloomControl="icon"] { min-width: %17px; padding: 0; }
)QSS");
    if (suiteUi) {
        buttonRules.replace(QStringLiteral("QPushButton"),
                            QStringLiteral("QPushButton[pinloomSuiteUiClassic=\"true\"]"));
        buttonRules.replace(QStringLiteral("QToolButton"),
                            QStringLiteral("QToolButton[pinloomSuiteUiClassic=\"true\"]"));
        buttonRules.prepend(QStringLiteral(R"QSS(
QPushButton { min-height: %8px; padding: 0 %16px; color: %2; }
QToolButton { min-height: %17px; padding: 0 %16px; color: %2; }
QPushButton[pinloomControl="primary"] { min-height: %21px; font-weight: 600; color: %12; }
QPushButton:disabled, QToolButton:disabled,
QPushButton[pinloomControl="primary"]:disabled { color: %14; }
QToolButton[pinloomControl="icon"] { min-width: %17px; padding: 0; }
)QSS"));
    }
    QString sheet = QStringLiteral(R"QSS(
QMainWindow, QDialog, QWidget[pinloomRole="canvas"] {
  background: %1; color: %2;
}
QWidget[pinloomRole="panel"], QFrame[pinloomRole="panel"],
QScrollArea[pinloomRole="panel"] { background: %3; color: %2; }
QWidget[pinloomRole="raised"], QFrame[pinloomRole="raised"] {
  background: %4; color: %2; border: 1px solid %5; border-radius: %6px;
}
QLabel { color: %2; }
QLabel[pinloomTextRole="title"] { font-size: 18px; font-weight: 600; }
QLabel[pinloomTextRole="panelTitle"] { font-size: 15px; font-weight: 600; }
QLabel[pinloomTextRole="metadata"] { color: %7; font-size: 12px; }
QLabel[pinloomTextRole="technical"], QLineEdit[pinloomTextRole="technical"],
QPlainTextEdit[pinloomTextRole="technical"] {
  font-family: "Cascadia Mono", "Consolas", monospace;
}
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox, QDoubleSpinBox {
  min-height: %8px; background: %3; color: %2;
  border: 1px solid %5; border-radius: %9px; padding: 2px %10px;
  selection-background-color: %11; selection-color: %12;
}
QLineEdit:hover, QPlainTextEdit:hover, QTextEdit:hover, QComboBox:hover,
QSpinBox:hover, QDoubleSpinBox:hover { border-color: %7; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus,
QSpinBox:focus, QDoubleSpinBox:focus, QAbstractItemView:focus {
  border: 2px solid %13;
}
QLineEdit:disabled, QPlainTextEdit:disabled, QTextEdit:disabled,
QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
  color: %14; background: %15; border-color: %5;
}
@BUTTONS@
QPushButton[accent="positive"], QToolButton[accent="positive"] {
  color: %22; border-color: %22;
}
QPushButton[accent="destructive"], QToolButton[accent="destructive"] {
  color: %23; border-color: %23;
}
QListView, QTreeView, QTableView, QListWidget, QTreeWidget, QTableWidget {
  background: %3; alternate-background-color: %24; color: %2;
  border: 1px solid %5; border-radius: %6px; outline: 0;
  selection-background-color: %11; selection-color: %12;
}
QAbstractItemView::item:hover { background: %18; }
QAbstractItemView::item:selected { background: %11; color: %12; }
QHeaderView::section {
  background: %4; color: %2; border: 0; border-right: 1px solid %5;
  border-bottom: 1px solid %5; padding: %25px %10px; font-weight: 600;
}
QGroupBox {
  border: 1px solid %5; border-radius: %26px; margin-top: %16px;
  padding-top: %10px;
}
QGroupBox::title { subcontrol-origin: margin; left: %10px; padding: 0 %27px; color: %28; }
QSplitter::handle { background: %5; }
QSplitter::handle:horizontal { width: 5px; margin: 0 1px; }
QSplitter::handle:vertical { height: 5px; margin: 1px 0; }
QStatusBar { background: %4; color: %7; border-top: 1px solid %5; }
QMenu { background: %3; color: %2; border: 1px solid %5; padding: %27px; }
QMenu::item { min-height: %17px; padding: 2px %16px; }
QMenu::item:selected { background: %11; color: %12; }
QToolTip { background: %2; color: %3; border: 1px solid %5; padding: %27px; }
QScrollBar:vertical { width: 10px; background: transparent; }
QScrollBar::handle:vertical { min-height: %17px; background: %7; border-radius: 5px; }
QLabel[pinloomNotice="info"] {
  background: %4; color: %2; border-left: 3px solid %28;
  border-radius: %27px; padding: %27px %25px;
}
QLabel[pinloomNotice="success"], QWidget[pinloomState="success"] {
  background: %29; color: %22; border-left: 3px solid %22;
}
QLabel[pinloomNotice="warning"], QWidget[pinloomState="warning"],
QWidget[pinloomState="stale"] {
  background: %30; color: %31; border-left: 3px solid %31;
}
QLabel[pinloomNotice="error"], QWidget[pinloomState="error"] {
  background: %32; color: %23; border-left: 3px solid %23;
}
QWidget[pinloomState="loading"] { color: %7; border-color: %28; }
QWidget[pinloomState="empty"] { color: %7; font-style: italic; }
QMainWindow#anchorLibraryWindow[trashMode="true"],
QMainWindow#anchorLibraryWindow[trashMode="true"] QWidget#anchorLibraryCentral,
QMainWindow#clipLibraryWindow[trashMode="true"] {
  background: %32; color: %2;
}
QMainWindow#anchorLibraryWindow[trashMode="true"] QTableView::item:selected,
QMainWindow#clipLibraryWindow[trashMode="true"] QTableView::item:selected,
QToolButton#anchorLibraryTrashButton:checked {
  background: %23; color: %12; border-color: %23;
}
)QSS");
    sheet.replace(QStringLiteral("@BUTTONS@\n"), buttonRules.mid(1));
    return sheet
        .arg(cssColor(tokens.canvas))
        .arg(cssColor(tokens.text))
        .arg(cssColor(tokens.panel))
        .arg(cssColor(tokens.raisedSurface))
        .arg(cssColor(tokens.border))
        .arg(metrics.largeRadius)
        .arg(cssColor(tokens.mutedText))
        .arg(metrics.regularControlHeight)
        .arg(metrics.smallRadius)
        .arg(metrics.panelHeaderPadding)
        .arg(cssColor(tokens.selection))
        .arg(cssColor(tokens.selectionText))
        .arg(cssColor(tokens.focus))
        .arg(cssColor(tokens.disabledText))
        .arg(cssColor(tokens.disabledSurface))
        .arg(metrics.baseSpacing * 3)
        .arg(metrics.compactControlHeight)
        .arg(cssColor(tokens.hoverSurface))
        .arg(cssColor(tokens.pressedSurface))
        .arg(cssColor(tokens.accent))
        .arg(metrics.primaryControlHeight)
        .arg(cssColor(tokens.success))
        .arg(cssColor(tokens.error))
        .arg(cssColor(tokens.alternateSurface))
        .arg(metrics.panelHeaderPadding)
        .arg(metrics.largeRadius)
        .arg(metrics.baseSpacing)
        .arg(cssColor(tokens.accent))
        .arg(cssColor(successSurface))
        .arg(cssColor(warningSurface))
        .arg(cssColor(tokens.warning))
        .arg(cssColor(errorSurface));
}

QString pinloomVisualThemeStyleSheet(PinloomVisualScheme scheme)
{
    return visualThemeStyleSheet(scheme, false);
}

void applyPinloomVisualTheme(QApplication &application,
                             PinloomVisualScheme scheme)
{
#ifdef PINLOOM_ENABLE_SUITEUI
    SuiteUiAdapter::apply(application, scheme);
    const bool suiteUi = SuiteUiAdapter::enabled();
#else
    constexpr bool suiteUi = false;
#endif
    const PinloomVisualTokens tokens = pinloomVisualTokens(scheme);
    application.setProperty("pinloomVisualScheme",
                            scheme == PinloomVisualScheme::Dark
                                ? QStringLiteral("dark")
                                : QStringLiteral("light"));
    Ui::applyElaTheme(application, scheme);
    QPalette palette = application.palette();
    palette.setColor(QPalette::Window, tokens.canvas);
    palette.setColor(QPalette::WindowText, tokens.text);
    palette.setColor(QPalette::Base, tokens.panel);
    palette.setColor(QPalette::AlternateBase, tokens.alternateSurface);
    palette.setColor(QPalette::Text, tokens.text);
    palette.setColor(QPalette::Button, tokens.panel);
    palette.setColor(QPalette::ButtonText, tokens.text);
    palette.setColor(QPalette::Highlight, tokens.selection);
    palette.setColor(QPalette::HighlightedText, tokens.selectionText);
    palette.setColor(QPalette::ToolTipBase, tokens.text);
    palette.setColor(QPalette::ToolTipText, tokens.panel);
    palette.setColor(QPalette::PlaceholderText, tokens.mutedText);
    palette.setColor(QPalette::Link, tokens.accent);
    palette.setColor(QPalette::Disabled, QPalette::Text, tokens.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, tokens.disabledText);
    application.setPalette(palette);
    application.setProperty("pinloomReducedMotion",
                            pinloomReducedMotionEnabled());
    application.setProperty("pinloomBaseSpacing", 4);
    application.setStyleSheet(Ui::scopedStyleSheet(visualThemeStyleSheet(scheme, suiteUi)));
    Ui::refreshViewPalettes(application);
}

void applySystemPinloomVisualTheme(QApplication &application)
{
    applyPinloomVisualTheme(application, systemPinloomVisualScheme());
}

} // namespace Pinloom
