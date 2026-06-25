#pragma once

#include "pinloom/core/Anchor.h"
#include "pinloom/core/LibraryRepository.h"

#include <QList>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <optional>

class QLabel;
class QCheckBox;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace Pinloom {

class IndexingService;

struct PinloomOpenTarget {
    QString resourceId;
    ResourceKind resourceKind = ResourceKind::Unknown;
    QString title;
    QString location;
    QString matchedField;
    QString matchedContextTag;
    QString matchedContextLocationPrefix;
    double score = 0.0;
    std::optional<Anchor> anchor;
};

struct PinloomPanelOptions {
    std::function<bool(const PinloomOpenTarget &target)> openTargetHandler;
    std::function<void(const PinloomOpenTarget &target)> currentOpenTargetChangedHandler;
    std::function<void(int resultCount)> resultCountChangedHandler;
    bool showLibraryRootControls = true;
    bool showManualEditControls = true;
    bool showPinControls = true;
};

struct PinloomIndexingResult {
    bool success = false;
    int indexedCount = 0;
    QString error;
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
    int resultCount() const;
    bool selectFirstResult();
    bool selectNextResult();
    bool selectPreviousResult();
    bool activateCurrentOpenTarget();
    void setRemoteWebFetchingEnabled(bool enabled);
    bool remoteWebFetchingEnabled() const;
    PinloomIndexingResult indexSelectedLibraryRoot();
    PinloomIndexingResult indexAllEnabledLibraryRoots();
    PinloomIndexingResult rebuildAllEnabledLibraryRoots();
    bool addAliasToSelectedResource(const QString &alias);
    bool addManualAnchorToSelectedResource(const QString &target, int line = -1);
    bool setSelectedResourcePinned(bool pinned);

private slots:
    void addLibraryRoot();
    void removeSelectedLibraryRoot();
    void refreshSelectedRoot();
    void refreshAllRoots();
    void rebuildAllRoots();
    void refreshResults();
    void openSelectedResource();
    void openResultItem(QListWidgetItem *item);
    void toggleSelectedLibraryRootPin();
    void promptAddAlias();
    void promptAddManualAnchor();
    void toggleSelectedResourcePin();
    void refreshRelationSummary();
    void refreshPinButtonState();
    void refreshRootPinButtonState();
    void notifyCurrentOpenTargetChanged();
    void notifyResultCountChanged();

private:
    void loadLibraryRoots();
    void selectLibraryRoot(const QString &id);
    void updateStatus(const QString &message);
    QString selectedRootId() const;
    QString selectedResultResourceId() const;
    QString selectedLocation() const;
    void selectResultResource(const QString &resourceId);
    bool tryHostOpenTarget(const PinloomOpenTarget &target);
    void configureIndexingService(IndexingService &indexer) const;

    ILibraryRepository &repository_;
    PinloomPanelOptions options_;
    QStringList requiredTags_;
    QStringList requiredLocationPrefixes_;
    QList<ResourceKind> requiredResourceKinds_;
    QStringList contextTags_;
    QStringList contextLocationPrefixes_;
    QWidget *rootControlsWidget_ = nullptr;
    QListWidget *rootList_ = nullptr;
    QLineEdit *searchEdit_ = nullptr;
    QListWidget *resultList_ = nullptr;
    QLabel *relationLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QCheckBox *fetchRemoteWebPagesCheck_ = nullptr;
    QPushButton *removeRootButton_ = nullptr;
    QPushButton *refreshSelectedButton_ = nullptr;
    QPushButton *refreshAllButton_ = nullptr;
    QPushButton *rebuildAllButton_ = nullptr;
    QPushButton *pinRootButton_ = nullptr;
    QPushButton *openButton_ = nullptr;
    QPushButton *addAliasButton_ = nullptr;
    QPushButton *addAnchorButton_ = nullptr;
    QPushButton *pinButton_ = nullptr;
};

} // namespace Pinloom
