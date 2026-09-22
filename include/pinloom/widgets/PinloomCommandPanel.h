#pragma once

#include "pinloom/core/InboxFileCapture.h"
#include "pinloom/clip/ClipRepository.h"
#include "pinloom/clip/ClipSearch.h"
#include "pinloom/widgets/PinloomCommandSystem.h"
#include "pinloom/widgets/PinloomEntry.h"

#include <QWidget>
#include <functional>
#include <memory>
#include <optional>

class QLabel;
class QEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QLineEdit;
#include "pinloom/widgets/PinloomItemViews.h"

class QPaintEvent;
class QToolButton;

namespace Pinloom {

enum class PinloomCommandTheme {
    Neutral,
    Anchor,
    Clip,
    Inbox
};

struct PinloomCommandResultAction {
    QString id;
    QString label;
    QString detail;
    bool enabled = true;
    QString disabledReason;
};

using PinloomCommandActionResult = PinloomCommandDispatchResult;

struct PinloomCommandPanelOptions {
    PinloomCommandDispatcher *commandDispatcher = nullptr;
    std::function<QList<PinloomEntry>(const QString &query)> unifiedEntrySearchHandler;
    std::function<QList<PinloomEntry>(const QString &query)> deletedEntrySearchHandler;
    std::function<QList<PinloomCommandResultAction>(const PinloomEntry &entry)> unifiedEntryActionProvider;
    std::function<bool(const PinloomOpenTarget &target, QString *status)> anchorJumpHandler;
    std::function<bool(const PinloomOpenTarget &target, QString *status)> resourceOpenHandler;
    std::function<PinloomCommandActionResult(QWidget *parent,
                                             const PinloomEntry &entry,
                                             const PinloomCommandResultAction &action)> unifiedEntryCommandHandler;
    std::function<QList<ClipSearchResult>(const QString &query, const ClipSearchOptions &options)> clipSearchHandler;
    std::function<bool(const QString &clipId, QString *error)> clipInsertionHandler;
    std::function<std::optional<PinloomClipSaveRequest>(
        QWidget *parent,
        const ClipSearchResult &result)> clipSaveRequestProvider;
    std::function<bool(const PinloomClipSaveRequest &request, QString *error)> clipSaveHandler;
    std::function<bool(QString *status)> clipLibraryHandler;
    std::function<bool(QString *status)> anchorCaptureHandler;
    std::function<bool(QString *status)> rectangleAnchorCaptureHandler;
    std::function<bool(QString *status)> textAnchorCaptureHandler;
    std::function<bool(QString *status)> pdfTextClipCaptureHandler;
    std::function<bool(QString *status)> anchorLibraryHandler;
    std::function<bool(QString *status)> libraryRootHandler;
    std::function<QStringList(QString *status)> inboxSelectionProvider;
    std::function<QStringList()> inboxTagProvider;
    std::function<std::optional<InboxFileSaveRequest>(
        QWidget *parent,
        const QString &filePath)> inboxSaveRequestProvider;
    std::function<InboxFileSaveResult(const InboxFileSaveRequest &request)> inboxSaveHandler;
    std::function<bool(QWidget *parent, const QString &text, QString *status)> droppedTextSaveHandler;
    std::function<void(const QString &status)> statusChangedHandler;
};

class PinloomCommandPanel final : public QWidget {
    Q_OBJECT

public:
    explicit PinloomCommandPanel(QWidget *parent = nullptr);
    explicit PinloomCommandPanel(PinloomCommandPanelOptions options, QWidget *parent = nullptr);

    void setCommandText(const QString &text);
    QString commandText() const;
    void openCommandSearch(const QString &query = QString());
    void openClipSearch(const QString &query = QString());
    void setPendingInboxFiles(const QStringList &filePaths);
    QStringList pendingInboxFiles() const;
    void focusCommand();

