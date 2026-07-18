#include "pinloom/widgets/AnchorLibraryWindow.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace Pinloom {

namespace {

constexpr int ResourceIdRole = Qt::UserRole + 1;
constexpr int AnchorIdRole = Qt::UserRole + 2;

enum class AnchorLibraryScope {
    All = 0,
    Untagged,
    Missing
};

QString resourceKindLabel(ResourceKind kind)
{
    switch (kind) {
    case ResourceKind::File:
        return QStringLiteral("File");
    case ResourceKind::Folder:
        return QStringLiteral("Folder");
    case ResourceKind::Pdf:
        return QStringLiteral("PDF");
    case ResourceKind::TextSnippet:
        return QStringLiteral("Text");
    case ResourceKind::Url:
        return QStringLiteral("URL");
    case ResourceKind::Note:
        return QStringLiteral("Note");
    case ResourceKind::ManualAnchor:
        return QStringLiteral("Manual");
    case ResourceKind::Unknown:
        return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

void appendUnique(QStringList &values, const QString &value)
{
    const QString trimmed = value.trimmed();
    if (!trimmed.isEmpty() && !values.contains(trimmed, Qt::CaseInsensitive)) {
        values.append(trimmed);
    }
}

QStringList fileTags(const AnchorLibraryFile &file)
{
    QStringList tags;
    for (const QString &tag : file.resource.tags) {
        appendUnique(tags, tag);
    }
    for (const AnchorLibraryAnchor &entry : file.anchors) {
        for (const QString &tag : entry.anchor.tags) {
            appendUnique(tags, tag);
        }
    }
    return tags;
}

QDateTime lastMarkedAt(const AnchorLibraryFile &file)
{
    QDateTime lastMarked;
    for (const AnchorLibraryAnchor &entry : file.anchors) {
        const Anchor &anchor = entry.anchor;
        const QDateTime candidate = anchor.updatedAt.isValid() ? anchor.updatedAt : anchor.createdAt;
        if (candidate.isValid() && (!lastMarked.isValid() || candidate > lastMarked)) {
            lastMarked = candidate;
        }
    }
    return lastMarked;
}

QString anchorLocatorSummary(const Anchor &anchor)
{
    if (anchor.locatorType.trimmed().isEmpty()) {
        return anchor.locatorJson.simplified();
    }
    if (anchor.locatorJson.trimmed().isEmpty()) {
        return anchor.locatorType.trimmed();
    }
    return QStringLiteral("%1 %2").arg(anchor.locatorType.trimmed(), anchor.locatorJson.simplified());
}

bool containsText(const QString &value, const QString &needle)
{
    return value.contains(needle, Qt::CaseInsensitive);
}

QString fileDisplayName(const AnchorLibraryFile &file)
{
    if (!file.resource.title.trimmed().isEmpty()) {
        return file.resource.title.trimmed();
    }
    const QString fileName = QFileInfo(file.resource.location).fileName().trimmed();
    return fileName.isEmpty() ? file.resource.location : fileName;
}

QString fileLocationLabel(const AnchorLibraryFile &file)
{
    const QString location = file.resource.location.trimmed();
    return location.contains(QStringLiteral("://"))
        ? location
        : QDir::toNativeSeparators(location);
}

QString fileGroupingKey(const Resource &resource)
{
    const QString location = resource.location.trimmed();
    if (location.isEmpty()) {
        return QStringLiteral("resource:%1").arg(resource.id);
    }
    if (location.contains(QStringLiteral("://"))) {
        return QStringLiteral("url:%1").arg(location);
    }
    const QString absolutePath = QDir::cleanPath(
        QDir::fromNativeSeparators(QFileInfo(location).absoluteFilePath()));
    return QStringLiteral("path:%1").arg(absolutePath.toCaseFolded());
}

void mergeResourceMetadata(Resource &target, const Resource &source)
{
    if (target.title.trimmed().isEmpty() && !source.title.trimmed().isEmpty()) {
        target.title = source.title;
    }
    if (target.kind == ResourceKind::Unknown && source.kind != ResourceKind::Unknown) {
        target.kind = source.kind;
    }
    for (const QString &alias : source.aliases) {
        appendUnique(target.aliases, alias);
    }
    for (const QString &tag : source.tags) {
        appendUnique(target.tags, tag);
    }
}

void appendUniqueAnchor(AnchorLibraryFile &file, const AnchorLibraryAnchor &entry)
{
    for (const AnchorLibraryAnchor &existing : file.anchors) {
        if (existing.resourceId == entry.resourceId && existing.anchor.id == entry.anchor.id) {
            return;
        }
    }
    file.anchors.append(entry);
}

} // namespace

AnchorLibraryWindow::AnchorLibraryWindow(AnchorLibraryWindowOptions options, QWidget *parent)
    : QMainWindow(parent)
    , options_(std::move(options))
{
    setObjectName(QStringLiteral("anchorLibraryWindow"));
    setWindowTitle(tr("Pinloom Anchor Library"));
    setMinimumSize(760, 480);
    resize(1040, 680);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(10, 10, 10, 8);
    layout->setSpacing(8);

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(6);
    filterEdit_ = new QLineEdit(central);
    filterEdit_->setObjectName(QStringLiteral("anchorLibraryFilterEdit"));
    filterEdit_->setPlaceholderText(tr("Filter marked files and anchors"));
    filterEdit_->setClearButtonEnabled(true);
    scopeCombo_ = new QComboBox(central);
    scopeCombo_->setObjectName(QStringLiteral("anchorLibraryScopeCombo"));
    scopeCombo_->addItem(tr("All marked files"), static_cast<int>(AnchorLibraryScope::All));
    scopeCombo_->addItem(tr("Untagged"), static_cast<int>(AnchorLibraryScope::Untagged));
    scopeCombo_->addItem(tr("Missing files"), static_cast<int>(AnchorLibraryScope::Missing));
    refreshButton_ = new QToolButton(central);
    refreshButton_->setObjectName(QStringLiteral("anchorLibraryRefreshButton"));
    refreshButton_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    refreshButton_->setToolTip(tr("Refresh marked files"));
    refreshButton_->setAccessibleName(refreshButton_->toolTip());
    jumpButton_ = new QToolButton(central);
    jumpButton_->setObjectName(QStringLiteral("anchorLibraryJumpButton"));
    jumpButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    jumpButton_->setToolTip(tr("Jump to selected anchor"));
    jumpButton_->setAccessibleName(jumpButton_->toolTip());
    jumpButton_->setEnabled(false);
    deleteButton_ = new QToolButton(central);
    deleteButton_->setObjectName(QStringLiteral("anchorLibraryDeleteButton"));
    deleteButton_->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    deleteButton_->setToolTip(tr("Delete selected anchor"));
    deleteButton_->setAccessibleName(deleteButton_->toolTip());
    deleteButton_->setEnabled(false);

    toolbar->addWidget(filterEdit_, 1);
    toolbar->addWidget(scopeCombo_);
    toolbar->addWidget(refreshButton_);
    toolbar->addWidget(jumpButton_);
    toolbar->addWidget(deleteButton_);

    auto *splitter = new QSplitter(Qt::Vertical, central);
    splitter->setObjectName(QStringLiteral("anchorLibrarySplitter"));
    splitter->setChildrenCollapsible(false);

    fileTable_ = new QTableWidget(splitter);
    fileTable_->setObjectName(QStringLiteral("anchorLibraryFileTable"));
    fileTable_->setColumnCount(7);
    fileTable_->setHorizontalHeaderLabels({tr("File"),
                                           tr("Location"),
                                           tr("Type"),
                                           tr("Anchors"),
                                           tr("Tags"),
                                           tr("Last marked"),
                                           tr("Status")});
    fileTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fileTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    fileTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fileTable_->setAlternatingRowColors(true);
    fileTable_->setSortingEnabled(true);
    fileTable_->verticalHeader()->setVisible(false);
    fileTable_->horizontalHeader()->setStretchLastSection(false);
    fileTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    fileTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    fileTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    fileTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    fileTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    fileTable_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    fileTable_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);

    anchorTable_ = new QTableWidget(splitter);
    anchorTable_->setObjectName(QStringLiteral("anchorLibraryAnchorTable"));
    anchorTable_->setColumnCount(5);
    anchorTable_->setHorizontalHeaderLabels({tr("Anchor"),
                                             tr("Aliases"),
                                             tr("Tags"),
                                             tr("Locator"),
                                             tr("Updated")});
    anchorTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    anchorTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    anchorTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    anchorTable_->setAlternatingRowColors(true);
    anchorTable_->setSortingEnabled(true);
    anchorTable_->verticalHeader()->setVisible(false);
    anchorTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    anchorTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    anchorTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    anchorTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    anchorTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    splitter->addWidget(fileTable_);
    splitter->addWidget(anchorTable_);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    statusLabel_ = new QLabel(central);
    statusLabel_->setObjectName(QStringLiteral("anchorLibraryStatusLabel"));

    layout->addLayout(toolbar);
    layout->addWidget(splitter, 1);
    layout->addWidget(statusLabel_);
    setCentralWidget(central);

    connect(filterEdit_, &QLineEdit::textChanged, this, &AnchorLibraryWindow::applyFilter);
    connect(scopeCombo_, &QComboBox::currentIndexChanged, this, &AnchorLibraryWindow::applyFilter);
    connect(refreshButton_, &QToolButton::clicked, this, &AnchorLibraryWindow::refreshLibrary);
    connect(jumpButton_, &QToolButton::clicked, this, &AnchorLibraryWindow::activateSelectedAnchor);
    connect(deleteButton_, &QToolButton::clicked, this, &AnchorLibraryWindow::deleteSelectedAnchor);
    connect(fileTable_, &QTableWidget::itemSelectionChanged,
            this, &AnchorLibraryWindow::populateSelectedFileAnchors);
    connect(anchorTable_, &QTableWidget::itemSelectionChanged,
            this, &AnchorLibraryWindow::updateActionButtons);
    connect(anchorTable_, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *) {
        activateSelectedAnchor();
    });
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, anchorTable_);
    deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, &AnchorLibraryWindow::deleteSelectedAnchor);

    refreshLibrary();
}

