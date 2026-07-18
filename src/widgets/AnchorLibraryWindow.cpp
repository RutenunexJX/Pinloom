#include "pinloom/widgets/AnchorLibraryWindow.h"

#include "pinloom/core/AnchorLocator.h"
#include "pinloom/widgets/AnchorLocatorPreviewWidget.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace Pinloom {

namespace {

constexpr int FileKeyRole = Qt::UserRole + 1;
constexpr int ResourceIdRole = Qt::UserRole + 2;
constexpr int AnchorIdentityRole = Qt::UserRole + 3;
constexpr int SortValueRole = Qt::UserRole + 4;

enum class AnchorLibraryScope {
    All = 0,
    Untagged = 1,
    Missing = 2,
    Trash = 3,
    Duplicates = 4,
    InvalidLocator = 5,
    RecentlyModified = 6,
    RecentlyDeleted = 7
};

enum class UsageFilter {
    Any = 0,
    Pinned = 1,
    RecentlyOpened = 2,
    NeverOpened = 3
};

QString resourceKindLabel(ResourceKind kind)
{
    switch (kind) {
    case ResourceKind::File: return QStringLiteral("File");
    case ResourceKind::Folder: return QStringLiteral("Folder");
    case ResourceKind::Pdf: return QStringLiteral("PDF");
    case ResourceKind::TextSnippet: return QStringLiteral("Text");
    case ResourceKind::Url: return QStringLiteral("URL");
    case ResourceKind::Note: return QStringLiteral("Note");
    case ResourceKind::ManualAnchor: return QStringLiteral("Manual");
    case ResourceKind::Unknown: return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

QString issueKindLabel(AnchorLibraryIssueKind kind)
{
    switch (kind) {
    case AnchorLibraryIssueKind::MissingTarget: return QStringLiteral("Missing target");
    case AnchorLibraryIssueKind::DuplicateResource: return QStringLiteral("Duplicate file");
    case AnchorLibraryIssueKind::DuplicateAnchor: return QStringLiteral("Duplicate anchor");
    case AnchorLibraryIssueKind::InvalidLocator: return QStringLiteral("Invalid locator");
    }
    return QStringLiteral("Issue");
}

void appendUnique(QStringList &values, const QString &value)
{
    const QString cleaned = value.trimmed();
    if (!cleaned.isEmpty() && !values.contains(cleaned, Qt::CaseInsensitive)) {
        values.append(cleaned);
    }
}

QStringList editorValues(const QString &text)
{
    QStringList values;
    QString separated = text;
    separated.replace(QLatin1Char('\r'), QLatin1Char(','));
    separated.replace(QLatin1Char('\n'), QLatin1Char(','));
    for (const QString &part : separated.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        appendUnique(values, part);
    }
    return values;
}

QStringList fileTags(const AnchorLibraryFile &file, const QList<AnchorLibraryAnchor> &anchors)
{
    QStringList tags;
    for (const QString &tag : file.resource.tags) {
        appendUnique(tags, tag);
    }
    for (const AnchorLibraryAnchor &entry : anchors) {
        for (const QString &tag : entry.anchor.tags) {
            appendUnique(tags, tag);
        }
    }
    return tags;
}

QDateTime lastMarkedAt(const QList<AnchorLibraryAnchor> &anchors)
{
    QDateTime latest;
    for (const AnchorLibraryAnchor &entry : anchors) {
        const QDateTime candidate = entry.anchor.updatedAt.isValid()
            ? entry.anchor.updatedAt
            : entry.anchor.createdAt;
        if (candidate.isValid() && (!latest.isValid() || candidate > latest)) {
            latest = candidate;
        }
    }
    return latest;
}

QDateTime lastOpenedAt(const AnchorLibraryFile &file)
{
    QDateTime latest = file.usage.lastOpenedAt;
    for (const AnchorLibraryAnchor &entry : file.anchors) {
        if (entry.usage.lastOpenedAt.isValid()
            && (!latest.isValid() || entry.usage.lastOpenedAt > latest)) {
            latest = entry.usage.lastOpenedAt;
        }
    }
    return latest;
}

int totalOpenCount(const AnchorLibraryFile &file)
{
    int count = file.usage.openCount;
    for (const AnchorLibraryAnchor &entry : file.anchors) {
        count += entry.usage.openCount;
    }
    return count;
}

bool isPinned(const AnchorLibraryFile &file)
{
    if (file.usage.pinned) {
        return true;
    }
    return std::any_of(file.anchors.cbegin(), file.anchors.cend(), [](const AnchorLibraryAnchor &entry) {
        return entry.anchor.pinned;
    });
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

QString fileDisplayName(const AnchorLibraryFile &file)
{
    if (!file.resource.title.trimmed().isEmpty()) {
        return file.resource.title.trimmed();
    }
    const QString name = QFileInfo(file.resource.location).fileName().trimmed();
    return name.isEmpty() ? file.resource.location : name;
}

QString anchorDisplayName(const Anchor &anchor)
{
    return anchor.name.trimmed().isEmpty() ? anchor.id : anchor.name.trimmed();
}

QString fileLocationLabel(const AnchorLibraryFile &file)
{
    const QString location = file.resource.location.trimmed();
    return location.contains(QStringLiteral("://")) ? location : QDir::toNativeSeparators(location);
}

QString fileGroupingKey(const Resource &resource)
{
    const QString state = resource.deleted ? QStringLiteral("archived:") : QStringLiteral("active:");
    const QString location = resource.location.trimmed();
    if (location.isEmpty()) {
        return state + QStringLiteral("resource:%1").arg(resource.id);
    }
    if (location.contains(QStringLiteral("://"))) {
        return state + QStringLiteral("url:%1").arg(location.toCaseFolded());
    }
    const QString path = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(location).absoluteFilePath()));
    return state + QStringLiteral("path:%1").arg(path.toCaseFolded());
}

void mergeResourceMetadata(Resource &target, const Resource &source)
{
    if (target.title.trimmed().isEmpty() && !source.title.trimmed().isEmpty()) {
        target.title = source.title;
    }
    if (target.kind == ResourceKind::Unknown && source.kind != ResourceKind::Unknown) {
        target.kind = source.kind;
    }
    for (const QString &alias : source.aliases) appendUnique(target.aliases, alias);
    for (const QString &tag : source.tags) appendUnique(target.tags, tag);
    if (source.updatedAt > target.updatedAt) target.updatedAt = source.updatedAt;
}

void mergeUsage(ResourceUsage &target, const ResourceUsage &source)
{
    target.openCount += source.openCount;
    target.pinned = target.pinned || source.pinned;
    if (source.lastOpenedAt > target.lastOpenedAt) target.lastOpenedAt = source.lastOpenedAt;
}

void appendUniqueAnchor(AnchorLibraryFile &file, const AnchorLibraryAnchor &entry)
{
    for (const AnchorLibraryAnchor &existing : file.anchors) {
        if (existing.resourceId == entry.resourceId
            && anchorIdentityKey(existing.anchor) == anchorIdentityKey(entry.anchor)) {
            return;
        }
    }
    file.anchors.append(entry);
}

bool containsText(const QString &value, const QString &needle)
{
    return value.contains(needle, Qt::CaseInsensitive);
}

bool sameText(const QString &left, const QString &right)
{
    return left.compare(right, Qt::CaseInsensitive) == 0;
}

bool localTargetExists(const Resource &resource)
{
    return resource.location.trimmed().isEmpty()
        || resource.kind == ResourceKind::Url
        || resource.location.contains(QStringLiteral("://"))
        || QFileInfo::exists(resource.location);
}

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

QFrame *horizontalRule(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

QString viewSettingsKey(const QString &name)
{
    return QStringLiteral("anchorLibrary/savedViews/%1")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(name.trimmed())));
}

QString compactLocator(const AnchorLocatorUpdate &locator)
{
    return QStringLiteral("%1\n%2").arg(locator.locatorType, locator.locatorJson);
}

} // namespace

