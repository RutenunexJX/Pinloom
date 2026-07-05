#include "pinloom/widgets/PinloomPanel.h"

#include "pinloom/core/IndexingService.h"
#include "pinloom/core/LegacyCompatibility.h"
#include "pinloom/widgets/ManualPdfAnchorDialog.h"
#include "pinloom/widgets/TextPreviewDialog.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSize>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace Pinloom {

namespace {

QString rootItemText(const LibraryRoot &root)
{
    const QString indexedAt = root.lastIndexedAt.isValid()
        ? root.lastIndexedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
        : QStringLiteral("never");
    return QStringLiteral("%1%2  |  %3  |  %4")
        .arg(root.pinned ? QStringLiteral("[Pinned] ") : QString(),
             root.displayName.isEmpty() ? root.path : root.displayName,
             root.path,
             indexedAt);
}

QString anchorLabel(const Anchor &anchor)
{
    switch (anchor.type) {
    case AnchorType::FileLine:
        return QStringLiteral("Line");
    case AnchorType::MarkdownHeading:
    case AnchorType::TextHeading:
        return QStringLiteral("Heading");
    case AnchorType::MarkdownBlock:
    case AnchorType::TextBlock:
        return QStringLiteral("Block");
    case AnchorType::Marker:
        return QStringLiteral("Marker");
    case AnchorType::PdfPage:
        return QStringLiteral("Page");
    case AnchorType::PdfRegion:
        return QStringLiteral("PDF Region");
    case AnchorType::UrlFragment:
        return QStringLiteral("Fragment");
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
        return QStringLiteral("Text");
    case ResourceKind::TextSnippet:
        return QStringLiteral("Text");
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

QString compactValue(QString value, int maxLength = 96)
{
    value = value.simplified();
    if (value.size() <= maxLength) {
        return value;
    }
    return value.left(std::max(0, maxLength - 3)) + QStringLiteral("...");
}

QString anchorDisplayName(const Anchor &anchor, const Resource &resource)
{
    const QString name = anchor.name.trimmed();
    if (!name.isEmpty()) {
        return name;
    }
    const QString target = anchor.target.trimmed();
    if (!target.isEmpty()) {
        return target;
    }
    if (!resource.title.trimmed().isEmpty()) {
        return resource.title;
    }
    return resource.location;
}

QString anchorTargetSummary(const Anchor &anchor, const Resource &resource)
{
    QStringList parts;
    if (!anchor.targetApp.trimmed().isEmpty()) {
        parts.append(anchor.targetApp.trimmed());
    }

    QString target = anchor.targetFile.trimmed();
    if (target.isEmpty()) {
        target = anchor.targetUri.trimmed();
    }
    if (target.isEmpty()) {
        target = resource.location.trimmed();
    }
    if (!target.isEmpty()) {
        parts.append(target);
    }

    return parts.join(QStringLiteral(" | "));
}

QString legacyLocatorSummary(const Anchor &anchor)
{
    QStringList parts;
    if (anchor.page > 0) {
        parts.append(QStringLiteral("page %1").arg(anchor.page));
    }
    if (anchor.line > 0) {
        parts.append(QStringLiteral("line %1").arg(anchor.line));
    }
    if (anchor.region.isValid()) {
        parts.append(QStringLiteral("region %1,%2,%3,%4")
                         .arg(QString::number(anchor.region.x(), 'f', 2),
                              QString::number(anchor.region.y(), 'f', 2),
                              QString::number(anchor.region.width(), 'f', 2),
                              QString::number(anchor.region.height(), 'f', 2)));
    }
    return parts.join(QStringLiteral(", "));
}

QString locatorSummary(const Anchor &anchor)
{
    const QString locatorType = anchor.locatorType.trimmed();
    const QString locatorJson = compactValue(anchor.locatorJson.trimmed());
    if (!locatorType.isEmpty() && !locatorJson.isEmpty()) {
        return QStringLiteral("%1 %2").arg(locatorType, locatorJson);
    }
    if (!locatorType.isEmpty()) {
        return locatorType;
    }

    const QString legacy = legacyLocatorSummary(anchor);
    if (!legacy.isEmpty()) {
        return legacy;
    }
    return anchorLabel(anchor);
}

QString anchorHintsSummary(const Anchor &anchor)
{
    QStringList parts;
    if (!anchor.tags.isEmpty()) {
        QStringList tags;
        for (const QString &tag : anchor.tags) {
            const QString trimmed = tag.trimmed();
            if (!trimmed.isEmpty()) {
                tags.append(QStringLiteral("#%1").arg(trimmed));
            }
        }
        if (!tags.isEmpty()) {
            parts.append(tags.join(QLatin1Char(' ')));
        }
    }
    if (!anchor.aliases.isEmpty()) {
        parts.append(QStringLiteral("aliases: %1").arg(anchor.aliases.join(QStringLiteral(", "))));
    }
    return parts.join(QStringLiteral(" | "));
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
    QStringList details;
    const QString target = anchorTargetSummary(anchor, result.resource);
    if (!target.isEmpty()) {
        details.append(target);
    }
    const QString locator = locatorSummary(anchor);
    if (!locator.isEmpty()) {
        details.append(locator);
    }
    const QString hints = anchorHintsSummary(anchor);
    if (!hints.isEmpty()) {
        details.append(hints);
    }

    return QStringLiteral("%1\n%2")
        .arg(anchorDisplayName(anchor, result.resource),
             details.join(QStringLiteral(" | ")));
}

QString matchedContextTag(const Resource &resource, const QStringList &contextTags)
{
    for (const QString &contextTag : contextTags) {
        for (const QString &tag : resource.tags) {
            if (tag.compare(contextTag, Qt::CaseInsensitive) == 0) {
                return tag;
            }
        }
    }
    return {};
}

QString matchedContextLocationPrefix(const Resource &resource, const QStringList &contextLocationPrefixes)
{
    for (const QString &prefix : contextLocationPrefixes) {
        if (!prefix.isEmpty() && resource.location.startsWith(prefix, Qt::CaseInsensitive)) {
            return prefix;
        }
    }
    return {};
}

QString resultMatchSummary(const SearchResult &result, const SearchQuery &query)
{
    QStringList lines;
    if (!result.matchedField.isEmpty()) {
        lines.append(QStringLiteral("Match: %1").arg(result.matchedField));
    }
    if (result.matchedAnchor.has_value()) {
        const Anchor &anchor = result.matchedAnchor.value();
        lines.append(QStringLiteral("Anchor: %1").arg(anchorLabel(anchor)));
        lines.append(QStringLiteral("Locator: %1").arg(locatorSummary(anchor)));
    }

    const QString contextTag = matchedContextTag(result.resource, query.contextTags);
    if (!contextTag.isEmpty()) {
        lines.append(QStringLiteral("Context tag: %1").arg(contextTag));
    }

    const QString contextLocationPrefix = matchedContextLocationPrefix(result.resource, query.contextLocationPrefixes);
    if (!contextLocationPrefix.isEmpty()) {
        lines.append(QStringLiteral("Context location: %1").arg(contextLocationPrefix));
    }
    if (!result.matchedContextResourceId.isEmpty()) {
        if (result.matchedContextRelationLabel.isEmpty()) {
            lines.append(QStringLiteral("Context resource: %1").arg(result.matchedContextResourceId));
        } else {
            QString relationLine = QStringLiteral("Context relation: %1 via %2")
                                       .arg(result.matchedContextResourceId,
                                            result.matchedContextRelationLabel);
            const QString relationNote = result.matchedContextRelationNote.trimmed();
            if (!relationNote.isEmpty()) {
                relationLine.append(QStringLiteral(" (%1)").arg(relationNote));
            }
            lines.append(relationLine);
        }
    }

    return lines.join(QLatin1Char('\n'));
}

QString resultToolTip(const SearchResult &result, const SearchQuery &query)
{
    QStringList lines{result.resource.location};
    if (result.matchedAnchor.has_value()) {
        const Anchor &anchor = result.matchedAnchor.value();
        lines.append(QStringLiteral("Anchor name: %1").arg(anchorDisplayName(anchor, result.resource)));
        const QString target = anchorTargetSummary(anchor, result.resource);
        if (!target.isEmpty()) {
            lines.append(QStringLiteral("Target: %1").arg(target));
        }
        lines.append(QStringLiteral("Locator: %1").arg(locatorSummary(anchor)));
        const QString hints = anchorHintsSummary(anchor);
        if (!hints.isEmpty()) {
            lines.append(hints);
        }
    }
    const QString summary = resultMatchSummary(result, query);
    if (!summary.isEmpty()) {
        lines.append(summary);
    }
    return lines.join(QLatin1Char('\n'));
}

QString relationText(const ResourceRelation &relation, const QString &currentResourceId, ILibraryRepository &repository)
{
    const bool currentIsSource = relation.sourceResourceId == currentResourceId;
    const QString otherResourceId = currentIsSource ? relation.targetResourceId : relation.sourceResourceId;
    const std::optional<Resource> otherResource = repository.findResource(otherResourceId);
    const QString otherLabel = otherResource.has_value()
        ? (otherResource->title.isEmpty() ? otherResource->location : otherResource->title)
        : otherResourceId;
    const QString direction = currentIsSource ? QStringLiteral("->") : QStringLiteral("<-");
    if (relation.note.trimmed().isEmpty()) {
        return QStringLiteral("%1 %2 %3").arg(relation.label, direction, otherLabel);
    }
    return QStringLiteral("%1 %2 %3 (%4)").arg(relation.label, direction, otherLabel, relation.note);
}

bool containsValueCaseInsensitive(const QStringList &values, const QString &needle)
{
    return values.contains(needle, Qt::CaseInsensitive);
}

void appendUniqueCaseInsensitive(QStringList &values, const QString &value)
{
    const QString trimmed = value.trimmed();
    if (!trimmed.isEmpty() && !containsValueCaseInsensitive(values, trimmed)) {
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

QStringList cleanedValues(const QStringList &values, bool tags = false)
{
    QStringList cleaned;
    for (const QString &value : values) {
        appendUniqueCaseInsensitive(cleaned, tags ? cleanTag(value) : value);
    }
    return cleaned;
}

QStringList valuesFromCommaText(const QString &text, bool tags = false)
{
    return cleanedValues(text.split(QLatin1Char(','), Qt::SkipEmptyParts), tags);
}

bool sameAnchor(const Anchor &left, const Anchor &right)
{
    if (!left.id.trimmed().isEmpty() && !right.id.trimmed().isEmpty()) {
        return left.id == right.id;
    }
    return left.type == right.type
        && left.target == right.target
        && left.line == right.line
        && left.page == right.page
        && left.region == right.region
        && left.locatorType == right.locatorType
        && left.locatorJson == right.locatorJson;
}

int anchorIndex(const Resource &resource, const Anchor &anchor)
{
    for (int i = 0; i < resource.anchors.size(); ++i) {
        if (sameAnchor(resource.anchors.at(i), anchor)) {
            return i;
        }
    }
    return -1;
}

PinloomOpenTarget openTargetForResource(const Resource &resource)
{
    PinloomOpenTarget target;
    target.resourceId = resource.id;
    target.resourceKind = resource.kind;
    target.title = resource.title;
    target.location = resource.location;
    return target;
}

PinloomOpenTarget openTargetForItem(const QListWidgetItem *item, int row = -1)
{
    PinloomOpenTarget target;
    if (!item) {
        return target;
    }

    target.resultRow = row;
    target.resourceId = item->data(Qt::UserRole).toString();
    target.location = item->data(Qt::UserRole + 1).toString();
    target.title = item->data(Qt::UserRole + 11).toString();
    target.resourceKind = static_cast<ResourceKind>(item->data(Qt::UserRole + 12).toInt());
    target.matchedField = item->data(Qt::UserRole + 13).toString();
    target.score = item->data(Qt::UserRole + 14).toDouble();
    target.matchedContextTag = item->data(Qt::UserRole + 15).toString();
    target.matchedContextLocationPrefix = item->data(Qt::UserRole + 16).toString();
    target.matchedContextResourceId = item->data(Qt::UserRole + 17).toString();
    target.matchedContextRelationLabel = item->data(Qt::UserRole + 18).toString();
    target.matchedContextRelationNote = item->data(Qt::UserRole + 19).toString();
    target.matchSummary = item->data(Qt::UserRole + 20).toString();

    if (item->data(Qt::UserRole + 2).toBool()) {
        Anchor anchor;
        anchor.id = item->data(Qt::UserRole + 21).toString();
        anchor.name = item->data(Qt::UserRole + 22).toString();
        anchor.targetApp = item->data(Qt::UserRole + 23).toString();
        anchor.targetFile = item->data(Qt::UserRole + 24).toString();
        anchor.targetUri = item->data(Qt::UserRole + 25).toString();
        anchor.locatorType = item->data(Qt::UserRole + 26).toString();
        anchor.locatorJson = item->data(Qt::UserRole + 27).toString();
        anchor.aliases = item->data(Qt::UserRole + 28).toStringList();
        anchor.tags = item->data(Qt::UserRole + 29).toStringList();
        anchor.pinned = item->data(Qt::UserRole + 30).toBool();
        anchor.createdAt = item->data(Qt::UserRole + 31).toDateTime();
        anchor.updatedAt = item->data(Qt::UserRole + 32).toDateTime();
        anchor.usedAt = item->data(Qt::UserRole + 33).toDateTime();
        anchor.type = static_cast<AnchorType>(item->data(Qt::UserRole + 5).toInt());
        anchor.target = item->data(Qt::UserRole + 4).toString();
        anchor.line = item->data(Qt::UserRole + 3).toInt();
        anchor.page = item->data(Qt::UserRole + 6).toInt();
        anchor.region = QRectF(item->data(Qt::UserRole + 7).toDouble(),
                               item->data(Qt::UserRole + 8).toDouble(),
                               item->data(Qt::UserRole + 9).toDouble(),
                               item->data(Qt::UserRole + 10).toDouble());
        target.anchor = anchor;
    }

    return target;
}

bool isDeprecatedPdfManualLineOpenTarget(const PinloomOpenTarget &target)
{
    if (!target.anchor.has_value()) {
        return false;
    }

    Resource resource;
    resource.kind = target.resourceKind;
    resource.location = target.location;
    return isDeprecatedPdfManualLineAnchor(resource, target.anchor.value());
}

PinloomLibraryRootTarget libraryRootTargetForRoot(const LibraryRoot &root, int row)
{
    PinloomLibraryRootTarget target;
    target.id = root.id;
    target.path = root.path;
    target.displayName = root.displayName;
    target.enabled = root.enabled;
    target.pinned = root.pinned;
    target.lastIndexedAt = root.lastIndexedAt;
    target.rootRow = row;
    return target;
}

QUrl urlForLocation(const QString &location)
{
    const QUrl parsed(location);
    if (parsed.isValid()
        && (parsed.scheme() == QLatin1String("http") || parsed.scheme() == QLatin1String("https"))) {
        return parsed;
    }
    return QUrl::fromLocalFile(location);
}

QString pdfFragmentForAnchor(const Anchor &anchor)
{
    if (anchor.page <= 0) {
        return {};
    }

    if (anchor.type == AnchorType::PdfRegion && anchor.region.isValid()) {
        return QStringLiteral("page=%1&viewrect=%2,%3,%4,%5")
            .arg(QString::number(anchor.page),
                 QString::number(anchor.region.x(), 'f', 2),
                 QString::number(anchor.region.y(), 'f', 2),
                 QString::number(anchor.region.width(), 'f', 2),
                 QString::number(anchor.region.height(), 'f', 2));
    }

    return QStringLiteral("page=%1").arg(anchor.page);
}

bool isExcelComAutomationAvailable()
{
#ifdef Q_OS_WIN
    QSettings excelApplicationKey(QStringLiteral("HKEY_CLASSES_ROOT\\Excel.Application"),
                                  QSettings::NativeFormat);
    const QString classId = excelApplicationKey.value(QStringLiteral("CLSID/.")).toString().trimmed();
    const QString currentVersion = excelApplicationKey.value(QStringLiteral("CurVer/.")).toString().trimmed();
    return !classId.isEmpty() || !currentVersion.isEmpty();
#else
    return false;
#endif
}

bool isVisioComAutomationAvailable()
{
#ifdef Q_OS_WIN
    QSettings visioApplicationKey(QStringLiteral("HKEY_CLASSES_ROOT\\Visio.Application"),
                                  QSettings::NativeFormat);
    const QString classId = visioApplicationKey.value(QStringLiteral("CLSID/.")).toString().trimmed();
    const QString currentVersion = visioApplicationKey.value(QStringLiteral("CurVer/.")).toString().trimmed();
    return !classId.isEmpty() || !currentVersion.isEmpty();
#else
    return false;
#endif
}

bool isWordComAutomationAvailable()
{
#ifdef Q_OS_WIN
    QSettings wordApplicationKey(QStringLiteral("HKEY_CLASSES_ROOT\\Word.Application"),
                                 QSettings::NativeFormat);
    const QString classId = wordApplicationKey.value(QStringLiteral("CLSID/.")).toString().trimmed();
    const QString currentVersion = wordApplicationKey.value(QStringLiteral("CurVer/.")).toString().trimmed();
    return !classId.isEmpty() || !currentVersion.isEmpty();
#else
    return false;
#endif
}

bool isPowerPointComAutomationAvailable()
{
#ifdef Q_OS_WIN
    QSettings powerPointApplicationKey(QStringLiteral("HKEY_CLASSES_ROOT\\PowerPoint.Application"),
                                       QSettings::NativeFormat);
    const QString classId = powerPointApplicationKey.value(QStringLiteral("CLSID/.")).toString().trimmed();
    const QString currentVersion = powerPointApplicationKey.value(QStringLiteral("CurVer/.")).toString().trimmed();
    return !classId.isEmpty() || !currentVersion.isEmpty();
#else
    return false;
#endif
}

} // namespace

PinloomPanel::PinloomPanel(ILibraryRepository &repository, QWidget *parent)
    : PinloomPanel(repository, PinloomPanelOptions{}, parent)
{
}

PinloomPanel::PinloomPanel(ILibraryRepository &repository, PinloomPanelOptions options, QWidget *parent)
    : QWidget(parent)
    , repository_(repository)
    , options_(std::move(options))
{
    auto *layout = new QVBoxLayout(this);

    rootControlsWidget_ = new QWidget(this);
    rootControlsWidget_->setObjectName(QStringLiteral("libraryRootControls"));
    auto *rootToolbar = new QHBoxLayout(rootControlsWidget_);
    rootToolbar->setContentsMargins(0, 0, 0, 0);
    auto *addRootButton = new QPushButton(tr("Add Folder"), this);
    addRootButton->setObjectName(QStringLiteral("addRootButton"));
    removeRootButton_ = new QPushButton(tr("Remove Folder"), this);
    refreshSelectedButton_ = new QPushButton(tr("Refresh Selected"), this);
    refreshAllButton_ = new QPushButton(tr("Refresh All"), this);
    rebuildAllButton_ = new QPushButton(tr("Rebuild All"), this);
    pinRootButton_ = new QPushButton(tr("Pin Folder"), this);
    pinRootButton_->setObjectName(QStringLiteral("pinRootButton"));
    pinRootButton_->setCheckable(true);
    fetchRemoteWebPagesCheck_ = new QCheckBox(tr("Fetch Web"), this);
    fetchRemoteWebPagesCheck_->setObjectName(QStringLiteral("fetchRemoteWebPagesCheck"));
    fetchRemoteWebPagesCheck_->setToolTip(tr("Fetch linked HTML pages while indexing web shortcuts"));

    rootToolbar->addWidget(addRootButton);
    rootToolbar->addWidget(removeRootButton_);
    rootToolbar->addWidget(refreshSelectedButton_);
    rootToolbar->addWidget(refreshAllButton_);
    rootToolbar->addWidget(rebuildAllButton_);
    rootToolbar->addWidget(pinRootButton_);
    rootToolbar->addWidget(fetchRemoteWebPagesCheck_);
    rootToolbar->addStretch(1);

    rootList_ = new QListWidget(this);
    rootList_->setObjectName(QStringLiteral("libraryRootList"));
    rootList_->setMaximumHeight(130);

    auto *resultToolbar = new QHBoxLayout();
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setObjectName(QStringLiteral("searchEdit"));
    searchEdit_->setPlaceholderText(tr("Search anchors, aliases, #tags, targets"));
    openButton_ = new QPushButton(tr("Jump"), this);
    openButton_->setObjectName(QStringLiteral("openButton"));
    addAliasButton_ = new QPushButton(tr("Add Alias"), this);
    addAliasButton_->setObjectName(QStringLiteral("addAliasButton"));
    addAnchorButton_ = new QPushButton(tr("Add Anchor"), this);
    addAnchorButton_->setObjectName(QStringLiteral("addAnchorButton"));
    pinButton_ = new QPushButton(tr("Pin"), this);
    pinButton_->setObjectName(QStringLiteral("pinButton"));
    pinButton_->setCheckable(true);
    auto *manageButton = new QPushButton(tr("Manage"), this);
    manageButton->setObjectName(QStringLiteral("manageLibraryButton"));
    manageButton->setCheckable(true);
    manageButton->setChecked(options_.showLibraryRootControls);
    resultToolbar->addWidget(searchEdit_, 1);
    resultToolbar->addWidget(openButton_);
    resultToolbar->addWidget(addAliasButton_);
    resultToolbar->addWidget(addAnchorButton_);
    resultToolbar->addWidget(pinButton_);
    resultToolbar->addWidget(manageButton);

    resultList_ = new QListWidget(this);
    resultList_->setObjectName(QStringLiteral("resultList"));
    resultList_->setAlternatingRowColors(true);
    resultList_->setUniformItemSizes(true);
    searchEdit_->installEventFilter(this);
    resultList_->installEventFilter(this);
    relationLabel_ = new QLabel(this);
    relationLabel_->setObjectName(QStringLiteral("relationLabel"));
    relationLabel_->setWordWrap(true);
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("statusLabel"));

    layout->addLayout(resultToolbar);
    layout->addWidget(resultList_, 1);
    layout->addWidget(relationLabel_);
    layout->addWidget(rootControlsWidget_);
    layout->addWidget(rootList_);
    layout->addWidget(statusLabel_);

    connect(addRootButton, &QPushButton::clicked, this, &PinloomPanel::addLibraryRoot);
    connect(removeRootButton_, &QPushButton::clicked, this, &PinloomPanel::removeSelectedLibraryRoot);
    connect(refreshSelectedButton_, &QPushButton::clicked, this, &PinloomPanel::refreshSelectedRoot);
    connect(refreshAllButton_, &QPushButton::clicked, this, &PinloomPanel::refreshAllRoots);
    connect(rebuildAllButton_, &QPushButton::clicked, this, &PinloomPanel::rebuildAllRoots);
    connect(pinRootButton_, &QPushButton::clicked, this, &PinloomPanel::toggleSelectedLibraryRootPin);
    connect(openButton_, &QPushButton::clicked, this, &PinloomPanel::openSelectedResource);
    connect(addAliasButton_, &QPushButton::clicked, this, &PinloomPanel::promptAddAlias);
    connect(addAnchorButton_, &QPushButton::clicked, this, &PinloomPanel::promptAddManualAnchor);
    connect(pinButton_, &QPushButton::clicked, this, &PinloomPanel::toggleSelectedResourcePin);
    connect(manageButton, &QPushButton::toggled, this, [this](bool checked) {
        rootControlsWidget_->setVisible(checked);
        rootList_->setVisible(checked);
    });
    connect(searchEdit_, &QLineEdit::textChanged, this, &PinloomPanel::refreshResults);
    connect(searchEdit_, &QLineEdit::returnPressed, this, &PinloomPanel::openSelectedResource);
    connect(resultList_, &QListWidget::currentItemChanged, this, &PinloomPanel::refreshRelationSummary);
    connect(resultList_, &QListWidget::currentItemChanged, this, &PinloomPanel::refreshPinButtonState);
    connect(resultList_, &QListWidget::currentItemChanged, this, &PinloomPanel::notifyCurrentOpenTargetChanged);
    connect(resultList_, &QListWidget::itemDoubleClicked, this, &PinloomPanel::openResultItem);
    connect(rootList_, &QListWidget::currentItemChanged, this, &PinloomPanel::refreshRootPinButtonState);
    connect(rootList_, &QListWidget::currentItemChanged, this, &PinloomPanel::notifyCurrentLibraryRootChanged);

    rootControlsWidget_->setVisible(options_.showLibraryRootControls);
    rootList_->setVisible(options_.showLibraryRootControls);
    addAliasButton_->setVisible(options_.showManualEditControls);
    addAnchorButton_->setVisible(options_.showManualEditControls);
    pinButton_->setVisible(options_.showPinControls);
    pinRootButton_->setVisible(options_.showPinControls);

    loadLibraryRoots();
    refreshResults();
    refreshRootPinButtonState();
    focusSearch();

    auto installShortcut = [this](const QKeySequence &sequence, const QObject *receiver, const char *member) {
        auto *shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, SIGNAL(activated()), receiver, member);
    };
    installShortcut(QKeySequence(Qt::Key_Return), this, SLOT(openSelectedResource()));
    installShortcut(QKeySequence(Qt::Key_Enter), this, SLOT(openSelectedResource()));
    installShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this, SLOT(triggerCaptureCurrentAppPosition()));
    installShortcut(QKeySequence(QStringLiteral("Alt+A")), this, SLOT(addSearchTextAsAlias()));
    installShortcut(QKeySequence(QStringLiteral("Alt+T")), this, SLOT(addSearchTextAsTag()));
    installShortcut(QKeySequence(QStringLiteral("Ctrl+E")), this, SLOT(promptEditAnchor()));
    installShortcut(QKeySequence(Qt::Key_Delete), this, SLOT(promptDeleteSelectedAnchor()));
}

void PinloomPanel::setSearchText(const QString &text)
{
    searchEdit_->setText(text);
}

QString PinloomPanel::searchText() const
{
    return searchEdit_->text();
}

void PinloomPanel::focusSearch()
{
    searchEdit_->setFocus(Qt::ShortcutFocusReason);
    searchEdit_->selectAll();
}

bool PinloomPanel::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == searchEdit_ || watched == resultList_) && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const Qt::KeyboardModifiers modifiers =
            keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
        const int key = keyEvent->key();

        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && modifiers == Qt::NoModifier) {
            openSelectedResource();
            return true;
        }
        if (key == Qt::Key_K && modifiers == Qt::ControlModifier) {
            triggerCaptureCurrentAppPosition();
            return true;
        }
        if (key == Qt::Key_A && modifiers == Qt::AltModifier) {
            addSearchTextAsAlias();
            return true;
        }
        if (key == Qt::Key_T && modifiers == Qt::AltModifier) {
            addSearchTextAsTag();
            return true;
        }
        if (key == Qt::Key_E && modifiers == Qt::ControlModifier) {
            promptEditAnchor();
            return true;
        }
        if (key == Qt::Key_Delete && modifiers == Qt::NoModifier) {
            promptDeleteSelectedAnchor();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void PinloomPanel::setRequiredTags(const QStringList &tags)
{
    requiredTags_ = tags;
    refreshResults();
}

QStringList PinloomPanel::requiredTags() const
{
    return requiredTags_;
}

void PinloomPanel::setRequiredLocationPrefixes(const QStringList &prefixes)
{
    requiredLocationPrefixes_ = prefixes;
    refreshResults();
}

QStringList PinloomPanel::requiredLocationPrefixes() const
{
    return requiredLocationPrefixes_;
}

void PinloomPanel::setRequiredResourceKinds(const QList<ResourceKind> &kinds)
{
    requiredResourceKinds_ = kinds;
    refreshResults();
}

QList<ResourceKind> PinloomPanel::requiredResourceKinds() const
{
    return requiredResourceKinds_;
}

void PinloomPanel::setContextTags(const QStringList &tags)
{
    contextTags_ = tags;
    refreshResults();
}

QStringList PinloomPanel::contextTags() const
{
    return contextTags_;
}

void PinloomPanel::setContextLocationPrefixes(const QStringList &prefixes)
{
    contextLocationPrefixes_ = prefixes;
    refreshResults();
}

QStringList PinloomPanel::contextLocationPrefixes() const
{
    return contextLocationPrefixes_;
}

void PinloomPanel::setContextResourceIds(const QStringList &resourceIds)
{
    contextResourceIds_ = resourceIds;
    refreshResults();
}

QStringList PinloomPanel::contextResourceIds() const
{
    return contextResourceIds_;
}

void PinloomPanel::setContextRelationLabels(const QStringList &labels)
{
    contextRelationLabels_ = labels;
    refreshResults();
}

QStringList PinloomPanel::contextRelationLabels() const
{
    return contextRelationLabels_;
}

void PinloomPanel::applyHostContext(const PinloomHostContext &context)
{
    {
        const QSignalBlocker blocker(searchEdit_);
        searchEdit_->setText(context.searchText);
    }
    requiredTags_ = context.requiredTags;
    requiredLocationPrefixes_ = context.requiredLocationPrefixes;
    requiredResourceKinds_ = context.requiredResourceKinds;
    contextTags_ = context.contextTags;
    contextLocationPrefixes_ = context.contextLocationPrefixes;
    contextResourceIds_ = context.contextResourceIds;
    contextRelationLabels_ = context.contextRelationLabels;
    refreshResults();
}

PinloomHostContext PinloomPanel::hostContext() const
{
    PinloomHostContext context;
    context.searchText = searchText();
    context.requiredTags = requiredTags_;
    context.requiredLocationPrefixes = requiredLocationPrefixes_;
    context.requiredResourceKinds = requiredResourceKinds_;
    context.contextTags = contextTags_;
    context.contextLocationPrefixes = contextLocationPrefixes_;
    context.contextResourceIds = contextResourceIds_;
    context.contextRelationLabels = contextRelationLabels_;
    return context;
}

PinloomOpenTarget PinloomPanel::currentOpenTarget() const
{
    return openTargetForItem(resultList_->currentItem(), resultList_->currentRow());
}

PinloomOpenTarget PinloomPanel::openTargetForResourceId(const QString &resourceId) const
{
    if (resourceId.isEmpty()) {
        return {};
    }

    const std::optional<Resource> resource = repository_.findResource(resourceId);
    if (!resource.has_value()) {
        return {};
    }
    return openTargetForResource(resource.value());
}

PinloomOpenTarget PinloomPanel::resultAt(int row) const
{
    if (row < 0 || row >= resultList_->count()) {
        return {};
    }
    return openTargetForItem(resultList_->item(row), row);
}

QList<PinloomOpenTarget> PinloomPanel::currentResults() const
{
    QList<PinloomOpenTarget> targets;
    targets.reserve(resultList_->count());
    for (int row = 0; row < resultList_->count(); ++row) {
        targets.append(openTargetForItem(resultList_->item(row), row));
    }
    return targets;
}

QList<PinloomRelatedTarget> PinloomPanel::currentRelatedTargets() const
{
    return relatedTargetsForResource(selectedResultResourceId());
}

QList<PinloomRelatedTarget> PinloomPanel::relatedTargetsForResource(const QString &resourceId) const
{
    QList<PinloomRelatedTarget> targets;
    if (resourceId.isEmpty()) {
        return targets;
    }
    for (const ResourceRelation &relation : repository_.resourceRelations(resourceId)) {
        const bool currentIsSource = relation.sourceResourceId == resourceId;
        const QString otherResourceId = currentIsSource ? relation.targetResourceId : relation.sourceResourceId;

        PinloomRelatedTarget related;
        related.relationLabel = relation.label;
        related.relationNote = relation.note;
        related.currentIsSource = currentIsSource;

        const std::optional<Resource> resource = repository_.findResource(otherResourceId);
        if (resource.has_value()) {
            related.target = openTargetForResource(resource.value());
        } else {
            related.target.resourceId = otherResourceId;
            related.target.title = otherResourceId;
        }
        targets.append(related);
    }

    return targets;
}

bool PinloomPanel::upsertResourceRelation(const QString &sourceResourceId,
                                          const QString &targetResourceId,
                                          const QString &label,
                                          const QString &note)
{
    const QString trimmedLabel = label.trimmed();
    if (sourceResourceId.isEmpty() || targetResourceId.isEmpty() || trimmedLabel.isEmpty()) {
        updateStatus(tr("Select related resources and enter a relation label"));
        return false;
    }

    if (!repository_.findResource(sourceResourceId).has_value()
        || !repository_.findResource(targetResourceId).has_value()) {
        refreshResults();
        updateStatus(tr("Related resource no longer exists"));
        return false;
    }

    ResourceRelation relation;
    relation.sourceResourceId = sourceResourceId;
    relation.targetResourceId = targetResourceId;
    relation.label = trimmedLabel;
    relation.note = note.trimmed();
    if (!repository_.upsertResourceRelation(relation)) {
        updateStatus(tr("Unable to save resource relation"));
        return false;
    }

    const QString previousResourceId = selectedResultResourceId();
    refreshResults();
    if (!previousResourceId.isEmpty()) {
        selectResultResource(previousResourceId);
    }
    if (selectedResultResourceId().isEmpty()) {
        selectResultResource(sourceResourceId);
    }
    refreshRelationSummary();
    updateStatus(tr("Saved resource relation"));
    return true;
}

bool PinloomPanel::removeResourceRelation(const QString &sourceResourceId,
                                          const QString &targetResourceId,
                                          const QString &label)
{
    const QString trimmedLabel = label.trimmed();
    if (sourceResourceId.isEmpty() || targetResourceId.isEmpty() || trimmedLabel.isEmpty()) {
        updateStatus(tr("Select related resources and enter a relation label"));
        return false;
    }

    if (!repository_.removeResourceRelation(sourceResourceId, targetResourceId, trimmedLabel)) {
        updateStatus(tr("Unable to remove resource relation"));
        return false;
    }

    const QString previousResourceId = selectedResultResourceId();
    refreshResults();
    if (!previousResourceId.isEmpty()) {
        selectResultResource(previousResourceId);
    }
    refreshRelationSummary();
    updateStatus(tr("Removed resource relation"));
    return true;
}

PinloomLibraryRootTarget PinloomPanel::selectedLibraryRoot() const
{
    const QString id = selectedRootId();
    if (id.isEmpty()) {
        return {};
    }

    const QList<LibraryRoot> roots = repository_.libraryRoots();
    for (int row = 0; row < roots.size(); ++row) {
        if (roots.at(row).id == id) {
            return libraryRootTargetForRoot(roots.at(row), row);
        }
    }
    return {};
}

QList<PinloomLibraryRootTarget> PinloomPanel::libraryRoots() const
{
    QList<PinloomLibraryRootTarget> targets;
    const QList<LibraryRoot> roots = repository_.libraryRoots();
    targets.reserve(roots.size());
    for (int row = 0; row < roots.size(); ++row) {
        targets.append(libraryRootTargetForRoot(roots.at(row), row));
    }
    return targets;
}

int PinloomPanel::resultCount() const
{
    return resultList_->count();
}

bool PinloomPanel::selectResultAt(int row)
{
    if (row < 0 || row >= resultList_->count()) {
        return false;
    }
    resultList_->setCurrentRow(row);
    return resultList_->currentItem() != nullptr;
}

bool PinloomPanel::selectResultResource(const QString &resourceId)
{
    if (resourceId.isEmpty()) {
        return false;
    }
    for (int row = 0; row < resultList_->count(); ++row) {
        QListWidgetItem *item = resultList_->item(row);
        if (item->data(Qt::UserRole).toString() == resourceId) {
            resultList_->setCurrentItem(item);
            return true;
        }
    }
    return false;
}

bool PinloomPanel::selectLibraryRootById(const QString &id)
{
    if (id.isEmpty()) {
        return false;
    }
    for (int row = 0; row < rootList_->count(); ++row) {
        QListWidgetItem *item = rootList_->item(row);
        if (item->data(Qt::UserRole).toString() == id) {
            rootList_->setCurrentItem(item);
            return rootList_->currentItem() == item;
        }
    }
    return false;
}

bool PinloomPanel::selectFirstResult()
{
    return selectResultAt(0);
}

bool PinloomPanel::selectNextResult()
{
    const int count = resultList_->count();
    if (count <= 0) {
        return false;
    }
    const int currentRow = resultList_->currentRow();
    const int nextRow = currentRow < 0 ? 0 : std::min(currentRow + 1, count - 1);
    return selectResultAt(nextRow);
}

bool PinloomPanel::selectPreviousResult()
{
    const int count = resultList_->count();
    if (count <= 0) {
        return false;
    }
    const int currentRow = resultList_->currentRow();
    const int previousRow = currentRow < 0 ? 0 : std::max(currentRow - 1, 0);
    return selectResultAt(previousRow);
}

void PinloomPanel::setRemoteWebFetchingEnabled(bool enabled)
{
    if (fetchRemoteWebPagesCheck_) {
        fetchRemoteWebPagesCheck_->setChecked(enabled);
    }
}

bool PinloomPanel::remoteWebFetchingEnabled() const
{
    return fetchRemoteWebPagesCheck_ && fetchRemoteWebPagesCheck_->isChecked();
}

QString PinloomPanel::statusText() const
{
    return statusText_;
}

PinloomIndexingResult PinloomPanel::lastIndexingResult() const
{
    return lastIndexingResult_;
}

PinloomIndexingResult PinloomPanel::indexSelectedLibraryRoot()
{
    return indexLibraryRootById(selectedRootId());
}

PinloomIndexingResult PinloomPanel::indexLibraryRootById(const QString &id)
{
    if (id.isEmpty()) {
        const QString error = tr("No library folder selected");
        updateStatus(error);
        return finishIndexingResult({false, 0, error});
    }

    const std::optional<LibraryRoot> root = repository_.findLibraryRoot(id);
    if (!root.has_value()) {
        const QString error = tr("Library folder no longer exists");
        loadLibraryRoots();
        updateStatus(error);
        return finishIndexingResult({false, 0, error});
    }

    IndexingService indexer(repository_);
    configureIndexingService(indexer);
    if (!indexer.indexRoot(root.value())) {
        const QString error = indexer.lastError();
        updateStatus(error);
        return finishIndexingResult({false, indexer.lastIndexedCount(), error});
    }

    loadLibraryRoots();
    selectLibraryRootById(id);
    refreshResults();
    updateStatus(tr("Indexed %n resource(s)", nullptr, indexer.lastIndexedCount()));
    return finishIndexingResult({true, indexer.lastIndexedCount(), {}});
}

PinloomIndexingResult PinloomPanel::indexAllEnabledLibraryRoots()
{
    IndexingService indexer(repository_);
    configureIndexingService(indexer);
    if (!indexer.indexEnabledRoots()) {
        const QString error = indexer.lastError();
        updateStatus(error);
        return finishIndexingResult({false, indexer.lastIndexedCount(), error});
    }

    loadLibraryRoots();
    refreshResults();
    updateStatus(tr("Indexed %n resource(s)", nullptr, indexer.lastIndexedCount()));
    return finishIndexingResult({true, indexer.lastIndexedCount(), {}});
}

PinloomIndexingResult PinloomPanel::rebuildAllEnabledLibraryRoots()
{
    IndexingService indexer(repository_);
    configureIndexingService(indexer);
    if (!indexer.rebuildEnabledRoots()) {
        const QString error = indexer.lastError();
        updateStatus(error);
        return finishIndexingResult({false, indexer.lastIndexedCount(), error});
    }

    loadLibraryRoots();
    refreshResults();
    updateStatus(tr("Rebuilt %n resource(s)", nullptr, indexer.lastIndexedCount()));
    return finishIndexingResult({true, indexer.lastIndexedCount(), {}});
}

bool PinloomPanel::captureCurrentAppPosition()
{
    if (options_.manualExcelAnchorRequestProvider) {
        const std::optional<ManualExcelAnchorCreationRequest> excelRequest =
            options_.manualExcelAnchorRequestProvider();
        if (!excelRequest.has_value()) {
            updateStatus(tr("Capture canceled"));
            return false;
        }

        ManualExcelAnchorCreationService creationService(repository_);
        const ManualExcelAnchorCreationResult result =
            creationService.createManualExcelAnchor(excelRequest.value());
        if (!result.success()) {
            updateStatus(result.error);
            return false;
        }

        refreshResults();
        if (!selectResultResource(result.resource.id)) {
            setSearchText(result.anchor.name);
            selectResultResource(result.resource.id);
        }
        updateStatus(tr("Created Excel anchor \"%1\"").arg(result.anchor.name));
        return true;
    }

    if (options_.manualVisioAnchorRequestProvider) {
        const std::optional<ManualVisioAnchorCreationRequest> visioRequest =
            options_.manualVisioAnchorRequestProvider();
        if (!visioRequest.has_value()) {
            updateStatus(tr("Capture canceled"));
            return false;
        }

        ManualVisioAnchorCreationService creationService(repository_);
        const ManualVisioAnchorCreationResult result =
            creationService.createManualVisioAnchor(visioRequest.value());
        if (!result.success()) {
            updateStatus(result.error);
            return false;
        }

        refreshResults();
        if (!selectResultResource(result.resource.id)) {
            setSearchText(result.anchor.name);
            selectResultResource(result.resource.id);
        }
        updateStatus(tr("Created Visio anchor \"%1\"").arg(result.anchor.name));
        return true;
    }

    if (options_.manualWordAnchorRequestProvider) {
        const std::optional<ManualWordAnchorCreationRequest> wordRequest =
            options_.manualWordAnchorRequestProvider();
        if (!wordRequest.has_value()) {
            updateStatus(tr("Capture canceled"));
            return false;
        }

        ManualWordAnchorCreationService creationService(repository_);
        const ManualWordAnchorCreationResult result =
            creationService.createManualWordBookmarkAnchor(wordRequest.value());
        if (!result.success()) {
            updateStatus(result.error);
            return false;
        }

        refreshResults();
        if (!selectResultResource(result.resource.id)) {
            setSearchText(result.anchor.name);
            selectResultResource(result.resource.id);
        }
        updateStatus(tr("Created Word anchor \"%1\"").arg(result.anchor.name));
        return true;
    }

    if (options_.manualPowerPointAnchorRequestProvider) {
        const std::optional<ManualPowerPointAnchorCreationRequest> powerPointRequest =
            options_.manualPowerPointAnchorRequestProvider();
        if (!powerPointRequest.has_value()) {
            updateStatus(tr("Capture canceled"));
            return false;
        }

        ManualPowerPointAnchorCreationService creationService(repository_);
        const ManualPowerPointAnchorCreationResult result =
            creationService.createManualPowerPointShapeAnchor(powerPointRequest.value());
        if (!result.success()) {
            updateStatus(result.error);
            return false;
        }

        refreshResults();
        if (!selectResultResource(result.resource.id)) {
            setSearchText(result.anchor.name);
            selectResultResource(result.resource.id);
        }
        updateStatus(tr("Created PowerPoint anchor \"%1\"").arg(result.anchor.name));
        return true;
    }

    std::optional<ManualPdfAnchorCreationRequest> request;
    if (options_.manualPdfAnchorRequestProvider) {
        request = options_.manualPdfAnchorRequestProvider();
    } else if (options_.manualPdfAnchorDialogHandler) {
        request = options_.manualPdfAnchorDialogHandler(this);
    } else {
        ManualPdfAnchorDialog dialog(this);
        updateStatus(tr("Creating PDF anchor"));
        if (dialog.exec() == QDialog::Accepted) {
            request = dialog.request();
        }
    }

    if (!request.has_value()) {
        updateStatus(tr("Capture canceled"));
        return false;
    }

    ManualPdfAnchorCreationService creationService(repository_);
    const ManualPdfAnchorCreationResult result =
        creationService.createManualPdfXChangeRectAnchor(request.value());
    if (!result.success()) {
        updateStatus(result.error);
        return false;
    }

    refreshResults();
    if (!selectResultResource(result.resource.id)) {
        setSearchText(result.anchor.name);
        selectResultResource(result.resource.id);
    }
    updateStatus(tr("Created PDF anchor \"%1\"").arg(result.anchor.name));
    return true;
}

bool PinloomPanel::addAliasToSelectedTarget(const QString &alias)
{
    const QString trimmedAlias = alias.trimmed();
    const PinloomOpenTarget target = currentOpenTarget();
    if (!target.anchor.has_value()) {
        return addAliasToResource(target.resourceId, trimmedAlias);
    }
    if (target.resourceId.isEmpty() || trimmedAlias.isEmpty()) {
        updateStatus(tr("Select an anchor and enter an alias"));
        return false;
    }

    std::optional<Resource> resource = repository_.findResource(target.resourceId);
    if (!resource.has_value()) {
        updateStatus(tr("Selected resource no longer exists"));
        refreshResults();
        return false;
    }

    const int index = anchorIndex(resource.value(), target.anchor.value());
    if (index < 0) {
        updateStatus(tr("Selected anchor no longer exists"));
        refreshResults();
        return false;
    }

    Anchor &anchor = resource->anchors[index];
    if (containsValueCaseInsensitive(anchor.aliases, trimmedAlias)) {
        updateStatus(tr("Anchor alias already exists"));
        return false;
    }

    anchor.aliases.append(trimmedAlias);
    anchor.updatedAt = QDateTime::currentDateTimeUtc();
    resource->updatedAt = anchor.updatedAt;
    if (!repository_.upsertResource(resource.value())) {
        updateStatus(tr("Unable to save anchor alias"));
        return false;
    }

    refreshResults();
    selectResultResource(target.resourceId);
    updateStatus(tr("Added anchor alias \"%1\"").arg(trimmedAlias));
    return true;
}

bool PinloomPanel::addAliasToSelectedResource(const QString &alias)
{
    return addAliasToResource(selectedResultResourceId(), alias);
}

bool PinloomPanel::addAliasToResource(const QString &resourceId, const QString &alias)
{
    const QString trimmedAlias = alias.trimmed();
    if (resourceId.isEmpty() || trimmedAlias.isEmpty()) {
        updateStatus(tr("Select a resource and enter an alias"));
        return false;
    }

    std::optional<Resource> resource = repository_.findResource(resourceId);
    if (!resource.has_value()) {
        updateStatus(tr("Selected resource no longer exists"));
        refreshResults();
        return false;
    }

    if (resource->aliases.contains(trimmedAlias, Qt::CaseInsensitive)) {
        updateStatus(tr("Alias already exists"));
        return false;
    }

    resource->aliases.append(trimmedAlias);
    resource->updatedAt = QDateTime::currentDateTimeUtc();
    if (!repository_.upsertResource(resource.value())) {
        updateStatus(tr("Unable to save alias"));
        return false;
    }

    refreshResults();
    selectResultResource(resourceId);
    updateStatus(tr("Added alias \"%1\"").arg(trimmedAlias));
    return true;
}

bool PinloomPanel::addTagToSelectedTarget(const QString &tag)
{
    const QString trimmedTag = cleanTag(tag);
    const PinloomOpenTarget target = currentOpenTarget();
    if (target.resourceId.isEmpty() || trimmedTag.isEmpty()) {
        updateStatus(tr("Select a result and enter a tag"));
        return false;
    }

    std::optional<Resource> resource = repository_.findResource(target.resourceId);
    if (!resource.has_value()) {
        updateStatus(tr("Selected resource no longer exists"));
        refreshResults();
        return false;
    }

    QDateTime updatedAt = QDateTime::currentDateTimeUtc();
    if (target.anchor.has_value()) {
        const int index = anchorIndex(resource.value(), target.anchor.value());
        if (index < 0) {
            updateStatus(tr("Selected anchor no longer exists"));
            refreshResults();
            return false;
        }

        Anchor &anchor = resource->anchors[index];
        if (containsValueCaseInsensitive(anchor.tags, trimmedTag)) {
            updateStatus(tr("Anchor tag already exists"));
            return false;
        }
        anchor.tags.append(trimmedTag);
        anchor.updatedAt = updatedAt;
    } else {
        if (containsValueCaseInsensitive(resource->tags, trimmedTag)) {
            updateStatus(tr("Tag already exists"));
            return false;
        }
        resource->tags.append(trimmedTag);
    }

    resource->updatedAt = updatedAt;
    if (!repository_.upsertResource(resource.value())) {
        updateStatus(tr("Unable to save tag"));
        return false;
    }

    refreshResults();
    selectResultResource(target.resourceId);
    updateStatus(tr("Added tag \"%1\"").arg(trimmedTag));
    return true;
}

bool PinloomPanel::editSelectedAnchor(const QString &name, const QStringList &aliases, const QStringList &tags)
{
    const PinloomOpenTarget target = currentOpenTarget();
    if (target.resourceId.isEmpty() || !target.anchor.has_value()) {
        updateStatus(tr("Select an anchor to edit"));
        return false;
    }

    std::optional<Resource> resource = repository_.findResource(target.resourceId);
    if (!resource.has_value()) {
        updateStatus(tr("Selected resource no longer exists"));
        refreshResults();
        return false;
    }

    const int index = anchorIndex(resource.value(), target.anchor.value());
    if (index < 0) {
        updateStatus(tr("Selected anchor no longer exists"));
        refreshResults();
        return false;
    }

    Anchor &anchor = resource->anchors[index];
    anchor.name = name.trimmed();
    anchor.aliases = cleanedValues(aliases);
    anchor.tags = cleanedValues(tags, true);
    anchor.updatedAt = QDateTime::currentDateTimeUtc();
    resource->updatedAt = anchor.updatedAt;
    if (!repository_.upsertResource(resource.value())) {
        updateStatus(tr("Unable to update anchor"));
        return false;
    }

    refreshResults();
    selectResultResource(target.resourceId);
    updateStatus(tr("Updated anchor \"%1\"").arg(anchorDisplayName(anchor, resource.value())));
    return true;
}

bool PinloomPanel::requestDeleteSelectedAnchor()
{
    const PinloomOpenTarget target = currentOpenTarget();
    if (target.resourceId.isEmpty() || !target.anchor.has_value()) {
        updateStatus(tr("Select an anchor to delete"));
        return false;
    }

    updateStatus(tr("Anchor deletion is not implemented yet; repository needs an anchor delete API"));
    return false;
}

bool PinloomPanel::addManualAnchorToSelectedResource(const QString &target, int line)
{
    return addManualAnchorToResource(selectedResultResourceId(), target, line);
}

bool PinloomPanel::addManualAnchorToResource(const QString &resourceId, const QString &target, int line)
{
    const QString trimmedTarget = target.trimmed();
    if (resourceId.isEmpty() || trimmedTarget.isEmpty()) {
        updateStatus(tr("Select a resource and enter an anchor"));
        return false;
    }

    std::optional<Resource> resource = repository_.findResource(resourceId);
    if (!resource.has_value()) {
        updateStatus(tr("Selected resource no longer exists"));
        refreshResults();
        return false;
    }

    if (normalizedResourceKind(resource->kind) == ResourceKind::Pdf) {
        updateStatus(tr("PDF line anchors are deprecated; use PDF-XChange anchor capture"));
        return false;
    }

    Anchor anchor;
    anchor.type = AnchorType::Manual;
    anchor.target = trimmedTarget;
    anchor.line = line > 0 ? line : -1;

    const auto isDuplicate = [&anchor](const Anchor &existing) {
        return existing.type == AnchorType::Manual
            && existing.target.compare(anchor.target, Qt::CaseInsensitive) == 0
            && existing.line == anchor.line;
    };
    if (std::any_of(resource->anchors.cbegin(), resource->anchors.cend(), isDuplicate)) {
        updateStatus(tr("Anchor already exists"));
        return false;
    }

    resource->anchors.append(anchor);
    resource->updatedAt = QDateTime::currentDateTimeUtc();
    if (!repository_.upsertResource(resource.value())) {
        updateStatus(tr("Unable to save anchor"));
        return false;
    }

    refreshResults();
    selectResultResource(resourceId);
    updateStatus(tr("Added anchor \"%1\"").arg(trimmedTarget));
    return true;
}

bool PinloomPanel::setSelectedResourcePinned(bool pinned)
{
    return setResourcePinnedById(selectedResultResourceId(), pinned);
}

bool PinloomPanel::setResourcePinnedById(const QString &resourceId, bool pinned)
{
    if (resourceId.isEmpty()) {
        updateStatus(tr("No resource selected"));
        refreshPinButtonState();
        return false;
    }

    if (!repository_.findResource(resourceId).has_value()) {
        refreshResults();
        updateStatus(tr("Resource no longer exists"));
        return false;
    }

    if (!repository_.setResourcePinned(resourceId, pinned)) {
        updateStatus(tr("Unable to update pinned resource"));
        refreshPinButtonState();
        return false;
    }

    refreshResults();
    selectResultResource(resourceId);
    updateStatus(pinned ? tr("Pinned resource") : tr("Unpinned resource"));
    return true;
}

bool PinloomPanel::setSelectedLibraryRootPinned(bool pinned)
{
    return setLibraryRootPinnedById(selectedRootId(), pinned);
}

bool PinloomPanel::setLibraryRootPinnedById(const QString &id, bool pinned)
{
    if (id.isEmpty()) {
        updateStatus(tr("No library folder selected"));
        refreshRootPinButtonState();
        return false;
    }

    if (!repository_.findLibraryRoot(id).has_value()) {
        loadLibraryRoots();
        updateStatus(tr("Library folder no longer exists"));
        return false;
    }

    if (!repository_.setLibraryRootPinned(id, pinned)) {
        updateStatus(tr("Unable to update pinned folder"));
        refreshRootPinButtonState();
        return false;
    }

    loadLibraryRoots();
    selectLibraryRootById(id);
    refreshResults();
    updateStatus(pinned ? tr("Pinned folder") : tr("Unpinned folder"));
    return true;
}

bool PinloomPanel::setSelectedLibraryRootEnabled(bool enabled)
{
    return setLibraryRootEnabledById(selectedRootId(), enabled);
}

bool PinloomPanel::setLibraryRootEnabledById(const QString &id, bool enabled)
{
    if (id.isEmpty()) {
        updateStatus(tr("No library folder selected"));
        refreshRootPinButtonState();
        return false;
    }

    if (!repository_.findLibraryRoot(id).has_value()) {
        loadLibraryRoots();
        updateStatus(tr("Library folder no longer exists"));
        return false;
    }

    if (!repository_.setLibraryRootEnabled(id, enabled)) {
        updateStatus(tr("Unable to update enabled folder"));
        refreshRootPinButtonState();
        return false;
    }

    loadLibraryRoots();
    selectLibraryRootById(id);
    updateStatus(enabled ? tr("Enabled folder") : tr("Disabled folder"));
    return true;
}

bool PinloomPanel::addLibraryRootPath(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        updateStatus(tr("No library folder path provided"));
        return false;
    }

    const LibraryRoot root = makeLibraryRootForPath(path);
    if (!repository_.upsertLibraryRoot(root)) {
        updateStatus(tr("Unable to save library folder"));
        return false;
    }

    loadLibraryRoots();
    selectLibraryRootById(root.id);
    updateStatus(tr("Added library folder"));
    return true;
}

