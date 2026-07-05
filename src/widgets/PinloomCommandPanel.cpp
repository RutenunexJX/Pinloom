#include "pinloom/widgets/PinloomCommandPanel.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSize>
#include <QVBoxLayout>
#include <algorithm>
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

enum class CommandNamespace {
    None,
    Clip,
    Anchor,
    Search
};

enum class CommandAction {
    None,
    ClipSearch,
    ClipNew,
    AnchorNew,
    OpenSearch
};

enum class CommandRowAction {
    Unknown = 0,
    OpenCommand,
    ClipInsert,
    ClipSave,
    AnchorCapture,
    OpenSearchWindow
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
    if (!item || (action != CommandRowAction::ClipInsert && action != CommandRowAction::ClipSave)) {
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
    resize(760, 300);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 8);
    layout->setSpacing(6);

    commandEdit_ = new QLineEdit(this);
    commandEdit_->setObjectName(QStringLiteral("commandSearchEdit"));
    commandEdit_->setPlaceholderText(tr("Command"));
    commandEdit_->setClearButtonEnabled(true);

    resultList_ = new QListWidget(this);
    resultList_->setObjectName(QStringLiteral("commandResultList"));
    resultList_->setAlternatingRowColors(true);
    resultList_->setUniformItemSizes(true);
    resultList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("commandStatusLabel"));
    statusLabel_->setWordWrap(true);

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
        } else {
            updateStatus(tr("Unknown command"));
        }
        return false;
    }

    return activateCommandItem(item);
}

bool PinloomCommandPanel::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == commandEdit_ || watched == resultList_) && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const Qt::KeyboardModifiers modifiers =
            keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
        const int key = keyEvent->key();

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

void PinloomCommandPanel::refreshResults()
{
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

    constexpr int resultLimit = 100;
    if (command.commandNamespace == CommandNamespace::Clip
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
        updateStatus(tr("Type c for Clip commands or k for Anchor commands"));
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

    if (action == CommandRowAction::ClipInsert) {
        return insertClipFromItem(item);
    }
    if (action == CommandRowAction::ClipSave) {
        return saveClipFromItem(item);
    }
    if (action == CommandRowAction::AnchorCapture) {
        return captureAnchor();
    }
    if (action == CommandRowAction::OpenSearchWindow) {
        return openSearchWindow(item);
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

} // namespace Pinloom
