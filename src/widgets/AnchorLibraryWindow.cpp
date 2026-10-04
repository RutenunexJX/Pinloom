#include "pinloom/widgets/PinloomUiControls.h"
#include "pinloom/widgets/AnchorLibraryWindow.h"
#include "AnchorLibraryUi.h"

#include "pinloom/core/AnchorLibraryPolicy.h"
#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/SumatraPdfCommand.h"
#include "pinloom/widgets/AnchorLocatorPreviewWidget.h"
#include "pinloom/widgets/PinloomVisualTheme.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHeaderView>
#include <QHash>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include "pinloom/widgets/PinloomItemViews.h"
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStatusBar>

#include <QTimer>
#include <QScrollBar>
#include <QToolButton>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <climits>
#include <utility>

namespace Pinloom {

namespace {

constexpr int FileKeyRole = Qt::UserRole + 1;
constexpr int ResourceIdRole = Qt::UserRole + 2;
constexpr int AnchorIdentityRole = Qt::UserRole + 3;
constexpr int SortValueRole = Qt::UserRole + 4;
constexpr int TagValuesRole = Qt::UserRole + 5;

enum FileColumn {
    FileNameColumn = 0,
    FileAliasesColumn = 1,
    FileLocationColumn = 2,
    FileTypeColumn = 3,
    FileAnchorCountColumn = 4,
    FileTagsColumn = 5,
    FileLastMarkedColumn = 6,
    FileStatusColumn = 7,
    FileOpenCountColumn = 8,
    FileLastOpenedColumn = 9,
    FileColumnCount = 10
};

enum AnchorColumn {
    AnchorNameColumn = 0,
    AnchorAliasesColumn = 1,
    AnchorTagsColumn = 2,
    AnchorTypeColumn = 3,
    AnchorUpdatedColumn = 4,
    AnchorOpenCountColumn = 5,
    AnchorLastOpenedColumn = 6,
    AnchorValidityColumn = 7,
    AnchorColumnCount = 8
};

enum InlineCellState {
    InlineCellClean = 0,
    InlineCellDirty = 1,
    InlineCellSaved = 2
};

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

QString locatorPreviewCacheKey(const AnchorLibraryFile &file, const Anchor &anchor,
                               const PdfLocatorPreviewRenderOptions *pdfOptions = nullptr)
{
    QString targetPath = anchor.targetFile.trimmed();
    if (targetPath.isEmpty()) targetPath = anchor.targetUri.trimmed();
    if (targetPath.isEmpty()) targetPath = file.resource.location.trimmed();
    const QUrl targetUrl(targetPath);
    if (targetUrl.isLocalFile()) targetPath = targetUrl.toLocalFile();
    const QFileInfo target(targetPath);
    QString key = QStringList{file.resource.id,
                       QDir::fromNativeSeparators(target.absoluteFilePath()),
                       QString::number(target.size()),
                       QString::number(target.lastModified().toMSecsSinceEpoch()),
                       anchor.id,
                       anchor.targetFile,
                       anchor.targetUri,
                       anchor.locatorType,
                       anchor.locatorJson}
        .join(QChar(0x1f));
    if (pdfOptions) key += QChar(0x1f) + pdfLocatorPreviewRequestKey(file.resource, anchor, *pdfOptions);
    return key;
}

bool isDirectPdfPreview(const Resource &resource, const Anchor &anchor)
{
    return resource.kind == ResourceKind::Pdf
        || resource.location.trimmed().endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)
        || isSumatraPdfAnchor(anchor);
}

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

QStringList editorValues(const QString &text, bool retainIdentityDuplicates = false)
{
    QStringList values;
    QString separated = text;
    separated.replace(QLatin1Char('\r'), QLatin1Char(','));
    separated.replace(QLatin1Char('\n'), QLatin1Char(','));
    for (const QString &part : separated.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        if (retainIdentityDuplicates) {
            if (!part.trimmed().isEmpty()) values.append(part);
        } else {
            appendUnique(values, part);
        }
    }
    return values;
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

QDateTime lastMarkedAt(const Resource &resource, const QList<AnchorLibraryAnchor> &anchors)
{
    const QDateTime anchorMarkedAt = lastMarkedAt(anchors);
    if (!resource.updatedAt.isValid()) return anchorMarkedAt;
    return !anchorMarkedAt.isValid() || resource.updatedAt > anchorMarkedAt
        ? resource.updatedAt
        : anchorMarkedAt;
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

QString locatorTypeLabel(const QString &locatorType)
{
    const QString type = locatorType.trimmed().toLower();
    if (type == QLatin1String("sumatrapdf.rect") || type == QLatin1String("pdf.region")) {
        return QStringLiteral("矩形选区");
    }
    if (type == QLatin1String("sumatrapdf.search") || type.startsWith(QStringLiteral("text."))) {
        return QStringLiteral("文字");
    }
    if (type == QLatin1String("sumatrapdf.page") || type == QLatin1String("pdf.page")) {
        return QStringLiteral("页码");
    }
    if (type == QLatin1String("file.line")) return QStringLiteral("行号");
    if (type.endsWith(QStringLiteral(".heading"))) return QStringLiteral("标题");
    if (type.endsWith(QStringLiteral(".bookmark"))) return QStringLiteral("书签");
    if (type.endsWith(QStringLiteral(".cell"))) return QStringLiteral("单元格");
    if (type.endsWith(QStringLiteral(".range"))) return QStringLiteral("单元格区域");
    if (type.endsWith(QStringLiteral(".slide"))) return QStringLiteral("幻灯片");
    if (type.endsWith(QStringLiteral(".shape"))) return QStringLiteral("图形");
    if (type == QLatin1String("url.fragment")) return QStringLiteral("网页位置");
    return type.isEmpty() ? QStringLiteral("未知") : type;
}

QString anchorLocatorSummary(const Anchor &anchor)
{
    return QStringLiteral("%1\n%2")
        .arg(anchor.locatorType.trimmed(), anchor.locatorJson.simplified());
}

QString inlineAnchorKey(const QString &resourceId, const QString &anchorIdentity)
{
    return resourceId + QLatin1Char('|') + anchorIdentity;
}

QString inlineCellKey(const QString &anchorKey, int column)
{
    return anchorKey + (column == AnchorAliasesColumn
                            ? QStringLiteral("|aliases")
                            : QStringLiteral("|tags"));
}

QString inlineFileCellKey(const QString &fileKey, int column)
{
    return fileKey + (column == FileAliasesColumn
                          ? QStringLiteral("|aliases")
                          : QStringLiteral("|tags"));
}

bool isHiddenApplicationFilterValue(const QString &value)
{
    QString normalized;
    for (const QChar character : value.trimmed().toLower()) {
        if (character.isLetterOrNumber()) normalized.append(character);
    }
    return normalized == QLatin1String("pdf")
        || (normalized.contains(QStringLiteral("pdf"))
            && normalized.contains(QStringLiteral("change")));
}

class AliasDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override {
        auto *editor = Ui::lineEdit(parent);
        editor->setAccessibleName(QObject::tr("Aliases (comma-separated)"));
        editor->setObjectName(QStringLiteral("anchorLibraryAliasEditor"));
        return editor;
    }
};

class TagChipDelegate final : public QStyledItemDelegate {
public:
    explicit TagChipDelegate(std::function<QColor(const QString &)> colorProvider,
                             QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
        , colorProvider_(std::move(colorProvider))
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
        if (tags.isEmpty()) return;
        painter->save();
        painter->setClipRect(option.rect);
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QFontMetrics metrics(option.font);
        const int height = metrics.height() + 6;
        const int left = option.rect.left() + 5;
        const int right = option.rect.right() - 5;
        int x = left;
        int y = option.rect.top() + 4;
        for (int tagIndex = 0; tagIndex < tags.size(); ++tagIndex) {
            const QString &tag = tags.at(tagIndex);
            const int width = std::min(right - left + 1, metrics.horizontalAdvance(tag) + 16);
            if (x != left && x + width > right) {
                painter->setPen(option.palette.color(option.state.testFlag(QStyle::State_Selected)
                                                        ? QPalette::HighlightedText : QPalette::Text));
                painter->drawText(QRect(x, y, right - x, height), Qt::AlignVCenter,
                                  QStringLiteral("+%1").arg(tags.size() - tagIndex));
                break;
            }
            const QRect chip(x, y, width, height);
            const QColor color = colorProvider_(tag);
            painter->setPen(Qt::NoPen);
            painter->setBrush(color);
            painter->drawRoundedRect(chip, 4, 4);
            painter->setPen(pinloomContrastRatio(Qt::black, color) >= pinloomContrastRatio(Qt::white, color)
                                ? Qt::black : Qt::white);
            painter->drawText(chip.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                              metrics.elidedText(tag, Qt::ElideRight, qMax(0, chip.width() - 16)));
            x += width + 5;
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(34, QFontMetrics(option.font).height() + 16));
        return size;
    }

private:
    std::function<QColor(const QString &)> colorProvider_;
};

QWidgetAction *addToneMenuAction(QMenu *menu,
                                 const QString &objectName,
                                 const QString &text,
                                 const QString &accent,
                                 QObject *context,
                                 std::function<void()> handler)
{
    auto *action = new QWidgetAction(menu);
    action->setObjectName(objectName);
    action->setText(text);
    QFont font = action->font();
    font.setBold(true);
    action->setFont(font);
    auto *button = Pinloom::Ui::toolButton(menu);
    button->setObjectName(objectName + QStringLiteral("Button"));
    button->setText(text);
    button->setFont(font);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setAutoRaise(true);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    button->setProperty("accent", accent);
    button->setAccessibleName(text);
    action->setDefaultWidget(button);
    menu->addAction(action);
    QObject::connect(action, &QAction::triggered, context, [menu, handler = std::move(handler)]() {
        menu->close();
        handler();
    });
    QObject::connect(button, &QToolButton::clicked, action, &QAction::trigger);
    return action;
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

// These rows belong to one refresh, so provider, scope and filesystem changes are
// observed on the next refresh without maintaining a persistent derived cache.
struct AnchorLibraryWindow::PdfPreviewRequest {
    Resource resource;
    Anchor anchor;
    PdfLocatorPreviewRenderOptions options;
    QString cacheKey;
    quint64 generation = 0;
    bool showExpanded = false;
    PdfLocatorPreviewCancellation cancellation = std::make_shared<std::atomic_bool>(false);
};

struct AnchorLibraryWindow::FileViewData {
    FileViewData(const AnchorLibraryFile *source, QList<AnchorLibraryAnchor> scoped)
        : file(source), anchors(std::move(scoped))
    {
    }

    const QString &name() const
    {
        if (!nameValue) nameValue = fileDisplayName(*file);
        return *nameValue;
    }

    const QDateTime &markedAt() const
    {
        if (!markedValue) markedValue = lastMarkedAt(file->resource, anchors);
        return *markedValue;
    }

    const QDateTime &openedAt() const
    {
        if (!openedValue) openedValue = lastOpenedAt(*file);
        return *openedValue;
    }

    int openCount() const
    {
        if (!openCountValue) openCountValue = totalOpenCount(*file);
        return *openCountValue;
    }

    bool targetExists() const
    {
        if (!targetExistsValue) targetExistsValue = localTargetExists(file->resource);
        return *targetExistsValue;
    }

    void prepareForDisplay(bool sortTags)
    {
        key = fileGroupingKey(file->resource);
        aliases = file->resource.aliases.join(QLatin1Char(','));
        if (sortTags) tags = file->resource.tags.join(QLatin1Char(','));
        type = resourceKindLabel(file->resource.kind);
        name();
        markedAt();
        openedAt();
        openCount();
        targetExists();
    }

    const AnchorLibraryFile *file;
    QList<AnchorLibraryAnchor> anchors;
    QString key;
    QString aliases;
    QString tags;
    QString type;
    mutable std::optional<QString> nameValue;
    mutable std::optional<QDateTime> markedValue;
    mutable std::optional<QDateTime> openedValue;
    mutable std::optional<int> openCountValue;
    mutable std::optional<bool> targetExistsValue;
    mutable std::optional<bool> invalidLocator;
};

struct AnchorLibraryWindow::AnchorViewData {
    AnchorViewData(const AnchorLibraryAnchor &source, bool sortTags)
        : entry(source)
        , identity(anchorIdentityKey(source.anchor))
        , name(anchorDisplayName(source.anchor))
        , aliases(source.anchor.aliases.join(QLatin1Char(',')))
        , tags(sortTags ? source.anchor.tags.join(QLatin1Char(',')) : QString())
        , type(locatorTypeLabel(source.anchor.locatorType))
        , updated(source.anchor.updatedAt.isValid() ? source.anchor.updatedAt : source.anchor.createdAt)
    {
    }

    AnchorLibraryAnchor entry;
    QString identity;
    QString name;
    QString aliases;
    QString tags;
    QString type;
    QDateTime updated;
};

AnchorLibraryWindow::AnchorLibraryWindow(AnchorLibraryWindowOptions options, QWidget *parent)
    : Ui::MainWindow(parent)
    , options_(std::move(options))
{
    setObjectName(QStringLiteral("anchorLibraryWindow"));
    setProperty("pinloomRole", QStringLiteral("canvas"));
    setWindowTitle(tr("Pinloom Anchor Library"));
    setMinimumSize(920, 600);
    resize(1380, 820);

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("anchorLibraryCentral"));
    central->setProperty("pinloomRole", QStringLiteral("canvas"));
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 8);
    layout->setSpacing(8);

    auto *titleRow = new QHBoxLayout;
    auto *title = AnchorLibraryUi::text(tr("Anchor Library"), central, true);
    title->setObjectName(QStringLiteral("anchorLibraryHeading"));
    titleRow->addWidget(title, 1);
    auto *queryRow = new QHBoxLayout;
    queryRow->setSpacing(8);
    filterEdit_ = Pinloom::Ui::lineEdit(central);
    filterEdit_->setObjectName(QStringLiteral("anchorLibraryFilterEdit"));
    filterEdit_->setPlaceholderText(tr("Search marked files, anchors, tags, paths, and locators"));
    filterEdit_->setClearButtonEnabled(true);
    filterEdit_->setAccessibleName(tr("Search Anchor Library"));
    savedViewCombo_ = Pinloom::Ui::comboBox(central);
    savedViewCombo_->setObjectName(QStringLiteral("anchorLibrarySavedViewCombo"));
    savedViewCombo_->setMinimumWidth(145);
    savedViewCombo_->setAccessibleName(tr("Saved view"));
    scopeCombo_ = Pinloom::Ui::comboBox(central);
    scopeCombo_->setObjectName(QStringLiteral("anchorLibraryScopeCombo"));
    scopeCombo_->addItem(tr("All marked files"), static_cast<int>(AnchorLibraryScope::All));
    scopeCombo_->addItem(tr("Untagged"), static_cast<int>(AnchorLibraryScope::Untagged));
    scopeCombo_->addItem(tr("Missing files"), static_cast<int>(AnchorLibraryScope::Missing));
    scopeCombo_->addItem(tr("Trash"), static_cast<int>(AnchorLibraryScope::Trash));
    scopeCombo_->addItem(tr("Duplicates"), static_cast<int>(AnchorLibraryScope::Duplicates));
    scopeCombo_->addItem(tr("Invalid locators"), static_cast<int>(AnchorLibraryScope::InvalidLocator));
    scopeCombo_->addItem(tr("Recently modified"), static_cast<int>(AnchorLibraryScope::RecentlyModified));
    scopeCombo_->addItem(tr("Recently deleted"), static_cast<int>(AnchorLibraryScope::RecentlyDeleted));
    scopeCombo_->setAccessibleName(tr("Library scope"));
    refreshButton_ = Pinloom::Ui::toolButton(central);
    refreshButton_->setObjectName(QStringLiteral("anchorLibraryRefreshButton"));
    refreshButton_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    refreshButton_->setToolTip(tr("Refresh library"));
    refreshButton_->setAccessibleName(refreshButton_->toolTip());
    refreshButton_->setProperty("pinloomControl", QStringLiteral("icon"));
    trashButton_ = Pinloom::Ui::toolButton(central);
    trashButton_->setObjectName(QStringLiteral("anchorLibraryTrashButton"));
    trashButton_->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    trashButton_->setText(QStringLiteral("回收站"));
    trashButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    trashButton_->setCheckable(true);
    trashButton_->setToolTip(tr("Open Trash"));
    trashButton_->setAccessibleName(trashButton_->toolTip());
    queryRow->addWidget(filterEdit_, 1);
    titleRow->addWidget(savedViewCombo_);
    titleRow->addWidget(scopeCombo_);
    titleRow->addWidget(trashButton_);
    titleRow->addWidget(refreshButton_);

    auto *filterRow = new QGridLayout;
    filterRow->setHorizontalSpacing(6);
    filterRow->setVerticalSpacing(6);
    tagFilterCombo_ = Pinloom::Ui::comboBox(central);
    tagFilterCombo_->setObjectName(QStringLiteral("anchorLibraryTagFilterCombo"));
    tagFilterCombo_->setMinimumWidth(120);
    tagFilterCombo_->setAccessibleName(tr("File tag filter"));
    anchorTagFilterCombo_ = Pinloom::Ui::comboBox(central);
    anchorTagFilterCombo_->setObjectName(QStringLiteral("anchorLibraryAnchorTagFilterCombo"));
    anchorTagFilterCombo_->setMinimumWidth(120);
    anchorTagFilterCombo_->setAccessibleName(tr("Anchor tag filter"));
    kindFilterCombo_ = Pinloom::Ui::comboBox(central);
    kindFilterCombo_->setObjectName(QStringLiteral("anchorLibraryKindFilterCombo"));
    kindFilterCombo_->setAccessibleName(tr("File type filter"));
    appFilterCombo_ = Pinloom::Ui::comboBox(central);
    appFilterCombo_->setObjectName(QStringLiteral("anchorLibraryAppFilterCombo"));
    appFilterCombo_->setMinimumWidth(120);
    appFilterCombo_->setAccessibleName(tr("Source application filter"));
    directoryFilterEdit_ = Pinloom::Ui::lineEdit(central);
    directoryFilterEdit_->setObjectName(QStringLiteral("anchorLibraryDirectoryFilterEdit"));
    directoryFilterEdit_->setPlaceholderText(tr("Directory"));
    directoryFilterEdit_->setClearButtonEnabled(true);
    directoryFilterEdit_->setAccessibleName(tr("Directory filter"));
    timeFilterCombo_ = Pinloom::Ui::comboBox(central);
    timeFilterCombo_->setObjectName(QStringLiteral("anchorLibraryTimeFilterCombo"));
    timeFilterCombo_->addItem(tr("Any time"), 0);
    timeFilterCombo_->addItem(tr("Last 7 days"), 7);
    timeFilterCombo_->addItem(tr("Last 30 days"), 30);
    timeFilterCombo_->addItem(tr("Last 90 days"), 90);
    timeFilterCombo_->setAccessibleName(tr("Modified time filter"));
    usageFilterCombo_ = Pinloom::Ui::comboBox(central);
    usageFilterCombo_->setObjectName(QStringLiteral("anchorLibraryUsageFilterCombo"));
    usageFilterCombo_->addItem(tr("Any usage"), static_cast<int>(UsageFilter::Any));
    usageFilterCombo_->addItem(tr("Pinned"), static_cast<int>(UsageFilter::Pinned));
    usageFilterCombo_->addItem(tr("Recently opened"), static_cast<int>(UsageFilter::RecentlyOpened));
    usageFilterCombo_->addItem(tr("Never opened"), static_cast<int>(UsageFilter::NeverOpened));
    usageFilterCombo_->setAccessibleName(tr("Usage filter"));
    filterRow->addWidget(tagFilterCombo_, 0, 0);
    filterRow->addWidget(anchorTagFilterCombo_, 0, 1);
    filterRow->addWidget(kindFilterCombo_, 0, 2);
    filterRow->addWidget(appFilterCombo_, 0, 3);
    filterRow->addWidget(directoryFilterEdit_, 1, 2, 1, 2);
    filterRow->addWidget(timeFilterCombo_, 1, 0);
    filterRow->addWidget(usageFilterCombo_, 1, 1);
    filterRow->setColumnStretch(2, 1);
    filterRow->setColumnStretch(3, 1);
    for (auto *combo : {savedViewCombo_, scopeCombo_, tagFilterCombo_, anchorTagFilterCombo_,
                        kindFilterCombo_, appFilterCombo_, timeFilterCombo_, usageFilterCombo_}) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(12);
        connect(combo, &QComboBox::currentTextChanged, combo, &QWidget::setToolTip);
    }

