#pragma once

#include <QString>
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
};

} // namespace Pinloom