    QString statusText() const;
    int resultCount() const;
    ClipSearchResult resultAt(int row) const;
    ClipSearchResult currentResult() const;
    PinloomOpenTarget openTargetAt(int row) const;
    PinloomOpenTarget currentOpenTarget() const;
    bool selectResultAt(int row);
    bool selectFirstResult();
    bool selectNextResult();
    bool selectPreviousResult();
    bool activateCurrentCommandItem();
    bool showActionsForCurrentResult();
    bool returnToResultList();
    bool isShowingResultActions() const;
    bool isClipPicker() const;
    PinloomCommandTheme theme() const;
    bool isCompact() const;
    int preferredWindowHeight() const;
    bool triggerRectangleAnchorCapture();
    bool triggerTextAnchorCapture();
    bool triggerPdfTextClipCapture();

signals:
    void statusChanged(const QString &status);
    void clipInserted(const QString &clipId);
    void clipSaved(const QString &clipId);
    void clipLibraryRequested();
    void anchorCaptureRequested();
    void libraryRootRequested();
    void anchorJumped(const QString &resourceId);
    void resourceOpened(const QString &resourceId);
    void inboxSaved(const QString &resourceId);
    void presentationChanged(bool compact, int preferredWindowHeight);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void refreshResults();
    void activateResultItem(Pinloom::Ui::ListItem *item);

private:
    void updateStatus(const QString &status);
    bool activateCommandItem(Pinloom::Ui::ListItem *item);
    bool insertClipFromItem(const Pinloom::Ui::ListItem *item);
    bool activateUnifiedTargetFromItem(const Pinloom::Ui::ListItem *item);
    bool saveClipFromItem(const Pinloom::Ui::ListItem *item);
    bool openClipLibrary();
    bool dispatchCommand(PinloomCommandId id);
    bool saveInboxFromCommand();
    bool activateUnifiedTarget(const PinloomOpenTarget &target);
    QList<PinloomCommandResultAction> actionsForTarget(const PinloomOpenTarget &target) const;
    bool activateResultActionFromItem(const Pinloom::Ui::ListItem *item);
    void populateActionResults(const PinloomOpenTarget &target, int sourceRow);
    bool restoreResultSelection(const PinloomOpenTarget &target, int fallbackRow);
    std::optional<PinloomClipSaveRequest> promptClipSaveRequest(const ClipSearchResult &result);
    std::optional<InboxFileSaveRequest> promptInboxSaveRequest(const QString &filePath);
    bool handleInboxDragEnter(QEvent *event);
    bool handleInboxDrop(QEvent *event);
    void setTheme(PinloomCommandTheme theme);
    void updatePresentation();
    void configureOwnedCommandDispatcher();

    PinloomCommandPanelOptions options_;
    std::unique_ptr<PinloomCommandDispatcher> ownedCommandDispatcher_;
    PinloomCommandDispatcher *commandDispatcher_ = nullptr;
    QLineEdit *commandEdit_ = nullptr;
    QLabel *versionLabel_ = nullptr;
    QToolButton *clipLibraryButton_ = nullptr;
    QWidget *quickActionRow_ = nullptr;
    QToolButton *rectangleAnchorButton_ = nullptr;
    QToolButton *textAnchorButton_ = nullptr;
    QToolButton *pdfTextClipButton_ = nullptr;
    Pinloom::Ui::List *resultList_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QString statusText_;
    QStringList pendingInboxFiles_;
    bool showingResultActions_ = false;
    PinloomOpenTarget actionSourceTarget_;
    int actionSourceRow_ = -1;
    PinloomCommandTheme theme_ = PinloomCommandTheme::Neutral;
    QString appliedThemeKey_;
    bool compact_ = true;
    bool clipPickerMode_ = false;
    int preferredWindowHeight_ = 62;
};

void showCommandPanelForHotkey(QWidget &commandWindow, PinloomCommandPanel &panel);
PinloomEntry enrichedPinloomEntryForAction(const PinloomEntry &entry,
                                           const std::optional<Clip> &clip = std::nullopt,
                                           const std::optional<Resource> &resource = std::nullopt,
                                           const std::optional<ResourceUsage> &usage = std::nullopt);
QList<PinloomCommandResultAction> defaultActionsForPinloomEntry(const PinloomEntry &entry,
                                                                bool removeEnabled = true);

} // namespace Pinloom
