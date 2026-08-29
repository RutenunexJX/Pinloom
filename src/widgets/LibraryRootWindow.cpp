#include "pinloom/widgets/LibraryRootWindow.h"

#include "pinloom/core/InboxFileCapture.h"
#include "pinloom/core/LibraryRoot.h"
#include "pinloom/core/Version.h"

#include <QAbstractItemView>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QShowEvent>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace Pinloom {

namespace {

constexpr int RootIdRole = Qt::UserRole + 1;
constexpr int RootSyncRole = Qt::UserRole + 2;

class RootFileFilterProxy final : public QSortFilterProxyModel {
public:
    explicit RootFileFilterProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        setRecursiveFilteringEnabled(true);
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setFilterKeyColumn(0);
    }

    void setLibraryRoot(const LibraryRoot &root)
    {
        beginFilterChange();
        root_ = root;
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        const auto *model = qobject_cast<const QFileSystemModel *>(sourceModel());
        if (model) {
            const QModelIndex index = model->index(sourceRow, 0, sourceParent);
            if (libraryRootIgnoresPath(root_, model->filePath(index))) {
                return false;
            }
        }
        return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
    }

private:
    LibraryRoot root_;
};

void appendUnique(QStringList &values, QString value, bool tag)
{
    value = value.trimmed();
    if (tag) {
        while (value.startsWith(QLatin1Char('#'))) {
            value.remove(0, 1);
            value = value.trimmed();
        }
    }
    if (!value.isEmpty() && !values.contains(value, Qt::CaseInsensitive)) {
        values.append(value);
    }
}

bool samePath(const QString &left, const QString &right)
{
    return normalizedInboxFilePath(left).compare(normalizedInboxFilePath(right),
#ifdef Q_OS_WIN
                                                  Qt::CaseInsensitive
#else
                                                  Qt::CaseSensitive
#endif
                                                  ) == 0;
}

} // namespace