bool PinloomPanel::removeSelectedLibraryRoot()
{
    return removeLibraryRootById(selectedRootId());
}

bool PinloomPanel::removeLibraryRootById(const QString &id)
{
    if (id.isEmpty()) {
        updateStatus(tr("No library folder selected"));
        return false;
    }

    if (!repository_.findLibraryRoot(id).has_value()) {
        loadLibraryRoots();
        updateStatus(tr("Library folder no longer exists"));
        return false;
    }

    if (!repository_.removeLibraryRoot(id)) {
        updateStatus(tr("Unable to remove library folder"));
        return false;
    }

    loadLibraryRoots();
    refreshResults();
    updateStatus(tr("Removed library folder; indexed resources were kept"));
    return true;
}

void PinloomPanel::addLibraryRoot()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("Add Library Folder"));
    if (path.isEmpty()) {
        return;
    }

    if (!addLibraryRootPath(path)) {
        QMessageBox::warning(this, tr("Add folder failed"), tr("Unable to save library folder."));
        return;
    }

    refreshSelectedRoot();
}

void PinloomPanel::refreshSelectedRoot()
{
    const PinloomIndexingResult result = indexSelectedLibraryRoot();
    if (!result.success && !result.error.isEmpty()) {
        QMessageBox::warning(this, tr("Indexing failed"), result.error);
    }
}