void AnchorLibraryWindow::refreshLibrary()
{
    const QList<AnchorLibraryFile> providedFiles = options_.filesProvider
        ? options_.filesProvider()
        : QList<AnchorLibraryFile>{};
    files_.clear();
    QHash<QString, int> groupedFileIndexes;
    for (AnchorLibraryFile file : providedFiles) {
        if (file.resource.deleted) {
            continue;
        }
        file.anchors.erase(std::remove_if(file.anchors.begin(), file.anchors.end(), [](const AnchorLibraryAnchor &entry) {
            return entry.anchor.deleted;
        }), file.anchors.end());
        if (file.anchors.isEmpty()) {
            continue;
        }
        for (AnchorLibraryAnchor &entry : file.anchors) {
            if (entry.resourceId.trimmed().isEmpty()) {
                entry.resourceId = file.resource.id;
            }
        }
        file.resource.anchors.clear();

        const QString groupingKey = fileGroupingKey(file.resource);
        const auto existing = groupedFileIndexes.constFind(groupingKey);
        if (existing == groupedFileIndexes.constEnd()) {
            groupedFileIndexes.insert(groupingKey, files_.size());
            files_.append(file);
            continue;
        }

        AnchorLibraryFile &groupedFile = files_[existing.value()];
        mergeResourceMetadata(groupedFile.resource, file.resource);
        for (const AnchorLibraryAnchor &entry : file.anchors) {
            appendUniqueAnchor(groupedFile, entry);
        }
    }
    std::sort(files_.begin(), files_.end(), [](const AnchorLibraryFile &left, const AnchorLibraryFile &right) {
        const QDateTime leftMarked = lastMarkedAt(left);
        const QDateTime rightMarked = lastMarkedAt(right);
        if (leftMarked.isValid() != rightMarked.isValid()) {
            return leftMarked.isValid();
        }
        if (leftMarked.isValid() && leftMarked != rightMarked) {
            return leftMarked > rightMarked;
        }
        return fileDisplayName(left).compare(fileDisplayName(right), Qt::CaseInsensitive) < 0;
    });
    applyFilter();
}

