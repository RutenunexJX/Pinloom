#include "pinloom/widgets/ManualPdfAnchorDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

namespace Pinloom {

namespace {

QStringList valuesFromCommaText(const QString &text)
{
    return text.split(QLatin1Char(','), Qt::SkipEmptyParts);
}

void configureCoordinateSpin(QDoubleSpinBox *spin)
{
    spin->setRange(-1000000000.0, 1000000000.0);
    spin->setDecimals(2);
    spin->setSingleStep(1.0);
}

} // namespace

ManualPdfAnchorDialog::ManualPdfAnchorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Create PDF Anchor"));

    auto *form = new QFormLayout(this);

    nameEdit_ = new QLineEdit(this);
    nameEdit_->setObjectName(QStringLiteral("manualPdfAnchorNameEdit"));

    fileEdit_ = new QLineEdit(this);
    fileEdit_->setObjectName(QStringLiteral("manualPdfAnchorFileEdit"));
    auto *browseButton = new QPushButton(tr("Browse"), this);
    browseButton->setObjectName(QStringLiteral("manualPdfAnchorBrowseButton"));
    auto *fileLayout = new QHBoxLayout();
    fileLayout->setContentsMargins(0, 0, 0, 0);
    fileLayout->addWidget(fileEdit_, 1);
    fileLayout->addWidget(browseButton);

    pageSpin_ = new QSpinBox(this);
    pageSpin_->setObjectName(QStringLiteral("manualPdfAnchorPageSpin"));
    pageSpin_->setRange(1, 1000000000);
    pageSpin_->setValue(1);

    leftSpin_ = new QDoubleSpinBox(this);
    leftSpin_->setObjectName(QStringLiteral("manualPdfAnchorLeftSpin"));
    configureCoordinateSpin(leftSpin_);
    topSpin_ = new QDoubleSpinBox(this);
    topSpin_->setObjectName(QStringLiteral("manualPdfAnchorTopSpin"));
    configureCoordinateSpin(topSpin_);
    rightSpin_ = new QDoubleSpinBox(this);
    rightSpin_->setObjectName(QStringLiteral("manualPdfAnchorRightSpin"));
    configureCoordinateSpin(rightSpin_);
    rightSpin_->setValue(100.0);
    bottomSpin_ = new QDoubleSpinBox(this);
    bottomSpin_->setObjectName(QStringLiteral("manualPdfAnchorBottomSpin"));
    configureCoordinateSpin(bottomSpin_);
    bottomSpin_->setValue(100.0);

    zoomSpin_ = new QDoubleSpinBox(this);
    zoomSpin_->setObjectName(QStringLiteral("manualPdfAnchorZoomSpin"));
    zoomSpin_->setRange(0.0, 10000.0);
    zoomSpin_->setDecimals(2);
    zoomSpin_->setSingleStep(25.0);
    zoomSpin_->setValue(100.0);

    aliasesEdit_ = new QLineEdit(this);
    aliasesEdit_->setObjectName(QStringLiteral("manualPdfAnchorAliasesEdit"));
    tagsEdit_ = new QLineEdit(this);
    tagsEdit_->setObjectName(QStringLiteral("manualPdfAnchorTagsEdit"));
    pinnedCheck_ = new QCheckBox(tr("Pinned"), this);
    pinnedCheck_->setObjectName(QStringLiteral("manualPdfAnchorPinnedCheck"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("manualPdfAnchorButtons"));

    form->addRow(tr("Name"), nameEdit_);
    form->addRow(tr("PDF file"), fileLayout);
    form->addRow(tr("Page"), pageSpin_);
    form->addRow(tr("Left"), leftSpin_);
    form->addRow(tr("Top"), topSpin_);
    form->addRow(tr("Right"), rightSpin_);
    form->addRow(tr("Bottom"), bottomSpin_);
    form->addRow(tr("Zoom"), zoomSpin_);
    form->addRow(tr("Aliases"), aliasesEdit_);
    form->addRow(tr("Tags"), tagsEdit_);
    form->addRow(QString(), pinnedCheck_);
    form->addWidget(buttons);

    connect(browseButton, &QPushButton::clicked, this, &ManualPdfAnchorDialog::browsePdfFile);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ManualPdfAnchorCreationRequest ManualPdfAnchorDialog::request() const
{
    ManualPdfAnchorCreationRequest request;
    request.name = nameEdit_->text();
    request.file = fileEdit_->text();
    request.page = pageSpin_->value();
    request.rect.left = leftSpin_->value();
    request.rect.top = topSpin_->value();
    request.rect.right = rightSpin_->value();
    request.rect.bottom = bottomSpin_->value();
    request.zoom = zoomSpin_->value();
    request.aliases = valuesFromCommaText(aliasesEdit_->text());
    request.tags = valuesFromCommaText(tagsEdit_->text());
    request.pinned = pinnedCheck_->isChecked();
    return request;
}

void ManualPdfAnchorDialog::browsePdfFile()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        tr("Select PDF File"),
        QString(),
        tr("PDF files (*.pdf);;All files (*)"));
    if (!file.isEmpty()) {
        fileEdit_->setText(file);
    }
}

} // namespace Pinloom
