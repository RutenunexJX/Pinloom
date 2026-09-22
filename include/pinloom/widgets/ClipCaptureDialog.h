#pragma once

#include "pinloom/widgets/PinloomUiControls.h"

#include <QString>
#include <QStringList>

class QFrame;
class QLineEdit;
class QPlainTextEdit;
class QToolButton;
class QWidget;

namespace Pinloom {

struct ClipCaptureMetadata {
    QString name;
    QStringList tags;
};

class ClipCaptureDialog final : public Ui::Dialog {
    Q_OBJECT

public:
    explicit ClipCaptureDialog(const QString &selectedText,
                               const QString &suggestedName,
                               const QStringList &availableTags = {},
                               QWidget *parent = nullptr);

    ClipCaptureMetadata metadata() const;
    void setName(const QString &name);
    void setTags(const QStringList &tags);

private:
    void openTagPicker();
    void updateTagButton();

    QLineEdit *nameEdit_ = nullptr;
    QToolButton *tagsButton_ = nullptr;
    QPlainTextEdit *previewEdit_ = nullptr;
    Pinloom::Ui::DialogButtonBox *buttons_ = nullptr;
    QFrame *tagPickerPopup_ = nullptr;
    QStringList availableTags_;
    QStringList selectedTags_;
};

} // namespace Pinloom
