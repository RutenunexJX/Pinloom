#pragma once

#include "pinloom/core/Resource.h"

#include <QPixmap>
#include <QPointer>
#include <QWidget>

class QDialog;
class QLabel;
class QKeyEvent;
class QMouseEvent;

namespace Pinloom {

class AnchorLocatorPreviewWidget final : public QWidget {
    Q_OBJECT

public:
    explicit AnchorLocatorPreviewWidget(QWidget *parent = nullptr);

    void setLocator(const Resource &resource, const Anchor &anchor);
    void setScreenshot(const QPixmap &screenshot);
    void setError(const QString &message);
    void clearPreview();
    bool hasScreenshot() const;
    bool showExpandedPreview();

    QSize sizeHint() const override;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void closeExpandedPreview();
    void updateExpandedPreview();

    Resource resource_;
    Anchor anchor_;
    QPixmap screenshot_;
    QString errorMessage_;
    QPointer<QDialog> expandedPreviewDialog_;
    QPointer<QLabel> expandedPreviewLabel_;
};

} // namespace Pinloom
