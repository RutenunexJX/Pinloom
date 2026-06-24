#include "pinloom/widgets/PinloomPanel.h"

#include "pinloom/core/IndexingService.h"
#include "pinloom/widgets/TextPreviewDialog.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QSize>
#include <QUrl>
#include <QVBoxLayout>

namespace Pinloom {

namespace {

QString rootItemText(const LibraryRoot &root)
{
    const QString indexedAt = root.lastIndexedAt.isValid()
        ? root.lastIndexedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
        : QStringLiteral("never");
    return QStringLiteral("%1  |  %2  |  %3")
        .arg(root.displayName.isEmpty() ? root.path : root.displayName,
             root.path,
             indexedAt);
}

QString anchorLabel(const Anchor &anchor)
{
    switch (anchor.type) {
    case AnchorType::FileLine:
        return QStringLiteral("Line");
    case AnchorType::MarkdownHeading:
        return QStringLiteral("Heading");
    case AnchorType::MarkdownBlock:
        return QStringLiteral("Block");
    default:
        return QStringLiteral("Anchor");
    }
}

QString resourceKindLabel(ResourceKind kind)
{
    switch (kind) {
    case ResourceKind::Folder:
        return QStringLiteral("Folder");
    case ResourceKind::Pdf:
        return QStringLiteral("PDF");
    case ResourceKind::Markdown:
        return QStringLiteral("Markdown");
    case ResourceKind::CodeSnippet:
        return QStringLiteral("Code");
    case ResourceKind::Url:
        return QStringLiteral("URL");
    case ResourceKind::Note:
        return QStringLiteral("Note");
    case ResourceKind::ManualAnchor:
        return QStringLiteral("Anchor");
    case ResourceKind::File:
        return QStringLiteral("File");
    case ResourceKind::Unknown:
        break;
    }
    return QStringLiteral("Resource");
}

QString resultText(const SearchResult &result)
{
    if (!result.matchedAnchor.has_value()) {
        return QStringLiteral("[%1] %2\n%3")
            .arg(resourceKindLabel(result.resource.kind),
                 result.resource.title,
                 result.resource.location);
    }

    const Anchor &anchor = result.matchedAnchor.value();
    return QStringLiteral("[%1] %2 - line %3\n%4")
        .arg(anchorLabel(anchor),
             anchor.target,
             QString::number(anchor.line),
             result.resource.location);
}

} // namespace

PinloomPanel::PinloomPanel(ILibraryRepository &repository, QWidget *parent)
    : QWidget(parent)
    , repository_(repository)
{
    auto *layout = new QVBoxLayout(this);

    auto *rootToolbar = new QHBoxLayout();
    auto *addRootButton = new QPushButton(tr("Add Folder"), this);
    removeRootButton_ = new QPushButton(tr("Remove Folder"), this);
    refreshSelectedButton_ = new QPushButton(tr("Refresh Selected"), this);
    refreshAllButton_ = new QPushButton(tr("Refresh All"), this);
    rebuildAllButton_ = new QPushButton(tr("Rebuild All"), this);

    rootToolbar->addWidget(addRootButton);
    rootToolbar->addWidget(removeRootButton_);
    rootToolbar->addWidget(refreshSelectedButton_);
    rootToolbar->addWidget(refreshAllButton_);
    rootToolbar->addWidget(rebuildAllButton_);
    rootToolbar->addStretch(1);

    rootList_ = new QListWidget(this);
    rootList_->setObjectName(QStringLiteral("libraryRootList"));
    rootList_->setMaximumHeight(130);

    auto *resultToolbar = new QHBoxLayout();
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setObjectName(QStringLiteral("searchEdit"));
    searchEdit_->setPlaceholderText(tr("Search resources, tags, aliases, anchors"));
    openButton_ = new QPushButton(tr("Open"), this);
    resultToolbar->addWidget(searchEdit_, 1);
    resultToolbar->addWidget(openButton_);

    resultList_ = new QListWidget(this);
    resultList_->setObjectName(QStringLiteral("resultList"));
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("statusLabel"));

