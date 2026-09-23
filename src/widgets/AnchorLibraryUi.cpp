#include "AnchorLibraryUi.h"
#include "pinloom/widgets/PinloomUiControls.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QToolBar>
#include <QVBoxLayout>

#ifdef PINLOOM_ENABLE_ELA
#include "ElaContentDialog.h"
#include "ElaScrollPage.h"
#include "ElaText.h"
#include "ElaToolBar.h"
#include "ElaToolButton.h"
#endif

namespace Pinloom::AnchorLibraryUi {
#ifdef PINLOOM_ENABLE_ELA
namespace {
class LibraryToolBar final : public ElaToolBar {
public:
    using ElaToolBar::ElaToolBar;
};
}
#endif

QLabel *text(const QString &value, QWidget *parent, bool heading)
{
    QLabel *result = nullptr;
#ifdef PINLOOM_ENABLE_ELA
    if (Ui::usesEla()) {
        result = new ElaText(value, parent);
        result->setProperty("pinloomElaControl", true);
    }
#endif
    if (!result) result = new QLabel(value, parent);
    auto font = qApp->font();
    if (heading) { font.setPointSizeF(font.pointSizeF() + 4); font.setWeight(QFont::DemiBold); }
    result->setFont(font);
    result->setTextFormat(Qt::PlainText);
    result->setMinimumWidth(0);
    result->setWordWrap(true);
    return result;
}

QWidget *page(QWidget *header, QWidget *content, QWidget *footer, QWidget *parent)
{
#ifdef PINLOOM_ENABLE_ELA
    if (Ui::usesEla()) {
        auto *page = new ElaScrollPage(parent);
        page->setObjectName(QStringLiteral("anchorLibraryElaPage"));
        page->setProperty("pinloomElaControl", true);
        page->setContentsMargins(12, 8, 12, 8);
        page->setTitleVisible(false);
        page->setTopCustomWidget(header);
        content->setWindowTitle(QObject::tr("Anchor Library"));
        // Tables handle selection and scrolling themselves; page gestures must not steal clicks.
        page->addCentralWidget(content, true, false);
        page->setBottomCustomWidget(footer);
        return page;
    }
#endif
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->addWidget(header);
    layout->addWidget(content, 1);
    layout->addWidget(footer);
    return page;
}

QToolBar *toolBar(QWidget *parent)
{
    QToolBar *bar = nullptr;
#ifdef PINLOOM_ENABLE_ELA
    if (Ui::usesEla()) {
        auto *ela = new LibraryToolBar(parent);
        ela->setToolBarSpacing(6);
        ela->setProperty("pinloomElaControl", true);
        bar = ela;
    }
#endif
    if (!bar) bar = new QToolBar(parent);
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setAccessibleName(QObject::tr("Library actions"));
    return bar;
}

void applyIcons(QWidget *window)
{
#ifdef PINLOOM_ENABLE_ELA
    if (!Ui::usesEla()) return;
    // The vendored Ela enum also contains Pro/private glyphs. These are the
    // verified Unicode mappings in the bundled Font Awesome Free Solid font.
    const QList<QPair<QString, char32_t>> icons = {
        {"anchorLibraryRefreshButton", 0xf021},
        {"anchorLibraryTrashButton", 0xf1f8},
        {"anchorLibraryRestoreButton", 0xf0e2},
        {"anchorLibraryRelinkButton", 0xf0c1},
        {"anchorLibraryUndoButton", 0xf0e2},
        {"anchorLibrarySaveButton", 0xf0c7},
        {"anchorLibraryColumnsButton", 0xf0db},
        {"anchorLibraryOpenButton", 0xf08e},
        {"anchorLibraryPreviewButton", 0xf065}};
    for (const auto &entry : icons) {
        if (auto *button = window->findChild<ElaToolButton *>(entry.first)) {
            button->setElaIcon(static_cast<ElaIconType::IconName>(entry.second));
            button->setToolButtonStyle(button->text().isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        }
    }
#else
    Q_UNUSED(window);
#endif
}

#ifdef PINLOOM_ENABLE_ELA
namespace {
class Confirmation final : public ElaContentDialog {
public:
    explicit Confirmation(QWidget *parent) : ElaContentDialog(parent) {
        const auto buttons = findChildren<QPushButton *>();
        // Ela's stock callbacks post context-free signals after closing. Use QObject-bound
        // callbacks so a synchronous modal call can destroy its dialog safely.
        Q_ASSERT(buttons.size() == 3);
        for (auto *button : buttons) {
            disconnect(button, nullptr, this, nullptr);
            button->setAutoDefault(false);
        }
        buttons[0]->setText(tr("Cancel"));
        buttons[0]->setAccessibleName(tr("Cancel"));
        buttons[0]->setObjectName(QStringLiteral("anchorLibraryConfirmCancel"));
        buttons[0]->setDefault(true);
        buttons[0]->setFocus();
        buttons[1]->hide();
        buttons[2]->setText(tr("Confirm"));
        buttons[2]->setAccessibleName(tr("Confirm"));
        buttons[2]->setObjectName(QStringLiteral("anchorLibraryConfirmAccept"));
        connect(buttons[0], &QPushButton::clicked, this, &QDialog::reject);
        connect(buttons[2], &QPushButton::clicked, this, &QDialog::accept);
    }
protected:
    void keyPressEvent(QKeyEvent *event) override { QDialog::keyPressEvent(event); }
};
}
#endif

bool confirm(QWidget *parent, const QString &title, const QString &message)
{
#ifdef PINLOOM_ENABLE_ELA
    if (Ui::usesEla()) {
        const auto existingChildren = parent->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
        Confirmation dialog(parent);
        dialog.setObjectName(QStringLiteral("anchorLibraryConfirmation"));
        dialog.setWindowTitle(title);
        dialog.setAccessibleName(title);
        for (auto *child : parent->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (child->inherits("ElaMaskWidget") && !existingChildren.contains(child))
                QObject::connect(&dialog, &QDialog::finished, child, &QWidget::hide);
        }
        auto *content = new QWidget(&dialog);
        auto *layout = new QVBoxLayout(content);
        layout->setContentsMargins(24, 24, 24, 20);
        layout->addWidget(text(title, content, true));
        auto *detail = text(message, content);
        detail->setAccessibleName(message);
        layout->addWidget(detail);
        dialog.setCentralWidget(content);
        dialog.resize(qMin(520, qMax(320, parent->width() - 48)), 240);
        return dialog.exec() == QDialog::Accepted;
    }
#endif
    Ui::Dialog dialog(parent);
    dialog.setObjectName(QStringLiteral("anchorLibraryConfirmation"));
    dialog.setWindowTitle(title);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(text(message, &dialog));
    auto *buttons = new Ui::DialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("anchorLibraryConfirmAccept"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("anchorLibraryConfirmCancel"));
    buttons->button(QDialogButtonBox::Ok)->setDefault(false);
    buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    return dialog.exec() == QDialog::Accepted;
}
}
