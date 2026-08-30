#include "pinloom/widgets/AnchorCaptureDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace Pinloom {

namespace {

QStringList commaValues(const QString &text)
{
    return text.split(QLatin1Char(','), Qt::SkipEmptyParts);
}

QString compactLocatorSummary(const AnchorCaptureDraft &draft)
{
    const QJsonDocument document = QJsonDocument::fromJson(
        draft.locatorJson.toUtf8());
    const QJsonObject locator = document.object();
    QStringList parts{draft.locatorType};
    const int page = locator.value(QStringLiteral("page")).toInt(-1);
    if (page > 0) {
        parts.append(QStringLiteral("page %1").arg(page));
    }
    const QString bookmark = locator.value(QStringLiteral("bookmark")).toString();
    if (!bookmark.isEmpty()) {
        parts.append(QStringLiteral("bookmark %1").arg(bookmark));
    }
    const QString sheet = locator.value(QStringLiteral("sheet")).toString();
    const QString range = locator.value(QStringLiteral("range")).toString();
    if (!sheet.isEmpty() || !range.isEmpty()) {
        parts.append(QStringLiteral("%1!%2").arg(sheet, range));
    }
    const QString pageName = locator.value(QStringLiteral("pageNameU")).toString();
    const QString shape = locator.value(QStringLiteral("shapeUniqueId")).toString();
    if (!pageName.isEmpty() || !shape.isEmpty()) {
        parts.append(QStringLiteral("%1 / %2").arg(pageName, shape));
    }
    const QString text = locator.value(QStringLiteral("text")).toString().simplified();
    if (!text.isEmpty()) {
        parts.append(QStringLiteral("\"%1\"").arg(text.left(80)));
    }
    return parts.join(QStringLiteral("  ·  "));
}

} // namespace

