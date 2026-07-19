#pragma once

#include "pinloom/core/AnchorLibraryArchive.h"
#include "pinloom/core/AnchorLibraryManagement.h"
#include "pinloom/core/ResourceUsage.h"

#include <QColor>
#include <QHash>
#include <QMainWindow>
#include <QPixmap>
#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPoint;
class QPushButton;
class QSettings;
class QTableWidget;
class QTableWidgetItem;
class QToolButton;
class QWidget;

namespace Pinloom {

class AnchorLocatorPreviewWidget;

struct AnchorLibraryAnchor {
    QString resourceId;
    Anchor anchor;
    bool resourceDeleted = false;
    AnchorUsage usage;
};

struct AnchorLibraryFile {
    Resource resource;
    ResourceUsage usage;
    QList<AnchorLibraryAnchor> anchors;
};

struct AnchorLibraryWindowOptions {
    AnchorLibraryManagementService *managementService = nullptr;
    AnchorLibraryArchiveService *archiveService = nullptr;
    ILibraryRepository *repository = nullptr;
    QSettings *settings = nullptr;
    QString automaticBackupDirectory;
    std::function<QList<AnchorLibraryFile>()> filesProvider;
    std::function<bool(const AnchorLibraryFile &file,
                       const AnchorLibraryAnchor &anchor,
                       QString *status)> anchorJumpHandler;
    std::function<std::optional<AnchorLocatorUpdate>(const AnchorLibraryFile &file,
                                                     const AnchorLibraryAnchor &anchor,
                                                     QString *status)> locatorRecaptureHandler;
    std::function<QPixmap(const AnchorLibraryFile &file,
                          const AnchorLibraryAnchor &anchor,
                          QString *status)> locatorPreviewHandler;
    std::function<bool(const QString &title, const QString &message)> confirmationHandler;
    std::function<QString(const AnchorLibraryFile &file)> relinkPathProvider;
};

class AnchorLibraryWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit AnchorLibraryWindow(AnchorLibraryWindowOptions options, QWidget *parent = nullptr);
    ~AnchorLibraryWindow() override;

    void refreshLibrary();
    void setFilterText(const QString &text);
    QString filterText() const;
    int visibleFileCount() const;
    int visibleAnchorCount() const;
    QString statusText() const;
    bool selectFileAt(int row);
    bool selectAnchorAt(int row);
    int selectedFileCount() const;
    int selectedAnchorCount() const;
    bool activateSelectedAnchor();
    bool deleteSelectedAnchor();
    bool deleteSelectedAnchors();
    bool restoreSelectedAnchors();
    bool deleteAllAnchorsForSelectedFiles();
    bool restoreAllAnchorsForSelectedFiles();
    bool archiveSelectedFiles();
    bool restoreSelectedFiles();
    bool permanentlyDeleteSelection();
    bool permanentlyDeleteSelectedAnchors();
    bool permanentlyClearSelectedFileMetadata();
    bool permanentlyDeleteAllAnchorsForSelectedFiles();
    bool savePendingInlineEdits();
    void showTrash();
    bool isTrashVisible() const;
    bool saveSelectedAnchorMetadata();
    bool saveSelectedAnchorLocator();
    bool saveSelectedFileMetadata();
    bool updateSelectedAnchorTags(const QStringList &tags, bool remove);
    bool updateSelectedFileTags(const QStringList &tags, bool remove);
    bool setSelectedAnchorsPinned(bool pinned);
    bool setSelectedFilesPinned(bool pinned);
    bool relinkSelectedFile(const QString &newLocation = {});
    bool autoRelinkSelectedFiles(const QString &searchRoot = {});
    bool mergeSelectedFileDuplicates();
    bool deduplicateSelectedFileAnchors();
    bool validateSelectedAnchor();
    bool previewSelectedAnchor();
    bool recaptureSelectedAnchor();
    bool inspectIntegrity();
    bool renameTag(const QString &oldTag, const QString &newTag);
    bool deleteTag(const QString &tag);
    bool showOperationHistory();
    bool undoLastOperation();
    bool saveCurrentView(const QString &name);
    bool loadSavedView(const QString &name);
    bool deleteSavedView(const QString &name);

signals:
    void anchorActivated(const QString &resourceId, const QString &anchorId);
    void anchorDeleted(const QString &resourceId, const QString &anchorId);
    void anchorRestored(const QString &resourceId, const QString &anchorId);

private slots:
    void applyFilter();
    void populateSelectedFileAnchors();
    void populateInspector();

private:
    struct SortKey {
        int column = 0;
        Qt::SortOrder order = Qt::AscendingOrder;
    };

    struct InlineAnchorEdit {
        QString resourceId;
        QString anchorIdentity;
        QStringList aliases;
        QStringList tags;
        bool aliasesDirty = false;
        bool tagsDirty = false;
    };