void AnchorLibraryWindow::setFilterText(const QString &text)
{
    filterEdit_->setText(text);
}

QString AnchorLibraryWindow::filterText() const
{
    return filterEdit_->text();
}

int AnchorLibraryWindow::visibleFileCount() const
{
    return fileTable_->rowCount();
}

int AnchorLibraryWindow::visibleAnchorCount() const
{
    return anchorTable_->rowCount();
}

QString AnchorLibraryWindow::statusText() const
{
    return statusText_;
}

bool AnchorLibraryWindow::selectFileAt(int row)
{
    if (row < 0 || row >= fileTable_->rowCount()) {
        return false;
    }
    fileTable_->selectRow(row);
    return selectedFile() != nullptr;
}

bool AnchorLibraryWindow::selectAnchorAt(int row)
{
    if (row < 0 || row >= anchorTable_->rowCount()) {
        return false;
    }
    anchorTable_->selectRow(row);
    return selectedAnchor().has_value();
}

bool AnchorLibraryWindow::activateSelectedAnchor()
{
    const AnchorLibraryFile *file = selectedFile();
    const std::optional<AnchorLibraryAnchor> anchor = selectedAnchor();
    if (!file || !anchor.has_value()) {
        statusText_ = tr("Select an anchor to jump");
        statusLabel_->setText(statusText_);
        return false;
    }
    if (!options_.anchorJumpHandler) {
        statusText_ = tr("Anchor jump is not configured");
        statusLabel_->setText(statusText_);
        return false;
    }

    QString status;
    const bool activated = options_.anchorJumpHandler(*file, anchor.value(), &status);
    statusText_ = status.trimmed().isEmpty()
        ? (activated ? tr("Opened anchor") : tr("Unable to open anchor"))
        : status.trimmed();
    statusLabel_->setText(statusText_);
    if (activated) {
        emit anchorActivated(anchor->resourceId, anchor->anchor.id);
    }
    return activated;
}