void PinloomPanel::refreshAllRoots()
{
    const PinloomIndexingResult result = indexAllEnabledLibraryRoots();
    if (!result.success && !result.error.isEmpty()) {
        QMessageBox::warning(this, tr("Indexing failed"), result.error);
    }
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

    const PinloomIndexingResult result = rebuildAllEnabledLibraryRoots();
    if (!result.success && !result.error.isEmpty()) {
        QMessageBox::warning(this, tr("Rebuild failed"), result.error);
    }
}

void PinloomPanel::toggleSelectedLibraryRootPin()
{
    const QString id = selectedRootId();
    if (id.isEmpty()) {
        setLibraryRootPinnedById(id, true);
        return;
    }

    const std::optional<LibraryRoot> root = repository_.findLibraryRoot(id);
    if (!root.has_value()) {
        setLibraryRootPinnedById(id, true);
        return;
    }

    setLibraryRootPinnedById(id, !root->pinned);
}

void PinloomPanel::refreshResults()
{
    const PinloomOpenTarget previousTarget = currentOpenTarget();
    resultList_->clear();

    SearchQuery query;
    query.text = searchEdit_->text();
    query.requiredTags = requiredTags_;
    query.requiredLocationPrefixes = requiredLocationPrefixes_;
    query.requiredKinds = requiredResourceKinds_;
    query.contextTags = contextTags_;
    query.contextLocationPrefixes = contextLocationPrefixes_;
    query.contextResourceIds = contextResourceIds_;
    query.contextRelationLabels = contextRelationLabels_;
    query.limit = 100;

    const QList<SearchResult> results = repository_.search(query);
    for (const SearchResult &result : results) {
        auto *item = new QListWidgetItem(resultText(result), resultList_);
        item->setToolTip(resultToolTip(result, query));
        item->setSizeHint(QSize(0, resultList_->fontMetrics().lineSpacing() * 2 + 12));
        item->setData(Qt::UserRole, result.resource.id);
        item->setData(Qt::UserRole + 1, result.resource.location);
        item->setData(Qt::UserRole + 11, result.resource.title);
        item->setData(Qt::UserRole + 12, static_cast<int>(result.resource.kind));
        item->setData(Qt::UserRole + 13, result.matchedField);
        item->setData(Qt::UserRole + 14, result.score);
        item->setData(Qt::UserRole + 15, matchedContextTag(result.resource, query.contextTags));
        item->setData(Qt::UserRole + 16, matchedContextLocationPrefix(result.resource, query.contextLocationPrefixes));
        item->setData(Qt::UserRole + 17, result.matchedContextResourceId);
        item->setData(Qt::UserRole + 18, result.matchedContextRelationLabel);
        item->setData(Qt::UserRole + 19, result.matchedContextRelationNote);
        item->setData(Qt::UserRole + 20, resultMatchSummary(result, query));
        if (result.matchedAnchor.has_value()) {
            const Anchor &anchor = result.matchedAnchor.value();
            item->setData(Qt::UserRole + 2, true);
            item->setData(Qt::UserRole + 3, anchor.line);
            item->setData(Qt::UserRole + 4, anchor.target);
            item->setData(Qt::UserRole + 5, static_cast<int>(anchor.type));
            item->setData(Qt::UserRole + 6, anchor.page);
            item->setData(Qt::UserRole + 7, anchor.region.x());
            item->setData(Qt::UserRole + 8, anchor.region.y());
            item->setData(Qt::UserRole + 9, anchor.region.width());
            item->setData(Qt::UserRole + 10, anchor.region.height());
            item->setData(Qt::UserRole + 21, anchor.id);
            item->setData(Qt::UserRole + 22, anchor.name);
            item->setData(Qt::UserRole + 23, anchor.targetApp);
            item->setData(Qt::UserRole + 24, anchor.targetFile);
            item->setData(Qt::UserRole + 25, anchor.targetUri);
            item->setData(Qt::UserRole + 26, anchor.locatorType);
            item->setData(Qt::UserRole + 27, anchor.locatorJson);
            item->setData(Qt::UserRole + 28, anchor.aliases);
            item->setData(Qt::UserRole + 29, anchor.tags);
            item->setData(Qt::UserRole + 30, anchor.pinned);
            item->setData(Qt::UserRole + 31, anchor.createdAt);
            item->setData(Qt::UserRole + 32, anchor.updatedAt);
            item->setData(Qt::UserRole + 33, anchor.usedAt);
        }
    }

    bool restoredSelection = false;
    if (!previousTarget.resourceId.isEmpty()) {
        for (int row = 0; row < resultList_->count(); ++row) {
            QListWidgetItem *item = resultList_->item(row);
            if (item->data(Qt::UserRole).toString() != previousTarget.resourceId) {
                continue;
            }
            const QString itemAnchorId = item->data(Qt::UserRole + 21).toString();
            const QString previousAnchorId = previousTarget.anchor.has_value() ? previousTarget.anchor->id : QString();
            if (!previousAnchorId.isEmpty() && itemAnchorId != previousAnchorId) {
                continue;
            }
            resultList_->setCurrentItem(item);
            restoredSelection = true;
            break;
        }
    }
    if (!restoredSelection && resultList_->count() > 0) {
        resultList_->setCurrentRow(0);
    }

    updateStatus(tr("%n result(s)", nullptr, results.size()));
    refreshRelationSummary();
    refreshPinButtonState();
    notifyResultCountChanged();
    notifyResultsChanged();
}

