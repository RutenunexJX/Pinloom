#pragma once

#include "pinloom/core/AnchorCaptureDraft.h"

#include <QDialog>

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;

namespace Pinloom {

class AnchorCaptureDialog final : public QDialog {
    Q_OBJECT

public:
    explicit AnchorCaptureDialog(const AnchorCaptureDraft &draft,
                                 QWidget *parent = nullptr);

    AnchorCaptureDraft draft() const;

private:
    void updateAcceptance();

    AnchorCaptureDraft initialDraft_;
    QLabel *targetLabel_ = nullptr;
    QLabel *locatorLabel_ = nullptr;
    QLabel *provenanceLabel_ = nullptr;
    QLineEdit *nameEdit_ = nullptr;
    QLineEdit *aliasesEdit_ = nullptr;
    QLineEdit *tagsEdit_ = nullptr;
    QCheckBox *pinnedCheck_ = nullptr;
    QCheckBox *mutationCheck_ = nullptr;
    QLabel *validationLabel_ = nullptr;
    QDialogButtonBox *buttons_ = nullptr;
};

} // namespace Pinloom
