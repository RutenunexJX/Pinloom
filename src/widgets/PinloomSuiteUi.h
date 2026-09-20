#pragma once

#include "pinloom/widgets/PinloomVisualTheme.h"

class QApplication;
class QStyle;

namespace Pinloom::SuiteUiAdapter {
bool enabled();
QStyle *installedStyle();
void apply(QApplication &application, PinloomVisualScheme scheme);
}
