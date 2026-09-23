#pragma once

#include "pinloom/core/LibraryRepository.h"

#include "pinloom/widgets/PinloomUiControls.h"
#include <functional>

class QFileSystemModel;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QSortFilterProxyModel;
#include "pinloom/widgets/PinloomItemViews.h"
class QTreeView;

namespace Pinloom {

struct LibraryRootWindowOptions {
    ILibraryRepository *repository = nullptr;
    std::function<QStringList()> fileTagsProvider;
    QSettings *settings = nullptr;
};

class LibraryRootWindow final : public Ui::MainWindow {
    Q_OBJECT

public:
    explicit LibraryRootWindow(LibraryRootWindowOptions options, QWidget *parent = nullptr);

    void refresh();
    int rootCount() const;
    QString selectedPath() const;
    QString statusText() const;
    bool selectRootAt(int row);
    bool selectPath(const QString &path);
    bool saveSelectedMetadata();
    bool removeSelectedRoot(bool requireConfirmation = true);

protected:
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void addRoot();
    void openSelectedPath();
    void refreshRootTable(const QString &preferredRootId = {});
    void activateSelectedRoot();
    void updateSelectedFilesystemPath();
    void loadMetadata(const QString &path);
    void setStatus(const QString &status, bool notify = true);
    QString selectedRootId() const;
    std::optional<LibraryRoot> selectedRoot() const;
    QStringList commaSeparatedValues(const QString &text, bool tags = false) const;

    LibraryRootWindowOptions options_;
    Pinloom::Ui::Table *rootTable_ = nullptr;
    QLineEdit *searchEdit_ = nullptr;
    QTreeView *tree_ = nullptr;
    QFileSystemModel *fileModel_ = nullptr;
    QSortFilterProxyModel *filterModel_ = nullptr;
    QLabel *pathLabel_ = nullptr;
    QLineEdit *nameEdit_ = nullptr;
    QLineEdit *aliasesEdit_ = nullptr;
    QLineEdit *tagsEdit_ = nullptr;
    QPushButton *saveButton_ = nullptr;
    QPushButton *removeRootButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QString selectedPath_;
    QString statusText_;
};

} // namespace Pinloom
