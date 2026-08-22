#pragma once

#include "pinloom/widgets/PinloomCommandPanel.h"

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QWidget>

class QEvent;
class QHideEvent;
class QTimer;

namespace Pinloom {

struct ForegroundTextTarget;

struct ClipQuickPickerOptions {
    PinloomCommandPanelOptions panelOptions;
    int untouchedDismissMilliseconds = 3000;
    int idleDismissMilliseconds = 15000;
    int preferredWidth = 500;
};

QRect clipQuickPickerGeometry(const QPoint &anchorPoint,
                              const QSize &popupSize,
                              const QRect &availableGeometry,
                              int gap = 4);

class ClipQuickPicker final : public QWidget {
    Q_OBJECT

public:
    explicit ClipQuickPicker(ClipQuickPickerOptions options,
                             QWidget *parent = nullptr);

    void openAt(const QPoint &anchorPoint, const QString &query = QString());
    void openForTarget(const ForegroundTextTarget &target,
                       const QString &query = QString());
    void dismiss();

    PinloomCommandPanel *panel() const;

signals:
    void dismissed();

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    enum class SessionState {
        Closed,
        Untouched,
        Active
    };

    void markInteraction();
    void startDismissTimer(int milliseconds);
    void resizeAndPosition();
    QPoint fallbackAnchorForTarget(const ForegroundTextTarget &target) const;

    ClipQuickPickerOptions options_;
    PinloomCommandPanel *panel_ = nullptr;
    QTimer *dismissTimer_ = nullptr;
    QPoint anchorPoint_;
    SessionState sessionState_ = SessionState::Closed;
    quint64 sessionGeneration_ = 0;
};

} // namespace Pinloom
