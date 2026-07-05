#include "pinloom/widgets/PinloomCommandPanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSize>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <utility>

namespace Pinloom {

namespace {

constexpr int CommandActionRole = Qt::UserRole + 80;
constexpr int CommandSearchTextRole = Qt::UserRole + 81;
constexpr int CommandSearchQueryRole = Qt::UserRole + 82;
constexpr int ClipIdRole = Qt::UserRole + 83;
constexpr int ClipDisplayNameRole = Qt::UserRole + 84;
constexpr int ClipPreviewRole = Qt::UserRole + 85;
constexpr int ClipTagsRole = Qt::UserRole + 86;
constexpr int ClipAliasesRole = Qt::UserRole + 87;
constexpr int ClipUpdatedAtRole = Qt::UserRole + 88;
constexpr int ClipUsedAtRole = Qt::UserRole + 89;
constexpr int ClipStateRole = Qt::UserRole + 90;
constexpr int ClipMatchedFieldRole = Qt::UserRole + 91;
constexpr int ClipMatchedValueRole = Qt::UserRole + 92;
constexpr int ClipScoreRole = Qt::UserRole + 93;
constexpr int ClipRankRole = Qt::UserRole + 94;
constexpr int ClipPinnedRole = Qt::UserRole + 95;
constexpr int ClipCreatedAtRole = Qt::UserRole + 96;
constexpr int TargetResourceIdRole = Qt::UserRole + 120;
constexpr int TargetClipIdRole = Qt::UserRole + 121;
constexpr int TargetKindRole = Qt::UserRole + 122;
constexpr int TargetTitleRole = Qt::UserRole + 123;
constexpr int TargetLocationRole = Qt::UserRole + 124;
constexpr int TargetMatchedFieldRole = Qt::UserRole + 125;
constexpr int TargetScoreRole = Qt::UserRole + 126;
constexpr int TargetMatchSummaryRole = Qt::UserRole + 127;
constexpr int TargetHasAnchorRole = Qt::UserRole + 128;
constexpr int TargetAnchorTypeRole = Qt::UserRole + 129;
constexpr int TargetAnchorTargetRole = Qt::UserRole + 130;
constexpr int TargetAnchorLineRole = Qt::UserRole + 131;
constexpr int TargetAnchorPageRole = Qt::UserRole + 132;
constexpr int TargetAnchorRegionXRole = Qt::UserRole + 133;
constexpr int TargetAnchorRegionYRole = Qt::UserRole + 134;
constexpr int TargetAnchorRegionWidthRole = Qt::UserRole + 135;
constexpr int TargetAnchorRegionHeightRole = Qt::UserRole + 136;
constexpr int TargetAnchorIdRole = Qt::UserRole + 137;
constexpr int TargetAnchorNameRole = Qt::UserRole + 138;
constexpr int TargetAnchorTargetAppRole = Qt::UserRole + 139;
constexpr int TargetAnchorTargetFileRole = Qt::UserRole + 140;
constexpr int TargetAnchorTargetUriRole = Qt::UserRole + 141;
constexpr int TargetAnchorLocatorTypeRole = Qt::UserRole + 142;
constexpr int TargetAnchorLocatorJsonRole = Qt::UserRole + 143;
constexpr int TargetAnchorAliasesRole = Qt::UserRole + 144;
constexpr int TargetAnchorTagsRole = Qt::UserRole + 145;
constexpr int TargetAnchorPinnedRole = Qt::UserRole + 146;
constexpr int TargetAnchorCreatedAtRole = Qt::UserRole + 147;
constexpr int TargetAnchorUpdatedAtRole = Qt::UserRole + 148;
constexpr int TargetAnchorUsedAtRole = Qt::UserRole + 149;
constexpr int ResultActionIdRole = Qt::UserRole + 160;
constexpr int ResultActionLabelRole = Qt::UserRole + 161;
constexpr int ResultActionDetailRole = Qt::UserRole + 162;
constexpr int ResultActionEnabledRole = Qt::UserRole + 163;
constexpr int ResultActionDisabledReasonRole = Qt::UserRole + 164;

constexpr const char *PrimaryResultActionId = "primary";

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

enum class CommandNamespace {
    None,
    Clip,
    Anchor,
    Inbox,
    Search
};

enum class CommandAction {
    None,
    ClipSearch,
    ClipNew,
    AnchorNew,
    InboxNew,
    InboxSearch,
    OpenSearch
};

enum class CommandRowAction {
    Unknown = 0,
    OpenCommand,
    OpenUnifiedTarget,
    ClipInsert,
    ClipSave,
    AnchorCapture,
    InboxSave,
    OpenSearchWindow,
    UnifiedTargetAction
};

struct CommandState {
    CommandNamespace commandNamespace = CommandNamespace::None;
    CommandAction action = CommandAction::None;
    QString query;
};

QString compactValue(QString value, int maxLength = 96)
{
    value = value.simplified();
    if (value.size() <= maxLength) {
        return value;
    }
    return value.left(std::max(0, maxLength - 3)) + QStringLiteral("...");
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

QString inboxFilesSummary(const QStringList &filePaths)
{
    if (filePaths.isEmpty()) {
        return QStringLiteral("drop a file or use Explorer selection");
    }
    if (filePaths.size() == 1) {
        return QFileInfo(filePaths.first()).fileName();
    }
    return QStringLiteral("%1 files").arg(filePaths.size());
}

QStringList localFilePathsFromMimeData(const QMimeData *mimeData)
{
    QStringList filePaths;
    if (!mimeData || !mimeData->hasUrls()) {
        return filePaths;
    }

    for (const QUrl &url : mimeData->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString filePath = url.toLocalFile().trimmed();
        if (!filePath.isEmpty()) {
            appendUniqueValue(filePaths, filePath);
        }
    }
    return filePaths;
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

QString commandResourceKindLabel(ResourceKind kind)
{
    switch (kind) {
    case ResourceKind::Folder:
        return QStringLiteral("Folder");
    case ResourceKind::Pdf:
        return QStringLiteral("PDF");
    case ResourceKind::Markdown:
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

QString commandAnchorTitle(const Anchor &anchor, const PinloomOpenTarget &target)
{
    if (!anchor.name.trimmed().isEmpty()) {
        return anchor.name.trimmed();
    }
    if (!anchor.target.trimmed().isEmpty()) {
        return anchor.target.trimmed();
    }
    if (!target.title.trimmed().isEmpty()) {
        return target.title.trimmed();
    }
    return target.location.trimmed();
}

QString commandAnchorLocatorSummary(const Anchor &anchor)
{
    const QString locatorType = anchor.locatorType.trimmed();
    const QString locatorJson = compactValue(anchor.locatorJson.trimmed(), 72);
    if (!locatorType.isEmpty() && !locatorJson.isEmpty()) {
        return QStringLiteral("%1 %2").arg(locatorType, locatorJson);
    }
    if (!locatorType.isEmpty()) {
        return locatorType;
    }

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

QString commandAnchorHintsSummary(const Anchor &anchor)
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
        parts.append(QStringLiteral("aliases: %1").arg(compactValue(anchor.aliases.join(QStringLiteral(", ")), 64)));
    }
    return parts.join(QStringLiteral(" | "));
}

QString commandTargetTitle(const PinloomOpenTarget &target)
{
    if (target.anchor.has_value()) {
        return commandAnchorTitle(target.anchor.value(), target);
    }
    if (!target.title.trimmed().isEmpty()) {
        return target.title.trimmed();
    }
    if (!target.location.trimmed().isEmpty()) {
        const QString fileName = QFileInfo(target.location).fileName().trimmed();
        return fileName.isEmpty() ? target.location.trimmed() : fileName;
    }
    if (!target.clipId.trimmed().isEmpty()) {
        return target.clipId.trimmed();
    }
    return QStringLiteral("Untitled");
}

QString commandTargetKindLabel(const PinloomOpenTarget &target)
{
    if (!target.clipId.trimmed().isEmpty()) {
        return QStringLiteral("Clip");
    }
    if (target.anchor.has_value()) {
        return QStringLiteral("Anchor");
    }
    if (isInboxResourceId(target.resourceId)) {
        return QStringLiteral("Inbox");
    }
    return commandResourceKindLabel(target.resourceKind);
}

QString commandTargetVerb(const PinloomOpenTarget &target)
{
    if (!target.clipId.trimmed().isEmpty()) {
        return QStringLiteral("Insert");
    }
    if (target.anchor.has_value()) {
        return QStringLiteral("Jump");
    }
    return QStringLiteral("Open");
}

QString commandTargetDetails(const PinloomOpenTarget &target)
{
    QStringList details;
    if (target.anchor.has_value()) {
        const Anchor &anchor = target.anchor.value();
        if (!anchor.targetApp.trimmed().isEmpty()) {
            details.append(anchor.targetApp.trimmed());
        }

        QString targetPath = anchor.targetFile.trimmed();
        if (targetPath.isEmpty()) {
            targetPath = anchor.targetUri.trimmed();
        }
        if (targetPath.isEmpty()) {
            targetPath = target.location.trimmed();
        }
        if (!targetPath.isEmpty()) {
            details.append(compactValue(targetPath, 80));
        }

        const QString locator = commandAnchorLocatorSummary(anchor);
        if (!locator.isEmpty()) {
            details.append(locator);
        }
        const QString hints = commandAnchorHintsSummary(anchor);
        if (!hints.isEmpty()) {
            details.append(hints);
        }
    } else {
        const QString location = compactValue(target.location, 96);
        if (!location.isEmpty()) {
            details.append(location);
        }
    }

    const QString match = target.matchSummary.trimmed().isEmpty()
        ? target.matchedField.trimmed()
        : target.matchSummary.trimmed();
    if (!match.isEmpty()) {
        details.append(compactValue(match, 80));
    }

    return details.join(QStringLiteral(" | "));
}

QString commandTargetText(const PinloomOpenTarget &target)
{
    return QStringLiteral("[%1] %2 -> %3\n%4")
        .arg(commandTargetKindLabel(target),
             compactValue(commandTargetTitle(target), 84),
             commandTargetVerb(target),
             commandTargetDetails(target));
}

QString commandTargetToolTip(const PinloomOpenTarget &target)
{
    QStringList lines;
    lines.append(QStringLiteral("%1: %2").arg(commandTargetKindLabel(target), commandTargetTitle(target)));
    if (!target.resourceId.trimmed().isEmpty()) {
        lines.append(QStringLiteral("Resource: %1").arg(target.resourceId));
    }
    if (!target.clipId.trimmed().isEmpty()) {
        lines.append(QStringLiteral("Clip: %1").arg(target.clipId));
    }
    if (!target.location.trimmed().isEmpty()) {
        lines.append(QStringLiteral("Location: %1").arg(target.location));
    }
    if (target.anchor.has_value()) {
        const Anchor &anchor = target.anchor.value();
        lines.append(QStringLiteral("Anchor: %1").arg(anchor.id));
        const QString locator = commandAnchorLocatorSummary(anchor);
        if (!locator.isEmpty()) {
            lines.append(QStringLiteral("Locator: %1").arg(locator));
        }
        const QString hints = commandAnchorHintsSummary(anchor);
        if (!hints.isEmpty()) {
            lines.append(hints);
        }
    }
    if (!target.matchSummary.trimmed().isEmpty()) {
        lines.append(target.matchSummary.trimmed());
    } else if (!target.matchedField.trimmed().isEmpty()) {
        lines.append(QStringLiteral("Match: %1").arg(target.matchedField.trimmed()));
    }
    return lines.join(QLatin1Char('\n'));
}

int skipSpaces(const QString &text, int index)
{
    while (index < text.size() && text.at(index).isSpace()) {
        ++index;
    }
    return index;
}

QString readCommandToken(const QString &text, int *index)
{
    if (!index) {
        return {};
    }

    int cursor = skipSpaces(text, *index);
    const int start = cursor;
    while (cursor < text.size() && !text.at(cursor).isSpace()) {
        ++cursor;
    }

    *index = cursor;
    return text.mid(start, cursor - start);
}

CommandState parseCommandState(const QString &text)
{
    int index = 0;
    const QString firstToken = readCommandToken(text, &index).toCaseFolded();
    if (firstToken.isEmpty()) {
        return {};
    }

    if (firstToken == QLatin1String("c")) {
        CommandState state;
        state.commandNamespace = CommandNamespace::Clip;
        const QString option = readCommandToken(text, &index).toCaseFolded();
        if (option.isEmpty()) {
            return state;
        }
        if (option == QLatin1String("s")) {
            state.action = CommandAction::ClipSearch;
            state.query = text.mid(index).trimmed();
            return state;
        }
        if (option == QLatin1String("n")) {
            state.action = CommandAction::ClipNew;
            state.query = text.mid(index).trimmed();
            return state;
        }
        return state;
    }

    if (firstToken == QLatin1String("k")) {
        CommandState state;
        state.commandNamespace = CommandNamespace::Anchor;
        const QString option = readCommandToken(text, &index).toCaseFolded();
        if (option.isEmpty()) {
            return state;
        }
        if (option == QLatin1String("n")) {
            state.action = CommandAction::AnchorNew;
            state.query = text.mid(index).trimmed();
            return state;
        }
        return state;
    }

    if (firstToken == QLatin1String("i")) {
        CommandState state;
        state.commandNamespace = CommandNamespace::Inbox;
        const QString option = readCommandToken(text, &index).toCaseFolded();
        if (option.isEmpty()) {
            return state;
        }
        if (option == QLatin1String("n")) {
            state.action = CommandAction::InboxNew;
            state.query = text.mid(index).trimmed();
            return state;
        }
        if (option == QLatin1String("s")) {
            state.action = CommandAction::InboxSearch;
            state.query = text.mid(index).trimmed();
            return state;
        }
        return state;
    }

    if (firstToken == QLatin1String("s") || firstToken == QLatin1String("search")) {
        CommandState state;
        state.commandNamespace = CommandNamespace::Search;
        state.action = CommandAction::OpenSearch;
        state.query = text.mid(index).trimmed();
        return state;
    }

    return {};
}

CommandRowAction rowActionForItem(const QListWidgetItem *item)
{
    if (!item) {
        return CommandRowAction::Unknown;
    }
    return static_cast<CommandRowAction>(item->data(CommandActionRole).toInt());
}

ClipSearchResult clipResultForItem(const QListWidgetItem *item)
{
    ClipSearchResult result;
    const CommandRowAction action = rowActionForItem(item);
    if (!item || (action != CommandRowAction::ClipInsert
                  && action != CommandRowAction::ClipSave
                  && action != CommandRowAction::OpenUnifiedTarget)) {
        return result;
    }

    result.clipId = item->data(ClipIdRole).toString();
    result.displayName = item->data(ClipDisplayNameRole).toString();
    result.preview = item->data(ClipPreviewRole).toString();
    result.tags = item->data(ClipTagsRole).toStringList();
    result.aliases = item->data(ClipAliasesRole).toStringList();
    result.updatedAt = item->data(ClipUpdatedAtRole).toDateTime();
    result.usedAt = item->data(ClipUsedAtRole).toDateTime();
    result.state = static_cast<ClipState>(item->data(ClipStateRole).toInt());
    result.matchedField = item->data(ClipMatchedFieldRole).toString();
    result.matchedValue = item->data(ClipMatchedValueRole).toString();
    result.score = item->data(ClipScoreRole).toDouble();
    result.rank = item->data(ClipRankRole).toInt();
    result.pinned = item->data(ClipPinnedRole).toBool();
    result.createdAt = item->data(ClipCreatedAtRole).toDateTime();
    return result;
}

void storeClipResult(QListWidgetItem *item, const ClipSearchResult &result)
{
    item->setData(ClipIdRole, result.clipId);
    item->setData(ClipDisplayNameRole, result.displayName);
    item->setData(ClipPreviewRole, result.preview);
    item->setData(ClipTagsRole, result.tags);
    item->setData(ClipAliasesRole, result.aliases);
    item->setData(ClipUpdatedAtRole, result.updatedAt);
    item->setData(ClipUsedAtRole, result.usedAt);
    item->setData(ClipStateRole, static_cast<int>(result.state));
    item->setData(ClipMatchedFieldRole, result.matchedField);
    item->setData(ClipMatchedValueRole, result.matchedValue);
    item->setData(ClipScoreRole, result.score);
    item->setData(ClipRankRole, result.rank);
    item->setData(ClipPinnedRole, result.pinned);
    item->setData(ClipCreatedAtRole, result.createdAt);
}

void storeOpenTarget(QListWidgetItem *item, const PinloomOpenTarget &target)
{
    if (!item) {
        return;
    }

    item->setData(TargetResourceIdRole, target.resourceId);
    item->setData(TargetClipIdRole, target.clipId);
    item->setData(TargetKindRole, static_cast<int>(target.resourceKind));
    item->setData(TargetTitleRole, target.title);
    item->setData(TargetLocationRole, target.location);
    item->setData(TargetMatchedFieldRole, target.matchedField);
    item->setData(TargetScoreRole, target.score);
    item->setData(TargetMatchSummaryRole, target.matchSummary);

    if (!target.clipId.trimmed().isEmpty()) {
        item->setData(ClipIdRole, target.clipId);
        item->setData(ClipDisplayNameRole, target.title);
        item->setData(ClipPreviewRole, target.location);
        item->setData(ClipMatchedFieldRole, target.matchedField);
        item->setData(ClipScoreRole, target.score);
    }

    item->setData(TargetHasAnchorRole, target.anchor.has_value());
    if (!target.anchor.has_value()) {
        return;
    }

    const Anchor &anchor = target.anchor.value();
    item->setData(TargetAnchorTypeRole, static_cast<int>(anchor.type));
    item->setData(TargetAnchorTargetRole, anchor.target);
    item->setData(TargetAnchorLineRole, anchor.line);
    item->setData(TargetAnchorPageRole, anchor.page);
    item->setData(TargetAnchorRegionXRole, anchor.region.x());
    item->setData(TargetAnchorRegionYRole, anchor.region.y());
    item->setData(TargetAnchorRegionWidthRole, anchor.region.width());
    item->setData(TargetAnchorRegionHeightRole, anchor.region.height());
    item->setData(TargetAnchorIdRole, anchor.id);
    item->setData(TargetAnchorNameRole, anchor.name);
    item->setData(TargetAnchorTargetAppRole, anchor.targetApp);
    item->setData(TargetAnchorTargetFileRole, anchor.targetFile);
    item->setData(TargetAnchorTargetUriRole, anchor.targetUri);
    item->setData(TargetAnchorLocatorTypeRole, anchor.locatorType);
    item->setData(TargetAnchorLocatorJsonRole, anchor.locatorJson);
    item->setData(TargetAnchorAliasesRole, anchor.aliases);
    item->setData(TargetAnchorTagsRole, anchor.tags);
    item->setData(TargetAnchorPinnedRole, anchor.pinned);
    item->setData(TargetAnchorCreatedAtRole, anchor.createdAt);
    item->setData(TargetAnchorUpdatedAtRole, anchor.updatedAt);
    item->setData(TargetAnchorUsedAtRole, anchor.usedAt);
}

PinloomOpenTarget openTargetForCommandItem(const QListWidgetItem *item, int row = -1)
{
    PinloomOpenTarget target;
    if (!item) {
        return target;
    }

    target.resultRow = row;
    target.resourceId = item->data(TargetResourceIdRole).toString();
    target.clipId = item->data(TargetClipIdRole).toString();
    target.resourceKind = static_cast<ResourceKind>(item->data(TargetKindRole).toInt());
    target.title = item->data(TargetTitleRole).toString();
    target.location = item->data(TargetLocationRole).toString();
    target.matchedField = item->data(TargetMatchedFieldRole).toString();
    target.score = item->data(TargetScoreRole).toDouble();
    target.matchSummary = item->data(TargetMatchSummaryRole).toString();

    if (item->data(TargetHasAnchorRole).toBool()) {
        Anchor anchor;
        anchor.type = static_cast<AnchorType>(item->data(TargetAnchorTypeRole).toInt());
        anchor.target = item->data(TargetAnchorTargetRole).toString();
        anchor.line = item->data(TargetAnchorLineRole).toInt();
        anchor.page = item->data(TargetAnchorPageRole).toInt();
        anchor.region = QRectF(item->data(TargetAnchorRegionXRole).toDouble(),
                               item->data(TargetAnchorRegionYRole).toDouble(),
                               item->data(TargetAnchorRegionWidthRole).toDouble(),
                               item->data(TargetAnchorRegionHeightRole).toDouble());
        anchor.id = item->data(TargetAnchorIdRole).toString();
        anchor.name = item->data(TargetAnchorNameRole).toString();
        anchor.targetApp = item->data(TargetAnchorTargetAppRole).toString();
        anchor.targetFile = item->data(TargetAnchorTargetFileRole).toString();
        anchor.targetUri = item->data(TargetAnchorTargetUriRole).toString();
        anchor.locatorType = item->data(TargetAnchorLocatorTypeRole).toString();
        anchor.locatorJson = item->data(TargetAnchorLocatorJsonRole).toString();
        anchor.aliases = item->data(TargetAnchorAliasesRole).toStringList();
        anchor.tags = item->data(TargetAnchorTagsRole).toStringList();
        anchor.pinned = item->data(TargetAnchorPinnedRole).toBool();
        anchor.createdAt = item->data(TargetAnchorCreatedAtRole).toDateTime();
        anchor.updatedAt = item->data(TargetAnchorUpdatedAtRole).toDateTime();
        anchor.usedAt = item->data(TargetAnchorUsedAtRole).toDateTime();
        target.anchor = anchor;
    }

    return target;
}

QString commandActionText(const PinloomCommandResultAction &action, const PinloomOpenTarget &target)
{
    const QString label = action.enabled
        ? action.label.trimmed()
        : QStringLiteral("%1 (disabled)").arg(action.label.trimmed());
    QString detail = action.enabled
        ? action.detail.trimmed()
        : action.disabledReason.trimmed();
    if (detail.isEmpty()) {
        detail = action.detail.trimmed();
    }
    if (detail.isEmpty()) {
        detail = QStringLiteral("%1 %2").arg(action.label.trimmed(), commandTargetTitle(target));
    }
    return QStringLiteral("[Action] %1\n%2").arg(label, detail);
}

PinloomCommandResultAction resultActionForItem(const QListWidgetItem *item)
{
    PinloomCommandResultAction action;
    if (!item || rowActionForItem(item) != CommandRowAction::UnifiedTargetAction) {
        return action;
    }
    action.id = item->data(ResultActionIdRole).toString();
    action.label = item->data(ResultActionLabelRole).toString();
    action.detail = item->data(ResultActionDetailRole).toString();
    action.enabled = item->data(ResultActionEnabledRole).toBool();
    action.disabledReason = item->data(ResultActionDisabledReasonRole).toString();
    return action;
}

void storeResultAction(QListWidgetItem *item,
                       const PinloomOpenTarget &target,
                       const PinloomCommandResultAction &action)
{
    if (!item) {
        return;
    }
    item->setData(CommandActionRole, static_cast<int>(CommandRowAction::UnifiedTargetAction));
    item->setData(ResultActionIdRole, action.id);
    item->setData(ResultActionLabelRole, action.label);
    item->setData(ResultActionDetailRole, action.detail);
    item->setData(ResultActionEnabledRole, action.enabled);
    item->setData(ResultActionDisabledReasonRole, action.disabledReason);
    storeOpenTarget(item, target);
}

bool sameCommandTarget(const PinloomOpenTarget &left, const PinloomOpenTarget &right)
{
    if (!left.clipId.trimmed().isEmpty() || !right.clipId.trimmed().isEmpty()) {
        return !left.clipId.trimmed().isEmpty() && left.clipId == right.clipId;
    }
    if (left.resourceId != right.resourceId) {
        return false;
    }
    const QString leftAnchorId = left.anchor.has_value() ? left.anchor->id : QString();
    const QString rightAnchorId = right.anchor.has_value() ? right.anchor->id : QString();
    if (!leftAnchorId.isEmpty() || !rightAnchorId.isEmpty()) {
        return !leftAnchorId.isEmpty() && leftAnchorId == rightAnchorId;
    }
    return left.anchor.has_value() == right.anchor.has_value();
}

bool canExpandResultActions(const QListWidgetItem *item)
{
    if (!item || rowActionForItem(item) != CommandRowAction::OpenUnifiedTarget) {
        return false;
    }
    const PinloomOpenTarget target = openTargetForCommandItem(item);
    return !target.clipId.trimmed().isEmpty()
        || !target.resourceId.trimmed().isEmpty()
        || !target.location.trimmed().isEmpty();
}

} // namespace

PinloomCommandPanel::PinloomCommandPanel(QWidget *parent)
    : PinloomCommandPanel(PinloomCommandPanelOptions{}, parent)
{
}

PinloomCommandPanel::PinloomCommandPanel(PinloomCommandPanelOptions options, QWidget *parent)
    : QWidget(parent)
    , options_(std::move(options))
{
    setObjectName(QStringLiteral("pinloomCommandPanel"));
    setWindowTitle(tr("Pinloom Command"));
    setAcceptDrops(true);
    resize(760, 300);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 8);
    layout->setSpacing(6);

    commandEdit_ = new QLineEdit(this);
    commandEdit_->setObjectName(QStringLiteral("commandSearchEdit"));
    commandEdit_->setPlaceholderText(tr("Command"));
    commandEdit_->setClearButtonEnabled(true);
    commandEdit_->setAcceptDrops(true);

    resultList_ = new QListWidget(this);
    resultList_->setObjectName(QStringLiteral("commandResultList"));
    resultList_->setAlternatingRowColors(true);
    resultList_->setUniformItemSizes(true);
    resultList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    resultList_->setAcceptDrops(true);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("commandStatusLabel"));
    statusLabel_->setWordWrap(true);
    installStatusContextMenu(statusLabel_, this, [this]() {
        return statusText_;
    });

    layout->addWidget(commandEdit_);
    layout->addWidget(resultList_, 1);
    layout->addWidget(statusLabel_);

    commandEdit_->installEventFilter(this);
    resultList_->installEventFilter(this);

    connect(commandEdit_, &QLineEdit::textChanged, this, &PinloomCommandPanel::refreshResults);
    connect(commandEdit_, &QLineEdit::returnPressed, this, &PinloomCommandPanel::activateCurrentCommandItem);
    connect(resultList_, &QListWidget::itemActivated, this, &PinloomCommandPanel::activateResultItem);
    connect(resultList_, &QListWidget::itemDoubleClicked, this, &PinloomCommandPanel::activateResultItem);

    refreshResults();
    focusCommand();
}

void PinloomCommandPanel::setCommandText(const QString &text)
{
    commandEdit_->setText(text);
}

QString PinloomCommandPanel::commandText() const
{
    return commandEdit_->text();
}

void PinloomCommandPanel::openClipSearch(const QString &query)
{
    const QString trimmedQuery = query.trimmed();
    setCommandText(trimmedQuery.isEmpty()
                       ? QStringLiteral("c s")
                       : QStringLiteral("c s %1").arg(trimmedQuery));
    focusCommand();
}

void PinloomCommandPanel::setPendingInboxFiles(const QStringList &filePaths)
{
    pendingInboxFiles_.clear();
    for (const QString &filePath : filePaths) {
        appendUniqueValue(pendingInboxFiles_, filePath);
    }
    refreshResults();
}

QStringList PinloomCommandPanel::pendingInboxFiles() const
{
    return pendingInboxFiles_;
}

void PinloomCommandPanel::focusCommand()
{
    commandEdit_->setFocus(Qt::ShortcutFocusReason);
    commandEdit_->selectAll();
}

QString PinloomCommandPanel::statusText() const
{
    return statusText_;
}

int PinloomCommandPanel::resultCount() const
{
    return resultList_->count();
}

ClipSearchResult PinloomCommandPanel::resultAt(int row) const
{
    if (row < 0 || row >= resultList_->count()) {
        return {};
    }
    return clipResultForItem(resultList_->item(row));
}

ClipSearchResult PinloomCommandPanel::currentResult() const
{
    return clipResultForItem(resultList_->currentItem());
}

PinloomOpenTarget PinloomCommandPanel::openTargetAt(int row) const
{
    if (row < 0 || row >= resultList_->count()) {
        return {};
    }
    return openTargetForCommandItem(resultList_->item(row), row);
}

PinloomOpenTarget PinloomCommandPanel::currentOpenTarget() const
{
    return openTargetForCommandItem(resultList_->currentItem(), resultList_->currentRow());
}

bool PinloomCommandPanel::selectResultAt(int row)
{
    if (row < 0 || row >= resultList_->count()) {
        return false;
    }
    resultList_->setCurrentRow(row);
    return resultList_->currentItem() != nullptr;
}

bool PinloomCommandPanel::selectFirstResult()
{
    return selectResultAt(0);
}

bool PinloomCommandPanel::selectNextResult()
{
    const int count = resultList_->count();
    if (count <= 0) {
        return false;
    }

    const int currentRow = resultList_->currentRow();
    const int nextRow = currentRow < 0 ? 0 : std::min(currentRow + 1, count - 1);
    return selectResultAt(nextRow);
}

bool PinloomCommandPanel::selectPreviousResult()
{
    const int count = resultList_->count();
    if (count <= 0) {
        return false;
    }

    const int currentRow = resultList_->currentRow();
    const int previousRow = currentRow < 0 ? 0 : std::max(currentRow - 1, 0);
    return selectResultAt(previousRow);
}

bool PinloomCommandPanel::activateCurrentCommandItem()
{
    QListWidgetItem *item = resultList_->currentItem();
    if (!item && resultList_->count() > 0) {
        resultList_->setCurrentRow(0);
        item = resultList_->currentItem();
    }

    if (!item) {
        const CommandState command = parseCommandState(commandEdit_ ? commandEdit_->text() : QString());
        if (command.commandNamespace == CommandNamespace::Clip
            && command.action == CommandAction::ClipSearch) {
            updateStatus(tr("No clips to insert"));
        } else if (command.commandNamespace == CommandNamespace::Clip
                   && command.action == CommandAction::ClipNew) {
            updateStatus(tr("No clip selected"));
        } else if (command.commandNamespace == CommandNamespace::Anchor
                   && command.action == CommandAction::AnchorNew) {
            updateStatus(tr("No anchor context available"));
        } else if (command.commandNamespace == CommandNamespace::Inbox
                   && command.action == CommandAction::InboxNew) {
            updateStatus(tr("No pending Inbox file; drop a file or select one in Explorer"));
        } else if (command.commandNamespace == CommandNamespace::Inbox
                   && command.action == CommandAction::InboxSearch) {
            updateStatus(tr("No Inbox search command selected"));
        } else if (command.commandNamespace == CommandNamespace::None
                   && commandEdit_
                   && !commandEdit_->text().trimmed().isEmpty()) {
            updateStatus(options_.unifiedSearchHandler
                             ? tr("No unified results")
                             : tr("Unified search is not configured"));
        } else {
            updateStatus(tr("Unknown command"));
        }
        return false;
    }

    return activateCommandItem(item);
}

bool PinloomCommandPanel::showActionsForCurrentResult()
{
    QListWidgetItem *item = resultList_->currentItem();
    if (!item && resultList_->count() > 0) {
        resultList_->setCurrentRow(0);
        item = resultList_->currentItem();
    }
    if (!canExpandResultActions(item)) {
        updateStatus(tr("Select a unified result before opening actions"));
        return false;
    }

    const int sourceRow = resultList_->row(item);
    const PinloomOpenTarget target = openTargetForCommandItem(item, sourceRow);
    populateActionResults(target, sourceRow);
    return true;
}

bool PinloomCommandPanel::returnToResultList()
{
    if (!showingResultActions_) {
        return false;
    }

    const PinloomOpenTarget target = actionSourceTarget_;
    const int sourceRow = actionSourceRow_;
    showingResultActions_ = false;
    actionSourceTarget_ = {};
    actionSourceRow_ = -1;
    refreshResults();
    restoreResultSelection(target, sourceRow);
    return true;
}

bool PinloomCommandPanel::isShowingResultActions() const
{
    return showingResultActions_;
}

bool PinloomCommandPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == commandEdit_ || watched == resultList_) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            return handleInboxDragEnter(event);
        }
        if (event->type() == QEvent::Drop) {
            return handleInboxDrop(event);
        }
    }

    if ((watched == commandEdit_ || watched == resultList_) && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const Qt::KeyboardModifiers modifiers =
            keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
        const int key = keyEvent->key();

        if (showingResultActions_
            && (key == Qt::Key_Left || key == Qt::Key_Escape)
            && modifiers == Qt::NoModifier) {
            returnToResultList();
            return true;
        }
        if (!showingResultActions_
            && key == Qt::Key_Right
            && modifiers == Qt::NoModifier) {
            return showActionsForCurrentResult();
        }
        if (watched == resultList_
            && (key == Qt::Key_Return || key == Qt::Key_Enter)
            && modifiers == Qt::NoModifier) {
            activateCurrentCommandItem();
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
            window()->hide();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void PinloomCommandPanel::dragEnterEvent(QDragEnterEvent *event)
{
    handleInboxDragEnter(event);
}

void PinloomCommandPanel::dragMoveEvent(QDragMoveEvent *event)
{
    handleInboxDragEnter(event);
}

void PinloomCommandPanel::dropEvent(QDropEvent *event)
{
    handleInboxDrop(event);
}

void PinloomCommandPanel::refreshResults()
{
    showingResultActions_ = false;
    actionSourceTarget_ = {};
    actionSourceRow_ = -1;
    resultList_->clear();

    const CommandState command = parseCommandState(commandEdit_->text());
    QStringList listedClipIds;

    const auto appendCommandResult = [this](CommandRowAction action,
                                            const QString &nextCommandText,
                                            const QString &title,
                                            const QString &verb,
                                            const QString &detail) {
        auto *item = new QListWidgetItem(QStringLiteral("[Command] %1 -> %2\n%3").arg(title, verb, detail),
                                         resultList_);
        item->setSizeHint(QSize(0, resultList_->fontMetrics().lineSpacing() * 2 + 12));
        item->setData(CommandActionRole, static_cast<int>(action));
        item->setData(CommandSearchTextRole, nextCommandText);
    };
    const auto appendClipResults = [this, &listedClipIds](const QList<ClipSearchResult> &clipResults,
                                                          CommandRowAction action) {
        const QString actionText = action == CommandRowAction::ClipSave
            ? QStringLiteral("save")
            : QStringLiteral("insert");
        for (const ClipSearchResult &result : clipResults) {
            if (!result.clipId.isEmpty() && listedClipIds.contains(result.clipId)) {
                continue;
            }
            listedClipIds.append(result.clipId);

            auto *item = new QListWidgetItem(clipResultText(result, actionText), resultList_);
            item->setToolTip(clipToolTip(result));
            item->setSizeHint(QSize(0, resultList_->fontMetrics().lineSpacing() * 2 + 12));
            item->setData(CommandActionRole, static_cast<int>(action));
            storeClipResult(item, result);
        }
    };
    const auto appendUnifiedResults = [this](const QList<PinloomOpenTarget> &targets) {
        for (const PinloomOpenTarget &target : targets) {
            auto *item = new QListWidgetItem(commandTargetText(target), resultList_);
            item->setToolTip(commandTargetToolTip(target));
            item->setSizeHint(QSize(0, resultList_->fontMetrics().lineSpacing() * 2 + 12));
            item->setData(CommandActionRole, static_cast<int>(CommandRowAction::OpenUnifiedTarget));
            storeOpenTarget(item, target);
        }
    };
    const auto appendUnifiedEntries = [&appendUnifiedResults](const QList<PinloomEntry> &entries) {
        QList<PinloomOpenTarget> targets;
        const QList<PinloomEntry> sortedEntries = sortedPinloomEntries(entries);
        targets.reserve(sortedEntries.size());
        for (const PinloomEntry &entry : sortedEntries) {
            targets.append(openTargetFromEntry(entry));
        }
        appendUnifiedResults(targets);
    };

    constexpr int resultLimit = 100;
    const QString plainQuery = commandEdit_->text().trimmed();
    if (command.commandNamespace == CommandNamespace::None
        && !plainQuery.isEmpty()
        && (options_.unifiedEntrySearchHandler || options_.unifiedSearchHandler)) {
        if (options_.unifiedEntrySearchHandler) {
            appendUnifiedEntries(options_.unifiedEntrySearchHandler(plainQuery));
        } else {
            appendUnifiedResults(options_.unifiedSearchHandler(plainQuery));
        }
    } else if (command.commandNamespace == CommandNamespace::Clip
        && command.action == CommandAction::None) {
        appendCommandResult(CommandRowAction::OpenCommand,
                            QStringLiteral("c s"),
                            tr("Clip Search"),
                            tr("Open"),
                            tr("c s <query> - search Clip name, alias, tag, or content"));
        appendCommandResult(CommandRowAction::OpenCommand,
                            QStringLiteral("c n"),
                            tr("New Saved Clip"),
                            tr("Open"),
                            tr("c n - save a recent clipboard history item"));
    } else if (command.commandNamespace == CommandNamespace::Clip
               && command.action == CommandAction::ClipSearch
               && options_.clipSearchHandler) {
        if (command.query.isEmpty()) {
            ClipSearchOptions temporaryOptions;
            temporaryOptions.includeSaved = false;
            temporaryOptions.includeTemporary = true;
            temporaryOptions.emptyQueryReturnsPinnedAndRecent = true;
            temporaryOptions.limit = resultLimit;
            appendClipResults(options_.clipSearchHandler(command.query, temporaryOptions),
                              CommandRowAction::ClipInsert);

            ClipSearchOptions savedOptions;
            savedOptions.includeSaved = true;
            savedOptions.includeTemporary = false;
            savedOptions.emptyQueryReturnsPinnedAndRecent = true;
            savedOptions.limit = std::max(0, resultLimit - resultList_->count());
            appendClipResults(options_.clipSearchHandler(command.query, savedOptions),
                              CommandRowAction::ClipInsert);
        } else {
            ClipSearchOptions clipOptions;
            clipOptions.includeSaved = true;
            clipOptions.includeTemporary = true;
            clipOptions.emptyQueryReturnsPinnedAndRecent = true;
            clipOptions.limit = resultLimit;
            appendClipResults(options_.clipSearchHandler(command.query, clipOptions),
                              CommandRowAction::ClipInsert);
        }
    } else if (command.commandNamespace == CommandNamespace::Clip
               && command.action == CommandAction::ClipNew
               && options_.clipSearchHandler) {
        ClipSearchOptions temporaryOptions;
        temporaryOptions.includeSaved = false;
        temporaryOptions.includeTemporary = true;
        temporaryOptions.emptyQueryReturnsPinnedAndRecent = true;
        temporaryOptions.limit = resultLimit;
        appendClipResults(options_.clipSearchHandler(command.query, temporaryOptions),
                          CommandRowAction::ClipSave);
    } else if (command.commandNamespace == CommandNamespace::Anchor
               && command.action == CommandAction::None) {
        appendCommandResult(CommandRowAction::OpenCommand,
                            QStringLiteral("k n"),
                            tr("New Anchor / Capture Anchor"),
                            tr("Open"),
                            tr("k n - capture current app position"));
    } else if (command.commandNamespace == CommandNamespace::Anchor
               && command.action == CommandAction::AnchorNew) {
        appendCommandResult(CommandRowAction::AnchorCapture,
                            QStringLiteral("k n"),
                            tr("New Anchor / Capture Anchor"),
                            tr("Capture"),
                            tr("k n - capture current app position"));
    } else if (command.commandNamespace == CommandNamespace::Inbox
               && command.action == CommandAction::None) {
        appendCommandResult(CommandRowAction::OpenCommand,
                            QStringLiteral("i n"),
                            tr("New Inbox File"),
                            tr("Open"),
                            tr("i n - save pending dropped file or current Explorer selection"));
        appendCommandResult(CommandRowAction::OpenCommand,
                            QStringLiteral("i s"),
                            tr("Inbox Search"),
                            tr("Open"),
                            tr("i s <query> - search archived Inbox files"));
    } else if (command.commandNamespace == CommandNamespace::Inbox
               && command.action == CommandAction::InboxNew) {
        appendCommandResult(CommandRowAction::InboxSave,
                            QStringLiteral("i n"),
                            tr("New Inbox File"),
                            tr("Save"),
                            tr("Link mode - %1").arg(inboxFilesSummary(pendingInboxFiles_)));
    } else if (command.commandNamespace == CommandNamespace::Inbox
               && command.action == CommandAction::InboxSearch) {
        auto *item = new QListWidgetItem(QStringLiteral("[Command] Inbox Search -> Open\ni s <query> - open Inbox results in Pinloom search"),
                                         resultList_);
        item->setSizeHint(QSize(0, resultList_->fontMetrics().lineSpacing() * 2 + 12));
        item->setData(CommandActionRole, static_cast<int>(CommandRowAction::OpenSearchWindow));
        item->setData(CommandSearchQueryRole, command.query);
    } else if (command.commandNamespace == CommandNamespace::Search
               && command.action == CommandAction::OpenSearch) {
        auto *item = new QListWidgetItem(QStringLiteral("[Command] Pinloom Search -> Open\nsearch <query> - open the search window"),
                                         resultList_);
        item->setSizeHint(QSize(0, resultList_->fontMetrics().lineSpacing() * 2 + 12));
        item->setData(CommandActionRole, static_cast<int>(CommandRowAction::OpenSearchWindow));
        item->setData(CommandSearchQueryRole, command.query);
    }

    if (resultList_->count() > 0) {
        resultList_->setCurrentRow(0);
    }

    if (commandEdit_->text().trimmed().isEmpty()) {
        updateStatus(tr("Type to search Anchor, Clip, Inbox, or File; c/k/i for commands"));
    } else if (command.commandNamespace == CommandNamespace::None) {
        if (!options_.unifiedSearchHandler) {
            updateStatus(tr("Unified search is not configured"));
        } else {
            updateStatus(resultList_->count() > 0
                             ? tr("Unified search: %n result(s)", nullptr, resultList_->count())
                             : tr("No unified results"));
        }
    } else if (command.commandNamespace == CommandNamespace::Clip
               && command.action == CommandAction::None) {
        updateStatus(tr("Clip commands"));
    } else if (command.commandNamespace == CommandNamespace::Clip
               && command.action == CommandAction::ClipSearch) {
        updateStatus(resultList_->count() > 0
                         ? tr("Clip search: %n clip(s)", nullptr, resultList_->count())
                         : tr("No clips to insert"));
    } else if (command.commandNamespace == CommandNamespace::Clip
               && command.action == CommandAction::ClipNew) {
        updateStatus(resultList_->count() > 0
                         ? tr("Clip save: %n history item(s)", nullptr, resultList_->count())
                         : tr("No clip selected"));
    } else if (command.commandNamespace == CommandNamespace::Anchor
               && command.action == CommandAction::None) {
        updateStatus(tr("Anchor commands"));
    } else if (command.commandNamespace == CommandNamespace::Anchor
               && command.action == CommandAction::AnchorNew) {
        updateStatus(tr("Capture anchor current app context pending"));
    } else if (command.commandNamespace == CommandNamespace::Inbox
               && command.action == CommandAction::None) {
        updateStatus(tr("Inbox commands"));
    } else if (command.commandNamespace == CommandNamespace::Inbox
               && command.action == CommandAction::InboxNew) {
        updateStatus(pendingInboxFiles_.isEmpty()
                         ? tr("Inbox: drop a file or use Explorer selection")
                         : tr("Inbox pending: %1").arg(inboxFilesSummary(pendingInboxFiles_)));
    } else if (command.commandNamespace == CommandNamespace::Inbox
               && command.action == CommandAction::InboxSearch) {
        updateStatus(command.query.trimmed().isEmpty()
                         ? tr("Open Inbox search")
                         : tr("Open Inbox search for \"%1\"").arg(command.query.trimmed()));
    } else if (command.commandNamespace == CommandNamespace::Search
               && command.action == CommandAction::OpenSearch) {
        updateStatus(tr("Open Pinloom search window"));
    } else {
        updateStatus(tr("Unknown command"));
    }
}

void PinloomCommandPanel::activateResultItem(QListWidgetItem *item)
{
    if (!item) {
        return;
    }
    resultList_->setCurrentItem(item);
    activateCommandItem(item);
}

void PinloomCommandPanel::updateStatus(const QString &status)
{
    statusText_ = status;
    statusLabel_->setText(statusText_);
    if (options_.statusChangedHandler) {
        options_.statusChangedHandler(statusText_);
    }
    emit statusChanged(statusText_);
}

bool PinloomCommandPanel::activateCommandItem(QListWidgetItem *item)
{
    if (!item) {
        updateStatus(tr("Unknown command"));
        return false;
    }

    resultList_->setCurrentItem(item);
    const CommandRowAction action = rowActionForItem(item);
    if (action == CommandRowAction::OpenCommand) {
        const QString nextCommandText = item->data(CommandSearchTextRole).toString().trimmed();
        if (!nextCommandText.isEmpty()) {
            setCommandText(nextCommandText);
            return true;
        }
        updateStatus(tr("Unknown command"));
        return false;
    }

    if (action == CommandRowAction::OpenUnifiedTarget) {
        return activateUnifiedTargetFromItem(item);
    }
    if (action == CommandRowAction::ClipInsert) {
        return insertClipFromItem(item);
    }
    if (action == CommandRowAction::ClipSave) {
        return saveClipFromItem(item);
    }
    if (action == CommandRowAction::AnchorCapture) {
        return captureAnchor();
    }
    if (action == CommandRowAction::InboxSave) {
        return saveInboxFromCommand();
    }
    if (action == CommandRowAction::OpenSearchWindow) {
        return openSearchWindow(item);
    }
    if (action == CommandRowAction::UnifiedTargetAction) {
        return activateResultActionFromItem(item);
    }

    updateStatus(tr("Unknown command"));
    return false;
}

bool PinloomCommandPanel::insertClipFromItem(const QListWidgetItem *item)
{
    const ClipSearchResult result = clipResultForItem(item);
    if (result.clipId.trimmed().isEmpty()) {
        updateStatus(tr("No clips to insert"));
        return false;
    }
    if (!options_.clipInsertionHandler) {
        updateStatus(tr("Clip insertion is not configured"));
        return false;
    }

    QString error;
    if (!options_.clipInsertionHandler(result.clipId, &error)) {
        updateStatus(error.trimmed().isEmpty() ? tr("Clip insertion failed") : error.trimmed());
        return false;
    }

    updateStatus(tr("Inserted clip"));
    emit clipInserted(result.clipId);
    return true;
}

bool PinloomCommandPanel::activateUnifiedTargetFromItem(const QListWidgetItem *item)
{
    const PinloomOpenTarget target =
        openTargetForCommandItem(item, item ? resultList_->row(item) : -1);
    return activateUnifiedTarget(target);
}

bool PinloomCommandPanel::activateUnifiedTarget(const PinloomOpenTarget &target)
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
        emit clipInserted(target.clipId);
        return true;
    }

    if (target.anchor.has_value()) {
        if (!options_.anchorJumpHandler && !options_.resourceOpenHandler) {
            updateStatus(tr("Anchor jump is not configured"));
            return false;
        }

        QString status;
        const bool jumped = options_.anchorJumpHandler
            ? options_.anchorJumpHandler(target, &status)
            : options_.resourceOpenHandler(target, &status);
        if (!jumped) {
            updateStatus(status.trimmed().isEmpty() ? tr("Anchor jump failed") : status.trimmed());
            return false;
        }

        updateStatus(status.trimmed().isEmpty() ? tr("Jumped anchor") : status.trimmed());
        emit anchorJumped(target.resourceId);
        return true;
    }

    if (target.resourceId.trimmed().isEmpty() && target.location.trimmed().isEmpty()) {
        updateStatus(tr("No unified result selected"));
        return false;
    }
    if (!options_.resourceOpenHandler) {
        updateStatus(tr("Resource opening is not configured"));
        return false;
    }

    QString status;
    if (!options_.resourceOpenHandler(target, &status)) {
        updateStatus(status.trimmed().isEmpty() ? tr("Resource open failed") : status.trimmed());
        return false;
    }

    updateStatus(status.trimmed().isEmpty()
                     ? (isInboxResourceId(target.resourceId) ? tr("Opened Inbox file") : tr("Opened resource"))
                     : status.trimmed());
    emit resourceOpened(target.resourceId);
    return true;
}

