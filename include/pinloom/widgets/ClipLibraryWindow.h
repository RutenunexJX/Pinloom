#pragma once

#include "pinloom/clip/ClipRepository.h"

#include <QColor>
#include <QHash>
#include "pinloom/widgets/PinloomUiControls.h"
#include <functional>
#include <optional>

class QAction;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QKeyEvent;
class QPlainTextEdit;
#include "pinloom/widgets/PinloomItemViews.h"

class QShowEvent;

namespace Pinloom {

enum class ClipLibraryScope {
    Saved,
    History,
    Trash
};

struct ClipLibraryWindowOptions {
    std::function<QList<Clip>()> clipsProvider;
    std::function<bool(const Clip &clip, QString *error)> saveClipHandler;
    std::function<bool(const QString &clipId, QString *error)> deleteClipHandler;
    std::function<bool(const QString &clipId, QString *error)> restoreClipHandler;
    std::function<bool(const QString &clipId, QString *error)> permanentlyDeleteClipHandler;
    std::function<bool(const QString &clipId, QString *error)> openSourceHandler;
};

class ClipLibraryWindow final : public Ui::MainWindow {
    Q_OBJECT

public:
    explicit ClipLibraryWindow(ClipLibraryWindowOptions options = {}, QWidget *parent = nullptr);

    void refresh();
    void setSearchText(const QString &text);
    QString searchText() const;
    void setScope(ClipLibraryScope scope);
    ClipLibraryScope scope() const;
    int visibleClipCount() const;
    bool selectClipAt(int row);
    std::optional<Clip> selectedClip() const;
    QString statusText() const;

    bool editSelectedClip();
    bool savePendingEdits();
    bool deleteSelectedClip(bool requireConfirmation = true);
    bool restoreSelectedClip();
    bool permanentlyDeleteSelectedClip(bool requireConfirmation = true);
    bool openSelectedSource();

protected:
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct PendingClipEdit {
        QStringList aliases;
        QStringList tags;
        bool aliasesDirty = false;
        bool tagsDirty = false;
    };

    void rebuildTagFilter(const QList<Clip> &clips);
    void refreshRows();
    void updatePreview();
    void handleItemChanged(Pinloom::Ui::TableItem *item);
    void openTagEditor(int row);
    void updatePendingTags(const QString &clipId, const QStringList &tags);
    void applyInlineCellState(int row, int column, const QString &clipId);
    void updateInlineEditStatus();
    QStringList availableTags() const;
    QColor colorForTag(const QString &tag) const;
    void showContextMenu(const QPoint &position);
    void setStatus(const QString &status, bool notify = true);
    bool reselectClip(const QString &clipId);

    ClipLibraryWindowOptions options_;
    QList<Clip> clips_;
    QLineEdit *searchEdit_ = nullptr;
    QComboBox *scopeCombo_ = nullptr;
    QComboBox *tagCombo_ = nullptr;
    Pinloom::Ui::Table *table_ = nullptr;
    QLabel *previewTitle_ = nullptr;
    QLabel *previewMetadata_ = nullptr;
    QPlainTextEdit *previewText_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QString statusText_;
    QHash<QString, PendingClipEdit> pendingEdits_;
    QHash<QString, int> inlineCellStates_;
    QFrame *tagEditorPopup_ = nullptr;
    bool populatingTable_ = false;
};

} // namespace Pinloom
