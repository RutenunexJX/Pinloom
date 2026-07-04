#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QRectF>

namespace Pinloom {

enum class AnchorType {
    None,
    FileLine,
    MarkdownHeading,
    MarkdownBlock,
    Marker,
    PdfPage,
    PdfRegion,
    UrlFragment,
    Manual,
    TextHeading,
    TextBlock
};

struct Anchor {
    AnchorType type = AnchorType::None;
    QString target;
    int line = -1;
    int page = -1;
    QRectF region;
    QString id;
    QString name;
    QString targetApp;
    QString targetFile;
    QString targetUri;
    QString locatorType;
    QString locatorJson;
    QStringList aliases;
    QStringList tags;
    bool pinned = false;
    QDateTime createdAt;
    QDateTime updatedAt;
    QDateTime usedAt;
};

} // namespace Pinloom
