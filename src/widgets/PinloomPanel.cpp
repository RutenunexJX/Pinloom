#include "pinloom/widgets/PinloomPanel.h"

#include "pinloom/core/DirectoryLibrarySource.h"
#include "pinloom/core/IndexingService.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace Pinloom {

PinloomPanel::PinloomPanel(ILibraryRepository &repository, QWidget *parent)
    : QWidget(parent)
    , repository_(repository)
{
    auto *layout = new QVBoxLayout(this);

    auto *toolbar = new QHBoxLayout();
    auto *addRootButton = new QPushButton(tr("Add Folder"), this);
    refreshButton_ = new QPushButton(tr("Refresh Index"), this);
    openButton_ = new QPushButton(tr("Open"), this);

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText(tr("Search resources, tags, aliases, anchors"));

    resultList_ = new QListWidget(this);
    statusLabel_ = new QLabel(this);

    toolbar->addWidget(addRootButton);
    toolbar->addWidget(refreshButton_);
    toolbar->addWidget(openButton_);
    toolbar->addStretch(1);

    layout->addLayout(toolbar);
    layout->addWidget(searchEdit_);
    layout->addWidget(resultList_, 1);
    layout->addWidget(statusLabel_);

    connect(addRootButton, &QPushButton::clicked, this, &PinloomPanel::addLibraryRoot);
    connect(refreshButton_, &QPushButton::clicked, this, &PinloomPanel::refreshIndex);
    connect(openButton_, &QPushButton::clicked, this, &PinloomPanel::openSelectedResource);
    connect(searchEdit_, &QLineEdit::textChanged, this, &PinloomPanel::refreshResults);
    connect(resultList_, &QListWidget::itemDoubleClicked, this, &PinloomPanel::openResultItem);

    refreshResults();
}

void PinloomPanel::addLibraryRoot()
{
    const QString root = QFileDialog::getExistingDirectory(this, tr("Add Library Folder"));
    if (root.isEmpty()) {
        return;
    }
    if (!libraryRoots_.contains(root, Qt::CaseInsensitive)) {
        libraryRoots_.append(root);
    }
    refreshIndex();
}

void PinloomPanel::refreshIndex()
{
    if (libraryRoots_.isEmpty()) {
        updateStatus(tr("No library folder selected"));
        return;
    }

    int totalIndexed = 0;
    for (const QString &root : libraryRoots_) {
        DirectoryLibrarySource source(root);
        IndexingService indexer(repository_);
        if (!indexer.index(source)) {
            QMessageBox::warning(this, tr("Indexing failed"), indexer.lastError());
            updateStatus(indexer.lastError());
            return;
        }
        totalIndexed += indexer.lastIndexedCount();
    }

    refreshResults();
    updateStatus(tr("Indexed %n resource(s) from %1 folder(s)", nullptr, totalIndexed)
                     .arg(libraryRoots_.size()));
}

void PinloomPanel::refreshResults()
{
    resultList_->clear();

    SearchQuery query;
    query.text = searchEdit_->text();
    query.limit = 100;

    const QList<SearchResult> results = repository_.search(query);
    for (const SearchResult &result : results) {
        auto *item = new QListWidgetItem(QStringLiteral("%1  |  %2  |  %3")
                                             .arg(result.resource.title,
                                                  result.resource.location,
                                                  result.matchedField),
                                         resultList_);
        item->setData(Qt::UserRole, result.resource.id);
        item->setData(Qt::UserRole + 1, result.resource.location);
    }

    updateStatus(tr("%n result(s)", nullptr, results.size()));
}

void PinloomPanel::openSelectedResource()
{
    const QString location = selectedLocation();
    if (location.isEmpty()) {
        updateStatus(tr("No resource selected"));
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

    const QString location = item->data(Qt::UserRole + 1).toString();
    if (!location.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(location));
    }
}

void PinloomPanel::updateStatus(const QString &message)
{
    statusLabel_->setText(message);
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
