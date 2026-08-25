#pragma once

#include "pinloom/core/Anchor.h"
#include "pinloom/core/ApplicationLaunchSettings.h"
#include "pinloom/core/ExcelCommand.h"
#include "pinloom/core/LibraryRepository.h"
#include "pinloom/core/ManualPdfAnchorCreation.h"
#include "pinloom/core/PowerPointCommand.h"
#include "pinloom/core/SumatraPdfCommand.h"
#include "pinloom/core/VisioCommand.h"
#include "pinloom/core/WordCommand.h"
#include "pinloom/clip/ClipSearch.h"
#include "pinloom/widgets/PinloomEntry.h"
#include "pinloom/widgets/PinloomOpenService.h"

#include <QDateTime>
#include <QList>
#include <QStringList>
#include <QVariantMap>
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>

class QLabel;
class QEvent;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace Pinloom {

struct PinloomPanelOptions {
    std::function<bool(const PinloomOpenTarget &target)> openTargetHandler;
    std::function<void(const PinloomOpenTarget &target)> currentOpenTargetChangedHandler;
    std::function<void(int resultCount)> resultCountChangedHandler;
    std::function<void(const QList<PinloomOpenTarget> &results)> resultsChangedHandler;
    std::function<void(const QString &status)> statusChangedHandler;
    std::function<QList<ClipSearchResult>(const QString &query, const ClipSearchOptions &options)> clipSearchHandler;
    std::function<bool(const QString &clipId, QString *error)> clipInsertionHandler;
    std::function<std::optional<ManualPdfAnchorCreationRequest>(QString *status)> foregroundPdfAnchorCaptureRequestProvider;
    std::function<std::optional<ManualPdfAnchorCreationRequest>(
        const ManualPdfAnchorCreationRequest &suggestedRequest)> pdfAnchorCaptureRequestProvider;
    std::function<std::optional<ManualPdfAnchorCreationRequest>(
        QWidget *parent,
        const ManualPdfAnchorCreationRequest &suggestedRequest)> pdfAnchorCaptureDialogHandler;
    std::function<std::optional<ManualPdfAnchorCreationRequest>()> manualPdfAnchorRequestProvider;
    std::function<std::optional<ManualPdfAnchorCreationRequest>(QWidget *parent)> manualPdfAnchorDialogHandler;
    ApplicationLaunchSettings applicationLaunchSettings;
    std::function<bool(const ExcelJumpCommand &command, QString *error)> excelLaunchHandler;
    std::function<QString()> sumatraPdfExecutablePathProvider;
    std::function<bool(const SumatraPdfCommand &command, QString *error)> sumatraPdfLaunchHandler;
    std::function<SumatraPdfDdeFileState(int timeoutMilliseconds)>
        sumatraPdfStateProvider;
    std::function<bool(const SumatraPdfCommand &command,
                       const SumatraPdfDdeFileState &lastState,
                       QString *error)> sumatraPdfRetryHandler;
    std::function<bool(const SumatraPdfPersistentHighlight &highlight)>
        sumatraPdfHighlightHandler;
    int sumatraPdfVerificationTimeoutMilliseconds = 4200;
    int sumatraPdfVerificationPollMilliseconds = 180;
    std::function<bool(const PowerPointJumpCommand &command, QString *error)> powerPointLaunchHandler;
    std::function<bool(const VisioJumpCommand &command, QString *error)> visioLaunchHandler;
    std::function<bool(const WordJumpCommand &command, QString *error)> wordLaunchHandler;
    bool showOpenButton = false;
    bool showManualEditControls = false;
    bool showPinControls = false;
    bool showStatusLine = false;
    bool compactLauncherMode = true;
};

struct PinloomHostContext {
    QString searchText;
    QStringList requiredTags;
    QStringList requiredLocationPrefixes;
    QList<ResourceKind> requiredResourceKinds;
    QStringList contextTags;
    QStringList contextLocationPrefixes;
};

class PinloomPanel : public QWidget {
    Q_OBJECT

public:
    explicit PinloomPanel(ILibraryRepository &repository, QWidget *parent = nullptr);
    PinloomPanel(ILibraryRepository &repository, PinloomPanelOptions options, QWidget *parent = nullptr);

