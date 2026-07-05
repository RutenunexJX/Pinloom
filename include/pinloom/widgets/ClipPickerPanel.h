#pragma once

#include "pinloom/clip/ClipSearch.h"

#include <QString>
#include <QWidget>
#include <functional>

class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace Pinloom {

class ClipInsertionService;

using ClipPickerInsertionHandler = std::function<bool(const QString &clipId, QString *error)>;

struct ClipPickerOptions {
    ClipSearchOptions searchOptions;
    ClipPickerInsertionHandler insertionHandler;
    std::function<void(const QList<ClipSearchResult> &results)> resultsChangedHandler;
    std::function<void(const ClipSearchResult &result)> currentResultChangedHandler;
    std::function<void(const QString &status)> statusChangedHandler;
    bool closeOnActivationSuccess = true;
};

ClipPickerInsertionHandler makeClipPickerInsertionHandler(ClipInsertionService &service);

class ClipPickerPanel final : public QWidget {
    Q_OBJECT

public:
    explicit ClipPickerPanel(ClipSearchService &searchService, QWidget *parent = nullptr);
    ClipPickerPanel(ClipSearchService &searchService, ClipPickerOptions options, QWidget *parent = nullptr);

    void setQuery(const QString &query);
    QString query() const;
    void focusSearch();
    void refreshResults();

    void setSearchOptions(const ClipSearchOptions &options);
    ClipSearchOptions searchOptions() const;
    void setInsertionHandler(ClipPickerInsertionHandler handler);
    void setCloseOnActivationSuccess(bool closeOnSuccess);
    bool closeOnActivationSuccess() const;

    ClipSearchResult currentResult() const;
    ClipSearchResult resultAt(int row) const;
    QList<ClipSearchResult> currentResults() const;
    int resultCount() const;
    bool selectResultAt(int row);
    bool selectFirstResult();
    bool selectNextResult();
    bool selectPreviousResult();
    bool activateCurrentResult();
    bool saveCurrentClipAsSaved(const QString &name,
                                const QStringList &aliases = {},
                                const QStringList &tags = {},
                                bool pinned = false);

    QString statusText() const;
    QString lastError() const;
    bool lastActivationSucceeded() const;

signals:
    void resultsChanged();
    void currentResultChanged();
    void activated(const QString &clipId);
    void activationFailed(const QString &clipId, const QString &error);
    void statusChanged(const QString &status);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void activateItem(QListWidgetItem *item);
    void promptSaveCurrentClip();
    void notifyCurrentResultChanged();
    void refreshSaveButtonState();

private:
    void updateStatus(const QString &status);
    QListWidgetItem *itemForClipId(const QString &clipId) const;

    ClipSearchService &searchService_;
    ClipPickerOptions options_;
    QLineEdit *searchEdit_ = nullptr;
    QPushButton *saveButton_ = nullptr;
    QListWidget *resultList_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QString statusText_;
    QString lastError_;
    bool lastActivationSucceeded_ = false;
};

} // namespace Pinloom