bool AnchorLibraryWindow::deleteSelectedAnchor()
{
    const AnchorLibraryFile *selected = selectedFile();
    const std::optional<AnchorLibraryAnchor> selectedEntry = selectedAnchor();
    if (!selected || !selectedEntry.has_value()) {
        statusText_ = tr("Select an anchor to delete");
        statusLabel_->setText(statusText_);
        return false;
    }
    if (!options_.anchorDeleteHandler) {
        statusText_ = tr("Anchor deletion is not configured");
        statusLabel_->setText(statusText_);
        return false;
    }

    const AnchorLibraryFile file = *selected;
    const AnchorLibraryAnchor entry = selectedEntry.value();
    const QString anchorName = entry.anchor.name.trimmed().isEmpty()
        ? entry.anchor.id
        : entry.anchor.name.trimmed();
    const bool confirmed = options_.anchorDeleteConfirmationHandler
        ? options_.anchorDeleteConfirmationHandler(file, entry)
        : QMessageBox::question(this,
                                tr("Delete Anchor"),
                                tr("Delete anchor \"%1\"?\n\n"
                                   "The anchor will be moved to trash. The marked file will not be deleted.")
                                    .arg(anchorName),
                                QMessageBox::Yes | QMessageBox::No,
                                QMessageBox::No) == QMessageBox::Yes;
    if (!confirmed) {
        statusText_ = tr("Delete canceled");
        statusLabel_->setText(statusText_);
        return false;
    }

    QString status;
    const bool deleted = options_.anchorDeleteHandler(file, entry, &status);
    if (!deleted) {
        statusText_ = status.trimmed().isEmpty()
            ? tr("Unable to delete anchor")
            : status.trimmed();
        statusLabel_->setText(statusText_);
        return false;
    }

    refreshLibrary();
    statusText_ = status.trimmed().isEmpty()
        ? tr("Deleted anchor: %1").arg(anchorName)
        : status.trimmed();
    statusLabel_->setText(statusText_);
    emit anchorDeleted(entry.resourceId, entry.anchor.id);
    return true;
}