void PinloomPanel::refreshRelationSummary()
{
    const QListWidgetItem *item = resultList_->currentItem();
    if (!item) {
        relationLabel_->clear();
        return;
    }

    const QString resourceId = item->data(Qt::UserRole).toString();
    const QList<ResourceRelation> relations = repository_.resourceRelations(resourceId);
    if (relations.isEmpty()) {
        relationLabel_->clear();
        return;
    }

    QStringList relationLines;
    const int visibleRelationCount = std::min(static_cast<int>(relations.size()), 3);
    for (int i = 0; i < visibleRelationCount; ++i) {
        relationLines.append(relationText(relations.at(i), resourceId, repository_));
    }
    if (relations.size() > visibleRelationCount) {
        relationLines.append(tr("+%n more relation(s)", nullptr, relations.size() - visibleRelationCount));
    }

    relationLabel_->setText(tr("Related: %1").arg(relationLines.join(QStringLiteral("; "))));
}

bool PinloomPanel::activateCurrentOpenTarget()
{
    const PinloomOpenTarget target = currentOpenTarget();
    return activateOpenTarget(target);
}

bool PinloomPanel::activateResourceById(const QString &resourceId)
{
    if (resourceId.isEmpty()) {
        updateStatus(tr("No resource selected"));
        return false;
    }

    const PinloomOpenTarget target = openTargetForResourceId(resourceId);
    if (target.resourceId.isEmpty()) {
        updateStatus(tr("Resource no longer exists"));
        return false;
    }
    return activateOpenTarget(target);
}

