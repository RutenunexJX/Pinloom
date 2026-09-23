#include "pinloom/widgets/PinloomUiControls.h"
#include "pinloom/widgets/ClipLibraryWindow.h"
#include "pinloom/clip/ClipSearch.h"
#include "pinloom/widgets/PinloomVisualTheme.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include "pinloom/widgets/PinloomItemViews.h"
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPainter>
#include <QRegularExpression>
#include <QScreen>
#include <QShowEvent>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QStyle>

#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace Pinloom {

namespace {

constexpr int ClipIdRole = Qt::UserRole + 1;
constexpr int TagValuesRole = Qt::UserRole + 2;
constexpr int InlineCellClean = 0;
constexpr int InlineCellDirty = 1;
constexpr int InlineCellSaved = 2;

enum ClipColumn {
    ClipNameColumn = 0,
    ClipAliasesColumn,
    ClipTagsColumn,
    ClipActionColumn,
    ClipMatchColumn,
    ClipUpdatedColumn,
    ClipPinnedColumn
};

QString inlineCellKey(const QString &clipId, int column)
{
    return clipId + (column == ClipAliasesColumn
                         ? QStringLiteral("|aliases")
                         : QStringLiteral("|tags"));
}

class ClipTagChipDelegate final : public QStyledItemDelegate {
public:
    explicit ClipTagChipDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem background(option);
        initStyleOption(&background, index);
        background.text.clear();
        const QWidget *widget = option.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &background, painter, widget);

        const QStringList tags = index.data(TagValuesRole).toStringList();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QFontMetrics metrics(option.font);
        const int chipHeight = metrics.height() + 6;
        const int left = option.rect.left() + 5;
        const int right = option.rect.right() - 5;
        int x = left;
        int y = option.rect.top() + 4;
        for (const QString &tag : tags) {
            const int width = std::min(right - left + 1, metrics.horizontalAdvance(tag) + 16);
            if (x != left && x + width > right) {
                x = left;
                y += chipHeight + 4;
            }
            const QRect chip(x, y, width, chipHeight);
            const QColor color = QColor::fromHsv(static_cast<int>(qHash(tag.toCaseFolded()) % 360U),
                                                  150,
                                                  220);
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            painter->drawRoundedRect(chip, 4, 4);
            const PinloomVisualTokens tokens = pinloomVisualTokens(activePinloomVisualScheme());
            painter->setPen(color.lightness() < 145 ? tokens.selectionText : tokens.text);
            painter->drawText(chip.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, tag);
            x += width + 5;
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QStringList tags = index.data(TagValuesRole).toStringList();
        if (tags.isEmpty()) {
            return QStyledItemDelegate::sizeHint(option, index);
        }
        const QFontMetrics metrics(option.font);
        const int available = std::max(80, option.rect.width() - 10);
        const int chipHeight = metrics.height() + 6;
        int lines = 1;
        int used = 0;
        for (const QString &tag : tags) {
            const int width = std::min(available, metrics.horizontalAdvance(tag) + 16);
            if (used > 0 && used + width + 5 > available) {
                ++lines;
                used = width;
            } else {
                used += (used > 0 ? 5 : 0) + width;
            }
        }
        return QSize(available + 10, lines * chipHeight + (lines - 1) * 4 + 8);
    }
};

QStringList valuesFromText(const QString &text, bool tags)
{
    QStringList values;
    const QStringList candidates = text.split(QRegularExpression(QStringLiteral("[,;\\n]")),
                                               Qt::SkipEmptyParts);
    for (QString value : candidates) {
        if (tags) {
            value = value.trimmed();
            while (value.startsWith(QLatin1Char('#'))) {
                value.remove(0, 1);
                value = value.trimmed();
            }
        }
        if (!value.trimmed().isEmpty()
            && (!tags || !values.contains(value, Qt::CaseInsensitive))) {
            values.append(value);
        }
    }
    return values;
}

QString tagsText(const QStringList &tags)
{
    QStringList values;
    for (const QString &tag : tags) {
        if (!tag.trimmed().isEmpty()) {
            values.append(QStringLiteral("#%1").arg(tag.trimmed()));
        }
    }
    return values.join(QLatin1Char(' '));
}

QString formattedBytes(qsizetype bytes)
{
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

QString defaultClipName(const Clip &clip)
{
    if (clip.state == ClipState::Temporary) {
        const QString source = clip.sourceApp.trimmed();
        return source.isEmpty()
            ? QStringLiteral("Clipboard item")
            : QStringLiteral("Clipboard item from %1").arg(source);
    }
    return QStringLiteral("Untitled Clip");
}

bool clipInScope(const Clip &clip, ClipLibraryScope scope)
{
    switch (scope) {
    case ClipLibraryScope::Saved:
        return clip.state == ClipState::Saved;
    case ClipLibraryScope::History:
        return clip.state == ClipState::Temporary;
    case ClipLibraryScope::Trash:
        return clip.state == ClipState::Deleted;
    }
    return false;
}

QDateTime sortTimestamp(const Clip &clip)
{
    if (clip.updatedAt.isValid()) {
        return clip.updatedAt;
    }
    return clip.createdAt;
}

QString displayName(const Clip &clip)
{
    return clip.name.trimmed().isEmpty() ? defaultClipName(clip) : clip.name;
}

QString actionDisplayName(ClipActionType action)
{
    return action == ClipActionType::OpenWebUrl
        ? QStringLiteral("Open URL")
        : QStringLiteral("Insert text");
}

QString storageDisplayName(ClipStorageBackend backend)
{
    return backend == ClipStorageBackend::Obsidian
        ? QStringLiteral("Obsidian")
        : QStringLiteral("Local database");
}

} // namespace

