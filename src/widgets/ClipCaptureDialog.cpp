#include "pinloom/widgets/PinloomUiControls.h"
#include "pinloom/widgets/ClipCaptureDialog.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QDialogButtonBox>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include "pinloom/widgets/PinloomItemViews.h"
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace Pinloom {

namespace {

QString normalizedTag(QString tag)
{
    tag = tag.trimmed();
    while (tag.startsWith(QLatin1Char('#'))) {
        tag.remove(0, 1);
        tag = tag.trimmed();
    }
    return tag;
}

void appendUniqueTag(QStringList &tags, const QString &value)
{
    const QString tag = normalizedTag(value);
    if (!tag.isEmpty() && !tags.contains(tag, Qt::CaseInsensitive)) {
        tags.append(tag);
    }
}

QColor clipTagColor(const QString &tag)
{
    const uint hue = qHash(tag.toCaseFolded()) % 360U;
    return QColor::fromHsv(static_cast<int>(hue), 78, 232).lighter(150);
}

} // namespace

ClipCaptureDialog::ClipCaptureDialog(const QString &selectedText,
                                     const QString &suggestedName,
                                     const QStringList &availableTags,
                                     QWidget *parent)
    : Ui::Dialog(parent)
{
    setObjectName(QStringLiteral("clipCaptureDialog"));
    setProperty("pinloomRole", QStringLiteral("canvas"));
    setWindowTitle(tr("Save Clip"));
    setWindowModality(Qt::ApplicationModal);
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    setMinimumWidth(480);
    resize(520, 330);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto *title = Pinloom::Ui::label(tr("Save selected text"), this);
    title->setObjectName(QStringLiteral("clipCaptureTitle"));
    title->setProperty("pinloomTextRole", QStringLiteral("title"));
    root->addWidget(title);

    previewEdit_ = Ui::plainTextEdit(selectedText, this);
    previewEdit_->setObjectName(QStringLiteral("clipCapturePreview"));
    previewEdit_->setProperty("pinloomRole", QStringLiteral("raised"));
    previewEdit_->setReadOnly(true);
    previewEdit_->setMaximumHeight(140);
    root->addWidget(previewEdit_);

    auto *form = new Pinloom::Ui::FormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    nameEdit_ = Pinloom::Ui::lineEdit(suggestedName, this);
    nameEdit_->setObjectName(QStringLiteral("clipCaptureNameEdit"));
    nameEdit_->setClearButtonEnabled(true);
    for (const QString &tag : availableTags) {
        appendUniqueTag(availableTags_, tag);
    }
    availableTags_.sort(Qt::CaseInsensitive);
    tagsButton_ = Pinloom::Ui::toolButton(this);
    tagsButton_->setObjectName(QStringLiteral("clipCaptureTagsButton"));
    tagsButton_->setArrowType(Qt::DownArrow);
    tagsButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tagsButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tagsButton_->setMinimumHeight(32);
    form->addRow(tr("Name"), nameEdit_);
    form->addRow(tr("Tags"), tagsButton_);
    root->addLayout(form);

    buttons_ = new Pinloom::Ui::DialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons_->setObjectName(QStringLiteral("clipCaptureButtons"));
    if (QPushButton *saveButton = buttons_->button(QDialogButtonBox::Save)) {
        saveButton->setProperty("pinloomControl", QStringLiteral("primary"));
    }
    root->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(tagsButton_, &QToolButton::clicked, this, &ClipCaptureDialog::openTagPicker);
    connect(nameEdit_, &QLineEdit::textChanged, this, [this](const QString &name) {
        if (QPushButton *saveButton = buttons_->button(QDialogButtonBox::Save)) {
            saveButton->setEnabled(!name.trimmed().isEmpty());
        }
    });

    updateTagButton();
    nameEdit_->selectAll();
    nameEdit_->setFocus(Qt::PopupFocusReason);
}

ClipCaptureMetadata ClipCaptureDialog::metadata() const
{
    ClipCaptureMetadata metadata;
    metadata.name = nameEdit_->text();
    metadata.tags = selectedTags_;
    return metadata;
}

void ClipCaptureDialog::setName(const QString &name)
{
    nameEdit_->setText(name);
}

void ClipCaptureDialog::setTags(const QStringList &tags)
{
    selectedTags_.clear();
    for (const QString &tag : tags) {
        appendUniqueTag(selectedTags_, tag);
        appendUniqueTag(availableTags_, tag);
    }
    availableTags_.sort(Qt::CaseInsensitive);
    updateTagButton();
}

void ClipCaptureDialog::openTagPicker()
{
    if (tagPickerPopup_) {
        tagPickerPopup_->close();
    }

    auto *popup = Ui::popupFrame(this);
    popup->setObjectName(QStringLiteral("clipCaptureTagPicker"));
    popup->setAccessibleName(tr("Clip tags"));
    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    auto *queryRow = new QHBoxLayout;
    queryRow->setContentsMargins(0, 0, 0, 0);
    auto *query = Pinloom::Ui::lineEdit(popup);
    query->setObjectName(QStringLiteral("clipCaptureTagFilter"));
    query->setPlaceholderText(tr("Filter or create a tag"));
    query->setClearButtonEnabled(true);
    auto *create = Pinloom::Ui::toolButton(popup);
    create->setObjectName(QStringLiteral("clipCaptureCreateTagButton"));
    create->setProperty("pinloomControl", QStringLiteral("icon"));
    create->setText(QStringLiteral("+"));
    create->setToolTip(tr("Create and select this tag"));
    queryRow->addWidget(query, 1);
    queryRow->addWidget(create);

    auto *list = new Pinloom::Ui::List(popup);
    list->setObjectName(QStringLiteral("clipCaptureTagList"));
    list->setSelectionMode(QAbstractItemView::NoSelection);
    for (const QString &tag : availableTags_) {
        auto *item = new Pinloom::Ui::ListItem(tag, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(selectedTags_.contains(tag, Qt::CaseInsensitive)
                                ? Qt::Checked
                                : Qt::Unchecked);
        item->setBackground(clipTagColor(tag));
    }
    layout->addLayout(queryRow);
    layout->addWidget(list, 1);

    const auto updateCreateState = [query, create, list]() {
        const QString candidate = normalizedTag(query->text());
        bool duplicate = false;
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->text().compare(candidate, Qt::CaseInsensitive) == 0) {
                duplicate = true;
                break;
            }
        }
        create->setEnabled(!candidate.isEmpty() && !duplicate);
    };
    connect(query, &QLineEdit::textChanged, popup, [list, updateCreateState](const QString &text) {
        const QString needle = normalizedTag(text);
        for (int row = 0; row < list->count(); ++row) {
            list->item(row)->setHidden(!needle.isEmpty()
                                       && !list->item(row)->text().contains(needle,
                                                                           Qt::CaseInsensitive));
        }
        updateCreateState();
    });
    connect(list, &Pinloom::Ui::List::itemChanged, popup, [this, list](Pinloom::Ui::ListItem *) {
        selectedTags_.clear();
        for (int row = 0; row < list->count(); ++row) {
            if (list->item(row)->checkState() == Qt::Checked) {
                appendUniqueTag(selectedTags_, list->item(row)->text());
            }
        }
        updateTagButton();
    });
    connect(create, &QToolButton::clicked, popup, [this, query, list, updateCreateState]() {
        const QString tag = normalizedTag(query->text());
        if (tag.isEmpty() || availableTags_.contains(tag, Qt::CaseInsensitive)) {
            return;
        }
        appendUniqueTag(availableTags_, tag);
        auto *item = new Pinloom::Ui::ListItem(tag, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setBackground(clipTagColor(tag));
        item->setCheckState(Qt::Checked);
        query->clear();
        updateCreateState();
    });
    connect(query, &QLineEdit::returnPressed, create, &QToolButton::click);
    updateCreateState();

    tagPickerPopup_ = popup;
    connect(popup, &QObject::destroyed, this, [this, popup]() {
        if (tagPickerPopup_ == popup) {
            tagPickerPopup_ = nullptr;
        }
    });

    popup->resize(std::max(320, tagsButton_->width()), 260);
    QPoint position = tagsButton_->mapToGlobal(QPoint(0, tagsButton_->height()));
    if (QScreen *screen = QApplication::screenAt(position)) {
        const QRect available = screen->availableGeometry();
        position.setX(std::clamp(position.x(),
                                 available.left(),
                                 std::max(available.left(), available.right() - popup->width() + 1)));
        if (position.y() + popup->height() > available.bottom() + 1) {
            position.setY(tagsButton_->mapToGlobal(QPoint(0, 0)).y() - popup->height());
        }
        position.setY(std::clamp(position.y(),
                                 available.top(),
                                 std::max(available.top(), available.bottom() - popup->height() + 1)));
    }
    popup->move(position);
    popup->show();
    query->setFocus(Qt::PopupFocusReason);
}

void ClipCaptureDialog::updateTagButton()
{
    QStringList labels;
    for (const QString &tag : selectedTags_) {
        labels.append(QStringLiteral("#%1").arg(tag));
    }
    const QString fullText = labels.join(QStringLiteral("  "));
    tagsButton_->setToolTip(fullText);
    tagsButton_->setText(fullText.isEmpty()
                             ? tr("Select tags")
                             : tagsButton_->fontMetrics().elidedText(fullText,
                                                                     Qt::ElideRight,
                                                                     std::max(220, tagsButton_->width() - 36)));
}

} // namespace Pinloom
