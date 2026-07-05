#include "pinloom/widgets/ClipPickerPanel.h"

#include "pinloom/clip/ClipInsertionService.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QScrollBar>
#include <QSize>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace Pinloom {

namespace {

enum ClipResultRole {
    ClipIdRole = Qt::UserRole,
    DisplayNameRole,
    PreviewRole,
    MatchedFieldRole,
    MatchedValueRole,
    ScoreRole,
    RankRole,
    StateRole,
    TagsRole,
    AliasesRole,
    PinnedRole,
    CreatedAtRole,
    UpdatedAtRole,
    UsedAtRole
};

QString compactValue(QString value, int maxLength = 96)
{
    value = value.simplified();
    if (value.size() <= maxLength) {
        return value;
    }
    return value.left(std::max(0, maxLength - 3)) + QStringLiteral("...");
}

QString stateLabel(ClipState state)
{
    switch (state) {
    case ClipState::Saved:
        return QStringLiteral("saved");
    case ClipState::Temporary:
        return QStringLiteral("temporary");
    }
    return QStringLiteral("saved");
}

QString tagsText(const QStringList &tags)
{
    QStringList cleanedTags;
    for (const QString &tag : tags) {
        const QString trimmed = tag.trimmed();
        if (!trimmed.isEmpty()) {
            cleanedTags.append(QStringLiteral("#%1").arg(trimmed));
        }
    }
    return cleanedTags.join(QLatin1Char(' '));
}

QString aliasesText(const QStringList &aliases)
{
    QStringList cleanedAliases;
    for (const QString &alias : aliases) {
        const QString trimmed = alias.trimmed();
        if (!trimmed.isEmpty()) {
            cleanedAliases.append(trimmed);
        }
    }
    return cleanedAliases.join(QStringLiteral(", "));
}

void appendUniqueValue(QStringList &values, const QString &value)
{
    const QString trimmed = value.trimmed();
    if (!trimmed.isEmpty() && !values.contains(trimmed, Qt::CaseInsensitive)) {
        values.append(trimmed);
    }
}

QString cleanTag(QString tag)
{
    tag = tag.trimmed();
    while (tag.startsWith(QLatin1Char('#'))) {
        tag.remove(0, 1);
        tag = tag.trimmed();
    }
    return tag;
}

QStringList valuesFromCommaText(const QString &text, bool tags = false)
{
    QStringList values;
    for (const QString &value : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        appendUniqueValue(values, tags ? cleanTag(value) : value);
    }
    return values;
}

QStringList cleanedValues(const QStringList &source, bool tags = false)
{
    QStringList values;
    for (const QString &value : source) {
        appendUniqueValue(values, tags ? cleanTag(value) : value);
    }
    return values;
}

QString matchText(const ClipSearchResult &result)
{
    if (result.matchedField.isEmpty()) {
        return {};
    }
    if (result.matchedValue.trimmed().isEmpty()) {
        return QStringLiteral("match: %1").arg(result.matchedField);
    }
    return QStringLiteral("match: %1=%2").arg(result.matchedField, compactValue(result.matchedValue, 48));
}

QString resultItemText(const ClipSearchResult &result)
{
    QStringList metadata;
    metadata.append(stateLabel(result.state));

    const QString match = matchText(result);
    if (!match.isEmpty()) {
        metadata.append(match);
    }

    const QString tags = tagsText(result.tags);
    if (!tags.isEmpty()) {
        metadata.append(tags);
    }

    const QString aliases = aliasesText(result.aliases);
    if (!aliases.isEmpty()) {
        metadata.append(QStringLiteral("aliases: %1").arg(compactValue(aliases, 64)));
    }

    if (result.pinned) {
        metadata.append(QStringLiteral("pinned"));
    }

    metadata.append(QStringLiteral("rank %1").arg(result.rank));
    metadata.append(QStringLiteral("score %1").arg(QString::number(result.score, 'f', 1)));

    const QString displayName = compactValue(result.displayName.trimmed().isEmpty() ? result.preview : result.displayName,
                                             84);
    const QString preview = compactValue(result.preview, 96);
    return QStringLiteral("%1%2\n%3%4%5")
        .arg(result.pinned ? QStringLiteral("[Pinned] ") : QString(),
             displayName,
             preview,
             preview.isEmpty() ? QString() : QStringLiteral(" | "),
             metadata.join(QStringLiteral(" | ")));
}

QString dateText(const QDateTime &dateTime)
{
    return dateTime.isValid() ? dateTime.toUTC().toString(Qt::ISODateWithMs) : QStringLiteral("-");
}

QString resultToolTip(const ClipSearchResult &result)
{
    QStringList lines;
    lines.append(QStringLiteral("Clip: %1").arg(result.clipId));
    lines.append(QStringLiteral("Name: %1").arg(result.displayName));
    lines.append(QStringLiteral("Preview: %1").arg(result.preview));
    lines.append(QStringLiteral("State: %1").arg(stateLabel(result.state)));
    lines.append(QStringLiteral("Match: %1=%2").arg(result.matchedField, result.matchedValue));
    lines.append(QStringLiteral("Tags: %1").arg(tagsText(result.tags)));
    lines.append(QStringLiteral("Aliases: %1").arg(aliasesText(result.aliases)));
    lines.append(QStringLiteral("Pinned: %1").arg(result.pinned ? QStringLiteral("yes") : QStringLiteral("no")));
    lines.append(QStringLiteral("Rank: %1").arg(result.rank));
    lines.append(QStringLiteral("Score: %1").arg(QString::number(result.score, 'f', 1)));
    lines.append(QStringLiteral("Created: %1").arg(dateText(result.createdAt)));
    lines.append(QStringLiteral("Updated: %1").arg(dateText(result.updatedAt)));
    lines.append(QStringLiteral("Used: %1").arg(dateText(result.usedAt)));
    return lines.join(QLatin1Char('\n'));
}

ClipSearchResult resultFromItem(const QListWidgetItem *item)
{
    ClipSearchResult result;
    if (!item) {
        return result;
    }

    result.clipId = item->data(ClipIdRole).toString();
    result.displayName = item->data(DisplayNameRole).toString();
    result.preview = item->data(PreviewRole).toString();
    result.matchedField = item->data(MatchedFieldRole).toString();
    result.matchedValue = item->data(MatchedValueRole).toString();
    result.score = item->data(ScoreRole).toDouble();
    result.rank = item->data(RankRole).toInt();
    result.state = static_cast<ClipState>(item->data(StateRole).toInt());
    result.tags = item->data(TagsRole).toStringList();
    result.aliases = item->data(AliasesRole).toStringList();
    result.pinned = item->data(PinnedRole).toBool();
    result.createdAt = item->data(CreatedAtRole).toDateTime();
    result.updatedAt = item->data(UpdatedAtRole).toDateTime();
    result.usedAt = item->data(UsedAtRole).toDateTime();
    return result;
}

void storeResult(QListWidgetItem *item, const ClipSearchResult &result)
{
    item->setData(ClipIdRole, result.clipId);
    item->setData(DisplayNameRole, result.displayName);
    item->setData(PreviewRole, result.preview);
    item->setData(MatchedFieldRole, result.matchedField);
    item->setData(MatchedValueRole, result.matchedValue);
    item->setData(ScoreRole, result.score);
    item->setData(RankRole, result.rank);
    item->setData(StateRole, static_cast<int>(result.state));
    item->setData(TagsRole, result.tags);
    item->setData(AliasesRole, result.aliases);
    item->setData(PinnedRole, result.pinned);
    item->setData(CreatedAtRole, result.createdAt);
    item->setData(UpdatedAtRole, result.updatedAt);
    item->setData(UsedAtRole, result.usedAt);
}

} // namespace

