#include "pinloom/widgets/PinloomUiControls.h"
#include "pinloom/widgets/PinloomCommandPanel.h"
#include "pinloom/widgets/CommandFloatingController.h"

#include "pinloom/core/Version.h"
#include "pinloom/clip/ClipAction.h"

#include "pinloom/core/AnchorLocator.h"
#include "pinloom/widgets/PinloomVisualTheme.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include "pinloom/widgets/PinloomItemViews.h"

#include <QMenu>
#include <QMimeData>
#include <QPaintEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSize>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <utility>

#ifdef PINLOOM_ENABLE_ELA
#include "ElaIcon.h"
#endif

static int initializePinloomThemeResources()
{
    Q_INIT_RESOURCE(pinloom_themes);
    return 0;
}

namespace Pinloom {

namespace {

constexpr int CommandActionRole = Qt::UserRole + 80;
constexpr int CommandSearchTextRole = Qt::UserRole + 81;
constexpr int CommandIdRole = Qt::UserRole + 82;
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
constexpr int TargetDeletedRole = Qt::UserRole + 150;
constexpr int TargetAnchorDeletedRole = Qt::UserRole + 151;
constexpr int ResultActionIdRole = Qt::UserRole + 160;
constexpr int ResultActionLabelRole = Qt::UserRole + 161;
constexpr int ResultActionDetailRole = Qt::UserRole + 162;
constexpr int ResultActionEnabledRole = Qt::UserRole + 163;
constexpr int ResultActionDisabledReasonRole = Qt::UserRole + 164;
constexpr int ResultSubtitleRole = Qt::UserRole + 165;
constexpr int ResultBadgeRole = Qt::UserRole + 166;
constexpr int ResultIconRole = Qt::UserRole + 167;
constexpr int ResultPresentationRole = Qt::UserRole + 168;

constexpr const char *PrimaryResultActionId = "primary";
constexpr const char *OpenFolderResultActionId = "open_folder";

void installStatusContextMenu(QLabel *label, QWidget *parent, const std::function<QString()> &statusText)
{
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(label, &QLabel::customContextMenuRequested, parent, [label, parent, statusText](const QPoint &pos) {
        const QString status = statusText().trimmed();
        std::unique_ptr<QMenu> ownedMenu(Pinloom::Ui::menu(parent));        QMenu &menu = *ownedMenu;
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

        Pinloom::Ui::Dialog dialog(parent);
        dialog.setWindowTitle(QObject::tr("Status Details"));
        auto *layout = new QVBoxLayout(&dialog);
        auto *text = Ui::plainTextEdit(status, &dialog);
        text->setReadOnly(true);
        auto *buttons = new Pinloom::Ui::DialogButtonBox(QDialogButtonBox::Close, &dialog);
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

using CommandNamespace = PinloomCommandNamespace;
using CommandAction = PinloomCommandId;
using CommandState = PinloomParsedCommand;

enum class CommandRowAction {
    Unknown = 0,
    OpenCommand,
    OpenUnifiedTarget,
    ClipInsert,
    ClipSave,
    InboxSave,
    RegisteredCommand,
    UnifiedTargetAction
};

PinloomCommandTheme themeForNamespace(CommandNamespace commandNamespace)
{
    switch (commandNamespace) {
    case CommandNamespace::Anchor:
        return PinloomCommandTheme::Anchor;
    case CommandNamespace::Clip:
        return PinloomCommandTheme::Clip;
    case CommandNamespace::Inbox:
        return PinloomCommandTheme::Inbox;
    case CommandNamespace::None:
    case CommandNamespace::Library:
    case CommandNamespace::Root:
    case CommandNamespace::Application:
        return PinloomCommandTheme::Neutral;
    }
    return PinloomCommandTheme::Neutral;
}

PinloomCommandTheme themeForTarget(const PinloomOpenTarget &target)
{
    if (!target.clipId.trimmed().isEmpty()) {
        return PinloomCommandTheme::Clip;
    }
    if (target.anchor.has_value()) {
        return PinloomCommandTheme::Anchor;
    }
    if (isInboxResourceId(target.resourceId)) {
        return PinloomCommandTheme::Inbox;
    }
    return PinloomCommandTheme::Neutral;
}

QColor themeAccent(PinloomCommandTheme theme,
                   PinloomVisualScheme scheme)
{
    const bool dark = scheme == PinloomVisualScheme::Dark;
    switch (theme) {
    case PinloomCommandTheme::Anchor:
        return QColor(dark ? QStringLiteral("#60A5FA")
                           : QStringLiteral("#2563EB"));
    case PinloomCommandTheme::Clip:
        return QColor(dark ? QStringLiteral("#2DD4BF")
                           : QStringLiteral("#0F766E"));
    case PinloomCommandTheme::Inbox:
        return QColor(dark ? QStringLiteral("#FBBF24")
                           : QStringLiteral("#B45309"));
    case PinloomCommandTheme::Neutral:
        return pinloomVisualTokens(scheme).accent;
    }
    return pinloomVisualTokens(scheme).accent;
}

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
        if (tags) {
            appendUniqueValue(values, cleanTag(value));
        } else if (!value.trimmed().isEmpty()) {
            values.append(value);
        }
    }
    return values;
}

QStringList cleanedValues(const QStringList &source, bool tags = false)
{
    QStringList values;
    for (const QString &value : source) {
        if (tags) {
            appendUniqueValue(values, cleanTag(value));
        } else if (!value.trimmed().isEmpty()) {
            values.append(value);
        }
    }
    return values;
}

QString inboxFilesSummary(const QStringList &filePaths)
{
    if (filePaths.isEmpty()) {
        return QStringLiteral("drop a file or folder; or use Explorer selection");
    }
    if (filePaths.size() == 1) {
        return QFileInfo(filePaths.first()).fileName();
    }
    return QStringLiteral("%1 items").arg(filePaths.size());
}

QString droppedTextFromMimeData(const QMimeData *mimeData)
{
    if (!mimeData || !mimeData->hasText()) {
        return {};
    }
    return mimeData->text().trimmed();
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
        if (!alias.trimmed().isEmpty()) {
            cleanedAliases.append(alias);
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

QString commandActionResultStatus(const PinloomCommandActionResult &result, const QString &fallback)
{
    QStringList lines;
    const QString message = result.message.trimmed().isEmpty() ? fallback.trimmed() : result.message.trimmed();
    if (!message.isEmpty()) {
        lines.append(message);
    }
    const QString diagnostics = result.diagnostics.trimmed();
    if (!diagnostics.isEmpty()) {
        lines.append(QStringLiteral("Diagnostics: %1").arg(diagnostics));
    }
    const QString nextUiHint = result.nextUiHint.trimmed();
    if (!nextUiHint.isEmpty()) {
        lines.append(QStringLiteral("Next: %1").arg(nextUiHint));
    }
    return lines.join(QLatin1Char('\n'));
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
    const bool webAction = !saveAction
        && result.actionType == ClipActionType::OpenWebUrl;
    const QString rowType = result.state == ClipState::Temporary
        ? QStringLiteral("History")
        : QStringLiteral("Saved");
    return QStringLiteral("[%1] %2 -> %3\n%4%5%6")
        .arg(rowType,
             displayName,
             saveAction ? QStringLiteral("Save")
                        : (webAction ? QStringLiteral("Open URL") : QStringLiteral("Insert")),
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

    return {};
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
    const QString deletedPrefix = target.deleted ? QStringLiteral("Deleted ") : QString();
    if (!target.clipId.trimmed().isEmpty()) {
        return deletedPrefix + QStringLiteral("Clip");
    }
    if (target.anchor.has_value()) {
        return deletedPrefix + QStringLiteral("Anchor");
    }
    if (isInboxResourceId(target.resourceId)) {
        return deletedPrefix + QStringLiteral("Inbox");
    }
    return deletedPrefix + commandResourceKindLabel(target.resourceKind);
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

QString commandTargetLocalPath(const PinloomOpenTarget &target)
{
    // Clip locations contain preview text, not filesystem paths.
    if (!target.clipId.trimmed().isEmpty()) return {};
    QString location = target.location.trimmed();
    if (target.anchor.has_value()) {
        const Anchor &anchor = target.anchor.value();
        if (!anchor.targetFile.trimmed().isEmpty()) location = anchor.targetFile.trimmed();
        else if (!anchor.targetUri.trimmed().isEmpty()) location = anchor.targetUri.trimmed();
    }
    const QUrl url(location);
    if (url.isLocalFile()) location = url.toLocalFile();
    if (location.isEmpty() || !QFileInfo(location).isAbsolute()) return {};
    return QDir::cleanPath(QDir::fromNativeSeparators(location));
}

PinloomCommandResultAction openFolderAction(const PinloomOpenTarget &target)
{
    PinloomCommandResultAction action;
    action.id = QString::fromLatin1(OpenFolderResultActionId);
    action.label = QObject::tr("Open folder");
    const QString path = commandTargetLocalPath(target);
    const QString directory = path.isEmpty() ? QString() : QFileInfo(path).absolutePath();
    action.detail = directory.isEmpty() ? QObject::tr("Open the containing folder") : directory;
    action.enabled = !target.deleted && !directory.isEmpty() && QDir(directory).exists();
    if (!action.enabled) {
        action.disabledReason = target.deleted
            ? QObject::tr("Restore this entry before using it")
            : directory.isEmpty() ? QObject::tr("This entry has no local file")
                                  : QObject::tr("The containing folder no longer exists");
    }
    return action;
}

QString commandTargetDetails(const PinloomOpenTarget &target)
{
    if (!target.clipId.trimmed().isEmpty()) return target.location.simplified();
    const QString localPath = commandTargetLocalPath(target);
    if (target.anchor.has_value()) {
        const QString fileName = localPath.isEmpty() ? target.location : QFileInfo(localPath).fileName();
        const QString locator = commandAnchorLocatorSummary(*target.anchor);
        return locator.isEmpty() ? fileName : fileName + QStringLiteral(" · ") + locator;
    }
    if (!localPath.isEmpty()) {
        const QString parent = QFileInfo(localPath).absolutePath();
        const QStringList components = parent.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        return components.size() > 2
            ? components.mid(components.size() - 2).join(QStringLiteral(" / "))
            : QDir::toNativeSeparators(parent);
    }
    return target.location.simplified();
}

QString commandTargetToolTip(const PinloomOpenTarget &target)
{
    QStringList lines;
    lines.append(QStringLiteral("%1: %2").arg(commandTargetKindLabel(target), commandTargetTitle(target)));
    const QString localPath = commandTargetLocalPath(target);
    if (!localPath.isEmpty() || !target.location.trimmed().isEmpty()) {
        lines.append(localPath.isEmpty() ? target.location : QDir::toNativeSeparators(localPath));
    }
    if (target.anchor.has_value()) {
        const Anchor &anchor = target.anchor.value();
        const QString locator = commandAnchorLocatorSummary(anchor);
        if (!locator.isEmpty()) {
            lines.append(QStringLiteral("Locator: %1").arg(locator));
        }
        const QString hints = commandAnchorHintsSummary(anchor);
        if (!hints.isEmpty()) {
            lines.append(hints);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

CommandState parseCommandState(const QString &text)
{
    return PinloomCommandRegistry::parse(text);
}

CommandRowAction rowActionForItem(const Pinloom::Ui::ListItem *item)
{
    if (!item) {
        return CommandRowAction::Unknown;
    }
    return static_cast<CommandRowAction>(item->data(CommandActionRole).toInt());
}

ClipSearchResult clipResultForItem(const Pinloom::Ui::ListItem *item)
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

void storeClipResult(Pinloom::Ui::ListItem *item, const ClipSearchResult &result)
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

void storeOpenTarget(Pinloom::Ui::ListItem *item, const PinloomOpenTarget &target)
{
    if (!item) {
        return;
    }

    item->setData(TargetResourceIdRole, target.resourceId);
    item->setData(TargetClipIdRole, target.clipId);
    item->setData(TargetKindRole, static_cast<int>(target.resourceKind));
    item->setData(TargetTitleRole, target.title);
    item->setData(TargetLocationRole, target.location);
    item->setData(TargetDeletedRole, target.deleted);
    item->setData(TargetMatchedFieldRole, target.matchedField);
    item->setData(TargetScoreRole, target.score);
    item->setData(TargetMatchSummaryRole, target.matchSummary);

    if (!target.clipId.trimmed().isEmpty()) {
        item->setData(ClipIdRole, target.clipId);
        item->setData(ClipDisplayNameRole, target.title);
        item->setData(ClipPreviewRole, target.location);
        item->setData(ClipMatchedFieldRole, target.matchedField);
        item->setData(ClipScoreRole, target.score);
        item->setData(ClipStateRole,
                      static_cast<int>(target.deleted ? ClipState::Deleted : ClipState::Saved));
    }

    item->setData(TargetHasAnchorRole, target.anchor.has_value());
    if (!target.anchor.has_value()) {
        return;
    }

    const Anchor &anchor = target.anchor.value();
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
    item->setData(TargetAnchorDeletedRole, anchor.deleted);
}

PinloomOpenTarget openTargetForCommandItem(const Pinloom::Ui::ListItem *item, int row = -1)
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
    target.deleted = item->data(TargetDeletedRole).toBool();
    target.matchedField = item->data(TargetMatchedFieldRole).toString();
    target.score = item->data(TargetScoreRole).toDouble();
    target.matchSummary = item->data(TargetMatchSummaryRole).toString();

    if (item->data(TargetHasAnchorRole).toBool()) {
        Anchor anchor;
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
        anchor.deleted = item->data(TargetAnchorDeletedRole).toBool();
        target.anchor = anchor;
        target.deleted = target.deleted || anchor.deleted;
    }

    return target;
}

QFont commandSecondaryFont(QFont font)
{
    if (font.pixelSize() > 0) font.setPixelSize(std::max(10, font.pixelSize() - 1));
    else font.setPointSizeF(std::max(8.0, font.pointSizeF() - 1.0));
    font.setWeight(QFont::Normal);
    return font;
}

int commandResultRowHeight(const QFont &font, bool action = false)
{
    const int titleHeight = QFontMetrics(font).height();
    return action ? std::max(36, titleHeight + 16)
                  : std::max(56, titleHeight + QFontMetrics(commandSecondaryFont(font)).height() + 20);
}

QIcon commandRowIcon(const QString &key, const QColor &color)
{
#ifdef PINLOOM_ENABLE_ELA
    // The bundled Font Awesome Free uses standard Unicode values, while many
    // of Ela's legacy icon enumerators refer to a different font's glyphs.
    char32_t glyph = 0xf0c9; // bars
    if (key == QLatin1String("open_folder")) glyph = 0xf07c;
    else if (key == QLatin1String("anchor")) glyph = 0xf13d;
    else if (key == QLatin1String("clip")) glyph = 0xf328;
    else if (key == QLatin1String("pdf")) glyph = 0xf1c1;
    else if (key == QLatin1String("file")) glyph = 0xf15b;
    else if (key == QLatin1String("rename")) glyph = 0xf303;
    else if (key == QLatin1String("edit_aliases") || key == QLatin1String("add_alias")) glyph = 0xf0c1;
    else if (key == QLatin1String("edit_tags") || key == QLatin1String("add_tag")) glyph = 0xf02c;
    else if (key == QLatin1String("pin") || key == QLatin1String("unpin")) glyph = 0xf08d;
    else if (key == QLatin1String("remove")) glyph = 0xf2ed;
    else if (key == QLatin1String("restore")) glyph = 0xf2ea;
    else if (key == QLatin1String("primary") || key == QLatin1String("open_source")) glyph = 0xf08e;
    return ElaIcon::getInstance()->getElaIcon(static_cast<ElaIconType::IconName>(glyph), 36, 40, 40, color);
#else
    Q_UNUSED(color)
    return QApplication::style()->standardIcon(key == QLatin1String("open_folder")
        ? QStyle::SP_DirOpenIcon : QStyle::SP_FileIcon);
#endif
}

void setCommandRowPresentation(Ui::ListItem *item, const QString &title,
                               const QString &subtitle, const QString &badge,
                               const QString &iconKey, bool action = false)
{
    item->setText(title.simplified());
    item->setData(ResultPresentationRole, action ? 2 : 1);
    item->setData(ResultSubtitleRole, subtitle.simplified());
    item->setData(ResultBadgeRole, badge);
    item->setData(ResultIconRole, iconKey);
    item->setIcon(commandRowIcon(iconKey, pinloomVisualTokens(activePinloomVisualScheme()).mutedText));
    item->setData(Qt::AccessibleTextRole, badge.isEmpty() ? title : badge + QStringLiteral(": ") + title);
    item->setData(Qt::AccessibleDescriptionRole, item->toolTip());
}

class CommandResultDelegate final : public QStyledItemDelegate {
public:
    explicit CommandResultDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const int presentation = index.data(ResultPresentationRole).toInt();
        if (!presentation) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        const bool action = presentation == 2;
        const bool enabled = !action || index.data(ResultActionEnabledRole).toBool();
        const auto scheme = activePinloomVisualScheme();
        const auto tokens = pinloomVisualTokens(scheme);
        const QRect row = option.rect.adjusted(2, 2, -2, -2);
        painter->save();
        painter->setClipRect(option.rect);
        painter->setRenderHint(QPainter::Antialiasing);
        if (option.state & QStyle::State_Selected) {
            QColor tint = tokens.accent;
            tint.setAlpha(scheme == PinloomVisualScheme::Light ? 26 : 48);
            painter->setPen(Qt::NoPen);
            painter->setBrush(tokens.panel);
            painter->drawRoundedRect(row, 6, 6);
            painter->setBrush(tint);
            painter->drawRoundedRect(row, 6, 6);
        } else if (option.state & QStyle::State_MouseOver) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(tokens.hoverSurface);
            painter->drawRoundedRect(row, 6, 6);
        }
        if (option.state & QStyle::State_HasFocus) {
            painter->setPen(QPen(tokens.focus, 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(row.adjusted(1, 1, -1, -1), 5, 5);
        }

        const QColor titleColor = enabled ? tokens.text : tokens.disabledText;
        const QColor secondaryColor = enabled ? tokens.mutedText : tokens.disabledText;
        const QRect iconRect(row.left() + 10, row.center().y() - 10, 20, 20);
        commandRowIcon(index.data(ResultIconRole).toString(), secondaryColor).paint(painter, iconRect);
        const int textLeft = iconRect.right() + 12;
        const int textWidth = std::max(0, row.right() - 12 - textLeft);
        QFont titleFont = option.font;
        if (!action) titleFont.setWeight(QFont::DemiBold);
        const QFontMetrics titleMetrics(titleFont);
        const QFont detailFont = commandSecondaryFont(option.font);
        const QFontMetrics detailMetrics(detailFont);
        const int titleTop = action ? row.top() : row.top() + 7;
        const int titleHeight = action ? row.height() : titleMetrics.height();
        int titleWidth = textWidth;

        if (!action) {
            const QString badge = index.data(ResultBadgeRole).toString();
            const int badgeWidth = std::min(detailMetrics.horizontalAdvance(badge) + 14,
                                             std::max(0, (textWidth - 32) / 3));
            const QRect chevronRect(row.right() - 24, titleTop, 16, titleHeight);
            painter->setFont(option.font);
            painter->setPen(secondaryColor);
            painter->drawText(chevronRect, Qt::AlignCenter, QStringLiteral("›"));
            if (badgeWidth > 14) {
                const QRect badgeRect(chevronRect.left() - badgeWidth - 8, titleTop - 1,
                                      badgeWidth, detailMetrics.height() + 4);
                painter->setPen(Qt::NoPen);
                painter->setBrush(tokens.alternateSurface);
                painter->drawRoundedRect(badgeRect, 4, 4);
                painter->setFont(detailFont);
                painter->setPen(secondaryColor);
                painter->drawText(badgeRect.adjusted(7, 0, -7, 0), Qt::AlignCenter,
                    detailMetrics.elidedText(badge, Qt::ElideRight, badgeWidth - 14));
                titleWidth = std::max(0, badgeRect.left() - 12 - textLeft);
            } else {
                titleWidth = std::max(0, chevronRect.left() - 8 - textLeft);
            }
            const QRect detailRect(textLeft, titleTop + titleHeight + 3, textWidth, detailMetrics.height());
            painter->setFont(detailFont);
            painter->setPen(secondaryColor);
            painter->drawText(detailRect, Qt::AlignLeft | Qt::AlignVCenter,
                detailMetrics.elidedText(index.data(ResultSubtitleRole).toString(), Qt::ElideMiddle, textWidth));
        }
        painter->setFont(titleFont);
        painter->setPen(titleColor);
        painter->drawText(QRect(textLeft, titleTop, titleWidth, titleHeight), Qt::AlignLeft | Qt::AlignVCenter,
            titleMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, titleWidth));
        painter->restore();
    }
};

PinloomCommandResultAction resultActionForItem(const Pinloom::Ui::ListItem *item)
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

void storeResultAction(Pinloom::Ui::ListItem *item,
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

bool canExpandResultActions(const Pinloom::Ui::ListItem *item)
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

void PinloomCommandPanel::configureOwnedCommandDispatcher()
{
    ownedCommandDispatcher_ = std::make_unique<PinloomCommandDispatcher>();
    commandDispatcher_ = ownedCommandDispatcher_.get();
    const auto addBooleanHandler =
        [this](CommandAction id,
               const std::function<bool(QString *)> &handler,
               const QString &successFallback,
               const QString &failureFallback) {
            if (!handler) {
                return;
            }
            commandDispatcher_->registerHandler(
                id,
                [handler, successFallback, failureFallback](const PinloomCommandInvocation &) {
                    QString status;
                    const bool succeeded = handler(&status);
                    return pinloomCommandResultFromBoolean(
                        succeeded, status, successFallback, failureFallback);
                });
        };

    addBooleanHandler(CommandAction::ClipLibrary,
                      options_.clipLibraryHandler,
                      tr("Opened Clip Library"),
                      tr("Unable to open Clip Library"));
    addBooleanHandler(CommandAction::ClipPdfText,
                      options_.pdfTextClipCaptureHandler,
                      tr("Captured PDF text Clip"),
                      tr("PDF Text Clip is unavailable for the remembered target"));
    addBooleanHandler(CommandAction::AnchorNew,
                      options_.anchorCaptureHandler,
                      tr("Captured anchor"),
                      tr("No anchor context available"));
    addBooleanHandler(CommandAction::AnchorPdfRectangle,
                      options_.rectangleAnchorCaptureHandler
                          ? options_.rectangleAnchorCaptureHandler
                          : options_.anchorCaptureHandler,
                      tr("Captured PDF rectangle Anchor"),
                      tr("PDF Rectangle Anchor is unavailable for the remembered target"));
    addBooleanHandler(CommandAction::AnchorPdfText,
                      options_.textAnchorCaptureHandler,
                      tr("Captured PDF text Anchor"),
                      tr("PDF Text Anchor is unavailable for the remembered target"));
    addBooleanHandler(CommandAction::AnchorLibrary,
                      options_.anchorLibraryHandler,
                      tr("Opened Anchor Library"),
                      tr("Unable to open Anchor Library"));
    addBooleanHandler(CommandAction::RootLibrary,
                      options_.libraryRootHandler,
                      tr("Opened Root Library"),
                      tr("Unable to open Root Library"));
}

PinloomCommandPanel::PinloomCommandPanel(QWidget *parent)
    : PinloomCommandPanel(PinloomCommandPanelOptions{}, parent)
{
}

PinloomCommandPanel::PinloomCommandPanel(PinloomCommandPanelOptions options, QWidget *parent)
    : QWidget(parent)
    , options_(std::move(options))
{
    static const int themeResourcesInitialized = initializePinloomThemeResources();
    Q_UNUSED(themeResourcesInitialized);

    commandDispatcher_ = options_.commandDispatcher;
    if (!commandDispatcher_) {
        configureOwnedCommandDispatcher();
    }

    setObjectName(QStringLiteral("pinloomCommandPanel"));
    setProperty("pinloomRole", QStringLiteral("canvas"));
    setWindowTitle(tr("Pinloom Command %1").arg(pinloomVersionLabel()));
    setAcceptDrops(true);
    setAutoFillBackground(false);
    resize(760, preferredWindowHeight_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    const PinloomVisualMetrics visualMetrics = pinloomVisualMetrics();
    commandEdit_ = Pinloom::Ui::lineEdit(this);
    commandEdit_->setObjectName(QStringLiteral("commandSearchEdit"));
    commandEdit_->setAccessibleName(tr("Pinloom command and search"));
    commandEdit_->setAccessibleDescription(
        tr("Search Pinloom or enter a command. Use Up and Down to navigate results."));
    commandEdit_->setPlaceholderText(tr("Search Anchor, Clip, Inbox, or File"));
    commandEdit_->setClearButtonEnabled(true);
    commandEdit_->setAcceptDrops(true);
    commandEdit_->setFixedHeight(visualMetrics.primaryControlHeight);

    clipLibraryButton_ = Pinloom::Ui::toolButton(this);
    clipLibraryButton_->setObjectName(QStringLiteral("commandClipLibraryButton"));
    clipLibraryButton_->setProperty("pinloomControl", QStringLiteral("icon"));
    clipLibraryButton_->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    clipLibraryButton_->setToolTip(tr("Open Clip Library"));
    clipLibraryButton_->setFixedSize(visualMetrics.primaryControlHeight,
                                     visualMetrics.primaryControlHeight);
    clipLibraryButton_->setVisible(false);

    quickActionRow_ = new QWidget(this);
    quickActionRow_->setObjectName(QStringLiteral("commandQuickActionRow"));
    auto *quickLayout = new QHBoxLayout(quickActionRow_);
    quickLayout->setContentsMargins(0, 0, 0, 0);
    quickLayout->setSpacing(8);
    auto *quickLabel = Pinloom::Ui::label(tr("Capture"), quickActionRow_);
    quickLabel->setObjectName(QStringLiteral("commandQuickActionLabel"));
    quickLabel->setProperty("pinloomTextRole", QStringLiteral("metadata"));
    quickLayout->addWidget(quickLabel);

    rectangleAnchorButton_ = Pinloom::Ui::toolButton(quickActionRow_);
    rectangleAnchorButton_->setObjectName(QStringLiteral("commandRectangleAnchorButton"));
    rectangleAnchorButton_->setProperty("pinloomControl", QStringLiteral("compact"));
    rectangleAnchorButton_->setMinimumHeight(visualMetrics.compactControlHeight);
    rectangleAnchorButton_->setText(tr("PDF Rectangle Anchor"));
    rectangleAnchorButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    rectangleAnchorButton_->setToolTip(tr("Capture a rectangle in the remembered SumatraPDF document"));
    rectangleAnchorButton_->setAccessibleName(tr("PDF Rectangle Anchor"));
    rectangleAnchorButton_->setEnabled(commandDispatcher_
        && commandDispatcher_->hasHandler(CommandAction::AnchorPdfRectangle));
    quickLayout->addWidget(rectangleAnchorButton_);

    textAnchorButton_ = Pinloom::Ui::toolButton(quickActionRow_);
    textAnchorButton_->setObjectName(QStringLiteral("commandTextAnchorButton"));
    textAnchorButton_->setProperty("pinloomControl", QStringLiteral("compact"));
    textAnchorButton_->setMinimumHeight(visualMetrics.compactControlHeight);
    textAnchorButton_->setText(tr("PDF Text Anchor"));
    textAnchorButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    textAnchorButton_->setToolTip(tr("Capture selected text in the remembered SumatraPDF document"));
    textAnchorButton_->setAccessibleName(tr("PDF Text Anchor"));
    textAnchorButton_->setEnabled(commandDispatcher_
        && commandDispatcher_->hasHandler(CommandAction::AnchorPdfText));
    quickLayout->addWidget(textAnchorButton_);

    pdfTextClipButton_ = Pinloom::Ui::toolButton(quickActionRow_);
    pdfTextClipButton_->setObjectName(QStringLiteral("commandPdfTextClipButton"));
    pdfTextClipButton_->setProperty("pinloomControl", QStringLiteral("compact"));
    pdfTextClipButton_->setMinimumHeight(visualMetrics.compactControlHeight);
    pdfTextClipButton_->setText(tr("PDF Text Clip"));
    pdfTextClipButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    pdfTextClipButton_->setToolTip(tr("Save selected text from the remembered SumatraPDF document as a Clip"));
    pdfTextClipButton_->setAccessibleName(tr("PDF Text Clip"));
    pdfTextClipButton_->setEnabled(commandDispatcher_
        && commandDispatcher_->hasHandler(CommandAction::ClipPdfText));
    quickLayout->addWidget(pdfTextClipButton_);
    quickLayout->addStretch(1);

    resultList_ = new Pinloom::Ui::List(this);
    resultList_->setObjectName(QStringLiteral("commandResultList"));
    resultList_->setProperty("pinloomRole", QStringLiteral("raised"));
    resultList_->setAccessibleName(tr("Pinloom command and search results"));
    resultList_->setAccessibleDescription(
        tr("Use Up and Down to select, Enter to activate, and Right Arrow for actions."));
    resultList_->setAlternatingRowColors(false);
    resultList_->setUniformItemSizes(true);
    resultList_->setFrameShape(QFrame::NoFrame);
    resultList_->setItemDelegate(new CommandResultDelegate(resultList_));
    resultList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    resultList_->setAcceptDrops(true);

    footer_ = new QWidget(this);
    footer_->setObjectName(QStringLiteral("commandFooter"));
    auto *footerLayout = new QHBoxLayout(footer_);
    footerLayout->setContentsMargins(10, 0, 10, 0);
    footerLayout->setSpacing(12);
    statusLabel_ = Pinloom::Ui::label(footer_);
    statusLabel_->setObjectName(QStringLiteral("commandStatusLabel"));
    statusLabel_->setAccessibleName(tr("Pinloom command status"));
    statusLabel_->setWordWrap(false);
    statusLabel_->setTextFormat(Qt::PlainText);
    statusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusLabel_->setMinimumWidth(0);
    statusLabel_->setFont(commandSecondaryFont(font()));
    statusLabel_->installEventFilter(this);
    installStatusContextMenu(statusLabel_, this, [this]() {
        return statusText_;
    });
    keyboardHintLabel_ = Pinloom::Ui::label(footer_);
    keyboardHintLabel_->setObjectName(QStringLiteral("commandKeyboardHint"));
    keyboardHintLabel_->setFont(commandSecondaryFont(font()));
    keyboardHintLabel_->setAccessibleName(tr("Keyboard shortcuts"));
    footerLayout->addWidget(statusLabel_, 1);
    footerLayout->addWidget(keyboardHintLabel_);

    auto *inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(0, 0, 0, 0);
    inputRow->setSpacing(8);
    inputRow->addWidget(commandEdit_, 1);
    auto *navigation = Ui::toolButton(this);
    navigation->setObjectName(QStringLiteral("commandNavigationButton"));
    navigation->setText(tr("Navigate"));
    navigation->setAccessibleName(tr("Navigate Pinloom"));
    navigation->setToolTip(tr("Choose a search namespace"));
    navigation->setPopupMode(QToolButton::InstantPopup);
    navigation->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto *navigationMenu = Ui::menu(navigation);
    navigationMenu->setObjectName(QStringLiteral("commandNavigationMenu"));
    const QList<QPair<QString, QString>> destinations = {
        {tr("All items"), QString()}, {tr("Anchor"), QStringLiteral("anchor ")},
        {tr("Clip"), QStringLiteral("clip ")}, {tr("Inbox"), QStringLiteral("inbox ")},
        {tr("Library"), QStringLiteral("library ")}, {tr("Roots"), QStringLiteral("root ")}};
    for (const auto &destination : destinations) {
        auto *action = navigationMenu->addAction(destination.first);
        connect(action, &QAction::triggered, this, [this, destination] {
            setCommandText(destination.second);
            focusCommand();
        });
    }
    Ui::installNavigation(navigation, navigationMenu);
    inputRow->addWidget(navigation);
    inputRow->addWidget(clipLibraryButton_);
    layout->addLayout(inputRow);
    layout->addWidget(quickActionRow_);
    layout->addWidget(resultList_, 1);
    layout->addWidget(footer_);

    commandEdit_->installEventFilter(this);
    resultList_->installEventFilter(this);

    connect(commandEdit_, &QLineEdit::textChanged, this, &PinloomCommandPanel::refreshResults);
    connect(commandEdit_, &QLineEdit::returnPressed, this, &PinloomCommandPanel::activateCurrentCommandItem);
    connect(clipLibraryButton_, &QToolButton::clicked, this, &PinloomCommandPanel::openClipLibrary);
    connect(rectangleAnchorButton_,
            &QToolButton::clicked,
            this,
            &PinloomCommandPanel::triggerRectangleAnchorCapture);
    connect(textAnchorButton_,
            &QToolButton::clicked,
            this,
            &PinloomCommandPanel::triggerTextAnchorCapture);
    connect(pdfTextClipButton_,
            &QToolButton::clicked,
            this,
            &PinloomCommandPanel::triggerPdfTextClipCapture);
    connect(resultList_, &Pinloom::Ui::List::itemActivated, this, &PinloomCommandPanel::activateResultItem);
    connect(resultList_, &Pinloom::Ui::List::itemDoubleClicked, this, &PinloomCommandPanel::activateResultItem);
    connect(resultList_, &Pinloom::Ui::List::currentItemChanged, this, [this] {
        updateKeyboardHint();
    });

    QWidget::setTabOrder(commandEdit_, rectangleAnchorButton_);
    QWidget::setTabOrder(rectangleAnchorButton_, textAnchorButton_);
    QWidget::setTabOrder(textAnchorButton_, pdfTextClipButton_);
    QWidget::setTabOrder(pdfTextClipButton_, resultList_);

    setTheme(PinloomCommandTheme::Neutral);
    refreshResults();
    focusCommand();
}

void PinloomCommandPanel::setCommandText(const QString &text)
{
    const bool refreshRequired = commandEdit_->text() == text;
    clipPickerMode_ = false;
    clipLibraryButton_->setVisible(false);
    commandEdit_->setPlaceholderText(tr("Command or search"));
    window()->setWindowTitle(tr("Pinloom Command %1").arg(pinloomVersionLabel()));
    commandEdit_->setText(text);
    if (refreshRequired) {
        refreshResults();
    }
}

QString PinloomCommandPanel::commandText() const
{
    return commandEdit_->text();
}

void PinloomCommandPanel::openCommandSearch(const QString &query)
{
    const bool refreshRequired = commandEdit_->text() == query;
    clipPickerMode_ = false;
    clipLibraryButton_->setVisible(false);
    commandEdit_->setPlaceholderText(tr("Search Anchor, Clip, Inbox, or File"));
    window()->setWindowTitle(tr("Pinloom Command %1").arg(pinloomVersionLabel()));
    commandEdit_->setText(query);
    if (refreshRequired) {
        refreshResults();
    }
    focusCommand();
}

void PinloomCommandPanel::openClipSearch(const QString &query)
{
    const QString trimmedQuery = query.trimmed();
    const bool refreshRequired = commandEdit_->text() == trimmedQuery;
    clipPickerMode_ = true;
    clipLibraryButton_->setVisible(true);
    commandEdit_->setPlaceholderText(tr("Name or alias; use tag;name to filter by tag"));
    window()->setWindowTitle(tr("Insert Clip - Pinloom %1").arg(pinloomVersionLabel()));
    commandEdit_->setText(trimmedQuery);
    if (refreshRequired) {
        refreshResults();
    }
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
    Pinloom::Ui::ListItem *item = resultList_->currentItem();
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
            updateStatus(tr("No pending Inbox item; drop a file or folder, or select one in Explorer"));
        } else if (command.commandNamespace == CommandNamespace::Inbox
                   && command.action == CommandAction::InboxSearch) {
            updateStatus(tr("No Inbox results"));
        } else if (command.commandNamespace == CommandNamespace::Library
                   && command.action == CommandAction::OpenSearch) {
            updateStatus(tr("No unified results"));
        } else if (command.commandNamespace == CommandNamespace::None
                   && commandEdit_
                   && !commandEdit_->text().trimmed().isEmpty()) {
            updateStatus(options_.unifiedEntrySearchHandler
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
    Pinloom::Ui::ListItem *item = resultList_->currentItem();
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

bool PinloomCommandPanel::isClipPicker() const
{
    return clipPickerMode_;
}

PinloomCommandTheme PinloomCommandPanel::theme() const
{
    return theme_;
}

bool PinloomCommandPanel::isCompact() const
{
    return compact_;
}

int PinloomCommandPanel::preferredWindowHeight() const
{
    return preferredWindowHeight_;
}

bool PinloomCommandPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == statusLabel_ && event->type() == QEvent::Resize) {
        updateStatusDisplay();
    }
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
            && (key == Qt::Key_Escape
                || (key == Qt::Key_Left && watched == resultList_))
            && modifiers == Qt::NoModifier) {
            returnToResultList();
            return true;
        }
        if (!showingResultActions_
            && key == Qt::Key_Right
            && modifiers == Qt::NoModifier) {
            if (watched == commandEdit_
                && (commandEdit_->hasSelectedText()
                    || commandEdit_->cursorPosition() < commandEdit_->text().size())) {
                return QWidget::eventFilter(watched, event);
            }
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

void PinloomCommandPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (!event) return;
    if (event->type() == QEvent::ApplicationPaletteChange) {
        appliedThemeKey_.clear();
        setTheme(theme_);
    }
}

void PinloomCommandPanel::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    const PinloomVisualScheme scheme = activePinloomVisualScheme();
    const PinloomVisualTokens tokens = pinloomVisualTokens(scheme);
    painter.fillRect(rect(), tokens.canvas);
    painter.fillRect(QRect(0, 0, 4, height()), themeAccent(theme_, scheme));
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

    CommandState command;
    if (clipPickerMode_) {
        command.commandNamespace = CommandNamespace::Clip;
        command.action = CommandAction::ClipSearch;
        command.query = commandEdit_->text().trimmed();
    } else {
        command = parseCommandState(commandEdit_->text());
    }
    setTheme(themeForNamespace(command.commandNamespace));
    QStringList listedClipIds;

    const auto appendCommandResult = [this](CommandRowAction action,
                                            const QString &nextCommandText,
                                            const QString &title,
                                            const QString &verb,
                                            const QString &detail) {
        auto *item = new Pinloom::Ui::ListItem(QStringLiteral("[Command] %1 -> %2\n%3").arg(title, verb, detail),
                                         resultList_);
        item->setSizeHint(QSize(0, resultList_->fontMetrics().lineSpacing() * 2 + 12));
        item->setData(CommandActionRole, static_cast<int>(action));
        item->setData(CommandSearchTextRole, nextCommandText);
        item->setData(CommandIdRole,
                      static_cast<int>(PinloomCommandRegistry::parse(nextCommandText).action));
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

            auto *item = new Pinloom::Ui::ListItem(clipResultText(result, actionText), resultList_);
            item->setToolTip(clipToolTip(result));
            item->setSizeHint(QSize(0, resultList_->fontMetrics().lineSpacing() * 2 + 12));
            item->setData(CommandActionRole, static_cast<int>(action));
            storeClipResult(item, result);
        }
    };
    const auto appendUnifiedResults = [this](const QList<PinloomOpenTarget> &targets) {
        for (const PinloomOpenTarget &target : targets) {
            auto *item = new Pinloom::Ui::ListItem(QString(), resultList_);
            item->setToolTip(commandTargetToolTip(target));
            const QString iconKey = !target.clipId.isEmpty() ? QStringLiteral("clip")
                : target.anchor.has_value() ? QStringLiteral("anchor")
                : QFileInfo(commandTargetLocalPath(target)).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0
                    ? QStringLiteral("pdf") : QStringLiteral("file");
            setCommandRowPresentation(item, commandTargetTitle(target), commandTargetDetails(target),
                                      commandTargetKindLabel(target), iconKey);
            item->setSizeHint(QSize(0, commandResultRowHeight(resultList_->font())));
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
    const auto appendNamespaceCommands =
        [&appendCommandResult](CommandNamespace commandNamespace) {
            for (const PinloomCommandDefinition &definition
                 : PinloomCommandRegistry::definitionsForNamespace(commandNamespace)) {
                appendCommandResult(CommandRowAction::OpenCommand,
                                    definition.canonical,
                                    definition.title,
                                    definition.verb,
                                    definition.detail);
            }
        };

    constexpr int resultLimit = 100;
    const QString plainQuery = commandEdit_->text().trimmed();
    if (command.commandNamespace == CommandNamespace::None
        && !plainQuery.isEmpty()
        && options_.unifiedEntrySearchHandler) {
        appendUnifiedEntries(options_.unifiedEntrySearchHandler(plainQuery));
    } else if (command.commandNamespace == CommandNamespace::Inbox
               && command.action == CommandAction::InboxSearch
               && options_.unifiedEntrySearchHandler) {
        QList<PinloomEntry> inboxEntries;
        for (const PinloomEntry &entry : options_.unifiedEntrySearchHandler(command.query)) {
            if (entry.type == PinloomEntryType::Inbox || isInboxResourceId(entry.resourceId)) {
                inboxEntries.append(entry);
            }
        }
        appendUnifiedEntries(inboxEntries);
    } else if (command.commandNamespace == CommandNamespace::Library
               && command.action == CommandAction::OpenSearch
               && options_.unifiedEntrySearchHandler) {
        appendUnifiedEntries(options_.unifiedEntrySearchHandler(command.query));
    } else if (command.commandNamespace == CommandNamespace::Library
               && command.action == CommandAction::RestoreSearch
               && options_.deletedEntrySearchHandler) {
        appendUnifiedEntries(options_.deletedEntrySearchHandler(command.query));
    } else if (command.commandNamespace == CommandNamespace::Clip
               && command.action == CommandAction::None) {
        appendNamespaceCommands(CommandNamespace::Clip);
    } else if (command.commandNamespace == CommandNamespace::Clip
               && command.action == CommandAction::ClipSearch
               && options_.clipSearchHandler) {
        if (clipPickerMode_) {
            ClipSearchOptions savedOptions;
            savedOptions.includeSaved = true;
            savedOptions.includeTemporary = false;
            savedOptions.emptyQueryReturnsPinnedAndRecent = true;
            savedOptions.limit = resultLimit;
            savedOptions.mode = ClipSearchMode::Identity;

            appendClipResults(options_.clipSearchHandler(command.query, savedOptions),
                              CommandRowAction::ClipInsert);
        } else if (command.query.isEmpty()) {
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
    } else if (command.commandNamespace == CommandNamespace::Clip
               && command.action == CommandAction::ClipLibrary) {
        const PinloomCommandDefinition definition =
            PinloomCommandRegistry::definition(command.action).value();
        appendCommandResult(CommandRowAction::RegisteredCommand,
                            definition.canonical,
                            definition.title,
                            definition.verb,
                            definition.detail);
    } else if (command.commandNamespace == CommandNamespace::Clip
               && command.action == CommandAction::ClipPdfText) {
        const PinloomCommandDefinition definition =
            PinloomCommandRegistry::definition(command.action).value();
        appendCommandResult(CommandRowAction::RegisteredCommand,
                            definition.canonical,
                            definition.title,
                            definition.verb,
                            definition.detail);
    } else if (command.commandNamespace == CommandNamespace::Anchor
               && command.action == CommandAction::None) {
        appendNamespaceCommands(CommandNamespace::Anchor);
    } else if (command.commandNamespace == CommandNamespace::Anchor
               && (command.action == CommandAction::AnchorNew
                   || command.action == CommandAction::AnchorPdfRectangle
                   || command.action == CommandAction::AnchorPdfText
                   || command.action == CommandAction::AnchorLibrary)) {
        const PinloomCommandDefinition definition =
            PinloomCommandRegistry::definition(command.action).value();
        appendCommandResult(CommandRowAction::RegisteredCommand,
                            definition.canonical,
                            definition.title,
                            definition.verb,
                            definition.detail);
    } else if (command.commandNamespace == CommandNamespace::Root
               && command.action == CommandAction::None) {
        appendNamespaceCommands(CommandNamespace::Root);
    } else if (command.commandNamespace == CommandNamespace::Root
               && command.action == CommandAction::RootLibrary) {
        const PinloomCommandDefinition definition =
            PinloomCommandRegistry::definition(command.action).value();
        appendCommandResult(CommandRowAction::RegisteredCommand,
                            definition.canonical,
                            definition.title,
                            definition.verb,
                            definition.detail);
    } else if (command.commandNamespace == CommandNamespace::Application
               && command.action != CommandAction::None) {
        const PinloomCommandDefinition definition =
            PinloomCommandRegistry::definition(command.action).value();
        appendCommandResult(CommandRowAction::RegisteredCommand,
                            definition.canonical,
                            definition.title,
                            definition.verb,
                            definition.detail);
    } else if (command.commandNamespace == CommandNamespace::Inbox
               && command.action == CommandAction::None) {
        appendNamespaceCommands(CommandNamespace::Inbox);
    } else if (command.commandNamespace == CommandNamespace::Inbox
               && command.action == CommandAction::InboxNew) {
        appendCommandResult(CommandRowAction::InboxSave,
                            QStringLiteral("inbox;new"),
                            tr("Add Inbox Item"),
                            tr("Save"),
                            tr("Review storage and metadata - %1").arg(inboxFilesSummary(pendingInboxFiles_)));
    } else if (command.commandNamespace == CommandNamespace::Library
               && command.action == CommandAction::RestoreSearch
               && !options_.deletedEntrySearchHandler) {
        appendCommandResult(CommandRowAction::OpenCommand,
                            QStringLiteral("restore"),
                            tr("Restore Deleted Entry"),
                            tr("Unavailable"),
                            tr("restore <query> - restore search is not configured"));
    } else if (command.commandNamespace == CommandNamespace::Library
               && command.action == CommandAction::None) {
        appendNamespaceCommands(CommandNamespace::Library);
    }

    if (resultList_->count() > 0) {
        resultList_->setCurrentRow(0);
    }

    if (clipPickerMode_) {
        updateStatus(resultList_->count() > 0
                         ? tr("Saved Clips: %n result(s)", nullptr, resultList_->count())
                         : tr("No Saved Clips match"));
    } else if (commandEdit_->text().trimmed().isEmpty()) {
        updateStatus(tr("Type to search Anchor, Clip, Inbox, Root, or File; c/k/i/r for commands"));
    } else if (command.commandNamespace == CommandNamespace::None) {
        if (!options_.unifiedEntrySearchHandler) {
            updateStatus(tr("Unified search is not configured"));
        } else {
            updateStatus(resultList_->count() > 0
                             ? tr("%n result(s)", nullptr, resultList_->count())
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
    } else if (command.commandNamespace == CommandNamespace::Clip
               && command.action == CommandAction::ClipLibrary) {
        updateStatus(tr("Open Clip Library pending"));
    } else if (command.commandNamespace == CommandNamespace::Clip
               && command.action == CommandAction::ClipPdfText) {
        updateStatus(tr("Capture selected PDF text as Clip pending"));
    } else if (command.commandNamespace == CommandNamespace::Anchor
               && command.action == CommandAction::None) {
        updateStatus(tr("Anchor commands"));
    } else if (command.commandNamespace == CommandNamespace::Anchor
               && command.action == CommandAction::AnchorNew) {
        updateStatus(tr("Capture anchor current app context pending"));
    } else if (command.commandNamespace == CommandNamespace::Anchor
               && command.action == CommandAction::AnchorPdfRectangle) {
        updateStatus(tr("Capture PDF rectangle Anchor pending"));
    } else if (command.commandNamespace == CommandNamespace::Anchor
               && command.action == CommandAction::AnchorPdfText) {
        updateStatus(tr("Capture PDF text Anchor pending"));
    } else if (command.commandNamespace == CommandNamespace::Anchor
               && command.action == CommandAction::AnchorLibrary) {
        updateStatus(tr("Open Anchor Library pending"));
    } else if (command.commandNamespace == CommandNamespace::Inbox
               && command.action == CommandAction::None) {
        updateStatus(tr("Inbox commands"));
    } else if (command.commandNamespace == CommandNamespace::Inbox
               && command.action == CommandAction::InboxNew) {
        updateStatus(pendingInboxFiles_.isEmpty()
                         ? tr("Inbox: drop a file or folder, or use Explorer selection")
                         : tr("Inbox pending: %1").arg(inboxFilesSummary(pendingInboxFiles_)));
    } else if (command.commandNamespace == CommandNamespace::Inbox
               && command.action == CommandAction::InboxSearch) {
        if (!options_.unifiedEntrySearchHandler) {
            updateStatus(tr("Unified search is not configured"));
        } else {
            updateStatus(resultList_->count() > 0
                             ? tr("%n result(s)", nullptr, resultList_->count())
                             : tr("No Inbox results"));
        }
    } else if (command.commandNamespace == CommandNamespace::Root) {
        updateStatus(command.action == CommandAction::RootLibrary
                         ? tr("Open Root Library")
                         : tr("Root commands"));
    } else if (command.commandNamespace == CommandNamespace::Library
               && command.action == CommandAction::OpenSearch) {
        if (!options_.unifiedEntrySearchHandler) {
            updateStatus(tr("Unified search is not configured"));
        } else {
            updateStatus(resultList_->count() > 0
                             ? tr("%n result(s)", nullptr, resultList_->count())
                             : tr("No unified results"));
        }
    } else if (command.commandNamespace == CommandNamespace::Library
               && command.action == CommandAction::RestoreSearch) {
        if (!options_.deletedEntrySearchHandler) {
            updateStatus(tr("Restore search is not configured"));
        } else {
            updateStatus(resultList_->count() > 0
                             ? tr("Restore search: %n deleted item(s)", nullptr, resultList_->count())
                             : tr("No deleted items to restore"));
        }
    } else if (command.commandNamespace == CommandNamespace::Library
               && command.action == CommandAction::None) {
        updateStatus(tr("Library commands"));
    } else if (command.commandNamespace == CommandNamespace::Application
               && command.action != CommandAction::None) {
        const std::optional<PinloomCommandDefinition> definition =
            PinloomCommandRegistry::definition(command.action);
        updateStatus(definition.has_value()
                         ? tr("Run %1 pending").arg(definition->title)
                         : tr("Unknown command"));
    } else {
        updateStatus(tr("Unknown command"));
    }
}

void PinloomCommandPanel::activateResultItem(Pinloom::Ui::ListItem *item)
{
    if (!item) {
        return;
    }
    resultList_->setCurrentItem(item);
    activateCommandItem(item);
}

void PinloomCommandPanel::setTheme(PinloomCommandTheme theme)
{
    const PinloomVisualScheme scheme = activePinloomVisualScheme();
    const QString themeKey = QStringLiteral("%1:%2")
                                 .arg(static_cast<int>(theme))
                                 .arg(static_cast<int>(scheme));
    if (appliedThemeKey_ == themeKey && !styleSheet().isEmpty()) {
        return;
    }

    theme_ = theme;
    appliedThemeKey_ = themeKey;
    const PinloomVisualTokens tokens = pinloomVisualTokens(scheme);
    const QString accent = themeAccent(theme_, scheme).name(QColor::HexRgb);
    const QString panel = tokens.panel.name(QColor::HexRgb);
    const QString alternate = tokens.alternateSurface.name(QColor::HexRgb);
    const QString border = tokens.border.name(QColor::HexRgb);
    const QString text = tokens.text.name(QColor::HexRgb);
    const QString muted = tokens.mutedText.name(QColor::HexRgb);
    const QString hover = tokens.hoverSurface.name(QColor::HexRgb);
    const QString disabledSurface = tokens.disabledSurface.name(QColor::HexRgb);
    const QString disabledText = tokens.disabledText.name(QColor::HexRgb);
    const QString focus = tokens.focus.name(QColor::HexRgb);
    const QString selectionText = tokens.selectionText.name(QColor::HexRgb);
    const QString selection = tokens.selection.name(QColor::HexRgb);
    setStyleSheet(Ui::scopedStyleSheet(QStringLiteral(
        "QWidget#pinloomCommandPanel { color: %2; }"
        "QLineEdit#commandSearchEdit {"
        "  background-color: %3;"
        "  border: 1px solid %4;"
        "  border-radius: 8px;"
        "  color: %2;"
        "  font-size: 14px;"
        "  padding: 4px 12px;"
        "  selection-background-color: %12;"
        "  selection-color: %5;"
        "}"
        "QLineEdit#commandSearchEdit:focus { border: 2px solid %6; }"
        "QToolButton#commandClipLibraryButton {"
        "  background-color: %3;"
        "  border: 1px solid %4;"
        "  border-radius: 8px;"
        "  padding: 4px;"
        "}"
        "QToolButton#commandClipLibraryButton:hover { background-color: %7; }"
        "QWidget#commandQuickActionRow { background: transparent; }"
        "QLabel#commandQuickActionLabel { color: %8; font-size: 11px; padding: 0 4px; }"
        "QToolButton#commandRectangleAnchorButton, QToolButton#commandTextAnchorButton,"
        "QToolButton#commandPdfTextClipButton {"
        "  background-color: %3;"
        "  border: 1px solid %4;"
        "  border-radius: 6px;"
        "  color: %2;"
        "  min-height: 28px;"
        "  padding: 0 8px;"
        "}"
        "QToolButton#commandRectangleAnchorButton:hover, QToolButton#commandTextAnchorButton:hover,"
        "QToolButton#commandPdfTextClipButton:hover {"
        "  background-color: %7; border-color: %1; color: %1;"
        "}"
        "QToolButton#commandRectangleAnchorButton:focus, QToolButton#commandTextAnchorButton:focus,"
        "QToolButton#commandPdfTextClipButton:focus {"
        "  border: 2px solid %6;"
        "}"
        "QToolButton:disabled { color: %9; background-color: %10; border-color: %4; }"
        "QLabel#commandVersionLabel {"
        "  color: %8;"
        "  font-size: 11px;"
        "  padding: 0 2px;"
        "}"
        "QListView#commandResultList {"
        "  background-color: %3;"
        "  alternate-background-color: %11;"
        "  border: none;"
        "  border-radius: 0;"
        "  color: %2;"
        "  outline: 0;"
        "}"
        "QListView#commandResultList::item {"
        "  border-bottom: 1px solid %4;"
        "  padding: 8px;"
        "}"
        "QListView#commandResultList::item:hover { background-color: %7; }"
        "QListView#commandResultList::item:selected {"
        "  background-color: %12;"
        "  color: %5;"
        "}"
        "QWidget#commandFooter { background: transparent; }"
        "QLabel#commandStatusLabel, QLabel#commandKeyboardHint {"
        "  background: transparent;"
        "  border: none;"
        "  color: %8;"
        "  padding: 0;"
        "}"
    ).arg(accent,
          text,
          panel,
          border,
          selectionText,
          focus,
          hover,
          muted,
          disabledText,
          disabledSurface,
          alternate,
          selection)));
    update();
}

void PinloomCommandPanel::updatePresentation()
{
    const bool nextCompact = resultList_->count() == 0 && !clipPickerMode_;
    const bool showQuickActions = !clipPickerMode_;
    quickActionRow_->setVisible(showQuickActions);
    resultList_->setVisible(!nextCompact);
    footer_->setVisible(!nextCompact);
    statusLabel_->setVisible(!nextCompact);

    auto *boxLayout = static_cast<QVBoxLayout *>(layout());
    const QMargins margins = boxLayout->contentsMargins();
    int nextHeight = margins.top() + commandEdit_->height() + margins.bottom();
    if (showQuickActions) {
        nextHeight += boxLayout->spacing() + quickActionRow_->sizeHint().height();
    }
    if (!nextCompact) {
        const int statusHeight = std::max(28, statusLabel_->fontMetrics().height() + 8);
        footer_->setFixedHeight(statusHeight);
        const int availableListHeight = std::max(1, 430 - nextHeight - boxLayout->spacing() * 2 - statusHeight);
        const int visibleRows = std::min(resultList_->count(), showingResultActions_ ? 8 : 6);
        int listHeight = resultList_->frameWidth() * 2;
        for (int row = 0; row < visibleRows; ++row) {
            const QSize itemSize = resultList_->item(row)->sizeHint();
            const int rowHeight = itemSize.isValid()
                ? itemSize.height()
                : resultList_->fontMetrics().lineSpacing() * 2 + 12;
            if (row > 0 && listHeight + rowHeight > availableListHeight) break;
            listHeight += rowHeight;
        }
        listHeight = std::max(listHeight, resultList_->fontMetrics().lineSpacing() * 2 + 14);
        listHeight = std::min(listHeight, availableListHeight);
        resultList_->setFixedHeight(listHeight);
        nextHeight += boxLayout->spacing() * 2 + listHeight + statusHeight;
    }

    nextHeight = std::clamp(nextHeight, 62, 430);
    const bool changed = compact_ != nextCompact || preferredWindowHeight_ != nextHeight;
    compact_ = nextCompact;
    preferredWindowHeight_ = nextHeight;
    setFixedHeight(preferredWindowHeight_);
    updateGeometry();

    QWidget *host = window();
    if (host && host != this) {
        const auto hostMargins = host->contentsMargins();
        const int hostHeight = preferredWindowHeight_ + hostMargins.top() + hostMargins.bottom();
        host->setFixedHeight(hostHeight);
        host->resize(std::max(host->width(), host->minimumWidth()), hostHeight);
    } else if (height() != preferredWindowHeight_) {
        resize(width(), preferredWindowHeight_);
    }
    if (changed) {
        emit presentationChanged(compact_, preferredWindowHeight_);
    }
    update();
}

void PinloomCommandPanel::updateStatus(const QString &status)
{
    statusText_ = status;
    updateStatusDisplay();
    updateKeyboardHint();
    commandEdit_->setToolTip(statusText_);
    if (options_.statusChangedHandler) {
        options_.statusChangedHandler(statusText_);
    }
    emit statusChanged(statusText_);
    updatePresentation();
}

void PinloomCommandPanel::updateStatusDisplay()
{
    statusLabel_->setText(statusLabel_->fontMetrics().elidedText(
        statusText_.simplified(), Qt::ElideRight, std::max(0, statusLabel_->contentsRect().width())));
    statusLabel_->setToolTip(statusText_);
    statusLabel_->setAccessibleDescription(statusText_);
}

void PinloomCommandPanel::updateKeyboardHint()
{
    const auto *item = resultList_->currentItem();
    if (showingResultActions_) {
        keyboardHintLabel_->setText(resultActionForItem(item).enabled
            ? tr("Enter Run · Esc / ← Back") : tr("Unavailable · Esc / ← Back"));
    } else if (canExpandResultActions(item)) {
        keyboardHintLabel_->setText(tr("Enter %1 · → Actions").arg(commandTargetVerb(openTargetForCommandItem(item))));
    } else if (item) {
        const auto action = rowActionForItem(item);
        keyboardHintLabel_->setText(action == CommandRowAction::ClipInsert ? tr("Enter Insert")
            : action == CommandRowAction::ClipSave ? tr("Enter Save") : tr("Enter Open"));
    } else {
        keyboardHintLabel_->clear();
    }
}

bool PinloomCommandPanel::activateCommandItem(Pinloom::Ui::ListItem *item)
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
    if (action == CommandRowAction::InboxSave) {
        return saveInboxFromCommand();
    }
    if (action == CommandRowAction::RegisteredCommand) {
        return dispatchCommand(static_cast<CommandAction>(
            item->data(CommandIdRole).toInt()));
    }
    if (action == CommandRowAction::UnifiedTargetAction) {
        return activateResultActionFromItem(item);
    }

    updateStatus(tr("Unknown command"));
    return false;
}

bool PinloomCommandPanel::insertClipFromItem(const Pinloom::Ui::ListItem *item)
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

    QString operationStatus;
    if (!options_.clipInsertionHandler(result.clipId, &operationStatus)) {
        updateStatus(operationStatus.trimmed().isEmpty()
                         ? tr("Clip insertion failed")
                         : operationStatus.trimmed());
        return false;
    }

    updateStatus(operationStatus.trimmed().isEmpty()
                     ? tr("Inserted clip")
                     : operationStatus.trimmed());
    emit clipInserted(result.clipId);
    return true;
}

bool PinloomCommandPanel::activateUnifiedTargetFromItem(const Pinloom::Ui::ListItem *item)
{
    const PinloomOpenTarget target =
        openTargetForCommandItem(item, item ? resultList_->row(item) : -1);
    return activateUnifiedTarget(target);
}

bool PinloomCommandPanel::activateUnifiedTarget(const PinloomOpenTarget &target)
{
    if (target.deleted) {
        updateStatus(tr("Entry is deleted; press Right Arrow and choose Restore"));
        return false;
    }

    if (!target.clipId.trimmed().isEmpty()) {
        if (!options_.clipInsertionHandler) {
            updateStatus(tr("Clip insertion is not configured"));
            return false;
        }

        QString operationStatus;
        if (!options_.clipInsertionHandler(target.clipId, &operationStatus)) {
            updateStatus(operationStatus.trimmed().isEmpty()
                             ? tr("Clip insertion failed")
                             : operationStatus.trimmed());
            return false;
        }

        updateStatus(operationStatus.trimmed().isEmpty()
                         ? tr("Inserted clip")
                         : operationStatus.trimmed());
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

    auto folderAction = std::find_if(actions.begin(), actions.end(), [](const auto &action) {
        return action.id == QLatin1String(OpenFolderResultActionId);
    });
    const auto firstAction = folderAction != actions.end() ? *folderAction : openFolderAction(target);
    if (folderAction != actions.end()) actions.erase(folderAction);
    actions.prepend(firstAction);

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
    setTheme(themeForTarget(target));
    resultList_->clear();

    for (const PinloomCommandResultAction &action : actions) {
        auto *item = new Pinloom::Ui::ListItem(QString(), resultList_);
        item->setSizeHint(QSize(0, commandResultRowHeight(resultList_->font(), true)));
        item->setToolTip(action.enabled || action.disabledReason.trimmed().isEmpty()
                             ? action.detail
                             : action.disabledReason);
        storeResultAction(item, target, action);
        setCommandRowPresentation(item, action.label, QString(), QString(), action.id, true);
    }
    if (resultList_->count() > 0) {
        resultList_->setCurrentRow(0);
    }

    updateStatus(tr("Actions for %1").arg(commandTargetTitle(target)));
}

bool PinloomCommandPanel::activateResultActionFromItem(const Pinloom::Ui::ListItem *item)
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
    if (options_.unifiedEntryCommandHandler) {
        const PinloomEntry entry = entryFromOpenTarget(target);
        const PinloomCommandActionResult result =
            options_.unifiedEntryCommandHandler(this, entry, action);
        const QString fallback = result.completed()
            ? tr("Completed action \"%1\"").arg(action.label)
            : (result.cancelled()
                   ? tr("Canceled action \"%1\"").arg(action.label)
                   : tr("Unable to run action \"%1\"").arg(action.label));
        const QString status = commandActionResultStatus(result, fallback);
        updateStatus(status.trimmed().isEmpty() ? fallback : status.trimmed());
        return result.completed();
    }
    if (action.id == QLatin1String(OpenFolderResultActionId)) {
        QString status;
        const bool opened = openContainingFolderForPinloomEntry(entryFromOpenTarget(target), &status);
        updateStatus(status);
        return opened;
    }
    if (action.id == QLatin1String(PrimaryResultActionId)) {
        return activateUnifiedTarget(target);
    }
    updateStatus(tr("Result action is not configured"));
    return false;
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

bool PinloomCommandPanel::saveClipFromItem(const Pinloom::Ui::ListItem *item)
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
    request->aliases = cleanedValues(request->aliases);
    request->tags = cleanedValues(request->tags, true);

    QString error;
    if (!options_.clipSaveHandler(request.value(), &error)) {
        updateStatus(error.trimmed().isEmpty() ? tr("Unable to save clip") : error.trimmed());
        return false;
    }

    const QString savedName = request->name.trimmed().isEmpty() ? result.preview : request->name;
    openClipSearch(savedName);
    updateStatus(tr("Saved clip \"%1\"").arg(savedName));
    emit clipSaved(result.clipId);
    return true;
}

bool PinloomCommandPanel::triggerRectangleAnchorCapture()
{
    return dispatchCommand(CommandAction::AnchorPdfRectangle);
}

bool PinloomCommandPanel::triggerTextAnchorCapture()
{
    return dispatchCommand(CommandAction::AnchorPdfText);
}

bool PinloomCommandPanel::triggerPdfTextClipCapture()
{
    return dispatchCommand(CommandAction::ClipPdfText);
}

bool PinloomCommandPanel::openClipLibrary()
{
    return dispatchCommand(CommandAction::ClipLibrary);
}

bool PinloomCommandPanel::dispatchCommand(CommandAction id)
{
    if (id == CommandAction::ClipLibrary) {
        emit clipLibraryRequested();
    } else if (id == CommandAction::AnchorNew
               || id == CommandAction::AnchorPdfRectangle
               || id == CommandAction::AnchorPdfText) {
        emit anchorCaptureRequested();
    } else if (id == CommandAction::RootLibrary) {
        emit libraryRootRequested();
    }

    if (!commandDispatcher_) {
        updateStatus(tr("Command dispatcher is not configured"));
        return false;
    }

    PinloomCommandInvocation invocation;
    invocation.parent = this;
    const PinloomCommandDispatchResult result =
        commandDispatcher_->dispatch(id, invocation);
    const std::optional<PinloomCommandDefinition> definition =
        PinloomCommandRegistry::definition(id);
    const QString title = definition.has_value()
        ? definition->title
        : tr("command");
    QStringList lines;
    if (!result.message.trimmed().isEmpty()) {
        lines.append(result.message.trimmed());
    } else if (result.completed()) {
        lines.append(tr("Completed %1").arg(title));
    } else if (result.cancelled()) {
        lines.append(tr("Canceled %1").arg(title));
    } else {
        lines.append(tr("Unable to run %1").arg(title));
    }
    if (!result.diagnostics.trimmed().isEmpty()) {
        lines.append(tr("Diagnostics: %1").arg(result.diagnostics.trimmed()));
    }
    if (!result.nextUiHint.trimmed().isEmpty()) {
        lines.append(tr("Next: %1").arg(result.nextUiHint.trimmed()));
    }
    updateStatus(lines.join(QLatin1Char('\n')));
    return result.completed();
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
        updateStatus(tr("No pending Inbox item; drop a file or folder, or select one in Explorer"));
        return false;
    }

    QStringList savedResourceIds;
    QStringList savedNames;
    QString lastStatus;
    bool registeredRoot = false;
    for (int i = 0; i < filePaths.size(); ++i) {
        const QString filePath = filePaths.at(i);
        std::optional<InboxFileSaveRequest> promptedRequest = options_.inboxSaveRequestProvider
            ? options_.inboxSaveRequestProvider(this, filePath)
            : promptInboxSaveRequest(filePath);
        if (!promptedRequest.has_value()) {
            updateStatus(tr("Inbox save canceled"));
            return false;
        }
        InboxFileSaveRequest request = promptedRequest.value();

        if (request.filePath.trimmed().isEmpty()) {
            request.filePath = filePath;
        }
        request.aliases = cleanedValues(request.aliases);
        request.tags = cleanedValues(request.tags, true);

        if (request.name.trimmed().isEmpty()) {
            request.name = defaultInboxFileName(request.filePath);
        }

        const InboxFileSaveResult result = options_.inboxSaveHandler(request);
        if (!result.success()) {
            updateStatus(result.status.trimmed().isEmpty()
                             ? tr("Unable to save Inbox item")
                             : result.status.trimmed());
            return false;
        }
        savedResourceIds.append(result.resourceId);
        savedNames.append(result.displayName);
        lastStatus = result.status.trimmed();
        registeredRoot = registeredRoot || request.registerAsLibraryRoot;
    }

    pendingInboxFiles_.clear();

    if (savedResourceIds.size() == 1) {
        const QString savedName = savedNames.first().trimmed();
        setCommandText(registeredRoot
                           ? QStringLiteral("root;library")
                           : (savedName.isEmpty()
                                  ? QStringLiteral("inbox;search")
                                  : QStringLiteral("inbox;search %1").arg(savedName)));
        updateStatus(lastStatus.isEmpty()
                         ? tr("Saved Inbox item \"%1\"").arg(savedName)
                         : lastStatus);
        emit inboxSaved(savedResourceIds.first());
        return true;
    }

    setCommandText(registeredRoot ? QStringLiteral("root;library") : QStringLiteral("inbox;search"));
    updateStatus(tr("Saved %n Inbox item(s)", nullptr, savedResourceIds.size()));
    for (const QString &resourceId : savedResourceIds) {
        emit inboxSaved(resourceId);
    }
    return true;
}

std::optional<PinloomClipSaveRequest> PinloomCommandPanel::promptClipSaveRequest(const ClipSearchResult &result)
{
    Pinloom::Ui::Dialog dialog(this);
    dialog.setWindowTitle(tr("Save Clip"));
    auto *form = new Pinloom::Ui::FormLayout(&dialog);
    auto *nameEdit = Pinloom::Ui::lineEdit(result.displayName.trimmed().isEmpty() ? result.preview : result.displayName,
                                   &dialog);
    nameEdit->setObjectName(QStringLiteral("commandClipSaveNameEdit"));
    auto *aliasesEdit = Pinloom::Ui::lineEdit(&dialog);
    aliasesEdit->setObjectName(QStringLiteral("commandClipSaveAliasesEdit"));
    auto *tagsEdit = Pinloom::Ui::lineEdit(&dialog);
    tagsEdit->setObjectName(QStringLiteral("commandClipSaveTagsEdit"));
    auto *pinnedCheck = Pinloom::Ui::checkBox(tr("Pinned"), &dialog);
    pinnedCheck->setObjectName(QStringLiteral("commandClipSavePinnedCheck"));
    auto *buttons = new Pinloom::Ui::DialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
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
    const QFileInfo itemInfo(filePath);
    const bool folder = itemInfo.isDir();
    Pinloom::Ui::Dialog dialog(this);
    dialog.setWindowTitle(folder ? tr("Add Folder") : tr("Save File"));
    dialog.setMinimumWidth(520);
    auto *form = new Pinloom::Ui::FormLayout(&dialog);
    auto *locationLabel = Pinloom::Ui::label(QDir::toNativeSeparators(itemInfo.absoluteFilePath()), &dialog);
    locationLabel->setObjectName(QStringLiteral("commandInboxSaveLocationLabel"));
    locationLabel->setWordWrap(true);
    locationLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *nameEdit = Pinloom::Ui::lineEdit(defaultInboxFileName(filePath), &dialog);
    nameEdit->setObjectName(QStringLiteral("commandInboxSaveNameEdit"));
    auto *aliasesEdit = Pinloom::Ui::lineEdit(&dialog);
    aliasesEdit->setObjectName(QStringLiteral("commandInboxSaveAliasesEdit"));
    auto *tagsEdit = Pinloom::Ui::lineEdit(&dialog);
    tagsEdit->setObjectName(QStringLiteral("commandInboxSaveTagsEdit"));
    tagsEdit->setPlaceholderText(tr("Comma-separated tags"));
    if (options_.inboxTagProvider) {
        const QStringList availableTags = options_.inboxTagProvider();
        if (!availableTags.isEmpty()) {
            tagsEdit->setToolTip(tr("Existing tags: %1").arg(availableTags.join(QStringLiteral(", "))));
        }
    }
    auto *modeCombo = Pinloom::Ui::comboBox(&dialog);
    modeCombo->setObjectName(QStringLiteral("commandInboxSaveModeCombo"));
    if (folder) {
        modeCombo->addItem(tr("Tag this folder only"), false);
        modeCombo->addItem(tr("Register as a root directory"), true);
    } else {
        modeCombo->addItem(tr("Keep in original location"), static_cast<int>(InboxFileArchiveMode::Link));
        modeCombo->addItem(tr("Copy into the Pinloom library"), static_cast<int>(InboxFileArchiveMode::Copy));
    }
    auto *pinnedCheck = Pinloom::Ui::checkBox(tr("Pinned"), &dialog);
    pinnedCheck->setObjectName(QStringLiteral("commandInboxSavePinnedCheck"));
    auto *buttons = new Pinloom::Ui::DialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName(QStringLiteral("commandInboxSaveButtons"));

    form->addRow(tr("Location"), locationLabel);
    form->addRow(tr("Name"), nameEdit);
    form->addRow(tr("Aliases"), aliasesEdit);
    form->addRow(tr("Tags"), tagsEdit);
    form->addRow(folder ? tr("Folder role") : tr("Storage"), modeCombo);
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
    request.mode = folder
        ? InboxFileArchiveMode::Link
        : static_cast<InboxFileArchiveMode>(modeCombo->currentData().toInt());
    request.registerAsLibraryRoot = folder && modeCombo->currentData().toBool();
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

    if (localFilePathsFromMimeData(mimeData).isEmpty()
        && droppedTextFromMimeData(mimeData).isEmpty()) {
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
    if (!filePaths.isEmpty()) {
        setPendingInboxFiles(filePaths);
        setCommandText(QStringLiteral("inbox;new"));
        updateStatus(tr("Inbox pending: %1").arg(inboxFilesSummary(pendingInboxFiles_)));
        dropEvent->acceptProposedAction();
        QTimer::singleShot(0, this, [this]() { saveInboxFromCommand(); });
        return true;
    }

    const QString text = droppedTextFromMimeData(dropEvent->mimeData());
    if (text.isEmpty() || !options_.droppedTextSaveHandler) {
        return false;
    }
    dropEvent->acceptProposedAction();
    setTheme(PinloomCommandTheme::Clip);
    updateStatus(tr("Dropped text pending"));
    QTimer::singleShot(0, this, [this, text]() {
        QString status;
        const bool saved = options_.droppedTextSaveHandler(this, text, &status);
        const QString finalStatus = status.trimmed().isEmpty()
            ? (saved ? tr("Saved dropped text") : tr("Unable to save dropped text"))
            : status.trimmed();
        if (saved) {
            openClipSearch();
        }
        updateStatus(finalStatus);
    });
    return true;
}

void showCommandPanelForHotkey(QWidget &commandWindow, PinloomCommandPanel &panel)
{
    if (auto *controller = commandWindow.findChild<CommandFloatingController *>()) {
        if (!controller->requestExpansion()) return;
    }
    const auto margins = commandWindow.contentsMargins();
    const int hostHeight = panel.preferredWindowHeight() + margins.top() + margins.bottom();
    commandWindow.setFixedHeight(hostHeight);
    commandWindow.resize(std::max(commandWindow.width(), commandWindow.minimumWidth()),
                         hostHeight);
    if (commandWindow.isMinimized()) {
        commandWindow.showNormal();
    } else {
        commandWindow.show();
    }

    commandWindow.raise();
    commandWindow.activateWindow();
    panel.focusCommand();
}

PinloomEntry enrichedPinloomEntryForAction(const PinloomEntry &entry,
                                           const std::optional<Clip> &clip,
                                           const std::optional<Resource> &resource,
                                           const std::optional<ResourceUsage> &usage)
{
    PinloomEntry enriched = entry;

    if (clip.has_value()) {
        enriched.type = PinloomEntryType::SavedClip;
        enriched.id = QStringLiteral("clip:%1").arg(clip->id);
        enriched.clipId = clip->id;
        enriched.resourceId.clear();
        enriched.anchor.reset();
        enriched.name = clip->name.trimmed().isEmpty() ? clip->preview : clip->name;
        enriched.aliases = clip->aliases;
        enriched.tags = clip->tags;
        enriched.pinned = clip->pinned;
        enriched.deleted = clip->state == ClipState::Deleted;
        enriched.location = clip->preview;
        enriched.targetSummary = clip->preview;
        enriched.usedAt = clip->usedAt;
        enriched.metadata.insert(QStringLiteral("clipStorageBackend"),
                                 clip->storageBackend == ClipStorageBackend::Obsidian
                                     ? QStringLiteral("obsidian")
                                     : QStringLiteral("local"));
    }

    if (resource.has_value()) {
        enriched.resourceId = resource->id;
        enriched.resourceKind = resource->kind;
        enriched.location = resource->location;
        enriched.targetSummary = resource->location;

        if (enriched.anchor.has_value()) {
            auto anchor = enriched.anchor.value();
            for (const Anchor &candidate : resource->anchors) {
                if (!anchor.id.trimmed().isEmpty() && candidate.id == anchor.id) {
                    anchor = candidate;
                    break;
                }
            }
            enriched.type = PinloomEntryType::Anchor;
            enriched.anchor = anchor;
            enriched.id = anchor.id.trimmed().isEmpty()
                ? QStringLiteral("anchor:%1").arg(resource->id)
                : QStringLiteral("anchor:%1").arg(anchor.id);
            enriched.name = anchor.name.trimmed().isEmpty()
                ? (resource->title.trimmed().isEmpty() ? resource->location : resource->title)
                : anchor.name;
            enriched.aliases = anchor.aliases;
            enriched.tags = anchor.tags;
            enriched.pinned = anchor.pinned;
            enriched.deleted = entry.deleted || resource->deleted || anchor.deleted;
            if (!anchor.targetFile.trimmed().isEmpty()) {
                enriched.targetSummary = anchor.targetFile;
                enriched.location = anchor.targetFile;
            }
        } else {
            enriched.type = isInboxResourceId(resource->id) ? PinloomEntryType::Inbox : PinloomEntryType::FileResource;
            enriched.id = QStringLiteral("resource:%1").arg(resource->id);
            enriched.name = resource->title.trimmed().isEmpty()
                ? QFileInfo(resource->location).fileName()
                : resource->title;
            enriched.aliases = resource->aliases;
            enriched.tags = resource->tags;
            enriched.pinned = usage.has_value() && usage->pinned;
            enriched.deleted = entry.deleted || resource->deleted;
        }
    } else if (enriched.anchor.has_value()) {
        enriched.type = PinloomEntryType::Anchor;
        enriched.aliases = enriched.anchor->aliases;
        enriched.tags = enriched.anchor->tags;
        enriched.pinned = enriched.anchor->pinned;
        enriched.deleted = entry.deleted || enriched.anchor->deleted;
    }

    if (enriched.name.trimmed().isEmpty()) {
        enriched.name = enriched.id.trimmed().isEmpty() ? QStringLiteral("Pinloom entry") : enriched.id;
    }
    enriched.metadata.insert(QStringLiteral("resourceId"), enriched.resourceId);
    enriched.metadata.insert(QStringLiteral("clipId"), enriched.clipId);
    enriched.metadata.insert(QStringLiteral("location"), enriched.location);
    enriched.metadata.insert(QStringLiteral("deleted"), enriched.deleted);
    return enriched;
}

bool openContainingFolderForPinloomEntry(const PinloomEntry &entry, QString *status)
{
    const auto action = openFolderAction(openTargetFromEntry(entry));
    if (!action.enabled) {
        if (status) *status = action.disabledReason;
        return false;
    }
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(action.detail));
    if (status) {
        *status = opened ? QObject::tr("Opened folder: %1").arg(action.detail)
                         : QObject::tr("Unable to open folder: %1").arg(action.detail);
    }
    return opened;
}

QList<PinloomCommandResultAction> defaultActionsForPinloomEntry(const PinloomEntry &entry, bool removeEnabled)
{
    const PinloomOpenTarget target = openTargetFromEntry(entry);
    QList<PinloomCommandResultAction> actions;
    actions.append(openFolderAction(target));
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

    const bool deleted = entry.deleted || (entry.anchor.has_value() && entry.anchor->deleted);
    const QString restoreFirstReason = QStringLiteral("Restore this entry before editing or using it");
    addAction(QString::fromLatin1(PrimaryResultActionId),
              commandTargetVerb(target),
              QStringLiteral("%1 %2").arg(commandTargetVerb(target), entry.name),
              !deleted,
              restoreFirstReason);
    if (entry.type == PinloomEntryType::SavedClip
        && entry.metadata.value(QStringLiteral("clipStorageBackend")).toString()
               == QLatin1String("obsidian")) {
        addAction(QStringLiteral("open_source"),
                  QStringLiteral("Source note"),
                  QStringLiteral("Open this Saved Clip in Obsidian"));
    }
    addAction(QStringLiteral("rename"),
              QStringLiteral("Rename"),
              QStringLiteral("Rename this Pinloom entry"),
              !deleted,
              restoreFirstReason);
    addAction(QStringLiteral("edit_aliases"),
              QStringLiteral("Aliases"),
              QStringLiteral("Edit aliases for this Pinloom entry"),
              !deleted,
              restoreFirstReason);
    addAction(QStringLiteral("edit_tags"),
              QStringLiteral("Tags"),
              QStringLiteral("Edit tags for this Pinloom entry"),
              !deleted,
              restoreFirstReason);
    addAction(entry.pinned ? QStringLiteral("unpin") : QStringLiteral("pin"),
              entry.pinned ? QStringLiteral("Unpin") : QStringLiteral("Pin"),
              QStringLiteral("Change pinned state"),
              !deleted,
              restoreFirstReason);
    if (deleted) {
        addAction(QStringLiteral("restore"),
                  QStringLiteral("Restore"),
                  QStringLiteral("Restore this entry to ordinary Pinloom search"));
        addAction(QStringLiteral("remove"),
                  QStringLiteral("Remove"),
                  QStringLiteral("Already deleted in Pinloom"),
                  false,
                  QStringLiteral("This entry is already deleted; use Restore"));
    } else {
        addAction(QStringLiteral("remove"),
                  QStringLiteral("Remove"),
                  entry.type == PinloomEntryType::SavedClip
                      ? QStringLiteral("Archive this Saved Clip inside Pinloom")
                      : entry.type == PinloomEntryType::Anchor
                            ? QStringLiteral("Delete this Pinloom anchor without deleting the target file")
                            : QStringLiteral("Remove this resource from Pinloom without deleting the original file"),
                  removeEnabled,
                  removeEnabled ? QString() : QStringLiteral("Remove is not available"));
    }
    return actions;
}

} // namespace Pinloom
