#pragma once

#include "pinloom/core/SumatraPdfDdeClient.h"

#include <QDialog>
#include <QHash>
#include <QObject>
#include <QPair>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>
#include <functional>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QTimer;

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

struct SumatraPdfPersistentHighlight {
    QString key;
    QString targetFile;
    QRectF pdfRect;
    int page = -1;
    double zoom = -1.0;
    quintptr targetWindowHandle = 0;

    bool isValid() const;
};

class SumatraPdfHighlightManager final : public QObject {
public:
    static SumatraPdfHighlightManager &instance();

    bool addOrUpdate(const SumatraPdfPersistentHighlight &highlight);
    bool remove(const QString &key);
    void clear();
    bool contains(const QString &key) const;
    int count() const;
    void refreshNow();

private:
    struct Entry;

    explicit SumatraPdfHighlightManager(QObject *parent = nullptr);
    ~SumatraPdfHighlightManager() override;
    void removeEntries(const QStringList &keys);

    QHash<QString, Entry *> entries_;
    QTimer *refreshTimer_ = nullptr;
    int refreshSerial_ = 0;
};

bool registerSumatraPdfPersistentHighlight(
    const SumatraPdfPersistentHighlight &highlight);
bool showSumatraPdfRectHighlight(const QRectF &pdfRect,
                                 int page,
                                 double zoom,
                                 int durationMilliseconds = 1400);

} // namespace Pinloom