bool PinloomPanel::activateOpenTarget(const PinloomOpenTarget &target)
{
    if (target.location.isEmpty()) {
        updateStatus(tr("No resource selected"));
        return false;
    }

    const auto recordOpen = [this, &target]() {
        if (!target.resourceId.isEmpty()) {
            repository_.recordResourceOpen(target.resourceId);
            if (target.anchor.has_value()) {
                repository_.recordAnchorOpen(target.resourceId, target.anchor.value());
            }
        }
    };

    if (isDeprecatedPdfManualLineOpenTarget(target)) {
        updateStatus(tr("Legacy PDF line anchor is deprecated; delete it and recreate a PDF-XChange anchor"));
        return false;
    }

    if (tryHostOpenTarget(target)) {
        recordOpen();
        return true;
    }

    if (target.anchor.has_value() && isExcelAnchor(target.anchor.value())) {
        if (activateExcelTarget(target)) {
            recordOpen();
            return true;
        }
        return false;
    }

    if (target.anchor.has_value() && isVisioAnchor(target.anchor.value())) {
        if (activateVisioTarget(target)) {
            recordOpen();
            return true;
        }
        return false;
    }

    if (target.anchor.has_value() && isWordAnchor(target.anchor.value())) {
        if (activateWordTarget(target)) {
            recordOpen();
            return true;
        }
        return false;
    }

    if (target.anchor.has_value() && isPowerPointAnchor(target.anchor.value())) {
        if (activatePowerPointTarget(target)) {
            recordOpen();
            return true;
        }
        return false;
    }

    if (target.anchor.has_value() && isPdfXChangeAnchor(target.anchor.value())) {
        if (activatePdfXChangeTarget(target)) {
            recordOpen();
            return true;
        }
        return false;
    }

    if (target.anchor.has_value() && target.anchor->line > 0) {
        TextPreviewDialog preview(target.location, target.anchor->line, this);
        if (!preview.load()) {
            updateStatus(tr("Unable to preview %1").arg(target.location));
            return false;
        }
        recordOpen();
        preview.exec();
        return true;
    }

    QUrl targetUrl = urlForLocation(target.location);
    if (target.anchor.has_value()
        && (target.anchor->type == AnchorType::PdfPage || target.anchor->type == AnchorType::PdfRegion)) {
        targetUrl.setFragment(pdfFragmentForAnchor(target.anchor.value()));
    } else if (target.anchor.has_value() && target.anchor->type == AnchorType::UrlFragment) {
        targetUrl.setFragment(target.anchor->target);
    }

    if (QDesktopServices::openUrl(targetUrl)) {
        recordOpen();
        return true;
    } else {
        updateStatus(tr("Unable to open %1").arg(target.location));
    }
    return false;
}