LibraryRootWindow::LibraryRootWindow(LibraryRootWindowOptions options, QWidget *parent)
    : QMainWindow(parent)
    , options_(std::move(options))
{
    setObjectName(QStringLiteral("libraryRootWindow"));
    setWindowTitle(tr("Pinloom Root Library %1").arg(pinloomVersionLabel()));
    setMinimumSize(980, 620);
    resize(1280, 760);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);

    auto *toolbar = new QHBoxLayout;
    auto *addButton = new QToolButton(central);
    addButton->setObjectName(QStringLiteral("libraryRootAddButton"));
    addButton->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
    addButton->setText(tr("Add root"));
    addButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    addButton->setToolTip(tr("Register a root directory"));
    removeRootButton_ = new QPushButton(style()->standardIcon(QStyle::SP_TrashIcon),
                                        tr("Remove root"),
                                        central);
    removeRootButton_->setObjectName(QStringLiteral("libraryRootRemoveButton"));
    auto *openButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogOpenButton),
                                       tr("Open"),
                                       central);
    openButton->setObjectName(QStringLiteral("libraryRootOpenButton"));
    auto *refreshButton = new QToolButton(central);
    refreshButton->setObjectName(QStringLiteral("libraryRootRefreshButton"));
    refreshButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    refreshButton->setToolTip(tr("Refresh roots and the current directory"));
    toolbar->addWidget(addButton);
    toolbar->addWidget(removeRootButton_);
    toolbar->addWidget(openButton);
    toolbar->addStretch(1);
    toolbar->addWidget(refreshButton);
    layout->addLayout(toolbar);

    auto *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setObjectName(QStringLiteral("libraryRootSplitter"));
    rootTable_ = new QTableWidget(splitter);
    rootTable_->setObjectName(QStringLiteral("libraryRootTable"));
    rootTable_->setColumnCount(4);
    rootTable_->setHorizontalHeaderLabels({tr("Root"), tr("Location"), tr("Tags"), tr("State")});
    rootTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    rootTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    rootTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rootTable_->verticalHeader()->hide();
    rootTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    rootTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    rootTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    rootTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

    auto *browser = new QWidget(splitter);
    auto *browserLayout = new QVBoxLayout(browser);
    browserLayout->setContentsMargins(0, 0, 0, 0);
    browserLayout->setSpacing(7);
    searchEdit_ = new QLineEdit(browser);
    searchEdit_->setObjectName(QStringLiteral("libraryRootSearchEdit"));
    searchEdit_->setPlaceholderText(tr("Filter the current root by file or folder name"));
    searchEdit_->setClearButtonEnabled(true);
    browserLayout->addWidget(searchEdit_);

    fileModel_ = new QFileSystemModel(this);
    fileModel_->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    fileModel_->setReadOnly(true);
    filterModel_ = new RootFileFilterProxy(this);
    filterModel_->setSourceModel(fileModel_);
    tree_ = new QTreeView(browser);
    tree_->setObjectName(QStringLiteral("libraryRootFileTree"));
    tree_->setModel(filterModel_);
    tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setSortingEnabled(true);
    tree_->sortByColumn(0, Qt::AscendingOrder);
    tree_->setAnimated(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 4; ++column) {
        tree_->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    browserLayout->addWidget(tree_, 1);

    auto *metadata = new QWidget(splitter);
    auto *metadataLayout = new QVBoxLayout(metadata);
    metadataLayout->setContentsMargins(10, 0, 0, 0);
    metadataLayout->setSpacing(8);
    auto *metadataTitle = new QLabel(tr("Selected item"), metadata);
    metadataTitle->setObjectName(QStringLiteral("libraryRootMetadataTitle"));
    pathLabel_ = new QLabel(metadata);
    pathLabel_->setObjectName(QStringLiteral("libraryRootSelectedPath"));
    pathLabel_->setWordWrap(true);
    pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    metadataLayout->addWidget(metadataTitle);
    metadataLayout->addWidget(pathLabel_);
    auto *form = new QFormLayout;
    nameEdit_ = new QLineEdit(metadata);
    nameEdit_->setObjectName(QStringLiteral("libraryRootNameEdit"));
    aliasesEdit_ = new QLineEdit(metadata);
    aliasesEdit_->setObjectName(QStringLiteral("libraryRootAliasesEdit"));
    aliasesEdit_->setPlaceholderText(tr("Comma-separated aliases"));
    tagsEdit_ = new QLineEdit(metadata);
    tagsEdit_->setObjectName(QStringLiteral("libraryRootTagsEdit"));
    tagsEdit_->setPlaceholderText(tr("Comma-separated tags"));
    form->addRow(tr("Name"), nameEdit_);
    form->addRow(tr("Aliases"), aliasesEdit_);
    form->addRow(tr("Tags"), tagsEdit_);
    metadataLayout->addLayout(form);
    saveButton_ = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton),
                                  tr("Save metadata"),
                                  metadata);
    saveButton_->setObjectName(QStringLiteral("libraryRootSaveButton"));
    metadataLayout->addWidget(saveButton_);
    metadataLayout->addStretch(1);

    splitter->addWidget(rootTable_);
    splitter->addWidget(browser);
    splitter->addWidget(metadata);
    splitter->setSizes({310, 650, 310});
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    statusLabel_ = new QLabel(central);
    statusLabel_->setObjectName(QStringLiteral("libraryRootStatusLabel"));
    statusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(statusLabel_);
    setCentralWidget(central);

    connect(addButton, &QToolButton::clicked, this, &LibraryRootWindow::addRoot);
    connect(removeRootButton_, &QPushButton::clicked, this, [this]() { removeSelectedRoot(); });
    connect(openButton, &QPushButton::clicked, this, &LibraryRootWindow::openSelectedPath);
    connect(refreshButton, &QToolButton::clicked, this, &LibraryRootWindow::refresh);
    connect(saveButton_, &QPushButton::clicked, this, &LibraryRootWindow::saveSelectedMetadata);
    connect(rootTable_, &QTableWidget::currentCellChanged, this, [this]() { activateSelectedRoot(); });
    connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this]() { updateSelectedFilesystemPath(); });
    connect(tree_, &QTreeView::doubleClicked, this, [this]() { openSelectedPath(); });
    connect(searchEdit_, &QLineEdit::textChanged, this, [this](const QString &text) {
        filterModel_->setFilterRegularExpression(
            QRegularExpression(QRegularExpression::escape(text.trimmed()),
                               QRegularExpression::CaseInsensitiveOption));
    });

    setStyleSheet(QStringLiteral(
        "QMainWindow#libraryRootWindow { background: #edf4f1; color: #172421; }"
        "QTableWidget, QTreeView { background: #ffffff; alternate-background-color: #f6faf8; "
        "border: 1px solid #b7c9c2; outline: 0; }"
        "QTableWidget::item:selected, QTreeView::item:selected { background: #2f7668; color: white; }"
        "QHeaderView::section { background: #dce9e4; border: 0; border-right: 1px solid #b7c9c2; "
        "border-bottom: 1px solid #a6bbb3; padding: 6px; font-weight: 600; }"
        "QLineEdit { background: white; border: 1px solid #a7bbb3; border-radius: 3px; padding: 6px; }"
        "QLineEdit:focus { border: 2px solid #2f7668; }"
        "QLabel#libraryRootMetadataTitle { color: #17584e; font-size: 16px; font-weight: 600; }"
        "QLabel#libraryRootStatusLabel { border-left: 3px solid #2f7668; padding: 4px 7px; }"));

    refresh();
}

