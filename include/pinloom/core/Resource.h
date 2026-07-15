#pragma once

#include "pinloom/core/Anchor.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace Pinloom {

enum class ResourceKind {
    Unknown,
    File,
    Folder,
    Pdf,
    TextSnippet,
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
    QString content;
    bool deleted = false;
    QDateTime updatedAt;
};

} // namespace Pinloom
