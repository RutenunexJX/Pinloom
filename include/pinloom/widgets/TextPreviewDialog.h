#pragma once

#include <QDialog>
#include <QString>

class QPlainTextEdit;

namespace Pinloom {

class TextPreviewDialog final : public QDialog {
public:
    explicit TextPreviewDialog(QString filePath, int targetLine, QWidget *parent = nullptr);

    bool load();

private:
    void highlightTargetLine();

    QString filePath_;
    int targetLine_ = -1;
    QPlainTextEdit *editor_ = nullptr;
};

} // namespace Pinloom
