#pragma once

#include "pinloom/core/LibraryRepository.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace Pinloom {

class PinloomPanel : public QWidget {
    Q_OBJECT

public:
    explicit PinloomPanel(ILibraryRepository &repository, QWidget *parent = nullptr);

private slots:
    void addLibraryRoot();
    void removeSelectedLibraryRoot();
    void refreshSelectedRoot();
    void refreshAllRoots();
    void rebuildAllRoots();
    void refreshResults();
    void openSelectedResource();
    void openResultItem(QListWidgetItem *item);

private:
    void loadLibraryRoots();
    void selectLibraryRoot(const QString &id);
    void updateStatus(const QString &message);
    QString selectedRootId() const;
    QString selectedLocation() const;

    ILibraryRepository &repository_;
    QListWidget *rootList_ = nullptr;
    QLineEdit *searchEdit_ = nullptr;
    QListWidget *resultList_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QPushButton *removeRootButton_ = nullptr;
    QPushButton *refreshSelectedButton_ = nullptr;
    QPushButton *refreshAllButton_ = nullptr;
    QPushButton *rebuildAllButton_ = nullptr;
    QPushButton *openButton_ = nullptr;
};

} // namespace Pinloom
