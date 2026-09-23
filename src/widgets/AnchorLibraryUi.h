#pragma once

#include <QString>

class QLabel;
class QToolBar;
class QWidget;

namespace Pinloom::AnchorLibraryUi {
QLabel *text(const QString &value, QWidget *parent, bool heading = false);
QWidget *page(QWidget *header, QWidget *content, QWidget *footer, QWidget *parent);
QToolBar *toolBar(QWidget *parent);
void applyIcons(QWidget *window);
bool confirm(QWidget *parent, const QString &title, const QString &message);
}
