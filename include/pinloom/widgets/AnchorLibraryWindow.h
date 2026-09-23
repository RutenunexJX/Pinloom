#pragma once

#include "pinloom/core/AnchorLibraryArchive.h"
#include "pinloom/core/AnchorLibraryManagement.h"
#include "pinloom/core/ResourceUsage.h"
#include "pinloom/widgets/PdfLocatorPreviewRenderer.h"

#include <QColor>
#include <QCache>
#include <QHash>
#include "pinloom/widgets/PinloomUiControls.h"
#include <QPixmap>
#include <functional>
#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPoint;
class QSettings;
#include "pinloom/widgets/PinloomItemViews.h"

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
    std::function<PdfLocatorPreviewRenderOptions()> pdfPreviewOptionsProvider;
    std::function<bool(const QString &title, const QString &message)> confirmationHandler;
    std::function<QString(const AnchorLibraryFile &file)> relinkPathProvider;
};

class AnchorLibraryWindow final : public Ui::MainWindow {
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
    bool clearSelectedAnchorlessFileMetadata();
    bool permanentlyDeleteAllAnchorsForSelectedFiles();
    bool savePendingInlineEdits();
    void showTrash();
    bool isTrashVisible() const;
    bool updateSelectedAnchorTags(const QStringList &tags, bool remove);
    bool updateSelectedFileTags(const QStringList &tags, bool remove);
    bool setSelectedAnchorsPinned(bool pinned);
    bool setSelectedFilesPinned(bool pinned);
    bool relinkSelectedFile(const QString &newLocation = {});
    bool autoRelinkSelectedFiles(const QString &searchRoot = {});
    bool mergeSelectedFileDuplicates();
    bool deduplicateSelectedFileAnchors();
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

    struct InlineFileEdit {
        QString fileKey;
        QStringList aliases;
        QStringList tags;
        bool aliasesDirty = false;
        bool tagsDirty = false;
    };

    struct LocatorPreviewCacheEntry {
        QPixmap image;
        QString status;
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
    void updateViewSummary();
    void setDetailedColumns(bool detailed);
    void handleFileSortRequest(int column);
    void handleAnchorSortRequest(int column);
    void scheduleRepositoryRefresh();
    void showFileContextMenu(const QPoint &position);
    void showAnchorContextMenu(const QPoint &position);
    void showPermanentFileDeleteMenu();
    void openFileTagEditor(int row);
    void openAnchorTagEditor(int row);
    void openTagEditor(Pinloom::Ui::Table *table,
                       int row,
                       int column,
                       const QStringList &selectedTags,
                       QStringList availableTags,
                       bool fileTags,
                       std::function<void(const QStringList &)> updateHandler);
    void handleFileItemChanged(Pinloom::Ui::TableItem *item);
    void handleAnchorItemChanged(Pinloom::Ui::TableItem *item);
    void updatePendingFileTags(const QString &key, const QStringList &tags);
    void updatePendingAnchorTags(const QString &key, const QStringList &tags);
    void applyFileInlineCellState(int row, int column, const QString &key);
    void applyInlineCellState(int row, int column, const QString &key);
    void updateInlineEditStatus();
    void applyLibraryTheme();
    void scheduleSelectedAnchorPreview();
    bool renderSelectedAnchorPreview(bool showExpanded, bool forceRender);
    bool startPdfLocatorPreview(const AnchorLibraryFile &file,
                                const AnchorLibraryAnchor &anchor,
                                const QString &cacheKey,
                                bool showExpanded);
    void applyLocatorPreviewResult(const QString &cacheKey,
                                   const QPixmap &screenshot,
                                   const QString &status,
                                   bool showExpanded);
    QStringList availableFileTags() const;
    QStringList availableAnchorTags() const;
    QColor colorForTag(const QString &tag);
    QList<AnchorReference> allAnchorReferencesForSelectedFiles(bool deletedOnly) const;
    const AnchorLibraryAnchor *anchorForInlineKey(const QString &key) const;
    QList<const AnchorLibraryFile *> sortedVisibleFiles() const;
    QList<AnchorLibraryAnchor> sortedAnchors(const QList<AnchorLibraryAnchor> &anchors) const;

    AnchorLibraryWindowOptions options_;
    QList<AnchorLibraryFile> files_;
    QHash<QString, QStringList> fileResourceIds_;
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
    QAction *restoreAction_ = nullptr;
    QToolButton *tagsButton_ = nullptr;
    QToolButton *fileActionsButton_ = nullptr;
    QToolButton *relinkButton_ = nullptr;
    QToolButton *mergeButton_ = nullptr;
    QToolButton *integrityButton_ = nullptr;
    QToolButton *manageButton_ = nullptr;
    QToolButton *undoButton_ = nullptr;
    QToolButton *saveButton_ = nullptr;
    QToolButton *openButton_ = nullptr;
    QToolButton *previewButton_ = nullptr;
    QPushButton *filterToggle_ = nullptr;
    QLabel *fileCountLabel_ = nullptr;
    QLabel *anchorCountLabel_ = nullptr;
    QLabel *fileEmptyLabel_ = nullptr;
    QLabel *anchorEmptyLabel_ = nullptr;
    QLabel *inspectorTitle_ = nullptr;
    QLabel *inspectorLocation_ = nullptr;
    Pinloom::Ui::Table *fileTable_ = nullptr;
    Pinloom::Ui::Table *anchorTable_ = nullptr;
    AnchorLocatorPreviewWidget *locatorPreview_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QString statusText_;
    QCache<QString, LocatorPreviewCacheEntry> locatorPreviewMemoryCache_{128 * 1024};
    quint64 locatorPreviewRequestGeneration_ = 0;
    QList<SortKey> fileSortKeys_{{6, Qt::DescendingOrder}, {0, Qt::AscendingOrder}};
    QList<SortKey> anchorSortKeys_{{4, Qt::DescendingOrder}, {0, Qt::AscendingOrder}};
    QHash<QString, InlineFileEdit> pendingFileInlineEdits_;
    QHash<QString, InlineAnchorEdit> pendingInlineEdits_;
    QHash<QString, int> fileInlineCellStates_;
    QHash<QString, int> inlineCellStates_;
    QHash<QString, QColor> tagColors_;
    QWidget *tagEditorPopup_ = nullptr;
    bool populatingFileTable_ = false;
    bool populatingAnchorTable_ = false;
    int repositoryListenerId_ = -1;
    quint64 loadedRepositoryRevision_ = 0;
    bool repositoryRefreshPending_ = false;
};

} // namespace Pinloom
