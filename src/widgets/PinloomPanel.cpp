#include "pinloom/widgets/PinloomPanel.h"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace Pinloom {

PinloomPanel::PinloomPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText(tr("Search resources, tags, aliases, anchors"));

    resultList_ = new QListWidget(this);
    statusLabel_ = new QLabel(this);

    layout->addWidget(searchEdit_);
    layout->addWidget(resultList_, 1);
    layout->addWidget(statusLabel_);

    seedDemoData();
    connect(searchEdit_, &QLineEdit::textChanged, this, &PinloomPanel::refreshResults);
    refreshResults();
}

void PinloomPanel::seedDemoData()
{
    Resource readme;
    readme.id = QStringLiteral("pinloom-readme");
    readme.kind = ResourceKind::Markdown;
    readme.title = QStringLiteral("Pinloom README");
    readme.location = QStringLiteral("readme.md");
    readme.tags = {QStringLiteral("pinloom"), QStringLiteral("docs")};
    readme.aliases = {QStringLiteral("project overview")};
    readme.anchors = {Anchor{AnchorType::MarkdownHeading, QStringLiteral("Project Bootstrap MVP")}};

    repository_.upsertResource(readme);
}

void PinloomPanel::refreshResults()
{
    resultList_->clear();

    const QList<SearchResult> results = repository_.search(SearchQuery{searchEdit_->text()});
    for (const SearchResult &result : results) {
        resultList_->addItem(QStringLiteral("%1  |  %2  |  %3")
                                 .arg(result.resource.title, result.resource.location, result.matchedField));
    }

    statusLabel_->setText(tr("%n result(s)", nullptr, results.size()));
}

} // namespace Pinloom
