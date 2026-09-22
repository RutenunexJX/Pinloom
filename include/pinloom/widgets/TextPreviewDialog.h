#pragma once

#include "pinloom/widgets/PinloomUiControls.h"
#include <QString>

class QPlainTextEdit;

namespace Pinloom {

class TextPreviewDialog final : public Ui::Dialog {
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
