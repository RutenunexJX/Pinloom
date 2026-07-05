#pragma once

#include "pinloom/clip/ClipSearch.h"
#include "pinloom/widgets/PinloomPanel.h"

#include <QWidget>
#include <functional>
#include <optional>

class QLabel;
class QEvent;
class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace Pinloom {

struct PinloomCommandPanelOptions {
    std::function<QList<ClipSearchResult>(const QString &query, const ClipSearchOptions &options)> clipSearchHandler;
    std::function<bool(const QString &clipId, QString *error)> clipInsertionHandler;
    std::function<std::optional<PinloomClipSaveRequest>(
        QWidget *parent,
        const ClipSearchResult &result)> clipSaveRequestProvider;
    std::function<bool(const PinloomClipSaveRequest &request, QString *error)> clipSaveHandler;
    std::function<bool(QString *status)> anchorCaptureHandler;
    std::function<void(const QString &query)> searchWindowHandler;
    std::function<void(const QString &status)> statusChangedHandler;
};

class PinloomCommandPanel final : public QWidget {
    Q_OBJECT

public:
    explicit PinloomCommandPanel(QWidget *parent = nullptr);
    explicit PinloomCommandPanel(PinloomCommandPanelOptions options, QWidget *parent = nullptr);

    void setCommandText(const QString &text);
    QString commandText() const;
    void openClipSearch(const QString &query = QString());
    void focusCommand();

    QString statusText() const;
    int resultCount() const;
    ClipSearchResult resultAt(int row) const;
    ClipSearchResult currentResult() const;
    bool selectResultAt(int row);
    bool selectFirstResult();
    bool selectNextResult();
    bool selectPreviousResult();
    bool activateCurrentCommandItem();

signals:
    void statusChanged(const QString &status);
    void clipInserted(const QString &clipId);
    void clipSaved(const QString &clipId);
    void anchorCaptureRequested();
    void searchWindowRequested(const QString &query);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void refreshResults();
    void activateResultItem(QListWidgetItem *item);

private:
    void updateStatus(const QString &status);
    bool activateCommandItem(QListWidgetItem *item);
    bool insertClipFromItem(const QListWidgetItem *item);
    bool saveClipFromItem(const QListWidgetItem *item);
    bool captureAnchor();
    bool openSearchWindow(const QListWidgetItem *item);
    std::optional<PinloomClipSaveRequest> promptClipSaveRequest(const ClipSearchResult &result);

    PinloomCommandPanelOptions options_;
    QLineEdit *commandEdit_ = nullptr;
    QListWidget *resultList_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QString statusText_;
};

void showCommandPanelForHotkey(QWidget &commandWindow, PinloomCommandPanel &panel);

} // namespace Pinloom
