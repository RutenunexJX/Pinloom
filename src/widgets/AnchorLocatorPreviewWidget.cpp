#include "pinloom/widgets/AnchorLocatorPreviewWidget.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollArea>
#include <QVBoxLayout>
#include <algorithm>

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
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
}

void AnchorLocatorPreviewWidget::setLocator(const Resource &resource, const Anchor &anchor)
{
    closeExpandedPreview();
    resource_ = resource;
    anchor_ = anchor;
    screenshot_ = {};
    errorMessage_.clear();
    unsetCursor();
    setToolTip({});
    update();
}

void AnchorLocatorPreviewWidget::setScreenshot(const QPixmap &screenshot)
{
    screenshot_ = screenshot;
    if (!screenshot_.isNull()) errorMessage_.clear();
    if (screenshot_.isNull()) {
        unsetCursor();
        setToolTip({});
    } else {
        setCursor(Qt::PointingHandCursor);
        setToolTip(tr("Open enlarged preview"));
    }
    updateExpandedPreview();
    update();
}

void AnchorLocatorPreviewWidget::setError(const QString &message)
{
    closeExpandedPreview();
    screenshot_ = {};
    errorMessage_ = message.trimmed();
    unsetCursor();
    setToolTip(errorMessage_);
    update();
}

void AnchorLocatorPreviewWidget::clearPreview()
{
    closeExpandedPreview();
    resource_ = {};
    anchor_ = {};
    screenshot_ = {};
    errorMessage_.clear();
    unsetCursor();
    setToolTip({});
    update();
}

bool AnchorLocatorPreviewWidget::hasScreenshot() const
{
    return !screenshot_.isNull();
}

bool AnchorLocatorPreviewWidget::showExpandedPreview()
{
    if (screenshot_.isNull()) return false;
    if (!expandedPreviewDialog_) {
        auto *dialog = new QDialog(window());
        dialog->setObjectName(QStringLiteral("anchorLocatorExpandedPreview"));
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(anchor_.name.trimmed().isEmpty()
                                   ? tr("Anchor preview")
                                   : tr("Anchor preview - %1").arg(anchor_.name.trimmed()));
        dialog->resize(960, 640);

        auto *layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(8, 8, 8, 8);
        auto *scrollArea = new QScrollArea(dialog);
        scrollArea->setObjectName(QStringLiteral("anchorLocatorExpandedPreviewScroll"));
        scrollArea->setAlignment(Qt::AlignCenter);
        scrollArea->setWidgetResizable(false);
        auto *label = new QLabel(scrollArea);
        label->setObjectName(QStringLiteral("anchorLocatorExpandedPreviewImage"));
        label->setAlignment(Qt::AlignCenter);
        scrollArea->setWidget(label);
        layout->addWidget(scrollArea);

        expandedPreviewDialog_ = dialog;
        expandedPreviewLabel_ = label;
        updateExpandedPreview();
    }
    expandedPreviewDialog_->show();
    expandedPreviewDialog_->raise();
    expandedPreviewDialog_->activateWindow();
    return true;
}

QSize AnchorLocatorPreviewWidget::sizeHint() const
{
    return {320, 240};
}

void AnchorLocatorPreviewWidget::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
         || event->key() == Qt::Key_Space)
        && showExpandedPreview()) {
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void AnchorLocatorPreviewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())
        && showExpandedPreview()) {
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
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
    if (!errorMessage_.isEmpty()) {
        painter.setPen(QColor(QStringLiteral("#9d3340")));
        painter.drawText(content.adjusted(14, 14, -14, -14),
                         Qt::AlignCenter | Qt::TextWordWrap,
                         errorMessage_);
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

void AnchorLocatorPreviewWidget::closeExpandedPreview()
{
    if (expandedPreviewDialog_) expandedPreviewDialog_->close();
    expandedPreviewDialog_.clear();
    expandedPreviewLabel_.clear();
}

void AnchorLocatorPreviewWidget::updateExpandedPreview()
{
    if (!expandedPreviewLabel_ || screenshot_.isNull()) return;

    const QSize maximumSize = expandedPreviewDialog_
        ? expandedPreviewDialog_->size() - QSize(40, 60)
        : QSize(920, 580);
    qreal scale = std::min(static_cast<qreal>(maximumSize.width()) / screenshot_.width(),
                           static_cast<qreal>(maximumSize.height()) / screenshot_.height());
    scale = std::clamp(scale, 0.05, 2.0);
    const QSize displaySize(qRound(screenshot_.width() * scale),
                            qRound(screenshot_.height() * scale));
    expandedPreviewLabel_->setPixmap(screenshot_.scaled(displaySize,
                                                         Qt::KeepAspectRatio,
                                                         Qt::SmoothTransformation));
    expandedPreviewLabel_->adjustSize();
}

} // namespace Pinloom
