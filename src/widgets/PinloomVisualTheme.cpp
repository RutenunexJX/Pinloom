#include "pinloom/widgets/PinloomVisualTheme.h"

#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

namespace Pinloom {

PinloomVisualScheme systemPinloomVisualScheme()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
        ? PinloomVisualScheme::Dark
        : PinloomVisualScheme::Light;
}

QString pinloomVisualThemeStyleSheet(PinloomVisualScheme scheme)
{
    if (scheme == PinloomVisualScheme::Dark) {
        return QStringLiteral(R"QSS(
QMainWindow, QDialog { background: #111827; color: #E5E7EB; }
QLabel { color: #E5E7EB; }
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox, QDoubleSpinBox {
  min-height: 30px; background: #1F2937; color: #F9FAFB;
  border: 1px solid #475569; border-radius: 6px; padding: 2px 8px;
  selection-background-color: #0F766E; selection-color: #FFFFFF;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus,
QSpinBox:focus, QDoubleSpinBox:focus { border: 2px solid #60A5FA; }
QPushButton, QToolButton {
  min-height: 30px; background: #263244; color: #F8FAFC;
  border: 1px solid #526175; border-radius: 6px; padding: 0 12px;
}
QPushButton:hover, QToolButton:hover { background: #334155; border-color: #7C8CA3; }
QPushButton:pressed, QToolButton:pressed { background: #0F766E; }
QPushButton:disabled, QToolButton:disabled { color: #7C8798; background: #1B2432; border-color: #374151; }
QListView, QTreeView, QTableView, QListWidget, QTreeWidget, QTableWidget {
  background: #182231; alternate-background-color: #1D2939; color: #E5E7EB;
  border: 1px solid #3E4B5D; border-radius: 7px; outline: 0;
  selection-background-color: #0F766E; selection-color: #FFFFFF;
}
QHeaderView::section { background: #253247; color: #E5E7EB; border: 0; border-right: 1px solid #435168; padding: 7px; }
QGroupBox { border: 1px solid #3E4B5D; border-radius: 8px; margin-top: 12px; padding-top: 10px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #99F6E4; }
QStatusBar { background: #172033; color: #CBD5E1; border-top: 1px solid #334155; }
QToolTip { background: #0F172A; color: #F8FAFC; border: 1px solid #64748B; padding: 5px; }
QScrollBar:vertical { width: 10px; background: transparent; }
QScrollBar::handle:vertical { min-height: 28px; background: #526175; border-radius: 5px; }
)QSS");
    }

    return QStringLiteral(R"QSS(
QMainWindow, QDialog { background: #F4F7FB; color: #172033; }
QLabel { color: #263244; }
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox, QDoubleSpinBox {
  min-height: 30px; background: #FFFFFF; color: #172033;
  border: 1px solid #C7D0DD; border-radius: 6px; padding: 2px 8px;
  selection-background-color: #0F766E; selection-color: #FFFFFF;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus,
QSpinBox:focus, QDoubleSpinBox:focus { border: 2px solid #2563EB; }
QPushButton, QToolButton {
  min-height: 30px; background: #FFFFFF; color: #243044;
  border: 1px solid #BCC7D5; border-radius: 6px; padding: 0 12px;
}
QPushButton:hover, QToolButton:hover { background: #EAF1F8; border-color: #8EA0B6; }
QPushButton:pressed, QToolButton:pressed { background: #CCFBF1; border-color: #0F766E; }
QPushButton:disabled, QToolButton:disabled { color: #99A3B2; background: #EEF1F5; border-color: #D8DEE7; }
QListView, QTreeView, QTableView, QListWidget, QTreeWidget, QTableWidget {
  background: #FFFFFF; alternate-background-color: #F7F9FC; color: #202A39;
  border: 1px solid #CBD4E0; border-radius: 7px; outline: 0;
  selection-background-color: #0F766E; selection-color: #FFFFFF;
}
QHeaderView::section { background: #E8EEF5; color: #2C3B4F; border: 0; border-right: 1px solid #CBD4E0; padding: 7px; }
QGroupBox { border: 1px solid #CBD4E0; border-radius: 8px; margin-top: 12px; padding-top: 10px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #0F766E; }
QStatusBar { background: #EAF0F7; color: #4A5A70; border-top: 1px solid #CBD4E0; }
QToolTip { background: #172033; color: #FFFFFF; border: 1px solid #526175; padding: 5px; }
QScrollBar:vertical { width: 10px; background: transparent; }
QScrollBar::handle:vertical { min-height: 28px; background: #A9B5C5; border-radius: 5px; }
)QSS");
}

void applyPinloomVisualTheme(QApplication &application,
                             PinloomVisualScheme scheme)
{
    QPalette palette = application.palette();
    const bool dark = scheme == PinloomVisualScheme::Dark;
    palette.setColor(QPalette::Window,
                     QColor(dark ? QStringLiteral("#111827")
                                 : QStringLiteral("#F4F7FB")));
    palette.setColor(QPalette::WindowText,
                     QColor(dark ? QStringLiteral("#E5E7EB")
                                 : QStringLiteral("#172033")));
    palette.setColor(QPalette::Base,
                     QColor(dark ? QStringLiteral("#182231")
                                 : QStringLiteral("#FFFFFF")));
    palette.setColor(QPalette::AlternateBase,
                     QColor(dark ? QStringLiteral("#1D2939")
                                 : QStringLiteral("#F7F9FC")));
    palette.setColor(QPalette::Text,
                     QColor(dark ? QStringLiteral("#E5E7EB")
                                 : QStringLiteral("#202A39")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#0F766E")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#FFFFFF")));
    application.setPalette(palette);
    application.setStyleSheet(pinloomVisualThemeStyleSheet(scheme));
}

void applySystemPinloomVisualTheme(QApplication &application)
{
    applyPinloomVisualTheme(application, systemPinloomVisualScheme());
}

} // namespace Pinloom
