#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <map>
#include <optional>

class QWidget;

namespace Pinloom {

enum class PinloomCommandNamespace {
    None,
    Clip,
    Anchor,
    Inbox,
    Library,
    Root,
    Application
};

enum class PinloomCommandId {
    None,
    ClipSearch,
    ClipNew,
    ClipLibrary,
    ClipPdfText,
    AnchorNew,
    AnchorPdfRectangle,
    AnchorPdfText,
    AnchorLibrary,
    InboxNew,
    InboxSearch,
    OpenSearch,
    RestoreSearch,
    RootLibrary,
    Settings,
    Diagnostics,
    // Internal command used by host/deep-link activation. It is intentionally
    // absent from user-visible registry rows.
    OpenIdentity
};

struct PinloomCommandDefinition {
    PinloomCommandId id = PinloomCommandId::None;
    PinloomCommandNamespace commandNamespace = PinloomCommandNamespace::None;
    QString canonical;
    QString actionName;
    QString title;
    QString verb;
    QString detail;
    QStringList aliases;
    bool executable = false;
};

struct PinloomParsedCommand {
    PinloomCommandNamespace commandNamespace = PinloomCommandNamespace::None;
    PinloomCommandId action = PinloomCommandId::None;
    QString query;

    bool recognized() const;
};

class PinloomCommandRegistry final {
public:
    static const QList<PinloomCommandDefinition> &definitions();
    static QList<PinloomCommandDefinition> definitionsForNamespace(
        PinloomCommandNamespace commandNamespace);
    static std::optional<PinloomCommandDefinition> definition(
        PinloomCommandId id);
    static PinloomParsedCommand parse(const QString &text);
    static bool validate(QString *error = nullptr);
};

enum class PinloomCommandExecutionState {
    Completed,
    Cancelled,
    Failed
};

struct PinloomCommandInvocation {
    QWidget *parent = nullptr;
    QVariantMap arguments;
};

struct PinloomCommandDispatchResult {
    PinloomCommandExecutionState state = PinloomCommandExecutionState::Failed;
    QString message;
    QString diagnostics;
    QString nextUiHint;

    bool completed() const;
    bool cancelled() const;
    bool failed() const;

    static PinloomCommandDispatchResult complete(
        const QString &message = QString(),
        const QString &nextUiHint = QString());
    static PinloomCommandDispatchResult cancel(
        const QString &message = QString());
    static PinloomCommandDispatchResult failure(
        const QString &message,
        const QString &diagnostics = QString(),
        const QString &nextUiHint = QString());
};

class PinloomCommandDispatcher final {
public:
    using Handler = std::function<PinloomCommandDispatchResult(
        const PinloomCommandInvocation &)>;

    bool registerHandler(PinloomCommandId id,
                         Handler handler,
                         QString *error = nullptr);
    bool hasHandler(PinloomCommandId id) const;
    PinloomCommandDispatchResult dispatch(
        PinloomCommandId id,
        const PinloomCommandInvocation &invocation = {}) const;

private:
    std::map<PinloomCommandId, Handler> handlers_;
};

PinloomCommandDispatchResult pinloomCommandResultFromBoolean(
    bool succeeded,
    const QString &status,
    const QString &successFallback,
    const QString &failureFallback);

} // namespace Pinloom