void LibraryRootWindow::refresh()
{
    const QString rootId = selectedRootId();
    const QString path = selectedPath_;
    refreshRootTable(rootId);
    activateSelectedRoot();
    if (!path.isEmpty()) {
        selectPath(path);
    }
}

int LibraryRootWindow::rootCount() const
{
    return rootTable_ ? rootTable_->rowCount() : 0;
}

QString LibraryRootWindow::selectedPath() const
{
    return selectedPath_;
}

QString LibraryRootWindow::statusText() const
{
    return statusText_;
}

bool LibraryRootWindow::selectRootAt(int row)
{
    if (!rootTable_ || row < 0 || row >= rootTable_->rowCount()) {
        return false;
    }
    rootTable_->selectRow(row);
    rootTable_->setCurrentCell(row, 0);
    return rootTable_->currentRow() == row;
}

bool LibraryRootWindow::selectPath(const QString &path)
{
    const QModelIndex source = fileModel_->index(normalizedInboxFilePath(path));
    if (!source.isValid()) {
        return false;
    }
    const QModelIndex proxy = filterModel_->mapFromSource(source);
    if (!proxy.isValid()) {
        return false;
    }
    tree_->setCurrentIndex(proxy);
    tree_->scrollTo(proxy);
    updateSelectedFilesystemPath();
    return samePath(selectedPath_, path);
}

bool LibraryRootWindow::saveSelectedMetadata()
{
    if (!options_.repository || selectedPath_.isEmpty()) {
        setStatus(tr("Select a file or folder first"));
        return false;
    }
    const QString savedPath = selectedPath_;
    const QString rootId = selectedRootId();
    InboxFileSaveRequest request;
    request.filePath = savedPath;
    request.name = nameEdit_->text();
    request.aliases = commaSeparatedValues(aliasesEdit_->text());
    request.tags = commaSeparatedValues(tagsEdit_->text(), true);
    request.mode = InboxFileArchiveMode::Link;
    const InboxFileSaveResult result = saveInboxFile(*options_.repository, request);
    if (!result.success()) {
        setStatus(result.status);
        return false;
    }
    loadMetadata(savedPath);
    refreshRootTable(rootId);
    activateSelectedRoot();
    selectPath(savedPath);
    setStatus(tr("Saved metadata for %1").arg(QFileInfo(savedPath).fileName()));
    return true;
}

