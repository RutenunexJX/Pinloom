#pragma once

#include "pinloom/core/ManualPdfAnchorCreation.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

namespace Pinloom {

class ManualPdfAnchorDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ManualPdfAnchorDialog(QWidget *parent = nullptr);

    ManualPdfAnchorCreationRequest request() const;

private:
    void browsePdfFile();

    QLineEdit *nameEdit_ = nullptr;
    QLineEdit *fileEdit_ = nullptr;
    QSpinBox *pageSpin_ = nullptr;
    QDoubleSpinBox *leftSpin_ = nullptr;
    QDoubleSpinBox *topSpin_ = nullptr;
    QDoubleSpinBox *rightSpin_ = nullptr;
    QDoubleSpinBox *bottomSpin_ = nullptr;
    QDoubleSpinBox *zoomSpin_ = nullptr;
    QLineEdit *aliasesEdit_ = nullptr;
    QLineEdit *tagsEdit_ = nullptr;
    QCheckBox *pinnedCheck_ = nullptr;
};

} // namespace Pinloom
