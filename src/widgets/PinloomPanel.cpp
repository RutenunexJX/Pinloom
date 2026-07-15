#include "pinloom/widgets/PinloomPanel.h"

#include "pinloom/core/AnchorLocator.h"
#include "pinloom/core/InboxFileCapture.h"
#include "pinloom/core/ResourceNormalization.h"
#include "pinloom/widgets/ManualPdfAnchorDialog.h"
#include "pinloom/widgets/SumatraPdfRegionCaptureOverlay.h"
#include "pinloom/widgets/TextPreviewDialog.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSize>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <utility>

namespace Pinloom {

namespace {

constexpr int LauncherActionRole = Qt::UserRole + 40;
constexpr int ClipIdRole = Qt::UserRole + 41;
constexpr int ClipDisplayNameRole = Qt::UserRole + 42;
constexpr int ClipPreviewRole = Qt::UserRole + 43;
constexpr int ClipTagsRole = Qt::UserRole + 44;
constexpr int ClipAliasesRole = Qt::UserRole + 45;
constexpr int ClipUpdatedAtRole = Qt::UserRole + 46;
constexpr int ClipUsedAtRole = Qt::UserRole + 47;
constexpr int ClipStateRole = Qt::UserRole + 48;
constexpr int ResourceDeletedRole = Qt::UserRole + 34;
constexpr int AnchorDeletedRole = Qt::UserRole + 35;

enum class LauncherItemAction {
    Unknown = 0,
    ResourceOpen,
    ClipInsert
};

void installStatusContextMenu(QLabel *label, QWidget *parent, const std::function<QString()> &statusText)
{
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(label, &QLabel::customContextMenuRequested, parent, [label, parent, statusText](const QPoint &pos) {
        const QString status = statusText().trimmed();
        QMenu menu(parent);
        QAction *copyAction = menu.addAction(QObject::tr("Copy status"));
        copyAction->setEnabled(!status.isEmpty());
        QAction *detailsAction = menu.addAction(QObject::tr("Show details"));
        detailsAction->setEnabled(!status.isEmpty());
        QAction *selected = menu.exec(label->mapToGlobal(pos));
        if (!selected) {
            return;
        }
        if (selected == copyAction) {
            if (QClipboard *clipboard = QApplication::clipboard()) {
                clipboard->setText(status);
            }
            return;
        }

        QDialog dialog(parent);
        dialog.setWindowTitle(QObject::tr("Status Details"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *text = new QPlainTextEdit(status, &dialog);
        text->setReadOnly(true);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        auto *copyButton = buttons->addButton(QObject::tr("Copy"), QDialogButtonBox::ActionRole);
        layout->addWidget(text);
        layout->addWidget(buttons);
        QObject::connect(copyButton, &QPushButton::clicked, &dialog, [text]() {
            if (QClipboard *clipboard = QApplication::clipboard()) {
                clipboard->setText(text->toPlainText());
            }
        });
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.resize(480, 240);
        dialog.exec();
    });
}

QString anchorLabel(const Anchor &anchor)
{
    const QString type = anchorLocatorType(anchor);
    if (type == QLatin1String("file.line")) {
        return QStringLiteral("Line");
    }
    if (type == QLatin1String("text.heading")) {
        return QStringLiteral("Heading");
    }
    if (type == QLatin1String("text.block")) {
        return QStringLiteral("Block");
    }
    if (type == QLatin1String("marker")) {
        return QStringLiteral("Marker");
    }
    if (type.endsWith(QLatin1String(".page"))) {
        return QStringLiteral("Page");
    }
    if (type.endsWith(QLatin1String(".rect")) || type == QLatin1String("pdf.region")) {
        return QStringLiteral("PDF Region");
    }
    if (type == QLatin1String("url.fragment")) {
        return QStringLiteral("Fragment");
    }
    return QStringLiteral("Anchor");
}

QString resourceKindLabel(ResourceKind kind)
{
    switch (kind) {
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
        return QStringLiteral("Anchor");
    case ResourceKind::File:
        return QStringLiteral("File");
    case ResourceKind::Unknown:
        break;
    }
    return QStringLiteral("Resource");
}

QString resourceResultLabel(const Resource &resource)
{
    if (isInboxResource(resource)) {
        return QStringLiteral("Inbox");
    }
    return resourceKindLabel(resource.kind);
}

QString compactValue(QString value, int maxLength = 96)
{
    value = value.simplified();
    if (value.size() <= maxLength) {
        return value;
    }
    return value.left(std::max(0, maxLength - 3)) + QStringLiteral("...");
}

QString clipTagsText(const QStringList &tags)
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

QString clipAliasesText(const QStringList &aliases)
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

QString clipTimestampText(const ClipSearchResult &result)
{
    const QDateTime timestamp = result.usedAt.isValid() ? result.usedAt : result.updatedAt;
    return timestamp.isValid() ? timestamp.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString();
}

QString clipMatchText(const ClipSearchResult &result)
{
    if (result.matchedField.trimmed().isEmpty()) {
        return {};
    }
    if (result.matchedValue.trimmed().isEmpty()) {
        return QStringLiteral("match: %1").arg(result.matchedField);
    }
    return QStringLiteral("match: %1=%2").arg(result.matchedField, compactValue(result.matchedValue, 48));
}

QString clipResultText(const ClipSearchResult &result, const QString &action)
{
    const QString displayName = compactValue(
        result.displayName.trimmed().isEmpty() ? result.preview : result.displayName,
        84);
    QStringList metadata;

    const QString timestamp = clipTimestampText(result);
    if (!timestamp.isEmpty()) {
        metadata.append(timestamp);
    }
    const QString tags = clipTagsText(result.tags);
    if (!tags.isEmpty()) {
        metadata.append(tags);
    }
    const QString aliases = clipAliasesText(result.aliases);
    if (!aliases.isEmpty()) {
        metadata.append(QStringLiteral("aliases: %1").arg(compactValue(aliases, 64)));
    }
    const QString match = clipMatchText(result);
    if (!match.isEmpty()) {
        metadata.append(match);
    }
    if (result.pinned) {
        metadata.append(QStringLiteral("pinned"));
    }

    const QString preview = compactValue(result.preview, 96);
    const bool saveAction = action == QLatin1String("save");
    return QStringLiteral("[%1] %2 -> %3\n%4%5%6")
        .arg(saveAction ? QStringLiteral("History") : QStringLiteral("Clip"),
             displayName,
             saveAction ? QStringLiteral("Save") : QStringLiteral("Insert"),
             preview,
             preview.isEmpty() || metadata.isEmpty() ? QString() : QStringLiteral(" | "),
             metadata.join(QStringLiteral(" | ")));
}

QString clipToolTip(const ClipSearchResult &result)
{
    QStringList lines;
    lines.append(QStringLiteral("Clip: %1").arg(result.clipId));
    lines.append(QStringLiteral("Name: %1").arg(result.displayName));
    lines.append(QStringLiteral("Preview: %1").arg(result.preview));
    lines.append(QStringLiteral("Match: %1=%2").arg(result.matchedField, result.matchedValue));

    const QString tags = clipTagsText(result.tags);
    if (!tags.isEmpty()) {
        lines.append(QStringLiteral("Tags: %1").arg(tags));
    }
    const QString aliases = clipAliasesText(result.aliases);
    if (!aliases.isEmpty()) {
        lines.append(QStringLiteral("Aliases: %1").arg(aliases));
    }
    return lines.join(QLatin1Char('\n'));
}

QString clipMatchSummary(const ClipSearchResult &result)
{
    if (result.matchedField == QLatin1String("empty")) {
        if (result.state == ClipState::Temporary) {
            return QStringLiteral("Clip History");
        }
        return result.state == ClipState::Deleted ? QStringLiteral("Deleted Clip") : QStringLiteral("Saved Clip");
    }

    const QString match = clipMatchText(result);
    if (!match.isEmpty()) {
        return match;
    }
    if (result.state == ClipState::Temporary) {
        return QStringLiteral("Clip History");
    }
    if (result.state == ClipState::Deleted) {
        return QStringLiteral("Deleted Clip");
    }
    return QStringLiteral("Saved Clip");
}

QString anchorDisplayName(const Anchor &anchor, const Resource &resource)
{
    const QString name = anchor.name.trimmed();
    if (!name.isEmpty()) {
        return name;
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
            .arg(resourceResultLabel(result.resource),
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

struct LauncherResultEntry {
    SearchResult searchResult;
    ClipSearchResult clipResult;
    bool clip = false;
    int priority = 100;
    double score = 100.0;
    QString title;
    QString stableId;
};

int searchResultPriority(const SearchResult &result)
{
    const QString field = result.matchedField;
    if (field == QLatin1String("anchor")
        || field == QLatin1String("anchor_name")
        || field == QLatin1String("title")) {
        return 0;
    }
    if (field == QLatin1String("filename")) {
        return 5;
    }
    if (field == QLatin1String("anchor_alias")
        || field == QLatin1String("alias")) {
        return 20;
    }
    if (field == QLatin1String("anchor_tag")
        || field == QLatin1String("tag")) {
        return 30;
    }
    if (field == QLatin1String("anchor_metadata")) {
        return 70;
    }
    if (field == QLatin1String("content")
        || field == QLatin1String("path")) {
        return 80;
    }
    return 90;
}

int clipResultPriority(const ClipSearchResult &result)
{
    const QString field = result.matchedField;
    if (field == QLatin1String("name")) {
        return 0;
    }
    if (field == QLatin1String("alias")) {
        return 20;
    }
    if (field == QLatin1String("tag")) {
        return 30;
    }
    if (field == QLatin1String("preview")
        || field == QLatin1String("text")) {
        return 80;
    }
    return 90;
}

double clipComparableScore(const ClipSearchResult &result)
{
    // Clip search uses larger-is-better priorities; convert them to a small
    // lower-is-better adjustment inside the shared match bucket.
    return static_cast<double>(clipResultPriority(result)) - (result.score / 10000.0);
}

bool launcherEntryLessThan(const LauncherResultEntry &left, const LauncherResultEntry &right)
{
    if (left.priority != right.priority) {
        return left.priority < right.priority;
    }
    if (left.score != right.score) {
        return left.score < right.score;
    }
    if (left.title != right.title) {
        return left.title < right.title;
    }
    return left.stableId < right.stableId;
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

int anchorIndex(const Resource &resource, const Anchor &anchor)
{
    for (int i = 0; i < resource.anchors.size(); ++i) {
        if (sameAnchorIdentity(resource.anchors.at(i), anchor)) {
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
    target.deleted = resource.deleted;
    return target;
}

LauncherItemAction launcherActionForItem(const QListWidgetItem *item)
{
    if (!item) {
        return LauncherItemAction::Unknown;
    }
    return static_cast<LauncherItemAction>(item->data(LauncherActionRole).toInt());
}

PinloomOpenTarget openTargetForItem(const QListWidgetItem *item, int row = -1)
{
    PinloomOpenTarget target;
    if (!item) {
        return target;
    }

    const LauncherItemAction action = launcherActionForItem(item);
    if (action == LauncherItemAction::ClipInsert) {
        target.resultRow = row;
        target.clipId = item->data(ClipIdRole).toString();
        target.title = item->data(ClipDisplayNameRole).toString();
        target.location = item->data(ClipPreviewRole).toString();
        target.deleted = static_cast<ClipState>(item->data(ClipStateRole).toInt()) == ClipState::Deleted;
        target.matchedField = item->data(Qt::UserRole + 13).toString();
        target.score = item->data(Qt::UserRole + 14).toDouble();
        target.matchSummary = item->data(Qt::UserRole + 20).toString();
        return target;
    }

    if (action != LauncherItemAction::ResourceOpen) {
        return target;
    }

    target.resultRow = row;
    target.resourceId = item->data(Qt::UserRole).toString();
    target.location = item->data(Qt::UserRole + 1).toString();
    target.title = item->data(Qt::UserRole + 11).toString();
    target.resourceKind = static_cast<ResourceKind>(item->data(Qt::UserRole + 12).toInt());
    target.deleted = item->data(ResourceDeletedRole).toBool();
    target.matchedField = item->data(Qt::UserRole + 13).toString();
    target.score = item->data(Qt::UserRole + 14).toDouble();
    target.matchedContextTag = item->data(Qt::UserRole + 15).toString();
    target.matchedContextLocationPrefix = item->data(Qt::UserRole + 16).toString();
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
        anchor.deleted = item->data(AnchorDeletedRole).toBool();
        target.anchor = anchor;
        target.deleted = target.deleted || anchor.deleted;
    }

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

QString pinloomEntryTypeLabel(PinloomEntryType type)
{
    switch (type) {
    case PinloomEntryType::Anchor:
        return QStringLiteral("Anchor");
    case PinloomEntryType::SavedClip:
        return QStringLiteral("Clip");
    case PinloomEntryType::Inbox:
        return QStringLiteral("Inbox");
    case PinloomEntryType::FileResource:
        return QStringLiteral("File");
    }
    return QStringLiteral("File");
}

int pinloomEntryMatchPriority(const PinloomEntry &entry)
{
    const QString field = entry.matchedField;
    if (field == QLatin1String("anchor")
        || field == QLatin1String("anchor_name")
        || field == QLatin1String("title")
        || field == QLatin1String("name")) {
        return 0;
    }
    if (field == QLatin1String("filename")) {
        return 5;
    }
    if (field == QLatin1String("anchor_alias")
        || field == QLatin1String("alias")) {
        return 20;
    }
    if (field == QLatin1String("anchor_tag")
        || field == QLatin1String("tag")) {
        return 30;
    }
    if (field == QLatin1String("anchor_metadata")) {
        return 70;
    }
    if (field == QLatin1String("content")
        || field == QLatin1String("preview")
        || field == QLatin1String("text")
        || field == QLatin1String("path")) {
        return 80;
    }
    return 90;
}

bool pinloomEntryLessThan(const PinloomEntry &left, const PinloomEntry &right)
{
    const int leftPriority = pinloomEntryMatchPriority(left);
    const int rightPriority = pinloomEntryMatchPriority(right);
    if (leftPriority != rightPriority) {
        return leftPriority < rightPriority;
    }
    if (left.pinned != right.pinned) {
        return left.pinned;
    }
    if (left.usedAt.isValid() != right.usedAt.isValid()) {
        return left.usedAt.isValid();
    }
    if (left.usedAt.isValid() && left.usedAt != right.usedAt) {
        return left.usedAt > right.usedAt;
    }
    if (left.frequency != right.frequency) {
        return left.frequency > right.frequency;
    }
    if (left.score != right.score) {
        return left.score < right.score;
    }
    if (left.name != right.name) {
        return left.name < right.name;
    }
    return left.id < right.id;
}

QList<PinloomEntry> sortedPinloomEntries(QList<PinloomEntry> entries)
{
    std::sort(entries.begin(), entries.end(), pinloomEntryLessThan);
    return entries;
}

PinloomEntry entryFromOpenTarget(const PinloomOpenTarget &target)
{
    PinloomEntry entry;
    entry.resourceId = target.resourceId;
    entry.clipId = target.clipId;
    entry.resourceKind = target.resourceKind;
    entry.location = target.location;
    entry.deleted = target.deleted;
    entry.matchedField = target.matchedField;
    entry.matchSummary = target.matchSummary;
    entry.resultRow = target.resultRow;
    entry.score = target.score;
    entry.anchor = target.anchor;

    if (!target.clipId.trimmed().isEmpty()) {
        entry.type = PinloomEntryType::SavedClip;
        entry.id = QStringLiteral("clip:%1").arg(target.clipId);
        entry.name = target.title.trimmed().isEmpty() ? target.location : target.title;
        entry.targetSummary = target.location;
        entry.deleted = target.deleted;
    } else if (target.anchor.has_value()) {
        entry.type = PinloomEntryType::Anchor;
        entry.id = target.anchor->id.trimmed().isEmpty()
            ? QStringLiteral("anchor:%1").arg(target.resourceId)
            : QStringLiteral("anchor:%1").arg(target.anchor->id);
        entry.name = target.anchor->name.trimmed().isEmpty()
            ? target.title
            : target.anchor->name;
        entry.aliases = target.anchor->aliases;
        entry.tags = target.anchor->tags;
        entry.pinned = target.anchor->pinned;
        entry.deleted = target.anchor->deleted || target.deleted;
        entry.usedAt = target.anchor->usedAt;
        entry.targetSummary = target.anchor->targetFile.trimmed().isEmpty()
            ? target.location
            : target.anchor->targetFile;
    } else {
        entry.type = isInboxResourceId(target.resourceId)
            ? PinloomEntryType::Inbox
            : PinloomEntryType::FileResource;
        entry.id = target.resourceId.trimmed().isEmpty()
            ? QStringLiteral("file:%1").arg(target.location)
            : QStringLiteral("resource:%1").arg(target.resourceId);
        entry.name = target.title.trimmed().isEmpty()
            ? QFileInfo(target.location).fileName()
            : target.title;
        entry.targetSummary = target.location;
        entry.deleted = target.deleted;
    }

    if (entry.name.trimmed().isEmpty()) {
        entry.name = entry.id;
    }
    entry.metadata.insert(QStringLiteral("resourceId"), entry.resourceId);
    entry.metadata.insert(QStringLiteral("clipId"), entry.clipId);
    entry.metadata.insert(QStringLiteral("location"), entry.location);
    entry.metadata.insert(QStringLiteral("matchedField"), entry.matchedField);
    entry.metadata.insert(QStringLiteral("deleted"), entry.deleted);
    return entry;
}

PinloomOpenTarget openTargetFromEntry(const PinloomEntry &entry)
{
    PinloomOpenTarget target;
    target.resourceId = entry.resourceId;
    target.clipId = entry.clipId;
    target.resourceKind = entry.resourceKind;
    target.title = entry.name;
    target.location = entry.location;
    target.deleted = entry.deleted;
    target.matchedField = entry.matchedField;
    target.matchSummary = entry.matchSummary;
    target.resultRow = entry.resultRow;
    target.score = entry.score;
    target.anchor = entry.anchor;
    if (target.anchor.has_value() && target.anchor->deleted) {
        target.deleted = true;
    }
    return target;
}

QList<PinloomEntry> entriesFromOpenTargets(const QList<PinloomOpenTarget> &targets)
{
    QList<PinloomEntry> entries;
    entries.reserve(targets.size());
    for (const PinloomOpenTarget &target : targets) {
        entries.append(entryFromOpenTarget(target));
    }
    return entries;
}

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
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    auto *resultToolbar = new QHBoxLayout();
    resultToolbar->setContentsMargins(0, 0, 0, 0);
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setObjectName(QStringLiteral("searchEdit"));
    searchEdit_->setPlaceholderText(tr("Search anchors and Saved Clips"));
    searchEdit_->setClearButtonEnabled(true);
    searchEdit_->setMinimumHeight(34);
    openButton_ = new QPushButton(tr("Jump"), this);
    openButton_->setObjectName(QStringLiteral("openButton"));
    addAliasButton_ = new QPushButton(tr("Add Alias"), this);
    addAliasButton_->setObjectName(QStringLiteral("addAliasButton"));
    addAnchorButton_ = new QPushButton(tr("Add Anchor"), this);
    addAnchorButton_->setObjectName(QStringLiteral("addAnchorButton"));
    pinButton_ = new QPushButton(tr("Pin"), this);
    pinButton_->setObjectName(QStringLiteral("pinButton"));
    pinButton_->setCheckable(true);
    resultToolbar->addWidget(searchEdit_, 1);
    resultToolbar->addWidget(openButton_);
    resultToolbar->addWidget(addAliasButton_);
    resultToolbar->addWidget(addAnchorButton_);
    resultToolbar->addWidget(pinButton_);

    resultList_ = new QListWidget(this);
    resultList_->setObjectName(QStringLiteral("resultList"));
    resultList_->setAlternatingRowColors(true);
    resultList_->setUniformItemSizes(true);
    resultList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    resultList_->setMinimumHeight(0);
    resultList_->setMaximumHeight(resultList_->fontMetrics().lineSpacing() * 10 + 24);
    searchEdit_->installEventFilter(this);
    resultList_->installEventFilter(this);
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("statusLabel"));
    installStatusContextMenu(statusLabel_, this, [this]() {
        return statusText_;
    });

    layout->addLayout(resultToolbar);
    layout->addWidget(resultList_, options_.compactLauncherMode ? 0 : 1);
    layout->addWidget(statusLabel_);

    connect(openButton_, &QPushButton::clicked, this, &PinloomPanel::openSelectedResource);
    connect(addAliasButton_, &QPushButton::clicked, this, &PinloomPanel::promptAddAlias);
    connect(addAnchorButton_, &QPushButton::clicked, this, &PinloomPanel::promptAddManualAnchor);
    connect(pinButton_, &QPushButton::clicked, this, &PinloomPanel::toggleSelectedResourcePin);
    connect(searchEdit_, &QLineEdit::textChanged, this, &PinloomPanel::refreshResults);
    connect(searchEdit_, &QLineEdit::returnPressed, this, &PinloomPanel::activateCurrentLauncherItem);
    connect(resultList_, &QListWidget::currentItemChanged, this, &PinloomPanel::refreshAnchorButtonState);
    connect(resultList_, &QListWidget::currentItemChanged, this, &PinloomPanel::refreshPinButtonState);
    connect(resultList_, &QListWidget::currentItemChanged, this, &PinloomPanel::notifyCurrentOpenTargetChanged);
    connect(resultList_, &QListWidget::itemDoubleClicked, this, &PinloomPanel::openResultItem);
    connect(resultList_, &QListWidget::itemActivated, this, &PinloomPanel::openResultItem);
    openButton_->setVisible(options_.showOpenButton);
    addAliasButton_->setVisible(options_.showManualEditControls);
    addAnchorButton_->setVisible(options_.showManualEditControls);
    pinButton_->setVisible(options_.showPinControls);
    statusLabel_->setVisible(options_.showStatusLine);
    resultList_->setVisible(!options_.compactLauncherMode);

    refreshResults();
    refreshAnchorButtonState();
    focusSearch();

    auto installShortcut = [this](const QKeySequence &sequence, const QObject *receiver, const char *member) {
        auto *shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, SIGNAL(activated()), receiver, member);
    };
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

        if (watched == resultList_
            && (key == Qt::Key_Return || key == Qt::Key_Enter)
            && modifiers == Qt::NoModifier) {
            activateCurrentLauncherItem();
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

QList<PinloomEntry> PinloomPanel::currentEntries() const
{
    return entriesFromOpenTargets(currentResults());
}

QList<PinloomEntry> PinloomPanel::searchEntries(const QString &text, bool includeDeleted) const
{
    QList<PinloomEntry> entries;

    SearchQuery query;
    query.text = text;
    query.requiredTags = requiredTags_;
    query.requiredLocationPrefixes = requiredLocationPrefixes_;
    query.requiredKinds = requiredResourceKinds_;
    query.contextTags = contextTags_;
    query.contextLocationPrefixes = contextLocationPrefixes_;
    query.includeDeleted = includeDeleted;
    query.limit = 100;

    for (const SearchResult &result : repository_.search(query)) {
        PinloomOpenTarget target;
        target.resourceId = result.resource.id;
        target.resourceKind = result.resource.kind;
        target.title = result.matchedAnchor.has_value()
            ? anchorDisplayName(result.matchedAnchor.value(), result.resource)
            : result.resource.title;
        target.location = result.resource.location;
        target.deleted = result.matchedAnchor.has_value()
            ? (result.resource.deleted || result.matchedAnchor->deleted)
            : result.resource.deleted;
        target.matchedField = result.matchedField;
        target.matchSummary = resultMatchSummary(result, query);
        target.score = result.score;
        target.anchor = result.matchedAnchor;

        PinloomEntry entry = entryFromOpenTarget(target);
        if (!result.matchedAnchor.has_value()) {
            entry.aliases = result.resource.aliases;
            entry.tags = result.resource.tags;
            const std::optional<ResourceUsage> usage = repository_.resourceUsage(result.resource.id);
            if (usage.has_value()) {
                entry.pinned = usage->pinned;
                entry.usedAt = usage->lastOpenedAt;
                entry.frequency = usage->openCount;
            }
        }
        entries.append(entry);
    }

    if (options_.clipSearchHandler) {
        ClipSearchOptions clipOptions;
        clipOptions.includeSaved = true;
        clipOptions.includeTemporary = false;
        clipOptions.includeDeleted = includeDeleted;
        clipOptions.emptyQueryReturnsPinnedAndRecent = true;
        clipOptions.limit = std::max(0, query.limit - static_cast<int>(entries.size()));

        for (const ClipSearchResult &result : options_.clipSearchHandler(text, clipOptions)) {
            PinloomEntry entry;
            entry.id = QStringLiteral("clip:%1").arg(result.clipId);
            entry.type = PinloomEntryType::SavedClip;
            entry.name = result.displayName.trimmed().isEmpty() ? result.preview : result.displayName;
            entry.aliases = result.aliases;
            entry.tags = result.tags;
            entry.pinned = result.pinned;
            entry.deleted = result.state == ClipState::Deleted;
            entry.usedAt = result.usedAt;
            entry.targetSummary = result.preview;
            entry.clipId = result.clipId;
            entry.location = result.preview;
            entry.matchedField = result.matchedField;
            entry.matchSummary = clipMatchSummary(result);
            entry.score = clipComparableScore(result);
            entry.metadata.insert(QStringLiteral("clipId"), entry.clipId);
            entry.metadata.insert(QStringLiteral("deleted"), entry.deleted);
            entries.append(entry);
        }
    }

    return sortedPinloomEntries(entries);
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

QString PinloomPanel::statusText() const
{
    return statusText_;
}

std::optional<ManualPdfAnchorCreationRequest> PinloomPanel::selectedPdfAnchorCaptureRequest() const
{
    const PinloomOpenTarget target = currentOpenTarget();
    if (target.resourceId.isEmpty() && target.location.trimmed().isEmpty()) {
        return std::nullopt;
    }

    QString file;
    int page = 1;
    PdfCaptureRect rect{0.0, 0.0, 612.0, 792.0};
    double zoom = -1.0;
    QString initialName = searchEdit_ ? searchEdit_->text().trimmed() : QString();

    if (target.anchor.has_value() && isSumatraPdfAnchor(target.anchor.value())) {
        const Anchor &anchor = target.anchor.value();
        file = anchor.targetFile.trimmed();
        if (file.isEmpty()) {
            file = target.location.trimmed();
        }
        const int anchorPage = anchorLocatorPage(anchor);
        if (anchorPage > 0) {
            page = anchorPage;
        }
        const std::optional<QRectF> anchorRegion = anchorLocatorRegion(anchor);
        if (anchorRegion.has_value()) {
            rect = {anchorRegion->left(),
                    anchorRegion->top(),
                    anchorRegion->right(),
                    anchorRegion->bottom()};
        }
        if (!anchor.name.trimmed().isEmpty()) {
            initialName = anchor.name.trimmed();
        }
    } else if (target.resourceKind == ResourceKind::Pdf) {
        file = target.location.trimmed();
    } else {
        return std::nullopt;
    }

    if (file.isEmpty()) {
        return std::nullopt;
    }

    ManualPdfAnchorCreationRequest request;
    request.name = initialName;
    request.file = file;
    request.page = page;
    request.rect = rect;
    request.zoom = zoom;
    request.source = QStringLiteral("selected-pdf-fallback");
    request.targetApp = QStringLiteral("SumatraPDF");
    return request;
}

bool PinloomPanel::capturePdfAnchorFromSuggestedRequest(
    const std::optional<ManualPdfAnchorCreationRequest> &suggestedPdfRequest,
    const QString &missingContextStatus,
    bool allowManualFallback)
{
    std::optional<ManualPdfAnchorCreationRequest> request;
    if (options_.pdfAnchorCaptureRequestProvider
        && (suggestedPdfRequest.has_value() || allowManualFallback)) {
        request = options_.pdfAnchorCaptureRequestProvider(
            suggestedPdfRequest.value_or(ManualPdfAnchorCreationRequest{}));
    } else if (suggestedPdfRequest.has_value()) {
        const bool foregroundPdfFallback =
            suggestedPdfRequest->source.trimmed().compare(QStringLiteral("foreground-sumatrapdf-fallback"),
                                                          Qt::CaseInsensitive) == 0;
        const bool foregroundPdfRegion =
            suggestedPdfRequest->source.trimmed().compare(QStringLiteral("foreground-sumatrapdf-region"),
                                                          Qt::CaseInsensitive) == 0;
        const bool foregroundPdfViewState =
            suggestedPdfRequest->source.trimmed().compare(QStringLiteral("foreground-sumatrapdf-viewstate"),
                                                          Qt::CaseInsensitive) == 0;
        if (options_.pdfAnchorCaptureDialogHandler) {
            request = options_.pdfAnchorCaptureDialogHandler(this, suggestedPdfRequest.value());
        } else if (options_.manualPdfAnchorDialogHandler) {
            request = options_.manualPdfAnchorDialogHandler(this);
        } else {
            ManualPdfAnchorDialog dialog(suggestedPdfRequest.value(), this);
            updateStatus(foregroundPdfRegion
                                   ? tr("Capturing PDF anchor from foreground SumatraPDF region")
                             : foregroundPdfViewState
                                   ? tr("Capturing PDF anchor from foreground PDF page/zoom")
                                   : foregroundPdfFallback
                                         ? tr("Capturing PDF anchor from foreground PDF fallback; page defaults to 1, edit if needed")
                                         : tr("Capturing PDF anchor from selected PDF fallback"));
            if (dialog.exec() == QDialog::Accepted) {
                request = dialog.request();
            }
        }
    } else if (allowManualFallback && options_.manualPdfAnchorRequestProvider) {
        request = options_.manualPdfAnchorRequestProvider();
    } else if (allowManualFallback && options_.manualPdfAnchorDialogHandler) {
        request = options_.manualPdfAnchorDialogHandler(this);
    } else {
        const QString status = missingContextStatus.trimmed();
        updateStatus(status.isEmpty()
                         ? tr("Open or focus a SumatraPDF PDF before k n")
                         : status);
        return false;
    }

    if (!request.has_value()) {
        updateStatus(tr("Capture canceled"));
        return false;
    }

    ManualPdfAnchorCreationService creationService(repository_);
    const ManualPdfAnchorCreationResult result =
        creationService.createManualPdfAnchor(request.value());
    if (!result.success()) {
        updateStatus(result.error);
        return false;
    }

    refreshResults();
    if (!selectResultResource(result.resource.id)) {
        setSearchText(result.anchor.name);
        selectResultResource(result.resource.id);
    }
    const bool selectedPdfFallback =
        request->source.trimmed().compare(QStringLiteral("selected-pdf-fallback"), Qt::CaseInsensitive) == 0;
    const bool foregroundPdfFallback =
        request->source.trimmed().compare(QStringLiteral("foreground-sumatrapdf-fallback"), Qt::CaseInsensitive) == 0;
    const bool foregroundPdfRegion =
        request->source.trimmed().compare(QStringLiteral("foreground-sumatrapdf-region"), Qt::CaseInsensitive) == 0;
    const bool foregroundPdfViewState =
        request->source.trimmed().compare(QStringLiteral("foreground-sumatrapdf-viewstate"), Qt::CaseInsensitive) == 0;
    updateStatus(foregroundPdfRegion
                     ? tr("Captured PDF anchor \"%1\" (foreground SumatraPDF region)").arg(result.anchor.name)
                     : foregroundPdfViewState
                     ? tr("Captured PDF anchor \"%1\" (foreground SumatraPDF page/zoom)").arg(result.anchor.name)
                     : foregroundPdfFallback
                           ? tr("Captured PDF anchor \"%1\" (foreground SumatraPDF fallback)").arg(result.anchor.name)
                           : selectedPdfFallback
                                 ? tr("Captured PDF anchor \"%1\" (selected-PDF fallback)").arg(result.anchor.name)
                                 : tr("Captured PDF anchor \"%1\"").arg(result.anchor.name));
    return true;
}

bool PinloomPanel::captureForegroundPdfAnchor()
{
    QString foregroundPdfStatus;
    std::optional<ManualPdfAnchorCreationRequest> foregroundPdfRequest;
    if (options_.foregroundPdfAnchorCaptureRequestProvider) {
        foregroundPdfRequest = options_.foregroundPdfAnchorCaptureRequestProvider(&foregroundPdfStatus);
    }

    return capturePdfAnchorFromSuggestedRequest(
        foregroundPdfRequest,
        foregroundPdfStatus.trimmed().isEmpty()
            ? tr("Open or focus a SumatraPDF PDF before k n")
            : foregroundPdfStatus.trimmed(),
        false);
}

bool PinloomPanel::captureCurrentAppPosition()
{
    QString foregroundPdfStatus;
    std::optional<ManualPdfAnchorCreationRequest> foregroundPdfRequest;
    if (options_.foregroundPdfAnchorCaptureRequestProvider) {
        foregroundPdfRequest = options_.foregroundPdfAnchorCaptureRequestProvider(&foregroundPdfStatus);
    }

    const std::optional<ManualPdfAnchorCreationRequest> selectedPdfRequest =
        selectedPdfAnchorCaptureRequest();
    const std::optional<ManualPdfAnchorCreationRequest> suggestedPdfRequest =
        foregroundPdfRequest.has_value() ? foregroundPdfRequest : selectedPdfRequest;
    return capturePdfAnchorFromSuggestedRequest(
        suggestedPdfRequest,
        foregroundPdfStatus.trimmed().isEmpty()
            ? tr("Open or select a PDF before capturing an anchor")
            : foregroundPdfStatus.trimmed(),
        true);
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

    const QString anchorName = anchorDisplayName(target.anchor.value(), Resource{});
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        tr("Delete Anchor"),
        tr("Delete \"%1\" from Pinloom?\n\nThis only removes the Pinloom anchor. It will not delete the target file.")
            .arg(anchorName));
    if (choice != QMessageBox::Yes) {
        updateStatus(tr("Delete canceled"));
        return false;
    }

    if (!repository_.softDeleteAnchor(target.resourceId, target.anchor.value())) {
        updateStatus(tr("Unable to delete anchor"));
        refreshResults();
        return false;
    }

    refreshResults();
    updateStatus(tr("Deleted anchor \"%1\" from Pinloom").arg(anchorName));
    return true;
}

bool PinloomPanel::setSelectedAnchorPinned(bool pinned)
{
    const PinloomOpenTarget target = currentOpenTarget();
    if (target.resourceId.isEmpty() || !target.anchor.has_value()) {
        updateStatus(tr("Select an anchor to pin"));
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
    anchor.pinned = pinned;
    anchor.updatedAt = QDateTime::currentDateTimeUtc();
    resource->updatedAt = anchor.updatedAt;
    if (!repository_.upsertResource(resource.value())) {
        updateStatus(tr("Unable to update pinned anchor"));
        return false;
    }

    refreshResults();
    selectResultResource(target.resourceId);
    updateStatus(pinned ? tr("Pinned anchor") : tr("Unpinned anchor"));
    return true;
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

    if (resource->kind == ResourceKind::Pdf) {
        updateStatus(tr("PDF line anchors are deprecated; use SumatraPDF anchor capture"));
        return false;
    }

    Anchor anchor;
    anchor.name = trimmedTarget;
    anchor.locatorType = line > 0 ? QStringLiteral("file.line") : QStringLiteral("manual");
    QJsonObject locator;
    locator.insert(QStringLiteral("type"), anchor.locatorType);
    if (line > 0) {
        locator.insert(QStringLiteral("line"), line);
    }
    anchor.locatorJson =
        QString::fromUtf8(QJsonDocument(locator).toJson(QJsonDocument::Compact));

    const auto isDuplicate = [&anchor](const Anchor &existing) {
        return existing.name.compare(anchor.name, Qt::CaseInsensitive) == 0
            && anchorLocatorType(existing) == anchorLocatorType(anchor)
            && anchorLocatorLine(existing) == anchorLocatorLine(anchor);
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

void PinloomPanel::refreshResults()
{
    const PinloomOpenTarget previousTarget = currentOpenTarget();
    resultList_->clear();

    const QString searchText = searchEdit_->text();
    refreshSearchResults(searchText, previousTarget);

    refreshLauncherVisibility();
    refreshPinButtonState();
    notifyResultCountChanged();
    notifyResultsChanged();
}

void PinloomPanel::refreshSearchResults(const QString &searchText, const PinloomOpenTarget &previousTarget)
{
    QStringList listedClipIds;
    QList<LauncherResultEntry> entries;
    const auto appendClipEntries = [&entries, &listedClipIds](const QList<ClipSearchResult> &clipResults) {
        for (const ClipSearchResult &result : clipResults) {
            if (!result.clipId.isEmpty() && listedClipIds.contains(result.clipId)) {
                continue;
            }
            listedClipIds.append(result.clipId);

            LauncherResultEntry entry;
            entry.clip = true;
            entry.clipResult = result;
            entry.priority = clipResultPriority(result);
            entry.score = clipComparableScore(result);
            entry.title = result.displayName.trimmed().isEmpty() ? result.preview : result.displayName;
            entry.stableId = result.clipId;
            entries.append(entry);
        }
    };

    SearchQuery query;
    query.text = searchText;
    query.requiredTags = requiredTags_;
    query.requiredLocationPrefixes = requiredLocationPrefixes_;
    query.requiredKinds = requiredResourceKinds_;
    query.contextTags = contextTags_;
    query.contextLocationPrefixes = contextLocationPrefixes_;
    query.limit = 100;

    const bool emptyCompactLauncherQuery = options_.compactLauncherMode && searchText.trimmed().isEmpty();
    const QList<SearchResult> results = emptyCompactLauncherQuery
        ? QList<SearchResult>{}
        : repository_.search(query);
    for (const SearchResult &result : results) {
        LauncherResultEntry entry;
        entry.searchResult = result;
        entry.priority = searchResultPriority(result);
        entry.score = result.score;
        entry.title = result.matchedAnchor.has_value()
            ? anchorDisplayName(result.matchedAnchor.value(), result.resource)
            : result.resource.title;
        entry.stableId = result.resource.id;
        entries.append(entry);
    }

    if (!emptyCompactLauncherQuery && options_.clipSearchHandler) {
        ClipSearchOptions clipOptions;
        clipOptions.includeSaved = true;
        clipOptions.includeTemporary = false;
        clipOptions.emptyQueryReturnsPinnedAndRecent = false;
        clipOptions.limit = std::max(0, query.limit - static_cast<int>(entries.size()));

        appendClipEntries(options_.clipSearchHandler(searchText, clipOptions));
    }

    std::sort(entries.begin(), entries.end(), launcherEntryLessThan);
    if (query.limit > 0 && entries.size() > query.limit) {
        entries.erase(entries.begin() + query.limit, entries.end());
    }

    for (const LauncherResultEntry &entry : entries) {
        if (entry.clip) {
            const ClipSearchResult &result = entry.clipResult;
            auto *item = new QListWidgetItem(clipResultText(result, QStringLiteral("insert")), resultList_);
            item->setToolTip(clipToolTip(result));
            item->setSizeHint(QSize(0, resultList_->fontMetrics().lineSpacing() * 2 + 12));
            item->setData(LauncherActionRole, static_cast<int>(LauncherItemAction::ClipInsert));
            item->setData(ClipIdRole, result.clipId);
            item->setData(ClipDisplayNameRole, result.displayName);
            item->setData(ClipPreviewRole, result.preview);
            item->setData(ClipTagsRole, result.tags);
            item->setData(ClipAliasesRole, result.aliases);
            item->setData(ClipUpdatedAtRole, result.updatedAt);
            item->setData(ClipUsedAtRole, result.usedAt);
            item->setData(ClipStateRole, static_cast<int>(result.state));
            item->setData(Qt::UserRole + 13, result.matchedField);
            item->setData(Qt::UserRole + 14, result.score);
            item->setData(Qt::UserRole + 20, clipMatchSummary(result));
            continue;
        }

        const SearchResult &result = entry.searchResult;
        auto *item = new QListWidgetItem(resultText(result), resultList_);
        item->setToolTip(resultToolTip(result, query));
        item->setSizeHint(QSize(0, resultList_->fontMetrics().lineSpacing() * 2 + 12));
        item->setData(LauncherActionRole, static_cast<int>(LauncherItemAction::ResourceOpen));
        item->setData(Qt::UserRole, result.resource.id);
        item->setData(Qt::UserRole + 1, result.resource.location);
        item->setData(Qt::UserRole + 11, result.resource.title);
        item->setData(Qt::UserRole + 12, static_cast<int>(result.resource.kind));
        item->setData(ResourceDeletedRole, result.resource.deleted);
        item->setData(Qt::UserRole + 13, result.matchedField);
        item->setData(Qt::UserRole + 14, result.score);
        item->setData(Qt::UserRole + 15, matchedContextTag(result.resource, query.contextTags));
        item->setData(Qt::UserRole + 16, matchedContextLocationPrefix(result.resource, query.contextLocationPrefixes));
        item->setData(Qt::UserRole + 20, resultMatchSummary(result, query));
        if (result.matchedAnchor.has_value()) {
            const Anchor &anchor = result.matchedAnchor.value();
            item->setData(Qt::UserRole + 2, true);
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
            item->setData(AnchorDeletedRole, anchor.deleted);
        }
    }

    bool restoredSelection = false;
    if (!previousTarget.clipId.isEmpty()) {
        for (int row = 0; row < resultList_->count(); ++row) {
            QListWidgetItem *item = resultList_->item(row);
            const LauncherItemAction action = launcherActionForItem(item);
            if (action == LauncherItemAction::ClipInsert
                && item->data(ClipIdRole).toString() == previousTarget.clipId) {
                resultList_->setCurrentItem(item);
                restoredSelection = true;
                break;
            }
        }
    } else if (!previousTarget.resourceId.isEmpty()) {
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

    updateStatus(tr("%n result(s)", nullptr, resultList_->count()));
}

bool PinloomPanel::activateCurrentOpenTarget()
{
    return activateCurrentLauncherItem();
}

bool PinloomPanel::activateCurrentLauncherItem()
{
    QListWidgetItem *item = resultList_->currentItem();
    if (!item && resultList_->count() > 0) {
        resultList_->setCurrentRow(0);
        item = resultList_->currentItem();
    }
    if (!item) {
        updateStatus(tr("No resource selected"));
        return false;
    }
    return activateLauncherItem(item);
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

bool PinloomPanel::activateLauncherItem(QListWidgetItem *item)
{
    if (!item) {
        updateStatus(tr("No resource selected"));
        return false;
    }

    resultList_->setCurrentItem(item);
    const LauncherItemAction action = launcherActionForItem(item);
    if (action == LauncherItemAction::ClipInsert || action == LauncherItemAction::ResourceOpen) {
        return activateOpenTarget(openTargetForItem(item, resultList_->row(item)));
    }

    updateStatus(tr("Unknown launcher action"));
    return false;
}

bool PinloomPanel::activateOpenTarget(const PinloomOpenTarget &target)
{
    if (!target.clipId.trimmed().isEmpty()) {
        if (!options_.clipInsertionHandler) {
            updateStatus(tr("Clip insertion is not configured"));
            return false;
        }

        QString error;
        if (!options_.clipInsertionHandler(target.clipId, &error)) {
            updateStatus(error.trimmed().isEmpty() ? tr("Clip insertion failed") : error.trimmed());
            return false;
        }

        updateStatus(tr("Inserted clip"));
        return true;
    }

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

    if (target.anchor.has_value() && isSumatraPdfAnchor(target.anchor.value())) {
        if (activateSumatraPdfTarget(target)) {
            recordOpen();
            return true;
        }
        return false;
    }

    const int anchorLine = target.anchor.has_value()
        ? anchorLocatorLine(target.anchor.value())
        : -1;
    if (anchorLine > 0) {
        TextPreviewDialog preview(target.location, anchorLine, this);
        if (!preview.load()) {
            updateStatus(tr("Unable to preview %1").arg(target.location));
            return false;
        }
        recordOpen();
        preview.exec();
        return true;
    }

    if (isInboxResourceId(target.resourceId)) {
        const QFileInfo fileInfo(target.location);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            updateStatus(tr("Inbox file no longer exists: %1").arg(target.location));
            return false;
        }
    }

    QUrl targetUrl = urlForLocation(target.location);
    if (target.anchor.has_value()
        && anchorLocatorType(target.anchor.value()) == QLatin1String("url.fragment")) {
        targetUrl.setFragment(anchorLocatorFragment(target.anchor.value()));
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

bool PinloomPanel::activateSumatraPdfTarget(const PinloomOpenTarget &target)
{
    if (!target.anchor.has_value()) {
        updateStatus(tr("No SumatraPDF anchor selected"));
        return false;
    }

    const SumatraPdfCommandResult buildResult = options_.sumatraPdfExecutablePathProvider
        ? buildSumatraPdfCommand(target.anchor.value(),
                                 target.location,
                                 options_.sumatraPdfExecutablePathProvider().trimmed())
        : buildSumatraPdfCommand(target.anchor.value(),
                                 target.location,
                                 options_.applicationLaunchSettings);
    if (!buildResult.success()) {
        updateStatus(buildResult.error);
        return false;
    }

    if (options_.sumatraPdfLaunchHandler) {
        QString error;
        if (!options_.sumatraPdfLaunchHandler(buildResult.command, &error)) {
            updateStatus(error.trimmed().isEmpty()
                             ? tr("Unable to launch SumatraPDF")
                             : error.trimmed());
            return false;
        }
        updateStatus(tr("Opened SumatraPDF target"));
        return true;
    }

    const QFileInfo executable(buildResult.command.executablePath);
    if (!executable.exists() || !executable.isFile()) {
        updateStatus(tr("SumatraPDF executable is not configured/found"));
        return false;
    }

    if (!QProcess::startDetached(buildResult.command.executablePath, buildResult.command.arguments)) {
        updateStatus(tr("Unable to launch SumatraPDF"));
        return false;
    }

    if (buildResult.command.highlightRect.isValid()
        && buildResult.command.page > 0
        && buildResult.command.zoom > 0.0) {
        const QRectF highlightRect = buildResult.command.highlightRect;
        const int highlightPage = buildResult.command.page;
        const double highlightZoom = buildResult.command.zoom;
        QTimer::singleShot(450, this, [highlightRect, highlightPage, highlightZoom]() {
            showSumatraPdfRectHighlight(highlightRect, highlightPage, highlightZoom);
        });
    }

    updateStatus(tr("Opened SumatraPDF target"));
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
    if (selectedTarget.resourceKind == ResourceKind::Pdf) {
        captureCurrentAppPosition();
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

void PinloomPanel::refreshAnchorButtonState()
{
    if (!addAnchorButton_) {
        return;
    }

    if (selectedPdfAnchorCaptureRequest().has_value()) {
        addAnchorButton_->setText(tr("Capture PDF Anchor"));
        addAnchorButton_->setToolTip(tr("Capture a SumatraPDF anchor from the selected PDF context"));
        return;
    }

    addAnchorButton_->setText(tr("Add Anchor"));
    addAnchorButton_->setToolTip(tr("Add a text/file anchor to the selected resource"));
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

void PinloomPanel::updateStatus(const QString &message)
{
    statusText_ = message;
    statusLabel_->setText(message);
    if (options_.statusChangedHandler) {
        options_.statusChangedHandler(statusText_);
    }
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

bool PinloomPanel::tryHostOpenTarget(const PinloomOpenTarget &target)
{
    if (!options_.openTargetHandler) {
        return false;
    }
    return options_.openTargetHandler(target);
}

void PinloomPanel::refreshLauncherVisibility()
{
    if (!options_.compactLauncherMode) {
        resultList_->setVisible(true);
        statusLabel_->setVisible(options_.showStatusLine);
        return;
    }

    const bool hasQuery = searchEdit_ && !searchEdit_->text().trimmed().isEmpty();
    const bool hasResults = resultList_ && resultList_->count() > 0;
    resultList_->setVisible(hasQuery && hasResults);
    statusLabel_->setVisible(options_.showStatusLine);

    QWidget *topLevel = window();
    if (topLevel) {
        QTimer::singleShot(0, topLevel, [topLevel]() {
            topLevel->adjustSize();
        });
    }
}

} // namespace Pinloom