AnchorLibraryWindow::AnchorLibraryWindow(AnchorLibraryWindowOptions options, QWidget *parent)
    : QMainWindow(parent)
    , options_(std::move(options))
{
    setObjectName(QStringLiteral("anchorLibraryWindow"));
    setWindowTitle(tr("Pinloom Anchor Library"));
    setMinimumSize(920, 600);
    resize(1380, 820);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(10, 10, 10, 8);
    layout->setSpacing(7);

    auto *queryRow = new QHBoxLayout;
    queryRow->setSpacing(6);
    filterEdit_ = new QLineEdit(central);
    filterEdit_->setObjectName(QStringLiteral("anchorLibraryFilterEdit"));
    filterEdit_->setPlaceholderText(tr("Search marked files, anchors, tags, paths, and locators"));
    filterEdit_->setClearButtonEnabled(true);
    savedViewCombo_ = new QComboBox(central);
    savedViewCombo_->setObjectName(QStringLiteral("anchorLibrarySavedViewCombo"));
    savedViewCombo_->setMinimumWidth(145);
    scopeCombo_ = new QComboBox(central);
    scopeCombo_->setObjectName(QStringLiteral("anchorLibraryScopeCombo"));
    scopeCombo_->addItem(tr("All marked files"), static_cast<int>(AnchorLibraryScope::All));
    scopeCombo_->addItem(tr("Untagged"), static_cast<int>(AnchorLibraryScope::Untagged));
    scopeCombo_->addItem(tr("Missing files"), static_cast<int>(AnchorLibraryScope::Missing));
    scopeCombo_->addItem(tr("Trash"), static_cast<int>(AnchorLibraryScope::Trash));
    scopeCombo_->addItem(tr("Duplicates"), static_cast<int>(AnchorLibraryScope::Duplicates));
    scopeCombo_->addItem(tr("Invalid locators"), static_cast<int>(AnchorLibraryScope::InvalidLocator));
    scopeCombo_->addItem(tr("Recently modified"), static_cast<int>(AnchorLibraryScope::RecentlyModified));
    scopeCombo_->addItem(tr("Recently deleted"), static_cast<int>(AnchorLibraryScope::RecentlyDeleted));
    refreshButton_ = new QToolButton(central);
    refreshButton_->setObjectName(QStringLiteral("anchorLibraryRefreshButton"));
    refreshButton_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    refreshButton_->setToolTip(tr("Refresh library"));
    refreshButton_->setAccessibleName(refreshButton_->toolTip());
    queryRow->addWidget(filterEdit_, 1);
    queryRow->addWidget(savedViewCombo_);
    queryRow->addWidget(scopeCombo_);
    queryRow->addWidget(refreshButton_);

    auto *filterRow = new QGridLayout;
    filterRow->setHorizontalSpacing(6);
    filterRow->setVerticalSpacing(6);
    tagFilterCombo_ = new QComboBox(central);
    tagFilterCombo_->setObjectName(QStringLiteral("anchorLibraryTagFilterCombo"));
    tagFilterCombo_->setMinimumWidth(120);
    kindFilterCombo_ = new QComboBox(central);
    kindFilterCombo_->setObjectName(QStringLiteral("anchorLibraryKindFilterCombo"));
    appFilterCombo_ = new QComboBox(central);
    appFilterCombo_->setObjectName(QStringLiteral("anchorLibraryAppFilterCombo"));
    appFilterCombo_->setMinimumWidth(120);
    directoryFilterEdit_ = new QLineEdit(central);
    directoryFilterEdit_->setObjectName(QStringLiteral("anchorLibraryDirectoryFilterEdit"));
    directoryFilterEdit_->setPlaceholderText(tr("Directory"));
    directoryFilterEdit_->setClearButtonEnabled(true);
    timeFilterCombo_ = new QComboBox(central);
    timeFilterCombo_->setObjectName(QStringLiteral("anchorLibraryTimeFilterCombo"));
    timeFilterCombo_->addItem(tr("Any time"), 0);
    timeFilterCombo_->addItem(tr("Last 7 days"), 7);
    timeFilterCombo_->addItem(tr("Last 30 days"), 30);
    timeFilterCombo_->addItem(tr("Last 90 days"), 90);
    usageFilterCombo_ = new QComboBox(central);
    usageFilterCombo_->setObjectName(QStringLiteral("anchorLibraryUsageFilterCombo"));
    usageFilterCombo_->addItem(tr("Any usage"), static_cast<int>(UsageFilter::Any));
    usageFilterCombo_->addItem(tr("Pinned"), static_cast<int>(UsageFilter::Pinned));
    usageFilterCombo_->addItem(tr("Recently opened"), static_cast<int>(UsageFilter::RecentlyOpened));
    usageFilterCombo_->addItem(tr("Never opened"), static_cast<int>(UsageFilter::NeverOpened));
    filterRow->addWidget(tagFilterCombo_, 0, 0);
    filterRow->addWidget(kindFilterCombo_, 0, 1);
    filterRow->addWidget(appFilterCombo_, 0, 2);
    filterRow->addWidget(directoryFilterEdit_, 0, 3);
    filterRow->addWidget(timeFilterCombo_, 1, 0);
    filterRow->addWidget(usageFilterCombo_, 1, 1);
    filterRow->setColumnStretch(3, 1);

    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(6);
    jumpButton_ = new QToolButton(central);
    jumpButton_->setObjectName(QStringLiteral("anchorLibraryJumpButton"));
    jumpButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    jumpButton_->setToolTip(tr("Open selected anchor"));
    deleteButton_ = new QToolButton(central);
    deleteButton_->setObjectName(QStringLiteral("anchorLibraryDeleteButton"));
    deleteButton_->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    deleteButton_->setToolTip(tr("Move selected anchors to Trash"));
    restoreButton_ = new QToolButton(central);
    restoreButton_->setObjectName(QStringLiteral("anchorLibraryRestoreButton"));
    restoreButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    restoreButton_->setToolTip(tr("Restore selection"));
    purgeButton_ = new QToolButton(central);
    purgeButton_->setObjectName(QStringLiteral("anchorLibraryPurgeButton"));
    purgeButton_->setText(tr("Purge"));
    purgeButton_->setToolTip(tr("Permanently delete the selected Trash records"));
    tagsButton_ = new QToolButton(central);
    tagsButton_->setObjectName(QStringLiteral("anchorLibraryTagsButton"));
    tagsButton_->setText(tr("Anchors"));
    tagsButton_->setPopupMode(QToolButton::InstantPopup);
    auto *anchorMenu = new QMenu(tagsButton_);
    QAction *addAnchorTags = anchorMenu->addAction(tr("Add tags"));
    QAction *removeAnchorTags = anchorMenu->addAction(tr("Remove tags"));
    anchorMenu->addSeparator();
    QAction *pinAnchors = anchorMenu->addAction(tr("Pin"));
    QAction *unpinAnchors = anchorMenu->addAction(tr("Unpin"));
    anchorMenu->addSeparator();
    QAction *tagManager = anchorMenu->addAction(tr("Tag manager"));
    tagsButton_->setMenu(anchorMenu);
    fileActionsButton_ = new QToolButton(central);
    fileActionsButton_->setObjectName(QStringLiteral("anchorLibraryFileActionsButton"));
    fileActionsButton_->setText(tr("Files"));
    fileActionsButton_->setPopupMode(QToolButton::InstantPopup);
    auto *fileMenu = new QMenu(fileActionsButton_);
    QAction *archiveFiles = fileMenu->addAction(tr("Move records to Trash"));
    QAction *restoreFiles = fileMenu->addAction(tr("Restore records"));
    fileMenu->addSeparator();
    QAction *addFileTags = fileMenu->addAction(tr("Add tags"));
    QAction *removeFileTags = fileMenu->addAction(tr("Remove tags"));
    QAction *pinFiles = fileMenu->addAction(tr("Pin"));
    QAction *unpinFiles = fileMenu->addAction(tr("Unpin"));
    fileMenu->addSeparator();
    QAction *autoRelink = fileMenu->addAction(tr("Auto-find missing files"));
    QAction *deduplicateAnchors = fileMenu->addAction(tr("Merge duplicate anchors"));
    fileActionsButton_->setMenu(fileMenu);
    relinkButton_ = new QToolButton(central);
    relinkButton_->setObjectName(QStringLiteral("anchorLibraryRelinkButton"));
    relinkButton_->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    relinkButton_->setToolTip(tr("Relink selected file"));
    mergeButton_ = new QToolButton(central);
    mergeButton_->setObjectName(QStringLiteral("anchorLibraryMergeButton"));
    mergeButton_->setText(tr("Merge"));
    mergeButton_->setToolTip(tr("Merge duplicate file records"));
    integrityButton_ = new QToolButton(central);
    integrityButton_->setObjectName(QStringLiteral("anchorLibraryIntegrityButton"));
    integrityButton_->setText(tr("Inspect"));
    integrityButton_->setToolTip(tr("Scan library integrity"));
    dataButton_ = new QToolButton(central);
    dataButton_->setObjectName(QStringLiteral("anchorLibraryDataButton"));
    dataButton_->setText(tr("Data"));
    dataButton_->setPopupMode(QToolButton::InstantPopup);
    auto *dataMenu = new QMenu(dataButton_);
    QAction *saveView = dataMenu->addAction(tr("Save current view"));
    QAction *deleteView = dataMenu->addAction(tr("Delete current saved view"));
    QAction *operationHistory = dataMenu->addAction(tr("Operation history"));
    dataMenu->addSeparator();
    QAction *exportJson = dataMenu->addAction(tr("Export JSON"));
    QAction *importMerge = dataMenu->addAction(tr("Import JSON and merge"));
    QAction *importReplace = dataMenu->addAction(tr("Import JSON and replace"));
    dataMenu->addSeparator();
    QAction *backupDatabase = dataMenu->addAction(tr("Back up SQLite database"));
    QAction *restoreDatabase = dataMenu->addAction(tr("Restore SQLite database"));
    dataButton_->setMenu(dataMenu);
    undoButton_ = new QToolButton(central);
    undoButton_->setObjectName(QStringLiteral("anchorLibraryUndoButton"));
    undoButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    undoButton_->setToolTip(tr("Undo last library operation"));
    for (QToolButton *button : {jumpButton_, deleteButton_, restoreButton_, purgeButton_, tagsButton_,
                                fileActionsButton_, relinkButton_, mergeButton_, integrityButton_,
                                dataButton_, undoButton_}) {
        button->setAccessibleName(button->toolTip().isEmpty() ? button->text() : button->toolTip());
        actionRow->addWidget(button);
    }
    actionRow->addStretch(1);

    auto *mainSplitter = new QSplitter(Qt::Horizontal, central);
    mainSplitter->setObjectName(QStringLiteral("anchorLibraryMainSplitter"));
    mainSplitter->setChildrenCollapsible(false);
    auto *tablesSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    tablesSplitter->setObjectName(QStringLiteral("anchorLibraryTablesSplitter"));
    tablesSplitter->setChildrenCollapsible(false);

    fileTable_ = new QTableWidget(tablesSplitter);
    fileTable_->setObjectName(QStringLiteral("anchorLibraryFileTable"));
    fileTable_->setColumnCount(9);
    fileTable_->setHorizontalHeaderLabels({tr("File"), tr("Location"), tr("Type"), tr("Anchors"),
                                           tr("Tags"), tr("Last marked"), tr("Status"), tr("Opens"),
                                           tr("Last opened")});
    fileTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fileTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fileTable_->setAlternatingRowColors(true);
    fileTable_->setSortingEnabled(false);
    fileTable_->verticalHeader()->setVisible(false);
    fileTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    fileTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    fileTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    for (int column : {2, 3, 5, 6, 7, 8}) {
        fileTable_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    fileTable_->horizontalHeader()->setSortIndicatorShown(true);
    fileTable_->horizontalHeader()->setSortIndicator(5, Qt::DescendingOrder);

    anchorTable_ = new QTableWidget(tablesSplitter);
    anchorTable_->setObjectName(QStringLiteral("anchorLibraryAnchorTable"));
    anchorTable_->setColumnCount(8);
    anchorTable_->setHorizontalHeaderLabels({tr("Anchor"), tr("Aliases"), tr("Tags"), tr("Locator"),
                                             tr("Updated"), tr("Opens"), tr("Last opened"), tr("Validity")});
    anchorTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    anchorTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    anchorTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    anchorTable_->setAlternatingRowColors(true);
    anchorTable_->setSortingEnabled(false);
    anchorTable_->verticalHeader()->setVisible(false);
    for (int column : {0, 1, 2, 3}) {
        anchorTable_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Stretch);
    }
    for (int column : {4, 5, 6, 7}) {
        anchorTable_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    anchorTable_->horizontalHeader()->setSortIndicatorShown(true);
    anchorTable_->horizontalHeader()->setSortIndicator(4, Qt::DescendingOrder);
    tablesSplitter->addWidget(fileTable_);
    tablesSplitter->addWidget(anchorTable_);
    tablesSplitter->setStretchFactor(0, 3);
    tablesSplitter->setStretchFactor(1, 2);

    auto *inspectorScroll = new QScrollArea(mainSplitter);
    inspectorScroll->setObjectName(QStringLiteral("anchorLibraryInspectorScroll"));
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setMinimumWidth(310);
    inspectorScroll->setMaximumWidth(430);
    auto *inspector = new QWidget(inspectorScroll);
    inspector->setObjectName(QStringLiteral("anchorLibraryInspector"));
    auto *inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(10, 6, 10, 8);
    inspectorLayout->setSpacing(7);
    selectionLabel_ = new QLabel(inspector);
    selectionLabel_->setObjectName(QStringLiteral("anchorLibrarySelectionLabel"));
    selectionLabel_->setWordWrap(true);
    inspectorLayout->addWidget(selectionLabel_);
    inspectorLayout->addWidget(sectionLabel(tr("File"), inspector));
    auto *fileForm = new QFormLayout;
    fileTitleEdit_ = new QLineEdit(inspector);
    fileTitleEdit_->setObjectName(QStringLiteral("anchorLibraryFileTitleEdit"));
    fileAliasesEdit_ = new QLineEdit(inspector);
    fileAliasesEdit_->setObjectName(QStringLiteral("anchorLibraryFileAliasesEdit"));
    fileTagsEdit_ = new QLineEdit(inspector);
    fileTagsEdit_->setObjectName(QStringLiteral("anchorLibraryFileTagsEdit"));
    fileLocationEdit_ = new QLineEdit(inspector);
    fileLocationEdit_->setObjectName(QStringLiteral("anchorLibraryFileLocationEdit"));
    fileLocationEdit_->setReadOnly(true);
    filePinnedCheck_ = new QCheckBox(tr("Pinned"), inspector);
    filePinnedCheck_->setObjectName(QStringLiteral("anchorLibraryFilePinnedCheck"));
    fileForm->addRow(tr("Title"), fileTitleEdit_);
    fileForm->addRow(tr("Aliases"), fileAliasesEdit_);
    fileForm->addRow(tr("Tags"), fileTagsEdit_);
    fileForm->addRow(tr("Location"), fileLocationEdit_);
    fileForm->addRow(QString(), filePinnedCheck_);
    inspectorLayout->addLayout(fileForm);
    saveFileButton_ = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Save file"), inspector);
    saveFileButton_->setObjectName(QStringLiteral("anchorLibrarySaveFileButton"));
    inspectorLayout->addWidget(saveFileButton_);
    inspectorLayout->addWidget(horizontalRule(inspector));
    inspectorLayout->addWidget(sectionLabel(tr("Anchor"), inspector));
    auto *anchorForm = new QFormLayout;
    anchorNameEdit_ = new QLineEdit(inspector);
    anchorNameEdit_->setObjectName(QStringLiteral("anchorLibraryAnchorNameEdit"));
    anchorAliasesEdit_ = new QLineEdit(inspector);
    anchorAliasesEdit_->setObjectName(QStringLiteral("anchorLibraryAnchorAliasesEdit"));
    anchorTagsEdit_ = new QLineEdit(inspector);
    anchorTagsEdit_->setObjectName(QStringLiteral("anchorLibraryAnchorTagsEdit"));
    anchorPinnedCheck_ = new QCheckBox(tr("Pinned"), inspector);
    anchorPinnedCheck_->setObjectName(QStringLiteral("anchorLibraryAnchorPinnedCheck"));
    anchorForm->addRow(tr("Name"), anchorNameEdit_);
    anchorForm->addRow(tr("Aliases"), anchorAliasesEdit_);
    anchorForm->addRow(tr("Tags"), anchorTagsEdit_);
    anchorForm->addRow(QString(), anchorPinnedCheck_);
    inspectorLayout->addLayout(anchorForm);
    saveAnchorButton_ = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Save anchor"), inspector);
    saveAnchorButton_->setObjectName(QStringLiteral("anchorLibrarySaveAnchorButton"));
    inspectorLayout->addWidget(saveAnchorButton_);
    inspectorLayout->addWidget(horizontalRule(inspector));
    inspectorLayout->addWidget(sectionLabel(tr("Locator"), inspector));
    auto *locatorForm = new QFormLayout;
    targetAppEdit_ = new QLineEdit(inspector);
    targetAppEdit_->setObjectName(QStringLiteral("anchorLibraryTargetAppEdit"));
    targetFileEdit_ = new QLineEdit(inspector);
    targetFileEdit_->setObjectName(QStringLiteral("anchorLibraryTargetFileEdit"));
    targetUriEdit_ = new QLineEdit(inspector);
    targetUriEdit_->setObjectName(QStringLiteral("anchorLibraryTargetUriEdit"));
    locatorTypeEdit_ = new QLineEdit(inspector);
    locatorTypeEdit_->setObjectName(QStringLiteral("anchorLibraryLocatorTypeEdit"));
    locatorJsonEdit_ = new QPlainTextEdit(inspector);
    locatorJsonEdit_->setObjectName(QStringLiteral("anchorLibraryLocatorJsonEdit"));
    locatorJsonEdit_->setMaximumHeight(90);
    locatorForm->addRow(tr("Application"), targetAppEdit_);
    locatorForm->addRow(tr("Target file"), targetFileEdit_);
    locatorForm->addRow(tr("Target URI"), targetUriEdit_);
    locatorForm->addRow(tr("Type"), locatorTypeEdit_);
    locatorForm->addRow(tr("JSON"), locatorJsonEdit_);
    inspectorLayout->addLayout(locatorForm);
    auto *locatorActions = new QGridLayout;
    locatorActions->setHorizontalSpacing(6);
    locatorActions->setVerticalSpacing(6);
    saveLocatorButton_ = new QPushButton(tr("Save"), inspector);
    saveLocatorButton_->setObjectName(QStringLiteral("anchorLibrarySaveLocatorButton"));
    validateLocatorButton_ = new QPushButton(tr("Validate"), inspector);
    validateLocatorButton_->setObjectName(QStringLiteral("anchorLibraryValidateLocatorButton"));
    previewLocatorButton_ = new QPushButton(tr("Preview"), inspector);
    previewLocatorButton_->setObjectName(QStringLiteral("anchorLibraryPreviewLocatorButton"));
    recaptureLocatorButton_ = new QPushButton(tr("Recapture"), inspector);
    recaptureLocatorButton_->setObjectName(QStringLiteral("anchorLibraryRecaptureLocatorButton"));
    locatorActions->addWidget(saveLocatorButton_, 0, 0);
    locatorActions->addWidget(validateLocatorButton_, 0, 1);
    locatorActions->addWidget(previewLocatorButton_, 1, 0);
    locatorActions->addWidget(recaptureLocatorButton_, 1, 1);
    inspectorLayout->addLayout(locatorActions);
    locatorPreview_ = new AnchorLocatorPreviewWidget(inspector);
    inspectorLayout->addWidget(locatorPreview_);
    inspectorLayout->addStretch(1);
    inspectorScroll->setWidget(inspector);
    mainSplitter->addWidget(tablesSplitter);
    mainSplitter->addWidget(inspectorScroll);
    mainSplitter->setStretchFactor(0, 4);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({1000, 360});

    statusLabel_ = new QLabel(central);
    statusLabel_->setObjectName(QStringLiteral("anchorLibraryStatusLabel"));
    layout->addLayout(queryRow);
    layout->addLayout(filterRow);
    layout->addLayout(actionRow);
    layout->addWidget(mainSplitter, 1);
    layout->addWidget(statusLabel_);
    setCentralWidget(central);

    connect(filterEdit_, &QLineEdit::textChanged, this, &AnchorLibraryWindow::applyFilter);
    connect(scopeCombo_, &QComboBox::currentIndexChanged, this, &AnchorLibraryWindow::applyFilter);
    connect(tagFilterCombo_, &QComboBox::currentIndexChanged, this, &AnchorLibraryWindow::applyFilter);
    connect(kindFilterCombo_, &QComboBox::currentIndexChanged, this, &AnchorLibraryWindow::applyFilter);
    connect(appFilterCombo_, &QComboBox::currentIndexChanged, this, &AnchorLibraryWindow::applyFilter);
    connect(timeFilterCombo_, &QComboBox::currentIndexChanged, this, &AnchorLibraryWindow::applyFilter);
    connect(usageFilterCombo_, &QComboBox::currentIndexChanged, this, &AnchorLibraryWindow::applyFilter);
    connect(directoryFilterEdit_, &QLineEdit::textChanged, this, &AnchorLibraryWindow::applyFilter);
    connect(savedViewCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const QString name = savedViewCombo_->itemData(index).toString();
        if (!name.isEmpty()) loadSavedView(name);
    });
    connect(refreshButton_, &QToolButton::clicked, this, &AnchorLibraryWindow::refreshLibrary);
    connect(jumpButton_, &QToolButton::clicked, this, &AnchorLibraryWindow::activateSelectedAnchor);
    connect(deleteButton_, &QToolButton::clicked, this, &AnchorLibraryWindow::deleteSelectedAnchors);
    connect(restoreButton_, &QToolButton::clicked, this, [this]() {
        if (!restoreSelectedFiles()) restoreSelectedAnchors();
    });
    connect(purgeButton_, &QToolButton::clicked, this, &AnchorLibraryWindow::permanentlyDeleteSelection);
    connect(addAnchorTags, &QAction::triggered, this, [this]() { promptAnchorTagUpdate(false); });
    connect(removeAnchorTags, &QAction::triggered, this, [this]() { promptAnchorTagUpdate(true); });
    connect(pinAnchors, &QAction::triggered, this, [this]() { setSelectedAnchorsPinned(true); });
    connect(unpinAnchors, &QAction::triggered, this, [this]() { setSelectedAnchorsPinned(false); });
    connect(tagManager, &QAction::triggered, this, &AnchorLibraryWindow::promptTagManager);
    connect(archiveFiles, &QAction::triggered, this, &AnchorLibraryWindow::archiveSelectedFiles);
    connect(restoreFiles, &QAction::triggered, this, &AnchorLibraryWindow::restoreSelectedFiles);
    connect(addFileTags, &QAction::triggered, this, [this]() { promptFileTagUpdate(false); });
    connect(removeFileTags, &QAction::triggered, this, [this]() { promptFileTagUpdate(true); });
    connect(pinFiles, &QAction::triggered, this, [this]() { setSelectedFilesPinned(true); });
    connect(unpinFiles, &QAction::triggered, this, [this]() { setSelectedFilesPinned(false); });
    connect(autoRelink, &QAction::triggered, this, [this]() { autoRelinkSelectedFiles(); });
    connect(deduplicateAnchors, &QAction::triggered, this, &AnchorLibraryWindow::deduplicateSelectedFileAnchors);
    connect(relinkButton_, &QToolButton::clicked, this, [this]() { relinkSelectedFile(); });
    connect(mergeButton_, &QToolButton::clicked, this, &AnchorLibraryWindow::mergeSelectedFileDuplicates);
    connect(integrityButton_, &QToolButton::clicked, this, &AnchorLibraryWindow::inspectIntegrity);
    connect(undoButton_, &QToolButton::clicked, this, &AnchorLibraryWindow::undoLastOperation);
    connect(saveView, &QAction::triggered, this, &AnchorLibraryWindow::promptSavedViewCreation);
    connect(deleteView, &QAction::triggered, this, [this]() {
        const QString name = savedViewCombo_->currentData().toString();
        if (!name.isEmpty()) deleteSavedView(name);
    });
    connect(operationHistory, &QAction::triggered, this, &AnchorLibraryWindow::showOperationHistory);
    connect(exportJson, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export Anchor Library"), QString(), tr("JSON files (*.json)"));
        if (!path.isEmpty()) exportLibraryJson(path);
    });
    connect(importMerge, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Import Anchor Library"), QString(), tr("JSON files (*.json)"));
        if (!path.isEmpty()) importLibraryJson(path, AnchorLibraryImportMode::Merge);
    });
    connect(importReplace, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Replace Anchor Library"), QString(), tr("JSON files (*.json)"));
        if (!path.isEmpty() && confirmOperation(tr("Replace Anchor Library"), tr("Replace all current resources with this JSON archive?"))) {
            importLibraryJson(path, AnchorLibraryImportMode::Replace);
        }
    });
    connect(backupDatabase, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, tr("Back Up Anchor Library"), QString(), tr("SQLite databases (*.sqlite3)"));
        if (!path.isEmpty()) backupLibraryDatabase(path);
    });
    connect(restoreDatabase, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Restore Anchor Library"), QString(), tr("SQLite databases (*.sqlite3)"));
        if (!path.isEmpty() && confirmOperation(tr("Restore Anchor Library"), tr("Replace the active database with this backup?"))) {
            restoreLibraryDatabase(path);
        }
    });
    connect(fileTable_, &QTableWidget::itemSelectionChanged, this, &AnchorLibraryWindow::populateSelectedFileAnchors);
    connect(anchorTable_, &QTableWidget::itemSelectionChanged, this, [this]() {
        populateInspector();
        updateActionButtons();
    });
    connect(anchorTable_, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *) { activateSelectedAnchor(); });
    connect(fileTable_->horizontalHeader(), &QHeaderView::sectionClicked, this, &AnchorLibraryWindow::handleFileSortRequest);
    connect(anchorTable_->horizontalHeader(), &QHeaderView::sectionClicked, this, &AnchorLibraryWindow::handleAnchorSortRequest);
    connect(saveFileButton_, &QPushButton::clicked, this, &AnchorLibraryWindow::saveSelectedFileMetadata);
    connect(saveAnchorButton_, &QPushButton::clicked, this, &AnchorLibraryWindow::saveSelectedAnchorMetadata);
    connect(saveLocatorButton_, &QPushButton::clicked, this, &AnchorLibraryWindow::saveSelectedAnchorLocator);
    connect(validateLocatorButton_, &QPushButton::clicked, this, &AnchorLibraryWindow::validateSelectedAnchor);
    connect(previewLocatorButton_, &QPushButton::clicked, this, &AnchorLibraryWindow::previewSelectedAnchor);
    connect(recaptureLocatorButton_, &QPushButton::clicked, this, &AnchorLibraryWindow::recaptureSelectedAnchor);
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, anchorTable_);
    deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, &AnchorLibraryWindow::deleteSelectedAnchors);

    if (options_.repository) {
        repositoryListenerId_ = options_.repository->addChangeListener([this](const LibraryChange &change) {
            const quint64 revision = change.revision;
            QMetaObject::invokeMethod(this, [this, revision]() {
                if (revision > loadedRepositoryRevision_) scheduleRepositoryRefresh();
            }, Qt::QueuedConnection);
        });
    }
    refreshSavedViews();
    refreshLibrary();
}