QList<PinloomCommandResultAction> PinloomCommandPanel::actionsForTarget(const PinloomOpenTarget &target) const
{
    QList<PinloomCommandResultAction> actions;
    if (options_.unifiedEntryActionProvider) {
        actions = options_.unifiedEntryActionProvider(entryFromOpenTarget(target));
    } else if (options_.unifiedActionProvider) {
        actions = options_.unifiedActionProvider(target);
    }

    const bool hasPrimaryAction = std::any_of(actions.cbegin(), actions.cend(), [](const PinloomCommandResultAction &action) {
        return action.id == QLatin1String(PrimaryResultActionId);
    });
    if (!hasPrimaryAction) {
        PinloomCommandResultAction primary;
        primary.id = QString::fromLatin1(PrimaryResultActionId);
        primary.label = commandTargetVerb(target);
        primary.detail = QStringLiteral("%1 %2").arg(primary.label, commandTargetTitle(target));
        actions.prepend(primary);
    }

    for (PinloomCommandResultAction &action : actions) {
        action.id = action.id.trimmed();
        action.label = action.label.trimmed();
        if (action.id.isEmpty()) {
            action.id = action.label.toCaseFolded().replace(QLatin1Char(' '), QLatin1Char('_'));
        }
        if (action.label.isEmpty()) {
            action.label = action.id;
        }
    }
    return actions;
}