    auto *filterContent = new QWidget(central);
    filterContent->setObjectName(QStringLiteral("anchorLibraryFilterContent"));
    filterContent->setLayout(filterRow);
    filterToggle_ = Ui::pushButton(tr("Filters"), central);
    filterToggle_->setObjectName(QStringLiteral("anchorLibraryFilterToggle"));
    auto *filterDrawer = Ui::collapsibleSection(filterToggle_, filterContent, central);
    filterDrawer->setObjectName(QStringLiteral("anchorLibraryFilterDrawer"));

    auto *actionRow = AnchorLibraryUi::toolBar(central);
    actionRow->setObjectName(QStringLiteral("anchorLibraryActionBar"));
    restoreButton_ = Pinloom::Ui::toolButton(central);
    restoreButton_->setObjectName(QStringLiteral("anchorLibraryRestoreButton"));
    restoreButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    restoreButton_->setToolTip(tr("Restore selection"));
    tagsButton_ = Pinloom::Ui::toolButton(central);
    tagsButton_->setObjectName(QStringLiteral("anchorLibraryTagsButton"));
    tagsButton_->setText(tr("Anchors"));
    tagsButton_->setPopupMode(QToolButton::InstantPopup);
    auto *anchorMenu = Pinloom::Ui::menu(tagsButton_);
    QAction *addAnchorTags = anchorMenu->addAction(tr("Add tags"));
    QAction *removeAnchorTags = anchorMenu->addAction(tr("Remove tags"));
    anchorMenu->addSeparator();
    QAction *pinAnchors = anchorMenu->addAction(tr("Pin"));
    QAction *unpinAnchors = anchorMenu->addAction(tr("Unpin"));
    anchorMenu->addSeparator();
    QAction *tagManager = anchorMenu->addAction(tr("Tag manager"));
    tagsButton_->setMenu(anchorMenu);
    fileActionsButton_ = Pinloom::Ui::toolButton(central);
    fileActionsButton_->setObjectName(QStringLiteral("anchorLibraryFileActionsButton"));
    fileActionsButton_->setText(tr("Files"));
    fileActionsButton_->setPopupMode(QToolButton::InstantPopup);
    auto *fileMenu = Pinloom::Ui::menu(fileActionsButton_);
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
    relinkButton_ = Pinloom::Ui::toolButton(central);
    relinkButton_->setObjectName(QStringLiteral("anchorLibraryRelinkButton"));
    relinkButton_->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    relinkButton_->setToolTip(tr("Relink selected file"));
    mergeButton_ = Pinloom::Ui::toolButton(central);
    mergeButton_->setObjectName(QStringLiteral("anchorLibraryMergeButton"));
    mergeButton_->setText(tr("Merge"));
    mergeButton_->setToolTip(tr("Merge duplicate file records"));
    integrityButton_ = Pinloom::Ui::toolButton(central);
    integrityButton_->setObjectName(QStringLiteral("anchorLibraryIntegrityButton"));
    integrityButton_->setText(tr("Inspect"));
    integrityButton_->setToolTip(tr("Scan library integrity"));
    manageButton_ = Pinloom::Ui::toolButton(central);
    manageButton_->setObjectName(QStringLiteral("anchorLibraryManageButton"));
    manageButton_->setText(tr("Manage"));
    manageButton_->setPopupMode(QToolButton::InstantPopup);
    auto *manageMenu = Pinloom::Ui::menu(manageButton_);
    QAction *saveView = manageMenu->addAction(tr("Save current view"));
    QAction *deleteView = manageMenu->addAction(tr("Delete current saved view"));
    QAction *operationHistory = manageMenu->addAction(tr("Operation history"));
    manageButton_->setMenu(manageMenu);
    undoButton_ = Pinloom::Ui::toolButton(central);
    undoButton_->setObjectName(QStringLiteral("anchorLibraryUndoButton"));
    undoButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    undoButton_->setToolTip(tr("Undo last library operation"));
    for (QToolButton *button : {restoreButton_, tagsButton_, fileActionsButton_, relinkButton_,
                                mergeButton_, integrityButton_, manageButton_, undoButton_}) {
        button->setAccessibleName(button->toolTip().isEmpty() ? button->text() : button->toolTip());
        auto *action = actionRow->addWidget(button);
        if (button == restoreButton_) restoreAction_ = action;
    }
    saveButton_ = Ui::toolButton(central);
    saveButton_->setObjectName(QStringLiteral("anchorLibrarySaveButton"));
    saveButton_->setText(tr("Save changes"));
    saveButton_->setAccessibleName(tr("Save pending alias and tag edits"));
    saveButton_->setToolTip(tr("Save pending alias and tag edits (Ctrl+S)"));
    actionRow->addWidget(saveButton_);
    auto *detailsButton = Ui::toolButton(central);
    detailsButton->setObjectName(QStringLiteral("anchorLibraryColumnsButton"));
    detailsButton->setText(tr("Details"));
    detailsButton->setCheckable(true);
    detailsButton->setAccessibleName(tr("Show detailed table columns"));
    detailsButton->setToolTip(tr("Show paths, timestamps and usage columns"));
    actionRow->addWidget(detailsButton);

    auto *mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->setObjectName(QStringLiteral("anchorLibraryMainSplitter"));
    mainSplitter->setChildrenCollapsible(false);
    auto *tablesSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    tablesSplitter->setObjectName(QStringLiteral("anchorLibraryTablesSplitter"));
    tablesSplitter->setChildrenCollapsible(false);