ClipLibraryWindow::ClipLibraryWindow(ClipLibraryWindowOptions options, QWidget *parent)
    : Ui::MainWindow(parent)
    , options_(std::move(options))
{
    setObjectName(QStringLiteral("clipLibraryWindow"));
    setProperty("pinloomRole", QStringLiteral("canvas"));
    setWindowTitle(tr("Pinloom Clip Library"));
    setMinimumSize(620, 460);
    resize(1180, 760);

    auto *central = new QWidget(this);
    central->setProperty("pinloomRole", QStringLiteral("canvas"));
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto *filters = new QHBoxLayout;
    filters->setSpacing(8);
    searchEdit_ = Pinloom::Ui::lineEdit(central);
    searchEdit_->setObjectName(QStringLiteral("clipLibrarySearchEdit"));
    searchEdit_->setAccessibleName(tr("Search Clips"));
    searchEdit_->setPlaceholderText(tr("Name or alias; use tag;name to filter by tag"));
    searchEdit_->setClearButtonEnabled(true);
    scopeCombo_ = Pinloom::Ui::comboBox(central);
    scopeCombo_->setObjectName(QStringLiteral("clipLibraryScopeCombo"));
    scopeCombo_->setAccessibleName(tr("Clip Library scope"));
    scopeCombo_->addItem(tr("Saved Clips"), static_cast<int>(ClipLibraryScope::Saved));
    scopeCombo_->addItem(tr("Clipboard History"), static_cast<int>(ClipLibraryScope::History));
    scopeCombo_->addItem(tr("Trash"), static_cast<int>(ClipLibraryScope::Trash));
    tagCombo_ = Pinloom::Ui::comboBox(central);
    tagCombo_->setObjectName(QStringLiteral("clipLibraryTagCombo"));
    tagCombo_->setAccessibleName(tr("Clip tag filter"));
    tagCombo_->setMinimumWidth(160);
    filters->addWidget(searchEdit_, 1);
    filters->addWidget(scopeCombo_);
    filters->addWidget(tagCombo_);
    root->addLayout(filters);

    auto *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setObjectName(QStringLiteral("clipLibrarySplitter"));
    splitter->setChildrenCollapsible(false);

    table_ = new Pinloom::Ui::Table(splitter);
    table_->setObjectName(QStringLiteral("clipLibraryTable"));
    table_->setAccessibleName(tr("Clip list"));
    table_->setAccessibleDescription(
        tr("Compact Clip metadata. Select a row to read the complete content in the preview pane."));
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels({tr("Name"),
                                       tr("Aliases"),
                                       tr("Tags"),
                                       tr("Action"),
                                       tr("Match"),
                                       tr("Updated"),
                                       tr("Pinned")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true);
    table_->setShowGrid(false);
    table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideRight);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(
        pinloomVisualMetrics().primaryControlHeight);
    table_->horizontalHeader()->setMinimumSectionSize(52);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(ClipNameColumn, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(ClipAliasesColumn, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(ClipTagsColumn, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(ClipActionColumn, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(ClipMatchColumn, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(ClipUpdatedColumn, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(ClipPinnedColumn, QHeaderView::ResizeToContents);
    table_->setColumnWidth(ClipAliasesColumn, 150);
    table_->setColumnWidth(ClipTagsColumn, 150);
    table_->setItemDelegateForColumn(ClipTagsColumn, new ClipTagChipDelegate(table_));
    table_->setContextMenuPolicy(Qt::CustomContextMenu);

    auto *preview = new QWidget(splitter);
    preview->setObjectName(QStringLiteral("clipLibraryPreviewPane"));
    preview->setProperty("pinloomRole", QStringLiteral("panel"));
    auto *previewLayout = new QVBoxLayout(preview);
    previewLayout->setContentsMargins(12, 4, 0, 0);
    previewLayout->setSpacing(8);
    previewTitle_ = Pinloom::Ui::label(tr("Select a Clip"), preview);
    previewTitle_->setObjectName(QStringLiteral("clipLibraryPreviewTitle"));
    previewTitle_->setProperty("pinloomTextRole", QStringLiteral("panelTitle"));
    previewTitle_->setWordWrap(true);
    previewMetadata_ = Pinloom::Ui::label(preview);
    previewMetadata_->setObjectName(QStringLiteral("clipLibraryPreviewMetadata"));
    previewMetadata_->setProperty("pinloomTextRole", QStringLiteral("metadata"));
    previewMetadata_->setWordWrap(true);
    previewText_ = Ui::plainTextEdit(preview);
    previewText_->setObjectName(QStringLiteral("clipLibraryPreview"));
    previewText_->setAccessibleName(tr("Selected Clip content"));
    previewText_->setReadOnly(true);
    previewText_->setPlaceholderText(tr("The complete Clip content appears here"));
    previewLayout->addWidget(previewTitle_);
    previewLayout->addWidget(previewMetadata_);
    previewLayout->addWidget(previewText_, 1);

    splitter->addWidget(table_);
    splitter->addWidget(preview);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    Ui::rememberSplitter(splitter, options_.settings, QStringLiteral("layout/clipLibrarySplitter"));
    root->addWidget(splitter, 1);

    statusLabel_ = Pinloom::Ui::label(central);
    statusLabel_->setObjectName(QStringLiteral("clipLibraryStatusLabel"));
    statusLabel_->setProperty("pinloomNotice", QStringLiteral("info"));
    statusLabel_->setAccessibleName(tr("Clip Library status"));
    root->addWidget(statusLabel_);
    setCentralWidget(central);

    connect(searchEdit_, &QLineEdit::textChanged, this, &ClipLibraryWindow::refreshRows);
    connect(scopeCombo_, &QComboBox::currentIndexChanged, this, [this]() {
        setProperty("trashMode", scope() == ClipLibraryScope::Trash);
        for (QWidget *widget : findChildren<QWidget *>()) {
            widget->style()->unpolish(widget);
            widget->style()->polish(widget);
            widget->update();
        }
        style()->unpolish(this);
        style()->polish(this);
        Ui::refreshViewPalettes(*qApp);
        rebuildTagFilter(clips_);
        refreshRows();
    });
    connect(tagCombo_, &QComboBox::currentIndexChanged, this, &ClipLibraryWindow::refreshRows);
    connect(table_, &Pinloom::Ui::Table::itemSelectionChanged, this, &ClipLibraryWindow::updatePreview);
    connect(table_, &Pinloom::Ui::Table::itemChanged, this, &ClipLibraryWindow::handleItemChanged);
    connect(table_, &Pinloom::Ui::Table::itemClicked, this, [this](Pinloom::Ui::TableItem *item) {
        if (item && item->column() == ClipTagsColumn && scope() != ClipLibraryScope::Trash) {
            openTagEditor(item->row());
        }
    });
    connect(table_, &Pinloom::Ui::Table::customContextMenuRequested, this, &ClipLibraryWindow::showContextMenu);
    connect(table_, &Pinloom::Ui::Table::itemDoubleClicked, this, [this](Pinloom::Ui::TableItem *item) {
        if (!item || scope() == ClipLibraryScope::Trash) {
            return;
        }
        if (item->column() == ClipAliasesColumn) {
            table_->editItem(item);
        } else if (item->column() == ClipTagsColumn) {
            openTagEditor(item->row());
        } else {
            editSelectedClip();
        }
    });

    refresh();
}

void ClipLibraryWindow::refresh()
{
    clips_ = options_.clipsProvider ? options_.clipsProvider() : QList<Clip>{};
    rebuildTagFilter(clips_);
    refreshRows();
}

void ClipLibraryWindow::setSearchText(const QString &text)
{
    searchEdit_->setText(text);
}

QString ClipLibraryWindow::searchText() const
{
    return searchEdit_->text();
}

void ClipLibraryWindow::setScope(ClipLibraryScope scope)
{
    const int index = scopeCombo_->findData(static_cast<int>(scope));
    if (index >= 0) {
        scopeCombo_->setCurrentIndex(index);
    }
}

ClipLibraryScope ClipLibraryWindow::scope() const
{
    return static_cast<ClipLibraryScope>(scopeCombo_->currentData().toInt());
}

int ClipLibraryWindow::visibleClipCount() const
{
    return table_->rowCount();
}

bool ClipLibraryWindow::selectClipAt(int row)
{
    if (row < 0 || row >= table_->rowCount()) {
        return false;
    }
    table_->selectRow(row);
    return true;
}

std::optional<Clip> ClipLibraryWindow::selectedClip() const
{
    const int row = table_->currentRow();
    if (row < 0 || !table_->item(row, 0)) {
        return std::nullopt;
    }
    const QString id = table_->item(row, 0)->data(ClipIdRole).toString();
    const auto found = std::find_if(clips_.cbegin(), clips_.cend(), [&id](const Clip &clip) {
        return clip.id == id;
    });
    if (found == clips_.cend()) {
        return std::nullopt;
    }

    Clip clip = *found;
    const auto pending = pendingEdits_.constFind(id);
    if (pending != pendingEdits_.constEnd()) {
        if (pending->aliasesDirty) {
            clip.aliases = pending->aliases;
        }
        if (pending->tagsDirty) {
            clip.tags = pending->tags;
        }
    }
    return clip;
}

QString ClipLibraryWindow::statusText() const
{
    return statusText_;
}

bool ClipLibraryWindow::editSelectedClip()
{
    const std::optional<Clip> selected = selectedClip();
    if (!selected.has_value() || selected->state == ClipState::Deleted || !options_.saveClipHandler) {
        setStatus(tr("Select a Saved Clip or clipboard-history item to edit"));
        return false;
    }

    Pinloom::Ui::Dialog dialog(this);
    dialog.setWindowTitle(selected->state == ClipState::Temporary ? tr("Save Clip") : tr("Edit Clip"));
    auto *form = new Pinloom::Ui::FormLayout(&dialog);
    auto *nameEdit = Pinloom::Ui::lineEdit(displayName(selected.value()), &dialog);
    auto *aliasesEdit = Pinloom::Ui::lineEdit(selected->aliases.join(QLatin1Char(',')), &dialog);
    auto *tagsEdit = Pinloom::Ui::lineEdit(selected->tags.join(QStringLiteral(", ")), &dialog);
    auto *pinnedCheck = Pinloom::Ui::checkBox(tr("Pinned"), &dialog);
    pinnedCheck->setChecked(selected->pinned);
    auto *buttons = new Pinloom::Ui::DialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    form->addRow(tr("Name"), nameEdit);
    form->addRow(tr("Aliases"), aliasesEdit);
    form->addRow(tr("Tags"), tagsEdit);
    form->addRow(QString(), pinnedCheck);
    form->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    Clip updated = selected.value();
    updated.state = ClipState::Saved;
    updated.name = nameEdit->text();
    updated.aliases = valuesFromText(aliasesEdit->text(), false);
    updated.tags = valuesFromText(tagsEdit->text(), true);
    updated.pinned = pinnedCheck->isChecked();
    const bool aliasesChanged = updated.aliases != selected->aliases;
    const bool tagsChanged = updated.tags != selected->tags;
    QString error;
    if (!options_.saveClipHandler(updated, &error)) {
        setStatus(error.trimmed().isEmpty() ? tr("Unable to save Clip") : error.trimmed());
        return false;
    }

    const QString id = updated.id;
    pendingEdits_.remove(id);
    if (aliasesChanged) {
        inlineCellStates_.insert(inlineCellKey(id, ClipAliasesColumn), InlineCellSaved);
    }
    if (tagsChanged) {
        inlineCellStates_.insert(inlineCellKey(id, ClipTagsColumn), InlineCellSaved);
    }
    refresh();
    setScope(ClipLibraryScope::Saved);
    reselectClip(id);
    setStatus(tr("Saved Clip \"%1\"").arg(displayName(updated)));
    return true;
}

bool ClipLibraryWindow::deleteSelectedClip(bool requireConfirmation)
{
    const std::optional<Clip> selected = selectedClip();
    if (!selected.has_value() || selected->state != ClipState::Saved || !options_.deleteClipHandler) {
        setStatus(tr("Select a Saved Clip to delete"));
        return false;
    }
    if (requireConfirmation
        && Pinloom::Ui::question(this,
                                 tr("Delete Saved Clip"),
                                 tr("Move \"%1\" to Clip Trash?").arg(displayName(selected.value())))
               != QMessageBox::Yes) {
        return false;
    }

    QString error;
    if (!options_.deleteClipHandler(selected->id, &error)) {
        setStatus(error.trimmed().isEmpty() ? tr("Unable to delete Clip") : error.trimmed());
        return false;
    }
    refresh();
    setStatus(tr("Moved Clip to Trash"));
    return true;
}

bool ClipLibraryWindow::restoreSelectedClip()
{
    const std::optional<Clip> selected = selectedClip();
    if (!selected.has_value() || selected->state != ClipState::Deleted || !options_.restoreClipHandler) {
        setStatus(tr("Select a deleted Clip to restore"));
        return false;
    }
    QString error;
    if (!options_.restoreClipHandler(selected->id, &error)) {
        setStatus(error.trimmed().isEmpty() ? tr("Unable to restore Clip") : error.trimmed());
        return false;
    }
    refresh();
    setStatus(tr("Restored Clip"));
    return true;
}

bool ClipLibraryWindow::permanentlyDeleteSelectedClip(bool requireConfirmation)
{
    const std::optional<Clip> selected = selectedClip();
    if (!selected.has_value() || selected->state != ClipState::Deleted
        || !options_.permanentlyDeleteClipHandler) {
        setStatus(tr("Select a deleted Clip to remove permanently"));
        return false;
    }
    if (requireConfirmation
        && Pinloom::Ui::warning(this,
                                tr("Permanently Remove Clip"),
                                tr("Permanently remove \"%1\" from Pinloom?\n\n"
                                   "An Obsidian-backed note is retained and marked as forgotten.")
                                    .arg(displayName(selected.value())),
                                QMessageBox::Yes | QMessageBox::Cancel,
                                QMessageBox::Cancel)
               != QMessageBox::Yes) {
        return false;
    }

    QString error;
    if (!options_.permanentlyDeleteClipHandler(selected->id, &error)) {
        setStatus(error.trimmed().isEmpty()
                      ? tr("Unable to permanently remove Clip")
                      : error.trimmed());
        return false;
    }
    pendingEdits_.remove(selected->id);
    refresh();
    setStatus(tr("Permanently removed Clip from Pinloom"));
    return true;
}

bool ClipLibraryWindow::openSelectedSource()
{
    const std::optional<Clip> selected = selectedClip();
    if (!selected.has_value() || selected->state == ClipState::Temporary
        || selected->storageBackend != ClipStorageBackend::Obsidian
        || !options_.openSourceHandler) {
        setStatus(tr("This Clip has no configured source note"));
        return false;
    }
    QString error;
    if (!options_.openSourceHandler(selected->id, &error)) {
        setStatus(error.trimmed().isEmpty() ? tr("Unable to open Clip source") : error.trimmed());
        return false;
    }
    setStatus(tr("Opened Clip source note"));
    return true;
}

void ClipLibraryWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    refresh();
    searchEdit_->setFocus(Qt::ActiveWindowFocusReason);
}

void ClipLibraryWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Save)) {
        savePendingEdits();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Find)) {
        searchEdit_->setFocus(Qt::ShortcutFocusReason);
        searchEdit_->selectAll();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete && event->modifiers() == Qt::NoModifier) {
        if (scope() == ClipLibraryScope::Saved) {
            deleteSelectedClip();
        } else if (scope() == ClipLibraryScope::Trash) {
            permanentlyDeleteSelectedClip();
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F2 && event->modifiers() == Qt::NoModifier) {
        editSelectedClip();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void ClipLibraryWindow::rebuildTagFilter(const QList<Clip> &clips)
{
    const QString selectedTag = tagCombo_->currentData().toString();
    QStringList tags;
    for (const Clip &clip : clips) {
        if (!clipInScope(clip, scope())) {
            continue;
        }
        for (const QString &tag : clip.tags) {
            if (!tag.trimmed().isEmpty() && !tags.contains(tag.trimmed(), Qt::CaseInsensitive)) {
                tags.append(tag.trimmed());
            }
        }
    }
    tags.sort(Qt::CaseInsensitive);

    tagCombo_->blockSignals(true);
    tagCombo_->clear();
    tagCombo_->addItem(tr("All tags"), QString());
    for (const QString &tag : tags) {
        tagCombo_->addItem(QStringLiteral("#%1").arg(tag), tag);
    }
    const int selectedIndex = tagCombo_->findData(selectedTag);
    tagCombo_->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
    tagCombo_->blockSignals(false);
}

void ClipLibraryWindow::refreshRows()
{
    const QString selectedId = selectedClip().has_value() ? selectedClip()->id : QString();
    const QString query = searchEdit_->text().trimmed();
    QString effectiveQuery = query;
    QString qualifiedTag;
    const qsizetype qualifierSeparator = query.indexOf(QLatin1Char(';'));
    if (qualifierSeparator >= 0) {
        qualifiedTag = query.left(qualifierSeparator).trimmed();
        while (qualifiedTag.startsWith(QLatin1Char('#'))) {
            qualifiedTag.remove(0, 1);
            qualifiedTag = qualifiedTag.trimmed();
        }
        effectiveQuery = query.mid(qualifierSeparator + 1).trimmed();
    }
    const QString tag = tagCombo_->currentData().toString();
    QList<Clip> searchable = clips_;
    for (Clip &clip : searchable) {
        const auto pending = pendingEdits_.constFind(clip.id);
        if (pending == pendingEdits_.constEnd()) {
            continue;
        }
        if (pending->aliasesDirty) {
            clip.aliases = pending->aliases;
        }
        if (pending->tagsDirty) {
            clip.tags = pending->tags;
        }
    }

    ClipSearchOptions searchOptions;
    searchOptions.includeSaved = scope() == ClipLibraryScope::Saved;
    searchOptions.includeTemporary = scope() == ClipLibraryScope::History;
    searchOptions.includeDeleted = scope() == ClipLibraryScope::Trash;
    searchOptions.emptyQueryReturnsPinnedAndRecent = true;
    searchOptions.limit = -1;
    searchOptions.mode = ClipSearchMode::AllFields;

    QList<Clip> visible;
    QHash<QString, QString> matchLabels;
    for (const ClipSearchResult &result : searchClips(searchable, effectiveQuery, searchOptions)) {
        const auto found = std::find_if(searchable.cbegin(), searchable.cend(), [&result](const Clip &clip) {
            return clip.id == result.clipId;
        });
        if (found == searchable.cend()) {
            continue;
        }
        if (!tag.isEmpty() && !found->tags.contains(tag, Qt::CaseInsensitive)) {
            continue;
        }
        if (!qualifiedTag.isEmpty()
            && !found->tags.contains(qualifiedTag, Qt::CaseInsensitive)) {
            continue;
        }
        visible.append(*found);
        if (!effectiveQuery.isEmpty()
            && (result.matchedField == QLatin1String("text")
                || result.matchedField == QLatin1String("preview"))) {
            matchLabels.insert(found->id, tr("Content match"));
        }
    }

    populatingTable_ = true;
    const QSignalBlocker tableSignals(table_);
    const QSignalBlocker selectionSignals(table_->selectionModel());
    table_->setUpdatesEnabled(false);
    table_->setRowCount(visible.size());
    for (int row = 0; row < visible.size(); ++row) {
        const Clip &clip = visible.at(row);
        const QStringList values{
            displayName(clip),
            clip.aliases.join(QLatin1Char(',')),
            tagsText(clip.tags),
            actionDisplayName(clip.actionType),
            matchLabels.value(clip.id),
            sortTimestamp(clip).isValid() ? sortTimestamp(clip).toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString(),
            clip.pinned ? tr("Yes") : QString()
        };
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new Pinloom::Ui::TableItem(values.at(column));
            item->setData(ClipIdRole, clip.id);
            if (column == ClipAliasesColumn && scope() != ClipLibraryScope::Trash) {
                item->setFlags(item->flags() | Qt::ItemIsEditable);
            } else {
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            }
            if (column == ClipTagsColumn) {
                item->setData(TagValuesRole, clip.tags);
            }
            table_->setItem(row, column, item);
        }
        applyInlineCellState(row, ClipAliasesColumn, clip.id);
        applyInlineCellState(row, ClipTagsColumn, clip.id);
    }
    table_->verticalHeader()->setDefaultSectionSize(qMax(34, table_->fontMetrics().height() + 16));
    table_->setUpdatesEnabled(true);
    populatingTable_ = false;

    if (!selectedId.isEmpty() && reselectClip(selectedId)) {
        updatePreview();
    } else if (table_->rowCount() > 0) {
        table_->selectRow(0);
        updatePreview();
    } else {
        updatePreview();
    }
    if (pendingEdits_.isEmpty()) {
        setStatus(tr("%n Clip(s)", nullptr, table_->rowCount()), false);
    } else {
        updateInlineEditStatus();
    }
}

void ClipLibraryWindow::updatePreview()
{
    const std::optional<Clip> selected = selectedClip();
    if (!selected.has_value()) {
        previewTitle_->setText(tr("Select a Clip"));
        previewMetadata_->clear();
        previewText_->clear();
        return;
    }

    previewTitle_->setText(displayName(selected.value()));
    QStringList metadata;
    if (!selected->tags.isEmpty()) {
        metadata.append(tagsText(selected->tags));
    }
    if (!selected->aliases.isEmpty()) {
        metadata.append(tr("Aliases: %1").arg(selected->aliases.join(QStringLiteral(", "))));
    }
    if (!selected->sourceApp.trimmed().isEmpty()) {
        metadata.append(tr("Source app: %1").arg(selected->sourceApp.trimmed()));
    }
    if (!selected->sourceWindowTitle.trimmed().isEmpty()) {
        metadata.append(tr("Window: %1").arg(selected->sourceWindowTitle.trimmed()));
    }
    if (!selected->sourceUri.trimmed().isEmpty()) {
        metadata.append(tr("Source URI: %1").arg(selected->sourceUri.trimmed()));
    }
    metadata.append(tr("Action: %1").arg(actionDisplayName(selected->actionType)));
    metadata.append(tr("Stored in: %1").arg(storageDisplayName(selected->storageBackend)));
    metadata.append(formattedBytes(selected->sizeBytes));
    previewMetadata_->setText(metadata.join(QStringLiteral("  |  ")));
    previewText_->setPlainText(selected->text);
    previewText_->moveCursor(QTextCursor::Start);
}

void ClipLibraryWindow::handleItemChanged(Pinloom::Ui::TableItem *item)
{
    if (populatingTable_ || !item || item->column() != ClipAliasesColumn
        || scope() == ClipLibraryScope::Trash) {
        return;
    }
    const QString clipId = item->data(ClipIdRole).toString();
    const auto found = std::find_if(clips_.cbegin(), clips_.cend(), [&clipId](const Clip &clip) {
        return clip.id == clipId;
    });
    if (found == clips_.cend()) {
        return;
    }

    PendingClipEdit edit = pendingEdits_.value(clipId);
    edit.aliases = valuesFromText(item->text(), false);
    edit.aliasesDirty = edit.aliases != found->aliases;
    if (!edit.tagsDirty) {
        edit.tags = found->tags;
    }
    if (edit.aliasesDirty) {
        pendingEdits_.insert(clipId, edit);
        inlineCellStates_.insert(inlineCellKey(clipId, ClipAliasesColumn), InlineCellDirty);
    } else {
        if (edit.tagsDirty) {
            pendingEdits_.insert(clipId, edit);
        } else {
            pendingEdits_.remove(clipId);
        }
        if (inlineCellStates_.value(inlineCellKey(clipId, ClipAliasesColumn)) != InlineCellSaved) {
            inlineCellStates_.remove(inlineCellKey(clipId, ClipAliasesColumn));
        }
    }
    applyInlineCellState(item->row(), ClipAliasesColumn, clipId);
    updatePreview();
    updateInlineEditStatus();
}

void ClipLibraryWindow::updatePendingTags(const QString &clipId, const QStringList &tags)
{
    const auto found = std::find_if(clips_.cbegin(), clips_.cend(), [&clipId](const Clip &clip) {
        return clip.id == clipId;
    });
    if (found == clips_.cend()) {
        return;
    }

    PendingClipEdit edit = pendingEdits_.value(clipId);
    if (!edit.aliasesDirty) {
        edit.aliases = found->aliases;
    }
    edit.tags = tags;
    edit.tagsDirty = edit.tags != found->tags;
    if (edit.tagsDirty) {
        pendingEdits_.insert(clipId, edit);
        inlineCellStates_.insert(inlineCellKey(clipId, ClipTagsColumn), InlineCellDirty);
    } else {
        if (edit.aliasesDirty) {
            pendingEdits_.insert(clipId, edit);
        } else {
            pendingEdits_.remove(clipId);
        }
        if (inlineCellStates_.value(inlineCellKey(clipId, ClipTagsColumn)) != InlineCellSaved) {
            inlineCellStates_.remove(inlineCellKey(clipId, ClipTagsColumn));
        }
    }

    for (int row = 0; row < table_->rowCount(); ++row) {
        Pinloom::Ui::TableItem *nameItem = table_->item(row, ClipNameColumn);
        if (!nameItem || nameItem->data(ClipIdRole).toString() != clipId) {
            continue;
        }
        Pinloom::Ui::TableItem *tagItem = table_->item(row, ClipTagsColumn);
        if (tagItem) {
            populatingTable_ = true;
            tagItem->setText(tagsText(tags));
            tagItem->setData(TagValuesRole, tags);
            populatingTable_ = false;
            applyInlineCellState(row, ClipTagsColumn, clipId);
            table_->resizeRowToContents(row);
        }
        break;
    }
    updatePreview();
    updateInlineEditStatus();
}

void ClipLibraryWindow::applyInlineCellState(int row, int column, const QString &clipId)
{
    Pinloom::Ui::TableItem *item = table_->item(row, column);
    if (!item) {
        return;
    }
    const int state = inlineCellStates_.value(inlineCellKey(clipId, column), InlineCellClean);
    if (state == InlineCellDirty) {
        QColor color = pinloomVisualTokens(activePinloomVisualScheme()).warning;
        color.setAlpha(48);
        item->setBackground(color);
    } else if (state == InlineCellSaved) {
        QColor color = pinloomVisualTokens(activePinloomVisualScheme()).success;
        color.setAlpha(48);
        item->setBackground(color);
    } else {
        item->setBackground(QBrush());
    }
}

void ClipLibraryWindow::updateInlineEditStatus()
{
    setStatus(pendingEdits_.isEmpty()
                  ? tr("No unsaved Clip Alias or Tag changes")
                  : tr("%1 Clip(s) have unsaved Alias or Tag changes; press Ctrl+S to save")
                        .arg(pendingEdits_.size()));
}

QStringList ClipLibraryWindow::availableTags() const
{
    QStringList tags;
    for (const Clip &clip : clips_) {
        QStringList clipTags = clip.tags;
        const auto pending = pendingEdits_.constFind(clip.id);
        if (pending != pendingEdits_.constEnd() && pending->tagsDirty) {
            clipTags = pending->tags;
        }
        for (const QString &tag : clipTags) {
            const QString trimmed = tag.trimmed();
            if (!trimmed.isEmpty() && !tags.contains(trimmed, Qt::CaseInsensitive)) {
                tags.append(trimmed);
            }
        }
    }
    tags.sort(Qt::CaseInsensitive);
    return tags;
}

QColor ClipLibraryWindow::colorForTag(const QString &tag) const
{
    return QColor::fromHsv(static_cast<int>(qHash(tag.trimmed().toCaseFolded()) % 360U),
                           150,
                           220);
}

bool ClipLibraryWindow::savePendingEdits()
{
    if (auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget())) {
        if (table_->isAncestorOf(editor)) {
            table_->setFocus(Qt::OtherFocusReason);
            QApplication::processEvents();
        }
    }
    if (pendingEdits_.isEmpty()) {
        updateInlineEditStatus();
        return false;
    }
    if (!options_.saveClipHandler || scope() == ClipLibraryScope::Trash) {
        setStatus(tr("Inline Clip edits cannot be saved in this view"));
        return false;
    }

    int savedCells = 0;
    const QStringList clipIds = pendingEdits_.keys();
    for (const QString &clipId : clipIds) {
        const auto found = std::find_if(clips_.cbegin(), clips_.cend(), [&clipId](const Clip &clip) {
            return clip.id == clipId;
        });
        if (found == clips_.cend()) {
            setStatus(tr("An edited Clip no longer exists"));
            return false;
        }
        const PendingClipEdit edit = pendingEdits_.value(clipId);
        Clip updated = *found;
        updated.state = ClipState::Saved;
        if (edit.aliasesDirty) {
            updated.aliases = edit.aliases;
        }
        if (edit.tagsDirty) {
            updated.tags = edit.tags;
        }
        QString error;
        if (!options_.saveClipHandler(updated, &error)) {
            setStatus(error.trimmed().isEmpty() ? tr("Unable to save Clip metadata") : error.trimmed());
            return false;
        }
        if (edit.aliasesDirty) {
            inlineCellStates_.insert(inlineCellKey(clipId, ClipAliasesColumn), InlineCellSaved);
            ++savedCells;
        }
        if (edit.tagsDirty) {
            inlineCellStates_.insert(inlineCellKey(clipId, ClipTagsColumn), InlineCellSaved);
            ++savedCells;
        }
        pendingEdits_.remove(clipId);
    }
    refresh();
    setStatus(tr("Saved %1 Clip Alias/Tag cell(s)").arg(savedCells));
    return true;
}

void ClipLibraryWindow::openTagEditor(int row)
{
    if (row < 0 || row >= table_->rowCount() || scope() == ClipLibraryScope::Trash) {
        return;
    }
    Pinloom::Ui::TableItem *nameItem = table_->item(row, ClipNameColumn);
    Pinloom::Ui::TableItem *tagItem = table_->item(row, ClipTagsColumn);
    if (!nameItem || !tagItem) {
        return;
    }
    const QString clipId = nameItem->data(ClipIdRole).toString();
    const auto found = std::find_if(clips_.cbegin(), clips_.cend(), [&clipId](const Clip &clip) {
        return clip.id == clipId;
    });
    if (found == clips_.cend()) {
        return;
    }
    QStringList selectedTags = found->tags;
    const auto pending = pendingEdits_.constFind(clipId);
    if (pending != pendingEdits_.constEnd() && pending->tagsDirty) {
        selectedTags = pending->tags;
    }

    if (tagEditorPopup_) {
        tagEditorPopup_->close();
    }
    auto *popup = Ui::popupFrame(this);
    popup->setObjectName(QStringLiteral("clipLibraryTagEditorPopup"));
    popup->setAccessibleName(tr("Clip tags"));
    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    auto *queryRow = new QHBoxLayout;
    auto *query = Pinloom::Ui::lineEdit(popup);
    query->setObjectName(QStringLiteral("clipLibraryTagEditorFilter"));
    query->setPlaceholderText(tr("Filter or create a Clip tag"));
    query->setClearButtonEnabled(true);
    auto *create = Pinloom::Ui::toolButton(popup);
    create->setObjectName(QStringLiteral("clipLibraryCreateTagButton"));
    create->setText(QStringLiteral("+"));
    create->setToolTip(tr("Create and select this Clip tag"));
    queryRow->addWidget(query, 1);
    queryRow->addWidget(create);
    auto *list = new Pinloom::Ui::List(popup);
    list->setObjectName(QStringLiteral("clipLibraryTagEditorList"));
    list->setSelectionMode(QAbstractItemView::NoSelection);
    for (const QString &tag : availableTags()) {
        auto *item = new Pinloom::Ui::ListItem(tag, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(selectedTags.contains(tag, Qt::CaseInsensitive)
                                ? Qt::Checked
                                : Qt::Unchecked);
        item->setBackground(colorForTag(tag).lighter(145));
    }
    layout->addLayout(queryRow);
    layout->addWidget(list, 1);

    const auto normalizedTag = [](QString tag) {
        tag = tag.trimmed();
        while (tag.startsWith(QLatin1Char('#'))) {
            tag.remove(0, 1);
            tag = tag.trimmed();
        }
        return tag;
    };
    const auto updateCreateState = [query, create, list, normalizedTag]() {
        const QString candidate = normalizedTag(query->text());
        bool duplicate = false;
        for (int index = 0; index < list->count(); ++index) {
            if (list->item(index)->text().compare(candidate, Qt::CaseInsensitive) == 0) {
                duplicate = true;
                break;
            }
        }
        create->setEnabled(!candidate.isEmpty() && !duplicate);
    };
    const auto selectedValues = [list]() {
        QStringList tags;
        for (int index = 0; index < list->count(); ++index) {
            const QString tag = list->item(index)->text().trimmed();
            if (list->item(index)->checkState() == Qt::Checked
                && !tag.isEmpty()
                && !tags.contains(tag, Qt::CaseInsensitive)) {
                tags.append(tag);
            }
        }
        return tags;
    };
    connect(query, &QLineEdit::textChanged, popup, [list, normalizedTag, updateCreateState](const QString &text) {
        const QString needle = normalizedTag(text);
        for (int index = 0; index < list->count(); ++index) {
            list->item(index)->setHidden(!needle.isEmpty()
                                        && !list->item(index)->text().contains(needle,
                                                                              Qt::CaseInsensitive));
        }
        updateCreateState();
    });
    connect(list, &Pinloom::Ui::List::itemChanged, popup, [this, clipId, selectedValues](Pinloom::Ui::ListItem *) {
        updatePendingTags(clipId, selectedValues());
    });
    connect(create, &QToolButton::clicked, popup, [this, query, list, normalizedTag, updateCreateState]() {
        const QString tag = normalizedTag(query->text());
        if (tag.isEmpty()) {
            return;
        }
        for (int index = 0; index < list->count(); ++index) {
            if (list->item(index)->text().compare(tag, Qt::CaseInsensitive) == 0) {
                return;
            }
        }
        auto *item = new Pinloom::Ui::ListItem(tag, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setBackground(colorForTag(tag).lighter(145));
        item->setCheckState(Qt::Checked);
        query->clear();
        updateCreateState();
    });
    connect(query, &QLineEdit::returnPressed, create, &QToolButton::click);
    updateCreateState();

    tagEditorPopup_ = popup;
    connect(popup, &QObject::destroyed, this, [this, popup]() {
        if (tagEditorPopup_ == popup) {
            tagEditorPopup_ = nullptr;
        }
    });
    const QRect cell = table_->visualItemRect(tagItem);
    QPoint position = table_->viewport()->mapToGlobal(cell.bottomLeft());
    popup->resize(std::max(300, cell.width()), 280);
    if (QScreen *screen = QApplication::screenAt(position)) {
        const QRect available = screen->availableGeometry();
        if (position.x() + popup->width() > available.right()) {
            position.setX(available.right() - popup->width());
        }
        if (position.y() + popup->height() > available.bottom()) {
            position.setY(position.y() - popup->height() - cell.height());
        }
    }
    popup->move(position);
    popup->show();
    query->setFocus(Qt::PopupFocusReason);
}

void ClipLibraryWindow::showContextMenu(const QPoint &position)
{
    const int row = table_->rowAt(position.y());
    if (row < 0) {
        return;
    }
    table_->selectRow(row);
    const std::optional<Clip> selected = selectedClip();
    if (!selected.has_value()) {
        return;
    }

    std::unique_ptr<QMenu> ownedMenu(Pinloom::Ui::menu(this));
    QMenu &menu = *ownedMenu;
    if (selected->state == ClipState::Deleted) {
        QAction *restore = menu.addAction(tr("Restore"));
        QFont font = restore->font();
        font.setBold(true);
        restore->setFont(font);
        restore->setProperty("accent", QStringLiteral("positive"));
        connect(restore, &QAction::triggered, this, &ClipLibraryWindow::restoreSelectedClip);

        QAction *remove = menu.addAction(tr("Permanently remove from Pinloom"));
        QFont removeFont = remove->font();
        removeFont.setBold(true);
        remove->setFont(removeFont);
        remove->setProperty("accent", QStringLiteral("destructive"));
        remove->setEnabled(static_cast<bool>(options_.permanentlyDeleteClipHandler));
        connect(remove, &QAction::triggered, this, [this]() {
            permanentlyDeleteSelectedClip();
        });
    } else {
        QAction *edit = menu.addAction(selected->state == ClipState::Temporary ? tr("Save as Clip") : tr("Edit metadata"));
        edit->setEnabled(static_cast<bool>(options_.saveClipHandler));
        connect(edit, &QAction::triggered, this, &ClipLibraryWindow::editSelectedClip);

        QAction *openSource = menu.addAction(tr("Open source note"));
        openSource->setEnabled(selected->state == ClipState::Saved
                               && selected->storageBackend == ClipStorageBackend::Obsidian
                               && static_cast<bool>(options_.openSourceHandler));
        connect(openSource, &QAction::triggered, this, &ClipLibraryWindow::openSelectedSource);

        if (selected->state == ClipState::Saved) {
            menu.addSeparator();
            QAction *remove = menu.addAction(tr("Delete"));
            QFont font = remove->font();
            font.setBold(true);
            remove->setFont(font);
            remove->setProperty("accent", QStringLiteral("destructive"));
            connect(remove, &QAction::triggered, this, [this]() {
                deleteSelectedClip();
            });
        }
    }
    menu.exec(table_->viewport()->mapToGlobal(position));
}

void ClipLibraryWindow::setStatus(const QString &status, bool notify)
{
    statusText_ = status;
    Pinloom::Ui::setStatusText(statusLabel_, statusText_, notify);
}

bool ClipLibraryWindow::reselectClip(const QString &clipId)
{
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (table_->item(row, 0) && table_->item(row, 0)->data(ClipIdRole).toString() == clipId) {
            table_->selectRow(row);
            return true;
        }
    }
    return false;
}

} // namespace Pinloom