void PinloomCommandPanel::populateActionResults(const PinloomOpenTarget &target, int sourceRow)
{
    const QList<PinloomCommandResultAction> actions = actionsForTarget(target);

    showingResultActions_ = true;
    actionSourceTarget_ = target;
    actionSourceRow_ = sourceRow;
    resultList_->clear();

    for (const PinloomCommandResultAction &action : actions) {
        auto *item = new QListWidgetItem(commandActionText(action, target), resultList_);
        item->setSizeHint(QSize(0, resultList_->fontMetrics().lineSpacing() * 2 + 12));
        item->setToolTip(action.disabledReason.trimmed().isEmpty()
                             ? action.detail
                             : action.disabledReason);
        storeResultAction(item, target, action);
    }
    if (resultList_->count() > 0) {
        resultList_->setCurrentRow(0);
    }

    updateStatus(tr("Actions for %1; Enter runs, Esc/Left returns").arg(commandTargetTitle(target)));
}

bool PinloomCommandPanel::activateResultActionFromItem(const QListWidgetItem *item)
{
    const PinloomCommandResultAction action = resultActionForItem(item);
    const PinloomOpenTarget target =
        openTargetForCommandItem(item, item ? resultList_->row(item) : -1);
    if (action.id.trimmed().isEmpty()) {
        updateStatus(tr("Unknown result action"));
        return false;
    }
    if (!action.enabled) {
        updateStatus(action.disabledReason.trimmed().isEmpty()
                         ? tr("Action is not available")
                         : action.disabledReason.trimmed());
        return false;
    }
    if (action.id == QLatin1String(PrimaryResultActionId)) {
        return activateUnifiedTarget(target);
    }
    if (options_.unifiedEntryActionHandler) {
        QString status;
        if (!options_.unifiedEntryActionHandler(this, entryFromOpenTarget(target), action, &status)) {
            updateStatus(status.trimmed().isEmpty()
                             ? tr("Unable to run action \"%1\"").arg(action.label)
                             : status.trimmed());
            return false;
        }

        updateStatus(status.trimmed().isEmpty()
                         ? tr("Completed action \"%1\"").arg(action.label)
                         : status.trimmed());
        return true;
    }
    if (!options_.unifiedActionHandler) {
        updateStatus(tr("Result action is not configured"));
        return false;
    }

    QString status;
    if (!options_.unifiedActionHandler(this, target, action, &status)) {
        updateStatus(status.trimmed().isEmpty()
                         ? tr("Unable to run action \"%1\"").arg(action.label)
                         : status.trimmed());
        return false;
    }

    updateStatus(status.trimmed().isEmpty()
                     ? tr("Completed action \"%1\"").arg(action.label)
                     : status.trimmed());
    return true;
}

