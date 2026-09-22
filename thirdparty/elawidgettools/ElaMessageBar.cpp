#include "ElaMessageBar.h"

#include "ElaApplication.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QTimer>
#include <QTextLayout>

#include "ElaIconButton.h"
#include "ElaTheme.h"
#include "private/ElaMessageBarPrivate.h"

ElaMessageBar::ElaMessageBar(ElaMessageBarType::PositionPolicy policy, ElaMessageBarType::MessageMode messageMode, QString& title, QString& text, int displayMsec, QWidget* parent)
    : QWidget{parent}, d_ptr(new ElaMessageBarPrivate())
{
    Q_D(ElaMessageBar);
    d->q_ptr = this;
    d->_borderRadius = 6;
    d->_title = title;
    d->_text = text;
    d->_policy = policy;
    d->_messageMode = messageMode;
    d->_themeMode = eTheme->getThemeMode();
    const bool instant = parent && parent->property("pinloomInstantNotices").toBool();
    setProperty("pinloomInstantNotice", instant);
    setFixedHeight(60);
    setMouseTracking(true);
    d->_pOpacity = 1;
    setFont(QFont("微软雅黑"));
    parent->installEventFilter(this);
    d->_closeButton = new ElaIconButton(ElaIconType::Xmark, 17, d->_closeButtonWidth, 30, this);
    d->_closeButton->setAccessibleName(tr("Dismiss notification"));
    switch (d->_messageMode)
    {
    case ElaMessageBarType::Success:
    {
        d->_closeButton->setLightHoverColor(QColor(0xCA, 0xDE, 0xC8));
        d->_closeButton->setDarkHoverColor(QColor(0xCA, 0xDE, 0xC8));
        d->_closeButton->setDarkIconColor(Qt::black);
        d->_closeButton->setDarkHoverIconColor(Qt::black);
        break;
    }
    case ElaMessageBarType::Warning:
    {
        d->_closeButton->setLightHoverColor(QColor(0x5E, 0x4C, 0x22));
        d->_closeButton->setDarkHoverColor(QColor(0x5E, 0x4C, 0x22));
        d->_closeButton->setLightIconColor(Qt::white);
        d->_closeButton->setDarkIconColor(Qt::white);
        d->_closeButton->setLightHoverIconColor(Qt::white);
        break;
    }
    case ElaMessageBarType::Information:
    {
        d->_closeButton->setLightHoverColor(QColor(0xDE, 0xDE, 0xDE));
        d->_closeButton->setDarkHoverColor(QColor(0xDE, 0xDE, 0xDE));
        d->_closeButton->setDarkIconColor(Qt::black);
        d->_closeButton->setDarkHoverIconColor(Qt::black);
        break;
    }
    case ElaMessageBarType::Error:
    {
        d->_closeButton->setLightHoverColor(QColor(0xF2, 0xDD, 0xE0));
        d->_closeButton->setDarkHoverColor(QColor(0xF2, 0xDD, 0xE0));
        d->_closeButton->setDarkIconColor(Qt::black);
        d->_closeButton->setDarkHoverIconColor(Qt::black);
        break;
    }
    }
    d->_closeButton->setBorderRadius(5);
    connect(d->_closeButton, &ElaIconButton::clicked, d, &ElaMessageBarPrivate::messageBarEnd);
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 10, 0);
    mainLayout->addStretch();
    mainLayout->addWidget(d->_closeButton);
    setObjectName("ElaMessageBar");
    setStyleSheet("#ElaMessageBar{background-color:transparent;}");
    if (instant)
    {
        setFont(parent->font());
        setFixedWidth(qMax(160, qMin(500, parent->width() - 16)));
        setFixedHeight(qMin(parent->height() - 16, qMax(60, fontMetrics().lineSpacing() * 3 + 24)));
        move((parent->width() - width()) / 2, qMax(0, parent->height() - height() - 8));
        connect(eTheme, &ElaTheme::themeModeChanged, this, [this, d](ElaThemeType::ThemeMode mode) {
            d->_themeMode = mode;
            update();
        });
        QTimer::singleShot(displayMsec, this, &QObject::deleteLater);
        show();
        raise();
    }
    else
    {
        d->_messageBarCreate(displayMsec);
    }
}

ElaMessageBar::~ElaMessageBar()
{
}

