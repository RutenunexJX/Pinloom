#pragma once

#include <QObject>
#include <memory>

class QWidget;

namespace Pinloom {
class PinloomCommandPanel;

class CommandFloatingController final : public QObject {
    Q_OBJECT
public:
    CommandFloatingController(QWidget &window, PinloomCommandPanel &panel);
    ~CommandFloatingController() override;
    bool isFloating() const;
    bool isCapturing() const;
    QWidget *floatingWindow() const;
    // Returns false when expansion is deferred until an active capture ends.
    bool requestExpansion();

public slots:
    void handleApplicationStateChanged(Qt::ApplicationState state);

signals:
    void captureStarted();
    void captureFinished();

private:
    void showFloatingWindow();
    void capture(int action);
    QWidget &window_;
    PinloomCommandPanel &panel_;
    std::unique_ptr<QWidget> floating_;
    bool collapsed_ = false;
    bool capturing_ = false;
    bool expansionPending_ = false;
};
}
