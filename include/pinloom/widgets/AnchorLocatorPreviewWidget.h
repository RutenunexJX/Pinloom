#pragma once

#include "pinloom/core/Resource.h"

#include <QPixmap>
#include <QWidget>

namespace Pinloom {

class AnchorLocatorPreviewWidget final : public QWidget {
    Q_OBJECT

public:
    explicit AnchorLocatorPreviewWidget(QWidget *parent = nullptr);

    void setLocator(const Resource &resource, const Anchor &anchor);
    void setScreenshot(const QPixmap &screenshot);
    void clearPreview();

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Resource resource_;
    Anchor anchor_;
    QPixmap screenshot_;
};

} // namespace Pinloom
