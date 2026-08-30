#pragma once

#include "pinloom/core/SumatraPdfDdeClient.h"

#include <QDialog>
#include <QPair>
#include <QPoint>
#include <QString>
#include <functional>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QTimer;

namespace Pinloom {

struct SumatraPdfRegionCaptureResult {
    SumatraPdfDdeRegion region;
    bool canceled = false;
    bool usedFallback = false;
    QString diagnostics;

    bool success() const;
};

// Capture-only overlay. Rectangle Anchor presentation is owned by
// PdfAnchorPresenter and never uses this dialog.
class SumatraPdfRegionCaptureOverlay final : public QDialog {
public:
    using MousePositionProvider = std::function<SumatraPdfDdeMousePosition()>;

    explicit SumatraPdfRegionCaptureOverlay(
        quintptr targetWindowHandle,
        MousePositionProvider mousePositionProvider = {},
        QWidget *parent = nullptr,
        int fallbackPage = 1,
        double fallbackZoom = -1.0,
        int captureTimeoutMilliseconds = 30000);

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
    SumatraPdfDdeMousePosition sampleMousePositionAt(const QPoint &globalPosition);
    SumatraPdfDdeRegion fallbackRegionForSelection(
        const QPoint &globalStart,
        const QPoint &globalEnd,
        const SumatraPdfDdeMousePosition &start,
        const SumatraPdfDdeMousePosition &end);
    void showCaptureError(const QString &message);

    quintptr targetWindowHandle_ = 0;
    MousePositionProvider mousePositionProvider_;
    SumatraPdfRegionCaptureResult result_;
    QPoint dragStart_;
    QPoint dragCurrent_;
    bool dragging_ = false;
    bool usingDefaultMousePositionProvider_ = false;
    int fallbackPage_ = 1;
    double fallbackZoom_ = -1.0;
    QTimer *captureTimer_ = nullptr;
};

SumatraPdfRegionCaptureResult captureSumatraPdfRegion(
    quintptr targetWindowHandle,
    int fallbackPage = 1,
    double fallbackZoom = -1.0,
    int captureTimeoutMilliseconds = 30000);

} // namespace Pinloom
