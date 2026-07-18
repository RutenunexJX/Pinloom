#pragma once

#include "pinloom/core/InboxFileCapture.h"
#include "pinloom/clip/ClipRepository.h"
#include "pinloom/clip/ClipSearch.h"
#include "pinloom/widgets/PinloomPanel.h"

#include <QPixmap>
#include <QWidget>
#include <functional>
#include <optional>

class QLabel;
class QEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPaintEvent;

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

struct PinloomCommandActionResult {
    bool success = false;
    QString message;
    QString diagnostics;
    QString nextUiHint;
};

struct PinloomCommandPanelOptions {
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
    std::function<bool(QString *status)> anchorCaptureHandler;
    std::function<bool(QString *status)> anchorLibraryHandler;
    std::function<QStringList(QString *status)> inboxSelectionProvider;
    std::function<std::optional<InboxFileSaveRequest>(
        QWidget *parent,
        const QString &filePath)> inboxSaveRequestProvider;
    std::function<bool(const InboxFileSaveRequest &request, QString *status)> inboxSaveHandler;
    std::function<void(const QString &status)> statusChangedHandler;
};

class PinloomCommandPanel final : public QWidget {
    Q_OBJECT

public:
    explicit PinloomCommandPanel(QWidget *parent = nullptr);
    explicit PinloomCommandPanel(PinloomCommandPanelOptions options, QWidget *parent = nullptr);

    void setCommandText(const QString &text);
    QString commandText() const;
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
    PinloomCommandTheme theme() const;
    bool isCompact() const;
    int preferredWindowHeight() const;

signals:
    void statusChanged(const QString &status);
    void clipInserted(const QString &clipId);
    void clipSaved(const QString &clipId);
    void anchorCaptureRequested();
    void anchorJumped(const QString &resourceId);
    void resourceOpened(const QString &resourceId);
    void inboxSaved(const QString &resourceId);
    void presentationChanged(bool compact, int preferredWindowHeight);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void refreshResults();
    void activateResultItem(QListWidgetItem *item);

private:
    void updateStatus(const QString &status);
    bool activateCommandItem(QListWidgetItem *item);
    bool insertClipFromItem(const QListWidgetItem *item);
    bool activateUnifiedTargetFromItem(const QListWidgetItem *item);
    bool saveClipFromItem(const QListWidgetItem *item);
    bool captureAnchor();
    bool openAnchorLibrary();
    bool saveInboxFromCommand();
    bool activateUnifiedTarget(const PinloomOpenTarget &target);
    QList<PinloomCommandResultAction> actionsForTarget(const PinloomOpenTarget &target) const;
    bool activateResultActionFromItem(const QListWidgetItem *item);
    void populateActionResults(const PinloomOpenTarget &target, int sourceRow);
    bool restoreResultSelection(const PinloomOpenTarget &target, int fallbackRow);
    std::optional<PinloomClipSaveRequest> promptClipSaveRequest(const ClipSearchResult &result);
    std::optional<InboxFileSaveRequest> promptInboxSaveRequest(const QString &filePath);
    bool handleInboxDragEnter(QEvent *event);
    bool handleInboxDrop(QEvent *event);
    void setTheme(PinloomCommandTheme theme);
    void updatePresentation();

    PinloomCommandPanelOptions options_;
    QLineEdit *commandEdit_ = nullptr;
    QListWidget *resultList_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QString statusText_;
    QStringList pendingInboxFiles_;
    bool showingResultActions_ = false;
    PinloomOpenTarget actionSourceTarget_;
    int actionSourceRow_ = -1;
    PinloomCommandTheme theme_ = PinloomCommandTheme::Neutral;
    QPixmap backgroundPixmap_;
    bool compact_ = true;
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