bool PinloomPanel::activateExcelTarget(const PinloomOpenTarget &target)
{
    if (!target.anchor.has_value()) {
        updateStatus(tr("No Excel anchor selected"));
        return false;
    }

    const ExcelJumpCommandResult buildResult =
        buildExcelJumpCommand(target.anchor.value(), target.location, options_.applicationLaunchSettings);
    if (!buildResult.success()) {
        updateStatus(buildResult.error);
        return false;
    }

    if (options_.excelLaunchHandler) {
        QString error;
        if (!options_.excelLaunchHandler(buildResult.command, &error)) {
            updateStatus(error.trimmed().isEmpty()
                             ? tr("Unable to launch Excel")
                             : error.trimmed());
            return false;
        }
        updateStatus(tr("Opened Excel target"));
        return true;
    }

#ifndef Q_OS_WIN
    updateStatus(tr("Excel jump requires Windows COM automation"));
    return false;
#else
    if (!isExcelComAutomationAvailable()) {
        updateStatus(tr("Microsoft Excel COM automation is not available"));
        return false;
    }

    if (!QProcess::startDetached(buildResult.command.executablePath, buildResult.command.arguments)) {
        updateStatus(tr("Unable to launch Excel"));
        return false;
    }

    updateStatus(tr("Opened Excel target"));
    return true;
#endif
}