AnchorLibraryWindow::~AnchorLibraryWindow()
{
    if (options_.repository && repositoryListenerId_ >= 0) {
        options_.repository->removeChangeListener(repositoryListenerId_);
    }
}

void AnchorLibraryWindow::refreshLibrary()
{
    repositoryRefreshPending_ = false;
    const QList<AnchorLibraryFile> provided = options_.filesProvider ? options_.filesProvider() : QList<AnchorLibraryFile>{};
    files_.clear();
    QHash<QString, int> groupedIndexes;
    for (AnchorLibraryFile file : provided) {
        if (file.anchors.isEmpty()) continue;
        for (AnchorLibraryAnchor &entry : file.anchors) {
            if (entry.resourceId.trimmed().isEmpty()) entry.resourceId = file.resource.id;
            entry.resourceDeleted = file.resource.deleted;
            if (entry.usage.resourceId.trimmed().isEmpty()) entry.usage.resourceId = entry.resourceId;
        }
        file.resource.anchors.clear();
        if (file.usage.resourceId.trimmed().isEmpty()) file.usage.resourceId = file.resource.id;
        const QString key = fileGroupingKey(file.resource);
        const auto existing = groupedIndexes.constFind(key);
        if (existing == groupedIndexes.constEnd()) {
            groupedIndexes.insert(key, files_.size());
            files_.append(file);
        } else {
            AnchorLibraryFile &grouped = files_[existing.value()];
            mergeResourceMetadata(grouped.resource, file.resource);
            mergeUsage(grouped.usage, file.usage);
            for (const AnchorLibraryAnchor &entry : file.anchors) appendUniqueAnchor(grouped, entry);
        }
    }
    refreshFilterChoices();
    applyFilter();
    if (options_.repository) loadedRepositoryRevision_ = options_.repository->changeRevision();
}

