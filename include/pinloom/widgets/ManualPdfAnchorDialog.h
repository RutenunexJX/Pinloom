#pragma once

#include "pinloom/core/ManualPdfAnchorCreation.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QWidget;

namespace Pinloom {

class ManualPdfAnchorDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ManualPdfAnchorDialog(QWidget *parent = nullptr);
    ManualPdfAnchorDialog(const ManualPdfAnchorCreationRequest &initialRequest, QWidget *parent = nullptr);

    ManualPdfAnchorCreationRequest request() const;

private:
    void browsePdfFile();
    void setRequest(const ManualPdfAnchorCreationRequest &request);
    void updateSummary();

    QLineEdit *nameEdit_ = nullptr;
    QLabel *summaryLabel_ = nullptr;
    QPushButton *advancedToggleButton_ = nullptr;
    QWidget *advancedWidget_ = nullptr;
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
    QString source_ = QStringLiteral("manual");
    QString targetApp_ = QStringLiteral("SumatraPDF");
    QString unit_ = QStringLiteral("pt");
    QString locatorType_;
    QString searchText_;
    QString contextBefore_;
    QString contextAfter_;
    int occurrence_ = -1;
    PdfCaptureRect fallbackRect_;
};

} // namespace Pinloom
