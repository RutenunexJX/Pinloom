#include "pinloom/widgets/TextPreviewDialog.h"
#include "pinloom/widgets/PinloomVisualTheme.h"

#include <QFile>
#include <QFileInfo>
#include <QColor>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextFormat>
#include <QVBoxLayout>
#include <utility>

namespace Pinloom {

TextPreviewDialog::TextPreviewDialog(QString filePath, int targetLine, QWidget *parent)
    : QDialog(parent)
    , filePath_(std::move(filePath))
    , targetLine_(targetLine)
{
    setObjectName(QStringLiteral("textPreviewDialog"));
    setProperty("pinloomRole", QStringLiteral("canvas"));
    setWindowTitle(QFileInfo(filePath_).fileName());
    resize(900, 640);

    auto *layout = new QVBoxLayout(this);
    auto *label = new QLabel(QStringLiteral("%1 : line %2").arg(filePath_).arg(targetLine_), this);
    label->setProperty("pinloomTextRole", QStringLiteral("technical"));
    editor_ = new QPlainTextEdit(this);
    editor_->setProperty("pinloomTextRole", QStringLiteral("technical"));
    editor_->setAccessibleName(tr("Text file preview"));
    editor_->setReadOnly(true);
    editor_->setLineWrapMode(QPlainTextEdit::NoWrap);

    layout->addWidget(label);
    layout->addWidget(editor_, 1);
}

bool TextPreviewDialog::load()
{
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    editor_->setPlainText(QString::fromUtf8(file.readAll()));
    highlightTargetLine();
    return true;
}

void TextPreviewDialog::highlightTargetLine()
{
    if (targetLine_ <= 0) {
        return;
    }

    const QTextBlock block = editor_->document()->findBlockByNumber(targetLine_ - 1);
    if (!block.isValid()) {
        return;
    }

    QTextCursor cursor(block);
    editor_->setTextCursor(cursor);
    editor_->centerCursor();

    QTextEdit::ExtraSelection selection;
    selection.cursor = cursor;
    QColor highlight = pinloomVisualTokens(activePinloomVisualScheme()).warning;
    highlight.setAlpha(48);
    selection.format.setBackground(highlight);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    editor_->setExtraSelections({selection});
}

} // namespace Pinloom