void AnchorLibraryWindow::setFilterText(const QString &text) { filterEdit_->setText(text); }
QString AnchorLibraryWindow::filterText() const { return filterEdit_->text(); }
int AnchorLibraryWindow::visibleFileCount() const { return fileTable_->rowCount(); }
int AnchorLibraryWindow::visibleAnchorCount() const { return anchorTable_->rowCount(); }
QString AnchorLibraryWindow::statusText() const { return statusText_; }

bool AnchorLibraryWindow::selectFileAt(int row)
{
    if (row < 0 || row >= fileTable_->rowCount()) return false;
    fileTable_->selectRow(row);
    return selectedFile() != nullptr;
}

bool AnchorLibraryWindow::selectAnchorAt(int row)
{
    if (row < 0 || row >= anchorTable_->rowCount()) return false;
    anchorTable_->selectRow(row);
    return selectedAnchor().has_value();
}

int AnchorLibraryWindow::selectedFileCount() const { return selectedFiles().size(); }
int AnchorLibraryWindow::selectedAnchorCount() const { return selectedAnchors().size(); }

bool AnchorLibraryWindow::activateSelectedAnchor()
{
    const AnchorLibraryFile *file = selectedFile();
    const auto entry = selectedAnchor();
    if (!file || !entry || selectedAnchors().size() != 1) {
        statusText_ = tr("Select one anchor to open");
        statusLabel_->setText(statusText_);
        return false;
    }
    if (showingTrash() || entry->resourceDeleted) {
        statusText_ = tr("Restore the anchor before opening it");
        statusLabel_->setText(statusText_);
        return false;
    }
    if (!options_.anchorJumpHandler) {
        statusText_ = tr("Anchor opening is not configured");
        statusLabel_->setText(statusText_);
        return false;
    }
    QString status;
    const bool opened = options_.anchorJumpHandler(*file, entry.value(), &status);
    statusText_ = status.trimmed().isEmpty() ? (opened ? tr("Opened anchor") : tr("Unable to open anchor")) : status.trimmed();
    statusLabel_->setText(statusText_);
    if (opened) emit anchorActivated(entry->resourceId, entry->anchor.id);
    return opened;
}

bool AnchorLibraryWindow::deleteSelectedAnchor() { return deleteSelectedAnchors(); }

bool AnchorLibraryWindow::deleteSelectedAnchors()
{
    const QList<AnchorLibraryAnchor> entries = selectedAnchors();
    if (entries.isEmpty() || showingTrash()) {
        statusText_ = entries.isEmpty() ? tr("Select at least one anchor to delete") : tr("Selected anchors are already in Trash");
        statusLabel_->setText(statusText_);
        return false;
    }
    if (!options_.managementService) return false;
    const QString subject = entries.size() == 1
        ? QStringLiteral("\"%1\"").arg(anchorDisplayName(entries.first().anchor))
        : tr("%1 selected anchors").arg(entries.size());
    if (!confirmOperation(tr("Delete Anchors"), tr("Move %1 to Trash?\n\nThe files will not be deleted.").arg(subject))) {
        statusText_ = tr("Delete canceled");
        statusLabel_->setText(statusText_);
        return false;
    }
    const AnchorLibraryOperationResult result = options_.managementService->setAnchorsDeleted(selectedAnchorReferences(), true);
    setOperationResult(result);
    if (result.success) {
        for (const AnchorLibraryAnchor &entry : entries) emit anchorDeleted(entry.resourceId, entry.anchor.id);
    }
    return result.success;
}

bool AnchorLibraryWindow::restoreSelectedAnchors()
{
    const QList<AnchorLibraryAnchor> entries = selectedAnchors();
    if (entries.isEmpty() || !showingTrash() || !options_.managementService) {
        statusText_ = tr("Select deleted anchors in Trash to restore");
        statusLabel_->setText(statusText_);
        return false;
    }
    if (entries.first().resourceDeleted) return false;
    const AnchorLibraryOperationResult result = options_.managementService->setAnchorsDeleted(selectedAnchorReferences(), false);
    setOperationResult(result);
    if (result.success) {
        for (const AnchorLibraryAnchor &entry : entries) emit anchorRestored(entry.resourceId, entry.anchor.id);
    }
    return result.success;
}