bool PinloomCommandPanel::restoreResultSelection(const PinloomOpenTarget &target, int fallbackRow)
{
    if (fallbackRow >= 0 && fallbackRow < resultList_->count()) {
        const PinloomOpenTarget candidate = openTargetForCommandItem(resultList_->item(fallbackRow), fallbackRow);
        if (sameCommandTarget(candidate, target)) {
            resultList_->setCurrentRow(fallbackRow);
            return true;
        }
    }

    for (int row = 0; row < resultList_->count(); ++row) {
        const PinloomOpenTarget candidate = openTargetForCommandItem(resultList_->item(row), row);
        if (sameCommandTarget(candidate, target)) {
            resultList_->setCurrentRow(row);
            return true;
        }
    }
    return false;
}

bool PinloomCommandPanel::saveClipFromItem(const QListWidgetItem *item)
{
    const ClipSearchResult result = clipResultForItem(item);
    if (result.clipId.trimmed().isEmpty()) {
        updateStatus(tr("No clip selected"));
        return false;
    }
    if (result.state == ClipState::Saved) {
        updateStatus(tr("Clip is already saved"));
        return false;
    }
    if (!options_.clipSaveHandler) {
        updateStatus(tr("Clip saving is not configured"));
        return false;
    }

    std::optional<PinloomClipSaveRequest> request = options_.clipSaveRequestProvider
        ? options_.clipSaveRequestProvider(this, result)
        : promptClipSaveRequest(result);
    if (!request.has_value()) {
        updateStatus(tr("Save canceled"));
        return false;
    }

    if (request->clipId.trimmed().isEmpty()) {
        request->clipId = result.clipId;
    }
    request->name = request->name.trimmed();
    request->aliases = cleanedValues(request->aliases);
    request->tags = cleanedValues(request->tags, true);

    QString error;
    if (!options_.clipSaveHandler(request.value(), &error)) {
        updateStatus(error.trimmed().isEmpty() ? tr("Unable to save clip") : error.trimmed());
        return false;
    }

    const QString savedName = request->name.isEmpty() ? result.preview : request->name;
    setCommandText(QStringLiteral("c s %1").arg(savedName));
    updateStatus(tr("Saved clip \"%1\"").arg(savedName));
    emit clipSaved(result.clipId);
    return true;
}