    auto *fileCard = Ui::section(tr("Files"), tablesSplitter);
    fileCard->setObjectName(QStringLiteral("anchorLibraryFilesCard"));
    fileCountLabel_ = fileCard->findChild<QLabel *>();
    fileCountLabel_->setObjectName(QStringLiteral("anchorLibraryFileCount"));
    fileTable_ = new Pinloom::Ui::Table(fileCard);
    fileTable_->setObjectName(QStringLiteral("anchorLibraryFileTable"));
    fileTable_->setAccessibleName(tr("Marked files"));
    fileTable_->setAccessibleDescription(tr("Files in the current Anchor Library scope"));
    fileTable_->setColumnCount(FileColumnCount);
    fileTable_->setHorizontalHeaderLabels({tr("File"), tr("File aliases"), tr("Location"),
                                           tr("Type"), tr("Anchors"), tr("File tags"),
                                           tr("Last marked"), tr("Status"), tr("Opens"),
                                           tr("Last opened")});
    fileTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fileTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileTable_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    fileTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    fileTable_->setAlternatingRowColors(true);
    fileTable_->setSortingEnabled(false);
    fileTable_->setWordWrap(false);
    fileTable_->setTextElideMode(Qt::ElideRight);
    fileTable_->setMinimumSize(240, 100);
    fileTable_->verticalHeader()->setMinimumSectionSize(34);
    fileTable_->verticalHeader()->setVisible(false);
    fileTable_->horizontalHeader()->setMinimumSectionSize(56);
    for (int column : {FileNameColumn, FileAliasesColumn, FileTagsColumn}) {
        fileTable_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    fileTable_->horizontalHeader()->setSectionResizeMode(FileLocationColumn, QHeaderView::Stretch);
    fileTable_->setColumnWidth(FileNameColumn, 170);
    fileTable_->setColumnWidth(FileAliasesColumn, 150);
    fileTable_->setColumnWidth(FileLocationColumn, 240);
    fileTable_->setColumnWidth(FileTagsColumn, 170);
    for (int column : {FileTypeColumn, FileAnchorCountColumn, FileLastMarkedColumn,
                       FileStatusColumn, FileOpenCountColumn, FileLastOpenedColumn}) {
        fileTable_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    fileTable_->horizontalHeader()->setSortIndicatorShown(true);
    fileTable_->horizontalHeader()->setSortIndicator(FileLastMarkedColumn, Qt::DescendingOrder);

    auto *anchorCard = Ui::section(tr("Anchors"), tablesSplitter);
    anchorCard->setObjectName(QStringLiteral("anchorLibraryAnchorsCard"));
    anchorCountLabel_ = anchorCard->findChild<QLabel *>();
    anchorCountLabel_->setObjectName(QStringLiteral("anchorLibraryAnchorCount"));
    anchorTable_ = new Pinloom::Ui::Table(anchorCard);
    anchorTable_->setObjectName(QStringLiteral("anchorLibraryAnchorTable"));
    anchorTable_->setAccessibleName(tr("Anchors"));
    anchorTable_->setAccessibleDescription(tr("Anchors for the selected marked file"));
    anchorTable_->setColumnCount(AnchorColumnCount);
    anchorTable_->setHorizontalHeaderLabels({tr("Anchor"), tr("Anchor aliases"), tr("Anchor tags"), tr("Type"),
                                             tr("Updated"), tr("Opens"), tr("Last opened"), tr("Validity")});
    anchorTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    anchorTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    anchorTable_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    anchorTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    anchorTable_->setAlternatingRowColors(true);
    anchorTable_->setSortingEnabled(false);
    anchorTable_->setWordWrap(false);
    anchorTable_->setTextElideMode(Qt::ElideRight);
    anchorTable_->setMinimumSize(240, 100);
    anchorTable_->verticalHeader()->setMinimumSectionSize(34);
    anchorTable_->verticalHeader()->setVisible(false);
    anchorTable_->horizontalHeader()->setMinimumSectionSize(56);
    for (int column : {AnchorNameColumn, AnchorTypeColumn}) {
        anchorTable_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
    }
    anchorTable_->horizontalHeader()->setSectionResizeMode(AnchorAliasesColumn, QHeaderView::Stretch);
    anchorTable_->horizontalHeader()->setSectionResizeMode(AnchorTagsColumn, QHeaderView::Stretch);
    anchorTable_->setColumnWidth(AnchorNameColumn, 150);
    anchorTable_->setColumnWidth(AnchorAliasesColumn, 180);
    anchorTable_->setColumnWidth(AnchorTagsColumn, 180);
    anchorTable_->setColumnWidth(AnchorTypeColumn, 110);
    for (int column : {4, 5, 6, 7}) {
        anchorTable_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    anchorTable_->horizontalHeader()->setSortIndicatorShown(true);
    anchorTable_->horizontalHeader()->setSortIndicator(4, Qt::DescendingOrder);
    fileTable_->setItemDelegateForColumn(
        FileTagsColumn,
        new TagChipDelegate([this](const QString &tag) { return colorForTag(tag); }, fileTable_));
    anchorTable_->setItemDelegateForColumn(
        AnchorTagsColumn,
        new TagChipDelegate([this](const QString &tag) { return colorForTag(tag); }, anchorTable_));
    fileTable_->setItemDelegateForColumn(FileAliasesColumn, new AliasDelegate(fileTable_));
    anchorTable_->setItemDelegateForColumn(AnchorAliasesColumn, new AliasDelegate(anchorTable_));
    fileEmptyLabel_ = AnchorLibraryUi::text(tr("No marked files match this view.\nCapture an anchor or adjust the filters."), fileCard);
    fileEmptyLabel_->setObjectName(QStringLiteral("anchorLibraryFileEmptyState"));
    fileEmptyLabel_->setAlignment(Qt::AlignCenter);
    anchorEmptyLabel_ = AnchorLibraryUi::text(tr("Select a file to browse its anchors."), anchorCard);
    anchorEmptyLabel_->setObjectName(QStringLiteral("anchorLibraryAnchorEmptyState"));
    anchorEmptyLabel_->setAlignment(Qt::AlignCenter);
    auto *fileCardLayout = qobject_cast<QVBoxLayout *>(fileCard->layout());
    fileCardLayout->addWidget(fileTable_, 1);
    fileCardLayout->addWidget(fileEmptyLabel_, 1);
    auto *anchorCardLayout = qobject_cast<QVBoxLayout *>(anchorCard->layout());
    anchorCardLayout->addWidget(anchorTable_, 1);
    anchorCardLayout->addWidget(anchorEmptyLabel_, 1);
    tablesSplitter->addWidget(fileCard);
    tablesSplitter->addWidget(anchorCard);
    tablesSplitter->setStretchFactor(0, 3);
    tablesSplitter->setStretchFactor(1, 2);

    auto *inspectorCard = Ui::section(tr("Preview"), mainSplitter);
    inspectorCard->setObjectName(QStringLiteral("anchorLibraryPreviewCard"));
    inspectorCard->setMinimumWidth(240);
    inspectorCard->setMaximumWidth(400);
    auto *inspectorScroll = Ui::scrollArea(inspectorCard);
    inspectorScroll->setObjectName(QStringLiteral("anchorLibraryInspectorScroll"));
    inspectorScroll->setProperty("pinloomRole", QStringLiteral("panel"));
    inspectorScroll->setAccessibleName(tr("Anchor inspector"));
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setMinimumWidth(0);
    inspectorScroll->setFrameShape(QFrame::NoFrame);
    auto *inspector = new QWidget(inspectorScroll);
    inspector->setObjectName(QStringLiteral("anchorLibraryInspector"));
    inspector->setProperty("pinloomRole", QStringLiteral("panel"));
    auto *inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    inspectorLayout->setSpacing(8);
    inspectorTitle_ = AnchorLibraryUi::text(tr("Select an anchor"), inspector);
    inspectorTitle_->setObjectName(QStringLiteral("anchorLibraryInspectorTitle"));
    inspectorLocation_ = AnchorLibraryUi::text({}, inspector);
    inspectorLocation_->setObjectName(QStringLiteral("anchorLibraryInspectorLocation"));
    inspectorLocation_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    inspectorLocation_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    inspectorLayout->addWidget(inspectorTitle_);
    inspectorLayout->addWidget(inspectorLocation_);

    locatorPreview_ = new AnchorLocatorPreviewWidget(inspector);
    inspectorLayout->addWidget(locatorPreview_, 1);
    inspectorScroll->setWidget(inspector);
    auto *inspectorCardLayout = qobject_cast<QVBoxLayout *>(inspectorCard->layout());
    inspectorCardLayout->addWidget(inspectorScroll, 1);
    auto *previewActions = new QHBoxLayout;
    openButton_ = Ui::toolButton(inspectorCard);
    openButton_->setObjectName(QStringLiteral("anchorLibraryOpenButton"));
    openButton_->setText(tr("Open anchor"));
    openButton_->setAccessibleName(openButton_->text());
    previewButton_ = Ui::toolButton(inspectorCard);
    previewButton_->setObjectName(QStringLiteral("anchorLibraryPreviewButton"));
    previewButton_->setText(tr("Enlarge"));
    previewButton_->setAccessibleName(tr("Enlarge anchor preview"));
    previewActions->addWidget(openButton_);
    previewActions->addWidget(previewButton_);
    previewActions->addStretch();
    inspectorCardLayout->addLayout(previewActions);
    mainSplitter->addWidget(tablesSplitter);
    mainSplitter->addWidget(inspectorCard);
    mainSplitter->setStretchFactor(0, 4);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({1000, 360});
    Ui::rememberSplitter(mainSplitter, options_.settings, QStringLiteral("layout/anchorLibraryMainSplitter"));
    Ui::rememberSplitter(tablesSplitter, options_.settings, QStringLiteral("layout/anchorLibraryTablesSplitter"));

    auto *footer = Ui::statusBar(this);
    footer->setObjectName(QStringLiteral("anchorLibraryStatusBar"));
    footer->setSizeGripEnabled(false);
    statusLabel_ = AnchorLibraryUi::text({}, footer);
    statusLabel_->setObjectName(QStringLiteral("anchorLibraryStatusLabel"));
    statusLabel_->setAccessibleName(tr("Library status"));
    statusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    footer->addWidget(statusLabel_, 1);
    layout->addLayout(titleRow);
    layout->addLayout(queryRow);
    layout->addWidget(filterDrawer);
    layout->addWidget(actionRow);
    setCentralWidget(AnchorLibraryUi::page(central, mainSplitter, footer, this));
    AnchorLibraryUi::applyIcons(this);
    setDetailedColumns(false);
    connect(detailsButton, &QToolButton::toggled, this, &AnchorLibraryWindow::setDetailedColumns);
    connect(saveButton_, &QToolButton::clicked, this, &AnchorLibraryWindow::savePendingInlineEdits);
    connect(openButton_, &QToolButton::clicked, this, &AnchorLibraryWindow::activateSelectedAnchor);
    connect(previewButton_, &QToolButton::clicked, this, &AnchorLibraryWindow::previewSelectedAnchor);

    connect(filterEdit_, &QLineEdit::textChanged, this, &AnchorLibraryWindow::applyFilter);
    connect(scopeCombo_, &QComboBox::currentIndexChanged, this, [this]() {
        applyFilter();
        applyLibraryTheme();
    });
    connect(tagFilterCombo_, &QComboBox::currentIndexChanged, this, &AnchorLibraryWindow::applyFilter);
    connect(anchorTagFilterCombo_, &QComboBox::currentIndexChanged, this, &AnchorLibraryWindow::applyFilter);
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
    connect(trashButton_, &QToolButton::clicked, this, [this](bool checked) {
        if (checked) showTrash();
        else scopeCombo_->setCurrentIndex(std::max(0, scopeCombo_->findData(static_cast<int>(AnchorLibraryScope::All))));
    });
    connect(restoreButton_, &QToolButton::clicked, this, [this]() {
        if (!restoreSelectedFiles()) restoreSelectedAnchors();
    });
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
    connect(fileTable_, &Pinloom::Ui::Table::itemSelectionChanged, this, &AnchorLibraryWindow::populateSelectedFileAnchors);
    connect(anchorTable_, &Pinloom::Ui::Table::itemSelectionChanged, this, [this]() {
        populateInspector();
        updateActionButtons();
    });
    connect(fileTable_, &QWidget::customContextMenuRequested, this, &AnchorLibraryWindow::showFileContextMenu);
    connect(anchorTable_, &QWidget::customContextMenuRequested, this, &AnchorLibraryWindow::showAnchorContextMenu);
    connect(fileTable_, &Pinloom::Ui::Table::cellClicked, this, [this](int row, int column) {
        if (column == FileTagsColumn && !showingTrash()) openFileTagEditor(row);
    });
    connect(fileTable_, &Pinloom::Ui::Table::itemChanged, this, &AnchorLibraryWindow::handleFileItemChanged);
    connect(fileTable_, &Pinloom::Ui::Table::itemDoubleClicked, this, [this](Pinloom::Ui::TableItem *item) {
        if (item && item->column() == FileAliasesColumn && !showingTrash()) fileTable_->editItem(item);
    });
    connect(anchorTable_, &Pinloom::Ui::Table::cellClicked, this, [this](int row, int column) {
        if (column == AnchorTagsColumn && !showingTrash()) openAnchorTagEditor(row);
    });
    connect(anchorTable_, &Pinloom::Ui::Table::itemChanged, this, &AnchorLibraryWindow::handleAnchorItemChanged);
    connect(anchorTable_, &Pinloom::Ui::Table::itemDoubleClicked, this, [this](Pinloom::Ui::TableItem *item) {
        if (!item) return;
        if (item->column() == AnchorAliasesColumn && !showingTrash()) {
            anchorTable_->editItem(item);
            return;
        }
        if (item->column() != AnchorTagsColumn) activateSelectedAnchor();
    });
    connect(fileTable_->horizontalHeader(), &QHeaderView::sectionClicked, this, &AnchorLibraryWindow::handleFileSortRequest);
    connect(anchorTable_->horizontalHeader(), &QHeaderView::sectionClicked, this, &AnchorLibraryWindow::handleAnchorSortRequest);
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, anchorTable_);
    deleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, [this]() {
        if (qobject_cast<QLineEdit *>(QApplication::focusWidget())) return;
        if (showingTrash()) permanentlyDeleteSelectedAnchors();
        else deleteSelectedAnchors();
    });
    auto *fileDeleteShortcut = new QShortcut(QKeySequence::Delete, fileTable_);
    fileDeleteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(fileDeleteShortcut, &QShortcut::activated, this, [this]() {
        if (qobject_cast<QLineEdit *>(QApplication::focusWidget())) return;
        if (showingTrash()) showPermanentFileDeleteMenu();
        else deleteAllAnchorsForSelectedFiles();
    });
    auto *saveInlineShortcut = new QShortcut(QKeySequence::Save, this);
    saveInlineShortcut->setContext(Qt::WindowShortcut);
    connect(saveInlineShortcut, &QShortcut::activated, this, &AnchorLibraryWindow::savePendingInlineEdits);
    auto *findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, filterEdit_, qOverload<>(&QWidget::setFocus));
    auto *refreshShortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(refreshShortcut, &QShortcut::activated, this, &AnchorLibraryWindow::refreshLibrary);
    auto *openShortcut = new QShortcut(QKeySequence(Qt::Key_Return), anchorTable_);
    openShortcut->setContext(Qt::WidgetShortcut);
    connect(openShortcut, &QShortcut::activated, this, &AnchorLibraryWindow::activateSelectedAnchor);
    QWidget::setTabOrder(filterEdit_, filterToggle_);
    QWidget::setTabOrder(filterToggle_, fileTable_);
    QWidget::setTabOrder(fileTable_, anchorTable_);
    QWidget::setTabOrder(anchorTable_, locatorPreview_);

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
    applyLibraryTheme();
}

AnchorLibraryWindow::~AnchorLibraryWindow()
{
    cancelPdfLocatorPreview();
    if (options_.repository && repositoryListenerId_ >= 0) {
        options_.repository->removeChangeListener(repositoryListenerId_);
    }
}

void AnchorLibraryWindow::hideEvent(QHideEvent *event)
{
    ++locatorPreviewRequestGeneration_;
    cancelPdfLocatorPreview();
    Ui::MainWindow::hideEvent(event);
}

void AnchorLibraryWindow::showEvent(QShowEvent *event)
{
    Ui::MainWindow::showEvent(event);
    scheduleSelectedAnchorPreview();
}

