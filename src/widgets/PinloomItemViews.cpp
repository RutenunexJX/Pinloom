#include "pinloom/widgets/PinloomItemViews.h"
#include "pinloom/widgets/PinloomUiControls.h"

namespace Pinloom::Ui {
ListItem::ListItem(const QString &text, List *owner) : QStandardItem(text)
{
    setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
    if (owner) owner->addItem(this);
}
List *ListItem::list() const { return model() ? qobject_cast<List *>(model()->parent()) : nullptr; }
void ListItem::setHidden(bool hidden) { if (auto *owner = list()) owner->setRowHidden(row(), hidden); }
bool ListItem::isHidden() const { auto *owner = list(); return owner && owner->isRowHidden(row()); }
void ListItem::setSelected(bool selected)
{
    if (auto *owner = list()) owner->selectionModel()->select(index(), selected
        ? QItemSelectionModel::Select : QItemSelectionModel::Deselect);
}
bool ListItem::isSelected() const
{
    auto *owner = list(); return owner && owner->selectionModel()->isSelected(index());
}

Table::Table(QWidget *parent) : Table(0, 0, parent) {}
Table::Table(int rows, int columns, QWidget *parent)
    : TableBase(parent), items_(new QStandardItemModel(rows, columns, this))
{
    initializeItemView(this);
    items_->setItemPrototype(new TableItem);
    setModel(items_);
    connect(items_, &QStandardItemModel::itemChanged, this, [this](QStandardItem *item) {
        if (auto *cell = dynamic_cast<TableItem *>(item)) emit itemChanged(cell);
    });
    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { emit itemSelectionChanged(); });
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this,
        [this](const QModelIndex &current, const QModelIndex &previous) {
            emit currentCellChanged(current.row(), current.column(), previous.row(), previous.column());
        });
    connect(this, &QTableView::clicked, this, [this](const QModelIndex &index) {
        emit cellClicked(index.row(), index.column());
        if (auto *cell = item(index.row(), index.column())) emit itemClicked(cell);
    });
    connect(this, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
        if (auto *cell = item(index.row(), index.column())) emit itemDoubleClicked(cell);
    });
}
int Table::rowCount() const { return items_->rowCount(); }
int Table::columnCount() const { return items_->columnCount(); }
void Table::setRowCount(int rows) { items_->setRowCount(rows); }
void Table::setColumnCount(int columns) { items_->setColumnCount(columns); }
void Table::insertRow(int row) { items_->insertRow(row); }
void Table::removeRow(int row) { items_->removeRow(row); }
TableItem *Table::item(int row, int column) const { return dynamic_cast<TableItem *>(items_->item(row, column)); }
void Table::setItem(int row, int column, TableItem *item) { items_->setItem(row, column, item); }
TableItem *Table::updateItem(int row, int column, const QVariant &displayValue)
{
    auto *cell = item(row, column);
    if (!cell) {
        cell = new TableItem;
        cell->setData(Qt::DisplayRole, displayValue);
        setItem(row, column, cell);
    } else {
        const QVariant previous = cell->data(Qt::DisplayRole);
        if (previous.metaType() != displayValue.metaType() || previous != displayValue) {
            cell->setData(Qt::DisplayRole, displayValue);
        }
    }
    return cell;
}
TableItem *Table::currentItem() const { return item(currentRow(), currentColumn()); }
int Table::currentRow() const { return currentIndex().row(); }
int Table::currentColumn() const { return currentIndex().column(); }
void Table::setCurrentCell(int row, int column) { setCurrentIndex(items_->index(row, column)); }
void Table::setCurrentCell(int row, int column, QItemSelectionModel::SelectionFlags flags)
{ selectionModel()->setCurrentIndex(items_->index(row, column), flags); }
void Table::setCurrentItem(TableItem *item) { setCurrentIndex(item ? item->index() : QModelIndex()); }
void Table::setHorizontalHeaderLabels(const QStringList &labels) { items_->setHorizontalHeaderLabels(labels); }
QStandardItem *Table::horizontalHeaderItem(int column) const { return items_->horizontalHeaderItem(column); }
QRect Table::visualItemRect(const TableItem *item) const { return item ? visualRect(item->index()) : QRect(); }
void Table::editItem(TableItem *item) { if (item) edit(item->index()); }
void Table::clearContents()
{
    const int rows = rowCount();
    items_->removeRows(0, rows);
    items_->setRowCount(rows);
}

List::List(QWidget *parent) : ListBase(parent), items_(new QStandardItemModel(this))
{
    initializeItemView(this);
    items_->setItemPrototype(new ListItem);
    setModel(items_);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(items_, &QStandardItemModel::itemChanged, this, [this](QStandardItem *item) {
        if (auto *entry = dynamic_cast<ListItem *>(item)) emit itemChanged(entry);
    });
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this,
        [this](const QModelIndex &current, const QModelIndex &previous) {
            emit currentItemChanged(item(current.row()), item(previous.row()));
        });
    connect(this, &QListView::activated, this, [this](const QModelIndex &index) {
        if (auto *entry = item(index.row())) emit itemActivated(entry);
    });
    connect(this, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
        if (auto *entry = item(index.row())) emit itemDoubleClicked(entry);
    });
}
int List::count() const { return items_->rowCount(); }
ListItem *List::item(int row) const { return dynamic_cast<ListItem *>(items_->item(row)); }
ListItem *List::currentItem() const { return item(currentRow()); }
int List::currentRow() const { return currentIndex().row(); }
int List::row(const ListItem *item) const { return item && item->model() == items_ ? item->row() : -1; }
void List::addItem(ListItem *item) { items_->appendRow(item); }
void List::addItem(const QString &text) { addItem(new ListItem(text)); }
void List::clear() { items_->clear(); }
void List::setCurrentRow(int row) { setCurrentIndex(items_->index(row, 0)); }
void List::setCurrentItem(ListItem *item) { setCurrentRow(row(item)); }
void List::scrollToItem(ListItem *item, ScrollHint hint) { if (item) scrollTo(item->index(), hint); }
QRect List::visualItemRect(const ListItem *item) const { return item ? visualRect(item->index()) : QRect(); }
QList<ListItem *> List::findItems(const QString &text, Qt::MatchFlags flags) const
{
    QList<ListItem *> matches;
    for (auto *item : items_->findItems(text, flags))
        if (auto *entry = dynamic_cast<ListItem *>(item)) matches.append(entry);
    return matches;
}
} // namespace Pinloom::Ui