bool PinloomPanel::activateVisioTarget(const PinloomOpenTarget &target)
{
    if (!target.anchor.has_value()) {
        updateStatus(tr("No Visio anchor selected"));
        return false;
    }

    const VisioJumpCommandResult buildResult =
        buildVisioJumpCommand(target.anchor.value(), target.location, options_.applicationLaunchSettings);
    if (!buildResult.success()) {
        updateStatus(buildResult.error);
        return false;
    }

    if (options_.visioLaunchHandler) {
        QString error;
        if (!options_.visioLaunchHandler(buildResult.command, &error)) {
            updateStatus(error.trimmed().isEmpty()
                             ? tr("Unable to launch Visio")
                             : error.trimmed());
            return false;
        }
        updateStatus(tr("Opened Visio target"));
        return true;
    }

#ifndef Q_OS_WIN
    updateStatus(tr("Visio jump requires Windows COM automation"));
    return false;
#else
    if (!isVisioComAutomationAvailable()) {
        updateStatus(tr("Microsoft Visio COM automation is not available"));
        return false;
    }

    if (!QProcess::startDetached(buildResult.command.executablePath, buildResult.command.arguments)) {
        updateStatus(tr("Unable to launch Visio"));
        return false;
    }

    updateStatus(tr("Opened Visio target"));
    return true;
#endif
}

bool PinloomPanel::activateWordTarget(const PinloomOpenTarget &target)
{
    if (!target.anchor.has_value()) {
        updateStatus(tr("No Word anchor selected"));
        return false;
    }

    const WordJumpCommandResult buildResult =
        buildWordJumpCommand(target.anchor.value(), target.location, options_.applicationLaunchSettings);
    if (!buildResult.success()) {
        updateStatus(buildResult.error);
        return false;
    }

    if (options_.wordLaunchHandler) {
        QString error;
        if (!options_.wordLaunchHandler(buildResult.command, &error)) {
            updateStatus(error.trimmed().isEmpty()
                             ? tr("Unable to launch Word")
                             : error.trimmed());
            return false;
        }
        updateStatus(tr("Opened Word target"));
        return true;
    }

#ifndef Q_OS_WIN
    updateStatus(tr("Word jump requires Windows COM automation"));
    return false;
#else
    if (!isWordComAutomationAvailable()) {
        updateStatus(tr("Microsoft Word COM automation is not available"));
        return false;
    }

    if (!QProcess::startDetached(buildResult.command.executablePath, buildResult.command.arguments)) {
        updateStatus(tr("Unable to launch Word"));
        return false;
    }

    updateStatus(tr("Opened Word target"));
    return true;
#endif
}

