#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>
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

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void showFloatingWindow();
    void moveFloatingWindow(const QPoint &position, const QPoint &screenPoint);
    void resetDrag(bool cancelClick = true);
    void capture(int action);
    QWidget &window_;
    PinloomCommandPanel &panel_;
    std::unique_ptr<QWidget> floating_;
    bool collapsed_ = false;
    bool capturing_ = false;
    bool expansionPending_ = false;
    bool userPositioned_ = false;
    bool dragging_ = false;
    QPointer<QWidget> pressedWidget_;
    QPoint pressPosition_;
    QPoint dragOrigin_;
};
}