ClipPickerInsertionHandler makeClipPickerInsertionHandler(ClipInsertionService &service)
{
    return [&service](const QString &clipId, QString *error) {
        const ClipInsertionResult result = service.insertClip(clipId);
        if (!result.inserted() && error) {
            *error = result.error;
        }
        return result.inserted();
    };
}

ClipPickerPanel::ClipPickerPanel(ClipSearchService &searchService, QWidget *parent)
    : ClipPickerPanel(searchService, ClipPickerOptions{}, parent)
{
}

ClipPickerPanel::ClipPickerPanel(ClipSearchService &searchService, ClipPickerOptions options, QWidget *parent)
    : QWidget(parent)
    , searchService_(searchService)
    , options_(std::move(options))
{
    setObjectName(QStringLiteral("clipPickerPanel"));
    setWindowTitle(tr("Pinloom Clip"));
    resize(620, 360);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 8);
    layout->setSpacing(6);

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setObjectName(QStringLiteral("clipPickerSearchEdit"));
    searchEdit_->setPlaceholderText(tr("Search clips, aliases, #tags"));
    searchEdit_->setClearButtonEnabled(true);

    saveButton_ = new QPushButton(tr("Save Clip"), this);
    saveButton_->setObjectName(QStringLiteral("clipPickerSaveButton"));

    resultList_ = new QListWidget(this);
    resultList_->setObjectName(QStringLiteral("clipPickerResultList"));
    resultList_->setAlternatingRowColors(true);
    resultList_->setUniformItemSizes(true);
    resultList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("clipPickerStatusLabel"));
    statusLabel_->setWordWrap(true);

    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->addWidget(searchEdit_, 1);
    toolbar->addWidget(saveButton_);

    layout->addLayout(toolbar);
    layout->addWidget(resultList_, 1);
    layout->addWidget(statusLabel_);

    searchEdit_->installEventFilter(this);
    resultList_->installEventFilter(this);

    connect(searchEdit_, &QLineEdit::textChanged, this, &ClipPickerPanel::refreshResults);
    connect(saveButton_, &QPushButton::clicked, this, &ClipPickerPanel::promptSaveCurrentClip);
    connect(resultList_, &QListWidget::currentItemChanged, this, &ClipPickerPanel::notifyCurrentResultChanged);
    connect(resultList_, &QListWidget::itemActivated, this, &ClipPickerPanel::activateItem);
    connect(resultList_, &QListWidget::itemDoubleClicked, this, &ClipPickerPanel::activateItem);

    refreshResults();
    focusSearch();
}