bool PinloomCommandPanel::captureAnchor()
{
    emit anchorCaptureRequested();
    if (!options_.anchorCaptureHandler) {
        updateStatus(tr("No anchor context available"));
        return false;
    }

    QString status;
    const bool captured = options_.anchorCaptureHandler(&status);
    if (status.trimmed().isEmpty()) {
        status = captured ? tr("Captured anchor") : tr("No anchor context available");
    }
    updateStatus(status.trimmed());
    return captured;
}

bool PinloomCommandPanel::saveInboxFromCommand()
{
    if (!options_.inboxSaveHandler) {
        updateStatus(tr("Inbox saving is not configured"));
        return false;
    }

    QStringList filePaths = pendingInboxFiles_;
    if (filePaths.isEmpty() && options_.inboxSelectionProvider) {
        QString selectionStatus;
        filePaths = options_.inboxSelectionProvider(&selectionStatus);
        if (filePaths.isEmpty() && !selectionStatus.trimmed().isEmpty()) {
            updateStatus(selectionStatus.trimmed());
            return false;
        }
    }

    filePaths.removeDuplicates();
    if (filePaths.isEmpty()) {
        updateStatus(tr("No pending Inbox file; drop a file or select one in Explorer"));
        return false;
    }

    QStringList savedResourceIds;
    QStringList savedNames;
    QString lastStatus;
    for (int i = 0; i < filePaths.size(); ++i) {
        const QString filePath = filePaths.at(i);
        InboxFileSaveRequest request;
        if (filePaths.size() == 1) {
            std::optional<InboxFileSaveRequest> promptedRequest = options_.inboxSaveRequestProvider
                ? options_.inboxSaveRequestProvider(this, filePath)
                : promptInboxSaveRequest(filePath);
            if (!promptedRequest.has_value()) {
                updateStatus(tr("Inbox save canceled"));
                return false;
            }
            request = promptedRequest.value();
        } else {
            request.filePath = filePath;
            request.name = defaultInboxFileName(filePath);
            request.mode = InboxFileArchiveMode::Link;
        }

        if (request.filePath.trimmed().isEmpty()) {
            request.filePath = filePath;
        }
        request.name = request.name.trimmed();
        request.aliases = cleanedValues(request.aliases);
        request.tags = cleanedValues(request.tags, true);
        request.mode = InboxFileArchiveMode::Link;

        if (request.name.isEmpty()) {
            request.name = defaultInboxFileName(request.filePath);
        }

        QString status;
        if (!options_.inboxSaveHandler(request, &status)) {
            updateStatus(status.trimmed().isEmpty() ? tr("Unable to save Inbox file") : status.trimmed());
            return false;
        }
        savedResourceIds.append(inboxResourceIdForPath(request.filePath));
        savedNames.append(request.name);
        lastStatus = status.trimmed();
    }

    pendingInboxFiles_.clear();

    if (savedResourceIds.size() == 1) {
        const QString savedName = savedNames.first().trimmed();
        setCommandText(savedName.isEmpty()
                           ? QStringLiteral("i s")
                           : QStringLiteral("i s %1").arg(savedName));
        updateStatus(lastStatus.isEmpty()
                         ? tr("Saved Inbox file \"%1\"").arg(savedName)
                         : lastStatus);
        emit inboxSaved(savedResourceIds.first());
        return true;
    }

    setCommandText(QStringLiteral("i s"));
    updateStatus(tr("Saved %n Inbox file(s) in Link mode", nullptr, savedResourceIds.size()));
    for (const QString &resourceId : savedResourceIds) {
        emit inboxSaved(resourceId);
    }
    return true;
}