    void setSearchText(const QString &text);
    QString searchText() const;
    void focusSearch();
    void setRequiredTags(const QStringList &tags);
    QStringList requiredTags() const;
    void setRequiredLocationPrefixes(const QStringList &prefixes);
    QStringList requiredLocationPrefixes() const;
    void setRequiredResourceKinds(const QList<ResourceKind> &kinds);
    QList<ResourceKind> requiredResourceKinds() const;
    void setContextTags(const QStringList &tags);
    QStringList contextTags() const;
    void setContextLocationPrefixes(const QStringList &prefixes);
    QStringList contextLocationPrefixes() const;
    void applyHostContext(const PinloomHostContext &context);
    PinloomHostContext hostContext() const;
    PinloomOpenTarget currentOpenTarget() const;
    PinloomOpenTarget openTargetForResourceId(const QString &resourceId) const;
    PinloomOpenTarget resultAt(int row) const;
    QList<PinloomOpenTarget> currentResults() const;
    QList<PinloomEntry> currentEntries() const;
    QList<PinloomEntry> searchEntries(const QString &text, bool includeDeleted = false) const;
    int resultCount() const;
    bool selectResultAt(int row);
    bool selectResultResource(const QString &resourceId);
    bool selectFirstResult();
    bool selectNextResult();
    bool selectPreviousResult();
    bool activateCurrentOpenTarget();
    bool activateResourceById(const QString &resourceId);
    bool activateOpenTarget(const PinloomOpenTarget &target);
    QString statusText() const;
    bool captureCurrentAppPosition();
    bool addAliasToSelectedTarget(const QString &alias);
    bool addAliasToSelectedResource(const QString &alias);
    bool addAliasToResource(const QString &resourceId, const QString &alias);
    bool addTagToSelectedTarget(const QString &tag);
    bool editSelectedAnchor(const QString &name, const QStringList &aliases, const QStringList &tags);
    bool requestDeleteSelectedAnchor();
    bool setSelectedAnchorPinned(bool pinned);
    bool captureForegroundPdfAnchor();
    bool addManualAnchorToSelectedResource(const QString &target, int line = -1);
    bool addManualAnchorToResource(const QString &resourceId, const QString &target, int line = -1);
    bool setSelectedResourcePinned(bool pinned);
    bool setResourcePinnedById(const QString &resourceId, bool pinned);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void refreshResults();
    void openSelectedResource();
    void openResultItem(QListWidgetItem *item);
    void triggerCaptureCurrentAppPosition();
    void addSearchTextAsAlias();
    void addSearchTextAsTag();
    void promptAddAlias();
    void promptEditAnchor();
    void promptDeleteSelectedAnchor();
    void promptAddManualAnchor();
    void toggleSelectedResourcePin();
    void refreshAnchorButtonState();
    void refreshPinButtonState();
    void notifyCurrentOpenTargetChanged();
    void notifyResultCountChanged();
    void notifyResultsChanged();

private:
    void updateStatus(const QString &message);
    QString selectedResultResourceId() const;
    QString selectedLocation() const;
    bool capturePdfAnchorFromSuggestedRequest(
        const std::optional<ManualPdfAnchorCreationRequest> &suggestedPdfRequest,
        const QString &missingContextStatus,
        bool allowManualFallback);
    std::optional<ManualPdfAnchorCreationRequest> selectedPdfAnchorCaptureRequest() const;
    void refreshSearchResults(const QString &searchText, const PinloomOpenTarget &previousTarget);
    bool activateCurrentLauncherItem();
    bool activateLauncherItem(QListWidgetItem *item);
    bool activateExcelTarget(const PinloomOpenTarget &target);
    bool activateSumatraPdfTarget(const PinloomOpenTarget &target);
    bool activatePowerPointTarget(const PinloomOpenTarget &target);
    bool activateVisioTarget(const PinloomOpenTarget &target);
    bool activateWordTarget(const PinloomOpenTarget &target);
    bool tryHostOpenTarget(const PinloomOpenTarget &target);
    void refreshLauncherVisibility();

    ILibraryRepository &repository_;
    PinloomPanelOptions options_;
    std::unique_ptr<PinloomOpenService> openService_;
    QStringList requiredTags_;
    QStringList requiredLocationPrefixes_;
    QList<ResourceKind> requiredResourceKinds_;
    QStringList contextTags_;
    QStringList contextLocationPrefixes_;
    QString statusText_;
    QLineEdit *searchEdit_ = nullptr;
    QListWidget *resultList_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QPushButton *openButton_ = nullptr;
    QPushButton *addAliasButton_ = nullptr;
    QPushButton *addAnchorButton_ = nullptr;
    QPushButton *pinButton_ = nullptr;
};

} // namespace Pinloom