void ElaMessageBar::success(ElaMessageBarType::PositionPolicy policy, QString title, QString text, int displayMsec, QWidget* parent)
{
    // qDebug() << QApplication::topLevelWidgets();
    if (!parent)
    {
        QList<QWidget*> widgetList = QApplication::topLevelWidgets();
        for (auto widget: widgetList)
        {
            if (widget->property("ElaBaseClassName").toString() == "ElaWindow")
            {
                parent = widget;
            }
        }
        if (!parent)
        {
            return;
        }
    }

    ElaMessageBar* bar = new ElaMessageBar(policy, ElaMessageBarType::Success, title, text, displayMsec, parent);
    Q_UNUSED(bar);
}

void ElaMessageBar::warning(ElaMessageBarType::PositionPolicy policy, QString title, QString text, int displayMsec, QWidget* parent)
{
    if (!parent)
    {
        QList<QWidget*> widgetList = QApplication::topLevelWidgets();
        for (auto widget: widgetList)
        {
            if (widget->property("ElaBaseClassName").toString() == "ElaWindow")
            {
                parent = widget;
            }
        }
        if (!parent)
        {
            return;
        }
    }
    ElaMessageBar* bar = new ElaMessageBar(policy, ElaMessageBarType::Warning, title, text, displayMsec, parent);
    Q_UNUSED(bar);
}

void ElaMessageBar::information(ElaMessageBarType::PositionPolicy policy, QString title, QString text, int displayMsec, QWidget* parent)
{
    if (!parent)
    {
        QList<QWidget*> widgetList = QApplication::topLevelWidgets();
        for (auto widget: widgetList)
        {
            if (widget->property("ElaBaseClassName").toString() == "ElaWindow")
            {
                parent = widget;
            }
        }
        if (!parent)
        {
            return;
        }
    }
    ElaMessageBar* bar = new ElaMessageBar(policy, ElaMessageBarType::Information, title, text, displayMsec, parent);
    Q_UNUSED(bar);
}

void ElaMessageBar::error(ElaMessageBarType::PositionPolicy policy, QString title, QString text, int displayMsec, QWidget* parent)
{
    if (!parent)
    {
        QList<QWidget*> widgetList = QApplication::topLevelWidgets();
        for (auto widget: widgetList)
        {
            if (widget->property("ElaBaseClassName").toString() == "ElaWindow")
            {
                parent = widget;
            }
        }
        if (!parent)
        {
            return;
        }
    }
    ElaMessageBar* bar = new ElaMessageBar(policy, ElaMessageBarType::Error, title, text, displayMsec, parent);
    Q_UNUSED(bar);
}

