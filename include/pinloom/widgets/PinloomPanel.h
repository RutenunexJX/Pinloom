#pragma once

#include "pinloom/core/LibraryRepository.h"

#include <QStringList>
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
    void refreshIndex();
    void refreshResults();
    void openSelectedResource();
    void openResultItem(QListWidgetItem *item);

private:
    void updateStatus(const QString &message);
    QString selectedLocation() const;

    ILibraryRepository &repository_;
    QStringList libraryRoots_;
    QLineEdit *searchEdit_ = nullptr;
    QListWidget *resultList_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
    QPushButton *openButton_ = nullptr;
};

} // namespace Pinloom
