#pragma once

#include "pinloom/core/InMemoryLibraryRepository.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;

namespace Pinloom {

class PinloomPanel : public QWidget {
    Q_OBJECT

public:
    explicit PinloomPanel(QWidget *parent = nullptr);

private slots:
    void refreshResults();

private:
    void seedDemoData();

    InMemoryLibraryRepository repository_;
    QLineEdit *searchEdit_ = nullptr;
    QListWidget *resultList_ = nullptr;
    QLabel *statusLabel_ = nullptr;
};

} // namespace Pinloom
