#include "pinloom/widgets/PinloomMainWindow.h"

#include <QCloseEvent>

namespace Pinloom {

PinloomMainWindow::PinloomMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
}

void PinloomMainWindow::closeEvent(QCloseEvent *event)
{
    hide();
    if (event) {
        event->ignore();
    }
    emit hiddenToTray();
}

} // namespace Pinloom
