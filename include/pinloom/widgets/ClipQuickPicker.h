#pragma once

#include "pinloom/widgets/PinloomCommandPanel.h"

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QWidget>

class QCloseEvent;
class QHideEvent;
class QKeyEvent;
class QMoveEvent;
class QScreen;
class QSettings;
class QTimer;

namespace Pinloom {

struct ForegroundTextTarget;

struct ClipQuickPickerOptions {
    PinloomCommandPanelOptions panelOptions;
    QSettings *settings = nullptr;
    int preferredWidth = 500;
};

QRect clipQuickPickerCenteredGeometry(const QSize &windowSize,
                                      const QRect &availableGeometry);
QRect clipQuickPickerPositionedGeometry(const QPoint &topLeft,
                                        const QSize &windowSize,
                                        const QRect &availableGeometry);

class ClipQuickPicker final : public QWidget {
    Q_OBJECT

public:
    explicit ClipQuickPicker(ClipQuickPickerOptions options,
                             QWidget *parent = nullptr);

    void openForTarget(const ForegroundTextTarget &target,
                       const QString &query = QString());
    void dismiss();

    PinloomCommandPanel *panel() const;

signals:
    void dismissed();

protected:
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private:
    QScreen *screenForTarget(const ForegroundTextTarget &target) const;
    void resizeAndPosition();
    void persistPosition();

    ClipQuickPickerOptions options_;
    PinloomCommandPanel *panel_ = nullptr;
    QTimer *positionSaveTimer_ = nullptr;
    QScreen *openingScreen_ = nullptr;
    bool positionInitialized_ = false;
    bool applyingGeometry_ = false;
};

} // namespace Pinloom