void ClipPickerPanel::setQuery(const QString &query)
{
    searchEdit_->setText(query);
}

QString ClipPickerPanel::query() const
{
    return searchEdit_->text();
}

void ClipPickerPanel::focusSearch()
{
    searchEdit_->setFocus(Qt::ShortcutFocusReason);
    searchEdit_->selectAll();
}

void ClipPickerPanel::setSearchOptions(const ClipSearchOptions &options)
{
    options_.searchOptions = options;
    refreshResults();
}

ClipSearchOptions ClipPickerPanel::searchOptions() const
{
    return options_.searchOptions;
}

void ClipPickerPanel::setInsertionHandler(ClipPickerInsertionHandler handler)
{
    options_.insertionHandler = std::move(handler);
}

void ClipPickerPanel::setCloseOnActivationSuccess(bool closeOnSuccess)
{
    options_.closeOnActivationSuccess = closeOnSuccess;
}

bool ClipPickerPanel::closeOnActivationSuccess() const
{
    return options_.closeOnActivationSuccess;
}

ClipSearchResult ClipPickerPanel::currentResult() const
{
    return resultFromItem(resultList_->currentItem());
}

ClipSearchResult ClipPickerPanel::resultAt(int row) const
{
    if (row < 0 || row >= resultList_->count()) {
        return {};
    }
    return resultFromItem(resultList_->item(row));
}

QList<ClipSearchResult> ClipPickerPanel::currentResults() const
{
    QList<ClipSearchResult> results;
    results.reserve(resultList_->count());
    for (int row = 0; row < resultList_->count(); ++row) {
        results.append(resultFromItem(resultList_->item(row)));
    }
    return results;
}

int ClipPickerPanel::resultCount() const
{
    return resultList_->count();
}

bool ClipPickerPanel::selectResultAt(int row)
{
    if (row < 0 || row >= resultList_->count()) {
        return false;
    }
    resultList_->setCurrentRow(row);
    return resultList_->currentItem() != nullptr;
}

bool ClipPickerPanel::selectFirstResult()
{
    return selectResultAt(0);
}

bool ClipPickerPanel::selectNextResult()
{
    const int count = resultList_->count();
    if (count <= 0) {
        return false;
    }

    const int currentRow = resultList_->currentRow();
    const int nextRow = currentRow < 0 ? 0 : std::min(currentRow + 1, count - 1);
    return selectResultAt(nextRow);
}

