#include "PinloomSuiteUi.h"

#include <SuiteUi/ControlStyle.hpp>
#include <QAbstractItemView>
#include <QApplication>
#include <QDynamicPropertyChangeEvent>
#include <QPointer>
#include <QStyleFactory>
#include <QWidget>

namespace Pinloom::SuiteUiAdapter {
namespace {
QPointer<SuiteUi::ControlStyle> backend;

bool protectedWidget(const QWidget *widget)
{
    for (auto *parent = widget; parent; parent = parent->parentWidget()) {
        const QString name = parent->objectName();
        if (qobject_cast<const QAbstractItemView *>(parent)
            || name == QLatin1String("pdfRegionSelectionOverlay")
            || name == QLatin1String("textPreviewDialog")
            || name == QLatin1String("anchorLibraryLocatorPreview")
            || name == QLatin1String("anchorLocatorExpandedPreview")
            || name == QLatin1String("anchorLibraryTrashButton")
            || !parent->property("accent").toString().isEmpty()) {
            return true;
        }
    }
    return false;
}

class ViewBoundaries final : public QObject {
public:
    ViewBoundaries(QApplication &application, QStyle *base) : QObject(&application), base_(base)
    {
        base_->setParent(this);
        application.installEventFilter(this);
    }

private:
    QStyle *base_;
    bool updating_ = false;

    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (updating_) return false;
        const auto type = event->type();
        if (type != QEvent::Polish && type != QEvent::ParentChange
            && type != QEvent::DynamicPropertyChange) return false;
        if (type == QEvent::DynamicPropertyChange) {
            const auto property = static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName();
            if (property != "accent") return false;
        }
        auto *widget = qobject_cast<QWidget *>(object);
        if (!widget) return false;
        const bool protect = protectedWidget(widget);
        if (protect == widget->property("pinloomSuiteUiClassic").toBool()) return false;
        updating_ = true;
        widget->setProperty("pinloomSuiteUiClassic", protect);
        widget->setStyle(protect ? base_ : nullptr);
        for (auto *child : widget->findChildren<QWidget *>()) {
            const bool childProtected = protectedWidget(child);
            child->setProperty("pinloomSuiteUiClassic", childProtected);
            child->setStyle(childProtected ? base_ : nullptr);
        }
        updating_ = false;
        return false;
    }
};

class Style final : public SuiteUi::ControlStyle {
public:
    explicit Style(QStyle *base) : ControlStyle(base) {}

protected:
    bool isPrimary(const QWidget *widget) const override
    {
        return (widget && widget->property("pinloomControl") == QLatin1String("primary"))
            || ControlStyle::isPrimary(widget);
    }
};
}

bool enabled()
{
    const QString value = qEnvironmentVariable("PINLOOM_UI_STYLE").trimmed();
    if (value.isEmpty() || value == QLatin1String("suiteui")) return true;
    if (value == QLatin1String("classic")) {
        if (backend) qFatal("PINLOOM_UI_STYLE must be selected before installing SuiteUi");
        return false;
    }
    qFatal("Unsupported PINLOOM_UI_STYLE; expected classic or suiteui");
    return false;
}

QStyle *installedStyle() { return backend; }

void apply(QApplication &application, PinloomVisualScheme scheme)
{
    if (!enabled()) {
        application.setProperty("pinloomControlBackend", QStringLiteral("classic"));
        return;
    }
    if (!backend) {
        const QString baseName = application.style()->objectName();
        QStyle *base = QStyleFactory::create(baseName);
        QStyle *viewBase = QStyleFactory::create(baseName);
        if (!base || !viewBase) {
            delete base;
            delete viewBase;
            qFatal("Cannot preserve Pinloom's current Qt style: %s", qPrintable(baseName));
        }
        backend = new Style(base);
        backend->setObjectName(QStringLiteral("PinloomSuiteUi"));
        application.setProperty("pinloomSuiteUiBaseStyle", baseName);
        application.setStyle(backend);
        new ViewBoundaries(application, viewBase);
    }
    // A theme replacement is immediate; never interpolate colors from the old palette.
    backend->setAnimationsEnabled(false);
    const auto t = pinloomVisualTokens(scheme);
    backend->setControlPalette({
        {t.panel, t.hoverSurface, t.pressedSurface, t.disabledSurface},
        {t.text, t.text, t.text, t.disabledText},
        {t.selection, t.accent, t.accent.darker(110), t.disabledSurface},
        {t.selectionText, t.selectionText, t.selectionText, t.disabledText},
        {t.border, t.mutedText, t.accent, t.border}, t.focus});
    backend->setAnimationsEnabled(!pinloomReducedMotionEnabled());
    backend->setProperty("pinloomAnimationsEnabled", backend->animationsEnabled());
    application.setProperty("pinloomControlBackend", backend->objectName());
}
}
