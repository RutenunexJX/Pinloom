#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace Pinloom {

struct Anchor {
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
    bool deleted = false;
    QDateTime createdAt;
    QDateTime updatedAt;
    QDateTime usedAt;
};

} // namespace Pinloom