bool LibraryRootWindow::removeSelectedRoot(bool requireConfirmation)
{
    const std::optional<LibraryRoot> root = selectedRoot();
    if (!root.has_value() || !options_.repository) {
        setStatus(tr("Select a root directory first"));
        return false;
    }
    if (root->syncRoot) {
        setStatus(tr("The default sync root cannot be removed"));
        return false;
    }
    if (requireConfirmation
        && QMessageBox::question(this,
                                 tr("Remove root"),
                                 tr("Remove this root registration? Files and metadata will be kept."))
               != QMessageBox::Yes) {
        return false;
    }
    if (!options_.repository->removeLibraryRoot(root->id)) {
        setStatus(tr("Unable to remove the root directory"));
        return false;
    }
    refreshRootTable();
    activateSelectedRoot();
    setStatus(tr("Removed root registration; files and metadata were kept"));
    return true;
}

void LibraryRootWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    refresh();
}

void LibraryRootWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Save)) {
        saveSelectedMetadata();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete && rootTable_->hasFocus()) {
        removeSelectedRoot();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void LibraryRootWindow::addRoot()
{
    if (!options_.repository) {
        setStatus(tr("Root repository is unavailable"));
        return;
    }
    const QString path = QFileDialog::getExistingDirectory(this, tr("Add root directory"));
    if (path.isEmpty()) {
        return;
    }
    const LibraryRoot root = makeLibraryRootForPath(path);
    if (!options_.repository->upsertLibraryRoot(root)) {
        setStatus(tr("Unable to register the root directory"));
        return;
    }
    refreshRootTable(root.id);
    activateSelectedRoot();
    setStatus(tr("Registered root directory %1").arg(root.displayName));
}

void LibraryRootWindow::openSelectedPath()
{
    const QString path = selectedPath_.isEmpty()
        ? selectedRoot().value_or(LibraryRoot{}).path
        : selectedPath_;
    if (path.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        setStatus(tr("Unable to open the selected path"));
        return;
    }
    setStatus(tr("Opened %1").arg(QDir::toNativeSeparators(path)));
}

void LibraryRootWindow::refreshRootTable(const QString &preferredRootId)
{
    rootTable_->setRowCount(0);
    if (!options_.repository) {
        setStatus(tr("Root repository is unavailable"));
        return;
    }
    const QList<LibraryRoot> roots = options_.repository->libraryRoots();
    for (const LibraryRoot &root : roots) {
        const int row = rootTable_->rowCount();
        rootTable_->insertRow(row);
        auto *name = new QTableWidgetItem(root.displayName);
        name->setData(RootIdRole, root.id);
        name->setData(RootSyncRole, root.syncRoot);
        if (root.syncRoot) {
            QFont font = name->font();
            font.setBold(true);
            name->setFont(font);
            name->setToolTip(tr("Default synchronized root"));
        }
        auto *location = new QTableWidgetItem(QDir::toNativeSeparators(root.path));
        const std::optional<Resource> metadata =
            options_.repository->findResource(inboxResourceIdForPath(root.path));
        auto *tags = new QTableWidgetItem(metadata.has_value()
                                              ? metadata->tags.join(QStringLiteral(", "))
                                              : QString());
        const QFileInfo info(root.path);
        auto *state = new QTableWidgetItem(info.isDir()
                                               ? (root.syncRoot ? tr("Sync root") : tr("Available"))
                                               : tr("Missing"));
        rootTable_->setItem(row, 0, name);
        rootTable_->setItem(row, 1, location);
        rootTable_->setItem(row, 2, tags);
        rootTable_->setItem(row, 3, state);
    }
    int rowToSelect = roots.isEmpty() ? -1 : 0;
    for (int row = 0; row < rootTable_->rowCount(); ++row) {
        if (rootTable_->item(row, 0)->data(RootIdRole).toString() == preferredRootId) {
            rowToSelect = row;
            break;
        }
    }
    if (rowToSelect >= 0) {
        rootTable_->selectRow(rowToSelect);
        rootTable_->setCurrentCell(rowToSelect, 0);
    }
}

void LibraryRootWindow::activateSelectedRoot()
{
    const std::optional<LibraryRoot> root = selectedRoot();
    removeRootButton_->setEnabled(root.has_value() && !root->syncRoot);
    if (!root.has_value()) {
        tree_->setRootIndex({});
        selectedPath_.clear();
        loadMetadata({});
        setStatus(tr("No root directories registered"));
        return;
    }
    static_cast<RootFileFilterProxy *>(filterModel_)->setLibraryRoot(root.value());
    const QModelIndex sourceRoot = fileModel_->setRootPath(root->path);
    tree_->setRootIndex(filterModel_->mapFromSource(sourceRoot));
    selectedPath_ = root->path;
    loadMetadata(selectedPath_);
    setStatus(QFileInfo(root->path).isDir()
                  ? tr("Browsing %1").arg(QDir::toNativeSeparators(root->path))
                  : tr("Root directory is currently unavailable: %1")
                        .arg(QDir::toNativeSeparators(root->path)));
}

void LibraryRootWindow::updateSelectedFilesystemPath()
{
    const QModelIndex proxy = tree_->currentIndex();
    const QModelIndex source = filterModel_->mapToSource(proxy);
    const QString path = source.isValid() ? fileModel_->filePath(source) : QString();
    if (!path.isEmpty()) {
        selectedPath_ = normalizedInboxFilePath(path);
        loadMetadata(selectedPath_);
    }
}

void LibraryRootWindow::loadMetadata(const QString &path)
{
    const bool available = !path.trimmed().isEmpty() && QFileInfo::exists(path);
    pathLabel_->setText(path.trimmed().isEmpty() ? tr("No item selected")
                                                 : QDir::toNativeSeparators(path));
    const std::optional<Resource> resource = available && options_.repository
        ? options_.repository->findResource(inboxResourceIdForPath(path))
        : std::nullopt;
    nameEdit_->setText(resource.has_value() ? resource->title : defaultInboxFileName(path));
    aliasesEdit_->setText(resource.has_value()
                              ? resource->aliases.join(QLatin1Char(','))
                              : QString());
    tagsEdit_->setText(resource.has_value()
                           ? resource->tags.join(QStringLiteral(", "))
                           : QString());
    if (options_.fileTagsProvider) {
        const QStringList tags = options_.fileTagsProvider();
        tagsEdit_->setToolTip(tags.isEmpty()
                                  ? QString()
                                  : tr("Existing tags: %1").arg(tags.join(QStringLiteral(", "))));
    }
    nameEdit_->setEnabled(available);
    aliasesEdit_->setEnabled(available);
    tagsEdit_->setEnabled(available);
    saveButton_->setEnabled(available);
}

void LibraryRootWindow::setStatus(const QString &status)
{
    statusText_ = status.trimmed();
    statusLabel_->setText(statusText_);
}

QString LibraryRootWindow::selectedRootId() const
{
    const int row = rootTable_ ? rootTable_->currentRow() : -1;
    const QTableWidgetItem *item = row >= 0 ? rootTable_->item(row, 0) : nullptr;
    return item ? item->data(RootIdRole).toString() : QString();
}

std::optional<LibraryRoot> LibraryRootWindow::selectedRoot() const
{
    return options_.repository ? options_.repository->findLibraryRoot(selectedRootId()) : std::nullopt;
}

QStringList LibraryRootWindow::commaSeparatedValues(const QString &text, bool tags) const
{
    QStringList values;
    for (const QString &value : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        if (tags) {
            appendUnique(values, value, true);
        } else if (!value.trimmed().isEmpty()) {
            values.append(value);
        }
    }
    return values;
}

} // namespace Pinloom