    layout->addLayout(rootToolbar);
    layout->addWidget(rootList_);
    layout->addLayout(resultToolbar);
    layout->addWidget(resultList_, 1);
    layout->addWidget(statusLabel_);

    connect(addRootButton, &QPushButton::clicked, this, &PinloomPanel::addLibraryRoot);
    connect(removeRootButton_, &QPushButton::clicked, this, &PinloomPanel::removeSelectedLibraryRoot);
    connect(refreshSelectedButton_, &QPushButton::clicked, this, &PinloomPanel::refreshSelectedRoot);
    connect(refreshAllButton_, &QPushButton::clicked, this, &PinloomPanel::refreshAllRoots);
    connect(rebuildAllButton_, &QPushButton::clicked, this, &PinloomPanel::rebuildAllRoots);
    connect(openButton_, &QPushButton::clicked, this, &PinloomPanel::openSelectedResource);
    connect(searchEdit_, &QLineEdit::textChanged, this, &PinloomPanel::refreshResults);
    connect(resultList_, &QListWidget::itemDoubleClicked, this, &PinloomPanel::openResultItem);

    loadLibraryRoots();
    refreshResults();
}

void PinloomPanel::addLibraryRoot()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("Add Library Folder"));
    if (path.isEmpty()) {
        return;
    }

    const LibraryRoot root = makeLibraryRootForPath(path);
    if (!repository_.upsertLibraryRoot(root)) {
        QMessageBox::warning(this, tr("Add folder failed"), tr("Unable to save library folder."));
        return;
    }

    loadLibraryRoots();
    selectLibraryRoot(root.id);
    refreshSelectedRoot();
}

void PinloomPanel::removeSelectedLibraryRoot()
{
    const QString id = selectedRootId();
    if (id.isEmpty()) {
        updateStatus(tr("No library folder selected"));
        return;
    }

    if (!repository_.removeLibraryRoot(id)) {
        updateStatus(tr("Unable to remove library folder"));
        return;
    }

    loadLibraryRoots();
    updateStatus(tr("Removed library folder; indexed resources were kept"));
}

void PinloomPanel::refreshSelectedRoot()
{
    const QString id = selectedRootId();
    if (id.isEmpty()) {
        updateStatus(tr("No library folder selected"));
        return;
    }

    const std::optional<LibraryRoot> root = repository_.findLibraryRoot(id);
    if (!root.has_value()) {
        loadLibraryRoots();
        updateStatus(tr("Library folder no longer exists"));
        return;
    }

    IndexingService indexer(repository_);
    if (!indexer.indexRoot(root.value())) {
        QMessageBox::warning(this, tr("Indexing failed"), indexer.lastError());
        updateStatus(indexer.lastError());
        return;
    }

    loadLibraryRoots();
    selectLibraryRoot(id);
    refreshResults();
    updateStatus(tr("Indexed %n resource(s)", nullptr, indexer.lastIndexedCount()));
}

void PinloomPanel::refreshAllRoots()
{
    IndexingService indexer(repository_);
    if (!indexer.indexEnabledRoots()) {
        QMessageBox::warning(this, tr("Indexing failed"), indexer.lastError());
        updateStatus(indexer.lastError());
        return;
    }

    loadLibraryRoots();
    refreshResults();
    updateStatus(tr("Indexed %n resource(s)", nullptr, indexer.lastIndexedCount()));
}

void PinloomPanel::rebuildAllRoots()
{
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        tr("Rebuild Index"),
        tr("Clear indexed resources and rebuild from enabled library folders?"));
    if (answer != QMessageBox::Yes) {
        return;
    }

    IndexingService indexer(repository_);
    if (!indexer.rebuildEnabledRoots()) {
        QMessageBox::warning(this, tr("Rebuild failed"), indexer.lastError());
        updateStatus(indexer.lastError());
        return;
    }

    loadLibraryRoots();
    refreshResults();
    updateStatus(tr("Rebuilt %n resource(s)", nullptr, indexer.lastIndexedCount()));
}

