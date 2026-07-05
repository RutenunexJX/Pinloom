#pragma once

#include <QMainWindow>

class QCloseEvent;

namespace Pinloom {

class PinloomMainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit PinloomMainWindow(QWidget *parent = nullptr);

signals:
    void hiddenToTray();

protected:
    void closeEvent(QCloseEvent *event) override;
};

} // namespace Pinloom