void AnchorLibraryWindow::applyFilter()
{
    const QString selectedResourceId = selectedFile()
        ? selectedFile()->resource.id
        : QString();
    fileTable_->setSortingEnabled(false);
    fileTable_->setRowCount(0);

    for (const AnchorLibraryFile &file : files_) {
        if (!fileMatchesFilter(file)) {
            continue;
        }
        const int row = fileTable_->rowCount();
        fileTable_->insertRow(row);
        auto *nameItem = new QTableWidgetItem(fileDisplayName(file));
        nameItem->setData(ResourceIdRole, file.resource.id);
        nameItem->setToolTip(file.resource.location);
        fileTable_->setItem(row, 0, nameItem);
        auto *locationItem = new QTableWidgetItem(fileLocationLabel(file));
        locationItem->setToolTip(file.resource.location);
        fileTable_->setItem(row, 1, locationItem);
        fileTable_->setItem(row, 2, new QTableWidgetItem(resourceKindLabel(file.resource.kind)));
        auto *countItem = new QTableWidgetItem;
        countItem->setData(Qt::DisplayRole, file.anchors.size());
        fileTable_->setItem(row, 3, countItem);
        fileTable_->setItem(row, 4, new QTableWidgetItem(fileTags(file).join(QStringLiteral(", "))));
        const QDateTime markedAt = lastMarkedAt(file);
        fileTable_->setItem(row, 5, new QTableWidgetItem(markedAt.isValid()
                                                            ? QLocale().toString(markedAt.toLocalTime(), QLocale::ShortFormat)
                                                            : QString()));
        const bool exists = file.resource.location.trimmed().isEmpty()
            || file.resource.kind == ResourceKind::Url
            || QFileInfo::exists(file.resource.location);
        fileTable_->setItem(row, 6, new QTableWidgetItem(exists ? tr("Ready") : tr("Missing")));
    }

    fileTable_->setSortingEnabled(true);
    bool restored = false;
    for (int row = 0; row < fileTable_->rowCount(); ++row) {
        if (fileTable_->item(row, 0)->data(ResourceIdRole).toString() == selectedResourceId) {
            fileTable_->selectRow(row);
            restored = true;
            break;
        }
    }
    if (!restored && fileTable_->rowCount() > 0) {
        fileTable_->selectRow(0);
    } else if (fileTable_->rowCount() == 0) {
        anchorTable_->setRowCount(0);
    }
    populateSelectedFileAnchors();
    updateStatus();
}

void AnchorLibraryWindow::populateSelectedFileAnchors()
{
    const std::optional<AnchorLibraryAnchor> previousSelection = selectedAnchor();
    const QString selectedAnchorId = previousSelection.has_value()
        ? previousSelection->anchor.id
        : QString();
    const QString selectedAnchorResourceId = previousSelection.has_value()
        ? previousSelection->resourceId
        : QString();
    anchorTable_->setSortingEnabled(false);
    anchorTable_->setRowCount(0);

    const AnchorLibraryFile *file = selectedFile();
    if (file) {
        for (const AnchorLibraryAnchor &entry : file->anchors) {
            const Anchor &anchor = entry.anchor;
            const int row = anchorTable_->rowCount();
            anchorTable_->insertRow(row);
            auto *nameItem = new QTableWidgetItem(anchor.name.trimmed().isEmpty() ? anchor.id : anchor.name);
            nameItem->setData(AnchorIdRole, anchor.id);
            nameItem->setData(ResourceIdRole, entry.resourceId);
            anchorTable_->setItem(row, 0, nameItem);
            anchorTable_->setItem(row, 1, new QTableWidgetItem(anchor.aliases.join(QStringLiteral(", "))));
            anchorTable_->setItem(row, 2, new QTableWidgetItem(anchor.tags.join(QStringLiteral(", "))));
            anchorTable_->setItem(row, 3, new QTableWidgetItem(anchorLocatorSummary(anchor)));
            const QDateTime updated = anchor.updatedAt.isValid() ? anchor.updatedAt : anchor.createdAt;
            anchorTable_->setItem(row, 4, new QTableWidgetItem(updated.isValid()
                                                                  ? QLocale().toString(updated.toLocalTime(), QLocale::ShortFormat)
                                                                  : QString()));
        }
    }

    anchorTable_->setSortingEnabled(true);
    bool restored = false;
    for (int row = 0; row < anchorTable_->rowCount(); ++row) {
        if (anchorTable_->item(row, 0)->data(AnchorIdRole).toString() == selectedAnchorId
            && anchorTable_->item(row, 0)->data(ResourceIdRole).toString() == selectedAnchorResourceId) {
            anchorTable_->selectRow(row);
            restored = true;
            break;
        }
    }
    if (!restored && anchorTable_->rowCount() > 0) {
        anchorTable_->selectRow(0);
    }
    updateActionButtons();
    updateStatus();
}