void ElaMessageBar::paintEvent(QPaintEvent* event)
{
    Q_D(ElaMessageBar);
    int fontPixelSize = eApp->getFontPixelSize();
    QPainter painter(this);
    painter.setOpacity(d->_pOpacity);
    painter.setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing | QPainter::TextAntialiasing);
    // 高性能阴影
    eTheme->drawEffectShadow(&painter, rect(), d->_shadowBorderWidth, d->_borderRadius);

    // 背景和图标绘制
    painter.save();
    painter.setPen(d->_themeMode == ElaThemeType::Light ? QColor(0xBE, 0xBA, 0xBE) : QColor(0x52, 0x50, 0x52));
    switch (d->_messageMode)
    {
    case ElaMessageBarType::Success:
    {
        d->_drawSuccess(&painter);
        break;
    }
    case ElaMessageBarType::Warning:
    {
        d->_drawWarning(&painter);
        break;
    }
    case ElaMessageBarType::Information:
    {
        d->_drawInformation(&painter);
        break;
    }
    case ElaMessageBarType::Error:
    {
        d->_drawError(&painter);
        break;
    }
    }
    // 文字绘制
    if (property("pinloomInstantNotice").toBool())
    {
        // Bounded three-line preview. The accessible description and status retain the full text.
        painter.setFont(font());
        const int left = d->_leftPadding + d->_titleLeftSpacing;
        const int available = qMax(1, width() - left - d->_closeButtonWidth - 24);
        const QString text = d->_text.simplified();
        QTextLayout layout(text, font());
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        const int rows = qBound(1, (height() - 20) / fontMetrics().lineSpacing(), 3);
        for (int row = 0; row < rows; ++row)
        {
            auto line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(available);
            const QPointF position(left, 10 + row * fontMetrics().lineSpacing());
            if (row == rows - 1 && line.textStart() + line.textLength() < text.size())
                painter.drawText(QRectF(position, QSizeF(available, fontMetrics().lineSpacing())),
                    Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(text.mid(line.textStart()), Qt::ElideRight, available));
            else line.draw(&painter, position);
        }
        layout.endLayout();
        painter.restore();
        return;
    }
    // 标题
    QFont font = this->font();
    font.setWeight(QFont::Bold);
    font.setPixelSize(fontPixelSize + 3);
    painter.setFont(font);
    int titleTextWidth = painter.fontMetrics().horizontalAdvance(d->_title) + 1;
    if (titleTextWidth > 100)
    {
        titleTextWidth = 100;
    }
    int textFlags = Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap | Qt::TextWrapAnywhere;
    painter.drawText(QRect(d->_leftPadding + d->_titleLeftSpacing, -1, titleTextWidth, height()), textFlags, d->_title);
    // 正文
    font.setWeight(QFont::Light);
    font.setPixelSize(fontPixelSize + 2);
    painter.setFont(font);
    painter.drawText(QRect(d->_leftPadding + d->_titleLeftSpacing + titleTextWidth + d->_textLeftSpacing, 0, width() - (d->_leftPadding + d->_titleLeftSpacing + titleTextWidth + d->_textLeftSpacing + d->_closeButtonWidth + d->_closeButtonLeftRightMargin / 2), height() - d->_timePercentHeight), textFlags, d->_text);
    int textHeight = painter.fontMetrics().boundingRect(QRect(d->_leftPadding + d->_titleLeftSpacing + titleTextWidth + d->_textLeftSpacing, 0, width() - (d->_leftPadding + d->_titleLeftSpacing + titleTextWidth + d->_textLeftSpacing + d->_closeButtonWidth + d->_closeButtonLeftRightMargin), height()), textFlags, d->_text).height();
    if (textHeight >= minimumHeight() - 20)
    {
        setMinimumHeight(textHeight + 20);
    }
    painter.restore();
}

bool ElaMessageBar::eventFilter(QObject* watched, QEvent* event)
{
    Q_D(ElaMessageBar);
    if (property("pinloomInstantNotice").toBool())
    {
        if (watched == parentWidget() && (event->type() == QEvent::Resize || event->type() == QEvent::FontChange))
        {
            setFont(parentWidget()->font());
            setFixedWidth(qMax(160, qMin(500, parentWidget()->width() - 16)));
            setFixedHeight(qMax(0, qMin(parentWidget()->height() - 16, qMax(60, fontMetrics().lineSpacing() * 3 + 24))));
            move((parentWidget()->width() - width()) / 2, qMax(0, parentWidget()->height() - height() - 8));
        }
        return QWidget::eventFilter(watched, event);
    }
    if (watched == parentWidget())
    {
        switch (event->type())
        {
        case QEvent::Resize:
        {
            QResizeEvent* resizeEvent = dynamic_cast<QResizeEvent*>(event);
            QSize offsetSize = parentWidget()->size() - resizeEvent->oldSize();
            if (d->_isNormalDisplay)
            {
                switch (d->_policy)
                {
                case ElaMessageBarType::Top:
                {
                    this->move(parentWidget()->width() / 2 - minimumWidth() / 2, this->y());
                    break;
                }
                case ElaMessageBarType::Bottom:
                {
                    this->move(parentWidget()->width() / 2 - minimumWidth() / 2, this->pos().y() + offsetSize.height());
                    break;
                }
                case ElaMessageBarType::Left:
                case ElaMessageBarType::TopLeft:
                {
                    this->move(d->_messageBarHorizontalMargin, this->pos().y());
                    break;
                }
                case ElaMessageBarType::BottomLeft:
                {
                    this->move(d->_messageBarHorizontalMargin, this->pos().y() + offsetSize.height());
                    break;
                }
                case ElaMessageBarType::Right:
                case ElaMessageBarType::TopRight:
                {
                    this->move(parentWidget()->width() - minimumWidth() - d->_messageBarHorizontalMargin, this->y());
                    break;
                }
                case ElaMessageBarType::BottomRight:
                {
                    this->move(parentWidget()->width() - minimumWidth() - d->_messageBarHorizontalMargin, this->pos().y() + offsetSize.height());
                    break;
                }
                }
            }
            break;
        }
        default:
        {
            break;
        }
        }
    }
    return QWidget::eventFilter(watched, event);
}
