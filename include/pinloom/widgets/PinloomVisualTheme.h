#pragma once

#include <QString>

class QApplication;

namespace Pinloom {

enum class PinloomVisualScheme {
    Light,
    Dark
};

PinloomVisualScheme systemPinloomVisualScheme();
QString pinloomVisualThemeStyleSheet(PinloomVisualScheme scheme);
void applyPinloomVisualTheme(QApplication &application,
                             PinloomVisualScheme scheme);
void applySystemPinloomVisualTheme(QApplication &application);

} // namespace Pinloom
