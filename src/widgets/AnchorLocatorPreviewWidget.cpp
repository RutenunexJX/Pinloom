#include "pinloom/widgets/AnchorLocatorPreviewWidget.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPaintEvent>

namespace Pinloom {

namespace {

struct PdfLocatorGeometry {
    int page = -1;
    QRectF rectangle;
};

PdfLocatorGeometry pdfLocatorGeometry(const Anchor &anchor)
{
    PdfLocatorGeometry geometry;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(anchor.locatorJson.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return geometry;
    }

    const QJsonObject object = document.object();
    geometry.page = object.value(QStringLiteral("page")).toInt(-1);
    QJsonArray values = object.value(QStringLiteral("rect")).toArray();
    if (values.size() != 4) {
        values = object.value(QStringLiteral("region")).toArray();
    }
    if (values.size() == 4) {
        const double left = values.at(0).toDouble();
        const double top = values.at(1).toDouble();
        const double right = values.at(2).toDouble();
        const double bottom = values.at(3).toDouble();
        if (right > left && bottom > top) {
            geometry.rectangle = QRectF(QPointF(left, top), QPointF(right, bottom));
        }
    }
    return geometry;
}

} // namespace

AnchorLocatorPreviewWidget::AnchorLocatorPreviewWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("anchorLibraryLocatorPreview"));
    setMinimumHeight(150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void AnchorLocatorPreviewWidget::setLocator(const Resource &resource, const Anchor &anchor)
{
    resource_ = resource;
    anchor_ = anchor;
    screenshot_ = {};
    update();
}

void AnchorLocatorPreviewWidget::setScreenshot(const QPixmap &screenshot)
{
    screenshot_ = screenshot;
    update();
}

void AnchorLocatorPreviewWidget::clearPreview()
{
    resource_ = {};
    anchor_ = {};
    screenshot_ = {};
    update();
}

QSize AnchorLocatorPreviewWidget::sizeHint() const
{
    return {320, 180};
}

void AnchorLocatorPreviewWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().brush(QPalette::Base));

    const QRect content = rect().adjusted(8, 8, -8, -8);
    if (!screenshot_.isNull()) {
        const QPixmap scaled = screenshot_.scaled(content.size(),
                                                   Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation);
        const QPoint origin(content.center().x() - scaled.width() / 2,
                            content.center().y() - scaled.height() / 2);
        painter.drawPixmap(origin, scaled);
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(QRect(origin, scaled.size()).adjusted(0, 0, -1, -1));
        return;
    }

    const PdfLocatorGeometry geometry = pdfLocatorGeometry(anchor_);
    const QSizeF pageSize(612.0, 792.0);
    QSizeF previewSize = pageSize;
    previewSize.scale(content.size(), Qt::KeepAspectRatio);
    const QRectF pageRect(content.center().x() - previewSize.width() / 2.0,
                          content.center().y() - previewSize.height() / 2.0,
                          previewSize.width(),
                          previewSize.height());

    painter.fillRect(pageRect, Qt::white);
    painter.setPen(QPen(QColor(150, 155, 160), 1.0));
    painter.drawRect(pageRect);

    if (geometry.rectangle.isValid()) {
        const QRectF clipped = geometry.rectangle.intersected(QRectF(QPointF(0, 0), pageSize));
        const QRectF highlighted(pageRect.left() + clipped.left() / pageSize.width() * pageRect.width(),
                                 pageRect.top() + clipped.top() / pageSize.height() * pageRect.height(),
                                 clipped.width() / pageSize.width() * pageRect.width(),
                                 clipped.height() / pageSize.height() * pageRect.height());
        painter.fillRect(highlighted, QColor(245, 205, 35, 95));
        painter.setPen(QPen(QColor(185, 135, 0), 2.0));
        painter.drawRect(highlighted);
    }

    painter.setPen(QColor(55, 60, 65));
    const QString pageLabel = geometry.page > 0
        ? tr("Page %1").arg(geometry.page)
        : tr("Locator preview");
    painter.drawText(pageRect.adjusted(8, 6, -8, -6),
                     Qt::AlignTop | Qt::AlignRight,
                     pageLabel);
    if (anchor_.locatorType.trimmed().isEmpty()) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(pageRect, Qt::AlignCenter, tr("No locator selected"));
    }
}

} // namespace Pinloom
