#pragma once

#include "pinloom/core/Anchor.h"
#include "pinloom/core/ResourceRelation.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace Pinloom {

enum class ResourceKind {
    Unknown,
    File,
    Folder,
    Pdf,
    Markdown,
    CodeSnippet,
    Url,
    Note,
    ManualAnchor
};

struct Resource {
    QString id;
    ResourceKind kind = ResourceKind::Unknown;
    QString title;
    QString location;
    QStringList tags;
    QStringList aliases;
    QList<Anchor> anchors;
    QList<ResourceRelation> relations;
    QString content;
    QDateTime updatedAt;
};

} // namespace Pinloom