    const AnchorLibraryFile *fileForKey(const QString &fileKey) const;
    const AnchorLibraryFile *selectedFile() const;
    QList<const AnchorLibraryFile *> selectedFiles() const;
    std::optional<AnchorLibraryAnchor> selectedAnchor() const;
    QList<AnchorLibraryAnchor> selectedAnchors() const;
    QList<AnchorLibraryAnchor> scopedAnchors(const AnchorLibraryFile &file) const;
    QStringList resourceIdsForFile(const AnchorLibraryFile &file) const;
    QStringList selectedResourceIds() const;
    QList<AnchorReference> selectedAnchorReferences() const;
    bool showingTrash() const;
    bool fileMatchesFilter(const AnchorLibraryFile &file) const;
    bool fileHasInvalidLocator(const AnchorLibraryFile &file) const;
    bool confirmOperation(const QString &title, const QString &message);
    bool createSafetyBackup(const QString &operation);
    void promptAnchorTagUpdate(bool remove);
    void promptFileTagUpdate(bool remove);
    void promptSavedViewCreation();
    void promptTagManager();
    void setOperationResult(const AnchorLibraryOperationResult &result,
                            bool refreshOnSuccess = true);
    void refreshFilterChoices();
    void refreshSavedViews();
    void updateActionButtons();
    void updateStatus();
    void handleFileSortRequest(int column);
    void handleAnchorSortRequest(int column);
    void scheduleRepositoryRefresh();
    void showFileContextMenu(const QPoint &position);
    void showAnchorContextMenu(const QPoint &position);
    void showPermanentFileDeleteMenu();
    void openAnchorTagEditor(int row);
    void handleAnchorItemChanged(QTableWidgetItem *item);
    void updatePendingAnchorTags(const QString &key, const QStringList &tags);
    void applyInlineCellState(int row, int column, const QString &key);
    void applyTrashTheme();
    QStringList availableAnchorTags() const;
    QColor colorForTag(const QString &tag);
    QList<AnchorReference> allAnchorReferencesForSelectedFiles(bool deletedOnly) const;
    const AnchorLibraryAnchor *anchorForInlineKey(const QString &key) const;
    QList<const AnchorLibraryFile *> sortedVisibleFiles() const;
    QList<AnchorLibraryAnchor> sortedAnchors(const QList<AnchorLibraryAnchor> &anchors) const;

    AnchorLibraryWindowOptions options_;
    QList<AnchorLibraryFile> files_;
    QLineEdit *filterEdit_ = nullptr;
    QComboBox *savedViewCombo_ = nullptr;
    QComboBox *scopeCombo_ = nullptr;
    QComboBox *tagFilterCombo_ = nullptr;
    QComboBox *anchorTagFilterCombo_ = nullptr;
    QComboBox *kindFilterCombo_ = nullptr;
    QComboBox *appFilterCombo_ = nullptr;
    QComboBox *timeFilterCombo_ = nullptr;
    QComboBox *usageFilterCombo_ = nullptr;
    QLineEdit *directoryFilterEdit_ = nullptr;
    QToolButton *refreshButton_ = nullptr;
    QToolButton *trashButton_ = nullptr;
    QToolButton *restoreButton_ = nullptr;
    QToolButton *tagsButton_ = nullptr;
    QToolButton *fileActionsButton_ = nullptr;
    QToolButton *relinkButton_ = nullptr;
    QToolButton *mergeButton_ = nullptr;
    QToolButton *integrityButton_ = nullptr;
    QToolButton *manageButton_ = nullptr;
    QToolButton *undoButton_ = nullptr;
    QTableWidget *fileTable_ = nullptr;
    QTableWidget *anchorTable_ = nullptr;
    QLabel *selectionLabel_ = nullptr;
    QLineEdit *fileTitleEdit_ = nullptr;
    QLineEdit *fileAliasesEdit_ = nullptr;
    QLineEdit *fileTagsEdit_ = nullptr;
    QLineEdit *fileLocationEdit_ = nullptr;
    QCheckBox *filePinnedCheck_ = nullptr;
    QPushButton *saveFileButton_ = nullptr;
    QLineEdit *anchorNameEdit_ = nullptr;
    QLineEdit *anchorAliasesEdit_ = nullptr;
    QLineEdit *anchorTagsEdit_ = nullptr;
    QCheckBox *anchorPinnedCheck_ = nullptr;
    QPushButton *saveAnchorButton_ = nullptr;
    QLineEdit *targetAppEdit_ = nullptr;
    QLineEdit *targetFileEdit_ = nullptr;
    QLineEdit *locatorTypeEdit_ = nullptr;
    QPlainTextEdit *locatorJsonEdit_ = nullptr;
    QPushButton *saveLocatorButton_ = nullptr;
    QPushButton *validateLocatorButton_ = nullptr;
    QPushButton *previewLocatorButton_ = nullptr;
    QPushButton *recaptureLocatorButton_ = nullptr;
    AnchorLocatorPreviewWidget *locatorPreview_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QString statusText_;
    QList<SortKey> fileSortKeys_{{6, Qt::DescendingOrder}, {0, Qt::AscendingOrder}};
    QList<SortKey> anchorSortKeys_{{4, Qt::DescendingOrder}, {0, Qt::AscendingOrder}};
    QHash<QString, InlineAnchorEdit> pendingInlineEdits_;
    QHash<QString, int> inlineCellStates_;
    QHash<QString, QColor> tagColors_;
    QWidget *tagEditorPopup_ = nullptr;
    bool populatingAnchorTable_ = false;
    int repositoryListenerId_ = -1;
    quint64 loadedRepositoryRevision_ = 0;
    bool repositoryRefreshPending_ = false;
};

} // namespace Pinloom