bool AnchorLibraryWindow::archiveSelectedFiles()
{
    if (!options_.managementService || showingTrash()) return false;
    const QStringList ids = selectedResourceIds();
    if (ids.isEmpty()) return false;
    if (!confirmOperation(tr("Archive File Records"), tr("Move %1 selected file record(s) to Trash?\n\nFiles on disk are not changed.").arg(ids.size()))) {
        statusText_ = tr("Archive canceled");
        statusLabel_->setText(statusText_);
        return false;
    }
    const AnchorLibraryOperationResult result = options_.managementService->setResourcesDeleted(ids, true);
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::restoreSelectedFiles()
{
    if (!options_.managementService || !showingTrash()) return false;
    QStringList ids;
    for (const AnchorLibraryFile *file : selectedFiles()) {
        if (file->resource.deleted) {
            for (const QString &id : resourceIdsForFile(*file)) appendUnique(ids, id);
        }
    }
    if (ids.isEmpty()) return false;
    const AnchorLibraryOperationResult result = options_.managementService->setResourcesDeleted(ids, false);
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::permanentlyDeleteSelection()
{
    if (!options_.managementService || !showingTrash()) {
        statusText_ = tr("Permanent deletion is available only in Trash");
        statusLabel_->setText(statusText_);
        return false;
    }
    QStringList archivedIds;
    QList<AnchorReference> anchorReferences;
    const QList<const AnchorLibraryFile *> files = selectedFiles();
    for (const AnchorLibraryFile *file : files) {
        if (file->resource.deleted) {
            for (const QString &id : resourceIdsForFile(*file)) appendUnique(archivedIds, id);
        } else {
            const QList<AnchorLibraryAnchor> scoped = scopedAnchors(*file);
            for (const AnchorLibraryAnchor &entry : scoped) anchorReferences.append({entry.resourceId, entry.anchor});
        }
    }
    if (archivedIds.isEmpty() && anchorReferences.isEmpty()) return false;
    const int count = archivedIds.size() + anchorReferences.size();
    if (!confirmOperation(tr("Permanently Delete"), tr("Permanently delete %1 selected Trash item(s)?\n\nThis cannot be undone.").arg(count))) {
        statusText_ = tr("Permanent deletion canceled");
        statusLabel_->setText(statusText_);
        return false;
    }
    if (!createSafetyBackup(QStringLiteral("permanent deletion"))) return false;
    AnchorLibraryOperationResult result;
    if (!archivedIds.isEmpty()) result = options_.managementService->permanentlyDeleteResources(archivedIds);
    if (result.success || archivedIds.isEmpty()) {
        if (!anchorReferences.isEmpty()) result = options_.managementService->permanentlyDeleteAnchors(anchorReferences);
    }
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::saveSelectedAnchorMetadata()
{
    const auto entry = selectedAnchor();
    if (!entry || selectedAnchors().size() != 1 || showingTrash() || !options_.managementService) return false;
    AnchorMetadataUpdate update;
    update.name = anchorNameEdit_->text();
    update.aliases = editorValues(anchorAliasesEdit_->text());
    update.tags = editorValues(anchorTagsEdit_->text());
    update.pinned = anchorPinnedCheck_->isChecked();
    const AnchorLibraryOperationResult result = options_.managementService->updateAnchorMetadata({entry->resourceId, entry->anchor}, update);
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::saveSelectedAnchorLocator()
{
    const auto entry = selectedAnchor();
    if (!entry || selectedAnchors().size() != 1 || showingTrash() || !options_.managementService) return false;
    AnchorLocatorUpdate update;
    update.targetApp = targetAppEdit_->text();
    update.targetFile = targetFileEdit_->text();
    update.targetUri = targetUriEdit_->text();
    update.locatorType = locatorTypeEdit_->text();
    update.locatorJson = locatorJsonEdit_->toPlainText();
    const AnchorLibraryOperationResult result = options_.managementService->updateAnchorLocator({entry->resourceId, entry->anchor}, update);
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::saveSelectedFileMetadata()
{
    const AnchorLibraryFile *file = selectedFile();
    if (!file || selectedFiles().size() != 1 || showingTrash() || !options_.managementService) return false;
    ResourceMetadataUpdate update;
    update.title = fileTitleEdit_->text();
    update.aliases = editorValues(fileAliasesEdit_->text());
    update.tags = editorValues(fileTagsEdit_->text());
    update.pinned = filePinnedCheck_->isChecked();
    const AnchorLibraryOperationResult result = options_.managementService->updateResourceMetadata(resourceIdsForFile(*file), update);
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::updateSelectedAnchorTags(const QStringList &tags, bool remove)
{
    if (!options_.managementService || showingTrash() || selectedAnchors().isEmpty()) return false;
    const AnchorLibraryOperationResult result = options_.managementService->updateAnchorTags(selectedAnchorReferences(), tags, remove);
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::updateSelectedFileTags(const QStringList &tags, bool remove)
{
    if (!options_.managementService || showingTrash() || selectedResourceIds().isEmpty()) return false;
    const AnchorLibraryOperationResult result = options_.managementService->updateResourceTags(selectedResourceIds(), tags, remove);
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::setSelectedAnchorsPinned(bool pinned)
{
    if (!options_.managementService || showingTrash() || selectedAnchors().isEmpty()) return false;
    const AnchorLibraryOperationResult result = options_.managementService->setAnchorsPinned(selectedAnchorReferences(), pinned);
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::setSelectedFilesPinned(bool pinned)
{
    if (!options_.managementService || showingTrash() || selectedResourceIds().isEmpty()) return false;
    const AnchorLibraryOperationResult result = options_.managementService->setResourcesPinned(selectedResourceIds(), pinned);
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::relinkSelectedFile(const QString &newLocation)
{
    const AnchorLibraryFile *file = selectedFile();
    if (!file || selectedFiles().size() != 1 || file->resource.kind == ResourceKind::Url || !options_.managementService) return false;
    QString location = newLocation.trimmed();
    if (location.isEmpty()) {
        if (options_.relinkPathProvider) {
            location = options_.relinkPathProvider(*file);
        } else if (file->resource.kind == ResourceKind::Folder) {
            location = QFileDialog::getExistingDirectory(this, tr("Relink Marked Folder"), file->resource.location);
        } else {
            location = QFileDialog::getOpenFileName(this, tr("Relink Marked File"), QFileInfo(file->resource.location).absolutePath());
        }
    }
    if (location.trimmed().isEmpty()) {
        statusText_ = tr("Relink canceled");
        statusLabel_->setText(statusText_);
        return false;
    }
    const AnchorLibraryOperationResult result = options_.managementService->relinkResources(resourceIdsForFile(*file), location);
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::autoRelinkSelectedFiles(const QString &searchRoot)
{
    if (!options_.managementService || selectedResourceIds().isEmpty()) return false;
    QString root = searchRoot.trimmed();
    if (root.isEmpty()) root = QFileDialog::getExistingDirectory(this, tr("Search for Missing Files"));
    if (root.isEmpty()) return false;
    const AnchorLibraryOperationResult result = options_.managementService->autoRelinkMissingResources(selectedResourceIds(), root);
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::mergeSelectedFileDuplicates()
{
    const AnchorLibraryFile *file = selectedFile();
    if (!file || selectedFiles().size() != 1 || showingTrash() || !options_.managementService) return false;
    QStringList sources = resourceIdsForFile(*file);
    sources.removeAll(file->resource.id);
    if (sources.isEmpty()) {
        statusText_ = tr("The selected file has no duplicate records");
        statusLabel_->setText(statusText_);
        return false;
    }
    if (!confirmOperation(tr("Merge Duplicate Records"), tr("Merge %1 duplicate record(s) into \"%2\"?").arg(sources.size()).arg(fileDisplayName(*file)))) return false;
    const AnchorLibraryOperationResult result = options_.managementService->mergeResources(file->resource.id, sources);
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::deduplicateSelectedFileAnchors()
{
    if (!options_.managementService || showingTrash() || selectedResourceIds().isEmpty()) return false;
    const AnchorLibraryOperationResult result = options_.managementService->deduplicateAnchors(selectedResourceIds());
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::validateSelectedAnchor()
{
    const AnchorLibraryFile *file = selectedFile();
    const auto entry = selectedAnchor();
    if (!file || !entry || !options_.managementService) return false;
    Anchor edited = entry->anchor;
    edited.targetApp = targetAppEdit_->text();
    edited.targetFile = targetFileEdit_->text();
    edited.targetUri = targetUriEdit_->text();
    edited.locatorType = locatorTypeEdit_->text();
    edited.locatorJson = locatorJsonEdit_->toPlainText();
    const AnchorValidationResult validation = options_.managementService->validateAnchor(file->resource, edited);
    statusText_ = validation.valid ? tr("Locator is valid") : tr("Invalid locator: %1").arg(validation.issues.join(QStringLiteral("; ")));
    statusLabel_->setText(statusText_);
    return validation.valid;
}

bool AnchorLibraryWindow::previewSelectedAnchor()
{
    const AnchorLibraryFile *file = selectedFile();
    const auto entry = selectedAnchor();
    if (!file || !entry) return false;
    locatorPreview_->setLocator(file->resource, entry->anchor);
    QString status;
    QPixmap screenshot;
    if (options_.locatorPreviewHandler) {
        const bool restoreWindow = isVisible();
        if (restoreWindow) {
            hide();
            QApplication::processEvents();
        }
        screenshot = options_.locatorPreviewHandler(*file, entry.value(), &status);
        if (restoreWindow) {
            show();
            raise();
            activateWindow();
        }
    }
    if (!screenshot.isNull()) locatorPreview_->setScreenshot(screenshot);
    statusText_ = status.trimmed().isEmpty()
        ? (screenshot.isNull() ? tr("Showing locator geometry") : tr("Captured application preview"))
        : status.trimmed();
    statusLabel_->setText(statusText_);
    return true;
}

bool AnchorLibraryWindow::recaptureSelectedAnchor()
{
    const AnchorLibraryFile *file = selectedFile();
    const auto entry = selectedAnchor();
    if (!file || !entry || showingTrash() || !options_.managementService || !options_.locatorRecaptureHandler) return false;
    const QString locatorType = entry->anchor.locatorType.trimmed().toLower();
    if (file->resource.kind != ResourceKind::Pdf
        && !locatorType.startsWith(QStringLiteral("sumatrapdf."))
        && !locatorType.startsWith(QStringLiteral("pdf."))) {
        statusText_ = tr("Rectangle recapture is available only for PDF anchors");
        statusLabel_->setText(statusText_);
        return false;
    }
    QString status;
    const bool restoreWindow = isVisible();
    if (restoreWindow) {
        hide();
        QApplication::processEvents();
    }
    const std::optional<AnchorLocatorUpdate> captured = options_.locatorRecaptureHandler(*file, entry.value(), &status);
    if (restoreWindow) {
        show();
        raise();
        activateWindow();
    }
    if (!captured) {
        statusText_ = status.trimmed().isEmpty() ? tr("Locator recapture canceled") : status;
        statusLabel_->setText(statusText_);
        return false;
    }
    AnchorLocatorUpdate old;
    old.targetApp = entry->anchor.targetApp;
    old.targetFile = entry->anchor.targetFile;
    old.targetUri = entry->anchor.targetUri;
    old.locatorType = entry->anchor.locatorType;
    old.locatorJson = entry->anchor.locatorJson;
    if (!confirmOperation(tr("Replace Locator"), tr("Replace the stored locator?\n\nCurrent:\n%1\n\nCaptured:\n%2").arg(compactLocator(old), compactLocator(captured.value())))) {
        statusText_ = tr("Locator recapture canceled");
        statusLabel_->setText(statusText_);
        return false;
    }
    const AnchorLibraryOperationResult result = options_.managementService->updateAnchorLocator({entry->resourceId, entry->anchor}, captured.value());
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::inspectIntegrity()
{
    if (!options_.managementService) return false;
    const AnchorLibraryIntegrityReport report = options_.managementService->inspectIntegrity();
    statusText_ = report.healthy()
        ? tr("Integrity scan passed")
        : tr("Integrity scan: %1 missing, %2 duplicate files, %3 duplicate anchors, %4 invalid locators")
              .arg(report.missingTargetCount).arg(report.duplicateResourceCount)
              .arg(report.duplicateAnchorCount).arg(report.invalidLocatorCount);
    statusLabel_->setText(statusText_);
    if (isVisible()) {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Anchor Library Integrity"));
        dialog.resize(820, 420);
        auto *dialogLayout = new QVBoxLayout(&dialog);
        auto *summary = new QLabel(statusText_, &dialog);
        auto *table = new QTableWidget(report.issues.size(), 3, &dialog);
        table->setHorizontalHeaderLabels({tr("Issue"), tr("Item"), tr("Detail")});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        for (int row = 0; row < report.issues.size(); ++row) {
            const auto &issue = report.issues.at(row);
            table->setItem(row, 0, new QTableWidgetItem(issueKindLabel(issue.kind)));
            table->setItem(row, 1, new QTableWidgetItem(issue.title));
            table->setItem(row, 2, new QTableWidgetItem(issue.detail));
        }
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialogLayout->addWidget(summary);
        dialogLayout->addWidget(table, 1);
        dialogLayout->addWidget(buttons);
        dialog.exec();
    }
    return report.healthy();
}

bool AnchorLibraryWindow::renameTag(const QString &oldTag, const QString &newTag)
{
    if (!options_.managementService) return false;
    const AnchorLibraryOperationResult result = options_.managementService->renameTag(oldTag, newTag);
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::deleteTag(const QString &tag)
{
    if (!options_.managementService) return false;
    const AnchorLibraryOperationResult result = options_.managementService->deleteTag(tag);
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::showOperationHistory()
{
    if (!options_.managementService) return false;
    const QList<AnchorLibraryHistoryItem> history = options_.managementService->history();
    statusText_ = history.isEmpty()
        ? tr("No session operations have been recorded")
        : tr("%1 session operation(s); newest first").arg(history.size());
    statusLabel_->setText(statusText_);
    if (!isVisible()) return !history.isEmpty();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Anchor Library Operation History"));
    dialog.resize(700, 380);
    auto *layout = new QVBoxLayout(&dialog);
    auto *table = new QTableWidget(history.size(), 4, &dialog);
    table->setObjectName(QStringLiteral("anchorLibraryHistoryTable"));
    table->setHorizontalHeaderLabels({tr("Operation"), tr("Affected"), tr("Time"), tr("State")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column : {1, 2, 3}) table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    for (int row = 0; row < history.size(); ++row) {
        const AnchorLibraryHistoryItem &item = history.at(row);
        table->setItem(row, 0, new QTableWidgetItem(item.action));
        table->setItem(row, 1, new QTableWidgetItem(QString::number(item.affectedCount)));
        table->setItem(row, 2, new QTableWidgetItem(QLocale().toString(item.timestamp.toLocalTime(), QLocale::ShortFormat)));
        table->setItem(row, 3, new QTableWidgetItem(item.undone ? tr("Undone") : tr("Applied")));
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(table, 1);
    layout->addWidget(buttons);
    dialog.exec();
    return !history.isEmpty();
}

bool AnchorLibraryWindow::undoLastOperation()
{
    if (!options_.managementService) return false;
    const AnchorLibraryOperationResult result = options_.managementService->undoLast();
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::exportLibraryJson(const QString &filePath)
{
    if (!options_.archiveService) return false;
    const auto result = options_.archiveService->exportJson(filePath);
    setOperationResult(result, false);
    return result.success;
}

bool AnchorLibraryWindow::importLibraryJson(const QString &filePath, AnchorLibraryImportMode mode)
{
    if (!options_.archiveService) return false;
    const auto result = options_.archiveService->importJson(filePath, mode);
    if (result.success && options_.managementService) options_.managementService->clearHistory();
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::backupLibraryDatabase(const QString &filePath)
{
    if (!options_.archiveService) return false;
    const auto result = options_.archiveService->backupDatabase(filePath);
    setOperationResult(result, false);
    return result.success;
}

bool AnchorLibraryWindow::restoreLibraryDatabase(const QString &filePath)
{
    if (!options_.archiveService) return false;
    const auto result = options_.archiveService->restoreDatabase(filePath);
    if (result.success && options_.managementService) options_.managementService->clearHistory();
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::saveCurrentView(const QString &name)
{
    if (!options_.settings || name.trimmed().isEmpty()) return false;
    const QString key = viewSettingsKey(name);
    options_.settings->beginGroup(key);
    options_.settings->setValue(QStringLiteral("name"), name.trimmed());
    options_.settings->setValue(QStringLiteral("query"), filterEdit_->text());
    options_.settings->setValue(QStringLiteral("scope"), scopeCombo_->currentData());
    options_.settings->setValue(QStringLiteral("tag"), tagFilterCombo_->currentData());
    options_.settings->setValue(QStringLiteral("kind"), kindFilterCombo_->currentData());
    options_.settings->setValue(QStringLiteral("app"), appFilterCombo_->currentData());
    options_.settings->setValue(QStringLiteral("directory"), directoryFilterEdit_->text());
    options_.settings->setValue(QStringLiteral("days"), timeFilterCombo_->currentData());
    options_.settings->setValue(QStringLiteral("usage"), usageFilterCombo_->currentData());
    options_.settings->endGroup();
    options_.settings->sync();
    refreshSavedViews();
    const int index = savedViewCombo_->findData(name.trimmed());
    if (index >= 0) savedViewCombo_->setCurrentIndex(index);
    statusText_ = tr("Saved view: %1").arg(name.trimmed());
    statusLabel_->setText(statusText_);
    return true;
}

bool AnchorLibraryWindow::loadSavedView(const QString &name)
{
    if (!options_.settings || name.trimmed().isEmpty()) return false;
    const QString key = viewSettingsKey(name);
    options_.settings->beginGroup(key);
    if (!options_.settings->contains(QStringLiteral("name"))) {
        options_.settings->endGroup();
        return false;
    }
    const QString query = options_.settings->value(QStringLiteral("query")).toString();
    const QVariant scope = options_.settings->value(QStringLiteral("scope"), 0);
    const QVariant tag = options_.settings->value(QStringLiteral("tag"));
    const QVariant kind = options_.settings->value(QStringLiteral("kind"), -1);
    const QVariant app = options_.settings->value(QStringLiteral("app"));
    const QString directory = options_.settings->value(QStringLiteral("directory")).toString();
    const QVariant days = options_.settings->value(QStringLiteral("days"), 0);
    const QVariant usage = options_.settings->value(QStringLiteral("usage"), 0);
    options_.settings->endGroup();
    const QSignalBlocker queryBlocker(filterEdit_);
    const QSignalBlocker scopeBlocker(scopeCombo_);
    const QSignalBlocker tagBlocker(tagFilterCombo_);
    const QSignalBlocker kindBlocker(kindFilterCombo_);
    const QSignalBlocker appBlocker(appFilterCombo_);
    const QSignalBlocker directoryBlocker(directoryFilterEdit_);
    const QSignalBlocker timeBlocker(timeFilterCombo_);
    const QSignalBlocker usageBlocker(usageFilterCombo_);
    filterEdit_->setText(query);
    scopeCombo_->setCurrentIndex(std::max(0, scopeCombo_->findData(scope)));
    tagFilterCombo_->setCurrentIndex(std::max(0, tagFilterCombo_->findData(tag)));
    kindFilterCombo_->setCurrentIndex(std::max(0, kindFilterCombo_->findData(kind)));
    appFilterCombo_->setCurrentIndex(std::max(0, appFilterCombo_->findData(app)));
    directoryFilterEdit_->setText(directory);
    timeFilterCombo_->setCurrentIndex(std::max(0, timeFilterCombo_->findData(days)));
    usageFilterCombo_->setCurrentIndex(std::max(0, usageFilterCombo_->findData(usage)));
    applyFilter();
    statusText_ = tr("Loaded view: %1").arg(name);
    statusLabel_->setText(statusText_);
    return true;
}

bool AnchorLibraryWindow::deleteSavedView(const QString &name)
{
    if (!options_.settings || name.trimmed().isEmpty()) return false;
    options_.settings->remove(viewSettingsKey(name));
    options_.settings->sync();
    refreshSavedViews();
    statusText_ = tr("Deleted saved view: %1").arg(name.trimmed());
    statusLabel_->setText(statusText_);
    return true;
}

void AnchorLibraryWindow::applyFilter()
{
    QStringList selectedKeys;
    if (fileTable_->selectionModel()) {
        for (const QModelIndex &index : fileTable_->selectionModel()->selectedRows(0)) {
            appendUnique(selectedKeys, fileTable_->item(index.row(), 0)->data(FileKeyRole).toString());
        }
    }
    fileTable_->setRowCount(0);
    const QList<const AnchorLibraryFile *> visible = sortedVisibleFiles();
    for (const AnchorLibraryFile *file : visible) {
        const QList<AnchorLibraryAnchor> anchors = scopedAnchors(*file);
        const int row = fileTable_->rowCount();
        fileTable_->insertRow(row);
        auto *name = new QTableWidgetItem(fileDisplayName(*file));
        name->setData(FileKeyRole, fileGroupingKey(file->resource));
        name->setToolTip(file->resource.location);
        if (file->resource.deleted) name->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        fileTable_->setItem(row, 0, name);
        auto *location = new QTableWidgetItem(fileLocationLabel(*file));
        location->setToolTip(file->resource.location);
        fileTable_->setItem(row, 1, location);
        fileTable_->setItem(row, 2, new QTableWidgetItem(resourceKindLabel(file->resource.kind)));
        auto *count = new QTableWidgetItem;
        count->setData(Qt::DisplayRole, anchors.size());
        fileTable_->setItem(row, 3, count);
        fileTable_->setItem(row, 4, new QTableWidgetItem(fileTags(*file, anchors).join(QStringLiteral(", "))));
        const QDateTime marked = lastMarkedAt(anchors);
        auto *markedItem = new QTableWidgetItem(marked.isValid() ? QLocale().toString(marked.toLocalTime(), QLocale::ShortFormat) : QString());
        markedItem->setData(SortValueRole, marked);
        fileTable_->setItem(row, 5, markedItem);
        QStringList states;
        if (file->resource.deleted) states.append(tr("Archived"));
        else states.append(localTargetExists(file->resource) ? tr("Ready") : tr("Missing"));
        const int records = resourceIdsForFile(*file).size();
        if (records > 1) states.append(tr("%1 records").arg(records));
        if (localTargetExists(file->resource) && fileHasInvalidLocator(*file)) {
            states.append(tr("Invalid locator"));
        }
        fileTable_->setItem(row, 6, new QTableWidgetItem(states.join(QStringLiteral(" | "))));
        auto *opens = new QTableWidgetItem;
        opens->setData(Qt::DisplayRole, totalOpenCount(*file));
        fileTable_->setItem(row, 7, opens);
        const QDateTime opened = lastOpenedAt(*file);
        auto *openedItem = new QTableWidgetItem(opened.isValid() ? QLocale().toString(opened.toLocalTime(), QLocale::ShortFormat) : QString());
        openedItem->setData(SortValueRole, opened);
        fileTable_->setItem(row, 8, openedItem);
    }
    bool restored = false;
    for (int row = 0; row < fileTable_->rowCount(); ++row) {
        if (selectedKeys.contains(fileTable_->item(row, 0)->data(FileKeyRole).toString())) {
            fileTable_->selectionModel()->select(fileTable_->model()->index(row, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
            if (!restored) fileTable_->setCurrentCell(row, 0);
            restored = true;
        }
    }
    if (!restored && fileTable_->rowCount() > 0) fileTable_->selectRow(0);
    if (fileTable_->rowCount() == 0) {
        anchorTable_->setRowCount(0);
        populateInspector();
    } else {
        populateSelectedFileAnchors();
    }
    updateStatus();
}

void AnchorLibraryWindow::populateSelectedFileAnchors()
{
    QStringList selectedIds;
    if (anchorTable_->selectionModel()) {
        for (const QModelIndex &index : anchorTable_->selectionModel()->selectedRows(0)) {
            const auto *item = anchorTable_->item(index.row(), 0);
            selectedIds.append(item->data(ResourceIdRole).toString() + QLatin1Char('|') + item->data(AnchorIdentityRole).toString());
        }
    }
    anchorTable_->setRowCount(0);
    const AnchorLibraryFile *file = selectedFile();
    if (file) {
        for (const AnchorLibraryAnchor &entry : sortedAnchors(scopedAnchors(*file))) {
            const int row = anchorTable_->rowCount();
            anchorTable_->insertRow(row);
            auto *name = new QTableWidgetItem(anchorDisplayName(entry.anchor));
            name->setData(AnchorIdentityRole, anchorIdentityKey(entry.anchor));
            name->setData(ResourceIdRole, entry.resourceId);
            anchorTable_->setItem(row, 0, name);
            anchorTable_->setItem(row, 1, new QTableWidgetItem(entry.anchor.aliases.join(QStringLiteral(", "))));
            anchorTable_->setItem(row, 2, new QTableWidgetItem(entry.anchor.tags.join(QStringLiteral(", "))));
            anchorTable_->setItem(row, 3, new QTableWidgetItem(anchorLocatorSummary(entry.anchor)));
            const QDateTime updated = entry.anchor.updatedAt.isValid() ? entry.anchor.updatedAt : entry.anchor.createdAt;
            auto *updatedItem = new QTableWidgetItem(updated.isValid() ? QLocale().toString(updated.toLocalTime(), QLocale::ShortFormat) : QString());
            updatedItem->setData(SortValueRole, updated);
            anchorTable_->setItem(row, 4, updatedItem);
            auto *opens = new QTableWidgetItem;
            opens->setData(Qt::DisplayRole, entry.usage.openCount);
            anchorTable_->setItem(row, 5, opens);
            auto *opened = new QTableWidgetItem(entry.usage.lastOpenedAt.isValid() ? QLocale().toString(entry.usage.lastOpenedAt.toLocalTime(), QLocale::ShortFormat) : QString());
            opened->setData(SortValueRole, entry.usage.lastOpenedAt);
            anchorTable_->setItem(row, 6, opened);
            QString validity = tr("Unknown");
            if (options_.managementService) {
                const AnchorValidationResult result = options_.managementService->validateAnchor(file->resource, entry.anchor);
                validity = result.valid ? tr("Valid") : tr("Invalid");
            }
            anchorTable_->setItem(row, 7, new QTableWidgetItem(validity));
        }
    }
    bool restored = false;
    for (int row = 0; row < anchorTable_->rowCount(); ++row) {
        const auto *item = anchorTable_->item(row, 0);
        const QString key = item->data(ResourceIdRole).toString() + QLatin1Char('|') + item->data(AnchorIdentityRole).toString();
        if (selectedIds.contains(key)) {
            anchorTable_->selectionModel()->select(anchorTable_->model()->index(row, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
            if (!restored) anchorTable_->setCurrentCell(row, 0);
            restored = true;
        }
    }
    if (!restored && anchorTable_->rowCount() > 0) anchorTable_->selectRow(0);
    populateInspector();
    updateActionButtons();
    updateStatus();
}

void AnchorLibraryWindow::populateInspector()
{
    const AnchorLibraryFile *file = selectedFile();
    const bool oneFile = file && selectedFiles().size() == 1;
    const bool fileEditable = oneFile && options_.managementService && !showingTrash();
    for (QWidget *widget : QList<QWidget *>{fileTitleEdit_, fileAliasesEdit_, fileTagsEdit_, filePinnedCheck_}) {
        widget->setEnabled(fileEditable);
    }
    if (oneFile) {
        fileTitleEdit_->setText(file->resource.title);
        fileAliasesEdit_->setText(file->resource.aliases.join(QStringLiteral(", ")));
        fileTagsEdit_->setText(file->resource.tags.join(QStringLiteral(", ")));
        fileLocationEdit_->setText(fileLocationLabel(*file));
        filePinnedCheck_->setChecked(file->usage.pinned);
    } else {
        fileTitleEdit_->clear(); fileAliasesEdit_->clear(); fileTagsEdit_->clear(); fileLocationEdit_->clear(); filePinnedCheck_->setChecked(false);
    }
    const QList<AnchorLibraryAnchor> entries = selectedAnchors();
    const bool oneAnchor = entries.size() == 1;
    const bool anchorEditable = oneAnchor && options_.managementService && !showingTrash();
    for (QWidget *widget : QList<QWidget *>{anchorNameEdit_, anchorAliasesEdit_, anchorTagsEdit_, anchorPinnedCheck_,
                                            targetAppEdit_, targetFileEdit_, targetUriEdit_, locatorTypeEdit_, locatorJsonEdit_}) {
        widget->setEnabled(anchorEditable);
    }
    if (oneAnchor) {
        const Anchor &anchor = entries.first().anchor;
        selectionLabel_->setText(selectedFiles().size() > 1 ? tr("%1 files selected").arg(selectedFiles().size()) : tr("1 anchor selected"));
        anchorNameEdit_->setText(anchor.name);
        anchorAliasesEdit_->setText(anchor.aliases.join(QStringLiteral(", ")));
        anchorTagsEdit_->setText(anchor.tags.join(QStringLiteral(", ")));
        anchorPinnedCheck_->setChecked(anchor.pinned);
        targetAppEdit_->setText(anchor.targetApp);
        targetFileEdit_->setText(anchor.targetFile);
        targetUriEdit_->setText(anchor.targetUri);
        locatorTypeEdit_->setText(anchor.locatorType);
        locatorJsonEdit_->setPlainText(anchor.locatorJson);
        if (file) locatorPreview_->setLocator(file->resource, anchor);
    } else {
        selectionLabel_->setText(entries.isEmpty() ? tr("%1 file(s) selected").arg(selectedFiles().size()) : tr("%1 anchors selected").arg(entries.size()));
        anchorNameEdit_->clear(); anchorAliasesEdit_->clear(); anchorTagsEdit_->clear(); anchorPinnedCheck_->setChecked(false);
        targetAppEdit_->clear(); targetFileEdit_->clear(); targetUriEdit_->clear(); locatorTypeEdit_->clear(); locatorJsonEdit_->clear();
        locatorPreview_->clearPreview();
    }
}

const AnchorLibraryFile *AnchorLibraryWindow::fileForKey(const QString &key) const
{
    for (const AnchorLibraryFile &file : files_) if (fileGroupingKey(file.resource) == key) return &file;
    return nullptr;
}

const AnchorLibraryFile *AnchorLibraryWindow::selectedFile() const
{
    const int row = fileTable_->currentRow();
    if (row < 0 || !fileTable_->item(row, 0)) return nullptr;
    return fileForKey(fileTable_->item(row, 0)->data(FileKeyRole).toString());
}

QList<const AnchorLibraryFile *> AnchorLibraryWindow::selectedFiles() const
{
    QList<const AnchorLibraryFile *> selected;
    if (!fileTable_->selectionModel()) return selected;
    for (const QModelIndex &index : fileTable_->selectionModel()->selectedRows(0)) {
        const AnchorLibraryFile *file = fileForKey(fileTable_->item(index.row(), 0)->data(FileKeyRole).toString());
        if (file && !selected.contains(file)) selected.append(file);
    }
    return selected;
}

std::optional<AnchorLibraryAnchor> AnchorLibraryWindow::selectedAnchor() const
{
    const AnchorLibraryFile *file = selectedFile();
    const int row = anchorTable_->currentRow();
    if (!file || row < 0 || !anchorTable_->item(row, 0)) return std::nullopt;
    const QString identity = anchorTable_->item(row, 0)->data(AnchorIdentityRole).toString();
    const QString resourceId = anchorTable_->item(row, 0)->data(ResourceIdRole).toString();
    for (const AnchorLibraryAnchor &entry : file->anchors) {
        if (entry.resourceId == resourceId && anchorIdentityKey(entry.anchor) == identity) return entry;
    }
    return std::nullopt;
}

QList<AnchorLibraryAnchor> AnchorLibraryWindow::selectedAnchors() const
{
    QList<AnchorLibraryAnchor> selected;
    const AnchorLibraryFile *file = selectedFile();
    if (!file || !anchorTable_->selectionModel()) return selected;
    for (const QModelIndex &index : anchorTable_->selectionModel()->selectedRows(0)) {
        const auto *item = anchorTable_->item(index.row(), 0);
        const QString identity = item->data(AnchorIdentityRole).toString();
        const QString resourceId = item->data(ResourceIdRole).toString();
        for (const AnchorLibraryAnchor &entry : file->anchors) {
            if (entry.resourceId == resourceId && anchorIdentityKey(entry.anchor) == identity) {
                selected.append(entry);
                break;
            }
        }
    }
    return selected;
}

QList<AnchorLibraryAnchor> AnchorLibraryWindow::scopedAnchors(const AnchorLibraryFile &file) const
{
    QList<AnchorLibraryAnchor> anchors;
    const bool trash = showingTrash();
    if (trash && file.resource.deleted) return file.anchors;
    if (!trash && file.resource.deleted) return anchors;
    for (const AnchorLibraryAnchor &entry : file.anchors) {
        if (entry.anchor.deleted == trash) anchors.append(entry);
    }
    return anchors;
}

QStringList AnchorLibraryWindow::resourceIdsForFile(const AnchorLibraryFile &file) const
{
    QStringList ids;
    appendUnique(ids, file.resource.id);
    for (const AnchorLibraryAnchor &entry : file.anchors) appendUnique(ids, entry.resourceId);
    return ids;
}

QStringList AnchorLibraryWindow::selectedResourceIds() const
{
    QStringList ids;
    for (const AnchorLibraryFile *file : selectedFiles()) {
        for (const QString &id : resourceIdsForFile(*file)) appendUnique(ids, id);
    }
    return ids;
}

QList<AnchorReference> AnchorLibraryWindow::selectedAnchorReferences() const
{
    QList<AnchorReference> references;
    for (const AnchorLibraryAnchor &entry : selectedAnchors()) references.append({entry.resourceId, entry.anchor});
    return references;
}

bool AnchorLibraryWindow::showingTrash() const
{
    const auto scope = static_cast<AnchorLibraryScope>(scopeCombo_->currentData().toInt());
    return scope == AnchorLibraryScope::Trash || scope == AnchorLibraryScope::RecentlyDeleted;
}

bool AnchorLibraryWindow::fileMatchesFilter(const AnchorLibraryFile &file) const
{
    const QList<AnchorLibraryAnchor> anchors = scopedAnchors(file);
    if (anchors.isEmpty()) return false;
    const auto scope = static_cast<AnchorLibraryScope>(scopeCombo_->currentData().toInt());
    if (scope == AnchorLibraryScope::Untagged && !fileTags(file, anchors).isEmpty()) return false;
    if (scope == AnchorLibraryScope::Missing && localTargetExists(file.resource)) return false;
    if (scope == AnchorLibraryScope::Duplicates && resourceIdsForFile(file).size() < 2) return false;
    if (scope == AnchorLibraryScope::InvalidLocator && !fileHasInvalidLocator(file)) return false;
    const QDateTime modified = lastMarkedAt(anchors);
    if ((scope == AnchorLibraryScope::RecentlyModified || scope == AnchorLibraryScope::RecentlyDeleted)
        && (!modified.isValid() || modified < QDateTime::currentDateTimeUtc().addDays(-30))) return false;

    const QString selectedTag = tagFilterCombo_->currentData().toString();
    if (!selectedTag.isEmpty() && !fileTags(file, anchors).contains(selectedTag, Qt::CaseInsensitive)) return false;
    const int kind = kindFilterCombo_->currentData().toInt();
    if (kind >= 0 && static_cast<int>(file.resource.kind) != kind) return false;
    const QString app = appFilterCombo_->currentData().toString();
    if (!app.isEmpty()) {
        bool matched = false;
        for (const AnchorLibraryAnchor &entry : anchors) if (sameText(entry.anchor.targetApp, app)) { matched = true; break; }
        if (!matched) return false;
    }
    const QString directory = QDir::fromNativeSeparators(directoryFilterEdit_->text().trimmed());
    if (!directory.isEmpty() && !QDir::fromNativeSeparators(file.resource.location).contains(directory, Qt::CaseInsensitive)) return false;
    const int days = timeFilterCombo_->currentData().toInt();
    if (days > 0 && (!modified.isValid() || modified < QDateTime::currentDateTimeUtc().addDays(-days))) return false;
    const auto usage = static_cast<UsageFilter>(usageFilterCombo_->currentData().toInt());
    if (usage == UsageFilter::Pinned && !isPinned(file)) return false;
    if (usage == UsageFilter::RecentlyOpened) {
        const QDateTime opened = lastOpenedAt(file);
        if (!opened.isValid() || opened < QDateTime::currentDateTimeUtc().addDays(-30)) return false;
    }
    if (usage == UsageFilter::NeverOpened && totalOpenCount(file) > 0) return false;

    const QString needle = filterEdit_->text().trimmed();
    if (needle.isEmpty()) return true;
    if (containsText(fileDisplayName(file), needle)
        || containsText(file.resource.location, needle)
        || containsText(file.resource.aliases.join(QLatin1Char('\n')), needle)
        || containsText(fileTags(file, anchors).join(QLatin1Char('\n')), needle)) return true;
    for (const AnchorLibraryAnchor &entry : anchors) {
        const Anchor &anchor = entry.anchor;
        if (containsText(anchor.name, needle)
            || containsText(anchor.aliases.join(QLatin1Char('\n')), needle)
            || containsText(anchor.tags.join(QLatin1Char('\n')), needle)
            || containsText(anchorLocatorSummary(anchor), needle)
            || containsText(anchor.targetApp, needle)) return true;
    }
    return false;
}

bool AnchorLibraryWindow::fileHasInvalidLocator(const AnchorLibraryFile &file) const
{
    if (!options_.managementService) return false;
    for (const AnchorLibraryAnchor &entry : scopedAnchors(file)) {
        if (!options_.managementService->validateAnchor(file.resource, entry.anchor).valid) return true;
    }
    return false;
}

bool AnchorLibraryWindow::confirmOperation(const QString &title, const QString &message)
{
    if (options_.confirmationHandler) return options_.confirmationHandler(title, message);
    return QMessageBox::question(this, title, message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

bool AnchorLibraryWindow::createSafetyBackup(const QString &operation)
{
    if (!options_.archiveService || options_.automaticBackupDirectory.trimmed().isEmpty()) return true;
    const AnchorLibraryOperationResult result = options_.archiveService->createAutomaticBackup(options_.automaticBackupDirectory, 10);
    if (result.success) return true;
    statusText_ = tr("Canceled %1 because the safety backup failed: %2").arg(operation, result.message);
    statusLabel_->setText(statusText_);
    return false;
}

void AnchorLibraryWindow::promptAnchorTagUpdate(bool remove)
{
    bool accepted = false;
    const QString text = QInputDialog::getText(this, remove ? tr("Remove Anchor Tags") : tr("Add Anchor Tags"), tr("Tags (comma-separated)"), QLineEdit::Normal, QString(), &accepted);
    if (accepted) updateSelectedAnchorTags(editorValues(text), remove);
}

void AnchorLibraryWindow::promptFileTagUpdate(bool remove)
{
    bool accepted = false;
    const QString text = QInputDialog::getText(this, remove ? tr("Remove File Tags") : tr("Add File Tags"), tr("Tags (comma-separated)"), QLineEdit::Normal, QString(), &accepted);
    if (accepted) updateSelectedFileTags(editorValues(text), remove);
}

void AnchorLibraryWindow::promptSavedViewCreation()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Save View"), tr("View name"), QLineEdit::Normal, QString(), &accepted);
    if (accepted) saveCurrentView(name);
}

void AnchorLibraryWindow::promptTagManager()
{
    if (!options_.managementService) return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Tag Manager"));
    dialog.resize(560, 380);
    auto *layout = new QVBoxLayout(&dialog);
    const QList<AnchorLibraryTagSummary> tags = options_.managementService->tagSummary();
    auto *table = new QTableWidget(tags.size(), 3, &dialog);
    table->setObjectName(QStringLiteral("anchorLibraryTagManagerTable"));
    table->setHorizontalHeaderLabels({tr("Tag"), tr("Files"), tr("Anchors")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int row = 0; row < tags.size(); ++row) {
        table->setItem(row, 0, new QTableWidgetItem(tags.at(row).tag));
        table->setItem(row, 1, new QTableWidgetItem(QString::number(tags.at(row).resourceCount)));
        table->setItem(row, 2, new QTableWidgetItem(QString::number(tags.at(row).anchorCount)));
    }
    auto *actions = new QHBoxLayout;
    auto *rename = new QPushButton(tr("Rename"), &dialog);
    auto *remove = new QPushButton(tr("Delete"), &dialog);
    actions->addWidget(rename);
    actions->addWidget(remove);
    actions->addStretch(1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(rename, &QPushButton::clicked, &dialog, [this, table, &dialog]() {
        if (table->currentRow() < 0) return;
        const QString oldTag = table->item(table->currentRow(), 0)->text();
        bool accepted = false;
        const QString newTag = QInputDialog::getText(&dialog, tr("Rename Tag"), tr("New name"), QLineEdit::Normal, oldTag, &accepted);
        if (accepted && renameTag(oldTag, newTag)) dialog.accept();
    });
    connect(remove, &QPushButton::clicked, &dialog, [this, table, &dialog]() {
        if (table->currentRow() < 0) return;
        const QString tag = table->item(table->currentRow(), 0)->text();
        if (confirmOperation(tr("Delete Tag"), tr("Remove tag \"%1\" from every file and anchor?").arg(tag)) && deleteTag(tag)) dialog.accept();
    });
    layout->addWidget(table, 1);
    layout->addLayout(actions);
    layout->addWidget(buttons);
    dialog.exec();
}

void AnchorLibraryWindow::setOperationResult(const AnchorLibraryOperationResult &result, bool refreshOnSuccess)
{
    if (result.success && refreshOnSuccess) refreshLibrary();
    statusText_ = result.message.trimmed().isEmpty()
        ? (result.success ? tr("Anchor Library updated") : tr("Anchor Library update failed"))
        : result.message.trimmed();
    statusLabel_->setText(statusText_);
    updateActionButtons();
}

void AnchorLibraryWindow::refreshFilterChoices()
{
    const QString tag = tagFilterCombo_->currentData().toString();
    const int kind = kindFilterCombo_->count() > 0 ? kindFilterCombo_->currentData().toInt() : -1;
    const QString app = appFilterCombo_->currentData().toString();
    QStringList tags;
    QStringList apps;
    QList<int> kinds;
    for (const AnchorLibraryFile &file : files_) {
        if (!kinds.contains(static_cast<int>(file.resource.kind))) kinds.append(static_cast<int>(file.resource.kind));
        for (const QString &value : file.resource.tags) appendUnique(tags, value);
        for (const AnchorLibraryAnchor &entry : file.anchors) {
            for (const QString &value : entry.anchor.tags) appendUnique(tags, value);
            appendUnique(apps, entry.anchor.targetApp);
        }
    }
    tags.sort(Qt::CaseInsensitive);
    apps.sort(Qt::CaseInsensitive);
    std::sort(kinds.begin(), kinds.end());
    const QSignalBlocker tagBlocker(tagFilterCombo_);
    const QSignalBlocker kindBlocker(kindFilterCombo_);
    const QSignalBlocker appBlocker(appFilterCombo_);
    tagFilterCombo_->clear();
    tagFilterCombo_->addItem(tr("All tags"), QString());
    for (const QString &value : tags) tagFilterCombo_->addItem(value, value);
    kindFilterCombo_->clear();
    kindFilterCombo_->addItem(tr("All types"), -1);
    for (int value : kinds) kindFilterCombo_->addItem(resourceKindLabel(static_cast<ResourceKind>(value)), value);
    appFilterCombo_->clear();
    appFilterCombo_->addItem(tr("All applications"), QString());
    for (const QString &value : apps) appFilterCombo_->addItem(value, value);
    tagFilterCombo_->setCurrentIndex(std::max(0, tagFilterCombo_->findData(tag)));
    kindFilterCombo_->setCurrentIndex(std::max(0, kindFilterCombo_->findData(kind)));
    appFilterCombo_->setCurrentIndex(std::max(0, appFilterCombo_->findData(app)));
}

void AnchorLibraryWindow::refreshSavedViews()
{
    const QString selected = savedViewCombo_ ? savedViewCombo_->currentData().toString() : QString();
    const QSignalBlocker blocker(savedViewCombo_);
    savedViewCombo_->clear();
    savedViewCombo_->addItem(tr("Saved views"), QString());
    if (options_.settings) {
        QStringList names;
        options_.settings->beginGroup(QStringLiteral("anchorLibrary/savedViews"));
        const QStringList groups = options_.settings->childGroups();
        for (const QString &group : groups) {
            options_.settings->beginGroup(group);
            const QString name = options_.settings->value(QStringLiteral("name")).toString();
            options_.settings->endGroup();
            if (!name.isEmpty()) names.append(name);
        }
        options_.settings->endGroup();
        names.sort(Qt::CaseInsensitive);
        for (const QString &name : names) savedViewCombo_->addItem(name, name);
    }
    const int index = savedViewCombo_->findData(selected);
    savedViewCombo_->setCurrentIndex(index >= 0 ? index : 0);
}

void AnchorLibraryWindow::updateActionButtons()
{
    const AnchorLibraryFile *file = selectedFile();
    const QList<AnchorLibraryAnchor> anchors = selectedAnchors();
    const bool manager = options_.managementService;
    const bool trash = showingTrash();
    const bool oneAnchor = anchors.size() == 1;
    const bool oneFile = selectedFiles().size() == 1;
    const QString selectedLocatorType = oneAnchor ? anchors.first().anchor.locatorType.toLower() : QString();
    const bool pdfAnchor = oneAnchor && file
        && (file->resource.kind == ResourceKind::Pdf
            || selectedLocatorType.startsWith(QStringLiteral("sumatrapdf."))
            || selectedLocatorType.startsWith(QStringLiteral("pdf.")));
    jumpButton_->setEnabled(oneAnchor && !trash && static_cast<bool>(options_.anchorJumpHandler));
    deleteButton_->setEnabled(!anchors.isEmpty() && !trash && manager);
    restoreButton_->setEnabled(trash && manager && (!anchors.isEmpty() || !selectedFiles().isEmpty()));
    purgeButton_->setEnabled(trash && manager && !selectedFiles().isEmpty());
    tagsButton_->setEnabled(manager && (!anchors.isEmpty() || !files_.isEmpty()));
    fileActionsButton_->setEnabled(manager && !selectedFiles().isEmpty());
    relinkButton_->setEnabled(manager && oneFile && file && file->resource.kind != ResourceKind::Url);
    mergeButton_->setEnabled(manager && oneFile && file && !trash && resourceIdsForFile(*file).size() > 1);
    integrityButton_->setEnabled(manager);
    dataButton_->setEnabled(options_.archiveService || options_.settings);
    undoButton_->setEnabled(manager && options_.managementService->canUndo());
    saveFileButton_->setEnabled(manager && oneFile && !trash);
    saveAnchorButton_->setEnabled(manager && oneAnchor && !trash);
    saveLocatorButton_->setEnabled(manager && oneAnchor && !trash);
    validateLocatorButton_->setEnabled(manager && oneAnchor);
    previewLocatorButton_->setEnabled(oneAnchor);
    recaptureLocatorButton_->setEnabled(manager && pdfAnchor && !trash && static_cast<bool>(options_.locatorRecaptureHandler));
}

void AnchorLibraryWindow::updateStatus()
{
    int anchors = 0;
    for (int row = 0; row < fileTable_->rowCount(); ++row) anchors += fileTable_->item(row, 3)->data(Qt::DisplayRole).toInt();
    statusText_ = showingTrash()
        ? tr("%1 file(s) | %2 Trash item(s)").arg(fileTable_->rowCount()).arg(anchors)
        : tr("%1 marked file(s) | %2 anchor(s)").arg(fileTable_->rowCount()).arg(anchors);
    statusLabel_->setText(statusText_);
}

void AnchorLibraryWindow::handleFileSortRequest(int column)
{
    const bool append = QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
    auto found = std::find_if(fileSortKeys_.begin(), fileSortKeys_.end(), [column](const SortKey &key) { return key.column == column; });
    Qt::SortOrder order = Qt::AscendingOrder;
    if (found != fileSortKeys_.end()) order = found->order == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder;
    if (!append) fileSortKeys_.clear();
    fileSortKeys_.erase(std::remove_if(fileSortKeys_.begin(), fileSortKeys_.end(), [column](const SortKey &key) { return key.column == column; }), fileSortKeys_.end());
    if (append) fileSortKeys_.append({column, order});
    else fileSortKeys_.prepend({column, order});
    fileTable_->horizontalHeader()->setSortIndicator(column, order);
    applyFilter();
}

void AnchorLibraryWindow::handleAnchorSortRequest(int column)
{
    const bool append = QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
    auto found = std::find_if(anchorSortKeys_.begin(), anchorSortKeys_.end(), [column](const SortKey &key) { return key.column == column; });
    Qt::SortOrder order = Qt::AscendingOrder;
    if (found != anchorSortKeys_.end()) order = found->order == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder;
    if (!append) anchorSortKeys_.clear();
    anchorSortKeys_.erase(std::remove_if(anchorSortKeys_.begin(), anchorSortKeys_.end(), [column](const SortKey &key) { return key.column == column; }), anchorSortKeys_.end());
    if (append) anchorSortKeys_.append({column, order});
    else anchorSortKeys_.prepend({column, order});
    anchorTable_->horizontalHeader()->setSortIndicator(column, order);
    populateSelectedFileAnchors();
}

void AnchorLibraryWindow::scheduleRepositoryRefresh()
{
    if (repositoryRefreshPending_) return;
    repositoryRefreshPending_ = true;
    QTimer::singleShot(0, this, [this]() {
        if (repositoryRefreshPending_) refreshLibrary();
    });
}

QList<const AnchorLibraryFile *> AnchorLibraryWindow::sortedVisibleFiles() const
{
    QList<const AnchorLibraryFile *> visible;
    for (const AnchorLibraryFile &file : files_) if (fileMatchesFilter(file)) visible.append(&file);
    auto textCompare = [](const QString &left, const QString &right) { return left.compare(right, Qt::CaseInsensitive); };
    std::stable_sort(visible.begin(), visible.end(), [&](const AnchorLibraryFile *left, const AnchorLibraryFile *right) {
        for (const SortKey &key : fileSortKeys_) {
            int comparison = 0;
            switch (key.column) {
            case 0: comparison = textCompare(fileDisplayName(*left), fileDisplayName(*right)); break;
            case 1: comparison = textCompare(left->resource.location, right->resource.location); break;
            case 2: comparison = textCompare(resourceKindLabel(left->resource.kind), resourceKindLabel(right->resource.kind)); break;
            case 3: comparison = scopedAnchors(*left).size() - scopedAnchors(*right).size(); break;
            case 4: comparison = textCompare(fileTags(*left, scopedAnchors(*left)).join(QLatin1Char(',')), fileTags(*right, scopedAnchors(*right)).join(QLatin1Char(','))); break;
            case 5: comparison = lastMarkedAt(scopedAnchors(*left)) < lastMarkedAt(scopedAnchors(*right)) ? -1 : (lastMarkedAt(scopedAnchors(*left)) > lastMarkedAt(scopedAnchors(*right)) ? 1 : 0); break;
            case 6: comparison = static_cast<int>(localTargetExists(left->resource)) - static_cast<int>(localTargetExists(right->resource)); break;
            case 7: comparison = totalOpenCount(*left) - totalOpenCount(*right); break;
            case 8: comparison = lastOpenedAt(*left) < lastOpenedAt(*right) ? -1 : (lastOpenedAt(*left) > lastOpenedAt(*right) ? 1 : 0); break;
            default: break;
            }
            if (comparison != 0) return key.order == Qt::AscendingOrder ? comparison < 0 : comparison > 0;
        }
        return fileGroupingKey(left->resource) < fileGroupingKey(right->resource);
    });
    return visible;
}

QList<AnchorLibraryAnchor> AnchorLibraryWindow::sortedAnchors(const QList<AnchorLibraryAnchor> &anchors) const
{
    QList<AnchorLibraryAnchor> sorted = anchors;
    auto textCompare = [](const QString &left, const QString &right) { return left.compare(right, Qt::CaseInsensitive); };
    std::stable_sort(sorted.begin(), sorted.end(), [&](const AnchorLibraryAnchor &left, const AnchorLibraryAnchor &right) {
        for (const SortKey &key : anchorSortKeys_) {
            int comparison = 0;
            const QDateTime leftUpdated = left.anchor.updatedAt.isValid() ? left.anchor.updatedAt : left.anchor.createdAt;
            const QDateTime rightUpdated = right.anchor.updatedAt.isValid() ? right.anchor.updatedAt : right.anchor.createdAt;
            switch (key.column) {
            case 0: comparison = textCompare(anchorDisplayName(left.anchor), anchorDisplayName(right.anchor)); break;
            case 1: comparison = textCompare(left.anchor.aliases.join(QLatin1Char(',')), right.anchor.aliases.join(QLatin1Char(','))); break;
            case 2: comparison = textCompare(left.anchor.tags.join(QLatin1Char(',')), right.anchor.tags.join(QLatin1Char(','))); break;
            case 3: comparison = textCompare(anchorLocatorSummary(left.anchor), anchorLocatorSummary(right.anchor)); break;
            case 4: comparison = leftUpdated < rightUpdated ? -1 : (leftUpdated > rightUpdated ? 1 : 0); break;
            case 5: comparison = left.usage.openCount - right.usage.openCount; break;
            case 6: comparison = left.usage.lastOpenedAt < right.usage.lastOpenedAt ? -1 : (left.usage.lastOpenedAt > right.usage.lastOpenedAt ? 1 : 0); break;
            case 7: comparison = textCompare(left.anchor.locatorType, right.anchor.locatorType); break;
            default: break;
            }
            if (comparison != 0) return key.order == Qt::AscendingOrder ? comparison < 0 : comparison > 0;
        }
        return anchorIdentityKey(left.anchor) < anchorIdentityKey(right.anchor);
    });
    return sorted;
}

} // namespace Pinloom