AnchorCaptureDialog::AnchorCaptureDialog(const AnchorCaptureDraft &draft,
                                         QWidget *parent)
    : QDialog(parent)
    , initialDraft_(draft)
{
    setObjectName(QStringLiteral("anchorCaptureDialog"));
    setProperty("pinloomRole", QStringLiteral("canvas"));
    setWindowTitle(tr("Confirm Anchor"));
    setModal(true);
    resize(560, 360);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    auto *title = new QLabel(tr("Confirm captured position"), this);
    title->setObjectName(QStringLiteral("anchorCaptureTitle"));
    title->setProperty("pinloomTextRole", QStringLiteral("title"));
    root->addWidget(title);

    targetLabel_ = new QLabel(this);
    targetLabel_->setObjectName(QStringLiteral("anchorCaptureTarget"));
    targetLabel_->setProperty("pinloomRole", QStringLiteral("raised"));
    targetLabel_->setProperty("pinloomTextRole", QStringLiteral("technical"));
    targetLabel_->setWordWrap(true);
    const QString target = draft.targetFile.trimmed().isEmpty()
        ? draft.targetUri.trimmed()
        : draft.targetFile.trimmed();
    targetLabel_->setText(tr("%1  ·  %2")
                              .arg(draft.targetApp.trimmed(), target));
    root->addWidget(targetLabel_);

    locatorLabel_ = new QLabel(compactLocatorSummary(draft), this);
    locatorLabel_->setObjectName(QStringLiteral("anchorCaptureLocator"));
    locatorLabel_->setProperty("pinloomRole", QStringLiteral("raised"));
    locatorLabel_->setProperty("pinloomTextRole", QStringLiteral("technical"));
    locatorLabel_->setWordWrap(true);
    root->addWidget(locatorLabel_);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    nameEdit_ = new QLineEdit(draft.suggestedName, this);
    nameEdit_->setObjectName(QStringLiteral("anchorCaptureNameEdit"));
    aliasesEdit_ = new QLineEdit(draft.aliases.join(QLatin1Char(',')), this);
    aliasesEdit_->setObjectName(QStringLiteral("anchorCaptureAliasesEdit"));
    tagsEdit_ = new QLineEdit(draft.tags.join(QStringLiteral(", ")), this);
    tagsEdit_->setObjectName(QStringLiteral("anchorCaptureTagsEdit"));
    pinnedCheck_ = new QCheckBox(tr("Pinned"), this);
    pinnedCheck_->setObjectName(QStringLiteral("anchorCapturePinnedCheck"));
    pinnedCheck_->setChecked(draft.pinned);
    form->addRow(tr("Name"), nameEdit_);
    form->addRow(tr("Aliases"), aliasesEdit_);
    form->addRow(tr("Tags"), tagsEdit_);
    form->addRow(QString(), pinnedCheck_);
    root->addLayout(form);

    mutationCheck_ = new QCheckBox(this);
    mutationCheck_->setObjectName(QStringLiteral("anchorCaptureMutationCheck"));
    mutationCheck_->setText(draft.mutationLabel.trimmed().isEmpty()
                                ? tr("Allow Pinloom to create a stable locator in the document")
                                : draft.mutationLabel.trimmed());
    mutationCheck_->setVisible(draft.mutationRequired || draft.mutationOptional);
    root->addWidget(mutationCheck_);

    provenanceLabel_ = new QLabel(
        draft.provenance.trimmed().isEmpty()
            ? tr("Captured from the remembered foreground application")
            : tr("Source: %1").arg(draft.provenance.trimmed()),
        this);
    provenanceLabel_->setObjectName(QStringLiteral("anchorCaptureProvenance"));
    provenanceLabel_->setProperty("pinloomTextRole", QStringLiteral("metadata"));
    provenanceLabel_->setWordWrap(true);
    root->addWidget(provenanceLabel_);

    validationLabel_ = new QLabel(this);
    validationLabel_->setObjectName(QStringLiteral("anchorCaptureValidation"));
    validationLabel_->setProperty("pinloomNotice", QStringLiteral("error"));
    validationLabel_->setAccessibleName(tr("Anchor validation status"));
    validationLabel_->setWordWrap(true);
    root->addWidget(validationLabel_);

    buttons_ = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons_->setObjectName(QStringLiteral("anchorCaptureButtons"));
    if (QPushButton *saveButton = buttons_->button(QDialogButtonBox::Save)) {
        saveButton->setProperty("pinloomControl", QStringLiteral("primary"));
    }
    root->addWidget(buttons_);

    connect(nameEdit_, &QLineEdit::textChanged,
            this, &AnchorCaptureDialog::updateAcceptance);
    connect(mutationCheck_, &QCheckBox::toggled,
            this, &AnchorCaptureDialog::updateAcceptance);
    connect(buttons_, &QDialogButtonBox::accepted,
            this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    updateAcceptance();
    nameEdit_->setFocus(Qt::OtherFocusReason);
    nameEdit_->selectAll();
}

AnchorCaptureDraft AnchorCaptureDialog::draft() const
{
    AnchorCaptureDraft result = initialDraft_;
    result.suggestedName = nameEdit_->text();
    result.aliases = commaValues(aliasesEdit_->text());
    result.tags = commaValues(tagsEdit_->text());
    result.pinned = pinnedCheck_->isChecked();
    result.mutationAuthorized = !mutationCheck_->isHidden()
        && mutationCheck_->isChecked();
    return result;
}

void AnchorCaptureDialog::updateAcceptance()
{
    QString failure;
    if (nameEdit_->text().trimmed().isEmpty()) {
        failure = tr("Name is required.");
    } else if (initialDraft_.mutationRequired
               && !mutationCheck_->isChecked()) {
        failure = tr("Authorize the required document change or cancel this capture.");
    }
    validationLabel_->setText(failure);
    if (QPushButton *save = buttons_->button(QDialogButtonBox::Save)) {
        save->setEnabled(failure.isEmpty());
        save->setDefault(failure.isEmpty());
    }
}

} // namespace Pinloom