bool PinloomCommandPanel::openSearchWindow(const QListWidgetItem *item)
{
    const QString query = item ? item->data(CommandSearchQueryRole).toString() : QString();
    if (!options_.searchWindowHandler) {
        updateStatus(tr("Pinloom search window is not configured"));
        return false;
    }

    options_.searchWindowHandler(query);
    updateStatus(query.trimmed().isEmpty()
                     ? tr("Opened Pinloom search")
                     : tr("Opened Pinloom search for \"%1\"").arg(query.trimmed()));
    emit searchWindowRequested(query);
    return true;
}

std::optional<PinloomClipSaveRequest> PinloomCommandPanel::promptClipSaveRequest(const ClipSearchResult &result)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Save Clip"));
    auto *form = new QFormLayout(&dialog);
    auto *nameEdit = new QLineEdit(result.displayName.trimmed().isEmpty() ? result.preview : result.displayName,
                                   &dialog);
    nameEdit->setObjectName(QStringLiteral("commandClipSaveNameEdit"));
    auto *aliasesEdit = new QLineEdit(&dialog);
    aliasesEdit->setObjectName(QStringLiteral("commandClipSaveAliasesEdit"));
    auto *tagsEdit = new QLineEdit(&dialog);
    tagsEdit->setObjectName(QStringLiteral("commandClipSaveTagsEdit"));
    auto *pinnedCheck = new QCheckBox(tr("Pinned"), &dialog);
    pinnedCheck->setObjectName(QStringLiteral("commandClipSavePinnedCheck"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName(QStringLiteral("commandClipSaveButtons"));

    form->addRow(tr("Name"), nameEdit);
    form->addRow(tr("Aliases"), aliasesEdit);
    form->addRow(tr("Tags"), tagsEdit);
    form->addRow(QString(), pinnedCheck);
    form->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    PinloomClipSaveRequest request;
    request.clipId = result.clipId;
    request.name = nameEdit->text();
    request.aliases = valuesFromCommaText(aliasesEdit->text());
    request.tags = valuesFromCommaText(tagsEdit->text(), true);
    request.pinned = pinnedCheck->isChecked();
    return request;
}

std::optional<InboxFileSaveRequest> PinloomCommandPanel::promptInboxSaveRequest(const QString &filePath)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Save Inbox File"));
    auto *form = new QFormLayout(&dialog);
    auto *nameEdit = new QLineEdit(defaultInboxFileName(filePath), &dialog);
    nameEdit->setObjectName(QStringLiteral("commandInboxSaveNameEdit"));
    auto *aliasesEdit = new QLineEdit(&dialog);
    aliasesEdit->setObjectName(QStringLiteral("commandInboxSaveAliasesEdit"));
    auto *tagsEdit = new QLineEdit(&dialog);
    tagsEdit->setObjectName(QStringLiteral("commandInboxSaveTagsEdit"));
    auto *modeLabel = new QLabel(tr("Link"), &dialog);
    modeLabel->setObjectName(QStringLiteral("commandInboxSaveModeLabel"));
    auto *pinnedCheck = new QCheckBox(tr("Pinned"), &dialog);
    pinnedCheck->setObjectName(QStringLiteral("commandInboxSavePinnedCheck"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName(QStringLiteral("commandInboxSaveButtons"));

    form->addRow(tr("Name"), nameEdit);
    form->addRow(tr("Aliases"), aliasesEdit);
    form->addRow(tr("Tags"), tagsEdit);
    form->addRow(tr("Mode"), modeLabel);
    form->addRow(QString(), pinnedCheck);
    form->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }

    InboxFileSaveRequest request;
    request.filePath = filePath;
    request.name = nameEdit->text();
    request.aliases = valuesFromCommaText(aliasesEdit->text());
    request.tags = valuesFromCommaText(tagsEdit->text(), true);
    request.pinned = pinnedCheck->isChecked();
    request.mode = InboxFileArchiveMode::Link;
    return request;
}

bool PinloomCommandPanel::handleInboxDragEnter(QEvent *event)
{
    const QMimeData *mimeData = nullptr;
    if (event->type() == QEvent::DragEnter) {
        mimeData = static_cast<QDragEnterEvent *>(event)->mimeData();
    } else if (event->type() == QEvent::DragMove) {
        mimeData = static_cast<QDragMoveEvent *>(event)->mimeData();
    } else {
        return false;
    }

    if (localFilePathsFromMimeData(mimeData).isEmpty()) {
        return false;
    }

    if (event->type() == QEvent::DragEnter) {
        static_cast<QDragEnterEvent *>(event)->acceptProposedAction();
    } else {
        static_cast<QDragMoveEvent *>(event)->acceptProposedAction();
    }
    return true;
}

bool PinloomCommandPanel::handleInboxDrop(QEvent *event)
{
    if (event->type() != QEvent::Drop) {
        return false;
    }

    auto *dropEvent = static_cast<QDropEvent *>(event);
    const QStringList filePaths = localFilePathsFromMimeData(dropEvent->mimeData());
    if (filePaths.isEmpty()) {
        return false;
    }

    setPendingInboxFiles(filePaths);
    setCommandText(QStringLiteral("i n"));
    updateStatus(tr("Inbox pending: %1").arg(inboxFilesSummary(pendingInboxFiles_)));
    dropEvent->acceptProposedAction();
    return true;
}

void showCommandPanelForHotkey(QWidget &commandWindow, PinloomCommandPanel &panel)
{
    if (commandWindow.isMinimized()) {
        commandWindow.showNormal();
    } else {
        commandWindow.show();
    }

    commandWindow.raise();
    commandWindow.activateWindow();
    panel.focusCommand();
}

QList<PinloomCommandResultAction> defaultActionsForPinloomEntry(const PinloomEntry &entry, bool removeEnabled)
{
    const PinloomOpenTarget target = openTargetFromEntry(entry);
    QList<PinloomCommandResultAction> actions;
    const auto addAction = [&actions](const QString &id,
                                      const QString &label,
                                      const QString &detail,
                                      bool enabled = true,
                                      const QString &disabledReason = QString()) {
        PinloomCommandResultAction action;
        action.id = id;
        action.label = label;
        action.detail = detail;
        action.enabled = enabled;
        action.disabledReason = disabledReason;
        actions.append(action);
    };

    addAction(QString::fromLatin1(PrimaryResultActionId),
              commandTargetVerb(target),
              QStringLiteral("%1 %2").arg(commandTargetVerb(target), entry.name));
    addAction(entry.pinned ? QStringLiteral("unpin") : QStringLiteral("pin"),
              entry.pinned ? QStringLiteral("Unpin") : QStringLiteral("Pin"),
              QStringLiteral("Change pinned state"));
    addAction(QStringLiteral("add_alias"), QStringLiteral("Add alias"), QStringLiteral("Add an alias"));
    addAction(QStringLiteral("add_tag"), QStringLiteral("Add tag"), QStringLiteral("Add a tag"));
    addAction(QStringLiteral("edit_metadata"),
              QStringLiteral("Edit name/metadata"),
              QStringLiteral("Edit name, aliases, tags, and pinned state"));
    addAction(QStringLiteral("remove"),
              QStringLiteral("Delete / Remove"),
              entry.type == PinloomEntryType::SavedClip
                  ? QStringLiteral("Archive this Saved Clip inside Pinloom")
                  : entry.type == PinloomEntryType::Anchor
                        ? QStringLiteral("Delete this Pinloom anchor without deleting the target file")
                        : QStringLiteral("Remove this resource from Pinloom without deleting the original file"),
              removeEnabled,
              removeEnabled ? QString() : QStringLiteral("Remove is not available"));
    return actions;
}

} // namespace Pinloom