bool ClipPickerPanel::selectPreviousResult()
{
    const int count = resultList_->count();
    if (count <= 0) {
        return false;
    }

    const int currentRow = resultList_->currentRow();
    const int previousRow = currentRow < 0 ? 0 : std::max(currentRow - 1, 0);
    return selectResultAt(previousRow);
}

bool ClipPickerPanel::activateCurrentResult()
{
    const ClipSearchResult result = currentResult();
    if (result.clipId.trimmed().isEmpty()) {
        lastActivationSucceeded_ = false;
        lastError_ = tr("No clip selected");
        updateStatus(lastError_);
        emit activationFailed({}, lastError_);
        return false;
    }

    if (!options_.insertionHandler) {
        lastActivationSucceeded_ = false;
        lastError_ = tr("Insertion handler is required");
        updateStatus(lastError_);
        emit activationFailed(result.clipId, lastError_);
        return false;
    }

    QString error;
    if (!options_.insertionHandler(result.clipId, &error)) {
        lastActivationSucceeded_ = false;
        lastError_ = error.trimmed().isEmpty() ? tr("Insertion failed") : error.trimmed();
        updateStatus(lastError_);
        emit activationFailed(result.clipId, lastError_);
        return false;
    }

    lastActivationSucceeded_ = true;
    lastError_.clear();
    updateStatus(tr("Inserted clip"));
    emit activated(result.clipId);
    if (options_.closeOnActivationSuccess) {
        close();
    }
    return true;
}

bool ClipPickerPanel::saveCurrentClipAsSaved(const QString &name,
                                             const QStringList &aliases,
                                             const QStringList &tags,
                                             bool pinned)
{
    const ClipSearchResult result = currentResult();
    if (result.clipId.trimmed().isEmpty()) {
        lastError_ = tr("No clip selected");
        updateStatus(lastError_);
        return false;
    }

    const std::optional<Clip> clip = searchService_.findClip(result.clipId);
    if (!clip.has_value()) {
        lastError_ = tr("Clip not found");
        updateStatus(lastError_);
        return false;
    }

    if (clip->state == ClipState::Saved) {
        lastError_ = tr("Clip is already saved");
        updateStatus(lastError_);
        return false;
    }

    if (!searchService_.saveClip(result.clipId,
                                 name,
                                 cleanedValues(aliases),
                                 cleanedValues(tags, true),
                                 pinned)) {
        const QString repositoryError = searchService_.lastError().trimmed();
        lastError_ = repositoryError.isEmpty() ? tr("Unable to save clip") : repositoryError;
        updateStatus(lastError_);
        return false;
    }

    lastError_.clear();
    refreshResults();
    updateStatus(tr("Saved clip"));
    return itemForClipId(result.clipId) != nullptr;
}

QString ClipPickerPanel::statusText() const
{
    return statusText_;
}

QString ClipPickerPanel::lastError() const
{
    return lastError_;
}

bool ClipPickerPanel::lastActivationSucceeded() const
{
    return lastActivationSucceeded_;
}

