#pragma once

#include "pinloom/core/SumatraPdfDdeClient.h"

#include <QDialog>
#include <QPair>
#include <QPoint>
#include <QRect>
#include <functional>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;

namespace Pinloom {

struct SumatraPdfRegionCaptureResult {
    SumatraPdfDdeRegion region;
    bool canceled = false;

    bool success() const;
};

class SumatraPdfRegionCaptureOverlay final : public QDialog {
public:
    using MousePositionProvider = std::function<SumatraPdfDdeMousePosition()>;

    explicit SumatraPdfRegionCaptureOverlay(
        quintptr targetWindowHandle,
        MousePositionProvider mousePositionProvider = {},
        QWidget *parent = nullptr);

    SumatraPdfRegionCaptureResult captureResult() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void reject() override;

private:
    QPair<SumatraPdfDdeMousePosition, SumatraPdfDdeMousePosition> sampleRegionPositions(
        const QPoint &globalStart,
        const QPoint &globalEnd);
    void showCaptureError(const QString &message);

    quintptr targetWindowHandle_ = 0;
    MousePositionProvider mousePositionProvider_;
    SumatraPdfRegionCaptureResult result_;
    QPoint dragStart_;
    QPoint dragCurrent_;
    bool dragging_ = false;
};

SumatraPdfRegionCaptureResult captureSumatraPdfRegion(quintptr targetWindowHandle);
bool showSumatraPdfRectHighlight(const QRectF &pdfRect,
                                 int page,
                                 double zoom,
                                 int durationMilliseconds = 1400);

} // namespace Pinloom
