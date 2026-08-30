#pragma once

#include <QColor>
#include <QString>

class QApplication;

namespace Pinloom {

enum class PinloomVisualScheme {
    Light,
    Dark
};

struct PinloomVisualTokens {
    QColor canvas;
    QColor panel;
    QColor raisedSurface;
    QColor alternateSurface;
    QColor border;
    QColor text;
    QColor mutedText;
    QColor accent;
    QColor selection;
    QColor selectionText;
    QColor focus;
    QColor success;
    QColor warning;
    QColor error;
    QColor hoverSurface;
    QColor pressedSurface;
    QColor disabledSurface;
    QColor disabledText;

    bool isValid(QString *error = nullptr) const;
};

struct PinloomVisualMetrics {
    int scalePercent = 100;
    int baseSpacing = 4;
    int compactControlHeight = 28;
    int regularControlHeight = 32;
    int primaryControlHeight = 36;
    int smallRadius = 6;
    int largeRadius = 8;
    int panelHeaderPadding = 8;
};

PinloomVisualScheme systemPinloomVisualScheme();
PinloomVisualScheme activePinloomVisualScheme();
PinloomVisualTokens pinloomVisualTokens(PinloomVisualScheme scheme);
PinloomVisualMetrics pinloomVisualMetrics(int scalePercent = 100);
double pinloomContrastRatio(const QColor &foreground,
                            const QColor &background);
bool pinloomReducedMotionEnabled();
QString pinloomVisualThemeStyleSheet(PinloomVisualScheme scheme);
void applyPinloomVisualTheme(QApplication &application,
                             PinloomVisualScheme scheme);
void applySystemPinloomVisualTheme(QApplication &application);

} // namespace Pinloom
