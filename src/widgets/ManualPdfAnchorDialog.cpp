#include "pinloom/widgets/ManualPdfAnchorDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
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
    : ManualPdfAnchorDialog(ManualPdfAnchorCreationRequest{}, parent)
{
}

ManualPdfAnchorDialog::ManualPdfAnchorDialog(const ManualPdfAnchorCreationRequest &initialRequest, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Capture PDF Anchor"));

    auto *form = new QFormLayout(this);

    nameEdit_ = new QLineEdit(this);
    nameEdit_->setObjectName(QStringLiteral("manualPdfAnchorNameEdit"));

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setObjectName(QStringLiteral("manualPdfAnchorSummaryLabel"));
    summaryLabel_->setWordWrap(true);

    advancedToggleButton_ = new QPushButton(tr("Advanced locator fallback"), this);
    advancedToggleButton_->setObjectName(QStringLiteral("manualPdfAnchorAdvancedToggleButton"));
    advancedToggleButton_->setCheckable(true);

    advancedWidget_ = new QWidget(this);
    advancedWidget_->setObjectName(QStringLiteral("manualPdfAnchorAdvancedWidget"));
    auto *advancedForm = new QFormLayout(advancedWidget_);
    advancedForm->setContentsMargins(0, 0, 0, 0);

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
    form->addRow(tr("Captured locator"), summaryLabel_);
    form->addRow(tr("Aliases"), aliasesEdit_);
    form->addRow(tr("Tags"), tagsEdit_);
    form->addRow(QString(), pinnedCheck_);
    form->addWidget(advancedToggleButton_);
    advancedForm->addRow(tr("PDF file"), fileLayout);
    advancedForm->addRow(tr("Page"), pageSpin_);
    advancedForm->addRow(tr("Left"), leftSpin_);
    advancedForm->addRow(tr("Top"), topSpin_);
    advancedForm->addRow(tr("Right"), rightSpin_);
    advancedForm->addRow(tr("Bottom"), bottomSpin_);
    advancedForm->addRow(tr("Zoom"), zoomSpin_);
    form->addWidget(advancedWidget_);
    form->addWidget(buttons);
    advancedWidget_->setVisible(false);

    connect(browseButton, &QPushButton::clicked, this, &ManualPdfAnchorDialog::browsePdfFile);
    connect(advancedToggleButton_, &QPushButton::toggled, advancedWidget_, &QWidget::setVisible);
    connect(fileEdit_, &QLineEdit::textChanged, this, &ManualPdfAnchorDialog::updateSummary);
    connect(pageSpin_, &QSpinBox::valueChanged, this, &ManualPdfAnchorDialog::updateSummary);
    connect(leftSpin_, &QDoubleSpinBox::valueChanged, this, &ManualPdfAnchorDialog::updateSummary);
    connect(topSpin_, &QDoubleSpinBox::valueChanged, this, &ManualPdfAnchorDialog::updateSummary);
    connect(rightSpin_, &QDoubleSpinBox::valueChanged, this, &ManualPdfAnchorDialog::updateSummary);
    connect(bottomSpin_, &QDoubleSpinBox::valueChanged, this, &ManualPdfAnchorDialog::updateSummary);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setRequest(initialRequest);
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
    request.unit = unit_;
    request.source = source_;
    request.targetApp = targetApp_;
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

void ManualPdfAnchorDialog::setRequest(const ManualPdfAnchorCreationRequest &request)
{
    source_ = request.source.trimmed().isEmpty() ? QStringLiteral("manual") : request.source.trimmed();
    targetApp_ = request.targetApp.trimmed().isEmpty() ? QStringLiteral("PDF-XChange") : request.targetApp.trimmed();
    unit_ = request.unit.trimmed().isEmpty() ? QStringLiteral("pt") : request.unit.trimmed();

    nameEdit_->setText(request.name);
    fileEdit_->setText(request.file);
    if (request.page > 0) {
        pageSpin_->setValue(request.page);
    }
    if (request.rect.isValid()) {
        leftSpin_->setValue(request.rect.left);
        topSpin_->setValue(request.rect.top);
        rightSpin_->setValue(request.rect.right);
        bottomSpin_->setValue(request.rect.bottom);
    }
    if (request.zoom > 0.0) {
        zoomSpin_->setValue(request.zoom);
    }
    aliasesEdit_->setText(request.aliases.join(QStringLiteral(", ")));
    tagsEdit_->setText(request.tags.join(QStringLiteral(", ")));
    pinnedCheck_->setChecked(request.pinned);
    updateSummary();
}

void ManualPdfAnchorDialog::updateSummary()
{
    QString sourceSummary;
    if (source_.compare(QStringLiteral("foreground-pdfxchange-fallback"), Qt::CaseInsensitive) == 0) {
        sourceSummary = tr("foreground PDF-XChange fallback; page defaults to 1, edit if needed");
    } else if (source_.compare(QStringLiteral("foreground-pdfxchange-viewstate"), Qt::CaseInsensitive) == 0) {
        sourceSummary = tr("foreground PDF-XChange page/zoom; rectangle is full-page fallback, edit if needed");
    } else if (source_.compare(QStringLiteral("selected-pdf-fallback"), Qt::CaseInsensitive) == 0) {
        sourceSummary = tr("selected-PDF fallback, not native current-view capture");
    } else {
        sourceSummary = tr("%1 fallback, not native current-view capture").arg(source_);
    }

    ManualPdfAnchorCreationRequest current = request();
    summaryLabel_->setText(tr("%1 | %2").arg(manualPdfAnchorLocatorSummary(current), sourceSummary));
}

} // namespace Pinloom
