#pragma once

#include <QDialogButtonBox>
#include <QDialog>
#include <QMainWindow>
#include <QFormLayout>
#include <QHash>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QHeaderView>
#include <QTableView>
#include <QTreeView>
#include <memory>

class QApplication;
class QAbstractItemView;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QMenuBar;
class QLabel;
class QPlainTextEdit;
class QScrollArea;
class QSpinBox;
class QStatusBar;
class QToolButton;

namespace Pinloom {
enum class PinloomVisualScheme;
namespace Ui {
// Composition preserves Qt close/reject and resident hide-to-tray semantics.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
protected:
    bool nativeEvent(const QByteArray &type, void *message, qintptr *result) override;
private:
    QWidget *chrome_ = nullptr;
};
class Dialog : public QDialog {
    Q_OBJECT
public:
    explicit Dialog(QWidget *parent = nullptr, Qt::WindowFlags flags = {});
protected:
    bool nativeEvent(const QByteArray &type, void *message, qintptr *result) override;
private:
    QWidget *chrome_ = nullptr;
};
class FormLayout : public QFormLayout {
public:
    using QFormLayout::QFormLayout;
    using QFormLayout::addRow;
    void addRow(const QString &text, QWidget *field);
    void addRow(const QString &text, QLayout *field);
};
bool usesEla();
void applyElaTheme(QApplication &application, PinloomVisualScheme scheme);
void refreshViewPalettes(QApplication &application);
QString scopedStyleSheet(QString sheet);
QPushButton *pushButton(QWidget *parent = nullptr);
QPushButton *pushButton(const QString &text, QWidget *parent = nullptr);
QPushButton *pushButton(const QIcon &icon, const QString &text, QWidget *parent = nullptr);
QToolButton *toolButton(QWidget *parent = nullptr);
QLineEdit *lineEdit(QWidget *parent = nullptr);
QLineEdit *lineEdit(const QString &text, QWidget *parent = nullptr);
QComboBox *comboBox(QWidget *parent = nullptr);
QCheckBox *checkBox(QWidget *parent = nullptr);
QCheckBox *checkBox(const QString &text, QWidget *parent = nullptr);
QSpinBox *spinBox(QWidget *parent = nullptr);
QDoubleSpinBox *doubleSpinBox(QWidget *parent = nullptr);
QMenu *menu(QWidget *parent = nullptr);
QMenu *menu(const QString &title, QWidget *parent = nullptr);
QPlainTextEdit *plainTextEdit(QWidget *parent = nullptr);
QPlainTextEdit *plainTextEdit(const QString &text, QWidget *parent = nullptr);
QScrollArea *scrollArea(QWidget *parent = nullptr);
QMenuBar *menuBar(QWidget *parent = nullptr);
QStatusBar *statusBar(QWidget *parent = nullptr);
void initializeItemView(QAbstractItemView *view);
QTreeView *treeView(QWidget *parent = nullptr);
QLabel *label(QWidget *parent = nullptr);
QLabel *label(const QString &text, QWidget *parent = nullptr);
void installNavigation(QToolButton *button, QMenu *routes);
QWidget *showNotice(QWidget *host, const QString &text);
void setStatusText(QLabel *label, const QString &text, bool notify = true);
void installToolTips(QApplication &application);
QWidget *section(const QString &title, QWidget *parent = nullptr);
QWidget *collapsibleSection(QPushButton *toggle, QWidget *content, QWidget *parent = nullptr);
QFrame *popupFrame(QWidget *parent = nullptr);
// Keeps Qt's role ordering and accepted/rejected signals, but uses our factory
// for standard buttons instead of Qt's private QPushButton construction.
class DialogButtonBox final : public QDialogButtonBox {
    Q_OBJECT
public:
    explicit DialogButtonBox(StandardButtons buttons, QWidget *parent = nullptr);
    QPushButton *button(StandardButton standard) const;
    StandardButton standardButton(QAbstractButton *button) const;
    StandardButtons standardButtons() const;
    void setStandardButtons(StandardButtons buttons);
    QPushButton *addButton(StandardButton standard);
    void removeButton(QAbstractButton *button);
    void clear();
    QPushButton *addButton(const QString &text, ButtonRole role);
    using QDialogButtonBox::addButton;
private:
    QHash<StandardButton, QPointer<QPushButton>> standard_;
};

QString getText(QWidget *parent, const QString &title, const QString &label,
                QLineEdit::EchoMode mode = QLineEdit::Normal,
                const QString &text = {}, bool *ok = nullptr);
int getInt(QWidget *parent, const QString &title, const QString &label,
           int value = 0, int min = -2147483647, int max = 2147483647,
           int step = 1, bool *ok = nullptr);
} // namespace Ui
} // namespace Pinloom
