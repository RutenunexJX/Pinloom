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
#include <QListWidget>
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
    : QDialog(parent)
{
    setObjectName(QStringLiteral("clipCaptureDialog"));
    setWindowTitle(tr("Save Clip"));
    setWindowModality(Qt::ApplicationModal);
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    setMinimumWidth(480);
    resize(520, 330);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(10);

    auto *title = new QLabel(tr("Save selected text"), this);
    title->setObjectName(QStringLiteral("clipCaptureTitle"));
    root->addWidget(title);

    previewEdit_ = new QPlainTextEdit(selectedText, this);
    previewEdit_->setObjectName(QStringLiteral("clipCapturePreview"));
    previewEdit_->setReadOnly(true);
    previewEdit_->setMaximumHeight(140);
    root->addWidget(previewEdit_);

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(9);
    nameEdit_ = new QLineEdit(suggestedName, this);
    nameEdit_->setObjectName(QStringLiteral("clipCaptureNameEdit"));
    nameEdit_->setClearButtonEnabled(true);
    for (const QString &tag : availableTags) {
        appendUniqueTag(availableTags_, tag);
    }
    availableTags_.sort(Qt::CaseInsensitive);
    tagsButton_ = new QToolButton(this);
    tagsButton_->setObjectName(QStringLiteral("clipCaptureTagsButton"));
    tagsButton_->setArrowType(Qt::DownArrow);
    tagsButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tagsButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tagsButton_->setMinimumHeight(32);
    form->addRow(tr("Name"), nameEdit_);
    form->addRow(tr("Tags"), tagsButton_);
    root->addLayout(form);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons_->setObjectName(QStringLiteral("clipCaptureButtons"));
    root->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(tagsButton_, &QToolButton::clicked, this, &ClipCaptureDialog::openTagPicker);
    connect(nameEdit_, &QLineEdit::textChanged, this, [this](const QString &name) {
        if (QPushButton *saveButton = buttons_->button(QDialogButtonBox::Save)) {
            saveButton->setEnabled(!name.trimmed().isEmpty());
        }
    });

    setStyleSheet(QStringLiteral(
        "QDialog#clipCaptureDialog { background: #eef4f2; color: #182421; }"
        "QLabel#clipCaptureTitle { color: #0b4e47; font-size: 17px; font-weight: 600; }"
        "QLineEdit, QPlainTextEdit, QToolButton#clipCaptureTagsButton { background: #ffffff; border: 1px solid #a8bbb5; border-radius: 4px; padding: 6px 8px; }"
        "QLineEdit:focus, QPlainTextEdit:focus, QToolButton#clipCaptureTagsButton:focus { border: 2px solid #0f766e; }"
        "QPlainTextEdit#clipCapturePreview { color: #344b46; }"
        "QPushButton { min-width: 84px; min-height: 30px; }"));

    updateTagButton();
    nameEdit_->selectAll();
    nameEdit_->setFocus(Qt::PopupFocusReason);
}

ClipCaptureMetadata ClipCaptureDialog::metadata() const
{
    ClipCaptureMetadata metadata;
    metadata.name = nameEdit_->text().trimmed();
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

    auto *popup = new QFrame(this, Qt::Popup);
    popup->setObjectName(QStringLiteral("clipCaptureTagPicker"));
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setFrameShape(QFrame::StyledPanel);
    popup->setStyleSheet(QStringLiteral(
        "QFrame#clipCaptureTagPicker { background: #f7fbfa; border: 1px solid #8eaaa3; }"
        "QLineEdit { background: white; border: 1px solid #9db4ae; padding: 5px 7px; }"
        "QListWidget { background: white; border: 1px solid #b4c6c1; outline: 0; }"
        "QListWidget::item { padding: 5px 7px; }"
        "QToolButton { min-width: 30px; min-height: 30px; font-weight: 700; }"));

    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    auto *queryRow = new QHBoxLayout;
    queryRow->setContentsMargins(0, 0, 0, 0);
    auto *query = new QLineEdit(popup);
    query->setObjectName(QStringLiteral("clipCaptureTagFilter"));
    query->setPlaceholderText(tr("Filter or create a tag"));
    query->setClearButtonEnabled(true);
    auto *create = new QToolButton(popup);
    create->setObjectName(QStringLiteral("clipCaptureCreateTagButton"));
    create->setText(QStringLiteral("+"));
    create->setToolTip(tr("Create and select this tag"));
    queryRow->addWidget(query, 1);
    queryRow->addWidget(create);

    auto *list = new QListWidget(popup);
    list->setObjectName(QStringLiteral("clipCaptureTagList"));
    list->setSelectionMode(QAbstractItemView::NoSelection);
    for (const QString &tag : availableTags_) {
        auto *item = new QListWidgetItem(tag, list);
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
    connect(list, &QListWidget::itemChanged, popup, [this, list](QListWidgetItem *) {
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
        auto *item = new QListWidgetItem(tag, list);
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
