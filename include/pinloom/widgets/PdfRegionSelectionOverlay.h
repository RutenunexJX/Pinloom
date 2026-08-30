#pragma once

#include <QDialog>
#include <QRect>
#include <QString>

#include <functional>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QTimer;

namespace Pinloom {

enum class PdfRegionSelectionState {
    Selected,
    Failed,
    Canceled,
    TimedOut
};

struct PdfRegionSelectionResult {
    PdfRegionSelectionState state = PdfRegionSelectionState::Failed;
    QRect screenRect;
    QString diagnostics;

    bool selected() const;
    bool canceled() const;
    bool timedOut() const;
};

// Viewer-neutral screen selection surface. Viewer discovery, activation and
// conversion to page coordinates belong to PdfViewerAdapter implementations.
class PdfRegionSelectionOverlay final : public QDialog {
public:
    using TargetStateProvider = std::function<QString()>;

    explicit PdfRegionSelectionOverlay(
        const QRect &targetScreenGeometry,
        TargetStateProvider targetStateProvider = {},
        QWidget *parent = nullptr,
        int timeoutMilliseconds = 30000,
        int minimumSelectionPixels = 4);

    PdfRegionSelectionResult selectionResult() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void reject() override;

private:
    void finish(PdfRegionSelectionState state,
                const QString &diagnostics = QString());

    TargetStateProvider targetStateProvider_;
    PdfRegionSelectionResult result_;
    QPoint dragStart_;
    QPoint dragCurrent_;
    bool dragging_ = false;
    int minimumSelectionPixels_ = 4;
    QTimer *timeoutTimer_ = nullptr;
    QTimer *targetStateTimer_ = nullptr;
};

} // namespace Pinloom