void PinloomPanel::refreshResults()
{
    resultList_->clear();

    SearchQuery query;
    query.text = searchEdit_->text();
    query.limit = 100;

    const QList<SearchResult> results = repository_.search(query);
    for (const SearchResult &result : results) {
        auto *item = new QListWidgetItem(resultText(result), resultList_);
        item->setToolTip(result.resource.location);
        item->setSizeHint(QSize(0, resultList_->fontMetrics().lineSpacing() * 2 + 12));
        item->setData(Qt::UserRole, result.resource.id);
        item->setData(Qt::UserRole + 1, result.resource.location);
        if (result.matchedAnchor.has_value()) {
            const Anchor &anchor = result.matchedAnchor.value();
            item->setData(Qt::UserRole + 2, true);
            item->setData(Qt::UserRole + 3, anchor.line);
            item->setData(Qt::UserRole + 4, anchor.target);
            item->setData(Qt::UserRole + 5, static_cast<int>(anchor.type));
        }
    }

    updateStatus(tr("%n result(s)", nullptr, results.size()));
}

void PinloomPanel::openSelectedResource()
{
    QListWidgetItem *item = resultList_->currentItem();
    if (!item) {
        updateStatus(tr("No resource selected"));
        return;
    }

    const QString location = item->data(Qt::UserRole + 1).toString();
    if (location.isEmpty()) {
        updateStatus(tr("No resource selected"));
        return;
    }

    if (item->data(Qt::UserRole + 2).toBool() && item->data(Qt::UserRole + 3).toInt() > 0) {
        TextPreviewDialog preview(location, item->data(Qt::UserRole + 3).toInt(), this);
        if (!preview.load()) {
            updateStatus(tr("Unable to preview %1").arg(location));
            return;
        }
        preview.exec();
        return;
    }

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(location))) {
        updateStatus(tr("Unable to open %1").arg(location));
    }
}

void PinloomPanel::openResultItem(QListWidgetItem *item)
{
    if (!item) {
        return;
    }

    resultList_->setCurrentItem(item);
    openSelectedResource();
}

void PinloomPanel::loadLibraryRoots()
{
    const QString currentId = selectedRootId();
    rootList_->clear();

    for (const LibraryRoot &root : repository_.libraryRoots()) {
        auto *item = new QListWidgetItem(rootItemText(root), rootList_);
        item->setData(Qt::UserRole, root.id);
        item->setData(Qt::UserRole + 1, root.path);
        item->setToolTip(root.path);
    }

    if (!currentId.isEmpty()) {
        selectLibraryRoot(currentId);
    }
    if (!rootList_->currentItem() && rootList_->count() > 0) {
        rootList_->setCurrentRow(0);
    }
}

void PinloomPanel::selectLibraryRoot(const QString &id)
{
    for (int row = 0; row < rootList_->count(); ++row) {
        QListWidgetItem *item = rootList_->item(row);
        if (item->data(Qt::UserRole).toString() == id) {
            rootList_->setCurrentItem(item);
            return;
        }
    }
}

void PinloomPanel::updateStatus(const QString &message)
{
    statusLabel_->setText(message);
}

QString PinloomPanel::selectedRootId() const
{
    const QListWidgetItem *item = rootList_->currentItem();
    if (!item) {
        return {};
    }
    return item->data(Qt::UserRole).toString();
}

QString PinloomPanel::selectedLocation() const
{
    const QListWidgetItem *item = resultList_->currentItem();
    if (!item) {
        return {};
    }
    return item->data(Qt::UserRole + 1).toString();
}

} // namespace Pinloom