bool PinloomPanel::activatePowerPointTarget(const PinloomOpenTarget &target)
{
    if (!target.anchor.has_value()) {
        updateStatus(tr("No PowerPoint anchor selected"));
        return false;
    }

    const PowerPointJumpCommandResult buildResult =
        buildPowerPointJumpCommand(target.anchor.value(), target.location, options_.applicationLaunchSettings);
    if (!buildResult.success()) {
        updateStatus(buildResult.error);
        return false;
    }

    if (options_.powerPointLaunchHandler) {
        QString error;
        if (!options_.powerPointLaunchHandler(buildResult.command, &error)) {
            updateStatus(error.trimmed().isEmpty()
                             ? tr("Unable to launch PowerPoint")
                             : error.trimmed());
            return false;
        }
        updateStatus(tr("Opened PowerPoint target"));
        return true;
    }

#ifndef Q_OS_WIN
    updateStatus(tr("PowerPoint jump requires Windows COM automation"));
    return false;
#else
    if (!isPowerPointComAutomationAvailable()) {
        updateStatus(tr("Microsoft PowerPoint COM automation is not available"));
        return false;
    }

    if (!QProcess::startDetached(buildResult.command.executablePath, buildResult.command.arguments)) {
        updateStatus(tr("Unable to launch PowerPoint"));
        return false;
    }

    updateStatus(tr("Opened PowerPoint target"));
    return true;
#endif
}

bool PinloomPanel::activatePdfXChangeTarget(const PinloomOpenTarget &target)
{
    if (!target.anchor.has_value()) {
        updateStatus(tr("No PDF-XChange anchor selected"));
        return false;
    }

    const PdfXChangeCommandResult buildResult = options_.pdfXChangeExecutablePathProvider
        ? buildPdfXChangeCommand(target.anchor.value(),
                                 target.location,
                                 options_.pdfXChangeExecutablePathProvider().trimmed())
        : buildPdfXChangeCommand(target.anchor.value(),
                                 target.location,
                                 options_.applicationLaunchSettings);
    if (!buildResult.success()) {
        updateStatus(buildResult.error);
        return false;
    }

    if (options_.pdfXChangeLaunchHandler) {
        QString error;
        if (!options_.pdfXChangeLaunchHandler(buildResult.command, &error)) {
            updateStatus(error.trimmed().isEmpty()
                             ? tr("Unable to launch PDF-XChange")
                             : error.trimmed());
            return false;
        }
        updateStatus(tr("Opened PDF-XChange target"));
        return true;
    }

    const QFileInfo executable(buildResult.command.executablePath);
    if (!executable.exists() || !executable.isFile()) {
        updateStatus(tr("PDF-XChange executable is not configured/found"));
        return false;
    }

    if (!QProcess::startDetached(buildResult.command.executablePath, buildResult.command.arguments)) {
        updateStatus(tr("Unable to launch PDF-XChange"));
        return false;
    }

    updateStatus(tr("Opened PDF-XChange target"));
    return true;
}

void PinloomPanel::openSelectedResource()
{
    activateCurrentOpenTarget();
}

void PinloomPanel::openResultItem(QListWidgetItem *item)
{
    if (!item) {
        return;
    }

    resultList_->setCurrentItem(item);
    openSelectedResource();
}

void PinloomPanel::triggerCaptureCurrentAppPosition()
{
    captureCurrentAppPosition();
}

void PinloomPanel::addSearchTextAsAlias()
{
    const QString alias = searchEdit_->text().trimmed();
    if (alias.isEmpty()) {
        updateStatus(tr("Type an alias in the search box before pressing Alt+A"));
        return;
    }
    addAliasToSelectedTarget(alias);
}

void PinloomPanel::addSearchTextAsTag()
{
    const QString tag = cleanTag(searchEdit_->text());
    if (tag.isEmpty()) {
        updateStatus(tr("Type a tag in the search box before pressing Alt+T"));
        return;
    }
    addTagToSelectedTarget(tag);
}

void PinloomPanel::promptAddAlias()
{
    bool accepted = false;
    const QString alias = QInputDialog::getText(
        this,
        tr("Add Alias"),
        tr("Alias"),
        QLineEdit::Normal,
        QString(),
        &accepted);
    if (accepted) {
        addAliasToSelectedTarget(alias);
    }
}

void PinloomPanel::promptEditAnchor()
{
    const PinloomOpenTarget target = currentOpenTarget();
    if (target.resourceId.isEmpty() || !target.anchor.has_value()) {
        updateStatus(tr("Select an anchor to edit"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Edit Anchor"));
    auto *form = new QFormLayout(&dialog);
    auto *nameEdit = new QLineEdit(anchorDisplayName(target.anchor.value(), Resource{}), &dialog);
    nameEdit->setObjectName(QStringLiteral("anchorNameEdit"));
    auto *aliasesEdit = new QLineEdit(target.anchor->aliases.join(QStringLiteral(", ")), &dialog);
    aliasesEdit->setObjectName(QStringLiteral("anchorAliasesEdit"));
    auto *tagsEdit = new QLineEdit(target.anchor->tags.join(QStringLiteral(", ")), &dialog);
    tagsEdit->setObjectName(QStringLiteral("anchorTagsEdit"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName(QStringLiteral("anchorEditButtons"));

    form->addRow(tr("Name"), nameEdit);
    form->addRow(tr("Aliases"), aliasesEdit);
    form->addRow(tr("Tags"), tagsEdit);
    form->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    updateStatus(tr("Editing anchor"));
    if (dialog.exec() == QDialog::Accepted) {
        editSelectedAnchor(nameEdit->text(),
                           valuesFromCommaText(aliasesEdit->text()),
                           valuesFromCommaText(tagsEdit->text(), true));
    }
}

void PinloomPanel::promptDeleteSelectedAnchor()
{
    requestDeleteSelectedAnchor();
}

void PinloomPanel::promptAddManualAnchor()
{
    const PinloomOpenTarget selectedTarget = currentOpenTarget();
    if (normalizedResourceKind(selectedTarget.resourceKind) == ResourceKind::Pdf) {
        updateStatus(tr("PDF line anchors are deprecated; use PDF-XChange anchor capture"));
        return;
    }

    bool accepted = false;
    const QString target = QInputDialog::getText(
        this,
        tr("Add Anchor"),
        tr("Anchor"),
        QLineEdit::Normal,
        QString(),
        &accepted);
    if (!accepted || target.trimmed().isEmpty()) {
        return;
    }

    const int line = QInputDialog::getInt(
        this,
        tr("Anchor Line"),
        tr("Line"),
        -1,
        -1,
        1000000000,
        1,
        &accepted);
    if (accepted) {
        addManualAnchorToSelectedResource(target, line);
    }
}

void PinloomPanel::toggleSelectedResourcePin()
{
    setSelectedResourcePinned(pinButton_->isChecked());
}

void PinloomPanel::refreshPinButtonState()
{
    const QString resourceId = selectedResultResourceId();
    const bool hasSelection = !resourceId.isEmpty();
    bool pinned = false;
    if (hasSelection) {
        const std::optional<ResourceUsage> usage = repository_.resourceUsage(resourceId);
        pinned = usage.has_value() && usage->pinned;
    }

    const QSignalBlocker blocker(pinButton_);
    pinButton_->setEnabled(hasSelection);
    pinButton_->setChecked(pinned);
    pinButton_->setText(pinned ? tr("Unpin") : tr("Pin"));
}

void PinloomPanel::refreshRootPinButtonState()
{
    if (!pinRootButton_) {
        return;
    }

    const QString rootId = selectedRootId();
    bool pinned = false;
    if (!rootId.isEmpty()) {
        const std::optional<LibraryRoot> root = repository_.findLibraryRoot(rootId);
        pinned = root.has_value() && root->pinned;
    }

    const QSignalBlocker blocker(pinRootButton_);
    pinRootButton_->setEnabled(!rootId.isEmpty());
    pinRootButton_->setChecked(pinned);
    pinRootButton_->setText(pinned ? tr("Unpin Folder") : tr("Pin Folder"));
}

void PinloomPanel::notifyCurrentOpenTargetChanged()
{
    if (options_.currentOpenTargetChangedHandler) {
        options_.currentOpenTargetChangedHandler(currentOpenTarget());
    }
}

void PinloomPanel::notifyResultCountChanged()
{
    if (options_.resultCountChangedHandler) {
        options_.resultCountChangedHandler(resultCount());
    }
}

void PinloomPanel::notifyResultsChanged()
{
    if (options_.resultsChangedHandler) {
        options_.resultsChangedHandler(currentResults());
    }
}

void PinloomPanel::notifyCurrentLibraryRootChanged()
{
    if (options_.currentLibraryRootChangedHandler) {
        options_.currentLibraryRootChangedHandler(selectedLibraryRoot());
    }
}

void PinloomPanel::notifyLibraryRootsChanged()
{
    if (options_.libraryRootsChangedHandler) {
        options_.libraryRootsChangedHandler(libraryRoots());
    }
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
        selectLibraryRootById(currentId);
    }
    if (!rootList_->currentItem() && rootList_->count() > 0) {
        rootList_->setCurrentRow(0);
    }
    refreshRootPinButtonState();
    notifyLibraryRootsChanged();
}

void PinloomPanel::updateStatus(const QString &message)
{
    statusText_ = message;
    statusLabel_->setText(message);
    if (options_.statusChangedHandler) {
        options_.statusChangedHandler(statusText_);
    }
}

QString PinloomPanel::selectedRootId() const
{
    const QListWidgetItem *item = rootList_->currentItem();
    if (!item) {
        return {};
    }
    return item->data(Qt::UserRole).toString();
}

QString PinloomPanel::selectedResultResourceId() const
{
    const QListWidgetItem *item = resultList_->currentItem();
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

PinloomIndexingResult PinloomPanel::finishIndexingResult(const PinloomIndexingResult &result)
{
    lastIndexingResult_ = result;
    if (options_.indexingCompletedHandler) {
        options_.indexingCompletedHandler(lastIndexingResult_);
    }
    return lastIndexingResult_;
}

bool PinloomPanel::tryHostOpenTarget(const PinloomOpenTarget &target)
{
    if (!options_.openTargetHandler) {
        return false;
    }
    return options_.openTargetHandler(target);
}

void PinloomPanel::configureIndexingService(IndexingService &indexer) const
{
    indexer.setRemoteWebFetchingEnabled(remoteWebFetchingEnabled());
}

} // namespace Pinloom