const AnchorLibraryFile *AnchorLibraryWindow::fileForResourceId(const QString &resourceId) const
{
    for (const AnchorLibraryFile &file : files_) {
        if (file.resource.id == resourceId) {
            return &file;
        }
    }
    return nullptr;
}

const AnchorLibraryFile *AnchorLibraryWindow::selectedFile() const
{
    const int row = fileTable_->currentRow();
    if (row < 0 || !fileTable_->item(row, 0)) {
        return nullptr;
    }
    return fileForResourceId(fileTable_->item(row, 0)->data(ResourceIdRole).toString());
}

std::optional<AnchorLibraryAnchor> AnchorLibraryWindow::selectedAnchor() const
{
    const AnchorLibraryFile *file = selectedFile();
    const int row = anchorTable_->currentRow();
    if (!file || row < 0 || !anchorTable_->item(row, 0)) {
        return std::nullopt;
    }
    const QString anchorId = anchorTable_->item(row, 0)->data(AnchorIdRole).toString();
    const QString resourceId = anchorTable_->item(row, 0)->data(ResourceIdRole).toString();
    for (const AnchorLibraryAnchor &entry : file->anchors) {
        if (entry.resourceId == resourceId && entry.anchor.id == anchorId) {
            return entry;
        }
    }
    return std::nullopt;
}

bool AnchorLibraryWindow::fileMatchesFilter(const AnchorLibraryFile &file) const
{
    const AnchorLibraryScope scope = static_cast<AnchorLibraryScope>(scopeCombo_->currentData().toInt());
    if (scope == AnchorLibraryScope::Untagged && !fileTags(file).isEmpty()) {
        return false;
    }
    if (scope == AnchorLibraryScope::Missing
        && (file.resource.location.trimmed().isEmpty()
            || file.resource.kind == ResourceKind::Url
            || QFileInfo::exists(file.resource.location))) {
        return false;
    }

    const QString needle = filterEdit_->text().trimmed();
    if (needle.isEmpty()) {
        return true;
    }
    if (containsText(fileDisplayName(file), needle)
        || containsText(file.resource.location, needle)
        || containsText(file.resource.aliases.join(QLatin1Char('\n')), needle)
        || containsText(fileTags(file).join(QLatin1Char('\n')), needle)) {
        return true;
    }
    for (const AnchorLibraryAnchor &entry : file.anchors) {
        const Anchor &anchor = entry.anchor;
        if (containsText(anchor.name, needle)
            || containsText(anchor.aliases.join(QLatin1Char('\n')), needle)
            || containsText(anchor.tags.join(QLatin1Char('\n')), needle)
            || containsText(anchorLocatorSummary(anchor), needle)) {
            return true;
        }
    }
    return false;
}

void AnchorLibraryWindow::updateActionButtons()
{
    const bool hasSelection = selectedAnchor().has_value();
    jumpButton_->setEnabled(hasSelection && static_cast<bool>(options_.anchorJumpHandler));
    deleteButton_->setEnabled(hasSelection && static_cast<bool>(options_.anchorDeleteHandler));
}

void AnchorLibraryWindow::updateStatus()
{
    int anchorCount = 0;
    for (int row = 0; row < fileTable_->rowCount(); ++row) {
        anchorCount += fileTable_->item(row, 3)->data(Qt::DisplayRole).toInt();
    }
    statusText_ = tr("%1 marked file(s) | %2 anchor(s)")
                      .arg(fileTable_->rowCount())
                      .arg(anchorCount);
    statusLabel_->setText(statusText_);
}

} // namespace Pinloom
