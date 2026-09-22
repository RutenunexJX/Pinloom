#pragma once

#include <QListView>
#include <QStandardItemModel>
#include <QTableView>
#ifdef PINLOOM_ENABLE_ELA
#include "ElaListView.h"
#include "ElaTableView.h"
#endif

namespace Pinloom::Ui {
#ifdef PINLOOM_ENABLE_ELA
using ListBase = ElaListView;
using TableBase = ElaTableView;
#else
using ListBase = QListView;
using TableBase = QTableView;
#endif

class List;
class TableItem : public QStandardItem {
public:
    TableItem() = default;
    explicit TableItem(const QString &text) : QStandardItem(text) {}
    void setData(int role, const QVariant &value) { QStandardItem::setData(value, role); }
    QStandardItem *clone() const override { return new TableItem(*this); }
};

class ListItem : public QStandardItem {
public:
    explicit ListItem(const QString &text = {}, List *list = nullptr);
    void setData(int role, const QVariant &value) { QStandardItem::setData(value, role); }
    QStandardItem *clone() const override { return new ListItem(*this); }
    void setHidden(bool hidden);
    bool isHidden() const;
    void setSelected(bool selected);
    bool isSelected() const;
private:
    List *list() const;
};

// Standard models own items and stable role data; the renderer owns only the view.
class Table final : public TableBase {
    Q_OBJECT
public:
    explicit Table(QWidget *parent = nullptr);
    Table(int rows, int columns, QWidget *parent = nullptr);
    int rowCount() const;
    int columnCount() const;
    void setRowCount(int rows);
    void setColumnCount(int columns);
    void insertRow(int row);
    void removeRow(int row);
    TableItem *item(int row, int column) const;
    void setItem(int row, int column, TableItem *item);
    TableItem *currentItem() const;
    int currentRow() const;
    int currentColumn() const;
    void setCurrentCell(int row, int column);
    void setCurrentCell(int row, int column, QItemSelectionModel::SelectionFlags flags);
    void setCurrentItem(TableItem *item);
    void setHorizontalHeaderLabels(const QStringList &labels);
    QStandardItem *horizontalHeaderItem(int column) const;
    QRect visualItemRect(const TableItem *item) const;
    void editItem(TableItem *item);
    void clearContents();
signals:
    void itemSelectionChanged();
    void itemChanged(Pinloom::Ui::TableItem *item);
    void itemClicked(Pinloom::Ui::TableItem *item);
    void itemDoubleClicked(Pinloom::Ui::TableItem *item);
    void cellClicked(int row, int column);
    void currentCellChanged(int row, int column, int previousRow, int previousColumn);
private:
    QStandardItemModel *items_;
};

class List final : public ListBase {
    Q_OBJECT
public:
    explicit List(QWidget *parent = nullptr);
    int count() const;
    ListItem *item(int row) const;
    ListItem *currentItem() const;
    int currentRow() const;
    int row(const ListItem *item) const;
    void addItem(ListItem *item);
    void addItem(const QString &text);
    void clear();
    void setCurrentRow(int row);
    void setCurrentItem(ListItem *item);
    void scrollToItem(ListItem *item, ScrollHint hint = EnsureVisible);
    QRect visualItemRect(const ListItem *item) const;
    QList<ListItem *> findItems(const QString &text, Qt::MatchFlags flags) const;
signals:
    void itemChanged(Pinloom::Ui::ListItem *item);
    void currentItemChanged(Pinloom::Ui::ListItem *current, Pinloom::Ui::ListItem *previous);
    void itemActivated(Pinloom::Ui::ListItem *item);
    void itemDoubleClicked(Pinloom::Ui::ListItem *item);
private:
    QStandardItemModel *items_;
};
} // namespace Pinloom::Ui