void AnchorLibraryWindow::refreshLibrary()
{
    repositoryRefreshPending_ = false;
    const QList<AnchorLibraryFile> provided = options_.filesProvider ? options_.filesProvider() : QList<AnchorLibraryFile>{};
    files_.clear();
    fileResourceIds_.clear();
    QHash<QString, int> groupedIndexes;
    for (AnchorLibraryFile file : provided) {
        for (AnchorLibraryAnchor &entry : file.anchors) {
            if (entry.resourceId.trimmed().isEmpty()) entry.resourceId = file.resource.id;
            entry.resourceDeleted = file.resource.deleted;
            if (entry.usage.resourceId.trimmed().isEmpty()) entry.usage.resourceId = entry.resourceId;
        }
        file.resource.anchors.clear();
        if (file.usage.resourceId.trimmed().isEmpty()) file.usage.resourceId = file.resource.id;
        const QString key = fileGroupingKey(file.resource);
        // Anchorless records still own metadata when multiple records share a file row.
        appendUnique(fileResourceIds_[key], file.resource.id);
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

void AnchorLibraryWindow::showTrash()
{
    const int index = scopeCombo_->findData(static_cast<int>(AnchorLibraryScope::Trash));
    if (index >= 0) scopeCombo_->setCurrentIndex(index);
}

bool AnchorLibraryWindow::isTrashVisible() const
{
    return showingTrash();
}

bool AnchorLibraryWindow::activateSelectedAnchor()
{
    const AnchorLibraryFile *file = selectedFile();
    const auto entry = selectedAnchor();
    if (!file || !entry || selectedAnchors().size() != 1) {
        statusText_ = tr("Select one anchor to open");
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    if (showingTrash() || entry->resourceDeleted) {
        statusText_ = tr("Restore the anchor before opening it");
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    if (!options_.anchorJumpHandler) {
        statusText_ = tr("Anchor opening is not configured");
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    QString status;
    const bool opened = options_.anchorJumpHandler(*file, entry.value(), &status);
    statusText_ = status.trimmed().isEmpty() ? (opened ? tr("Opened anchor") : tr("Unable to open anchor")) : status.trimmed();
    Pinloom::Ui::setStatusText(statusLabel_, statusText_);
    if (opened) emit anchorActivated(entry->resourceId, entry->anchor.id);
    return opened;
}

bool AnchorLibraryWindow::deleteSelectedAnchor() { return deleteSelectedAnchors(); }

bool AnchorLibraryWindow::deleteSelectedAnchors()
{
    const QList<AnchorLibraryAnchor> entries = selectedAnchors();
    if (entries.isEmpty() || showingTrash()) {
        statusText_ = entries.isEmpty() ? tr("Select at least one anchor to delete") : tr("Selected anchors are already in Trash");
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    if (!options_.managementService) return false;
    const QString subject = entries.size() == 1
        ? QStringLiteral("\"%1\"").arg(anchorDisplayName(entries.first().anchor))
        : tr("%1 selected anchors").arg(entries.size());
    if (!confirmOperation(tr("Delete Anchors"), tr("Move %1 to Trash?\n\nThe files will not be deleted.").arg(subject))) {
        statusText_ = tr("Delete canceled");
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    const AnchorLibraryOperationResult result = options_.managementService->setAnchorsDeleted(selectedAnchorReferences(), true);
    setOperationResult(result);
    if (result.success) {
        for (const AnchorLibraryAnchor &entry : entries) emit anchorDeleted(entry.resourceId, entry.anchor.id);
    }
    return result.success;
}

bool AnchorLibraryWindow::deleteAllAnchorsForSelectedFiles()
{
    if (!options_.managementService || showingTrash()) return false;
    const QList<AnchorReference> references = allAnchorReferencesForSelectedFiles(false);
    if (references.isEmpty()) {
        statusText_ = tr("The selected files have no active anchors");
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    if (!confirmOperation(tr("Delete All Anchors"),
                          tr("Move all %1 anchors in the selected files to Trash?\n\nThe files will not be deleted.")
                              .arg(references.size()))) {
        statusText_ = tr("Delete canceled");
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    AnchorLibraryOperationResult result = options_.managementService->setAnchorsDeleted(references, true);
    if (!result.success) result.message = tr("Delete all anchors failed: %1").arg(result.message);
    setOperationResult(result);
    if (result.success) {
        for (const AnchorReference &reference : references) emit anchorDeleted(reference.resourceId, reference.anchor.id);
    }
    return result.success;
}

bool AnchorLibraryWindow::restoreSelectedAnchors()
{
    const QList<AnchorLibraryAnchor> entries = selectedAnchors();
    if (entries.isEmpty() || !showingTrash() || !options_.managementService) {
        statusText_ = tr("Select deleted anchors in Trash to restore");
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    const AnchorLibraryOperationResult result = options_.managementService->setAnchorsDeleted(selectedAnchorReferences(), false);
    setOperationResult(result);
    if (result.success) {
        for (const AnchorLibraryAnchor &entry : entries) emit anchorRestored(entry.resourceId, entry.anchor.id);
    }
    return result.success;
}

bool AnchorLibraryWindow::restoreAllAnchorsForSelectedFiles()
{
    if (!options_.managementService || !showingTrash()) return false;
    const QList<AnchorReference> references = allAnchorReferencesForSelectedFiles(true);
    if (references.isEmpty()) {
        statusText_ = tr("The selected files have no anchors in Trash");
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    const AnchorLibraryOperationResult result = options_.managementService->setAnchorsDeleted(references, false);
    setOperationResult(result);
    if (result.success) {
        for (const AnchorReference &reference : references) emit anchorRestored(reference.resourceId, reference.anchor.id);
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
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
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
    return permanentlyDeleteSelectedAnchors();
}

bool AnchorLibraryWindow::permanentlyDeleteSelectedAnchors()
{
    if (!options_.managementService || !showingTrash()) return false;
    const QList<AnchorReference> references = selectedAnchorReferences();
    if (references.isEmpty()) {
        statusText_ = tr("Select anchors in Trash to delete permanently");
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    if (!confirmOperation(tr("Permanently Delete Anchors"),
                          tr("Permanently delete %1 selected anchor(s)?\n\nThis cannot be undone.")
                              .arg(references.size()))) {
        statusText_ = tr("Permanent deletion canceled");
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    if (!createSafetyBackup(QStringLiteral("permanent anchor deletion"))) return false;
    const AnchorLibraryOperationResult result = options_.managementService->permanentlyDeleteAnchors(references);
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::permanentlyClearSelectedFileMetadata()
{
    if (!options_.managementService || !showingTrash()) return false;
    QStringList ids;
    for (const AnchorLibraryFile *file : selectedFiles()) {
        if (!file->resource.deleted) continue;
        for (const QString &id : resourceIdsForFile(*file)) appendUnique(ids, id);
    }
    if (ids.isEmpty()) {
        statusText_ = tr("Select file Alias and Tag metadata in Trash");
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    if (!confirmOperation(tr("Permanently Delete File Metadata"),
                          tr("Permanently delete Alias and Tag metadata for %1 file(s)?\n\nThe files and anchors will not be deleted.")
                              .arg(ids.size()))) {
        statusText_ = tr("Permanent deletion canceled");
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    if (!createSafetyBackup(QStringLiteral("permanent file metadata deletion"))) return false;
    const AnchorLibraryOperationResult result = options_.managementService->permanentlyClearResourceMetadata(ids);
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::clearSelectedAnchorlessFileMetadata()
{
    if (!options_.managementService || showingTrash()) return false;
    const auto files = selectedFiles();
    if (files.isEmpty() || !allAnchorReferencesForSelectedFiles(false).isEmpty()) {
        statusText_ = tr("Select only files without active anchors to clear Tags and Aliases");
        Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    const QStringList ids = selectedResourceIds();
    QStringList keys;
    for (const auto *file : files) keys.append(fileGroupingKey(file->resource));
    if (!confirmOperation(tr("Clear File Tags and Aliases"),
                          tr("Clear all Tags and Aliases for %1 selected file(s) without active anchors?\n\n"
                             "Source files and anchors in Trash will not be changed. This operation can be undone.")
                              .arg(files.size()))) {
        statusText_ = tr("Clear canceled");
        Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    const auto result = options_.managementService->clearAnchorlessResourceMetadata(ids);
    if (result.success) {
        for (const QString &key : keys) {
            pendingFileInlineEdits_.remove(key);
            fileInlineCellStates_.remove(inlineFileCellKey(key, FileAliasesColumn));
            fileInlineCellStates_.remove(inlineFileCellKey(key, FileTagsColumn));
        }
    }
    setOperationResult(result);
    return result.success;
}

bool AnchorLibraryWindow::permanentlyDeleteAllAnchorsForSelectedFiles()
{
    if (!options_.managementService || !showingTrash()) return false;
    const QList<AnchorReference> references = allAnchorReferencesForSelectedFiles(true);
    if (references.isEmpty()) {
        statusText_ = tr("The selected files have no anchors in Trash");
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    if (!confirmOperation(tr("Permanently Delete All Anchors"),
                          tr("Permanently delete all %1 anchors in the selected files?\n\nThis cannot be undone.")
                              .arg(references.size()))) {
        statusText_ = tr("Permanent deletion canceled");
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    if (!createSafetyBackup(QStringLiteral("permanent anchor deletion"))) return false;
    const AnchorLibraryOperationResult result = options_.managementService->permanentlyDeleteAnchors(references);
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
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
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
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
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

bool AnchorLibraryWindow::previewSelectedAnchor()
{
    return renderSelectedAnchorPreview(true, true);
}

bool AnchorLibraryWindow::renderSelectedAnchorPreview(bool showExpanded, bool forceRender)
{
    ++locatorPreviewRequestGeneration_;
    const AnchorLibraryFile *file = selectedFile();
    const auto entry = selectedAnchor();
    if (!file || !entry) {
        cancelPdfLocatorPreview();
        return false;
    }

    const Anchor &anchor = entry->anchor;
    const bool directPdfPreview = isDirectPdfPreview(file->resource, anchor);
    const std::optional<PdfLocatorPreviewRenderOptions> pdfOptions =
        directPdfPreview && options_.pdfPreviewOptionsProvider
        ? std::optional<PdfLocatorPreviewRenderOptions>(options_.pdfPreviewOptionsProvider())
        : std::nullopt;
    const QString cacheKey = locatorPreviewCacheKey(*file, anchor, pdfOptions ? &*pdfOptions : nullptr);
    if (!forceRender) {
        const LocatorPreviewCacheEntry *cached = locatorPreviewMemoryCache_.object(cacheKey);
        if (cached) {
            cancelPdfLocatorPreview();
            locatorPreview_->setScreenshot(cached->image);
            if (showExpanded) locatorPreview_->showExpandedPreview();
            statusText_ = cached->status;
            Pinloom::Ui::setStatusText(statusLabel_, statusText_, showExpanded);
            return true;
        }
    }

    if (pdfOptions) {
        return startPdfLocatorPreview(*file, entry.value(), cacheKey, *pdfOptions, showExpanded);
    }

    cancelPdfLocatorPreview();
    QString status;
    QPixmap screenshot;
    if (options_.locatorPreviewHandler) {
        const bool restoreWindow = isVisible() && !directPdfPreview;
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
    } else {
        status = tr("Preview is not configured");
    }
    applyLocatorPreviewResult(cacheKey, screenshot, status, showExpanded);
    return !screenshot.isNull();
}

bool AnchorLibraryWindow::startPdfLocatorPreview(const AnchorLibraryFile &file,
                                                  const AnchorLibraryAnchor &anchor,
                                                  const QString &cacheKey,
                                                  const PdfLocatorPreviewRenderOptions &renderOptions,
                                                  bool showExpanded)
{
    for (const auto &request : {activePdfPreview_, pendingPdfPreview_}) {
        if (request && request->cacheKey == cacheKey
            && !request->cancellation->load(std::memory_order_relaxed)) {
            request->generation = locatorPreviewRequestGeneration_;
            request->showExpanded = request->showExpanded || showExpanded;
            return true;
        }
    }
    cancelPdfLocatorPreview();
    auto request = std::make_shared<PdfPreviewRequest>();
    request->resource = file.resource;
    request->anchor = anchor.anchor;
    request->options = renderOptions;
    request->cacheKey = cacheKey;
    request->generation = locatorPreviewRequestGeneration_;
    request->showExpanded = showExpanded;

    statusText_ = tr("Rendering PDF preview...");
    Pinloom::Ui::setStatusText(statusLabel_, statusText_, showExpanded);
    if (activePdfPreview_) {
        pendingPdfPreview_ = std::move(request);
    } else {
        launchPdfLocatorPreview(request);
    }
    return true;
}

void AnchorLibraryWindow::cancelPdfLocatorPreview()
{
    if (activePdfPreview_) activePdfPreview_->cancellation->store(true, std::memory_order_relaxed);
    pendingPdfPreview_.reset();
}

void AnchorLibraryWindow::launchPdfLocatorPreview(const std::shared_ptr<PdfPreviewRequest> &request)
{
    activePdfPreview_ = request;
    // Only value-owned render inputs cross the worker boundary. UI intent can be
    // merged while rendering without racing with worker reads.
    const Resource resource = request->resource;
    const Anchor locator = request->anchor;
    const PdfLocatorPreviewRenderOptions renderOptions = request->options;
    const PdfLocatorPreviewCancellation cancellation = request->cancellation;

    auto *watcher = new QFutureWatcher<PdfLocatorPreviewRenderResult>(this);
    connect(watcher,
            &QFutureWatcher<PdfLocatorPreviewRenderResult>::finished,
            this,
            [this, watcher, request]() {
                const PdfLocatorPreviewRenderResult result = watcher->result();
                watcher->deleteLater();
                activePdfPreview_.reset();
                if (!result.cancelled && !request->cancellation->load(std::memory_order_relaxed)
                    && request->generation == locatorPreviewRequestGeneration_) {
                    const QString status = result.success()
                        ? (result.cropped
                               ? tr("Showing page %1 anchor region").arg(result.page)
                               : tr("Showing rendered PDF page %1").arg(result.page))
                        : result.error;
                    applyLocatorPreviewResult(request->cacheKey,
                                              result.success() ? QPixmap::fromImage(result.image) : QPixmap{},
                                              status,
                                              request->showExpanded);
                }
                auto pending = std::exchange(pendingPdfPreview_, {});
                if (pending && pending->generation == locatorPreviewRequestGeneration_) {
                    launchPdfLocatorPreview(pending);
                }
            });
    watcher->setFuture(QtConcurrent::run([resource, locator, renderOptions, cancellation]() {
        return renderPdfLocatorPreview(resource, locator, renderOptions, cancellation);
    }));
}

void AnchorLibraryWindow::applyLocatorPreviewResult(const QString &cacheKey,
                                                     const QPixmap &screenshot,
                                                     const QString &status,
                                                     bool showExpanded)
{
    statusText_ = status.trimmed().isEmpty()
        ? (screenshot.isNull() ? tr("Preview could not be generated") : tr("Captured application preview"))
        : status.trimmed();
    if (!screenshot.isNull()) {
        const qint64 imageBytes = static_cast<qint64>(screenshot.width()) * screenshot.height() * 4;
        const int cacheCost = static_cast<int>(std::clamp<qint64>(
            (imageBytes + 1023) / 1024, 1, INT_MAX));
        locatorPreviewMemoryCache_.insert(
            cacheKey, new LocatorPreviewCacheEntry{screenshot, statusText_}, cacheCost);
        locatorPreview_->setScreenshot(screenshot);
        if (showExpanded) locatorPreview_->showExpandedPreview();
    } else {
        locatorPreview_->setError(statusText_);
    }
    Pinloom::Ui::setStatusText(statusLabel_, statusText_, showExpanded || screenshot.isNull());
}

void AnchorLibraryWindow::scheduleSelectedAnchorPreview()
{
    const quint64 requestGeneration = ++locatorPreviewRequestGeneration_;
    if (!isVisible()) {
        cancelPdfLocatorPreview();
        return;
    }
    const AnchorLibraryFile *file = selectedFile();
    const auto entry = selectedAnchor();
    if (!file || !entry) {
        cancelPdfLocatorPreview();
        return;
    }
    const std::optional<PdfLocatorPreviewRenderOptions> pdfOptions =
        isDirectPdfPreview(file->resource, entry->anchor) && options_.pdfPreviewOptionsProvider
        ? std::optional<PdfLocatorPreviewRenderOptions>(options_.pdfPreviewOptionsProvider())
        : std::nullopt;
    const QString cacheKey = locatorPreviewCacheKey(*file, entry->anchor, pdfOptions ? &*pdfOptions : nullptr);
    for (const auto &request : {activePdfPreview_, pendingPdfPreview_}) {
        if (request && request->cacheKey == cacheKey
            && !request->cancellation->load(std::memory_order_relaxed)) {
            request->generation = requestGeneration;
            return;
        }
    }
    cancelPdfLocatorPreview();
    if (locatorPreviewMemoryCache_.contains(cacheKey)) {
        renderSelectedAnchorPreview(false, false);
        return;
    }
    QTimer::singleShot(120, this, [this, requestGeneration]() {
        if (requestGeneration != locatorPreviewRequestGeneration_) return;
        const AnchorLibraryFile *file = selectedFile();
        const auto entry = selectedAnchor();
        if (!file || !entry) return;
        if (isDirectPdfPreview(file->resource, entry->anchor)) renderSelectedAnchorPreview(false, false);
    });
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
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
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
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
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
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
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
    Pinloom::Ui::setStatusText(statusLabel_, statusText_);
    if (isVisible()) {
        Pinloom::Ui::Dialog dialog(this);
        dialog.setWindowTitle(tr("Anchor Library Integrity"));
        dialog.resize(820, 420);
        auto *dialogLayout = new QVBoxLayout(&dialog);
        auto *summary = Pinloom::Ui::label(statusText_, &dialog);
        auto *table = new Pinloom::Ui::Table(report.issues.size(), 3, &dialog);
        table->setHorizontalHeaderLabels({tr("Issue"), tr("Item"), tr("Detail")});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        for (int row = 0; row < report.issues.size(); ++row) {
            const auto &issue = report.issues.at(row);
            table->setItem(row, 0, new Pinloom::Ui::TableItem(issueKindLabel(issue.kind)));
            table->setItem(row, 1, new Pinloom::Ui::TableItem(issue.title));
            table->setItem(row, 2, new Pinloom::Ui::TableItem(issue.detail));
        }
        auto *buttons = new Pinloom::Ui::DialogButtonBox(QDialogButtonBox::Close, &dialog);
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
    Pinloom::Ui::setStatusText(statusLabel_, statusText_);
    if (!isVisible()) return !history.isEmpty();

    Pinloom::Ui::Dialog dialog(this);
    dialog.setWindowTitle(tr("Anchor Library Operation History"));
    dialog.resize(700, 380);
    auto *layout = new QVBoxLayout(&dialog);
    auto *table = new Pinloom::Ui::Table(history.size(), 4, &dialog);
    table->setObjectName(QStringLiteral("anchorLibraryHistoryTable"));
    table->setHorizontalHeaderLabels({tr("Operation"), tr("Affected"), tr("Time"), tr("State")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column : {1, 2, 3}) table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    for (int row = 0; row < history.size(); ++row) {
        const AnchorLibraryHistoryItem &item = history.at(row);
        table->setItem(row, 0, new Pinloom::Ui::TableItem(item.action));
        table->setItem(row, 1, new Pinloom::Ui::TableItem(QString::number(item.affectedCount)));
        table->setItem(row, 2, new Pinloom::Ui::TableItem(QLocale().toString(item.timestamp.toLocalTime(), QLocale::ShortFormat)));
        table->setItem(row, 3, new Pinloom::Ui::TableItem(item.undone ? tr("Undone") : tr("Applied")));
    }
    auto *buttons = new Pinloom::Ui::DialogButtonBox(QDialogButtonBox::Close, &dialog);
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

bool AnchorLibraryWindow::saveCurrentView(const QString &name)
{
    if (!options_.settings || name.trimmed().isEmpty()) return false;
    const QString key = viewSettingsKey(name);
    options_.settings->beginGroup(key);
    options_.settings->setValue(QStringLiteral("name"), name.trimmed());
    options_.settings->setValue(QStringLiteral("query"), filterEdit_->text());
    options_.settings->setValue(QStringLiteral("scope"), scopeCombo_->currentData());
    options_.settings->setValue(QStringLiteral("fileTag"), tagFilterCombo_->currentData());
    options_.settings->setValue(QStringLiteral("anchorTag"), anchorTagFilterCombo_->currentData());
    options_.settings->remove(QStringLiteral("tag"));
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
    Pinloom::Ui::setStatusText(statusLabel_, statusText_);
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
    const QVariant legacyTag = options_.settings->value(QStringLiteral("tag"));
    const QVariant fileTag = options_.settings->value(QStringLiteral("fileTag"), legacyTag);
    const QVariant anchorTag = options_.settings->value(QStringLiteral("anchorTag"));
    const QVariant kind = options_.settings->value(QStringLiteral("kind"), -1);
    const QVariant app = options_.settings->value(QStringLiteral("app"));
    const QString directory = options_.settings->value(QStringLiteral("directory")).toString();
    const QVariant days = options_.settings->value(QStringLiteral("days"), 0);
    const QVariant usage = options_.settings->value(QStringLiteral("usage"), 0);
    options_.settings->endGroup();
    const QSignalBlocker queryBlocker(filterEdit_);
    const QSignalBlocker scopeBlocker(scopeCombo_);
    const QSignalBlocker tagBlocker(tagFilterCombo_);
    const QSignalBlocker anchorTagBlocker(anchorTagFilterCombo_);
    const QSignalBlocker kindBlocker(kindFilterCombo_);
    const QSignalBlocker appBlocker(appFilterCombo_);
    const QSignalBlocker directoryBlocker(directoryFilterEdit_);
    const QSignalBlocker timeBlocker(timeFilterCombo_);
    const QSignalBlocker usageBlocker(usageFilterCombo_);
    filterEdit_->setText(query);
    scopeCombo_->setCurrentIndex(std::max(0, scopeCombo_->findData(scope)));
    tagFilterCombo_->setCurrentIndex(std::max(0, tagFilterCombo_->findData(fileTag)));
    anchorTagFilterCombo_->setCurrentIndex(std::max(0, anchorTagFilterCombo_->findData(anchorTag)));
    kindFilterCombo_->setCurrentIndex(std::max(0, kindFilterCombo_->findData(kind)));
    appFilterCombo_->setCurrentIndex(std::max(0, appFilterCombo_->findData(app)));
    directoryFilterEdit_->setText(directory);
    timeFilterCombo_->setCurrentIndex(std::max(0, timeFilterCombo_->findData(days)));
    usageFilterCombo_->setCurrentIndex(std::max(0, usageFilterCombo_->findData(usage)));
    applyFilter();
    statusText_ = tr("Loaded view: %1").arg(name);
    Pinloom::Ui::setStatusText(statusLabel_, statusText_);
    return true;
}

bool AnchorLibraryWindow::deleteSavedView(const QString &name)
{
    if (!options_.settings || name.trimmed().isEmpty()) return false;
    options_.settings->remove(viewSettingsKey(name));
    options_.settings->sync();
    refreshSavedViews();
    statusText_ = tr("Deleted saved view: %1").arg(name.trimmed());
    Pinloom::Ui::setStatusText(statusLabel_, statusText_);
    return true;
}

void AnchorLibraryWindow::applyFilter()
{
    QStringList selectedKeys;
    if (fileTable_->selectionModel()) {
        for (const QModelIndex &index : fileTable_->selectionModel()->selectedRows(0)) {
            appendUnique(selectedKeys, fileTable_->item(index.row(), FileNameColumn)->data(FileKeyRole).toString());
        }
    }
    const auto *current = fileTable_->item(fileTable_->currentRow(), FileNameColumn);
    const QString currentKey = current ? current->data(FileKeyRole).toString() : QString();
    const int currentColumn = qMax(0, fileTable_->currentColumn());
    const int verticalScroll = fileTable_->verticalScrollBar()->value();
    const int horizontalScroll = fileTable_->horizontalScrollBar()->value();
    populatingFileTable_ = true;
    const QSignalBlocker tableSignals(fileTable_);
    const QSignalBlocker selectionSignals(fileTable_->selectionModel());
    fileTable_->setUpdatesEnabled(false);
    const QList<FileViewData> visible = sortedVisibleFiles();
    fileTable_->setRowCount(visible.size());
    for (int row = 0; row < visible.size(); ++row) {
        const FileViewData &view = visible.at(row);
        const AnchorLibraryFile *file = view.file;
        const QString &key = view.key;
        const auto pending = pendingFileInlineEdits_.constFind(key);
        const QString aliases = pending != pendingFileInlineEdits_.constEnd() && pending->aliasesDirty
            ? pending->aliases.join(QLatin1Char(','))
            : view.aliases;
        const QStringList tags = pending != pendingFileInlineEdits_.constEnd() && pending->tagsDirty
            ? pending->tags
            : file->resource.tags;
        auto *name = fileTable_->updateItem(row, FileNameColumn, view.name());
        name->setData(FileKeyRole, key);
        name->setToolTip(file->resource.location);
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        const QVariant foreground = file->resource.deleted
            ? QVariant(QBrush(palette().color(QPalette::Disabled, QPalette::Text)))
            : QVariant();
        if (name->data(Qt::ForegroundRole) != foreground) name->setData(Qt::ForegroundRole, foreground);
        auto *aliasesItem = fileTable_->updateItem(row, FileAliasesColumn, aliases);
        aliasesItem->setFlags(showingTrash() ? aliasesItem->flags() & ~Qt::ItemIsEditable
                                            : aliasesItem->flags() | Qt::ItemIsEditable);
        auto *location = fileTable_->updateItem(row, FileLocationColumn, fileLocationLabel(*file));
        location->setToolTip(file->resource.location);
        location->setFlags(location->flags() & ~Qt::ItemIsEditable);
        auto *type = fileTable_->updateItem(row, FileTypeColumn, view.type);
        type->setFlags(type->flags() & ~Qt::ItemIsEditable);
        auto *count = fileTable_->updateItem(row, FileAnchorCountColumn, view.anchors.size());
        count->setFlags(count->flags() & ~Qt::ItemIsEditable);
        auto *tagsItem = fileTable_->updateItem(row, FileTagsColumn, tags.join(QStringLiteral(", ")));
        tagsItem->setData(TagValuesRole, tags);
        tagsItem->setToolTip(tags.join(QStringLiteral(", ")));
        tagsItem->setFlags(tagsItem->flags() & ~Qt::ItemIsEditable);
        const QDateTime &marked = view.markedAt();
        auto *markedItem = fileTable_->updateItem(row, FileLastMarkedColumn, marked.isValid() ? QLocale().toString(marked.toLocalTime(), QLocale::ShortFormat) : QString());
        markedItem->setData(SortValueRole, marked);
        markedItem->setFlags(markedItem->flags() & ~Qt::ItemIsEditable);
        QStringList states;
        const bool targetExists = view.targetExists();
        if (file->resource.deleted) states.append(tr("Archived"));
        else states.append(targetExists ? tr("Ready") : tr("Missing"));
        const int records = resourceIdsForFile(*file).size();
        if (records > 1) states.append(tr("%1 records").arg(records));
        if (targetExists && fileHasInvalidLocator(view)) {
            states.append(tr("Invalid locator"));
        }
        auto *status = fileTable_->updateItem(row, FileStatusColumn, states.join(QStringLiteral(" | ")));
        status->setFlags(status->flags() & ~Qt::ItemIsEditable);
        auto *opens = fileTable_->updateItem(row, FileOpenCountColumn, view.openCount());
        opens->setFlags(opens->flags() & ~Qt::ItemIsEditable);
        const QDateTime &opened = view.openedAt();
        auto *openedItem = fileTable_->updateItem(row, FileLastOpenedColumn, opened.isValid() ? QLocale().toString(opened.toLocalTime(), QLocale::ShortFormat) : QString());
        openedItem->setData(SortValueRole, opened);
        openedItem->setFlags(openedItem->flags() & ~Qt::ItemIsEditable);
        applyFileInlineCellState(row, FileAliasesColumn, key);
        applyFileInlineCellState(row, FileTagsColumn, key);
    }
    populatingFileTable_ = false;
    QItemSelection retainedSelection;
    int retainedCurrentRow = -1;
    int firstSelectedRow = -1;
    for (int row = 0; row < fileTable_->rowCount(); ++row) {
        const QString key = fileTable_->item(row, FileNameColumn)->data(FileKeyRole).toString();
        if (key == currentKey && retainedCurrentRow < 0) retainedCurrentRow = row;
        if (selectedKeys.contains(key)) {
            retainedSelection.select(fileTable_->model()->index(row, 0),
                                     fileTable_->model()->index(row, fileTable_->columnCount() - 1));
            if (firstSelectedRow < 0) firstSelectedRow = row;
        }
    }
    fileTable_->selectionModel()->select(retainedSelection, QItemSelectionModel::ClearAndSelect);
    if (retainedCurrentRow >= 0 || firstSelectedRow >= 0) {
        fileTable_->setCurrentCell(retainedCurrentRow >= 0 ? retainedCurrentRow : firstSelectedRow,
                                  currentColumn, QItemSelectionModel::NoUpdate);
    } else if (fileTable_->rowCount() > 0) {
        fileTable_->selectRow(0);
    } else {
        fileTable_->selectionModel()->clearCurrentIndex();
    }
    if (fileTable_->rowCount() == 0) {
        anchorTable_->setRowCount(0);
        populateInspector();
    } else {
        populateSelectedFileAnchors();
    }
    fileTable_->verticalHeader()->setDefaultSectionSize(qMax(34, fileTable_->fontMetrics().height() + 16));
    fileTable_->verticalScrollBar()->setValue(verticalScroll);
    fileTable_->horizontalScrollBar()->setValue(horizontalScroll);
    fileTable_->setUpdatesEnabled(true);
    updateStatus();
}

void AnchorLibraryWindow::populateSelectedFileAnchors()
{
    populatingAnchorTable_ = true;
    const QSignalBlocker tableSignals(anchorTable_);
    const QSignalBlocker selectionSignals(anchorTable_->selectionModel());
    anchorTable_->setUpdatesEnabled(false);
    QStringList selectedIds;
    if (anchorTable_->selectionModel()) {
        for (const QModelIndex &index : anchorTable_->selectionModel()->selectedRows(0)) {
            const auto *item = anchorTable_->item(index.row(), AnchorNameColumn);
            selectedIds.append(item->data(ResourceIdRole).toString() + QLatin1Char('|') + item->data(AnchorIdentityRole).toString());
        }
    }
    const auto *current = anchorTable_->item(anchorTable_->currentRow(), AnchorNameColumn);
    const QString currentKey = current
        ? current->data(ResourceIdRole).toString() + QLatin1Char('|') + current->data(AnchorIdentityRole).toString()
        : QString();
    const int currentColumn = qMax(0, anchorTable_->currentColumn());
    const int verticalScroll = anchorTable_->verticalScrollBar()->value();
    const int horizontalScroll = anchorTable_->horizontalScrollBar()->value();
    const AnchorLibraryFile *file = selectedFile();
    const QList<AnchorViewData> anchors = file ? sortedAnchors(scopedAnchors(*file)) : QList<AnchorViewData>{};
    anchorTable_->setRowCount(anchors.size());
    if (file) {
        for (int row = 0; row < anchors.size(); ++row) {
            const AnchorViewData &view = anchors.at(row);
            const AnchorLibraryAnchor &entry = view.entry;
            auto *name = anchorTable_->updateItem(row, AnchorNameColumn, view.name);
            name->setData(AnchorIdentityRole, view.identity);
            name->setData(ResourceIdRole, entry.resourceId);
            name->setFlags(name->flags() & ~Qt::ItemIsEditable);
            const QString key = inlineAnchorKey(entry.resourceId, view.identity);
            const auto pending = pendingInlineEdits_.constFind(key);
            const QString aliases = pending != pendingInlineEdits_.constEnd() && pending->aliasesDirty
                ? pending->aliases.join(QLatin1Char(','))
                : view.aliases;
            const QStringList tags = pending != pendingInlineEdits_.constEnd() && pending->tagsDirty
                ? pending->tags
                : entry.anchor.tags;
            auto *aliasesItem = anchorTable_->updateItem(row, AnchorAliasesColumn, aliases);
            aliasesItem->setData(AnchorIdentityRole, anchorIdentityKey(entry.anchor));
            aliasesItem->setData(ResourceIdRole, entry.resourceId);
            aliasesItem->setFlags(showingTrash() ? aliasesItem->flags() & ~Qt::ItemIsEditable
                                               : aliasesItem->flags() | Qt::ItemIsEditable);
            auto *tagsItem = anchorTable_->updateItem(row, AnchorTagsColumn, tags.join(QStringLiteral(", ")));
            tagsItem->setData(TagValuesRole, tags);
            tagsItem->setToolTip(tags.join(QStringLiteral(", ")));
            tagsItem->setFlags(tagsItem->flags() & ~Qt::ItemIsEditable);
            auto *typeItem = anchorTable_->updateItem(row, AnchorTypeColumn, view.type);
            typeItem->setToolTip(anchorLocatorSummary(entry.anchor));
            typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
            const QDateTime &updated = view.updated;
            auto *updatedItem = anchorTable_->updateItem(row, AnchorUpdatedColumn, updated.isValid() ? QLocale().toString(updated.toLocalTime(), QLocale::ShortFormat) : QString());
            updatedItem->setData(SortValueRole, updated);
            updatedItem->setFlags(updatedItem->flags() & ~Qt::ItemIsEditable);
            auto *opens = anchorTable_->updateItem(row, AnchorOpenCountColumn, entry.usage.openCount);
            opens->setFlags(opens->flags() & ~Qt::ItemIsEditable);
            auto *opened = anchorTable_->updateItem(row, AnchorLastOpenedColumn, entry.usage.lastOpenedAt.isValid() ? QLocale().toString(entry.usage.lastOpenedAt.toLocalTime(), QLocale::ShortFormat) : QString());
            opened->setData(SortValueRole, entry.usage.lastOpenedAt);
            opened->setFlags(opened->flags() & ~Qt::ItemIsEditable);
            QString validity = tr("Unknown");
            if (options_.managementService) {
                const AnchorValidationResult result = options_.managementService->validateAnchor(file->resource, entry.anchor);
                validity = result.valid ? tr("Valid") : tr("Invalid");
            }
            auto *validityItem = anchorTable_->updateItem(row, AnchorValidityColumn, validity);
            validityItem->setFlags(validityItem->flags() & ~Qt::ItemIsEditable);
            applyInlineCellState(row, AnchorAliasesColumn, key);
            applyInlineCellState(row, AnchorTagsColumn, key);
        }
    }
    QItemSelection retainedSelection;
    int retainedCurrentRow = -1;
    int firstSelectedRow = -1;
    for (int row = 0; row < anchorTable_->rowCount(); ++row) {
        const auto *item = anchorTable_->item(row, AnchorNameColumn);
        const QString key = item->data(ResourceIdRole).toString() + QLatin1Char('|') + item->data(AnchorIdentityRole).toString();
        if (key == currentKey && retainedCurrentRow < 0) retainedCurrentRow = row;
        if (selectedIds.contains(key)) {
            retainedSelection.select(anchorTable_->model()->index(row, 0),
                                     anchorTable_->model()->index(row, anchorTable_->columnCount() - 1));
            if (firstSelectedRow < 0) firstSelectedRow = row;
        }
    }
    anchorTable_->selectionModel()->select(retainedSelection, QItemSelectionModel::ClearAndSelect);
    if (retainedCurrentRow >= 0 || firstSelectedRow >= 0) {
        anchorTable_->setCurrentCell(retainedCurrentRow >= 0 ? retainedCurrentRow : firstSelectedRow,
                                    currentColumn, QItemSelectionModel::NoUpdate);
    } else if (anchorTable_->rowCount() > 0) {
        anchorTable_->selectRow(0);
    } else {
        anchorTable_->selectionModel()->clearCurrentIndex();
    }
    populatingAnchorTable_ = false;
    anchorTable_->verticalHeader()->setDefaultSectionSize(qMax(34, anchorTable_->fontMetrics().height() + 16));
    anchorTable_->verticalScrollBar()->setValue(verticalScroll);
    anchorTable_->horizontalScrollBar()->setValue(horizontalScroll);
    anchorTable_->setUpdatesEnabled(true);
    populateInspector();
    updateActionButtons();
    updateStatus();
}

void AnchorLibraryWindow::populateInspector()
{
    const AnchorLibraryFile *file = selectedFile();
    const QList<AnchorLibraryAnchor> entries = selectedAnchors();
    const bool oneAnchor = entries.size() == 1;
    inspectorTitle_->setText(oneAnchor ? anchorDisplayName(entries.first().anchor)
                                     : file ? fileDisplayName(*file) : tr("Select an anchor"));
    inspectorLocation_->setText(file ? fileLocationLabel(*file) : QString());
    inspectorLocation_->setToolTip(inspectorLocation_->text());
    if (oneAnchor) {
        const Anchor &anchor = entries.first().anchor;
        if (file) {
            locatorPreview_->setLocator(file->resource, anchor);
            scheduleSelectedAnchorPreview();
        }
    } else {
        ++locatorPreviewRequestGeneration_;
        cancelPdfLocatorPreview();
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
    if (row < 0 || !fileTable_->item(row, FileNameColumn)) return nullptr;
    return fileForKey(fileTable_->item(row, FileNameColumn)->data(FileKeyRole).toString());
}

QList<const AnchorLibraryFile *> AnchorLibraryWindow::selectedFiles() const
{
    QList<const AnchorLibraryFile *> selected;
    if (!fileTable_->selectionModel()) return selected;
    for (const QModelIndex &index : fileTable_->selectionModel()->selectedRows(FileNameColumn)) {
        const AnchorLibraryFile *file = fileForKey(fileTable_->item(index.row(), FileNameColumn)->data(FileKeyRole).toString());
        if (file && !selected.contains(file)) selected.append(file);
    }
    return selected;
}

std::optional<AnchorLibraryAnchor> AnchorLibraryWindow::selectedAnchor() const
{
    const AnchorLibraryFile *file = selectedFile();
    const int row = anchorTable_->currentRow();
    if (!file || row < 0 || !anchorTable_->item(row, AnchorNameColumn)) return std::nullopt;
    const QString identity = anchorTable_->item(row, AnchorNameColumn)->data(AnchorIdentityRole).toString();
    const QString resourceId = anchorTable_->item(row, AnchorNameColumn)->data(ResourceIdRole).toString();
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
    for (const QModelIndex &index : anchorTable_->selectionModel()->selectedRows(AnchorNameColumn)) {
        const auto *item = anchorTable_->item(index.row(), AnchorNameColumn);
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
    if (!trash && file.resource.deleted) return anchors;
    for (const AnchorLibraryAnchor &entry : file.anchors) {
        if (isValidAnchorLibraryAnchor(entry.anchor)
            && entry.anchor.deleted == trash) {
            anchors.append(entry);
        }
    }
    return anchors;
}

QStringList AnchorLibraryWindow::resourceIdsForFile(const AnchorLibraryFile &file) const
{
    QStringList ids = fileResourceIds_.value(fileGroupingKey(file.resource));
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

bool AnchorLibraryWindow::fileMatchesFilter(const FileViewData &view) const
{
    const AnchorLibraryFile &file = *view.file;
    const QList<AnchorLibraryAnchor> &anchors = view.anchors;
    const auto scope = static_cast<AnchorLibraryScope>(scopeCombo_->currentData().toInt());
    if (!showingTrash() && file.resource.deleted) return false;
    const bool hasFileMarker = hasAnchorLibraryUserMarker(file.resource, file.usage);
    if (!showingTrash() && anchors.isEmpty() && !hasFileMarker) return false;
    if (showingTrash() && anchors.isEmpty() && !file.resource.deleted) return false;
    if (scope == AnchorLibraryScope::Untagged) {
        const bool hasAnchorTag = std::any_of(anchors.cbegin(), anchors.cend(), [](const AnchorLibraryAnchor &entry) {
            return !entry.anchor.tags.isEmpty();
        });
        if (!file.resource.tags.isEmpty() || hasAnchorTag) return false;
    }
    if (scope == AnchorLibraryScope::Missing && view.targetExists()) return false;
    if (scope == AnchorLibraryScope::Duplicates && resourceIdsForFile(file).size() < 2) return false;
    if (scope == AnchorLibraryScope::InvalidLocator && !fileHasInvalidLocator(view)) return false;
    const QDateTime &modified = view.markedAt();
    if ((scope == AnchorLibraryScope::RecentlyModified || scope == AnchorLibraryScope::RecentlyDeleted)
        && (!modified.isValid() || modified < QDateTime::currentDateTimeUtc().addDays(-30))) return false;

    const QString selectedFileTag = tagFilterCombo_->currentData().toString();
    if (!selectedFileTag.isEmpty()
        && !file.resource.tags.contains(selectedFileTag, Qt::CaseInsensitive)) return false;
    const QString selectedAnchorTag = anchorTagFilterCombo_->currentData().toString();
    if (!selectedAnchorTag.isEmpty()) {
        const bool matched = std::any_of(anchors.cbegin(), anchors.cend(), [&](const AnchorLibraryAnchor &entry) {
            return entry.anchor.tags.contains(selectedAnchorTag, Qt::CaseInsensitive);
        });
        if (!matched) return false;
    }
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
        const QDateTime &opened = view.openedAt();
        if (!opened.isValid() || opened < QDateTime::currentDateTimeUtc().addDays(-30)) return false;
    }
    if (usage == UsageFilter::NeverOpened && view.openCount() > 0) return false;

    const QString needle = filterEdit_->text().trimmed();
    if (needle.isEmpty()) return true;
    if (containsText(view.name(), needle)
        || containsText(file.resource.location, needle)
        || containsText(file.resource.aliases.join(QLatin1Char('\n')), needle)
        || containsText(file.resource.tags.join(QLatin1Char('\n')), needle)) return true;
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

bool AnchorLibraryWindow::fileHasInvalidLocator(const FileViewData &view) const
{
    if (view.invalidLocator.has_value()) return *view.invalidLocator;
    view.invalidLocator = false;
    if (options_.managementService) {
        for (const AnchorLibraryAnchor &entry : view.anchors) {
            if (!options_.managementService->validateAnchor(view.file->resource, entry.anchor).valid) {
                view.invalidLocator = true;
                break;
            }
        }
    }
    return *view.invalidLocator;
}

bool AnchorLibraryWindow::confirmOperation(const QString &title, const QString &message)
{
    if (options_.confirmationHandler) return options_.confirmationHandler(title, message);
    return AnchorLibraryUi::confirm(this, title, message);
}

bool AnchorLibraryWindow::createSafetyBackup(const QString &operation)
{
    if (!options_.archiveService || options_.automaticBackupDirectory.trimmed().isEmpty()) return true;
    const AnchorLibraryOperationResult result = options_.archiveService->createAutomaticBackup(options_.automaticBackupDirectory, 10);
    if (result.success) return true;
    statusText_ = tr("Canceled %1 because the safety backup failed: %2").arg(operation, result.message);
    Pinloom::Ui::setStatusText(statusLabel_, statusText_);
    return false;
}

QList<AnchorReference> AnchorLibraryWindow::allAnchorReferencesForSelectedFiles(bool deletedOnly) const
{
    QList<AnchorReference> references;
    QStringList keys;
    for (const AnchorLibraryFile *file : selectedFiles()) {
        for (const AnchorLibraryAnchor &entry : file->anchors) {
            if (entry.anchor.deleted != deletedOnly) continue;
            const QString key = inlineAnchorKey(entry.resourceId, anchorIdentityKey(entry.anchor));
            if (keys.contains(key)) continue;
            keys.append(key);
            references.append({entry.resourceId, entry.anchor});
        }
    }
    return references;
}

const AnchorLibraryAnchor *AnchorLibraryWindow::anchorForInlineKey(const QString &key) const
{
    for (const AnchorLibraryFile &file : files_) {
        for (const AnchorLibraryAnchor &entry : file.anchors) {
            if (inlineAnchorKey(entry.resourceId, anchorIdentityKey(entry.anchor)) == key) return &entry;
        }
    }
    return nullptr;
}

QStringList AnchorLibraryWindow::availableFileTags() const
{
    QStringList tags;
    for (const AnchorLibraryFile &file : files_) {
        for (const QString &tag : file.resource.tags) appendUnique(tags, tag);
    }
    for (const InlineFileEdit &edit : pendingFileInlineEdits_) {
        for (const QString &tag : edit.tags) appendUnique(tags, tag);
    }
    tags.sort(Qt::CaseInsensitive);
    return tags;
}

QStringList AnchorLibraryWindow::availableAnchorTags() const
{
    QStringList tags;
    for (const AnchorLibraryFile &file : files_) {
        for (const AnchorLibraryAnchor &entry : file.anchors) {
            for (const QString &tag : entry.anchor.tags) appendUnique(tags, tag);
        }
    }
    for (const InlineAnchorEdit &edit : pendingInlineEdits_) {
        for (const QString &tag : edit.tags) appendUnique(tags, tag);
    }
    tags.sort(Qt::CaseInsensitive);
    return tags;
}

QColor AnchorLibraryWindow::colorForTag(const QString &tag)
{
    const QString key = tag.trimmed().toCaseFolded();
    const auto existing = tagColors_.constFind(key);
    if (existing != tagColors_.constEnd()) return existing.value();
    auto isDuplicate = [this](const QColor &candidate) {
        for (const QColor &used : tagColors_) {
            if (used.rgba() == candidate.rgba()) return true;
        }
        return false;
    };
    QColor color;
    for (int attempt = 0; attempt < 32; ++attempt) {
        color = QColor::fromHsl(QRandomGenerator::global()->bounded(360),
                                125 + QRandomGenerator::global()->bounded(75),
                                125 + QRandomGenerator::global()->bounded(55));
        if (!isDuplicate(color)) break;
    }
    int fallbackHue = (tagColors_.size() * 137) % 360;
    while (!color.isValid() || isDuplicate(color)) {
        color = QColor::fromHsl(fallbackHue, 165, 145);
        fallbackHue = (fallbackHue + 1) % 360;
    }
    tagColors_.insert(key, color);
    return color;
}

void AnchorLibraryWindow::applyFileInlineCellState(int row, int column, const QString &key)
{
    Pinloom::Ui::TableItem *item = fileTable_->item(row, column);
    if (!item) return;
    const int state = fileInlineCellStates_.value(inlineFileCellKey(key, column), InlineCellClean);
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

void AnchorLibraryWindow::applyInlineCellState(int row, int column, const QString &key)
{
    Pinloom::Ui::TableItem *item = anchorTable_->item(row, column);
    if (!item) return;
    const int state = inlineCellStates_.value(inlineCellKey(key, column), InlineCellClean);
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

void AnchorLibraryWindow::updateInlineEditStatus()
{
    const int fileCount = pendingFileInlineEdits_.size();
    const int anchorCount = pendingInlineEdits_.size();
    statusText_ = fileCount == 0 && anchorCount == 0
        ? tr("No unsaved inline edits")
        : tr("%1 file(s) and %2 anchor(s) have unsaved Alias or Tag changes; press Ctrl+S to save")
              .arg(fileCount)
              .arg(anchorCount);
    Pinloom::Ui::setStatusText(statusLabel_, statusText_);
}

void AnchorLibraryWindow::handleFileItemChanged(Pinloom::Ui::TableItem *item)
{
    if (populatingFileTable_ || !item || item->column() != FileAliasesColumn || showingTrash()) return;
    const Pinloom::Ui::TableItem *name = fileTable_->item(item->row(), FileNameColumn);
    if (!name) return;
    const QString key = name->data(FileKeyRole).toString();
    const AnchorLibraryFile *file = fileForKey(key);
    if (!file) return;
    InlineFileEdit edit = pendingFileInlineEdits_.value(key);
    edit.fileKey = key;
    edit.aliases = editorValues(item->text(), true);
    edit.aliasesDirty = edit.aliases != file->resource.aliases;
    if (edit.tags.isEmpty() && !edit.tagsDirty) edit.tags = file->resource.tags;
    if (edit.aliasesDirty) {
        pendingFileInlineEdits_.insert(key, edit);
        fileInlineCellStates_.insert(inlineFileCellKey(key, FileAliasesColumn), InlineCellDirty);
    } else {
        edit.aliasesDirty = false;
        if (edit.tagsDirty) pendingFileInlineEdits_.insert(key, edit);
        else pendingFileInlineEdits_.remove(key);
        if (fileInlineCellStates_.value(inlineFileCellKey(key, FileAliasesColumn)) != InlineCellSaved) {
            fileInlineCellStates_.remove(inlineFileCellKey(key, FileAliasesColumn));
        }
    }
    applyFileInlineCellState(item->row(), FileAliasesColumn, key);
    updateInlineEditStatus();
}

void AnchorLibraryWindow::handleAnchorItemChanged(Pinloom::Ui::TableItem *item)
{
    if (populatingAnchorTable_ || !item || item->column() != AnchorAliasesColumn || showingTrash()) return;
    const Pinloom::Ui::TableItem *name = anchorTable_->item(item->row(), AnchorNameColumn);
    if (!name) return;
    const QString key = inlineAnchorKey(name->data(ResourceIdRole).toString(),
                                        name->data(AnchorIdentityRole).toString());
    const AnchorLibraryAnchor *entry = anchorForInlineKey(key);
    if (!entry) return;
    InlineAnchorEdit edit = pendingInlineEdits_.value(key);
    edit.resourceId = entry->resourceId;
    edit.anchorIdentity = anchorIdentityKey(entry->anchor);
    edit.aliases = editorValues(item->text(), true);
    edit.aliasesDirty = edit.aliases != entry->anchor.aliases;
    if (edit.tags.isEmpty() && !edit.tagsDirty) edit.tags = entry->anchor.tags;
    if (edit.aliasesDirty) {
        pendingInlineEdits_.insert(key, edit);
        inlineCellStates_.insert(inlineCellKey(key, AnchorAliasesColumn), InlineCellDirty);
    } else {
        edit.aliasesDirty = false;
        if (edit.tagsDirty) pendingInlineEdits_.insert(key, edit);
        else pendingInlineEdits_.remove(key);
        if (inlineCellStates_.value(inlineCellKey(key, AnchorAliasesColumn)) != InlineCellSaved) {
            inlineCellStates_.remove(inlineCellKey(key, AnchorAliasesColumn));
        }
    }
    applyInlineCellState(item->row(), AnchorAliasesColumn, key);
    updateInlineEditStatus();
}

void AnchorLibraryWindow::updatePendingFileTags(const QString &key, const QStringList &tags)
{
    const AnchorLibraryFile *file = fileForKey(key);
    if (!file) return;
    InlineFileEdit edit = pendingFileInlineEdits_.value(key);
    edit.fileKey = key;
    if (edit.aliases.isEmpty() && !edit.aliasesDirty) edit.aliases = file->resource.aliases;
    edit.tags = tags;
    edit.tagsDirty = edit.tags != file->resource.tags;
    if (edit.tagsDirty) {
        pendingFileInlineEdits_.insert(key, edit);
        fileInlineCellStates_.insert(inlineFileCellKey(key, FileTagsColumn), InlineCellDirty);
    } else {
        edit.tagsDirty = false;
        if (edit.aliasesDirty) pendingFileInlineEdits_.insert(key, edit);
        else pendingFileInlineEdits_.remove(key);
        if (fileInlineCellStates_.value(inlineFileCellKey(key, FileTagsColumn)) != InlineCellSaved) {
            fileInlineCellStates_.remove(inlineFileCellKey(key, FileTagsColumn));
        }
    }
    for (int row = 0; row < fileTable_->rowCount(); ++row) {
        const Pinloom::Ui::TableItem *name = fileTable_->item(row, FileNameColumn);
        if (!name || name->data(FileKeyRole).toString() != key) continue;
        Pinloom::Ui::TableItem *tagItem = fileTable_->item(row, FileTagsColumn);
        if (tagItem) {
            const bool wasPopulating = populatingFileTable_;
            populatingFileTable_ = true;
            tagItem->setText(tags.join(QStringLiteral(", ")));
            tagItem->setData(TagValuesRole, tags);
            tagItem->setToolTip(tags.join(QStringLiteral(", ")));
            populatingFileTable_ = wasPopulating;
            applyFileInlineCellState(row, FileTagsColumn, key);
            fileTable_->resizeRowToContents(row);
        }
        break;
    }
    updateInlineEditStatus();
}

void AnchorLibraryWindow::updatePendingAnchorTags(const QString &key, const QStringList &tags)
{
    const AnchorLibraryAnchor *entry = anchorForInlineKey(key);
    if (!entry) return;
    InlineAnchorEdit edit = pendingInlineEdits_.value(key);
    edit.resourceId = entry->resourceId;
    edit.anchorIdentity = anchorIdentityKey(entry->anchor);
    if (edit.aliases.isEmpty() && !edit.aliasesDirty) edit.aliases = entry->anchor.aliases;
    edit.tags = tags;
    edit.tagsDirty = edit.tags != entry->anchor.tags;
    if (edit.tagsDirty) {
        pendingInlineEdits_.insert(key, edit);
        inlineCellStates_.insert(inlineCellKey(key, AnchorTagsColumn), InlineCellDirty);
    } else {
        edit.tagsDirty = false;
        if (edit.aliasesDirty) pendingInlineEdits_.insert(key, edit);
        else pendingInlineEdits_.remove(key);
        if (inlineCellStates_.value(inlineCellKey(key, AnchorTagsColumn)) != InlineCellSaved) {
            inlineCellStates_.remove(inlineCellKey(key, AnchorTagsColumn));
        }
    }
    for (int row = 0; row < anchorTable_->rowCount(); ++row) {
        const Pinloom::Ui::TableItem *name = anchorTable_->item(row, AnchorNameColumn);
        if (!name || inlineAnchorKey(name->data(ResourceIdRole).toString(),
                                     name->data(AnchorIdentityRole).toString()) != key) continue;
        Pinloom::Ui::TableItem *tagItem = anchorTable_->item(row, AnchorTagsColumn);
        if (tagItem) {
            const bool wasPopulating = populatingAnchorTable_;
            populatingAnchorTable_ = true;
            tagItem->setText(tags.join(QStringLiteral(", ")));
            tagItem->setData(TagValuesRole, tags);
            tagItem->setToolTip(tags.join(QStringLiteral(", ")));
            populatingAnchorTable_ = wasPopulating;
            applyInlineCellState(row, AnchorTagsColumn, key);
            anchorTable_->resizeRowToContents(row);
        }
        break;
    }
    updateInlineEditStatus();
}

void AnchorLibraryWindow::openFileTagEditor(int row)
{
    if (row < 0 || row >= fileTable_->rowCount() || showingTrash()) return;
    const Pinloom::Ui::TableItem *name = fileTable_->item(row, FileNameColumn);
    if (!name) return;
    const QString key = name->data(FileKeyRole).toString();
    const AnchorLibraryFile *file = fileForKey(key);
    if (!file) return;
    const auto pending = pendingFileInlineEdits_.constFind(key);
    const QStringList selectedTags = pending != pendingFileInlineEdits_.constEnd() && pending->tagsDirty
        ? pending->tags
        : file->resource.tags;
    openTagEditor(fileTable_,
                  row,
                  FileTagsColumn,
                  selectedTags,
                  availableFileTags(),
                  true,
                  [this, key](const QStringList &tags) { updatePendingFileTags(key, tags); });
}

void AnchorLibraryWindow::openAnchorTagEditor(int row)
{
    if (row < 0 || row >= anchorTable_->rowCount() || showingTrash()) return;
    const Pinloom::Ui::TableItem *name = anchorTable_->item(row, AnchorNameColumn);
    if (!name) return;
    const QString key = inlineAnchorKey(name->data(ResourceIdRole).toString(),
                                        name->data(AnchorIdentityRole).toString());
    const AnchorLibraryAnchor *entry = anchorForInlineKey(key);
    if (!entry) return;
    const auto pending = pendingInlineEdits_.constFind(key);
    const QStringList selectedTags = pending != pendingInlineEdits_.constEnd() && pending->tagsDirty
        ? pending->tags
        : entry->anchor.tags;
    openTagEditor(anchorTable_,
                  row,
                  AnchorTagsColumn,
                  selectedTags,
                  availableAnchorTags(),
                  false,
                  [this, key](const QStringList &tags) { updatePendingAnchorTags(key, tags); });
}

void AnchorLibraryWindow::openTagEditor(Pinloom::Ui::Table *table,
                                        int row,
                                        int column,
                                        const QStringList &selectedTags,
                                        QStringList availableTags,
                                        bool fileTags,
                                        std::function<void(const QStringList &)> updateHandler)
{
    if (!table || row < 0 || row >= table->rowCount() || !table->item(row, column) || showingTrash()) return;
    if (tagEditorPopup_) tagEditorPopup_->close();
    for (const QString &tag : selectedTags) appendUnique(availableTags, tag);
    availableTags.sort(Qt::CaseInsensitive);

    auto *popup = Ui::popupFrame(this);
    popup->setObjectName(fileTags ? QStringLiteral("anchorLibraryFileTagEditorPopup")
                                  : QStringLiteral("anchorLibraryTagEditorPopup"));
    popup->setAccessibleName(fileTags ? tr("File tags") : tr("Anchor tags"));
    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    auto *queryRow = new QHBoxLayout;
    auto *query = Pinloom::Ui::lineEdit(popup);
    query->setObjectName(fileTags ? QStringLiteral("anchorLibraryFileTagEditorFilter")
                                  : QStringLiteral("anchorLibraryTagEditorFilter"));
    query->setPlaceholderText(fileTags ? tr("Filter or create a file tag")
                                       : tr("Filter or create an anchor tag"));
    query->setClearButtonEnabled(true);
    auto *create = Pinloom::Ui::toolButton(popup);
    create->setObjectName(fileTags ? QStringLiteral("anchorLibraryCreateFileTagButton")
                                   : QStringLiteral("anchorLibraryCreateTagButton"));
    create->setText(QStringLiteral("+"));
    create->setToolTip(fileTags ? tr("Create and select this new file tag")
                                : tr("Create and select this new anchor tag"));
    create->setAccessibleName(create->toolTip());
    queryRow->addWidget(query, 1);
    queryRow->addWidget(create);
    auto *list = new Pinloom::Ui::List(popup);
    list->setObjectName(fileTags ? QStringLiteral("anchorLibraryFileTagEditorList")
                                 : QStringLiteral("anchorLibraryTagEditorList"));
    list->setSelectionMode(QAbstractItemView::NoSelection);
    for (const QString &tag : availableTags) {
        auto *item = new Pinloom::Ui::ListItem(tag, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(selectedTags.contains(tag, Qt::CaseInsensitive) ? Qt::Checked : Qt::Unchecked);
        item->setBackground(colorForTag(tag).lighter(145));
    }
    layout->addLayout(queryRow);
    layout->addWidget(list, 1);

    auto updateCreateState = [query, create, list]() {
        const QString candidate = query->text().trimmed();
        bool duplicate = false;
        for (int index = 0; index < list->count(); ++index) {
            if (list->item(index)->text().compare(candidate, Qt::CaseInsensitive) == 0) {
                duplicate = true;
                break;
            }
        }
        create->setEnabled(!candidate.isEmpty() && !duplicate);
    };
    auto selectedValues = [list]() {
        QStringList tags;
        for (int index = 0; index < list->count(); ++index) {
            if (list->item(index)->checkState() == Qt::Checked) appendUnique(tags, list->item(index)->text());
        }
        return tags;
    };
    connect(query, &QLineEdit::textChanged, popup, [list, updateCreateState](const QString &text) {
        const QString needle = text.trimmed();
        for (int index = 0; index < list->count(); ++index) {
            list->item(index)->setHidden(!needle.isEmpty()
                                        && !list->item(index)->text().contains(needle, Qt::CaseInsensitive));
        }
        updateCreateState();
    });
    connect(list, &Pinloom::Ui::List::itemChanged, popup, [selectedValues, updateHandler](Pinloom::Ui::ListItem *) {
        updateHandler(selectedValues());
    });
    connect(create, &QToolButton::clicked, popup, [this, query, list, updateCreateState]() {
        const QString tag = query->text().trimmed();
        if (tag.isEmpty()) return;
        for (int index = 0; index < list->count(); ++index) {
            if (list->item(index)->text().compare(tag, Qt::CaseInsensitive) == 0) return;
        }
        auto *item = new Pinloom::Ui::ListItem(tag, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setBackground(colorForTag(tag).lighter(145));
        item->setCheckState(Qt::Checked);
        query->clear();
        updateCreateState();
    });
    updateCreateState();
    tagEditorPopup_ = popup;
    connect(popup, &QObject::destroyed, this, [this, popup]() {
        if (tagEditorPopup_ == popup) tagEditorPopup_ = nullptr;
    });
    const QRect cell = table->visualItemRect(table->item(row, column));
    QPoint position = table->viewport()->mapToGlobal(cell.bottomLeft());
    popup->resize(std::max(300, cell.width()), 280);
    if (QScreen *screen = QApplication::screenAt(position)) {
        const QRect available = screen->availableGeometry();
        if (position.x() + popup->width() > available.right()) position.setX(available.right() - popup->width());
        if (position.y() + popup->height() > available.bottom()) position.setY(position.y() - popup->height() - cell.height());
    }
    popup->move(position);
    popup->show();
    query->setFocus();
}

bool AnchorLibraryWindow::savePendingInlineEdits()
{
    if (auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget())) {
        if (fileTable_->isAncestorOf(editor)) fileTable_->setFocus(Qt::OtherFocusReason);
        else if (anchorTable_->isAncestorOf(editor)) anchorTable_->setFocus(Qt::OtherFocusReason);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    const bool hasPendingEdits = !pendingFileInlineEdits_.isEmpty() || !pendingInlineEdits_.isEmpty();
    if (!options_.managementService || !hasPendingEdits || showingTrash()) {
        statusText_ = !hasPendingEdits ? tr("No inline Alias or Tag changes to save")
                                      : tr("Inline edits cannot be saved from Trash");
        Pinloom::Ui::setStatusText(statusLabel_, statusText_);
        return false;
    }
    int savedCells = 0;
    const QStringList fileKeys = pendingFileInlineEdits_.keys();
    for (const QString &key : fileKeys) {
        const AnchorLibraryFile *file = fileForKey(key);
        if (!file) {
            statusText_ = tr("An edited file no longer exists");
            Pinloom::Ui::setStatusText(statusLabel_, statusText_);
            return false;
        }
        const InlineFileEdit edit = pendingFileInlineEdits_.value(key);
        ResourceMetadataUpdate update;
        update.title = file->resource.title.trimmed().isEmpty() ? fileDisplayName(*file) : file->resource.title;
        update.aliases = edit.aliasesDirty ? edit.aliases : file->resource.aliases;
        update.tags = edit.tagsDirty ? edit.tags : file->resource.tags;
        const AnchorLibraryOperationResult result = options_.managementService->updateResourceMetadata(
            resourceIdsForFile(*file), update);
        if (!result.success) {
            setOperationResult(result, false);
            return false;
        }
        if (edit.aliasesDirty) {
            fileInlineCellStates_.insert(inlineFileCellKey(key, FileAliasesColumn), InlineCellSaved);
            ++savedCells;
        }
        if (edit.tagsDirty) {
            fileInlineCellStates_.insert(inlineFileCellKey(key, FileTagsColumn), InlineCellSaved);
            ++savedCells;
        }
        pendingFileInlineEdits_.remove(key);
    }
    const QStringList anchorKeys = pendingInlineEdits_.keys();
    for (const QString &key : anchorKeys) {
        const AnchorLibraryAnchor *entry = anchorForInlineKey(key);
        if (!entry) {
            statusText_ = tr("An edited anchor no longer exists");
            Pinloom::Ui::setStatusText(statusLabel_, statusText_);
            return false;
        }
        const InlineAnchorEdit edit = pendingInlineEdits_.value(key);
        AnchorMetadataUpdate update;
        update.name = entry->anchor.name;
        update.aliases = edit.aliasesDirty ? edit.aliases : entry->anchor.aliases;
        update.tags = edit.tagsDirty ? edit.tags : entry->anchor.tags;
        update.pinned = entry->anchor.pinned;
        const AnchorLibraryOperationResult result = options_.managementService->updateAnchorMetadata(
            {entry->resourceId, entry->anchor}, update);
        if (!result.success) {
            setOperationResult(result, false);
            return false;
        }
        if (edit.aliasesDirty) {
            inlineCellStates_.insert(inlineCellKey(key, AnchorAliasesColumn), InlineCellSaved);
            ++savedCells;
        }
        if (edit.tagsDirty) {
            inlineCellStates_.insert(inlineCellKey(key, AnchorTagsColumn), InlineCellSaved);
            ++savedCells;
        }
        pendingInlineEdits_.remove(key);
    }
    refreshLibrary();
    statusText_ = tr("Saved %1 inline Alias/Tag cell(s)").arg(savedCells);
    Pinloom::Ui::setStatusText(statusLabel_, statusText_);
    return true;
}

void AnchorLibraryWindow::showFileContextMenu(const QPoint &position)
{
    const QModelIndex index = fileTable_->indexAt(position);
    if (!index.isValid()) return;
    if (!fileTable_->selectionModel()->isRowSelected(index.row(), QModelIndex())) fileTable_->selectRow(index.row());
    std::unique_ptr<QMenu> ownedMenu(Pinloom::Ui::menu(this));    QMenu &menu = *ownedMenu;
    if (showingTrash()) {
        auto *restoreMetadata = addToneMenuAction(&menu,
                                                  QStringLiteral("anchorLibraryRestoreFileMetadataAction"),
                                                  QStringLiteral("恢复该文件的 Alias 和 Tag"),
                                                  QStringLiteral("positive"),
                                                  this,
                                                  [this]() { restoreSelectedFiles(); });
        restoreMetadata->setEnabled(selectedFile() && selectedFile()->resource.deleted);
        auto *restoreAnchors = addToneMenuAction(&menu,
                                                 QStringLiteral("anchorLibraryRestoreAllFileAnchorsAction"),
                                                 QStringLiteral("恢复该文件中的所有 Anchor"),
                                                 QStringLiteral("positive"),
                                                 this,
                                                 [this]() { restoreAllAnchorsForSelectedFiles(); });
        restoreAnchors->setEnabled(!allAnchorReferencesForSelectedFiles(true).isEmpty());
        menu.addSeparator();
        auto *deleteMetadata = addToneMenuAction(&menu,
                                                 QStringLiteral("anchorLibraryDeleteFileMetadataAction"),
                                                 QStringLiteral("永久删除该文件的 Alias 和 Tag"),
                                                 QStringLiteral("destructive"),
                                                 this,
                                                 [this]() { permanentlyClearSelectedFileMetadata(); });
        deleteMetadata->setEnabled(selectedFile() && selectedFile()->resource.deleted);
        auto *deleteAnchors = addToneMenuAction(&menu,
                                                QStringLiteral("anchorLibraryDeleteAllFileAnchorsPermanentlyAction"),
                                                QStringLiteral("永久删除该文件的所有 Anchor"),
                                                QStringLiteral("destructive"),
                                                this,
                                                [this]() { permanentlyDeleteAllAnchorsForSelectedFiles(); });
        deleteAnchors->setEnabled(!allAnchorReferencesForSelectedFiles(true).isEmpty());
    } else {
        const bool hasActiveAnchors = !allAnchorReferencesForSelectedFiles(false).isEmpty();
        auto *deleteAnchors = addToneMenuAction(&menu,
                          QStringLiteral("anchorLibraryDeleteAllFileAnchorsAction"),
                          QStringLiteral("删除所有 Anchor"),
                          QStringLiteral("destructive"),
                          this,
                          [this]() { deleteAllAnchorsForSelectedFiles(); });
        deleteAnchors->setEnabled(options_.managementService && hasActiveAnchors);
        if (!hasActiveAnchors) {
            auto *clearMetadata = addToneMenuAction(&menu,
                QStringLiteral("anchorLibraryClearAnchorlessFileMetadataAction"),
                QStringLiteral("清除全部 Tag 和 Alias"),
                QStringLiteral("destructive"), this,
                [this]() { clearSelectedAnchorlessFileMetadata(); });
            clearMetadata->setEnabled(options_.managementService != nullptr);
        }
    }
    menu.exec(fileTable_->viewport()->mapToGlobal(position));
}

void AnchorLibraryWindow::showAnchorContextMenu(const QPoint &position)
{
    const QModelIndex index = anchorTable_->indexAt(position);
    if (!index.isValid()) return;
    if (!anchorTable_->selectionModel()->isRowSelected(index.row(), QModelIndex())) anchorTable_->selectRow(index.row());
    anchorTable_->setCurrentCell(index.row(), AnchorNameColumn, QItemSelectionModel::NoUpdate);
    std::unique_ptr<QMenu> ownedMenu(Pinloom::Ui::menu(this));    QMenu &menu = *ownedMenu;
    if (showingTrash()) {
        addToneMenuAction(&menu,
                          QStringLiteral("anchorLibraryRestoreAnchorAction"),
                          QStringLiteral("恢复"),
                          QStringLiteral("positive"),
                          this,
                          [this]() { restoreSelectedAnchors(); });
        addToneMenuAction(&menu,
                          QStringLiteral("anchorLibraryDeleteAnchorPermanentlyAction"),
                          QStringLiteral("永久删除"),
                          QStringLiteral("destructive"),
                          this,
                          [this]() { permanentlyDeleteSelectedAnchors(); });
    } else {
        const auto anchor = selectedAnchor();
        const AnchorLibraryFile *file = selectedFile();
        const QString locatorType = anchor
            ? anchor->anchor.locatorType.trimmed().toLower()
            : QString();
        const bool pdfAnchor = anchor && file
            && (file->resource.kind == ResourceKind::Pdf
                || locatorType.startsWith(QStringLiteral("sumatrapdf."))
                || locatorType.startsWith(QStringLiteral("pdf.")));
        QAction *recapture = menu.addAction(tr("Recapture"));
        recapture->setObjectName(QStringLiteral("anchorLibraryRecaptureAnchorAction"));
        recapture->setEnabled(options_.managementService && pdfAnchor
                              && static_cast<bool>(options_.locatorRecaptureHandler));
        connect(recapture, &QAction::triggered, this, &AnchorLibraryWindow::recaptureSelectedAnchor);
        menu.addSeparator();
        addToneMenuAction(&menu,
                          QStringLiteral("anchorLibraryDeleteAnchorAction"),
                          QStringLiteral("删除"),
                          QStringLiteral("destructive"),
                          this,
                          [this]() { deleteSelectedAnchors(); });
        addToneMenuAction(&menu,
                          QStringLiteral("anchorLibraryDeleteAllAnchorsAction"),
                          QStringLiteral("删除所有"),
                          QStringLiteral("destructive"),
                          this,
                          [this]() { deleteAllAnchorsForSelectedFiles(); });
    }
    menu.exec(anchorTable_->viewport()->mapToGlobal(position));
}

void AnchorLibraryWindow::showPermanentFileDeleteMenu()
{
    if (!showingTrash() || selectedFiles().isEmpty()) return;
    std::unique_ptr<QMenu> ownedMenu(Pinloom::Ui::menu(this));    QMenu &menu = *ownedMenu;
    auto *deleteMetadata = addToneMenuAction(&menu,
                                             QStringLiteral("anchorLibraryDeleteFileMetadataAction"),
                                             QStringLiteral("永久删除该文件的 Alias 和 Tag"),
                                             QStringLiteral("destructive"),
                                             this,
                                             [this]() { permanentlyClearSelectedFileMetadata(); });
    deleteMetadata->setEnabled(selectedFile() && selectedFile()->resource.deleted);
    auto *deleteAnchors = addToneMenuAction(&menu,
                                            QStringLiteral("anchorLibraryDeleteAllFileAnchorsPermanentlyAction"),
                                            QStringLiteral("永久删除该文件的所有 Anchor"),
                                            QStringLiteral("destructive"),
                                            this,
                                            [this]() { permanentlyDeleteAllAnchorsForSelectedFiles(); });
    deleteAnchors->setEnabled(!allAnchorReferencesForSelectedFiles(true).isEmpty());
    menu.exec(QCursor::pos());
}

void AnchorLibraryWindow::applyLibraryTheme()
{
    const bool trash = showingTrash();
    const QSignalBlocker blocker(trashButton_);
    trashButton_->setChecked(trash);
    trashButton_->setToolTip(trash ? tr("Return to All marked files") : tr("Open Trash"));
    trashButton_->setAccessibleName(trashButton_->toolTip());
    trashButton_->setProperty("accent", trash ? QStringLiteral("destructive") : QString());
    setProperty("trashMode", trash);
    for (QWidget *widget : findChildren<QWidget *>()) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
    style()->unpolish(this);
    style()->polish(this);
    Ui::refreshViewPalettes(*qApp);
    update();
}

void AnchorLibraryWindow::promptAnchorTagUpdate(bool remove)
{
    bool accepted = false;
    const QString text = Pinloom::Ui::getText(this, remove ? tr("Remove Anchor Tags") : tr("Add Anchor Tags"), tr("Tags (comma-separated)"), QLineEdit::Normal, QString(), &accepted);
    if (accepted) updateSelectedAnchorTags(editorValues(text), remove);
}

void AnchorLibraryWindow::promptFileTagUpdate(bool remove)
{
    bool accepted = false;
    const QString text = Pinloom::Ui::getText(this, remove ? tr("Remove File Tags") : tr("Add File Tags"), tr("Tags (comma-separated)"), QLineEdit::Normal, QString(), &accepted);
    if (accepted) updateSelectedFileTags(editorValues(text), remove);
}

void AnchorLibraryWindow::promptSavedViewCreation()
{
    bool accepted = false;
    const QString name = Pinloom::Ui::getText(this, tr("Save View"), tr("View name"), QLineEdit::Normal, QString(), &accepted);
    if (accepted) saveCurrentView(name);
}

void AnchorLibraryWindow::promptTagManager()
{
    if (!options_.managementService) return;
    Pinloom::Ui::Dialog dialog(this);
    dialog.setWindowTitle(tr("Tag Manager"));
    dialog.resize(560, 380);
    auto *layout = new QVBoxLayout(&dialog);
    const QList<AnchorLibraryTagSummary> tags = options_.managementService->tagSummary();
    auto *table = new Pinloom::Ui::Table(tags.size(), 3, &dialog);
    table->setObjectName(QStringLiteral("anchorLibraryTagManagerTable"));
    table->setHorizontalHeaderLabels({tr("Tag"), tr("Files"), tr("Anchors")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int row = 0; row < tags.size(); ++row) {
        table->setItem(row, 0, new Pinloom::Ui::TableItem(tags.at(row).tag));
        table->setItem(row, 1, new Pinloom::Ui::TableItem(QString::number(tags.at(row).resourceCount)));
        table->setItem(row, 2, new Pinloom::Ui::TableItem(QString::number(tags.at(row).anchorCount)));
    }
    auto *actions = new QHBoxLayout;
    auto *rename = Pinloom::Ui::pushButton(tr("Rename"), &dialog);
    auto *remove = Pinloom::Ui::pushButton(tr("Delete"), &dialog);
    actions->addWidget(rename);
    actions->addWidget(remove);
    actions->addStretch(1);
    auto *buttons = new Pinloom::Ui::DialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(rename, &QPushButton::clicked, &dialog, [this, table, &dialog]() {
        if (table->currentRow() < 0) return;
        const QString oldTag = table->item(table->currentRow(), 0)->text();
        bool accepted = false;
        const QString newTag = Pinloom::Ui::getText(&dialog, tr("Rename Tag"), tr("New name"), QLineEdit::Normal, oldTag, &accepted);
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
    Pinloom::Ui::setStatusText(statusLabel_, statusText_);
    updateActionButtons();
}

void AnchorLibraryWindow::refreshFilterChoices()
{
    const QString fileTag = tagFilterCombo_->currentData().toString();
    const QString anchorTag = anchorTagFilterCombo_->currentData().toString();
    const int kind = kindFilterCombo_->count() > 0 ? kindFilterCombo_->currentData().toInt() : -1;
    const QString app = appFilterCombo_->currentData().toString();
    QStringList fileTags;
    QStringList anchorTags;
    QStringList apps;
    QList<int> kinds;
    for (const AnchorLibraryFile &file : files_) {
        if (!kinds.contains(static_cast<int>(file.resource.kind))) kinds.append(static_cast<int>(file.resource.kind));
        for (const QString &value : file.resource.tags) appendUnique(fileTags, value);
        for (const AnchorLibraryAnchor &entry : file.anchors) {
            for (const QString &value : entry.anchor.tags) appendUnique(anchorTags, value);
            if (!isHiddenApplicationFilterValue(entry.anchor.targetApp)) {
                appendUnique(apps, entry.anchor.targetApp);
            }
        }
    }
    fileTags.sort(Qt::CaseInsensitive);
    anchorTags.sort(Qt::CaseInsensitive);
    apps.sort(Qt::CaseInsensitive);
    std::sort(kinds.begin(), kinds.end());
    const QSignalBlocker tagBlocker(tagFilterCombo_);
    const QSignalBlocker anchorTagBlocker(anchorTagFilterCombo_);
    const QSignalBlocker kindBlocker(kindFilterCombo_);
    const QSignalBlocker appBlocker(appFilterCombo_);
    tagFilterCombo_->clear();
    tagFilterCombo_->addItem(tr("All file tags"), QString());
    for (const QString &value : fileTags) tagFilterCombo_->addItem(value, value);
    anchorTagFilterCombo_->clear();
    anchorTagFilterCombo_->addItem(tr("All anchor tags"), QString());
    for (const QString &value : anchorTags) anchorTagFilterCombo_->addItem(value, value);
    kindFilterCombo_->clear();
    kindFilterCombo_->addItem(tr("All types"), -1);
    for (int value : kinds) kindFilterCombo_->addItem(resourceKindLabel(static_cast<ResourceKind>(value)), value);
    appFilterCombo_->clear();
    appFilterCombo_->addItem(tr("All applications"), QString());
    for (const QString &value : apps) appFilterCombo_->addItem(value, value);
    tagFilterCombo_->setCurrentIndex(std::max(0, tagFilterCombo_->findData(fileTag)));
    anchorTagFilterCombo_->setCurrentIndex(std::max(0, anchorTagFilterCombo_->findData(anchorTag)));
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
    const bool oneFile = selectedFiles().size() == 1;
    restoreAction_->setVisible(trash);
    restoreButton_->setEnabled(trash && manager && (!anchors.isEmpty() || !selectedFiles().isEmpty()));
    tagsButton_->setEnabled(manager && (!anchors.isEmpty() || !files_.isEmpty()));
    fileActionsButton_->setEnabled(manager && !selectedFiles().isEmpty());
    relinkButton_->setEnabled(manager && oneFile && file && file->resource.kind != ResourceKind::Url);
    mergeButton_->setEnabled(manager && oneFile && file && !trash && resourceIdsForFile(*file).size() > 1);
    integrityButton_->setEnabled(manager);
    manageButton_->setEnabled(options_.settings || manager);
    undoButton_->setEnabled(manager && options_.managementService->canUndo());
    saveButton_->setEnabled(manager && !trash);
    openButton_->setEnabled(anchors.size() == 1 && !trash && file && !file->resource.deleted
                            && static_cast<bool>(options_.anchorJumpHandler));
    previewButton_->setEnabled(anchors.size() == 1);
}

void AnchorLibraryWindow::setDetailedColumns(bool detailed)
{
    for (int column : {FileLocationColumn, FileTypeColumn, FileLastMarkedColumn,
                       FileOpenCountColumn, FileLastOpenedColumn})
        fileTable_->setColumnHidden(column, !detailed);
    for (int column : {AnchorUpdatedColumn, AnchorOpenCountColumn, AnchorLastOpenedColumn})
        anchorTable_->setColumnHidden(column, !detailed);
    for (int column : {FileNameColumn, FileAliasesColumn, FileTagsColumn})
        fileTable_->horizontalHeader()->setSectionResizeMode(column, detailed ? QHeaderView::Interactive : QHeaderView::Stretch);
    fileTable_->horizontalHeader()->setSectionResizeMode(FileLocationColumn, QHeaderView::Interactive);
    fileTable_->setColumnWidth(FileLocationColumn, 260);
    for (int column : {AnchorNameColumn, AnchorAliasesColumn, AnchorTagsColumn})
        anchorTable_->horizontalHeader()->setSectionResizeMode(column, detailed ? QHeaderView::Interactive : QHeaderView::Stretch);
    if (detailed) {
        for (int column : {FileNameColumn, FileAliasesColumn, FileTagsColumn}) fileTable_->setColumnWidth(column, 170);
        for (int column : {AnchorNameColumn, AnchorAliasesColumn, AnchorTagsColumn}) anchorTable_->setColumnWidth(column, 170);
    }
}

void AnchorLibraryWindow::updateViewSummary()
{
    fileCountLabel_->setText(tr("Files · %1").arg(fileTable_->rowCount()));
    anchorCountLabel_->setText(tr("Anchors · %1").arg(anchorTable_->rowCount()));
    const bool noFiles = fileTable_->rowCount() == 0;
    const bool noAnchors = anchorTable_->rowCount() == 0;
    fileEmptyLabel_->setText(showingTrash() ? tr("No deleted records match this view.")
        : tr("No marked files match this view.\nCapture an anchor or adjust the filters."));
    anchorEmptyLabel_->setText(noFiles ? tr("Select a file to browse its anchors.")
        : showingTrash() ? tr("This file has no deleted anchors.") : tr("This file has no active anchors."));
    fileEmptyLabel_->setVisible(noFiles);
    anchorEmptyLabel_->setVisible(noAnchors);
    fileTable_->setVisible(!noFiles);
    anchorTable_->setVisible(!noAnchors);
    int filters = directoryFilterEdit_->text().trimmed().isEmpty() ? 0 : 1;
    for (auto *combo : {tagFilterCombo_, anchorTagFilterCombo_, kindFilterCombo_, appFilterCombo_,
                        timeFilterCombo_, usageFilterCombo_}) {
        if (combo->currentIndex() > 0) ++filters;
    }
    filterToggle_->setText(filters == 0 ? tr("Filters") : tr("Filters (%1 active)").arg(filters));
    filterToggle_->setAccessibleName(filterToggle_->text());
    updateActionButtons();
}

void AnchorLibraryWindow::updateStatus()
{
    int anchors = 0;
    for (int row = 0; row < fileTable_->rowCount(); ++row) {
        anchors += fileTable_->item(row, FileAnchorCountColumn)->data(Qt::DisplayRole).toInt();
    }
    statusText_ = showingTrash()
        ? tr("%1 file(s) | %2 Trash item(s)").arg(fileTable_->rowCount()).arg(anchors)
        : tr("%1 marked file(s) | %2 anchor(s)").arg(fileTable_->rowCount()).arg(anchors);
    Pinloom::Ui::setStatusText(statusLabel_, statusText_, false);
    updateViewSummary();
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

QList<AnchorLibraryWindow::FileViewData> AnchorLibraryWindow::sortedVisibleFiles() const
{
    QList<FileViewData> visible;
    const bool sortTags = std::any_of(fileSortKeys_.cbegin(), fileSortKeys_.cend(), [](const SortKey &key) {
        return key.column == FileTagsColumn;
    });
    for (const AnchorLibraryFile &file : files_) {
        FileViewData view(&file, scopedAnchors(file));
        if (!fileMatchesFilter(view)) continue;
        view.prepareForDisplay(sortTags);
        visible.append(std::move(view));
    }
    auto textCompare = [](const QString &left, const QString &right) { return left.compare(right, Qt::CaseInsensitive); };
    std::stable_sort(visible.begin(), visible.end(), [&](const FileViewData &left, const FileViewData &right) {
        for (const SortKey &key : fileSortKeys_) {
            int comparison = 0;
            switch (key.column) {
            case FileNameColumn: comparison = textCompare(left.name(), right.name()); break;
            case FileAliasesColumn: comparison = textCompare(left.aliases, right.aliases); break;
            case FileLocationColumn: comparison = textCompare(left.file->resource.location, right.file->resource.location); break;
            case FileTypeColumn: comparison = textCompare(left.type, right.type); break;
            case FileAnchorCountColumn: comparison = left.anchors.size() - right.anchors.size(); break;
            case FileTagsColumn: comparison = textCompare(left.tags, right.tags); break;
            case FileLastMarkedColumn: comparison = left.markedAt() < right.markedAt() ? -1 : (left.markedAt() > right.markedAt() ? 1 : 0); break;
            case FileStatusColumn: comparison = static_cast<int>(left.targetExists()) - static_cast<int>(right.targetExists()); break;
            case FileOpenCountColumn: comparison = left.openCount() - right.openCount(); break;
            case FileLastOpenedColumn: comparison = left.openedAt() < right.openedAt() ? -1 : (left.openedAt() > right.openedAt() ? 1 : 0); break;
            default: break;
            }
            if (comparison != 0) return key.order == Qt::AscendingOrder ? comparison < 0 : comparison > 0;
        }
        return left.key < right.key;
    });
    return visible;
}

QList<AnchorLibraryWindow::AnchorViewData> AnchorLibraryWindow::sortedAnchors(const QList<AnchorLibraryAnchor> &anchors) const
{
    QList<AnchorViewData> sorted;
    sorted.reserve(anchors.size());
    const bool sortTags = std::any_of(anchorSortKeys_.cbegin(), anchorSortKeys_.cend(), [](const SortKey &key) {
        return key.column == AnchorTagsColumn;
    });
    for (const AnchorLibraryAnchor &entry : anchors) sorted.append(AnchorViewData(entry, sortTags));
    auto textCompare = [](const QString &left, const QString &right) { return left.compare(right, Qt::CaseInsensitive); };
    std::stable_sort(sorted.begin(), sorted.end(), [&](const AnchorViewData &left, const AnchorViewData &right) {
        for (const SortKey &key : anchorSortKeys_) {
            int comparison = 0;
            switch (key.column) {
            case AnchorNameColumn: comparison = textCompare(left.name, right.name); break;
            case AnchorAliasesColumn: comparison = textCompare(left.aliases, right.aliases); break;
            case AnchorTagsColumn: comparison = textCompare(left.tags, right.tags); break;
            case AnchorTypeColumn: comparison = textCompare(left.type, right.type); break;
            case AnchorUpdatedColumn: comparison = left.updated < right.updated ? -1 : (left.updated > right.updated ? 1 : 0); break;
            case AnchorOpenCountColumn: comparison = left.entry.usage.openCount - right.entry.usage.openCount; break;
            case AnchorLastOpenedColumn: comparison = left.entry.usage.lastOpenedAt < right.entry.usage.lastOpenedAt ? -1 : (left.entry.usage.lastOpenedAt > right.entry.usage.lastOpenedAt ? 1 : 0); break;
            case AnchorValidityColumn: comparison = textCompare(left.entry.anchor.locatorType, right.entry.anchor.locatorType); break;
            default: break;
            }
            if (comparison != 0) return key.order == Qt::AscendingOrder ? comparison < 0 : comparison > 0;
        }
        return left.identity < right.identity;
    });
    return sorted;
}

} // namespace Pinloom