bool ClipPickerPanel::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == searchEdit_ || watched == resultList_) && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const Qt::KeyboardModifiers modifiers =
            keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
        const int key = keyEvent->key();

        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && modifiers == Qt::NoModifier) {
            activateCurrentResult();
            return true;
        }
        if (key == Qt::Key_Down && modifiers == Qt::NoModifier) {
            if (selectNextResult()) {
                resultList_->scrollToItem(resultList_->currentItem());
            }
            return true;
        }
        if (key == Qt::Key_Up && modifiers == Qt::NoModifier) {
            if (selectPreviousResult()) {
                resultList_->scrollToItem(resultList_->currentItem());
            }
            return true;
        }
        if (key == Qt::Key_Escape && modifiers == Qt::NoModifier) {
            close();
            return true;
        }
        if (key == Qt::Key_S && modifiers == Qt::ControlModifier) {
            promptSaveCurrentClip();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void ClipPickerPanel::refreshResults()
{
    const QString previousClipId = currentResult().clipId;
    resultList_->clear();

    const QList<ClipSearchResult> results = searchService_.search(searchEdit_->text(), options_.searchOptions);
    for (const ClipSearchResult &result : results) {
        auto *item = new QListWidgetItem(resultItemText(result), resultList_);
        item->setToolTip(resultToolTip(result));
        item->setSizeHint(QSize(0, resultList_->fontMetrics().lineSpacing() * 2 + 10));
        storeResult(item, result);
    }

    QListWidgetItem *restoredItem = itemForClipId(previousClipId);
    if (restoredItem) {
        resultList_->setCurrentItem(restoredItem);
    } else if (resultList_->count() > 0) {
        resultList_->setCurrentRow(0);
    }

    updateStatus(tr("%n clip(s)", nullptr, results.size()));
    if (options_.resultsChangedHandler) {
        options_.resultsChangedHandler(currentResults());
    }
    emit resultsChanged();
    notifyCurrentResultChanged();
}

void ClipPickerPanel::activateItem(QListWidgetItem *item)
{
    if (!item) {
        return;
    }
    resultList_->setCurrentItem(item);
    activateCurrentResult();
}

void ClipPickerPanel::promptSaveCurrentClip()
{
    const ClipSearchResult result = currentResult();
    if (result.clipId.trimmed().isEmpty()) {
        lastError_ = tr("No clip selected");
        updateStatus(lastError_);
        return;
    }
    if (result.state == ClipState::Saved) {
        lastError_ = tr("Clip is already saved");
        updateStatus(lastError_);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Save Clip"));
    auto *form = new QFormLayout(&dialog);
    auto *nameEdit = new QLineEdit(result.displayName.trimmed().isEmpty() ? result.preview : result.displayName,
                                   &dialog);
    nameEdit->setObjectName(QStringLiteral("clipSaveNameEdit"));
    auto *aliasesEdit = new QLineEdit(&dialog);
    aliasesEdit->setObjectName(QStringLiteral("clipSaveAliasesEdit"));
    auto *tagsEdit = new QLineEdit(&dialog);
    tagsEdit->setObjectName(QStringLiteral("clipSaveTagsEdit"));
    auto *pinnedCheck = new QCheckBox(tr("Pinned"), &dialog);
    pinnedCheck->setObjectName(QStringLiteral("clipSavePinnedCheck"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName(QStringLiteral("clipSaveButtons"));

    form->addRow(tr("Name"), nameEdit);
    form->addRow(tr("Aliases"), aliasesEdit);
    form->addRow(tr("Tags"), tagsEdit);
    form->addRow(QString(), pinnedCheck);
    form->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        saveCurrentClipAsSaved(nameEdit->text(),
                               valuesFromCommaText(aliasesEdit->text()),
                               valuesFromCommaText(tagsEdit->text(), true),
                               pinnedCheck->isChecked());
    }
}

void ClipPickerPanel::notifyCurrentResultChanged()
{
    refreshSaveButtonState();
    if (options_.currentResultChangedHandler) {
        options_.currentResultChangedHandler(currentResult());
    }
    emit currentResultChanged();
}

void ClipPickerPanel::refreshSaveButtonState()
{
    if (!saveButton_) {
        return;
    }

    const ClipSearchResult result = currentResult();
    const bool canSave = !result.clipId.trimmed().isEmpty() && result.state == ClipState::Temporary;
    saveButton_->setEnabled(canSave);
    saveButton_->setToolTip(canSave
                                ? tr("Save the selected temporary clip with a name, aliases, and tags")
                                : tr("Select a temporary clip to save it"));
}

void ClipPickerPanel::updateStatus(const QString &status)
{
    statusText_ = status;
    statusLabel_->setText(statusText_);
    if (options_.statusChangedHandler) {
        options_.statusChangedHandler(statusText_);
    }
    emit statusChanged(statusText_);
}

QListWidgetItem *ClipPickerPanel::itemForClipId(const QString &clipId) const
{
    if (clipId.isEmpty()) {
        return nullptr;
    }

    for (int row = 0; row < resultList_->count(); ++row) {
        QListWidgetItem *item = resultList_->item(row);
        if (item->data(ClipIdRole).toString() == clipId) {
            return item;
        }
    }
    return nullptr;
}

} // namespace Pinloom
