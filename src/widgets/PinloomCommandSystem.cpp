#include "pinloom/widgets/PinloomCommandSystem.h"

#include <QSet>
#include <algorithm>

namespace Pinloom {

namespace {

struct CommandDomainDefinition {
    PinloomCommandNamespace commandNamespace = PinloomCommandNamespace::None;
    QString canonical;
    QStringList aliases;
};

const QList<CommandDomainDefinition> &commandDomains()
{
    static const QList<CommandDomainDefinition> domains{
        {PinloomCommandNamespace::Clip, QStringLiteral("clip"), {QStringLiteral("c")}},
        {PinloomCommandNamespace::Anchor, QStringLiteral("anchor"), {QStringLiteral("k")}},
        {PinloomCommandNamespace::Inbox, QStringLiteral("inbox"), {QStringLiteral("i")}},
        {PinloomCommandNamespace::Library, QStringLiteral("library"), {QStringLiteral("l")}},
        {PinloomCommandNamespace::Root, QStringLiteral("root"), {QStringLiteral("r")}}
    };
    return domains;
}

int orderedSubsequenceScore(const QString &pattern, const QString &candidate)
{
    const QString foldedPattern = pattern.trimmed().toCaseFolded();
    const QString foldedCandidate = candidate.trimmed().toCaseFolded();
    if (foldedPattern.isEmpty() || foldedCandidate.isEmpty()) {
        return -1;
    }
    if (foldedPattern == foldedCandidate) {
        return 100000;
    }

    QList<int> positions;
    positions.reserve(foldedPattern.size());
    int cursor = 0;
    for (const QChar character : foldedPattern) {
        const int position = foldedCandidate.indexOf(character, cursor);
        if (position < 0) {
            return -1;
        }
        positions.append(position);
        cursor = position + 1;
    }

    int score = foldedPattern.size() * 1000 - foldedCandidate.size();
    if (!positions.isEmpty() && positions.first() == 0) {
        score += 20000;
    }
    if (foldedCandidate.startsWith(foldedPattern)) {
        score += 10000;
    }
    for (int index = 1; index < positions.size(); ++index) {
        const int gap = positions.at(index) - positions.at(index - 1) - 1;
        score += gap == 0 ? 500 : -gap * 20;
    }
    return score;
}

std::optional<PinloomCommandNamespace> matchCommandDomain(
    const QString &pattern)
{
    int bestScore = -1;
    std::optional<PinloomCommandNamespace> bestMatch;
    bool ambiguous = false;
    for (const CommandDomainDefinition &domain : commandDomains()) {
        int score = orderedSubsequenceScore(pattern, domain.canonical);
        for (const QString &alias : domain.aliases) {
            if (pattern.compare(alias, Qt::CaseInsensitive) == 0) {
                score = std::max(score, 90000);
            }
        }
        if (score > bestScore) {
            bestScore = score;
            bestMatch = domain.commandNamespace;
            ambiguous = false;
        } else if (score >= 0 && score == bestScore
                   && bestMatch.has_value()
                   && bestMatch.value() != domain.commandNamespace) {
            ambiguous = true;
        }
    }
    return bestScore >= 0 && !ambiguous ? bestMatch : std::nullopt;
}

std::optional<PinloomCommandId> matchCommandAction(
    PinloomCommandNamespace commandNamespace,
    const QString &pattern)
{
    int bestScore = -1;
    std::optional<PinloomCommandId> bestMatch;
    bool ambiguous = false;
    for (const PinloomCommandDefinition &definition
         : PinloomCommandRegistry::definitions()) {
        if (definition.commandNamespace != commandNamespace) {
            continue;
        }
        int score = orderedSubsequenceScore(pattern, definition.actionName);
        const QString structuredPrefix = definition.canonical.section(
            QLatin1Char(';'), 0, 0) + QLatin1Char(';');
        for (const QString &alias : definition.aliases) {
            if (!alias.startsWith(structuredPrefix, Qt::CaseInsensitive)) {
                continue;
            }
            const QString aliasAction = alias.mid(structuredPrefix.size());
            if (pattern.compare(aliasAction, Qt::CaseInsensitive) == 0) {
                score = std::max(score, 90000);
            }
        }
        if (score > bestScore) {
            bestScore = score;
            bestMatch = definition.id;
            ambiguous = false;
        } else if (score >= 0 && score == bestScore
                   && bestMatch.has_value()
                   && bestMatch.value() != definition.id) {
            ambiguous = true;
        }
    }
    return bestScore >= 0 && !ambiguous ? bestMatch : std::nullopt;
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

QString commandLabel(PinloomCommandId id)
{
    const std::optional<PinloomCommandDefinition> definition =
        PinloomCommandRegistry::definition(id);
    return definition.has_value()
        ? definition->canonical
        : QStringLiteral("unknown");
}

} // namespace

bool PinloomParsedCommand::recognized() const
{
    return commandNamespace != PinloomCommandNamespace::None
        || action != PinloomCommandId::None;
}

const QList<PinloomCommandDefinition> &PinloomCommandRegistry::definitions()
{
    static const QList<PinloomCommandDefinition> values{
        {PinloomCommandId::ClipSearch,
         PinloomCommandNamespace::Clip,
         QStringLiteral("clip;search"),
         QStringLiteral("search"),
         QStringLiteral("Clip Search"),
         QStringLiteral("Open"),
         QStringLiteral("clip;search <query> - search Clip name, alias, tag, or content"),
         {},
         false},
        {PinloomCommandId::ClipNew,
         PinloomCommandNamespace::Clip,
         QStringLiteral("clip;new"),
         QStringLiteral("new"),
         QStringLiteral("New Saved Clip"),
         QStringLiteral("Open"),
         QStringLiteral("clip;new - save a recent clipboard history item"),
         {},
         false},
        {PinloomCommandId::ClipLibrary,
         PinloomCommandNamespace::Clip,
         QStringLiteral("clip;library"),
         QStringLiteral("library"),
         QStringLiteral("Clip Library"),
         QStringLiteral("Open"),
         QStringLiteral("clip;library - browse and manage Saved, History, and Trash Clips"),
         {},
         true},
        {PinloomCommandId::ClipPdfText,
         PinloomCommandNamespace::Clip,
         QStringLiteral("clip;pdf-text"),
         QStringLiteral("pdf-text"),
         QStringLiteral("PDF Text Clip"),
         QStringLiteral("Capture"),
         QStringLiteral("clip;pdf-text - save selected text from the remembered SumatraPDF document"),
         {QStringLiteral("clip;text")},
         true},
        {PinloomCommandId::AnchorNew,
         PinloomCommandNamespace::Anchor,
         QStringLiteral("anchor;new"),
         QStringLiteral("new"),
         QStringLiteral("New Anchor / Capture Anchor"),
         QStringLiteral("Capture"),
         QStringLiteral("anchor;new - capture the remembered app position"),
         {},
         true},
        {PinloomCommandId::AnchorPdfRectangle,
         PinloomCommandNamespace::Anchor,
         QStringLiteral("anchor;rectangle"),
         QStringLiteral("rectangle"),
         QStringLiteral("PDF Rectangle Anchor"),
         QStringLiteral("Capture"),
         QStringLiteral("anchor;rectangle - select a rectangle in the remembered SumatraPDF document"),
         {QStringLiteral("anchor;rect")},
         true},
        {PinloomCommandId::AnchorPdfText,
         PinloomCommandNamespace::Anchor,
         QStringLiteral("anchor;text"),
         QStringLiteral("text"),
         QStringLiteral("PDF Text Anchor"),
         QStringLiteral("Capture"),
         QStringLiteral("anchor;text - anchor selected text in the remembered SumatraPDF document"),
         {},
         true},
        {PinloomCommandId::AnchorLibrary,
         PinloomCommandNamespace::Anchor,
         QStringLiteral("anchor;library"),
         QStringLiteral("library"),
         QStringLiteral("Anchor Library"),
         QStringLiteral("Open"),
         QStringLiteral("anchor;library - organize all marked files"),
         {},
         true},
        {PinloomCommandId::InboxNew,
         PinloomCommandNamespace::Inbox,
         QStringLiteral("inbox;new"),
         QStringLiteral("new"),
         QStringLiteral("Add Inbox Item"),
         QStringLiteral("Open"),
         QStringLiteral("inbox;new - tag or archive a dropped item or Explorer selection"),
         {},
         false},
        {PinloomCommandId::InboxSearch,
         PinloomCommandNamespace::Inbox,
         QStringLiteral("inbox;search"),
         QStringLiteral("search"),
         QStringLiteral("Inbox Search"),
         QStringLiteral("Open"),
         QStringLiteral("inbox;search <query> - search archived Inbox files"),
         {},
         false},
        {PinloomCommandId::OpenSearch,
         PinloomCommandNamespace::Library,
         QStringLiteral("library;search"),
         QStringLiteral("search"),
         QStringLiteral("Library Search"),
         QStringLiteral("Open"),
         QStringLiteral("library;search <query> - search all active entries"),
         {QStringLiteral("s"), QStringLiteral("search")},
         false},
        {PinloomCommandId::RestoreSearch,
         PinloomCommandNamespace::Library,
         QStringLiteral("library;restore"),
         QStringLiteral("restore"),
         QStringLiteral("Restore Deleted Entry"),
         QStringLiteral("Open"),
         QStringLiteral("library;restore <query> - search deleted entries"),
         {QStringLiteral("restore"), QStringLiteral("trash")},
         false},
        {PinloomCommandId::RootLibrary,
         PinloomCommandNamespace::Root,
         QStringLiteral("root;library"),
         QStringLiteral("library"),
         QStringLiteral("Root Library"),
         QStringLiteral("Open"),
         QStringLiteral("root;library - browse registered roots and tag their contents"),
         {},
         true},
        {PinloomCommandId::Settings,
         PinloomCommandNamespace::Application,
         QStringLiteral("settings"),
         QStringLiteral("settings"),
         QStringLiteral("Settings"),
         QStringLiteral("Open"),
         QStringLiteral("settings - configure Pinloom"),
         {QStringLiteral("preferences")},
         true},
        {PinloomCommandId::Diagnostics,
         PinloomCommandNamespace::Application,
         QStringLiteral("diagnostics"),
         QStringLiteral("diagnostics"),
         QStringLiteral("Diagnostics"),
         QStringLiteral("Open"),
         QStringLiteral("diagnostics - inspect resident status and recent errors"),
         {QStringLiteral("status")},
         true}
    };
    return values;
}

QList<PinloomCommandDefinition> PinloomCommandRegistry::definitionsForNamespace(
    PinloomCommandNamespace commandNamespace)
{
    QList<PinloomCommandDefinition> result;
    for (const PinloomCommandDefinition &definition : definitions()) {
        if (definition.commandNamespace == commandNamespace) {
            result.append(definition);
        }
    }
    return result;
}

std::optional<PinloomCommandDefinition> PinloomCommandRegistry::definition(
    PinloomCommandId id)
{
    const auto match = std::find_if(
        definitions().cbegin(), definitions().cend(),
        [id](const PinloomCommandDefinition &candidate) {
            return candidate.id == id;
        });
    return match == definitions().cend()
        ? std::nullopt
        : std::optional<PinloomCommandDefinition>(*match);
}

PinloomParsedCommand PinloomCommandRegistry::parse(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QString folded = trimmed.toCaseFolded();
    for (const PinloomCommandDefinition &definition : definitions()) {
        for (const QString &alias : definition.aliases) {
            if (alias.contains(QLatin1Char(';'))) {
                continue;
            }
            const QString foldedAlias = alias.toCaseFolded();
            if (folded == foldedAlias
                || folded.startsWith(foldedAlias + QLatin1Char(' '))) {
                return {definition.commandNamespace,
                        definition.id,
                        trimmed.mid(alias.size()).trimmed()};
            }
        }
    }

    for (const PinloomCommandDefinition &definition : definitions()) {
        if (definition.commandNamespace != PinloomCommandNamespace::Application) {
            continue;
        }
        const QString canonical = definition.canonical.toCaseFolded();
        if (folded == canonical
            || folded.startsWith(canonical + QLatin1Char(' '))) {
            return {definition.commandNamespace,
                    definition.id,
                    trimmed.mid(definition.canonical.size()).trimmed()};
        }
    }

    int tokenEnd = 0;
    const QString commandToken = readCommandToken(trimmed, &tokenEnd);
    int separator = commandToken.indexOf(QLatin1Char(';'));
    if (separator < 0) {
        separator = commandToken.indexOf(QLatin1Char(':'));
    }
    if (separator >= 0) {
        const std::optional<PinloomCommandNamespace> commandNamespace =
            matchCommandDomain(commandToken.left(separator));
        if (!commandNamespace.has_value()) {
            return {};
        }
        PinloomParsedCommand state;
        state.commandNamespace = commandNamespace.value();
        state.query = trimmed.mid(tokenEnd).trimmed();
        const QString actionPattern = commandToken.mid(separator + 1);
        if (!actionPattern.isEmpty()) {
            const std::optional<PinloomCommandId> action =
                matchCommandAction(state.commandNamespace, actionPattern);
            if (action.has_value()) {
                state.action = action.value();
            }
        }
        return state;
    }

    if (!trimmed.mid(tokenEnd).trimmed().isEmpty()) {
        return {};
    }
    const std::optional<PinloomCommandNamespace> commandNamespace =
        matchCommandDomain(commandToken);
    if (!commandNamespace.has_value()) {
        return {};
    }
    PinloomParsedCommand state;
    state.commandNamespace = commandNamespace.value();
    return state;
}

bool PinloomCommandRegistry::validate(QString *error)
{
    if (error) {
        error->clear();
    }
    QSet<int> ids;
    QSet<QString> forms;
    for (const PinloomCommandDefinition &definition : definitions()) {
        if (definition.id == PinloomCommandId::None
            || definition.commandNamespace == PinloomCommandNamespace::None
            || definition.canonical.trimmed().isEmpty()
            || definition.title.trimmed().isEmpty()) {
            if (error) {
                *error = QStringLiteral("Command registry contains an incomplete definition");
            }
            return false;
        }
        const int id = static_cast<int>(definition.id);
        if (ids.contains(id)) {
            if (error) {
                *error = QStringLiteral("Duplicate command id %1").arg(id);
            }
            return false;
        }
        ids.insert(id);

        const QString canonical = definition.canonical.trimmed().toCaseFolded();
        if (forms.contains(canonical)) {
            if (error) {
                *error = QStringLiteral("Duplicate command form %1").arg(canonical);
            }
            return false;
        }
        forms.insert(canonical);
        for (const QString &aliasValue : definition.aliases) {
            const QString alias = aliasValue.trimmed().toCaseFolded();
            if (alias.isEmpty() || forms.contains(alias)) {
                if (error) {
                    *error = QStringLiteral("Duplicate command alias %1").arg(alias);
                }
                return false;
            }
            forms.insert(alias);
        }
    }
    return true;
}

bool PinloomCommandDispatchResult::completed() const
{
    return state == PinloomCommandExecutionState::Completed;
}

bool PinloomCommandDispatchResult::cancelled() const
{
    return state == PinloomCommandExecutionState::Cancelled;
}

bool PinloomCommandDispatchResult::failed() const
{
    return state == PinloomCommandExecutionState::Failed;
}

PinloomCommandDispatchResult PinloomCommandDispatchResult::complete(
    const QString &message,
    const QString &nextUiHint)
{
    PinloomCommandDispatchResult result;
    result.state = PinloomCommandExecutionState::Completed;
    result.message = message;
    result.nextUiHint = nextUiHint;
    return result;
}

PinloomCommandDispatchResult PinloomCommandDispatchResult::cancel(
    const QString &message)
{
    PinloomCommandDispatchResult result;
    result.state = PinloomCommandExecutionState::Cancelled;
    result.message = message;
    return result;
}

PinloomCommandDispatchResult PinloomCommandDispatchResult::failure(
    const QString &message,
    const QString &diagnostics,
    const QString &nextUiHint)
{
    PinloomCommandDispatchResult result;
    result.state = PinloomCommandExecutionState::Failed;
    result.message = message;
    result.diagnostics = diagnostics;
    result.nextUiHint = nextUiHint;
    return result;
}

bool PinloomCommandDispatcher::registerHandler(PinloomCommandId id,
                                               Handler handler,
                                               QString *error)
{
    if (error) {
        error->clear();
    }
    if (id == PinloomCommandId::None || !handler) {
        if (error) {
            *error = QStringLiteral("A command id and handler are required");
        }
        return false;
    }
    if (handlers_.find(id) != handlers_.end()) {
        if (error) {
            *error = QStringLiteral("Handler already registered for %1")
                         .arg(commandLabel(id));
        }
        return false;
    }
    handlers_.emplace(id, std::move(handler));
    return true;
}

bool PinloomCommandDispatcher::hasHandler(PinloomCommandId id) const
{
    return handlers_.find(id) != handlers_.end();
}

PinloomCommandDispatchResult PinloomCommandDispatcher::dispatch(
    PinloomCommandId id,
    const PinloomCommandInvocation &invocation) const
{
    const auto handler = handlers_.find(id);
    if (handler == handlers_.end()) {
        return PinloomCommandDispatchResult::failure(
            QStringLiteral("Command %1 is not configured").arg(commandLabel(id)),
            QStringLiteral("commandId=%1").arg(static_cast<int>(id)));
    }
    return handler->second(invocation);
}

PinloomCommandDispatchResult pinloomCommandResultFromBoolean(
    bool succeeded,
    const QString &status,
    const QString &successFallback,
    const QString &failureFallback)
{
    const QString message = status.trimmed().isEmpty()
        ? (succeeded ? successFallback : failureFallback)
        : status.trimmed();
    if (succeeded) {
        return PinloomCommandDispatchResult::complete(message);
    }
    if (message.contains(QStringLiteral("cancel"), Qt::CaseInsensitive)) {
        return PinloomCommandDispatchResult::cancel(message);
    }
    return PinloomCommandDispatchResult::failure(message);
}

} // namespace Pinloom
