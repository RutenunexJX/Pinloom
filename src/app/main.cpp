#include "pinloom/widgets/PinloomPanel.h"

#include <QApplication>
#include <QMainWindow>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Pinloom"));
    window.resize(900, 560);
    window.setCentralWidget(new Pinloom::PinloomPanel(&window));
    window.show();

    return app.exec();
}
