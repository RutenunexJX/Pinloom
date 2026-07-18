#pragma once

#include "pinloom/core/Resource.h"

#include <QMainWindow>
#include <functional>
#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QToolButton;

namespace Pinloom {

struct AnchorLibraryAnchor {
    QString resourceId;
    Anchor anchor;
};

struct AnchorLibraryFile {
    Resource resource;
    QList<AnchorLibraryAnchor> anchors;
};

struct AnchorLibraryWindowOptions {
    std::function<QList<AnchorLibraryFile>()> filesProvider;
    std::function<bool(const AnchorLibraryFile &file,
                       const AnchorLibraryAnchor &anchor,
                       QString *status)> anchorJumpHandler;
    std::function<bool(const AnchorLibraryFile &file,
                       const AnchorLibraryAnchor &anchor)> anchorDeleteConfirmationHandler;
    std::function<bool(const AnchorLibraryFile &file,
                       const AnchorLibraryAnchor &anchor,
                       QString *status)> anchorDeleteHandler;
};

class AnchorLibraryWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit AnchorLibraryWindow(AnchorLibraryWindowOptions options, QWidget *parent = nullptr);

    void refreshLibrary();
    void setFilterText(const QString &text);
    QString filterText() const;
    int visibleFileCount() const;
    int visibleAnchorCount() const;
    QString statusText() const;
    bool selectFileAt(int row);
    bool selectAnchorAt(int row);
    bool activateSelectedAnchor();
    bool deleteSelectedAnchor();

signals:
    void anchorActivated(const QString &resourceId, const QString &anchorId);
    void anchorDeleted(const QString &resourceId, const QString &anchorId);

private slots:
    void applyFilter();
    void populateSelectedFileAnchors();

private:
    const AnchorLibraryFile *fileForResourceId(const QString &resourceId) const;
    const AnchorLibraryFile *selectedFile() const;
    std::optional<AnchorLibraryAnchor> selectedAnchor() const;
    bool fileMatchesFilter(const AnchorLibraryFile &file) const;
    void updateActionButtons();
    void updateStatus();

    AnchorLibraryWindowOptions options_;
    QList<AnchorLibraryFile> files_;
    QLineEdit *filterEdit_ = nullptr;
    QComboBox *scopeCombo_ = nullptr;
    QToolButton *refreshButton_ = nullptr;
    QToolButton *jumpButton_ = nullptr;
    QToolButton *deleteButton_ = nullptr;
    QTableWidget *fileTable_ = nullptr;
    QTableWidget *anchorTable_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QString statusText_;
};

} // namespace Pinloom
